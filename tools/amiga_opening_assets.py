"""Pack native-size opening graphics for the Amiga's bounded room cache."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

import numpy as np
from PIL import Image

from amiga_palette import color_error, fit_palette, map_image
from assets import ROOT
from content import CHECKPOINT_HIDDEN
from rle import packbits
from lz4_block import encode_block
from room_content import compile_rooms
from snes_tutorial_assets import ensure_original_imports, source_clips
from source_sprites import source_sprites, sprite_families


# guaranteed room banks, allocated at startup from free memory: a stock A500 with 512 KiB Chip and
# 512 KiB slow RAM under Kickstart 1.3 leaves ~297 KiB non-chip and ~322 KiB Chip after the display.
RESOURCE_NORMAL_BYTES = 236 * 1024
RESOURCE_CHIP_BYTES = 300 * 1024


# grab rope physics runs every tick per node; on a 68000 a rope settles fine with fewer nodes.
AMIGA_ROPE_SEGMENTS = 6


def amiga_recipes(recipes):
    """Room recipes with the Amiga's content limits applied."""
    return [(dict(recipe, rope_segments_max=AMIGA_ROPE_SEGMENTS), source) for recipe, source in recipes]


class Archive:
    def __init__(self):
        self.data = bytearray(48)

    def append(self, payload):
        if len(self.data) & 1:
            self.data.append(0)
        offset = len(self.data)
        self.data.extend(payload)
        return offset

    def rows(self, cells):
        packed = bytearray()
        offsets = []
        for row in cells:
            offsets.append(len(packed))
            packed.extend(packbits(np.asarray(row, dtype='>u2').tobytes(), 2))
        offsets.append(len(packed))
        table = struct.pack(f'>{len(offsets)}I', *offsets)
        return self.append(table + packed)


def planar(pixels, planes=5):
    return b''.join(np.packbits((pixels >> plane) & 1, axis=1).tobytes()
                    for plane in range(planes))


def raster_columns(patterns, background, foreground):
    tiles = np.frombuffer(b''.join(patterns), dtype=np.uint8).reshape(-1, 6, 8)
    layers = [tiles[grid].transpose(2, 0, 3, 1).reshape(
        6, grid.shape[0] * 8, grid.shape[1])
        for grid in (background, foreground)]
    base, front = layers
    mask = front[5:6]
    pixels = np.concatenate(((base[:5] & ~mask) | (front[:5] & mask), mask))
    pixels = np.pad(pixels, ((0, 0), (0, 0), (0, pixels.shape[2] % 2)))
    band_rows = 32
    padded = np.pad(pixels, ((0, 0), (0, -pixels.shape[1] % band_rows), (0, 0)))
    blocks = [encode_block(padded[:, y:y + band_rows, x:x + 2].tobytes())
              for x in range(0, pixels.shape[2], 2)
              for y in range(0, padded.shape[1], band_rows)]
    offsets = [0]
    for block in blocks:
        offsets.append(offsets[-1] + len(block))
    return (struct.pack('>4s4H', b'AC02', background.shape[1] * 8,
                        background.shape[0] * 8, 16, pixels.shape[2] // 2)
            + struct.pack(f'>{len(offsets)}I', *offsets) + b''.join(blocks),
            pixels.tobytes())


def room_families(source, recipe, sword):
    mechanisms = json.loads((source / 'mechanisms.json').read_text())
    enemies = json.loads((source / 'enemies.json').read_text())
    families = {'HEADTORCH', 'HEADTORCH_BEAM', 'SWORD' if sword else 'ACTOR'}
    mechanism_families = {
        3: 'LEVER', 4: 'BLOCK', 7: 'SPIKES', 8: 'COLLAPSE',
        10: 'IRON_DOOR', 11: 'WOODEN_PLANKS', 13: 'GRAB_ROPE', 14: 'MOVEABLE_BOX', 16: 'CRUSHER', 17: 'FLOOR_SPIKES', 18: 'SPIKE_BALL',
    }
    for mechanism in mechanisms['definitions']:
        kind = mechanism['kind']
        if isinstance(kind, str):
            names = {'DECEPTUS_MECH_LEVER': 3, 'DECEPTUS_MECH_BLOCK': 4,
                     'DECEPTUS_MECH_SPIKES': 7, 'DECEPTUS_MECH_COLLAPSING': 8,
                     'DECEPTUS_MECH_DOOR': 10, 'DECEPTUS_MECH_DESTRUCTIBLE': 11,
                     'DECEPTUS_MECH_GRAB_ROPE': 13, 'DECEPTUS_MECH_MOVEABLE_BOX': 14,
                     'MECHANISM_CRUSHER': 16, 'MECHANISM_FLOOR_SPIKES': 17, 'MECHANISM_SPIKE_BALL': 18,
                     'DECEPTUS_MECH_CHECKPOINT': 1}
            kind = names.get(kind, 0)
        if kind in mechanism_families:
            families.add(mechanism_families[kind])
        elif (kind == 1) and not (mechanism['flags'] & CHECKPOINT_HIDDEN):
            families.add('CHECKPOINT_CLOCK')
    if 'GRAB_ROPE' in families and not sword:
        families.add('PLAYER_ROPE')
    if Image.open(source / 'water.png').getbbox():
        families.add('SPLASH')
        if not sword:
            families.add('SWIM')
    enemy_families = {
        'DECEPTUS_RAT': ('RAT',),
        'DECEPTUS_ARROW_TRAP': ('TRAP', 'MUZZLE', 'ARROW'),
        'DECEPTUS_BLOB_2': ('BLOB',),
        'DECEPTUS_CANNON_2': ('CANNON', 'CANNONBALL'),
        'DECEPTUS_ORB': ('ORB',),
        'DECEPTUS_FROG_BLUE': ('FROG_BLUE', 'BLUE_FIREBALL'),
    }
    for enemy in enemies['definitions']:
        families.update(enemy_families[enemy['kind']])
    return families


def pack_room(data, recipe, source, images, records, families, output, sword=False):
    image = Image.open(source / 'catacombs.png').convert('RGB')
    foreground = Image.open(source / 'foreground.png').convert('RGBA')
    clips = source_clips(data, recipe, image)
    selected = room_families(source, recipe, sword)
    basename = 'graphics-sword' if sword else 'graphics'
    resources = [resource for family in families if family['name'] in selected
                 for resource in range(family['first'], family['first'] + family['count'])]
    actor_sheet = Image.new('RGBA', (256, sum((images[i].height + 1) for i in resources)))
    top = 0
    for index in resources:
        actor_sheet.paste(images[index], (0, top))
        top += images[index].height + 1
    colors = fit_palette(image, actor_sheet)
    # UI text needs an exact white slot, independently of the room's colours.
    colors[-1] = (255, 255, 255)
    background = map_image(image, colors)
    output.mkdir(parents=True, exist_ok=True)
    background.save(output / ('background-sword-preview.png' if sword else 'background-preview.png'))
    native_background = planar(np.pad(np.asarray(background),
                                     ((0, 0), (0, -image.width % 16))))
    (output / (basename + '.abg')).write_bytes(native_background)
    archive = Archive()
    palette = [(int(r) // 17) << 8 | (int(g) // 17) << 4 | int(b) // 17
               for r, g, b in colors]
    palette_offset = archive.append(struct.pack('>32H', *palette))
    patterns, lookup = [bytes(48)], {bytes(48): 0}

    def cells(frame, opaque):
        rgba = np.asarray(frame.convert('RGBA'))
        indices = np.asarray(map_image(frame, colors))
        mask = np.full(indices.shape, 255, dtype=np.uint8) if opaque else rgba[:, :, 3]
        result = np.zeros((frame.height // 8, frame.width // 8), dtype=np.uint16)
        for y in range(0, frame.height, 8):
            for x in range(0, frame.width, 8):
                visible = mask[y:y + 8, x:x + 8] >= 128
                pixels = np.where(visible, indices[y:y + 8, x:x + 8], 0)
                packed = planar(pixels) + np.packbits(visible, axis=1).tobytes()
                if packed not in lookup:
                    lookup[packed] = len(patterns)
                    patterns.append(packed)
                result[y // 8, x // 8] = lookup[packed]
        return result

    background_cells = cells(image, True)
    foreground_cells = cells(foreground, False)
    background_offset = archive.rows(background_cells)
    foreground_offset = archive.rows(foreground_cells)
    clip_entries = []
    for clip in clips:
        frames = np.concatenate([cells(frame, not clip.get('plane', 0))
                                 for frame in clip['frames']])
        map_offset = archive.rows(frames)
        durations = clip.get('frame_durations', [])
        duration_offset = archive.append(struct.pack(f'>{len(durations)}H', *durations)) if durations else 0
        clip_entries.append(struct.pack(
            '>4h4H2B2I', *clip['bounds'], clip['frame_count'], clip['frame_ticks'],
            clip.get('required_flags', 0), clip.get('forbidden_flags', 0),
            clip.get('plane', 0), 0, duration_offset, map_offset))
    clip_offset = archive.append(b''.join(clip_entries))
    packed_tiles, tile_offsets = bytearray(), []
    for start in range(0, len(patterns), 16):
        tile_offsets.append(len(packed_tiles))
        group = b''.join(patterns[start:start + 16]).ljust(16 * 48, b'\0')
        packed_tiles.extend(encode_block(group))
    tile_offsets.append(len(packed_tiles))
    tile_table = archive.append(struct.pack(f'>{len(tile_offsets)}I', *tile_offsets))
    tile_data = archive.append(packed_tiles)
    sprite_entries, packed_sprites, unique = [], bytearray(), {}
    maximum_sprite = 0
    mirrors = []

    def sprite_block(raw):
        packed = encode_block(raw)
        if packed not in unique:
            unique[packed] = len(packed_sprites)
            packed_sprites.extend(packed)
        return unique[packed], len(packed)

    for index in resources:
        original = images[index]
        bounds = original.getchannel('A').getbbox() or (0, 0, 1, 1)
        trimmed = original.crop(bounds)
        visible = np.asarray(trimmed.getchannel('A')) >= 128
        pixels = np.where(visible, np.asarray(map_image(trimmed, colors)), 0)
        width = ((trimmed.width + 15) // 16 + 1) * 16
        pad = ((0, 0), (0, width - trimmed.width))
        raw = (planar(np.pad(pixels, pad))
               + np.packbits(np.pad(visible, pad), axis=1).tobytes())
        maximum_sprite = max(maximum_sprite, len(raw))
        offset, size = sprite_block(raw)
        trim_x, trim_y = records[index].get('trim_offset', [0, 0])
        sprite_entries.append([
            index, trim_x + bounds[0], trim_x + original.width - bounds[2],
            trim_y + bounds[1], trimmed.width, trimmed.height, offset, size, 0, 0])
        mirrors.append(planar(np.pad(pixels[:, ::-1], pad))
                       + np.packbits(np.pad(visible[:, ::-1], pad), axis=1).tobytes())
    # keep only mirrors that already exist as stored blocks: renderers mirror a frame once when
    # caching it, and duplicated orientations cost resident memory in every room.
    budget = len(packed_sprites)
    mirrored_count = 0
    for entry, raw in zip(sprite_entries, mirrors):
        packed = encode_block(raw)
        if packed not in unique and len(packed_sprites) + len(packed) > budget:
            continue
        if packed not in unique:
            unique[packed] = len(packed_sprites)
            packed_sprites.extend(packed)
        entry[-2:] = unique[packed], len(packed)
        mirrored_count += 1
    sprite_table = archive.append(b''.join(struct.pack('>H3h2H4I', *entry)
                                          for entry in sprite_entries))
    sprite_bytes = len(packed_sprites)
    (output / (basename + '.asp')).write_bytes(packed_sprites)
    if len(patterns) > 65535:
        raise ValueError('Amiga tile index exceeds a word')
    struct.pack_into('>4s6H8I', archive.data, 0, b'AG03', image.width, image.height,
                     len(patterns), len(resources), len(clips), 16,
                     palette_offset, tile_table, tile_data, background_offset,
                     foreground_offset, clip_offset, sprite_table, sprite_bytes)
    payload = bytes(archive.data)
    (output / (basename + '.agp')).write_bytes(payload)
    raster, composite = raster_columns(patterns, background_cells, foreground_cells)
    if clips:
        composite = b''
    (output / (basename + '.acr')).write_bytes(raster)
    (output / (basename + '.acp')).write_bytes(composite)
    return dict(basename=basename, bytes=len(payload) + sprite_bytes, world_bytes=len(payload), patterns=len(patterns), sprites=len(resources),
                sprite_bytes=len(packed_sprites), tile_bytes=len(packed_tiles),
                background_bytes=len(native_background),
                background_sha256=hashlib.sha256(native_background).hexdigest(),
                raster_bytes=len(raster), raster_sha256=hashlib.sha256(raster).hexdigest(),
                composite_bytes=len(composite), composite_sha256=hashlib.sha256(composite).hexdigest(),
                max_sprite_bytes=maximum_sprite, mirrored_sprites=mirrored_count, families=sorted(selected),
                clips=len(clips), color_error=color_error(image, background),
                sha256=hashlib.sha256(payload).hexdigest(),
                sprites_sha256=hashlib.sha256(packed_sprites).hexdigest())


def converter_identity():
    names = ('amiga_opening_assets.py', 'amiga_palette.py', 'room_content.py',
             'source_sprites.py', 'lz4_block.py', 'rle.py', 'pixel_font.py')
    return hashlib.sha256(b''.join((ROOT / 'tools' / name).read_bytes()
                                  for name in names)).hexdigest()


def write_common(data, output, recipes, simulation, families, sprite_bytes, clip_count, rooms):
    from pixel_font import NativePixelFont

    limits = simulation['capacities']
    header = ['#ifndef DECEPTUS_AMIGA_OPENING_ASSETS_H',
              '#define DECEPTUS_AMIGA_OPENING_ASSETS_H',
              '#include "deceptus/appearance.h"',
              '#define AMIGA_OPENING_ROOMS ' + str(len(recipes)),
              '#define AMIGA_RESOURCE_NORMAL_BYTES ' + str(RESOURCE_NORMAL_BYTES) + 'UL',
              '#define AMIGA_RESOURCE_CHIP_BYTES ' + str(RESOURCE_CHIP_BYTES) + 'UL',
              '#define AMIGA_ROOM_BYTES ' + str(limits['bytes']),
              '#define AMIGA_ROOM_MECHANISMS ' + str(limits['mechanisms']),
              '#define AMIGA_ROOM_TARGETS ' + str(limits['targets']),
              '#define AMIGA_ROOM_ENEMIES ' + str(limits['enemies']),
              '#define AMIGA_ROOM_PATHS ' + str(limits['paths']),
              '#define AMIGA_SPRITE_SCRATCH_BYTES ' + str(sprite_bytes),
              '#define AMIGA_OPENING_CLIPS ' + str(max(1, clip_count)),
              'extern const DeceptusAdventureDef amiga_tutorial_definition;',
              'extern const DeceptusAppearanceDef amiga_appearance_definition;',
              'extern const DeceptusU16 amiga_sprite_families[DECEPTUS_SPRITE_FAMILY_COUNT];',
              'extern const char *const amiga_room_titles[AMIGA_OPENING_ROOMS];',
              'extern const DeceptusU32 amiga_raster_sizes[AMIGA_OPENING_ROOMS][2];',
              'extern const DeceptusU16 amiga_font[95][18];',
              'extern const DeceptusU8 amiga_font_advances[95];', '#endif']
    (output / 'amiga_opening_assets.h').write_text('\n'.join(header) + '\n')
    source = (ROOT / 'assets/generated/gba/room.c').read_text()
    first = source.index('static const char *const tutorial_pages_0_0')
    last = source.index('const GbaUiResource gba_ui_resources', first)
    bindings = source[first:last].replace('gba_campaign_definition', 'amiga_campaign_definition')
    bindings = bindings.replace('gba_tutorial_definition', 'amiga_tutorial_definition')
    lines = ['#include "amiga_opening_assets.h"',
             'static const DeceptusCampaignDef amiga_campaign_definition = {0,0,'
             + str(len(recipes)) + '};', bindings]
    lines.append('const DeceptusU32 amiga_raster_sizes[AMIGA_OPENING_ROOMS][2] = {')
    sizes = {room['room']: (room['raster_bytes'], room['sword']['raster_bytes'])
             for room in rooms}
    lines.extend('   {' + ','.join(map(str, sizes.get(index, (0, 0)))) + '},'
                 for index in range(len(recipes)))
    lines.append('};')
    for stem in ('sword', 'headtorch'):
        source = (ROOT / 'assets/generated/gba/area0' / f'gba_{stem}_resources.h').read_text()
        source = source.replace('GBA_', 'AMIGA_').replace('gba_', 'amiga_').replace('Gba', 'Amiga')
        (output / f'amiga_{stem}_resources.h').write_text(source)
        lines.append(f'#include "amiga_{stem}_resources.h"')
    lines.extend([
        'const DeceptusAppearanceDef amiga_appearance_definition = {',
        '   {amiga_sword_clips, amiga_sword_durations, sizeof(amiga_sword_durations),',
        '    sizeof(amiga_sword_clips)/sizeof(amiga_sword_clips[0])},',
        '   amiga_headtorch_normal, amiga_headtorch_swim, amiga_headtorch_rope,',
        '   amiga_headtorch_sword, amiga_headtorch_lamp_offsets, 0};',
        'const DeceptusU16 amiga_sprite_families[DECEPTUS_SPRITE_FAMILY_COUNT] = {'
        + ','.join(str(family['first']) for family in families) + '};',
        'const char *const amiga_room_titles[AMIGA_OPENING_ROOMS] = {'
        + ','.join(json.dumps(recipe['title']) for recipe, _ in recipes) + '};'])
    font = NativePixelFont(data / 'fonts')
    glyphs, advances = [], []
    for code in range(32, 127):
        visible = np.asarray(font.glyph(chr(code)).getchannel('A')) >= 128
        glyphs.append([sum(int(bit) << (15 - x) for x, bit in enumerate(row))
                       for row in visible])
        advances.append(round(font.metrics.getlength(chr(code))))
    lines.append('const DeceptusU16 amiga_font[95][18] = {')
    lines.extend('   {' + ','.join(map(str, glyph)) + '},' for glyph in glyphs)
    lines.extend(['};', 'const DeceptusU8 amiga_font_advances[95] = {'
                  + ','.join(map(str, advances)) + '};'])
    (output / 'opening_content.c').write_text('\n'.join(lines) + '\n')


def compile_opening(data, output, room_indices=None):
    data, output = Path(data).resolve(), Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    recipes, identity = ensure_original_imports(data)
    simulation = compile_rooms(amiga_recipes(recipes), output / 'rooms')
    images, records = source_sprites(data, output)
    families = sprite_families(len(images))
    capacity = int(re.search(r'#define AMIGA_GRAPHICS_RESOURCES (\d+)',
                             (ROOT / 'src/platform/amiga/graphics.h').read_text()).group(1))
    if len(images) > capacity:
        raise ValueError(f'{len(images)} sprite resources exceed AMIGA_GRAPHICS_RESOURCES ({capacity})')
    reports = []
    for index in (range(len(recipes)) if room_indices is None else room_indices):
        recipe, source = recipes[index]
        report = pack_room(data, recipe, source, images, records, families,
                           output / f'area{index}')
        sword = pack_room(data, recipe, source, images, records, families,
                          output / f'area{index}', sword=True)
        reports.append(dict(room=index, title=recipe['title'], sword=sword, **report))
        print(f"Amiga room {index}: {report['bytes']} bytes, "
              f"{report['patterns']} tiles, {report['sprites']} sprites; sword {sword['bytes']} bytes", flush=True)
    write_common(data, output, recipes, simulation, families,
                 max(variant['max_sprite_bytes'] for room in reports for variant in (room, room['sword'])),
                 max(room['clips'] for room in reports), reports)
    report = dict(source_identity=identity, full_game=False, format='AG03',
                  converter_sha256=converter_identity(),
                  pixel_scale=1, dithering=False, families=families,
                  simulation=simulation['capacities'], rooms=reports)
    (output / 'assets.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, default=ROOT / 'assets/game/data')
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'build/amiga-opening/assets')
    parser.add_argument('--rooms', type=int, nargs='+')
    args = parser.parse_args()
    if args.rooms and any(not 0 <= index < 26 for index in args.rooms):
        parser.error('room indices must be 0..25')
    compile_opening(args.data, args.output_dir, args.rooms)


if __name__ == '__main__':
    main()
