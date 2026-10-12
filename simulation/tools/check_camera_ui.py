"""通过真实Tk控件检查标定、采样配置往返、截图导入和窗口绘制。"""
import argparse
import ctypes
import json
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import tkinter as tk
from tkinter import messagebox
from PIL import ImageGrab
from app import App,desktop_dpi,window_handle
from parameters import defaults,save,load


def check(output,images):
    output.mkdir(parents=True,exist_ok=True)
    desktop_dpi();root=tk.Tk();root.withdraw()
    errors=[];old_error=messagebox.showerror
    messagebox.showerror=lambda title,message:errors.append((title,str(message)))
    try:
        app=App(root);report={}
        for count in (1,2,3):
            for sampling in ('nearest','bilinear'):
                p=defaults();p.update(camera_samples=count,camera_sampling=sampling)
                app.set_fields(p);q=app.collect()
                assert (q['camera_samples'],q['camera_sampling'])==(count,sampling),(count,sampling,q['camera_samples'],q['camera_sampling'])
        report['sampling_roundtrip_combinations']=6
        app.parameter_name.set('我的参数');app.select_params()
        assert app.sim.p['height']==.297 and not app.running
        original=app.sim.p.copy();app.fit('maximum')
        assert app.sim.p==original and app.fit_candidate['height']==.297
        app.apply_fit();assert app.sim.p['height']==.297
        assert abs(app.sim.p['hfov']-89.66951494024845)<1e-8
        report['fixed_height_preview_apply']=True
        saved=output/'ui-roundtrip.json';save(saved,app.collect());restored=load(saved)
        app.set_fields(restored);app.reset()
        assert app.sim.p==restored
        report['save_reload']=True
        snapshots=sorted(images.glob('*.png'))
        for path in snapshots:
            app.comparison.load_image(path)
            assert app.comparison.real.shape==(120,188)
        report['screenshot_imports']=len(snapshots)
        app.set_fields(defaults());app.reset();app.start()
        for _ in range(400):app.sim.tick()
        app.pause();assert app.sim.distance>0 and int(app.sim.data[0])==2
        report['native_running_after_4s']=True
        app.stop();app.pause();assert app.sim.applied_duties.max()==0
        report['stop_cuts_drive']=True
        app.reset();root.deiconify();root.update();root.update_idletasks()
        app.view_tabs.select(app.views['相机标定']);app.refresh(force=True);root.update()
        assert root.winfo_viewable()
        report['calibration_rows']=len(app.width_table.get_children())
        assert report['calibration_rows']==6
        ImageGrab.grab(window=window_handle(root)).save(output/'calibration-window.png')
        app.view_tabs.select(app.views['实测对照']);app.refresh(force=True);root.update()
        ImageGrab.grab(window=window_handle(root)).save(output/'comparison-window.png')
        assert not errors,errors
        report['ui_errors']=errors
        (output/'ui-checks.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps(report,ensure_ascii=False))
    finally:
        messagebox.showerror=old_error
        if 'app' in locals():app.close()
        else:root.destroy()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--images',type=Path,required=True)
    args=parser.parse_args();check(args.output,args.images)
