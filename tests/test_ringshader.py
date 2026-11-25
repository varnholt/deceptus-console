"""Source equation and native-resource checks for the visible sword ward."""
from pathlib import Path
import json
import sys
import unittest
from unittest.mock import patch

import numpy as np
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ringshader import heartbeat, bake_ring_shaders, shader_signature, RING_FRAGMENT_SIGNATURE

ROOT=Path(__file__).resolve().parents[1]
DATA=ROOT / 'assets/game/data'


class RingMathTests(unittest.TestCase):
    def test_heartbeat_has_source_double_beat_and_wraps(self):
        self.assertAlmostEqual(heartbeat(0),1.0)
        self.assertAlmostEqual(heartbeat(.16),.7,places=4)
        self.assertAlmostEqual(heartbeat(.99),heartbeat(-.01))
        self.assertLess(heartbeat(.5),.000001)

@unittest.skipUnless((DATA/'shaders/ring.frag').exists(),'original shader data required')
class SourceRingResourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.recipe=dict(map='level-catacombs/catacombs.tmx',crop_pixels=[2472,2736,1032,456],foreground_layers=[],
                        ring_shaders=[dict(source_id=1339,required_flags=8192,size=128,frame_ticks=5)])
        cls.clip=bake_ring_shaders(DATA,cls.recipe)[0]

    def test_native_dimensions_source_radius_and_timing(self):
        clip=self.clip
        self.assertEqual(clip['bounds'],[472,136,128,128])
        self.assertEqual(clip['source_center'],[3012,2940])
        self.assertAlmostEqual(clip['source_resting_radius'],46.666368)
        self.assertEqual((clip['frame_count'],clip['frame_ticks']),(24,5))
        self.assertEqual(clip['required_flags'],8192)
        self.assertEqual(clip['plane'],1)

    def test_pulse_animates_without_cutting_the_visible_band(self):
        self.assertEqual(len({frame.tobytes() for frame in self.clip['frames']}),24)
        for image in self.clip['frames']:
            pixels=np.asarray(image)
            alpha=pixels[:,:,3]
            self.assertGreater(np.count_nonzero(alpha>=128),600)
            edge=max(alpha[0,:].max(),alpha[-1,:].max(),alpha[:,0].max(),alpha[:,-1].max())
            self.assertLess(edge,128)
        # the source first beat expands the ring; it retains its original pixel radius.
        radii=[]
        yy,xx=np.mgrid[0:128,0:128]
        for index in (0,12):
            mask=np.asarray(self.clip['frames'][index])[:,:,3]>=128
            radii.append(float(np.hypot(xx-68,yy-67)[mask].mean()))
        self.assertGreater(radii[0],radii[1]+2)
        self.assertTrue(44<radii[1]<49)

    def test_source_foreground_occludes_ring_without_losing_its_pixels(self):
        foreground=Image.new('RGBA',(128,128),(13,27,41,255))
        with patch('ringshader.TiledMap') as tiled:
            tiled.return_value.render.return_value=foreground
            clip=bake_ring_shaders(DATA,self.recipe)[0]
        for image in clip['frames']:
            self.assertEqual(image.tobytes(),foreground.tobytes())

    def test_unreviewed_fragment_changes_fail_explicitly(self):
        source=(DATA/'shaders/ring.frag').read_text()
        self.assertEqual(shader_signature(source),RING_FRAGMENT_SIGNATURE)
        self.assertNotEqual(shader_signature(source.replace('0.15','0.2')),RING_FRAGMENT_SIGNATURE)
        with patch('ringshader.RING_FRAGMENT_SIGNATURE','different source'):
            with self.assertRaisesRegex(ValueError,'review'):
                bake_ring_shaders(DATA,self.recipe)


if __name__=='__main__':unittest.main()
