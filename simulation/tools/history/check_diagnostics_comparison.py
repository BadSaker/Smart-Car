"""Regression gates for stop evidence, camera pose, comparison and real Tk controls."""
import csv,ctypes,json,math,sys,time,statistics
from pathlib import Path
import numpy as np
from PIL import Image,ImageGrab
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT))
from parameters import defaults,load
from engine import Simulation
from camera_optics import basis,origin,ray,project_ground
from geometry import predicted_width
from comparison import Comparison
from render import scene_image

report={};p=defaults();s=Simulation(p);s.started=True
for _ in range(1000):s.tick()
old=np.load(ROOT/'reports/before-diagnostics-state.npz')
assert all(np.array_equal(old[k],getattr(s,k)) for k in ('image','rows','data'))
history=json.loads((ROOT/'reports/before-diagnostics-history.json').read_text(encoding='utf-8'))
assert len(history)==len(s.history) and all({k:row[k] for k in previous}==previous for row,previous in zip(s.history,history))
previous_p=json.loads((ROOT/'reports/before-diagnostics-parameters.json').read_text(encoding='utf-8'))
assert all(p[k]==v for k,v in previous_p.items())
report['default_projection_control_and_prior_telemetry']='exactly unchanged for 10s'
report['default_widths']=[predicted_width(p,30),predicted_width(p,116)]

# Roundtrip arbitrary ground rays through yaw, roll, translation and mild distortion.
for yaw,roll,lateral in [(0,0,0),(15,8,.02),(-20,-12,-.03)]:
 q=dict(p,camera_yaw=yaw,camera_roll=roll,camera_lateral=lateral)
 F,R,D=basis(q);assert np.allclose(np.stack((F,R,D))@np.stack((F,R,D)).T,np.eye(3))
 for u,v in [(60,90),(93,100),(140,105)]:
  direction=ray(u,v,q);ground=origin(q)+direction*q['height']/-direction[2]
  pixel,valid=project_ground([ground[:2]],q)
  assert valid[0] and np.allclose(pixel[0],[u,v],atol=1e-8)
 turned=Simulation(q);scene_image(turned,(640,480)).save(ROOT/f'reports/pose-{yaw}-{roll}.png')
report['pose_ground_projection_frustum_render']='passed'

# Native fault, actual pre/post images, then healthy vision after the locked stop.
s=Simulation(p);s.started=True
for _ in range(300):s.tick()
s.p['blackout']=True
for _ in range(60):
 s.tick()
 if s.stop_event:break
assert s.stop_event and s.stop_event['fault']==3
locked=json.dumps(s.stop_event,sort_keys=True);trigger=s.stop_image.copy();s.p['blackout']=False
for _ in range(230):s.tick()
assert json.dumps(s.stop_event,sort_keys=True)==locked and np.array_equal(trigger,s.stop_image)
assert not s.path_diagnostic()['accepted'] and s.path_diagnostic(False)['accepted']
assert s.stop_capture_complete and len(s.event_frames)>20
folder=s.export('diagnostics-verification');index=json.loads((folder/'frames.json').read_text(encoding='utf-8'))
times=[v['frame_time_ms'] for v in index];assert times==sorted(times)
assert any(t<s.stop_event['control_time_ms'] for t in times) and any(t>s.stop_event['control_time_ms'] for t in times)
assert all((folder/v['image']).exists() and (folder/v['rows']).exists() for v in index)
assert np.array_equal(np.asarray(Image.open(folder/'stop_trigger_frame.png')),trigger)
report['native_fault_and_healthy_post_frames']='passed';report['sequence_export']=str(folder)
report['locked_vs_live']={'locked':s.path_diagnostic(),'live':s.path_diagnostic(False),'frames':len(index)}
# A stale-camera fault must not invent fresh post-event frames.
stale=Simulation(p);stale.started=True
for _ in range(200):stale.tick()
stale.p['drop_camera']=True
for _ in range(240):stale.tick()
assert stale.stop_event['fault']==2 and stale.stop_capture_complete
assert not any(v['meta']['frame_time_ms']>stale.stop_event['control_time_ms'] for v in stale.event_frames)
report['stale_camera_no_fabricated_post_frames']='passed'

# Synthetic fixtures verify mechanics only; explicitly not real camera calibration.
c=Comparison();fixture=ROOT/'reports/comparison-synthetic-188x120.png';Image.fromarray(s.image).save(fixture);c.load_image(fixture)
pixels=[(55,80),(110,95),(150,110)]
for u,v in pixels:
 direction=ray(u,v,p);ground=origin(p)+direction*p['height']/-direction[2]
 c.add_point(ground[0]*1000,ground[1]*1000,u,v)
assert c.result(p)['rmse_px']<1e-8
c.points[0]['u_px']+=3;c.points[0]['v_px']+=4
assert abs(c.result(p)['rmse_px']-5/math.sqrt(3))<1e-8
out=ROOT/'reports/comparison-synthetic-verification';c.export(out,p,s)
other=Comparison();other.load_points(out/'points.csv');assert other.points==c.points
Image.new('L',(376,240)).save(ROOT/'reports/comparison-invalid-size.png')
try:c.load_image(ROOT/'reports/comparison-invalid-size.png');raise AssertionError('wrong size accepted')
except ValueError:pass
report['comparison_point_residual_and_csv']='passed with synthetic fixture only'

import tkinter as tk
from app import App
from tkinter import filedialog,simpledialog
root=tk.Tk()
try:
 app=App(root);root.update()
 # 44/45 valid flag must still be rejected; test boundary and actual active thresholds.
 original=app.sim.data.copy()
 for confidence,valid,minimum,expected in [(44,1,45,'44/45 失效'),(45,1,45,'45/45 有效'),(56,0,45,'56/45 失效'),(39,1,40,'39/40 失效')]:
  app.sim.data[3]=confidence;app.sim.data[4]=valid;app.sim.data[16]=minimum;app.sim.data[20]=0
  app.refresh();root.update();assert app.metric_vars['quality'].get()==expected
 app.sim.data=original
 picker=filedialog.askopenfilename;ask=simpledialog.askstring
 try:
  filedialog.askopenfilename=lambda **kwargs:str(fixture);app.load_measured()
  simpledialog.askstring=lambda *args,**kwargs:'1000, 0, 93, 50';app.add_calibration_point()
  assert len(app.comparison.points)==1
  simpledialog.askstring=lambda *args,**kwargs:'44,167';app.set_reference_widths()
 finally:filedialog.askopenfilename=picker;simpledialog.askstring=ask
 for w,h in [(1100,720),(1480,930)]:
  root.geometry(f'{w}x{h}');root.update();app.view_tabs.select(app.views['实测对照']);app.refresh();root.update()
  assert 'comparison' in app.photos
  hits=set()
  for x in range(app.view_tabs.winfo_width()):
   try:hits.add(app.view_tabs.index(f'@{x},12'))
   except tk.TclError:pass
  assert len(hits)==len(app.view_tabs.tabs()),(w,h,hits)
 app.parameter_tabs.select(len(app.parameter_tabs.tabs())-1);app.vars['camera_yaw'].set('8');app.reset();root.update()
 assert app.sim.p['camera_yaw']==8 and app.sim.p['hfov']==120 and app.sim.p['height']==.27
 app.vars['camera_yaw'].set('0');app.reset();assert app.sim.p['camera_yaw']==0
 app.show_point_errors();root.update()
 for child in root.winfo_children():
  if isinstance(child,tk.Toplevel):child.destroy()
 app.comparison.real=None;app.comparison.source=None;app.comparison.points=[];app.comparison.version+=1
 app.view_tabs.select(app.views['实测对照']);app.refresh();root.update()
 hwnd=ctypes.windll.user32.GetParent(root.winfo_id());ImageGrab.grab(window=hwnd).save(ROOT/'reports/comparison-desktop.png')
 app.sim=s;app.view_tabs.select(0);app.refresh();root.update()
 assert app.metric_vars['quality'].get().endswith('失效')
 ImageGrab.grab(window=hwnd).save(ROOT/'reports/stop-evidence-desktop.png')
 report['tk_quality_import_points_pose_small_window']='passed'
finally:root.destroy()

def timing(fn):
 fn();samples=[]
 for _ in range(25):
  t=time.perf_counter();fn();samples.append((time.perf_counter()-t)*1000)
 return {'median_ms':statistics.median(samples),'p95_ms':sorted(samples)[23]}
report['timings']={'tick_with_frame_buffer':timing(s.tick),'scene':timing(lambda:scene_image(s)),
                   'comparison':timing(lambda:c.image(s,(700,500)))}
for path in (ROOT/'configs').glob('*.json'):
 q=load(path);assert q['camera_yaw']==q['camera_roll']==q['camera_lateral']==0
(ROOT/'reports/diagnostics-comparison-checks.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
sys.stdout.reconfigure(encoding='utf-8');print(json.dumps(report,ensure_ascii=False,indent=2))
