"""Compare backed-up visuals and test bounded cache invalidation."""
import json,sys,time,types,zipfile,statistics
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT))
from parameters import defaults
from engine import Simulation
from render import map_image,scene_image
with zipfile.ZipFile(ROOT/'backups/before-ui-performance-20261011.zip') as z:
    old=types.ModuleType('old_render');exec(compile(z.read('render.py'),'old_render.py','exec'),old.__dict__)
s=Simulation(defaults());s.started=True
for _ in range(1000):s.tick()
report={}
for name,fn in [('2d',map_image),('3d',scene_image)]:
    before=np.asarray(getattr(old,'map_image' if name=='2d' else 'scene_image')(s))
    after=np.asarray(fn(s));report[name+'_changed_pixels']=int(np.count_nonzero(np.any(before!=after,axis=2)))
    assert report[name+'_changed_pixels']==0,(name,report)
    saved=s.p['show_ab'];s.p['show_ab']=False
    assert not np.array_equal(after,np.asarray(fn(s)));s.p['show_ab']=saved
    assert np.array_equal(after,np.asarray(fn(s)))
    assert fn(s,(620,480)).size==(620,480)
times=[]
for yaw in np.linspace(-60,-40,40):
    start=time.perf_counter();scene_image(s,yaw=float(yaw));times.append((time.perf_counter()-start)*1000)
report['rotating_3d_median_ms']=statistics.median(times);report['rotating_3d_p95_ms']=sorted(times)[37]
times=[]
for _ in range(40):
    s.tick();start=time.perf_counter();scene_image(s,follow=True);times.append((time.perf_counter()-start)*1000)
report['follow_3d_median_ms']=statistics.median(times);report['follow_3d_p95_ms']=sorted(times)[37]
assert report['rotating_3d_median_ms']<30 and report['follow_3d_median_ms']<30
# One retained background per view, rather than an ever-growing cache during mouse motion.
report['bounded_background_cache']=bool(s.track._scene_background[1].size==(760,550))
(ROOT/'reports/render-performance-checks.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
