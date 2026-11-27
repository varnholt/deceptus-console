"""The original's level music, read from its level scripts without running them.

A level script hands data/scripts/music_zones.lua its configuration:

    music_zones.configure({
       default_track = "data/music/<track>.ogg",
       zones = {{sensor_rect = "<object name>", track = "data/music/<track>.ogg", from_checkpoint = 1}},
    })

level_music() returns that configuration with the tracks as their file stems (the names
tools/music_assets.py maps to songs), the zones' sensor rectangles as map object ids, and the
checkpoints from which a zone's track starts the level as map object ids. A level without a
configure call has no music.
"""
from pathlib import Path
import re

from lua_source import ast, literal, name_of, nodes

CONFIGURE = 'music_zones.configure'
# the original's menu track (deceptus_engine src/game/audio/musicfilenames.cpp).
MENU_TRACK = 'menu_test_track_muffler_callisto'


def _table(node):
    """A Lua table literal with string keys, as a dict; sequential entries as a list."""
    if not isinstance(node, nodes.Table):
        return literal(node)
    if all(field.key is None or isinstance(field.key, nodes.Number) for field in node.fields):
        return [_table(field.value) for field in node.fields]
    return {name_of(field.key): _table(field.value) for field in node.fields}


def _configuration(script):
    tree = ast.parse(Path(script).read_text(encoding='utf-8-sig'))
    for node in ast.walk(tree):
        if isinstance(node, nodes.Call) and name_of(node.func) == CONFIGURE and node.args:
            return _table(node.args[0])
    return None


def _track(path):
    return Path(path).stem


def _object_ids(tmx):
    """Named map objects of a Tiled map: name -> object id."""
    text = Path(tmx).read_text(encoding='utf-8')
    return {name: int(object_id) for object_id, name in re.findall(r'<object id="(\d+)" name="([^"]+)"', text)}


def _checkpoints(tmx, first_index):
    """The checkpoint objects whose index is first_index or later."""
    text = Path(tmx).read_text(encoding='utf-8')
    ids = []
    for group in re.finditer(r'<objectgroup[^>]*name="checkpoints"[^>]*>(.*?)</objectgroup>', text, re.S):
        for match in re.finditer(r'<object id="(\d+)"[^>]*>(.*?)</object>', group.group(1), re.S):
            index = re.search(r'name="index"[^>]*value="(\d+)"', match.group(2))
            if index and int(index.group(1)) >= first_index:
                ids.append(int(match.group(1)))
    return sorted(ids)


def level_music(level_folder, tmx):
    """dict(default_track, zones=[dict(sensor_id, track, checkpoints)]) for a level, or None."""
    configuration = _configuration(Path(level_folder) / 'level.lua')
    if configuration is None:
        return None
    objects = _object_ids(tmx)
    zones = []
    for zone in configuration.get('zones', []):
        if zone['sensor_rect'] not in objects:
            raise ValueError(f'{tmx}: no object named {zone["sensor_rect"]} for a music zone')
        checkpoints = _checkpoints(tmx, int(zone['from_checkpoint'])) if 'from_checkpoint' in zone else []
        zones.append(dict(sensor_id=objects[zone['sensor_rect']], track=_track(zone['track']), checkpoints=checkpoints))
    return dict(default_track=_track(configuration['default_track']), zones=zones)
