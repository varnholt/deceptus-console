"""Controlled-start SNES rendering checks, separate from the normal-input route."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import sys

import build_snes
from emulator import Emulator, ROOT
from verify_snes_opening import snes_state

FIXTURES = {
    'altar-release-long': (7, 536, 294, 'adventure.flags=7583;adventure.inventory.counts[5]=1;', True),
    'altar-release': (7, 536, 294, 'adventure.flags=7583;adventure.inventory.counts[5]=1;', True),
    'crypt': (25, 597, 264, 'adventure.flags=7327;adventure.inventory.counts[3]=1;adventure.inventory.counts[5]=1;', True),
    'scroll-loop': (9, 370, 696, 'adventure.flags=31;', True),
    'loop': (9, 370, 696, 'adventure.flags=31;', True),
    'ward': (7, 540, 216, '', True),
    'torch': (4, 400, 288, 'adventure.flags=2048;adventure.inventory.counts[5]=1;', True),
    'rope': (22, 396, 288, 'adventure.flags=2944;adventure.inventory.counts[5]=1;'
             'adventure.inventory.counts[6]=1;adventure.has_sword=1;', False),
}


def verify(name, assets, sdk, core):
    room, x, y, progress, freeze = FIXTURES[name]
    folder = ROOT / 'build/tests/snes-scenes' / name
    folder.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'src/platform/snes/main.c'
    scratch = folder / 'main.c'
    code = source.read_text()
    marker = 'deceptus_adventure_init(&adventure, &snes_tutorial_definition, &video);'
    assert code.count(marker) == 1
    setup = (f'adventure.campaign.room_index={room};'
             f'deceptus_init(&adventure.campaign.engine,snes_campaign_definition.rooms[{room}],&video);'
             f'deceptus_place_player(&adventure.campaign.engine,{x},{y});'
             'adventure.campaign.mode=DECEPTUS_CAMPAIGN_PLAYING;' + progress)
    code = code.replace(marker, marker + setup)
    if freeze:
        render = 'deceptus_render(&adventure.campaign.engine, &frame);'
        assert code.count(render) == 1
        position = str(x) if name != 'scroll-loop' else f'{x}+(adventure.campaign.engine.ticks<12?adventure.campaign.engine.ticks:12)'
        extra = ''
        if name.startswith('altar-release'):
            extra = ('adventure.campaign.engine.mechanisms.states['
                     'deceptus_mechanism_find(&adventure.campaign.engine.mechanisms,1386)].enabled='
                     f'adventure.campaign.engine.ticks<{312 if name.endswith("-long") else 12};')
        code = code.replace(render, f'deceptus_place_player(&adventure.campaign.engine,{position},{y});' + extra + render)
    completed = 'deceptus_debug[11] = 19;'
    assert code.count(completed) == 1
    code = code.replace(completed, '++deceptus_debug[45];' + completed)
    scratch.write_text(code)
    original_run = build_snes.run

    def run(command, working_directory):
        if '-c' in command and source in command:
            command = [scratch if argument == source else argument for argument in command]
            command.append('-I' + str(source.parent))
        original_run(command, working_directory)

    build_snes.run = run
    try:
        build_snes.build(sdk, ROOT.parent / 'deceptus_engine/data', scene='tutorial', rooms=26,
                         reuse_assets=True, rom_mib=8, assets_dir=assets, output_dir=folder)
    finally:
        build_snes.run = original_run
    output = ROOT / 'artifacts/snes-scenes'
    output.mkdir(parents=True, exist_ok=True)
    emulator = Emulator(core, folder / 'deceptus-tutorial.sfc')
    values = None
    try:
        for tick in range(60000 if name.endswith('-long') else 12000):
            emulator.run(1)
            try:
                values = snes_state(emulator)
            except AssertionError:
                continue
            assert not values['content_error'], values
            assert not values['background_error'], values
            assert not values['sprite_overflow'], values
            assert not values['ui_overflow'], values
            presented = struct.unpack_from('<H', emulator.memory(), values['mailbox_offset'] + 90)[0]
            if presented >= (318 if name.endswith('-long') else 18 if name in ('scroll-loop', 'altar-release') else 12):
                break
        else:
            raise AssertionError(('controlled scene did not finish presenting', values))
        emulator.screenshot(output / (name + '.png'))
        if name == 'rope':
            assert values['rope_attached'], values
        report = dict(name=name, state=values, display_frames=emulator.video_frames,
                      rom_sha256=hashlib.sha256((folder / 'deceptus-tutorial.sfc').read_bytes()).hexdigest(),
                      scope='controlled rendering fixture; not a normal-input progression check')
        (output / (name + '.json')).write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
        return report
    except Exception:
        (output / (name + '-failure-memory.bin')).write_bytes(emulator.memory())
        raise
    finally:
        emulator.screenshot(output / (name + '-last.png'))
        emulator.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', choices=FIXTURES, required=True)
    parser.add_argument('--assets-dir', type=Path, default=ROOT / 'build/snes-quad-assets')
    parser.add_argument('--sdk', type=Path, default=ROOT / '.tools/pvsneslib')
    parser.add_argument('--core', type=Path, default=ROOT / '.tools/snes9x/snes9x_libretro.dll')
    args = parser.parse_args()
    verify(args.fixture, args.assets_dir.resolve(), args.sdk.resolve(), args.core.resolve())
