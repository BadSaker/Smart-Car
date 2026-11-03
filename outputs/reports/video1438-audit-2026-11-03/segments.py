from pathlib import Path
import ctypes as C,csv,json,mmap,collections
import numpy as np
from PIL import Image,ImageDraw
root=Path.cwd();out=root/'outputs/reports/video1438-audit-2026-11-03'
f=Path('C:/Users/Sumi/Desktop/2026_11_03_14_38_13_Video.avi').open('rb');b=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
pos=b.find(b'movi')+4;offsets=[]
while pos+8<=len(b):
 tag=b[pos:pos+4];n=int.from_bytes(b[pos+4:pos+8],'little')
 if tag in (b'00db',b'00dc'):offsets.append((pos+8,n))
 if tag==b'idx1':break
 pos+=8+n+(n&1)
def frame(k):
 p,n=offsets[k];return np.frombuffer(b,np.uint8,count=n,offset=p).reshape(120,188,3)[::-1,:,::-1].copy()
lib=C.CDLL(str(root/'tmp/video1438-audit/replay.dll'));lib.run.argtypes=[C.POINTER(C.c_uint8),C.c_uint,C.c_uint,C.POINTER(C.c_float),C.POINTER(C.c_uint8)]
names=next(csv.reader((root/'outputs/reports/track-diagnosis-2026-11-03/replay-50hz.csv').open()))[4:];v=(C.c_float*len(names))();r=(C.c_uint8*480)();summary=[];records=[]
for first,last in [(20,32),(35,39),(40,44),(67,71)]:
 for speed in (0,500):
  lib.profile(1);lib.reset();rows=[]
  for k in range(first*50,last*50):
   a=frame(k);mask=np.ptp(a,axis=2)>0;g=np.rint(a[:,:,0]*.299+a[:,:,1]*.587+a[:,:,2]*.114).astype(np.uint8)
   for y in range(120):
    good=np.flatnonzero(~mask[y]);bad=np.flatnonzero(mask[y])
    if len(good):g[y,bad]=np.rint(np.interp(bad,good,g[y,good])).astype(np.uint8)
   g=np.ascontiguousarray(g);lib.run(g.ctypes.data_as(C.POINTER(C.c_uint8)),k*20,(k-first*50)*speed//50,v,r)
   d=dict(start_s=first,assumed_speed=speed,frame=k,time_s=k/50,**dict(zip(names,map(float,v))));rows.append(d);records.append(d)
  hits=[d for d in rows if d['left_fork'] and d['left_opening_rows']>=12]
  summary.append(dict(start=first,end=last,assumed_speed=speed,valid=sum(d['valid']>0 for d in rows),elements=dict(collections.Counter(d['element'] for d in rows)),left_fork_open=len(hits),reject_on_left_fork_open=dict(collections.Counter(d['ring_left_reject'] for d in hits)),left_island=sum(d['left_island']>0 for d in rows),right_island=sum(d['right_island']>0 for d in rows)))
with (out/'segments.csv').open('w',newline='') as stream:
 w=csv.DictWriter(stream,fieldnames=records[0]);w.writeheader();w.writerows(records)
(out/'segments-summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
keys=[(22.76,'cross'),(22.92,'cross'),(23.66,'island'),(24.08,'island'),(36.50,'cross'),(42.30,'cross'),(68.90,'ring approach'),(69.10,'ring approach')]
im=Image.new('RGB',(752*2,510*4),'#222222');draw=ImageDraw.Draw(im)
for j,(s,label) in enumerate(keys):
 x=j%2*752;y=j//2*510;im.paste(Image.fromarray(frame(round(s*50))).resize((752,480),Image.Resampling.NEAREST),(x,y));draw.text((x,y+484),f'{s:.2f}s frame {round(s*50)} {label}',fill='white')
im.save(out/'keyframes.jpg',quality=95)
photo=Image.open('C:/temp/codex-clipboard-0c9226b0-f475-4d98-816a-4d48e9c3d1c4.jpg').rotate(180,expand=True)
photo.save(out/'photo-upright.jpg',quality=95)
