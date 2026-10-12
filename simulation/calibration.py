"""固定实测高度，由直道宽度条件拟合俯角和有效水平视场。"""
import itertools, math
import numpy as np
from camera_optics import straight_width

ROWS=(30,45,70,86,108,116)
WIDTHS=(44.,59.,97.,120.,156.,167.)

def references(p):
    return dict(zip(ROWS,p.get('reference_widths',WIDTHS)))

def comparison(p):
    rows=[]
    for row,target in references(p).items():
        value=straight_width(p,row)
        rows.append({'row':row,'reference':target,'predicted':value,
                     'error':None if value is None else value-target})
    errors=[r['error'] for r in rows if r['error'] is not None]
    return rows, (float(np.sqrt(np.mean(np.square(errors)))) if errors else None), (max(map(abs,errors)) if errors else None)

def fit(p,objective='maximum'):
    if objective not in ('average','maximum'):
        raise ValueError('未知拟合目标。')
    if p['k1'] or p['camera_yaw'] or p['camera_roll']:
        raise ValueError('六行宽度拟合要求无畸变、偏航和横滚；请先核对直道测量条件。')
    y=np.array(ROWS,float);w=np.array(p['reference_widths'],float)
    a,b=np.linalg.lstsq(np.column_stack((y,np.ones(len(y)))),w,rcond=None)[0]
    if objective=='maximum':
        best=None
        for ids in itertools.combinations(range(len(y)),3):
            for signs in itertools.product((-1,1),repeat=3):
                matrix=np.array([[y[i],1,-s] for i,s in zip(ids,signs)])
                try:aa,bb,t=np.linalg.solve(matrix,w[list(ids)])
                except np.linalg.LinAlgError:continue
                if t>=-1e-9 and np.max(np.abs(aa*y+bb-w))<=t+1e-8 and (best is None or t<best[2]):best=(aa,bb,t)
        if best is None:raise ValueError('不能求出六行最大误差拟合。')
        a,b,_=best
    if a<=0:raise ValueError('实测宽度必须整体随行号增加。')
    height=p['height']
    cosine=a*height*p['fy_ratio']/p['road_width']
    if not 0<cosine<1:
        raise ValueError('实测高度、道路宽度和行宽不相容；请核对测量，不会自动改变高度。')
    angle=math.acos(cosine)
    focal=(b+a*p['cy'])*height/(p['road_width']*math.sin(angle))
    if focal<=0:raise ValueError('拟合焦距非正，请核对主点和行宽。')
    hfov=math.degrees(2*math.atan(94/focal))
    if not 1<=math.degrees(angle)<=85 or not 20<=hfov<=165:
        raise ValueError('条件拟合的俯角或有效水平视场超出允许范围。')
    candidate=p.copy();candidate.update(pitch=math.degrees(angle),hfov=hfov,
        camera_mode='fixed_height_fit',fitted_pitch=math.degrees(angle),fitted_hfov=hfov)
    return candidate
