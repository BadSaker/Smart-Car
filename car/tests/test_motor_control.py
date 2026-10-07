"""编译真实电机模块，桩仅覆盖硬件IO、编码器复位与CMSIS屏蔽接口。"""
from pathlib import Path
import argparse
import os
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / 'tmp/motor-control-tests'
COMPILER = os.environ.get('AUTOCAR_HOST_CC', r'D:\Xilinx\Vivado\2022.2\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe')
PLATFORM = '''#ifndef MOTOR_TEST_PLATFORM_H
#define MOTOR_TEST_PLATFORM_H
#include <stdint.h>
typedef enum { C6 = 70, C7, C8, C9, C12 = 76, C14 = 78 } gpio_pin_enum;
typedef enum { GPI = 0, GPO = 1 } gpio_dir_enum;
typedef enum { PWM2_MODULE0_CHA_C6 = 85, PWM2_MODULE1_CHA_C8 = 95 } pwm_channel_enum;
#define GPIO_LOW 0U
#define GPIO_HIGH 1U
#define GPI_PULL_UP 22U
#define GPO_PUSH_PULL 33U
#ifndef PWM_DUTY_MAX
#define PWM_DUTY_MAX 10000U
#endif
void gpio_init(gpio_pin_enum, gpio_dir_enum, uint8_t, uint32_t);
uint8_t gpio_get_level(gpio_pin_enum);
void gpio_set_level(gpio_pin_enum, uint8_t);
void pwm_init(pwm_channel_enum, uint32_t, uint32_t);
void pwm_set_duty(pwm_channel_enum, uint32_t);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t);
void encoder_feedback_reset_totals(void);
#endif
'''
PROFILES = {
    'nominal': [],
    'maximum-duty': ['MOTOR_TEST_DUTY_PERMILLE=150'],
    'minimum-duty': ['MOTOR_TEST_DUTY_PERMILLE=1'],
    'duty-scale': ['PWM_DUTY_MAX=20000U'],
    'over-duty': ['MOTOR_TEST_DUTY_PERMILLE=151', 'EXPECT_CONFIG_ERROR=1'],
    'zero-duty': ['MOTOR_TEST_DUTY_PERMILLE=0', 'EXPECT_CONFIG_ERROR=1'],
    'negative-duty': ['MOTOR_TEST_DUTY_PERMILLE=-1', 'EXPECT_CONFIG_ERROR=1'],
    'incorrect-period': ['MOTOR_CONTROL_TICK_MS=20', 'EXPECT_CONFIG_ERROR=1'],
    'zero-period': ['MOTOR_CONTROL_TICK_MS=0', 'EXPECT_CONFIG_ERROR=1'],
}
SELECTED = None


class MotorControlTests(unittest.TestCase):
    def test_hardware_control_profiles(self):
        """错误的启动门控、停止顺序、时长或占空比会令真实模块行为失败。"""
        source = ROOT / 'car/src/motor_control.c'
        self.assertTrue(source.is_file(), '电机模块尚未实现，安全行为测试应先失败')
        stub = SCRATCH / 'include'
        stub.mkdir(parents=True, exist_ok=True)
        (stub / 'motor_test_platform.h').write_text(PLATFORM, encoding='ascii')
        for name in ['zf_driver_gpio.h', 'zf_driver_pwm.h', 'encoder_feedback.h']:
            (stub / name).write_text('#include "motor_test_platform.h"\n', encoding='ascii')
        profiles = PROFILES if SELECTED is None else {SELECTED: PROFILES[SELECTED]}
        for name, definitions in profiles.items():
            with self.subTest(profile=name):
                executable = SCRATCH / (name + '.exe')
                command = [COMPILER, '-std=c99', '-Wall', '-Wextra', '-Werror',
                           '-I' + str(stub), '-I' + str(ROOT / 'car/include'),
                           '-I' + str(ROOT / 'car/config')]
                command += ['-DREMOTE_CONTROL_ENABLED=0']
                command += ['-D' + value for value in definitions]
                command += [str(ROOT / 'car/tests/test_motor_control.c'),
                            str(source), '-o', str(executable)]
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
