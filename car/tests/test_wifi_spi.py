"""在主机端替代电气 I/O，运行真实的供应商 SPI 传输代码。"""
from pathlib import Path
import os
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / 'tmp/wifi-spi-tests'
COMPILER = os.environ.get('AUTOCAR_HOST_CC', r'D:\Xilinx\Vivado\2022.2\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe')
TYPES = '''#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint8_t uint8; typedef uint16_t uint16; typedef uint32_t uint32;
typedef int32_t int32;
#define zf_assert(x) assert(x)
'''
PLATFORM = '''#pragma once
#include "zf_common_typedef.h"
#define SPI_1 1
#define SPI_MODE3 3
#define SPI1_SCK_D12 12
#define SPI1_MOSI_D14 14
#define SPI1_MISO_D15 15
#define SPI_CS_NULL 0
#define D13 13
#define B16 16
#define B17 17
#define GPO 1
#define GPI 0
#define GPO_PUSH_PULL 0
#define GPI_PULL_DOWN 0
int gpio_get_level(int);
void gpio_low(int); void gpio_high(int);
void gpio_init(int,int,int,int); void gpio_set_level(int,int);
void system_delay_us(uint32); void system_delay_ms(uint32);
void spi_init(int,int,uint32,int,int,int,int);
void spi_transfer_8bit(int,const uint8*,uint8*,uint32);
void spi_write_8bit_array(int,const uint8*,uint32);
'''


class WifiSpiTests(unittest.TestCase):
    def test_transport_boundaries(self):
        stub=SCRATCH/'include'; stub.mkdir(parents=True,exist_ok=True)
        (stub/'zf_common_typedef.h').write_text(TYPES,encoding='ascii')
        (stub/'platform.h').write_text(PLATFORM,encoding='ascii')
        for header in ('zf_common_clock.h','zf_common_debug.h','zf_driver_delay.h',
                       'zf_driver_gpio.h','zf_driver_spi.h','zf_device_type.h'):
            (stub/header).write_text('#include "platform.h"\n',encoding='ascii')
        shutil.copy2(ROOT/'car/library/zf_common/zf_common_fifo.h',stub/'zf_common_fifo.h')
        shutil.copy2(ROOT/'car/library/zf_common/zf_common_fifo.c',SCRATCH/'zf_common_fifo.c')
        exe=SCRATCH/'test_wifi_spi.exe'
        cmd=[COMPILER,'-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
             '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
             '-I'+str(stub),'-I'+str(ROOT/'car/library/zf_device'),
             str(ROOT/'car/tests/test_wifi_spi.c'),str(ROOT/'car/library/zf_device/zf_device_wifi_spi.c'),
             str(SCRATCH/'zf_common_fifo.c'),'-o',str(exe)]
        built=subprocess.run(cmd,capture_output=True,text=True,encoding='utf-8',errors='replace')
        self.assertEqual(built.returncode,0,built.stdout+built.stderr)
        ran=subprocess.run([str(exe)],capture_output=True,text=True,encoding='utf-8',errors='replace')
        self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
        print(ran.stdout.strip())


if __name__=='__main__':
    unittest.main(verbosity=2)
