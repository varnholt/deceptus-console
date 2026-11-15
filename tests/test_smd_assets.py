import sys
from pathlib import Path
import unittest

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from smd_assets import packed_tile, palette_word, physical_tile, rgb333


class MegaDriveAssetsTests(unittest.TestCase):
    def test_chunky_tile_keeps_left_pixel_in_high_nibble(self):
        pixels = np.tile(np.arange(8, dtype=np.uint8), (8, 1))
        self.assertEqual(packed_tile(Image.fromarray(pixels)), bytes.fromhex('01234567') * 8)

    def test_palette_encoding_matches_cram_component_bits(self):
        self.assertEqual(palette_word(255, 0, 0), 0x000e)
        self.assertEqual(palette_word(0, 255, 0), 0x00e0)
        self.assertEqual(palette_word(0, 0, 255), 0x0e00)
        self.assertEqual(palette_word(255, 255, 255), 0x0eee)

    def test_graphics_skip_the_background_map_and_keep_piece_contiguity(self):
        self.assertEqual(physical_tile(1535), 1535)
        self.assertEqual(physical_tile(1536), 1664)
        for first in range(0, 1664, 4):
            physical = [physical_tile(first + part) for part in range(4)]
            self.assertEqual(physical, list(range(physical[0], physical[0] + 4)))
            self.assertFalse(any(1536 <= tile < 1664 for tile in physical))
            self.assertLess(physical[-1], 1792)

    def test_colour_conversion_preserves_size_grid_and_alpha(self):
        source = np.array([[[17, 112, 230, 0], [255, 4, 45, 255]]], dtype=np.uint8)
        converted = np.asarray(rgb333(Image.fromarray(source)))
        self.assertEqual(converted.shape, source.shape)
        self.assertTrue(np.array_equal(converted[:, :, 3], source[:, :, 3]))
        self.assertEqual(converted[0, 1].tolist(), [255, 0, 36, 255])


if __name__ == '__main__':
    unittest.main()
