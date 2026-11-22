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
    """Place generated immutable exported tables in ROM, with real far labels."""
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


def run(command, folder):
    print(" ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=folder, check=True)


def build(sdk, data, full_game=False, scene="tutorial", rooms=26, reuse_assets=False, rom_mib=8, assets_dir=None,
          output_dir=None):
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
                    "CARTRIDGETYPE": "00", "ROMSIZE": ("0D" if rom_mib == 8 else "0C") if opening else "08", "SRAMSIZE": "00", "COUNTRY": "01",
                    "LICENSEECODE": "00", "VERSION": "00", "ROMBANKS": str(rom_mib * 16) if opening else "8",
                    "ROMBANKSIZE": "10000" if opening else "8000", "ROMMODE": ("EXHIROM" if rom_mib == 8 else "HIROM") if opening else "LOROM", "ROMSPEED": "FASTROM"}
    for key, value in replacements.items():
        header = header.replace(f"@{key}@", value)
    (folder / "hdr.asm").write_text(header)
    objects = []
    sources = [ROOT / "src/engine/player/playerwater.c", ROOT / "src/engine/player/playerrope.c", ROOT / "src/engine/mechanisms/grabrope.c", ROOT / "src/game/adventure.c", ROOT / "src/game/weapons/playersword.c", ROOT / "src/game/player/playeranimation.c", ROOT / "src/game/items/itemheadtorch.c", ROOT / "src/game/appearance.c", ROOT / "src/engine/animation.c", ROOT / "src/engine/world.c", ROOT / "src/engine/entities.c", ROOT / "src/engine/events.c", ROOT / "src/engine/mechanisms.c",
               ROOT / "src/engine/logic.c", ROOT / "src/engine/pacing.c", ROOT / "src/engine/arith.c", ROOT / "src/game/enemies.c", ROOT / "src/game/deceptus.c",
               ROOT / "src/platform/snes/main.c"]
    if opening:
        sources += [ROOT / "src/game/campaign.c", ROOT / "src/platform/snes/background.c"]
        sources += [generated / name for name in metadata["c_sources"]]
    else:
        sources += [ROOT / "assets/generated/room.c"]
    # one tick per presentation, for the tick-exact route verifiers (include/deceptus/pacing.h).
    pacing_flags = ["-DDECEPTUS_PACING_LOCKED"] if os.environ.get("DECEPTUS_PACING_LOCKED") == "1" else []
    for source in sources:
        stem = source.stem
        run([compiler, *(["-H", "-DDECEPTUS_SNES_OPENING=1", "-DSNES_SPRITES_ASM=1", "-DSNES_BACKGROUND_ASM=1", "-DPLATFORM_ARITH=1", "-I" + str(ROOT / "src/platform/snes")] if opening else []), *pacing_flags, "-I" + str(ROOT / "include"), "-I" + str(generated), "-I" + str(sdk / "pvsneslib/include"),
             "-I" + str(sdk / "devkitsnes/include"), "-I" + str(folder), "-Wall", "-c", source,
             "-F", "-o", stem + ".ps"], folder)
        run([optimizer, "-i", stem + ".ps", "-o", stem + ".asm"], folder)
        if opening:
            assembly_path = folder / (stem + ".asm")
            assembly = assembly_path.read_text()
            if source.parent == generated:
                assembly = readonly_export_assembly(assembly, stem)
            assembly_path.write_text(banked_hirom_assembly(assembly, rom_mib == 8))
        run([assembler, "-d", "-s", "-x", "-o", stem + ".obj", stem + ".asm"], folder)
        objects.append(stem + ".obj")
    data_assembly = (generated / "data.asm").read_text()
    if opening and rom_mib == 8:
        data_assembly = exhirom_binary_assembly(data_assembly)
    elif not opening:
        data_assembly = re.sub(r'(\.incbin ")\.\./\.\./assets/generated/',
                               lambda match: match[1] + generated.as_posix() + '/', data_assembly)
    (folder / "data.asm").write_text(data_assembly)
    run([assembler, "-d", "-s", "-x", "-o", "data.obj", "data.asm"], folder)
    objects.append("data.obj")
    if opening:
        run([assembler, "-d", "-s", "-x", "-o", "background_ram.obj", ROOT / "src/platform/snes/background_ram.asm"], folder)
        objects.append("background_ram.obj")
        run([assembler, "-d", "-s", "-x", "-o", "vram_dma.obj", ROOT / "src/platform/snes/vram_dma.asm"], folder)
        objects.append("vram_dma.obj")
        run([assembler, "-d", "-s", "-x", "-o", "background_hash.obj", ROOT / "src/platform/snes/background_hash.asm"], folder)
        objects.append("background_hash.obj")
        run([assembler, "-d", "-s", "-x", "-o", "background_cell.obj", ROOT / "src/platform/snes/background_cell.asm"], folder)
        objects.append("background_cell.obj")
        run([assembler, "-d", "-s", "-x", "-o", "engine_math.obj", ROOT / "src/platform/snes/engine_math.asm"], folder)
        objects.append("engine_math.obj")
        run([assembler, "-d", "-s", "-x", "-o", "sprites_emit.obj", ROOT / "src/platform/snes/sprites_emit.asm"], folder)
        objects.append("sprites_emit.obj")
        run([assembler, "-d", "-s", "-x", "-o", "sprites_quads.obj", ROOT / "src/platform/snes/sprites_quads.asm"], folder)
        objects.append("sprites_quads.obj")
        run([assembler, "-d", "-s", "-x", "-o", "sprites_begin.obj", ROOT / "src/platform/snes/sprites_begin.asm"], folder)
        objects.append("sprites_begin.obj")
        run([assembler, "-d", "-s", "-x", "-o", "sprites_resource.obj", ROOT / "src/platform/snes/sprites_resource.asm"], folder)
        objects.append("sprites_resource.obj")
        if rom_mib == 8:
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
    if opening and rom_mib == 8:
        (folder / "deceptus.sfc").write_bytes(exhirom_boot_mirror((folder / "deceptus.sfc").read_bytes()))
    shutil.copyfile(folder / "deceptus.sfc", folder / ("deceptus-" + scene + ".sfc"))
    (folder / "build.json").write_text(json.dumps(dict(scene=scene, graphics_rooms=rooms if opening else 1,
        rom_mib=rom_mib if opening else 0.25, mapping=("ExHiROM" if rom_mib == 8 else "HiROM") if opening else "LoROM",
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
    parser.add_argument("--output-dir", type=Path, help="isolated SDK object and ROM directory")
    arguments = parser.parse_args()
    build(arguments.sdk.resolve(), arguments.data.resolve(), arguments.full_game, arguments.scene,
          arguments.rooms, arguments.reuse_assets, arguments.rom_mib, arguments.assets_dir, arguments.output_dir)
