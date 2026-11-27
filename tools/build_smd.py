"""Build the Mega Drive opening or isolated performance POC with the shared C engine."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from assets import ROOT
from smd_assets import convert


def run(command, folder):
    print(' '.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=folder, check=True)


def build(sdk, data, output, scene="tutorial", assets_dir=None, reuse_assets=False):
    sdk, output = sdk.resolve(), output.resolve()
    if not (sdk / 'bin/gcc.exe').is_file():
        raise SystemExit('SGDK is missing; run python tools/bootstrap_smd.py, or supply --sdk PATH')
    opening = scene == 'tutorial'
    generated = Path(assets_dir).resolve() if assets_dir else ROOT / ('assets/generated/smd-opening' if opening else 'assets/generated/smd')
    if opening:
        from smd_opening_assets import compile_opening, converter_identity
        from campaign_assets import asset_identity
        if not reuse_assets:
            from smd_opening_inputs import prepare_smd_metadata
            portable = prepare_smd_metadata(data, output / 'portable-metadata')
            compile_opening(data, generated, portable)
        metadata = json.loads((generated / 'build.json').read_text())
        digest = converter_identity()
        if metadata['source_identity'] != asset_identity(data) or metadata['converter_sha256'] != digest:
            raise SystemExit('Mega Drive cached opening does not match original sources or converter; rebuild without --reuse-assets')
        if metadata['rom_graphics_bytes'] > 4 * 1024 * 1024 - 262144:
            raise SystemExit('Mega Drive graphics exceed the flat-cartridge budget')
    else:
        metadata = convert(data, generated)
    folder = output / 'out'
    folder.mkdir(parents=True, exist_ok=True)
    compiler, objcopy = sdk / 'bin/gcc.exe', sdk / 'bin/objcopy.exe'
    flags = ['-m68000', '-DSGDK_GCC', '-O3', '-flto', '-fomit-frame-pointer', '-fno-builtin',
             '-ffunction-sections', '-fdata-sections', '-B' + str(sdk / 'bin'),
             '-I' + str(ROOT / 'src/platform/smd/include'), '-I' + str(ROOT / 'include'), '-I' + str(generated), '-I' + str(sdk / 'inc'), '-I' + str(sdk / 'res'),
             # 68000 arithmetic primitives (include/arith.h), shared with the Amiga: gcc otherwise calls its
             # 32-bit library routines even for 16x16 products and divisions the cpu does in one instruction.
             '-DPLATFORM_ARITH', '-I' + str(ROOT / 'src/platform/m68k')]
    if os.environ.get('DECEPTUS_PACING_LOCKED') == '1':
        # one tick per presentation, for the tick-exact route verifiers (include/deceptus/pacing.h).
        flags.append('-DDECEPTUS_PACING_LOCKED')
    header = (sdk / 'src/boot/rom_head.c').read_text().replace('SAMPLE PROGRAM'.ljust(48), ('DECEPTUS OPENING' if opening else 'DECEPTUS MEGA DRIVE POC').ljust(48))
    (output / 'rom_head.c').write_text(header)
    run([compiler, *[flag for flag in flags if flag != '-flto'], '-c', output / 'rom_head.c', '-o', folder / 'rom_head.o'], output)
    run([objcopy, '-O', 'binary', folder / 'rom_head.o', folder / 'rom_head.bin'], output)
    run([compiler, *flags, '-x', 'assembler-with-cpp', '-Wa,--register-prefix-optional,--bitwise-or',
         '-c', sdk / 'src/boot/sega.s', '-o', folder / 'sega.o'], output)
    sources = ['src/engine/player/playerwater.c', 'src/engine/player/playerrope.c',
               'src/engine/mechanisms/grabrope.c', 'src/engine/mechanisms/moveablebox.c', 'src/engine/mechanisms/crusher.c', 'src/engine/mechanisms/floorspikes.c', 'src/engine/mechanisms/spikeball.c', 'src/engine/mechanisms/checkpointclock.c', 'src/engine/animation.c', 'src/engine/world.c',
               'src/engine/entities.c', 'src/engine/events.c', 'src/engine/mechanisms.c', 'src/engine/logic.c', 'src/engine/pacing.c', 'src/engine/arith.c',
               'src/game/enemies.c', 'src/game/deceptus.c', 'src/platform/smd/main.c']
    sources = [ROOT / source for source in sources]
    if opening:
        sources = [source for source in sources if source != ROOT / 'src/platform/smd/main.c']
        sources += [ROOT / name for name in ('src/game/adventure.c', 'src/game/thunderstorm.c', 'src/game/soundcue.c', 'src/game/mechanismparts.c', 'src/game/campaign.c',
                    'src/game/weapons/playersword.c', 'src/game/player/playeranimation.c', 'src/game/items/itemheadtorch.c', 'src/game/appearance.c', 'src/game/ropelink.c', 'src/platform/smd/opening.c',
                    'src/platform/smd/background.c', 'src/platform/smd/graphics_io.c')]
        sources += [generated / name for name in metadata['c_sources']]
    else:
        sources += [generated / 'room.c', generated / 'smd_assets.c']
    objects = []
    if opening:
        # Keep SDK startup calls external so LTO cannot inline away DMA_init's
        # linker wrapper and allocate an unused DMA queue in the 64KiB RAM.
        target = folder / 'sdk_sys.o'
        sdk_flags = [flag for flag in flags
                     if flag != '-flto' and not flag.startswith('-I')]
        run([compiler, *sdk_flags, '-I' + str(sdk / 'inc'),
             '-I' + str(sdk / 'res'), '-c', sdk / 'src/sys.c',
             '-o', target], output)
        objects.append(target)
    for source in sources:
        target = folder / (source.stem + '.o')
        run([compiler, *flags, '-std=gnu90' if 'platform' in source.parts else '-std=c90', '-Wall', '-Wextra', '-Werror', '-Wno-main',
             '-c', source, '-o', target], output)
        objects.append(target)
    if opening:
        run([compiler, *flags, '-x', 'assembler-with-cpp', '-c', generated / 'data.s', '-o', folder / 'data.o'], output)
        objects.append(folder / 'data.o')
    stem = 'deceptus-tutorial' if opening else 'deceptus-poc'
    elf = output / (stem + '.elf')
    run([compiler, '-m68000', '-B' + str(sdk / 'bin'), '-n', '-T', sdk / 'md.ld', '-nostdlib',
         folder / 'sega.o', *objects, sdk / 'lib/libmd.a', sdk / 'lib/libgcc.a', '-o', elf,
         '-Wl,--gc-sections,-Map=' + str(output / (stem + '.map')), *(['-Wl,--wrap=DMA_init'] if opening else []), '-flto'], output)
    rom = output / (stem + '.bin')
    run([objcopy, '-O', 'binary', elf, rom], output)
    payload = bytearray(rom.read_bytes())
    if len(payload) > 4 * 1024 * 1024:
        raise SystemExit('Mega Drive code, tables and graphics exceed the 4MiB cartridge budget')
    payload.extend(bytes([255]) * (-len(payload) % 131072))
    payload[0x1a4:0x1a8] = (len(payload) - 1).to_bytes(4, 'big')
    checksum = sum(int.from_bytes(payload[start:start + 2], 'big') for start in range(512, len(payload), 2)) & 65535
    payload[0x18e:0x190] = checksum.to_bytes(2, 'big')
    rom.write_bytes(payload)
    symbols = subprocess.check_output([str(sdk / 'bin/nm.exe'), '-n', str(elf)], text=True)
    (output / (stem + '.sym')).write_text(symbols)
    metadata.update(scene=scene, rom_bytes=len(payload), rom_sha256=hashlib.sha256(payload).hexdigest(),
                    compiler=subprocess.check_output([compiler, '--version'], text=True).splitlines()[0],
                    emulator_checked=False)
    (output / 'build.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print('ROM:', rom)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=ROOT / '.tools/sgdk')
    parser.add_argument('--data', type=Path, default=ROOT / 'assets/game/data')
    parser.add_argument('--scene', choices=['prototype', 'tutorial'], default='tutorial')
    parser.add_argument('--assets-dir', type=Path)
    parser.add_argument('--reuse-assets', action='store_true')
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'build/smd')
    args = parser.parse_args()
    build(args.sdk, args.data, args.output_dir, args.scene, args.assets_dir, args.reuse_assets)
