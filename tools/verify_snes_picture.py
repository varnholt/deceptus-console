"""Check what a pacing-locked SNES opening cartridge shows while it plays the route.

Every display frame during play: a mostly black picture away from room changes is a flash.
Every N presentations, after the commit: the cached background window in video memory must equal
the converted room data (tile bytes, palette, flips, current animation frames); every visible
object must use a slot of the current frame whose tiles equal its sprite resource; exactly one
player body may be requested.

    python tools/verify_snes_picture.py --rom build/snes-verify/deceptus-tutorial.sfc --output artifacts/snes-picture.json
"""
import argparse, json, re, struct, subprocess
from pathlib import Path
import numpy as np
import ctypes as C

from emulator import Emulator, ROOT
from verify_snes_opening import ROUTE, SNES_CORE_OPTIONS, compile_native, native_state, snes_state, retro_buttons
from verify_opening import route_steps

ASSETS = ROOT / 'assets/generated/snes'
WORK = 0x10000                      # snes_background_storage at $7f0000
WIDTH, HEIGHT = 33, 29
MAP_BASE = (0x0000, 0x1000)         # byte addresses of the plane maps in vram
TILE_BASE = (0x4000, 0x2000)        # byte addresses of tile 0 per plane
BLACK_FRACTION = 0.97
ROOM_CHANGE_MARGIN = 90             # display frames around a room change where black is expected


def chunks(stem):
    paths = sorted(ASSETS.glob(stem + '_*.bin'), key=lambda p: int(p.stem.rsplit('_', 1)[1]))
    return b''.join(p.read_bytes() for p in paths)


def work_offsets(clang):
    """Byte offsets of the work-area members the checks read, from background_work.h itself. They
    all precede the first pointer member, so the host layout matches the SNES one."""
    folder = ROOT / 'build/tests/snes-picture'
    folder.mkdir(parents=True, exist_ok=True)
    members = ('maps', 'clip_frame', 'flags', 'left', 'top', 'loaded')
    source = ['#include <stddef.h>', '#include <stdio.h>', '#include "background_work.h"', 'int main(void)', '{']
    source += [f'   printf("%u\\n", (unsigned)offsetof(SnesBackgroundWork, {name}));' for name in members]
    source += ['   return 0;', '}']
    (folder / 'offsets.c').write_text('\n'.join(source) + '\n')
    executable = folder / 'offsets.exe'
    subprocess.run([clang, '-std=c90', '-I' + str(ROOT / 'src/platform/snes'), '-I' + str(ROOT / 'include'),
                    '-I' + str(ASSETS), str(folder / 'offsets.c'), '-o', str(executable)], check=True)
    values = subprocess.run([str(executable)], check=True, capture_output=True, text=True).stdout.split()
    return dict(zip(members, map(int, values)))


def load_rooms():
    resources = (ASSETS / 'opening_resources.c').read_text()
    rooms = {}
    for room, columns, rows, count in re.findall(r'snes_area(\d+)_palette,(\d+),(\d+),(\d+),', resources):
        room = int(room)
        rooms[room] = dict(columns=int(columns), rows=int(rows),
                           patterns=chunks(f'snes_area{room}_patterns'),
                           planes=[np.frombuffer(chunks(f'snes_area{room}_background'), '<u2').reshape(-1, 2),
                                   np.frombuffer(chunks(f'snes_area{room}_foreground'), '<u2').reshape(-1, 2)],
                           clips=[])
    for room, body in re.findall(r'static const SnesBackgroundAnimation snes_area(\d+)_animations\[\] = \{(.*?)\n\};', resources, re.S):
        for index, line in enumerate(l for l in body.strip().splitlines() if l.strip()):
            values = re.fullmatch(r'\s*\{(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),[^,]+,\{[^,]+,(\d+)\},(\d+)\},', line)
            left, top, columns, rows, frames, _, required, forbidden, _, plane = map(int, values.groups())
            cells = np.frombuffer(chunks(f'snes_area{room}_animation{index}_cells'), '<u2').reshape(-1, 2)
            rooms[int(room)]['clips'].append(dict(left=left, top=top, columns=columns, rows=rows, frames=frames,
                                                  required=required, forbidden=forbidden, plane=plane, cells=cells))
    return rooms


SPRITE_TILE_BASE = 0xc000           # byte address of sprite name table tile 0
SPRITE_PIECES = 128


def sprite_tile(slot):
    return (slot // 64) * 256 + ((slot & 63) // 8) * 32 + (slot & 7) * 2


def load_sprites():
    resources = (ASSETS / 'opening_resources.c').read_text()
    table = resources[resources.index('snes_sprite_resources['):]
    names = re.findall(r'\{(snes_\w+?),snes_\w+,(\d+),', table.split('};')[0])
    cache, graphics = {}, []
    for name, size in names:
        if name not in cache:
            cache[name] = (ASSETS / (name + '.bin')).read_bytes()
        graphics.append(cache[name])
    # every 8x8 tile of the sprite area belongs to one slot quarter: (slot, row half, column)
    owner = {}
    for slot in range(SPRITE_PIECES):
        base = sprite_tile(slot)
        for half, column in ((0, 0), (0, 1), (1, 0), (1, 1)):
            owner[base + half * 16 + column] = (slot, half, column)
    return graphics, owner


PLAYER_FAMILIES = ('ACTOR', 'SWIM', 'SWORD', 'PLAYER_ROPE')


def player_resources():
    header = (ASSETS / 'snes_assets.h').read_text()
    firsts = sorted((int(v), n) for n, v in re.findall(r'#define SNES_(\w+)_FIRST (\d+)', header))
    count = int(re.search(r'#define SNES_SPRITE_RESOURCE_COUNT (\d+)', header).group(1))
    ranges = []
    for i, (first, name) in enumerate(firsts):
        end = firsts[i + 1][0] if i + 1 < len(firsts) else count
        if name in PLAYER_FAMILIES:
            ranges.append((first, end))
    return ranges


def check_player_count(ranges, sym, wram):
    base = sym['tccs_main.asm_snes_obj_requests'] - 0x7e0000
    count = struct.unpack_from('<H', wram, sym['tccs_main.asm_snes_obj_request_count'] - 0x7e0000)[0]
    bodies = []
    for index in range(count):
        resource, x, y, flip, priority = struct.unpack_from('<HhhBB', wram, base + index * 8)
        if any(first <= resource < end for first, end in ranges):
            bodies.append((resource, x, y))
    return bodies


def symbols(rom):
    table = {}
    for line in Path(rom).with_name('deceptus.sym').read_text().splitlines():
        parts = line.split()
        if len(parts) == 2 and len(parts[0]) == 8:
            table[parts[1]] = int(parts[0], 16)
    return table


def check_sprites(sprites, sym, wram, vram):
    graphics, owner = sprites
    oam = wram[sym['snes_obj_oam'] - 0x7e0000: sym['snes_obj_oam'] - 0x7e0000 + 544]
    slots = wram[sym['snes_obj_slots'] - 0x7e0000: sym['snes_obj_slots'] - 0x7e0000 + 8 * SPRITE_PIECES]
    count = struct.unpack_from('<H', wram, sym['snes_obj_count'] - 0x7e0000)[0]
    problems = []
    for index in range(128):
        x, y, tile, attributes = oam[index * 4: index * 4 + 4]
        if index >= count:
            if y != 240:
                problems.append(('visible past the frame', index))
            continue
        name = tile | ((attributes & 1) << 8)
        if name not in owner:
            problems.append(('tile outside the slot area', index, name))
            continue
        slot, half, column = owner[name]
        resource, piece, valid, used, held, pending, _ = struct.unpack_from('<HBBBBBB', slots, slot * 8)
        if not used or pending or not valid:
            problems.append(('slot not current', index, slot, used, pending, valid))
            continue
        large = (oam[512 + index // 4] >> ((index & 3) * 2 + 1)) & 1
        quarters = ((0, 0), (0, 1), (1, 0), (1, 1)) if large else ((half, column),)
        for row_half, col in quarters:
            if not large:
                source_half, source_column = row_half, col
                vram_tile = name
            else:
                source_half, source_column = row_half, col
                vram_tile = sprite_tile(slot) + row_half * 16 + col
            wanted = graphics[resource][piece * 128 + source_half * 64 + source_column * 32: piece * 128 + source_half * 64 + source_column * 32 + 32]
            shown = vram[SPRITE_TILE_BASE + vram_tile * 32: SPRITE_TILE_BASE + vram_tile * 32 + 32]
            if shown != wanted:
                problems.append(('wrong tile data', index, slot, resource, piece))
                break
    return problems


def map_address(x, y):
    return (x & 31) + ((y & 31) << 5) + (1024 if x & 32 else 0)


def expected_cell(room, plane, x, y, flags, clip_frames):
    if not (0 <= x < room['columns'] and 0 <= y < room['rows']):
        return 0, 0
    pattern, attributes = room['planes'][plane][y * room['columns'] + x]
    for index, clip in enumerate(room['clips']):
        if clip['plane'] != plane or (flags & clip['required']) != clip['required'] or flags & clip['forbidden']:
            continue
        if clip['left'] <= x < clip['left'] + clip['columns'] and clip['top'] <= y < clip['top'] + clip['rows']:
            offset = (clip_frames[index] * clip['rows'] + y - clip['top']) * clip['columns'] + x - clip['left']
            pattern, attributes = clip['cells'][offset]
    # the static-cell flag and fixed tile do not change what the cell shows.
    return int(pattern) & 0x3fff, int(attributes)


def check_background(rooms, room_index, wram, vram, offsets):
    room = rooms[room_index]
    left = struct.unpack_from('<h', wram, WORK + offsets['left'])[0]
    top = struct.unpack_from('<h', wram, WORK + offsets['top'])[0]
    flags = struct.unpack_from('<H', wram, WORK + offsets['flags'])[0]
    clip_frames = struct.unpack_from('<8H', wram, WORK + offsets['clip_frame'])
    wrong = []
    for plane in (0, 1):
        for y in range(top, top + HEIGHT):
            for x in range(left, left + WIDTH):
                pattern, attributes = expected_cell(room, plane, x, y, flags, clip_frames)
                entry = struct.unpack_from('<H', vram, MAP_BASE[plane] + 2 * map_address(x, y))[0]
                tile = entry & 1023
                shown = vram[TILE_BASE[plane] + tile * 32: TILE_BASE[plane] + tile * 32 + 32]
                wanted = room['patterns'][pattern * 32: pattern * 32 + 32]
                if (entry & 0xdc00) != (attributes & 0xdc00) or shown != wanted:
                    wrong.append((plane, x, y))
    return wrong


def video_ram(emulator):
    return C.string_at(emulator.core.retro_get_memory_data(3), emulator.core.retro_get_memory_size(3))


def native_targets(steps, clang):
    """The state the native replay of the same content reaches at the end of each step."""
    library = compile_native(clang)
    library.openingInit()
    states = []
    for ticks, buttons in steps:
        for _ in range(ticks):
            library.openingTick(buttons)
        states.append(native_state(library))
    return states


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, default=ROOT / 'build/snes-verify/deceptus-tutorial.sfc')
    parser.add_argument('--core', type=Path, default=ROOT / '.tools/snes9x/snes9x_libretro.dll')
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/snes-picture.json')
    parser.add_argument('--last-step', type=int, default=10 ** 9)
    parser.add_argument('--every', type=int, default=20, help='object and background checks every N presentations')
    parser.add_argument('--screenshots', type=Path, help='folder for pictures of the first findings')
    parser.add_argument('--clang', default='clang')
    args = parser.parse_args()
    rooms = load_rooms()
    sprites = load_sprites()
    sym = symbols(args.rom)
    sprite_samples = []
    ranges = player_resources()
    duplicates = []
    route = json.loads(Path(ROUTE).read_text())
    steps = route_steps(route, 'snes')[:args.last_step + 1]
    states = native_targets(steps, args.clang)
    offsets = work_offsets(args.clang)
    emulator = Emulator(args.core, args.rom, SNES_CORE_OPTIONS)
    emulator.run(120)
    for _ in range(3600):
        try:
            s = snes_state(emulator)
        except AssertionError:
            s = None
        if s and s['health'] and s['phase'] >= 15:
            break
        emulator.run(1)
    emulator.run(5)
    frame, room_changes, black, background, presentations = 0, [], [], [], 0
    shots = args.screenshots
    if shots:
        shots.mkdir(parents=True, exist_ok=True)
    last_image = None
    previous = snes_state(emulator)
    for index, (ticks, buttons) in enumerate(steps):
        target = states[index]
        expected_buttons = buttons | (256 if buttons & 4 else 0)
        for _ in range(ticks * 120 + 600):
            emulator.run(1, retro_buttons(buttons))
            frame += 1
            s = snes_state(emulator)
            if s['room'] != previous['room'] or s['mode'] != previous['mode']:
                room_changes.append(frame)
            if s['mode'] == 1:
                pixels = np.asarray(emulator.last_image.convert('L'))
                if (pixels < 12).mean() > BLACK_FRACTION:
                    black.append(dict(frame=frame, step=index, room=s['room'], ticks=s['ticks']))
                    if shots and len(black) <= 12 and last_image is not None:
                        last_image.save(shots / f'black-{frame:06d}-before.png')
            last_image = emulator.last_image.copy()
            if s['ticks'] != previous['ticks'] and s['mode'] == 1:
                presentations += 1
                if presentations % args.every == 0:
                    wram = emulator.memory()
                    if wram[WORK + offsets['loaded']] and s['phase'] <= 9:
                        vram = video_ram(emulator)
                        wrong = check_background(rooms, s['room'], wram, vram, offsets)
                        if wrong:
                            background.append(dict(frame=frame, step=index, room=s['room'], cells=len(wrong), first=wrong[:6]))
                        bodies = check_player_count(ranges, sym, wram)
                        if len(bodies) != 1:
                            duplicates.append(dict(frame=frame, step=index, room=s['room'], bodies=bodies))
                            if shots and len(duplicates) <= 12:
                                emulator.last_image.save(shots / f'players-{frame:06d}.png')
                        problems = check_sprites(sprites, sym, wram, vram)
                        if problems:
                            sprite_samples.append(dict(frame=frame, step=index, room=s['room'], problems=len(problems), first=problems[:6]))
            previous = s
            if (s['phase'] >= 15 and s['campaign_buttons'] == expected_buttons and
                    all(s[k] == target[k] for k in ('room', 'mode', 'ticks', 'page', 'flags', 'inventory', 'has_sword'))):
                break
        else:
            raise SystemExit(('route diverged', index))
    emulator.close()
    flashes = [b for b in black if all(abs(b['frame'] - change) > ROOM_CHANGE_MARGIN for change in room_changes)]
    report = dict(display_frames=frame, presentations=presentations, black_frames=len(black), flashes=len(flashes),
                  flash_examples=flashes[:20], background_samples_wrong=len(background), background_examples=background[:20],
                  sprite_samples_wrong=len(sprite_samples), sprite_examples=sprite_samples[:20],
                  player_count_wrong=len(duplicates), player_examples=duplicates[:20])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1))
    print(json.dumps({k: v for k, v in report.items() if not k.endswith('examples')}))
    return report


if __name__ == '__main__':
    result = main()
    if result['flashes'] or result['background_samples_wrong'] or result['sprite_samples_wrong'] or result['player_count_wrong']:
        raise SystemExit('SNES picture check found errors; see the report')
