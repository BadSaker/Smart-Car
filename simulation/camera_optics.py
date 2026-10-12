"""Shared camera pose for ground sampling, calibration and scene frustum.
Body axes: forward +x, left +y, up +z. Positive yaw points left.
Positive roll rotates the camera right basis toward its down basis.
"""
import math
import numpy as np

def basis(p):
    t=math.radians(p['pitch']);a=math.radians(p.get('camera_yaw',0));r=math.radians(p.get('camera_roll',0))
    forward=np.array([math.cos(t)*math.cos(a),math.cos(t)*math.sin(a),-math.sin(t)])
    right=np.array([math.sin(a),-math.cos(a),0.])
    down=np.cross(forward,right)
    return forward,right*math.cos(r)+down*math.sin(r),down*math.cos(r)-right*math.sin(r)

def origin(p):return np.array([p['camera_offset'],p.get('camera_lateral',0),p['height']])

def ray(u,row,p):
    f=94/math.tan(math.radians(p['hfov']/2));dx=np.asarray(u)-p['cx'];dy=np.asarray(row)-p['cy']
    ux=dx.copy();uy=dy.copy();scale=math.hypot(94,60)
    for _ in range(10):
        factor=1+p['k1']*(ux*ux+uy*uy)/scale**2;ux=dx/factor;uy=dy/factor
    forward,right,down=basis(p)
    return forward+np.asarray(ux/f)[...,None]*right+np.asarray(uy/(f*p['fy_ratio']))[...,None]*down

def project_ground(points,p):
    """Points in rear-axle body coordinates (metres); return u,v and forward validity."""
    points=np.asarray(points,dtype=float).reshape(-1,2)
    q=np.column_stack((points,np.zeros(len(points))))-origin(p)
    forward,right,down=basis(p);depth=q@forward;safe=np.maximum(depth,1e-9)
    f=94/math.tan(math.radians(p['hfov']/2));dx=f*(q@right)/safe;dy=f*p['fy_ratio']*(q@down)/safe
    factor=1+p['k1']*(dx*dx+dy*dy)/(94**2+60**2)
    return np.column_stack((p['cx']+dx*factor,p['cy']+dy*factor)),depth>0

def ground_intersections(u,row,p,max_depth=15.):
    """返回镜头相对地面交点；深度沿光轴，允许镜头垂足后方的可见地面。"""
    direction=ray(u,row,p)
    depth=p['height']/np.maximum(-direction[...,2],1e-12)
    return direction*depth[...,None],(direction[...,2]<0)&(depth<max_depth)


def straight_edges(p,row):
    """计算居中直道两侧投影，包含同一径向模型；画面外的边缘不裁切。"""
    f=94/math.tan(math.radians(p['hfov']/2));F,R,D=basis(p)
    columns=[]
    for y in (-p['road_width']/2,p['road_width']/2):
        lateral=y-p.get('camera_lateral',0)
        a=(lateral*R[2]+p['height']*R[1])/f
        b=(lateral*D[2]+p['height']*D[1])/(f*p['fy_ratio'])
        c=lateral*F[2]+p['height']*F[1]
        if abs(a)<1e-12:return None
        dy=float(row-p['cy'])
        # 地面直线在无畸变像面上仍是直线，解其畸变后与指定行的交点。
        for _ in range(15):
            dx=-(b*dy+c)/a
            factor=1+p['k1']*(dx*dx+dy*dy)/(94**2+60**2)
            residual=dy*factor-(row-p['cy'])
            if abs(residual)<1e-10:break
            derivative=factor+dy*p['k1']*2*(dx*(-b/a)+dy)/(94**2+60**2)
            if abs(derivative)<1e-12:return None
            dy-=residual/derivative
        else:return None
        direction=F+(dx/f)*R+(dy/(f*p['fy_ratio']))*D
        radial_derivative=1+3*p['k1']*(dx*dx+dy*dy)/(94**2+60**2)
        if direction[2]>=0 or factor<=0 or radial_derivative<=0:return None
        column=float(p['cx']+dx*factor)
        # 排除负畸变折叠导致的第二分支；表格必须与渲染实际使用的射线一致。
        check=ray(column,float(row),p)
        if check[2]>=0:return None
        lateral_check=p.get('camera_lateral',0)-p['height']*check[1]/check[2]
        if abs(lateral_check-y)>1e-7:return None
        columns.append(column)
    return sorted(columns)


def straight_width(p,row):
    if not p.get('camera_yaw',0) and not p.get('camera_roll',0) and not p['k1']:
        f=94/math.tan(math.radians(p['hfov']/2));t=math.radians(p['pitch'])
        down=math.sin(t)+(row-p['cy'])/(f*p['fy_ratio'])*math.cos(t)
        return p['road_width']/p['height']*f*down if down>0 else None
    columns=straight_edges(p,row)
    return None if columns is None else columns[1]-columns[0]
