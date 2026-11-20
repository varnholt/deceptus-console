"""Package flattened OCS room layers for the streaming renderer.

Rooms keep 16x16 cells for scrolling, dirty tracking and animation, but each cell refers to
four entries of an 8x8 dictionary. Small tiles repeat far more often in hand-drawn rooms, so
the dictionary and its decode cache fit the A500's memory; the CPU combines two 8x8 rows
into one bitplane word when painting, so dictionary data needs no Chip RAM.
"""

import struct

import numpy as np

from lz4_block import encode_block


TILES_PER_BANK = 32
# playfield bitplanes. lowres OCS DMA for up to four planes uses only cycles the CPU never needs;
# a fifth plane takes CPU bus slots on every visible line, slowing all code and slow-RAM access.
PLANES = 4
COLOR_TILE_BYTES = 8 * PLANES
MASK_TILE_BYTES = 8
BLOCK_CELLS = 4
HEADER = '>4s6H4I'


# banks stored raw are read in place: no decode time and no cache memory at runtime. the banks LZ4
# helps least go raw first, until raw storage has grown the archive by RAW_GROWTH_BYTES.
RAW_GROWTH_BYTES = 40 * 1024


def tile_banks(tiles, raw_growth=RAW_GROWTH_BYTES):
    """Bound each independent decode to TILES_PER_BANK tiles; the last bank holds only the rest.

    A bank stored with exactly its raw size is raw. A stored LZ4 block is always shorter than its
    input (a block that is not is stored raw at no growth), so the two never collide."""
    groups = [b''.join(tiles[first:first + TILES_PER_BANK]) for first in range(0, len(tiles), TILES_PER_BANK)]
    blocks = [encode_block(group) for group in groups]
    raw = [len(block) >= len(group) for group, block in zip(groups, blocks)]
    growth = 0
    for index in sorted(range(len(groups)), key=lambda index: len(groups[index]) - len(blocks[index])):
        cost = len(groups[index]) - len(blocks[index])
        if not raw[index] and growth + cost <= raw_growth:
            raw[index] = True
            growth += cost
    payload, offsets = bytearray(), [0]
    for group, block, stored in zip(groups, blocks, raw):
        payload.extend(group if stored else block)
        offsets.append(len(payload))
    return bytes(payload), offsets


def _plane_bytes(pixels, planes):
    """Return (planes, height, width / 8) bitplane bytes."""
    return np.stack([np.packbits((pixels >> plane) & 1, axis=1) for plane in range(planes)])


class Dictionary:
    """Deduplicated 8x8 tiles; an optional reserved first tile keeps id 0 meaningful."""

    def __init__(self, reserved=None, limit=65536):
        self.tiles, self.lookup, self.limit = [], {}, limit
        if reserved is not None:
            self.intern(reserved)

    def intern(self, tile):
        tile_id = self.lookup.get(tile)
        if tile_id is None:
            tile_id = len(self.tiles)
            if tile_id >= self.limit:
                raise ValueError(f'native 8x8 dictionary exceeds {self.limit} entries')
            self.lookup[tile] = tile_id
            self.tiles.append(tile)
        return tile_id


def _cell_tiles(colors, masks, image_planes, mask_plane, cell_x, cell_y):
    """Intern the four 8x8 quarters of one 16x16 cell, top-left first."""
    color_ids, mask_ids = [], []
    for dy in (0, 1):
        for dx in (0, 1):
            top, column = cell_y * 16 + dy * 8, cell_x * 2 + dx
            # rows outermost, then planes: one byte per plane row.
            color_ids.append(colors.intern(image_planes[:, top:top + 8, column].T.tobytes()))
            mask_ids.append(masks.intern(mask_plane[top:top + 8, column].tobytes()))
    return color_ids, mask_ids


def _block_order(columns, rows):
    return [(x, y) for top in range(0, rows, BLOCK_CELLS) for left in range(0, columns, BLOCK_CELLS)
            for y in range(top, min(top + BLOCK_CELLS, rows)) for x in range(left, min(left + BLOCK_CELLS, columns))]


def _room_tiles(base, width, height, world=None, overlays=None, raw_growth=RAW_GROWTH_BYTES):
    """Keep PLANES-plane 8x8 tiles and one-plane foreground masks in separate dictionaries."""
    row_bytes = ((width + 15) // 16) * 2
    if width <= 0 or height <= 0 or len(base) != 6 * row_bytes * height:
        raise ValueError('invalid flattened room dimensions or plane data')
    planes = np.frombuffer(base, dtype=np.uint8).reshape(6, height, row_bytes)
    rows = (height + 15) // 16
    padded = np.pad(planes, ((0, 0), (0, rows * 16 - height), (0, 0)))
    columns = row_bytes // 2
    colors = Dictionary()
    # mask id 0 is always the empty mask so the renderer can skip foreground work without a lookup.
    masks = Dictionary(bytes(MASK_TILE_BYTES))
    color_cells = [None] * (columns * rows)
    mask_cells = [None] * (columns * rows)
    # 4x4-cell blocks keep neighbouring cells in few decode banks, so scrolling touches few banks.
    for x, y in _block_order(columns, rows):
        color_cells[y * columns + x], mask_cells[y * columns + x] = _cell_tiles(
            colors, masks, padded[:PLANES], padded[5], x, y)
    catalogue = b''
    if world is not None:
        bits = np.unpackbits(planes[5], axis=1)
        catalogue = animation_tiles(world, overlays, bits, colors, masks)
    # most cells have no foreground and the rest repeat few mask quads: cells index an interned
    # quad table whose entry 0 is the empty quad.
    quads = Dictionary((0, 0, 0, 0))
    quad_cells = [quads.intern(tuple(cell)) for cell in mask_cells]
    header_size = struct.calcsize(HEADER)
    color_map = header_size
    mask_map = color_map + len(color_cells) * 8
    quad_table = mask_map + len(quad_cells) * 2
    color_data, color_offsets = tile_banks(colors.tiles, raw_growth)
    mask_data, mask_offsets = tile_banks(masks.tiles)
    color_table = quad_table + len(quads.tiles) * 8
    mask_table = color_table + len(color_offsets) * 4
    header = struct.pack(HEADER, b'AT04', width, height, columns, rows,
                         len(colors.tiles), len(masks.tiles), color_map, mask_map,
                         color_table, mask_table)
    maps = (b''.join(struct.pack('>4H', *cell) for cell in color_cells)
            + struct.pack(f'>{len(quad_cells)}H', *quad_cells)
            + b''.join(struct.pack('>4H', *quad) for quad in quads.tiles))
    offsets = color_offsets + [offset + len(color_data) for offset in mask_offsets]
    tables = struct.pack(f'>{len(offsets)}I', *offsets)
    return header + maps + tables + color_data + mask_data, catalogue


def animation_tiles(world, overlays, foreground, colors, masks):
    """Intern changed-cell frame sets into the room's 8x8 dictionaries."""
    header = struct.unpack_from('>4s6H8I', world)
    count, table = header[5], header[12]
    data_start = 8 + (count + 1) * 4
    records, offsets = bytearray(), [0]
    for index in range(count):
        clip = struct.unpack_from('>4h4H2B2I', world, table + index * 26)
        x, y, width, height, frames = clip[:5]
        left, top = x // 16, y // 16
        row_bytes = ((x % 16 + width + 15) // 16) * 2
        columns = row_bytes // 2
        rows = (y % 16 + height + 15) // 16
        first, last = struct.unpack_from('>2I', overlays, 8 + index * 4)
        pixels = np.frombuffer(overlays[data_start + first:data_start + last],
                               dtype=np.uint8).reshape(frames, 6, height, row_bytes)
        records.extend(struct.pack('>2h3H', left, top, columns, rows, frames))
        for frame in pixels:
            bits = np.unpackbits(frame, axis=2)
            image = np.zeros((height, row_bytes * 8), dtype=np.uint8)
            for plane in range(5):
                image |= bits[plane] << plane
            alpha = bits[5].copy()
            if not clip[8]:
                # static foreground remains visible above a background animation.
                fx, fy = left * 16, y
                x0, x1 = max(0, fx), min(foreground.shape[1], fx + image.shape[1])
                y0, y1 = max(0, fy), min(foreground.shape[0], fy + height)
                if x0 < x1 and y0 < y1:
                    alpha[y0 - fy:y1 - fy, x0 - fx:x1 - fx] &= 1 - foreground[y0:y1, x0:x1]
            padding = ((y % 16, rows * 16 - y % 16 - height), (0, 0))
            image = np.pad(np.where(alpha, image, 0), padding)
            alpha = np.pad(alpha, padding)
            image_planes = _plane_bytes(image, PLANES)
            mask_plane = np.packbits(alpha, axis=1)
            for cell_y in range(rows):
                for cell_x in range(columns):
                    color_ids, mask_ids = _cell_tiles(colors, masks, image_planes, mask_plane, cell_x, cell_y)
                    records.extend(struct.pack('>8H', *color_ids, *mask_ids))
        offsets.append(len(records))
    return (struct.pack('>4s2H', b'AA02', count, 0)
            + struct.pack(f'>{len(offsets)}I', *offsets) + records)


def room_tiles(base, width, height):
    """Package a flattened static room without animated frame sets."""
    return _room_tiles(base, width, height)[0]


def opening_tiles(base, width, height, world, overlays, raw_growth=RAW_GROWTH_BYTES):
    """Return a shared native atlas and its separate animation-cell catalogue."""
    return _room_tiles(base, width, height, world, overlays, raw_growth)
