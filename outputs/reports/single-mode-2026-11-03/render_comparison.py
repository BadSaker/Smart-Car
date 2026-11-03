from pathlib import Path
import ctypes as C,json,zipfile
from PIL import Image,ImageDraw
root=Path.cwd();out=root/'outputs/reports/single-mode-2026-11-03'
l=C.CDLL(str(root/'tmp/single-mode-20261103/replay.dll'));l.run.argtypes=[C.POINTER(C.c_uint8),C.c_uint,C.c_uint,C.POINTER(C.c_float),C.POINTER(C.c_uint8)]
v=(C.c_float*47)();r=(C.c_uint8*480)();before=Image.open(root/'outputs/reports/video1438-audit-2026-11-03/keyframes.jpg')
canvas=Image.new('RGB',(1504,1530),'#222');draw=ImageDraw.Draw(canvas);details=[]
with zipfile.ZipFile(root/'car/tests/fixtures/video1438-cross-ring.zip') as z:
 meta=json.loads(z.read('metadata.json'))['segments']
 for j,(name,target,old_slot) in enumerate([('20-24.5',1146,1),('35-37',1825,4),('40-43',2115,5)]):
  l.reset();info=meta[name];raw=z.read(info['file'])
  for n,m in enumerate(info['frames']):
   b=raw[n*22560:(n+1)*22560];a=(C.c_uint8*22560).from_buffer_copy(b);l.run(a,m['ms'],0,v,r)
   if m['frame']==target:
    current=Image.frombytes('L',(188,120),b).convert('RGB')
    for y in range(30,117):
     if r[360+y]&1:current.putpixel((r[y],y),(0,255,0))
     if r[360+y]&2:current.putpixel((r[120+y],y),(255,255,0))
     if r[360+y]&4:current.putpixel((r[240+y],y),(255,0,0))
    x=old_slot%2*752;y=old_slot//2*510
    canvas.paste(before.crop((x,y,x+752,y+480)),(0,j*510))
    canvas.paste(current.resize((752,480),Image.Resampling.NEAREST),(752,j*510))
    draw.text((0,j*510+484),f'Recorded overlay {target/50:.2f}s',fill='white')
    draw.text((752,j*510+484),f'Current C / unique frames / V{int(v[0])} near={int(v[7])} far={int(v[8])}',fill='white')
    details.append(dict(frame=target,valid=int(v[0]),near=int(v[7]),far=int(v[8])));break
canvas.save(out/'cross-comparison.jpg',quality=95);(out/'cross-comparison.json').write_text(json.dumps(details,indent=2));print(details)
