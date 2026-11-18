"""Build the one-room Amiga 500 prototype with Bartman's GCC toolchain."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from amiga_assets import convert
from assets import ROOT


SOURCES = (
    'src/engine/player/playerwater.c', 'src/engine/player/playerrope.c',
    'src/engine/mechanisms/grabrope.c', 'src/engine/animation.c',
    'src/engine/world.c', 'src/engine/entities.c', 'src/engine/events.c', 'src/engine/mechanisms.c',
    'src/engine/logic.c', 'src/game/enemies.c', 'src/game/deceptus.c',
    'src/platform/amiga/runtime.c', 'src/platform/amiga/main.c',
    'src/platform/amiga/input.c', 'src/platform/amiga/video.c',
    'src/platform/amiga/system.c',
)


def build_program(sdk, output, sources, report, generated):
    sdk, output = sdk.resolve(), output.resolve()
    compiler = sdk / 'opt/bin/m68k-amiga-elf-gcc.exe'
    if not compiler.is_file():
        raise SystemExit('Amiga SDK missing; run python tools/bootstrap_amiga.py')
    objects_dir = output / 'obj'
    objects_dir.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment['PATH'] = str(compiler.parent) + os.pathsep + environment['PATH']
    flags = ['-m68000', '-O3', '-flto', '-g', '-nostdlib', '-ffreestanding',
             '-fno-builtin', '-fno-tree-loop-distribute-patterns',
             '-ffunction-sections', '-fdata-sections', '-fomit-frame-pointer',
             '-I' + str(ROOT / 'include'), '-I' + str(generated)]
    objects = []
    for source in sources:
        target = objects_dir / (source.stem + '.o')
        platform_flags = ['-Wno-volatile-register-var'] if 'platform' in source.parts else []
        # keep read-only graphics outside LTO's writable Chip RAM section.
        source_flags = flags
        if source.name in ('amiga_assets.c', 'opening_content.c'):
            source_flags = [flag for flag in flags if flag != '-flto']
        if (source.name in ('rooms.c', 'graphics.c', 'opening_ui.c')
                and source.parent.name == 'amiga'):
            # bound resource/UI code size so AmigaDOS can load the 1 MiB build.
            source_flags = [flag for flag in flags if flag != '-O3'] + ['-Os']
        command = [compiler, *source_flags, *platform_flags, '-std=c90',
                   '-Wall', '-Wextra', '-Werror',
                   '-c', source, '-o', target]
        subprocess.run(list(map(str, command)), env=environment, check=True)
        objects.append(target)
    support = sdk.parents[1] / 'template/support/gcc8_a_support.s'
    support_object = objects_dir / 'gcc_support.o'
    subprocess.run([str(compiler), '-m68000', '-c',
                    '-Wa,--register-prefix-optional', str(support),
                    '-o', str(support_object)], env=environment, check=True)
    objects.append(support_object)
    elf = output / 'deceptus-amiga.elf'
    executable = output / 'deceptus-amiga.exe'
    command = [compiler, *flags, '-Wl,--emit-relocs,--gc-sections,-Ttext=0',
               '-Wl,-e,_start,-Map=' + str(output / 'deceptus-amiga.map'),
               *objects, '-o', elf]
    subprocess.run(list(map(str, command)), env=environment, check=True)
    subprocess.run([str(sdk / 'elf2hunk.exe'), str(elf), str(executable)],
                   check=True)
    sizes = subprocess.check_output(
        [sdk / 'opt/bin/m68k-amiga-elf-size.exe', '-A', elf], text=True)
    sections = {}
    for line in sizes.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0].startswith('.'):
            sections[fields[0]] = int(fields[1])
    report.update(chip_bytes=sections['.MEMF_CHIP'],
                  static_bytes=sum(sections.get(name, 0) for name in (
                      '.text', '.rodata', '.data', '.MEMF_CHIP', '.bss')),
                  executable_bytes=executable.stat().st_size,
                  sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                  compiler=subprocess.check_output([compiler, '--version'],
                                                   text=True).splitlines()[0],
                  emulator_checked=False)
    (output / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
    print(executable)


def build(sdk, data, output):
    generated = output.resolve() / 'generated'
    report = convert(data, generated)
    sources = [ROOT / name for name in SOURCES] + [
        generated / 'room.c', generated / 'amiga_assets.c']
    return build_program(sdk, output, sources, report, generated)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=ROOT /
                        '.tools/amiga-debug/extension/bin/win32')
    parser.add_argument('--data', type=Path, default=ROOT / 'assets/game/data')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--scene', choices=('prototype', 'tutorial'), default='prototype')
    parser.add_argument('--reuse-assets', action='store_true')
    args = parser.parse_args()
    if args.scene == 'tutorial':
        from build_amiga_opening import build_opening
        build_opening(args.sdk, args.data, args.output_dir or ROOT / 'build/amiga-opening', args.reuse_assets)
    else:
        build(args.sdk, args.data, args.output_dir or ROOT / 'build/amiga')
