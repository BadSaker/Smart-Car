from pathlib import Path
import ctypes as C,csv,json,mmap,subprocess,sys,hashlib,collections
import numpy as np
from PIL import Image,ImageDraw
root=Path.cwd();out=root/'outputs/reports/video1223-retest-2026-11-03';temp=root/'tmp/video1223-retest'
src=Path('C:/Users/Sumi/Desktop/2026_11_03_12_23_17_Video.avi')
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
names=next(csv.reader((root/'outputs/reports/track-diagnosis-2026-11-03/replay-50hz.csv').open()))[4:]
extra={'geom_x':'o.geometry.target_right_mm','geom_y':'o.geometry.target_forward_mm','model_kind':'o.geometry.model_kind','fit_rms':'o.geometry.fit_rms_mm','contour_goal_valid':'contour_goal_valid','contour_points':'contour_points','circle_radius':'contour_model_valid==1?contour_model_radius:0','circle_x':'contour_model_x','circle_y':'contour_model_y','reconstructed_rows':'reconstructed','blob_side':'vision.blob_side','blob_bottom':'vision.blob_bottom'}
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
for first,last in [(20,32),(50,60)]:
 for speed in (0,500):cases.append((f'segment_{first}_{last}_{speed}',[k for k in unique if first*50<=k<last*50],speed))
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
selected=[]
for name,target in [('segment_20_32_0',23.5),('segment_20_32_0',28.0),('segment_50_60_500',54.0),('segment_50_60_500',55.5)]:
 eligible=[v for (case,k),v in captures.items() if case==name]
 selected.append(min(eligible,key=lambda x:abs(x[0]['time_s']-target)))
for element in (3,4,5,6):
 eligible=[v for v in captures.values() if v[0]['element']==element]
 if eligible:selected.append(max(eligible,key=lambda x:(x[0]['valid'],x[0]['reconstructed_rows'])))
im=Image.new('RGB',(1504,510*len(selected)),'#222');draw=ImageDraw.Draw(im)
for j,(d,rs,g) in enumerate(selected):
 k=d['frame'];a=Image.fromarray(frame(k));current=Image.fromarray(g).convert('RGB')
 for y in range(30,117):
  if rs[360+y]&1:current.putpixel((rs[y],y),(0,255,0))
  if rs[360+y]&2:current.putpixel((rs[120+y],y),(255,255,0))
  if rs[360+y]&4:current.putpixel((rs[240+y],y),(0,220,255) if rs[360+y]&8 else (255,0,0))
 im.paste(a.resize((752,480),Image.Resampling.NEAREST),(0,j*510));im.paste(current.resize((752,480),Image.Resampling.NEAREST),(752,j*510))
 draw.text((0,j*510+484),f'Recorded {k/50:.2f}s frame {k}',fill='white')
 draw.text((752,j*510+484),f"C E{int(d['element'])} V{int(d['valid'])} model{int(d['model_kind'])} rebuilt={int(d['reconstructed_rows'])} {d['case']}",fill='white')
im.save(out/'path-comparison.jpg',quality=95)
(out/'selected.json').write_text(json.dumps([d for d,rs,g in selected],indent=2))
