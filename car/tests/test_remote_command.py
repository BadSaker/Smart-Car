"""编译真实文本解析器，验证严格分帧与异常输入恢复，不依赖实车或网络。"""
from pathlib import Path
import os
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "tmp/remote-command-tests"
COMPILER = os.environ.get(
    "AUTOCAR_HOST_CC",
    r"D:\Xilinx\Vivado\2022.2\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe",
)


class RemoteCommandTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        cls.executable = SCRATCH / "test-remote-command.exe"
        result = subprocess.run(
            [COMPILER, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2",
             "-I" + str(ROOT / "car/include"),
             str(ROOT / "car/tests/test_remote_command.c"),
             str(ROOT / "car/src/remote_command.c"), "-o", str(cls.executable)],
            capture_output=True, text=True, encoding="utf-8", errors="replace",
        )
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_case(self, name):
        result = subprocess.run(
            [str(self.executable), name], capture_output=True, text=True,
            encoding="utf-8", errors="replace",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        print(result.stdout.strip())

    def test_all_eight_exact_keys_and_order(self):
        self.run_case("mapping")

    def test_every_stream_split_and_bytewise_delivery(self):
        self.run_case("chunks")

    def test_no_action_before_complete_crlf(self):
        self.run_case("incomplete")

    def test_unknown_case_whitespace_and_empty_lines(self):
        self.run_case("unknown")

    def test_malformed_crlf_poison_until_delimiter(self):
        self.run_case("malformed")

    def test_non_ascii_and_binary_prefix_never_become_commands(self):
        self.run_case("binary")

    def test_overlong_line_is_bounded_and_recovers(self):
        self.run_case("long")

    def test_null_arguments_and_zero_length_keep_partial_line(self):
        self.run_case("null")

    def test_null_handler_consumes_without_replaying(self):
        self.run_case("null-handler")

    def test_reset_discards_partial_and_rejected_line(self):
        self.run_case("reset")

    def test_counters_saturate_without_losing_events(self):
        self.run_case("saturation")

    def test_independent_parsers_and_null_context(self):
        self.run_case("contexts")


if __name__ == "__main__":
    unittest.main(verbosity=2)
