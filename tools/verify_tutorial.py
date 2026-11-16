"""Verify controlled-start GBA tutorial scenes; restore production outputs afterward.

These integration fixtures do not constitute an end-to-end tutorial playthrough.
Build --scene tutorial first. Fixtures start with normal game definitions at
specified scene positions; emulator memory is never patched.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct

import build_gba
from emulator import Emulator, ROOT


FIXTURES = [('floor', 2, 480, 336), ('exit', 2, 720, 576),
            ('dark', 4, 84, 288), ('water', 6, 448, 600),
            ('surface', 6, 420, 384), ('bed', 0, 270, 312),
            ('shrine', 7, 580, 336), ('blob',8,700,432), ('mines',6,40,360), ('gate',6,400,528),
            ('performance',6,420,384),
            ('rubies',24,864,336), ('owl-release',7,540,336),
            ('sword',7,540,216), ('headtorch',11,420,264),
            ('torch-gate',4,84,288), ('sinkhole',22,960,384),
            ('sword-lever',2,204,408), ('cannon',20,732,336),
            ('crypt-return',25,192,60),
            ('cannon-rat',20,443,288),
            ('title-scroll',0,270,312),
            ('ward',7,540,216), ('torch-look',4,400,288), ('torch-sword',7,540,216),
            ('iron-door',12,60,288), ('iron-open',12,60,288),
            ('sewers-collapse',10,660,168), ('cannon-fire',20,480,336),
            ('sealed-orb',13,240,480), ('sealed-frog',13,396,288),
            ('graveyard-rope',22,396,288), ('graveyard-rope-armed',22,396,288),
            ('graveyard-floor',22,256,312),
            ('title', -1, 0, 0)]

# prior progress belongs only to these controlled-start test cartridges.
PROGRESSION_SETUP = {
    'owl-release': 'adventure.flags=128; adventure.inventory.counts[3]=1;',
    'sword': 'adventure.flags=384;',
    'headtorch': 'adventure.flags=1024; adventure.inventory.counts[4]=1;',
    'torch-gate': 'adventure.flags=2048; adventure.inventory.counts[5]=1;',
    'sinkhole': 'adventure.flags=128; adventure.inventory.counts[3]=1;',
    'sword-lever': 'adventure.flags=512; adventure.inventory.counts[6]=1; adventure.has_sword=1;',
    'crypt-return': 'adventure.flags=4224; adventure.inventory.counts[3]=1;',
    # keep the upper source rat in view of the on/off blocks for palette stress.
    'cannon-rat': ('{ DeceptusU16 i; for(i=1;i<DECEPTUS_ENTITY_CAPACITY;++i) {'
                   'if(engine.entities[i].active && engine.entities[i].kind==DECEPTUS_RAT && '
                   'engine.room->enemy_definitions[engine.entities[i].definition].source_id==1618) '
                   '{engine.entities[i].y=DECEPTUS_FIXED(288);} } }'),
    'torch-look': 'adventure.flags=2048; adventure.inventory.counts[5]=1;',
    'torch-sword': 'adventure.flags=2944; adventure.inventory.counts[5]=1; adventure.inventory.counts[6]=1; adventure.has_sword=1;',
    'iron-open': 'adventure.inventory.counts[7]=1;',
    'graveyard-rope-armed': 'adventure.flags=2944; adventure.inventory.counts[5]=1; adventure.inventory.counts[6]=1; adventure.has_sword=1;',
}


def state(emulator):
    memory = emulator.memory()
    marker = memory.find(b'\xce\xd3')
    if marker < 0:
        raise AssertionError('tutorial debug mailbox is not initialized')
    return struct.unpack_from('<96H', memory, marker)


def press(emulator, button):
    emulator.run(4, [button])
    emulator.run(4)


def verify_remapped_object_pixels(emulator):
    """Check a remapped upload in actual VRAM, including transparent pixels."""
    area = ROOT / 'assets/generated/gba/area0'
    resources = json.loads((area / 'mechanisms-frames.json').read_text())
    source = (area / 'mechanisms.bin').read_bytes()
    emulator.run(1)
    objects = C.string_at(emulator.core.retro_get_memory_data(3), 98304)[0x10000:]
    # match a complete source frame at any aligned OBJ upload slot. This avoids
    # relying on the core exposing all EWRAM through its system-memory window.
    for entry in resources:
        if entry['source'] not in ('rat.png', 'on_off_block.png'):
            continue
        pixels = source[entry['offset']:entry['offset'] + entry['bytes']]
        if not any(pixels):
            continue
        for first in range(52, 257 - entry['palette_count'], 2):
            if first == entry['palette_first']:
                continue
            delta = first - entry['palette_first']
            remapped = bytes(pixel + delta if pixel else 0 for pixel in pixels)
            offset = objects.find(remapped)
            if offset >= 0 and offset % 32 == 0:
                return 1
    raise AssertionError('no complete remapped rat/block frame matches the source pixels in OBJ VRAM')


def verify(core, clang, linker, objcopy, fixture_name=None):
    folder = ROOT / 'build/gba'
    manifest = json.loads((folder / 'build.json').read_text())
    if manifest['scene'] != 'tutorial':
        raise ValueError('build --scene tutorial before running tutorial fixtures')
    source = ROOT / 'src/platform/gba/main.c'
    original_main = source.read_text()
    marker = 'deceptusAdventureInit(&adventure, &gba_tutorial_definition, &video);'
    if original_main.count(marker) != 1:
        raise ValueError('update the fixture entry hook for the current startup code')
    scratch = ROOT / 'build/tests/tutorial'
    output = ROOT / 'artifacts/tutorial-check'
    scratch.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    fixture = scratch / 'main.c'
    names = ('main.o', 'deceptus.elf', 'deceptus.gba', 'deceptus-tutorial.gba',
             'deceptus.map', 'build.json')
    backups = {folder / name: (folder / name).read_bytes() for name in names}
    original_run = build_gba.run

    def fixture_run(command, working_directory):
        # reuse the current generated data; every fixture changes only its start.
        if len(command) > 1 and str(command[1]).endswith('tutorial_assets.py'):
            return
        if '-c' in command and source in command:
            command = [fixture if item == source else item for item in command]
            command += ['-I' + str(source.parent)]
        original_run(command, working_directory)

    report = {'production_rom_sha256': hashlib.sha256(backups[folder / 'deceptus-tutorial.gba']).hexdigest(),
              'scope': 'controlled-start rendering, UI and selected connection fixtures', 'fixtures': []}
    build_gba.run = fixture_run
    try:
        for name, room, x, y in FIXTURES:
            if fixture_name and name!=fixture_name: continue
            code = original_main
            if room >= 0:
                setup = (f'\n campaign.room_index={room}; '
                         f'deceptusInit(&engine,gba_campaign_definition.rooms[{room}],&video); '
                         f'deceptusPlacePlayer(&engine,{x},{y}); campaign.mode=DECEPTUS_CAMPAIGN_PLAYING;'
                         + PROGRESSION_SETUP.get(name, ''))
                code = code.replace(marker, marker + setup)
                code = code.replace('active_stage = &gba_stages[0];',
                                    'active_stage = &gba_stages[campaign.room_index];')
            fixture.write_text(code)
            build_gba.build(ROOT / 'assets/game/data', clang, linker, objcopy, scene='tutorial')
            emulator = Emulator(core, folder / 'deceptus-tutorial.gba',
                                {'mgba_use_bios': 'OFF', 'mgba_frameskip': '0'})
            try:
                emulator.run(40)
                if name.startswith('graveyard-') or name.startswith('sealed-'):
                    emulator.screenshot(output / (name + '-start.png'))
                if name == 'graveyard-floor':
                    values = state(emulator)
                    assert values[2] == 336 and values[7], ('box-only collider still holds the player above the grave cap', values)
                elif name in ('graveyard-rope', 'graveyard-rope-armed'):
                    values = state(emulator)
                    assert values[75] == 1 and values[71] > 0, ('source grab rope is not visible', values)
                    assert values[72], ('airborne player did not catch the rope', values)
                    radius = values[77]
                    start_ticks = values[4]
                    positions = set()
                    for tick in range(90):
                        emulator.run(1, [7] if tick < 45 else [6])
                        values = state(emulator)
                        assert values[72] and not values[15], values
                        positions.add((values[1], values[2]))
                    assert len(positions) > 10, 'left/right did not swing the rope'
                    simulated = (state(emulator)[4] - start_ticks) & 65535
                    report[name + '_simulation_ticks_per_90_display_frames'] = simulated
                    assert simulated == 90, ('rope scene missed display ticks', simulated, state(emulator)[80:84])
                    emulator.run(8, [4]); emulator.run(8, [5])
                    assert state(emulator)[72] and state(emulator)[77] == radius, 'up/down changed the source grab point'
                    emulator.screenshot(output / (name + '-hang.png'))
                    if name == 'graveyard-rope-armed':
                        assert state(emulator)[61] == 3, 'hanging headtorch is missing'
                    emulator.run(1, [0])
                    for _ in range(3):
                        if not state(emulator)[72]:
                            break
                        emulator.run(1)
                    values = state(emulator)
                    assert not values[72] and values[73] >= 57 and values[74] >= 33, ('jump did not release with source grace timers', values)
                    report[name + '_swing_positions'] = len(positions)
                elif name in ('sealed-orb', 'sealed-frog'):
                    positions, images, fireballs = set(), set(), set()
                    for _ in range(420):
                        emulator.run(1)
                        values = state(emulator)
                        assert not values[15], values
                        assert values[64] == 1 and values[65] == 1, 'source orb or blue frog is missing'
                        positions.add((values[67], values[68]))
                        images.add(values[70] if name == 'sealed-orb' else values[69])
                        fireballs.add(values[66])
                    assert len(positions) > 100 and len(images) >= 8, (name, len(positions), images)
                    assert max(fireballs) > 0, 'blue frog did not launch its source projectile'
                    report[name + '_distinct_frames'] = len(images)
                elif name == 'ward':
                    assert state(emulator)[60] & 8192, state(emulator)
                    observed = set()
                    for _ in range(120):
                        emulator.run(1)
                        values = state(emulator)
                        assert not values[15], values
                        observed.add(values[58])
                    assert len(observed) >= 20, ('ward heartbeat is not animated', observed)
                    image = emulator.last_image.convert('RGB')
                    bright = sum(r > 100 and b > 150 and g < r*.8 for r,g,b in image.getdata())
                    assert bright > 30, 'the powered sword ward is not visible'
                    report['ward_distinct_states'] = len(observed)
                elif name in ('torch-look', 'torch-sword'):
                    assert state(emulator)[61] == 3, 'equipped helmet and light sprite are missing'
                    emulator.screenshot(output / (name + '-right.png'))
                    emulator.run(12, [6])
                    assert state(emulator)[61] == 3, state(emulator)
                    emulator.screenshot(output / (name + '-left.png'))
                    if name == 'torch-sword':
                        emulator.run(1,[10]); emulator.run(13)
                        assert state(emulator)[61] == 3 and state(emulator)[54], state(emulator)
                elif name == 'iron-door':
                    emulator.run(25,[6])
                    assert state(emulator)[1] >= 32, 'locked iron door did not block passage'
                    press(emulator,4)
                    assert state(emulator)[29] == 4, 'locked iron door has no source examination text'
                elif name == 'iron-open':
                    press(emulator,4)
                    emulator.run(75)
                    for _ in range(30):
                        emulator.run(1,[6])
                    assert state(emulator)[1] < 32, 'the red key did not release the iron door'
                elif name == 'sewers-collapse':
                    for _ in range(120):
                        emulator.run(1)
                        if state(emulator)[25] == 1:
                            break
                    values = state(emulator)
                    assert values[25] == 1 and not values[26], 'authored platform did not collapse underfoot'
                    emulator.run(24)
                    assert state(emulator)[2] > 168, 'player did not fall through the collapsed platform'
                elif name == 'cannon-fire':
                    active = set()
                    for tick in range(200):
                        emulator.run(1, [0] if tick < 24 else [])
                        values = state(emulator)
                        assert not values[15], values
                        active.add(values[62])
                    assert state(emulator)[32] >= 2 and max(active) > 0, 'source cannons did not fire visible cannonballs'
                    report['cannon_shots'] = state(emulator)[32]
                elif name == 'title-scroll':
                    font = (ROOT / 'assets/generated/gba/font.bin').read_bytes()
                    white_pixels = []
                    for index, char in enumerate('THE CELL'):
                        tile = font[(ord(char)-31)*32:(ord(char)-30)*32]
                        for row in range(8):
                            for column in range(8):
                                if (tile[row*4+column//2] >> (4*(column&1))) & 15:
                                    white_pixels.append((8+index*8+column,16+row))
                    cameras, vertical_cameras, prompts = set(), set(), set()
                    for tick in range(72):
                        buttons = [6] if tick < 32 else [7]
                        if tick < 20:
                            buttons.append(0)
                        emulator.run(1, buttons)
                        values = state(emulator)
                        cameras.add(values[3]); vertical_cameras.add(values[16]); prompts.add(values[40])
                        assert all(min(emulator.last_image.getpixel(point)[:3]) >= 230 for point in white_pixels), \
                            ('room title moved away from its screen anchor', tick, values)
                    assert len(cameras) > 8 and len(vertical_cameras) > 1 and len(prompts) > 1, \
                        ('fixture did not scroll through a moving cue', len(cameras), prompts, state(emulator))
                    report['room_title_fixed_screen_frames'] = 72
                elif name == 'rubies':
                    assert not state(emulator)[46], state(emulator)
                    press(emulator, 4)
                    assert state(emulator)[46] == 1 and state(emulator)[36] & 128, state(emulator)
                    for _ in range(12):
                        if state(emulator)[29] != 4:
                            break
                        press(emulator, 0)
                    press(emulator, 4)
                    assert state(emulator)[46] == 1, 'owl granted duplicate rubies'
                elif name == 'owl-release':
                    assert state(emulator)[46] == 1 and not state(emulator)[36] & 256 and state(emulator)[60]&8192, state(emulator)
                    emulator.run(1, [4])
                    values = state(emulator)
                    assert not values[46] and values[36] & 256 and values[50] == 27 and values[60]&8192, values
                    emulator.run(26)
                    assert state(emulator)[50] == 1 and not state(emulator)[51], state(emulator)
                    emulator.run(1)
                    assert state(emulator)[50] == 0 and not state(emulator)[60]&8192, state(emulator)
                    report['source_owl_release_delay_ticks'] = 27
                elif name == 'sword':
                    assert state(emulator)[49] == 1 and state(emulator)[51] == 1, state(emulator)
                    assert state(emulator)[36] & 512, state(emulator)
                    press(emulator, 2)
                    assert state(emulator)[29] == 6, state(emulator)
                    emulator.screenshot(output / 'sword-inventory.png')
                    press(emulator, 0)
                    assert state(emulator)[29] == 1, state(emulator)
                    emulator.run(1, [10])
                    assert state(emulator)[54] == 1, 'L did not start the equipped sword attack'
                    emulator.run(12, [10])
                    assert state(emulator)[54] == 13 and state(emulator)[55], state(emulator)
                    emulator.screenshot(output / 'sword-attack.png')
                    emulator.run(60, [10])
                    assert not state(emulator)[54], 'holding L repeated sword attacks'
                    assert state(emulator)[49] == 1, 'sword duplicated after returning to play'
                elif name == 'headtorch':
                    press(emulator, 4)
                    values = state(emulator)
                    assert values[36] & 2048 and values[48] == 1 and not values[47], values
                elif name == 'torch-gate':
                    for _ in range(100):
                        emulator.run(1, [6])
                        if state(emulator)[28] == 13:
                            break
                    assert state(emulator)[28] == 13 and state(emulator)[29] == 1, state(emulator)
                elif name == 'sinkhole':
                    emulator.run(160)
                    assert state(emulator)[36] & 4096 and state(emulator)[28] == 25, state(emulator)
                elif name in ('sword-lever', 'cannon'):
                    before = state(emulator)
                    press(emulator, 4)
                    after = state(emulator)
                    assert before[19] != after[19] and before[20] != after[20], (before, after)
                    assert not after[15], 'equipped player conflicts with mechanism resources'
                elif name == 'crypt-return':
                    assert state(emulator)[28] == 22, 'crypt source touch return required directional input'
                elif name == 'cannon-rat':
                    remaps = state(emulator)[56]
                    verified = verify_remapped_object_pixels(emulator) if remaps else 0
                    for _ in range(180):
                        emulator.run(1)
                        values = state(emulator)
                        assert not values[15], 'rat and on/off blocks exhausted compatible OBJ palettes'
                        remaps = max(remaps, values[56])
                        if values[56] and not verified:
                            verified = verify_remapped_object_pixels(emulator)
                    assert remaps, ('fixture did not exercise a simultaneous palette remap', state(emulator))
                    report['cannon_visible_palette_remaps'] = remaps
                    report['remapped_obj_uploads_verified_in_vram'] = verified
                elif name == 'mines':
                    for _ in range(60):
                        emulator.run(1,[6])
                        if state(emulator)[28]==12:break
                    assert state(emulator)[28]==12,state(emulator)
                    emulator.run(16);emulator.screenshot(output/'mines-room.png')
                    for _ in range(100):
                        emulator.run(1,[7])
                        if state(emulator)[28]==6:break
                    assert state(emulator)[28]==6,state(emulator)
                elif name == 'gate':
                    emulator.run(70)
                    values=state(emulator)
                    assert values[36]&64 and values[29]==11,values
                    assert values[2]<540,values
                    ticks=values[4];emulator.run(20)
                    assert ((state(emulator)[4]-ticks)&65535)>=19,'warning froze gameplay'
                elif name == 'performance':
                    emulator.run(60)
                    missed=0;steps=240
                    for tick in range(steps):
                        before=state(emulator)
                        emulator.run(1,[6] if tick%40<10 or tick%40>=30 else [7])
                        after=state(emulator)
                        assert not after[15],after
                        assert before[28]==after[28]==6 and before[29]==after[29]==1,after
                        delta=(after[4]-before[4])&65535
                        if delta!=1:missed+=1
                    report['water_scrolling_timing']={'display_frames':steps,'non_unit_tick_steps':missed}
                    assert missed<=2,report['water_scrolling_timing']
                elif name == 'exit':
                    for _ in range(30):
                        emulator.run(1, [7])
                        if state(emulator)[28] == 11:
                            break
                    assert state(emulator)[28] == 11, state(emulator)
                    emulator.run(16)
                    emulator.screenshot(output / 'fools-ledge.png')
                    emulator.run(30, [6])
                    assert state(emulator)[28] == 2, state(emulator)
                elif name == 'dark':
                    emulator.run(20, [6])
                    assert state(emulator)[29] == 4, state(emulator)
                elif name == 'bed':
                    emulator.screenshot(output / 'room-title-and-cue.png')
                    press(emulator, 4)
                    assert state(emulator)[29] == 4, state(emulator)
                elif name == 'water':
                    values = state(emulator)
                    left, top = values[1] - values[3] - 16, values[2] - values[16] - 48
                    actor = emulator.last_image.crop((left, top, left + 32, top + 48))
                    assert any(r > 150 and g > 80 and b < 140 for r, g, b in actor.getdata()), 'underwater player hidden'
                elif name == 'surface':
                    emulator.screenshot(output / 'surface-early.png')
                    emulator.run(18)
                    press(emulator, 3)
                    emulator.run(4)
                    values = state(emulator)
                    area = ROOT / 'assets/generated/gba/area6'
                    clip = json.loads((area / 'animations.json').read_text())[0]
                    cells = (area / 'animation-cells.bin').read_bytes()
                    patterns = (area / 'stream-patterns.bin').read_bytes()
                    vram = C.string_at(emulator.core.retro_get_memory_data(3), 98304)
                    bx, by, width, height = [n // 8 for n in clip['bounds']]
                    checked = 0
                    for row in range(height):
                        for column in range(width):
                            wx, wy = bx + column, by + row
                            if not (values[3] // 8 <= wx < values[3] // 8 + 30 and values[16] // 8 <= wy < values[16] // 8 + 20):
                                continue
                            offset = clip['cell_offset'] + ((values[34] * height + row) * width + column) * 4
                            pattern = struct.unpack_from('<H', cells, offset)[0]
                            address = ((wx & 63) // 32) * 1024 + (wy & 31) * 32 + (wx & 31)
                            slot = struct.unpack_from('<H', vram, 0xe000 + address * 2)[0] & 1023
                            assert vram[slot*32:(slot+1)*32] == patterns[pattern*32:(pattern+1)*32], 'surface tile mismatch'
                            checked += 1
                    assert checked, 'surface fixture did not show the animated region'
                elif name in ('shrine','foam'):
                    assert not state(emulator)[15],state(emulator)
                    observed=set()
                    if name == 'shrine':
                        clips = json.loads((ROOT / 'assets/generated/gba/area7/animations.json').read_text())
                        clip_index = next(index for index, clip in enumerate(clips) if clip['name'] == 'shrine_column')
                    for _ in range(90):
                        emulator.run(1)
                        if name=='shrine':
                            observed.add(state(emulator)[(34,44,52)[clip_index]])
                        else:
                            vram=C.string_at(emulator.core.retro_get_memory_data(3),98304)
                            observed.add(hashlib.sha256(vram[0xf000:0x10000]).hexdigest())
                    assert len(observed)>5, (name,'source tile animation is not advancing',state(emulator))
                    report[name+'_distinct_states']=len(observed)
                elif name == 'blob':
                    for _ in range(30):emulator.run(1)
                    assert state(emulator)[11], 'original blob placement is missing'
                elif name == 'title':
                    frames = []
                    for _ in range(60):
                        emulator.run(1)
                        frames.append(emulator.last_image.tobytes())
                    changes = sum(a != b for a, b in zip(frames, frames[1:]))
                    assert changes > 50, changes
                    report['rotation_changed_frames'] = changes
                    emulator.screenshot(output / 'title.png')
                    press(emulator, 5); press(emulator, 3)
                    assert state(emulator)[29] == 9
                    press(emulator, 3)
                    assert state(emulator)[29] == 7
                    press(emulator, 0); press(emulator, 5); press(emulator, 3)
                    assert state(emulator)[29] == 0
                    press(emulator, 3); press(emulator, 3)
                    assert state(emulator)[29] == 5
                emulator.screenshot(output / ('pause.png' if name == 'title' else name + '.png'))
                values = state(emulator)
                assert not values[15], values
                report['fixtures'].append({'name': name, 'room': values[28], 'mode': values[29], 'content_error': values[15]})
            except Exception:
                emulator.screenshot(output / ('failure-' + name + '.png'))
                (scratch / ('failure-' + name + '.gba')).write_bytes((folder / 'deceptus-tutorial.gba').read_bytes())
                (scratch / ('failure-' + name + '.map')).write_bytes((folder / 'deceptus.map').read_bytes())
                raise
            finally:
                emulator.close()
    finally:
        build_gba.run = original_run
        for path, data in backups.items():
            path.write_bytes(data)
    report['production_outputs_restored'] = all(path.read_bytes() == data for path, data in backups.items())
    assert report['production_outputs_restored']
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Tutorial scene checks passed; production outputs restored.')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=ROOT / '.tools/mgba/mgba_libretro.dll')
    parser.add_argument('--clang', default='clang')
    parser.add_argument('--linker', default='ld.lld')
    parser.add_argument('--objcopy', default='llvm-objcopy')
    parser.add_argument('--fixture',choices=[entry[0] for entry in FIXTURES])
    args = parser.parse_args()
    verify(args.core, args.clang, args.linker, args.objcopy,args.fixture)
