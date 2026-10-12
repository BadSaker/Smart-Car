"""覆盖实测高度保护、畸变投影、无黑边以及上位机截图输入。"""
import math
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from parameters import defaults, validate
from calibration import fit, comparison, ROWS
from camera_optics import project_ground, ray, straight_width, straight_edges
from geometry import Track
from comparison import Comparison


class CameraCalibrationTests(unittest.TestCase):
    def test_fit_preserves_measured_height_and_input(self):
        p = defaults()
        p.update(height=.297, pitch=53., hfov=120.)
        original = p.copy()
        q = fit(p, 'maximum')
        self.assertEqual(q['height'], .297)
        self.assertEqual(p, original)
        self.assertAlmostEqual(q['pitch'], 31.4689628533, places=7)
        self.assertAlmostEqual(q['hfov'], 89.6695149402, places=7)
        self.assertLessEqual(comparison(q)[2], 3.27)

    def test_fixed_height_fit_recovers_known_camera(self):
        p = defaults()
        p.update(height=.3, cy=60., road_width=.6, fy_ratio=1.)
        # 独立夹具：theta=30度、fx=100时，w(row)=100+sqrt(3)*(row-60)。
        p['reference_widths'] = [100 + math.sqrt(3)*(r-60) for r in ROWS]
        q = fit(p, 'average')
        self.assertEqual(q['height'], .3)
        self.assertAlmostEqual(q['pitch'], 30., places=8)
        self.assertAlmostEqual(q['hfov'], 86.4570605199, places=7)

    def test_incompatible_widths_do_not_change_height_to_hide_error(self):
        p = defaults()
        p.update(height=.8)
        with self.assertRaises(ValueError):
            fit(p)

    def test_distorted_width_agrees_with_projected_road_edges(self):
        p = defaults()
        p.update(height=.297, pitch=31., hfov=90., k1=.01)
        uv, visible = project_ground([[.4, -.25], [.4, .25]], p)
        self.assertTrue(visible.all())
        self.assertAlmostEqual(uv[0, 1], uv[1, 1], places=10)
        self.assertAlmostEqual(straight_width(p, float(uv[0, 1])),
                               abs(uv[0, 0]-uv[1, 0]), places=6)

    def test_pixel_ground_roundtrip_with_distortion_and_pose(self):
        p = defaults()
        p.update(height=.297, pitch=45., hfov=100., k1=.01,
                 camera_yaw=7., camera_roll=-4., camera_lateral=.02)
        pixels = np.array([[0., 60.], [187., 119.], [93., 70.], [40., 40.]])
        d = ray(pixels[:, 0], pixels[:, 1], p)
        points = d * (p['height'] / -d[:, 2])[:, None]
        points[:, 0] += p['camera_offset']
        points[:, 1] += p['camera_lateral']
        actual, visible = project_ground(points[:, :2], p)
        self.assertTrue(visible.all())
        np.testing.assert_allclose(actual, pixels, atol=1e-8, rtol=0)

    def test_folded_distortion_branch_is_not_a_road_boundary(self):
        p=defaults()
        p.update(pitch=53.,hfov=120.,camera_yaw=75.,camera_roll=-40.,
                 k1=-.003,fy_ratio=1.8)
        self.assertIsNone(straight_edges(p,86))

    def test_zero_black_edge_is_a_valid_renderable_scene(self):
        p = defaults()
        p.update(scenario='straight', edge_width=0., noise=0.)
        try:
            validate(p)
        except ValueError as error:
            self.fail(str(error))
        image = Track(p).camera(2., 0., 0., p)
        self.assertNotIn(p['gray_edge'], np.unique(image))
        self.assertGreater(int(image.max()), int(image.min()))

    def test_subpixel_sampling_produces_partial_edge_coverage(self):
        p = defaults()
        p.update(scenario='straight', noise=0., camera_samples=2,
                 camera_sampling='bilinear', pitch=31., hfov=90.)
        image = Track(p).camera(2., .017, .013, p)
        self.assertGreater(len(np.unique(image)), 10)

    def test_seekfree_screenshot_keeps_overlay_mask(self):
        scratch = ROOT.parent/'tmp/camera-realignment/test-inputs'
        scratch.mkdir(parents=True, exist_ok=True)
        # 4倍整数缩放的188x120图，边界线位于第0/187列。
        raw = np.full((120, 188, 3), 140, np.uint8)
        raw[:, 50:140] = 250
        raw[:, 0] = (0, 255, 0)
        raw[:, 187] = (255, 255, 0)
        raw[30:80, 90] = (255, 0, 0)
        raw[45:48, 105:135] = (255, 255, 0)
        screen = Image.new('RGB', (1000, 700), 'white')
        screen.paste(Image.fromarray(raw).resize((752, 480), Image.Resampling.NEAREST), (80, 100))
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            path = Path(directory)/'screenshot.png'
            screen.save(path)
            c = Comparison()
            try:
                c.load_image(path)
            except ValueError as error:
                self.fail(str(error))
            self.assertEqual(c.real.shape, (120, 188))
            self.assertEqual(c.source_kind, 'screenshot')
            self.assertFalse(c.valid_mask[40, 90])
            self.assertTrue(c.valid_mask[40, 120])
            self.assertEqual(int(c.real[40, 120]), 250)
            self.assertTrue(c.valid_mask[90, 120], '文字掩码不能传播到后续所有行')


if __name__ == '__main__':
    unittest.main(verbosity=2)
