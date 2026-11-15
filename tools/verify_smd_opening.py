"""Replay original opening inputs against SMD content and the actual cartridge."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from uuid import uuid4

from emulator import Emulator, ROOT
from verify_opening import ROUTE, check, native_state as opening_native_state


def native_state(library):
    state = opening_native_state(library)
    state['projectiles_fired'] = library.opening_state(15)
    state['hurt_ticks'] = library.opening_state(14)
    return state


def compile_native(clang,assets_dir=None):
    compiler = shutil.which(clang)
    if not compiler:
        raise RuntimeError(f'{clang} not found')
    generated = Path(assets_dir).resolve() if assets_dir else ROOT / 'build/smd-opening-codec-assets'
    content = [generated / f'opening_area{index}.c' for index in range(26)]
    content.append(generated / 'opening_adventure.c')
    missing = [str(path) for path in content if not path.is_file()]
    if missing:
        raise RuntimeError('build --scene tutorial SMD content first; missing ' + ', '.join(missing))
    sources = (sorted((ROOT / 'src/engine').glob('*.c')) +
               sorted((ROOT / 'src/engine/player').glob('*.c')) +
               [ROOT / 'src/engine/mechanisms/grabrope.c'] +
               sorted((ROOT / 'src/game').rglob('*.c')))
    folder = ROOT / 'build/tests/smd-opening'
    folder.mkdir(parents=True, exist_ok=True)
    library = folder / ('opening-' + uuid4().hex + ('.dll' if sys.platform == 'win32' else '.so'))
    flags = ['-shared', '-O2', '-std=c90', '-Wall', '-Wextra', '-Werror', '-pedantic',
             '-Dgba_tutorial_definition=smd_tutorial_definition',
             '-DOPENING_VIDEO_WIDTH=320', '-DOPENING_VIDEO_HEIGHT=224']
    if sys.platform != 'win32':
        flags.append('-fPIC')
    subprocess.run([compiler, *flags, '-I' + str(ROOT / 'include'), '-I' + str(generated),
                    str(ROOT / 'tests/opening_traversal_tests.c'), *map(str, content + sources),
                    '-o', str(library)], check=True)
    return C.CDLL(str(library))


def smd_state(emulator):
    memory = emulator.memory()
    marker = memory.find(struct.pack('<HH', 0xd3ce, 0x4d4f))
    packets = []
    while marker >= 0:
        v = struct.unpack_from('<56H', memory, marker)
        if not (v[2] & 1) and v[2] == v[55] and v[8] < 26 and v[9] <= 11:
            packets.append((marker, v))
        marker = memory.find(struct.pack('<HH', 0xd3ce, 0x4d4f), marker + 2)
    if not packets:
        raise AssertionError('Mega Drive opening complete mailbox missing')
    marker, v = packets[0]
    for candidate, words in packets[1:]:
        if 0 < ((words[2] - v[2]) & 65535) < 32768:
            marker, v = candidate, words
    return dict(room=v[8], x=v[4], y=v[5], camera_x=v[6], camera_y=v[7], mode=v[9], flags=v[10],
                health=v[11], deaths=v[12], content_error=v[13], has_sword=v[18], ticks=v[3], page=v[19],
                inventory=list(v[40:47]), rope_attached=v[28], background_error=v[14], graphics_error=v[15],
                sprite_overflow=v[16], ui_overflow=v[17], objects=v[22], sprite_upload_bytes=v[24],
                sprite_blank=v[38], phase=19, engine_buttons=v[20], campaign_buttons=v[21],
                projectiles_fired=v[30], mailbox_offset=marker, sequence=v[2], cpu_load=v[27])


def retro_buttons(buttons):
    mapping = {1: 6, 2: 7, 4: 0, 8: 4, 16: 3, 32: 2, 64: 5, 128: 10, 256: 11, 512: 1}
    return [button for flag, button in mapping.items() if buttons & flag]


def verify(core=None, rom=None, route_path=ROUTE, clang='clang', milestone='full',assets_dir=None):
    route = json.loads(Path(route_path).read_text())
    checkpoints = {point['step']: point for point in route['checkpoints']}
    last_step = (next(point['step'] for point in route['checkpoints'] if point['expected']['room'] == 1)
                 if milestone == 'boot' else len(route['steps']) - 1)
    steps = route['steps'][:last_step + 1]
    library = compile_native(clang,assets_dir)
    library.opening_init()
    library.opening_tick(16)
    for _ in range(60):
        library.opening_tick(0)
    idle_expected = native_state(library)
    library.opening_init()
    report = dict(scope=route['scope'] if milestone == 'full' else 'New Game, original Cell and live Lower Cells portal',
                  route_sha256=hashlib.sha256(Path(route_path).read_bytes()).hexdigest(),
                  input_ticks=sum(ticks for ticks, _ in steps), native=[], smd=[],
                  native_damage_events=[], smd_damage_events=[])
    trace = []
    previous_native_health = library.opening_state(5)
    for index, (ticks, buttons) in enumerate(steps):
        for _ in range(ticks):
            library.opening_tick(buttons)
            health = library.opening_state(5)
            if health != previous_native_health:
                report['native_damage_events'].append(dict(step=index, damage=previous_native_health - health,
                                                           **native_state(library)))
                previous_native_health = health
        current = native_state(library)
        trace.append(current)
        if index in checkpoints:
            point = checkpoints[index]
            check(current, point['expected'], point['name'])
            report['native'].append(dict(name=point['name'], **current))
    report['simulation_ticks'] = library.opening_state(12)
    report['native_rope_attached_ticks'] = library.opening_state(13)
    output = ROOT / 'artifacts/smd-opening-check'
    output.mkdir(parents=True, exist_ok=True)
    if core:
        emulator = Emulator(core, rom)
        emulator.core.retro_set_controller_port_device(0, 513)
        report['rom_sha256'] = hashlib.sha256(Path(rom).read_bytes()).hexdigest()
        report['extra_display_frames'] = 0
        report['sprite_blank_observations'] = 0
        try:
            emulator.run(120)
            for _ in range(3600):
                try:
                    initial = smd_state(emulator)
                except AssertionError:
                    initial = None
                if initial and initial['health'] and initial['phase'] >= 15:
                    break
                emulator.run(1)
            else:
                emulator.screenshot(output / 'startup-failure.png')
                raise AssertionError(('SMD startup did not finish', initial))
            # publication precedes the forced-blank startup presentation commit.
            emulator.run(5)
            initial = smd_state(emulator)
            assert initial['room'] == 0 and initial['mode'] == 0, ('SMD must boot at New Game title', initial)
            report['startup_display_frames'] = emulator.video_frames
            emulator.screenshot(output / 'title.png')
            # a positive rightward route must not conceal phantom controller input.
            for _ in range(3600):
                emulator.run(1, [3])
                current = smd_state(emulator)
                if current['mode'] == 1:
                    break
            else:
                raise AssertionError(('SMD New Game did not start', current))
            for _ in range(12000):
                emulator.run(1)
                current = smd_state(emulator)
                if current['mode'] != 1 or any(current[key] != idle_expected[key] for key in ('room', 'x', 'y')):
                    emulator.screenshot(output / 'phantom-input-failure.png')
                    raise AssertionError(('SMD moved without controller input', idle_expected, current))
                if current['ticks'] >= idle_expected['ticks']:
                    break
            else:
                raise AssertionError(('SMD idle simulation did not advance', current))
            check(current, {key: idle_expected[key] for key in ('flags', 'health', 'deaths')}, 'idle after New Game')
            report['idle_check'] = current
            # restart the cartridge normally so enemy clocks and the replay begin fresh.
            emulator.core.retro_reset()
            for _ in range(3600):
                emulator.run(1)
                try:
                    current = smd_state(emulator)
                except AssertionError:
                    current = None
                if current and current['mode'] == 0 and current['health'] and current['phase'] >= 15:
                    break
            else:
                raise AssertionError(('SMD reset did not return to title', current))
            emulator.run(5)
            previous_smd_health = smd_state(emulator)['health']
            # one display frame is the minimum observation interval; actual simulation
            # progress governs input changes when the 65816 renderer misses a deadline.
            for index, (ticks, buttons) in enumerate(steps):
                target = trace[index]
                expected_buttons = buttons
                for elapsed in range(ticks * 120 + 600):
                    emulator.run(1, retro_buttons(buttons))
                    current = smd_state(emulator)
                    # the 65816 may span a display boundary while writing the mailbox.
                    # phase15 starts only after the complete packet has been published.
                    if current['phase'] < 15:
                        continue
                    if current['health'] != previous_smd_health:
                        event = dict(step=index, damage=previous_smd_health - current['health'], **current)
                        report['smd_damage_events'].append(event)
                        previous_smd_health = current['health']
                        print('SMD health', current['room'], current['ticks'], current['health'],
                              'damage', event['damage'], flush=True)
                    if any(current[key] for key in ('content_error', 'background_error', 'graphics_error', 'ui_overflow', 'sprite_overflow')):
                        emulator.screenshot(output / f'failure-{index}.png')
                        (output / f'failure-{index}-memory.bin').write_bytes(emulator.memory())
                    assert not current['content_error'], ('SMD content error', index, current)
                    assert not current['graphics_error'], ('SMD graphics codec error', index, current)
                    assert not current['background_error'], ('SMD background error', index, current)
                    assert not current['ui_overflow'], ('SMD text overflow', index, current)
                    assert not current['sprite_overflow'], ('SMD sprite budget error', index, current)
                    report['sprite_blank_observations'] += int(bool(current['sprite_blank']))
                    if (current['campaign_buttons'] == expected_buttons and
                            all(current[key] == target[key] for key in
                                ('room', 'mode', 'ticks', 'page', 'flags', 'inventory', 'has_sword'))):
                        check(current, {key: target[key] for key in
                                        ('x', 'y', 'projectiles_fired', 'health', 'deaths')},
                              f'SMD input segment {index}')
                        break
                else:
                    emulator.screenshot(output / f'failure-{index}.png')
                    raise AssertionError(('SMD simulation diverged', index, buttons, target, current))
                report['extra_display_frames'] += max(0, elapsed + 1 - ticks)
                if index in checkpoints:
                    point = checkpoints[index]
                    emulator.screenshot(output / (point['name'] + '.png'))
                    check(current, point['expected'], point['name'])
                    report['smd'].append(dict(name=point['name'], display_frames=emulator.video_frames, **current))
                    print('SMD checkpoint', point['name'], 'room', current['room'],
                          'health', current['health'], 'display frames', emulator.video_frames, flush=True)
            for settled in range(3600):
                emulator.run(1)
                current = smd_state(emulator)
                if current['phase'] >= 18 and emulator.last_image.getbbox():
                    break
            else:
                raise AssertionError(('SMD final presentation did not finish', current))
            report['presentation_settle_frames'] = settled + 1
            report['presented_final'] = smd_state(emulator)
            emulator.screenshot(output / f'{milestone}-presented.png')
            report.update(emulator=emulator.version, video_frames=emulator.video_frames)
        except Exception as error:
            report['failure'] = str(error)
            report['last_state'] = current if 'current' in locals() else None
            report['video_frames'] = emulator.video_frames
            emulator.screenshot(output / f'{milestone}-failure.png')
            (output / f'{milestone}-failure-memory.bin').write_bytes(emulator.memory())
            (output / f'{milestone}-failure-report.json').write_text(json.dumps(report, indent=2) + '\n')
            raise
        finally:
            emulator.close()
    report_name=f'{milestone}-report.json' if core else f'{milestone}-native-report.json'
    (output / report_name).write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=ROOT / '.tools/genesis_plus_gx/genesis_plus_gx_libretro.dll')
    parser.add_argument('--rom', type=Path, default=ROOT / 'build/smd/deceptus-tutorial.bin')
    parser.add_argument('--route', type=Path, default=ROUTE)
    parser.add_argument('--clang', default='clang')
    parser.add_argument('--native-only', action='store_true')
    parser.add_argument('--milestone', choices=['boot', 'full'], default='full')
    parser.add_argument('--assets-dir',type=Path,default=ROOT/'assets/generated/smd-opening')
    args = parser.parse_args()
    verify(None if args.native_only else args.core, args.rom, args.route, args.clang, args.milestone,args.assets_dir)
