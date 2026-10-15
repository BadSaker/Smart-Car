"""复现控制模式跳变、差速绕过加速度限制及短时丢路回中。"""
import ctypes as C
import os,re,subprocess,unittest
from pathlib import Path
from host_toolchain import find_compiler
ROOT=Path(__file__).resolve().parents[2]
SCRATCH=Path(os.environ.get('AUTOCAR_TEST_ROOT',ROOT/'tmp/motion-stability-20261014/tests'))/'motion'

class MotionLimitsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True,exist_ok=True)
        config=(ROOT/'car/config/autonomous_config.h').read_text(encoding='utf-8')
        for macro in ('AUTO_CRUISE_SPEED_MM_S','AUTO_CURVE_SPEED_MM_S','AUTO_CURVE_OUTER_SPEED_MM_S','AUTO_ELEMENT_SPEED_MM_S'):
            config=re.sub(r'(^#define\s+'+macro+r'\s+)[^\r\n]+',lambda m:m[1]+'1200.0f',config,flags=re.M)
        config=re.sub(r'(^#define\s+AUTO_MAX_DUTY_PERMILLE\s+)[^\r\n]+',lambda m:m[1]+'1000U',config,flags=re.M)
        (SCRATCH/'autonomous_config.h').write_text(config,encoding='utf-8')
        code=SCRATCH/'motion.c'
        code.write_text('''#include "autonomous_drive.h"
#include <string.h>
static autonomous_drive_t d;static autonomous_drive_input_t i;
static float lt,rt;
static float observe(control_pid_t *p,float s,float m,float f,float dt){
 if(p==&d.left_pid)lt=s;
 if(p==&d.right_pid)rt=s;
 return control_pid_step(p,s,m,f,dt);
}
#define control_pid_step observe
#include "autonomous_drive.c"
#undef control_pid_step
__declspec(dllexport) void reset(void){autonomous_drive_init(&d);memset(&i,0,sizeof i);lt=rt=0;}
__declspec(dllexport) void tick(unsigned ms,int mode,int valid,int stop,int left,int right,float *v){
 i.now_ms=i.servo_service_ms=ms;++i.feedback_sequence;
 if(ms%20==0){++i.frame_sequence;i.frame_ms=ms;}
 i.ready=i.vision_near_supported=1;i.vision_valid=(unsigned char)valid;i.confidence=95;
 i.servo_generation=d.status.run_generation;i.start_pressed=ms>=160 && ms<240;i.stop_pressed=(unsigned char)stop;
 i.left_speed_mm_s=left;i.right_speed_mm_s=right;
 i.geometry.valid=mode==1 || mode==3;i.geometry.target_right_mm=mode==3?50:200;i.geometry.target_forward_mm=mode==3?400:320;
 i.near_error_px=mode==2?-20:0;i.far_error_px=mode==2?-40:0;
 autonomous_drive_tick(&d,&i);
 v[0]=d.status.steering_permille;v[1]=lt;v[2]=rt;v[3]=d.status.left_duty_permille;v[4]=d.status.right_duty_permille;
 v[5]=d.status.target_speed_mm_s;v[6]=d.status.fault;v[7]=d.status.active;v[8]=d.status.steering_rate_permille_s;
 v[9]=d.status.left_integral;v[10]=d.left_pid.integral;v[11]=d.status.right_integral;v[12]=d.right_pid.integral;
}
''',encoding='utf-8')
        dll=SCRATCH/'motion.dll'
        cmd=[find_compiler(),'-std=c99','-shared','-O2','-Wall','-Wextra','-Werror',
             '-I'+str(SCRATCH),'-I'+str(ROOT/'car/include'),'-I'+str(ROOT/'car/config'),'-I'+str(ROOT/'car/src'),
             str(code),str(ROOT/'car/src/control_pid.c'),'-o',str(dll)]
        p=subprocess.run(cmd,capture_output=True,text=True,errors='replace')
        if p.returncode:raise AssertionError(p.stderr)
        cls.lib=C.CDLL(str(dll));cls.lib.tick.argtypes=[C.c_uint,*([C.c_int]*5),C.POINTER(C.c_float)]

    def tick(self,ms,mode=0,valid=1,stop=0,left=1000,right=1000):
        v=(C.c_float*13)();self.lib.tick(ms,mode,valid,stop,left,right,v);return tuple(v)

    def start(self):
        self.lib.reset()
        for ms in range(0,8000,10):v=self.tick(ms)
        self.assertEqual(v[7],1)
        return v

    def test_geometric_and_pixel_handoff_has_bounded_steering(self):
        prev=self.start()
        for ms in range(8000,8600,10):
            v=self.tick(ms,mode=1 if ms<8300 else 2)
            self.assertLessEqual(abs(v[0]-prev[0]),51,'两种控制模式必须共享转角轨迹')
            prev=v

    def test_each_wheel_target_is_limited_after_turn_allocation(self):
        prev=self.start()
        for ms in range(8000,10200,10):
            v=self.tick(ms,mode=1 if ms<9200 else 0)
            for wheel in (1,2):
                self.assertLessEqual(v[wheel]-prev[wheel],2.01)
                self.assertGreaterEqual(v[wheel]-prev[wheel],-8.01)
            prev=v

    def test_steering_velocity_changes_smoothly_when_target_returns(self):
        prev=self.start()
        for ms in range(8000,9500,10):
            v=self.tick(ms,mode=1 if ms<8180 else 0)
            self.assertLessEqual(abs(v[8]-prev[8]),500.1,'到达目标不能把转向速度突然清零')
            prev=v

    def test_each_motor_pwm_cannot_bypass_rise_limit(self):
        prev=self.start()
        for ms in range(8000,10200,10):
            v=self.tick(ms,mode=1 if ms<9200 else 0)
            for motor in (3,4):self.assertLessEqual(v[motor]-prev[motor],3)
            prev=v

    def test_one_bad_frame_cuts_power_without_a_centering_kick(self):
        self.start()
        for ms in range(8000,8500,10):prev=self.tick(ms,mode=1)
        self.assertNotEqual(prev[0],0)
        bad=self.tick(8500,mode=1,valid=0)
        self.assertEqual(bad[3:5],(0,0));self.assertEqual(bad[0],prev[0]);self.assertEqual(bad[7],1)

    def test_sharp_turn_caps_speed_even_if_all_four_settings_match(self):
        self.start()
        for ms in range(8000,11000,10):v=self.tick(ms,mode=1)
        self.assertLessEqual(v[5],850,'转向大时不能因四档相等失去速度约束')

    def test_pid_diagnostics_follow_reset_on_loss_and_stop(self):
        for stop in (0,1):
            previous=self.start();self.assertNotEqual(previous[9],0)
            v=self.tick(8000,valid=0,stop=stop)
            self.assertEqual(v[9:13],(0,0,0,0))

    def test_stop_and_overspeed_bypass_output_ramps(self):
        self.start();v=self.tick(8000,stop=1)
        self.assertEqual(v[3:5],(0,0));self.assertEqual(v[7],0)
        self.start();v=self.tick(8000,left=2501)
        self.assertEqual(v[3:5],(0,0));self.assertEqual(v[6],6)

    def test_gentle_turn_allocation_does_not_introduce_a_center_speed_dip(self):
        prev=self.start()
        for ms in range(8000,10200,10):
            v=self.tick(ms,mode=3 if ms<9200 else 0)
            self.assertGreaterEqual(v[5],prev[5]-.01,'差速单边限速不能制造减速后再追速')
            prev=v

    def test_pwm_integer_quantization_does_not_bias_steady_speed(self):
        import math
        self.lib.reset();speed=0;last=[]
        for ms in range(0,30000,10):
            v=self.tick(ms,left=round(speed),right=round(speed))
            free=max(0,(v[3]+v[4])*.5-15)*1.25
            speed+=(free-speed)*(1-math.exp(-.01/.18))
            if ms>=29000:last.append(speed)
        self.assertAlmostEqual(sum(last)/len(last),1200,delta=2,msg='PWM取整不应被当作外部限幅反复倒扣积分')
