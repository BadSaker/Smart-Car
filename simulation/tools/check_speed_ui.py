"""Measure the actual Tk event loop, fixed simulation steps and pacing controls."""
from pathlib import Path
import argparse,json,sys,time,tkinter as tk
import numpy as np
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from app import App,desktop_dpi
from engine import Simulation
from parameters import defaults

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--scenario',choices=('full','straight'),default='full')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    desktop_dpi();root=tk.Tk();app=App(root);root.update()
    results=[];snapshots=[];gaps=[];last=time.perf_counter()
    def heartbeat():
        nonlocal last
        now=time.perf_counter();gaps.append(now-last);last=now;root.after(50,heartbeat)
    root.after(50,heartbeat)
    def pump_until(predicate,limit):
        deadline=time.perf_counter()+limit
        while not predicate():
            root.update();time.sleep(.001)
            if time.perf_counter()>deadline:raise AssertionError('GUI did not reach its requested simulation time')
    try:
        for factor in (.5,1.,2.,4.,8.):
            app.pause();p=defaults();p.update(scenario=args.scenario,seed=42,disturbance_level=0)
            app.sim=Simulation(p);app.accumulator=0.;app._rate_wall=app._rate_sim=0.
            app.speed.set(f'{factor:g}×');end=[];tick=app.sim.tick
            def measured_tick():
                tick()
                if app.sim.now==6000:
                    end.append(time.perf_counter());app.running=False
            app.sim.tick=measured_tick
            app._window_hidden=False;app.start();begin=time.perf_counter()
            pump_until(lambda:bool(end),40)
            wall=end[0]-begin;rate=6/wall
            snapshots.append((app.sim.data.copy(),app.sim.image.copy(),(app.sim.x,app.sim.y,app.sim.h,app.sim.frame_id)))
            results.append({'requested':factor,'wall_s':wall,'sim_s':6,'actual':rate})
            print(json.dumps(results[-1]),flush=True)
            frozen=app.sim.now;app.pause();until=time.perf_counter()+.15
            pump_until(lambda:time.perf_counter()>=until,1)
            assert app.sim.now==frozen,'Pause advanced simulation time'
        for data,image,pose in snapshots[1:]:
            np.testing.assert_array_equal(data,snapshots[0][0]);np.testing.assert_array_equal(image,snapshots[0][1])
            assert pose==snapshots[0][2],'Speed setting changed physical trajectory or camera sampling'
        assert all(b['actual']>a['actual']*1.1 for a,b in zip(results,results[1:])),results
        assert abs(results[0]['actual']-.5)<.12 and abs(results[1]['actual']-1)<.2,results
        # Switch speed while running, then reset and exercise replay pacing.
        app.sim=Simulation(defaults());app.speed.set('1×');app.accumulator=0.;app.start()
        pump_until(lambda:app.sim.now>=200,3);app.speed.set('4×')
        pump_until(lambda:app.sim.now>=1000,3)
        root.iconify();pump_until(lambda:root.state()=='iconic',3)
        frozen=app.sim.now;until=time.perf_counter()+.2
        pump_until(lambda:time.perf_counter()>=until,1);assert app.sim.now==frozen
        root.deiconify();pump_until(lambda:app.sim.now>frozen,3)
        app.pause();app.reset()
        assert app.sim.now==0 and not app.running and app.accumulator==0
        app.sim.replay=True;app.replay_frames=[('fixture',snapshots[0][1].copy()) for _ in range(12)]
        app.replay_index=0;app.replay_clock=0.;app.speed.set('4×');app.start()
        pump_until(lambda:not app.running,3)
        assert app.replay_index==12
        report={'passed':True,'scenario':args.scenario,'rates':results,'same_trajectory_and_frames':True,'heartbeat_max_ms':max(gaps)*1000,
                'pause_switch_reset_replay':True,'minimize_restore':True}
        (args.output/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    finally:app.close()
if __name__=='__main__':main()
