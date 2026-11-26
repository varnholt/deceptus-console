"""Replay the opening using ordinary inputs on native C and the production GBA ROM.

Requires an existing --scene tutorial build. No player positions, inventory,
mechanisms, ROM bytes or emulator memory are changed by this verifier.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

from emulator import Emulator, ROOT

ROUTE = ROOT / 'tests/fixtures/opening-route.json'


def route_steps(route, platform):
    """The route's input steps with a platform's replacements. A platform whose content differs
    (the SNES grab rope has fewer nodes) may replace individual steps; the step count stays the same,
    so every checkpoint keeps its step."""
    steps = [list(step) for step in route['steps']]
    for index, step in route.get('platform_steps', {}).get(platform, {}).items():
        steps[int(index)] = list(step)
    return steps
FIELDS = {'room': 0, 'x': 1, 'y': 2, 'mode': 3, 'flags': 4,
          'health': 5, 'deaths': 6, 'content_error': 7, 'has_sword': 8, 'ticks': 10, 'page': 11}


def simulation_declarations(source):
    """Keep generated simulation definitions and exclude platform graphics."""
    declarations = []
    for match in re.finditer(r'^(?:static )?const (?:Deceptus\w+|char \*const)\b', source, re.M):
        quote = None
        escaped = False
        for end in range(match.end(), len(source)):
            character = source[end]
            if escaped:
                escaped = False
            elif quote and character == '\\':
                escaped = True
            elif character == quote:
                quote = None
            elif not quote and character in ('"', "'"):
                quote = character
            elif not quote and character == ';':
                declarations.append(source[match.start():end + 1])
                break
        else:
            raise ValueError('unterminated generated C declaration')
    return ('#include "deceptus/adventure.h"\n#include "deceptus/enemies.h"\n' +
            '\n'.join(declarations) + '\n')


def compile_native(clang):
    compiler = shutil.which(str(clang))
    if not compiler:
        raise RuntimeError(f'{clang} not found')
    folder = ROOT / 'build/tests/opening'
    folder.mkdir(parents=True, exist_ok=True)
    content = folder / 'content.c'
    content.write_text(simulation_declarations((ROOT / 'assets/generated/gba/room.c').read_text()))
    library = folder / ('opening.dll' if sys.platform == 'win32' else 'opening.so')
    sources = (sorted((ROOT / 'src/engine').glob('*.c')) +
               sorted((ROOT / 'src/engine/player').glob('*.c')) +
               [ROOT / 'src/engine/mechanisms/grabrope.c', ROOT / 'src/engine/mechanisms/moveablebox.c', ROOT / 'src/engine/mechanisms/crusher.c'] +
               sorted(path for path in (ROOT / 'src/game').rglob('*.c')
                      if path.parent != ROOT / 'src/game/enemies'))
    flags = ['-shared', '-O2', '-std=c90', '-Wall', '-Wextra', '-Werror', '-pedantic']
    if sys.platform != 'win32':
        flags.append('-fPIC')
    subprocess.run([compiler, *flags, '-I' + str(ROOT / 'include'),
                    str(ROOT / 'tests/opening_traversal_tests.c'), str(content),
                    *map(str, sources), '-o', str(library)], check=True)
    return C.CDLL(str(library))


def native_state(library):
    result = {name: library.openingState(index) for name, index in FIELDS.items()}
    result['inventory'] = [library.openingState(16 + index) for index in range(7)]
    return result


def gba_state(emulator):
    memory = emulator.memory()
    marker = memory.find(b'\xce\xd3')
    if marker < 0:
        raise AssertionError('GBA tutorial debug mailbox missing')
    values = struct.unpack_from('<52H', memory, marker)
    return dict(room=values[28], x=values[1], y=values[2], mode=values[29],
                flags=values[36], health=values[6], deaths=values[30],
                content_error=values[15], has_sword=values[51], ticks=values[4], page=values[39],
                inventory=[0, values[37], values[38], values[46], values[47], values[48], values[49]])


def retro_buttons(buttons):
    mapping = {1: 6, 2: 7, 4: 0, 8: 4, 16: 3, 32: 2, 64: 5, 128: 8, 256: 0, 512: 10}
    return [button for flag, button in mapping.items() if buttons & flag]


def check(state, expected, label):
    assert not state['content_error'], (label, state)
    for name, value in expected.items():
        assert state[name] == value, (label, name, value, state)


def verify(route_path=ROUTE, clang='clang', core=None, rom=None):
    route = json.loads(Path(route_path).read_text())
    library = compile_native(clang)
    library.openingInit()
    checkpoints = {point['step']: point for point in route['checkpoints']}
    report = {'scope': route['scope'], 'route_sha256': hashlib.sha256(Path(route_path).read_bytes()).hexdigest(),
              'input_ticks': sum(ticks for ticks, _ in route['steps']), 'native': []}
    trace = []
    for index, (ticks, buttons) in enumerate(route['steps']):
        for _ in range(ticks):
            library.openingTick(buttons)
        trace.append(native_state(library))
        if index in checkpoints:
            point = checkpoints[index]
            current = native_state(library)
            check(current, point['expected'], point['name'])
            report['native'].append(dict(name=point['name'], **current))
    report['simulation_ticks'] = library.openingState(12)
    report['native_rope_attached_ticks'] = library.openingState(13)
    if core:
        emulator = Emulator(core, rom, {'mgba_use_bios': 'OFF', 'mgba_frameskip': '0'})
        report['rom_sha256'] = hashlib.sha256(Path(rom).read_bytes()).hexdigest()
        report['gba'] = []
        output = ROOT / 'artifacts/opening-check'
        output.mkdir(parents=True, exist_ok=True)
        try:
            emulator.run(60)
            assert gba_state(emulator)['room'] == 0 and gba_state(emulator)['mode'] == 0, \
                'the ROM must boot at the original New Game title'
            stalled_frames = 0
            for index, (ticks, buttons) in enumerate(route['steps']):
                # the core samples its input one display frame before the mailbox.
                emulator.run(ticks + (1 if index == 0 else 0), retro_buttons(buttons))
                # room loading and rendering can span multiple display frames. Keep the
                # same ordinary input until its simulation ticks catch up; never
                # advance the next input early or alter emulator state.
                for _ in range(120):
                    current = gba_state(emulator)
                    if current['content_error']:
                        emulator.screenshot(output / f'failure-{index}.png')
                        raise AssertionError(('GBA content failure', index, buttons, current))
                    if all(current[key] == trace[index][key] for key in ('room', 'mode', 'ticks', 'page')):
                        break
                    emulator.run(1, retro_buttons(buttons))
                    stalled_frames += 1
                else:
                    emulator.screenshot(output / f'failure-{index}.png')
                    raise AssertionError(('simulation diverged', index, buttons, trace[index], current))
                if index in checkpoints:
                    point = checkpoints[index]
                    current = gba_state(emulator)
                    emulator.screenshot(output / (point['name'] + '.png'))
                    check(current, point['expected'], point['name'])
                    report['gba'].append(dict(name=point['name'], **current))
            report['emulator'] = emulator.version
            report['extra_display_frames'] = stalled_frames
        finally:
            emulator.close()
    output = ROOT / 'artifacts/opening-check'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--route', type=Path, default=ROUTE)
    parser.add_argument('--clang', default='clang')
    parser.add_argument('--native-only', action='store_true')
    parser.add_argument('--core', type=Path, default=ROOT / '.tools/mgba/mgba_libretro.dll')
    parser.add_argument('--rom', type=Path, default=ROOT / 'build/gba/deceptus-tutorial.gba')
    args = parser.parse_args()
    verify(args.route, args.clang, None if args.native_only else args.core, args.rom)
