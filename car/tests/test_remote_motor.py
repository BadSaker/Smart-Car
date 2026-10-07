"""真实电机遥控后端的主机安全测试，桩仅替代硬件边界。"""
from pathlib import Path
import subprocess
import sys
import unittest

sys.dont_write_bytecode = True
from test_motor_control import COMPILER, PLATFORM

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / 'tmp/remote-motor-tests'
PROFILES = {
    'nominal': ([], None),
    'pwm-scale': (['PWM_DUTY_MAX=20000U'], None),
    'lease-during-gap': (['EXPECT_SHORT_LEASE=1'], 50),
    'invalid-period': (['MOTOR_CONTROL_TICK_MS=20', 'EXPECT_CONFIG_ERROR=1'], None),
}


class RemoteMotorTests(unittest.TestCase):
    def test_remote_safety(self):
        """错误的本地门控、失联处理、租约或换向顺序会触发行为断言。"""
        configuration = (ROOT / 'car/config/remote_control_config.h').read_text(encoding='utf-8-sig')
        for profile, (definitions, lease) in PROFILES.items():
            with self.subTest(profile=profile):
                stub = SCRATCH / profile / 'include'
                stub.mkdir(parents=True, exist_ok=True)
                (stub / 'motor_test_platform.h').write_text(PLATFORM, encoding='ascii')
                for name in ['zf_driver_gpio.h', 'zf_driver_pwm.h', 'encoder_feedback.h']:
                    (stub / name).write_text('#include "motor_test_platform.h"\n', encoding='ascii')
                # 缩短租约仅用于证明等待换向期间超时也会丢弃目标，不修改应用配置。
                if lease is not None:
                    (stub / 'remote_control_config.h').write_text(configuration.replace(
                        '#define REMOTE_CONTROL_DRIVE_LEASE_MS 300U',
                        '#define REMOTE_CONTROL_DRIVE_LEASE_MS ' + str(lease) + 'U'), encoding='utf-8')
                executable = SCRATCH / profile / 'remote-motor.exe'
                command = [COMPILER, '-std=c99', '-Wall', '-Wextra', '-Werror',
                           '-DREMOTE_CONTROL_ENABLED=1', '-I' + str(stub),
                           '-I' + str(ROOT / 'car/include'), '-I' + str(ROOT / 'car/config')]
                command += ['-D' + value for value in definitions]
                command += [str(ROOT / 'car/tests/test_remote_motor.c'),
                            str(ROOT / 'car/src/motor_control.c'), '-o', str(executable)]
                build = subprocess.run(command, capture_output=True, text=True,
                                       encoding='utf-8', errors='replace')
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                result = subprocess.run([str(executable)], capture_output=True, text=True,
                                        encoding='utf-8', errors='replace')
                print(profile + ': ' + result.stdout.strip())
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
