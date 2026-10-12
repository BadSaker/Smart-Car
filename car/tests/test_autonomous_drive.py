from host_toolchain import find_compiler
"""编译真实自动驾驶状态机，输入仅替代相机摘要、按键和编码器采样。"""
from pathlib import Path
import subprocess
import unittest
import os
ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "tmp/autodrive/controller-tests"
COMPILER = find_compiler()
class AutonomousDriveTests(unittest.TestCase):
    def test_controller(self):
        source = ROOT / "car/src/autonomous_drive.c"
        self.assertTrue(source.exists(), "自动驾驶状态机尚未实现")
        SCRATCH.mkdir(parents=True, exist_ok=True)
        exe = SCRATCH / "autonomous-drive.exe"
        cmd = [COMPILER, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
               "-I"+str(ROOT/"car/include"), "-I"+str(ROOT/"car/config"),
               str(ROOT/"car/tests/test_autonomous_drive.c"), str(source),
               str(ROOT/"car/src/control_pid.c"), "-lm", "-o", str(exe)]
        result = subprocess.run(cmd, capture_output=True, text=True, encoding="utf8", errors="replace")
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        result = subprocess.run([str(exe)], capture_output=True, text=True, encoding="utf8", errors="replace")
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        print(result.stdout.strip())
if __name__ == "__main__": unittest.main(verbosity=2)
