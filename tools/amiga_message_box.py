"""Compile the Amiga dialogue boxes from the source message box artwork.

The GBA renders each page as a 224x80 bubble (tools/tutorial_ui.py). The Amiga shows the same
artwork on a separate copper-fed bitplane region, 304 pixels wide: the source window is 324
pixels wide and the GBA already composes it from a left and a right half, so the Amiga keeps
wider halves. Per background colour and next-page state one frame template is stored (LZ4,
interleaved planes, exactly the region the copper shows); per page only a one-bit text mask.
Each room's templates and pages go into that room's own file, read with the room's graphics.
"""
import json
import re
import struct
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


def room_file(templates, actions, pages):
    """one room's boxes, big-endian for the 68000; offsets count from the start of the file.

    header: u16 templates, u16 actions, u16 pages, u16 0
    templates: u16 palette[16], u32 offset, u16 size, u8 text colour, u8 0 (40 bytes each)
    actions: u16 first page per action plus the end, padded to four bytes
    pages: u32 offset, u16 size, u8 template, u8 0 (8 bytes each), then the LZ4 data
    """
    tables = 8 + 40 * len(templates) + (2 * (len(actions) + 1) + 3 & ~3) + 8 * len(pages)
    blob = bytearray()
    template_rows, page_rows = [], []
    for item in templates:
        template_rows.append(struct.pack('>16HIHBB', *item['palette'], tables + len(blob), len(item['data']), item['text'], 0))
        blob += item['data']
    for template_id, packed in pages:
        page_rows.append(struct.pack('>IHBB', tables + len(blob), len(packed), template_id, 0))
        blob += packed
    action_table = struct.pack(f'>{len(actions) + 1}H', *actions, len(pages))
    action_table += bytes(-len(action_table) % 4)
    data = struct.pack('>4H', len(templates), len(actions), len(pages), 0) + b''.join(template_rows) + action_table + b''.join(page_rows)
    assert len(data) == tables
    return data + blob


def compile_message_boxes(data, gba_generated, output):
    """writes area<room>/messagebox.amb for every room with dialogue pages, plus the size table.

    the files are read with the room (the screen is already blank then) and decoded only while a
    dialogue shows, so nothing of the boxes sits in the executable or in memory at startup.
    """
    data, gba_generated, output = Path(data), Path(gba_generated), Path(output)
    ui = TutorialUI(data)
    tutorial = json.loads((gba_generated / 'tutorial.json').read_text())
    shared_pages = room_pages((gba_generated / 'room.c').read_text())
    objects, rooms, rendered, action_index = {}, {}, {}, {}
    for entry in tutorial['interactions']:
        room = entry['room']
        index = action_index.get(room, 0)
        action_index[room] = index + 1
        texts = shared_pages.get((room, index), [])
        if len(texts) != entry['pages']:
            raise ValueError(f'room {room} action {index}: {len(texts)} shared pages, report says {entry["pages"]}')
        if entry['source_map'] not in objects:
            objects[entry['source_map']] = {item['id']: item for item in map_objects(data / entry['source_map'])[1]}
        properties = objects[entry['source_map']][entry['source_id']]['properties']
        background = properties.get('01_background_color', DEFAULT_BACKGROUND)
        state = rooms.setdefault(room, dict(templates=[], keys={}, actions=[], pages=[]))
        state['actions'].append(len(state['pages']))
        for page_index, text in enumerate(texts):
            key = background, page_index + 1 < len(texts)
            if key not in rendered:
                rendered[key] = template(ui, *key)
            if key not in state['keys']:
                state['keys'][key] = len(state['templates'])
                state['templates'].append(rendered[key])
            state['pages'].append((state['keys'][key], text_mask(ui, text)))
    text_rows = [y for state in rooms.values() for _, rows in state['pages'] for y, row in enumerate(rows) if any(row)]
    top, bottom = (min(text_rows), max(text_rows) + 1) if text_rows else (0, 1)
    room_count = max(rooms) + 1 if rooms else 0
    sizes = [0] * room_count
    for room, state in rooms.items():
        path = output / f'area{room}' / 'messagebox.amb'
        if not state['pages']:
            path.unlink(missing_ok=True)
            continue
        pages = [(template_id, encode_block(b''.join(rows[top:bottom]))) for template_id, rows in state['pages']]
        blob = room_file(state['templates'], state['actions'], pages)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(blob)
        sizes[room] = len(blob)
    header = ['#ifndef AMIGA_MESSAGE_BOX_H', '#define AMIGA_MESSAGE_BOX_H', '#include "deceptus/types.h"',
              f'#define AMIGA_BOX_PLANES {PLANES}', f'#define AMIGA_BOX_ROW_BYTES {ROW_BYTES}',
              f'#define AMIGA_BOX_TOP {REGION_TOP}', f'#define AMIGA_BOX_ROWS {REGION_ROWS}',
              f'#define AMIGA_BOX_BUBBLE_ROW {BUBBLE_ROW}', '#define AMIGA_BOX_BUBBLE_ROWS 78',
              f'#define AMIGA_BOX_TEXT_TOP {BUBBLE_ROW + top}', f'#define AMIGA_BOX_TEXT_ROWS {bottom - top}',
              f'#define AMIGA_BOX_TEXT_WORD {TEXT_WORD}', f'#define AMIGA_BOX_TEXT_WORDS {TEXT_WORDS}',
              f'#define AMIGA_BOX_ROOMS {room_count}',
              '/* the layout of area<room>/messagebox.amb (tools/amiga_message_box.py room_file). */',
              'typedef struct AmigaBoxHeader { DeceptusU16 templates, actions, pages, reserved; } AmigaBoxHeader;',
              'typedef struct AmigaBoxTemplate { DeceptusU16 palette[16]; DeceptusU32 offset; DeceptusU16 size; DeceptusU8 text, reserved; } AmigaBoxTemplate;',
              'typedef struct AmigaBoxPage { DeceptusU32 offset; DeceptusU16 size; DeceptusU8 template_id, reserved; } AmigaBoxPage;',
              '/* bytes of each room file, 0 when the room has no dialogue pages. */',
              'extern const DeceptusU16 amiga_box_room_bytes[AMIGA_BOX_ROOMS];', '#endif']
    (output / 'amiga_message_box.h').write_text('\n'.join(header) + '\n')
    source = ['#include "amiga_message_box.h"',
              'const DeceptusU16 amiga_box_room_bytes[AMIGA_BOX_ROOMS] = {' + ','.join(map(str, sizes)) + '};']
    (output / 'amiga_message_box.c').write_text('\n'.join(source) + '\n')
    return dict(templates=len(rendered), pages=sum(len(state['pages']) for state in rooms.values()),
                room_bytes={room: size for room, size in enumerate(sizes) if size}, text_rows=bottom - top,
                region_bytes=REGION_ROWS * PLANES * ROW_BYTES)
