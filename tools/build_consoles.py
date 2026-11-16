"""Prepare original imports once, then build all three console openings concurrently."""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def prepare_opening(data,targets):
    from snes_tutorial_assets import ensure_original_imports
    inputs=ensure_original_imports(data,refresh_metadata=True)
    if 'smd' in targets:
        from smd_opening_inputs import prepare_smd_metadata
        from smd_opening_assets import compile_opening
        metadata=prepare_smd_metadata(data,ROOT/'build/consoles/smd-metadata',prepared_imports=inputs)
        # prepare these before threads so SNES and SMD never share mutable metadata.
        compile_opening(data,ROOT/'assets/generated/smd-opening',metadata)


def build_target(target, data, sdk, clang, linker, objcopy, smd_sdk=None):
    folder = ROOT / 'build/consoles'
    folder.mkdir(parents=True, exist_ok=True)
    log = folder / f'{target}.log'
    if target == 'gba':
        command = [sys.executable, ROOT / 'tools/build_gba.py', '--scene', 'tutorial',
                   '--data', data, '--clang', clang, '--linker', linker, '--objcopy', objcopy,
                   '--prepared-room-assets']
        rom = ROOT / 'build/gba/deceptus-tutorial.gba'
        coverage = 'opening adventure; not a complete-game claim'
    elif target == 'snes':
        command = [sys.executable, ROOT / 'tools/build_snes.py', '--scene', 'tutorial',
                   '--data', data, '--sdk', sdk]
        rom = ROOT / 'build/snes/deceptus-tutorial.sfc'
        coverage = 'opening adventure; not a complete-game claim'
    elif target == 'smd':
        command = [sys.executable, ROOT/'tools/build_smd.py','--scene','tutorial','--data',data,
                   '--sdk',smd_sdk or ROOT/'.tools/sgdk','--assets-dir',ROOT/'assets/generated/smd-opening','--reuse-assets']
        rom = ROOT/'build/smd/deceptus-tutorial.bin'
        coverage = 'experimental opening adventure; performance below target; not a complete-game claim'
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
    parser.add_argument('--targets', nargs='+', choices=['gba', 'snes', 'smd'], default=['gba', 'snes', 'smd'])
    parser.add_argument('--data', type=Path, default=ROOT / 'assets/game/data')
    parser.add_argument('--sdk', type=Path, default=Path(os.environ.get('PVSNESLIB_HOME', ROOT / '.tools/pvsneslib')))
    parser.add_argument('--smd-sdk',type=Path,default=ROOT/'.tools/sgdk')
    parser.add_argument('--clang', default='clang')
    parser.add_argument('--linker', default='ld.lld')
    parser.add_argument('--objcopy', default='llvm-objcopy')
    args = parser.parse_args()
    targets = list(dict.fromkeys(args.targets))
    reports = []
    folder = ROOT / 'build/consoles'
    folder.mkdir(parents=True, exist_ok=True)
    try:
        prepare_opening(args.data.resolve(),targets)
    except Exception as error:
        reports = [{'target': target, 'returncode': 1, 'phase': 'original-import preflight',
                    'error': str(error)} for target in targets]
        (folder / 'build.json').write_text(json.dumps({'full_game': False, 'targets': reports}, indent=2) + '\n')
        print(f'original-import preflight: FAILED: {error}', flush=True)
        return 1
    with ThreadPoolExecutor(max_workers=len(targets)) as executor:
        futures = {executor.submit(build_target, target, args.data.resolve(), args.sdk.resolve(),
                                   args.clang, args.linker, args.objcopy,args.smd_sdk.resolve()): target for target in targets}
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
