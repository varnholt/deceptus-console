"""Bounded literal/repeat packets for byte, word and four-byte resource units."""


def packbits(payload, unit_bytes=1):
    if unit_bytes not in (1, 2, 4) or len(payload) % unit_bytes:
        raise ValueError('RLE input requires complete 1, 2 or 4-byte units')
    values = [payload[start:start + unit_bytes]
              for start in range(0, len(payload), unit_bytes)]
    output = bytearray()
    index = 0
    threshold = 3 if unit_bytes == 1 else 2
    while index < len(values):
        run = 1
        while index + run < len(values) and values[index + run] == values[index] and run < 128:
            run += 1
        if run >= threshold:
            output.append(127 + run)
            output.extend(values[index])
            index += run
            continue
        start = index
        index += run
        while index < len(values) and index - start < 128:
            run = 1
            while index + run < len(values) and values[index + run] == values[index] and run < threshold:
                run += 1
            if run >= threshold:
                break
            index += min(run, 128 - (index - start))
        output.append(index - start - 1)
        output.extend(b''.join(values[start:index]))
    return bytes(output)
