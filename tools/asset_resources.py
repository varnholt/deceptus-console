"""Pack referenced binary resource spans without retaining unused copies."""


def pack_resources(payload, spans):
    payload = bytes(payload)
    packed = bytearray()
    locations = {}
    relocated = []
    for offset, size in spans:
        if offset < 0 or size <= 0 or offset > len(payload) - size:
            raise ValueError('resource span exceeds its source payload')
        block = payload[offset:offset + size]
        if block not in locations:
            locations[block] = len(packed)
            packed.extend(block)
        relocated.append((locations[block], size))
    return bytes(packed), relocated
