"""The opening launcher must not silently reuse the separate POC cartridge."""
import ctypes as C
from pathlib import Path
import shutil
import sys
import unittest
from unittest.mock import Mock, patch
from uuid import uuid4

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import play
import smd_player

BASE = Path(__file__).resolve().parents[1] / 'build/tests/smd-launchers'


class SmdLauncherTests(unittest.TestCase):
    def setUp(self):
        self.root = BASE / uuid4().hex
        self.folder = self.root / 'build/smd'
        self.folder.mkdir(parents=True)
        self.core = self.root / 'core.dll'
        self.core.write_bytes(b'core')
        self.sdk = self.root / 'sdk'
        self.data = self.root / 'data'
        self.data.mkdir()

    def tearDown(self):
        resolved = self.root.resolve()
        if resolved.is_relative_to(BASE.resolve()) and resolved != BASE.resolve():
            shutil.rmtree(resolved)

    def invoke(self, arguments, builder=None):
        with patch.object(smd_player, 'ROOT', self.root), \
                patch.object(smd_player, 'ensure_python', return_value=None), \
                patch.object(smd_player, 'ensure', return_value=(self.sdk, self.core)) as setup, \
                patch.object(smd_player.subprocess, 'run', side_effect=builder) as build, \
                patch.object(smd_player.subprocess, 'call', return_value=0) as launch:
            result = smd_player.main(arguments)
        return result, setup, build, launch

    def test_existing_default_opening_uses_tutorial_and_needs_no_sdk(self):
        opening = self.folder / 'deceptus-tutorial.bin'
        opening.write_bytes(b'opening')
        result, setup, build, launch = self.invoke([])
        self.assertEqual(result, 0)
        setup.assert_called_once_with(emulator=True, sdk_needed=False)
        build.assert_not_called()
        command = launch.call_args.args[0]
        self.assertEqual(command[command.index('--scene') + 1], 'tutorial')
        self.assertIn(str(opening), command)

    def test_explicit_prototype_uses_the_poc_cartridge(self):
        poc = self.folder / 'deceptus-poc.bin'
        poc.write_bytes(b'poc')
        result, _, build, launch = self.invoke(['--scene', 'prototype'])
        self.assertEqual(result, 0)
        build.assert_not_called()
        command = launch.call_args.args[0]
        self.assertIn(str(poc), command)
        self.assertEqual(command[command.index('--scene') + 1], 'prototype')

    def test_poc_does_not_satisfy_a_missing_default_opening(self):
        poc = self.folder / 'deceptus-poc.bin'
        poc.write_bytes(b'poc')

        def builder(command, **kwargs):
            self.assertEqual(command[-2:], ['--scene', 'tutorial'])
            (self.folder / 'deceptus-tutorial.bin').write_bytes(b'opening')

        result, setup, build, launch = self.invoke(['--prepare', '--data', str(self.data)], builder)
        self.assertEqual(result, 0)
        setup.assert_called_once_with(emulator=True, sdk_needed=True)
        build.assert_called_once()
        launch.assert_not_called()
        self.assertEqual(poc.read_bytes(), b'poc')

    def test_explicit_rebuild_forwards_prototype_scene(self):
        poc = self.folder / 'deceptus-poc.bin'
        poc.write_bytes(b'poc')
        _, setup, build, launch = self.invoke(['--scene', 'prototype', '--build', '--prepare',
                                              '--data', str(self.data), '--sdk', str(self.sdk)])
        setup.assert_called_once_with(emulator=True, sdk_needed=False)
        self.assertEqual(build.call_args.args[0][-2:], ['--scene', 'prototype'])
        launch.assert_not_called()

    def test_custom_cartridge_and_core_are_preserved(self):
        custom = self.root / 'custom.bin'
        custom.write_bytes(b'custom')
        _, setup, build, launch = self.invoke(['--rom', str(custom), '--core', str(self.core),
                                              '--scene', 'tutorial'])
        setup.assert_called_once_with(emulator=False, sdk_needed=False)
        build.assert_not_called()
        self.assertIn(str(custom), launch.call_args.args[0])
        self.assertIn(str(self.core), launch.call_args.args[0])

    def test_a_builder_returning_only_the_wrong_scene_is_not_launched(self):
        def builder(command, **kwargs):
            (self.folder / 'deceptus-poc.bin').write_bytes(b'poc')

        with self.assertRaisesRegex(RuntimeError, 'expected cartridge'):
            self.invoke(['--prepare', '--data', str(self.data)], builder)

    def test_venv_reexec_preserves_scene_arguments(self):
        (self.folder / 'deceptus-poc.bin').write_bytes(b'poc')
        arguments = ['--scene', 'prototype', '--prepare']
        with patch.object(smd_player, 'ROOT', self.root), \
                patch.object(smd_player, 'ensure_python', return_value=7) as dependencies, \
                patch.object(smd_player, 'ensure') as setup:
            self.assertEqual(smd_player.main(arguments), 7)
        dependencies.assert_called_once_with('smd', arguments, False)
        setup.assert_not_called()


class SmdControllerTests(unittest.TestCase):
    def test_tutorial_keyboard_matches_the_opening_six_button_input(self):
        mapping = play.console_keyboard_mapping('smd', 'tutorial')
        self.assertEqual({key: mapping[key] for key in ('z', 'space', 'x', 'c', 'q', 'e', 'tab', 'return')},
                         dict(z=0, space=0, x=8, c=1, q=10, e=11, tab=2, **{'return': 3}))
        self.assertNotIn('r', mapping)

    def test_six_button_device_is_configured_for_tutorial_only(self):
        emulator = Mock()
        play.configure_emulator_controls(emulator, 'smd', 'tutorial')
        controller = emulator.core.retro_set_controller_port_device
        controller.assert_called_once_with(0, 513)
        self.assertEqual(controller.argtypes, [C.c_uint, C.c_uint])
        self.assertIsNone(controller.restype)
        for backend, scene in (('smd', 'prototype'), ('gba', 'tutorial'), ('snes', 'tutorial')):
            other = Mock()
            play.configure_emulator_controls(other, backend, scene)
            other.core.retro_set_controller_port_device.assert_not_called()

    def test_prototype_and_other_console_key_mappings_remain_available(self):
        self.assertEqual(play.console_keyboard_mapping('smd', 'prototype')['r'], 1)
        self.assertEqual(play.console_keyboard_mapping('gba', 'tutorial')['z'], 8)
        self.assertEqual(play.console_keyboard_mapping('gba', 'tutorial')['c'], 10)
        self.assertEqual(play.console_keyboard_mapping('snes', 'tutorial')['c'], 1)
