"""Read original opening sprite rectangles without target palette conversion."""

import json
from pathlib import Path
import re

from PIL import Image

from assets import ROOT

# the normal player frames every target lists ahead of the shared families.
ACTOR_FRAMES = 26
FRAMES = ROOT / 'assets/generated/gba/area0/mechanisms-frames.json'


def shared_sprite_count():
    """Frames of the shared mechanism, enemy and effect families, as the GBA conversion lists them."""
    return len(json.loads(FRAMES.read_text()))


def sprite_resource_count():
    return ACTOR_FRAMES + shared_sprite_count()


ROTATIONS = {90: Image.Transpose.ROTATE_90,
             180: Image.Transpose.ROTATE_180,
             270: Image.Transpose.ROTATE_270}


def record_image(record, data, output, cache):
    """The source pixels of one converted frame: its rectangle, rotation and tint."""
    import water_tint
    folder = Path(output) if record.get('source_generated') else Path(data) / 'sprites'
    path = folder / record['source']
    if path not in cache:
        cache[path] = Image.open(path).convert('RGBA')
    x, y, width, height = record['source_rect']
    image = cache[path].crop((x, y, x + width, y + height))
    if record.get('rotation'):
        image = image.transpose(ROTATIONS[record['rotation']])
    if record.get('tint') == water_tint.NAME:
        image = water_tint.tint(image)
    return image


def source_sprites(data, output):
    from headtorch_assets import compile_headtorch
    from rope_assets import compile_rope
    from spike_ball_assets import compile_spike_ball
    from item_icon_assets import compile_item_icons

    data, output = Path(data), Path(output)
    compile_headtorch(data, output)
    compile_rope(data, output)
    compile_spike_ball(data, output)
    compile_item_icons(data, output)
    records = json.loads(FRAMES.read_text())
    cache = {}
    images = [record_image(record, data, output, cache) for record in records]
    animations = json.loads((data / 'sprites/animations.json').read_text())
    player = Image.open(data / 'sprites/player.png').convert('RGBA')
    selection = ([('player_idle_r', index) for index in range(8)]
                 + [('player_run_r', index) for index in range(8)]
                 + [('player_jump_up_r', 0), ('player_jump_down_r', 0), (None, 0)]
                 + [('player_bend_down_r', index) for index in range(7)])
    normal = []
    for name, index in selection:
        if name is None:
            normal.append(Image.new('RGBA', (32, 48)))
            continue
        animation = animations[name]
        x, y = animation['frame_offset']
        width, height = animation['frame_size']
        origin_x, origin_y = animation['origin']
        left = round(x + index * width + origin_x - 16)
        top = round(y + origin_y - 48)
        normal.append(player.crop((left, top, left + 32, top + 48)))
    return (normal + images,
            [dict(source='player.png', trim_offset=[0, 0]) for _ in normal] + records)


FAMILY_NAMES = (
    'ACTOR', 'LEVER', 'SPIKES', 'BLOCK', 'COLLAPSE', 'RAT', 'TRAP',
    'MUZZLE', 'ARROW', 'SWIM', 'BLOB', 'SPLASH', 'SWORD', 'CANNON',
    'CANNONBALL', 'IRON_DOOR', 'WOODEN_PLANKS', 'HEADTORCH',
    'HEADTORCH_BEAM', 'ORB', 'FROG_BLUE', 'BLUE_FIREBALL', 'GRAB_ROPE',
    'PLAYER_ROPE', 'MOVEABLE_BOX', 'CRUSHER', 'FLOOR_SPIKES', 'SPIKE_BALL',
    'CHECKPOINT_CLOCK', 'ROPE_LINK', 'ITEM_ICON',
)


def sprite_families(resource_count):
    source = (ROOT / 'assets/generated/gba/gba_assets.h').read_text()
    first = {name: int(value) + 26 for name, value in re.findall(
        r'^#define GBA_(\w+)_FIRST (\d+)$', source, re.M)}
    first['ACTOR'] = 0
    starts = [first[name] for name in FAMILY_NAMES] + [resource_count]
    if starts != sorted(starts) or starts[1] != 26:
        raise ValueError('source sprite family layout changed')
    return [dict(name=name, first=starts[index],
                 count=starts[index + 1] - starts[index])
            for index, name in enumerate(FAMILY_NAMES)]
