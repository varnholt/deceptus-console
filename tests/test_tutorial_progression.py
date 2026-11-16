"""Opening progression bindings must remain compatible with the original maps/scripts."""
from pathlib import Path
import sys
import unittest
import json
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from content import map_objects, crop_mechanisms
from tutorial_progression import (CATACOMBS, GRAVEYARD, ENTRANCES, extend_rooms,
    extend_actions, configure_room, room_bindings, validate_source, extra_portals,
    GEMS_TAKEN, OWL_EYES_INSERTED, SWORD_TAKEN, DRAWER_OPENED, HEADTORCH_ACQUIRED, SINKHOLE_OPEN)
from tutorial_ui import TutorialUI
from campaign_assets import validate_room_assets, ROOM_OUTPUTS, asset_identity

DATA = Path(__file__).resolve().parents[1] / 'assets/game/data'


@unittest.skipUnless((DATA / CATACOMBS).exists(), 'original game data required')
class TutorialProgressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.objects = {name: map_objects(DATA / name)[1] for name in (CATACOMBS, GRAVEYARD)}
        cls.maps = {name:{obj['id']:obj for obj in objects} for name,objects in cls.objects.items()}
        ids,names,spawns,maps=[],[],[],[]
        extend_rooms(ids,names,spawns,maps)
        cls.rooms = {index:(cls.maps[map_name][source_id],map_name)
                     for index,source_id,map_name in zip(range(13,26),ids,maps)}

    def test_native_rules_validate_current_source_callbacks_and_cages(self):
        validate_source(DATA)
        room = {'crop_pixels':[2472,2736,1032,456]}
        configure_room(7,room,DATA)
        self.assertEqual(room['native_links']['sword_cage_01'].split(';'),
                         ['sword_cage_02','sword_cage_03','sword_cage_04'])

    def test_cannon_source_lever_controls_all_eleven_opposite_state_blocks(self):
        room,_=self.rooms[20]
        recipe=dict(map=CATACOMBS,crop_pixels=[round(room[key]) for key in ('x','y','width','height')])
        configure_room(20,recipe,DATA)
        definitions,targets,_=crop_mechanisms(DATA,recipe)
        lever=next(value for value in definitions if value['source_id']==1585)
        blocks=[value for value in definitions if value['name'].startswith('ct-on-off-block-')]
        self.assertFalse(lever['enabled'])
        self.assertEqual(lever['flags'] & 4,4)
        self.assertEqual(lever['target_count'],11)
        self.assertEqual(set(targets),{value['source_id'] for value in blocks})
        self.assertTrue(all(value['enabled'] for value in blocks))

    def test_all_added_rooms_keep_native_pixel_grid_and_local_coordinate_budget(self):
        for room,map_name in self.rooms.values():
            for field in ('x','y','width','height'):
                self.assertEqual(int(room[field])%8,0,(map_name,room['id'],field))
            self.assertLessEqual(room['width'],2048)
            self.assertLessEqual(room['height'],2048)

    def test_source_entrances_keep_explicit_authored_arrivals(self):
        # all extra horizontal links use an actual paired entrance in the same map.
        for source,destination,source_id,destination_id in ENTRANCES:
            map_name = GRAVEYARD if source >= 21 else CATACOMBS
            entry,target = self.maps[map_name][source_id],self.maps[map_name][destination_id]
            self.assertEqual(entry['group'],'rooms')
            self.assertEqual(target['group'],'rooms')
            self.assertLessEqual(abs(entry['x']-target['x']),49)
            self.assertEqual(entry['y'],target['y'])
            self.assertIn('start_position_x_px',target['properties'])
            if destination in self.rooms:
                room,_ = self.rooms[destination]
                self.assertTrue(room['x'] <= target['properties']['start_position_x_px'] < room['x']+room['width'])
                self.assertTrue(room['y'] <= target['properties']['start_position_y_px'] < room['y']+room['height'])

    def test_crypt_shaft_callback_is_automatic_and_uses_original_destination(self):
        import re
        stages=[{'room_recipe':{'crop_pixels':[0,0,8,8]}} for _ in range(26)]
        for index,(room,_) in self.rooms.items():
            stages[index]['room_recipe']['crop_pixels']=[round(room[key]) for key in ('x','y','width','height')]
        portals=extra_portals(25,stages,self.objects)
        self.assertEqual(len(portals),1)
        fields=[int(value) for value in re.findall(r'-?\d+',portals[0])]
        self.assertEqual(fields[:4],[144,0,96,72])
        self.assertEqual(fields[6:8],[22,4])
        self.assertEqual(fields[8],2436-2256)
        source=(DATA/'level-graveyard/level.lua').read_text(encoding='utf-8-sig')
        self.assertIn('transitionPlayerTo(101.5 * 24, 70.5 * 24)',source)

    def test_item_chain_spends_owned_items_once_and_preserves_taken_rubies(self):
        actions=[[] for _ in range(13)]
        extend_actions(actions)
        self.assertEqual(len(actions),26)
        drawer=actions[18][0];lamp=actions[11][0];rubies=actions[24][0];shrine=actions[7][0];sword=actions[7][2]
        self.assertEqual((drawer['grant_item'],drawer['set_flags']),(4,DRAWER_OPENED))
        self.assertEqual((lamp['cost_item'],lamp['grant_item'],lamp['set_flags']),(4,5,HEADTORCH_ACQUIRED))
        self.assertEqual((rubies['grant_item'],rubies['forbidden_flags']),(3,GEMS_TAKEN))
        self.assertEqual((shrine['cost_item'],shrine['set_flags'],shrine['effect']),(3,OWL_EYES_INSERTED,4))
        self.assertEqual((sword['required_flags'],sword['set_flags'],sword['effect']),(OWL_EYES_INSERTED,SWORD_TAKEN,3))
        self.assertEqual(room_bindings()[4],[(HEADTORCH_ACQUIRED,0,952,0)])
        self.assertIn((SINKHOLE_OPEN,0,77,0),room_bindings()[22])
        self.assertEqual(room_bindings()[25],[(SINKHOLE_OPEN,0,77,0)])
        # consuming the rubies cannot make the graveyard statue refill on return.
        self.assertEqual(rubies['forbidden_flags'],GEMS_TAKEN)
        self.assertNotIn('clear_flags',shrine)

    def test_real_mines_door_and_reading_planks_keep_source_blocker_references(self):
        actions=[[] for _ in range(13)]
        extend_actions(actions)
        door,locked=actions[12]
        self.assertEqual((door['id'],door['mechanism_id'],door['cost_item'],door['effect']),(425,425,7,5))
        self.assertEqual((locked['id'],locked['mechanism_id'],locked['effect']),(735,425,6))
        wood=next(action for action in actions[18] if action['id']==1446)
        self.assertEqual((wood['mechanism_id'],wood['effect']),(1439,6))
        source=self.maps[CATACOMBS][425]
        self.assertEqual(source['properties']['item_required'],'red')
        self.assertFalse(any(action.get('grant_item')==7 for group in actions for action in group))

    def test_all_inventory_items_use_source_icons_and_fit_native_ui_budget(self):
        ui=TutorialUI(DATA)
        for item in range(8):
            resource=ui.menu('inventory',item)
            self.assertLessEqual(ui.resources[resource][3],192)


class ConvertedRoomReuseTests(unittest.TestCase):
    def setUp(self):
        self.folder=Path(__file__).resolve().parents[1]/'build'/('test-room-reuse-'+uuid.uuid4().hex)
        self.folder.mkdir(parents=True)

    def tearDown(self):
        for path in self.folder.iterdir():
            path.unlink()
        self.folder.rmdir()

    def test_arrow_source_changes_invalidate_cached_room_identity(self):
        weapons=self.folder/'weapons'
        weapons.mkdir()
        arrow=weapons/'arrow.png'
        try:
            arrow.write_bytes(b'original pixels')
            before=asset_identity(self.folder)
            arrow.write_bytes(b'changed pixels')
            after=asset_identity(self.folder)
            self.assertNotEqual(before['source_sha256'],after['source_sha256'])
            self.assertEqual(before['converters_sha256'],after['converters_sha256'])
        finally:
            arrow.unlink()
            weapons.rmdir()

    def test_reuse_rejects_changed_recipe_source_converter_and_missing_outputs(self):
        folder=self.folder
        expected=dict(source_sha256='source',converters_sha256='converter',recipe={'spawn':[16,24]})
        with self.assertRaisesRegex(ValueError,'full conversion'):
            validate_room_assets(folder,expected)
        (folder/'asset-inputs.json').write_text(json.dumps(expected))
        for name in ROOM_OUTPUTS:
            (folder/name).write_bytes(b'converted')
        validate_room_assets(folder,expected)
        for change in (dict(source_sha256='new source'),dict(converters_sha256='new converter'),dict(recipe={'spawn':[32,24]})):
            with self.assertRaisesRegex(ValueError,'input changes'):
                validate_room_assets(folder,dict(expected,**change))
        (folder/'stream-patterns.bin').unlink()
        with self.assertRaisesRegex(ValueError,'missing converted asset'):
            validate_room_assets(folder,expected)


if __name__ == '__main__':
    unittest.main()
