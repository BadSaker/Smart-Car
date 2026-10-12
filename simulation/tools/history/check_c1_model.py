"""Metric geometry, legacy dynamics, Tk controls and cached rendering checks."""
import hashlib,json,math,statistics,sys,time,zipfile
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT))
from parameters import defaults,validate,load,save
from engine import Simulation
from car_model import outline,tire_footprints,wheel_poses,mesh
from render import map_image,scene_image

def benchmark(fn):
    fn();times=[]
    for _ in range(40):
        start=time.perf_counter();fn();times.append((time.perf_counter()-start)*1000)
    return {'median_ms':statistics.median(times),'p95_ms':sorted(times)[37]}

def main():
    p=defaults();s=Simulation(p);s.x=s.y=s.h=0.
    plate=outline(p)
    assert np.allclose([plate[:,0].min(),plate[:,0].max()],[-.015,.23])
    tires=np.concatenate(tire_footprints(s))
    assert math.isclose(np.ptp(tires[:,1]),.180)
    assert math.isclose(np.ptp(tires[:,0]),.254)
    assert math.isclose(p['wheelbase']+p['front_overhang']-p['camera_offset'],.150)
    assert math.isclose(np.ptp(plate[:,0]),.245)
    for angle in (-28,0,28):
        s.steer=math.radians(angle);poses=wheel_poses(s)
        assert poses[0][2]==poses[1][2]==0
        if angle:
            curvature=math.tan(s.steer)/p['wheelbase'];r=1/curvature
            for u,v,a in poses[2:]:assert math.isclose(v+p['wheelbase']/math.tan(a),r)
        else:assert poses[2][2]==poses[3][2]==0
        faces,_=mesh(s);assert np.isfinite(faces).all();assert faces[:,:,2].min()>-1e-10
    s.steer=0
    scene_image(s,(1100,780),yaw=-125,elevation=35,model_only=True).save(ROOT/'reports/c1-model-preview.png')
    scene_image(s,(1100,780),yaw=-90,elevation=80,model_only=True).save(ROOT/'reports/c1-model-top.png')
    s.steer=math.radians(24)
    scene_image(s,(1100,780),yaw=-125,elevation=35,model_only=True).save(ROOT/'reports/c1-model-turn.png')
    # Render calls must not change the image/control state or vehicle pose.
    state=(s.x,s.y,s.h,s.steer,s.now,s.image.tobytes(),s.data.tobytes())
    scene_image(s);map_image(s)
    assert state==(s.x,s.y,s.h,s.steer,s.now,s.image.tobytes(),s.data.tobytes())
    # Old measured profile retains exactly the previous dynamics despite new visuals.
    with zipfile.ZipFile(ROOT/'backups/before-c1-model-20261011.zip') as archive:
        name=next(n for n in archive.namelist() if n.endswith('默认_完整赛道_C算法.json'))
        legacy=p.copy();legacy.update(json.loads(archive.read(name)))
    old=Simulation(legacy);old.started=True
    for _ in range(1000):old.tick()
    original_history=[{k:v for k,v in row.items() if k not in ('minimum_confidence','path_accepted','camera_frame_id','camera_frame_time_ms')} for row in old.history]
    digest=hashlib.sha256(old.image.tobytes()+old.rows.tobytes()+old.data.tobytes()+json.dumps(original_history,sort_keys=True).encode()).hexdigest()
    before=json.loads((ROOT/'reports/performance-before-c1.json').read_text(encoding='utf-8'))
    assert digest==before['numerical_fingerprint_10s']
    for path in (ROOT/'configs').glob('*.json'):validate(load(path))
    save(ROOT/'reports/c1-roundtrip.json',p);assert load(ROOT/'reports/c1-roundtrip.json')==p
    fresh=Simulation(p);fresh.started=True
    for _ in range(1000):fresh.tick()
    timings={'2d':benchmark(lambda:map_image(fresh)),
             '3d':benchmark(lambda:scene_image(fresh)),
             'model_detail':benchmark(lambda:scene_image(s,model_only=True))}
    # Real Tk application, actual tab layout and parameter reset path.
    import tkinter as tk
    from app import App
    root=tk.Tk()
    capture='not attempted'
    try:
        app=App(root);root.update()
        app.view_tabs.select(app.views['C1 车模']);root.update();app.refresh();root.update()
        assert 'model' in app.photos
        try:
            import ctypes
            from PIL import ImageGrab
            hwnd=ctypes.windll.user32.GetParent(root.winfo_id())
            ImageGrab.grab(window=hwnd).save(ROOT/'reports/c1-desktop.png')
            capture='saved'
        except OSError as error:capture=str(error)
        app.vars['front_overhang'].set('45');app.reset();root.update()
        assert app.sim.p['front_overhang']==.045
        assert math.isclose(outline(app.sim.p)[:,0].max(),.235)
        app.vars['front_overhang'].set('40');app.reset();root.update()
        app.view_tabs.select(app.views['3D 场景']);app.refresh();root.update()
        assert 'scene' in app.photos
    finally:root.destroy()
    result={'metric_checks':'passed','ackermann_checks':'passed','render_purity':'passed',
            'legacy_10s_fingerprint_matches':True,'config_roundtrip':'passed','tk_tabs_and_reset':'passed','desktop_capture':capture,
            'timings':timings,'new_measured_profile_10s':fresh.result(),'polygon_count':len(mesh(s)[0]),
            'overall_length_m':p['length'],'resolved_geometry_envelope_m':.262,
            'unresolved_longitudinal_envelope_m':.018}
    (ROOT/'reports/c1-model-checks.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
