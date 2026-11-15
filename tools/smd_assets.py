"""Convert an independent lossless import to native Mega Drive tile data."""
import json
from pathlib import Path

import numpy as np
from PIL import Image
from assets import ROOT, import_deceptus, indexed
from palettes import banked_background
from world_export import export_world


def rgb333(image):
    values = np.asarray(image.convert('RGBA')).copy()
    levels = (values[:, :, :3].astype(np.uint16) * 7 + 127) // 255
    values[:, :, :3] = (levels * 255 + 3) // 7
    return Image.fromarray(values)


def palette_word(red, green, blue):
    return ((red * 7 + 127) // 255) << 1 | ((green * 7 + 127) // 255) << 5 | ((blue * 7 + 127) // 255) << 9


def packed_tile(tile):
    pixels = np.asarray(tile, dtype=np.uint8) & 15
    return bytes(((pixels[:, ::2] << 4) | pixels[:, 1::2]).flatten())


def array(label, values, kind, width):
    rows = [','.join(str(value) + ('UL' if kind == 'u32' else '') for value in values[start:start + width]) for start in range(0, len(values), width)]
    return f'const deceptus_{kind} {label}[{len(values)}] = {{\n' + ',\n'.join(rows) + '\n};\n'


def physical_tile(index):
    # the 128 tile slots at c000 hold the background map, not graphics.
    return index if index < 1536 else index + 128


def words(payload):
    return [int.from_bytes(payload[start:start + 4], 'big') for start in range(0, len(payload), 4)]


def convert(data, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    original = ROOT / 'assets/source/smd-poc'
    recipe = json.loads((ROOT / 'assets/rooms/catacombs.json').read_text())
    import_deceptus(data, source=original)
    export_world(original, output, recipe)
    background = Image.open(original / 'catacombs.png')
    converted, banks, metrics = banked_background(rgb333(background), bank_count=3)
    converted.save(output / 'background-preview.png')
    patterns, lookup, entries = [bytes(32)], {bytes(32): 0}, []
    for top in range(0, background.height, 8):
        for left in range(0, background.width, 8):
            image = converted.crop((left, top, left + 8, top + 8))
            tile = packed_tile(image)
            if tile not in lookup:
                lookup[tile] = len(patterns)
                patterns.append(tile)
                for transform, flag in ((Image.Transpose.FLIP_LEFT_RIGHT, 0x0800),
                                        (Image.Transpose.FLIP_TOP_BOTTOM, 0x1000),
                                        (Image.Transpose.ROTATE_180, 0x1800)):
                    lookup.setdefault(packed_tile(image.transpose(transform)), lookup[tile] | flag)
            bank = banks[(top // 8) * (background.width // 8) + left // 8]
            value = lookup[tile]
            entries.append(physical_tile(1 + (value & 0x7ff)) | (value & 0x1800) | bank << 13)
    actor_image = indexed(rgb333(Image.open(original / 'actors.png')), transparent=True)
    actor_image.save(output / 'actors-preview.png')
    actor_first = (1 + len(patterns) + 3) & ~3
    actor_patterns, actor_lookup, frames = [], {}, []
    for frame in range(26):
        for piece in range(6):
            x = (frame % 4) * 32 + (piece % 2) * 16
            y = (frame // 4) * 48 + (piece // 2) * 16
            packed = b''.join(packed_tile(actor_image.crop((x + column * 8, y + row * 8,
                                                          x + column * 8 + 8, y + row * 8 + 8)))
                              for column in range(2) for row in range(2))
            if not any(packed):
                frames.append(65535)
                continue
            if packed not in actor_lookup:
                actor_lookup[packed] = len(actor_patterns)
                actor_patterns.append(packed)
            frames.append(physical_tile(actor_first + actor_lookup[packed] * 4))
    total_tiles = actor_first + len(actor_patterns) * 4
    if total_tiles > 1664:
        raise ValueError(f'POC needs {total_tiles} tiles; preloaded VRAM budget is 1664')
    palette = []
    for image, count in ((converted, 48), (actor_image, 16)):
        colours = image.getpalette()
        palette.extend(palette_word(*colours[index * 3:index * 3 + 3]) for index in range(count))
    (output / 'smd_assets.c').write_text('#include "deceptus/types.h"\n' +
        array('smd_background_tiles', words(b''.join(patterns)), 'u32', 8) +
        array('smd_actor_tiles', words(b''.join(actor_patterns)), 'u32', 8) +
        array('smd_room_map', entries, 'u16', 16) + array('smd_palette', palette, 'u16', 16) +
        array('smd_frame_tiles', frames, 'u16', 12))
    (output / 'smd_assets.h').write_text('#include "deceptus/types.h"\n' +
        '\n'.join(f'extern const deceptus_{kind} {name}[];' for name, kind in (
            ('smd_background_tiles', 'u32'), ('smd_actor_tiles', 'u32'), ('smd_room_map', 'u16'),
            ('smd_palette', 'u16'), ('smd_frame_tiles', 'u16'))) +
        f'\n#define SMD_BACKGROUND_TILES {len(patterns)}\n#define SMD_ACTOR_FIRST {actor_first}\n#define SMD_ACTOR_TILES {len(actor_patterns) * 4}\n')
    report = dict(full_game=False, scope='one-room shared-engine proof of concept',
                  source_import='assets/source/smd-poc', source_size=list(background.size),
                  pixel_scale=1, dithering=False, resampling=False,
                  background_tiles=len(patterns), actor_tiles=len(actor_patterns) * 4,
                  total_tiles=total_tiles, palette_banks=4, palette_fit_after_rgb333=metrics,
                  original_import=json.loads((original / 'import.json').read_text()))
    (output / 'assets.json').write_text(json.dumps(report, indent=2) + '\n')
    return report
