"""编译真实适配与协议进行主机验证，只替换硬件接口。"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / 'tmp/camera-debug-tests'
COMPILER = os.environ.get('AUTOCAR_HOST_CC', r'D:\Xilinx\Vivado\2022.2\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe')
TYPES = '''#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef uint8_t uint8; typedef uint16_t uint16; typedef uint32_t uint32;
typedef int32_t int32; typedef volatile uint8 vuint8;
/* MinGW PE弱别名行为与ARM链接器不同；保留真实函数体，
 * 仅主机测试使用普通链接方式。 */
#define ZF_WEAK
#define zf_assert(x) assert(x)
'''
PLATFORM = '''#pragma once
#include "zf_common_typedef.h"
#include "seekfree_assistant.h"
#include "seekfree_assistant_interface.h"
#define MT9V03X_W 188
#define MT9V03X_H 120
#define DEBUG_UART_INDEX 1
#define DEBUG_UART_TX_PIN 12
#define DEBUG_UART_RX_PIN 13
#define DEBUG_UART_USE_INTERRUPT 1
#define WIFI_UART_STATION 0
#define WIFI_UART_COMMAND 0
#define APP_ENABLE_IPS200 1
#define IPS200_TYPE_SPI 0
#define IPS200_CROSSWISE 2
#define IPS200_CROSSWISE_180 3
#define IPS200_8X16_FONT 1
#define RGB565_WHITE 0xffff
#define RGB565_BLACK 0
#define C13 45
#define B16 16
#define B17 17
#define GPI 0
#define GPO 1
#define GPIO_LOW 0
#define GPIO_HIGH 1
#define GPI_PULL_UP 22
#define GPO_PUSH_PULL 33
#define WIFI_SPI_RST_PIN B16
#define WIFI_SPI_INT_PIN B17
void gpio_init(int,int,uint8,uint32);
uint8 gpio_get_level(int);
uint8 wifi_spi_init(char*,char*);
uint8 wifi_spi_socket_disconnect(void);
void system_delay_ms(uint32);
uint8 wifi_spi_socket_connect(char*,char*,char*,char*);
void ips200_init(int);
void ips200_set_dir(int);
void ips200_set_font(int);
void ips200_set_color(uint16,uint16);
void ips200_clear(void);
void ips200_show_string(uint16,uint16,const char*);
void ips200_show_gray_image(uint16,uint16,const uint8*,uint16,uint16,uint16,uint16,uint8);
void uart_init(int,uint32,int,int);
void uart_rx_interrupt(int,uint32);
uint8 wifi_uart_init(char*,char*,int);
uint8 wifi_uart_connect_tcp_servers(char*,char*,int);
'''
SENDERS = ['debug_send_buffer','wifi_uart_send_buffer','wireless_uart_send_buffer',
           'bluetooth_ch9141_send_buffer','wifi_spi_send_buffer','ble6a20_send_buffer']
READERS = ['debug_read_ring_buffer','wifi_uart_read_buffer','wireless_uart_read_buffer',
           'bluetooth_ch9141_read_buffer','wifi_spi_read_buffer','ble6a20_read_buffer']
PLATFORM += '\n'.join('uint32 '+n+'(const uint8*,uint32);' for n in SENDERS)
PLATFORM += '\n' + '\n'.join('uint32 '+n+'(uint8*,uint32);' for n in READERS)
WIFI = ['CAMERA_DEBUG_WIFI_SSID="test-network"',
        'CAMERA_DEBUG_WIFI_PASSWORD="test-password"','CAMERA_DEBUG_WIFI_SERVER_IP="192.168.1.2"',
        'CAMERA_DEBUG_WIFI_SERVER_PORT="8080"','CAMERA_DEBUG_WIFI_LOCAL_PORT="0"']
PROFILES = {
    'spi-gray': WIFI, 'spi-binary': WIFI+['CAMERA_DEBUG_IMAGE_MODE=1'],
    'spi-boundaries': WIFI+['CAMERA_DEBUG_IMAGE_MODE=2'],
    'spi-decimated': WIFI+['CAMERA_DEBUG_FRAME_DIVIDER=2'],
    'wifi-missing-config': ['CAMERA_DEBUG_WIFI_SSID=""','CAMERA_DEBUG_WIFI_SERVER_IP=""','TEST_WIFI_MISSING_CONFIG=1'],
    'wifi-long-config': [*WIFI[:1], 'CAMERA_DEBUG_WIFI_PASSWORD="'+('x'*60)+'"', *WIFI[2:], 'TEST_WIFI_MISSING_CONFIG=1'],
    'wifi-bad-ip': [*WIFI[:2],'CAMERA_DEBUG_WIFI_SERVER_IP="999.1.2.3"',*WIFI[3:],'TEST_WIFI_MISSING_CONFIG=1'],
    'wifi-bad-port': [*WIFI[:3],'CAMERA_DEBUG_WIFI_SERVER_PORT="65536"',WIFI[4],'TEST_WIFI_MISSING_CONFIG=1'],
    'wifi-open': [WIFI[0],'CAMERA_DEBUG_WIFI_PASSWORD=""',*WIFI[2:],'TEST_WIFI_OPEN=1'],
    'disabled': ['CAMERA_DEBUG_ENABLED=0'],
}
SELECTED = None

# 原有协议场景显式开启启动联网；新增场景使用正式的默认关闭配置。
PROFILES = {name: definitions + ['CAMERA_DEBUG_WIFI_BOOT_ENABLED=1', 'REMOTE_CONTROL_ENABLED=0']
            for name, definitions in PROFILES.items()}
PROFILES['wifi-switch'] = WIFI + ['TEST_WIFI_SWITCH=1', 'REMOTE_CONTROL_ENABLED=0']
PROFILES['wifi-switch-invalid'] = ['CAMERA_DEBUG_WIFI_SSID=""',
    'TEST_WIFI_SWITCH=1', 'TEST_WIFI_SWITCH_INVALID=1', 'REMOTE_CONTROL_ENABLED=0']
PROFILES['motor-telemetry'] = WIFI + ['TEST_MOTOR_TELEMETRY=1', 'REMOTE_CONTROL_ENABLED=0']
PROFILES['control-rx'] = WIFI + ['TEST_WIFI_CONTROL_RX=1', 'CAMERA_DEBUG_WIFI_BOOT_ENABLED=1']
PROFILES['remote-display'] = WIFI + ['TEST_REMOTE_DISPLAY=1']


class CameraDebugTests(unittest.TestCase):
    def test_protocol_profiles(self):
        stub = SCRATCH / 'include'
        stub.mkdir(parents=True, exist_ok=True)
        (stub/'zf_common_typedef.h').write_text(TYPES, encoding='utf-8')
        (stub/'camera_platform.h').write_text(PLATFORM, encoding='ascii')
        headers = ['zf_common_headfile.h','zf_common_debug.h','zf_driver_uart.h',
                   'zf_device_wireless_uart.h','zf_device_bluetooth_ch9141.h',
                   'zf_device_wifi_uart.h','zf_device_wifi_spi.h','zf_device_ble6a20.h']
        for name in headers:
            (stub/name).write_text('#include "camera_platform.h"\n', encoding='ascii')
        shutil.copy2(ROOT/'car/library/zf_common/zf_common_fifo.h', stub/'zf_common_fifo.h')
        fifo_source = SCRATCH/'zf_common_fifo.c'
        shutil.copy2(ROOT/'car/library/zf_common/zf_common_fifo.c', fifo_source)
        profiles = PROFILES if not SELECTED else {SELECTED: PROFILES[SELECTED]}
        for name, definitions in profiles.items():
            with self.subTest(profile=name):
                exe = SCRATCH/(name+'.exe')
                command = [COMPILER,'-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                           '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
                           '-I'+str(stub),'-I'+str(ROOT/'car/include'),
                           '-I'+str(ROOT/'car/config'),'-I'+str(ROOT/'car/library/zf_components')]
                command += ['-D'+d for d in definitions]
                command += [str(ROOT/p) for p in ['car/tests/test_camera_debug.c','car/src/camera_debug.c',
                             'car/library/zf_components/seekfree_assistant.c',
                             'car/library/zf_components/seekfree_assistant_interface.c']]
                if (ROOT/'car/src/camera_display.c').exists():
                    command.append(str(ROOT/'car/src/camera_display.c'))
                command += [str(fifo_source),'-o',str(exe)]
                build = subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace')
                self.assertEqual(build.returncode,0,build.stdout+build.stderr)
                scenarios = [5,6,7,8,0,1,2,3,4,9,10,11,12,13,14] if name == 'spi-gray' else [0]
                if name == 'wifi-switch': scenarios = list(range(7))
                for scenario in scenarios:
                    result = subprocess.run([str(exe),str(scenario)],capture_output=True,text=True,encoding='utf-8',errors='replace')
                    self.assertEqual(result.returncode,0,f'{name}/{scenario}: '+result.stdout+result.stderr)
                    print(f'{name}/{scenario}: '+result.stdout.strip())


if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--profile',choices=PROFILES)
    options,remaining=parser.parse_known_args();SELECTED=options.profile
    unittest.main(argv=[__file__]+remaining,verbosity=2)
