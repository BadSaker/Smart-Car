"""使用真实遥控适配层和文本解析器，替换网络读取、时钟及执行器边界。"""
from pathlib import Path
import os
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "tmp/remote-control-tests"
COMPILER = os.environ.get(
    "AUTOCAR_HOST_CC",
    r"D:\Xilinx\Vivado\2022.2\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe",
)


class RemoteControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        stub = SCRATCH / "include"
        stub.mkdir(parents=True, exist_ok=True)
        # 公开摄像头头文件只需类型声明；测试不读取本机的真实热点配置。
        (stub / "camera_debug_config.h").write_text(
            "#ifndef REMOTE_TEST_CAMERA_CONFIG_H\n"
            "#define REMOTE_TEST_CAMERA_CONFIG_H\n#endif\n", encoding="ascii"
        )
        cls.executables = {}
        for name, mode in [("enabled", "1"), ("disabled", "0")]:
            executable = SCRATCH / ("remote-control-" + name + ".exe")
            command = [
                COMPILER, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2",
                "-DREMOTE_CONTROL_ENABLED=" + mode,
                "-I" + str(stub), "-I" + str(ROOT / "car/include"),
                "-I" + str(ROOT / "car/config"),
                str(ROOT / "car/tests/test_remote_control.c"),
                str(ROOT / "car/src/remote_control.c"),
                str(ROOT / "car/src/remote_command.c"), "-o", str(executable),
            ]
            result = subprocess.run(command, capture_output=True, text=True,
                                    encoding="utf-8", errors="replace")
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
            cls.executables[name] = executable

    def run_case(self, name, profile="enabled"):
        result = subprocess.run([str(self.executables[profile]), name],
                                capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        print(profile + ": " + result.stdout.strip())

    def test_initial_drain_requires_actual_empty_read(self):
        self.run_case("initial")

    def test_local_arm_discards_old_commands_and_partial_prefix(self):
        self.run_case("arm")

    def test_exact_drive_and_steering_mapping(self):
        self.run_case("mapping")

    def test_split_and_coalesced_real_parser(self):
        self.run_case("parser")

    def test_down_ack_before_wait_gate_and_stop_wins_chunk(self):
        self.run_case("stop")

    def test_steering_does_not_refresh_propulsion_lease(self):
        self.run_case("steering")

    def test_fresh_drive_after_gap_clears_previous_turn(self):
        self.run_case("gap")

    def test_steering_timeout_including_clock_wrap(self):
        self.run_case("wrap")

    def test_not_ready_never_reads_and_reconnect_never_rearms(self):
        self.run_case("link")

    def test_changed_error_or_reconnect_counter_invalidates_ready(self):
        self.run_case("counters")

    def test_session_change_inside_read_drops_returned_bytes(self):
        self.run_case("read-session")

    def test_local_arm_inside_read_drops_returned_bytes(self):
        self.run_case("read-arm")

    def test_read_latency_limit_and_post_read_timestamp(self):
        self.run_case("latency")

    def test_invalid_read_size_and_return_fail_closed(self):
        self.run_case("invalid-read")

    def test_null_clock_fails_closed_without_read(self):
        self.run_case("null-clock")

    def test_service_and_later_packets_cannot_reactivate_s3_disarm(self):
        self.run_case("disarm")

    def test_main_stall_rejects_queued_drive_before_read(self):
        self.run_case("main-poll-gap")

    def test_main_stall_service_cannot_restore_old_steering(self):
        self.run_case("main-service-gap")

    def test_main_service_gap_300_301ms_and_wrap(self):
        self.run_case("main-boundary")

    def test_silent_regular_polling_preserves_local_arm(self):
        self.run_case("main-silent")

    def test_post_read_clock_updates_shared_service_reference(self):
        self.run_case("main-post-read")

    def test_disabled_mode_has_no_external_side_effects(self):
        self.run_case("disabled", "disabled")


if __name__ == "__main__":
    unittest.main(verbosity=2)
