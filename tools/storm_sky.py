"""The room colours a thunderstorm lights.

thunderstormoverlay.cpp draws its flash between the far backdrop and everything in front of it, so the
lightning reaches only the sky and the distant forest. The consoles have one flat background, so the
converters find the palette entries that (almost) only those layers use and brighten just these.
"""
import numpy as np
from PIL import Image

LAYERS = ('gv-bg-sky', 'gv-bg-forest')
MASK = 'storm-sky.png'
# an entry shared with the graveyard in front would whiten it too; a few stray pixels are fine.
SKY_SHARE = 0.9


def sky_mask(sky, room):
    """Pixels where the composed room still shows the lit backdrop (sky, an RGBA image of LAYERS alone)."""
    sky_pixels = np.asarray(sky.convert('RGBA'))
    shown = np.all(np.asarray(room.convert('RGB')) == sky_pixels[:, :, :3], axis=2) & (sky_pixels[:, :, 3] > 0)
    return Image.fromarray((shown * 255).astype(np.uint8), mode='L')


def lit_entries(indices, mask, excluded=()):
    """Palette entries whose pixels lie at least SKY_SHARE in the mask, minus the excluded ones."""
    indices = np.asarray(indices).ravel()
    shown = (np.asarray(mask).ravel() >= 128)
    if indices.size != shown.size:
        raise ValueError('storm sky mask and room differ in size')
    total = np.bincount(indices, minlength=256)
    sky = np.bincount(indices[shown], minlength=256)
    return [int(entry) for entry in np.nonzero((sky > 0) & (sky >= total * SKY_SHARE))[0] if entry not in excluded]


def banked_entries(converted, mask):
    """lit_entries for a banked_background atlas whose first rows are the room: the rows below it (foreground,
    animation frames) count as unlit, and the transparent slot of each 16-colour bank is never lit."""
    indices = np.asarray(converted)
    shown = np.zeros(indices.shape, dtype=np.uint8)
    room = np.asarray(mask)
    shown[:room.shape[0], :room.shape[1]] = room
    return lit_entries(indices, shown, excluded=range(0, 256, 16))
