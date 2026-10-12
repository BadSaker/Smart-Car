"""Reproducible headless experiments, with all exports under simulation/runs."""
import argparse,json,time
from parameters import defaults,load
from engine import Simulation
from render import map_image,scene_image,camera_image,chart_image
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--config');parser.add_argument('--seconds',type=float,default=140)
    parser.add_argument('--mode',choices=['native','reference']);parser.add_argument('--direction',choices=['A→B','B→A'])
    parser.add_argument('--scenario',choices=['full','midterm','straight','circle']);args=parser.parse_args()
    p=load(args.config) if args.config else defaults()
    for key,arg in [('controller',args.mode),('direction',args.direction),('scenario',args.scenario)]:
        if arg:p[key]=arg
    s=Simulation(p);s.started=True;wall=time.perf_counter()
    for i in range(int(args.seconds*100)):
        s.tick()
        if (s.completed and s.stop_event is None) or (p['controller']=='native' and s.stop_capture_complete):break
    folder=s.export(p['controller']+'-'+p['scenario']+'-'+('AB' if p['direction']=='A→B' else 'BA'))
    map_image(s,(1100,760)).save(folder/'map.png');scene_image(s,(1100,760)).save(folder/'scene.png')
    camera_image(s).save(folder/'vision_overlay.png');chart_image(s).save(folder/'chart.png')
    print(json.dumps({'result':s.result(),'folder':str(folder),'wall_s':time.perf_counter()-wall},ensure_ascii=False,indent=2))
if __name__=='__main__':main()
