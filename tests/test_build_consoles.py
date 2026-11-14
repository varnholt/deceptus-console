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
import build_gba

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
        ready = threading.Barrier(2)

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
                                              {'target': 'snes', 'returncode': 1}])
        self.assertFalse(report['full_game'])

    def test_smd_rejected_before_any_build(self):
        with patch.object(sys, 'argv', ['build_consoles.py', '--targets', 'gba', 'smd']), \
                patch.object(build_consoles, 'build_target') as build, redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as failure:
                build_consoles.main()
            self.assertEqual(failure.exception.code, 2)
            build.assert_not_called()

    def test_failed_preflight_does_not_start_parallel_targets(self):
        with patch.object(build_consoles,'ROOT',self.root), \
                patch.object(build_consoles,'prepare_opening',side_effect=ValueError('original source missing')), \
                patch.object(build_consoles,'build_target') as build, \
                patch.object(sys,'argv',['build_consoles.py']):
            self.assertEqual(build_consoles.main(),1)
            build.assert_not_called()
        report=json.loads((self.root/'build/consoles/build.json').read_text())
        self.assertEqual(len(report['targets']),2)
        self.assertTrue(all(item['phase']=='original-import preflight' and item['returncode']==1 for item in report['targets']))

    def test_preflight_finishes_before_either_parallel_target(self):
        ready=[]
        def build(target,*args):
            self.assertEqual(ready,['imports validated'])
            return {'target':target,'returncode':0}
        with patch.object(build_consoles,'ROOT',self.root), \
                patch.object(build_consoles,'prepare_opening',side_effect=lambda data:ready.append('imports validated')), \
                patch.object(build_consoles,'build_target',side_effect=build), \
                patch.object(sys,'argv',['build_consoles.py']):
            self.assertEqual(build_consoles.main(),0)

    def test_gba_parallel_build_reuses_prepared_imports(self):
        with patch.object(build_consoles,'ROOT',self.root), \
                patch.object(build_consoles.subprocess,'run') as run:
            run.return_value.returncode=1
            build_consoles.build_target('gba',self.root,self.root,'clang','ld.lld','llvm-objcopy')
        self.assertIn('--prepared-room-assets',run.call_args.args[0])

    def test_prepared_gba_driver_validates_without_running_any_converter(self):
        (self.root/'.tools').mkdir()
        (self.root/'.tools/gbafix-reference.c').write_text('test reference')
        generated=self.root/'assets/generated/gba';generated.mkdir(parents=True)
        (generated/'gba_assets.h').write_text('#define GBA_TUTORIAL 1\n')
        with patch.object(build_gba,'ROOT',self.root), \
                patch.object(build_gba.shutil,'which',side_effect=lambda value:value), \
                patch('snes_tutorial_assets.original_imports') as validate, \
                patch.object(build_gba,'run',side_effect=RuntimeError('compiler boundary')) as run:
            with self.assertRaisesRegex(RuntimeError,'compiler boundary'):
                build_gba.build(self.root,prepared_room_assets=True)
            validate.assert_called_once_with(self.root)
            self.assertEqual(run.call_count,1)
            self.assertIn('-c',run.call_args.args[0])

    def test_prepared_gba_rejects_stale_inputs_before_compiling(self):
        (self.root/'.tools').mkdir()
        (self.root/'.tools/gbafix-reference.c').write_text('test reference')
        with patch.object(build_gba,'ROOT',self.root), \
                patch.object(build_gba.shutil,'which',side_effect=lambda value:value), \
                patch('snes_tutorial_assets.original_imports',side_effect=ValueError('stale source identity')), \
                patch.object(build_gba,'run') as run:
            with self.assertRaisesRegex(ValueError,'stale source identity'):
                build_gba.build(self.root,prepared_room_assets=True)
            run.assert_not_called()

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
