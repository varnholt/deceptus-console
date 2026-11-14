from pathlib import Path
import json
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from enemies import constants, crop_enemies
ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT.parent / 'deceptus_engine/data'

class EnemyImportTests(unittest.TestCase):
    def test_rat_settings_are_read_from_the_original_lua(self):
        if not DATA.exists(): self.skipTest('original checkout is required')
        values = constants(DATA / 'scripts/enemies/rat.lua')
        self.assertEqual(values['properties']['damage'], 0)
        self.assertEqual(values['properties']['sprite'], 'data/sprites/rat.png')
        self.assertEqual(values['CYCLE_LENGTHS'], [7, 7, 5, 7, 3])
        result = crop_enemies(DATA, json.loads((ROOT / 'assets/rooms/gba-cell.json').read_text()))
        definition = result['definitions'][0]
        self.assertEqual((definition['source_id'], definition['kind']), (111, 'DECEPTUS_RAT'))
        self.assertEqual((definition['speed'], definition['acceleration']), (10, 1))
        self.assertEqual(result['paths'], [[374, 302], [504, 288]])

    def test_arrow_timing_comes_from_source_initialization(self):
        if not DATA.exists(): self.skipTest('original checkout is required')
        result = crop_enemies(DATA, json.loads((ROOT / 'assets/rooms/gba-preview.json').read_text()))
        definition = next(item for item in result['definitions'] if item['source_id']==1059)
        self.assertEqual(definition['source_id'], 1059)
        self.assertEqual(definition['delays'], [60, 75, 90])
        self.assertEqual((definition['period'], definition['speed'], definition['damage']), (180, 96, 60))
        self.assertTrue(any(item['id'] == 1049 and 'leaves loaded room' in item['reason'] for item in result['unsupported']))
        self.assertTrue(any(item['kind'] == 'DECEPTUS_BLOB_2' for item in result['definitions']))

    def test_unsupported_enemies_are_reported(self):
        obj = dict(id=20, group='enemies', x=10, y=10, rotation=0, properties={'script': 'unported.lua'})
        with patch('enemies.map_objects', return_value=(None, [obj])):
            result = crop_enemies(DATA, dict(map='unused', crop_pixels=[0,0,256,224]))
        self.assertFalse(result['definitions'])
        self.assertEqual(result['unsupported'][0]['script'], 'unported.lua')

    def test_cannon_gun_and_timing_are_read_from_source(self):
        if not DATA.exists(): self.skipTest('original checkout is required')
        settings = constants(DATA / 'scripts/enemies/cannon_2.lua')
        self.assertEqual(settings['_weapon'], ['WeaponType.Gun', 1000, 60, 0.0, 0.2])
        self.assertEqual((settings['SPRITE_WIDTH_PX'], settings['SPRITE_HEIGHT_PX']), (144, 72))
        obj = dict(id=493, group='enemies', x=100, y=80, rotation=0,
                   properties={'script': 'cannon_2.lua', 'alignment': 'right',
                               'time_offset_s': 0.5, 'fire_interval_s': 4})
        with patch('enemies.map_objects', return_value=(None, [obj])):
            result = crop_enemies(DATA, dict(map='unused', crop_pixels=[0,0,256,224]))
        self.assertFalse(result['unsupported'])
        cannon = result['definitions'][0]
        self.assertEqual((cannon['kind'], cannon['speed'], cannon['damage']), ('DECEPTUS_CANNON_2', 102, 60))
        self.assertEqual((cannon['alignment'], cannon['period'], cannon['time_offset'], cannon['animation_fps']),
                         (1, 240, 30, 16))
        obj['properties']['alignment'] = 'down'
        with patch('enemies.map_objects', return_value=(None, [obj])):
            with self.assertRaisesRegex(ValueError, 'unsupported gun parameters or alignment'):
                crop_enemies(DATA, dict(map='unused', crop_pixels=[0,0,256,224]))

    def test_sealed_cells_use_real_contour_and_blue_frog_settings(self):
        if not DATA.exists(): self.skipTest('original checkout is required')
        result = crop_enemies(DATA, dict(map='level-catacombs/catacombs.tmx', crop_pixels=[96,1536,648,816]))
        self.assertEqual([(enemy['source_id'], enemy['kind']) for enemy in result['definitions']],
                         [(1396, 'DECEPTUS_ORB'), (1721, 'DECEPTUS_FROG_BLUE')])
        self.assertEqual(result['paths'], [[372,516], [372,420], [228,420], [228,516]])
        orb, frog = result['definitions']
        self.assertEqual((orb['speed'], orb['acceleration'], orb['animation_fps']), (20,123,20))
        self.assertEqual((frog['period'], frog['speed'], frog['delays'], frog['cycles'][:2]),
                         (300,48,[240,1252,320],[10,1]))
        self.assertEqual([enemy['id'] for enemy in result['unsupported']], [1397,1638,1639])
        self.assertTrue(all('no original physics contour' in enemy['reason'] for enemy in result['unsupported']))
        self.assertEqual(result['ignored_room_scripts'][0]['script'], 'bat_guard.lua')
        self.assertIn('layer_level_solid.obj', result['source_hashes'])

if __name__ == '__main__': unittest.main()
