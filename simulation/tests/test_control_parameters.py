"""验证界面中的元素速度和PWM上限确实进入原生C控制器。"""
import sys
import unittest
import zipfile
import numpy as np
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from parameters import defaults,validate
from engine import Simulation


class ControlParameterTests(unittest.TestCase):
    def test_acceleration_parameter_reaches_real_wheel_targets(self):
        for accel in (0.,500.,1000.):
            p=defaults();p.update(scenario='straight',accel=accel)
            s=Simulation(p);s.started=True
            while int(s.data[0])!=2:s.tick()
            expected=1200 if accel==0 else accel*.01
            self.assertAlmostEqual(s.native.motion['left_target_mm_s'],expected,delta=.01)
            self.assertAlmostEqual(s.native.motion['right_target_mm_s'],expected,delta=.01)
            s.stop_requested=True;s.tick()
            self.assertEqual(max(s.data[9:11]),0)

    def test_outer_and_lateral_parameters_reach_c_controller(self):
        root=Path(__file__).resolve().parents[2]
        with zipfile.ZipFile(root/'car/tests/fixtures/geometric-path-sequences.zip') as z:
            image=np.frombuffer(z.read('curve.gray')[:22560],dtype=np.uint8).reshape(120,188)
        for outer,lateral in [(0,2400.),(1,2400.),(1,100.)]:
            p=defaults();p.update(scenario='straight',curve_speed=710.,outer_speed=650.,outer_enabled=outer,lateral_accel=lateral)
            s=Simulation(p);s.started=True
            for _ in range(200):s.tick()
            for n in range(8):
                now=s.now+(n+1)*20;s.native.vision(image,now,100)
                s.native.tick(now,True,False,False,500,500,5000)
            values,_=s.native.data()
            self.assertEqual(values[6],1)
            if lateral==2400.:self.assertAlmostEqual(values[11],650. if outer else 710.,delta=1)
            else:self.assertLess(values[11],300.)

    def test_configured_pwm_ceiling_limits_actual_c_outputs(self):
        p=defaults();p.update(scenario='straight',cruise=350.,max_duty=80)
        s=Simulation(p);s.started=True
        for _ in range(180):s.tick()
        self.assertEqual(int(s.data[0]),2)
        self.assertLessEqual(max(s.data[9:11]),80)
        self.assertEqual(max(s.data[9:11]),80)

    def test_element_target_reaches_actual_c_controller(self):
        p=defaults();p.update(scenario='straight',cruise=1200.,element_speed=700.)
        s=Simulation(p);s.started=True
        for _ in range(180):s.tick()
        image=s.image.copy();image[48:94,:]=255
        for i in range(1,101):
            now=s.now+i*10
            s.native.vision(image,now,100+i)
            s.native.tick(now,True,False,False,80,80,800)
        values,_=s.native.data()
        self.assertEqual(int(values[6]),2)
        self.assertEqual(int(values[0]),2)
        self.assertEqual(values[11],700.)

    def test_invalid_limits_are_rejected(self):
        for key,value in (('max_duty',0),('max_duty',1001),('max_duty',80.5),('cruise',0),('curve_speed',0),('outer_speed',-1),('element_speed',float('nan')),('lateral_accel',0)):
            p=defaults();p[key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):validate(p)

    def test_speed_inputs_are_not_capped_at_competition_or_low_speed(self):
        for speed in (800.,1200.,3600.,100000.):
            p=defaults();p.update(cruise=speed)
            self.assertEqual(validate(p)['cruise'],speed)
        for speed in (0.,-1.,float('nan'),float('inf'),1e100,1e-100):
            p=defaults();p['cruise']=speed
            with self.assertRaises(ValueError):validate(p)

    def test_1200_target_can_drive_above_300_pwm(self):
        p=defaults();p.update(scenario='straight',cruise=1200.,max_duty=1000,speed_guard=2500)
        s=Simulation(p);s.started=True
        image=s.image.copy()
        # 固定直道图像，轮速仍来自动力模型。起步就伪造1200反馈会使
        # 执行量跟踪正确地减小积分，不能用它检验实际1200档的PWM。
        s.track.camera=lambda *args,**kwargs:image.copy()
        for _ in range(1100):s.tick()
        values=s.data
        self.assertEqual(int(values[0]),2)
        self.assertEqual(values[11],1200.)
        self.assertGreater(min(values[9:11]),300)
        self.assertLessEqual(max(values[9:11]),1000)
        self.assertGreater(s.v*1000,1180)


if __name__=='__main__':unittest.main(verbosity=2)
