"""The inventory item pictures the consoles show, cut from the source inventory sheet."""
import json
from pathlib import Path
import re

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SHEET = 'item-icons.png'
# the source inventory_items.json entries for the adventure's item numbers 1, 2, ... in order.
ITEM_KEYS = ('locker_key', 'handle', 'gems', 'solar_seal', 'headtorch', 'sword', 'key')


def header_define(name):
    header = (ROOT / 'include/deceptus/itemicon.h').read_text()
    return int(re.search(rf'^#define {name} (\d+)$', header, re.M).group(1))


ICON_COUNT = header_define('ITEM_ICON_COUNT')
ICON_CELL = header_define('ITEM_ICON_CELL')
if ICON_COUNT != len(ITEM_KEYS):
    raise ValueError('itemicon.h and the item order disagree')


def item_spec(data, key):
    return json.loads((Path(data) / 'sprites/inventory_items.json').read_text())[key]


def item_icon(data, key):
    spec = item_spec(data, key)
    x, y, width, height = (spec[field] for field in ('x_px', 'y_px', 'width_px', 'height_px'))
    return Image.open(Path(data) / 'sprites/inventory_items.png').convert('RGBA').crop((x, y, x + width, y + height))


def compile_item_icons(data, output):
    """One row of ICON_CELL cells, each source item picture centred in its cell."""
    sheet = Image.new('RGBA', (ICON_CELL * ICON_COUNT, ICON_CELL))
    for index, key in enumerate(ITEM_KEYS):
        icon = item_icon(data, key)
        if icon.width > ICON_CELL or icon.height > ICON_CELL:
            raise ValueError(f'item picture {key} exceeds the {ICON_CELL}px cell')
        sheet.alpha_composite(icon, (index * ICON_CELL + (ICON_CELL - icon.width) // 2, (ICON_CELL - icon.height) // 2))
    sheet.save(Path(output) / SHEET)
