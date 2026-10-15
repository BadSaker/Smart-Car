"""到达终点不能掩盖低速恢复、路径擦线或瞬间撤输出。"""
import copy,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from parameters import defaults
from tools.validate_control import run_is_acceptable

class AcceptanceTests(unittest.TestCase):
    result={'completed':True,'max_outside_wheels':0,'fault':'无','min_tire_corner_margin_mm':20,
            'max_error_m':.13,'max_route_deviation_m':.19,'distance_m':29.8,'route_length_m':30,
            'dynamics_model':'independent','motion_limits':{'max_command_step':50,'max_target_rise':2,'max_target_fall':8,'max_pwm_rise':3},
            'stability_10ms':{'running_ticks':100,'zero_pwm_ticks':0,'min_path_margin':30,'min_speed_mm_s':799.8}}

    def test_small_speed_quantization_is_accepted(self):
        self.assertTrue(run_is_acceptable(self.result,defaults()))

    def test_reaching_finish_does_not_hide_a_failed_run(self):
        for key,value in [('zero_pwm_ticks',1),('min_speed_mm_s',65),('min_path_margin',1),('running_ticks',0)]:
            r=copy.deepcopy(self.result);r['stability_10ms'][key]=value
            with self.subTest(key=key):self.assertFalse(run_is_acceptable(r,defaults()))
        r=copy.deepcopy(self.result);r['min_tire_corner_margin_mm']=-1
        self.assertFalse(run_is_acceptable(r,defaults()))

    def test_completion_does_not_hide_steering_or_wheel_target_jumps(self):
        for key,value in [('max_command_step',900),('max_target_rise',257),('max_target_fall',257),('max_pwm_rise',15)]:
            r=copy.deepcopy(self.result);r['motion_limits'][key]=value
            with self.subTest(key=key):self.assertFalse(run_is_acceptable(r,defaults()))

    def test_legacy_model_is_not_accepted_as_independent_wheel_validation(self):
        r=copy.deepcopy(self.result);r['dynamics_model']='legacy'
        self.assertFalse(run_is_acceptable(r,defaults()))

    def test_wrong_branch_or_an_extra_loop_is_not_a_valid_lap(self):
        for key,value in [('max_error_m',5.6),('distance_m',53.)]:
            r=copy.deepcopy(self.result);r[key]=value
            with self.subTest(key=key):self.assertFalse(run_is_acceptable(r,defaults()))
