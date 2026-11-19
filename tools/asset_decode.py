"""Bounded decoders for inspecting and repacking target asset archives."""


def unpackbits(payload, expected_size, unit_bytes=1):
    if unit_bytes not in (1, 2, 4) or expected_size % unit_bytes:
        raise ValueError('RLE output requires complete resource units')
    output = bytearray()
    index = 0
    while index < len(payload):
        control = payload[index]
        index += 1
        count = (control & 127) + 1
        size = unit_bytes if control & 128 else count * unit_bytes
        if size > len(payload) - index:
            raise ValueError('truncated RLE packet')
        packet = payload[index:index + size]
        index += size
        output.extend(packet * count if control & 128 else packet)
        if len(output) > expected_size:
            raise ValueError('RLE output exceeds its resource')
    if len(output) != expected_size:
        raise ValueError('incomplete RLE output')
    return bytes(output)


def decode_lz4(payload, expected_size):
    output = bytearray()
    index = 0

    def length(value):
        nonlocal index
        if value == 15:
            while True:
                if index >= len(payload):
                    raise ValueError('truncated LZ4 length')
                extension = payload[index]
                index += 1
                value += extension
                if value > expected_size:
                    raise ValueError('LZ4 length exceeds its resource')
                if extension != 255:
                    break
        return value

    while index < len(payload):
        token = payload[index]
        index += 1
        count = length(token >> 4)
        if count > len(payload) - index or count > expected_size - len(output):
            raise ValueError('invalid LZ4 literals')
        output.extend(payload[index:index + count])
        index += count
        if index == len(payload):
            if len(output) != expected_size:
                raise ValueError('incomplete LZ4 output')
            return bytes(output)
        if len(payload) - index < 2:
            raise ValueError('truncated LZ4 offset')
        offset = payload[index] | payload[index + 1] << 8
        index += 2
        count = length(token & 15) + 4
        if not offset or offset > len(output) or count > expected_size - len(output):
            raise ValueError('invalid LZ4 match')
        for _ in range(count):
            output.append(output[-offset])
    raise ValueError('missing LZ4 final literals')
