"""齿比、固件舍入与视觉里程一致性；不借用地图真值做元素判断。"""
import math
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from engine import Simulation
from parameters import defaults


class EncoderScaleTests(unittest.TestCase):
    def test_physical_fifteen_turns_produce_34816_counts(self):
        sim = Simulation(defaults())
        for _ in range(15):
            sim.left_v = sim.right_v = math.pi * .064 / sim.dt
            sim.tick()
        self.assertAlmostEqual(sim.count_total[0], 34816, places=7)

    def test_signed_feedback_matches_firmware_integer_rounding(self):
        sim = Simulation(defaults())
        # 计数余量避免浮点数恰落整数边界，模拟两轮相反方向。
        v = 100.1 * math.pi * .064 / (1024 * 68 / 30) / sim.dt
        sim.left_v, sim.right_v = v, -v
        sim.tick()
        self.assertEqual(sim.measured.tolist(), [866, -866])

    def test_element_distance_uses_encoder_odometry(self):
        sim = Simulation(defaults())
        sim.data[15] = 1234
        sim.distance = 999
        # 仅截获算法边界以检查输入来源；真实渲染仍执行。
        received = []
        sim.native.vision = lambda image, now, distance: received.append(distance)
        sim._frame()
        self.assertEqual(received, [1234])
