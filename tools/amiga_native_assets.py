"""Incrementally derive DMA-ready layers from validated Amiga target archives."""

import hashlib
import json
import os
from pathlib import Path
import re
import struct

import numpy as np

from asset_decode import decode_lz4, unpackbits
from amiga_room_tiles import PLANES, RAW_GROWTH_BYTES, opening_tiles
from asset_resources import pack_resources
from amiga_clip_overrides import apply_plans, plan_clips
from lz4_block import encode_block


def _indices(planes):
    """(5, ..., bytes) bitplane bytes -> (..., pixels) colour indices."""
    bits = np.unpackbits(planes, axis=-1)
    result = np.zeros(bits.shape[1:], dtype=np.uint8)
    for plane in range(planes.shape[0]):
        result |= bits[plane] << plane
    return result


def _planes(indices, count):
    return np.stack([np.packbits((indices >> plane) & 1, axis=-1) for plane in range(count)])


def remap_planes(planes, mapping):
    """Remap the five colour planes of a (6, ...) plane stack, keeping its mask plane."""
    if mapping is None:
        return planes
    result = np.array(planes)
    result[:5] = _planes(mapping[_indices(planes[:5])], 5)
    return result


# how much an erased border between two colours counts against merging them, per bordering pixel
# pair, relative to one recoloured pixel.
EDGE_WEIGHT = 4.0
# sprite pixels against background pixels: characters stay on screen and draw the eye, while one
# room holds far more background pixels than all its sprite frames together.
SPRITE_WEIGHT = float(os.environ.get('AMIGA_SPRITE_WEIGHT', '1'))
# per-room colours to keep exactly when the palette is reduced: {"area9": ["#335"]}.
PALETTE_PINS = Path(__file__).resolve().parent.parent / 'assets/amiga/palette-pins.json'


def palette_pins(room):
    if not PALETTE_PINS.exists():
        return ()
    values = json.loads(PALETTE_PINS.read_text()).get(f'area{room}', [])
    return tuple(int(value.lstrip('#'), 16) for value in values)


def _merge(palette, members, weights, edges, count, pinned=()):
    """Greedily merge palette entries `members` into `count` clusters, cheapest visible damage first.

    A merge costs the colour error it adds over the pixels it recolours, plus EDGE_WEIGHT times the
    error over the borders it erases: colours that outline each other (a dark silhouette on a dark
    wall, an eye, a rim light) stay apart even when they cover few pixels. Unused entries merge at
    no cost. `pinned` 12-bit values keep a cluster of their own (an optional manual override).
    Returns (member lists, 12-bit centre colours).
    """
    rgb = np.array([[(value >> 8) & 15, (value >> 4) & 15, value & 15] for value in palette], dtype=float)
    scale = np.array([3.0, 4.0, 2.0])
    clusters = [[index] for index in members]
    weight = [float(weights[index]) for index in members]
    centre = [rgb[index].copy() for index in members]
    border = np.zeros((32, 32)) if edges is None else np.array(edges, dtype=float)
    border = [[float(border[a, b] + border[b, a]) for b in members] for a in members]
    fixed = [palette[index] in pinned for index in members]
    while len(clusters) > count:
        best = None
        for a in range(len(clusters)):
            for b in range(a + 1, len(clusters)):
                if fixed[a] or fixed[b]:
                    continue
                total = weight[a] + weight[b]
                error = float((((centre[a] - centre[b]) ** 2) * scale).sum())
                cost = (weight[a] * weight[b] / total + EDGE_WEIGHT * border[a][b]) * error if total else 0.0
                if best is None or cost < best[0]:
                    best = (cost, a, b)
        if best is None:
            raise ValueError(f'more pinned colours than {count} palette entries can hold')
        _, a, b = best
        total = weight[a] + weight[b]
        if total:
            centre[a] = (centre[a] * weight[a] + centre[b] * weight[b]) / total
        weight[a] = total
        clusters[a] += clusters.pop(b)
        weight.pop(b)
        centre.pop(b)
        fixed.pop(b)
        for row in border:
            row[a] += row[b]
        border[a] = [border[a][index] + border[b][index] for index in range(len(border[a]))]
        border.pop(b)
        for row in border:
            row.pop(b)
        border[a][a] = 0.0
    colours = []
    for value in centre:
        red, green, blue = (int(round(min(15, max(0, channel)))) for channel in value)
        colours.append((red << 8) | (green << 4) | blue)
    return clusters, colours


def reduce_palette(palette, weights, colors, edges=None, pinned=()):
    """Merge a 32-entry OCS palette to `colors` playfield entries (see _merge for the cost).

    Index 0 (backdrop and cleared ui boxes) and index 31 (ui text, drawn with every plane set) keep
    their colours at new indices 0 and colors - 1.
    """
    clusters, colours = _merge(palette, list(range(1, 31)), weights, edges, colors - 2, pinned)
    mapping = np.zeros(32, dtype=np.uint8)
    reduced = [palette[0]]
    for number, members in enumerate(clusters, 1):
        mapping[members] = number
    reduced += colours
    reduced += [palette[0]] * (colors - 1 - len(reduced))
    mapping[31] = colors - 1
    reduced.append(palette[31])
    return mapping, reduced


def sprite_palette(palette, weights, edges):
    """15 colours for attached hardware sprites (colour registers 17-31; 0 is transparent).

    Returns a 32-entry mapping from room palette indices to sprite colours 1-15 and the 15 colours.
    """
    used = [index for index in range(32) if weights[index]]
    clusters, colours = _merge(palette, used, weights, edges, min(15, len(used)))
    mapping = np.ones(32, dtype=np.uint8)
    for number, members in enumerate(clusters, 1):
        mapping[members] = number
    return mapping, colours + [0] * (15 - len(colours))


def native_layers(world, mapping=None, plans=None):
    """plans (from amiga_clip_overrides) list the source frames each re-timed clip keeps."""
    header = struct.unpack_from('>4s6H8I', world)
    width, height, patterns, _, clips = header[1:6]
    tile_table, tile_data, _, _, clip_table = header[8:13]
    tiles = []
    for group in range((patterns + 15) // 16):
        first, last = struct.unpack_from('>2I', world, tile_table + group * 4)
        tiles.append(decode_lz4(world[tile_data + first:tile_data + last], 768))
    tiles = np.frombuffer(b''.join(tiles), dtype=np.uint8).reshape(-1, 6, 8)
    if mapping is not None:
        # every layer and animation frame is built from these 8x8 patterns: (plane, tile, row byte).
        tiles = remap_planes(tiles.transpose(1, 0, 2)[..., None], mapping)[..., 0].transpose(1, 0, 2)

    def grid(offset, columns, rows):
        start = offset + (rows + 1) * 4
        result = []
        for row in range(rows):
            first, last = struct.unpack_from('>2I', world, offset + row * 4)
            result.append(np.frombuffer(unpackbits(world[start + first:start + last], columns * 2, 2), dtype='>u2'))
        return np.asarray(result)

    layers = []
    for offset in header[10:12]:
        layers.append(tiles[grid(offset, width // 8, height // 8)].transpose(2, 0, 3, 1).reshape(6, height, width // 8))
    background, foreground = layers
    mask = foreground[5:6]
    base = np.concatenate(((background[:5] & ~mask) | (foreground[:5] & mask), mask))
    base = np.pad(base, ((0, 0), (0, 0), (0, -base.shape[2] % 2))).tobytes()
    frames = []
    offsets = [0]
    for index in range(clips):
        clip = struct.unpack_from('>4h4H2B2I', world, clip_table + index * 26)
        x, y, w, h, count = clip[:5]
        if (x | y | w | h) & 7:
            return b'', b''
        cells = grid(clip[11], w // 8, h // 8 * count)
        padding = x % 16 // 8
        for frame in ([kept for kept, _ in plans[index]] if plans and index in plans else range(count)):
            image = tiles[cells[frame * (h // 8):(frame + 1) * (h // 8)]].transpose(2, 0, 3, 1).reshape(6, h, w // 8)
            image = np.pad(image, ((0, 0), (0, 0), (padding, -(padding + w // 8) % 2)))
            frames.append(image.tobytes())
        offsets.append(sum(map(len, frames)))
    overlay = (struct.pack('>4s2H', b'AL01', clips, 0)
               + struct.pack(f'>{len(offsets)}I', *offsets) + b''.join(frames))
    return base, overlay


def compact_world(world):
    header = list(struct.unpack_from('>4s6H8I', world))
    result = bytearray(48)
    palette = len(result)
    result.extend(world[header[7]:header[7] + 64])
    clips = []
    for index in range(header[5]):
        clip = list(struct.unpack_from('>4h4H2B2I', world, header[12] + index * 26))
        if clip[10]:
            duration = world[clip[10]:clip[10] + clip[4] * 2]
            clip[10] = len(result)
            result.extend(duration)
        clip[11] = 0
        clips.append(struct.pack('>4h4H2B2I', *clip))
    clip_table = len(result)
    result.extend(b''.join(clips))
    sprite_table = len(result)
    sprites = world[header[13]:header[13] + header[4] * 28]
    result.extend(sprites)
    header[0], header[3] = b'AN01', 0
    header[7:14] = [palette, 0, 0, 0, 0, clip_table, sprite_table]
    struct.pack_into('>4s6H8I', result, 0, *header)
    boundaries = []
    for index in range(header[4]):
        for offset in (12, 20):
            start, length = struct.unpack_from('>2I', sprites, index * 28 + offset)
            if length:
                boundaries.append(start + length)
    return bytes(result), sorted(set(boundaries))


# the player's animation families (normal, swim, sword, rope) are drawn by sprite DMA when the
# playfield leaves colour registers 16-31 free. three attached pairs are 48 pixels wide: the scroll
# fetch (DDFSTRT $30) takes sprite 7's DMA slot. wider frames stay blitter objects.
# 17, the worn headtorch, is drawn into the player's sprite in front of her.
HARDWARE_SPRITE_FAMILIES = (0, 9, 12, 17, 23)
HARDWARE_SPRITE_WIDTH = 48
HARDWARE_SPRITE_FLAG = 1
# light effects (the headtorch beam) become a checkerboard of the playfield's white inside their
# shape: it reads as light on any room palette, the way older hardware showed it.
DITHERED_FAMILIES = (18,)


def family_resources(generated, families):
    text = (Path(generated) / 'opening_content.c').read_text()
    found = re.search(r'amiga_sprite_families\[DECEPTUS_SPRITE_FAMILY_COUNT\] = \{([^}]*)\}', text)
    starts = [int(value) for value in found.group(1).split(',')]
    resources = set()
    for family in families:
        last = starts[family + 1] if family + 1 < len(starts) else 65536
        resources.update(range(starts[family], last))
    return resources


BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0 + 1 / 32.0


def dither_light(pixels, palette):
    """A light effect as ordered-dithered playfield white: dense where the source is bright, fading
    out with its brightness; dark source pixels (the halo) become transparent."""
    rgb = np.array([[(value >> 8) & 15, (value >> 4) & 15, value & 15] for value in palette], dtype=float)
    luminance = (rgb @ np.array([0.30, 0.59, 0.11])) / 15.0
    indices = _indices(pixels[:5])
    mask = np.unpackbits(pixels[5], axis=-1).astype(bool)
    level = np.where(mask, luminance[indices], 0.0)
    if level.max() > 0:
        level = level / level.max()
    height, width = level.shape
    lit = mask & (level > np.tile(BAYER, (height // 4 + 1, width // 4 + 1))[:height, :width])
    result = np.array(pixels)
    result[5] = np.packbits(lit.astype(np.uint8), axis=-1)
    for plane in range(5):
        # source index 31 is the playfield white in either palette (it maps to the top index).
        result[plane] = result[5]
    return result


def hardware_resources(generated):
    """Resource ids of the hardware sprite families (none while the playfield uses 32 colours)."""
    return family_resources(generated, HARDWARE_SPRITE_FAMILIES) if PLANES < 5 else set()


def _hardware(metadata, index, hardware):
    header = struct.unpack_from('>4s6H8I', metadata)
    resource, = struct.unpack_from('>H', metadata, header[13] + index * 28)
    width, = struct.unpack_from('>H', metadata, header[13] + index * 28 + 8)
    return resource in hardware and width <= HARDWARE_SPRITE_WIDTH


def sprite_frames(metadata, sprites, select=None):
    """Yield each distinct sprite frame as a (6, height, row_bytes) plane stack."""
    header = struct.unpack_from('>4s6H8I', metadata)
    seen = set()
    for index in range(header[4]):
        offset, size = struct.unpack_from('>2I', metadata, header[13] + index * 28 + 12)
        width, height = struct.unpack_from('>2H', metadata, header[13] + index * 28 + 8)
        row_bytes = ((width + 15) // 16 + 1) * 2
        if (offset, size) in seen or not size or (select is not None and not select(index)):
            continue
        seen.add((offset, size))
        yield np.frombuffer(decode_lz4(sprites[offset:offset + size], row_bytes * height * 6),
                            dtype=np.uint8).reshape(6, height, row_bytes)


def _borders(indices, visible):
    """32x32 counts of horizontally and vertically neighbouring visible pixels per colour pair."""
    counts = np.zeros(32 * 32, dtype=float)
    for first, second, keep in ((indices[:, :-1], indices[:, 1:], visible[:, :-1] & visible[:, 1:]),
                                (indices[:-1], indices[1:], visible[:-1] & visible[1:])):
        keep = keep & (first != second)
        counts += np.bincount(first[keep].astype(int) * 32 + second[keep], minlength=32 * 32)
    return counts.reshape(32, 32)


def sprite_statistics(metadata, sprites, select):
    weights, edges = np.zeros(32), np.zeros((32, 32))
    for frame in sprite_frames(metadata, sprites, select):
        mask = np.unpackbits(frame[5], axis=-1).astype(bool)
        frame_indices = _indices(frame[:5])
        weights += np.bincount(frame_indices[mask], minlength=32)
        edges += _borders(frame_indices, mask)
    return weights, edges


def colour_statistics(base, height, metadata, sprites, select=None):
    """Pixel counts per colour index and colour border counts over the playfield and the sprites
    `select` accepts (all by default)."""
    planes = np.frombuffer(base, dtype=np.uint8).reshape(6, height, -1)
    indices = _indices(planes[:5])
    weights = np.bincount(indices.ravel(), minlength=32).astype(float)
    edges = _borders(indices, np.ones(indices.shape, dtype=bool))
    for frame in sprite_frames(metadata, sprites, select):
        mask = np.unpackbits(frame[5], axis=-1).astype(bool)
        frame_indices = _indices(frame[:5])
        weights += SPRITE_WEIGHT * np.bincount(frame_indices[mask], minlength=32)
        edges += SPRITE_WEIGHT * _borders(frame_indices, mask)
    return weights, edges


def native_sprites(metadata, sprites, mapping=None, sprite_mapping=None, hardware=frozenset(), dithered=frozenset()):
    """Emit interleaved pixels and one mask plane without the blitter's zero shift word.

    The renderer restores the shift word and repeats the mask per plane when it caches a frame.
    """
    header = struct.unpack_from('>4s6H8I', metadata)
    count, table = header[4], header[13]
    spans = [struct.unpack_from('>2I', metadata, table + index * 28 + 12)
             for index in range(count)]
    native = bytearray()
    native_spans = []
    flags = []
    encoded = {}
    for index, (offset, size) in enumerate(spans):
        width, height = struct.unpack_from('>2H', metadata, table + index * 28 + 8)
        row_bytes = ((width + 15) // 16 + 1) * 2
        block = sprites[offset:offset + size]
        sprite = sprite_mapping is not None and _hardware(metadata, index, hardware)
        light = struct.unpack_from('>H', metadata, table + index * 28)[0] in dithered
        key = (height, row_bytes, block, sprite, light)
        if key not in encoded:
            pixels = np.frombuffer(decode_lz4(block, row_bytes * height * 6), dtype=np.uint8).reshape(6, height, row_bytes)
            if light:
                # source index 31 is the playfield white in either palette (it maps to the top index).
                pixels = dither_light(pixels, struct.unpack_from('>32H', metadata, 48))
            pixels = remap_planes(pixels, sprite_mapping if sprite else mapping)
            if sprite:
                # sprite colour 0 is the hardware's transparency: clear everything outside the mask.
                pixels = np.array(pixels)
                pixels[:5] &= pixels[5]
            colors = pixels[:PLANES, :, :-2].transpose(1, 0, 2).tobytes()
            masks = pixels[5, :, :-2].tobytes()
            encoded[key] = encode_block(colors + masks)
        block = encoded[key]
        native_spans.append((len(native), len(block)))
        native.extend(block)
        flags.append(HARDWARE_SPRITE_FLAG if sprite else 0)
    payload, relocated = pack_resources(native, native_spans)
    result = bytearray(metadata)
    result[:4] = b'AN03'
    for index, (offset, size) in enumerate(relocated):
        # the third word marks frames drawn by sprite DMA, in sprite colours.
        struct.pack_into('>4I', result, table + index * 28 + 12,
                         offset, size, flags[index], 0)
    struct.pack_into('>I', result, 44, len(payload))
    return bytes(result), payload


def _aligned(value):
    return (value + 3) & ~3


def prepare_native_assets(generated, report, normal_capacity=88 * 1024):
    from amiga_opening_assets import RESOURCE_NORMAL_BYTES as normal_bank
    generated = Path(generated)
    identity = hashlib.sha256(b''.join(Path(__file__).with_name(name).read_bytes()
                                     for name in ('amiga_native_assets.py', 'asset_decode.py',
                                                  'amiga_room_tiles.py', 'amiga_tiles.py',
                                                  'lz4_block.py', 'asset_resources.py', 'amiga_clip_overrides.py'))
                              + (PALETTE_PINS.read_bytes() if PALETTE_PINS.exists() else b'')).hexdigest()
    hardware = hardware_resources(generated)
    dithered = family_resources(generated, DITHERED_FAMILIES)
    manifest_path = generated / 'native-layers.json'
    previous = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    records = []
    sizes = []
    streamed_sizes = []
    for room in report['rooms']:
        room_sizes = []
        streamed_room_sizes = []
        for variant in (room, room['sword']):
            folder = generated / f"area{room['room']}"
            basename = variant['basename']
            key = f"area{room['room']}/{basename}"
            old = previous.get('records', {}).get(key, {})
            paths = [folder / (basename + extension)
                     for extension in ('.acb', '.aco', '.agn', '.atg', '.ans', '.asb', '.ant')]
            valid = (previous.get('converter') == identity and old.get('source') == variant['sha256']
                     and all(path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == digest
                             for path, digest in zip(paths, old.get('hashes', []))) and len(old.get('hashes', [])) == len(paths))
            source_world = (folder / (basename + '.agp')).read_bytes()
            # amiga treatments re-time some shared clips; the source frames stay as generated.
            plans = plan_clips(source_world, room['room'])
            world = apply_plans(source_world, plans)
            metadata, boundaries = compact_world(world)
            if not valid:
                base, overlays = native_layers(source_world, plans=plans)
                width, height = struct.unpack_from('>2H', world, 4)
                raw_sprites = (folder / (basename + '.asp')).read_bytes()
                mapping = sprite_mapping = None
                if PLANES < 5 and base:
                    # the playfield and blitter objects share 16 colours; hardware sprite frames get
                    # colour registers 17-31 of their own.
                    palette = struct.unpack_from('>32H', metadata, 48)
                    select = lambda index: _hardware(metadata, index, hardware)
                    weights, edges = colour_statistics(base, height, metadata, raw_sprites, lambda index: not select(index))
                    mapping, reduced = reduce_palette(palette, weights, 1 << PLANES, edges, palette_pins(room['room']))
                    sprite_weights, sprite_edges = sprite_statistics(metadata, raw_sprites, select)
                    sprite_mapping, sprite_colours = sprite_palette(palette, sprite_weights, sprite_edges)
                    entries = reduced + [0] + sprite_colours
                    metadata = metadata[:48] + struct.pack('>32H', *entries) + metadata[48 + 64:]
                    base, overlays = native_layers(source_world, mapping, plans)
                sprite_metadata, sprites = native_sprites(metadata, raw_sprites, mapping, sprite_mapping, hardware, dithered)
                atlas, animations = opening_tiles(base, width, height, world, overlays) if base else (b'', b'')
                # raw tile banks trade memory for speed: a room whose tables would not fit the
                # guaranteed non-chip bank gives up exactly its overflow of raw storage.
                overflow = _aligned(_aligned(_aligned(len(sprite_metadata)) + len(atlas)) + len(animations)) - normal_bank
                if base and overflow > 0:
                    # give up exactly the overflow of the raw growth this room actually used (often
                    # less than the cap), plus slack for the pad bytes that keep raw banks aligned.
                    smallest = opening_tiles(base, width, height, world, overlays, 0)[0]
                    used = max(0, len(atlas) - len(smallest))
                    atlas, animations = opening_tiles(base, width, height, world, overlays,
                                                      max(0, used - overflow - 1024))
                payloads = ((base, overlays, metadata, atlas, sprite_metadata, sprites, animations)
                            if base else (b'',) * len(paths))
                for path, payload in zip(paths, payloads):
                    path.write_bytes(payload)
            hashes = [hashlib.sha256(path.read_bytes()).hexdigest() for path in paths]
            records.append((key, dict(source=variant['sha256'], hashes=hashes,
                                      native_static_sprite_bytes=sum(path.stat().st_size for path in paths[3:]))))
            streamed_room_sizes.append([path.stat().st_size for path in paths[3:]])
            required = max(0, variant['sprite_bytes'] - normal_capacity)
            prefix = next((offset for offset in boundaries if offset >= required), 0) if required else 0
            room_sizes.append([paths[0].stat().st_size, paths[1].stat().st_size if variant['clips'] else 0,
                               paths[2].stat().st_size, prefix])
        sizes.append(room_sizes)
        streamed_sizes.append(streamed_room_sizes)
    manifest_path.write_text(json.dumps(dict(converter=identity, records=dict(records)), indent=2) + '\n')
    (generated / 'amiga_native_assets.h').write_text(
        '#include "amiga_opening_assets.h"\n'
        f'#define AMIGA_NATIVE_PLANES {PLANES}\n'
        'extern const DeceptusU32 amiga_native_sizes[AMIGA_OPENING_ROOMS][2][4];\n'
        'extern const DeceptusU32 amiga_streamed_sizes[AMIGA_OPENING_ROOMS][2][4];\n'
        'extern const DeceptusU16 amiga_native_font[95][18];\n')
    rows = ['   {{' + ','.join(map(str, normal)) + '},{' + ','.join(map(str, sword)) + '}},' for normal, sword in sizes]
    streamed_rows = ['   {{' + ','.join(map(str, normal)) + '},{' + ','.join(map(str, sword)) + '}},'
                     for normal, sword in streamed_sizes]
    font = re.search(r'const DeceptusU16 amiga_font\[95\]\[18\] = \{.*?\n\};',
                     (generated / 'opening_content.c').read_text(), re.S)
    if not font:
        raise ValueError('native font data is missing from the target archive')
    font = font.group().replace('amiga_font[95][18] =', 'amiga_native_font[95][18] __attribute__((section(".MEMF_CHIP"))) =')
    (generated / 'amiga_native_assets.c').write_text(
        '#include "amiga_native_assets.h"\nconst DeceptusU32 amiga_native_sizes[AMIGA_OPENING_ROOMS][2][4] '
        '__attribute__((section(".MEMF_CHIP"))) = {\n'
        + '\n'.join(rows) + '\n};\n'
        + 'const DeceptusU32 amiga_streamed_sizes[AMIGA_OPENING_ROOMS][2][4] = {\n'
        + '\n'.join(streamed_rows) + '\n};\n' + font + '\n')
