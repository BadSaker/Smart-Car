"""Offline 2D map and genuine perspective 3D scene; no network renderer."""
import math
import os
from pathlib import Path
from functools import lru_cache
from collections import OrderedDict
import numpy as np
from PIL import Image,ImageDraw,ImageFont
from car_model import outline,world,draw_2d,draw_3d
from camera_optics import ray,origin
from geometry import draw_road_strip

FONT_PATH=str(Path(os.environ.get('WINDIR') or os.environ.get('SystemRoot') or '')/'Fonts/msyh.ttc')
MAP_BACKGROUNDS=OrderedDict()
@lru_cache(maxsize=16)
def font(size):
    try:return ImageFont.truetype(FONT_PATH,size)
    except OSError:return ImageFont.load_default()
def car_points(s):
    return world(outline(s.p),s)
def map_image(s,size=(760,550)):
    w,h=size;t=s.track
    lo=t.visible.min(axis=0)-.45;hi=t.visible.max(axis=0)+.45
    scale=min((w-40)/(hi[0]-lo[0]),(h-70)/(hi[1]-lo[1]));center=(lo+hi)/2
    def xy(v):return (w/2+(v[0]-center[0])*scale,h/2+20-(v[1]-center[1])*scale)
    cached=getattr(t,'_map_background',None)
    if cached is None or cached[0]!=size:
        cache_key=(id(t.raster),size,t.p['scenario'],t.p['direction'])
        shared=MAP_BACKGROUNDS.get(cache_key)
        if shared is not None:
            MAP_BACKGROUNDS.move_to_end(cache_key);t._map_background=(size,shared[1])
            cached=t._map_background
    if cached is None or cached[0]!=size:
        im=Image.new('RGB',size,'#101d2d');d=ImageDraw.Draw(im)
        for gx in np.arange(math.floor(lo[0]),hi[0]+1,.5):d.line([xy((gx,lo[1])),xy((gx,hi[1]))],fill='#1c2c40')
        for gy in np.arange(math.floor(lo[1]),hi[1]+1,.5):d.line([xy((lo[0],gy)),xy((hi[0],gy))],fill='#1c2c40')
        for width,color in [(t.width+2*s.p['edge_width'],'#03080e'),(t.width,'#dce4e9')]:
            for road in t.road_paths:
                # Subpixel dense samples do not need thousands of Pillow curve joins.
                points=road[::4]
                if not np.array_equal(points[-1],road[-1]):points=np.vstack((points,road[-1]))
                draw_road_strip(d,[xy(v) for v in points],max(2,width*scale),color)
        d.line([xy(v) for v in t.samples[::5,:2]],fill='#8b9cae',width=1)
        t._map_background=(size,im)
        # Retain the raster with the image so its identity cannot be recycled.
        MAP_BACKGROUNDS[cache_key]=(t.raster,im)
        while len(MAP_BACKGROUNDS)>8:MAP_BACKGROUNDS.popitem(last=False)
    im=t._map_background[1].copy();d=ImageDraw.Draw(im)
    if len(s.trajectory)>1:
        points=np.asarray(s.trajectory)
        projected=np.column_stack((w/2+(points[:,0]-center[0])*scale,h/2+20-(points[:,1]-center[1])*scale))
        d.line(projected.ravel().tolist(),fill='#13bfa9',width=3)
    marks=([('A',t.marker_a,'#38d3b4'),('B',t.marker_b,'#ff9472')] if s.p['scenario'] in ('full','midterm') else
           [('起点',t.start,'#38d3b4'),('终点',t.end,'#ff9472')]) if s.p['show_ab'] else []
    for label,a,color in marks:
        normal=np.array([-math.sin(a[2]),math.cos(a[2])])*t.width*.65
        d.line([xy(a[:2]-normal),xy(a[:2]+normal)],fill=color,width=3)
        px,py=xy(a[:2]);d.rounded_rectangle((px-23,py-30,px+23,py-10),radius=5,fill='#15283e')
        d.text((px-16,py-30),label,font=font(13),fill=color)
    draw_2d(s,d,xy)
    camera=world(origin(s.p)[:2],s)
    cp=xy(camera);d.ellipse((cp[0]-3,cp[1]-3,cp[0]+3,cp[1]+3),fill='#408cff')
    route_name='完整赛道' if s.p['scenario']=='full' else s.p['direction']
    d.text((18,12),f"{route_name}   路径长度 {t.length:.2f} m",font=font(16),fill='#e4edf6')
    d.text((18,h-25),'青色为行驶轨迹，网格间距 0.5 m',font=font(12),fill='#a7b7ca')
    return im

def scene_image(s,size=(760,550),yaw=-55,elevation=48,zoom=1.0,follow=False,model_only=False):
    w,h=size;im=Image.new('RGB',size,'#101d2d');d=ImageDraw.Draw(im);t=s.track
    center=np.r_[((t.visible.min(axis=0)+t.visible.max(axis=0))/2),0.0]
    span=max(np.ptp(t.visible[:,0]),np.ptp(t.visible[:,1]),2)
    if follow:center=np.array([s.x,s.y,.12]);span=2.0
    if model_only:center=np.array([s.x+.095*math.cos(s.h),s.y+.095*math.sin(s.h),.13]);span=.52
    a=math.radians(yaw);e=math.radians(elevation);distance=span*1.5/zoom
    eye=center+distance*np.array([math.cos(e)*math.cos(a),math.cos(e)*math.sin(a),math.sin(e)])
    forward=(center-eye);forward/=np.linalg.norm(forward)
    right=np.cross(forward,[0,0,1]);right/=np.linalg.norm(right);up=np.cross(right,forward)
    f=min(w,h)*1.15
    def project(points):
        q=np.asarray(points,dtype=float)-eye
        depth=q@forward;safe=np.maximum(depth,.01)
        return np.column_stack((w/2+f*(q@right)/safe,h/2-f*(q@up)/safe)),depth
    def line3(points,color,width=1):
        v,depth=project(points)
        if np.min(depth)>.05:d.line(v.ravel().tolist(),fill=color,width=width)
    # A bounded last-view cache, with immutable metric mesh shared by moving/follow views.
    view_key=(size,float(yaw),float(elevation),float(zoom),tuple(center),float(span))
    cached=getattr(t,'_scene_background',None)
    view_key+=(model_only,)
    if model_only:
        for gx in np.arange(s.x-.4,s.x+.5,.05):line3([(gx,s.y-.4,0),(gx,s.y+.4,0)],'#1e3045')
        for gy in np.arange(s.y-.4,s.y+.5,.05):line3([(s.x-.4,gy,0),(s.x+.4,gy,0)],'#1e3045')
    elif cached is None or cached[0]!=view_key:
        for gx in np.arange(math.floor(t.minimum[0]),t.maximum[0],.5):line3([(gx,t.minimum[1],0),(gx,t.maximum[1],0)],'#1e3045')
        for gy in np.arange(math.floor(t.minimum[1]),t.maximum[1],.5):line3([(t.minimum[0],gy,0),(t.maximum[0],gy,0)],'#1e3045')
        if not hasattr(t,'_scene_mesh'):
            samples=t.render_samples[::4]
            if not np.array_equal(samples[-1],t.render_samples[-1]):samples=np.vstack((samples,t.render_samples[-1]))
            if s.p['scenario']=='straight':
                samples=np.vstack((np.r_[t.visible[0],t.start[2],-.8],samples,np.r_[t.visible[-1],t.end[2],t.length+.8]))
            paths=[samples,*t.extra_paths]
            start=np.concatenate([path[:-1] for path in paths]);end=np.concatenate([path[1:] for path in paths])
            na=np.column_stack((-np.sin(start[:,2]),np.cos(start[:,2])))
            nb=np.column_stack((-np.sin(end[:,2]),np.cos(end[:,2])))
            meshes=[]
            for low,high in ((-t.width/2,t.width/2),(t.width/2,t.width/2+s.p['edge_width']),
                             (-t.width/2-s.p['edge_width'],-t.width/2)):
                mesh=np.empty((len(start),4,3));mesh[:,:,2]=.005
                mesh[:,0,:2]=start[:,:2]+na*low;mesh[:,1,:2]=end[:,:2]+nb*low
                mesh[:,2,:2]=end[:,:2]+nb*high;mesh[:,3,:2]=start[:,:2]+na*high;meshes.append(mesh)
            t._scene_mesh=np.stack(meshes,axis=1).reshape(-1,4,3)
        vertices,depth=project(t._scene_mesh.reshape(-1,3));vertices=vertices.reshape(-1,4,2)
        depth=depth.reshape(-1,4).mean(axis=1)
        order=np.argsort(-depth,kind='stable')
        # Follow views see only a small fraction of the complete course. Do not
        # send polygons wholly outside the viewport through Pillow's rasterizer.
        visible=depth>.05
        if follow:
            low=vertices.min(axis=1);high=vertices.max(axis=1)
            visible&=(high[:,0]>=-2)&(high[:,1]>=-2)&(low[:,0]<=w+2)&(low[:,1]<=h+2)
        for layer,color in ((False,'#070d16'),(True,'#dce4e9')):
            indices=order[visible[order]&((order%3==0)==layer)]
            for i in indices:d.polygon(vertices[i].ravel().tolist(),fill=color)
        t._scene_background=(view_key,im)
    if not model_only:im=t._scene_background[1].copy();d=ImageDraw.Draw(im)
    if not model_only and len(s.trajectory)>1:line3([(*v,.012) for v in s.trajectory],'#13bfa9',2)
    marks=([('A',t.marker_a,'#38d3b4'),('B',t.marker_b,'#ff9472')] if s.p['scenario'] in ('full','midterm') else
           [('起点',t.start,'#38d3b4'),('终点',t.end,'#ff9472')]) if s.p['show_ab'] else []
    for label,v,color in ([] if model_only else marks):
        n=np.array([-math.sin(v[2]),math.cos(v[2])])*t.width*.6
        line3([(* (v[:2]-n),.02),(* (v[:2]+n),.02)],color,3)
        anchor,_=project([(*v[:2],.10)]);d.text(tuple(anchor[0]),label,font=font(13),fill=color)
    draw_3d(s,d,project)
    camera=world(origin(s.p),s);cx,cy=camera[:2]
    # Frustum corner rays, intersecting ground when directed downward.
    camera_origin=(cx,cy,s.p['height'])
    for u,row in (() if model_only else ((0,0),(187,0),(0,119),(187,119))):
        direction=ray(u,row,s.p);length=min(2.5,s.p['height']/-direction[2]) if direction[2]<0 else 1.0
        endpoint=world(origin(s.p)+length*direction,s)
        line3([camera_origin,endpoint],'#4583b5',1)
    title='C1 车模' if model_only else '赛道与相机视野'
    d.text((18,12),title,font=font(16),fill='#e4edf6')
    if model_only:
        p=s.p
        d.text((18,h-47),f"轴距 {p['wheelbase']*1000:.0f}mm  外沿宽 {(p['track']+p['tire_width'])*1000:.0f}mm  轮胎 Ø{p['wheel_diameter']*1000:.0f}×{p['tire_width']*1000:.0f}mm",font=font(12),fill='#c4d1df')
    d.text((18,h-25),'拖动旋转，滚轮缩放' if model_only else '拖动旋转，滚轮缩放，可开启跟车视角',font=font(12),fill='#a7b7ca')
    return im

def camera_image(s,size=(376,240)):
    im=Image.fromarray(s.image).convert('RGB').resize(size,Image.Resampling.NEAREST);d=ImageDraw.Draw(im)
    sx=size[0]/188;sy=size[1]/120
    for row in range(30,117):
        flags=int(s.rows[3,row])
        for j,bit,color in ((0,1,'#1bcee0'),(1,2,'#ff8b68'),(2,4,'#73ed86')):
            if flags & bit:
                x=int(s.rows[j,row])*sx;y=row*sy;d.rectangle((x,y,x+sx,y+sy),fill=color)
            elif j<2 and flags&8:
                x=int(s.rows[j,row])*sx;y=row*sy;d.rectangle((x,y,x+sx,y+sy),fill='#e7baff')
    d.line((93*sx,0,93*sx,size[1]),fill='#f3ce68',width=1)
    return im

def chart_image(s,size=(376,175)):
    w,h=size;im=Image.new('RGB',size,'#101d2d');d=ImageDraw.Draw(im)
    data=s.history[-400:];margin=27
    for gy in range(margin,h-margin,25):d.line((margin,gy,w-12,gy),fill='#26374a')
    if len(data)>1:
        series=[('Q','#41d2b3',100),('speed_mm_s','#f4bd63',500),('steer_deg','#669eff',60)]
        for key,color,limit in series:
            vals=[row[key]/limit if key!='steer_deg' else (row[key]/limit+.5) for row in data]
            points=[(margin+i*(w-margin-12)/(len(data)-1),h-margin-v*(h-2*margin)) for i,v in enumerate(vals)]
            d.line(points,fill=color,width=2)
    d.text((12,3),'Q 0–100',font=font(11),fill='#41d2b3')
    d.text((100,3),'速度 0–500mm/s',font=font(11),fill='#f4bd63')
    d.text((245,3),'转角 ±30°',font=font(11),fill='#669eff')
    d.text((12,h-20),'最近20s · CSV保留完整数值',font=font(11),fill='#a7b7ca')
    return im
