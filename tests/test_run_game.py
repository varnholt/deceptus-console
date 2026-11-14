"""Launcher setup should not replace cached cores or rebuild existing cartridges."""
import json
from pathlib import Path
import shutil
import sys
import unittest
from unittest.mock import patch
import uuid
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import bootstrap
import run_game

ROOT = Path(__file__).resolve().parents[1]


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.folder = ROOT / 'build/tests/launchers' / uuid.uuid4().hex
        self.folder.mkdir(parents=True)
        self.core = self.folder / 'core.dll'
        self.core.write_bytes(b'core')

    def tearDown(self):
        if self.folder.resolve().is_relative_to((ROOT / 'build/tests/launchers').resolve()):
            shutil.rmtree(self.folder)

    def test_existing_gba_tutorial_launches_without_build_or_sdk_setup(self):
        rom = self.folder / 'build/gba/deceptus-tutorial.gba'
        rom.parent.mkdir(parents=True)
        rom.write_bytes(b'cartridge')
        with patch.object(run_game, 'ROOT', self.folder), \
             patch.object(run_game, 'ensure_python', return_value=None), \
             patch.object(bootstrap, 'ensure_emulator', return_value=self.core), \
             patch.object(run_game, 'run') as build, \
             patch.object(bootstrap, 'bootstrap') as sdk, \
             patch.object(run_game.subprocess, 'call', return_value=0) as launch:
            self.assertEqual(run_game.main('gba', []), 0)
        build.assert_not_called()
        sdk.assert_not_called()
        command = launch.call_args.args[0]
        self.assertIn(str(rom), command)
        self.assertIn(str(self.core), command)
        self.assertIn('gba', command)

    def test_prepare_fetches_only_core_when_snes_opening_rom_exists(self):
        rom = self.folder / 'build/snes/deceptus-tutorial.sfc'
        rom.parent.mkdir(parents=True)
        rom.write_bytes(b'cartridge')
        with patch.object(run_game, 'ROOT', self.folder), \
             patch.object(run_game, 'ensure_python', return_value=None), \
             patch.object(bootstrap, 'ensure_emulator', return_value=self.core) as core, \
             patch.object(run_game, 'run') as build, \
             patch.object(run_game.subprocess, 'call') as launch:
            self.assertEqual(run_game.main('snes', ['--prepare']), 0)
        core.assert_called_once_with('snes')
        build.assert_not_called()
        launch.assert_not_called()

    def test_snes_prototype_is_an_explicit_separate_scene(self):
        rom = self.folder / 'build/snes/deceptus-prototype.sfc'
        rom.parent.mkdir(parents=True)
        rom.write_bytes(b'prototype')
        with patch.object(run_game, 'ROOT', self.folder), \
             patch.object(run_game, 'ensure_python', return_value=None), \
             patch.object(bootstrap, 'ensure_emulator', return_value=self.core), \
             patch.object(run_game, 'run') as build, \
             patch.object(run_game.subprocess, 'call', return_value=0) as launch:
            self.assertEqual(run_game.main('snes', ['--scene', 'prototype']), 0)
        build.assert_not_called()
        self.assertIn(str(rom), launch.call_args.args[0])
        self.assertEqual(launch.call_args.args[0][-2:], ['--scene', 'prototype'])

    def test_old_snes_prototype_does_not_satisfy_default_opening(self):
        old = self.folder / 'build/snes/deceptus.sfc'
        old.parent.mkdir(parents=True)
        old.write_bytes(b'old prototype')
        data = self.folder / 'data'
        data.mkdir()
        sdk = self.folder / 'sdk'
        compiler = sdk / 'devkitsnes/bin' / ('816-tcc.exe' if sys.platform == 'win32' else '816-tcc')
        compiler.parent.mkdir(parents=True)
        compiler.write_bytes(b'compiler')
        opening = old.with_name('deceptus-tutorial.sfc')

        def build(command):
            self.assertEqual(command[-2:], ['--scene', 'tutorial'])
            opening.write_bytes(b'opening')

        with patch.object(run_game, 'ROOT', self.folder), \
             patch.object(run_game, 'ensure_python', return_value=None), \
             patch.object(bootstrap, 'ensure_emulator', return_value=self.core), \
             patch.object(run_game, 'run', side_effect=build) as build_command:
            self.assertEqual(run_game.main('snes', ['--prepare', '--sdk', str(sdk), '--data', str(data)]), 0)
        build_command.assert_called_once()
        self.assertEqual(old.read_bytes(), b'old prototype')

    def test_missing_gba_rom_builds_the_tutorial_then_prepares(self):
        data = self.folder / 'data'
        data.mkdir()
        reference = self.folder / '.tools/gbafix-reference.c'
        reference.parent.mkdir()
        reference.write_text('reference')
        commands = []

        def build(command):
            commands.append(list(map(str, command)))
            rom = self.folder / 'build/gba/deceptus-tutorial.gba'
            rom.parent.mkdir(parents=True)
            rom.write_bytes(b'cartridge')

        with patch.object(run_game, 'ROOT', self.folder), \
             patch.object(run_game, 'ensure_python', return_value=None), \
             patch.object(bootstrap, 'ensure_emulator', return_value=self.core), \
             patch.object(run_game, 'run', side_effect=build):
            self.assertEqual(run_game.main('gba', ['--prepare', '--data', str(data)]), 0)
        self.assertEqual(len(commands), 1)
        self.assertIn('tutorial', commands[0])
        self.assertIn(str(data), commands[0])

    def test_cached_core_never_downloads(self):
        core = self.folder / '.tools/mgba/mgba_libretro.dll'
        core.parent.mkdir(parents=True)
        core.write_bytes(b'cached')
        with patch.object(bootstrap, 'ROOT', self.folder), \
             patch.object(bootstrap.platform, 'system', return_value='Windows'), \
             patch.object(bootstrap.platform, 'machine', return_value='AMD64'), \
             patch.object(bootstrap, 'download') as download:
            self.assertEqual(bootstrap.ensure_emulator('gba'), core)
        download.assert_not_called()
        self.assertEqual(core.read_bytes(), b'cached')

    def test_missing_core_is_extracted_and_recorded_without_sdk(self):
        def download(url, archive):
            with zipfile.ZipFile(archive, 'w') as zipped:
                zipped.writestr('snes9x_libretro.dll', b'new core')
            return 'download-checksum'

        with patch.object(bootstrap, 'ROOT', self.folder), \
             patch.object(bootstrap.platform, 'system', return_value='Windows'), \
             patch.object(bootstrap.platform, 'machine', return_value='AMD64'), \
             patch.object(bootstrap, 'download', side_effect=download):
            core = bootstrap.ensure_emulator('snes')
        self.assertEqual(core.read_bytes(), b'new core')
        manifest = json.loads((self.folder / '.tools/downloads.json').read_text())
        self.assertEqual(manifest['snes9x']['sha256'], 'download-checksum')
        self.assertFalse((self.folder / '.tools/pvsneslib').exists())

    def test_dependency_install_uses_local_venv_and_preserves_caller_directory(self):
        python = self.folder / '.venv' / ('Scripts/python.exe' if sys.platform == 'win32' else 'bin/python')
        python.parent.mkdir(parents=True)
        python.write_bytes(b'python')
        with patch.object(run_game, 'ROOT', self.folder), \
             patch.object(run_game.importlib.util, 'find_spec', return_value=None), \
             patch.object(run_game, 'run') as install, \
             patch.object(run_game.subprocess, 'call', return_value=0) as relaunch:
            self.assertEqual(run_game.ensure_python('gba', ['--rom', 'relative.gba'], False), 0)
        command = install.call_args.args[0]
        self.assertEqual(command[0], python)
        self.assertIn('pip', command)
        self.assertEqual(relaunch.call_args.kwargs['cwd'], Path.cwd())

    def test_missing_custom_cartridge_does_not_download_or_build(self):
        with patch.object(run_game, 'ROOT', self.folder), \
             patch.object(bootstrap, 'ensure_emulator') as core, \
             patch.object(run_game, 'run') as build, \
             self.assertRaises(SystemExit):
            run_game.main('snes', ['--rom', str(self.folder / 'missing.sfc')])
        core.assert_not_called()
        build.assert_not_called()


if __name__ == '__main__':
    unittest.main()
