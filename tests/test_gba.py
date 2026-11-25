from pathlib import Path
import struct
import json
import sys
import unittest

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from gba_assets import ACTOR_FRAME_COUNT
from palettes import palette_rgb555

ROOT = Path(__file__).resolve().parents[1]
GENERATED = ROOT / 'assets/generated/gba'

def graphics_folder():
    budget = GENERATED / 'budget.json'
    if budget.exists() and json.loads(budget.read_text()).get('stages'):
        return GENERATED / 'area0'
    return GENERATED


class GbaGraphicsTests(unittest.TestCase):

    def test_generated_maps_reconstruct_the_gba_image(self):
        if not (GENERATED / 'budget.json').exists():
            self.skipTest('build the GBA assets to check their hardware maps')
        campaign = json.loads((GENERATED / 'budget.json').read_text()).get('stages')
        folders = [GENERATED / f'area{index}' for index in range(len(campaign))] if campaign else [GENERATED]
        for folder in folders:
            expected = Image.open(folder / 'room-preview.png')
            budget = json.loads((folder / 'budget.json').read_text())
            streamed = budget.get('background_streamed', False)
            if streamed:
                data = (folder / 'stream-cells.bin').read_bytes()
                cells = struct.unpack(f'<{len(data) // 2}H', data)
                foreground_cells = None
                if budget.get('foreground_layered'):
                    data = (folder/'foreground-cells.bin').read_bytes()
                    foreground_cells = struct.unpack(f'<{len(data)//2}H',data)
                patterns = [(folder / 'stream-patterns.bin').read_bytes()]
            else:
                maps = [struct.unpack(f'<{(folder / f"map{plane}.bin").stat().st_size // 2}H', (folder / f'map{plane}.bin').read_bytes()) for plane in range(2)]
                patterns = [(folder / f'background{plane}.bin').read_bytes() for plane in range(2)]
            reconstructed = Image.new('P', expected.size)
            for top in range(0, expected.height, 8):
                for left in range(0, expected.width, 8):
                    column, row = left // 8, top // 8
                    if streamed:
                        offset = (row * (expected.width // 8) + column) * 2
                        entries = [(patterns[0], cells[offset], cells[offset + 1])]
                        if foreground_cells:
                            entries.append((patterns[0], foreground_cells[offset], foreground_cells[offset+1]))
                    else:
                        address = (column // 32) * 1024 + row * 32 + column % 32
                        entries = [(patterns[plane], maps[plane][address] & 1023, maps[plane][address] & 0xfc00) for plane in (1, 0)]
                    for atlas, pattern, attributes in entries:
                        tile = atlas[pattern * 32:pattern * 32 + 32]
                        self.assertEqual(len(tile), 32)
                        for pixel_y in range(8):
                            for pixel_x in range(8):
                                source_x = 7 - pixel_x if attributes & 0x400 else pixel_x
                                source_y = 7 - pixel_y if attributes & 0x800 else pixel_y
                                packed = tile[source_y * 4 + source_x // 2]
                                value = (packed >> ((source_x % 2) * 4)) & 15
                                if value:
                                    reconstructed.putpixel((left + pixel_x, top + pixel_y), value + (attributes >> 12) * 16)
            self.assertEqual(reconstructed.tobytes(), expected.tobytes())
            self.assertEqual(palette_rgb555(expected, 16), (folder / 'background.pal').read_bytes())

    def test_actor_frames_preserve_all_original_rgb555_pixels(self):
        folder = graphics_folder()
        if not (folder / 'actors.bin').exists():
            self.skipTest('build GBA assets to verify its actor frames')
        source = ROOT / ('assets/source/campaign/area0/gba/actors.png' if folder != GENERATED else 'assets/source/gba/actors.png')
        original = Image.open(source).convert('RGBA')
        frames = (folder / 'actors.bin').read_bytes()
        palette = struct.unpack('<256H', (folder / 'actors.pal').read_bytes())
        for frame in range(ACTOR_FRAME_COUNT):
            for piece in range(6):
                for tile_y in range(2):
                    for tile_x in range(2):
                        offset = frame * 1536 + piece * 256 + (tile_y * 2 + tile_x) * 64
                        for pixel_y in range(8):
                            for pixel_x in range(8):
                                value = frames[offset + pixel_y * 8 + pixel_x]
                                left = (frame % 4) * 32 + (piece % 2) * 16 + tile_x * 8 + pixel_x
                                top = (frame // 4) * 48 + (piece // 2) * 16 + tile_y * 8 + pixel_y
                                red, green, blue, alpha = original.getpixel((left, top))
                                if alpha < 128:
                                    self.assertEqual(value, 0)
                                else:
                                    self.assertNotEqual(value, 0)
                                    self.assertEqual(palette[value], (red >> 3) | ((green >> 3) << 5) | ((blue >> 3) << 10))

    def test_gba_header_checksum_and_rom_size(self):
        rom = ROOT / 'build/gba/deceptus.gba'
        if not rom.exists():
            self.skipTest('build the ROM to check its header')
        payload = rom.read_bytes()
        self.assertEqual(payload[0xb2], 0x96)
        self.assertEqual((sum(payload[0xa0:0xbe]) + 0x19) & 255, 0)
        self.assertEqual(len(payload) & (len(payload) - 1), 0)
        self.assertEqual(payload[0xac:0xb0], b'DCPT')
    def test_mechanism_frames_preserve_source_pixels(self):
        source = ROOT / 'assets/game/data/sprites'
        folder = graphics_folder()
        if not (source / 'levers.png').exists() or not (folder / 'mechanisms-frames.json').exists():
            self.skipTest('original assets and a GBA build are required')
        manifest = json.loads((folder / 'mechanisms-frames.json').read_text())
        graphics = (folder / 'mechanisms.bin').read_bytes()
        palettes = (folder / 'mechanisms.pal').read_bytes()
        table = (folder / 'mechanisms.tbl').read_bytes()
        animations = json.loads((source / 'animations.json').read_text())
        from gba_assets import SWORD_CLIPS
        sword_count = sum(animations[name + '_' + side]['sprite_count']
                          for side in ('r', 'l') for name in SWORD_CLIPS)
        self.assertEqual(len(manifest), 494 + sword_count + 86 + 4 + 114 + 100 + 3 + 8 + 63 + 8 + 9 + 6)
        atlases = {}
        # retain exact source-colour checks for all resources except the
        # explicitly colour-reduced sword range, which has its own checks.
        for resource, frame in enumerate(manifest):
            if 494 <= resource < 494 + sword_count:
                continue
            if frame['source'] not in atlases:
                atlases[frame['source']] = Image.open((folder if frame.get('source_generated') else source) / frame['source']).convert('RGBA')
            x, y, width, height = frame['source_rect']
            padded = Image.new('RGBA', frame['size'])
            image = atlases[frame['source']].crop((x, y, x + width, y + height))
            if frame.get('rotation'):
                image = image.transpose({90: Image.Transpose.ROTATE_90, 180: Image.Transpose.ROTATE_180, 270: Image.Transpose.ROTATE_270}[frame['rotation']])
            padded.alpha_composite(image)
            count, first = frame['palette_count'], frame['palette_first']
            self.assertEqual(first % 2, 0, 'palette DMA destination must be 32-bit aligned')
            self.assertEqual(frame['palette_offset'] % 4, 0, 'palette DMA source must be 32-bit aligned')
            self.assertEqual(count % 2, 0, 'palette DMA transfer must contain whole 32-bit words')
            palette = struct.unpack_from(f'<{count}H', palettes, frame['palette_offset'])
            descriptor = struct.unpack_from('<IIH4B2x', table, resource * 16)
            self.assertEqual(descriptor, (frame['offset'], frame['palette_offset'], frame['bytes'],
                                         padded.width // 16, padded.height // 16, first, count))
            reserved = struct.unpack_from('<H', table, resource * 16 + 14)[0]
            if 'logical_source_rect' in frame:
                lx, ly, lw, lh = frame['logical_source_rect']
                dx, dy = frame['trim_offset']
                self.assertEqual(reserved, dx | (dy << 8))
                self.assertEqual([x, y], [lx + dx, ly + dy])
                logical = atlases[frame['source']].crop((lx, ly, lx + lw, ly + lh))
                opaque_bounds = logical.getchannel('A').point(lambda alpha: 255 if alpha >= 128 else 0).getbbox()
                self.assertEqual((dx, dy, dx + width, dy + height), opaque_bounds or (0, 0, 1, 1))
                self.assertFalse(frame['resampling'])
                self.assertFalse(frame['dithering'])
            else:
                self.assertEqual(reserved, 0)
            offset = frame['offset']
            for top in range(0, padded.height, 16):
                for left in range(0, padded.width, 16):
                    for tile_y in range(2):
                        for tile_x in range(2):
                            for pixel_y in range(8):
                                for pixel_x in range(8):
                                    red, green, blue, alpha = padded.getpixel((left + tile_x * 8 + pixel_x, top + tile_y * 8 + pixel_y))
                                    index = graphics[offset]
                                    offset += 1
                                    if alpha >= 128:
                                        self.assertGreaterEqual(index, first)
                                        self.assertLess(index, first + count)
                                        self.assertEqual(palette[index - first], (red >> 3) | ((green >> 3) << 5) | ((blue >> 3) << 10))
                                    else:
                                        self.assertEqual(index, 0)
            self.assertEqual(offset, frame['offset'] + frame['bytes'])

    def test_sword_frames_preserve_source_grid_with_bounded_palette_error(self):
        source = ROOT / 'assets/game/data/sprites'
        folder = graphics_folder()
        if not (source / 'player.png').exists() or not (folder / 'mechanisms-frames.json').exists():
            self.skipTest('original assets and a GBA build are required')
        manifest = json.loads((folder / 'mechanisms-frames.json').read_text())
        graphics = (folder / 'mechanisms.bin').read_bytes()
        palettes = (folder / 'mechanisms.pal').read_bytes()
        table = (folder / 'mechanisms.tbl').read_bytes()
        animations = json.loads((source / 'animations.json').read_text())
        from gba_assets import SWORD_CLIPS
        expected = [(name + '_' + side, index)
                    for side in ('r', 'l') for name in SWORD_CLIPS
                    for index in range(animations[name + '_' + side]['sprite_count'])]
        self.assertEqual(len(manifest[494:638]), len(expected))
        atlas = Image.open(source / 'player.png').convert('RGBA')
        error_sum, pixel_count = 0, 0
        for resource, (frame, (name, step)) in enumerate(zip(manifest[494:638], expected), start=494):
            animation = animations[name]
            width, height = animation['frame_size']
            x, y = animation['frame_offset']
            x += step * width
            self.assertEqual(frame['animation'], name)
            self.assertEqual(frame['source_rect'], [x, y, width, height])
            self.assertEqual(frame['size'], [(width + 15) // 16 * 16, (height + 15) // 16 * 16])
            self.assertEqual((frame['palette_first'], frame['palette_count']), (2, 32))
            self.assertEqual(frame['palette_first'] % 2, 0)
            self.assertEqual(frame['palette_offset'] % 4, 0)
            self.assertEqual(frame['palette_count'] % 2, 0)
            # sword replaces the normal player, but can share a view with every
            # imported mechanism without overlapping its palette reservation.
            for other in manifest[:494]:
                self.assertGreaterEqual(other['palette_first'], 34)
            self.assertTrue(frame['palette_reduced'])
            self.assertFalse(frame['dithering'])
            self.assertFalse(frame['resampling'])
            padded = Image.new('RGBA', frame['size'])
            padded.alpha_composite(atlas.crop((x, y, x + width, y + height)))
            pixels = np.asarray(padded)
            opaque = pixels[:, :, 3] >= 128
            palette = np.array(struct.unpack_from('<32H', palettes, frame['palette_offset']), dtype=np.int16)
            colours = np.stack((palette & 31, (palette >> 5) & 31, palette >> 10), axis=1)
            payload = graphics[frame['offset']:frame['offset'] + frame['bytes']]
            indices = np.frombuffer(payload, dtype=np.uint8).reshape(padded.height // 16, padded.width // 16,
                                                                    2, 2, 8, 8)
            indices = indices.transpose(0, 2, 4, 1, 3, 5).reshape(padded.height, padded.width)
            self.assertTrue(np.all(indices[~opaque] == 0), name)
            self.assertTrue(np.all((indices[opaque] >= 2) & (indices[opaque] < 34)), name)
            source_colours = (pixels[:, :, :3][opaque] >> 3).astype(np.int16)
            output_colours = colours[indices[opaque] - 2]
            errors = ((source_colours - output_colours) ** 2).sum(axis=1)
            # each opaque source pixel must retain its position and choose the
            # nearest reserved colour: this also rejects dithering and resampling.
            nearest = ((source_colours[:, None, :] - colours[None, :, :]) ** 2).sum(axis=2).min(axis=1)
            self.assertTrue(np.array_equal(errors, nearest), name)
            descriptor = struct.unpack_from('<IIH4B2x', table, resource * 16)
            self.assertEqual(descriptor, (frame['offset'], frame['palette_offset'], frame['bytes'],
                                         padded.width // 16, padded.height // 16, 2, 32))
            error_sum += int(errors.sum())
            pixel_count += len(errors)
        # reviewed 32-colour source conversion measures 2.29 RGB555 units per channel.
        self.assertLessEqual(error_sum / (pixel_count * 3), 2.5)



if __name__ == '__main__':
    unittest.main()
