from pathlib import Path
import ctypes as C,csv,json,mmap,subprocess,sys,hashlib,collections
import numpy as np
from PIL import Image,ImageDraw
root=Path.cwd();out=root/'outputs/reports/video1723-review-2026-11-03';temp=root/'tmp/video1723-review'
temp.mkdir(parents=True,exist_ok=True)
out.mkdir(parents=True,exist_ok=True)
src=Path('E:/AA/car/2026_11_03_17_23_32_Video.avi')
f=src.open('rb');b=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ);pos=b.find(b'movi')+4;offsets=[]
while pos+8<=len(b):
 tag=b[pos:pos+4];n=int.from_bytes(b[pos+4:pos+8],'little')
 if tag in (b'00db',b'00dc'):offsets.append((pos+8,n))
 if tag==b'idx1':break
 pos+=8+n+(n&1)
def frame(k):
 p,n=offsets[k];return np.frombuffer(b,np.uint8,count=n,offset=p).reshape(120,188,3)[::-1,:,::-1].copy()
def gray(k):
 a=frame(k);mask=np.ptp(a,axis=2)>0;g=np.rint(a[:,:,0]*.299+a[:,:,1]*.587+a[:,:,2]*.114).astype(np.uint8)
 for y in range(120):
  good=np.flatnonzero(~mask[y]);bad=np.flatnonzero(mask[y])
  if len(good):g[y,bad]=np.rint(np.interp(bad,good,g[y,good])).astype(np.uint8)
 return np.ascontiguousarray(g)
unique=[];last=None
for k in range(len(offsets)):
 h=hashlib.sha256(frame(k)).digest()
 if h!=last:unique.append(k)
 last=h

for page in range((len(unique)+99)//100):
 im=Image.new('RGB',(1880,1400),'#222');draw=ImageDraw.Draw(im)
 for j,k in enumerate(unique[page*100:page*100+100]):
  x=j%10*188;y=j//10*140;im.paste(Image.fromarray(frame(k)),(x,y));draw.text((x,y+121),f'{k} {k/50:.2f}s',fill='white')
 im.save(out/f'all-{page+1:02}.jpg',quality=96)
summary={'frames':len(offsets),'unique_groups':len(unique),'duration_s':len(offsets)/50,'fps_assumption':50,'video_sha256':hashlib.sha256(b).hexdigest(),'pages':(len(unique)+99)//100,'firmware':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'outputs/firmware/autocar.hex',root/'outputs/firmware/autocar.bin']},'sources':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'car/src/track_vision.c',root/'car/config/vision_config.h',root/'car/src/autonomous_drive.c']}}
(out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
