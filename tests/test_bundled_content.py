"""The tracked game snapshot must supply the standalone converters' inputs."""
import hashlib
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BundledContentTests(unittest.TestCase):
    def test_snapshot_files_match_recorded_sources(self):
        source = ROOT / 'assets/game'
        manifest = json.loads((source / 'SOURCE.json').read_text())
        self.assertEqual(manifest['revision'], (ROOT / 'SOURCE_REVISION').read_text().strip())
        self.assertIn('data/level-catacombs/catacombs.tmx', manifest['files'])
        self.assertIn('src/game/mechanisms/controllerkeymap.h', manifest['files'])
        self.assertIn('LICENSES/GAME-CONTENT.txt', manifest['files'])
        for relative, expected in manifest['files'].items():
            with self.subTest(source=relative):
                path = source / relative
                self.assertTrue(path.resolve().is_relative_to(source.resolve()))
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), expected)


if __name__ == '__main__':
    unittest.main()
