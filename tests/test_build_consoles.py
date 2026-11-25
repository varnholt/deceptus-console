from contextlib import redirect_stderr
import io
import json
from pathlib import Path
import shutil
import sys
import threading
import unittest
from unittest.mock import patch
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import build_consoles

TEMP_ROOT = Path(__file__).resolve().parents[1] / 'build/tests/console-tool'


class ConsoleBuildTests(unittest.TestCase):
    def setUp(self):
        TEMP_ROOT.mkdir(parents=True, exist_ok=True)
        self.root = TEMP_ROOT / uuid.uuid4().hex
        self.root.mkdir()

    def tearDown(self):
        if self.root.resolve().is_relative_to(TEMP_ROOT.resolve()):
            shutil.rmtree(self.root)

    def test_parallel_failure_preserves_both_results(self):
        ready = threading.Barrier(3)

        def build(target, *args):
            ready.wait(timeout=5)
            return {'target': target, 'returncode': int(target == 'snes')}

        root = self.root
        (root / 'build/consoles').mkdir(parents=True)
        with patch.object(build_consoles, 'ROOT', root), \
                patch.object(build_consoles, 'prepare_opening') as prepare, \
                patch.object(build_consoles, 'build_target', side_effect=build), \
                patch.object(sys, 'argv', ['build_consoles.py']):
            self.assertEqual(build_consoles.main(), 1)
        prepare.assert_called_once()
        report = json.loads((root / 'build/consoles/build.json').read_text())
        self.assertEqual(report['targets'], [{'target': 'gba', 'returncode': 0},
                                              {'target': 'smd', 'returncode': 0},
                                              {'target': 'snes', 'returncode': 1}])
        self.assertFalse(report['full_game'])

    def test_failed_preflight_does_not_start_parallel_targets(self):
        with patch.object(build_consoles,'ROOT',self.root), \
                patch.object(build_consoles,'prepare_opening',side_effect=ValueError('original source missing')), \
                patch.object(build_consoles,'build_target') as build, \
                patch.object(sys,'argv',['build_consoles.py']):
            self.assertEqual(build_consoles.main(),1)
            build.assert_not_called()
        report=json.loads((self.root/'build/consoles/build.json').read_text())
        self.assertEqual(len(report['targets']),3)
        self.assertTrue(all(item['phase']=='original-import preflight' and item['returncode']==1 for item in report['targets']))

    def test_failed_build_does_not_report_stale_cartridge(self):
        root = self.root
        (root / 'build/snes').mkdir(parents=True)
        (root / 'build/snes/deceptus.sfc').write_bytes(b'old cartridge')
        with patch.object(build_consoles, 'ROOT', root), \
                patch.object(build_consoles.subprocess, 'run') as run:
            run.return_value.returncode = 1
            report = build_consoles.build_target('snes', root, root, 'clang', 'ld.lld', 'llvm-objcopy')
        self.assertEqual(report['returncode'], 1)
        self.assertNotIn('rom', report)
        self.assertNotIn('rom_sha256', report)


if __name__ == '__main__':
    unittest.main()
