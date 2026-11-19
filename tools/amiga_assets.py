"""Convert the lossless one-room import to Amiga OCS bitplanes."""

import json
from pathlib import Path

import numpy as np
from PIL import Image

from assets import ACTOR_FRAME_COUNT, ROOT, import_deceptus
from amiga_palette import color_error, fit_palette, map_image
from amiga_tiles import interleaved_words, native_tiles
from world_export import export_world


def write_array(name, values, chip=False):
    rows = [', '.join(f'0x{value:04x}' for value in values[start:start + 12])
            for start in range(0, len(values), 12)]
    section = ' __attribute__((section(".MEMF_CHIP")))' if chip else ''
    return (f'const DeceptusU16 {name}[{len(values)}]{section} = {{\n' +
            ',\n'.join('   ' + row for row in rows) + '\n};\n')


def convert(data, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    source = output / 'source'
    import_deceptus(data, source=source)
    recipe = json.loads((ROOT / 'assets/rooms/catacombs.json').read_text())
    recipe['patrol'] = [-1, -1]
    recipe['door'] = [-1, -1]
    export_world(source, output, recipe)
    original_background = Image.open(source / 'catacombs.png').convert('RGB')
    original_actors = Image.open(source / 'actors.png').convert('RGBA')
    manifest = json.loads((source / 'import.json').read_text())
    for frame, definition in enumerate(manifest['frames']):
        if not definition['animation'].startswith('player_'):
            left, top = (frame % 4) * 32, (frame // 4) * 48
            original_actors.paste((0, 0, 0, 0), (left, top, left + 32, top + 48))
    colors = fit_palette(original_background, original_actors)
    background = map_image(original_background, colors)
    actors = map_image(original_actors, colors)
    actor_preview = actors.convert('RGBA')
    actor_preview.putalpha(original_actors.getchannel('A'))
    background.save(output / 'background-preview.png')
    actor_preview.save(output / 'actors-preview.png')
    pixels = np.pad(np.asarray(background, dtype=np.uint8), ((0, 0), (16, 16)))
    width, height = background.size
    if width % 16 or not 320 <= width <= 512 or height != 224:
        raise ValueError(
            'Amiga prototype needs a word-aligned room 320..512 pixels wide and 224 high')
    tiles, cells = native_tiles(np.asarray(background, dtype=np.uint8))
    (output / 'native-tiles.bin').write_bytes(
        b''.join(word.to_bytes(2, 'big') for tile in tiles for word in tile))
    (output / 'native-map.bin').write_bytes(
        b''.join(cell.to_bytes(2, 'big') for cell in cells))
    actor_words, masks = [], []
    for frame in range(ACTOR_FRAME_COUNT):
        left, top = (frame % 4) * 32, (frame // 4) * 48
        image = np.array(actors.crop((left, top, left + 32, top + 48)))
        alpha = np.array(original_actors.getchannel('A').crop(
            (left, top, left + 32, top + 48))) >= 128
        for flipped in (False, True):
            values = np.fliplr(image) if flipped else image
            values = np.pad(values, ((0, 0), (0, 16)))
            visible = np.fliplr(alpha) if flipped else alpha
            visible = np.pad(visible, ((0, 0), (0, 16)))
            masks.extend(interleaved_words(visible.astype(np.uint8) * 31, 5))
            actor_words.extend(interleaved_words(np.where(visible, values, 0), 5))
    palette = [(int(red) // 17) << 8 | (int(green) // 17) << 4 | int(blue) // 17
               for red, green, blue in colors]
    payload = '#include "amiga_assets.h"\n'
    for name, values in (
            ('amiga_background', interleaved_words(pixels, 5)),
            ('amiga_actors', actor_words), ('amiga_masks', masks),
            ('amiga_palette', palette)):
        payload += write_array(name, values, name != 'amiga_palette')
    (output / 'amiga_assets.c').write_text(payload)
    (output / 'amiga_assets.h').write_text(
        '#ifndef DECEPTUS_AMIGA_ASSETS_H\n#define DECEPTUS_AMIGA_ASSETS_H\n'
        '#include "deceptus/types.h"\n'
        f'#define AMIGA_ROOM_WIDTH {width}\n#define AMIGA_ROOM_HEIGHT {height}\n'
        f'#define AMIGA_PLANES 5\n#define AMIGA_ACTOR_FRAMES {ACTOR_FRAME_COUNT}\n'
        'extern const DeceptusU16 amiga_background[];\n'
        'extern const DeceptusU16 amiga_actors[];\n'
        'extern const DeceptusU16 amiga_masks[];\n'
        'extern const DeceptusU16 amiga_palette[];\n#endif\n')
    report = dict(scope='one-room Amiga OCS prototype', full_game=False,
                  source_size=[width, height], viewport=[320, 224],
                  pixel_scale=1, dithering=False, bitplanes=5,
                  bitplane_layout='scanline-interleaved',
                  native_tiles=dict(size=[16, 16], count=len(tiles),
                                    tile_bytes=len(tiles) * 160,
                                    map_size=[width // 16, height // 16],
                                    map_bytes=len(cells) * 2,
                                    runtime_streaming=False),
                  background_bytes=(width + 32) * height * 5 // 8,
                  framebuffer_bytes=(width + 32) * height * 5 // 4,
                  actor_bytes=(len(actor_words) + len(masks)) * 2,
                  palette_mode='shared RGB444',
                  background_colors=len(np.unique(np.asarray(background))),
                  color_error=dict(background=color_error(original_background, background),
                                   actors=color_error(original_actors, actor_preview)),
                  original_import=json.loads((source / 'import.json').read_text()))
    (output / 'assets.json').write_text(json.dumps(report, indent=2) + '\n')
    return report
