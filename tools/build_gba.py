"""Build the shared C engine as a freestanding ARM7TDMI GBA ROM with LLVM."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(command, folder):
    print(' '.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=folder, check=True)


def fix_header(rom, reference):
    """Fill the mandatory header data using the unchanged public tool reference."""
    source = reference.read_text()
    match = re.search(r'// logo\s*\{([^}]+)\}', source)
    if not match:
        raise ValueError('could not find GBA logo data in the header reference')
    logo = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-fA-F]{2})', match.group(1)))
    if len(logo) != 156:
        raise ValueError('invalid GBA logo data length')
    payload = bytearray(rom.read_bytes())
    payload[4:160] = logo
    payload[0xa0:0xac] = b'DECEPTUS'.ljust(12, b'\0')
    payload[0xac:0xb0] = b'DCPT'
    payload[0xb0:0xb2] = b'00'
    payload[0xb2] = 0x96
    payload[0xb3:0xbd] = bytes(10)
    payload[0xbd] = (-sum(payload[0xa0:0xbd]) - 0x19) & 255
    payload[0xbe:0xc0] = b'\0\0'
    # 256KiB cartridges are misidentified as multiboot images by the current mGBA core.
    size = 1 << max(19, (len(payload) - 1).bit_length())
    payload.extend(b'\xff' * (size - len(payload)))
    rom.write_bytes(payload)


def build(data, clang='clang', linker='ld.lld', objcopy='llvm-objcopy', full_game=False, scene='tutorial', reuse_room_assets=False, prepared_room_assets=False, output_dir=None):
    for executable in (clang, linker, objcopy):
        if not shutil.which(str(executable)):
            raise SystemExit(f'{executable} not found; install LLVM or supply its executable path')
    clang, linker, objcopy = (shutil.which(str(executable)) for executable in (clang, linker, objcopy))
    reference = ROOT / '.tools/gbafix-reference.c'
    if not reference.exists():
        raise SystemExit('run python tools/bootstrap.py --target gba first')
    if full_game:
        result = subprocess.run([sys.executable, str(ROOT / 'tools/content.py'), '--data', str(data), '--strict'], cwd=ROOT)
        if result.returncode:
            raise SystemExit('Full-game content support is incomplete; see build/content/summary.json')
    folder = Path(output_dir) if output_dir else ROOT / 'build/gba'
    folder.mkdir(parents=True, exist_ok=True)
    if prepared_room_assets:
        if scene != 'tutorial':
            raise ValueError('prepared room assets require the tutorial scene')
        from snes_tutorial_assets import original_imports
        original_imports(data)
        header=(ROOT/'assets/generated/gba/gba_assets.h').read_text()
        if not re.search(r'^#define GBA_TUTORIAL 1$',header,re.MULTILINE):
            raise ValueError('prepared assets are not the tutorial; run opening preflight first')
    elif scene == 'tutorial':
        command = [sys.executable, ROOT / 'tools/tutorial_assets.py', '--data', data]
        if reuse_room_assets:
            command.append('--reuse-room-assets')
        run(command, ROOT)
    elif scene == 'tonight':
        run([sys.executable, ROOT / 'tools/campaign_assets.py', '--data', data], ROOT)
    else:
        run([sys.executable, ROOT / 'tools/gba_assets.py', '--data', data, '--scene', scene], ROOT)
    platform = ROOT / 'src/platform/gba'
    flags = ['--target=arm-none-eabi', '-mcpu=arm7tdmi', '-mthumb', '-O2', '-ffreestanding', '-fno-builtin',
             '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-ffunction-sections', '-fdata-sections',
             '-std=c90', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'include'),
             '-I' + str(ROOT / 'assets/generated/gba'), '-I' + str(platform / 'freestanding')]
    if os.environ.get('DECEPTUS_PACING_LOCKED') == '1':
        # one tick per presentation, for the tick-exact route verifiers (include/deceptus/pacing.h).
        flags.append('-DDECEPTUS_PACING_LOCKED')
    sources = [ROOT / 'src/engine/player/playerwater.c', ROOT / 'src/engine/player/playerrope.c', ROOT / 'src/engine/mechanisms/grabrope.c', ROOT / 'src/engine/mechanisms/moveablebox.c', ROOT / 'src/engine/mechanisms/crusher.c', ROOT / 'src/engine/mechanisms/floorspikes.c', ROOT / 'src/engine/mechanisms/spikeball.c', ROOT / 'src/engine/mechanisms/checkpointclock.c', ROOT / 'src/game/adventure.c', ROOT / 'src/game/thunderstorm.c', ROOT / 'src/game/mechanismparts.c', ROOT / 'src/engine/animation.c', ROOT / 'src/engine/world.c', ROOT / 'src/engine/entities.c', ROOT / 'src/engine/events.c', ROOT / 'src/engine/mechanisms.c',
               ROOT / 'src/engine/logic.c', ROOT / 'src/engine/pacing.c', ROOT / 'src/engine/arith.c', ROOT / 'src/game/enemies.c', ROOT / 'src/game/deceptus.c', ROOT / 'src/game/campaign.c', ROOT / 'src/game/weapons/playersword.c', ROOT / 'src/game/player/playeranimation.c', ROOT / 'src/game/items/itemheadtorch.c', platform / 'main.c',
               platform / 'runtime.c', ROOT / 'assets/generated/gba/room.c']
    objects = []
    for source in sources:
        target = folder / (source.stem + '.o')
        extra = ['-marm'] if source == platform / 'main.c' else []
        run([clang, *flags, *extra, '-c', source, '-o', target], folder)
        objects.append(target)
    for name, source in [('start', platform / 'start.s'), ('data', ROOT / 'assets/generated/gba/data.s')]:
        target = folder / (name + '.o')
        run([clang, '--target=arm-none-eabi', '-mcpu=arm7tdmi', '-c', source, '-o', target], folder)
        objects.append(target)
    elf = folder / 'deceptus.elf'
    run([linker, '-T', platform / 'linker.ld', '--gc-sections', '-Map=' + str(folder / 'deceptus.map'),
         *objects, '-o', elf], folder)
    rom = folder / 'deceptus.gba'
    run([objcopy, '-O', 'binary', elf, rom], folder)
    fix_header(rom, reference)
    shutil.copyfile(rom, folder / f"deceptus-{scene}.gba")
    (folder / 'build.json').write_text(json.dumps({'compiler': subprocess.check_output([clang, '--version'], text=True).splitlines()[0],
        'rom_bytes': rom.stat().st_size, 'rom_sha256': hashlib.sha256(rom.read_bytes()).hexdigest(),
        'header_reference_sha256': hashlib.sha256(reference.read_bytes()).hexdigest(),
        'full_game': False, 'scene': scene}, indent=2) + '\n')
    print(f'ROM: {rom}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, default=ROOT / 'assets/game/data')
    parser.add_argument('--clang', default='clang')
    parser.add_argument('--linker', default='ld.lld')
    parser.add_argument('--objcopy', default='llvm-objcopy')
    parser.add_argument('--full-game', action='store_true')
    parser.add_argument("--scene", choices=["lever", "platforms", "preview", "cell", "tonight", "water", "tutorial"], default="tutorial")
    parser.add_argument('--reuse-room-assets', action='store_true', help='reuse tutorial room graphics only when source, converter and recipe hashes match')
    parser.add_argument('--prepared-room-assets', action='store_true', help='validate preflight-prepared tutorial inputs without rewriting shared imports or metadata')
    parser.add_argument('--output-dir', type=Path, help='object and ROM folder (default: build/gba), e.g. for a pacing-locked verification cartridge')
    args = parser.parse_args()
    if args.reuse_room_assets and args.scene != 'tutorial':
        parser.error('--reuse-room-assets is supported only for the tutorial')
    if args.prepared_room_assets and (args.scene != 'tutorial' or args.reuse_room_assets):
        parser.error('--prepared-room-assets requires tutorial and cannot be combined with --reuse-room-assets')
    build(args.data.resolve(), args.clang, args.linker, args.objcopy, args.full_game, args.scene, args.reuse_room_assets,args.prepared_room_assets,
          args.output_dir.resolve() if args.output_dir else None)
