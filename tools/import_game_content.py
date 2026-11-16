"""Copy a versioned source-content snapshot for standalone builds."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DESTINATION = ROOT / 'assets/game'
REFERENCES = {
    'mechanisms': ('watersurface', 'ringshaderlayer', 'shaderlayer', 'rope', 'grabrope'),
    'items': ('itemheadtorch',),
    'player': ('playereyepositions', 'playerrope', 'playerropehold', 'playeranimation'),
    'effects': ('lightsystem',),
    'level': ('tmxenemy', 'level', 'luanode', 'levelscript'),
}


def import_content(source):
    source = Path(source).resolve()
    if source == DESTINATION.resolve() or source.is_relative_to(DESTINATION.resolve()):
        raise ValueError('select an original engine checkout, not the bundled snapshot')
    tracked = subprocess.check_output(
        ['git', '-C', str(source), 'ls-files', '-z', '--', 'data', 'LICENSES', 'LICENSE.md']
    ).decode('utf-8').split('\0')
    paths = {name for name in tracked if name and not any(
        part.startswith('.') or part == '__pycache__' for part in Path(name).parts)}
    for folder, names in REFERENCES.items():
        for name in names:
            for extension in ('.cpp', '.h'):
                paths.add(f'src/game/{folder}/{name}{extension}')
    paths.add('src/game/mechanisms/controllerkeymap.h')
    missing = [name for name in sorted(paths) if not (source / name).is_file()]
    if missing:
        raise ValueError(f'missing source files: {missing}')
    hashes = {}
    for name in sorted(paths):
        target = DESTINATION / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, target)
        hashes[name] = hashlib.sha256(target.read_bytes()).hexdigest()
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    changed = subprocess.check_output(
        ['git', '-C', str(source), 'diff', 'HEAD', '--name-only', '--', *sorted(paths)], text=True
    ).splitlines()
    manifest = dict(repository='https://github.com/varnholt/deceptus_engine',
                    revision=revision, modified_source_files=changed, files=hashes)
    (DESTINATION / 'SOURCE.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (ROOT / 'SOURCE_REVISION').write_text(revision + '\n', encoding='utf-8')
    print(f'Copied {len(paths)} files to {DESTINATION}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='original engine checkout')
    import_content(parser.parse_args().source)
