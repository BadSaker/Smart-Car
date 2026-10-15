"""速度环应补偿不同电机增益，稳态转弯目标不应与Ackermann轮速持续冲突。"""
import ctypes as C
import os,re,subprocess,unittest
from pathlib import Path
from host_toolchain import find_compiler
ROOT=Path(__file__).resolve().parents[2]
SCRATCH=Path(os.environ.get('AUTOCAR_TEST_ROOT',ROOT/'tmp/field-robustness-2026-10-13/tests'))/'speed-robustness'


class SpeedRobustnessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True,exist_ok=True)
        cfg=(ROOT/'car/config/autonomous_config.h').read_text(encoding='utf-8')
        for name,value in [('AUTO_CRUISE_SPEED_MM_S','1000.0f'),('AUTO_MAX_DUTY_PERMILLE','1000U'),('AUTO_MAX_MEASURED_SPEED_MM_S','2500')]:
            cfg=re.sub(r'(#define\s+'+name+r'\s+)\S+',lambda m:m[1]+value,cfg)
        (SCRATCH/'autonomous_config.h').write_text(cfg,encoding='utf-8')
        wrapper=SCRATCH/'speed.c';wrapper.write_text('''#include "autonomous_drive.h"
#include "control_pid.h"
#include <string.h>
#include <math.h>
__declspec(dllexport) void run(float gain,int turn,float *result) {
 autonomous_drive_t d;autonomous_drive_input_t i;unsigned now;float speed=0,sum=0,left_before=0,right_before=0;
 autonomous_drive_init(&d);memset(&i,0,sizeof i);i.ready=i.vision_valid=i.vision_near_supported=1;i.confidence=95;
 if(turn){i.geometry.valid=1;i.geometry.target_right_mm=50;i.geometry.target_forward_mm=400;}
 for(now=0;now<12000;now+=10) {
  float target;i.now_ms=i.servo_service_ms=now;++i.feedback_sequence;
  if(now%20==0){++i.frame_sequence;i.frame_ms=now;}
  i.servo_generation=d.status.run_generation;i.start_pressed=now>=160 && now<240;
  /* (50,400)mm目标、190mm轴距、153mm轮距，1m/s时右转外左轮约1047，内右轮约953。 */
  i.left_speed_mm_s=turn?1047:(int)lroundf(speed);i.right_speed_mm_s=turn?953:(int)lroundf(speed);
  if(turn && now==11000){left_before=d.left_pid.integral;right_before=d.right_pid.integral;}
  autonomous_drive_tick(&d,&i);
  target=((d.status.left_duty_permille+d.status.right_duty_permille)*.5f-15)*gain;if(target<0)target=0;
  speed+=(target-speed)*(1-expf(-.01f/.18f));if(now>=11000)sum+=speed;
 }
 result[0]=sum/100;result[1]=d.status.fault;result[2]=d.left_pid.integral-left_before;result[3]=d.right_pid.integral-right_before;
}
''',encoding='utf-8')
        dll=SCRATCH/'speed.dll'
        cmd=[find_compiler(),'-std=c99','-shared','-O2','-Wall','-Wextra','-Werror',
             '-I'+str(SCRATCH),'-I'+str(ROOT/'car/include'),'-I'+str(ROOT/'car/config'),str(wrapper)]
        cmd += [str(ROOT/'car/src'/name) for name in ('autonomous_drive.c','control_pid.c')]
        r=subprocess.run(cmd+['-o',str(dll)],capture_output=True,text=True,errors='replace')
        if r.returncode:raise AssertionError(r.stderr)
        cls.lib=C.CDLL(str(dll));cls.lib.run.argtypes=[C.c_float,C.c_int,C.POINTER(C.c_float)]

    def read(self,gain,turn=0):
        result=(C.c_float*4)();self.lib.run(gain,turn,result);return tuple(result)

    def test_speed_tracks_the_target_across_motor_gains(self):
        for gain in (1.25,2.34375):
            with self.subTest(gain=gain):
                result=self.read(gain)
                self.assertEqual(result[1],0)
                self.assertAlmostEqual(result[0],1000,delta=20)

    def test_steady_turn_does_not_accumulate_opposing_integrals(self):
        result=self.read(1.25,1)
        self.assertEqual(result[1],0)
        self.assertLess(abs(result[2]),1)
        self.assertLess(abs(result[3]),1)
