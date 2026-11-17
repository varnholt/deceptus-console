"""Fit a shared Amiga RGB444 palette without changing the pixel grid."""

import numpy as np
from PIL import Image


def _oklab(rgb):
    # Oklab matrices: https://bottosson.github.io/posts/oklab/ (public domain).
    values = np.asarray(rgb, dtype=np.float64) / 255
    linear = np.where(values <= 0.04045, values / 12.92,
                      ((values + 0.055) / 1.055) ** 2.4)
    lms = linear @ np.array([
        [0.4122214708, 0.5363325363, 0.0514459929],
        [0.2119034982, 0.6806995451, 0.1073969566],
        [0.0883024619, 0.2817188376, 0.6299787005],
    ]).T
    return np.cbrt(lms) @ np.array([
        [0.2104542553, 0.7936177850, -0.0040720468],
        [1.9779984951, -2.4285922050, 0.4505937099],
        [0.0259040371, 0.7827717662, -0.8086757660],
    ]).T


def _palette_space(rgb):
    # keep dark hues close to their source channels as well as perceived colour.
    fidelity = np.asarray(rgb, dtype=np.float64) / 255 * 0.35
    return np.concatenate((_oklab(rgb), fidelity), axis=-1)


def fit_palette(background, actors, actor_weight=0.25):
    """Keep black fixed and fit 31 shared colours on the actual hardware grid.

    Weight the complete animation sheet as a fraction of the room so small
    warm actor details survive without reserving half the palette for actors.
    """
    bg = np.array(background.convert('RGB')).reshape(-1, 3)
    actor = np.array(actors.convert('RGBA'))
    actor = actor[:, :, :3][actor[:, :, 3] >= 128]
    samples = np.concatenate((bg, actor))
    colors, inverse = np.unique(samples, axis=0, return_inverse=True)
    weights = np.bincount(inverse[:len(bg)], minlength=len(colors)).astype(float)
    actor_counts = np.bincount(inverse[len(bg):], minlength=len(colors))
    weights += actor_counts * len(bg) / max(1, len(actor)) * actor_weight
    space = _palette_space(colors)
    hardware = np.array(np.meshgrid(*([np.arange(16)] * 3), indexing='ij'))
    hardware = hardware.reshape(3, -1).T.astype(np.uint8) * 17
    hardware_space = _palette_space(hardware)
    distances = ((space[:, None] - hardware_space[None]) ** 2).sum(axis=2)
    nearest = distances.argmin(axis=1)
    minimum = distances.min(axis=1)
    selected = [0]
    for _ in range(31):
        error = distances[:, selected].min(axis=1) - minimum
        ranking = np.argsort(-(error * weights), kind='stable')
        chosen = next((int(nearest[index]) for index in ranking
                       if int(nearest[index]) not in selected), None)
        if chosen is None:
            chosen = next(index for index in range(4096) if index not in selected)
        selected.append(chosen)
    for _ in range(24):
        previous = selected.copy()
        assignment = distances[:, selected].argmin(axis=1)
        for slot in range(1, 32):
            members = assignment == slot
            if not members.any():
                continue
            mean = np.average(space[members], axis=0, weights=weights[members])
            ranking = np.argsort(((hardware_space - mean) ** 2).sum(axis=1))
            candidate = next(int(index) for index in ranking
                             if index == selected[slot] or index not in selected)
            candidate_error = distances[members, candidate] @ weights[members]
            current_error = distances[members, selected[slot]] @ weights[members]
            if candidate_error <= current_error:
                selected[slot] = candidate
        if selected == previous:
            break
    return hardware[selected]


def map_image(image, palette):
    source = np.array(image.convert('RGB'))
    colors, inverse = np.unique(source.reshape(-1, 3), axis=0, return_inverse=True)
    differences = _palette_space(colors)[:, None] - _palette_space(palette)[None]
    distances = (differences ** 2).sum(axis=2)
    indices = distances.argmin(axis=1)[inverse].astype(np.uint8)
    result = Image.frombytes('P', image.size, indices.tobytes())
    result.putpalette(palette.flatten().tolist() + [0] * (768 - palette.size))
    return result


def color_error(original, converted):
    rgba = np.asarray(original.convert('RGBA'))
    visible = rgba[:, :, 3] >= 128
    if not visible.any():
        return dict(rgb_rmse=0.0, oklab_rmse=0.0)
    source = rgba[:, :, :3][visible].astype(np.float64)
    target = np.asarray(converted.convert('RGB'))[visible].astype(np.float64)
    return dict(rgb_rmse=float(np.sqrt(np.mean((source - target) ** 2))),
                oklab_rmse=float(np.sqrt(np.mean((_oklab(source) -
                                                _oklab(target)) ** 2))))
