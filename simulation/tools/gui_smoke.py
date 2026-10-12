"""Exercise real Tk controls and capture the actual desktop window."""
import sys,json,math,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
import tkinter as tk
import ctypes
from PIL import ImageGrab
from app import App,desktop_dpi

desktop_dpi();root=tk.Tk();app=App(root);root.update();root.update_idletasks()
def screenshot(name):
    root.update();root.update_idletasks()
    try:
        hwnd=ctypes.windll.user32.GetParent(root.winfo_id())
        ImageGrab.grab(window=hwnd).save(ROOT/'reports'/name)
        report['desktop_capture']=True
    except OSError as e:
        report['desktop_capture']=str(e)
report={}
try:
    app.start()
    for _ in range(220):app.sim.tick()
    app.pause();app.refresh();root.update();screenshot('desktop-2d.png')
    report['native_started']=bool(app.sim.data[0]==2 and app.sim.distance>0)
    app.view_tabs.select(1);root.update();app.refresh();root.update();screenshot('desktop-3d.png')
    report['3d_rendered']=True
    app.nominal();root.update();report['nominal_applied']=app.sim.p['pitch']==20 and app.sim.p['hfov']==120
    app.fit();root.update();report['fit_applied']=abs(app.sim.p['pitch']-39.4368141872)<1e-5
    old_build=app.sim.native.version;app.vars['kp'].set('30');app.reset()
    report['new_c_build']=app.sim.p['kp']==30 and app.sim.native.version!=old_build
    app.vars['kp'].set('28');app.reset()
    app.start()
    for _ in range(150):app.sim.tick()
    app.vars['drop_camera'].set(True);app.inject()
    for _ in range(20):app.sim.tick()
    report['camera_fault_from_ui']=bool(app.sim.data[1]==2)
    app.export();report['exported']=True
    app.vars['show_ab'].set(False);app.inject();report['markers_toggle']=not app.sim.p['show_ab']
    app.vars['show_ab'].set(True);app.inject()
    # Feed exact-size images through the real replay callback, substituting only the file picker.
    from tkinter import filedialog
    from PIL import Image
    replay_path=ROOT/'reports/replay-test-frame.png';Image.fromarray(app.sim.image).save(replay_path)
    picker=filedialog.askopenfilenames;filedialog.askopenfilenames=lambda **kwargs:(str(replay_path),)
    try:app.replay();report['image_replay']=app.sim.replay and app.replay_index==1;app.export()
    finally:filedialog.askopenfilenames=picker
    app.vars['drop_camera'].set(False);app.reset();app.view_tabs.select(0);app.refresh();root.update()
    report['full_track_default']=app.sim.p['scenario']=='full'
    screenshot('desktop-ready.png')
    (ROOT/'reports/gui_smoke.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report));assert all(v for k,v in report.items() if k!='desktop_capture')
finally:root.destroy()
