"""Encode independent LZ4 blocks without a runtime package dependency.

Format: https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md
"""


def _length(output, value):
    while value >= 255:
        output.append(255)
        value -= 255
    output.append(value)


def encode_block(payload):
    payload = bytes(payload)
    output = bytearray()
    previous = {}
    anchor = index = 0
    # preserve the format's last-five-literals and last-match-start restrictions.
    while index <= len(payload) - 12:
        key = payload[index:index + 4]
        candidates = previous.setdefault(key, [])
        best, length = 0, 0
        for candidate in reversed(candidates):
            if index - candidate > 65535:
                break
            count = 4
            while index + count < len(payload) - 5 and payload[candidate + count] == payload[index + count]:
                count += 1
            if count > length:
                best, length = candidate, count
        candidates.append(index)
        if len(candidates) > 16:
            del candidates[0]
        if length < 4:
            index += 1
            continue
        literal_length = index - anchor
        output.append(min(literal_length, 15) << 4 | min(length - 4, 15))
        if literal_length >= 15:
            _length(output, literal_length - 15)
        output.extend(payload[anchor:index])
        distance = index - best
        output.extend((distance & 255, distance >> 8))
        if length >= 19:
            _length(output, length - 19)
        end = index + length
        for position in range(index + 1, end):
            matches = previous.setdefault(payload[position:position + 4], [])
            matches.append(position)
            if len(matches) > 16:
                del matches[0]
        index = anchor = end
    literal_length = len(payload) - anchor
    output.append(min(literal_length, 15) << 4)
    if literal_length >= 15:
        _length(output, literal_length - 15)
    output.extend(payload[anchor:])
    return bytes(output)
