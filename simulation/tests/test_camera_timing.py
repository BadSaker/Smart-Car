"""相机调度、延迟和导出里程必须对应实际送入C的图像。"""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from engine import Simulation
from parameters import defaults


class CameraTimingTests(unittest.TestCase):
    def test_startup_ramp_does_not_hide_a_brief_lost_frame(self):
        p=defaults();p.update(scenario='straight',cruise=800,curve_speed=800,outer_speed=800,element_speed=800)
        s=Simulation(p);s.started=True
        for _ in range(180):s.tick()
        self.assertEqual(int(s.data[0]),2)
        previous=s.zero_pwm_ticks
        s.p['blackout']=True;s.tick()
        self.assertEqual(int(s.data[0]),2)  # first bad frame, before latched fault
        self.assertGreater(s.zero_pwm_ticks,previous)
        self.assertLess(s.min_path_margin,0)
        self.assertEqual(s.running_ticks,0)  # speed floor still excludes acceleration

    def test_30hz_does_not_quantize_to_25hz(self):
        p=defaults();p.update(scenario='straight',fps=30)
        s=Simulation(p)
        for _ in range(100):s.tick()
        self.assertEqual(s.frame_id,31)  # reset图像加30张运行帧

    def test_latency_uses_past_pose_but_current_encoder_input(self):
        p=defaults();p.update(scenario='straight',camera_latency_ms=40)
        s=Simulation(p);s.now=100;s.x=9;s.y=8
        s.pose_history.clear();s.pose_history.extend([(40,1.,0.,0.),(50,2.,0.,0.),(60,3.,0.,0.)])
        s.data[15]=1234;s.distance=999
        s._frame();s.capture_frame();m=s.frame_buffer[-1]['meta']
        self.assertEqual(s.frame_pose,(2.,0.,0.))
        self.assertEqual(m['capture_time_ms'],60)
        self.assertEqual(m['vision_distance_mm'],1234)

    def test_tire_corner_margin_on_known_straight(self):
        p=defaults();p.update(scenario='straight')
        s=Simulation(p);s.x=2;s.y=.1;s.h=0;s.steer=0
        self.assertAlmostEqual(s.tire_margin(),.060,delta=.0002)
