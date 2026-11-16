"""Prepare and launch the Mega Drive opening or the separate one-room prototype."""
import argparse
from pathlib import Path
import subprocess
import sys

from bootstrap import ROOT
from bootstrap_smd import ensure
from run_game import ensure_python


def main(arguments=None):
    arguments = list(sys.argv[1:] if arguments is None else arguments)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene', choices=['tutorial', 'prototype'], default='tutorial',
                        help='opening adventure (default) or the separate one-room prototype')
    parser.add_argument('--rom', type=Path, help='play an existing cartridge')
    parser.add_argument('--core', type=Path)
    parser.add_argument('--sdk', type=Path)
    parser.add_argument('--data', type=Path, default=ROOT / 'assets/game/data')
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--prepare', action='store_true')
    args = parser.parse_args(arguments)
    if args.rom and args.build:
        parser.error('omit --rom when rebuilding the selected scene')
    filename = 'deceptus-tutorial.bin' if args.scene == 'tutorial' else 'deceptus-poc.bin'
    rom = args.rom.resolve() if args.rom else ROOT / 'build/smd' / filename
    if args.rom and not rom.is_file():
        parser.error(f'cartridge does not exist: {rom}')
    if args.core and not args.core.is_file():
        parser.error(f'emulator core does not exist: {args.core}')
    building = args.build or not rom.is_file()
    if building and not args.data.is_dir():
        parser.error(f'original game data not found: {args.data}; supply --data PATH or --rom PATH')
    result = ensure_python('smd', arguments, building)
    if result is not None:
        return result
    sdk, core = ensure(emulator=not bool(args.core), sdk_needed=building and not bool(args.sdk))
    if building:
        subprocess.run([sys.executable, str(ROOT / 'tools/build_smd.py'), '--sdk', str(args.sdk or sdk),
                        '--data', str(args.data), '--scene', args.scene], cwd=ROOT, check=True)
    if not rom.is_file():
        raise RuntimeError(f'build did not produce the expected cartridge: {rom}')
    core = args.core.resolve() if args.core else core
    if not core.is_file():
        parser.error(f'emulator core does not exist: {core}')
    print(f'Mega Drive {args.scene}: {rom}\nEmulator: {core}', flush=True)
    if args.prepare:
        return 0
    controls = ('Arrows move, Z/X jump, Up interact, Down bend, Enter pause, Tab inventory, C sword, Q confirm, E back.'
                if args.scene == 'tutorial' else
                'Arrows move, Z/X jump, Up interact, Enter pause, R restart.')
    print(controls + ' Close the window to exit.', flush=True)
    return subprocess.call([sys.executable, str(ROOT / 'tools/play.py'), '--backend', 'smd',
                            '--scene', args.scene, '--rom', str(rom), '--core', str(core)], cwd=ROOT)


if __name__ == '__main__':
    raise SystemExit(main())
