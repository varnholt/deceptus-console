"""Convert the lossless one-room import to Amiga OCS bitplanes."""

import json
from pathlib import Path

import numpy as np
from PIL import Image

from assets import ROOT, import_deceptus, indexed
from world_export import export_world


def rgb444(image):
    pixels = np.array(image.convert('RGBA'))
    pixels[:, :, :3] = ((pixels[:, :, :3].astype(np.uint16) + 8) // 17) * 17
    return Image.fromarray(pixels)


def planar_words(pixels, planes):
    words = []
    for plane in range(planes):
        packed = np.packbits((pixels >> plane) & 1, axis=1)
        words.extend(int.from_bytes(row, 'big') for row in packed.reshape(-1, 2))
    return words


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
    background = indexed(rgb444(Image.open(source / 'catacombs.png')))
    actors = indexed(rgb444(Image.open(source / 'actors.png')), transparent=True)
    for image in (background, actors):
        image.putpalette([((value + 8) // 17) * 17
                          for value in image.getpalette()])
    background.save(output / 'background-preview.png')
    actors.save(output / 'actors-preview.png')
    pixels = np.pad(np.asarray(background, dtype=np.uint8), ((0, 0), (16, 16)))
    width, height = background.size
    if width % 16 or not 320 <= width <= 512 or height != 224:
        raise ValueError(
            'Amiga prototype needs a word-aligned room 320..512 pixels wide and 224 high')
    actor_words, masks = [], []
    for frame in range(26):
        left, top = (frame % 4) * 32, (frame // 4) * 48
        image = np.array(actors.crop((left, top, left + 32, top + 48)))
        for flipped in (False, True):
            values = np.fliplr(image) if flipped else image
            values = np.pad(values, ((0, 0), (0, 16)))
            masks.extend(planar_words((values != 0).astype(np.uint8), 1))
            actor_words.extend(planar_words(np.where(values, values + 16, 0), 5))
    palette = []
    for image in (background, actors):
        colors = image.getpalette()
        for slot in range(16):
            red, green, blue = colors[slot * 3:slot * 3 + 3]
            palette.append(((red + 8) // 17) << 8 |
                           ((green + 8) // 17) << 4 | ((blue + 8) // 17))
    payload = '#include "amiga_assets.h"\n'
    for name, values in (
            ('amiga_background', planar_words(pixels, 5)),
            ('amiga_actors', actor_words), ('amiga_masks', masks),
            ('amiga_palette', palette)):
        payload += write_array(name, values, name != 'amiga_palette')
    (output / 'amiga_assets.c').write_text(payload)
    (output / 'amiga_assets.h').write_text(
        '#ifndef DECEPTUS_AMIGA_ASSETS_H\n#define DECEPTUS_AMIGA_ASSETS_H\n'
        '#include "deceptus/types.h"\n'
        f'#define AMIGA_ROOM_WIDTH {width}\n#define AMIGA_ROOM_HEIGHT {height}\n'
        '#define AMIGA_PLANES 5\n#define AMIGA_ACTOR_FRAMES 26\n'
        'extern const DeceptusU16 amiga_background[];\n'
        'extern const DeceptusU16 amiga_actors[];\n'
        'extern const DeceptusU16 amiga_masks[];\n'
        'extern const DeceptusU16 amiga_palette[];\n#endif\n')
    report = dict(scope='one-room Amiga OCS prototype', full_game=False,
                  source_size=[width, height], viewport=[320, 224],
                  pixel_scale=1, dithering=False, bitplanes=5,
                  background_bytes=(width + 32) * height * 5 // 8,
                  framebuffer_bytes=(width + 32) * height * 5 // 4,
                  actor_bytes=(len(actor_words) + len(masks)) * 2,
                  original_import=json.loads((source / 'import.json').read_text()))
    (output / 'assets.json').write_text(json.dumps(report, indent=2) + '\n')
    return report
