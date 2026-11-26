"""Build the experimental 26-room Amiga opening without replacing the prototype."""

import hashlib
import json
from pathlib import Path
import struct

from amiga_opening_assets import (RESOURCE_CHIP_BYTES, RESOURCE_NORMAL_BYTES, amiga_recipes,
                                  compile_opening, converter_identity, write_common)
from amiga_native_assets import prepare_native_assets
from amiga_room_tiles import COLOR_TILE_BYTES, MASK_TILE_BYTES, TILES_PER_BANK
from assets import ROOT
from build_amiga import SOURCES, build_program
from room_content import compile_rooms
from snes_tutorial_assets import ensure_original_imports
from amiga_message_box import compile_message_boxes


OPENING_SOURCES = (
    'src/engine/compression.c', 'src/game/adventure.c', 'src/game/mechanismparts.c', 'src/game/appearance.c', 'src/game/ropelink.c',
    'src/game/roomcontent.c', 'src/game/weapons/playersword.c',
    'src/game/player/playeranimation.c', 'src/game/items/itemheadtorch.c',
    'src/platform/amiga/rooms.c', 'src/platform/amiga/graphics.c',
    'src/platform/amiga/spritecache.c', 'src/platform/amiga/tilearchive.c', 'src/platform/amiga/lz4.c',
    'src/platform/amiga/rastercache.c',
    'src/platform/amiga/opening.c', 'src/platform/amiga/opening_video.c',
    'src/platform/amiga/opening_ui.c',
    'src/platform/amiga/scrollsurface.c',
)


def validate_assets(data, generated):
    from campaign_assets import asset_identity

    report = json.loads((generated / 'assets.json').read_text())
    if (report['source_identity'] != asset_identity(data)
            or report['converter_sha256'] != converter_identity()
            or len(report['rooms']) != 26):
        raise ValueError('Amiga opening assets are stale or partial; rebuild without --reuse-assets')
    for room in report['rooms']:
        for variant in (room, room['sword']):
            for extension, key in (('.agp', 'sha256'), ('.asp', 'sprites_sha256'),
                                   ('.abg', 'background_sha256'), ('.acr', 'raster_sha256'),
                                   ('.acp', 'composite_sha256')):
                path = generated / f"area{room['room']}" / (variant['basename'] + extension)
                if hashlib.sha256(path.read_bytes()).hexdigest() != variant[key]:
                    raise ValueError(f'Amiga opening graphics changed: {path}')
    return report


# mirrors amigaOpeningVideoReset: one decoded sprite frame, four banks of each dictionary and
# 8 KiB of saved bob backgrounds are the least Chip scratch the streamed renderer starts with.
MIN_SAVED_BYTES = 8 * 1024
# two cached gameplay ui text bands of 24 rows x 20 words (UI_MASK_BYTES in opening_streamed.c).
UI_MASK_BYTES = 2 * 24 * 20 * 2
# hardware sprite frame cache (SPRITE_CACHE_WORDS) and the minimum composed-cell sets.
SPRITE_CACHE_BYTES = 32 * 1024
# 16 sets each of plain and masked cells: pixels, keys and recent bytes (PLAIN/MASKED_SET_BYTES).
CELL_MINIMUM_BYTES = 16 * ((64 * 4 + 16 + 2 + 4) + (64 * 8 + 32 + 2))
FIXED_CHIP_BYTES = UI_MASK_BYTES + SPRITE_CACHE_BYTES + CELL_MINIMUM_BYTES


def _align(value):
    return (value + 3) & ~3


def native_chip_scratch(folder, basename):
    """Chip and non-chip scratch left after loadTileGraphics places this room variant."""
    atlas = (folder / (basename + '.atg')).stat().st_size
    metadata = (folder / (basename + '.ans')).read_bytes()
    sprites = (folder / (basename + '.asb')).stat().st_size
    animations = (folder / (basename + '.ant')).stat().st_size
    sprites_at = _align(_align(_align(len(metadata)) + atlas) + animations)
    if sprites_at > RESOURCE_NORMAL_BYTES:
        return None, None
    available = RESOURCE_NORMAL_BYTES - sprites_at
    split = sprites
    if sprites > available:
        table = struct.unpack_from('>I', metadata, 40)[0]
        split = 0
        for index in range(struct.unpack_from('>H', metadata, 10)[0]):
            offset, size = struct.unpack_from('>2I', metadata, table + index * 28 + 12)
            if split < offset + size <= available:
                split = offset + size
    return RESOURCE_CHIP_BYTES - _align(sprites - split), RESOURCE_NORMAL_BYTES - _align(sprites_at + split)


def check_native_budget(generated, report, box_bytes=None):
    """Fail the build for any room the streamed renderer could not enter.

    box_bytes: per room, the message box file loadTileGraphics places after the graphics."""
    sprite_frame = max(variant['max_sprite_bytes'] for room in report['rooms']
                       for variant in (room, room['sword'])) // 6 * 10
    caches = 4 * TILES_PER_BANK * (COLOR_TILE_BYTES + MASK_TILE_BYTES)
    budget, failures = [], []
    for room in report['rooms']:
        for variant in (room, room['sword']):
            scratch, normal = native_chip_scratch(generated / f"area{room['room']}", variant['basename'])
            # the room's message box file follows: non-chip scratch first, else Chip (rooms.c).
            boxes = _align((box_bytes or {}).get(room['room'], 0))
            if boxes and normal is not None and scratch is not None:
                if normal >= boxes:
                    normal -= boxes
                else:
                    scratch -= boxes
            # dictionary caches use non-chip scratch when it holds the minimum, else Chip.
            minimum = sprite_frame + MIN_SAVED_BYTES + FIXED_CHIP_BYTES + (caches if normal is None or normal < caches else 0)
            budget.append(dict(room=room['room'], variant=variant['basename'], chip_scratch=scratch, normal_scratch=normal))
            if scratch is None or scratch < minimum:
                failures.append(f"area{room['room']}/{variant['basename']}: {scratch} < {minimum} bytes")
    minimum = sprite_frame + MIN_SAVED_BYTES + FIXED_CHIP_BYTES
    if failures:
        raise ValueError('rooms exceed the A500 streamed-renderer memory budget:\n  ' + '\n  '.join(failures))
    return dict(minimum_chip_scratch=minimum, rooms=budget)


def build_opening(sdk, data, output, reuse_assets=False, renderer='legacy'):
    data, output = Path(data).resolve(), Path(output).resolve()
    generated = output / 'assets'
    if reuse_assets:
        report = validate_assets(data, generated)
        recipes, _ = ensure_original_imports(data, refresh_metadata=True)
        simulation = compile_rooms(amiga_recipes(recipes), generated / 'rooms')
        write_common(data, generated, recipes, simulation, report['families'],
                     max(variant['max_sprite_bytes'] for room in report['rooms'] for variant in (room, room['sword'])),
                     max(room['clips'] for room in report['rooms']), report['rooms'])
    else:
        report = compile_opening(data, generated)
    prepare_native_assets(generated, report)
    message_boxes = None
    if renderer == 'native':
        message_boxes = compile_message_boxes(data, ROOT / 'assets/generated/gba', generated)
    budget = check_native_budget(generated, report, message_boxes['room_bytes']) if renderer == 'native' else None
    # partition only at independent sprite-block boundaries; no frame-time allocation.
    for room in report['rooms']:
        for variant in (room, room['sword']):
            if (variant['world_bytes'] >= RESOURCE_CHIP_BYTES
                    or variant['bytes'] + variant['max_sprite_bytes'] > RESOURCE_NORMAL_BYTES + RESOURCE_CHIP_BYTES):
                raise ValueError(f"room {room['room']} exceeds the bounded Amiga resource banks")
    sources = [ROOT / name for name in SOURCES
               if name not in ('src/platform/amiga/main.c', 'src/platform/amiga/video.c')]
    sources.extend(ROOT / name for name in OPENING_SOURCES)
    if renderer == 'native':
        sources = [ROOT / 'src/platform/amiga/opening_streamed.c'
                   if source.name == 'opening_video.c' else source for source in sources]
    sources.append(generated / 'opening_content.c')
    sources.append(generated / 'amiga_native_assets.c')
    if renderer == 'native':
        sources.append(generated / 'amiga_message_box.c')
    build_report = dict(scope='experimental streamed 26-room Amiga opening',
                        renderer=renderer,
                        full_game=False, pixel_scale=1, dithering=False,
                        resource_normal_bytes=RESOURCE_NORMAL_BYTES, resource_chip_bytes=RESOURCE_CHIP_BYTES,
                        graphics=report, native_budget=budget, message_boxes=message_boxes)
    build_program(sdk, output, sources, build_report, generated)
