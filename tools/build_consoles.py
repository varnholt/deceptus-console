"""Build the GBA opening and retained SNES prototype concurrently."""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def build_target(target, data, sdk, clang, linker, objcopy):
    folder = ROOT / 'build/consoles'
    folder.mkdir(parents=True, exist_ok=True)
    log = folder / f'{target}.log'
    if target == 'gba':
        command = [sys.executable, ROOT / 'tools/build_gba.py', '--scene', 'tutorial',
                   '--data', data, '--clang', clang, '--linker', linker, '--objcopy', objcopy]
        rom = ROOT / 'build/gba/deceptus-tutorial.gba'
        coverage = 'opening adventure; not a complete-game claim'
    elif target == 'snes':
        command = [sys.executable, ROOT / 'tools/build_snes.py', '--data', data, '--sdk', sdk]
        rom = ROOT / 'build/snes/deceptus.sfc'
        coverage = 'single-room prototype; no GBA adventure frontend'
    else:
        raise ValueError(f'unsupported console target: {target}')
    print(f'{target}: building (log: {log})', flush=True)
    with log.open('w', encoding='utf-8') as output:
        result = subprocess.run(list(map(str, command)), cwd=ROOT, stdout=output,
                                stderr=subprocess.STDOUT, check=False)
    report = {'target': target, 'returncode': result.returncode,
              'log': str(log.relative_to(ROOT)), 'coverage': coverage}
    if result.returncode == 0:
        report.update(rom=str(rom.relative_to(ROOT)), rom_bytes=rom.stat().st_size,
                      rom_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),
                      emulator_checked=False)
    print(f"{target}: {'passed' if result.returncode == 0 else 'FAILED'} (build)", flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--targets', nargs='+', choices=['gba', 'snes', 'smd'], default=['gba', 'snes'])
    parser.add_argument('--data', type=Path, default=ROOT.parent / 'deceptus_engine/data')
    parser.add_argument('--sdk', type=Path, default=Path(os.environ.get('PVSNESLIB_HOME', ROOT / '.tools/pvsneslib')))
    parser.add_argument('--clang', default='clang')
    parser.add_argument('--linker', default='ld.lld')
    parser.add_argument('--objcopy', default='llvm-objcopy')
    args = parser.parse_args()
    if 'smd' in args.targets:
        parser.error('SMD is not implemented: a 68000 toolchain, original-source graphics converter, '
                     'cartridge startup and Mega Drive video/input backend are required; no ROM was built')
    targets = list(dict.fromkeys(args.targets))
    reports = []
    with ThreadPoolExecutor(max_workers=len(targets)) as executor:
        futures = {executor.submit(build_target, target, args.data.resolve(), args.sdk.resolve(),
                                   args.clang, args.linker, args.objcopy): target for target in targets}
        for future in as_completed(futures):
            try:
                reports.append(future.result())
            except Exception as error:
                reports.append({'target': futures[future], 'returncode': 1, 'error': str(error)})
                print(f'{futures[future]}: FAILED: {error}', flush=True)
    report = ROOT / 'build/consoles/build.json'
    report.write_text(json.dumps({'full_game': False, 'targets': sorted(reports, key=lambda item: item['target'])},
                                 indent=2) + '\n')
    return int(any(item['returncode'] != 0 for item in reports))


if __name__ == '__main__':
    raise SystemExit(main())
