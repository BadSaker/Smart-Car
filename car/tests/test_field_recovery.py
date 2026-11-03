"""真实控制实现的轮速抑制、弱路径恢复、连续Q与终点计距边界。"""
from pathlib import Path
import subprocess
import unittest
from host_toolchain import find_compiler

ROOT = Path(__file__).resolve().parents[2]

class FieldRecoveryTests(unittest.TestCase):
    def test_control_and_vision_boundaries(self):
        scratch = ROOT/'tmp/field-fix-20261103/control-tests'
        scratch.mkdir(parents=True, exist_ok=True)
        exe = scratch/'field-recovery.exe'
        command = [find_compiler(), '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-I'+str(ROOT/'car/include'), '-I'+str(ROOT/'car/config'),
                   str(ROOT/'car/tests/test_field_recovery.c'),
                   *[str(ROOT/'car/src'/name) for name in
                     ('autonomous_drive.c', 'control_pid.c', 'track_vision.c')],
                   '-lm', '-o', str(exe)]
        result = subprocess.run(command, capture_output=True, text=True, errors='replace')
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        result = subprocess.run([str(exe)], capture_output=True, text=True, errors='replace')
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        print(result.stdout.strip())
