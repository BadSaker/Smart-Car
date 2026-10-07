"""编译真实舵机模块，使用GPIO/PWM桩验证无法在主机执行的硬件边界。"""
from pathlib import Path
import argparse
import os
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / 'tmp/servo-control-tests'
COMPILER = os.environ.get('AUTOCAR_HOST_CC', r'D:\Xilinx\Vivado\2022.2\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe')
PLATFORM = '''#ifndef SERVO_TEST_PLATFORM_H
#define SERVO_TEST_PLATFORM_H
#include <stdint.h>
typedef enum { C14 = 46, C15 = 47, C30 = 62 } gpio_pin_enum;
typedef enum { GPI = 0, GPO = 1 } gpio_dir_enum;
typedef enum { PWM4_MODULE2_CHA_C30 = 185 } pwm_channel_enum;
#define GPIO_LOW 0
#define GPIO_HIGH 1
#define GPI_PULL_UP 22
#define GPO_PUSH_PULL 33
#ifndef PWM_DUTY_MAX
#define PWM_DUTY_MAX 10000
#endif
void gpio_init(gpio_pin_enum, gpio_dir_enum, uint8_t, uint32_t);
uint8_t gpio_get_level(gpio_pin_enum);
void pwm_init(pwm_channel_enum, uint32_t, uint32_t);
void pwm_set_duty(pwm_channel_enum, uint32_t);
#endif
'''
PROFILES = {
    'nominal': [],
    'undervoltage': ['SERVO_SUPPLY_MV=5000'],
    'overvoltage': ['SERVO_SUPPLY_MV=7500'],
    'upper-voltage': ['SERVO_SUPPLY_MV=7400'],
    'duty-scale': ['PWM_DUTY_MAX=20000'],
    'uneven-step': ['SERVO_TEST_STEP_US=7'],
}
SELECTED = None


class ServoControlTests(unittest.TestCase):
    def test_hardware_control_profiles(self):
        """覆盖上电关闭、手动触发、停止优先、限幅步进和计时回绕。"""
        stub = SCRATCH / 'include'
        stub.mkdir(parents=True, exist_ok=True)
        (stub / 'servo_test_platform.h').write_text(PLATFORM, encoding='ascii')
        for name in ['zf_driver_gpio.h', 'zf_driver_pwm.h']:
            (stub / name).write_text('#include "servo_test_platform.h"\n', encoding='ascii')
        profiles = PROFILES if SELECTED is None else {SELECTED: PROFILES[SELECTED]}
        for name, definitions in profiles.items():
            with self.subTest(profile=name):
                executable = SCRATCH / (name + '.exe')
                command = [COMPILER, '-std=c99', '-Wall', '-Wextra', '-Werror',
                           '-I' + str(stub), '-I' + str(ROOT / 'car/include'),
                           '-I' + str(ROOT / 'car/config')]
                command += ['-D' + value for value in definitions]
                command += [str(ROOT / 'car/tests/test_servo_control.c'),
                            str(ROOT / 'car/src/servo_control.c'), '-o', str(executable)]
                build = subprocess.run(command, capture_output=True, text=True,
                                       encoding='utf-8', errors='replace')
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                result = subprocess.run([str(executable)], capture_output=True, text=True,
                                        encoding='utf-8', errors='replace')
                print(name + ': ' + result.stdout.strip())
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--profile', choices=PROFILES)
    options, remaining = parser.parse_known_args()
    SELECTED = options.profile
    unittest.main(argv=[__file__] + remaining, verbosity=2)
