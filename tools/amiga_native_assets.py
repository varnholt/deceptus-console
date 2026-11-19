"""Incrementally derive DMA-ready layers from validated Amiga target archives."""

import hashlib
import json
from pathlib import Path
import re
import struct

import numpy as np

from asset_decode import decode_lz4, unpackbits


def native_layers(world):
    header = struct.unpack_from('>4s6H8I', world)
    width, height, patterns, _, clips = header[1:6]
    tile_table, tile_data, _, _, clip_table = header[8:13]
    tiles = []
    for group in range((patterns + 15) // 16):
        first, last = struct.unpack_from('>2I', world, tile_table + group * 4)
        tiles.append(decode_lz4(world[tile_data + first:tile_data + last], 768))
    tiles = np.frombuffer(b''.join(tiles), dtype=np.uint8).reshape(-1, 6, 8)

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
        for frame in range(count):
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


def prepare_native_assets(generated, report, normal_capacity=88 * 1024):
    generated = Path(generated)
    identity = hashlib.sha256(Path(__file__).read_bytes() + Path(__file__).with_name('asset_decode.py').read_bytes()).hexdigest()
    manifest_path = generated / 'native-layers.json'
    previous = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    records = []
    sizes = []
    for room in report['rooms']:
        room_sizes = []
        for variant in (room, room['sword']):
            folder = generated / f"area{room['room']}"
            basename = variant['basename']
            key = f"area{room['room']}/{basename}"
            old = previous.get('records', {}).get(key, {})
            paths = [folder / (basename + extension) for extension in ('.acb', '.aco', '.agn')]
            valid = (previous.get('converter') == identity and old.get('source') == variant['sha256']
                     and all(path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == digest
                             for path, digest in zip(paths, old.get('hashes', []))) and len(old.get('hashes', [])) == 3)
            world = (folder / (basename + '.agp')).read_bytes()
            metadata, boundaries = compact_world(world)
            if not valid:
                base, overlays = native_layers(world)
                payloads = (base, overlays, metadata) if base else (b'', b'', b'')
                for path, payload in zip(paths, payloads):
                    path.write_bytes(payload)
            hashes = [hashlib.sha256(path.read_bytes()).hexdigest() for path in paths]
            records.append((key, dict(source=variant['sha256'], hashes=hashes)))
            required = max(0, variant['sprite_bytes'] - normal_capacity)
            prefix = next((offset for offset in boundaries if offset >= required), 0) if required else 0
            room_sizes.append([paths[0].stat().st_size, paths[1].stat().st_size if variant['clips'] else 0,
                               paths[2].stat().st_size, prefix])
        sizes.append(room_sizes)
    manifest_path.write_text(json.dumps(dict(converter=identity, records=dict(records)), indent=2) + '\n')
    (generated / 'amiga_native_assets.h').write_text(
        '#include "amiga_opening_assets.h"\nextern const DeceptusU32 amiga_native_sizes[AMIGA_OPENING_ROOMS][2][4];\n'
        'extern const DeceptusU16 amiga_native_font[95][18];\n')
    rows = ['   {{' + ','.join(map(str, normal)) + '},{' + ','.join(map(str, sword)) + '}},' for normal, sword in sizes]
    font = re.search(r'const DeceptusU16 amiga_font\[95\]\[18\] = \{.*?\n\};',
                     (generated / 'opening_content.c').read_text(), re.S)
    if not font:
        raise ValueError('native font data is missing from the target archive')
    font = font.group().replace('amiga_font[95][18] =', 'amiga_native_font[95][18] __attribute__((section(".MEMF_CHIP"))) =')
    (generated / 'amiga_native_assets.c').write_text(
        '#include "amiga_native_assets.h"\nconst DeceptusU32 amiga_native_sizes[AMIGA_OPENING_ROOMS][2][4] '
        '__attribute__((section(".MEMF_CHIP"))) = {\n'
        + '\n'.join(rows) + '\n};\n' + font + '\n')
