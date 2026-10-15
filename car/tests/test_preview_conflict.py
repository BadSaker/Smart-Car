"""验证近场大偏差与远场分支冲突时的实际控制输出。"""
import ctypes as C
import os
from pathlib import Path
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[2]
SCRATCH=ROOT/'tmp/control-tuning/preview-tests'
COMPILER=os.environ.get('AUTOCAR_HOST_CC',str(ROOT/'simulation/toolchain/mingw64/bin/gcc.exe'))


class PreviewConflictTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True,exist_ok=True)
        wrapper=SCRATCH/'preview_bridge.c'
        wrapper.write_text('''#include "autonomous_drive.h"
#include <string.h>
__declspec(dllexport) int preview(int near_error,int far_error) {
 autonomous_drive_t d;autonomous_drive_input_t i;unsigned now;
 autonomous_drive_init(&d);memset(&i,0,sizeof i);
 for(now=0;now<=2000;now+=10) {
  i.now_ms=now;i.ready=1;i.start_pressed=now>=160 && now<240;
  i.frame_ms=now;i.frame_sequence++;i.feedback_sequence++;
  i.servo_service_ms=now;i.servo_generation=d.status.run_generation;
  i.vision_valid=1;i.vision_near_supported=1;i.confidence=100;
  i.left_speed_mm_s=100;i.right_speed_mm_s=100;i.forward_step_um=1000;
  i.near_error_px=now>=800?near_error:0;i.far_error_px=now>=800?far_error:0;
  autonomous_drive_tick(&d,&i);
 }
 return d.status.active?d.status.steering_permille:-9999;
}
''',encoding='utf-8')
        dll=SCRATCH/'preview-tests.dll'
        command=[COMPILER,'-std=c99','-shared','-O2','-Wall','-Wextra','-Werror','-static-libgcc',
            '-I'+str(ROOT/'car/include'),'-I'+str(ROOT/'car/config'),str(wrapper),
            str(ROOT/'car/src/autonomous_drive.c'),str(ROOT/'car/src/control_pid.c'),'-lm','-o',str(dll)]
        result=subprocess.run(command,capture_output=True,text=True,errors='replace')
        if result.returncode:raise AssertionError(result.stdout+result.stderr)
        cls.lib=C.CDLL(str(dll));cls.lib.preview.argtypes=[C.c_int,C.c_int];cls.lib.preview.restype=C.c_int

    def test_large_near_error_is_not_cancelled_by_opposite_far_branch(self):
        self.assertGreater(self.lib.preview(-29,22),600)
        self.assertLess(self.lib.preview(29,-22),-600)

    def test_small_near_error_keeps_preview_and_straight_response(self):
        self.assertGreater(self.lib.preview(2,-20),200)
        # 两个预瞄误差相同时为20px；按实测转角归一化Kp29，输出应为-580‰。
        self.assertAlmostEqual(self.lib.preview(20,20),-580,delta=2)
        self.assertEqual(self.lib.preview(0,0),0)


if __name__=='__main__':unittest.main(verbosity=2)
