"""Historical cross frames: check metric target and real controller output, not only E2."""
import ctypes as C
import json,subprocess,unittest,zipfile
from pathlib import Path
from host_toolchain import find_compiler
ROOT=Path(__file__).resolve().parents[2]
class CrossPathContinuityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        folder=ROOT/'tmp/cross-path-20261107';folder.mkdir(parents=True,exist_ok=True)
        wrapper=folder/'probe.c';wrapper.write_text(r'''#include "track_vision.h"
#include "autonomous_drive.h"
#include <string.h>
static autonomous_drive_t d;static autonomous_drive_input_t i;
__declspec(dllexport) void reset(void){track_vision_reset();autonomous_drive_init(&d);memset(&i,0,sizeof i);i.ready=1;i.left_speed_mm_s=i.right_speed_mm_s=800;for(unsigned n=0;n<100;++n){i.now_ms=n*10;i.start_pressed=n>=5 && n<9;++i.feedback_sequence;i.servo_generation=d.status.run_generation;autonomous_drive_tick(&d,&i);}}
__declspec(dllexport) void frame(const unsigned char*p,unsigned ms,unsigned mm,float*v){track_vision_result_t o;track_vision_process(p,ms,mm,&o);i.now_ms=i.frame_ms=ms+2000;++i.frame_sequence;++i.feedback_sequence;i.servo_generation=d.status.run_generation;i.geometry=o.geometry;i.has_path=o.has_path;i.element=o.element;i.curve_outer_side=o.curve_outer_side;i.near_error_px=o.near_error_px;i.far_error_px=o.far_error_px;autonomous_drive_tick(&d,&i);v[0]=o.element;v[1]=o.geometry.valid;v[2]=o.geometry.target_right_mm;v[3]=o.geometry.target_forward_mm;v[4]=d.status.steering_permille;v[5]=d.status.active;}
''',encoding='utf-8')
        dll=folder/'probe.dll'
        cmd=[find_compiler(),'-std=c99','-O2','-shared','-Wall','-Wextra','-Werror','-I'+str(ROOT/'car/include'),'-I'+str(ROOT/'car/config'),str(wrapper)]
        cmd += [str(ROOT/('car/src/'+name+'.c')) for name in ['track_vision','autonomous_drive','control_pid']]
        r=subprocess.run(cmd+['-lm','-o',str(dll)],capture_output=True,text=True,errors='replace')
        if r.returncode:raise AssertionError(r.stderr)
        cls.lib=C.CDLL(str(dll));cls.lib.frame.argtypes=[C.POINTER(C.c_ubyte),C.c_uint,C.c_uint,C.POINTER(C.c_float)]
    def replay(self,name):
        with zipfile.ZipFile(ROOT/'car/tests/fixtures/video1438-cross-ring.zip') as z:
            info=json.loads(z.read('metadata.json'))['segments'][name];raw=z.read(info['file'])
        self.lib.reset();rows=[]
        for n,m in enumerate(info['frames']):
            pixels=(C.c_ubyte*22560).from_buffer_copy(raw,n*22560);v=(C.c_float*6)()
            self.lib.frame(pixels,m['ms'],m['mm'],v);rows.append((m['ms'],tuple(v)))
        return rows
    def test_visible_left_forward_exit_does_not_become_right_target(self):
        rows=dict(self.replay('20-24.5'))
        # Incoming road and associated exit remain on the left in these frames.
        for ms in (22700,22740,22780):
            with self.subTest(ms=ms):
                v=rows[ms];self.assertEqual(v[0],2);self.assertEqual(v[1],1)
                self.assertLess(v[2],0);self.assertGreater(v[4],0);self.assertEqual(v[5],1)
    def test_cross_reconstruction_does_not_create_large_reverse_spikes(self):
        for scene in ('20-24.5','35-37','40-43'):
            with self.subTest(scene=scene):
                rows=self.replay(scene)
                spikes=[(tb,a[4],b[4]) for (ta,a),(tb,b) in zip(rows,rows[1:])
                        if a[0]==b[0]==2 and a[4]*b[4]<0 and abs(a[4]-b[4])>200]
                self.assertEqual(spikes,[])
