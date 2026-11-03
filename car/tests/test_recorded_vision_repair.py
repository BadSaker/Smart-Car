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
v[0]=o.element;v[1]=o.valid;v[2]=o.geometry.valid;v[3]=o.geometry.target_right_mm;
v[4]=o.geometry.target_forward_mm;v[5]=o.raw_near_error_px;v[6]=o.raw_far_error_px;
v[7]=o.near_error_px;v[8]=o.far_error_px;v[9]=obstacle.side;v[10]=obstacle.frames;
v[11]=obstacle.offset_mm;v[12]=obstacle_delta[86];
v[13]=island_seed_side;v[14]=island_seed_x;v[15]=island_seed_y;}
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

    def test_recorded_ring_has_valid_entry_and_inside_paths(self):
        rows=self.replay('curve_ring')
        for phase in (4,5):
            turn=[v for _,v in rows if v[0]==phase and v[1] and v[2] and v[3]<-20]
            self.assertGreaterEqual(len(turn),3,'不能只有状态标志，须有持续有效的左入环轮迹')

    def test_first_recorded_corner_keeps_the_observed_turn(self):
        rows=self.replay('curve_ring')
        for t in (34.48,35.0):
            _,v=min(rows,key=lambda r:abs(r[0]-t))
            self.assertNotEqual(v[0],2)
            self.assertTrue(v[1] and v[2])
            self.assertGreater(v[7]-v[8],10,'左弯不能被固定来向直线代替')
            self.assertLess(abs(v[7]-v[5]),6)

    def test_distant_island_does_not_authorize_the_lower_branch_arc(self):
        rows=self.replay('curve_ring')
        distant=[v for t,v in rows if 40<=t<41 and v[13] and v[15]<40]
        self.assertGreater(len(distant),4)
        self.assertTrue(all(v[0]==3 for v in distant),'远端灰岛不能借下方支路圆弧提前入环')
        entry=[v for _,v in rows if v[0]==4]
        self.assertTrue(entry)
        self.assertEqual(entry[0][13],1,'首个入环必须以当前已关联的左岛边为种子')

    def test_following_corners_recover_with_or_without_odometry(self):
        for speed in (0,800):
            rows=self.replay('curves',speed)
            for t in (46.76,47.24,53.5,54.02):
                _,v=min(rows,key=lambda r:abs(r[0]-t))
                self.assertTrue(v[1] and v[2],(speed,t,v))
                if t<48:
                    self.assertGreater(v[7]-v[8],5,'左弯应保持近远点的转弯方向')
                else:
                    self.assertGreater(v[3],100,'右弯必须保留向右的几何目标')
            _,v=min(rows,key=lambda r:abs(r[0]-53.5))
            self.assertNotEqual(v[0],2,'里程冻结也应撤销已经恢复曲线的暂定十字')

    def test_recorded_brick_confirmation_survives_one_damaged_row_group(self):
        rows=self.replay('brick')
        _,v=min(rows,key=lambda r:abs(r[0]-19.28))
        self.assertEqual(v[9],2)
        self.assertGreaterEqual(v[10],4)
        self.assertTrue(v[1] and v[2])
        self.assertLess(v[3],-25,'右侧砖必须产生向左的几何目标')
