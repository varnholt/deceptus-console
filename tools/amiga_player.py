"""Prepare and launch the one-room Amiga 500 prototype."""

import argparse
from pathlib import Path
import subprocess
import sys

from bootstrap import ROOT
from bootstrap_amiga import ensure_sdk
from run_game import ensure_python


def prepare_drive(executable, output):
    drive = output / 'drive'
    (drive / 's').mkdir(parents=True, exist_ok=True)
    # AmigaDOS treats CR bytes in Windows text files as filename characters.
    (drive / 's/startup-sequence').write_bytes(
        ('Build:' + executable.name + '\n').encode('ascii'))
    return drive


def write_config(executable, output, kickstart=None, slowmem=512, scene="prototype"):
    drive = prepare_drive(executable, output)
    settings = dict(
        config_description=('Deceptus Amiga 500 opening' if scene == 'tutorial'
                            else 'Deceptus Amiga 500 one-room prototype'),
        use_gui='no', quickstart='a500,1',
        kickstart_rom_file=str(kickstart) if kickstart else ':AROS',
        cpu_model='68000', cpu_cycle_exact='true',
        cpu_memory_cycle_exact='true', blitter_cycle_exact='true',
        chipmem_size=1, bogomem_size=2 if slowmem else 0, fastmem_size=0,
        ntsc='false', boot_rom_uae='automatic',
        gfx_width_windowed=752, gfx_height_windowed=574,
        gfx_resolution='hires', gfx_linemode='double',
        sound_output='interrupts', joyport0='mouse', joyport1='kbd2',
        filesystem=f'rw,dh0:{drive}',
        filesystem2=f'rw,DH1:Build:{executable.parent},-128',
    )
    config = output / 'deceptus-amiga.uae'
    lines = [f'{key}={value}' for key, value in settings.items()]
    lines += ['win32.start_not_captured=no',
              'win32.active_not_captured_pause=false',
              'win32.inactive_pause=false', 'input.config=0']
    config.write_text('\n'.join(lines) + '\n')
    return config


def main(arguments=None):
    arguments = list(sys.argv[1:] if arguments is None else arguments)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--scene', choices=('prototype', 'tutorial'), default='prototype')
    parser.add_argument('--reuse-assets', action='store_true')
    parser.add_argument('--sdk', type=Path)
    parser.add_argument('--data', type=Path, default=ROOT / 'assets/game/data')
    parser.add_argument('--kickstart', type=Path,
                        help='use your Kickstart 1.3 ROM instead of built-in AROS')
    parser.add_argument('--slowmem', type=int, choices=[0, 512], default=512,
                        help='A500 expansion RAM in KB (default: 512)')
    args = parser.parse_args(arguments)
    if args.kickstart and not args.kickstart.is_file():
        parser.error('Kickstart ROM file does not exist')
    output = ROOT / ('build/amiga-opening' if args.scene == 'tutorial' else 'build/amiga')
    executable = output / 'deceptus-amiga.exe'
    building = args.build or not executable.is_file()
    result = ensure_python('amiga', arguments, building)
    if result is not None:
        return result
    sdk = args.sdk.resolve() if args.sdk else ensure_sdk()
    if building:
        if args.scene == 'tutorial':
            from build_amiga_opening import build_opening
            build_opening(sdk, args.data.resolve(), output, args.reuse_assets)
        else:
            from build_amiga import build
            build(sdk, args.data.resolve(), output)
    kickstart = args.kickstart.resolve() if args.kickstart else None
    config = write_config(executable, output, kickstart, args.slowmem, args.scene)
    print(f'Executable: {executable}', flush=True)
    if args.prepare:
        return 0
    print('Cursor keys move, Z or right Ctrl jumps. Hold both mouse buttons to exit.',
          flush=True)
    if args.scene == 'tutorial':
        print('Up interacts, Space attacks, Tab opens inventory, Esc pauses/backs, Return accepts.', flush=True)
    return subprocess.call([str(sdk / 'winuae-gdb.exe'), '-f', str(config)],
                           cwd=output)
