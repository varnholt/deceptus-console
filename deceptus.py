"""One entry point for the Deceptus console builds: doctor, play, build and verify.

Each command delegates to the existing tools and launchers; it only fixes the defaults
(the opening on every platform) and toolchain locations, so results match the direct commands.
"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
TOOLS = ROOT / '.tools'
LOCAL_CONFIG = ROOT / 'deceptus.local.json'
TARGETS = ('gba', 'snes', 'smd', 'amiga')
EXE = '.exe' if sys.platform == 'win32' else ''
AMIGA_SDK = TOOLS / 'amiga-debug/extension/bin/win32'


def load_config():
    """Read the optional, git-ignored deceptus.local.json with per-machine tool locations."""
    if not LOCAL_CONFIG.is_file():
        return {}
    try:
        config = json.loads(LOCAL_CONFIG.read_text(encoding='utf-8'))
    except json.JSONDecodeError as error:
        raise SystemExit(f'{LOCAL_CONFIG.name}: line {error.lineno}: {error.msg}')
    known = {'llvm_bin', 'pvsneslib', 'sgdk', 'amiga_sdk', 'data'}
    unknown = sorted(set(config) - known)
    if unknown:
        raise SystemExit(f'{LOCAL_CONFIG.name}: unknown keys {unknown}; supported: {sorted(known)}')
    return config


def llvm_tools(config):
    folder = config.get('llvm_bin')
    names = ('clang', 'ld.lld', 'llvm-objcopy')
    if folder:
        return [str(Path(folder) / (name + EXE)) for name in names]
    return list(names)


def data_folder(config):
    return Path(config.get('data', ROOT / 'assets/game/data'))


def pvsneslib(config):
    return Path(config.get('pvsneslib') or os.environ.get('PVSNESLIB_HOME') or TOOLS / 'pvsneslib')


def sgdk(config):
    return Path(config.get('sgdk', TOOLS / 'sgdk'))


def amiga_sdk(config):
    return Path(config.get('amiga_sdk', AMIGA_SDK))


def run(command, environment=None):
    command = list(map(str, command))
    print('> ' + subprocess.list2cmdline(command), flush=True)
    return subprocess.call(command, cwd=ROOT, env=environment)


def python(script, *arguments):
    return [sys.executable, ROOT / script, *arguments]


# doctor


def doctor(args, config):
    problems = []

    def check(label, ok, fix):
        print(f"  [{'ok' if ok else 'missing'}] {label}")
        if not ok:
            problems.append((label, fix))

    def found(tool):
        return Path(tool).is_file() or shutil.which(tool) is not None

    print('Python')
    check(f'Python {sys.version.split()[0]} (3.11 or newer)', sys.version_info >= (3, 11),
          'install Python 3.13 (NumPy 2.3.4 needs 3.11 or newer)')
    for module, package in (('PIL', 'Pillow'), ('numpy', 'numpy'), ('luaparser', 'luaparser')):
        # tools/lua_source.py also accepts a luaparser copy in .tools/python.
        local = module == 'luaparser' and (TOOLS / 'python/luaparser').is_dir()
        check(f'package {package}', importlib.util.find_spec(module) is not None or local,
              'python -m pip install -r requirements.txt')
    check('tkinter (project player window)', importlib.util.find_spec('tkinter') is not None,
          'install Python from python.org with Tcl/Tk enabled')
    check(f'game data in {data_folder(config)}', data_folder(config).is_dir(),
          'restore assets/game/data from git, or set "data" in deceptus.local.json')
    print('Game Boy Advance')
    for tool in llvm_tools(config):
        check(f'LLVM {Path(tool).name}', found(tool),
              'install LLVM with the ARM target and add its bin folder to PATH, '
              'or set "llvm_bin" in deceptus.local.json')
    check('GBA header reference (.tools/gbafix-reference.c)', (TOOLS / 'gbafix-reference.c').is_file(),
          'python tools/bootstrap.py --target gba --emulator')
    check('mGBA libretro core', (TOOLS / f'mgba/mgba_libretro{".dll" if EXE else ".so"}').is_file(),
          'python tools/bootstrap.py --target gba --emulator')
    print('Super Nintendo')
    check(f'PVSnesLib in {pvsneslib(config)}', (pvsneslib(config) / f'devkitsnes/bin/816-tcc{EXE}').is_file(),
          'python tools/bootstrap.py --target snes --emulator')
    check('Snes9x libretro core', (TOOLS / 'snes9x/snes9x_libretro.dll').is_file(),
          'python tools/bootstrap.py --target snes --emulator')
    print('Mega Drive')
    check(f'SGDK in {sgdk(config)}', (sgdk(config) / 'bin/gcc.exe').is_file(),
          'python tools/bootstrap_smd.py --emulator')
    check('Genesis Plus GX libretro core', (TOOLS / 'genesis_plus_gx/genesis_plus_gx_libretro.dll').is_file(),
          'python tools/bootstrap_smd.py --emulator')
    print('Amiga 500')
    sdk = amiga_sdk(config)
    check(f'amiga-debug toolchain and WinUAE in {sdk}',
          all((sdk / name).is_file() for name in ('opt/bin/m68k-amiga-elf-gcc.exe', 'elf2hunk.exe', 'winuae-gdb.exe')),
          'python tools/bootstrap_amiga.py')
    if not problems:
        print('\nEverything is in place. Next: python deceptus.py play gba')
        return 0
    print(f'\n{len(problems)} missing. To fix:')
    for fix in dict.fromkeys(fix for _, fix in problems):
        print(f'  {fix}')
    print('Automatic downloads need Windows x64 Python; "play" also fetches missing emulator cores itself.')
    return 1


# play


def play(args, config):
    data = ['--data', data_folder(config)]
    extra = ['--build'] if args.build else []
    if args.prepare:
        extra.append('--prepare')
    if args.target == 'gba':
        clang, linker, objcopy = llvm_tools(config)
        return run(python('run_gba.py', '--scene', args.scene or 'tutorial', *data, *extra,
                          '--clang', clang, '--linker', linker, '--objcopy', objcopy))
    if args.target == 'snes':
        sdk = ['--sdk', config['pvsneslib']] if 'pvsneslib' in config else []
        return run(python('run_snes.py', '--scene', args.scene or 'tutorial', *data, *sdk, *extra))
    if args.target == 'smd':
        sdk = ['--sdk', config['sgdk']] if 'sgdk' in config else []
        return run(python('run_smd.py', '--scene', args.scene or 'tutorial', *data, *sdk, *extra))
    # the Amiga launcher still defaults to its one-room prototype; the opening uses the native renderer.
    scene = args.scene or 'tutorial'
    renderer = ['--renderer', 'native'] if scene == 'tutorial' else []
    sdk = ['--sdk', config['amiga_sdk']] if 'amiga_sdk' in config else []
    return run(python('run_amiga.py', '--scene', scene, *renderer, *data, *sdk, *extra))


# build


def build(args, config):
    targets = list(TARGETS) if args.all else list(dict.fromkeys(args.targets))
    unknown = [target for target in targets if target not in TARGETS]
    if unknown or not targets:
        raise SystemExit((f'unknown targets {unknown}; ' if unknown else '') +
                         'name one or more of gba, snes, smd, amiga, or pass --all')
    data = data_folder(config).resolve()
    result = 0
    consoles = [target for target in targets if target != 'amiga']
    if consoles:
        clang, linker, objcopy = llvm_tools(config)
        result |= run(python('tools/build_consoles.py', '--targets', *consoles, '--data', data,
                             '--sdk', pvsneslib(config), '--smd-sdk', sgdk(config),
                             '--clang', clang, '--linker', linker, '--objcopy', objcopy))
        if result:
            print('See build/consoles/<target>.log for the failing step.', file=sys.stderr)
    if 'amiga' in targets:
        # build_consoles.py has no Amiga worker; the Amiga build runs after the others.
        if run(python('tools/build_amiga.py', '--scene', 'tutorial', '--renderer', 'native',
                      '--sdk', amiga_sdk(config), '--data', data)):
            print('amiga: FAILED (build)', file=sys.stderr)
            result = 1
        elif run(python('tools/amiga_package.py')):
            print('amiga: FAILED (package)', file=sys.stderr)
            result = 1
        else:
            print('amiga: passed (build/amiga-native-opening, package in build/amiga-package)')
    return result


# verify


def verify(args, config):
    """Build a pacing-locked cartridge into its own folder and replay the 56-checkpoint route."""
    data = data_folder(config).resolve()
    clang, linker, objcopy = llvm_tools(config)
    folder = ROOT / f'build/{args.target}-verify'
    # the route is recorded in simulation ticks; normal builds catch up after late frames.
    environment = dict(os.environ, DECEPTUS_PACING_LOCKED='1')
    if args.target == 'gba':
        rom = folder / 'deceptus-tutorial.gba'
        steps = [python('tools/build_gba.py', '--scene', 'tutorial', '--data', data, '--output-dir', folder,
                        '--clang', clang, '--linker', linker, '--objcopy', objcopy),
                 python('tools/verify_opening.py', '--rom', rom, '--clang', clang)]
    elif args.target == 'snes':
        rom = folder / 'deceptus-tutorial.sfc'
        steps = [python('tools/build_snes.py', '--scene', 'tutorial', '--data', data, '--sdk', pvsneslib(config),
                        '--output-dir', folder),
                 python('tools/emulator.py', '--rom', rom)]
    elif args.target == 'smd':
        rom = folder / 'deceptus-tutorial.bin'
        steps = [python('tools/build_smd.py', '--scene', 'tutorial', '--data', data, '--sdk', sgdk(config),
                        '--output-dir', folder),
                 python('tools/verify_smd_opening.py', '--rom', rom, '--clang', clang,
                        '--assets-dir', ROOT / 'assets/generated/smd-opening')]
    else:
        print('No Amiga route verifier is part of this repository yet; see docs/AMIGA.md for the '
              'WinUAE measurements.', file=sys.stderr)
        return 1
    for step in steps:
        if run(step, environment):
            print(f'{args.target}: verification FAILED', file=sys.stderr)
            return 1
    print(f'{args.target}: route verified against {rom.relative_to(ROOT)}')
    return 0


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('doctor', help='check Python packages, toolchains, SDKs and emulator cores')
    command = commands.add_parser('play', help='open the opening in the project player (or WinUAE)')
    command.add_argument('target', choices=TARGETS)
    command.add_argument('--build', action='store_true', help='rebuild before playing')
    command.add_argument('--prepare', action='store_true', help='set up and build without opening a window')
    command.add_argument('--scene', help='another scene than the opening (see the run_<target>.py --help)')
    command = commands.add_parser('build', help='build the opening cartridges and Amiga image')
    command.add_argument('targets', nargs='*', metavar='target', help='gba, snes, smd or amiga')
    command.add_argument('--all', action='store_true', help='build every target')
    command = commands.add_parser('verify', help='replay the opening route on a pacing-locked build')
    command.add_argument('target', choices=TARGETS)
    args = parser.parse_args(arguments)
    config = load_config()
    return {'doctor': doctor, 'play': play, 'build': build, 'verify': verify}[args.command](args, config)


if __name__ == '__main__':
    raise SystemExit(main())
