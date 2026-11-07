"""16:04实录灰度重建回归：路径方向与有效性，非实车闭环验收。"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import subprocess
import unittest
import zipfile
from host_toolchain import find_compiler

ROOT=Path(__file__).resolve().parents[2]

class RecordedVisionRepairTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        scratch=ROOT/'tmp/vision-repair/recorded-tests'
        scratch.mkdir(parents=True,exist_ok=True)
        wrapper=scratch/'wrapper.c'
        wrapper.write_text('''#include "track_vision.c"
__declspec(dllexport) void reset(void){track_vision_reset();}
__declspec(dllexport) void frame(const unsigned char*g,unsigned ms,unsigned mm,float*v){
track_vision_result_t o;track_vision_process(g,ms,mm,&o);
v[0]=o.element;v[1]=o.has_path;v[2]=o.near_error_px;v[3]=o.far_error_px;
v[4]=obstacle.side;v[5]=obstacle.offset_mm;v[6]=island_seed_side;v[7]=island_seed_y;}
''')
        dll=scratch/'recorded.dll'
        result=subprocess.run([find_compiler(),'-std=c99','-O2','-shared','-Wall','-Wextra','-Werror',
            '-I'+str(ROOT/'car/src'),'-I'+str(ROOT/'car/include'),'-I'+str(ROOT/'car/config'),
            str(wrapper),'-lm','-o',str(dll)],capture_output=True,text=True,errors='replace')
        if result.returncode:raise AssertionError(result.stderr)
        cls.lib=C.CDLL(str(dll))
        cls.lib.frame.argtypes=[C.POINTER(C.c_ubyte),C.c_uint,C.c_uint,C.POINTER(C.c_float)]

    def replay(self,name,speed=800):
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/video1604-regression.zip') as z:
            meta=json.loads(z.read('metadata.json'));info=meta['segments'][name];data=z.read(name+'.gray')
        self.assertEqual(hashlib.sha256(data).hexdigest(),info['sha256'])
        self.lib.reset();v=(C.c_float*16)();rows=[]
        for n,k in enumerate(info['frames']):
            g=(C.c_ubyte*22560).from_buffer_copy(data,n*22560)
            self.lib.frame(g,k*20,(k-info['frames'][0])*speed//50,v)
            rows.append((k/50,tuple(v)))
        return rows

    def test_recorded_ring_has_entry_and_inside_paths(self):
        rows=self.replay('curve_ring')
        for phase in (4,5):
            turn=[v for _,v in rows if v[0]==phase and v[1] and v[3]<0]
            self.assertGreaterEqual(len(turn),3)

    def test_recorded_corners_keep_observed_direction(self):
        for speed in (0,800):
            for scene,times in [('curve_ring',(34.48,35.0)),('curves',(46.76,47.24,53.5,54.02))]:
                rows=self.replay(scene,speed)
                for t in times:
                    _,v=min(rows,key=lambda r:abs(r[0]-t))
                    self.assertTrue(v[1],(scene,speed,t,v))
                    if t<48:self.assertGreater(v[2]-v[3],5,(scene,speed,t,v))
                    else:self.assertGreater(v[3],0,(scene,speed,t,v))
                    if scene=='curve_ring' or t==53.5:self.assertNotEqual(v[0],2,(scene,speed,t,v))

    def test_distant_island_stays_approach(self):
        rows=self.replay('curve_ring')
        distant=[v for t,v in rows if 40<=t<41 and v[6] and v[7]<40]
        self.assertGreater(len(distant),4)
        self.assertTrue(all(v[0]==3 for v in distant))

    def test_recorded_right_brick_selects_left_path(self):
        rows=self.replay('brick');_,v=min(rows,key=lambda r:abs(r[0]-19.28))
        self.assertEqual(v[4],2);self.assertTrue(v[1]);self.assertLess(v[3],0)
