"""以原生C99编译真实PID模块，覆盖采样单位、限幅、微分及安全复位。"""
from pathlib import Path
import os
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "tmp/autodrive-pid"
COMPILER = os.environ.get(
    "AUTOCAR_HOST_CC",
    r"D:\Xilinx\Vivado\2022.2\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe",
)


class ControlPidTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        cls.executable = SCRATCH / "test-control-pid.exe"
        cls.environment = os.environ.copy()
        cls.environment["PATH"] = str(Path(COMPILER).parent) + os.pathsep + cls.environment["PATH"]
        command = [
            COMPILER, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2",
            "-I" + str(ROOT / "car/include"),
            str(ROOT / "car/tests/test_control_pid.c"),
            str(ROOT / "car/src/control_pid.c"), "-lm", "-o", str(cls.executable),
        ]
        result = subprocess.run(command, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", env=cls.environment)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_case(self, name):
        result = subprocess.run([str(self.executable), name], capture_output=True,
                                text=True, encoding="utf-8", errors="replace",
                                env=self.environment)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_proportional_feedforward_and_output_limits(self):
        self.run_case("proportional")

    def test_integral_scales_with_elapsed_seconds(self):
        self.run_case("integral-time")

    def test_integral_limits_are_output_units(self):
        self.run_case("integral-limit")

    def test_saturation_freezes_windup_and_reversal_recovers(self):
        self.run_case("antiwindup")

    def test_feedforward_saturation_allows_unwinding(self):
        self.run_case("feedforward-unwind")

    def test_signed_gain_uses_integral_direction(self):
        self.run_case("signed-gain")

    def test_integral_only_can_reach_output_limits(self):
        self.run_case("integral-only")

    def test_measurement_derivative_avoids_setpoint_kick(self):
        self.run_case("derivative-measurement")

    def test_derivative_filter_at_variable_sample_periods(self):
        self.run_case("derivative-filter")

    def test_reset_clears_integral_and_derivative_history(self):
        self.run_case("reset")

    def test_nonfinite_inputs_clear_state_and_next_sample_recovers(self):
        self.run_case("invalid-input")

    def test_invalid_configuration_and_null_calls_are_safe(self):
        self.run_case("invalid-config")

    def test_arithmetic_overflow_clears_state_and_reports_invalid(self):
        self.run_case("overflow")


if __name__ == "__main__":
    unittest.main(verbosity=2)
