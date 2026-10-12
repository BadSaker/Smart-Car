"""Measure actual Tk scheduling latency, including rendering and simulation."""
import argparse,sys,time,json,statistics,types,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
import tkinter as tk
from app import App,desktop_dpi
parser=argparse.ArgumentParser();parser.add_argument('--before',action='store_true');parser.add_argument('--label',default='');args=parser.parse_args()
if args.before:
    with zipfile.ZipFile(ROOT/'backups/before-ui-performance-20261011.zip') as z:
        old_render=types.ModuleType('old_render');exec(compile(z.read('render.py'),'old_render.py','exec'),old_render.__dict__)
        old_app=types.ModuleType('old_app');current_render=sys.modules['render'];sys.modules['render']=old_render
        try:exec(compile(z.read('app.py'),'old_app.py','exec'),old_app.__dict__)
        finally:sys.modules['render']=current_render
        App=old_app.App
desktop_dpi();root=tk.Tk();app=App(root);root.update()
report={};phase=[];draw_counts={}
original_show=app.show
def counted_show(key,label,image):
    draw_counts[key]=draw_counts.get(key,0)+1;original_show(key,label,image)
app.show=counted_show
def finish_phase():
    app.pause()
    report[phase[0]]={'heartbeat_p95_ms':sorted(latencies)[int(.95*(len(latencies)-1))],
                      'heartbeat_max_ms':max(latencies),'samples':len(latencies),
                      'simulated_seconds_advanced':(app.sim.now-phase[2])/1000,
                      'image_updates':draw_counts.copy()}
    root.after(30,next_phase)
phases=iter([('paused',0,False),('2d_1x',0,True),('3d_1x',1,True),('3d_8x',1,True)])
def next_phase():
    global latencies
    try:name,tab,running=next(phases)
    except StopIteration:
        name=f'event-loop-{args.label}.json' if args.label else ('event-loop-before.json' if args.before else 'event-loop-performance.json')
        (ROOT/'reports'/name).write_text(json.dumps(report,indent=2),encoding='utf-8')
        print(json.dumps(report,indent=2));root.destroy();return
    app.view_tabs.select(tab);app.refresh();latencies=[]
    app.speed.set('8×' if name.endswith('8x') else '1×')
    draw_counts.clear();phase[:]=[name,time.perf_counter()+2.0,app.sim.now]
    if running:app.start()
    scheduled=time.perf_counter()+.02;root.after(20,heartbeat,scheduled)
def heartbeat(scheduled):
    latencies.append(max(0,(time.perf_counter()-scheduled)*1000))
    if time.perf_counter()>=phase[1]:finish_phase();return
    root.after(20,heartbeat,time.perf_counter()+.02)
root.after(40,next_phase);root.mainloop()
if not args.before:
    assert all(row['heartbeat_p95_ms']<60 for row in report.values()),report
    assert sum(report['paused']['image_updates'].values())<=4,report['paused']
