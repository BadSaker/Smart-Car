"""Compare optimized scene pixels with the saved pre-change renderer."""
import sys, zipfile, types, time, json, math
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import app, render
from parameters import defaults, ROOT
from engine import Simulation
import numpy as np
old=types.ModuleType('previous_render')
with zipfile.ZipFile(ROOT/'reports/before-interactive-render-fix.zip') as archive:
    exec(compile(archive.read('render.py'),'previous_render','exec'),old.__dict__)
s=Simulation(defaults()); results=[]
for follow in (False,True):
    for yaw in (-55,-23,70):
        for size in ((760,550),(610,570)):
            s.track.__dict__.pop('_scene_background',None)
            a=old.scene_image(s,size,yaw,48,1,follow)
            s.track.__dict__.pop('_scene_background',None)
            b=render.scene_image(s,size,yaw,48,1,follow)
            different=int(np.count_nonzero(np.asarray(a)!=np.asarray(b)))
            assert different==0,(follow,yaw,size,different)
            timings=[]
            for module in (old,render):
                elapsed=[]
                for _ in range(10):
                    s.track.__dict__.pop('_scene_background',None)
                    start=time.perf_counter();module.scene_image(s,size,yaw,48,1,follow)
                    elapsed.append((time.perf_counter()-start)*1000)
                timings.append(float(np.median(elapsed)))
            results.append({'follow':follow,'yaw':yaw,'size':size,'different_channels':different,
                            'before_ms':timings[0],'after_ms':timings[1]})
(ROOT/'reports/viewport-culling-check.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
print(json.dumps(results,indent=2))
