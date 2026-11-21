"""Encode indexed images as word-aligned, scanline-interleaved OCS tiles."""

import numpy as np


def interleaved_words(pixels, planes=5):
    """Order words by scanline, then plane, then horizontal word."""
    pixels = np.asarray(pixels)
    if (pixels.ndim != 2 or not np.issubdtype(pixels.dtype, np.integer)
            or pixels.shape[1] % 16
            or not 1 <= planes <= 8 or np.any(pixels < 0)
            or np.any(pixels >= (1 << planes))):
        raise ValueError('expected word-aligned indexed pixels within the plane range')
    packed = np.stack([np.packbits((pixels >> plane) & 1, axis=1)
                       for plane in range(planes)], axis=1)
    return [int.from_bytes(word, 'big') for word in packed.reshape(-1, 2)]


def native_tiles(pixels, planes=5):
    """Return deduplicated 16x16 tiles and a row-major map of 16-bit tile ids."""
    pixels = np.asarray(pixels)
    if pixels.ndim != 2 or any(size % 16 for size in pixels.shape):
        raise ValueError('native tile images must have dimensions divisible by 16')
    tiles, cells, lookup = [], [], {}
    for y in range(0, pixels.shape[0], 16):
        for x in range(0, pixels.shape[1], 16):
            words = interleaved_words(pixels[y:y + 16, x:x + 16], planes)
            key = tuple(words)
            tile_id = lookup.get(key)
            if tile_id is None:
                tile_id = len(tiles)
                if tile_id >= 65536:
                    raise ValueError('native tile dictionary exceeds 16-bit ids')
                lookup[key] = tile_id
                tiles.append(words)
            cells.append(tile_id)
    return tiles, cells
