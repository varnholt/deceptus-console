"""Install the pinned project-local Amiga compiler and emulator bundle."""

import hashlib
import json
import platform
import urllib.request

from bootstrap import ROOT, extract

VERSION = '1.8.2'
CHECKSUM = 'c1e2b11175a5c5036d47eef095a21a616fbeb9f1a681364519c7910583d7ee3e'
URL = ('https://github.com/BartmanAbyss/vscode-amiga-debug/releases/download/'
       f'{VERSION}/amiga-debug-{VERSION}.vsix')
SDK = ROOT / '.tools/amiga-debug/extension/bin/win32'


def ensure_sdk():
    if platform.system() != 'Windows' or platform.machine().lower() not in (
            'amd64', 'x86_64'):
        raise SystemExit('automatic Amiga setup currently supports Windows x64')
    required = [SDK / 'opt/bin/m68k-amiga-elf-gcc.exe',
                SDK / 'elf2hunk.exe', SDK / 'winuae-gdb.exe',
                SDK.parents[1] / 'template/support/gcc8_a_support.s']
    if all(path.is_file() for path in required):
        return SDK
    archive = ROOT / '.tools' / f'amiga-debug-{VERSION}.vsix'
    archive.parent.mkdir(parents=True, exist_ok=True)
    candidate = archive if archive.is_file() else archive.with_suffix('.part')
    if not archive.is_file():
        print(f'Downloading Amiga toolchain {VERSION} (200 MB)...', flush=True)
        urllib.request.urlretrieve(URL, candidate)
    with candidate.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    if digest != CHECKSUM:
        raise RuntimeError(f'Amiga SDK checksum mismatch: {candidate}')
    if candidate != archive:
        candidate.replace(archive)
    extract(archive, ROOT / '.tools/amiga-debug')
    if not all(path.is_file() for path in required):
        raise RuntimeError('Amiga SDK archive is missing required tools')
    manifest = ROOT / '.tools/downloads.json'
    records = json.loads(manifest.read_text()) if manifest.exists() else {}
    records['amiga'] = dict(version=VERSION, url=URL, sha256=digest)
    manifest.write_text(json.dumps(records, indent=2) + '\n')
    return SDK


if __name__ == '__main__':
    print(ensure_sdk())
