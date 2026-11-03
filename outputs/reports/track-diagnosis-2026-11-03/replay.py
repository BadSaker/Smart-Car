from pathlib import Path
import ctypes as C, csv, hashlib, json, mmap, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw

ROOT=Path.cwd(); TEMP=ROOT/'tmp/track-diagnosis-20261103'; OUT=ROOT/'outputs/reports/track-diagnosis-2026-11-03'
TEMP.mkdir(parents=True,exist_ok=True)
OUT.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(ROOT/'car/tests'))
from host_toolchain import find_compiler
fields=['valid','confidence','observation_confidence','threshold','fault','element','side','near_error_px','far_error_px',
 'raw_near_error_px','raw_far_error_px','near_path_supported','curve_outer_side','curve_outer_supported',
 'curve_outer_near_rows','curve_outer_far_rows','curve_outer_reject','left_opening_rows','right_opening_rows',
 'left_fork','right_fork','left_island','right_island','ring_left_reject','ring_right_reject','ring_entry_supported',
 'zebra','zebra_near_row','zebra_far_row','zebra_rows','zebra_reject','geometry.valid','geometry.reject_reason']
extras=['obstacle.side','obstacle.candidate','obstacle.frames','obstacle.blocked','obstacle.offset_mm',
 'vision.candidate_left','vision.candidate_right','vision.candidate_cross','vision.approach_clear','vision.recovery',
 'vision.phase','vision.side','vision.diagnostic.limit_reason','vision.curve_outer_frames']
names=fields+extras
code='#include "track_vision.c"\n#define API __declspec(dllexport)\nAPI void reset(void){track_vision_reset();}\n'
code+='API void run(const unsigned char *g,unsigned ms,unsigned mm,float *v,unsigned char *r){track_vision_result_t o;track_vision_process(g,ms,mm,&o);\n'
for i,n in enumerate(fields):code+=f'v[{i}]=(float)o.{n};\n'
for i,n in enumerate(extras,len(fields)):code+=f'v[{i}]=(float){n};\n'
code+='memcpy(r,o.left,120);memcpy(r+120,o.right,120);memcpy(r+240,o.center,120);memcpy(r+360,o.row_flags,120);}\n'
(OUT/'replay-wrapper.c').write_text(code,encoding='utf-8')
compiler=find_compiler();dll=TEMP/'diagnostic.dll'
cmd=[compiler,'-std=c99','-shared','-O2','-Wall','-Wextra','-Werror','-static-libgcc','-I'+str(ROOT/'car/src'),'-I'+str(ROOT/'car/include'),'-I'+str(ROOT/'car/config'),str(OUT/'replay-wrapper.c'),'-lm','-o',str(dll)]
p=subprocess.run(cmd,capture_output=True,text=True);(OUT/'host-compile.txt').write_text(p.stdout+p.stderr,encoding='utf-8')
if p.returncode:raise RuntimeError(p.stderr)
lib=C.CDLL(str(dll));lib.run.argtypes=[C.POINTER(C.c_uint8),C.c_uint,C.c_uint,C.POINTER(C.c_float),C.POINTER(C.c_uint8)]
v=(C.c_float*len(names))();r=(C.c_uint8*480)()
f=Path(r'C:/Users/Sumi/Desktop/2026_11_03_12_23_17_Video.avi').open('rb');data=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
offset_list=[];pos=data.find(b'movi')+4
while pos+8<=len(data):
 tag=data[pos:pos+4];n=int.from_bytes(data[pos+4:pos+8],'little')
 if tag in (b'00db',b'00dc'):offset_list.append((pos+8,n))
 if tag==b'idx1':break
 pos+=8+n+(n&1)
offsets=np.array(offset_list)

def frame(k):
 off,n=map(int,offsets[k]);return np.frombuffer(data,np.uint8,count=n,offset=off).reshape(120,188,3)[::-1,:,::-1].copy()
def gray(a,repair):
 mask=np.ptp(a,axis=2)>0
 g=np.rint(a[:,:,0]*.299+a[:,:,1]*.587+a[:,:,2]*.114).astype(np.uint8)
 if repair:
  for y in range(120):
   good=np.flatnonzero(~mask[y]);bad=np.flatnonzero(mask[y])
   if len(good):g[y,bad]=np.rint(np.interp(bad,good,g[y,good])).astype(np.uint8)
 return np.ascontiguousarray(g),mask
def run(g,ms,mm=0):
 lib.run(g.ctypes.data_as(C.POINTER(C.c_uint8)),ms,mm,v,r)
 return dict(zip(names,map(float,v)))
stats={}; records=[]; previous=None; duplicates=0; runs=[];begin=0
for k in range(len(offsets)):
 a=frame(k);key=hashlib.sha256(a).digest()
 if key==previous:duplicates+=1
 else:
  if k>begin:runs.append((begin,k-1))
  begin=k
 previous=key
runs.append((begin,len(offsets)-1))
stats['exact_duplicate_adjacent_frames']=duplicates
stats['longest_identical_runs']=[dict(first=a,last=b,duration_s=(b-a+1)/50) for a,b in sorted(runs,key=lambda x:x[1]-x[0],reverse=True)[:10]]
for repair in (False,True):
 label='interpolated_overlay' if repair else 'luminance_with_overlay'
 lib.reset()
 for k in range(0,len(offsets),5):
  a=frame(k);g,mask=gray(a,repair);d=run(g,k*20)
  d=dict(mode=label,frame=k,time_s=k/50,overlay_pixels=int(mask.sum()),**d);records.append(d)
 # independent three exposures are a static sensitivity probe, not recorded temporal confirmation
 for k in range(0,len(offsets),25):
  g,mask=gray(frame(k),repair);lib.reset()
  for j in range(3):d=run(g,j*20)
  records.append(dict(mode=label+'_static3',frame=k,time_s=k/50,overlay_pixels=int(mask.sum()),**d))
with (OUT/'replay.csv').open('w',newline='',encoding='utf-8') as f:
 w=csv.DictWriter(f,fieldnames=list(records[0]));w.writeheader();w.writerows(records)
for mode in sorted(set(d['mode'] for d in records)):
 rows=[d for d in records if d['mode']==mode]
 stats[mode]=dict(frames=len(rows),valid=sum(d['valid']>0 for d in rows),zebra=sum(d['zebra']>0 for d in rows),
  obstacle=sum(d['obstacle.side']>0 for d in rows),elements={str(e):sum(d['element']==e for d in rows) for e in range(7)})
stats['limitations']=['Overlay pixels are lost; interpolation is approximate.','No encoder/time/firmware identity metadata.','Sequential replay sampled every 100 ms and distance fixed at zero; not a closed loop or vehicle fault reconstruction.','Static3 repeats a frame artificially to probe appearance eligibility only.']
stats['compiler']=compiler
stats['source_sha256']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'car/src/track_vision.c',ROOT/'car/config/vision_config.h',ROOT/'car/src/autonomous_drive.c',ROOT/'car/src/autonomous_control.c',ROOT/'car/config/autonomous_config.h']}
(OUT/'replay-summary.json').write_text(json.dumps(stats,indent=2),encoding='utf-8');print(json.dumps(stats,indent=2))
