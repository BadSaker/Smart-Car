"""验证单边实测充分时的路径裕量，以及未知/不可见区域的拒绝。"""
import ctypes as C
import os
from pathlib import Path
import subprocess
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[2]
SCRATCH=Path(os.environ.get('AUTOCAR_TEST_ROOT',ROOT/'tmp/field-robustness-2026-10-13/tests'))/'path-margin'
COMPILER=os.environ.get('AUTOCAR_HOST_CC',str(ROOT/'simulation/toolchain/mingw64/bin/gcc.exe'))

class PathMarginTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True,exist_ok=True)
        file=SCRATCH/'wrapper.c'
        file.write_text('''#include "track_vision.h"
static track_vision_result_t o;
__declspec(dllexport) void reset(void){track_vision_reset();}
__declspec(dllexport) void frame(const unsigned char *p,unsigned ms,unsigned mm,int *v){
 track_vision_process(p,ms,mm,&o);v[0]=o.valid;v[1]=o.confidence;v[2]=o.element;
 v[3]=o.near_error_px;v[4]=o.far_error_px;v[5]=o.fault;
}
''',encoding='utf-8')
        dll=SCRATCH/'path-margin.dll'
        build=subprocess.run([COMPILER,'-std=c99','-shared','-O2','-Wall','-Wextra','-Werror',
          '-I'+str(ROOT/'car/include'),'-I'+str(ROOT/'car/config'),str(file),str(ROOT/'car/src/track_vision.c'),'-o',str(dll)],capture_output=True,text=True,errors='replace')
        if build.returncode:raise AssertionError(build.stderr)
        cls.lib=C.CDLL(str(dll));cls.lib.frame.argtypes=[C.POINTER(C.c_uint8),C.c_uint,C.c_uint,C.POINTER(C.c_int)]

    def feed(self,image,ms=0,mm=0):
        values=(C.c_int*6)();pixels=(C.c_uint8*len(image)).from_buffer_copy(image)
        self.lib.frame(pixels,ms,mm,values);return tuple(values)

    def test_measured_outer_corridor_has_path_margin(self):
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/element-sequences.zip') as z:
            pixels=z.read('curve_quality.gray')[:188*120]
        self.lib.reset()
        for i in range(4):result=self.feed(pixels,i*20,i*4)
        self.assertEqual(result[0],1)
        self.assertGreaterEqual(result[1],70,'可靠单边走廊应有独立路径评分，不能被整幅双边行数压到40左右')

    def test_no_geometry_remains_invalid(self):
        for level in (0,153,255):
            self.lib.reset();result=self.feed(bytes([level])*(188*120))
            self.assertEqual(result[0],0)

    def test_black_far_field_is_not_a_high_quality_path(self):
        self.lib.reset();image=bytearray([20])*(188*120)
        for row in range(85,117):
            half=(44+(row-30)*123//86)//2
            image[row*188+93-half:row*188+94+half]=bytes([220])*(2*half+1)
        result=self.feed(image)
        self.assertEqual(result[0],0)

    def test_curved_visible_corridor_can_use_a_closer_forward_window(self):
        pixels=(ROOT/'car/tests/fixtures/visible-short-preview.gray').read_bytes()
        self.lib.reset()
        for i in range(4):result=self.feed(pixels,i*20,i*20)
        self.assertEqual(result[0],1,'连续外边仍在视野内，远处曲线出画面应缩短有效预瞄窗')
        self.assertGreaterEqual(result[1],70)

    @staticmethod
    def cross_image(top=0,bottom=-1):
        image=bytearray([20])*(188*120)
        for row in range(30,117):
            half=(44+(row-30)*123//86)//2
            image[row*188+93-half:row*188+94+half]=bytes([220])*(2*half+1)
        for row in range(top,bottom+1):image[row*188:(row+1)*188]=bytes([220])*188
        return image

    def test_far_cross_preview_budget_includes_approach_distance(self):
        self.lib.reset()
        for i in range(3):self.feed(self.cross_image(48,70),i*20,i*5)
        for i in range(3):self.feed(self.cross_image(60,112),60+i*20,900+i*10)
        for i in range(3):result=self.feed(self.cross_image(),120+i*20,1150+i*10)
        self.assertEqual(result[5]&1,0,'提前看到十字时，到达并越过路口不能用同一个1000mm限制截断')
        self.assertNotEqual(result[2],2)

    def test_near_cross_opening_still_has_a_short_distance_limit(self):
        self.lib.reset()
        for i in range(3):self.feed(self.cross_image(48,70),i*20,i*5)
        self.feed(self.cross_image(60,112),60,100)
        result=self.feed(self.cross_image(60,112),80,801)
        self.assertNotEqual(result[5]&1,0,'近场缺边通行仍需单独限距，不能随提前识别放宽盲区预算')

if __name__=='__main__':unittest.main(verbosity=2)
