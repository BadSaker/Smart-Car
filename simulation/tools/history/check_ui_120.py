"""Check actual camera values, display conversions, stable tab bounds and Tk rendering."""
import ctypes,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT))
import tkinter as tk
from PIL import ImageGrab
from parameters import defaults,load,save
from app import App

def tab_bounds(notebook):
    # Probe actual hit rectangles rather than just inspecting theme options.
    result=[]
    for index in range(len(notebook.tabs())):
        xs=[]
        for x in range(notebook.winfo_width()):
            try:
                if notebook.index(f'@{x},12')==index:xs.append(x)
            except tk.TclError:pass
        assert xs
        center=(xs[0]+xs[-1])//2;ys=[]
        for y in range(60):
            try:
                if notebook.index(f'@{center},{y}')==index:ys.append(y)
            except tk.TclError:pass
        result.append([xs[0],xs[-1],ys[0],ys[-1]])
    return result

root=tk.Tk();app=App(root);root.update();report={}
try:
    assert defaults()['hfov']==app.sim.p['hfov']==120
    assert app.vars['hfov'].get()=='120'
    assert str(app.field_widgets['hfov'].cget('state'))=='normal'
    assert app.field_widgets['hfov'].winfo_class()=='TEntry'
    assert app.vars['wheelbase'].get()=='190' and app.vars['servo_interval'].get()=='20'
    assert app.collect()['wheelbase']==.19 and app.collect()['servo_interval']==.02
    original_pitch=app.sim.p['pitch']
    app.vars['hfov'].set('125.5');app.reset();root.update()
    assert app.sim.p['hfov']==125.5 and app.sim.p['pitch']==original_pitch
    save(ROOT/'reports/ui-120-roundtrip.json',app.collect())
    restored=load(ROOT/'reports/ui-120-roundtrip.json');assert restored['hfov']==125.5
    app.set_fields(restored);assert app.vars['wheelbase'].get()=='190'
    app.vars['hfov'].set('120');app.reset();root.update()
    report['defaults_and_custom_fov']='passed';report['physical_unit_roundtrip']='passed'
    bounds={}
    for name,notebook in [('parameters',app.parameter_tabs),('views',app.view_tabs)]:
        baseline=tab_bounds(notebook)
        for i in range(len(notebook.tabs())):
            notebook.select(i);root.update();app.refresh();root.update()
            assert tab_bounds(notebook)==baseline,(name,i,baseline,tab_bounds(notebook))
        # Exercise actual mouse-pressed state on the first tab too.
        x=(baseline[0][0]+baseline[0][1])//2;y=12
        notebook.event_generate('<ButtonPress-1>',x=x,y=y);root.update()
        assert tab_bounds(notebook)==baseline
        notebook.event_generate('<ButtonRelease-1>',x=x,y=y);root.update()
        assert tab_bounds(notebook)==baseline
        bounds[name]=baseline
    report['selected_and_pressed_tab_bounds']=bounds
    app.toggle_details();root.update();assert app.details_visible
    app.toggle_details();root.update();assert not app.details_visible
    report['diagnostic_window']='passed'
    app.parameter_tabs.select(2);app.view_tabs.select(app.views['C1 车模']);app.refresh();root.update()
    hwnd=ctypes.windll.user32.GetParent(root.winfo_id())
    ImageGrab.grab(window=hwnd).save(ROOT/'reports/ui-120-camera.png')
    app.parameter_tabs.select(0);app.view_tabs.select(0);app.sim.started=True
    for _ in range(120):app.sim.tick()
    app.refresh();root.update()
    ImageGrab.grab(window=hwnd).save(ROOT/'reports/ui-120-status.png')
    assert 'quality' in app.metric_vars and app.metric_vars['speed'].get().endswith('mm/s')
    report['main_metrics_rendered']='passed';report['effective_hfov']=app.sim.p['hfov']
    report['effective_pitch']=app.sim.p['pitch'];report['status_after_1_2s']=app.run_state.get()
    for path in (ROOT/'configs').glob('*.json'):assert load(path)['hfov']==120
    report['supplied_profiles']='all 120 degrees'
    app.fit();root.update();assert abs(app.sim.p['hfov']-104.12881108611644)<1e-7
    app.nominal();root.update();assert app.sim.p['hfov']==120 and app.sim.p['pitch']==20
    report['explicit_fit_and_restore']='passed'
    (ROOT/'reports/ui-120-checks.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))
finally:root.destroy()
