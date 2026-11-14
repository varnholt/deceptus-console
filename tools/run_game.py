"""Shared one-command setup and launch for the existing console player."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import zipfile

import bootstrap

ROOT = Path(__file__).resolve().parents[1]


def run(command):
    subprocess.run(list(map(str, command)), cwd=ROOT, check=True)


def ensure_python(backend, arguments, building):
    modules = ('PIL', 'numpy', 'luaparser') if building else ('PIL',)
    if all(importlib.util.find_spec(name) is not None for name in modules):
        return None
    environment = ROOT / '.venv'
    python = environment / ('Scripts/python.exe' if sys.platform == 'win32' else 'bin/python')
    if not python.is_file():
        print('Creating project-local Python environment...', flush=True)
        run([sys.executable, '-m', 'venv', environment])
    print('Installing player/build dependencies into .venv...', flush=True)
    run([python, '-m', 'pip', 'install', '-r', ROOT / 'requirements.txt'])
    return subprocess.call([str(python), str(ROOT / f'run_{backend}.py'), *arguments], cwd=Path.cwd())


def main(backend, arguments=None):
    arguments = list(sys.argv[1:] if arguments is None else arguments)
    parser = argparse.ArgumentParser(description=f'Prepare and launch the Deceptus {backend.upper()} cartridge.')
    parser.add_argument('--rom', type=Path, help='use an existing cartridge instead of the default build')
    parser.add_argument('--core', type=Path, help='use an existing native libretro core')
    parser.add_argument('--build', action='store_true', help='rebuild the default cartridge before playing')
    parser.add_argument('--prepare', action='store_true', help='set up the core and cartridge without opening a window')
    parser.add_argument('--data', type=Path, default=ROOT.parent / 'deceptus_engine/data')
    if backend == 'gba':
        parser.add_argument('--scene', choices=['tutorial', 'tonight', 'lever', 'platforms', 'preview', 'cell', 'water'], default='tutorial')
        parser.add_argument('--clang', default='clang')
        parser.add_argument('--linker', default='ld.lld')
        parser.add_argument('--objcopy', default='llvm-objcopy')
    else:
        parser.add_argument('--scene', choices=['tutorial', 'prototype'], default='tutorial')
        parser.add_argument('--sdk', type=Path, help='use an existing PVSnesLib SDK')
    args = parser.parse_args(arguments)
    if args.rom and args.build:
        parser.error('--build rebuilds the default cartridge; omit --rom')
    rom = args.rom.resolve() if args.rom else ROOT / (
        f'build/gba/deceptus-{args.scene}.gba' if backend == 'gba' else
        f'build/snes/deceptus-{args.scene}.sfc')
    if args.rom and not rom.is_file():
        parser.error(f'cartridge does not exist: {rom}')
    if args.core and not args.core.is_file():
        parser.error(f'emulator core does not exist: {args.core}')
    building = args.build or not rom.is_file()
    if building and not args.data.is_dir():
        parser.error(f'original game data not found: {args.data}; supply --data PATH or --rom PATH')
    try:
        result = ensure_python(backend, arguments, building)
        if result is not None:
            return result
        core = args.core.resolve() if args.core else bootstrap.ensure_emulator(backend)
        if building:
            if backend == 'gba':
                if not (ROOT / '.tools/gbafix-reference.c').is_file():
                    bootstrap.bootstrap(target='gba')
                run([sys.executable, ROOT / 'tools/build_gba.py', '--data', args.data.resolve(),
                     '--scene', args.scene, '--clang', args.clang, '--linker', args.linker, '--objcopy', args.objcopy])
            else:
                sdk = args.sdk.resolve() if args.sdk else Path(os.environ.get('PVSNESLIB_HOME', ROOT / '.tools/pvsneslib')).resolve()
                compiler = sdk / 'devkitsnes/bin' / ('816-tcc.exe' if sys.platform == 'win32' else '816-tcc')
                if not compiler.is_file():
                    if args.sdk or os.environ.get('PVSNESLIB_HOME'):
                        parser.error(f'PVSnesLib compiler not found in {sdk}')
                    bootstrap.bootstrap(target='snes')
                run([sys.executable, ROOT / 'tools/build_snes.py', '--data', args.data.resolve(), '--sdk', sdk,
                     '--scene', args.scene])
        if not rom.is_file():
            raise RuntimeError(f'build did not produce the expected cartridge: {rom}')
        print(f'Emulator: {core}\nCartridge: {rom}', flush=True)
        if args.prepare:
            return 0
        if importlib.util.find_spec('tkinter') is None:
            raise RuntimeError('the project player requires Python with Tk support (tkinter)')
        if backend == 'gba':
            controls = 'Arrows move, Z/X jump, Up interact, Enter pause, Tab inventory, C sword.'
        elif args.scene == 'tutorial':
            controls = 'Arrows move, Z/X jump, Up interact, Enter pause, Tab inventory, C sword, Q confirm, E back.'
        else:
            controls = 'Arrows move, Z jump, Up interact, Enter pause, R restart.'
        print(controls + ' Close the window to exit.', flush=True)
        command = [sys.executable, str(ROOT / 'tools/play.py'), '--backend', backend,
                   '--rom', str(rom), '--core', str(core)]
        if backend == 'snes':
            command += ['--scene', args.scene]
        return subprocess.call(command, cwd=ROOT)
    except (OSError, RuntimeError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        print(f'Cannot start {backend.upper()}: {error}', file=sys.stderr)
        return 1
