"""用实际C算法执行闭环验收；地图只用于渲染和评分，不送入控制器。"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import time

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import parameters
from toolchain import find_compiler


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True,help='独立验收目录，推荐tmp/本任务目录')
    parser.add_argument('--directions',nargs='+',choices=('AB','BA'),default=['AB','BA'])
    parser.add_argument('--seconds',type=float,default=480)
    parser.add_argument('--offset',type=float)
    parser.add_argument('--heading',type=float)
    parser.add_argument('--seed',type=int)
    parser.add_argument('--disturbance',type=int)
    args=parser.parse_args()
    if args.seconds<=0:parser.error('--seconds必须为正数')
    p=parameters.load(args.config.resolve())
    if p['controller']!='native':parser.error('验收必须使用native现有C算法')
    for field,value in (('offset',args.offset),('heading_error',args.heading),('seed',args.seed),('disturbance_level',args.disturbance)):
        if value is not None:p[field]=value
    p['seed_auto']=0;parameters.validate(p)
    output=args.output.resolve();workspace=output/'workspace';workspace.mkdir(parents=True,exist_ok=True)
    # 编译副本及DLL缓存留在本次输出目录，避免改写发布源码或既有缓存。
    os.environ['SIMULATION_CC']=str(find_compiler())
    shutil.copy2(ROOT/'native_bridge.c',workspace/'native_bridge.c')
    for category in ('src','include','config'):
        shutil.copytree(ROOT/'vendor'/category,workspace/'vendor'/category,dirs_exist_ok=True)
    manifest=ROOT/'toolchain/deployment.json'
    if manifest.is_file():
        (workspace/'toolchain').mkdir(exist_ok=True);shutil.copy2(manifest,workspace/'toolchain/deployment.json')
    hashes={str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest()
            for path in [ROOT/'native_bridge.c',*sorted((ROOT/'vendor').glob('*/*.[ch]'))]}
    parameters.ROOT=workspace
    from engine import Simulation
    from PIL import Image,ImageDraw
    import numpy as np
    reports=[]
    for direction in args.directions:
        config={**p,'direction':'A→B' if direction=='AB' else 'B→A'}
        sim=Simulation(config);sim.started=True;events=[];previous=None;wall=time.perf_counter()
        for _ in range(int(args.seconds*100)):
            sim.tick();phase=int(sim.data[7])
            if phase!=previous:
                events.append({'time_s':sim.now/1000,'progress_m':sim.route_progress,'element':phase,**sim.native.features})
                previous=phase
            if sim.completed or sim.stop_capture_complete or sim.max_outside>=2:break
        sim.record();result=sim.result()
        result.update(pass_run=bool(sim.completed and sim.max_outside==0 and int(sim.data[1])==0),
                      progress_m=sim.route_progress,wall_s=time.perf_counter()-wall,events=events)
        folder=output/direction;folder.mkdir(exist_ok=True)
        (folder/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        (folder/'parameters.json').write_text(json.dumps(config,ensure_ascii=False,indent=2),encoding='utf-8')
        with (folder/'telemetry.csv').open('w',newline='',encoding='utf-8-sig') as stream:
            writer=csv.DictWriter(stream,fieldnames=list(sim.history[0]));writer.writeheader();writer.writerows(sim.history)
        Image.fromarray(sim.stop_image if sim.stop_event else sim.image).save(folder/'last-frame.png')
        gray=sim.track.raster[::-1];road=gray>(p['gray_road']+p['gray_background'])/2
        pixels=np.empty((*gray.shape,3),dtype=np.uint8);pixels[:]=[172,179,186];pixels[road]=[248,249,250]
        scale=min(1000/gray.shape[1],800/gray.shape[0]);size=(round(gray.shape[1]*scale),round(gray.shape[0]*scale))
        picture=Image.fromarray(pixels).resize(size);draw=ImageDraw.Draw(picture)
        points=[((x-sim.track.minimum[0])/sim.track.cell*scale,
                 (gray.shape[0]-1-(y-sim.track.minimum[1])/sim.track.cell)*scale) for x,y in sim.trajectory]
        if len(points)>1:draw.line(points,fill=(0,110,190) if result['pass_run'] else (210,55,45),width=3)
        picture.save(folder/'trajectory.png')
        summary={k:result[k] for k in ('pass_run','completed','time_s','distance_m','max_error_m','max_outside_wheels','fault','algorithm_build')}
        reports.append({'direction':direction,**summary});print(json.dumps(reports[-1],ensure_ascii=True),flush=True)
    (output/'summary.json').write_text(json.dumps({'runs':reports,'source_sha256':hashes},ensure_ascii=False,indent=2),encoding='utf-8')
    return 0 if all(r['pass_run'] for r in reports) else 1


if __name__=='__main__':raise SystemExit(main())
