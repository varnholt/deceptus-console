"""Install the pinned, project-local SGDK and optional Windows emulator."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from bootstrap import ROOT, download, extract

SDK_URL = 'https://github.com/Stephane-D/SGDK/releases/download/v2.11/sgdk211.7z'
SDK_SHA256 = '5cc704b7e3a15183c33e721a1d7f84c067cf78808754556d03bb14764df51437'


def ensure(emulator=False, sdk_needed=True):
    tools = ROOT / '.tools'
    tools.mkdir(exist_ok=True)
    sdk = tools / 'sgdk'
    if sdk_needed and not (sdk / 'bin/gcc.exe').is_file():
        archive = tools / 'sgdk211.7z'
        if not archive.is_file():
            download(SDK_URL, archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != SDK_SHA256:
            raise ValueError('SGDK archive does not match the pinned checksum')
        unpacker = shutil.which('7z') or shutil.which('7za')
        if not unpacker and Path('C:/Program Files/7-Zip/7z.exe').is_file():
            unpacker = 'C:/Program Files/7-Zip/7z.exe'
        if unpacker:
            subprocess.run([unpacker, 'x', str(archive), '-o' + str(sdk), '-y'], check=True)
        else:
            try:
                import py7zr
            except ImportError:
                raise SystemExit('SGDK extraction needs 7-Zip or py7zr; install 7-Zip or run python -m pip install py7zr')
            with py7zr.SevenZipFile(archive) as source:
                source.extractall(sdk)
    core = tools / 'genesis_plus_gx/genesis_plus_gx_libretro.dll'
    record = ({'sgdk': {'version': '2.11', 'url': SDK_URL, 'sha256': SDK_SHA256}}
              if (sdk / 'bin/gcc.exe').is_file() else {})
    if emulator and not core.is_file():
        url = 'https://buildbot.libretro.com/nightly/windows/x86_64/latest/genesis_plus_gx_libretro.dll.zip'
        archive = tools / 'genesis_plus_gx_libretro.zip'
        checksum = download(url, archive)
        extract(archive, core.parent)
        record['genesis_plus_gx'] = {'url': url, 'sha256': checksum, 'channel': 'nightly; cached until removed'}
    if emulator and core.is_file():
        record.setdefault('genesis_plus_gx', {})['binary_sha256'] = hashlib.sha256(core.read_bytes()).hexdigest()
    manifest = tools / 'smd-downloads.json'
    previous = json.loads(manifest.read_text()) if manifest.is_file() else {}
    previous.update(record)
    manifest.write_text(json.dumps(previous, indent=2) + '\n')
    return sdk, core


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator', action='store_true')
    ensure(parser.parse_args().emulator)
