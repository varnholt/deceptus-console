"""Encode portable streamed rooms from the validated lossless content imports."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from assets import ROOT
from world_export import capped_rope, collision_data


def _constants():
    result = {}
    for filename in ('mechanisms.h', 'enemies.h'):
        source = (ROOT / 'include/deceptus' / filename).read_text()
        for name, value in re.findall(r'^#define ([A-Z][A-Z0-9_]*) (\d+)$', source, re.M):
            result[name] = int(value)
    return result


def encode_room(source, recipe):
    """DRM1 has big-endian scalars and no pointers, padding or compiler ABI."""
    source = Path(source)
    constants = _constants()
    collision, columns, rows = collision_data(source, recipe)
    mechanisms = json.loads((source / 'mechanisms.json').read_text())
    enemies_path = source / 'enemies.json'
    enemies = (json.loads(enemies_path.read_text()) if enemies_path.exists()
               else dict(definitions=[], paths=[]))
    definitions, targets = mechanisms['definitions'], mechanisms['targets']
    actors, paths = enemies['definitions'], enemies['paths']
    if not (0 < columns <= 255 and 0 < rows <= 255):
        raise ValueError(f'room exceeds fixed-point coordinates: {columns}x{rows}')
    water = recipe.get('water_physics')
    player = [recipe.get(name, 0) for name in (
        'max_health', 'skills', 'player_gravity', 'player_gravity_fraction',
        'player_speed', 'player_fall_gravity')]
    payload = bytearray(struct.pack(
        '>4sHH6h4H7B', b'DRM1', columns, rows,
        *(recipe['spawn'] + recipe['patrol'] + recipe['door']),
        len(definitions), len(targets), len(actors), len(paths),
        *player, bool(water)))
    payload.extend(collision)
    for mechanism in definitions:
        kind = mechanism['kind']
        values = [mechanism[name] for name in ('target_first', 'target_count', 'value')]
        bounds, values[2] = capped_rope(mechanism, recipe)
        payload.extend(struct.pack(
            '>H4h3H3B', mechanism['source_id'], *bounds, *values,
            constants[kind] if isinstance(kind, str) else kind,
            mechanism['enabled'], mechanism['flags']))
        payload.extend(struct.pack('>5HB', *mechanism.get('timing', [0] * 6)))
    payload.extend(struct.pack(f'>{len(targets)}H', *targets))
    for actor in actors:
        payload.extend(struct.pack(
            '>H2h3H2h5H9B',
            *(actor[name] for name in ('source_id', 'x', 'y', 'path_first',
                                      'path_count', 'time_offset', 'speed',
                                      'acceleration', 'damage', 'period')),
            *actor['delays'], constants[actor['kind']], actor['alignment'],
            *actor['cycles'], actor['animation_fps'], actor['idle_ticks']))
    for point in paths:
        payload.extend(struct.pack('>2h', *point))
    if water:
        payload.extend(struct.pack('>26B', *water,
                                   *recipe.get('water_splash_ticks', [0] * 20)))
    expected = (35 + len(collision) + len(definitions) * 30 + len(targets) * 2
                + len(actors) * 35 + len(paths) * 4 + (26 if water else 0))
    if len(payload) != expected:
        raise ValueError('room encoder layout mismatch')
    return bytes(payload), dict(collision_bytes=len(collision),
                                mechanisms=len(definitions), targets=len(targets),
                                enemies=len(actors), paths=len(paths))


def compile_rooms(recipes, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    rooms = []
    for index, (recipe, source) in enumerate(recipes):
        payload, counts = encode_room(source, recipe)
        filename = f'room{index:02d}.drm'
        (output / filename).write_bytes(payload)
        rooms.append(dict(room=index, file=filename, title=recipe['title'],
                          bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest(),
                          **counts))
    report = dict(format='DRM1', byte_order='big-endian', rooms=rooms,
                  capacities={name: max(room[name] for room in rooms)
                              for name in ('bytes', 'mechanisms', 'targets',
                                           'enemies', 'paths')})
    (output / 'rooms.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    from snes_tutorial_assets import ensure_original_imports

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, default=ROOT / 'assets/game/data')
    parser.add_argument('--output-dir', type=Path,
                        default=ROOT / 'build/amiga-opening/rooms')
    args = parser.parse_args()
    recipes, _ = ensure_original_imports(args.data)
    report = compile_rooms(recipes, args.output_dir)
    print(json.dumps(report['capacities'], indent=2))


if __name__ == '__main__':
    main()
