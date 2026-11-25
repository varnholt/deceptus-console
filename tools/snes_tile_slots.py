"""Fixed VRAM slots for a room's background patterns.

Every room cell names a pattern. The runtime used to keep a reference-counted cache of the
patterns in the visible window. Here a room's static cells (both map layers and the single-frame
clips that flags switch on and off) get fixed slots instead, so the runtime only checks whether a
slot already holds the pattern a cell needs. Cells whose pattern cannot get a slot, and every cell
of an animated clip, stay dynamic: the runtime cache serves them from a pool of slots that both
layers can address.

A pattern used in one 33x29-cell region of the room is one copy. Two copies of different patterns
conflict when some of their cells can be visible together, which on a 32x28-cell screen with fine
scrolling means within 32 columns and 28 rows; conflicting copies need different slots, copies of
one pattern may share one. Slots are numbered like the
runtime cache: 0-1023 are VRAM 2000-5fff (the background addresses all of them, the foreground the
first 768), 1024-1143 are foreground-only overflow patterns at 1880-1fff. Slot 0 holds the blank
pattern 0.
"""

import hashlib
from pathlib import Path

import numpy as np

SLOTS = 1144
SHARED_SLOTS = 768           # slots both layers address
BACKGROUND_ONLY = range(768, 1024)
FOREGROUND_ONLY = range(1024, 1144)
WINDOW_COLUMNS, WINDOW_ROWS = 33, 29
REGION_COLUMNS, REGION_ROWS = 33, 29
MAX_ROUNDS = 6
# the foreground's tile for slot 1024, SNES_BG_OVERFLOW_TILE in background_work.h.
OVERFLOW_TILE = 136
# a static cell's pattern word carries this flag; its attribute word's tile bits name the slot.
STATIC_FLAG = 0x8000
TILE_BITS = 0x03ff


class Cells:
    """Pattern ids by cell for one room: two map layers and the clip frames, each a 2-D array."""

    def __init__(self, background, foreground, clips):
        self.background = background
        self.foreground = foreground
        # each clip: dict(left, top, plane, frames=[2-D arrays])
        self.clips = clips


def _static_cells(cells):
    """(plane, y, x, pattern, source) of every cell a fixed slot may serve; pattern 0 is the blank
    slot 0. source 0 and 1 are the map layers, 2 + k the single-frame clip k."""
    entries = []
    for plane, grid in ((0, cells.background), (1, cells.foreground)):
        ys, xs = np.nonzero(grid)
        entries.append(np.stack([np.full_like(ys, plane), ys, xs, grid[ys, xs], np.full_like(ys, plane)], axis=1))
    for number, clip in enumerate(cells.clips):
        if len(clip['frames']) != 1:
            continue
        grid = clip['frames'][0]
        ys, xs = np.nonzero(grid)
        entries.append(np.stack([np.full_like(ys, clip['plane']), ys + clip['top'], xs + clip['left'], grid[ys, xs],
                                 np.full_like(ys, 2 + number)], axis=1))
    return np.concatenate(entries) if entries else np.zeros((0, 5), dtype=np.int64)


def _copies(entries):
    """Copy index of every static cell, and per copy its pattern and the layers that show it."""
    region = (entries[:, 1] // REGION_ROWS) * 4096 + entries[:, 2] // REGION_COLUMNS
    keys = entries[:, 3] * (1 << 24) + region
    unique, index = np.unique(keys, return_inverse=True)
    masks = np.zeros(len(unique), dtype=np.int64)
    np.bitwise_or.at(masks, index, 1 << entries[:, 0])
    return index, unique >> 24, masks


def _conflicts(entries, index, patterns, rows, columns):
    """Neighbour lists: copies of different patterns with cells within the visible window of each
    other. Copies of one pattern may always share a slot."""
    count = len(patterns)
    # several static cells may share a position (a layer and a clip over it); keep each one.
    layers = []
    order = np.lexsort((entries[:, 0], entries[:, 1], entries[:, 2]))
    remaining = order
    while len(remaining):
        layer = np.full((rows + WINDOW_ROWS, columns + 2 * WINDOW_COLUMNS), -1, dtype=np.int64)
        position = entries[remaining, 1] * 65536 + entries[remaining, 2]
        _, first = np.unique(position, return_index=True)
        chosen = remaining[first]
        layer[entries[chosen, 1], entries[chosen, 2] + WINDOW_COLUMNS - 1] = index[chosen]
        layers.append(layer)
        remaining = np.setdiff1d(remaining, chosen, assume_unique=True)
    pairs = []
    for dy in range(0, WINDOW_ROWS):
        for dx in range(-(WINDOW_COLUMNS - 1), WINDOW_COLUMNS):
            for a in layers:
                here = a[:rows, WINDOW_COLUMNS - 1:WINDOW_COLUMNS - 1 + columns].ravel()
                for b in layers:
                    there = b[dy:dy + rows, WINDOW_COLUMNS - 1 + dx:WINDOW_COLUMNS - 1 + dx + columns].ravel()
                    keep = (here >= 0) & (there >= 0) & (here != there)
                    pairs.append(np.minimum(here[keep], there[keep]) * count + np.maximum(here[keep], there[keep]))
    pairs = np.unique(np.concatenate(pairs)) if pairs else np.zeros(0, dtype=np.int64)
    a, b = pairs // count, pairs % count
    a, b = a[patterns[a] != patterns[b]], b[patterns[a] != patterns[b]]
    order = np.argsort(np.concatenate([a, b]), kind='stable')
    source = np.concatenate([a, b])[order]
    target = np.concatenate([b, a])[order]
    starts = np.searchsorted(source, np.arange(count + 1))
    return [target[starts[v]:starts[v + 1]] for v in range(count)]


def _allowed(masks, pool_first):
    """Per copy the slots it may take in preference order: those only its layer addresses first, so
    the shared ones stay for others."""
    shared = list(range(1, pool_first))
    own = {1: list(BACKGROUND_ONLY), 2: list(FOREGROUND_ONLY), 3: []}
    choices = {mask: np.array(own[mask] + shared, dtype=np.int64) for mask in (1, 2, 3)}
    return [choices[int(mask)] for mask in masks]


def _colour(neighbours, allowed, dynamic):
    """DSatur within each copy's allowed slots, skipping dynamic copies; copies left over get no
    slot (-1)."""
    count = len(allowed)
    colour = np.full(count, -1, dtype=np.int64)
    seen = np.zeros((count, SLOTS), dtype=bool)
    saturation = np.zeros(count, dtype=np.int64)
    degree = np.array([len(n) for n in neighbours], dtype=np.int64)
    pending = ~np.asarray(dynamic, dtype=bool)
    for _ in range(int(pending.sum())):
        # most saturated first, then most neighbours, then the lowest index
        v = int(np.where(pending, saturation * (count + 1) + degree, -1).argmax())
        pending[v] = False
        slots = allowed[v]
        free = ~seen[v, slots]
        if not free.any():
            continue
        slot = int(slots[free.argmax()])
        colour[v] = slot
        nearby = neighbours[v]
        saturation[nearby[~seen[nearby, slot]]] += 1
        seen[nearby, slot] = True
    return colour


def _repair(neighbours, allowed, colour):
    """Give left-over copies a free slot, or a slot whose only holder among their neighbours can
    move to a slot none of its own neighbours holds."""
    colour = colour.copy()

    def held(v):
        near = colour[neighbours[v]]
        return np.bincount(near[near >= 0], minlength=SLOTS)

    improved = True
    while improved:
        improved = False
        for v in np.nonzero(colour < 0)[0]:
            slots = allowed[v]
            counts = held(v)[slots]
            free = slots[counts == 0]
            if len(free):
                colour[v] = free[0]
                improved = True
                continue
            for slot in slots[counts == 1]:
                near = neighbours[v]
                u = int(near[colour[near] == slot][0])
                moves = allowed[u][(held(u)[allowed[u]] == 0) & (allowed[u] != slot)]
                if len(moves):
                    colour[u] = moves[0]
                    colour[v] = slot
                    improved = True
                    break
    return colour


def _window_counts(presence_grids, rows, columns):
    """Per window position, the number of grids with a nonzero cell inside the window."""
    height, width = min(WINDOW_ROWS, rows), min(WINDOW_COLUMNS, columns)
    total = np.zeros((rows - height + 1, columns - width + 1), dtype=np.int64)
    for grid in presence_grids:
        summed = np.zeros((rows + 1, columns + 1), dtype=np.int64)
        summed[1:, 1:] = np.cumsum(np.cumsum(grid, axis=0), axis=1)
        total += (summed[height:, width:] - summed[:-height, width:] - summed[height:, :-width] + summed[:-height, :-width]) > 0
    return total


def _dynamic_demand(cells, leftover_entries, rows, columns):
    """Slots the runtime cache may need at once: in the busiest window, per animated clip the
    patterns of its busiest frame there, plus every leftover static pattern there."""
    total = np.zeros_like(_window_counts([], rows, columns))
    for clip in cells.clips:
        if len(clip['frames']) == 1:
            continue
        busiest = np.zeros_like(total)
        for frame in clip['frames']:
            frame_grids = []
            for pattern in np.unique(frame[frame != 0]):
                grid = np.zeros((rows, columns), dtype=np.int64)
                ys, xs = np.nonzero(frame == pattern)
                grid[ys + clip['top'], xs + clip['left']] = 1
                frame_grids.append(grid)
            busiest = np.maximum(busiest, _window_counts(frame_grids, rows, columns))
        total += busiest
    patterns = {}
    for plane, y, x, pattern, _ in leftover_entries:
        grid = patterns.setdefault(int(pattern), np.zeros((rows, columns), dtype=np.int64))
        grid[y, x] = 1
    total += _window_counts(list(patterns.values()), rows, columns)
    return int(total.max())


def plan(cells):
    """Fixed slots for one room: (pool_first, slots) with the slot of every _static_cells entry, -1
    where the runtime cache serves it; None when the room keeps the fully dynamic cache."""
    rows, columns = cells.background.shape
    entries = _static_cells(cells)
    index, patterns, masks = _copies(entries)
    neighbours = _conflicts(entries, index, patterns, rows, columns)
    demand = _dynamic_demand(cells, entries[:0], rows, columns)
    # copies left over in one round stay dynamic in the next one, with a pool that only grows.
    dynamic = np.zeros(len(masks), dtype=bool)
    for _ in range(MAX_ROUNDS):
        pool_first = SHARED_SLOTS - demand
        if pool_first <= 1:
            return None
        allowed = _allowed(masks, pool_first)
        colour = _repair(neighbours, allowed, _colour(neighbours, allowed, dynamic))
        dynamic = colour < 0
        needed = _dynamic_demand(cells, entries[dynamic[index]], rows, columns)
        if needed <= demand:
            break
        demand = needed
    else:
        return None
    return pool_first, colour[index]


def slot_tile(plane, slot):
    """The map tile that shows a slot on a plane: slotTile in background.c."""
    if plane == 0:
        return slot
    if slot < SHARED_SLOTS:
        return slot + 256
    return slot - FOREGROUND_ONLY.start + OVERFLOW_TILE


def _cache_key(background, foreground, clips, columns, rows):
    digest = hashlib.sha256(Path(__file__).read_bytes())
    digest.update(np.array([columns, rows], dtype='<u4').tobytes() + background + foreground)
    for clip in clips:
        digest.update(np.array([clip[name] for name in ('left', 'top', 'columns', 'rows', 'frames', 'plane')], dtype='<u4').tobytes())
        digest.update(clip['cells'])
    return digest.hexdigest()


def fix_room(background, foreground, clips, columns, rows, cache=None):
    """Plan a converted room and mark its static cells. background and foreground are the layers'
    cell words (pattern, attributes per cell, row by row); each clip is dict(left, top, columns,
    rows, frames, plane, cells) with its frames' cell words. Returns dict(pool_first, pool_end,
    background, foreground, clips) with the cell words rewritten; the runtime cache allocates from
    slots pool_first..pool_end - 1. Plans are kept in the cache folder, when given, by input."""
    planes = [np.frombuffer(data, dtype='<u2').reshape(rows, columns, 2).copy() for data in (background, foreground)]
    clip_words = [np.frombuffer(clip['cells'], dtype='<u2').reshape(clip['frames'], clip['rows'], clip['columns'], 2).copy()
                  for clip in clips]
    cells = Cells(planes[0][:, :, 0].astype(np.int64), planes[1][:, :, 0].astype(np.int64),
                  [dict(left=clip['left'], top=clip['top'], plane=clip['plane'],
                        frames=[frame[:, :, 0].astype(np.int64) for frame in words])
                   for clip, words in zip(clips, clip_words)])
    entries = _static_cells(cells)
    stored = None
    if cache is not None:
        stored = Path(cache) / (_cache_key(background, foreground, clips, columns, rows) + '.npy')
    if stored is not None and stored.is_file():
        saved = np.load(stored)
        pool_first, slots = int(saved[0]), saved[1:]
    else:
        result = plan(cells)
        pool_first, slots = result if result is not None else (1, np.full(len(entries), -1, dtype=np.int64))
        if stored is not None:
            stored.parent.mkdir(parents=True, exist_ok=True)
            np.save(stored, np.concatenate([[pool_first], slots]).astype(np.int64))
    pool_end = SLOTS if pool_first == 1 else SHARED_SLOTS
    for source, words in enumerate(planes + [frames[0] for frames in clip_words]):
        chosen = (entries[:, 4] == source) & (slots >= 0)
        if not chosen.any():
            continue
        top, left = (0, 0) if source < 2 else (clips[source - 2]['top'], clips[source - 2]['left'])
        ys, xs = entries[chosen, 1] - top, entries[chosen, 2] - left
        tiles = np.array([slot_tile(int(plane), int(slot)) for plane, slot in zip(entries[chosen, 0], slots[chosen])])
        words[ys, xs, 0] |= STATIC_FLAG
        words[ys, xs, 1] = (words[ys, xs, 1] & (0xffff ^ TILE_BITS)) | tiles
    return dict(pool_first=pool_first, pool_end=pool_end, background=planes[0].tobytes(), foreground=planes[1].tobytes(),
                clips=[words.tobytes() for words in clip_words])
