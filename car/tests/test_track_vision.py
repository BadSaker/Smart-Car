"""以真实灰度图像序列验证视觉模块，不替换扫线或状态机内部函数。"""
from pathlib import Path
import os
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "tmp/autodrive-vision"
COMPILER = os.environ.get("AUTOCAR_HOST_CC", r"D:\Xilinx\Vivado\2022.2\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe")


class TrackVisionTests(unittest.TestCase):
    def test_gray_sequences(self):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        executable = SCRATCH / "test-track-vision.exe"
        command = [COMPILER, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
                   "-I" + str(ROOT / "car/include"), "-I" + str(ROOT / "car/config"),
                   str(ROOT / "car/tests/test_track_vision.c"),
                   str(ROOT / "car/src/track_vision.c"), "-o", str(executable)]
        build = subprocess.run(command, capture_output=True, text=True, errors="replace")
        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
        run = subprocess.run([str(executable)], capture_output=True, text=True, errors="replace")
        print(run.stdout)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
