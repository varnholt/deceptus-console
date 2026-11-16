"""Native-pixel rope strand and original hanging-pose resource checks."""
from pathlib import Path
import json
import sys
import unittest
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from rope_assets import compile_rope
from headtorch_assets import compile_headtorch

ROOT=Path(__file__).resolve().parents[1]
DATA=ROOT / 'assets/game/data'


@unittest.skipUnless((DATA/'sprites/ropes.png').exists(),'original rope artwork required')
class SourceRopeAssetsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=ROOT/'build/tests/rope-artwork'
        cls.report=compile_rope(DATA,cls.folder)
        cls.headtorch=compile_headtorch(DATA,cls.folder)

    def test_slices_copy_original_pixels_with_only_transparent_rotation_padding(self):
        atlas=Image.open(self.folder/'rope-segments.png').convert('RGBA')
        original=Image.open(DATA/'sprites/ropes.png').convert('RGBA')
        self.assertEqual(atlas.size,(144,16))
        for index,spec in enumerate(self.report['slices']):
            x,y,w,h=spec['source_rect'];px,py=spec['placement']
            frame=atlas.crop((index*16,0,index*16+16,16))
            self.assertEqual(frame.crop((px,py,px+w,py+h)).tobytes(),original.crop((x,y,x+w,y+h)).tobytes())
            for row in range(16):
                for column in range(16):
                    if not(px<=column<px+w and py<=row<py+h):
                        self.assertEqual(frame.getpixel((column,row))[3],0)
        self.assertFalse(self.report['resampling'])
        self.assertFalse(self.report['dithering'])

    def test_native_width_single_knot_and_tail_heights_cover_original_rope(self):
        slices=self.report['slices']
        self.assertEqual(slices[0]['source_rect'],[0,0,7,16])
        self.assertEqual([piece['height'] for piece in slices],[16,1,2,3,4,5,6,7,8])
        self.assertTrue(all(piece['source_rect'][1]==16 for piece in slices[1:]))
        # the authored234px rope is covered by one knot,27full native strips and2tail pixels.
        self.assertEqual(slices[0]['height']+27*slices[8]['height']+slices[2]['height'],234)
        self.assertLessEqual(1+27+1,32)

    def test_hanging_poses_preserve_source_rectangles_origins_and_cumulative_timing(self):
        frames=self.report['player_frames']
        self.assertEqual(len(frames),6)
        self.assertEqual([frame['duration_ticks'] for frame in frames],[4,4,3,4,4,3])
        for index,frame in enumerate(frames):
            self.assertEqual(frame['source_rect'],[144+(index%3)*72,288+(index//3)*48,72,48])
            self.assertEqual(frame['origin'],[36,48])
            self.assertEqual(frame['animation'],'player_jump_midair_'+('r' if index<3 else 'l'))

    def test_headtorch_uses_the_original_hanging_eye_positions(self):
        poses=self.headtorch['rope']
        self.assertEqual(poses,[[-9,-51,0,1],[-9,-50,0,1],[-9,-50,0,1],
                                [-15,-51,1,1],[-15,-50,1,1],[-15,-50,1,1]])
        self.assertIn('gba_headtorch_rope[6]',(self.folder/'gba_headtorch_resources.h').read_text())


if __name__=='__main__':unittest.main()
