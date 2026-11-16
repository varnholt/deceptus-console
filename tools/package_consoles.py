"""Package explicit, hash-verified cartridges and reports without original assets.

The JSON specification supplies snapshot and exactly three targets. Each target
supplies target, rom, build_report, reports, coverage and performance. Coverage is
opening, partial-opening or prototype; performance is an explicit description.
Paths in the specification are relative to the checkout, or absolute.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
EXTENSIONS = {'gba': '.gba', 'snes': '.sfc', 'smd': '.bin'}
NOTICES = {
    'ENGINE-CODE.txt': ROOT / 'LICENSE',
    'DECEPTUS-DATA.md': ROOT / 'assets/game/data/LICENSE.md',
    'GAME-CONTENT.txt': ROOT / 'assets/game/LICENSES/GAME-CONTENT.txt',
    'PVSNESLIB.txt': ROOT / '.tools/pvsneslib/pvsneslib/pvsneslib_license.txt',
    'SGDK.txt': ROOT / '.tools/sgdk/license.txt',
    'SGDK-RUNTIME.txt': ROOT / '.tools/sgdk/COPYING.RUNTIME',
}
METRICS = ('input_ticks', 'simulation_ticks', 'video_frames', 'extra_display_frames',
           'native_rope_attached_ticks', 'benchmark', 'frame_pacing', 'comparisons')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def input_path(value):
    path = Path(value)
    return path.resolve() if path.is_absolute() else (ROOT / path).resolve()


def recorded_path(path):
    try:
        return path.relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return str(path)


def read_report(value):
    path = input_path(value)
    if path.suffix.lower() != '.json' or not path.is_file():
        raise ValueError(f'missing JSON report: {path}')
    payload = path.read_bytes()
    report = json.loads(payload.decode('utf-8-sig'))
    if (not isinstance(report, dict) or report.get('failure') or report.get('error') or
            report.get('returncode', 0) != 0 or report.get('verified') is False or
            report.get('passed') is False):
        raise ValueError(f'report is not a successful result: {path}')
    return path, report, hashlib.sha256(payload).hexdigest()


def verify_header(target, rom, data):
    if target == 'gba':
        valid = len(data) >= 192 and data[0xb2] == 0x96 and data[0xa0:0xa8] == b'DECEPTUS'
    elif target == 'smd':
        valid = len(data) >= 512 and data[0x100:0x104] == b'SEGA'
    else:
        valid = any(len(data) > offset + 32 and data[offset:offset + 8] == b'DECEPTUS'
                    for offset in (0x7fc0, 0xffc0, 0x40ffc0))
    if not valid:
        raise ValueError(f'{rom.name} does not contain the expected {target} cartridge header')


def actual_states(target, report):
    states = report.get(target)
    if isinstance(states, list) and states:
        return states
    if target == 'smd' and report.get('comparisons', 0) > 0:
        return report.get('checkpoints', [])
    return []


def validate_target(spec):
    target = spec['target']
    if target not in EXTENSIONS:
        raise ValueError(f'unsupported cartridge target: {target}')
    rom = input_path(spec['rom'])
    if not rom.is_file() or rom.suffix.lower() != EXTENSIONS[target]:
        raise ValueError(f'missing or misnamed {target} cartridge: {rom}')
    payload = rom.read_bytes()
    verify_header(target, rom, payload)
    sha = hashlib.sha256(payload).hexdigest()
    build_path, build, build_sha = read_report(spec['build_report'])
    if build.get('rom_sha256', build.get('sha256')) != sha:
        raise ValueError(f'stale {target} build report: {build_path}')
    if build.get('rom_bytes', rom.stat().st_size) != rom.stat().st_size:
        raise ValueError(f'{target} cartridge size disagrees with build report')
    coverage = spec['coverage']
    if coverage not in ('opening', 'partial-opening', 'prototype'):
        raise ValueError(f'unknown coverage for {target}: {coverage}')
    performance = spec['performance']
    if not isinstance(performance, str) or not performance.strip():
        raise ValueError(f'an explicit performance description is required for {target}')
    reports = []
    primary = None
    if not spec.get('reports'):
        raise ValueError(f'no verification reports supplied for {target}')
    for value in spec['reports']:
        path, report, report_sha = read_report(value)
        cartridge_sha = report.get('rom_sha256', report.get('production_rom_sha256'))
        if cartridge_sha != sha:
            raise ValueError(f'stale or unrelated {target} verification report: {path}')
        states = actual_states(target, report)
        if report.get('emulator') and states:
            for state in states:
                for field in ('content_error', 'background_error', 'graphics_error',
                              'sprite_overflow', 'ui_overflow'):
                    if state.get(field, 0):
                        raise ValueError(f'{target} verification contains {field}: {path}')
            primary = report
        reports.append((path, report, report_sha))
    if primary is None:
        raise ValueError(f'{target} needs an actual-cartridge emulator report, not only native results')
    states = actual_states(target, primary)
    if coverage == 'opening':
        final = states[-1]
        inventory = final.get('inventory', [])
        if (len(states) < 56 or not final.get('has_sword') or len(inventory) < 7 or
                not inventory[5] or not inventory[6] or inventory[3] or
                final.get('health', 0) <= 0 or final.get('deaths') != 0 or
                final.get('flags', 0) & 768 != 768):
            raise ValueError(f'{target} report does not establish the complete opening workflow')
        if len(primary.get('native', [])) != len(states):
            raise ValueError(f'{target} opening report lacks matching native checkpoints')
        for expected, actual in zip(primary['native'], states):
            if any(expected.get(field) != actual.get(field) for field in
                   ('room', 'flags', 'health', 'inventory', 'has_sword', 'deaths')):
                raise ValueError(f'{target} emulator and native opening checkpoints disagree')
        if primary.get('route_sha256') != digest(ROOT / 'tests/fixtures/opening-route.json'):
            raise ValueError(f'{target} report uses a stale opening input route')
    elif coverage == 'prototype' and (primary.get('native') or len(states) >= 56):
        raise ValueError(f'{target} report and prototype coverage disagree')
    result = dict(target=target, rom='roms/Deceptus-' + target + EXTENSIONS[target],
                  rom_bytes=rom.stat().st_size, rom_sha256=sha, coverage=coverage,
                  performance=dict(description=performance, evidence=[
                      {key: report[key] for key in METRICS if key in report} for _, report, _ in reports]),
                  emulator=primary['emulator'], checkpoints=len(states), scope=primary.get('scope', ''),
                  input_rom=recorded_path(rom), input_build_report=recorded_path(build_path))
    return result, rom, build_path, build_sha, reports


def code_revision():
    return subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()


def code_dirty():
    return bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True).strip())


def source_snapshot():
    names = subprocess.check_output(
        ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=ROOT).decode().split('\0')
    files = []
    for name in sorted(set(names) - {''}):
        path = ROOT / name
        if path.is_file():
            payload = path.read_bytes()
            files.append(dict(path=name, bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest()))
        else:
            files.append(dict(path=name, present=False))
    canonical = json.dumps(files, sort_keys=True, separators=(',', ':')).encode()
    return dict(sha256=hashlib.sha256(canonical).hexdigest(), files=files)


def package(spec_path, output):
    specification = json.loads(Path(spec_path).read_text(encoding='utf-8-sig'))
    snapshot = specification['snapshot']
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', snapshot):
        raise ValueError('snapshot must be a simple release label')
    targets = specification['targets']
    if sorted(item['target'] for item in targets) != sorted(EXTENSIONS):
        raise ValueError('supply exactly one explicit GBA, SNES and SMD cartridge')
    output = input_path(output)
    release_root = (ROOT / 'build/release').resolve()
    if not output.is_relative_to(release_root) or output == release_root:
        raise ValueError('release output must be a new directory inside build/release')
    archive = output.parent / (output.name + '.zip')
    if output.exists() or archive.exists():
        raise ValueError(f'release already exists; choose a new output name: {output}')
    # complete validation before creating the reviewable release directory.
    validated = [validate_target(item) for item in targets]
    for notice in NOTICES.values():
        if not notice.is_file():
            raise ValueError(f'missing ownership/dependency notice: {notice}')
    revision, dirty, sources = code_revision(), code_dirty(), source_snapshot()
    manifest = dict(snapshot=snapshot, source_commit=None if dirty else revision,
                    source_base_commit=revision, source_dirty=dirty,
                    source_snapshot_sha256=sources['sha256'], source_manifest='SOURCE.json',
                    source_snapshot_scope='worktree file digests at packaging time; cartridge identity is recorded separately',
                    full_game=False, targets=[])
    output.mkdir(parents=True)
    for record, rom, build, build_sha, reports in validated:
        destination = output / record['rom']
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(rom, destination)
        report_folder = output / 'reports' / record['target']
        report_folder.mkdir(parents=True)
        shutil.copyfile(build, report_folder / 'build.json')
        if digest(report_folder / 'build.json') != build_sha:
            raise ValueError(f'build report changed while packaging: {build}')
        record['build_report'] = dict(path=(report_folder / 'build.json').relative_to(output).as_posix(),
                                     sha256=build_sha)
        record['reports'] = []
        for index, (path, _, report_sha) in enumerate(reports):
            copied = report_folder / f'{index:02d}-{path.name}'
            shutil.copyfile(path, copied)
            if digest(copied) != report_sha:
                raise ValueError(f'verification report changed while packaging: {path}')
            record['reports'].append(dict(path=copied.relative_to(output).as_posix(), sha256=digest(copied)))
        # catch a changed input between validation and copying.
        if digest(destination) != record['rom_sha256']:
            raise ValueError(f'cartridge changed while packaging: {rom}')
        manifest['targets'].append(record)
    notices = output / 'LICENSES'
    notices.mkdir()
    for name, path in NOTICES.items():
        shutil.copyfile(path, notices / name)
    manifest['targets'].sort(key=lambda item: item['target'])
    (output / 'SOURCE.json').write_text(json.dumps(sources, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    (output / 'BUILD.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    lines = ['Deceptus console playtest ' + snapshot, '',
             'Open a cartridge from roms/ in an emulator for that console.',
             'These are development builds. BUILD.json records exact coverage and performance.', '']
    for item in manifest['targets']:
        lines += [item['target'].upper() + ': ' + item['coverage'], item['scope'],
                  item['performance']['description'], '']
    lines += ['Original maps and artwork are not included separately.',
              'Ownership and dependency notices are in LICENSES/.']
    (output / 'README.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
        for path in sorted(output.rglob('*')):
            if path.is_file():
                entry = zipfile.ZipInfo(path.relative_to(output).as_posix(), date_time=(1980, 1, 1, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.create_system = 3
                entry.external_attr = 0o100644 << 16
                bundle.writestr(entry, path.read_bytes())
    print(json.dumps(dict(directory=str(output), archive=str(archive), sha256=digest(archive)), indent=2))
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--spec', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    arguments = parser.parse_args()
    package(arguments.spec, arguments.output)
