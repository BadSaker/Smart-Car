from pathlib import Path
import ctypes as C,csv,json,mmap,subprocess,sys,hashlib,collections
import numpy as np
from PIL import Image,ImageDraw
root=Path.cwd();out=root/'outputs/reports/video1604-audit-2026-11-03';temp=root/'tmp/video1604-audit'
temp.mkdir(parents=True,exist_ok=True)
out.mkdir(parents=True,exist_ok=True)
src=Path('E:/AA/car/2026_11_03_16_04_23_Video.avi')
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
 im.save(out/f'all-{page+1:02}.jpg',quality=95)
names=next(csv.reader((root/'outputs/reports/track-diagnosis-2026-11-03/replay-50hz.csv').open()))[4:]
extra={'geom_x':'o.geometry.target_right_mm','geom_y':'o.geometry.target_forward_mm','model_kind':'o.geometry.model_kind','fit_rms':'o.geometry.fit_rms_mm','contour_goal_valid':'contour_goal_valid','contour_points':'contour_points','circle_radius':'contour_model_valid==1?contour_model_radius:0','circle_x':'contour_model_x','circle_y':'contour_model_y','reconstructed_rows':'reconstructed','blob_side':'vision.blob_side','blob_bottom':'vision.blob_bottom','incoming_near':'vision.incoming_near','incoming_far':'vision.incoming_far','cross_near_seen':'vision.cross_near_seen','cross_provisional':'vision.cross_provisional','bridge_active':'vision.bridge_active'}
code=(root/'outputs/reports/track-diagnosis-2026-11-03/replay-wrapper.c').read_text()
needle='memcpy(r,o.left,120);'
addition='unsigned reconstructed=0;for(int row=30;row<=116;++row)if((o.row_flags[row]&12U)==12U)++reconstructed;\n'
for idx,(name,expr) in enumerate(extra.items(),len(names)):addition+=f'v[{idx}]=(float)({expr});\n'
code=code.replace(needle,addition+needle,1);names+=list(extra)
(temp/'wrapper.c').write_text(code);sys.path.insert(0,str(root/'car/tests'));from host_toolchain import find_compiler
subprocess.run([find_compiler(),'-std=c99','-O2','-shared','-static-libgcc','-I'+str(root/'car/src'),'-I'+str(root/'car/include'),'-I'+str(root/'car/config'),str(temp/'wrapper.c'),'-lm','-o',str(temp/'retest.dll')],check=True,capture_output=True)
lib=C.CDLL(str(temp/'retest.dll'));lib.run.argtypes=[C.POINTER(C.c_uint8),C.c_uint,C.c_uint,C.POINTER(C.c_float),C.POINTER(C.c_uint8)]
v=(C.c_float*len(names))();r=(C.c_uint8*480)();records=[];summaries=[];captures={}
cases=[('full_unique_0',unique,0),('full_unique_800',unique,800),('full_all_0',range(len(offsets)),0)]
for name,indices,speed in cases:
 lib.reset();rows=[];start=indices[0]
 for k in indices:
  g=gray(k);lib.run(g.ctypes.data_as(C.POINTER(C.c_uint8)),k*20,(k-start)*speed//50,v,r)
  d=dict(case=name,frame=k,time_s=k/50,assumed_speed_mm_s=speed,**dict(zip(names,map(float,v))))
  rows.append(d);records.append(d)
  if name=='segment_20_32_0' or name=='segment_50_60_500':captures[(name,k)]=(d,bytes(r),g)
 summary=dict(case=name,processed=len(rows),valid=sum(d['valid']>0 for d in rows),elements=dict(collections.Counter(int(d['element']) for d in rows)),circle_models=sum(d['model_kind']==1 for d in rows),valid_circle_models=sum(d['model_kind']==1 and d['valid']>0 for d in rows),reconstructed_frames=sum(d['reconstructed_rows']>0 for d in rows))
 transitions=[];previous=None
 for d in rows:
  if d['element']!=previous:transitions.append([d['time_s'],int(d['element'])]);previous=d['element']
 summary['transitions']=transitions;summaries.append(summary)
with (out/'replay.csv').open('w',newline='') as stream:
 w=csv.DictWriter(stream,fieldnames=records[0]);w.writeheader();w.writerows(records)
summary=dict(video_sha256=hashlib.sha256(b).hexdigest(),frames=len(offsets),unique_groups=len(unique),duplicates=len(offsets)-len(unique),duration_s=len(offsets)/50,sources={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'car/src/track_vision.c',root/'car/config/vision_config.h',root/'car/src/autonomous_drive.c']},cases=summaries,limitations='Overlay interpolation; exact adjacent duplicates excluded in unique cases; assumed distance is not measured odometry; no closed-loop vehicle validation.')
(out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps({**summary,'cases':[{k:v for k,v in q.items() if k!='transitions'} for q in summaries]},indent=2))
