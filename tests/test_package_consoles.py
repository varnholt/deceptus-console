"""Release packaging must reject stale or incorrectly labelled cartridges."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import sys
import unittest
from unittest.mock import patch
from uuid import uuid4
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import package_consoles as packaging

BASE = Path(__file__).resolve().parents[1] / 'build/tests/console-packages'


class ConsolePackageTests(unittest.TestCase):
    def setUp(self):
        self.root = BASE / uuid4().hex
        self.root.mkdir(parents=True)
        (self.root / 'inputs').mkdir()
        route = self.root / 'tests/fixtures/opening-route.json'
        route.parent.mkdir(parents=True)
        route.write_text('{}')
        self.notices = {'NOTICE.txt': self.root / 'notice.txt'}
        self.notices['NOTICE.txt'].write_text('original ownership notice\n')
        self.spec = dict(snapshot='20261115-test', targets=[])
        for target, suffix in packaging.EXTENSIONS.items():
            rom = self.root / 'inputs' / (target + suffix)
            data = bytearray(65536 if target == 'snes' else 512)
            if target == 'gba':
                data[0xb2] = 0x96
                data[0xa0:0xa8] = b'DECEPTUS'
            elif target == 'snes':
                data[0x7fc0:0x7fc8] = b'DECEPTUS'
            else:
                data[0x100:0x104] = b'SEGA'
            rom.write_bytes(data)
            sha = hashlib.sha256(data).hexdigest()
            build = self.root / 'inputs' / (target + '-build.json')
            build.write_text(json.dumps(dict(rom_sha256=sha, rom_bytes=len(data))))
            state = dict(room=7, flags=8095, health=29, deaths=0, has_sword=1,
                         inventory=[0, 0, 0, 0, 0, 1, 1], content_error=0)
            states = [dict(state, room=index % 23) for index in range(56)]
            report = dict(scope='test fixture', emulator='test core', rom_sha256=sha)
            if target == 'smd':
                report.update(comparisons=600, checkpoints=[dict(name='idle')],
                              benchmark=dict(simulation_ticks=600, display_frames=600))
            else:
                report.update(native=copy.deepcopy(states), **{target: states},
                              route_sha256=hashlib.sha256(route.read_bytes()).hexdigest())
            result = self.root / 'inputs' / (target + '-report.json')
            result.write_text(json.dumps(report))
            self.spec['targets'].append(dict(target=target, rom=str(rom), build_report=str(build),
                                             reports=[str(result)],
                                             coverage='prototype' if target == 'smd' else 'opening',
                                             performance='performance recorded by test report'))

    def tearDown(self):
        resolved = self.root.resolve()
        if resolved.is_relative_to(BASE.resolve()) and resolved != BASE.resolve():
            shutil.rmtree(resolved)

    def package(self, name='snapshot', dirty=False):
        specification = self.root / 'spec.json'
        specification.write_text(json.dumps(self.spec))
        with patch.object(packaging, 'ROOT', self.root), \
                patch.object(packaging, 'NOTICES', self.notices), \
                patch.object(packaging, 'code_dirty', return_value=dirty), \
                patch.object(packaging, 'source_snapshot', return_value=dict(sha256='2' * 64, files=[])), \
                patch.object(packaging, 'code_revision', return_value='1' * 40):
            return packaging.package(specification, Path('build/release') / name)

    def test_package_contains_only_cartridges_reports_notices_and_manifest(self):
        result = self.package()
        archive = self.root / 'build/release/snapshot.zip'
        with zipfile.ZipFile(archive) as bundle:
            names = bundle.namelist()
            self.assertIn('roms/Deceptus-gba.gba', names)
            self.assertIn('roms/Deceptus-snes.sfc', names)
            self.assertIn('roms/Deceptus-smd.bin', names)
            self.assertFalse(any(name.endswith(('.png', '.tmx', '.lua')) for name in names))
        self.assertEqual(result['targets'][2]['coverage'], 'opening')
        self.assertFalse(result['full_game'])

    def test_archive_is_byte_reproducible(self):
        self.package('first')
        self.package('second')
        self.assertEqual((self.root / 'build/release/first.zip').read_bytes(),
                         (self.root / 'build/release/second.zip').read_bytes())

    def test_dirty_source_records_base_commit_without_claiming_a_clean_build(self):
        result = self.package(dirty=True)
        self.assertIsNone(result['source_commit'])
        self.assertEqual(result['source_base_commit'], '1' * 40)
        self.assertTrue(result['source_dirty'])
        self.assertEqual(result['source_snapshot_sha256'], '2' * 64)

    def test_stale_verification_fails_before_output_creation(self):
        path = Path(self.spec['targets'][0]['reports'][0])
        report = json.loads(path.read_text())
        report['rom_sha256'] = '0' * 64
        path.write_text(json.dumps(report))
        with self.assertRaisesRegex(ValueError, 'stale or unrelated'):
            self.package()
        self.assertFalse((self.root / 'build/release/snapshot').exists())

    def test_wrong_extension_and_machine_header_are_rejected(self):
        original = Path(self.spec['targets'][0]['rom'])
        wrong = original.with_suffix('.sfc')
        original.rename(wrong)
        self.spec['targets'][0]['rom'] = str(wrong)
        with self.assertRaisesRegex(ValueError, 'misnamed'):
            self.package()
        wrong.rename(original)
        self.spec['targets'][0]['rom'] = str(original)
        original.write_bytes(bytes(512))
        with self.assertRaisesRegex(ValueError, 'cartridge header'):
            self.package()

    def test_native_only_report_cannot_be_published_as_emulator_verified(self):
        path = Path(self.spec['targets'][0]['reports'][0])
        report = json.loads(path.read_text())
        del report['gba']
        path.write_text(json.dumps(report))
        with self.assertRaisesRegex(ValueError, 'actual-cartridge'):
            self.package()

    def test_poc_cannot_claim_the_complete_opening(self):
        self.spec['targets'][2]['coverage'] = 'opening'
        with self.assertRaisesRegex(ValueError, 'complete opening'):
            self.package()

    def test_full_mega_drive_opening_requires_clean_actual_graphics(self):
        target = self.spec['targets'][2]
        target['coverage'] = 'opening'
        path = Path(target['reports'][0])
        report = json.loads(path.read_text())
        opening = json.loads(Path(self.spec['targets'][0]['reports'][0]).read_text())
        report.update(native=opening['native'], smd=opening['gba'],
                      route_sha256=opening['route_sha256'])
        path.write_text(json.dumps(report))
        result = self.package('full-opening')
        self.assertTrue(all(item['coverage'] == 'opening' for item in result['targets']))
        report['smd'][20]['graphics_error'] = 1
        path.write_text(json.dumps(report))
        with self.assertRaisesRegex(ValueError, 'graphics_error'):
            self.package('bad-graphics')

    def test_existing_release_is_preserved(self):
        self.package()
        with self.assertRaisesRegex(ValueError, 'already exists'):
            self.package()

    def test_three_targets_are_required_once_each(self):
        self.spec['targets'][2] = self.spec['targets'][0]
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            self.package()

    def test_a_changed_build_manifest_is_rejected(self):
        path = Path(self.spec['targets'][0]['build_report'])
        build = json.loads(path.read_text())
        build['rom_sha256'] = '0' * 64
        path.write_text(json.dumps(build))
        with self.assertRaisesRegex(ValueError, 'stale gba build'):
            self.package()

    def test_failed_build_cannot_publish_a_leftover_cartridge(self):
        path = Path(self.spec['targets'][0]['build_report'])
        build = json.loads(path.read_text())
        build['returncode'] = 1
        path.write_text(json.dumps(build))
        with self.assertRaisesRegex(ValueError, 'successful result'):
            self.package()

    def test_missing_ownership_notice_prevents_release_creation(self):
        self.notices['NOTICE.txt'].unlink()
        with self.assertRaisesRegex(ValueError, 'ownership/dependency notice'):
            self.package()
        self.assertFalse((self.root / 'build/release/snapshot').exists())

    def test_report_changes_during_copy_do_not_produce_a_zip(self):
        report_path = Path(self.spec['targets'][0]['reports'][0])
        original_copy = shutil.copyfile

        def changed_copy(source, destination):
            if Path(source) == report_path:
                report = json.loads(report_path.read_text())
                report['scope'] = 'changed while copying'
                report_path.write_text(json.dumps(report))
            return original_copy(source, destination)

        with patch.object(packaging.shutil, 'copyfile', side_effect=changed_copy):
            with self.assertRaisesRegex(ValueError, 'report changed while packaging'):
                self.package()
        self.assertFalse((self.root / 'build/release/snapshot.zip').exists())
