"""Source identities and progression bindings for the opening ruby/sword route."""
from pathlib import Path

# existing flags 1..64 belong to the prison key/lever and notification chain.
GEMS_TAKEN = 128
OWL_EYES_INSERTED = 256
SWORD_TAKEN = 512
DRAWER_OPENED = 1024
HEADTORCH_ACQUIRED = 2048
SINKHOLE_OPEN = 4096
CATACOMBS = 'level-catacombs/catacombs.tmx'
GRAVEYARD = 'level-graveyard/graveyard.tmx'


def extend_rooms(ids, names, spawns, maps):
    rooms = [
        (1110, 'THE SEALED CELLS', [577, 696]),
        (504, 'THE FLOODED CHAMBER', [104, 792]),
        (424, 'THE RIFT ENGINE', [38, 312]),
        (1121, 'THE SYPHON', [64, 416]),
        (1285, 'SCRIPTORIUM', [72, 336]),
        (1282, 'READING ROOM', [136, 336]),
        (12, 'SKULL ROOM', [68, 288]),
        (60, 'CANNON FODDER', [48, 672]),
    ]
    for source_id, title, spawn in rooms:
        ids.append(source_id); names.append(title); spawns.append(spawn); maps.append(CATACOMBS)
    rooms = [
        (22, 'THE GRAVEYARD', [39, 384]),
        (24, 'THE MAUSOLEUM', [28, 384]),
        (18, 'THE OLD GRAVES', [42, 408]),
        (23, 'THE OWL SHRINE', [37, 408]),
        (102, 'THE CRYPT', [960, 264]),
    ]
    for source_id, title, spawn in rooms:
        ids.append(source_id); names.append(title); spawns.append(spawn); maps.append(GRAVEYARD)


def configure_room(index, recipe, data):
    if index == 7:
        recipe['native_links'] = {'sword_cage_01': 'sword_cage_02;sword_cage_03;sword_cage_04'}
        recipe['scenery_variants'] = [
            dict(name='owl_eyes_inserted', image_layers=['owl-eyes'], required_flags=OWL_EYES_INSERTED),
            dict(name='sword', extras=['sword'], forbidden_flags=SWORD_TAKEN),
        ]
        recipe['ring_shaders'] = [dict(source_id=1339,required_flags=8192,size=128,frame_ticks=5)]
    if index == 11:
        recipe['scenery_variants'] = [
            dict(name='locked_box_closed', chests=[dict(name='locked_box', animation='animation_idle_closed')], forbidden_flags=HEADTORCH_ACQUIRED),
            dict(name='locked_box_open', chests=[dict(name='locked_box', animation='animation_idle_open')], required_flags=HEADTORCH_ACQUIRED),
        ]
    if index == 18:
        recipe['scenery_variants'] = [dict(name='drawer_open', image_layers=['drawer_open'], required_flags=DRAWER_OPENED)]
    if index == 20:
        recipe['native_links'] = {'lever_spike_cannon': ';'.join(f'ct-on-off-block-{number:02}' for number in range(1,12))}
        recipe['native_inverted_levers'] = ['lever_spike_cannon']
    if index >= 21:
        # author-sized images are baked losslessly before the target palette conversion.
        # parallax is not simulated; preserve a single authored phase of the original backdrop.
        recipe['static_image_layers'] = ['gv-bg-sky', 'gv-bg-forest', 'gv-bg-trees-back-a',
                                        'gv-bg-trees-mid-a', 'gv-bg-graveyard-mid-a'] if index != 25 else []
        recipe['repeat_static_images'] = True
        recipe['visual_layers'] = ['bg0','bg1','bg2','bg3','bg4','bg5','level-0','decoration-b',
                                   'level','level-2','level-3','decoration-f1','decoration-f2','level_solid_onesided']
        recipe['one_way_layers'] = ['level_solid_onesided']
        if index == 22:
            recipe['scenery_variants'] = [dict(name='sinkhole_ground',tile_layers=['sinkhole_dirt','sinkhole_grass'],forbidden_flags=SINKHOLE_OPEN)]
        if index == 24:
            recipe['scenery_variants'] = [
                dict(name='owl_eyes_present', image_layers=['owl-eyes'], forbidden_flags=GEMS_TAKEN),
                dict(name='owl_eyes_empty', image_layers=['owl-empty'], required_flags=GEMS_TAKEN),
            ]


def extend_actions(actions):
    actions[4] = [dict(action, forbidden_flags=action.get('forbidden_flags',0)|HEADTORCH_ACQUIRED)
                  if action['id'] == 813 else action for action in actions[4]]
    actions[7] = [dict(id=18262, cost_item=3, set_flags=OWL_EYES_INSERTED,
                       forbidden_flags=OWL_EYES_INSERTED, mechanism_id=1386, effect=4),
                  dict(id=1209, forbidden_flags=OWL_EYES_INSERTED),
                  dict(id=418, required_flags=OWL_EYES_INSERTED, forbidden_flags=SWORD_TAKEN,
                       set_flags=SWORD_TAKEN, grant_item=6, effect=3, automatic=1, mechanism_id=1386)]
    actions[11] = [dict(id=18260,bounds_id=1571,cost_item=4,grant_item=5,
                        set_flags=HEADTORCH_ACQUIRED,forbidden_flags=HEADTORCH_ACQUIRED),
                   dict(id=1341,bounds_id=1571,forbidden_flags=HEADTORCH_ACQUIRED)]
    actions.extend([[], [], [], [], [],
                    [dict(id=1696,bounds_id=1299,grant_item=4,set_flags=DRAWER_OPENED,forbidden_flags=DRAWER_OPENED)],
                    [], [],
                    [],
                    [dict(id=77,bounds=[3180,1716,72,12],required_flags=GEMS_TAKEN,
                          forbidden_flags=SINKHOLE_OPEN,set_flags=SINKHOLE_OPEN,mechanism_id=77,effect=2,automatic=90,
                          locks_controls=1)],
                    [],
                    [dict(id=69,bounds_id=70,grant_item=3,set_flags=GEMS_TAKEN,forbidden_flags=GEMS_TAKEN),
                     dict(id=67,bounds_id=70,required_flags=GEMS_TAKEN)],
                    []])
    actions[12] = [dict(id=425,bounds_id=735,cost_item=7,effect=5,mechanism_id=425),
                   dict(id=735,effect=6,mechanism_id=425)]
    actions[18].append(dict(id=1446,effect=6,mechanism_id=1439))
    actions[14] = [dict(id=1192)]
    actions[20] = [dict(id=1585)]


def room_bindings():
    return {4: [(HEADTORCH_ACQUIRED,0,952,0)],
            7: [(OWL_EYES_INSERTED,0,1386,0)],
            21: [(GEMS_TAKEN,0,94,1)],
            22: [(SINKHOLE_OPEN,0,77,0),(GEMS_TAKEN,0,94,1)],
            23: [(GEMS_TAKEN,0,94,1)],
            24: [(GEMS_TAKEN,0,94,1)],
            25: [(SINKHOLE_OPEN,0,77,0)]}


# (source room, destination room, source entrance, destination entrance).
# preserve source entrance rectangles and their explicit arrival locations.
ENTRANCES = [
    (4,13,1368,1369), (13,4,1369,1368),
    (11,14,523,524), (14,11,524,523),
    (14,15,1100,1101), (15,14,1101,1100),
    (15,10,1098,1099), (10,15,1099,1098),
    (14,16,1117,1118), (16,14,1118,1117),
    (17,18,1287,1286), (18,17,1286,1287),
    (10,19,542,541), (19,10,541,542),
    (19,20,544,543), (20,19,543,544),
    (18,20,1284,1283), (20,18,1283,1284),
    (21,22,30,29), (22,21,29,30),
    (22,23,56,55), (23,22,55,56),
    (23,24,60,59), (24,23,59,60),
]


def extra_portals(index, stages, object_sets):
    result = []
    def add(source, destination, bounds, arrival, direction):
        if source != index:
            return
        origin = stages[source]['room_recipe']['crop_pixels']
        target = stages[destination]['room_recipe']['crop_pixels']
        bounds = [round(bounds[0]-origin[0]),round(bounds[1]-origin[1]),round(bounds[2]),round(bounds[3])]
        spawn = [round(arrival[0]-target[0]),(round(arrival[1])+9)//8*8-target[1],1]
        values = [origin[0]-target[0],origin[1]-target[1],destination,direction]+spawn
        result.append('    {{'+','.join(map(str,bounds))+'}, '+','.join(map(str,values))+'},')
    for source,destination,source_id,destination_id in ENTRANCES:
        if source != index:
            continue
        map_name = stages[source]['room_recipe']['map']
        by_id = {obj['id']:obj for obj in object_sets[map_name]}
        entry, target_entry = by_id[source_id], by_id[destination_id]
        direction = 2 if target_entry['x'] > entry['x'] else 1
        add(source,destination,[entry[key] for key in ('x','y','width','height')],
            [target_entry['properties']['start_position_x_px'],target_entry['properties']['start_position_y_px']],direction)
    # the syphon's western side of this opening has no separate enter_area object.
    add(16,17,[5880,2040,48,96],[6000,2128],2)
    add(17,16,[5928,2040,48,96],[5880,2128],1)
    # original level transition, with the source level's spawn and explicit return spawn.
    add(13,21,[696,1632,48,96],[783,1719],2)
    add(21,13,[744,1632,48,96],[697,1719],1)
    # the collapse opens directly above the crypt. the return shaft uses the exact
    # playerCollidesWithRect coordinates and transitionPlayerTo destination in level.lua.
    add(22,25,[3144,1768,144,24],[3216,1800],3)
    add(25,22,[2400,1776,96,72],[2436,1692],4)
    return result

def validate_source(data):
    """Reject source changes that would invalidate these explicit native translations."""
    from content import map_objects
    from content_error import ContentError
    from lua_source import analyse, BooleanCompiler
    data = Path(data)
    _, objects = map_objects(data / CATACOMBS)
    by_name = {(obj['group'],obj['name']):obj for obj in objects if obj['name']}
    expected = {'sword_cage_01':1386,'sword_cage_02':1387,'sword_cage_03':1388,'sword_cage_04':1389,'tunnel_block':952}
    for name,source_id in expected.items():
        obj = by_name['blocking_rects',name]
        if obj['id'] != source_id:
            raise ContentError(f'opening native binding changed: expected object id {source_id}', file=CATACOMBS,
                               layer='blocking_rects', object_id=obj['id'], object_name=name,
                               fix='restore the original object id or update tutorial_progression.py')
    box = by_name['treasure_chests','locked_box']
    chest = box['properties']
    if chest.get('item_required') != 'solar_seal' or chest.get('spawn_extra') != 'headtorch':
        raise ContentError('item_required/spawn_extra require a new native translation', file=CATACOMBS,
                           layer='treasure_chests', object_id=box['id'], object_name='locked_box',
                           fix='restore item_required=solar_seal and spawn_extra=headtorch or extend tutorial_progression.py')
    tree,_ = analyse(data / 'level-catacombs/level.lua')
    compiler = BooleanCompiler(tree,objects)
    expected_constants = {'_shrine_release_delay_s':.45,'_owl_eye_item':'gems',
                          '_owl_eyes_inserted_treasure':'catacombs_owl_eyes_inserted',
                          '_drawer_opened_treasure':'catacombs_drawer_opened'}
    for key,value in expected_constants.items():
        if compiler.constants.get(key) != value:
            raise ContentError(f'source constant {key} = {compiler.constants.get(key)!r} requires a new native translation (expected {value!r})',
                               file='level-catacombs/level.lua', fix='restore the constant or extend tutorial_progression.py')
    if compiler.constants.get('_sword_cage_ids') != list(expected)[:4]:
        raise ContentError('_sword_cage_ids changed', file='level-catacombs/level.lua',
                           fix=f'restore {list(expected)[:4]} or extend tutorial_progression.py')
    # these lines carry the item ownership semantics translated by the actions above.
    source = (data / 'level-catacombs/level.lua').read_text(encoding='utf-8-sig')
    for statement in ('inventoryRemove(_owl_eye_item)', 'inventoryAdd("solar_seal")',
                      'giveWeaponSword()', 'setMechanismEnabled("tunnel_block", torch_is_off, "blocking_rects")'):
        if statement not in source:
            raise ContentError(f'opening callback requires review: {statement} is missing', file='level-catacombs/level.lua',
                               fix='restore the statement or translate the new behaviour in tutorial_progression.py')
    tree,_ = analyse(data / 'level-graveyard/level.lua')
    compiler = BooleanCompiler(tree,[])
    if compiler.constants.get('_owl_eye_item') != 'gems' or compiler.constants.get('_sinkhole_tremble_duration_s') != 1.5:
        raise ContentError('_owl_eye_item or _sinkhole_tremble_duration_s requires a new native translation',
                           file='level-graveyard/level.lua', fix="restore 'gems' and 1.5 or extend tutorial_progression.py")
