"""The player's colours under water on the consoles.

The source draws semi-transparent water layers in front of the swimming player; the consoles cannot
blend those, so the swimming frames carry the tint themselves: three quarters of each colour and a
quarter of the water's.
"""
from PIL import Image
import numpy as np

# the water colour the tint mixes in, 8 bits per channel.
WATER = np.array([0, 112, 144])
NAME = 'water'


def tint(image):
    """The image with every pixel's colour moved a quarter of the way to the water; alpha kept."""
    pixels = np.asarray(image.convert('RGBA')).astype(np.int32)
    pixels[:, :, :3] = (pixels[:, :, :3] * 3 + WATER + 2) // 4
    return Image.fromarray(pixels.astype(np.uint8), 'RGBA')
