"""Verify parameter UI, deterministic perturbations and fixed-HFOV calibration."""
import ctypes, hashlib, json, math, sys, types, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT))
import numpy as np
from PIL import ImageGrab
from parameters import defaults,load,validate
from calibration import fit,comparison
from engine import Simulation
from camera_optics import origin
from car_model import mesh

report={};p=defaults()
for objective,height,pitch in [('average',.23579930817071457,46.342731622019045),('maximum',.2382082534415178,46.83629108859684)]:
    q=fit(p,objective);assert abs(q['height']-height)<1e-10 and abs(q['pitch']-pitch)<1e-8 and q['hfov']==120
report['six_row_fixed_fov']='average and minimax verified'
for value,expected in [(0,0),(1,1),(1.5,1),(8,1)]:
    q=p.copy();q['outer_enabled']=value;assert validate(q)['outer_enabled']==expected
for key,value in [('outer_enabled',-.1),('outer_enabled',.5),('disturbance_level',1.5),('seed_auto',2),('start_jitter',-.1)]:
    q=p.copy();q[key]=value
    try:validate(q);raise AssertionError((key,value))
    except ValueError:pass
for path in (ROOT/'configs').glob('*.json'):validate(load(path))
report['switch_validation_and_old_configs']='passed'
# Sequential simulation instances avoid the static C library state sharing.
def run(q):
    s=Simulation(q);s.started=True;images=[];data=[]
    for _ in range(200):s.tick();images.append(s.image.copy());data.append(s.data.copy())
    return s,np.array(images),np.array(data)
q=p.copy();q['disturbance_level']=2
s,images,data=run(q);again,images2,data2=run(q)
assert np.array_equal(images,images2) and np.array_equal(data,data2)
assert s.perturbation.record()==again.perturbation.record()
a=s.track.start;front=np.mean(s.wheels()[2:],axis=0)
# Use initial recorded pose rather than a pose after driving.
initial=s.history[0];front=np.array([initial['x_m'],initial['y_m']])+p['wheelbase']*np.array([math.cos(math.radians(initial['heading_deg'])),math.sin(math.radians(initial['heading_deg']))])
tangent=np.array([math.cos(a[2]),math.sin(a[2])]);assert abs((front-a[:2])@tangent)<1e-12
assert abs(s.perturbation.offset)<=.010 and abs(s.perturbation.heading)<=2
q['seed']=43;different,other,_=run(q);assert not np.array_equal(images,other)
q=p.copy();q.update(disturbance_level=0,start_jitter=.1,heading_jitter=10,noise=20,brightness_jitter=50)
off,offimages,offdata=run(q);plain,plainimages,plaindata=run(p)
assert np.array_equal(offimages,plainimages) and np.array_equal(offdata,plaindata)
# Compare current disabled mode against the actual pre-change engine.
with zipfile.ZipFile(ROOT/'backups/before-parameter-ui-calibration-perturbation.zip') as z:
    module=types.ModuleType('old_engine');exec(compile(z.read('engine.py'),'old_engine.py','exec'),module.__dict__)
    old=module.Simulation(p);old.started=True
    for _ in range(200):old.tick()
    assert np.array_equal(old.image,plain.image) and np.array_equal(old.data,plain.data)
    for a,b in zip(old.history,plain.history):
        assert all(a[k]==b[k] for k in a)
report['perturbation']='same seed identical frames/C outputs, different seed differs, disabled baseline unchanged'
q=p.copy();q['seed_auto']=1;random=Simulation(q);assert 0<=random.p['seed']<=2147483647
folder=s.export('parameter-workflow-check');saved=json.loads((folder/'parameters.json').read_text(encoding='utf-8'));result=json.loads((folder/'result.json').read_text(encoding='utf-8'))
assert saved['seed']==s.perturbation.seed and result['perturbation']==s.perturbation.record()
assert 'brightness_offset' in s.history[-1]
report['export']=str(folder)
import tkinter as tk
from app import App,FIELDS
from tkinter import messagebox
errors=[];original_error=messagebox.showerror;messagebox.showerror=lambda title,message:errors.append((title,message))
root=tk.Tk()
try:
    app=App(root);root.update()
    assert list(FIELDS)==['车辆','车模','相机','控制','场景','扰动']
    assert not hasattr(app,'step') and not hasattr(app,'load_params')
    assert app.field_widgets['outer_enabled'].winfo_class()=='TEntry'
    assert app.numeric_edit('-') and app.numeric_edit('12.5') and not app.numeric_edit('abc')
    q=p.copy();q['seed']=2147483647;app.set_fields(q);assert app.vars['seed'].get()=='2147483647'
    app.parameter_name.set('我的参数');app.select_params();root.update()
    assert app.parameter_name.get()=='我的参数' and app.sim.p['height']==.2682 and app.sim.p['hfov']==130
    app.vars['outer_enabled'].set('5');app.reset();assert app.vars['outer_enabled'].get()=='1'
    app.vars['disturbance_level'].set('2');assert app.vars['start_jitter'].get()=='10'
    app.vars['start_jitter'].set('12');assert app.vars['disturbance_level'].get()=='4'
    app.reset();assert app.sim.p['start_jitter']==.012
    app.vars['disturbance_level'].set('0');app.reset()
    app.fit('maximum');oldp=app.sim.p.copy();assert app.sim.p==oldp and app.fit_candidate['hfov']==130
    app.apply_fit();assert app.sim.p['hfov']==130 and app.sim.p['height']!=oldp['height']
    assert len(app.width_table.get_children())==6 and len(app.comparison.widths)==6
    app.parameter_name.set('默认参数');app.set_fields(p);app.reset();assert origin(app.sim.p)[0]==.08
    vertices,_=mesh(app.sim);assert np.isfinite(vertices).all()
    for width,height in [(1100,720),(1480,930)]:
        root.geometry(f'{width}x{height}');root.update()
        for tab in range(6):app.parameter_tabs.select(tab);root.update();assert app.parameter_pages[tab][0].winfo_ismapped()
        app.view_tabs.select(app.views['相机标定']);app.refresh();root.update()
        assert app.calibration_canvas.winfo_width()>200
    hwnd=ctypes.windll.user32.GetParent(root.winfo_id())
    ImageGrab.grab(window=hwnd).save(ROOT/'reports/six-row-calibration-desktop.png')
    app.parameter_tabs.select(5);app.view_tabs.select(app.views['3D 场景']);app.follow.set(True);app.refresh();root.update()
    ImageGrab.grab(window=hwnd).save(ROOT/'reports/perturbation-follow-desktop.png')
    # Resume a paused post-stop capture without rearming drive.
    app.sim.started=True
    for _ in range(120):app.sim.tick()
    app.stop();app.pause();assert app.sim.stop_event is not None
    app.start();assert app.running and app.sim.stop_requested
    while not app.sim.stop_capture_complete:app.sim.tick()
    assert app.sim.data[2]==0
    assert not errors,errors
    report['tk']='selector, numeric fields, preset/custom, six-row preview/apply, single-column small window, post-stop resume passed'
finally:root.destroy();messagebox.showerror=original_error
(ROOT/'reports/parameter-workflow-checks.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=True,indent=2))
