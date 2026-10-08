"""编译真实电机后端，验证自动驾驶、遥控及本地停车输出互斥。"""
from pathlib import Path
import subprocess
import sys
import unittest

sys.dont_write_bytecode = True
from test_motor_control import COMPILER, PLATFORM

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / 'tmp/autodrive-motor/autonomous'
PROFILES = {
    'remote': ['REMOTE_CONTROL_ENABLED=1'],
    'bench': ['REMOTE_CONTROL_ENABLED=0'],
    'scaled': ['REMOTE_CONTROL_ENABLED=1', 'PWM_DUTY_MAX=20000U'],
    'invalid-remote': ['REMOTE_CONTROL_ENABLED=1', 'MOTOR_CONTROL_TICK_MS=20', 'EXPECT_CONFIG_ERROR'],
    'invalid-bench': ['REMOTE_CONTROL_ENABLED=0', 'MOTOR_CONTROL_TICK_MS=20', 'EXPECT_CONFIG_ERROR'],
}


class AutonomousMotorTests(unittest.TestCase):
    def test_autonomous_ownership(self):
        """断链误停、旧遥控复活、停车未锁存或换向无间隔都必须使断言失败。"""
        for name, definitions in PROFILES.items():
            with self.subTest(profile=name):
                stub = SCRATCH / name / 'include'
                stub.mkdir(parents=True, exist_ok=True)
                (stub / 'motor_test_platform.h').write_text(PLATFORM, encoding='ascii')
                for header in ['zf_driver_gpio.h', 'zf_driver_pwm.h', 'encoder_feedback.h']:
                    (stub / header).write_text('#include "motor_test_platform.h"\n', encoding='ascii')
                exe = SCRATCH / name / 'autonomous-motor.exe'
                command = [COMPILER, '-std=c99', '-Wall', '-Wextra', '-Werror',
                           '-I' + str(stub), '-I' + str(ROOT / 'car/include'),
                           '-I' + str(ROOT / 'car/config')]
                command += ['-D' + value for value in definitions]
                command += [str(ROOT / 'car/tests/test_autonomous_motor.c'),
                            str(ROOT / 'car/src/motor_control.c'), '-o', str(exe)]
                result = subprocess.run(command, capture_output=True, text=True,
                                        encoding='utf-8', errors='replace')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(exe)], capture_output=True, text=True,
                                        encoding='utf-8', errors='replace')
                print(name + ': ' + result.stdout.strip())
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
