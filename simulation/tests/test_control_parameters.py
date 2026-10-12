"""验证界面中的元素速度和PWM上限确实进入原生C控制器。"""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from parameters import defaults,validate
from engine import Simulation


class ControlParameterTests(unittest.TestCase):
    def test_configured_pwm_ceiling_limits_actual_c_outputs(self):
        p=defaults();p.update(scenario='straight',cruise=350.,max_duty=80)
        s=Simulation(p);s.started=True
        for _ in range(180):s.tick()
        self.assertEqual(int(s.data[0]),2)
        self.assertLessEqual(max(s.data[12:14]),80)
        self.assertEqual(max(s.data[12:14]),80)

    def test_element_speed_changes_the_c_target(self):
        p=defaults();p.update(scenario='straight',element_speed=80.)
        s=Simulation(p);s.started=True
        for _ in range(180):s.tick()
        image=s.image.copy();image[48:94,:]=255
        for i in range(1,101):
            now=s.now+i*10
            s.native.vision(image,now,100+i)
            s.native.tick(now,True,False,False,80,80,800)
        values,_=s.native.data()
        self.assertEqual(int(values[7]),2)
        self.assertEqual(int(values[0]),2)
        self.assertEqual(values[14],80.)

    def test_invalid_limits_are_rejected(self):
        for key,value in (('max_duty',0),('max_duty',301),('max_duty',80.5),('element_speed',0)):
            p=defaults();p[key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):validate(p)


if __name__=='__main__':unittest.main(verbosity=2)
