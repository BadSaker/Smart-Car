"""单侧红砖的真实灰度输入与最终几何目标回归，不注入识别标志。"""
import ctypes as C
import os
from pathlib import Path
import subprocess
import unittest
from host_toolchain import find_compiler

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = Path(os.environ.get('AUTOCAR_TEST_ROOT', ROOT/'tmp/obstacle-20261102/tests'))/'obstacle'

def scene(side=0, gap=0, depth=180, top=58, bottom=88, bend=0):
    image = bytearray([30])*22560
    for row in range(120):
        width = max(6, 44+(row-30)*123/86)
        center = 93+bend*((116-row)/86)**2
        left, right = center-width/2, center+width/2
        for col in range(max(0, round(left)), min(188, round(right)+1)):
            image[row*188+col] = 220
        if side and top <= row <= bottom:
            lo, hi = (left+gap*width/500, left+depth*width/500) if side < 0 else (right-depth*width/500, right-gap*width/500)
            for col in range(max(0, round(lo)), min(188, round(hi)+1)):
                image[row*188+col] = 40
    return image

class SideObstacleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        wrapper = SCRATCH/'wrapper.c'
        wrapper.write_text('''#include "track_vision.c"
__declspec(dllexport) void reset(void){track_vision_reset();}
__declspec(dllexport) void frame(const unsigned char*p,unsigned ms,unsigned mm,float*v){
track_vision_result_t o;track_vision_process(p,ms,mm,&o);
v[0]=o.has_path;v[1]=o.near_error_px;v[2]=o.far_error_px;v[3]=o.zebra;
for(int r=0;r<120;++r)v[4+r]=o.center[r];
v[124]=obstacle.side;v[125]=obstacle.offset_mm;}
''', encoding='utf-8')
        dll=SCRATCH/'side.dll'
        result=subprocess.run([find_compiler(),'-std=c99','-O2','-shared','-Wall','-Wextra','-Werror',
            '-I'+str(ROOT/'car/include'),'-I'+str(ROOT/'car/config'),'-I'+str(ROOT/'car/src'),str(wrapper),
            '-lm','-o',str(dll)],capture_output=True,text=True,errors='replace')
        if result.returncode: raise AssertionError(result.stderr)
        cls.lib=C.CDLL(str(dll));cls.lib.frame.argtypes=[C.POINTER(C.c_uint8),C.c_uint,C.c_uint,C.POINTER(C.c_float)]

    def setUp(self): self.lib.reset()

    def feed(self, pixels, ms, mm):
        image=(C.c_uint8*22560).from_buffer_copy(pixels);out=(C.c_float*131)()
        self.lib.frame(image,ms,mm,out);return tuple(out)

    def acquire(self, pixels):
        for n in range(4):self.feed(scene(),n*20,n*20)
        return [self.feed(pixels,80+n*20,80+n*20) for n in range(6)]

    def test_left_and_right_bricks_select_opposite_free_side(self):
        for side in (-1,1):
            for gap in (0,20):
                with self.subTest(side=side,gap=gap):
                    self.lib.reset();o=self.acquire(scene(side,gap))[-1]
                    self.assertEqual(o[0],1);self.assertGreater(-side*o[2],3)
                    self.assertEqual(o[3],0);self.assertEqual(o[124],1 if side<0 else 2)

    def test_early_disappearance_holds_then_returns_by_distance(self):
        o=self.acquire(scene(-1))[-1];self.assertEqual(o[0],1)
        after=self.feed(scene(),220,220);self.assertGreater(after[2],3)
        for n in range(1,60):after=self.feed(scene(),220+n*20,220+n*20)
        self.assertEqual(after[0],1);self.assertLess(abs(after[2]),3)

    def test_plain_track_bends_and_reset(self):
        for bend in (0,8,-8):
            self.lib.reset();o=self.feed(scene(bend=bend),0,0)
            self.assertEqual(o[0],1);self.assertEqual(o[3],0);self.assertEqual(o[124],0)
        self.acquire(scene(-1));self.lib.reset();o=self.feed(scene(),0,0)
        self.assertEqual(o[124],0);self.assertLess(abs(o[2]),3)

    def test_detection_is_current_frame_not_a_drive_permission_counter(self):
        for side in (-1,1):
            self.lib.reset();pixels=scene(side,20);o=self.feed(pixels,100,0)
            self.assertEqual(o[0],1);self.assertGreater(-side*o[2],3)
            again=self.feed(pixels,100,0)
            self.assertEqual(o,again)

    def test_reconstructed_pixels_stay_on_white_road(self):
        for side in (-1,1):
            for depth in (180,):
                pixels=scene(side,depth=depth);self.lib.reset();o=self.acquire(pixels)[-1]
                for row in range(30,117):
                    self.assertGreater(pixels[row*188+round(o[4+row])],100,(side,depth,row,o[4+row]))

if __name__=='__main__':unittest.main(verbosity=2)
