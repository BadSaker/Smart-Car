import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from engine import Simulation
from parameters import defaults
from render import chart_image
class ChartTests(unittest.TestCase):
 def test_two_recorded_frames_render_without_removed_quality_field(self):
  p=defaults();p['scenario']='straight';s=Simulation(p)
  for _ in range(30):s.tick()
  self.assertGreaterEqual(len(s.history),2)
  self.assertEqual(chart_image(s).size,(376,175))

 def test_zero_pwm_is_counted_even_without_a_path_observation(self):
  p=defaults();p.update(scenario='straight',blackout=True,speed_kp=0.,speed_ki=0.,speed_ff=0.)
  s=Simulation(p);s.started=True
  for _ in range(100):s.tick()
  self.assertEqual(int(s.data[0]),2)
  self.assertGreater(s.zero_pwm_ticks,0)
  self.assertEqual(s.result()['stability_10ms']['zero_pwm_ticks'],s.zero_pwm_ticks)
