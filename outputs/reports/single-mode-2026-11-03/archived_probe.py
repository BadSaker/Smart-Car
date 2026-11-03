from pathlib import Path
import ctypes as C,csv,json
import numpy as np
from PIL import Image
root=Path.cwd();out=root/'outputs/reports/single-mode-2026-11-03/earlier-video';out.mkdir(exist_ok=True)
lib=C.CDLL(str(root/'tmp/single-mode-20261103/replay.dll'))
lib.run.argtypes=[C.POINTER(C.c_uint8),C.c_uint,C.c_uint,C.POINTER(C.c_float),C.POINTER(C.c_uint8)]
names=next(csv.reader((root/'outputs/reports/track-diagnosis-2026-11-03/replay-50hz.csv').open()))[4:]
v=(C.c_float*len(names))();r=(C.c_uint8*480)();rows=[]
for path in sorted((root/'outputs/reports/track-diagnosis-2026-11-03').glob('frame-*.png')):
 a=np.array(Image.open(path).convert('RGB'));mask=np.ptp(a,axis=2)>0
 g=np.rint(a[:,:,0]*.299+a[:,:,1]*.587+a[:,:,2]*.114).astype(np.uint8)
 for y in range(120):
  good=np.flatnonzero(~mask[y]);bad=np.flatnonzero(mask[y])
  if len(good):g[y,bad]=np.rint(np.interp(bad,good,g[y,good])).astype(np.uint8)
 g=np.ascontiguousarray(g);lib.reset()
 for n in range(4):lib.run(g.ctypes.data_as(C.POINTER(C.c_uint8)),n*20,0,v,r)
 rows.append(dict(file=path.name,**dict(zip(names,map(float,v)))))
with (out/'static4-snapshots.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=rows[0]);w.writeheader();w.writerows(rows)
summary=dict(snapshots=len(rows),valid=sum(d['valid']>0 for d in rows),elements={e:sum(d['element']==e for d in rows) for e in range(7)},limitations='Original 12:23 AVI is no longer at supplied path. Archived PNGs only; each repeated four times as an appearance probe. Not a temporal replay or a vehicle result.')
(out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
