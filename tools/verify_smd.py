"""Check the actual Mega Drive POC against native shared C and measure frame rate."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from emulator import Emulator, ROOT

FIELDS = ['ticks', 'x', 'y', 'camera_x', 'camera_y', 'paused', 'health', 'door_entries',
          'checkpoint_id', 'content_error', 'sprite_error', 'draws', 'sprites', 'buttons',
          'vblanks', 'cpu_load', 'grounded', 'checkpoint_x', 'checkpoint_y']
SHARED = ['src/engine/player/playerwater.c', 'src/engine/player/playerrope.c',
          'src/engine/mechanisms/grabrope.c', 'src/engine/mechanisms/moveablebox.c', 'src/engine/mechanisms/crusher.c', 'src/engine/mechanisms/floorspikes.c', 'src/engine/mechanisms/spikeball.c', 'src/engine/mechanisms/checkpointclock.c', 'src/engine/arith.c', 'src/engine/animation.c', 'src/engine/world.c',
          'src/engine/entities.c', 'src/engine/events.c', 'src/engine/mechanisms.c', 'src/engine/logic.c',
          'src/game/enemies.c', 'src/game/deceptus.c']
REFERENCE_FIELDS = ['ticks', 'x', 'y', 'camera_x', 'camera_y', 'paused', 'health',
                    'door_entries', 'checkpoint_id', 'content_error', 'grounded',
                    'checkpoint_x', 'checkpoint_y']


def state(emulator):
    memory = emulator.memory()
    for endian in ('<', '>'):
        offset = memory.find(struct.pack(endian + 'HH', 0xd3ce, 0x4d44))
        if offset >= 0:
            words = struct.unpack_from(endian + '23H', memory, offset)
            if words[2] & 1 or words[2] != words[22]:
                raise AssertionError('incomplete Mega Drive debug packet')
            values = dict(zip(FIELDS, words[3:22]))
            values.update(sequence=words[2], mailbox_offset=offset)
            return values
    raise AssertionError('Mega Drive debug packet not found')


def native_reference():
    folder = ROOT / 'build/tests/smd-reference'
    folder.mkdir(parents=True, exist_ok=True)
    bridge = folder / 'reference.c'
    expressions = ['e.ticks', 'deceptusFloor(e.entities[0].x)', 'deceptusFloor(e.entities[0].y)',
                   'e.camera_x', 'e.camera_y', 'e.paused', 'e.health', 'e.door_entries',
                   'e.checkpoint_id', 'e.content_error', 'e.entities[0].grounded',
                   'e.checkpoint_x', 'e.checkpoint_y']
    bridge.write_text('#include "deceptus/game.h"\nextern const DeceptusRoom deceptus_scene;\n'
        'static DeceptusEngine e;\n'
        '__declspec(dllexport) void ref_init(void){DeceptusVideoCaps v={320,224,12};'
        'deceptusInit(&e,&deceptus_scene,&v);}\n'
        '__declspec(dllexport) void ref_tick(unsigned short b){deceptusTick(&e,b);}\n'
        '__declspec(dllexport) int ref_state(int i){switch(i){' +
        ''.join(f'case {i}:return {value};' for i, value in enumerate(expressions)) + 'default:return -1;}}\n')
    library = folder / 'reference.dll'
    subprocess.run(['clang', '-shared', '-O2', '-std=c90', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(ROOT / 'include'), str(bridge),
                    *[str(ROOT / name) for name in SHARED],
                    str(ROOT / 'assets/generated/smd/room.c'), '-o', str(library)], check=True)
    reference = C.CDLL(str(library))
    reference.ref_tick.argtypes = [C.c_ushort]
    reference.ref_state.argtypes = [C.c_int]
    reference.ref_init()
    return reference


def verify(core, rom):
    reference = native_reference()
    emulator = Emulator(core, rom)
    output = ROOT / 'artifacts/smd-check'
    output.mkdir(parents=True, exist_ok=True)
    comparisons, checkpoints = 0, []
    try:
        emulator.run(120)
        current = state(emulator)
        for _ in range(current['ticks']):
            reference.ref_tick(0)

        def compare(values):
            nonlocal comparisons
            assert not values['content_error'], values
            assert not values['sprite_error'], values
            for index, name in enumerate(REFERENCE_FIELDS):
                expected = reference.ref_state(index) & 65535
                assert values[name] == expected, (name, expected, values)
            comparisons += 1

        def advance(frames, buttons=()):
            nonlocal current
            for _ in range(frames):
                previous = current
                emulator.run(1, buttons)
                current = state(emulator)
                steps = ((current['sequence'] - previous['sequence']) & 65535) // 2
                assert steps == 1, ('missed or repeated display update', previous, current)
                if steps:
                    reference.ref_tick(current['buttons'])
                    compare(current)
            return current

        def checkpoint(name):
            checkpoints.append(dict(name=name, **current))
            emulator.screenshot(output / (name + '.png'))

        compare(current)
        checkpoint('boot')
        start_tick = current['ticks']
        loads = []
        for _ in range(600):
            advance(1)
            loads.append(current['cpu_load'])
        ticks = (current['ticks'] - start_tick) & 65535
        assert ticks == 600, ('simulation missed a display frame', ticks)
        benchmark = dict(display_frames=600, simulation_ticks=ticks,
                         updates_per_second=ticks * emulator.fps / 600,
                         cpu_load_min=min(loads), cpu_load_max=max(loads),
                         cpu_load_mean=sum(loads) / len(loads))
        checkpoint('idle')
        x = current['x']
        advance(30, (7,))
        assert current['x'] > x + 10, current
        checkpoint('walk')
        y = current['y']
        advance(10, (0,))
        assert current['y'] < y - 8, current
        checkpoint('jump')
        advance(60)
        assert current['grounded'], current
        checkpoint('land')
        advance(8, (3,))
        frozen = current['ticks']
        advance(30, (7,))
        assert current['paused'] and current['ticks'] == frozen, current
        checkpoint('pause')
        advance(8, (3,))
        advance(10)
        assert not current['paused'] and current['ticks'] != frozen, current
        advance(8, (1,))
        advance(5)
        assert current['x'] == 112, current
        checkpoint('restart')
        advance(86, (7,))
        advance(50, (7, 0))
        checkpoint('stomp-route')
        advance(48, (7,))
        advance(30, (7, 0))
        advance(15, (7,))
        assert current['x'] > 440 and current['grounded'] and current['y'] == 168, current
        assert current['camera_x'] > 0, current
        checkpoint('ledge-scroll')
        advance(12, (4,))
        assert current['checkpoint_id'] == 1058, current
        assert current['door_entries'] == 1 and current['x'] == current['checkpoint_x'], current
        checkpoint('checkpoint-return')
        report = dict(scope='one-room shared-engine POC; not the 26-room opening',
                      rom_sha256=hashlib.sha256(Path(rom).read_bytes()).hexdigest(),
                      emulator=emulator.version, comparisons=comparisons,
                      benchmark=benchmark, checkpoints=checkpoints, video_frames=emulator.video_frames,
                      frame_pacing=dict(display_frames=emulator.video_frames - 120,
                                        checked_updates=comparisons - 1, missed_updates=0))
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        metadata = Path(rom).parent / 'build.json'
        if metadata.is_file():
            build = json.loads(metadata.read_text())
            if build.get('rom_sha256') == report['rom_sha256']:
                build.update(emulator_checked=True, integration_report='artifacts/smd-check/report.json',
                             verified_updates=comparisons - 1, benchmark=benchmark)
                metadata.write_text(json.dumps(build, indent=2) + '\n')
        print(json.dumps(report, indent=2))
        return report
    finally:
        emulator.screenshot(output / 'last.png')
        emulator.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=ROOT / '.tools/genesis_plus_gx/genesis_plus_gx_libretro.dll')
    parser.add_argument('--rom', type=Path, default=ROOT / 'build/smd/deceptus-poc.bin')
    args = parser.parse_args()
    verify(args.core, args.rom)
