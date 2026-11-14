"""Import the native enemy subset from original placements and Lua constants."""
import hashlib
import math
from pathlib import Path
from lua_source import ast, nodes, literal, name_of
from content import map_objects


def source_number(node, values):
    if isinstance(node, nodes.Number):
        return node.n
    if isinstance(node, nodes.Name) and isinstance(values.get(node.id), (int, float)):
        return values[node.id]
    operations = {'MultOp': lambda a, b: a*b, 'AddOp': lambda a, b: a+b,
                  'SubOp': lambda a, b: a-b, 'FloatDivOp': lambda a, b: a/b}
    if type(node).__name__ in operations:
        return operations[type(node).__name__](source_number(node.left, values), source_number(node.right, values))
    if type(node).__name__ == 'UMinusOp':
        return -source_number(node.operand, values)
    raise ValueError('enemy setting is not constant arithmetic')


def source_value(node, values):
    try:
        return literal(node)
    except ValueError:
        if isinstance(node, nodes.Name) and node.id in values:
            return values[node.id]
        return source_number(node, values)


def constants(path):
    tree = ast.parse(Path(path).read_text(encoding='utf-8-sig'))
    result = {}
    for statement in tree.body.body:
        if isinstance(statement, nodes.Assign):
            for target, value in zip(statement.targets, statement.values):
                if isinstance(target, nodes.Name):
                    if isinstance(value, nodes.Table) and target.id == 'properties':
                        result[target.id] = {name_of(field.key): source_value(field.value, result) for field in value.fields}
                    else:
                        try:
                            result[target.id] = literal(value)
                        except ValueError:
                            try:
                                result[target.id] = source_number(value, result)
                            except ValueError:
                                pass
        if isinstance(statement, nodes.Function) and name_of(statement.name) == 'initialize':
            for child in statement.body.body:
                if isinstance(child, nodes.Assign):
                    for target, value in zip(child.targets, child.values):
                        if isinstance(target, nodes.Name) and target.id == '_fire_delays':
                            result[target.id] = literal(value)
                if isinstance(child, nodes.Call) and name_of(child.func) == 'addWeapon' and len(child.args) == 5:
                    result['_weapon'] = [name_of(child.args[0])] + [source_number(value, result) for value in child.args[1:]]
    return result


def crop_enemies(data, recipe):
    left, top, width, height = recipe['crop_pixels']
    _, objects = map_objects(Path(data) / recipe['map'])
    definitions, unsupported, paths, hashes = [], [], [], {}
    for obj in objects:
        if obj['group'] != 'enemies' or not (left <= obj['x'] < left + width and top <= obj['y'] < top + height):
            continue
        script = obj['properties'].get('script')
        if script not in ('rat.lua', 'arrowtrap_2.lua', 'blob_2.lua', 'cannon_2.lua', 'orb.lua', 'frog_blue.lua') or obj['rotation']:
            unsupported.append(dict(id=obj['id'], script=script, reason='enemy behavior not ported'))
            continue
        if script=='blob_2.lua' and any(key in obj['properties'] for key in ('gravity_scale','jump_height_px','jump_interval_ms')):
            unsupported.append(dict(id=obj['id'],script=script,reason='blob jump/drop variant not ported'));continue
        path = Path(data) / 'scripts/enemies' / script
        values = constants(path)
        hashes[script] = hashlib.sha256(path.read_bytes()).hexdigest()
        props = values['properties']
        first = len(paths)
        if script == 'orb.lua':
            if not obj['properties'].get('generate_path'):
                unsupported.append(dict(id=obj['id'], script=script, reason='orb requires original generated collision contour'))
                continue
            from enemy_paths import generated_orb_path
            try:
                generated, contour_hashes = generated_orb_path(data, recipe['map'], obj)
            except ValueError as error:
                unsupported.append(dict(id=obj['id'], script=script, reason=str(error)))
                continue
            points = [[x-left, y-top] for x, y in generated]
            if len(points) > 8 or any(not (0 <= x < width and 0 <= y < height) for x, y in points):
                unsupported.append(dict(id=obj['id'], script=script, reason='generated orb contour exceeds room/path bounds'))
                continue
            paths.extend(points)
            hashes.update(contour_hashes)
            exact_speed = 192 * 0.2 * float(obj['properties'].get('speed', 1.0)) / 60 * 16
            speed = math.floor(exact_speed)
            fraction = round((exact_speed-speed) * 256)
            if fraction == 256:
                speed += 1
                fraction = 0
            definitions.append(dict(source_id=obj['id'], kind='DECEPTUS_ORB', x=int(obj['x'])-left,
                y=int(obj['y'])-top, path_first=first, path_count=len(points), alignment=0, time_offset=0,
                speed=speed, acceleration=fraction, damage=int(props['damage']), period=0, delays=[0,0,0],
                cycles=[0]*5, animation_fps=int(obj['properties'].get('animation_speed', values['mAnimSpeed'])),
                idle_ticks=0))
            continue
        if script == 'frog_blue.lua':
            settings = obj['properties']
            alignment = {'left': 0, 'right': 1}.get(settings.get('alignment', 'left'))
            orb_path = settings.get('orb_path', values['DEFAULT_ORB_PATH'])
            speed = float(settings.get('orb_speed', values['DEFAULT_FIREBALL_SPEED'])) / 60
            wave_speed = float(settings.get('wave_speed', values['DEFAULT_WAVE_SPEED']))
            wave_strength = float(settings.get('wave_strength', values['DEFAULT_WAVE_STRENGTH_PX']))
            phase = speed * 0.05 * wave_speed
            period = round(float(settings.get('attack_interval', values['DEFAULT_ATTACK_INTERVAL'])) * 60)
            lifetime = round(float(settings.get('orb_lifetime', values['DEFAULT_ORB_LIFETIME'])) * 60 / 1000)
            collision = int(settings.get('orb_collision_size', values['DEFAULT_ORB_COLLISION_SIZE_PX']))
            if (alignment is None or orb_path not in ('straight', 'sine') or not 0 < speed < 8 or
                    not 0 < period < 65000 or not 0 < lifetime <= 1000 or not 1 <= collision <= 64 or
                    not 0 <= wave_strength <= 64 or not 0 <= phase < math.pi or
                    (orb_path == 'sine' and 2 * wave_strength * math.sin(phase/2) >= 8)):
                raise ValueError(f'frog {obj["id"]}: unsupported projectile or fixed-step wave settings')
            definitions.append(dict(source_id=obj['id'], kind='DECEPTUS_FROG_BLUE', x=int(obj['x'])-left,
                y=int(obj['y'])-top, path_first=first, path_count=0, alignment=alignment, time_offset=0,
                speed=round(speed*16), acceleration=0, damage=int(props['damage']), period=period,
                delays=[lifetime, round(phase/(2*math.pi)*65536) if orb_path == 'sine' else 0, round(wave_strength*16)],
                cycles=[collision, int(orb_path == 'sine'), 0, 0, 0],
                animation_fps=int(settings.get('idle_anim_speed', values['_idle_anim_speed'])), idle_ticks=0))
            continue
        raw_path = [int(field.strip()) * 24 for field in obj['properties'].get('path', '').split(',') if field.strip()]
        if len(raw_path) % 2:
            raise ValueError(f'enemy {obj["id"]}: patrol path needs x/y pairs')
        if len(raw_path) == 2:
            raw_path = [int(obj['x']), int(obj['y'])] + raw_path
        if script in ('rat.lua','blob_2.lua') and not raw_path:
            raise ValueError(f'rat {obj["id"]}: no authored patrol path')
        points = [[x-left, y-top] for x, y in zip(raw_path[::2], raw_path[1::2])]
        if any(not (0 <= x < width and 0 <= y < height) for x, y in points):
            unsupported.append(dict(id=obj['id'], script=script, reason='patrol path leaves loaded room'))
            continue
        paths.extend(points)
        if len(paths) - first > 8:
            raise ValueError('enemy patrol exceeds eight points')
        cannon = script == 'cannon_2.lua'
        alignment = {'left': 0, 'right': 1, 'down': 2, 'up': 3}.get(obj['properties'].get('alignment', 'left' if cannon else 'right'))
        if alignment is None:
            raise ValueError('invalid arrow-trap alignment')
        rat = script == 'rat.lua'
        blob = script == 'blob_2.lua'
        patrol = rat or blob
        if cannon:
            weapon = values.get('_weapon')
            if not weapon or weapon[0] != 'WeaponType.Gun' or weapon[1:] != [1000, 60, 0.0, 0.2] or alignment > 1:
                raise ValueError(f'cannon {obj["id"]}: unsupported gun parameters or alignment')
            # source gun applies an impulse to a density-1 circle of radius 0.2m.
            speed = round(values['mSpeed'] / (math.pi * weapon[4] ** 2) * 48 / 60 * 16)
            period = round(float(obj['properties'].get('fire_interval_s', values['mFireInterval'])) * 60)
            if not 0 < period <= 900:
                raise ValueError('cannon fire interval exceeds the bounded 15-second animation counter')
        definitions.append(dict(source_id=obj['id'], kind='DECEPTUS_RAT' if rat else 'DECEPTUS_BLOB_2' if blob else 'DECEPTUS_CANNON_2' if cannon else 'DECEPTUS_ARROW_TRAP',
            x=int(obj['x'])-left, y=int(obj['y'])-top, path_first=first, path_count=len(paths)-first,
            alignment=alignment, time_offset=round(float(obj['properties'].get('time_offset_s', 0))*60),
            speed=speed if cannon else round((props['velocity_walk_max'] if patrol else values['_speed'] * 5) * 48/60*16),
            acceleration=round(props.get('acceleration_ground', 0)*48/60*16),
            damage=int(props['damage'] if patrol else weapon[2] if cannon else 60),
            period=period if cannon else 0 if patrol else round(values['_fire_interval']*60),
            delays=[0,0,0] if patrol or cannon else [round(value*60) for value in values['_fire_delays']],
            cycles=([values[k] for k in ('SPRITE_COUNT_IDLE','SPRITE_COUNT_LEFT','SPRITE_COUNT_RIGHT','SPRITE_COUNT_DEATH','IDLE_CYCLE_COUNT')] if blob else values.get('CYCLE_LENGTHS', [0]*5)),
            animation_fps=16 if cannon else int(values.get('ANIMATION_SPEED', 20)),
            idle_ticks=round(values.get('DELAY_BETWEEN_IDLE_FRAMES', 0)*60)))
    for definition in definitions:
        variable_fps = definition['kind'] in ('DECEPTUS_ORB', 'DECEPTUS_FROG_BLUE')
        if (not 0 < definition['speed'] < 128 or
                (variable_fps and not 1 <= definition['animation_fps'] <= 60) or
                (not variable_fps and definition['animation_fps'] != (16 if definition['kind'] == 'DECEPTUS_CANNON_2' else 20))):
            raise ValueError('enemy motion or animation exceeds the supported fixed-step contract')
        if any(not 0 <= definition[key] <= 65534 for key in ('source_id', 'time_offset', 'damage', 'period')):
            raise ValueError('enemy property exceeds the 16-bit runtime budget')
        if definition['kind'] == 'DECEPTUS_RAT' and (not definition['acceleration'] or not 0 <= definition['idle_ticks'] <= 255):
            raise ValueError('unsupported rat movement or idle timing')
        if any(not 0 <= delay <= 65520 for delay in definition['delays']):
            raise ValueError('trap delay exceeds runtime budget')
    if len(definitions) > 8 or len(paths) > 64:
        raise ValueError('enemy room exceeds its fixed-capacity budget')
    ignored = [dict(id=obj['id'], script=obj['properties']['script'],
                    reason='Room::deserialize does not execute room script properties; only enemy-group objects spawn Lua nodes')
               for obj in objects if obj['group'] == 'rooms' and obj['properties'].get('script') and
               obj['x'] < left + width and obj['x'] + obj['width'] > left and
               obj['y'] < top + height and obj['y'] + obj['height'] > top]
    return dict(definitions=definitions, paths=paths, unsupported=unsupported, source_hashes=hashes,
                ignored_room_scripts=ignored)
