"""Build a real, one-room Mega Drive proof of concept with the shared C engine."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from assets import ROOT
from smd_assets import convert


def run(command, folder):
    print(' '.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=folder, check=True)


def build(sdk, data, output):
    sdk, output = sdk.resolve(), output.resolve()
    if not (sdk / 'bin/gcc.exe').is_file():
        raise SystemExit('SGDK is missing; run python tools/bootstrap_smd.py, or supply --sdk PATH')
    generated = ROOT / 'assets/generated/smd'
    metadata = convert(data, generated)
    folder = output / 'out'
    folder.mkdir(parents=True, exist_ok=True)
    compiler, objcopy = sdk / 'bin/gcc.exe', sdk / 'bin/objcopy.exe'
    flags = ['-m68000', '-DSGDK_GCC', '-O3', '-flto', '-fomit-frame-pointer', '-fno-builtin',
             '-ffunction-sections', '-fdata-sections', '-B' + str(sdk / 'bin'),
             '-I' + str(ROOT / 'src/platform/smd/include'), '-I' + str(ROOT / 'include'), '-I' + str(generated), '-I' + str(sdk / 'inc'), '-I' + str(sdk / 'res')]
    header = (sdk / 'src/boot/rom_head.c').read_text().replace('SAMPLE PROGRAM'.ljust(48), 'DECEPTUS MEGA DRIVE POC'.ljust(48))
    (output / 'rom_head.c').write_text(header)
    run([compiler, *[flag for flag in flags if flag != '-flto'], '-c', output / 'rom_head.c', '-o', folder / 'rom_head.o'], output)
    run([objcopy, '-O', 'binary', folder / 'rom_head.o', folder / 'rom_head.bin'], output)
    run([compiler, *flags, '-x', 'assembler-with-cpp', '-Wa,--register-prefix-optional,--bitwise-or',
         '-c', sdk / 'src/boot/sega.s', '-o', folder / 'sega.o'], output)
    sources = ['src/engine/player/playerwater.c', 'src/engine/player/playerrope.c',
               'src/engine/mechanisms/grabrope.c', 'src/engine/animation.c', 'src/engine/world.c',
               'src/engine/entities.c', 'src/engine/mechanisms.c', 'src/engine/logic.c',
               'src/engine/enemies.c', 'src/game/deceptus.c', 'src/platform/smd/main.c']
    sources = [ROOT / source for source in sources] + [generated / 'room.c', generated / 'smd_assets.c']
    objects = []
    for source in sources:
        target = folder / (source.stem + '.o')
        run([compiler, *flags, '-std=gnu90' if 'platform' in source.parts else '-std=c90', '-Wall', '-Wextra', '-Werror', '-Wno-main',
             '-c', source, '-o', target], output)
        objects.append(target)
    elf = output / 'deceptus-poc.elf'
    run([compiler, '-m68000', '-B' + str(sdk / 'bin'), '-n', '-T', sdk / 'md.ld', '-nostdlib',
         folder / 'sega.o', *objects, sdk / 'lib/libmd.a', sdk / 'lib/libgcc.a', '-o', elf,
         '-Wl,--gc-sections,-Map=' + str(output / 'deceptus-poc.map'), '-flto'], output)
    rom = output / 'deceptus-poc.bin'
    run([objcopy, '-O', 'binary', elf, rom], output)
    payload = bytearray(rom.read_bytes())
    payload.extend(bytes([255]) * (-len(payload) % 131072))
    payload[0x1a4:0x1a8] = (len(payload) - 1).to_bytes(4, 'big')
    checksum = sum(int.from_bytes(payload[start:start + 2], 'big') for start in range(512, len(payload), 2)) & 65535
    payload[0x18e:0x190] = checksum.to_bytes(2, 'big')
    rom.write_bytes(payload)
    symbols = subprocess.check_output([str(sdk / 'bin/nm.exe'), '-n', str(elf)], text=True)
    (output / 'deceptus-poc.sym').write_text(symbols)
    metadata.update(rom_bytes=len(payload), rom_sha256=hashlib.sha256(payload).hexdigest(),
                    compiler=subprocess.check_output([compiler, '--version'], text=True).splitlines()[0],
                    emulator_checked=False)
    (output / 'build.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print('ROM:', rom)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=ROOT / '.tools/sgdk')
    parser.add_argument('--data', type=Path, default=ROOT.parent / 'deceptus_engine/data')
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'build/smd')
    args = parser.parse_args()
    build(args.sdk, args.data, args.output_dir)
