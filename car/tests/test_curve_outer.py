"""实际扫线的外侧优先、伪内侧、失帧和禁用开关回归。"""
from pathlib import Path
import os
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = os.environ.get("AUTOCAR_HOST_CC", r"D:\Xilinx\Vivado\2022.2\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe")

SCRATCH = ROOT / "tmp/curve-outer-tests"

class CurveOuterTests(unittest.TestCase):
    def test_gray_outer_selection(self):
        scratch = SCRATCH
        scratch.mkdir(parents=True, exist_ok=True)
        for enabled in (1, 0):
            with self.subTest(enabled=enabled):
                executable = scratch / f"curve-{enabled}.exe"
                cmd = [COMPILER, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
                       f"-DVISION_CURVE_OUTER_ENABLED={enabled}",
                       "-I" + str(ROOT / "car/include"), "-I" + str(ROOT / "car/config"),
                       str(ROOT / "car/tests/test_curve_outer.c"),
                       str(ROOT / "car/src/track_vision.c"), "-o", str(executable)]
                build = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                run = subprocess.run([str(executable)], capture_output=True, text=True, errors="replace")
                print(f"outer-enabled={enabled}: {run.stdout}")
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
