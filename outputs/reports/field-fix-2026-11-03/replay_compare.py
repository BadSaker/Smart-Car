"""复查全帧近似灰度；仅诊断，不将录屏插值当作原始灰度。"""
from pathlib import Path
import ctypes as C
import csv
import hashlib
import json
import mmap
import subprocess
import sys
import numpy as np

ROOT=Path.cwd()
OUT=ROOT/'outputs/reports/field-fix-2026-11-03'
OLD=ROOT/'outputs/reports/track-diagnosis-2026-11-03'
TEMP=ROOT/'tmp/field-fix-20261103/replay'
TEMP.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(ROOT/'car/tests'))
from host_toolchain import find_compiler

head=next(csv.reader((OLD/'replay-50hz.csv').open(encoding='utf-8')))
names=head[4:]
wrapper=(OLD/'replay-wrapper.c').read_text(encoding='utf-8')+'\nAPI void profile(unsigned mode){track_vision_set_relaxed((unsigned char)mode); }\n'
(TEMP/'wrapper.c').write_text(wrapper,encoding='utf-8')
dll=TEMP/'replay.dll'
cmd=[find_compiler(),'-std=c99','-shared','-O2','-Wall','-Wextra','-Werror','-static-libgcc',
     '-I'+str(ROOT/'car/src'),'-I'+str(ROOT/'car/include'),'-I'+str(ROOT/'car/config'),
     str(TEMP/'wrapper.c'),'-lm','-o',str(dll)]
p=subprocess.run(cmd,capture_output=True,text=True)
if p.returncode:raise RuntimeError(p.stdout+p.stderr)
lib=C.CDLL(str(dll))
lib.run.argtypes=[C.POINTER(C.c_uint8),C.c_uint,C.c_uint,C.POINTER(C.c_float),C.POINTER(C.c_uint8)]
v=(C.c_float*len(names))();r=(C.c_uint8*480)()
f=Path(r'C:/Users/Sumi/Desktop/2026_11_03_12_23_17_Video.avi').open('rb')
b=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
pos=b.find(b'movi')+4;offsets=[]
while pos+8<=len(b):
 tag=b[pos:pos+4];n=int.from_bytes(b[pos+4:pos+8],'little')
 if tag in (b'00db',b'00dc'):offsets.append((pos+8,n))
 if tag==b'idx1':break
 pos+=8+n+(n&1)
profiles={}
for profile in (0,1):
 rows=[];lib.profile(profile);lib.reset()
 for k,(pos,n) in enumerate(offsets):
  a=np.frombuffer(b,np.uint8,count=n,offset=pos).reshape(120,188,3)[::-1,:,::-1].copy()
  mask=np.ptp(a,axis=2)>0
  g=np.rint(a[:,:,0]*.299+a[:,:,1]*.587+a[:,:,2]*.114).astype(np.uint8)
  for y in range(120):
   good=np.flatnonzero(~mask[y]);bad=np.flatnonzero(mask[y])
   if len(good):g[y,bad]=np.rint(np.interp(bad,good,g[y,good])).astype(np.uint8)
  g=np.ascontiguousarray(g)
  lib.run(g.ctypes.data_as(C.POINTER(C.c_uint8)),k*20,0,v,r)
  rows.append(dict(mode='approximate_50hz_zero_distance',frame=k,time_s=k/50,
                   overlay_pixels=int(mask.sum()),**dict(zip(names,map(float,v)))))
 profiles[profile]=rows
 with (OUT/f'replay-P{profile}.csv').open('w',newline='',encoding='utf-8') as f:
  w=csv.DictWriter(f,fieldnames=head);w.writeheader();w.writerows(rows)
def stats(rows):
 return dict(frames=len(rows),valid=sum(float(d['valid'])>0 for d in rows),
             intermediate_q=sum(0<float(d['confidence'])<100 for d in rows),
             zebra=sum(float(d['zebra'])>0 for d in rows),
             zebra_valid=sum(float(d['zebra'])>0 and float(d['valid'])>0 for d in rows),
             elements={str(e):sum(float(d['element'])==e for d in rows) for e in range(7)})
before=list(csv.DictReader((OLD/'replay-50hz.csv').open(encoding='utf-8')))
summary=dict(before=stats(before),strict=stats(profiles[0]),relaxed=stats(profiles[1]),
             limitations='Interpolated overlays; duplicate recording frames; zero distance; no real vehicle feedback. Not a closed-loop performance claim.',
             sources={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in [ROOT/'car/src/track_vision.c',ROOT/'car/config/vision_config.h']})
(OUT/'replay-comparison.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps(summary,indent=2))
