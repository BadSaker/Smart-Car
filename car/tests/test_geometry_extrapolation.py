"""视野外推断只能在当前轮廓附近延伸，不能凭一个圆接受另一侧道路。"""
import ctypes as C
import os
import subprocess
import unittest
from pathlib import Path
from host_toolchain import find_compiler

ROOT = Path(__file__).resolve().parents[2]
SCRATCH = Path(os.environ.get('AUTOCAR_TEST_ROOT', ROOT/'tmp/field-robustness-2026-10-13/tests'))/'extrapolation'


class ExtrapolationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        source = SCRATCH/'extrapolate.c'
        source.write_text('''#include "track_vision.c"
__declspec(dllexport) int circle(float angle,float radius){
 contour_model_valid=1;contour_model_inner=1;contour_model_error=0;
 contour_model_x=contour_model_y=0;contour_model_radius=500;
 memset(contour_arc_support,0,sizeof contour_arc_support);
 contour_arc_support[0]=1;contour_arc_support[359]=1;
 return vision_model_contains(radius*cosf(angle),radius*sinf(angle));
}
__declspec(dllexport) int pixel_target_in_ring(void){
 static unsigned char gray[188*120];track_vision_result_t o;int row;
 track_vision_reset();memset(&o,0,sizeof o);memset(gray,220,sizeof gray);
 o.valid=1;o.threshold=100;o.element=TRACK_ELEMENT_ROUNDABOUT_INSIDE;
 for(row=0;row<120;++row){o.center[row]=93;o.row_flags[row]=TRACK_ROW_PATH_VALID;}
 vision_metric_pixel_path(gray,&o);return contour_goal_valid;
}
''', encoding='utf-8')
        dll = SCRATCH/'extrapolate.dll'
        result = subprocess.run([find_compiler(), '-std=c99', '-shared', '-O2', '-Wall', '-Wextra', '-Werror',
            '-I'+str(ROOT/'car/src'), '-I'+str(ROOT/'car/include'), '-I'+str(ROOT/'car/config'),
            str(source), '-o', str(dll)], capture_output=True, text=True, errors='replace')
        if result.returncode:
            raise AssertionError(result.stderr)
        cls.lib = C.CDLL(str(dll))
        cls.lib.circle.argtypes = [C.c_float, C.c_float]

    def test_circle_only_accepts_current_or_nearby_arc(self):
        self.assertEqual(self.lib.circle(0, 750), 1)
        self.assertEqual(self.lib.circle(.2, 750), 1)
        self.assertEqual(self.lib.circle(-.2, 750), 1)
        self.assertEqual(self.lib.circle(.5, 750), 0)
        self.assertEqual(self.lib.circle(.313, 750), 0)  # 156.5mm，不能因bin取整越过150限额
        self.assertEqual(self.lib.circle(3.14, 750), 0)

    def test_radial_clearance_still_applies(self):
        self.assertEqual(self.lib.circle(0, 501), 0)
        self.assertEqual(self.lib.circle(0, 999), 0)

    def test_generic_pixel_path_cannot_replace_ring_topology(self):
        self.assertEqual(self.lib.pixel_target_in_ring(),0,'环内轮廓未给出目标时，通用白区中线不能授权直行出环')
