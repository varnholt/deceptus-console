"""Compile the Amiga dialogue boxes from the source message box artwork.

The GBA renders each page as a 224x80 bubble (tools/tutorial_ui.py). The Amiga shows the same
artwork on a separate copper-fed bitplane region, 304 pixels wide: the source window is 324
pixels wide and the GBA already composes it from a left and a right half, so the Amiga keeps
wider halves. Per background colour and next-page state one frame template is stored (LZ4,
interleaved planes, exactly the region the copper shows); per page only a one-bit text mask.
"""
import json
import re
from pathlib import Path

from PIL import Image

from assets import indexed
from content import map_objects
from lz4_block import encode_block
from tutorial_ui import TutorialUI

PLANES = 4
ROW_BYTES = 42  # 21 fetched words: the first one stays left of the display window.
BOX_WIDTH = 304
BOX_X = 8  # screen x of the box; bitmap bit = 16 + screen x.
REGION_TOP = 134  # screen row of the region: two empty rows load the palette, 80 bubble rows follow.
REGION_ROWS = 82
BUBBLE_ROW = 2
TEXT_WORD = 1  # masks cover the visible 20 words; glyphs start one pixel left of the text origin.
TEXT_WORDS = 20
DEFAULT_BACKGROUND = '#ff101ad4'


def rgb444(colour):
    return tuple(round(channel / 17) for channel in colour[:3])


def bubble_template(ui, background, next_page):
    """The GBA bubble without text, widened to BOX_WIDTH from the window's own halves."""
    frame = ui.window['window']['image']
    half = BOX_WIDTH // 2
    image = Image.new('RGBA', (BOX_WIDTH, 80))
    image.paste(tuple(bytes.fromhex(background[-6:])) + (255,), (3, 3, BOX_WIDTH - 3, 71))
    image.alpha_composite(frame.crop((0, 0, half, 78)), (0, 0))
    image.alpha_composite(frame.crop((frame.width - half, 0, frame.width, 78)), (half, 0))
    # the GBA places the button and the arrow relative to the right edge of its 224 pixel bubble.
    image.alpha_composite(ui.window['yes_xbox_1']['image'], (BOX_WIDTH - 60, 48))
    if next_page:
        image.alpha_composite(ui.window['next_page']['image'], (BOX_WIDTH - 20, 57))
    return image


def planar_region(indices):
    """indices[y][x] in box coordinates -> interleaved region rows of ROW_BYTES per plane."""
    data = bytearray(REGION_ROWS * PLANES * ROW_BYTES)
    for y, row in enumerate(indices):
        for x, index in enumerate(row):
            if not index:
                continue
            bit = 16 + BOX_X + x
            for plane in range(PLANES):
                if index >> plane & 1:
                    offset = ((BUBBLE_ROW + y) * PLANES + plane) * ROW_BYTES + bit // 8
                    data[offset] |= 0x80 >> (bit & 7)
    return bytes(data)


def template(ui, background, next_page):
    image = bubble_template(ui, background, next_page)
    # quantised like the GBA bubble (15 median-cut colours); a white strip stands in for the
    # text pixels the GBA quantises together with the frame.
    sample = Image.new('RGBA', (image.width, image.height + 2), (255, 255, 255, 255))
    sample.paste(image, (0, 0))
    # rounding to RGB444 first lets median cut spend all 15 entries on colours the Amiga can tell apart.
    sample.putdata([tuple(round(channel / 17) * 17 for channel in pixel[:3]) + (pixel[3],) for pixel in sample.getdata()])
    reduced = indexed(sample, transparent=True)
    rgb = reduced.getpalette()[:48]
    colours = [rgb444(rgb[index * 3:index * 3 + 3]) for index in range(16)]
    pixels = list(reduced.getdata())
    indices = [pixels[y * image.width:(y + 1) * image.width] for y in range(image.height)]
    text = min(range(1, 16), key=lambda index: sum((15 - channel) ** 2 for channel in colours[index]))
    palette = [0] + [r << 8 | g << 4 | b for r, g, b in colours[1:]]
    return dict(palette=palette, text=text, data=encode_block(planar_region(indices)))


def text_mask(ui, page):
    image = Image.new('RGBA', (BOX_WIDTH, 80))
    ui.text(image, (8, 4), page, ui.font, (255, 255, 255, 255))
    alpha = image.getchannel('A')
    rows = []
    for y in range(80):
        words = bytearray(TEXT_WORDS * 2)
        for x in range(BOX_WIDTH):
            if alpha.getpixel((x, y)) >= 128:
                bit = 16 + BOX_X + x - TEXT_WORD * 16
                words[bit // 8] |= 0x80 >> (bit & 7)
        rows.append(bytes(words))
    return rows


def room_pages(room_source):
    """the shared page strings per room and action, in the order of tutorial_actions_N."""
    pages = {}
    for room, action, body in re.findall(r'static const char \*const tutorial_pages_(\d+)_(\d+)\[\] = \{(.*?)\};', room_source):
        pages[int(room), int(action)] = json.loads('[' + body + ']')
    return pages


def compile_message_boxes(data, gba_generated, output):
    data, gba_generated, output = Path(data), Path(gba_generated), Path(output)
    ui = TutorialUI(data)
    tutorial = json.loads((gba_generated / 'tutorial.json').read_text())
    shared_pages = room_pages((gba_generated / 'room.c').read_text())
    objects = {}
    templates, template_keys, pages, masks, room_first, actions = [], {}, [], [], [], []
    action_index = {}
    for entry in tutorial['interactions']:
        room = entry['room']
        while len(room_first) <= room:
            room_first.append(len(actions))
        index = action_index.get(room, 0)
        action_index[room] = index + 1
        texts = shared_pages.get((room, index), [])
        if len(texts) != entry['pages']:
            raise ValueError(f'room {room} action {index}: {len(texts)} shared pages, report says {entry["pages"]}')
        if entry['source_map'] not in objects:
            objects[entry['source_map']] = {item['id']: item for item in map_objects(data / entry['source_map'])[1]}
        properties = objects[entry['source_map']][entry['source_id']]['properties']
        background = properties.get('01_background_color', DEFAULT_BACKGROUND)
        actions.append(len(pages))
        for page_index, text in enumerate(texts):
            key = background, page_index + 1 < len(texts)
            if key not in template_keys:
                template_keys[key] = len(templates)
                templates.append(template(ui, *key))
            pages.append((template_keys[key], text_mask(ui, text)))
    room_first.append(len(actions))
    text_rows = [y for _, rows in pages for y, row in enumerate(rows) if any(row)]
    top, bottom = (min(text_rows), max(text_rows) + 1) if text_rows else (0, 1)
    blob = bytearray()
    template_lines, page_lines = [], []
    for item in templates:
        template_lines.append('   {{' + ','.join(f'0x{value:03x}' for value in item['palette']) + '}, '
                              + f'{len(blob)}, {len(item["data"])}, {item["text"]}' + '},')
        blob += item['data']
    for template_id, rows in pages:
        packed = encode_block(b''.join(rows[top:bottom]))
        page_lines.append(f'   {{{len(blob)}, {len(packed)}, {template_id}}},')
        blob += packed
    header = ['#ifndef AMIGA_MESSAGE_BOX_H', '#define AMIGA_MESSAGE_BOX_H', '#include "deceptus/types.h"',
              f'#define AMIGA_BOX_PLANES {PLANES}', f'#define AMIGA_BOX_ROW_BYTES {ROW_BYTES}',
              f'#define AMIGA_BOX_TOP {REGION_TOP}', f'#define AMIGA_BOX_ROWS {REGION_ROWS}',
              f'#define AMIGA_BOX_BUBBLE_ROW {BUBBLE_ROW}', f'#define AMIGA_BOX_BUBBLE_ROWS 78',
              f'#define AMIGA_BOX_TEXT_TOP {BUBBLE_ROW + top}', f'#define AMIGA_BOX_TEXT_ROWS {bottom - top}',
              f'#define AMIGA_BOX_TEXT_WORD {TEXT_WORD}', f'#define AMIGA_BOX_TEXT_WORDS {TEXT_WORDS}',
              f'#define AMIGA_BOX_ROOMS {len(room_first) - 1}', f'#define AMIGA_BOX_PAGES {len(pages)}',
              'typedef struct AmigaBoxTemplate { DeceptusU16 palette[16]; DeceptusU32 offset, size; DeceptusU8 text; } AmigaBoxTemplate;',
              'typedef struct AmigaBoxPage { DeceptusU32 offset; DeceptusU16 size; DeceptusU8 template_id; } AmigaBoxPage;',
              'extern const AmigaBoxTemplate amiga_box_templates[];', 'extern const AmigaBoxPage amiga_box_pages[];',
              'extern const DeceptusU16 amiga_box_room_actions[AMIGA_BOX_ROOMS + 1];',
              'extern const DeceptusU16 amiga_box_action_pages[];',
              '/* in Chip RAM: Chip RAM has room to spare, the other memory holds the room data block. */',
              'extern DeceptusU8 amiga_box_blob[];', '#endif']
    (output / 'amiga_message_box.h').write_text('\n'.join(header) + '\n')
    source = ['#include "amiga_message_box.h"', 'const AmigaBoxTemplate amiga_box_templates[] = {', *template_lines, '};',
              'const AmigaBoxPage amiga_box_pages[] = {', *page_lines, '};',
              'const DeceptusU16 amiga_box_room_actions[AMIGA_BOX_ROOMS + 1] = {' + ','.join(map(str, room_first)) + '};',
              'const DeceptusU16 amiga_box_action_pages[] = {' + ','.join(map(str, actions + [len(pages)])) + '};',
              'DeceptusU8 amiga_box_blob[] __attribute__((section(".MEMF_CHIP"))) = {']
    source += ['   ' + ','.join(str(value) for value in blob[index:index + 32]) + ',' for index in range(0, len(blob), 32)]
    source.append('};')
    (output / 'amiga_message_box.c').write_text('\n'.join(source) + '\n')
    return dict(templates=len(templates), pages=len(pages), blob_bytes=len(blob), text_rows=bottom - top,
                region_bytes=REGION_ROWS * PLANES * ROW_BYTES)
