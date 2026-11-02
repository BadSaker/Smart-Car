"""真实灰度标记从远场移动到底部；不注入视觉内部标志。"""
from pathlib import Path
import subprocess
import unittest
from host_toolchain import find_compiler

ROOT = Path(__file__).resolve().parents[2]

class ZebraPassageTests(unittest.TestCase):
    def test_gray_passage(self):
        scratch = ROOT / 'tmp/zebra-20261102'
        scratch.mkdir(parents=True, exist_ok=True)
        exe = scratch / 'zebra-passage.exe'
        command = [find_compiler(), '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-I'+str(ROOT/'car/include'), '-I'+str(ROOT/'car/config'),
                   str(ROOT/'car/tests/test_zebra_passage.c'),
                   str(ROOT/'car/src/track_vision.c'),
                   str(ROOT/'car/src/autonomous_drive.c'),
                   str(ROOT/'car/src/control_pid.c'), '-lm', '-o', str(exe)]
        result = subprocess.run(command, capture_output=True, text=True, errors='replace')
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        result = subprocess.run([str(exe)], capture_output=True, text=True, errors='replace')
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        print(result.stdout.strip())

if __name__ == '__main__':
    unittest.main(verbosity=2)
