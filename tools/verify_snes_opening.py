"""Replay original opening inputs against SNES content and the actual cartridge."""
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
from verify_opening import ROUTE, check, route_steps, native_state as opening_native_state

# like real hardware, video memory ignores writes outside vblank and forced blank; a renderer that
# writes too late then shows the error here as well.
SNES_CORE_OPTIONS = {'snes9x_block_invalid_vram_access': 'enabled'}


def native_state(library):
    state = opening_native_state(library)
    state['projectiles_fired'] = library.openingState(15)
    state['hurt_ticks'] = library.openingState(14)
    return state


def compile_native(clang):
    compiler = shutil.which(clang)
    if not compiler:
        raise RuntimeError(f'{clang} not found')
    generated = ROOT / 'assets/generated/snes'
    content = [generated / f'opening_area{index}.c' for index in range(26)]
    content.append(generated / 'opening_adventure.c')
    missing = [str(path) for path in content if not path.is_file()]
    if missing:
        raise RuntimeError('build --scene tutorial SNES content first; missing ' + ', '.join(missing))
    sources = (sorted((ROOT / 'src/engine').glob('*.c')) +
               sorted((ROOT / 'src/engine/player').glob('*.c')) +
               [ROOT / 'src/engine/mechanisms/grabrope.c', ROOT / 'src/engine/mechanisms/moveablebox.c', ROOT / 'src/engine/mechanisms/crusher.c', ROOT / 'src/engine/mechanisms/floorspikes.c', ROOT / 'src/engine/mechanisms/spikeball.c'] +
               sorted(path for path in (ROOT / 'src/game').rglob('*.c')
                      if path.parent != ROOT / 'src/game/enemies'))
    folder = ROOT / 'build/tests/snes-opening'
    folder.mkdir(parents=True, exist_ok=True)
    library = folder / ('opening-' + uuid4().hex + ('.dll' if sys.platform == 'win32' else '.so'))
    flags = ['-shared', '-O2', '-std=c90', '-Wall', '-Wextra', '-Werror', '-pedantic',
             '-Dgba_tutorial_definition=snes_tutorial_definition',
             '-DOPENING_VIDEO_WIDTH=256', '-DOPENING_VIDEO_HEIGHT=224']
    if sys.platform != 'win32':
        flags.append('-fPIC')
    subprocess.run([compiler, *flags, '-I' + str(ROOT / 'include'), '-I' + str(generated),
                    str(ROOT / 'tests/opening_traversal_tests.c'), *map(str, content + sources),
                    '-o', str(library)], check=True)
    return C.CDLL(str(library))


def snes_state(emulator):
    memory = emulator.memory()
    marker = getattr(emulator, '_snes_debug_offset', None)
    if marker is None:
        marker = memory.find(b'\xce\xd3')
        while marker >= 0:
            candidate = struct.unpack_from('<56H', memory, marker)
            if 0 < candidate[11] <= 19 and candidate[28] < 32 and candidate[29] <= 11:
                emulator._snes_debug_offset = marker
                break
            marker = memory.find(b'\xce\xd3', marker + 2)
    if marker is None or marker < 0 or memory[marker:marker + 2] != b'\xce\xd3':
        raise AssertionError('SNES opening debug mailbox missing')
    values = struct.unpack_from('<56H', memory, marker)
    return dict(room=values[28], x=values[1], y=values[2], camera_x=values[3], camera_y=values[16], mode=values[29],
                flags=values[36], health=values[6], deaths=values[30],
                content_error=values[15], has_sword=values[51], ticks=values[4], page=values[39],
                inventory=[0, values[37], values[38], values[46], values[47], values[48], values[49]],
                rope_attached=values[53], background_error=values[17], ui_overflow=values[20],
                sprite_overflow=values[21], sprite_upload_bytes=values[22], objects=values[23],
                object_palettes=values[24], sprite_blank=values[25], phase=values[11],
                engine_buttons=values[26], raw_pad=values[27],
                velocity_x=values[40] if values[40] < 32768 else values[40] - 65536,
                velocity_y=values[41] if values[41] < 32768 else values[41] - 65536,
                raw_x=values[42], raw_y=values[43], campaign_buttons=values[44], mailbox_offset=marker,
                projectiles_fired=values[32])


def retro_buttons(buttons):
    mapping = {1: 6, 2: 7, 4: 0, 8: 4, 16: 3, 32: 2, 64: 5, 128: 10, 256: 11, 512: 1}
    return [button for flag, button in mapping.items() if buttons & flag]


def verify(core=None, rom=None, route_path=ROUTE, clang='clang', milestone='full'):
    route = json.loads(Path(route_path).read_text())
    checkpoints = {point['step']: point for point in route['checkpoints']}
    last_step = (next(point['step'] for point in route['checkpoints'] if point['expected']['room'] == 1)
                 if milestone == 'boot' else len(route['steps']) - 1)
    steps = route_steps(route, 'snes')[:last_step + 1]
    library = compile_native(clang)
    library.openingInit()
    library.openingTick(16)
    for _ in range(60):
        library.openingTick(0)
    idle_expected = native_state(library)
    library.openingInit()
    report = dict(scope=route['scope'] if milestone == 'full' else 'New Game, original Cell and live Lower Cells portal',
                  route_sha256=hashlib.sha256(Path(route_path).read_bytes()).hexdigest(),
                  input_ticks=sum(ticks for ticks, _ in steps), native=[], snes=[],
                  native_damage_events=[], snes_damage_events=[])
    trace = []
    previous_native_health = library.openingState(5)
    for index, (ticks, buttons) in enumerate(steps):
        for _ in range(ticks):
            library.openingTick(buttons)
            health = library.openingState(5)
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
    report['simulation_ticks'] = library.openingState(12)
    report['native_rope_attached_ticks'] = library.openingState(13)
    output = ROOT / 'artifacts/snes-opening-check'
    output.mkdir(parents=True, exist_ok=True)
    if core:
        emulator = Emulator(core, rom, SNES_CORE_OPTIONS)
        report['rom_sha256'] = hashlib.sha256(Path(rom).read_bytes()).hexdigest()
        report['extra_display_frames'] = 0
        report['sprite_blank_observations'] = 0
        try:
            emulator.run(120)
            for _ in range(3600):
                try:
                    initial = snes_state(emulator)
                except AssertionError:
                    initial = None
                if initial and initial['health'] and initial['phase'] >= 15:
                    break
                emulator.run(1)
            else:
                emulator.screenshot(output / 'startup-failure.png')
                raise AssertionError(('SNES startup did not finish', initial))
            # publication precedes the forced-blank startup presentation commit.
            emulator.run(5)
            initial = snes_state(emulator)
            assert initial['room'] == 0 and initial['mode'] == 0, ('SNES must boot at New Game title', initial)
            report['startup_display_frames'] = emulator.video_frames
            emulator.screenshot(output / 'title.png')
            # a positive rightward route must not conceal phantom controller input.
            for _ in range(3600):
                emulator.run(1, [3])
                current = snes_state(emulator)
                if current['mode'] == 1:
                    break
            else:
                raise AssertionError(('SNES New Game did not start', current))
            for _ in range(12000):
                emulator.run(1)
                current = snes_state(emulator)
                if current['mode'] != 1 or any(current[key] != idle_expected[key] for key in ('room', 'x', 'y')):
                    emulator.screenshot(output / 'phantom-input-failure.png')
                    raise AssertionError(('SNES moved without controller input', idle_expected, current))
                if current['ticks'] >= idle_expected['ticks']:
                    break
            else:
                raise AssertionError(('SNES idle simulation did not advance', current))
            check(current, {key: idle_expected[key] for key in ('flags', 'health', 'deaths')}, 'idle after New Game')
            report['idle_check'] = current
            # restart the cartridge normally so enemy clocks and the replay begin fresh.
            emulator.core.retro_reset()
            for _ in range(3600):
                emulator.run(1)
                try:
                    current = snes_state(emulator)
                except AssertionError:
                    current = None
                if current and current['mode'] == 0 and current['health'] and current['phase'] >= 15:
                    break
            else:
                raise AssertionError(('SNES reset did not return to title', current))
            emulator.run(5)
            previous_snes_health = snes_state(emulator)['health']
            # one display frame is the minimum observation interval; actual simulation
            # progress governs input changes when the 65816 renderer misses a deadline.
            for index, (ticks, buttons) in enumerate(steps):
                target = trace[index]
                expected_buttons = buttons | (256 if buttons & 4 else 0)
                for elapsed in range(ticks * 120 + 600):
                    emulator.run(1, retro_buttons(buttons))
                    current = snes_state(emulator)
                    # the 65816 may span a display boundary while writing the mailbox.
                    # phase15 starts only after the complete packet has been published.
                    if current['phase'] < 15:
                        continue
                    if current['health'] != previous_snes_health:
                        event = dict(step=index, damage=previous_snes_health - current['health'], **current)
                        report['snes_damage_events'].append(event)
                        previous_snes_health = current['health']
                        print('SNES health', current['room'], current['ticks'], current['health'],
                              'damage', event['damage'], flush=True)
                    if any(current[key] for key in ('content_error', 'background_error', 'ui_overflow', 'sprite_overflow')):
                        emulator.screenshot(output / f'failure-{index}.png')
                        (output / f'failure-{index}-memory.bin').write_bytes(emulator.memory())
                    assert not current['content_error'], ('SNES content error', index, current)
                    assert not current['background_error'], ('SNES background error', index, current)
                    assert not current['ui_overflow'], ('SNES text overflow', index, current)
                    assert not current['sprite_overflow'], ('SNES sprite budget error', index, current)
                    report['sprite_blank_observations'] += int(bool(current['sprite_blank']))
                    if (current['campaign_buttons'] == expected_buttons and
                            all(current[key] == target[key] for key in
                                ('room', 'mode', 'ticks', 'page', 'flags', 'inventory', 'has_sword'))):
                        check(current, {key: target[key] for key in
                                        ('x', 'y', 'projectiles_fired', 'health', 'deaths')},
                              f'SNES input segment {index}')
                        break
                else:
                    emulator.screenshot(output / f'failure-{index}.png')
                    raise AssertionError(('SNES simulation diverged', index, buttons, target, current))
                report['extra_display_frames'] += max(0, elapsed + 1 - ticks)
                if index in checkpoints:
                    point = checkpoints[index]
                    emulator.screenshot(output / (point['name'] + '.png'))
                    check(current, point['expected'], point['name'])
                    report['snes'].append(dict(name=point['name'], display_frames=emulator.video_frames, **current))
                    print('SNES checkpoint', point['name'], 'room', current['room'],
                          'health', current['health'], 'display frames', emulator.video_frames, flush=True)
            for settled in range(3600):
                emulator.run(1)
                current = snes_state(emulator)
                # phase 15 is written after the previous presentation's commits;
                # the commit phases themselves now finish inside VBlank, unobserved.
                if current['phase'] >= 15 and emulator.last_image.getbbox():
                    break
            else:
                raise AssertionError(('SNES final presentation did not finish', current))
            report['presentation_settle_frames'] = settled + 1
            report['presented_final'] = snes_state(emulator)
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
    (output / f'{milestone}-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=ROOT / '.tools/snes9x/snes9x_libretro.dll')
    parser.add_argument('--rom', type=Path, default=ROOT / 'build/snes/deceptus-tutorial.sfc')
    parser.add_argument('--route', type=Path, default=ROUTE)
    parser.add_argument('--clang', default='clang')
    parser.add_argument('--native-only', action='store_true')
    parser.add_argument('--milestone', choices=['boot', 'full'], default='full')
    args = parser.parse_args()
    verify(None if args.native_only else args.core, args.rom, args.route, args.clang, args.milestone)
