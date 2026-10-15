"""Check the moved credential include using fake inputs and preprocessing only."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from host_toolchain import find_compiler

ROOT = Path(__file__).resolve().parents[2]


class WifiLayoutTests(unittest.TestCase):
    def test_relative_include_finds_wifi_template(self):
        scratch = Path(os.environ.get('AUTOCAR_TEST_ROOT', ROOT / 'tmp')) / 'wifi-layout'
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            root = Path(directory)
            config = root / 'car/config'
            config.mkdir(parents=True)
            (root / 'wifi').mkdir()
            shutil.copy2(ROOT / 'car/config/camera_debug_config.h', config)
            shutil.copy2(ROOT / 'wifi/camera_debug_local.example.h',
                         root / 'wifi/camera_debug_local.h')
            source = root / 'check.c'
            source.write_text('#include "camera_debug_config.h"\n'
                              'const char *ssid = CAMERA_DEBUG_WIFI_SSID;\n'
                              'const char *ip = CAMERA_DEBUG_WIFI_SERVER_IP;\n',
                              encoding='ascii')
            result = subprocess.run([find_compiler(), '-E', '-P', '-I', str(config),
                                     str(source)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('"ExampleHotspot"', result.stdout)
            self.assertIn('"192.168.137.1"', result.stdout)

    def test_old_locations_are_absent(self):
        for relative in ('热点配置.cmd', 'car/scripts/configure-wifi.ps1',
                         'car/scripts/wifi-menu.ps1', 'car/config/camera_debug_local.h',
                         'car/config/camera_debug_local.example.h'):
            with self.subTest(path=relative):
                self.assertFalse((ROOT / relative).exists())


if __name__ == '__main__':
    unittest.main()
