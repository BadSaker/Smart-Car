import math,unittest,sys,json
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from parameters import defaults,fit_camera,validate
from geometry import Track,integrate_pose,predicted_width
from engine import Simulation
from native import Native

class ModelTests(unittest.TestCase):
    def test_route_geometry_and_reverse(self):
        p=defaults();p['scenario']='midterm';t=Track(p)
        self.assertAlmostEqual(t.length,5.45+2*math.pi,places=12)
        np.testing.assert_allclose(t.start[:2],[1.95,3.5],atol=1e-12)
        np.testing.assert_allclose(t.end[:2],[3,0],atol=1e-12)
        for a,b in zip(t.parts[:-1],t.parts[1:]):
            xy,h=t.evaluate(a,np.array([a['length']]))
            np.testing.assert_allclose(xy[0],[b['x'],b['y']],atol=1e-12)
            self.assertAlmostEqual(float(h[0]),b['h'],places=12)
        p['direction']='B→A';rev=Track(p)
        np.testing.assert_allclose(rev.start[:2],t.end[:2]);np.testing.assert_allclose(rev.end[:2],t.start[:2])
        self.assertAlmostEqual(rev.start[3],0);self.assertAlmostEqual(rev.end[3],t.length)
    def test_constant_turn_exact_circle(self):
        pose=(0.,0.,0.);L=.2;R=.5;v=.15;dt=.01;delta=math.atan(L/R)
        for _ in range(1000):pose=integrate_pose(*pose,v,delta,L,dt)
        expected_h=v/R*10
        np.testing.assert_allclose(pose,[R*math.sin(expected_h),R*(1-math.cos(expected_h)),expected_h],atol=1e-11)
    def test_full_track_closes_and_keeps_midterm_markers(self):
        p=defaults();t=Track(p)
        np.testing.assert_allclose(t.start[:2],t.end[:2],atol=1e-12)
        self.assertAlmostEqual(t.ab_length,5.45+2*math.pi,places=12)
        self.assertAlmostEqual(t.closure_gap,.2215728752538095,places=10)
        self.assertEqual(len(t.extra_paths),1)
        self.assertGreater(t.length,30)
        np.testing.assert_allclose(t.marker_b[:2],[3,0],atol=1e-12)
        p['direction']='B→A';rev=Track(p)
        np.testing.assert_allclose(rev.start[:2],t.marker_b[:2],atol=1e-12)
        np.testing.assert_allclose(rev.start[:2],rev.end[:2],atol=1e-12)
        self.assertTrue(np.all(np.diff(rev.samples[:,3])>0))
    def test_fit_constraints_and_nominal_conflict(self):
        p=defaults();p.update(height=.27,**{k:v for k,v in fit_camera().items() if k in ('pitch','hfov')})
        self.assertAlmostEqual(predicted_width(p,30),44,places=10)
        self.assertAlmostEqual(predicted_width(p,116),167,places=10)
        self.assertAlmostEqual(p['pitch'],39.4368141872,places=8)
        p.update(pitch=20,hfov=130);self.assertIsNone(predicted_width(p,30))
        with self.assertRaises(ValueError):fit_camera(height=1)
    def test_projected_straight_image_width(self):
        p=defaults();p.update(scenario='straight',height=.3,road_width=.6,pitch=30.,
                              hfov=86.4570605199,cy=60.)
        t=Track(p);image=t.camera(2,0,0,p)
        for row,target in [(30,48.038475773),(60,100.),(86,145.033320997)]:
            road=np.flatnonzero(image[row]>(p['gray_road']+p['gray_background'])/2)
            self.assertLessEqual(abs(len(road)-target),3)
    def test_native_visual_start_and_camera_loss(self):
        p=defaults();p['scenario']='straight';s=Simulation(p);s.started=True
        for _ in range(180):s.tick()
        self.assertEqual(s.data[0],2);self.assertGreater(s.distance,.05);self.assertGreater(s.data[3],80)
        self.assertLess(abs(s.h),.05)
        s.p['drop_camera']=True
        for _ in range(20):s.tick()
        self.assertEqual(s.data[1],2);self.assertEqual(s.data[12],0);self.assertEqual(s.data[13],0)
    def test_native_feedback_loss(self):
        p=defaults();p['scenario']='straight';s=Simulation(p);s.started=True
        for _ in range(180):s.tick()
        s.p['drop_feedback']=True
        for _ in range(8):s.tick()
        self.assertEqual(s.data[1],4)
    def test_black_image_and_start_gate(self):
        p=defaults();p['scenario']='straight';s=Simulation(p);s.started=True
        for _ in range(180):s.tick()
        s.p['blackout']=True
        for _ in range(20):s.tick()
        self.assertEqual(s.data[1],3);self.assertEqual(s.data[12],0)
        p['blackout']=True;s=Simulation(p);s.started=True
        for _ in range(100):s.tick()
        self.assertEqual(s.data[1],12);self.assertEqual(s.distance,0)
    def test_bad_parameters_rejected(self):
        for key,value in [('height',0),('hfov',180),('kp',float('nan')),('outer_enabled',.5),('min_q',42.5)]:
            p=defaults();p[key]=value
            with self.assertRaises(ValueError):validate(p)
    def test_full_finish_uses_progress_not_nominal_distance(self):
        p=defaults();p['controller']='reference';s=Simulation(p);s.started=True
        self.assertFalse(s.completed)
        s.x,s.y,s.h=s.track.end[:3];s.route_progress=s.track.length-.001
        s.distance=.98*s.track.length;s.v=.12;s.tick()
        self.assertTrue(s.completed)
    def test_ab_markers_do_not_enter_camera_image(self):
        p=defaults();t=Track(p);image=t.camera(*t.start[:3],p)
        p['show_ab']=False;np.testing.assert_array_equal(image,t.camera(*t.start[:3],p))
    def test_native_steering_corrects_offset(self):
        p=defaults();p.update(scenario='straight',offset=.05);s=Simulation(p);s.started=True
        initial=abs(s.track.nearest(s.x,s.y)[1])
        for _ in range(400):s.tick()
        final=abs(s.track.nearest(s.x,s.y)[1])
        self.assertLess(final,initial);self.assertNotEqual(s.max_error,0)

if __name__=='__main__':unittest.main(verbosity=2)
