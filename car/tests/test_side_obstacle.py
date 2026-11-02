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
        wrapper.write_text('''#include "track_vision.h"
__declspec(dllexport) void reset(void){track_vision_reset();}
__declspec(dllexport) void frame(const unsigned char*p,unsigned ms,unsigned mm,float*v){
track_vision_result_t o;track_vision_process(p,ms,mm,&o);
v[0]=o.valid;v[1]=o.geometry.valid;v[2]=o.geometry.target_right_mm;
v[3]=o.geometry.target_forward_mm;v[4]=o.confidence;v[5]=o.element;v[6]=o.zebra;
for(int r=0;r<120;++r)v[7+r]=o.center[r];}
''', encoding='utf-8')
        dll=SCRATCH/'side.dll'
        result=subprocess.run([find_compiler(),'-std=c99','-O2','-shared','-Wall','-Wextra','-Werror',
            '-I'+str(ROOT/'car/include'),'-I'+str(ROOT/'car/config'),str(wrapper),
            str(ROOT/'car/src/track_vision.c'),'-lm','-o',str(dll)],capture_output=True,text=True,errors='replace')
        if result.returncode: raise AssertionError(result.stderr)
        cls.lib=C.CDLL(str(dll));cls.lib.frame.argtypes=[C.POINTER(C.c_uint8),C.c_uint,C.c_uint,C.POINTER(C.c_float)]

    def setUp(self): self.lib.reset()

    def feed(self, pixels, ms, mm):
        image=(C.c_uint8*22560).from_buffer_copy(pixels);out=(C.c_float*127)()
        self.lib.frame(image,ms,mm,out);return tuple(out)

    def acquire(self, pixels):
        for n in range(4):self.feed(scene(),n*20,n*20)
        return [self.feed(pixels,80+n*20,80+n*20) for n in range(6)]

    def test_left_and_right_bricks_generate_clear_metric_path(self):
        for side in (-1,1):
            for gap in (0,20):
                with self.subTest(side=side,gap=gap):
                    self.lib.reset();o=self.acquire(scene(side,gap))[-1]
                    self.assertEqual(o[:2],(1,1),o[:7])
                    self.assertGreater(-side*o[2],25,o[:7])
                    self.assertLess(-side*o[2],125,o[:7])
                    self.assertEqual(o[6],0)

    def test_no_room_rejects_instead_of_painting_over_brick(self):
        for side in (-1,1):
            self.lib.reset();o=self.acquire(scene(side,depth=340))[-1]
            self.assertEqual(o[0],0,o[:7])

    def test_early_disappearance_does_not_snap_to_center(self):
        o=self.acquire(scene(-1))[-1];self.assertEqual(o[1],1)
        after=self.feed(scene(),220,220)
        self.assertGreater(after[2],25,after[:7])
        for n in range(1,45):after=self.feed(scene(),220+n*20,220+n*20)
        self.assertEqual(after[:2],(1,1));self.assertLess(abs(after[2]),6)

    def test_plain_track_and_bend_do_not_create_false_zebra(self):
        for bend in (0,8,-8):
            self.lib.reset()
            for n in range(10):o=self.feed(scene(bend=bend),n*20,n*20)
            self.assertEqual(o[6],0)
            self.assertEqual(o[:2],(1,1),o[:7])

    def test_clipped_brick_keeps_the_same_side_and_current_clearance(self):
        for side in (-1,1):
            self.lib.reset();self.acquire(scene(side))
            # 车已向空侧移动60mm，砖接近画面底边；避免用不移动的车姿要求近距离急转。
            pixels=scene(side,top=88,bottom=119)
            shifted=bytearray([30])*22560
            for r in range(120):
                delta=round(side*60*max(6,44+(r-30)*123/86)/500)
                for c in range(188):
                    if 0<=c-delta<188:shifted[r*188+c]=pixels[r*188+c-delta]
            o=self.feed(shifted,240,320)
            self.assertEqual(o[:2],(1,1),o[:7])
            self.assertLess(-side*o[2],25,'已移到空侧时不能在窄通道中线之上重复叠加偏移')

    def test_approaching_brick_does_not_stop_when_far_road_edge_leaves_image(self):
        for side in (-1,1):
            for top,bottom in ((75,105),(80,110),(85,115)):
                with self.subTest(side=side,top=top):
                    self.lib.reset();self.acquire(scene(side))
                    pixels=scene(side,top=top,bottom=bottom);shifted=bytearray([30])*22560
                    for r in range(120):
                        delta=round(side*60*max(6,44+(r-30)*123/86)/500)
                        for c in range(188):
                            if 0<=c-delta<188:shifted[r*188+c]=pixels[r*188+c-delta]
                    o=self.feed(shifted,240,320)
                    self.assertEqual(o[:2],(1,1),o[:7])
                    self.assertLess(-side*o[2],25)

    def test_reset_and_repeated_timestamp_do_not_inherit_confirmation(self):
        pixels=scene(-1,gap=20)
        self.feed(pixels,100,0)
        for n in range(5):o=self.feed(pixels,100,0)
        self.assertEqual(o[0],0,'重复帧不能确认留缝砖并授予通行')
        o=self.feed(pixels,120,20);self.assertEqual(o[:2],(1,1),o[:7])
        self.lib.reset();o=self.feed(scene(),140,40)
        self.assertEqual(o[:2],(1,1));self.assertLess(abs(o[2]),6)

    def test_tracking_timeout_does_not_reauthorize_an_old_path(self):
        self.acquire(scene(-1));o=self.feed(scene(),6000,220)
        self.assertEqual(o[0],0,o[:7])

    def test_opposite_obstacle_cannot_flip_a_committed_side(self):
        self.acquire(scene(-1));o=self.feed(scene(1),240,240)
        self.assertEqual(o[0],0,o[:7])

    def test_bright_background_outside_black_edges_is_not_part_of_the_road(self):
        def background(pixels, shade, border):
            for r in range(120):
                width=max(6,44+(r-30)*123/86)
                for c in range(188):
                    if c<93-width/2-border or c>93+width/2+border:pixels[r*188+c]=shade
            return pixels
        for side in (-1,1):
            for gap in (0,20):
                for shade in (153,220):
                    for border in (4,12,20):
                        with self.subTest(side=side,gap=gap,shade=shade,border=border):
                            self.lib.reset()
                            for n in range(4):self.feed(background(scene(),shade,border),n*20,n*20)
                            for n in range(6):o=self.feed(background(scene(side,gap),shade,border),80+n*20,80+n*20)
                            self.assertEqual(o[:2],(1,1),o[:7]);self.assertGreater(-side*o[2],25)

if __name__=='__main__': unittest.main(verbosity=2)
