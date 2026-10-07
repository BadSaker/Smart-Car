"""编译真实编码器模块，以编码器寄存器和CMSIS边界桩验证采样行为。"""
from pathlib import Path
import os
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "tmp/encoder-feedback-tests"
COMPILER = os.environ.get(
    "AUTOCAR_HOST_CC",
    r"D:\Xilinx\Vivado\2022.2\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe",
)
CMSIS = """#ifndef ENCODER_TEST_CMSIS_H
#define ENCODER_TEST_CMSIS_H
#include <stdint.h>
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t);
#endif
"""


class EncoderFeedbackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        stub = SCRATCH / "include"
        stub.mkdir(parents=True, exist_ok=True)
        (stub / "fsl_common.h").write_text(CMSIS, encoding="ascii")
        (stub / "zf_common_typedef.h").write_text(
            "#include <stdint.h>\ntypedef int16_t int16;\n", encoding="ascii"
        )
        command = [
            COMPILER, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
            "-I" + str(stub), "-I" + str(ROOT / "car/include"),
            "-I" + str(ROOT / "car/config"),
            "-I" + str(ROOT / "car/library/zf_driver"),
            str(ROOT / "car/tests/test_encoder_feedback.c"),
            str(ROOT / "car/src/encoder_feedback.c"),
        ]
        profiles = {
            "nominal": [],
            "one-to-one": ["-DENCODER_TO_WHEEL_RATIO_NUM=1U",
                           "-DENCODER_TO_WHEEL_RATIO_DEN=1U", "-DTEST_RATIO_ONE_TO_ONE"],
        }
        cls.executables = {}
        for name, definitions in profiles.items():
            executable = SCRATCH / (name + ".exe")
            result = subprocess.run(command + definitions + ["-o", str(executable)],
                                    capture_output=True, text=True,
                                    encoding="utf-8", errors="replace")
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
            cls.executables[name] = executable

    def run_case(self, name, profile="nominal"):
        result = subprocess.run([str(self.executables[profile]), name], capture_output=True,
                                text=True, encoding="utf-8", errors="replace")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        print(profile + ": " + result.stdout.strip())

    def test_raw_signed_counts_and_wrap(self):
        self.run_case("raw")

    def test_initialization_pins_and_baseline(self):
        self.run_case("init")

    def test_wheel_rates_and_signed_rounding(self):
        self.run_case("rates")

    def test_one_to_one_gear_ratio_regression(self):
        self.run_case("rates", "one-to-one")

    def test_reset_rebases_without_clearing_hardware(self):
        self.run_case("reset")

    def test_edges_after_counter_read_are_not_lost(self):
        self.run_case("read-edge")

    def test_saturating_totals_and_sticky_flag(self):
        self.run_case("saturation")

    def test_snapshot_irq_mask_and_null_output(self):
        self.run_case("snapshot")


if __name__ == "__main__":
    unittest.main(verbosity=2)
