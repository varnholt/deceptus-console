"""Package flattened OCS room layers for the streaming renderer."""

import struct

import numpy as np

from amiga_tiles import native_tiles
from lz4_block import encode_block


TILES_PER_BANK = 16


def tile_banks(tiles):
    """Bound each independent decode to sixteen native tiles."""
    payload, offsets = bytearray(), [0]
    for first in range(0, len(tiles), TILES_PER_BANK):
        words = [word for tile in tiles[first:first + TILES_PER_BANK]
                 for word in tile]
        payload.extend(encode_block(b''.join(word.to_bytes(2, 'big')
                                            for word in words)))
        offsets.append(len(payload))
    return bytes(payload), offsets


def room_tiles(base, width, height):
    """Keep five-plane static tiles and foreground masks in separate dictionaries."""
    row_bytes = ((width + 15) // 16) * 2
    if width <= 0 or height <= 0 or len(base) != 6 * row_bytes * height:
        raise ValueError('invalid flattened room dimensions or plane data')
    planes = np.frombuffer(base, dtype=np.uint8).reshape(6, height, row_bytes)
    bits = np.unpackbits(planes, axis=2)
    pixels = np.zeros((height, row_bytes * 8), dtype=np.uint8)
    for plane in range(5):
        pixels |= bits[plane] << plane
    padding = ((0, -height % 16), (0, 0))
    colors, cells = native_tiles(np.pad(pixels, padding))
    masks, mask_cells = native_tiles(np.pad(bits[5], padding), planes=1)
    columns, rows = row_bytes // 2, (height + 15) // 16
    header_size = struct.calcsize('>4s6H4I')
    color_map = header_size
    mask_map = color_map + len(cells) * 2
    color_data, color_offsets = tile_banks(colors)
    mask_data, mask_offsets = tile_banks(masks)
    color_table = mask_map + len(mask_cells) * 2
    mask_table = color_table + len(color_offsets) * 4
    header = struct.pack('>4s6H4I', b'AT02', width, height, columns, rows,
                         len(colors), len(masks), color_map, mask_map,
                         color_table, mask_table)
    maps = b''.join(word.to_bytes(2, 'big') for word in cells + mask_cells)
    offsets = color_offsets + [offset + len(color_data) for offset in mask_offsets]
    tables = struct.pack(f'>{len(offsets)}I', *offsets)
    return header + maps + tables + color_data + mask_data
