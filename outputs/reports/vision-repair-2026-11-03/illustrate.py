from pathlib import Path
s=Path('outputs/reports/vision-repair-2026-11-03/segments.py').read_text();exec(s[:s.index('cases=')])
from PIL import Image,ImageDraw,ImageFont
font=ImageFont.truetype('C:/Windows/Fonts/consola.ttf',17)
shots=[]
for a,z,times in [(17,21,[19.28]),(33,43,[35.0,40.1,41.16,41.42]),(46,59,[46.76,53.5])]:
 lib.reset();ks=[k for k in unique if a<=k/50<=z];chosen={min(ks,key=lambda k:abs(k/50-t)) for t in times}
 for k in ks:
  g=gray(k);lib.run(g.ctypes.data_as(C.POINTER(C.c_uint8)),k*20,(k-ks[0])*800//50,v,r)
  if k not in chosen:continue
  d=dict(zip(names,map(float,v)));pixels=bytes(r);new=Image.fromarray(g).convert('RGB');draw=ImageDraw.Draw(new)
  for y in range(30,117):
   flags=pixels[360+y]
   if flags&1:draw.point((pixels[y],y),fill='#00ff00')
   if flags&2:draw.point((pixels[120+y],y),fill='#ffff00')
   if flags&8:draw.point((pixels[240+y],y),fill='#ff3030')
  shots.append((k,frame(k),new,d))
im=Image.new('RGB',(1128,len(shots)*400),'#202020');dr=ImageDraw.Draw(im)
for n,(k,old,new,d) in enumerate(shots):
 y=n*400;im.paste(Image.fromarray(old).resize((564,360)),(0,y));im.paste(new.resize((564,360)),(564,y))
 dr.text((5,y+362),f'{k/50:.2f}s Recorded overlay',font=font,fill='white')
 dr.text((570,y+362),f'Current E{int(d["element"])} V{int(d["valid"])} X={d["geom_x"]:.0f} Y={d["geom_y"]:.0f}',font=font,fill='white')
im.save(out/'path-comparison.jpg',quality=96)
im.crop((0,800,1128,1600)).save(out/'island-entry.jpg',quality=96)
