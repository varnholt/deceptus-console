"""Amiga-specific treatments of tile animations.

The shared generators stay the source of every clip: they decide what an animation shows and for
how long. A treatment here only re-times frames the shared pipeline produced, so changes made there
still reach the Amiga. Treatments are chosen by animation name (from the shared animations.json),
and a clip without one is converted unchanged.

The 68000 recomposes every 16x16 cell an animation changes, and a clip whose frames hold more
distinct cells than the masked cell cache misses on nearly every repaint.
"""
import json
import struct
from pathlib import Path

import numpy as np

from asset_decode import unpackbits

CLIP_FORMAT = '>4h4H2B2I'
CLIP_BYTES = struct.calcsize(CLIP_FORMAT)
ROOT = Path(__file__).resolve().parents[1]


def hold_similar(max_frames):
    """Merge each run of frames that differ little from the run's first frame into one held frame.

    The threshold is the smallest number of differing 8x8 tiles that leaves at most max_frames
    frames, so large moves (a pulse) keep their frames and slow drifts become a few steps. Every
    merged frame adds its duration to the held one: the cycle keeps its length and phase.
    """
    def plan(grids, durations):
        count = len(grids)
        flat = [grid.reshape(-1) for grid in grids]

        def merge(threshold):
            kept = []
            for index in range(count):
                if kept and np.count_nonzero(flat[kept[-1][0]] != flat[index]) <= threshold:
                    kept[-1][1] += durations[index]
                else:
                    kept.append([index, durations[index]])
            return kept

        limit = max(len(grid) for grid in flat)
        low, high = 0, limit
        while low < high:
            middle = (low + high) // 2
            if len(merge(middle)) <= max_frames:
                high = middle
            else:
                low = middle + 1
        return [tuple(entry) for entry in merge(low)]
    return plan


def every_nth(step):
    """Keep every step-th frame, each lasting as long as the frames it stands for.

    For steady motion (waves, rising sparks) where no frame stands out: the loop keeps its speed
    and length at a lower frame rate, and the distinct cells shrink by about the step.
    """
    def plan(grids, durations):
        kept = []
        for index in range(len(grids)):
            if index % step:
                kept[-1][1] += durations[index]
            else:
                kept.append([index, durations[index]])
        return [tuple(entry) for entry in kept]
    return plan


# animation name -> treatment.
OVERRIDES = {
    # a shader in the original game (a heartbeat-scaled, slowly wobbling ring): its baked 24 frames
    # hold ~740 distinct cells, five times the cache. the pulse keeps its frames, the wobble holds.
    'sword_ring': hold_similar(6),
    # rising sparks at ~20 fps: 437 distinct cells; ~10 fps reads the same.
    'shrine_column': every_nth(2),
    # wide water surfaces at 10 fps: room 10's 40-cell strip holds 275 distinct cells (over the
    # cache) and changes ~34 cells per step; 5 fps halves both. room 6's strip fits but repaints
    # ~20 cells per step.
    'surface_a': every_nth(2),
    'surface_c': every_nth(2),
}


def _durations(world, clip):
    frames, ticks, offset = clip[4], clip[5], clip[10]
    if offset:
        return list(struct.unpack_from(f'>{frames}H', world, offset))
    return [max(1, ticks)] * frames


def _grids(world, clip):
    """Each frame's 8x8 pattern indices (rows x columns), read like native_layers reads them."""
    columns, height, frames, offset = clip[2] // 8, clip[3] // 8, clip[4], clip[11]
    start = offset + (height * frames + 1) * 4
    rows = []
    for row in range(height * frames):
        first, last = struct.unpack_from('>2I', world, offset + row * 4)
        rows.append(np.frombuffer(unpackbits(world[start + first:start + last], columns * 2, 2), dtype='>u2'))
    grid = np.asarray(rows)
    return [grid[frame * height:(frame + 1) * height] for frame in range(frames)]


def clip_names(room):
    """Animation names by clip bounds, from the shared generator's catalogue of this room."""
    path = ROOT / 'assets' / 'generated' / 'gba' / f'area{room}' / 'animations.json'
    if not path.exists():
        return {}
    return {tuple(entry['bounds']): entry['name'] for entry in json.loads(path.read_text())}


def plan_clips(world, room):
    """Per clip index, the kept (source frame, ticks) list of every clip that has a treatment."""
    header = struct.unpack_from('>4s6H8I', world)
    names = clip_names(room)
    plans = {}
    for index in range(header[5]):
        clip = list(struct.unpack_from(CLIP_FORMAT, world, header[12] + index * CLIP_BYTES))
        treatment = OVERRIDES.get(names.get(tuple(clip[:4])))
        if not treatment or clip[4] < 2:
            continue
        kept = treatment(_grids(world, clip), _durations(world, clip))
        if len(kept) < clip[4]:
            plans[index] = kept
    return plans


def apply_plans(world, plans):
    """The world with planned clips re-timed: fewer frames, explicit per-frame durations.

    Frame pixels are untouched here (the cell map still lists every source frame); native_layers
    emits only the kept frames, in plan order.
    """
    if not plans:
        return world
    header = struct.unpack_from('>4s6H8I', world)
    result = bytearray(world)
    for index, kept in sorted(plans.items()):
        position = header[12] + index * CLIP_BYTES
        clip = list(struct.unpack_from(CLIP_FORMAT, result, position))
        if len(result) & 1:
            result.append(0)
        clip[10] = len(result)
        result.extend(struct.pack(f'>{len(kept)}H', *(ticks for _, ticks in kept)))
        clip[4] = len(kept)
        clip[5] = 1
        struct.pack_into(CLIP_FORMAT, result, position, *clip)
    return bytes(result)
