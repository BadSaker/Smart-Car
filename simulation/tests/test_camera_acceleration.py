"""The accelerated raster sampler must preserve camera pixels, including noise."""
from pathlib import Path
import sys,unittest,math
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from parameters import defaults
from geometry import Track
from native import Native

class CameraAccelerationTests(unittest.TestCase):
    def test_pixels_match_numpy_reference_at_varied_poses_and_optics(self):
        p=defaults();native=Native(p)
        for samples in (1,2,3):
            for distortion in (0.,-.001):
                config={**p,'camera_samples':samples,'k1':distortion,'camera_yaw':3.,'camera_roll':2.}
                track=Track(config)
                for distance,angle in ((0.,0.),(2.,.2),(8.,-.7)):
                    x,y=track.at(distance);h=track.start[2]+angle
                    track.camera_renderer=None;reference=track.camera(x,y,h,config)
                    track.camera_renderer=native.camera;actual=track.camera(x,y,h,config)
                    np.testing.assert_array_equal(actual,reference)
    def test_noise_and_blackout_keep_the_same_rng_sequence(self):
        p=defaults();p.update(noise=3.)
        native=Native(p);track=Track(p);x,y,h=track.start[:3]
        for blackout in (False,True):
            p['blackout']=blackout
            a=np.random.default_rng(42);b=np.random.default_rng(42)
            track.camera_renderer=None;reference=track.camera(x,y,h,p,a)
            track.camera_renderer=native.camera;actual=track.camera(x,y,h,p,b)
            np.testing.assert_array_equal(actual,reference)
            self.assertEqual(a.random(),b.random())
if __name__=='__main__':unittest.main()
