"""SNES original-pixel conversion, palette-role and bank-boundary checks."""
from pathlib import Path
import struct
import sys
import unittest
import uuid
from unittest.mock import patch
from PIL import Image
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from snes_tutorial_assets import BinaryWriter,compile_background,compile_font,sprite_planar,fit_sprite_palette
import snes_tutorial_assets
from pixel_font import NativePixelFont
ROOT=Path(__file__).resolve().parents[1]
DATA=ROOT / 'assets/game/data'


def decode4(payload):
    image=np.zeros((8,8),dtype=np.uint8)
    for y in range(8):
        for x in range(8):
            image[y,x]=sum(((payload[(plane//2)*16+y*2+plane%2]>>(7-x))&1)<<plane for plane in range(4))
    return image


def decode2(payload):
    return np.array([[((payload[y*2]>>(7-x))&1)|(((payload[y*2+1]>>(7-x))&1)<<1) for x in range(8)] for y in range(8)])


class SnesTargetConversionTests(unittest.TestCase):
    def setUp(self):
        self.folder=ROOT/'build/tests'/('snes-'+uuid.uuid4().hex)
        self.folder.mkdir(parents=True)

    def test_valid_original_imports_are_read_without_conversion(self):
        expected=([('source recipe','lossless import')],{'source_sha256':'checked'})
        with patch.object(snes_tutorial_assets,'original_imports',return_value=expected) as validate, \
                patch('tutorial_assets.compile_tutorial') as convert:
            self.assertEqual(snes_tutorial_assets.ensure_original_imports(DATA),expected)
            validate.assert_called_once_with(DATA.resolve())
            convert.assert_not_called()

    def test_missing_or_stale_imports_are_generated_once_and_revalidated(self):
        expected=([],{'source_sha256':'new source'})
        for error in (FileNotFoundError('cold clone'),ValueError('stale source identity')):
            with self.subTest(error=error), \
                    patch.object(snes_tutorial_assets,'original_imports',side_effect=[error,expected]) as validate, \
                    patch('tutorial_assets.compile_tutorial') as convert:
                self.assertEqual(snes_tutorial_assets.ensure_original_imports(DATA),expected)
                convert.assert_called_once_with(DATA.resolve(),reuse_assets=False)
                self.assertEqual(validate.call_count,2)

    def test_parallel_preflight_refreshes_only_common_metadata_when_imports_valid(self):
        with patch.object(snes_tutorial_assets,'original_imports',return_value=([],{})) as validate, \
                patch('tutorial_assets.compile_tutorial') as convert:
            snes_tutorial_assets.ensure_original_imports(DATA,refresh_metadata=True)
            convert.assert_called_once_with(DATA.resolve(),reuse_assets=True)
            self.assertEqual(validate.call_count,2)

    def test_failed_bootstrap_does_not_stamp_or_accept_unvalidated_imports(self):
        with patch.object(snes_tutorial_assets,'original_imports',side_effect=ValueError('missing imports')) as validate, \
                patch('tutorial_assets.compile_tutorial',side_effect=RuntimeError('source import failed')):
            with self.assertRaisesRegex(RuntimeError,'source import failed'):
                snes_tutorial_assets.ensure_original_imports(DATA)
            validate.assert_called_once()

    def test_rom_chunks_never_cross_the_32k_contract(self):
        writer=BinaryWriter(self.folder)
        payload=bytes(range(256))*128+b'last source bytes'
        descriptor=writer.blob('sample',payload)
        self.assertEqual((self.folder/'sample_0.bin').stat().st_size,32768)
        self.assertEqual((self.folder/'sample_1.bin').read_bytes(),b'last source bytes')
        self.assertIn(str(len(payload)),descriptor)
        self.assertIn('sample_0,sample_1',writer.tables[0])

    def test_world_uses_banks_one_through_seven_and_preserves_foreground_alpha(self):
        image=Image.new('RGB',(16,8),'red')
        front=Image.new('RGBA',(16,8));front.putpixel((0,0),(0,0,255,255))
        result=compile_background(image,front,[],self.folder)
        self.assertEqual(len(result['palette']),256)
        self.assertEqual(result['palette'][:32],bytes(32))
        fields=struct.unpack('<4H',result['foreground'])
        self.assertNotEqual(fields[0],0)
        self.assertEqual(fields[2],0)
        self.assertTrue(1<=((fields[1]>>10)&7)<=7)
        tile=decode4(result['patterns'][fields[0]*32:fields[0]*32+32])
        self.assertNotEqual(tile[0,0],0)
        self.assertEqual(np.count_nonzero(tile),1)
        self.assertFalse(result['metrics']['dithering'])
        self.assertFalse(result['metrics']['resampling'])

    def test_sprite_piece_order_is_two_tiles_then_bottom_two_tiles(self):
        image=Image.new('RGBA',(16,16),'red')
        image.paste('green',(8,0,16,8));image.paste('blue',(0,8,8,16));image.paste('yellow',(8,8,16,16))
        palette=fit_sprite_palette([image]);packed,columns,rows=sprite_planar(image,palette)
        self.assertEqual((len(packed),columns,rows),(128,1,1))
        expected=[(31,0,0),(0,16,0),(0,0,31),(31,31,0)]
        for index,color in enumerate(expected):
            tile=decode4(packed[index*32:index*32+32]);slot=int(tile[0,0])-1
            self.assertEqual(tuple(palette[slot]),color)
            self.assertTrue(np.all(tile==tile[0,0]))

    def test_sprite_occupancy_matches_exact_nontransparent_piece_pixels(self):
        image=Image.new('RGBA',(72,72))
        for x,y in ((0,0),(16,31),(71,71),(32,48)):
            image.putpixel((x,y),(255,255,255,255))
        packed,columns,rows=sprite_planar(image,fit_sprite_palette([image]))
        mask=snes_tutorial_assets.sprite_opaque_mask(packed)
        self.assertEqual((columns,rows,len(mask)),(5,5,8))
        expected={0,6,17,24}
        for piece in range(64):
            self.assertEqual(bool(mask[piece//8]&(1<<(piece%8))),piece in expected)
        self.assertEqual(snes_tutorial_assets.sprite_opaque_mask(bytes(128)),bytes(8))
        with self.assertRaises(ValueError):
            snes_tutorial_assets.sprite_opaque_mask(bytes(65*128))

    def test_sprite_quadrants_follow_exact_top_left_right_bottom_left_right_order(self):
        image=Image.new('RGBA',(80,16))
        for piece in range(4):
            image.putpixel((piece*16+(piece%2)*8,(piece//2)*8),(255,255,255,255))
        for x,y in ((64,0),(72,0),(64,8),(72,8)):
            image.putpixel((x,y),(255,255,255,255))
        packed,columns,rows=sprite_planar(image,fit_sprite_palette([image]))
        self.assertEqual((columns,rows),(5,1))
        self.assertEqual(snes_tutorial_assets.sprite_quads(packed),bytes([1,2,4,8,15]))
        self.assertEqual(snes_tutorial_assets.sprite_quads(bytes(128)),bytes([0]))
        with self.assertRaises(ValueError):
            snes_tutorial_assets.sprite_quads(bytes(127))

    @unittest.skipUnless((DATA/'fonts/deceptum.png').exists(),'native source font required')
    def test_native_font_retains_every_bitmap_pixel_with_only_horizontal_trim(self):
        writer=BinaryWriter(self.folder);lines,size=compile_font(DATA,self.folder,writer)
        self.assertLessEqual(size,6144)
        payload=(self.folder/'snes_font.bin').read_bytes()
        self.assertEqual(payload[:16],bytes(16))
        self.assertTrue(np.all(decode2(payload[16:32])==2))
        font=NativePixelFont(DATA/'fonts')
        code=ord('%');source=font.glyph('%');bounds=source.getbbox()
        offset=(2+(code-32)*3)*16
        actual=np.concatenate([decode2(payload[offset+row*16:offset+(row+1)*16]) for row in range(3)])
        expected=Image.new('L',(8,24),2)
        alpha=source.getchannel('A').crop((bounds[0],0,bounds[2],18))
        expected.paste(1,(0,0,bounds[2]-bounds[0],18),alpha)
        self.assertTrue(np.array_equal(actual,np.asarray(expected)))


if __name__=='__main__':unittest.main()
