import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import build_snes


class BankedBuildTests(unittest.TestCase):

    def test_exhirom_reset_mirror_and_both_checksums(self):
        source = bytearray([255]) * (8 * 1024 * 1024)
        source[:65536] = bytes(range(256)) * 256
        source[0x40FFB0:0x410000] = bytes([17]) * 80
        output = build_snes.exhirom_boot_mirror(source)
        self.assertEqual(output[:0xFFB0], output[0x400000:0x40FFB0])
        self.assertEqual(output[0x40FFE0:0x410000], source[0x40FFE0:0x410000])
        for offset in (0xFFDC, 0x40FFDC):
            complement, checksum = struct.unpack_from('<HH', output, offset)
            self.assertEqual(complement ^ checksum, 65535)
            self.assertEqual(checksum, sum(output) & 65535)

if __name__ == '__main__':
    unittest.main()
