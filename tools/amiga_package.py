"""Package the native Amiga 500 build for sharing.

Outputs, from one build:
  Deceptus-Amiga.hdf  one bootable hard disk image (OFS, boots on Kickstart 1.3): write it to a
                      CF card or mount it in an emulator.
  Deceptus-Amiga.zip  the image plus a Windows starter that writes a WinUAE configuration for
                      wherever it is unpacked, and a readme. The user supplies their Kickstart ROM.
"""

import argparse
from pathlib import Path
import shutil
import zipfile

from amiga_hdf import write_hdf
from bootstrap import ROOT

# files the streamed renderer reads per room variant (rooms.c, loadTileGraphics and room content).
ROOM_FILES = ('.ans', '.atg', '.ant', '.asb', '.amb')
VOLUME = 'Deceptus'

STARTER = r'''@echo off
rem Writes deceptus.uae for this folder and opens it in WinUAE (a stock Amiga 500, PAL).
setlocal
set "HERE=%~dp0"
set "KICKSTART=%~1"
if "%KICKSTART%"=="" if exist "%HERE%kickstart.txt" set /p KICKSTART=<"%HERE%kickstart.txt"
if "%KICKSTART%"=="" set /p "KICKSTART=Path to your Kickstart 1.3 ROM: "
if not exist "%KICKSTART%" (
  echo Kickstart ROM not found: %KICKSTART%
  pause
  exit /b 1
)
> "%HERE%kickstart.txt" echo %KICKSTART%
> "%HERE%deceptus.uae" (
  echo config_description=Deceptus - Amiga 500
  echo use_gui=no
  echo quickstart=a500,1
  echo kickstart_rom_file=%KICKSTART%
  echo cpu_model=68000
  echo cpu_cycle_exact=true
  echo cpu_memory_cycle_exact=true
  echo blitter_cycle_exact=true
  echo chipmem_size=1
  echo bogomem_size=2
  echo fastmem_size=0
  echo ntsc=false
  echo boot_rom_uae=automatic
  echo gfx_width_windowed=752
  echo gfx_height_windowed=574
  echo gfx_resolution=hires
  echo gfx_linemode=double
  echo sound_output=exact
  echo joyport0=mouse
  echo joyport1=kbd2
  echo hardfile2=rw,DH0:%HERE%Deceptus-Amiga.hdf,__SECTORS__,__HEADS__,2,512,0,,uae0
  echo win32.start_not_captured=no
  echo input.config=0
)
if not "%WINUAE%"=="" (
  start "" "%WINUAE%" -f "%HERE%deceptus.uae"
) else (
  start "" "%HERE%deceptus.uae"
)
'''

README = '''Deceptus - Amiga 500
====================

A stock Amiga 500: 512K Chip RAM + 512K slow (trapdoor) RAM, or 1 MB Chip, Kickstart 1.3 or
later, PAL. The game is one hard disk image, Deceptus-Amiga.hdf (about 13 MB: too large for
floppies).

Real Amiga
  Write Deceptus-Amiga.hdf to a CF card or hard drive partition with your usual tools, or copy
  the files from it (mount it in an emulator) onto an existing drive. To start it from a Shell:
      cd DH0:                  (the drive or folder that holds deceptus-amiga.exe)
      deceptus-amiga.exe
  The image also boots by itself. Some hard drive controllers keep drivers and buffers in
  memory; if the game reports that it needs more memory, boot without extra resident programs.

WinUAE
  Double-click start.bat. It asks once for your Kickstart 1.3 ROM file, remembers it, writes
  deceptus.uae next to itself and opens it. If .uae files do not open WinUAE, load deceptus.uae
  in WinUAE (Configurations > Load) and press Start, or set WINUAE to the path of winuae.exe
  before running start.bat. Other emulators: mount the image as a 512-byte-block hardfile with
  __SECTORS__ sectors, __HEADS__ surface(s) and 2 reserved blocks, and boot from it.

Controls
  Cursor keys move, Z or right Ctrl jumps, Up interacts, Space attacks, Tab opens the
  inventory, Esc pauses, Return accepts. Hold both mouse buttons to quit.

This is a work-in-progress build.
'''


def game_files(build):
    """The program, a boot script and the room data the game reads, as image paths."""
    build = Path(build)
    files = {'deceptus-amiga.exe': (build / 'deceptus-amiga.exe').read_bytes(),
             # booting starts the game with the volume root as its current directory.
             's/startup-sequence': b'deceptus-amiga.exe\n'}
    assets = build / 'assets'
    for area in sorted(assets.glob('area*')):
        for path in sorted(area.iterdir()):
            if path.suffix in ROOM_FILES:
                files[f'assets/{area.name}/{path.name}'] = path.read_bytes()
    for path in sorted((assets / 'rooms').glob('room*.drm')):
        files[f'assets/rooms/{path.name}'] = path.read_bytes()
    return files


def package(build, output):
    output = Path(output)
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    image = output / 'Deceptus-Amiga.hdf'
    _, heads, sectors = write_hdf(image, game_files(build), VOLUME)
    for name, text in (('start.bat', STARTER), ('README.txt', README)):
        text = text.replace('__SECTORS__', str(sectors)).replace('__HEADS__', str(heads))
        (output / name).write_bytes(text.replace('\n', '\r\n').encode('ascii'))
    archive = output.with_suffix('.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
        for path in sorted(output.iterdir()):
            bundle.write(path, Path('Deceptus-Amiga') / path.name)
    return image, archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/amiga-native-opening')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/amiga-package/Deceptus-Amiga')
    args = parser.parse_args()
    for path in package(args.build, args.output):
        print(path)


if __name__ == '__main__':
    main()
