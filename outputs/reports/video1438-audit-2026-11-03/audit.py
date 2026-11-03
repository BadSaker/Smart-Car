from pathlib import Path
import mmap,hashlib,json,struct,sys,subprocess,csv,ctypes as C
import numpy as np
from PIL import Image,ImageDraw
root=Path.cwd(); out=root/'outputs/reports/video1438-audit-2026-11-03'; temp=root/'tmp/video1438-audit'
src=Path('C:/Users/Sumi/Desktop/2026_11_03_14_38_13_Video.avi')
f=src.open('rb'); b=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
av=b.find(b'avih'); usec=struct.unpack_from('<I',b,av+8)[0]; fps=1e6/usec
pos=b.find(b'movi')+4; offsets=[]
while pos+8<=len(b):
 tag=b[pos:pos+4]; n=int.from_bytes(b[pos+4:pos+8],'little')
 if tag in (b'00db',b'00dc'): offsets.append((pos+8,n))
 if tag==b'idx1':break
 pos+=8+n+(n&1)
def frame(k):
 p,n=offsets[k]; return np.frombuffer(b,np.uint8,count=n,offset=p).reshape(120,188,3)[::-1,:,::-1].copy()
runs=[]; previous=None
for k in range(len(offsets)):
 a=frame(k); h=hashlib.sha256(a).digest()
 if h!=previous:runs.append([k,k])
 else:runs[-1][1]=k
 previous=h
for page in range((len(runs)+99)//100):
 im=Image.new('RGB',(1880,1400),'#222222'); d=ImageDraw.Draw(im)
 for j,(first,last) in enumerate(runs[page*100:page*100+100]):
  x=j%10*188;y=j//10*140;im.paste(Image.fromarray(frame(first)),(x,y));d.text((x,y+121),f'{first}-{last} {first/fps:.2f}s',fill='white')
 im.save(out/f'all-{page+1:02}.jpg',quality=95)
sys.path.insert(0,str(root/'car/tests'))
from host_toolchain import find_compiler
wrapper=(root/'outputs/reports/track-diagnosis-2026-11-03/replay-wrapper.c').read_text()+'\nAPI void profile(unsigned m){track_vision_set_relaxed((unsigned char)m);}\n'
(temp/'wrapper.c').write_text(wrapper)
cmd=[find_compiler(),'-std=c99','-shared','-O2','-static-libgcc','-I'+str(root/'car/src'),'-I'+str(root/'car/include'),'-I'+str(root/'car/config'),str(temp/'wrapper.c'),'-lm','-o',str(temp/'replay.dll')]
p=subprocess.run(cmd,capture_output=True,text=True);assert p.returncode==0,p.stderr
lib=C.CDLL(str(temp/'replay.dll'));lib.run.argtypes=[C.POINTER(C.c_uint8),C.c_uint,C.c_uint,C.POINTER(C.c_float),C.POINTER(C.c_uint8)]
names=next(csv.reader((root/'outputs/reports/track-diagnosis-2026-11-03/replay-50hz.csv').open()))[4:]
v=(C.c_float*len(names))(); r=(C.c_uint8*480)(); stats={}
for mode in (0,1):
 lib.profile(mode);lib.reset();rows=[]
 for k in range(len(offsets)):
  a=frame(k);mask=np.ptp(a,axis=2)>0;g=np.rint(a[:,:,0]*.299+a[:,:,1]*.587+a[:,:,2]*.114).astype(np.uint8)
  for y in range(120):
   good=np.flatnonzero(~mask[y]);bad=np.flatnonzero(mask[y])
   if len(good):g[y,bad]=np.rint(np.interp(bad,good,g[y,good])).astype(np.uint8)
  g=np.ascontiguousarray(g);lib.run(g.ctypes.data_as(C.POINTER(C.c_uint8)),round(k*1000/fps),0,v,r)
  rows.append(dict(frame=k,time_s=k/fps,**dict(zip(names,map(float,v)))))
 with (out/f'replay-P{mode}.csv').open('w',newline='') as stream:
  w=csv.DictWriter(stream,fieldnames=rows[0]);w.writeheader();w.writerows(rows)
 stats[mode]=dict(valid=sum(x['valid']>0 for x in rows),elements={e:sum(x['element']==e for x in rows) for e in range(7)})
summary=dict(frames=len(offsets),fps=fps,duration=len(offsets)/fps,distinct_runs=len(runs),duplicates=len(offsets)-len(runs),sheets=(len(runs)+99)//100,replay=stats,runs=runs,source_sha256={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'car/src/track_vision.c',root/'car/src/autonomous_drive.c',root/'car/config/vision_config.h']})
(out/'audit.json').write_text(json.dumps(summary,indent=2)); print(json.dumps({k:v for k,v in summary.items() if k not in ('runs','source_sha256')},indent=2))
