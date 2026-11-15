"""Prepare and launch the one-room Mega Drive proof of concept."""
import argparse
from pathlib import Path
import subprocess
import sys

from bootstrap import ROOT
from bootstrap_smd import ensure
from run_game import ensure_python


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path)
    parser.add_argument('--core', type=Path)
    parser.add_argument('--sdk', type=Path)
    parser.add_argument('--data', type=Path, default=ROOT.parent / 'deceptus_engine/data')
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--prepare', action='store_true')
    args = parser.parse_args()
    if args.rom and args.build:
        parser.error('omit --rom when rebuilding the proof of concept')
    rom = args.rom.resolve() if args.rom else ROOT / 'build/smd/deceptus-poc.bin'
    if args.rom and not rom.is_file():
        parser.error(f'cartridge does not exist: {rom}')
    building = args.build or not rom.is_file()
    result = ensure_python('smd', sys.argv[1:], building)
    if result is not None:
        return result
    sdk, core = ensure(emulator=not bool(args.core), sdk_needed=building and not bool(args.sdk))
    if building:
        subprocess.run([sys.executable, str(ROOT / 'tools/build_smd.py'), '--sdk', str(args.sdk or sdk),
                        '--data', str(args.data)], cwd=ROOT, check=True)
    core = args.core.resolve() if args.core else core
    if not core.is_file():
        parser.error(f'emulator core does not exist: {core}')
    print(f'Mega Drive POC: {rom}\nEmulator: {core}', flush=True)
    if args.prepare:
        return 0
    return subprocess.call([sys.executable, str(ROOT / 'tools/play.py'), '--backend', 'smd',
                            '--scene', 'prototype', '--rom', str(rom), '--core', str(core)], cwd=ROOT)


if __name__ == '__main__':
    raise SystemExit(main())
