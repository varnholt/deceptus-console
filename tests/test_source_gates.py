from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from content import crop_mechanisms


class SourceGateTests(unittest.TestCase):
    def convert(self, group, properties, **geometry):
        obj = dict(id=425, name='', group=group, x=888, y=3024, width=96, height=144,
                   rotation=0, shape='rectangle', properties=properties)
        obj.update(geometry)
        with patch('content.map_objects', return_value=(None, [obj])):
            return crop_mechanisms(Path('.'), dict(map='unused', crop_pixels=[912, 2832, 720, 384]))

    def test_iron_door_preserves_source_physics_offset_and_key_identity(self):
        definitions, _, unsupported = self.convert('doors', dict(texture='data/sprites/iron-door.png',
            animation_open='iron_door_open', animation_close='iron_door_close', item_required='red'))
        self.assertFalse(unsupported)
        self.assertEqual(definitions[0]['bounds'], [0, 216, 24, 72])
        self.assertEqual(definitions[0]['value'], 7)
        self.assertEqual(definitions[0]['enabled'], 0)

    def test_right_aligned_planks_use_the_actual_collider_not_sprite_width(self):
        obj = dict(id=1439, name='door_left', group='destructible_blocking_rects', x=6597, y=2035,
                   width=150, height=96, rotation=0, shape='rectangle', properties=dict(right_aligned=True))
        with patch('content.map_objects', return_value=(None, [obj])):
            definitions, _, unsupported = crop_mechanisms(Path('.'), dict(map='unused', crop_pixels=[6624,1800,672,408]))
        self.assertFalse(unsupported)
        self.assertEqual(definitions[0]['bounds'], [75,235,48,96])
        self.assertEqual(definitions[0]['flags'], 8)
        self.assertEqual(definitions[0]['value'], 30)

    def test_unknown_door_and_plank_variants_remain_explicit(self):
        definitions, _, unsupported = self.convert('doors', dict(texture='different.png'))
        self.assertFalse(definitions)
        self.assertIn('only the source', unsupported[0]['reason'])
        definitions, _, unsupported = self.convert('destructible_blocking_rects', dict(frame_count=99))
        self.assertFalse(definitions)
        self.assertIn('only the source', unsupported[0]['reason'])

    def test_cannon_collision_uses_source_shape_offset_and_remains_invisible(self):
        obj = dict(id=493, name='cannon', group='enemies', x=7320, y=2256, width=0, height=0,
                   rotation=0, shape='rectangle', properties=dict(script='cannon_2.lua',alignment='right'))
        values = dict(mFireInterval=3,_weapon=['WeaponType.Gun',1000,60,0.0,0.2])
        with patch('content.map_objects',return_value=(None,[obj])), patch('enemies.constants',return_value=values):
            definitions, _, unsupported = crop_mechanisms(Path('.'),dict(map='unused',crop_pixels=[7296,1848,888,744]))
        self.assertFalse(unsupported)
        self.assertEqual(definitions[0]['kind'],'DECEPTUS_MECH_CANNON_BODY')
        self.assertEqual(definitions[0]['bounds'],[24,420,48,36])
        self.assertIn('invisible',definitions[0]['visual_status'])
        self.assertIn('2.4px',definitions[0]['collision_note'])
