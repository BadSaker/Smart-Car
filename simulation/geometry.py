import math
from functools import lru_cache
import numpy as np
from PIL import Image, ImageDraw

def draw_road_strip(draw,points,width,color):
    """Union of filled segment rectangles and round joins, in raster coordinates.

    Explicit filled shapes avoid gaps left by wide polyline curve joins.
    Draw the whole edge layer before the road layer so crossings stay open.
    """
    points=np.asarray(points,dtype=float);radius=width/2
    delta=np.diff(points,axis=0);length=np.linalg.norm(delta,axis=1)
    for a,b,d,n in zip(points[:-1],points[1:],delta,length):
        if n<=1e-12:continue
        normal=np.array([-d[1],d[0]])*(radius/n)
        draw.polygon([tuple(a+normal),tuple(b+normal),tuple(b-normal),tuple(a-normal)],fill=color)
    for x,y in points:
        draw.ellipse((x-radius,y-radius,x+radius,y+radius),fill=color)

class Track:
    def __init__(self,p):
        self.p=p;self.parts=[];self.x=1.95;self.y=3.5;self.heading=math.pi;self.length=0.0
        r=p['radius']
        if p['scenario'] in ('midterm','full'):
            # A→B: L95, left U, right 90, L50, right U, left U, L100, left90, L300.
            self.line(.95);self.arc(r,180);self.arc(r,-90);self.line(.5)
            self.arc(r,-180);self.arc(r,180);self.line(1.0);self.arc(r,90);self.line(3.0)
            self.ab_length=self.length;self.marker_a=np.array([1.95,3.5,math.pi]);self.marker_b=np.array([self.x,self.y,self.heading])
            if p['scenario']=='full':
                # Complete topology: cross twice, middle ring, lower right U, right ring, waves, A.
                self.arc(r,90);self.line(.5);self.line(.5);self.arc(r,270);self.line(.5)
                self.line(1.0);self.arc(r,-90);self.arc(r,180);self.line(1.5)
                self.arc(r,360);self.line(1.0);self.arc(r,90);self.line(.5)
                for degrees in (45,-45,-45,45,45,-45,-45,45):self.arc(r,degrees)
                target=np.array([1.95,3.5]);delta=target-[self.x,self.y]
                # The source is a schematic: marked dimensions do not uniquely close its graph.
                # Preserve all marked R/L and explicitly add a computed unmarked connector.
                self.closure_gap=float(np.linalg.norm(delta))
                if self.closure_gap>1e-9:
                    old_h=self.heading;self.heading=math.atan2(delta[1],delta[0]);self.line(self.closure_gap);self.heading=old_h
        elif p['scenario']=='straight':
            self.x=self.y=self.heading=0.0;self.line(8.0)
        else:
            self.x=self.y=self.heading=0.0;self.arc(r,360)
        pieces=[]
        for part in self.parts:
            count=max(2,int(math.ceil(part['length']/.008))+1)
            values=np.linspace(0,part['length'],count)
            xy,angles=self.evaluate(part,values)
            pieces.append(np.column_stack((xy,angles,values+part['s'])))
        self.samples=np.concatenate([x[:-1] for x in pieces[:-1]]+[pieces[-1]])
        if p['scenario']=='full' and p['direction']=='B→A':
            # Start at B, then travel in reverse; rotate a closed metric route before reversing.
            i=int(np.argmin(np.abs(self.samples[:,3]-self.ab_length)))
            core=self.samples[:-1];rotated=np.vstack((core[i:],core[:i],core[i:i+1])).copy()
            distances=np.r_[core[i:,3]-self.ab_length,core[:i,3]+self.length-self.ab_length,self.length]
            rotated[:,3]=distances;self.samples=rotated
        if p['direction']=='B→A':
            self.samples=self.samples[::-1].copy()
            self.samples[:,2]+=math.pi;self.samples[:,3]=self.length-self.samples[:,3]
        self.start=self.samples[0].copy();self.end=self.samples[-1].copy()
        self.render_samples=self.samples
        self.extra_paths=[]
        if p['scenario']=='full':
            # The middle ring has a fourth quadrant visible even when the nominal route exits early.
            ring=next(part for part in self.parts if abs(part['k']*part['length']-1.5*math.pi)<1e-8)
            xy,h=self.evaluate(ring,np.array([ring['length']]))
            extra={'x':xy[0,0],'y':xy[0,1],'h':float(h[0]),'k':1/r,'length':math.pi*r/2,'s':0}
            xy,h=self.evaluate(extra,np.linspace(0,extra['length'],100))
            self.extra_paths.append(np.column_stack((xy,h,np.zeros(len(h)))))
        self.width=p['road_width'];self.cell=.004
        # Only road continuation is visible past the finish; A/B are virtual markers.
        visible=self.samples[:,:2].copy()
        if p['scenario'] not in ('circle','full'):
            a=self.start;b=self.end
            pre=a[:2]-np.array([math.cos(a[2]),math.sin(a[2])])*.8
            post=b[:2]+np.array([math.cos(b[2]),math.sin(b[2])])*.8
            visible=np.vstack((pre,visible,post))
        self.visible=visible
        self.road_paths=[visible]+[part[:,:2] for part in self.extra_paths]
        if p['scenario']=='midterm':
            # Midterm is an evaluation route within the full physical scene, not a cropped track.
            whole=Track({**p,'scenario':'full'})
            self.visible=visible=whole.visible;self.road_paths=whole.road_paths
            self.render_samples=whole.samples;self.extra_paths=whole.extra_paths
            self.minimum=whole.minimum;self.maximum=whole.maximum
        self.road_center_points=np.concatenate(self.road_paths)
        self.minimum=visible.min(axis=0)-1.0;self.maximum=visible.max(axis=0)+1.0
        shape=np.ceil((self.maximum-self.minimum)/self.cell).astype(int)+1
        self.raster=road_raster(tuple(shape),tuple(self.minimum),self.cell,self.width,p['edge_width'],
            p['gray_background'],p['gray_edge'],p['gray_road'],tuple(road.astype(np.float64).tobytes() for road in self.road_paths))
        self.ray_key=None
    def line(self,length):
        part={'x':self.x,'y':self.y,'h':self.heading,'k':0.0,'length':length,'s':self.length}
        self.parts.append(part);self.x+=length*math.cos(self.heading);self.y+=length*math.sin(self.heading);self.length+=length
    def arc(self,radius,degrees):
        angle=math.radians(degrees);k=math.copysign(1/radius,angle);length=abs(angle)*radius
        part={'x':self.x,'y':self.y,'h':self.heading,'k':k,'length':length,'s':self.length};self.parts.append(part)
        xy,h=self.evaluate(part,np.array([length]));self.x,self.y=xy[0];self.heading=h[0];self.length+=length
    @staticmethod
    def evaluate(part,s):
        h=part['h'];k=part['k'];angle=h+k*s
        if k:
            x=part['x']+(np.sin(angle)-math.sin(h))/k
            y=part['y']-(np.cos(angle)-math.cos(h))/k
        else:x=part['x']+s*math.cos(h);y=part['y']+s*math.sin(h)
        return np.column_stack((x,y)),angle
    def nearest(self,x,y,progress_hint=None):
        d=self.samples[:,:2]-[x,y];scores=np.einsum('ij,ij->i',d,d)
        if progress_hint is not None:
            allowed=(self.samples[:,3]>=max(0,progress_hint-.25))&(self.samples[:,3]<=progress_hint+1.0)
            scores=np.where(allowed,scores,np.inf)
        i=int(np.argmin(scores))
        v=self.samples[i];signed=(x-v[0])*(-math.sin(v[2]))+(y-v[1])*math.cos(v[2])
        return i,float(signed),float(v[3])
    def at(self,s):
        s=np.clip(s,0,self.length)
        return np.array([np.interp(s,self.samples[:,3],self.samples[:,j]) for j in range(2)])
    def road_distance(self,x,y):
        d=self.road_center_points-[x,y]
        return float(np.sqrt(np.min(np.einsum('ij,ij->i',d,d))))
    def camera(self,x,y,heading,p,rng=None):
        key=tuple(p[k] for k in ('hfov','fy_ratio','pitch','cx','cy','k1','height','camera_yaw','camera_roll'))+(p.get('camera_samples',1),)
        if self.ray_key!=key:
            self._rays(p);self.ray_key=key
        longitudinal,right,ray_ok=self.rays
        c=math.cos(heading);s=math.sin(heading)
        wx=x+p['camera_offset']*c+longitudinal*c+right*s-p['camera_lateral']*s
        wy=y+p['camera_offset']*s+longitudinal*s-right*c+p['camera_lateral']*c
        gx=(wx-self.minimum[0])/self.cell;gy=(wy-self.minimum[1])/self.cell
        gray=np.full(gx.shape,float(p['gray_background']))
        if p.get('camera_sampling','nearest')=='bilinear':
            ok=ray_ok&(gx>=0)&(gy>=0)&(gx<self.raster.shape[1]-1)&(gy<self.raster.shape[0]-1)
            ix=np.floor(gx[ok]).astype(int);iy=np.floor(gy[ok]).astype(int)
            ax=gx[ok]-ix;ay=gy[ok]-iy
            gray[ok]=((1-ax)*(1-ay)*self.raster[iy,ix]+ax*(1-ay)*self.raster[iy,ix+1]
                      +(1-ax)*ay*self.raster[iy+1,ix]+ax*ay*self.raster[iy+1,ix+1])
        else:
            ix=np.rint(gx).astype(np.int64);iy=np.rint(gy).astype(np.int64)
            ok=ray_ok&(ix>=0)&(iy>=0)&(ix<self.raster.shape[1])&(iy<self.raster.shape[0])
            gray[ok]=self.raster[iy[ok],ix[ok]]
        if gray.ndim==3:gray=gray.mean(axis=0)
        gray=np.rint(gray).astype(np.uint8)
        if p['noise'] and rng is not None:gray=np.clip(gray.astype(float)+rng.normal(0,p['noise'],gray.shape),0,255).astype(np.uint8)
        if p['blackout']:gray.fill(0)
        return gray
    def _rays(self,p):
        from camera_optics import ground_intersections
        cols,rows=np.meshgrid(np.arange(188),np.arange(120))
        count=int(p.get('camera_samples',1))
        if count>1:
            shifts=(np.arange(count)+.5)/count-.5
            offsets=[(x,y) for y in shifts for x in shifts]
            cols=np.stack([cols+x for x,y in offsets]);rows=np.stack([rows+y for x,y in offsets])
        q,valid=ground_intersections(cols,rows,p)
        self.rays=(q[...,0],-q[...,1],valid)

@lru_cache(maxsize=4)
def road_raster(shape,minimum,cell,width,edge_width,background,edge,road,paths):
    image=Image.new('L',shape,int(background));draw=ImageDraw.Draw(image)
    for thickness,color in [(width+2*edge_width,edge),(width,road)]:
        for encoded in paths:
            points=np.frombuffer(encoded,dtype=np.float64).reshape(-1,2)
            draw_road_strip(draw,(points-minimum)/cell,thickness/cell,int(color))
    result=np.asarray(image).copy();result.setflags(write=False)
    return result

def predicted_width(p,row):
    from camera_optics import straight_width
    return straight_width(p,row)

def integrate_pose(x,y,h,v,steer,wheelbase,dt):
    omega=v*math.tan(steer)/wheelbase
    if abs(omega)<1e-10:return x+v*math.cos(h)*dt,y+v*math.sin(h)*dt,h
    new_h=h+omega*dt
    return x+v/omega*(math.sin(new_h)-math.sin(h)),y-v/omega*(math.cos(new_h)-math.cos(h)),new_h
