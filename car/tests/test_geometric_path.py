"""几何目标的控制单位、冻结灰度序列及缺路拒绝回归。"""
import ctypes as C
import json
import os
import subprocess
import unittest
import zipfile
from pathlib import Path
from host_toolchain import find_compiler

ROOT=Path(__file__).resolve().parents[2]
SCRATCH=Path(os.environ.get('AUTOCAR_TEST_ROOT',ROOT/'tmp/field-robustness-2026-10-13/tests'))/'geometry'


class GeometricPathTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True,exist_ok=True)
        wrapper=SCRATCH/'geometry.c'
        wrapper.write_text('''#include "track_vision.h"
#include "autonomous_drive.h"
#include <string.h>
__declspec(dllexport) void reset(void){track_vision_reset();}
__declspec(dllexport) void frame(const unsigned char *p,unsigned ms,unsigned mm,float *v){
 track_vision_result_t o;track_vision_process(p,ms,mm,&o);
 v[0]=o.valid;v[1]=o.confidence;v[2]=o.element;v[3]=o.geometry.valid;
 v[4]=o.geometry.target_right_mm;v[5]=o.geometry.target_forward_mm;v[6]=o.fault;
}
__declspec(dllexport) void drive(float x,float y,int pixel,int geometry,int ticks,float *v){
 autonomous_drive_t d;autonomous_drive_input_t i;unsigned now;
 autonomous_drive_init(&d);memset(&i,0,sizeof i);
 i.ready=1;i.vision_valid=1;i.confidence=95;i.vision_near_supported=1;
 i.left_speed_mm_s=i.right_speed_mm_s=100;
 for(now=0;now<1200U+(unsigned)ticks*10U;now+=10){
  i.now_ms=i.frame_ms=i.servo_service_ms=now;++i.frame_sequence;++i.feedback_sequence;
  i.servo_generation=d.status.run_generation;i.start_pressed=now>=160 && now<240;
  if(now>=1200){i.geometry.valid=(unsigned char)geometry;i.geometry.target_right_mm=x;i.geometry.target_forward_mm=y;
   i.near_error_px=i.far_error_px=(int16_t)pixel;}
  autonomous_drive_tick(&d,&i);
 }
 v[0]=d.status.steering_permille;v[1]=d.status.active;v[2]=d.status.left_duty_permille;
 v[3]=d.status.right_duty_permille;v[4]=d.status.fault;v[5]=d.status.path_reason;
 v[6]=d.status.requested_steering_permille;
}
''',encoding='utf-8')
        dll=SCRATCH/'geometry.dll'
        cmd=[find_compiler(),'-std=c99','-shared','-O2','-Wall','-Wextra','-Werror',
             '-I'+str(ROOT/'car/include'),'-I'+str(ROOT/'car/config'),str(wrapper)]
        cmd += [str(ROOT/'car/src'/name) for name in ('track_vision.c','autonomous_drive.c','control_pid.c')]
        result=subprocess.run(cmd+['-o',str(dll)],capture_output=True,text=True,errors='replace')
        if result.returncode:raise AssertionError(result.stderr)
        cls.lib=C.CDLL(str(dll));cls.lib.frame.argtypes=[C.POINTER(C.c_uint8),C.c_uint,C.c_uint,C.POINTER(C.c_float)]
        cls.lib.drive.argtypes=[C.c_float,C.c_float,C.c_int,C.c_int,C.c_int,C.POINTER(C.c_float)]

    def feed(self,pixels,ms=0,mm=0):
        v=(C.c_float*7)();image=(C.c_uint8*len(pixels)).from_buffer_copy(pixels)
        self.lib.frame(image,ms,mm,v);return tuple(v)

    def control(self,x,y,pixel=0,geometry=1,ticks=1):
        v=(C.c_float*7)();self.lib.drive(x,y,pixel,geometry,ticks,v);return tuple(v)

    def test_geometry_steering_uses_mm_and_overrides_pixel_bias(self):
        # 190mm轴距、目标(50,400)mm：atan(2*190*50/(50²+400²))约6.67度。
        right=self.control(50,400,pixel=-80)
        left=self.control(-50,400,pixel=80)
        self.assertEqual(right[1],1);self.assertEqual(left[1],1)
        # 满幅前轮角实测34.743度；目标约6.67度对应192‰，实际输出逐步到达。
        self.assertAlmostEqual(right[6],-192,delta=2)
        self.assertAlmostEqual(left[6],192,delta=2)
        self.assertLess(abs(right[0]),abs(right[6]))
        self.assertAlmostEqual(self.control(50,400,pixel=-80,ticks=60)[0],-192,delta=2)
        self.assertEqual(self.control(0,400)[0],0)

    def test_invalid_geometry_is_not_replaced_by_a_straight_command(self):
        for x,y in ((0,0),(0,-400),(float('nan'),400),(0,float('inf'))):
            with self.subTest(x=x,y=y):
                first=self.control(x,y)
                self.assertEqual(first[2:4],(0,0))
                self.assertNotEqual(first[5],0)
                final=self.control(x,y,ticks=3)
                self.assertEqual(final[4],3)  # AUTO_FAULT_PATH_LOST

    def test_gray_curve_has_a_current_metric_target(self):
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/geometric-path-sequences.zip') as z:
            pixels=z.read('curve.gray')[:188*120]
        self.lib.reset()
        for n in range(4):v=self.feed(pixels,n*20,n*20)
        self.assertEqual(v[0],1);self.assertEqual(v[3],1)
        self.assertGreaterEqual((v[4]**2+v[5]**2)**.5,320)
        self.assertGreater(v[5],0)

    def test_straight_and_cross_paths_use_the_same_metric_target(self):
        # 恒定40mm地面横移，像素偏差随透视宽度变化，不能直接混成另一个舵角比例。
        for cross in (False,True):
            self.lib.reset();pixels=bytearray([20])*22560
            for row in range(120):
                width=max(6,44+(row-30)*123/86);center=93+40*width/500
                for col in range(max(0,round(center-width/2)),min(188,round(center+width/2)+1)):
                    pixels[row*188+col]=220
            for n in range(4):self.feed(pixels,n*20,n*5)
            if cross:pixels[48*188:71*188]=bytes([220])*(23*188)
            for n in range(4):v=self.feed(pixels,80+n*20,20+n*5)
            with self.subTest(cross=cross):
                self.assertEqual(v[0],1);self.assertEqual(v[3],1,'当前像素走廊也应提交经核验的毫米目标')
                self.assertAlmostEqual(v[4],40,delta=6)
                self.assertAlmostEqual(v[5],378,delta=20)

    def test_curve_geometry_cannot_bypass_a_black_forward_band(self):
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/geometric-path-sequences.zip') as z:
            pixels=z.read('curve.gray')[:22560]
        self.lib.reset()
        for n in range(4):v=self.feed(pixels,n*20,n*20)
        self.assertEqual(v[3],1)
        blocked=bytearray(pixels)
        for row in range(73,78):blocked[row*188+103:row*188+108]=bytes(5)
        v=self.feed(blocked,80,80)
        self.assertEqual(v[0],0,'普通弯道的几何目标也必须通过前向缺路检查')

    def test_frozen_ring_sequences_reach_exit_with_current_geometry(self):
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/geometric-path-sequences.zip') as z:
            meta=json.loads(z.read('metadata.json'))
            for name in ('ring_ab','ring_ba'):
                self.lib.reset();raw=z.read(name+'.gray');phases=set();geometric=0;bad=0;longest_bad=0
                for n,sample in enumerate(meta[name]['frames']):
                    v=self.feed(raw[n*22560:(n+1)*22560],sample['ms'],sample['mm'])
                    phases.add(int(v[2]));geometric+=bool(v[3])
                    bad=bad+1 if 4<=v[2]<=6 and not v[0] else 0
                    longest_bad=max(longest_bad,bad)
                with self.subTest(name=name):
                    self.assertTrue({4,5,6}.issubset(phases))
                    self.assertGreater(geometric,50)
                    self.assertLess(longest_bad,3,'出口可见横向轮廓不能因水平扫线缺边而连续失效')

    def test_30hz_exit_uses_current_verified_path_instead_of_raw_row_score(self):
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/ring-exit-30hz.zip') as z:
            frames=json.loads(z.read('metadata.json'))['frames'];raw=z.read('frames.gray')
        self.lib.reset();phases=set()
        for n,m in enumerate(frames):
            v=self.feed(raw[n*22560:(n+1)*22560],m['ms'],m['mm']);phases.add(int(v[2]))
        self.assertTrue({4,5,6}.issubset(phases),'可见出口不能因30Hz下第三帧原始双边Q降低而漏掉')

    def test_far_ambiguous_cross_can_be_corrected_by_current_ring_evidence(self):
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/provisional-cross-ring.zip') as z:
            frames=json.loads(z.read('metadata.json'))['frames'];raw=z.read('frames.gray')
        self.lib.reset();phases=[]
        for n,m in enumerate(frames):
            v=self.feed(raw[n*22560:(n+1)*22560],m['ms'],m['mm']);phases.append(int(v[2]))
        self.assertIn(2,phases,'保留远处双侧拓宽的早期通行处理')
        self.assertIn(3,phases,'远场暂定十字不得屏蔽后来连续出现的真实分叉/岛边')
        self.assertIn(4,phases)

    def test_near_cross_confirmation_blocks_later_ring_reinterpretation(self):
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/provisional-cross-ring.zip') as z:
            frames=json.loads(z.read('metadata.json'))['frames'];raw=z.read('frames.gray')
        self.lib.reset();strong=None
        for n,m in enumerate(frames):
            pixels=raw[n*22560:(n+1)*22560]
            if m['ms']<=24110:v=self.feed(pixels,m['ms'],m['mm']);last=m
            if m['ms']==24310:strong=pixels
        self.assertEqual(v[2],2);self.assertIsNotNone(strong)
        cross=bytearray(strong);cross[60*188:113*188]=bytes([255])*(53*188)
        self.feed(cross,last['ms']+20,last['mm']+16)
        for n in range(3):
            v=self.feed(strong,last['ms']+40+n*20,last['mm']+32+n*16)
            self.assertLess(v[2],3,'已经看到近场十字时，旁侧分支不能再诱发入环')

    def test_black_front_and_uniform_boards_do_not_acquire_geometry(self):
        for shade in (0,153,255):
            self.lib.reset();v=self.feed(bytes([shade])*22560)
            self.assertEqual((v[0],v[3]),(0,0))
        image=bytearray([20])*22560
        for row in range(85,117):
            half=(44+(row-30)*123//86)//2
            image[row*188+93-half:row*188+94+half]=bytes([220])*(2*half+1)
        self.lib.reset();v=self.feed(image)
        self.assertEqual((v[0],v[3]),(0,0))

    def test_black_band_inside_the_forward_sweep_is_not_averaged_away(self):
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/geometric-path-sequences.zip') as z:
            meta=json.loads(z.read('metadata.json'))['ring_ab']['frames'];raw=z.read('ring_ab.gray')
        self.lib.reset()
        for n,sample in enumerate(meta):
            pixels=raw[n*22560:(n+1)*22560]
            v=self.feed(pixels,sample['ms'],sample['mm'])
            if sample['ms']>=22300 and v[2]==5 and v[3]:break
        self.assertEqual(v[3],1)
        blocked=bytearray(pixels);blocked[80*188:86*188]=bytes(6*188)
        v=self.feed(blocked,sample['ms']+20,sample['mm']+24)
        self.assertEqual(v[0],0,'前向轨迹中的完整黑带不能被其他白点或旧路径的高分抵消')


if __name__=='__main__':unittest.main(verbosity=2)
