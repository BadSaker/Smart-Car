"""执行真实main.c单圈，替代硬件/模块边界，验证服务先于同步图传。"""
from pathlib import Path
import subprocess
import unittest
from host_toolchain import find_compiler

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / 'tmp/sync-control/main-tests'
PLATFORM = '''#pragma once
#include <stdint.h>
#include <string.h>
#define MT9V03X_H 120
#define MT9V03X_W 188
#define PIT_CH0 0
#define PIT_CH1 1
#define SYSTEM_CLOCK_600M 600
#define kStatus_CSI_FrameDone 1
typedef int CSI_Type;
typedef int status_t;
typedef struct csi_handle csi_handle_t;
typedef void (*csi_transfer_callback_t)(CSI_Type*,csi_handle_t*,status_t,void*);
struct csi_handle { csi_transfer_callback_t callback; };
extern csi_handle_t csi_handle;
extern volatile uint8_t mt9v03x_finish_flag;
extern uint8_t mt9v03x_image[120][188];
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t);
void clock_init(int);
void debug_init(void);
void system_delay_ms(uint32_t);
void pit_ms_init(int,int);
void interrupt_global_enable(int);
int mt9v03x_init(void);
'''

class MainLoopTests(unittest.TestCase):
    def test_control_before_full_frame(self):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        (SCRATCH/'zf_common_headfile.h').write_text(PLATFORM, encoding='ascii')
        exe = SCRATCH/'main-loop.exe'
        cmd = [find_compiler(), '-std=c99', '-Wall', '-Wextra', '-Werror',
               '-I'+str(SCRATCH), '-I'+str(ROOT/'car/include'), '-I'+str(ROOT/'car/config'),
               str(ROOT/'car/tests/test_main_loop.c'), '-o', str(exe)]
        build = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
        self.assertEqual(build.returncode, 0, build.stdout+build.stderr)
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
        print(run.stdout.strip())

if __name__ == '__main__':
    unittest.main(verbosity=2)
