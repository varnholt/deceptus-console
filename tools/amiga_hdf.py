"""Write a bootable Amiga hard disk partition image (.hdf) with the original Amiga filesystem.

OFS ("DOS\\0") is the filesystem Kickstart 1.3 has in ROM, so the image mounts and boots on
any Kickstart from 1.3 on without loading a filesystem from disk. The image is a partition
hardfile (no Rigid Disk Block): 512-byte blocks, two reserved boot blocks, one root block in
the middle, bitmap blocks after it and the files from there on.
"""

import struct

BLOCK = 512
LONGS = BLOCK // 4
HASH_SIZE = LONGS - 56
DATA_BYTES = BLOCK - 24
T_HEADER, T_DATA, T_LIST = 2, 8, 16
ST_ROOT, ST_USERDIR, ST_FILE = 1, 2, -3
# 2026-01-01 in AmigaDOS days since 1978-01-01.
DAYS = 17532


def _hash(name):
    value = len(name)
    for character in name.upper().encode('latin-1'):
        value = (value * 13 + character) & 0x7FF
    return value % HASH_SIZE


def _checksum(block, offset):
    struct.pack_into('>I', block, offset, 0)
    total = sum(struct.unpack('>%dI' % LONGS, block)) & 0xFFFFFFFF
    struct.pack_into('>I', block, offset, (-total) & 0xFFFFFFFF)


def _name(block, name):
    encoded = name.encode('latin-1')
    if len(encoded) > 30:
        raise ValueError(f'Amiga file names hold 30 characters: {name}')
    block[432] = len(encoded)
    block[433:433 + len(encoded)] = encoded


def _dates(block, offset):
    struct.pack_into('>3I', block, offset, DAYS, 0, 0)


class _Node:
    def __init__(self, name, data=None):
        self.name, self.data, self.children, self.block = name, data, {}, 0


def write_hdf(path, files, volume, sectors=32, heads=1):
    """files maps 'dir/sub/name' (forward slashes) to bytes. Returns the geometry (cylinders,
    heads, sectors) a mounting configuration needs."""
    root = _Node(volume)
    for name, data in files.items():
        node = root
        parts = name.split('/')
        for part in parts[:-1]:
            node = node.children.setdefault(part.lower(), _Node(part))
        node.children[parts[-1].lower()] = _Node(parts[-1], data)

    def blocks_for(node):
        if node.data is None:
            return 1 + sum(blocks_for(child) for child in node.children.values())
        data_blocks = max(0, -(-len(node.data) // DATA_BYTES))
        return 1 + data_blocks + max(0, -(-data_blocks // HASH_SIZE) - 1)

    needed = blocks_for(root) + 2
    # free space for saves and a whole number of cylinders.
    total = needed + needed // 16 + 512
    per_cylinder = sectors * heads
    total = -(-total // per_cylinder) * per_cylinder
    bitmap_blocks = -(-(total - 2) // ((LONGS - 1) * 32))
    if bitmap_blocks > 25:
        raise ValueError('image too large for root bitmap pointers')
    image = bytearray(total * BLOCK)
    root_block = (total - 1 + 2) // 2
    used = set(range(2)) | {root_block} | set(range(root_block + 1, root_block + 1 + bitmap_blocks))
    cursor = [2]

    def allocate():
        while cursor[0] in used:
            cursor[0] += 1
        if cursor[0] >= total:
            raise ValueError('image full')
        used.add(cursor[0])
        return cursor[0]

    def block(number):
        return memoryview(image)[number * BLOCK:(number + 1) * BLOCK]

    def place(node, parent):
        node.block = root_block if parent is None else allocate()
        if node.data is not None:
            data_numbers = [allocate() for _ in range(-(-len(node.data) // DATA_BYTES))]
            node.data_numbers = data_numbers
            node.extensions = [allocate() for _ in range(max(0, -(-len(data_numbers) // HASH_SIZE) - 1))]
        for child in node.children.values():
            place(child, node)

    place(root, None)

    def write_dir(node, parent):
        header = bytearray(BLOCK)
        table = [0] * HASH_SIZE
        for child in sorted(node.children.values(), key=lambda item: item.name.lower()):
            index = _hash(child.name)
            child.chain = table[index]
            table[index] = child.block
        struct.pack_into('>I', header, 0, T_HEADER)
        if parent is None:
            struct.pack_into('>I', header, 12, HASH_SIZE)
            struct.pack_into('>%dI' % HASH_SIZE, header, 24, *table)
            struct.pack_into('>i', header, 312, -1)
            struct.pack_into('>%dI' % bitmap_blocks, header, 316,
                             *range(root_block + 1, root_block + 1 + bitmap_blocks))
            _dates(header, 420)
            _name(header, node.name)
            _dates(header, 472)
            _dates(header, 484)
            struct.pack_into('>i', header, 508, ST_ROOT)
        else:
            struct.pack_into('>I', header, 4, node.block)
            struct.pack_into('>%dI' % HASH_SIZE, header, 24, *table)
            _dates(header, 420)
            _name(header, node.name)
            struct.pack_into('>I', header, 496, node.chain)
            struct.pack_into('>I', header, 500, parent.block)
            struct.pack_into('>i', header, 508, ST_USERDIR)
        _checksum(header, 20)
        block(node.block)[:] = header
        for child in node.children.values():
            if child.data is None:
                write_dir(child, node)
            else:
                write_file(child, node)

    def write_file(node, parent):
        numbers = node.data_numbers
        groups = [numbers[index:index + HASH_SIZE] for index in range(0, len(numbers), HASH_SIZE)] or [[]]
        owners = [node.block] + node.extensions
        for group_index, (owner, group) in enumerate(zip(owners, groups)):
            header = bytearray(BLOCK)
            struct.pack_into('>I', header, 0, T_HEADER if group_index == 0 else T_LIST)
            struct.pack_into('>I', header, 4, owner)
            struct.pack_into('>I', header, 8, len(group))
            if group:
                struct.pack_into('>I', header, 16, group[0])
            # data block pointers run backwards from the end of the table.
            for position, number in enumerate(group):
                struct.pack_into('>I', header, 24 + (HASH_SIZE - 1 - position) * 4, number)
            if group_index == 0:
                struct.pack_into('>I', header, 324, len(node.data))
                _dates(header, 420)
                _name(header, node.name)
                struct.pack_into('>I', header, 496, node.chain)
            struct.pack_into('>I', header, 500, parent.block if group_index == 0 else node.block)
            struct.pack_into('>I', header, 504, owners[group_index + 1] if group_index + 1 < len(owners) else 0)
            struct.pack_into('>i', header, 508, ST_FILE)
            _checksum(header, 20)
            block(owner)[:] = header
        for sequence, number in enumerate(numbers):
            chunk = node.data[sequence * DATA_BYTES:(sequence + 1) * DATA_BYTES]
            data = bytearray(BLOCK)
            struct.pack_into('>5I', data, 0, T_DATA, node.block, sequence + 1, len(chunk),
                             numbers[sequence + 1] if sequence + 1 < len(numbers) else 0)
            data[24:24 + len(chunk)] = chunk
            _checksum(data, 20)
            block(number)[:] = data

    write_dir(root, None)
    # bitmap: a set bit is a free block; bit n covers block n + 2.
    for page in range(bitmap_blocks):
        bitmap = bytearray(BLOCK)
        for index in range(LONGS - 1):
            value = 0
            for bit in range(32):
                number = 2 + (page * (LONGS - 1) + index) * 32 + bit
                if number < total and number not in used:
                    value |= 1 << bit
            struct.pack_into('>I', bitmap, 4 + index * 4, value)
        _checksum(bitmap, 0)
        block(root_block + 1 + page)[:] = bitmap
    image[0:4] = b'DOS\x00'
    struct.pack_into('>I', image, 8, root_block)
    with open(path, 'wb') as output:
        output.write(image)
    return total // per_cylinder, heads, sectors
