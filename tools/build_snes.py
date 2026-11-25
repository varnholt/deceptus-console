"""Build a SNES ROM using native PVSnesLib tools; make/MSYS are not required."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import shutil
import struct
import re

ROOT = Path(__file__).resolve().parents[1]


def exhirom_boot_mirror(payload):
    """Mirror bank-zero startup into the bank selected by the reset vectors."""
    if len(payload) != 8 * 1024 * 1024:
        raise ValueError("ExHiROM requires an exact eight MiB image")
    payload = bytearray(payload)
    payload[0x400000:0x40FFB0] = payload[:0xFFB0]
    for offset in (0xFFDC, 0x40FFDC):
        struct.pack_into("<HH", payload, offset, 65535, 0)
    checksum = sum(payload) & 65535
    for offset in (0xFFDC, 0x40FFDC):
        struct.pack_into("<HH", payload, offset, checksum ^ 65535, checksum)
    return payload


# SA-1 cartridge: the S-CPU and the SA-1 see four 1 MiB ROM blocks at C0-FF, selected by $2220-$2223.
# block 0 holds code and tables, blocks 1-2 the shared resources (D0-EF), and the rooms share the
# F0-FF window: each room lives inside one of blocks 3-7, switched in when the room loads.
SA1_BLOCK_BANKS = 16
SA1_SHARED_BLOCKS = (1, 2)
SA1_ROOM_BLOCKS = (3, 4, 5, 6, 7)
SA1_ROOM_WINDOW_BANK = 0xF0
SA1_MAP_MODE = 0x23
SA1_CARTRIDGE_TYPE = 0x34      # ROM, SA-1, BW-RAM
SA1_BWRAM_SIZE = 0x07          # 2^7 KiB
# the HiROM libraries run startup at 00:8000-FFFF and FastROM code at 80-81:8000-FFFF (the upper
# halves of ROM banks 0 and 1). the SA-1 shows blocks 0 and 2 there, LoROM style, so those upper
# halves are copied where it looks: (source, destination) ROM offsets of 32 KiB each.
SA1_BOOT_COPIES = ((0x008000, 0x000000), (0x008000, 0x200000), (0x018000, 0x208000))
SA1_FASTROM_COPY_BANK = 0x20


def sa1_boot_mirror(payload):
    """Mark the header SA-1, copy startup and FastROM library code where the SA-1 maps it, fix the checksum."""
    if len(payload) != 8 * 1024 * 1024:
        raise ValueError("the SA-1 cartridge requires an exact eight MiB image")
    payload = bytearray(payload)
    payload[0xFFD5] = SA1_MAP_MODE
    payload[0xFFD6] = SA1_CARTRIDGE_TYPE
    payload[0xFFD8] = SA1_BWRAM_SIZE
    struct.pack_into("<HH", payload, 0xFFDC, 65535, 0)
    for source, destination in SA1_BOOT_COPIES:
        payload[destination:destination + 0x8000] = payload[source:source + 0x8000]
    # the copy at 7fc0 is the cartridge header; a second valid one at ffc0 makes emulators pick HiROM.
    payload[0xFFC0:0xFFE0] = bytes(32)
    checksum = sum(payload) & 65535
    struct.pack_into("<HH", payload, 0x7FDC, checksum ^ 65535, checksum)
    return payload


def sa1_binary_assembly(assembly):
    """Place shared blobs in blocks 1-2 and each room's blobs inside one room block; return the room blocks."""
    lines = assembly.splitlines()
    blobs = []
    for index, line in enumerate(lines):
        if not line.startswith('.section '):
            continue
        match = re.fullmatch(r'\.incbin "([^"]+)"', lines[index + 2])
        if match is None:
            raise ValueError('unexpected SNES blob assembly shape')
        room = re.search(r'snes_area(\d+)_', line)
        blobs.append((index, Path(match[1]).stat().st_size, int(room[1]) if room else None))

    def bank_filler(blocks):
        return {block * SA1_BLOCK_BANKS + bank: 0 for block in blocks for bank in range(SA1_BLOCK_BANKS)}

    def place(index, size, used, window_bank):
        bank = next((bank for bank, offset in used.items() if offset + size <= 65536), None)
        if bank is None:
            return False
        offset = used[bank]
        used[bank] += size
        base = window_bank(bank) - bank
        lines[index] = f'.base ${base:02x}\n' + lines[index].replace('superfree', f'bank {bank} slot 0 org {offset} force')
        return True

    shared = bank_filler(SA1_SHARED_BLOCKS)
    shared[SA1_FASTROM_COPY_BANK] = 65536
    for index, size, room in sorted(blobs, key=lambda blob: -blob[1]):
        if room is None and not place(index, size, shared, lambda bank: 0xC0 + bank):
            raise ValueError('SA-1 shared resource blocks exhausted')
    rooms = {}
    for index, size, room in blobs:
        if room is not None:
            rooms.setdefault(room, []).append((index, size))
    room_blocks = {}
    free = {block: bank_filler((block,)) for block in SA1_ROOM_BLOCKS}
    for room in sorted(rooms, key=lambda room: -sum(size for _, size in rooms[room])):
        for block in SA1_ROOM_BLOCKS:
            trial = dict(free[block])
            window = lambda bank: SA1_ROOM_WINDOW_BANK + bank % SA1_BLOCK_BANKS
            saved = list(lines)
            if all(place(index, size, trial, window) for index, size in sorted(rooms[room], key=lambda blob: -blob[1])):
                free[block] = trial
                room_blocks[room] = block
                break
            lines[:] = saved
        else:
            raise ValueError(f'SA-1 room blocks cannot hold room {room}')
    return '\n'.join(lines) + '\n', room_blocks


def sa1_layout_assembly(room_blocks, rooms):
    """The ROM block of each room's resources, and the ROM ranges the boot copies overwrite."""
    values = ','.join(str(room_blocks.get(room, SA1_ROOM_BLOCKS[0])) for room in range(rooms))
    return ('.include "hdr.asm"\n.BASE $C0\n.SECTION "snes_stage_rom_blocks" SEMISUPERFREE BANKS 2-15\n'
            f'snes_stage_rom_blocks:\n.db {values}\n.ENDS\n'
            # the SA-1 maps ROM 0000-7FFF at 00:8000-FFFF; the boot mirror fills it with bank zero's upper half.
            # wla writes one header byte at 7fd5, which the mirror overwrites too.
            '.BASE $00\n.SECTION "sa1_boot_mirror" BANK 0 SLOT 0 ORG 0 FORCE\n.dsb 16384, $ff\n.dsb 16341, $ff\n.ENDS\n'
            '.SECTION "sa1_boot_mirror_end" BANK 0 SLOT 0 ORG $7FD6 FORCE\n.dsb 42, $ff\n.ENDS\n'
            f'.SECTION "sa1_boot_mirror_fast" BANK {SA1_FASTROM_COPY_BANK} SLOT 0 ORG 0 FORCE\n'
            '.dsb 32768, $ff\n.dsb 32768, $ff\n.ENDS\n')


# SA-1 objects run on the SA-1: their globals live in BW-RAM, which the SA-1 addresses through its
# data bank $40; the S-CPU keeps work RAM and its multiplier/divider, which the SA-1 cannot reach.
SA1_HOST_SOURCES = ("main.c", "background_commit.c", "sprites_commit.c")
SA1_BSS = '.RAMSECTION ".bss" BANK $40 SLOT 2'
SA1_RUNTIME = {"tcc__mul": "snesSa1Mul", "tcc__mull": "snesSa1Mull", "tcc__udiv": "snesSa1Udiv",
               "tcc__div": "snesSa1Div", "snesMulS16": "snesSa1MulS16", "snesSquareU16": "snesSa1SquareU16",
               "snesDivS32U16": "snesSa1DivS32U16"}
SA1_FORBIDDEN = ("tcc__hwlock",)
BSS_SECTION = re.compile(r'\.RAMSECTION "\.bss" BANK \$(?:7e|40) SLOT 2\n(.*?)\.ENDS', re.S)


# a static or file-scope object with an initializer, up to the "=": "static T name[N]".
STATIC_INITIALIZED = re.compile(r'\bstatic\s+([^;{}()=]*?)=[^;]*;')


def reject_writable_initialized_statics(paths):
    """SA-1 objects keep initialized data in ROM, so an initialized static must be const."""
    findings = []
    for path in paths:
        text = COMMENT_OR_STRING.sub(lambda match: re.sub(r"[^\n]", " ", match[0]), path.read_text())
        for match in STATIC_INITIALIZED.finditer(text):
            declaration = match[1]
            pointer = declaration.rfind('*')
            if re.search(r'\bconst\b', declaration[pointer + 1:]):
                continue
            findings.append(f"{path.relative_to(ROOT)}:{text.count(chr(10), 0, match.start()) + 1}")
    if findings:
        raise SystemExit("SA-1 objects keep initialized data in ROM; these initialized statics are writable:\n  " +
                         "\n  ".join(findings))


def bss_globals(assembly):
    """The global labels an object allocates in its uninitialized-data section."""
    section = BSS_SECTION.search(assembly)
    if section is None:
        return set()
    return {match[1] for match in re.finditer(r'^(\w+) dsb \d+', section[1], re.M)}


def sa1_object_assembly(assembly, name):
    """Retarget an object for the SA-1: constant data in ROM, globals in BW-RAM, SA-1 arithmetic."""
    assembly = readonly_export_assembly(assembly, name)
    assembly = assembly.replace('.RAMSECTION ".bss" BANK $7e SLOT 2', SA1_BSS)
    for routine, replacement in SA1_RUNTIME.items():
        assembly = re.sub(r'\b' + routine + r'\b', replacement, assembly)
    used = [word for word in SA1_FORBIDDEN if re.search(r'\b' + word + r'\b', assembly)]
    work_ram = re.search(r'APPENDTO "globram.data"\s*\n(?!\s*\.ENDS)', assembly)
    if used or ('BANK $7e' in assembly) or work_ram:
        raise SystemExit(f"{name}: SA-1 code reaches S-CPU work RAM or its arithmetic: {used or 'work RAM'}")
    return assembly


def check_sa1_boundary(host_assemblies, sa1_assemblies):
    """Each cpu may use the other's globals only as addresses or by 24-bit long access, never by a
    16-bit absolute access, which goes through the data bank of the cpu that executes it."""
    host_globals = set().union(*(bss_globals(text) for text in host_assemblies.values()))
    sa1_globals = set().union(*(bss_globals(text) for text in sa1_assemblies.values()))
    findings = []
    for name, text in sa1_assemblies.items():
        findings += [f"{name}: SA-1 code uses S-CPU work RAM {label}" for label in host_globals
                     if re.search(r'\b' + label + r'\b', text)]
    for name, text in host_assemblies.items():
        for label in sa1_globals:
            for line in re.findall(r'^.*\b' + label + r'\b.*$', text, re.M):
                # an immediate, a pushed address, a long access, or an entry of a data table (.dw, .dl).
                address_or_long = re.search(r'#:?' + label + r'\b|^\s*pea\.w\s|\.l\s|^\s*(?:\S+:\s*)?\.d[wl]\s', line)
                if not address_or_long and not line.lstrip().startswith(label):
                    findings.append(f"{name}: S-CPU code accesses BW-RAM {label} directly: {line.strip()}")
    if findings:
        raise SystemExit("SA-1 boundary violated:\n  " + "\n  ".join(sorted(set(findings))))


# the sdk start-up clears work RAM from SECTIONSTART_.bss to SECTIONEND_.bss, which name only one
# of the many ".bss" sections (the libraries' among them). the game's work RAM globals go into one
# section, game_bss, owned by main; snesClearGameRam (game_ram.asm) zeroes it first thing in main.
BSS_OWNER = "main"
WORK_RAM_BSS = '.RAMSECTION ".bss" BANK $7e SLOT 2'
OWNED_BSS = '.RAMSECTION "game_bss" BANK $7e SLOT 2'
APPENDED_BSS = '.RAMSECTION "game_bss_{WLA_FILENAME}" APPENDTO "game_bss"'


def banked_hirom_assembly(assembly, exhirom=False):
    """Keep far ROM data bankable; retain zero-based work RAM labels."""
    lines = []
    for line in assembly.splitlines():
        if line.startswith('.RAMSECTION '):
            lines.append('.BASE $00')
        elif line.startswith('.SECTION '):
            lines.append('.BASE $C0')
            line = line.replace('SEMIFREE ORG $8000', 'SUPERFREE')
            if exhirom:
                line = line.replace('SUPERFREE', 'SEMISUPERFREE BANKS 2-15')
        lines.append(line)
    return '\n'.join(lines) + '\n'


def readonly_export_assembly(assembly, name):
    """Place an object's immutable initialized data in ROM, with real far labels."""
    ram = re.search(r'\.RAMSECTION "ram\{WLA_FILENAME\}\.data" APPENDTO "globram.data"\n(.*?)\.ENDS', assembly, re.S)
    if ram is None:
        raise ValueError('missing generated initialized-data RAM section')
    labels = []
    size = 0
    for line in ram[1].splitlines():
        if not line.strip():
            continue
        allocation = re.fullmatch(r'(\S+) dsb (\d+)', line.strip())
        if allocation is None:
            raise ValueError('unexpected generated RAM allocation: ' + line)
        labels.append((size, allocation[1]))
        size += int(allocation[2])
    if not labels:
        return assembly
    data = re.search(r'\.SECTION "\{WLA_FILENAME\}\.data" APPENDTO "glob.data"\n(.*?)\.ENDS', assembly, re.S)
    if data is None:
        raise ValueError('missing generated initialized-data ROM bytes')
    output, offset, next_label = [], 0, 0
    for line in data[1].splitlines():
        if not line.strip():
            continue
        directive = re.fullmatch(r'\.(db|dw) (.+)', line.strip())
        if directive is None:
            raise ValueError('unexpected generated data directive: ' + line)
        width = 1 if directive[1] == 'db' else 2
        operands = []
        for operand in directive[2].split(','):
            while next_label < len(labels) and labels[next_label][0] == offset:
                if operands:
                    output.append('.' + directive[1] + ' ' + ','.join(operands))
                    operands = []
                output.append(labels[next_label][1] + ':')
                next_label += 1
            operands.append(operand)
            offset += width
        if operands:
            output.append('.' + directive[1] + ' ' + ','.join(operands))
    if offset != size or next_label != len(labels):
        raise ValueError(f'generated ROM table layout differs from RAM labels: {offset} != {size}')
    replacement = '.SECTION "readonly_' + name + '" SUPERFREE\n' + '\n'.join(output) + '\n.ENDS'
    assembly = assembly[:data.start()] + replacement + assembly[data.end():]
    return assembly[:ram.start()] + assembly[ram.end():]


def exhirom_binary_assembly(assembly):
    """Pack blobs into directly mapped banks and encode their actual CPU banks."""
    used = {bank: 0 for bank in list(range(16, 64)) + list(range(65, 126))}
    lines = assembly.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith('.section '):
            continue
        match = re.fullmatch(r'\.incbin "([^"]+)"', lines[index + 2])
        if match is None:
            raise ValueError('unexpected SNES blob assembly shape')
        size = Path(match[1]).stat().st_size
        bank = next((bank for bank, offset in used.items() if offset + size <= 65536), None)
        if bank is None:
            raise ValueError('ExHiROM directly mapped resource banks exhausted')
        offset = used[bank]
        used[bank] += size
        base = 192 if bank < 64 else 0
        lines[index] = f'.base ${base:02x}\n' + line.replace('superfree', f'bank {bank} slot 0 org {offset} force')
    return '\n'.join(lines) + '\n'


# 816-tcc inverts the left operand of `a || !b`: when a is true, the code skips what should run.
# write such tests as `(a) || (b == 0)`; parentheses alone do not help.
OR_NOT = re.compile(r"\|\|\s*\(*\s*!(?!=)")
COMMENT_OR_STRING = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)


def reject_or_not_conditions(paths):
    """Stop the build on any `|| !` in code 816-tcc compiles."""
    findings = []
    for path in paths:
        # blank comments and literals but keep their line breaks, so line numbers stay right.
        text = COMMENT_OR_STRING.sub(lambda match: re.sub(r"[^\n]", " ", match[0]), path.read_text())
        for match in OR_NOT.finditer(text):
            findings.append(f"{path.relative_to(ROOT)}:{text.count(chr(10), 0, match.start()) + 1}")
    if findings:
        raise SystemExit("816-tcc miscompiles `a || !b`; write `(a) || (b == 0)` instead:\n  " + "\n  ".join(findings))


def run(command, folder):
    print(" ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=folder, check=True)


def build(sdk, data, full_game=False, scene="tutorial", rooms=26, reuse_assets=False, rom_mib=8, assets_dir=None,
          output_dir=None, cartridge="exhirom"):
    sa1 = cartridge == "sa1"
    if sa1 and (scene != "tutorial" or rom_mib != 8):
        raise SystemExit("the SA-1 cartridge holds the eight MiB tutorial scene only")
    folder = Path(output_dir).resolve() if output_dir is not None else ROOT / "build/snes"
    folder.mkdir(parents=True, exist_ok=True)
    if full_game:
        result = subprocess.run([sys.executable, str(ROOT / "tools/content.py"), "--data", str(data), "--strict"], cwd=ROOT)
        if result.returncode:
            raise SystemExit("Full-game build is incomplete. See build/content/summary.json; the existing cartridge is unchanged.")
    opening = scene == "tutorial"
    generated = ROOT / "assets/generated/snes" if opening else ROOT / "assets/generated"
    if assets_dir is not None:
        generated = Path(assets_dir).resolve()
        if not opening:
            raise SystemExit("--assets-dir is available for the tutorial scene only")
    metadata = None
    if opening:
        converter = ROOT / "tools/snes_tutorial_assets.py"
        digest = hashlib.sha256(converter.read_bytes()).hexdigest()
        if not reuse_assets:
            run([sys.executable, converter, "--data", data, "--rooms", rooms, "--output", generated], ROOT)
            metadata = json.loads((generated / "build.json").read_text())
            metadata["snes_converter_sha256"] = digest
            (generated / "build.json").write_text(json.dumps(metadata, indent=2) + "\n")
        else:
            from campaign_assets import asset_identity
            metadata = json.loads((generated / "build.json").read_text())
            if (metadata.get("source_identity") != asset_identity(data) or
                    metadata.get("snes_converter_sha256") != digest or
                    metadata.get("graphics_rooms") != rooms or
                    not all((generated / name).is_file() for name in metadata["c_sources"])):
                raise SystemExit("SNES cached assets do not match current sources, converter, or room selection; rebuild without --reuse-assets.")
        if metadata["binary_bytes"] > rom_mib * 1024 * 1024 - 262144:
            raise SystemExit("SNES resources exceed the selected ROM capacity; select --rom-mib 8.")
    else:
        run([sys.executable, ROOT / "tools/assets.py", "--import-data", data], ROOT)
    suffix = ".exe" if os.name == "nt" else ""
    compiler = sdk / "devkitsnes/bin" / ("816-tcc" + suffix)
    assembler = sdk / "devkitsnes/bin" / ("wla-65816" + suffix)
    linker = sdk / "devkitsnes/bin" / ("wlalink" + suffix)
    optimizer = sdk / "devkitsnes/tools" / ("816-opt" + suffix)
    if not compiler.is_file():
        raise SystemExit("PVSnesLib not found. Run python tools/bootstrap.py, or pass --sdk PATH.")
    header = (sdk / "devkitsnes/include/hdr.asm.in").read_text()
    replacements = {"HIROMDEF": ".DEFINE HIROM 1" if opening else "", "FASTROMDEF": ".DEFINE FASTROM 1", "ROMTITLE": "DECEPTUS CATACOMBS",
                    "CARTRIDGETYPE": "00", "ROMSIZE": ("0D" if rom_mib == 8 else "0C") if opening else "08", "SRAMSIZE": f"{SA1_BWRAM_SIZE:02X}" if sa1 else "00", "COUNTRY": "01",
                    "LICENSEECODE": "00", "VERSION": "00", "ROMBANKS": str(rom_mib * 16) if opening else "8",
                    "ROMBANKSIZE": "10000" if opening else "8000", "ROMMODE": ("EXHIROM" if (rom_mib == 8) and not sa1 else "HIROM") if opening else "LOROM", "ROMSPEED": "FASTROM"}
    for key, value in replacements.items():
        header = header.replace(f"@{key}@", value)
    (folder / "hdr.asm").write_text(header)
    objects = []
    sources = [ROOT / "src/engine/player/playerwater.c", ROOT / "src/engine/player/playerrope.c", ROOT / "src/engine/mechanisms/grabrope.c", ROOT / "src/game/adventure.c", ROOT / "src/game/weapons/playersword.c", ROOT / "src/game/player/playeranimation.c", ROOT / "src/game/items/itemheadtorch.c", ROOT / "src/game/appearance.c", ROOT / "src/engine/animation.c", ROOT / "src/engine/world.c", ROOT / "src/engine/entities.c", ROOT / "src/engine/events.c", ROOT / "src/engine/mechanisms.c",
               ROOT / "src/engine/logic.c", ROOT / "src/engine/pacing.c", ROOT / "src/engine/arith.c", ROOT / "src/game/enemies.c", ROOT / "src/game/deceptus.c",
               ROOT / "src/platform/snes/main.c"]
    if opening:
        sources += [ROOT / "src/game/campaign.c", ROOT / "src/platform/snes/background.c",
                    ROOT / "src/platform/snes/background_commit.c", ROOT / "src/platform/snes/sprites.c",
                    ROOT / "src/platform/snes/sprites_commit.c"]
        sources += [generated / name for name in metadata["c_sources"]]
        if sa1:
            sources.append(ROOT / "src/platform/snes/sa1_main.c")
    else:
        sources += [ROOT / "assets/generated/room.c"]
    # one tick per presentation, for the tick-exact route verifiers (include/deceptus/pacing.h).
    pacing_flags = ["-DDECEPTUS_PACING_LOCKED"] if os.environ.get("DECEPTUS_PACING_LOCKED") == "1" else []
    headers = [path for folder_name in ("include", "src/platform/snes") for path in (ROOT / folder_name).rglob("*.h")]
    reject_or_not_conditions([source for source in sources if source.parent != generated] + headers)
    if sa1:
        reject_writable_initialized_statics([source for source in sources
                                             if (source.parent != generated) and (source.name not in SA1_HOST_SOURCES)])
    host_assemblies, sa1_assemblies = {}, {}
    for source in sources:
        stem = source.stem
        run([compiler, *(["-H", "-DDECEPTUS_SNES_OPENING=1", "-DSNES_SPRITES_ASM=1", "-DSNES_BACKGROUND_ASM=1", "-DPLATFORM_ARITH=1", *(["-DSNES_SA1=1"] if sa1 else []), "-I" + str(ROOT / "src/platform/snes")] if opening else []), *pacing_flags, "-I" + str(ROOT / "include"), "-I" + str(generated), "-I" + str(sdk / "pvsneslib/include"),
             "-I" + str(sdk / "devkitsnes/include"), "-I" + str(folder), "-Wall", "-c", source,
             "-F", "-o", stem + ".ps"], folder)
        run([optimizer, "-i", stem + ".ps", "-o", stem + ".asm"], folder)
        if opening:
            assembly_path = folder / (stem + ".asm")
            assembly = assembly_path.read_text()
            if sa1 and source.name not in SA1_HOST_SOURCES:
                assembly = sa1_object_assembly(assembly, stem)
                sa1_assemblies[stem] = assembly
            elif source.parent == generated:
                assembly = readonly_export_assembly(assembly, stem)
            elif sa1:
                host_assemblies[stem] = assembly
            assembly = assembly.replace(WORK_RAM_BSS, OWNED_BSS if stem == BSS_OWNER else APPENDED_BSS)
            assembly_path.write_text(banked_hirom_assembly(assembly, rom_mib == 8))
        run([assembler, "-d", "-s", "-x", "-o", stem + ".obj", stem + ".asm"], folder)
        objects.append(stem + ".obj")
    if sa1:
        check_sa1_boundary(host_assemblies, sa1_assemblies)
        for name in ("sa1_boot", "sa1_runtime"):
            run([assembler, "-d", "-s", "-x", "-o", name + ".obj", ROOT / f"src/platform/snes/{name}.asm"], folder)
            objects.append(name + ".obj")
    data_assembly = (generated / "data.asm").read_text()
    if sa1:
        data_assembly, room_blocks = sa1_binary_assembly(data_assembly)
        (folder / "sa1_layout.asm").write_text(sa1_layout_assembly(room_blocks, rooms))
        run([assembler, "-d", "-s", "-x", "-o", "sa1_layout.obj", "sa1_layout.asm"], folder)
        objects.append("sa1_layout.obj")
    elif opening and rom_mib == 8:
        data_assembly = exhirom_binary_assembly(data_assembly)
    elif not opening:
        data_assembly = re.sub(r'(\.incbin ")\.\./\.\./assets/generated/',
                               lambda match: match[1] + generated.as_posix() + '/', data_assembly)
    (folder / "data.asm").write_text(data_assembly)
    run([assembler, "-d", "-s", "-x", "-o", "data.obj", "data.asm"], folder)
    objects.append("data.obj")
    if opening:
        run([assembler, *(["-DSNES_SA1"] if sa1 else []), "-d", "-s", "-x", "-o", "background_ram.obj",
             ROOT / "src/platform/snes/background_ram.asm"], folder)
        objects.append("background_ram.obj")
        run([assembler, "-d", "-s", "-x", "-o", "vram_dma.obj", ROOT / "src/platform/snes/vram_dma.asm"], folder)
        objects.append("vram_dma.obj")
        run([assembler, "-d", "-s", "-x", "-o", "game_ram.obj", ROOT / "src/platform/snes/game_ram.asm"], folder)
        objects.append("game_ram.obj")
        run([assembler, "-d", "-s", "-x", "-o", "background_hash.obj", ROOT / "src/platform/snes/background_hash.asm"], folder)
        objects.append("background_hash.obj")
        run([assembler, "-d", "-s", "-x", "-o", "background_cell.obj", ROOT / "src/platform/snes/background_cell.asm"], folder)
        objects.append("background_cell.obj")
        run([assembler, "-d", "-s", "-x", "-o", "engine_math.obj", ROOT / "src/platform/snes/engine_math.asm"], folder)
        objects.append("engine_math.obj")
        run([assembler, "-d", "-s", "-x", "-o", "sprites_begin.obj", ROOT / "src/platform/snes/sprites_begin.asm"], folder)
        objects.append("sprites_begin.obj")
        run([assembler, "-d", "-s", "-x", "-o", "sprites_draw.obj", ROOT / "src/platform/snes/sprites_draw.asm"], folder)
        objects.append("sprites_draw.obj")
        if (rom_mib == 8) and not sa1:
            # these physical banks map reset vectors and cpu work ram; no assets
            # may occupy their reserved ranges before the boot mirror is copied.
            reservations = ['.include "hdr.asm"']
            for bank, size in ((64, 0xFFB0), (126, 65536), (127, 65536)):
                reservations += [f'.section "exhirom_reserved_{bank}" bank {bank} slot 0 org 0 force',
                                 f'.dsb {size // 2}, $ff', f'.dsb {size - size // 2}, $ff', '.ends']
            (folder / "exhirom_reserved.asm").write_text("\n".join(reservations) + "\n")
            run([assembler, "-d", "-s", "-x", "-o", "exhirom_reserved.obj", "exhirom_reserved.asm"], folder)
            objects.append("exhirom_reserved.obj")
    library = sdk / ("pvsneslib/lib/HiROM_FastROM" if opening else "pvsneslib/lib/LoROM_FastROM")
    objects += [path.as_posix() for path in sorted(library.glob("*.obj"))]
    (folder / "linkfile").write_text("[objects]\n" + "\n".join(objects) + "\n")
    run([linker, "-d", "-s", "-A", "-c", "-L", library, "linkfile", "deceptus.sfc"], folder)
    if sa1:
        (folder / "deceptus.sfc").write_bytes(sa1_boot_mirror((folder / "deceptus.sfc").read_bytes()))
    elif opening and rom_mib == 8:
        (folder / "deceptus.sfc").write_bytes(exhirom_boot_mirror((folder / "deceptus.sfc").read_bytes()))
    shutil.copyfile(folder / "deceptus.sfc", folder / ("deceptus-" + scene + ".sfc"))
    (folder / "build.json").write_text(json.dumps(dict(scene=scene, graphics_rooms=rooms if opening else 1,
        rom_mib=rom_mib if opening else 0.25, mapping=("SA-1" if sa1 else "ExHiROM" if rom_mib == 8 else "HiROM") if opening else "LoROM",
        full_game=False, sha256=hashlib.sha256((folder / "deceptus.sfc").read_bytes()).hexdigest()), indent=2) + "\n")
    print(f"ROM: {folder / 'deceptus.sfc'}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=Path(os.environ.get("PVSNESLIB_HOME", ROOT / ".tools/pvsneslib")))
    parser.add_argument("--data", type=Path, default=ROOT / 'assets/game/data')
    parser.add_argument("--full-game", action="store_true", help="require complete content support instead of building the prototype")
    parser.add_argument("--scene", choices=("tutorial", "prototype"), default="tutorial")
    parser.add_argument("--rooms", type=int, default=26)
    parser.add_argument("--reuse-assets", action="store_true")
    parser.add_argument("--rom-mib", type=int, choices=(4, 8), default=8)
    parser.add_argument("--assets-dir", type=Path, help="isolated tutorial conversion directory")
    parser.add_argument("--cartridge", choices=("exhirom", "sa1"), default="exhirom",
                        help="sa1: SA-1 cartridge with switched 1 MiB room blocks")
    parser.add_argument("--output-dir", type=Path, help="isolated SDK object and ROM directory")
    arguments = parser.parse_args()
    build(arguments.sdk.resolve(), arguments.data.resolve(), arguments.full_game, arguments.scene,
          arguments.rooms, arguments.reuse_assets, arguments.rom_mib, arguments.assets_dir, arguments.output_dir,
          arguments.cartridge)
