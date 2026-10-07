"""原生联调真实遥控解析、适配、电机与舵机模块，硬件边界由桩替代。"""
from pathlib import Path
import os
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / 'tmp/remote-integration-tests'
COMPILER = os.environ.get('AUTOCAR_HOST_CC', r'D:\Xilinx\Vivado\2022.2\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe')
PLATFORM = '''#ifndef REMOTE_INTEGRATION_PLATFORM_H
#define REMOTE_INTEGRATION_PLATFORM_H
#include <stdint.h>
typedef enum { C6 = 38, C7 = 39, C8 = 40, C9 = 41, C12 = 44,
               C14 = 46, C15 = 47, C30 = 62 } gpio_pin_enum;
typedef enum { GPI = 0, GPO = 1 } gpio_dir_enum;
typedef enum { PWM2_MODULE0_CHA_C6 = 80, PWM2_MODULE1_CHA_C8 = 96,
               PWM4_MODULE2_CHA_C30 = 185 } pwm_channel_enum;
#define GPIO_LOW 0
#define GPIO_HIGH 1
#define GPI_PULL_UP 22
#define GPO_PUSH_PULL 33
#define PWM_DUTY_MAX 10000U
void gpio_init(gpio_pin_enum, gpio_dir_enum, uint8_t, uint32_t);
uint8_t gpio_get_level(gpio_pin_enum);
void gpio_set_level(gpio_pin_enum, uint8_t);
void pwm_init(pwm_channel_enum, uint32_t, uint32_t);
void pwm_set_duty(pwm_channel_enum, uint32_t);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t);
#endif
'''
CAMERA = '''#ifndef AUTOCAR_CAMERA_DEBUG_H
#define AUTOCAR_CAMERA_DEBUG_H
#include <stdint.h>
typedef enum { CAMERA_DEBUG_DISABLED, CAMERA_DEBUG_READY,
               CAMERA_DEBUG_CONFIG_ERROR, CAMERA_DEBUG_INIT_ERROR,
               CAMERA_DEBUG_CONNECT_ERROR, CAMERA_DEBUG_SEND_ERROR,
               CAMERA_DEBUG_INITIALIZING, CAMERA_DEBUG_CONNECTING } camera_debug_status_t;
extern volatile camera_debug_status_t camera_debug_status;
extern volatile uint32_t camera_debug_error_count;
extern volatile uint32_t camera_debug_reconnect_count;
int32_t camera_debug_read_control(uint8_t *, uint32_t);
#endif
'''


class RemoteIntegrationTests(unittest.TestCase):
    def test_real_remote_pipeline(self):
        """错误的模块交接、油门续期、重连门控或S3锁存会导致实际输出断言失败。"""
        stub = SCRATCH / 'include'
        stub.mkdir(parents=True, exist_ok=True)
        (stub / 'remote_integration_platform.h').write_text(PLATFORM, encoding='ascii')
        for name in ['zf_driver_gpio.h', 'zf_driver_pwm.h']:
            (stub / name).write_text('#include "remote_integration_platform.h"\n', encoding='ascii')
        (stub / 'camera_debug.h').write_text(CAMERA, encoding='ascii')
        executable = SCRATCH / 'remote-integration.exe'
        command = [COMPILER, '-std=c99', '-Wall', '-Wextra', '-Werror',
                   '-DREMOTE_CONTROL_ENABLED=1', '-I' + str(stub),
                   '-I' + str(ROOT / 'car/include'), '-I' + str(ROOT / 'car/config')]
        command += [str(ROOT / 'car/tests/test_remote_integration.c')]
        command += [str(ROOT / 'car/src' / name) for name in
                    ['remote_command.c', 'remote_control.c', 'motor_control.c', 'servo_control.c']]
        command += ['-o', str(executable)]
        build = subprocess.run(command, capture_output=True, text=True,
                               encoding='utf-8', errors='replace')
        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
        result = subprocess.run([str(executable)], capture_output=True, text=True,
                                encoding='utf-8', errors='replace')
        print(result.stdout.strip())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
