"""Full-route acceptance observations without tuning the excluded optics or sampling."""
import json,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from parameters import defaults
from engine import Simulation
sys.stdout.reconfigure(encoding='utf-8');results=[]
for mode in ('native','reference'):
 for direction in ('A→B','B→A'):
  p=defaults();p.update(controller=mode,direction=direction)
  s=Simulation(p);s.started=True;wall=time.perf_counter()
  for _ in range(35000):
   s.tick()
   if (s.completed and s.stop_event is None) or s.stop_capture_complete:break
  result=s.result();result['direction']=direction;result['wall_seconds']=time.perf_counter()-wall
  if mode=='reference':assert s.completed and s.max_outside<2,result
  else:
   assert s.completed or (s.stop_event is not None and s.stop_capture_complete),result
   folder=s.export('full-native-diagnostic-'+('AB' if direction=='A→B' else 'BA'));result['export']=str(folder)
  results.append(result);print(json.dumps({'mode':mode,'direction':direction,'completed':s.completed,'fault':result['fault'],'sim_time_s':s.now/1000}),flush=True)
(ROOT/'reports/full-track-diagnostics.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
