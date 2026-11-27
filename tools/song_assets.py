"""Convert FastTracker 2 XM modules into the song data include/deceptus/song.h describes.

Patterns keep XM's own cell packing. Samples become signed 8-bit; a ping-pong loop is unrolled into
a forward one. Each instrument keeps its key map and volume envelope; panning, auto-vibrato and the
panning envelope are left out (the consoles play songs in mono).
"""
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHANNELS_MAX = 8
KEYS = 96
NO_SAMPLE = 255
ENVELOPE_SUSTAIN, ENVELOPE_LOOP = 1, 2
XM_ENVELOPE_ON, XM_ENVELOPE_SUSTAIN, XM_ENVELOPE_LOOP = 1, 2, 4
LOOP_FORWARD, LOOP_PINGPONG = 1, 2
SAMPLE_16_BIT = 16


class SongError(ValueError):
    pass


def _signed8(value):
    return value - 256 if value > 127 else value


def _decode_samples(data, offset, header, sixteen):
    """XM stores sample points as deltas; returns signed 8-bit values."""
    if sixteen:
        count = header['length'] // 2
        values, total = [], 0
        for index in range(count):
            total = (total + struct.unpack_from('<h', data, offset + index * 2)[0] + 32768) % 65536 - 32768
            values.append(total >> 8)
        return values
    values, total = [], 0
    for index in range(header['length']):
        total = (total + _signed8(data[offset + index]) + 128) % 256 - 128
        values.append(total)
    return values


def parse_xm(data):
    if not data.startswith(b'Extended Module: '):
        raise SongError('not an XM module')
    header_size = struct.unpack_from('<I', data, 60)[0]
    length, restart, channels, pattern_count, instrument_count, flags, speed, bpm = struct.unpack_from('<8H', data, 64)
    if not flags & 1:
        raise SongError('only linear-frequency XM modules are supported')
    if channels > CHANNELS_MAX:
        raise SongError(f'{channels} channels exceed the player limit of {CHANNELS_MAX}')
    orders = list(data[80:80 + length])
    offset = 60 + header_size
    patterns, rows = [], []
    for _ in range(pattern_count):
        pattern_header, _, row_count, packed = struct.unpack_from('<IBHH', data, offset)
        offset += pattern_header
        cells = bytes(data[offset:offset + packed])
        offset += packed
        if not cells:
            cells = bytes([0x80]) * (row_count * channels)
        patterns.append(cells)
        rows.append(row_count)
    instruments, samples = [], []
    for _ in range(instrument_count):
        instrument_size = struct.unpack_from('<I', data, offset)[0]
        sample_count = struct.unpack_from('<H', data, offset + 27)[0]
        instrument = dict(keys=[NO_SAMPLE] * KEYS, envelope=None, fadeout=0)
        if not sample_count:
            offset += instrument_size
            instruments.append(instrument)
            continue
        sample_header_size = struct.unpack_from('<I', data, offset + 29)[0]
        key_map = data[offset + 33:offset + 33 + KEYS]
        points = struct.unpack_from('<24H', data, offset + 129)
        point_count, _, sustain, loop_start, loop_end = data[offset + 225], data[offset + 226], data[offset + 227], data[offset + 228], data[offset + 229]
        envelope_type = data[offset + 233]
        instrument['fadeout'] = struct.unpack_from('<H', data, offset + 239)[0]
        if (envelope_type & XM_ENVELOPE_ON) and point_count:
            point_count = min(point_count, 12)
            instrument['envelope'] = dict(
                ticks=[points[index * 2] for index in range(point_count)],
                values=[min(points[index * 2 + 1], 64) for index in range(point_count)],
                sustain=min(sustain, point_count - 1), loop_start=min(loop_start, point_count - 1),
                loop_end=min(loop_end, point_count - 1),
                flags=(ENVELOPE_SUSTAIN if envelope_type & XM_ENVELOPE_SUSTAIN else 0) |
                      (ENVELOPE_LOOP if envelope_type & XM_ENVELOPE_LOOP else 0))
        offset += instrument_size
        headers = []
        for index in range(sample_count):
            length_bytes, loop_start_bytes, loop_length_bytes, volume, finetune, sample_type, _, relative = \
                struct.unpack_from('<IIIBbBBb', data, offset)
            headers.append(dict(length=length_bytes, loop_start=loop_start_bytes, loop_length=loop_length_bytes,
                                volume=min(volume, 64), finetune=finetune, type=sample_type, relative=relative))
            offset += sample_header_size
        first = len(samples)
        for sample in headers:
            sixteen = bool(sample['type'] & SAMPLE_16_BIT)
            values = _decode_samples(data, offset, sample, sixteen)
            offset += sample['length']
            scale = 2 if sixteen else 1
            loop_start, loop_length = sample['loop_start'] // scale, sample['loop_length'] // scale
            loop = sample['type'] & 3
            if loop == LOOP_PINGPONG and loop_length > 1:
                forward = values[loop_start:loop_start + loop_length]
                values = values[:loop_start] + forward + forward[-2:0:-1]
                loop_length = len(values) - loop_start
            elif loop != LOOP_FORWARD or loop_length == 0:
                loop_length = 0
            values = values[:loop_start + loop_length] if loop_length else values
            samples.append(dict(values=values, loop_start=loop_start if loop_length else 0, loop_length=loop_length,
                                volume=sample['volume'], finetune=sample['finetune'], relative=sample['relative']))
        instrument['keys'] = [first + key if key < sample_count else NO_SAMPLE for key in key_map]
        instruments.append(instrument)
    return dict(channels=channels, orders=orders, restart=restart, speed=speed, bpm=bpm, patterns=patterns,
                rows=rows, instruments=instruments, samples=samples)


def _array(kind, name, values, per_line=16, attribute=''):
    rows = [', '.join(str(value) for value in values[index:index + per_line]) for index in range(0, len(values), per_line)]
    return [f'static const {kind} {name}[]{attribute} = {{'] + ['   ' + row + ',' for row in rows] + ['};']


def padded_values(sample, tail):
    """The sample's values followed by tail more: its loop continued, or silence after a one-shot. a
    mixer that checks the end only now and then reads them instead of whatever follows."""
    values, loop_start, loop_length = sample['values'], sample['loop_start'], sample['loop_length']
    if not loop_length:
        return values + [0] * tail
    loop = values[loop_start:loop_start + loop_length]
    return values + (loop * (tail // loop_length + 1))[:tail]


def song_source(song, name, sample_attribute='', tail=0):
    """C definitions of one song; the Song itself is named name, the rest is static. sample_attribute
    places the sample data, such as the Amiga's chip RAM section; tail values follow each sample
    (padded_values) without counting in its length."""
    lines = []
    for index, cells in enumerate(song['patterns']):
        lines += _array('DeceptusU8', f'{name}_pattern{index}', list(cells), 24)
    lines += [f'static const DeceptusU8 *const {name}_patterns[] = {{' +
              ', '.join(f'{name}_pattern{index}' for index in range(len(song['patterns']))) + '};']
    lines += _array('DeceptusU16', f'{name}_rows', song['rows'])
    lines += _array('DeceptusU8', f'{name}_orders', song['orders'])
    for index, sample in enumerate(song['samples']):
        lines += _array('DeceptusI8', f'{name}_sample{index}', padded_values(sample, tail) or [0], 32, sample_attribute)
    lines += [f'static const SongSample {name}_samples[] = {{'] + [
        f'   {{{name}_sample{index}, {max(1, len(sample["values"]))}, {sample["loop_start"]}, {sample["loop_length"]}, '
        f'{sample["volume"]}, {sample["relative"]}, {sample["finetune"]}}},' for index, sample in enumerate(song['samples'])] + ['};']
    instruments = []
    for index, instrument in enumerate(song['instruments']):
        lines += _array('DeceptusU8', f'{name}_keys{index}', instrument['keys'], 24)
        envelope = instrument['envelope']
        if envelope:
            lines += _array('DeceptusU16', f'{name}_envelope_ticks{index}', envelope['ticks'])
            lines += _array('DeceptusU8', f'{name}_envelope_values{index}', envelope['values'])
            volume = (f'{{{name}_envelope_ticks{index}, {name}_envelope_values{index}, {len(envelope["ticks"])}, '
                      f'{envelope["sustain"]}, {envelope["loop_start"]}, {envelope["loop_end"]}, {envelope["flags"]}}}')
        else:
            volume = '{0, 0, 0, 0, 0, 0, 0}'
        instruments.append(f'   {{{name}_keys{index}, {volume}, {instrument["fadeout"]}}},')
    lines += [f'static const SongInstrument {name}_instruments[] = {{'] + (instruments or ['   {0, {0, 0, 0, 0, 0, 0, 0}, 0},']) + ['};']
    lines += [f'const Song {name} = {{{name}_orders, {name}_patterns, {name}_rows, {name}_instruments, {name}_samples, '
              f'{song["channels"]}, {len(song["orders"])}, {song["restart"]}, {song["speed"]}, {song["bpm"]}, '
              f'{len(song["instruments"])}}};']
    return lines


def sample_bytes(song):
    return sum(len(sample['values']) for sample in song['samples'])
