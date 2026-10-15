"""有限接地双轮模型必须保留单轮空转和负载变化，不能平均掉反馈。"""
import importlib,importlib.util,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from parameters import defaults

class WheelDynamicsTests(unittest.TestCase):
    def plant(self,**changes):
        self.assertIsNotNone(importlib.util.find_spec('dynamics'),'需要独立轮速动力学')
        p=defaults();p.update(changes)
        return importlib.import_module('dynamics').WheelDynamics(p)

    def test_unloaded_wheel_can_overspeed_while_chassis_is_slow(self):
        p=self.plant(motor_gain=3.6,traction_left=0.,traction_right=1.)
        for _ in range(150):p.step((1000,100),0,.01)
        self.assertGreater(p.wheel[0],2.5)
        self.assertLess(p.speed,1.)
        self.assertGreater(p.wheel[0]-p.wheel[1],1.)

    def test_asymmetric_motor_output_does_not_produce_identical_encoder_speeds(self):
        p=self.plant()
        for _ in range(200):p.step((600,300),0,.01)
        self.assertGreater(p.wheel[0]-p.wheel[1],.005)
        self.assertGreater(p.speed,0)

    def test_equal_outputs_settle_to_the_configured_motor_curve(self):
        p=self.plant()
        for _ in range(500):p.step((300,300),0,.01)
        self.assertAlmostEqual(p.speed,.35625,delta=.003)
        self.assertAlmostEqual(p.wheel[0],p.wheel[1],delta=1e-7)

    def test_load_release_changes_speed_without_resetting_wheel_state(self):
        p=self.plant();p.p['wheel_load_left']=p.p['wheel_load_right']=500.
        for _ in range(300):p.step((500,500),0,.01)
        loaded=p.speed;p.p['wheel_load_left']=p.p['wheel_load_right']=0
        p.step((500,500),0,.01)
        self.assertGreater(p.speed,loaded);self.assertLess(p.speed-loaded,.03)
        for _ in range(300):p.step((500,500),0,.01)
        self.assertGreater(p.speed,loaded+.1)
