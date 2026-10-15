"""验证差速转向时保留平均驱动力，并保持停车立即撤输出。"""
import ctypes as C
import os,re
from pathlib import Path
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[2]
SCRATCH=Path(os.environ.get('AUTOCAR_TEST_ROOT',ROOT/'tmp/field-robustness-2026-10-13/tests'))/'slew-balance'

class DriveSlewTests(unittest.TestCase):
    def test_turn_change_preserves_mean_duty_and_stop_is_immediate(self):
        SCRATCH.mkdir(parents=True,exist_ok=True)
        config=(ROOT/'car/config/autonomous_config.h').read_text(encoding='utf-8')
        # 固定四档相同，隔离差速动作；不能把正常弯道降速误当平均动力故障。
        for macro in ('AUTO_CRUISE_SPEED_MM_S','AUTO_CURVE_SPEED_MM_S','AUTO_CURVE_OUTER_SPEED_MM_S','AUTO_ELEMENT_SPEED_MM_S'):
            config=re.sub(r'(^#define\s+'+macro+r'\s+)[^\r\n]+',lambda m:m[1]+'300.0f',config,flags=re.M)
        (SCRATCH/'autonomous_config.h').write_text(config,encoding='utf-8')
        source=SCRATCH/'case.c' 
        source.write_text('''#include "autonomous_drive.h"
#include "autonomous_config.h"
#include <string.h>
__declspec(dllexport) void check(float *v) {
 autonomous_drive_t d;autonomous_drive_input_t i;unsigned now;
 autonomous_drive_init(&d);memset(&i,0,sizeof i);
 for(now=0;now<=6080;now+=10) {
  i.now_ms=now;i.ready=1;i.start_pressed=now>=160 && now<240;
  i.frame_ms=now;i.frame_sequence++;i.feedback_sequence++;
  i.servo_service_ms=now;i.servo_generation=d.status.run_generation;
  i.vision_valid=1;i.vision_near_supported=1;i.confidence=100;
  i.left_speed_mm_s=(int)AUTO_CRUISE_SPEED_MM_S;i.right_speed_mm_s=(int)AUTO_CRUISE_SPEED_MM_S;
  i.forward_step_um=1000;
  if(now==6000) {v[0]=(d.status.left_duty_permille+d.status.right_duty_permille)*.5f;i.near_error_px=-20;i.far_error_px=-20;}
  autonomous_drive_tick(&d,&i);
 }
 v[1]=(d.status.left_duty_permille+d.status.right_duty_permille)*.5f;
 v[2]=d.status.left_duty_permille;v[3]=d.status.right_duty_permille;v[4]=d.status.active;
 i.now_ms+=10;i.stop_pressed=1;autonomous_drive_tick(&d,&i);
 v[5]=d.status.left_duty_permille+d.status.right_duty_permille;
}
''',encoding='utf-8')
        dll=SCRATCH/'case.dll';cc=os.environ.get('AUTOCAR_HOST_CC',str(ROOT/'simulation/toolchain/mingw64/bin/gcc.exe'))
        build=subprocess.run([cc,'-shared','-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(SCRATCH),'-I'+str(ROOT/'car/include'),'-I'+str(ROOT/'car/config'),str(source),str(ROOT/'car/src/autonomous_drive.c'),str(ROOT/'car/src/control_pid.c'),'-o',str(dll)],capture_output=True,text=True,errors='replace')
        self.assertEqual(build.returncode,0,build.stderr)
        lib=C.CDLL(str(dll));lib.check.argtypes=[C.POINTER(C.c_float)];v=(C.c_float*6)();lib.check(v)
        self.assertEqual(v[4],1)
        self.assertGreater(v[0],0)
        self.assertGreaterEqual(v[1],v[0]-5,'仅改变差速不应因一升一降的非对称斜率导致平均PWM骤降')
        self.assertNotEqual(v[2],v[3])
        self.assertEqual(v[5],0)

if __name__=='__main__':unittest.main(verbosity=2)
