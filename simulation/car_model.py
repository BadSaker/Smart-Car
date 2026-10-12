"""C1 chassis, metric geometry. Unspecified mechanical details are illustrative.

Rear axle is x=0; forward is +x; left is +y; ground is z=0.
Factory envelope length is metadata, not a substitute for measured plate bounds.
"""
import math
from functools import lru_cache
import numpy as np
from camera_optics import basis,origin

MODEL_KEYS=('wheelbase','track','body_width','tire_width','wheel_diameter',
            'front_overhang','rear_overhang','plate_height','plate_thickness',
            'camera_offset','height','pitch','camera_lateral','camera_yaw','camera_roll','camera_mount_gap')

@lru_cache(maxsize=32)
def plate_outline(wheelbase,width,front,rear):
    # Relative outline traced qualitatively from the C1 photograph, not CAD data.
    edge=[(0,.33),(.05,.34),(.09,.28),(.18,.32),(.30,.48),(.42,.50),
          (.62,.47),(.76,.35),(.89,.30),(.94,.38),(1,.38)]
    length=wheelbase+front+rear
    return np.array([(u*length-rear,v*width) for u,v in edge]+
                    [(u*length-rear,-v*width) for u,v in reversed(edge)])

def outline(p):
    return plate_outline(p['wheelbase'],p['body_width'],p['front_overhang'],p['rear_overhang'])

def world(points,s):
    q=np.array(points,dtype=float,copy=True);c=math.cos(s.h);sn=math.sin(s.h)
    x=q[...,0].copy();y=q[...,1].copy()
    q[...,0]=s.x+c*x-sn*y;q[...,1]=s.y+sn*x+c*y
    return q

def wheel_poses(s):
    p=s.p;curvature=math.tan(s.steer)/p['wheelbase'];result=[]
    for u in (0,p['wheelbase']):
        for v in (-p['track']/2,p['track']/2):
            angle=math.atan2(p['wheelbase']*curvature,1-v*curvature) if u else 0.
            result.append((u,v,angle))
    return result

def tire_footprints(s):
    p=s.p;a=p['wheel_diameter']/2;b=p['tire_width']/2
    corners=np.array([[-a,-b],[a,-b],[a,b],[-a,b]])
    result=[]
    for u,v,angle in wheel_poses(s):
        c=math.cos(angle);sn=math.sin(angle)
        local=corners@np.array([[c,sn],[-sn,c]])+np.array([u,v])
        result.append(world(local,s))
    return result

def _box(faces,colors,center,extent,color):
    x,y,z=center;a,b,c=np.array(extent)/2
    q=np.array([[x-a,y-b,z-c],[x+a,y-b,z-c],[x+a,y+b,z-c],[x-a,y+b,z-c],
                [x-a,y-b,z+c],[x+a,y-b,z+c],[x+a,y+b,z+c],[x-a,y+b,z+c]])
    for indices in ((0,1,2,3),(4,7,6,5),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)):
        faces.append(q[list(indices)]);colors.append(color)

def _cylinder(faces,colors,center,radius,length,axis,color,cap,segments=12):
    a=np.arange(segments)*2*math.pi/segments
    q=np.column_stack((radius*np.cos(a),radius*np.sin(a)))
    rings=[]
    for offset in (-length/2,length/2):
        ring=np.empty((segments,3));others=[j for j in range(3) if j!=axis]
        ring[:,axis]=offset;ring[:,others]=q;ring+=np.array(center);rings.append(ring)
    faces.extend(rings);colors.extend([cap,cap])
    for i in range(segments):
        j=(i+1)%segments
        faces.append(np.array([rings[0][i],rings[0][j],rings[1][j],rings[1][i]]))
        colors.append(color if i%2 else tuple(min(255,k+12) for k in color))

def _pack(faces,colors):
    count=max(map(len,faces));mesh=np.empty((len(faces),count,3))
    for i,face in enumerate(faces):
        mesh[i,:len(face)]=face;mesh[i,len(face):]=face[-1]
    return mesh,tuple(colors)

@lru_cache(maxsize=24)
def _static_mesh(values):
    p=dict(zip(MODEL_KEYS,values));faces=[];colors=[]
    shape=outline(p);bottom=np.column_stack((shape,np.full(len(shape),p['plate_height'])))
    top=bottom.copy();top[:,2]+=p['plate_thickness']
    faces.extend([bottom,top]);colors.extend([(22,25,30),(43,47,53)])
    for i in range(len(shape)):
        j=(i+1)%len(shape);faces.append(np.array([bottom[i],bottom[j],top[j],top[i]]));colors.append((65,69,75))
    z=top[0,2]+.0003;length=p['wheelbase']+p['front_overhang']+p['rear_overhang']
    # Recess-colored slot/fastener surfaces; locations approximate the product photo.
    for u in (.32,.65):
        for v in (-.035,-.025,.025,.035):
            _box(faces,colors,(u*length-p['rear_overhang'],v,z),(.032,.004,.0004),(9,12,16))
    for u in (.22,.42,.62,.82):
        for v in (-.013,.013):
            _cylinder(faces,colors,(u*length-p['rear_overhang'],v,z+.0005),.0018,.0006,2,(10,12,15),(10,12,15),6)
    radius=p['wheel_diameter']/2
    for u in (0,p['wheelbase']):
        _box(faces,colors,(u,0,radius),(.012,p['track']-.018,.012),(99,104,111))
        for side in (-1,1):
            _box(faces,colors,(u,side*(p['track']/2-.016),radius+.01),(.024,.013,.024),(34,38,44))
    # Two RS380-style motors: dimensions and mounting heights are visual assumptions.
    for side in (-1,1):
        _cylinder(faces,colors,(.018,side*.032,.054),.018,.037,0,(149,155,161),(92,98,105))
        _box(faces,colors,(.018,side*.032,.032),(.043,.041,.012),(37,42,49))
        _cylinder(faces,colors,(0,side*(p['track']/2-.018),radius),.026,.006,1,(73,77,83),(181,184,189),16)
    _box(faces,colors,(p['wheelbase']-.015,0,radius+.016),(.007,p['track']-.040,.005),(194,197,202))
    # Camera post and optical-axis-oriented housing; no assumed user electronics.
    post_bottom=p['plate_height']+p['plate_thickness'];post_top=p['height']-.010
    post_x=p['camera_offset']-p['camera_mount_gap']
    _box(faces,colors,(post_x,p['camera_lateral'],(post_bottom+post_top)/2),(.007,.007,post_top-post_bottom),(30,34,39))
    _box(faces,colors,(post_x,p['camera_lateral'],post_bottom+.004),(.026,.023,.008),(44,55,69))
    _box(faces,colors,(post_x+.006,p['camera_lateral'],p['height']+.006),(.008,.038,.010),(35,39,44))
    camera_faces=[];camera_colors=[]
    _box(camera_faces,camera_colors,(-.011,0,0),(.002,.035,.030),(121,113,55))
    _box(camera_faces,camera_colors,(0,0,0),(.018,.029,.021),(46,63,85))
    _cylinder(camera_faces,camera_colors,(.013,0,0),.007,.009,0,(56,66,76),(53,136,190))
    forward,right,down=basis(p);rotation=np.column_stack((forward,-right,-down))
    # The projection origin is the lens front center; move the housing behind it.
    camera_origin=origin(p)-.0175*forward
    for side in (-1,1):
        start=np.array([post_x+.010,p['camera_lateral']+side*.013,p['height']+.006])
        end=camera_origin+rotation@np.array([-.011,side*.013,.009])
        vector=end-start;length=np.linalg.norm(vector);axis=vector/length
        transverse=np.cross(axis,[0,0,1] if abs(axis[2])<.9 else [0,1,0]);transverse/=np.linalg.norm(transverse)
        mount_rotation=np.column_stack((axis,transverse,np.cross(axis,transverse)))
        mount_faces=[];mount_colors=[]
        _cylinder(mount_faces,mount_colors,(0,0,0),.003,length,0,(221,222,212),(182,155,86),8)
        for face,color in zip(mount_faces,mount_colors):faces.append(face@mount_rotation.T+(start+end)/2);colors.append(color)
    for face,color in zip(camera_faces,camera_colors):
        q=face@rotation.T+camera_origin;faces.append(q);colors.append(color)
    return _pack(faces,colors)

@lru_cache(maxsize=16)
def _wheel_mesh(diameter,width):
    faces=[];colors=[];r=diameter/2
    _cylinder(faces,colors,(0,0,r),r,width,1,(20,23,27),(34,37,42),16)
    for side in (-1,1):
        y=side*(width/2+.00025)
        _cylinder(faces,colors,(0,y,r),r*.68,.0005,1,(148,36,39),(185,42,45),12)
        _cylinder(faces,colors,(0,y+side*.0005,r),r*.49,.0005,1,(69,72,78),(112,118,126),10)
        _cylinder(faces,colors,(0,y+side*.001,r),r*.18,.0006,1,(178,184,191),(178,184,191),8)
        for a in np.arange(5)*2*math.pi/5:
            x=math.cos(a)*r*.36;z=r+math.sin(a)*r*.36
            _cylinder(faces,colors,(x,y+side*.001,z),r*.09,.0005,1,(26,29,34),(26,29,34),6)
    return _pack(faces,colors)

def mesh(s):
    p=s.p;static,colors=_static_mesh(tuple(p[k] for k in MODEL_KEYS))
    wheel,wcolors=_wheel_mesh(p['wheel_diameter'],p['tire_width']);parts=[static];palette=list(colors)
    for u,v,angle in wheel_poses(s):
        q=wheel.copy();c=math.cos(angle);sn=math.sin(angle)
        q[:,:,0]=wheel[:,:,0]*c-wheel[:,:,1]*sn+u
        q[:,:,1]=wheel[:,:,0]*sn+wheel[:,:,1]*c+v
        # Pack cached meshes to a common polygon size for one projection call.
        parts.append(q);palette.extend(wcolors)
    count=max(part.shape[1] for part in parts)
    packed=[np.concatenate((part,np.repeat(part[:,-1:,:],count-part.shape[1],axis=1)),axis=1) for part in parts]
    return world(np.concatenate(packed),s),palette

def draw_3d(s,draw,project):
    faces,colors=mesh(s);vertices,depth=project(faces.reshape(-1,3))
    vertices=vertices.reshape(faces.shape[0],faces.shape[1],2);depth=depth.reshape(faces.shape[:2])
    # Subpixel slots, hub holes and side faces need no individual draw at track scale.
    # Keep detailed geometry when zoomed in; batch screen-size filtering in NumPy.
    extent=np.ptp(vertices,axis=1)
    visible=(depth.min(axis=1)>.05)&(extent[:,0]*extent[:,1]>.6)
    indices=np.flatnonzero(visible);indices=indices[np.argsort(-depth.mean(axis=1)[indices],kind='stable')]
    for i in indices:draw.polygon(vertices[i].ravel().tolist(),fill=colors[i])

def draw_2d(s,draw,xy):
    draw.polygon([xy(v) for v in world(outline(s.p),s)],fill='#30363e',outline='#929ba7')
    for points in tire_footprints(s):draw.polygon([xy(v) for v in points],fill='#0a1018',outline='#b0bac5')
    for u in (0,s.p['wheelbase']):
        points=world(np.array([[u,-s.p['track']/2],[u,s.p['track']/2]]),s)
        draw.line([xy(v) for v in points],fill='#a5aeb9',width=1)
