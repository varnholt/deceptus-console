"""Original helmet-eye attachment and bounded headtorch resource checks."""
from pathlib import Path
import json
import sys
import unittest

from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from headtorch_assets import compile_headtorch, helmet_pose
from gba_assets import SWORD_CLIPS

ROOT=Path(__file__).resolve().parents[1]
DATA=ROOT.parent/'deceptus_engine/data'


class HeadtorchOffsetTests(unittest.TestCase):
    def test_original_last_eye_sample_is_clamped_and_cycle_origin_is_subtracted(self):
        animations={'pose':{'origin':[60,48]}}
        eyes={'pose':{'eye_positions_x':[63,65],'eye_positions_y':[16,17]}}
        self.assertEqual(helmet_pose(animations,eyes,'pose',99,0),[-9,-45,0,1])
        self.assertEqual(helmet_pose(animations,eyes,None,0,1),[0,0,1,0])


@unittest.skipUnless((DATA/'sprites/eye_positions.json').exists(),'original headtorch/player data required')
class SourceHeadtorchResourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=ROOT/'build/tests/headtorch-resource'
        cls.report=compile_headtorch(DATA,cls.folder,SWORD_CLIPS)
        cls.eyes=json.loads((DATA/'sprites/eye_positions.json').read_text())
        cls.animations=json.loads((DATA/'sprites/animations.json').read_text())

    def test_normal_and_swim_follow_every_displayed_source_head(self):
        report=self.report
        self.assertEqual([len(side) for side in report['normal']],[26,26])
        self.assertEqual(report['normal'][0][0],[-11,-46,0,1])
        self.assertEqual(report['normal'][1][0],[-13,-46,1,1])
        self.assertEqual(report['normal'][0][18][3],0)
        self.assertEqual(len(report['swim']),26)
        self.assertTrue(all(pose[3] for pose in report['swim']))
        self.assertNotEqual(report['normal'][0][0][1],report['normal'][0][2][1])

    def test_sword_poses_use_exact_animation_eye_and_origin_metadata(self):
        self.assertEqual(len(self.report['sword']),144)
        for pose,source in zip(self.report['sword'],self.report['sword_source_frames']):
            name=source['animation'];index=source['source_frame'];side=pose[2]
            eyes=self.eyes[name];index=min(index,len(eyes['eye_positions_x'])-1)
            origin=self.animations[name]['origin']
            self.assertEqual(pose[:2],[round(eyes['eye_positions_x'][index]-origin[0]+(-14 if side==0 else -9)),
                                      round(eyes['eye_positions_y'][index]-origin[1]-14)])

    def test_helmet_stays_native_size_and_beam_is_explicit_bounded_effect(self):
        self.assertEqual(self.report['helmet_source_rects'],[[0,1776,24,24],[24,1776,24,24]])
        self.assertEqual(self.report['lamp_offsets'],[[13,6],[10,6]])
        self.assertEqual(self.report['configured_beam_size'],[256,128])
        self.assertEqual(self.report['configured_color'],[255,200,100])
        image=Image.open(self.folder/'headtorch-beam.png').convert('RGBA')
        self.assertEqual(image.size,(64,32))
        colors=set(image.getdata())
        self.assertLessEqual(len(colors),16)
        self.assertTrue(any(pixel[3]==0 for pixel in colors))
        self.assertTrue(any(pixel[3]==255 for pixel in colors))
        self.assertTrue(all(pixel[0]>=pixel[1]>=pixel[2] for pixel in colors))


if __name__=='__main__':unittest.main()
