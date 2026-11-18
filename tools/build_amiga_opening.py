"""Build the experimental 26-room Amiga opening without replacing the prototype."""

import hashlib
import json
from pathlib import Path

from amiga_opening_assets import (RESOURCE_CHIP_BYTES, RESOURCE_NORMAL_BYTES,
                                  compile_opening, converter_identity, write_common)
from assets import ROOT
from build_amiga import SOURCES, build_program
from room_content import compile_rooms
from snes_tutorial_assets import ensure_original_imports


OPENING_SOURCES = (
    'src/engine/compression.c', 'src/game/adventure.c', 'src/game/appearance.c',
    'src/game/roomcontent.c', 'src/game/weapons/playersword.c',
    'src/game/player/playeranimation.c', 'src/game/items/itemheadtorch.c',
    'src/platform/amiga/rooms.c', 'src/platform/amiga/graphics.c',
    'src/platform/amiga/keyboard.c', 'src/platform/amiga/spritecache.c',
    'src/platform/amiga/opening.c', 'src/platform/amiga/opening_video.c',
    'src/platform/amiga/opening_ui.c',
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
                                   ('.abg', 'background_sha256')):
                path = generated / f"area{room['room']}" / (variant['basename'] + extension)
                if hashlib.sha256(path.read_bytes()).hexdigest() != variant[key]:
                    raise ValueError(f'Amiga opening graphics changed: {path}')
    return report


def build_opening(sdk, data, output, reuse_assets=False):
    data, output = Path(data).resolve(), Path(output).resolve()
    generated = output / 'assets'
    if reuse_assets:
        report = validate_assets(data, generated)
        recipes, _ = ensure_original_imports(data, refresh_metadata=True)
        simulation = compile_rooms(recipes, generated / 'rooms')
        write_common(data, generated, recipes, simulation, report['families'],
                     max(variant['max_sprite_bytes'] for room in report['rooms'] for variant in (room, room['sword'])),
                     max(room['clips'] for room in report['rooms']))
    else:
        report = compile_opening(data, generated)
    # partition only at independent sprite-block boundaries; no frame-time allocation.
    for room in report['rooms']:
        for variant in (room, room['sword']):
            if (variant['world_bytes'] >= RESOURCE_CHIP_BYTES
                    or variant['bytes'] + variant['max_sprite_bytes'] > RESOURCE_NORMAL_BYTES + RESOURCE_CHIP_BYTES):
                raise ValueError(f"room {room['room']} exceeds the bounded Amiga resource banks")
    sources = [ROOT / name for name in SOURCES
               if name not in ('src/platform/amiga/main.c', 'src/platform/amiga/video.c')]
    sources.extend(ROOT / name for name in OPENING_SOURCES)
    sources.append(generated / 'opening_content.c')
    build_report = dict(scope='experimental streamed 26-room Amiga opening',
                        full_game=False, pixel_scale=1, dithering=False,
                        resource_normal_bytes=RESOURCE_NORMAL_BYTES, resource_chip_bytes=RESOURCE_CHIP_BYTES,
                        graphics=report)
    build_program(sdk, output, sources, build_report, generated)
