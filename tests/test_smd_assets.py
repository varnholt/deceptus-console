import sys
import shutil
import subprocess
from pathlib import Path
import unittest
import uuid

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from smd_assets import array as c_array
from smd_assets import packed_tile, palette_word, rgb333, fit_rgb333_palette, sprite_chunks
from smd_opening_assets import packbits,compile_background,viewport_peak,Writer,compile_font
from pixel_font import NativePixelFont

ROOT=Path(__file__).resolve().parents[1]
DATA=ROOT / 'assets/game/data'


def decode_packets(payload,unit_bytes):
    output=bytearray();index=0
    while index<len(payload):
        code=payload[index];index+=1
        if code<128:
            count=(code+1)*unit_bytes;output.extend(payload[index:index+count]);index+=count
        else:
            output.extend(payload[index:index+unit_bytes]*(code-127));index+=unit_bytes
    return bytes(output)


class MegaDriveAssetsTests(unittest.TestCase):
    def test_generated_c_tables_and_blob_declarations_compile_with_engine_types(self):
        compiler = shutil.which('clang')
        if not compiler:
            self.skipTest('clang is required for the generated C contract check')
        folder = ROOT / 'build/tests' / ('smd-c-types-' + uuid.uuid4().hex)
        folder.mkdir(parents=True)
        writer = Writer(folder)
        writer.blob('sample', bytes(32), 32)
        source = folder / 'declarations.c'
        declarations = ['#include "deceptus/types.h"', *writer.externs]
        for kind in ('u8', 'u16', 'u32'):
            declarations.append(c_array('values_' + kind, [0, 1, 255], kind, 8))
        source.write_text('\n'.join(declarations) + '\n')
        subprocess.run(
            [compiler, '-std=c90', '-Wall', '-Wextra', '-Werror', '-fsyntax-only',
             '-I' + str(ROOT / 'include'), str(source)],
            check=True, capture_output=True, text=True,
        )

    def test_palette_is_shared_source_rgb333_medoids_with_bounded_size(self):
        first=Image.new('RGBA',(8,8),(255,0,0,255))
        second=Image.new('RGBA',(8,8),(0,255,0,255))
        palette=fit_rgb333_palette([first,second],14)
        self.assertEqual(palette.shape,(14,3))
        self.assertEqual({tuple(value) for value in palette},{(7,0,0),(0,7,0)})

    def test_sprite_pieces_are_column_major_and_quadrants_are_screen_order(self):
        image=Image.new('RGBA',(16,16))
        image.paste('red',(0,0,8,8));image.paste('green',(8,0,16,8));image.paste('blue',(0,8,8,16))
        palette=fit_rgb333_palette([image]);packed,columns,rows,quads=sprite_chunks(image,palette)
        self.assertEqual((len(packed),columns,rows,quads),(128,1,1,bytes([7])))
        # physical tile order is TL,BL,TR,BR; every visible pixel stays in place.
        for tile,color in enumerate(((7,0,0),(0,0,7),(0,4,0))):
            slot=(packed[tile*32]>>4)-1
            self.assertEqual(tuple(palette[slot]),color)
        self.assertEqual(packed[96:],bytes(32))

    def test_world_native_cells_keep_foreground_alpha_and_two_palette_banks(self):
        image=Image.new('RGB',(16,8),'red');front=Image.new('RGBA',(16,8))
        front.putpixel((0,0),(0,0,255,255))
        result=compile_background(image,front,[])
        self.assertEqual(len(result['palette']),32)
        self.assertNotEqual(int(result['foreground'][0,0,0]),0)
        self.assertEqual(int(result['foreground'][0,1,0]),0)
        self.assertTrue(np.all((result['background'][:,:,1]>>13)<2))
        self.assertTrue(np.all(result['foreground'][:,:,1]&0x8000))
        self.assertEqual(result['preview'].size,image.size)
        self.assertFalse(result['metrics']['dithering'])
        self.assertEqual(viewport_peak(result['background'],result['foreground'])['patterns'],3)

    def test_byte_rle_round_trip_including_literal_and_repeat_boundaries(self):
        for payload in (b'',bytes(range(256)),b'a'*1024,bytes(range(127))+b'x'*3+b'zz'):
            packed=packbits(payload);index=0;decoded=bytearray()
            while index<len(packed):
                code=packed[index];index+=1
                if code<128:
                    decoded.extend(packed[index:index+code+1]);index+=code+1
                else:
                    decoded.extend(bytes([packed[index]])*(code-127));index+=1
            self.assertEqual(bytes(decoded),payload)

    def test_cell_tuple_packets_keep_big_endian_pairs_and_literal_run_boundaries(self):
        cells=[bytes.fromhex('12348000'),bytes.fromhex('00172000')]
        payload=cells[0]*129+b''.join(cells)*72+cells[1]*3
        self.assertEqual(decode_packets(packbits(payload,4),4),payload)
        with self.assertRaises(ValueError):
            packbits(b'odd',4)

    def test_random_access_blocks_have_big_endian_sentinel_offsets(self):
        folder=ROOT/'build/tests'/('smd-'+uuid.uuid4().hex);folder.mkdir(parents=True)
        writer=Writer(folder);payload=bytes(range(32))+bytes(32)+bytes([17])*32
        blob=writer.blob('sample',payload,32)
        offsets=np.frombuffer((folder/'sample_offsets.bin').read_bytes(),dtype='>u4')
        packed=(folder/'sample_rle.bin').read_bytes()
        self.assertEqual(len(offsets),4)
        self.assertEqual(int(offsets[-1]),len(packed))
        for block in range(3):
            self.assertEqual(decode_packets(packed[offsets[block]:offsets[block+1]],1),payload[block*32:(block+1)*32])
        self.assertIn('32,3,1',blob)
        self.assertEqual((folder/'sample.bin').read_bytes(),payload)

    @unittest.skipUnless((DATA/'fonts/deceptum.png').exists(),'original font required')
    def test_font_common_vertical_crop_retains_every_source_ink_pixel(self):
        folder=ROOT/'build/tests'/('smd-font-'+uuid.uuid4().hex);folder.mkdir(parents=True)
        lines,count=compile_font(DATA,Writer(folder));packed=(folder/'smd_font.bin').read_bytes()
        self.assertEqual(count,191)
        self.assertEqual(packed[:32],bytes([0x11])*32)
        source_font=NativePixelFont(DATA/'fonts')
        for code in range(32,127):
            source=source_font.glyph(chr(code));bounds=source.getbbox()
            pixels=[]
            for row in range(2):
                tile=np.frombuffer(packed[(1+(code-32)*2+row)*32:(2+(code-32)*2+row)*32],dtype=np.uint8).reshape(8,4)
                image=np.empty((8,8),dtype=np.uint8);image[:,::2]=tile>>4;image[:,1::2]=tile&15;pixels.append(image)
            actual=np.concatenate(pixels);expected=np.ones((16,8),dtype=np.uint8)
            if bounds:
                alpha=np.asarray(source.getchannel('A'))
                self.assertFalse(np.any(alpha[:3]));self.assertFalse(np.any(alpha[17:]))
                width=bounds[2]-bounds[0];expected[:14,:width][alpha[3:17,bounds[0]:bounds[2]]>=128]=15
            self.assertTrue(np.array_equal(actual,expected),chr(code))

    def test_chunky_tile_keeps_left_pixel_in_high_nibble(self):
        pixels = np.tile(np.arange(8, dtype=np.uint8), (8, 1))
        self.assertEqual(packed_tile(Image.fromarray(pixels)), bytes.fromhex('01234567') * 8)

    def test_palette_encoding_matches_cram_component_bits(self):
        self.assertEqual(palette_word(255, 0, 0), 0x000e)
        self.assertEqual(palette_word(0, 255, 0), 0x00e0)
        self.assertEqual(palette_word(0, 0, 255), 0x0e00)
        self.assertEqual(palette_word(255, 255, 255), 0x0eee)

    def test_colour_conversion_preserves_size_grid_and_alpha(self):
        source = np.array([[[17, 112, 230, 0], [255, 4, 45, 255]]], dtype=np.uint8)
        converted = np.asarray(rgb333(Image.fromarray(source)))
        self.assertEqual(converted.shape, source.shape)
        self.assertTrue(np.array_equal(converted[:, :, 3], source[:, :, 3]))
        self.assertEqual(converted[0, 1].tolist(), [255, 0, 36, 255])


if __name__ == '__main__':
    unittest.main()
