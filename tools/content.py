"""Inventory all upstream levels and export original Tiled objects without losing properties."""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

from content_error import ContentError
from lua_source import analyse, BooleanCompiler, CompileError

ROOT = Path(__file__).resolve().parents[1]


def typed_properties(element):
    result = {}
    for field in element.findall("properties/property"):
        value = field.get("value", field.text or "")
        kind = field.get("type", "string")
        if kind in ("int", "object"):
            value = int(value)
        elif kind == "float":
            value = float(value)
        elif kind == "bool":
            value = value == "true"
        result[field.get("name")] = value
    return result


def map_objects(path):
    root = ET.parse(path).getroot()
    objects = []

    def visit(parent, offset=(0, 0)):
        for child in parent:
            position = (offset[0] + float(child.get("offsetx", "0")), offset[1] + float(child.get("offsety", "0")))
            if child.tag == "group":
                visit(child, position)
            elif child.tag == "objectgroup":
                for item in child.findall("object"):
                    if item.get("template"):
                        raise ContentError('object templates (.tx) need explicit resolution', file=path,
                                           layer=child.get('name', ''), object_id=int(item.get('id')),
                                           object_name=item.get('name', ''),
                                           fix='detach the template in Tiled (Replace with Object) so its properties are stored on the object')
                    shape = "rectangle"
                    points = []
                    for kind in ("polygon", "polyline", "ellipse", "point"):
                        node = item.find(kind)
                        if node is not None:
                            shape = kind
                            points = [list(map(float, pair.split(","))) for pair in node.get("points", "").split()]
                    objects.append({"id": int(item.get("id")), "name": item.get("name", ""),
                        "group": child.get("name", ""), "class": item.get("class", item.get("type", "")),
                        "x": float(item.get("x", "0")) + position[0],
                        "y": float(item.get("y", "0")) + position[1],
                        "width": float(item.get("width", "0")), "height": float(item.get("height", "0")),
                        "rotation": float(item.get("rotation", "0")), "shape": shape, "points": points,
                        "gid": int(item.get("gid", "0")), "properties": typed_properties(item)})
    visit(root)
    return root, objects


def inventory(data, output):
    data, output = Path(data).resolve(), Path(output)
    output.mkdir(parents=True, exist_ok=True)
    # a failed/unsupported recompilation must never leave an old successful fixture.
    for filename in ("source_logic.c", "setSwordCageEnabled.bin", "releaseSwordRing.bin"):
        (output / filename).unlink(missing_ok=True)
    levels = []
    active_scripts = Counter()
    modules = {}
    callback_support = {}
    programs = {}
    callback_objects = []
    for reference in json.loads((data / "config/levels.json").read_text()):
        filename = reference["levelname"]
        config_path = data.parent / filename
        config = json.loads(config_path.read_text())
        map_path = data.parent / config["filename"]
        root, objects = map_objects(map_path)
        counts = Counter(obj["group"] for obj in objects)
        for obj in objects:
            script = obj["properties"].get("script")
            if script and obj["group"] == "enemies":
                active_scripts[script] += 1
        level = {"id": config_path.parent.name, "config": filename,
                 "map": config["filename"], "source_sha256": hashlib.sha256(map_path.read_bytes()).hexdigest(),
                 "tile_width": int(root.get("tilewidth")), "tile_height": int(root.get("tileheight")),
                 "columns": int(root.get("width")), "rows": int(root.get("height")),
                 "group_counts": dict(sorted(counts.items())), "objects": objects}
        levels.append(level)
        script_path = config_path.parent / "level.lua"
        if script_path.exists():
            tree, info = analyse(script_path)
            key = script_path.relative_to(data).as_posix()
            modules[key] = info
            if config_path.parent.name == "level-catacombs":
                for function in ("setSwordCageEnabled", "releaseSwordRing"):
                    compiler = BooleanCompiler(tree, objects)
                    try:
                        program = compiler.compile(function)
                        (output / (function + ".bin")).write_bytes(program)
                        programs[function] = program
                        if function == "releaseSwordRing":
                            callback_objects = [obj for obj in objects if obj["id"] in compiler.resolved_targets]
                        callback_support[function] = {"status": "compiled", "bytes": len(program),
                            "scope": "boolean mechanism actions only; not the complete level script"}
                    except CompileError as error:
                        callback_support[function] = {"status": "unsupported", "reason": str(error)}
    pending = [(data / "scripts/enemies" / name) for name in active_scripts]
    pending += [data / key for key in modules]
    seen = set()
    while pending:
        path = pending.pop()
        key = path.relative_to(data).as_posix()
        if key in seen:
            continue
        seen.add(key)
        tree, info = analyse(path)
        info["source_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        modules[key] = info
        for module in info["requires"]:
            dependency = data.parent / (module + ".lua")
            if not dependency.resolve().is_relative_to(data):
                raise ContentError(f'require escapes the source data folder: {module}', file=key,
                                   fix='require modules inside the data folder only')
            if not dependency.is_file():
                raise ContentError(f'missing required Lua module: {module}', file=key,
                                   fix='add the module under the source data folder or remove the require')
            pending.append(dependency)
    source = data.parent / "src/game/level"
    api = set()
    for name in ("luanode.cpp", "levelscript.cpp"):
        api.update(re.findall(r'lua_register\([^,]+,\s*"([^"]+)"', (source / name).read_text()))
    referenced_api = sorted({call for module in modules.values() for call in module["calls"] if call in api})
    native_groups = ('collapsing_platforms', 'on_off_blocks', 'spike_blocks', 'sensor_rects',
                     'blocking_rects', 'checkpoints', 'levers', 'damage_rects', 'doors', 'destructible_blocking_rects', 'grab_ropes',
                     'moveable_objects', 'box_colliders')
    mechanism_counts = {group: sum(level['group_counts'].get(group, 0) for level in levels) for group in native_groups}
    summary = {"levels": len(levels), "rooms": sum(level["group_counts"].get("rooms", 0) for level in levels),
        "enemy_placements": sum(active_scripts.values()), "active_enemy_scripts": dict(sorted(active_scripts.items())),
        "reachable_lua_modules": len(modules), "referenced_host_api": referenced_api,
        "compiled_callbacks": callback_support,
        "native_mechanism_family_placements": mechanism_counts,
        "native_enemy_family_placements": {name: active_scripts[name] for name in ("rat.lua", "arrowtrap_2.lua", "blob_2.lua", "cannon_2.lua") if name in active_scripts},
        "native_enemy_variant_limits": {
            "blob_2.lua": "normal ground patrol and stomp death; jumping and ceiling-drop variants remain unsupported",
            "cannon_2.lua": "unrotated horizontal source gun, 16fps, fire interval up to15s; bounded circle/AABB collision approximation and no audio"},
        "unported_enemy_types": sorted(set(active_scripts) - {"rat.lua", "arrowtrap_2.lua", "blob_2.lua", "cannon_2.lua"}),
        "native_mechanism_scope": "C components; import validates supported settings; not all placements are loaded or drawn",
        "full_game_supported": False,
        "remaining": ["room/resource streaming", "remaining mechanisms", "enemy behavior and animations",
                      "complete level scripts and cutscenes", "inventory and progression", "audio", "persistent save backend"]}
    if "releaseSwordRing" in programs:
        source = ['#include "deceptus/logic.h"', '', 'const DeceptusMechanismDef source_logic_definitions[] = {']
        for obj in callback_objects:
            kind = "DECEPTUS_MECH_BLOCK" if obj["group"] == "blocking_rects" else "DECEPTUS_MECH_FLAG"
            source.append(f'    {{{obj["id"]}, {{0, 0, 24, 24}}, 0, 0, 0, {kind}, 1, 0}},')
        source += ['};', f'const DeceptusU16 source_logic_definition_count = {len(callback_objects)};',
                   'const char *const source_logic_names[] = {' + ','.join(json.dumps(obj['name']) for obj in callback_objects) + '};',
                   'const char *const source_logic_groups[] = {' + ','.join(json.dumps(obj['group']) for obj in callback_objects) + '};',
                   'const DeceptusU8 source_logic_program[] = {' + ','.join(map(str, programs["releaseSwordRing"])) + '};',
                   f'const DeceptusU16 source_logic_program_length = {len(programs["releaseSwordRing"])};', '']
        (output / "source_logic.c").write_text("\n".join(source))
    (output / "catalog.json").write_text(json.dumps({"levels": levels, "lua": modules, "summary": summary}, indent=2) + "\n")
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return levels, summary


def crop_mechanisms(data, recipe):
    """Export the implemented rectangle semantics; report every remaining object."""
    root, objects = map_objects(Path(data) / recipe["map"])
    left, top, width, height = recipe["crop_pixels"]
    supported = {"checkpoints": "DECEPTUS_MECH_CHECKPOINT", "sensor_rects": "DECEPTUS_MECH_SENSOR",
                 "levers": "DECEPTUS_MECH_LEVER", "blocking_rects": "DECEPTUS_MECH_BLOCK",
                 "collapsing_platforms": "DECEPTUS_MECH_COLLAPSING", "on_off_blocks": "DECEPTUS_MECH_BLOCK", "spike_blocks": "DECEPTUS_MECH_SPIKES", "damage_rects": "DECEPTUS_MECH_DAMAGE", "wind": "DECEPTUS_MECH_WIND",
                 "doors": "DECEPTUS_MECH_DOOR", "destructible_blocking_rects": "DECEPTUS_MECH_DESTRUCTIBLE",
                 "enemies": "DECEPTUS_MECH_CANNON_BODY", "grab_ropes": "DECEPTUS_MECH_GRAB_ROPE",
                 "moveable_objects": "DECEPTUS_MECH_MOVEABLE_BOX", "box_colliders": "DECEPTUS_MECH_BOX_COLLIDER"}
    selected, unsupported = [], []
    for obj in objects:
        object_left, object_top = obj['x'], obj['y']
        object_width, object_height = max(1,obj['width']), max(1,obj['height'])
        if obj['group'] == 'grab_ropes' and obj.get('points'):
            object_left += min(point[0] for point in obj['points'])-8
            object_top += min(point[1] for point in obj['points'])
            object_width = max(point[0] for point in obj['points'])-min(point[0] for point in obj['points'])+16
            object_height = max(point[1] for point in obj['points'])-min(point[1] for point in obj['points'])
        if object_left >= left + width or object_top >= top + height or object_left + object_width <= left or object_top + object_height <= top:
            continue
        reason = None
        if obj["group"] not in supported:
            reason = "runtime or visual adapter not implemented"
        elif obj['group'] == 'enemies' and obj['properties'].get('script') != 'cannon_2.lua':
            reason = 'enemy body is handled by its separate behavior importer'
        elif obj['group'] == 'grab_ropes':
            points = obj.get('points',[]);props = obj['properties']
            if (obj['rotation'] or obj['shape'] != 'polyline' or len(points) != 2 or
                    list(points[0]) != [0,0] or points[1][0] != 0 or not -512 <= points[1][1] < 0 or
                    not 2 <= int(props.get('segments',7)) <= 24 or obj['width'] or
                    props.get('texture','data/sprites/ropes.png') != 'data/sprites/ropes.png' or
                    int(props.get('sprite',1)) != 1 or float(props.get('push_strength',.02)) != 0 or
                    'player_impulse' in props):
                reason = 'grab rope requires a vertical source rope, 2..24 links, plain texture and no ambient push'
        elif obj["rotation"] or obj["shape"] != "rectangle":
            reason = "dynamic mechanisms currently require unrotated rectangles"
        elif obj["properties"].get("mode", "lever") not in ("lever", "interval"):
            reason = "unknown mechanism mode"
        elif obj["group"] == "collapsing_platforms" and (obj["width"] % 24 or obj["height"] != 24):
            reason = "collapsing platform requires 24px segments and a 24px collision height"
        elif obj["group"] == "collapsing_platforms" and (float(obj["properties"].get("destruction_speed", 30)) != 30 or
                                                        float(obj["properties"].get("fall_speed", 6)) != 6):
            reason = "custom collapse visual speeds are not implemented"
        elif obj['group'] == 'moveable_objects' and (obj['width'] != 48 or obj['height'] != 48 or obj['properties'].get('texture')):
            reason = 'only the source 48px box drawn from moveable_box.png is implemented'
        elif obj['group'] == 'doors':
            props = obj['properties']
            if (props.get('texture') != 'data/sprites/iron-door.png' or
                    props.get('animation_open') != 'iron_door_open' or
                    props.get('animation_close') != 'iron_door_close' or
                    props.get('version') or obj['width'] != 96 or obj['height'] != 144):
                reason = 'only the source 96x144 animated iron door is implemented'
            elif props.get('item_required',props.get('key','')) not in ('','red'):
                reason = 'door inventory item has no native identity binding'
        elif obj['group'] == 'destructible_blocking_rects':
            props = obj['properties']
            if (props.get('texture','data/sprites/wooden_planks.png') != 'data/sprites/wooden_planks.png' or
                    int(props.get('frame_width',150)) != 150 or int(props.get('frame_height',163)) != 163 or
                    int(props.get('frame_count',50)) != 50 or float(props.get('animation_speed',40)) != 40):
                reason = 'only the source 150x163, 50-frame wooden planks at 40fps are implemented'
        elif obj['group'] == 'enemies':
            from enemies import constants
            values = constants(Path(data) / 'scripts/enemies/cannon_2.lua')
            weapon = values.get('_weapon')
            period = round(float(obj['properties'].get('fire_interval_s',values['mFireInterval']))*60)
            if (obj['properties'].get('alignment','left') not in ('left','right') or
                    weapon != ['WeaponType.Gun',1000,60,0.0,0.2] or not 0 < period <= 900):
                reason = 'cannon body requires the validated source horizontal gun variant'
        if reason:
            unsupported.append({"id": obj["id"], "name": obj["name"], "group": obj["group"], "reason": reason})
            continue
        selected.append(obj)
    if len(selected) > 32:
        raise ContentError.at(f'crop holds {len(selected)} mechanisms; the runtime budget is 32', recipe,
                              fix=f'remove {len(selected) - 32} mechanisms or split the room; it holds '
                                  + ', '.join(f'{count} {group}' for group, count in Counter(obj['group'] for obj in selected).most_common()))
    targets, definitions = [], []
    selected_ids = {obj["id"] for obj in selected}
    for obj in selected:
        if not 1 <= obj["id"] <= 65535:
            raise ContentError.at('object id exceeds the supported 16-bit identity range (1..65535)', recipe, obj,
                                  fix='renumber the object in Tiled')
        props = obj["properties"]
        names = recipe.get('native_links', {}).get(obj['name'], props.get("target_ids", props.get("target_id", "")))
        first = len(targets)
        for name in str(names).split(";"):
            if not name.strip():
                continue
            matches = [target for target in objects if target["name"] == name.strip()]
            if not matches or any(target["id"] not in selected_ids for target in matches):
                where = 'is outside the supported crop' if matches else 'does not name any object in the map'
                raise ContentError.at(f'target {name.strip()!r} {where}', recipe, obj,
                                      fix='correct the target_id/target_ids property, or move the target into the room')
            targets.extend(target["id"] for target in matches)
        bounds = [round(obj["x"] - left), round(obj["y"] - top), round(obj["width"]), round(obj["height"])]
        boolean = lambda value: str(value).lower() in ("true", "1")
        flags = int(boolean(props.get("inverted", False))) | (0 if boolean(props.get("handle_available", True)) else 2)
        enabled = int(boolean(props.get('enabled',True)))
        if obj['group'] == 'doors':
            # door.cpp places its 24x72 physics body one source tile right/down.
            bounds = [round(obj['x']-left)+24,round(obj['y']-top)+24,24,72]
            enabled = int(boolean(props.get('open',False)))
        elif obj['group'] == 'destructible_blocking_rects':
            right_aligned = boolean(props.get('right_aligned',False))
            bounds = [round(obj['x']-left)+(102 if right_aligned else 0),round(obj['y']-top),48,96]
            if right_aligned:
                flags |= 8
        elif obj['group'] == 'enemies':
            # cannon_2.lua addShapeRectBevel(1,.75,.05,0,.25), with 48px/metre.
            bounds = [round(obj['x']-left),round(obj['y']-top)+12,48,36]
        elif obj['group'] == 'grab_ropes':
            bounds = [round(obj['x']-left),round(obj['y']+obj['points'][1][1]-top),7,round(-obj['points'][1][1])]
        if obj['name'] in recipe.get('native_inverted_levers', []):
            if obj['group'] != 'levers':
                raise ContentError.at('only levers can invert their native target state', recipe, obj,
                                      fix='remove this object from native_inverted_levers')
            flags |= 4
        value = int(props.get("damage", 100)) if obj["group"] in ("damage_rects", "spike_blocks") else int(props.get("index", 0))
        if obj['group'] == 'doors':
            value = 7 if props.get('item_required',props.get('key','')) == 'red' else 0
        elif obj['group'] == 'destructible_blocking_rects':
            value = int(props.get('max_damage',30))
            if value <= 0:
                raise ContentError.at('durability (max_damage) must be positive', recipe, obj,
                                      fix='set max_damage to 1 or more')
        elif obj['group'] == 'grab_ropes':
            value = int(props.get('segments',7))
        if obj['group']=='wind':
            import math
            dx=float(props.get('direction_x',0));dy=float(props.get('direction_y',0))
            length=math.hypot(dx,dy)
            if 'strength' in props and length:
                dx=dx/length*float(props['strength']);dy=dy/length*float(props['strength'])
            fx,fy=round(dx*16),round(-dy*16)
            if not(-127<=fx<=127 and -127<=fy<=127):raise ContentError.at('wind force exceeds native fixed-step budget', recipe, obj,
                                                                         fix='reduce strength or direction_x/direction_y')
            value=(fx&255)|((fy&255)<<8)
        if not 0 <= value <= 65535:
            raise ContentError.at(f'value {value} exceeds the supported unsigned 16-bit range', recipe, obj,
                                  fix='use a damage/index value between 0 and 65535')
        animated = obj['group'] in ('levers', 'on_off_blocks', 'spike_blocks')
        interval = props.get('mode', 'lever') == 'interval'
        if interval and obj['group'] not in ('on_off_blocks', 'spike_blocks'):
            raise ContentError.at('interval mode is valid only for on_off_blocks and spike_blocks', recipe, obj,
                                  fix='set mode to lever or remove the mode property')
        def ticks(value, scale=1):
            result = round(float(value) * scale * 60)
            if not 0 <= result <= 65534:
                raise ContentError.at(f'timer exceeds the 16-bit tick budget of about 18 minutes (value {value})', recipe, obj,
                                      fix='shorten the time_* or fade_in_duration_s property')
            return result
        timing = [max(1, ticks(props.get('time_on_ms', 4000), .001)),
                  max(1, ticks(props.get('time_off_ms', 3000), .001)),
                  ticks(props.get('time_to_collapse_s', 1)), ticks(props.get('time_to_respawn_s', 4)),
                  ticks(props.get('fade_in_duration_s', 1)),
                  int(animated) | (2 if interval else 0) | (4 if boolean(props.get('collapse_once_touched', False)) else 0)]
        definitions.append({"source_id": obj["id"], "name": obj["name"], "kind": supported[obj["group"]],
                            "bounds": bounds, "timing": timing, "enabled": enabled, "flags": flags,
                            "value": value, "target_first": first, "target_count": len(targets) - first,
                            "visual_status": "static background" if obj["group"] == "checkpoints" else "not implemented"})
        if obj['group'] == 'enemies':
            definitions[-1]['visual_status'] = 'invisible collision body; enemy importer owns artwork'
            definitions[-1]['collision_note'] = 'source 48x36 body bounds; the 8px collision grid conservatively approximates its 2.4px corner bevel'
        elif obj['group'] == 'moveable_objects':
            definitions[-1]['visual_status'] = 'source moveable_box.png 48x72 cell, lip one tile above the body'
            definitions[-1]['simulation_note'] = 'pixel steps through cells, solids and box colliders replace the Box2D body; pushed at half the body speed, no sliding after a push'
        elif obj['group'] == 'box_colliders':
            definitions[-1]['visual_status'] = 'invisible; stops moveable boxes only'
        elif obj['group'] == 'grab_ropes':
            definitions[-1]['visual_status'] = 'native plain rope strips and source player midair pose'
            definitions[-1]['simulation_note'] = 'bounded fixed-point chain constraints replace Box2D joints; automatic airborne grab, swing-only hold, jump release'
    return definitions, targets, unsupported


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=ROOT / 'assets/game/data')
    parser.add_argument("--output", type=Path, default=ROOT / "build/content")
    parser.add_argument("--strict", action="store_true", help="fail while the complete game has unsupported features")
    arguments = parser.parse_args()
    levels, summary = inventory(arguments.data, arguments.output)
    if arguments.strict and not summary["full_game_supported"]:
        raise SystemExit("full-game build is incomplete; see " + str(arguments.output / "summary.json"))
