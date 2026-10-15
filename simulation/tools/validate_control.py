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
import math

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import parameters
from toolchain import find_compiler
from source_layout import source_root,SOURCE_NAMES,source_files


def run_is_acceptable(result,parameters):
    """完成位置不能掩盖撤PWM或降到低速爬行；正常期允许2%的测速/闭环误差。"""
    stats=result['stability_10ms']
    limits=result.get('motion_limits',{})
    speed_floor=.98*min(800.,*(parameters[k] for k in ('cruise','curve_speed','outer_speed','element_speed')))
    return bool(result.get('dynamics_model')=='independent' and result['completed'] and result['max_outside_wheels']==0 and result['fault']=='无'
        and result.get('max_error_m',float('inf'))<=parameters['road_width']/2
        and result.get('max_route_deviation_m',float('inf'))<=parameters['road_width']/2+.02
        and .8*result['route_length_m']<=result['distance_m']<=1.1*result['route_length_m']
        and limits.get('max_command_step',float('inf'))<=parameters['steering_rate']*.01+1.01
        and limits.get('max_target_rise',float('inf'))<=parameters['accel']*.01+.01
        and limits.get('max_target_fall',float('inf'))<=parameters['decel']*.01+.01
        and limits.get('max_pwm_rise',float('inf'))<=3.01
        and result['min_tire_corner_margin_mm'] is not None and result['min_tire_corner_margin_mm']>=0
        and stats['running_ticks']>0 and stats['zero_pwm_ticks']==0
        and stats['min_path_margin'] is not None and stats['min_path_margin']>=10
        and stats['min_speed_mm_s'] is not None and stats['min_speed_mm_s']>=speed_floor)


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
    parser.add_argument('--capture-waves',action='store_true',help='记录完整赛道R50A45八连弯原始灰度和状态')
    parser.add_argument('--capture-frames',action='store_true',help='记录全部原始帧，供失败回放和对照')
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
    (workspace/'runtime').mkdir(exist_ok=True)
    for file in ROOT.glob('*.py'):shutil.copy2(file,workspace/'runtime'/file.name)
    original=source_root()
    for category,names in SOURCE_NAMES.items():
        destination=workspace/'source'/category;destination.mkdir(parents=True,exist_ok=True)
        for name in names:shutil.copy2(original/category/name,destination/name)
    manifest=ROOT/'toolchain/deployment.json'
    if manifest.is_file():
        (workspace/'toolchain').mkdir(exist_ok=True);shutil.copy2(manifest,workspace/'toolchain/deployment.json')
    hashes={str(path.relative_to(original)):hashlib.sha256(path.read_bytes()).hexdigest() for path in source_files()}
    hashes['native_bridge.c']=hashlib.sha256((ROOT/'native_bridge.c').read_bytes()).hexdigest()
    os.environ['SIMULATION_SOURCE_ROOT']=str(workspace/'source')
    parameters.ROOT=workspace
    from engine import Simulation
    from PIL import Image,ImageDraw
    import numpy as np
    reports=[]
    for direction in args.directions:
        config={**p,'direction':'A→B' if direction=='AB' else 'B→A'}
        sim=Simulation(config);sim.started=True;events=[];previous=None;wall=time.perf_counter()
        wave_frames=[];wave_meta=[];last_wave_frame=-1;wave_window=None
        captured=[];last_frame=-1
        if args.capture_waves:
            arcs=[part for part in sim.track.parts if abs(abs(part['k']*part['length'])-math.pi/4)<1e-8]
            if len(arcs)!=8:raise ValueError('R50A45采集必须使用包含八连弯的完整赛道')
            a,b=arcs[0]['s'],arcs[-1]['s']+arcs[-1]['length']
            wave_window=(a,b) if direction=='AB' else ((sim.track.ab_length-b)%sim.track.length,(sim.track.ab_length-a)%sim.track.length)
        for _ in range(int(args.seconds*100)):
            sim.tick();phase=int(sim.data[7])
            if args.capture_frames and sim.frame_id!=last_frame:
                captured.append(sim.frame_buffer[-1]);last_frame=sim.frame_id
            if wave_window and wave_window[0]-.2<=sim.route_progress<=wave_window[1]+.2 and sim.frame_id!=last_wave_frame:
                last_wave_frame=sim.frame_id;wave_frames.append(sim.image.tobytes())
                wave_meta.append({'time_ms':sim.frame_time_ms,'distance_mm':sim.vision_distance_mm,
                    'progress_m':sim.route_progress,'element':phase,'valid':int(sim.data[4]),
                    'q':int(sim.data[3]),'fault':int(sim.data[1]),**sim.native.features})
            if phase!=previous:
                events.append({'time_s':sim.now/1000,'progress_m':sim.route_progress,'element':phase,**sim.native.features})
                previous=phase
            if sim.completed or sim.stop_capture_complete or sim.max_outside>=2 or sim.max_route_deviation>sim.track.width/2+.02:break
        sim.record();result=sim.result()
        result.update(pass_run=run_is_acceptable(result,config),
                      progress_m=sim.route_progress,wall_s=time.perf_counter()-wall,events=events)
        folder=output/direction;folder.mkdir(exist_ok=True)
        (folder/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        (folder/'parameters.json').write_text(json.dumps(config,ensure_ascii=False,indent=2),encoding='utf-8')
        packets=captured or sim.event_frames
        if packets:
            np.savez_compressed(folder/'frames.npz',images=np.array([q['image'] for q in packets]),rows=np.array([q['rows'] for q in packets]))
            (folder/'frames.json').write_text(json.dumps([q['meta'] for q in packets],ensure_ascii=False),encoding='utf-8')
        if wave_window:
            (folder/'r50a45.gray').write_bytes(b''.join(wave_frames))
            wave_result={'window_m':wave_window,'frames':len(wave_meta),'confirmed_element_frames':sum(m['element']>=2 for m in wave_meta),
                'fault_frames':sum(m['fault']!=0 for m in wave_meta),'metadata':wave_meta}
            (folder/'r50a45.json').write_text(json.dumps(wave_result,indent=2),encoding='utf-8')
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
