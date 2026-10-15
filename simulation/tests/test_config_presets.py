"""Activity, archive and camera comparison paths share the real loader."""
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from app import App
from parameters import load
from tools.check_camera_alignment import argument_parser


class ConfigPresetTests(unittest.TestCase):
    def test_all_active_presets_load(self):
        paths = list((ROOT / 'configs').glob('*.json'))
        self.assertEqual(len(paths), 11)
        for path in paths:
            with self.subTest(preset=path.name):
                load(path)

    def test_historical_presets_remain_loadable(self):
        paths = list((ROOT / 'configs/archive').glob('*.json'))
        self.assertEqual(len(paths), 2)
        for path in paths:
            with self.subTest(preset=path.name):
                load(path)

    def test_archive_is_not_in_builtin_selector(self):
        dummy = SimpleNamespace(extra_presets={}, parameter_selector=Mock())
        App.update_presets(dummy)
        builtin = {path.stem for path in (ROOT / 'configs').glob('*.json')}
        archive = {path.stem for path in (ROOT / 'configs/archive').glob('*.json')}
        self.assertTrue(builtin.issubset(dummy.preset_paths))
        self.assertTrue(archive.isdisjoint(dummy.preset_paths))
        dummy.parameter_selector.configure.assert_called_once_with(
            values=['默认参数', *dummy.preset_paths])

    def test_camera_comparison_defaults_load(self):
        args = argument_parser().parse_args(['--output', 'unused'])
        self.assertEqual(args.before.parent, ROOT / 'configs/archive')
        self.assertEqual(load(args.before)['pitch'], 53.0)
        self.assertEqual(args.after, ROOT / 'configs/我的参数.json')
        load(args.after)


if __name__ == '__main__':
    unittest.main()
