import sys
from pathlib import Path
import unittest
from unittest.mock import Mock

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from player_display import integer_zoom, present


class Photo:
    def __init__(self, image):
        self.size = image.size
        self.pasted = []

    def width(self):
        return self.size[0]

    def height(self):
        return self.size[1]

    def paste(self, image):
        self.pasted.append(image.size)


class PlayerDisplayTests(unittest.TestCase):
    def test_mega_drive_mode_change_replaces_the_startup_sized_photo(self):
        canvas = Mock(spec=['configure'])
        factory = Mock(side_effect=Photo)
        self.assertTrue(present(canvas, Image.new('RGB', (256, 192)), (1920, 1080), factory))
        startup = canvas.image
        self.assertTrue(present(canvas, Image.new('RGB', (320, 224)), (1920, 1080), factory))
        self.assertIsNot(canvas.image, startup)
        self.assertEqual(canvas.image.size, (960, 672))
        self.assertEqual(startup.pasted, [])
        self.assertFalse(present(canvas, Image.new('RGB', (320, 224)), (1920, 1080), factory))
        self.assertEqual(canvas.image.pasted, [(960, 672)])

    def test_full_frame_fits_small_desktop_with_integer_zoom(self):
        self.assertEqual(integer_zoom((320, 224), (1092, 614)), 2)
        self.assertEqual(integer_zoom((320, 224), (800, 600)), 2)
        self.assertEqual(integer_zoom((320, 224), (640, 400)), 1)

    def test_pixel_grid_uses_only_integer_magnification(self):
        self.assertEqual(integer_zoom((320, 224), (1920, 1080)), 3)
        self.assertEqual(integer_zoom((240, 160), (1092, 614)), 3)


if __name__ == '__main__':
    unittest.main()
