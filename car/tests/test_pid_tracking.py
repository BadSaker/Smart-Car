"""PID跟踪下游实际执行量，不能只看自身限幅。"""
import ctypes as C
import os,subprocess,unittest
from pathlib import Path
from host_toolchain import find_compiler
ROOT=Path(__file__).resolve().parents[2]
SCRATCH=Path(os.environ.get('AUTOCAR_TEST_ROOT',ROOT/'tmp/motion-stability-20261014/tests'))/'pid-tracking'

class Config(C.Structure):
    _fields_=[(n,C.c_float) for n in ('kp','ki','kd','output_min','output_max','integral_limit','derivative_tau_s')]
class Pid(C.Structure):
    _fields_=[('config',Config),('integral',C.c_float),('derivative',C.c_float),('previous_measurement',C.c_float),
              ('has_previous_measurement',C.c_uint8),('initialized',C.c_uint8),('valid',C.c_uint8)]

class TrackingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(parents=True,exist_ok=True);dll=SCRATCH/'pid.dll'
        p=subprocess.run([find_compiler(),'-shared','-O2','-std=c99','-I'+str(ROOT/'car/include'),
            str(ROOT/'car/src/control_pid.c'),'-o',str(dll)],capture_output=True,text=True)
        if p.returncode:raise AssertionError(p.stderr)
        cls.lib=C.CDLL(str(dll));cls.lib.control_pid_init.argtypes=[C.POINTER(Pid),C.POINTER(Config)]
        cls.lib.control_pid_step.argtypes=[C.POINTER(Pid),*([C.c_float]*4)];cls.lib.control_pid_step.restype=C.c_float

    def test_unrealized_output_does_not_accumulate_positive_integral(self):
        self.assertTrue(hasattr(self.lib,'control_pid_track_output'),'缺少最终执行量的PID跟踪入口')
        track=self.lib.control_pid_track_output;track.argtypes=[C.POINTER(Pid),*([C.c_float]*4)]
        p=Pid();cfg=Config(1,1,0,0,1000,600,0);self.lib.control_pid_init(C.byref(p),C.byref(cfg))
        for _ in range(300):
            request=self.lib.control_pid_step(C.byref(p),100,0,0,.01)
            track(C.byref(p),20,request,10,.01)
        self.assertLess(p.integral,0);self.assertGreaterEqual(p.integral,-600)
        self.assertLess(self.lib.control_pid_step(C.byref(p),100,0,0,.01),35)

    def test_tracking_has_no_effect_when_actuator_matches_request(self):
        self.assertTrue(hasattr(self.lib,'control_pid_track_output'))
        track=self.lib.control_pid_track_output;track.argtypes=[C.POINTER(Pid),*([C.c_float]*4)]
        p=Pid();cfg=Config(1,1,0,0,1000,600,0);self.lib.control_pid_init(C.byref(p),C.byref(cfg))
        request=self.lib.control_pid_step(C.byref(p),100,0,0,.01);before=p.integral
        track(C.byref(p),request,request,10,.01);self.assertEqual(p.integral,before)
