from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from content import crop_mechanisms


class GrabRopeImportTests(unittest.TestCase):
    def convert(self, **changes):
        obj = dict(id=109, name='mausoleum_rope', group='grab_ropes',
                   x=2652, y=1626, width=0, height=0, rotation=0,
                   shape='polyline', points=[(0, 0), (0, -234)],
                   properties=dict(segments=22, z=24, push_strength=0))
        obj.update(changes)
        with patch('content.map_objects', return_value=(None, [obj])):
            return crop_mechanisms(Path('.'), dict(map='unused', crop_pixels=[2256, 1344, 1536, 720]))

    def test_authored_anchor_length_links_and_identity(self):
        definitions, targets, unsupported = self.convert()
        self.assertFalse(unsupported)
        self.assertFalse(targets)
        self.assertEqual(definitions[0]['source_id'], 109)
        self.assertEqual(definitions[0]['bounds'], [396, 48, 7, 234])
        self.assertEqual(definitions[0]['value'], 22)
        self.assertEqual(definitions[0]['kind'], 'DECEPTUS_MECH_GRAB_ROPE')

    def test_nonported_variants_are_rejected_explicitly(self):
        for changes in (dict(points=[(0, 0), (24, -234)]),
                        dict(properties=dict(segments=25, push_strength=0)),
                        dict(properties=dict(segments=22, push_strength=.02)),
                        dict(properties=dict(segments=22, push_strength=0, sprite=2))):
            definitions, _, unsupported = self.convert(**changes)
            self.assertFalse(definitions)
            self.assertIn('grab rope requires', unsupported[0]['reason'])
