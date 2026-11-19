"""Package flattened OCS room layers for the streaming renderer."""

import struct

import numpy as np

from amiga_tiles import native_tiles


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
    color_tiles = mask_map + len(mask_cells) * 2
    mask_tiles = color_tiles + len(colors) * 160
    header = struct.pack('>4s6H4I', b'AT01', width, height, columns, rows,
                         len(colors), len(masks), color_map, mask_map,
                         color_tiles, mask_tiles)
    words = cells + mask_cells
    words += [word for tile in colors for word in tile]
    words += [word for mask in masks for word in mask]
    return header + b''.join(word.to_bytes(2, 'big') for word in words)
