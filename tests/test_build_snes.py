import struct
import sys
from pathlib import Path
import unittest
from unittest.mock import patch
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import build_snes


class BankedBuildTests(unittest.TestCase):
    def test_hirom_far_data_and_ram_banks_are_distinct(self):
        source = '.BASE $00\n.RAMSECTION "state" BANK $7e SLOT 2\n.ENDS\n.SECTION ".rodata" SEMIFREE ORG $8000\n'
        output = build_snes.banked_hirom_assembly(source)
        self.assertIn('.BASE $00\n.RAMSECTION', output)
        self.assertIn('.BASE $C0\n.SECTION ".rodata" SUPERFREE', output)
        self.assertNotIn('SEMIFREE', output)

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

    def test_exhirom_rejects_wrong_size(self):
        with self.assertRaises(ValueError):
            build_snes.exhirom_boot_mirror(bytes(65536))

    def test_exhirom_upper_blobs_use_real_cpu_bank_and_skip_reset_bank(self):
        assembly = '\n'.join(f'.section "chunk{i}" superfree\nchunk{i}:\n.incbin "part{i}.bin"\n.ends'
                             for i in range(97))
        with patch.object(Path, 'stat', return_value=SimpleNamespace(st_size=32768)):
            output = build_snes.exhirom_binary_assembly(assembly)
        self.assertIn('.base $c0\n.section "chunk0" bank 16 slot 0 org 0 force', output)
        self.assertIn('.base $00\n.section "chunk96" bank 65 slot 0 org 0 force', output)
        self.assertNotIn(' bank 64 ', output)

    def test_exported_const_tables_keep_exact_rom_labels_without_ram_copy(self):
        source = '.RAMSECTION "ram{WLA_FILENAME}.data" APPENDTO "globram.data"\nfirst dsb 2\nsecond dsb 4\nlast dsb 2\n.ENDS\n.SECTION "{WLA_FILENAME}.data" APPENDTO "glob.data"\n.db $1,$2\n.dw image, :image\n.db $3,$4\n.ENDS\n'
        output = build_snes.readonly_export_assembly(source, 'resources')
        self.assertNotIn('globram.data', output)
        self.assertIn('first:\n.db $1,$2\nsecond:\n.dw image, :image\nlast:\n.db $3,$4', output)
        self.assertIn('"readonly_resources" SUPERFREE', output)

    def test_exported_const_table_labels_can_split_a_byte_directive(self):
        source = '.RAMSECTION "ram{WLA_FILENAME}.data" APPENDTO "globram.data"\na dsb 2\nb dsb 2\n.ENDS\n.SECTION "{WLA_FILENAME}.data" APPENDTO "glob.data"\n.db 1,2,3,4\n.ENDS\n'
        self.assertIn('a:\n.db 1,2\nb:\n.db 3,4', build_snes.readonly_export_assembly(source, 'test'))
        with self.assertRaises(ValueError):
            build_snes.readonly_export_assembly(source.replace('b dsb 2', 'b dsb 3'), 'test')


if __name__ == '__main__':
    unittest.main()
