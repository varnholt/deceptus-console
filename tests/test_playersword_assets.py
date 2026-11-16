import json
from pathlib import Path
import shutil
import sys
import unittest
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from gba_assets import SWORD_CLIPS, sword_frames

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / 'assets/game/data'


class PlayerSwordAssetsTests(unittest.TestCase):
    def test_source_frames_origins_and_authored_durations(self):
        if not (DATA / 'sprites/animations.json').exists():
            self.skipTest('authoritative animation source unavailable')
        parent = ROOT / 'build/tests/sword-metadata'
        parent.mkdir(parents=True, exist_ok=True)
        output = parent / uuid.uuid4().hex
        output.mkdir()
        try:
            frames = sword_frames(DATA, output)
            source = json.loads((DATA / 'sprites/animations.json').read_text())
            index = 0
            for side in ('r', 'l'):
                for clip in SWORD_CLIPS:
                    animation = source[clip + '_' + side]
                    count = animation['sprite_count']
                    selected = frames[index:index + count]
                    width, height = animation['frame_size']
                    x, y = animation['frame_offset']
                    self.assertEqual([item['source_rect'] for item in selected],
                                     [[x + offset * width, y, width, height] for offset in range(count)])
                    self.assertEqual(sum(item['duration'] for item in selected),
                                     (sum(animation['frame_durations']) * 60 + 999) // 1000)
                    index += count
            self.assertEqual(len(frames), index)
            self.assertLessEqual(len(frames), 255)
            header = (output / 'gba_sword_resources.h').read_text()
            # standing attacks retain asymmetric authored origins in each direction.
            self.assertIn(', 12, 46, 72}', header)
            self.assertIn(', 12, 100, 72}', header)
        finally:
            if output.resolve().is_relative_to(parent.resolve()):
                shutil.rmtree(output)


if __name__ == '__main__':
    unittest.main()
