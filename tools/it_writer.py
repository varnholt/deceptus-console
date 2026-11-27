"""Impulse Tracker (IT) modules from the songs tools/song_assets.py reads, for the SNES's snesmod
(smconv converts IT only). Instruments keep their key maps, volume envelopes and fadeout; XM effects
map to their IT counterparts, and those IT lacks (panning, the key-off tick) are dropped.
"""
import math
import struct

IT_NOTE_OFF = 255
XM_NOTE_OFF = 97
# XM's C-0 is IT's C-1: an XM note n (1 = C-0) is IT note n + 11.
NOTE_OFFSET = 11
IT_NOTES = 120
SAMPLE_PRESENT, SAMPLE_LOOP = 1, 16
SIGNED_SAMPLES = 1
USE_INSTRUMENTS, LINEAR_SLIDES = 4, 8
ENVELOPE_ON, ENVELOPE_LOOP, ENVELOPE_SUSTAIN = 1, 2, 4
ENVELOPE_NODES = 25
# XM fades from 32768 by its fadeout per tick, IT from 1024.
FADEOUT_SCALE = 32


def _letter(command):
    return ord(command) - ord('A') + 1


def convert_effect(effect, parameter):
    """An XM effect as an IT (command, parameter), or None; a set-volume becomes ('vol', volume)."""
    high, low = parameter >> 4, parameter & 15
    simple = {0x1: 'F', 0x2: 'E', 0x3: 'G', 0x4: 'H', 0x5: 'L', 0x6: 'K', 0x7: 'R', 0x9: 'O', 0xA: 'D',
              0xB: 'B', 0x11: 'W', 0x1B: 'Q'}
    if effect == 0 and parameter:
        return _letter('J'), parameter
    if effect in simple:
        return _letter(simple[effect]), parameter
    if effect == 0xC:
        return 'vol', min(parameter, 64)
    if effect == 0xD:
        return _letter('C'), min(high * 10 + low, 255)
    if effect == 0xE:
        table = {0x1: ('F', 0xF0 | low), 0x2: ('E', 0xF0 | low), 0x9: ('Q', low), 0xA: ('D', (low << 4) | 0xF),
                 0xB: ('D', 0xF0 | low), 0xC: ('S', 0xC0 | low), 0xD: ('S', 0xD0 | low), 0xE: ('S', 0xE0 | low)}
        if high in table:
            return _letter(table[high][0]), table[high][1]
        return None
    if effect == 0xF:
        if parameter == 0:
            return None
        return (_letter('A'), parameter) if parameter < 32 else (_letter('T'), parameter)
    if effect == 0x10:
        return _letter('V'), min(parameter * 2, 128)
    if effect == 0x21:
        if high == 1:
            return _letter('F'), 0xE0 | low
        if high == 2:
            return _letter('E'), 0xE0 | low
    return None


def convert_volume(volume):
    """An XM volume-column byte as an IT volume-column byte, or None."""
    kind, value = volume >> 4, volume & 15
    if 0x1 <= kind <= 0x5:
        return min(volume - 0x10, 64)
    return {0x6: 95 + min(value, 9), 0x7: 85 + min(value, 9), 0x8: 75 + min(value, 9),
            0x9: 65 + min(value, 9)}.get(kind)


def _unpack_cells(cells, rows, channels):
    """XM packed cells -> rows of channel tuples (note, instrument, volume, effect, parameter)."""
    result, index = [], 0
    for _ in range(rows):
        row = []
        for _ in range(channels):
            flags = cells[index]
            if flags & 0x80:
                index += 1
                fields = []
                for bit in range(5):
                    if flags & (1 << bit):
                        fields.append(cells[index])
                        index += 1
                    else:
                        fields.append(0)
            else:
                fields = list(cells[index:index + 5])
                index += 5
            row.append(tuple(fields))
        result.append(row)
    return result


def _pattern(cells, rows, channels):
    packed = bytearray()
    for row in _unpack_cells(cells, rows, channels):
        for channel, (note, instrument, volume, effect, parameter) in enumerate(row):
            fields, mask = bytearray(), 0
            if note:
                mask |= 1
                fields.append(IT_NOTE_OFF if note == XM_NOTE_OFF else min(note + NOTE_OFFSET, IT_NOTES - 1))
            if instrument:
                mask |= 2
                fields.append(instrument)
            converted = convert_effect(effect, parameter)
            volume_value = convert_volume(volume)
            if converted and converted[0] == 'vol':
                volume_value, converted = converted[1], None
            if volume_value is not None:
                mask |= 4
                fields.append(volume_value)
            if converted:
                mask |= 8
                fields += bytes(converted)
            if mask:
                packed += bytes([(channel + 1) | 0x80, mask]) + fields
        packed.append(0)
    return struct.pack('<HH4x', len(packed), rows) + bytes(packed)


def _envelope(envelope):
    data = bytearray(1 + 1 + 4 + ENVELOPE_NODES * 3 + 1)
    if envelope:
        count = len(envelope['ticks'])
        flags = ENVELOPE_ON | (ENVELOPE_LOOP if envelope['flags'] & 2 else 0) | (ENVELOPE_SUSTAIN if envelope['flags'] & 1 else 0)
        data[0:6] = bytes([flags, count, envelope['loop_start'], envelope['loop_end'], envelope['sustain'], envelope['sustain']])
        for index in range(count):
            struct.pack_into('<BH', data, 6 + index * 3, envelope['values'][index], envelope['ticks'][index])
    return bytes(data)


def _instrument(instrument, name):
    header = bytearray(b'IMPI' + name.encode('ascii', 'replace')[:12].ljust(12, b'\0'))
    header += bytes([0, 0, 0, 0]) + struct.pack('<H', min(instrument['fadeout'] // FADEOUT_SCALE, 1024))
    header += bytes([0, 60, 128, 32, 0, 0]) + struct.pack('<H', 0) + bytes([0, 0])
    header += name.encode('ascii', 'replace')[:26].ljust(26, b'\0') + bytes([0, 0, 0, 0]) + struct.pack('<H', 0)
    keyboard = bytearray()
    for note in range(IT_NOTES):
        key = note - NOTE_OFFSET - 1
        sample = instrument['keys'][key] if 0 <= key < len(instrument['keys']) else 255
        keyboard += bytes([note, 0 if sample == 255 else sample + 1])
    body = bytes(header) + bytes(keyboard) + _envelope(instrument['envelope']) + _envelope(None) + _envelope(None)
    return body + bytes(554 - len(body))


def c5_speed(sample):
    return int(round(8363 * math.pow(2, (sample['relative'] * 128 + sample['finetune']) / 1536)))


def _sample_header(sample, name, pointer):
    loop = sample['loop_length'] > 0
    flags = SAMPLE_PRESENT | (SAMPLE_LOOP if loop else 0)
    length = len(sample['values'])
    header = b'IMPS' + name.encode('ascii', 'replace')[:12].ljust(12, b'\0') + bytes([0, 64, flags, sample['volume']])
    header += name.encode('ascii', 'replace')[:26].ljust(26, b'\0') + bytes([SIGNED_SAMPLES, 32])
    header += struct.pack('<7I', length, sample['loop_start'] if loop else 0, sample['loop_start'] + sample['loop_length'] if loop else 0,
                          sample.get('c5_speed') or c5_speed(sample), 0, 0, pointer)
    return header + bytes(4)


def write_it(song, title):
    """The song as IT bytes: instrument mode, linear slides, mono."""
    orders = list(song['orders']) + [255]
    instruments, samples, patterns = song['instruments'], song['samples'], song['patterns']
    header = bytearray(b'IMPM' + title.encode('ascii', 'replace')[:26].ljust(26, b'\0') + bytes([4, 16]))
    header += struct.pack('<6H', len(orders), len(instruments), len(samples), len(patterns), 0x0214, 0x0214)
    header += struct.pack('<HH', USE_INSTRUMENTS | LINEAR_SLIDES, 0)
    header += bytes([128, 48, song['speed'], song['bpm'], 128, 0]) + struct.pack('<HII', 0, 0, 0)
    header += bytes([32 if channel < song['channels'] else 160 for channel in range(64)]) + bytes([64] * 64)
    header += bytes(orders)
    pointer_table = len(header)
    header += bytes(4 * (len(instruments) + len(samples) + len(patterns)))
    body, offsets = bytearray(), []
    base = len(header)
    for index, instrument in enumerate(instruments):
        offsets.append(base + len(body))
        body += _instrument(instrument, f'i{index}')
    sample_headers = []
    for index in range(len(samples)):
        sample_headers.append(base + len(body))
        offsets.append(base + len(body))
        body += bytes(80)
    for index, cells in enumerate(patterns):
        offsets.append(base + len(body))
        body += _pattern(cells, song['rows'][index], song['channels'])
    for index, sample in enumerate(samples):
        pointer = base + len(body)
        header_offset = sample_headers[index] - base
        body[header_offset:header_offset + 80] = _sample_header(sample, f's{index}', pointer)
        body += bytes(int(value) & 255 for value in sample['values'])
    struct.pack_into(f'<{len(offsets)}I', header, pointer_table, *offsets)
    return bytes(header) + bytes(body)


def effects_song(effects):
    """A song of one empty pattern whose samples are the effects ((name, signed values, rate) each)."""
    samples = [dict(values=list(values), loop_start=0, loop_length=0, volume=64, relative=0, finetune=0,
                    c5_speed=int(rate)) for _, values, rate in effects]
    instruments = [dict(keys=[index] * 96, envelope=None, fadeout=0) for index in range(len(samples))]
    return dict(channels=1, orders=[0], restart=0, speed=6, bpm=125, patterns=[bytes([0x80] * 64)], rows=[64],
                instruments=instruments, samples=samples)
