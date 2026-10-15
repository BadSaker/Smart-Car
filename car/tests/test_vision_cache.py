"""旧路径必须从保存时刻计龄，切换阶段与时钟回绕均不能延长寿命。"""
import ctypes as C
import os
import subprocess
import unittest
from pathlib import Path
from host_toolchain import find_compiler
ROOT=Path(__file__).resolve().parents[2]
SCRATCH=Path(os.environ.get('AUTOCAR_TEST_ROOT',ROOT/'tmp/field-robustness-2026-10-13/tests'))/'vision-cache'


class VisionCacheTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True,exist_ok=True)
        source=SCRATCH/'cache.c'
        source.write_text('''#include "track_vision.c"
__declspec(dllexport) int alive(unsigned saved_ms,unsigned saved_mm,unsigned now,unsigned mm,int exit_phase){
 track_vision_reset();vision.phase=TRACK_ELEMENT_ROUNDABOUT_INSIDE;vision.side=TRACK_SIDE_LEFT;
 vision.ring_path_valid=1;vision.ring_path_saved_ms=saved_ms;vision.ring_path_saved_mm=saved_mm;
 if(exit_phase)vision_change(TRACK_ELEMENT_ROUNDABOUT_EXIT,now-1,mm);
 vision_cache_expire(now,mm);return vision.ring_path_valid;
}
''',encoding='utf-8')
        dll=SCRATCH/'cache.dll'
        result=subprocess.run([find_compiler(),'-std=c99','-shared','-O2','-Wall','-Wextra','-Werror',
            '-I'+str(ROOT/'car/src'),'-I'+str(ROOT/'car/include'),'-I'+str(ROOT/'car/config'),str(source),'-o',str(dll)],capture_output=True,text=True,errors='replace')
        if result.returncode:raise AssertionError(result.stderr)
        cls.lib=C.CDLL(str(dll));cls.lib.alive.argtypes=[C.c_uint,C.c_uint,C.c_uint,C.c_uint,C.c_int]

    def test_exact_boundary_is_still_current(self):
        self.assertEqual(self.lib.alive(100,200,350,360,0),1)

    def test_either_budget_expires_the_path(self):
        self.assertEqual(self.lib.alive(100,200,351,201,0),0)
        self.assertEqual(self.lib.alive(100,200,101,361,0),0)

    def test_exit_transition_does_not_restart_the_clock(self):
        self.assertEqual(self.lib.alive(100,200,351,201,1),0)

    def test_unsigned_clock_wrap_preserves_the_budget(self):
        self.assertEqual(self.lib.alive(0xfffffff0,0xfffffff0,234,144,1),1)
        self.assertEqual(self.lib.alive(0xfffffff0,0xfffffff0,235,144,1),0)
