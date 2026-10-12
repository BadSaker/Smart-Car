"""Bounded, reproducible performance measurements, with numerical fingerprint."""
import argparse,hashlib,json,sys,time,cProfile,pstats,io,statistics
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from parameters import defaults
from engine import Simulation
from render import map_image,scene_image,camera_image,chart_image
def measure(fn,count=30):
    samples=[]
    fn()
    for _ in range(count):
        start=time.perf_counter();fn();samples.append((time.perf_counter()-start)*1000)
    return {'median_ms':statistics.median(samples),'p95_ms':sorted(samples)[int(.95*(len(samples)-1))]}
def main():
    parser=argparse.ArgumentParser();parser.add_argument('label');args=parser.parse_args()
    s=Simulation(defaults());s.started=True
    ticks=measure(s.tick,500)
    while s.now<10000:s.tick()
    digest=hashlib.sha256(s.image.tobytes()+s.rows.tobytes()+s.data.tobytes()+
        json.dumps(s.history,sort_keys=True).encode()).hexdigest()
    result={'tick':ticks,'2d':measure(lambda:map_image(s)),
      '3d':measure(lambda:scene_image(s)),'camera':measure(lambda:camera_image(s)),
      'chart':measure(lambda:chart_image(s)), 'numerical_fingerprint_10s':digest,
      'result_10s':s.result()}
    profiler=cProfile.Profile();profiler.enable()
    for _ in range(5):scene_image(s)
    profiler.disable();stream=io.StringIO();pstats.Stats(profiler,stream=stream).sort_stats('cumtime').print_stats(18)
    (ROOT/'reports'/f'performance-{args.label}-profile.txt').write_text(stream.getvalue(),encoding='utf-8')
    (ROOT/'reports'/f'performance-{args.label}.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
