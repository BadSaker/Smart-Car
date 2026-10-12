import csv, json, math, time
from collections import deque
from pathlib import Path
import numpy as np
from PIL import Image
from parameters import ROOT,validate
from geometry import Track,integrate_pose,predicted_width
from native import Native
from perturbation import Perturbation

STATES=['待启动','中位等待','运行','终点推进','已结束','已停止','故障']
FAULTS=['无','配置错误','摄像头超时','路径丢失','编码器超时','编码器反向','超速','堵转','舵机服务超时','元素故障','运行超时','终点超时','启动未就绪']
ELEMENTS=['直道','弯道','十字','环岛候选','环岛入口','环岛内','环岛出口']
REASONS=['接受','视觉无效','近场不足','Q低','外侧近场不足','外侧远场不足','外侧连续性不足']

class Simulation:
    dt=.01
    def __init__(self,p):
        self.p=validate(p.copy());p=self.p;self.perturbation=Perturbation(p);self.brightness_offset=0.
        self.track=Track(p);self.native=Native(p)
        actual_offset=p['offset']+self.perturbation.offset
        a=self.track.start;self.h=a[2]+math.radians(p['heading_error']+self.perturbation.heading)
        # Pose is referenced to the rear axle; place the front axle midpoint on the start line.
        self.x=a[0]-math.sin(a[2])*actual_offset-p['wheelbase']*math.cos(self.h)
        self.y=a[1]+math.cos(a[2])*actual_offset-p['wheelbase']*math.sin(self.h)
        self.now=0;self.next_frame=0.0
        self.pulse=p['servo_center'];self.steer=0.;self.last_servo=0.;self.v=0.;self.left_v=self.right_v=0.
        self.count_total=np.zeros(2);self.last_counts=np.zeros(2,dtype=int);self.measured=np.zeros(2)
        self.rng=np.random.default_rng(int(p['seed']));self.distance=0.;self.started=False;self.stop_requested=False
        self.data=np.zeros(22);self.rows=np.zeros((4,120),dtype=np.uint8);self.image=np.zeros((120,188),dtype=np.uint8)
        self.applied_duties=np.zeros(2);self.applied_command=0.
        self.frame_id=0;self.frame_time_ms=0;self.frame_pose=(self.x,self.y,self.h);self._new_frame=False
        self.frame_buffer=deque(maxlen=500);self.stop_event=None;self.event_frames=[]
        self.history=[];self.trajectory=[(self.x,self.y)];self.completed=False;self.max_error=0.;self.max_outside=0
        self.replay=False;self._frame();self.data,self.rows=self.native.data();self.capture_frame();self.record()
        self.route_progress=0.
    def _frame(self):
        camera_p=self.p if self.perturbation.enabled else {**self.p,'noise':0.}
        self.image=self.track.camera(self.x,self.y,self.h,camera_p,self.rng)
        self.brightness_offset=self.perturbation.brightness(self.now)
        if self.brightness_offset and not self.p['blackout']:
            self.image=np.clip(self.image.astype(float)+self.brightness_offset,0,255).astype(np.uint8)
        self.native.vision(self.image,self.now,round(self.distance*1000))
        self.frame_id+=1;self.frame_time_ms=self.now;self.frame_pose=(self.x,self.y,self.h);self._new_frame=True
    def path_diagnostic(self,locked=True):
        if locked and self.stop_event is not None:return self.stop_event['path'].copy()
        q=int(self.data[3]);minimum=int(self.data[16]) or int(self.p['min_q'])
        valid=bool(self.data[4]);fresh=self.now-self.frame_time_ms<=150
        accepted=valid and q>=minimum and not (int(self.data[20])&1) and fresh
        reason=REASONS[int(self.data[10])]
        if accepted:reason='接受'
        elif not fresh:reason='摄像头超时'
        elif self.stop_event is not None:
            reason='视觉无效' if not valid else ('Q低' if q<minimum else '元素故障')
        elif not valid and reason=='接受':reason='视觉无效'
        elif q<minimum and reason=='接受':reason='Q低'
        return {'confidence':q,'minimum':minimum,'vision_valid':valid,'accepted':bool(accepted),
                'reason':reason,'element':int(self.data[7]),'frame_fresh':fresh,
                'frame_id':self.frame_id,'frame_time_ms':self.frame_time_ms,'sample_time_ms':self.now,
                'near_px':int(self.data[5]),'far_px':int(self.data[6]),'vision_fault':int(self.data[20]),
                **self.native.features}
    def capture_frame(self):
        if not self._new_frame:return
        meta={'frame_id':self.frame_id,'frame_time_ms':self.frame_time_ms,'control_time_ms':self.now,
              'pose':list(self.frame_pose),'path':self.path_diagnostic(False),
              'state':int(self.data[0]),'fault':int(self.data[1]),
              'left_pwm':float(self.data[12]),'right_pwm':float(self.data[13]),'replay':self.replay}
        meta['data']=self.data.tolist();meta['brightness_offset']=self.brightness_offset
        packet={'meta':meta,'image':self.image.copy(),'rows':self.rows.copy()}
        self.frame_buffer.append(packet);self._new_frame=False
        while self.frame_buffer and self.frame_buffer[0]['meta']['frame_time_ms']<self.now-5000:self.frame_buffer.popleft()
        if self.stop_event is not None and self.now<=self.stop_event['control_time_ms']+2000:self.event_frames.append(packet)
    def latch_stop(self):
        if self.stop_event is not None or self.replay or self.p['controller']!='native':return
        if int(self.data[0]) not in (4,5,6):return
        self.stop_event={'control_time_ms':self.now,'frame_time_ms':self.frame_time_ms,'frame_id':self.frame_id,
          'state':int(self.data[0]),'fault':int(self.data[1]),'path':self.path_diagnostic(False),
          'data':self.data.tolist(),'pose':[self.x,self.y,self.h],
          'pre_window_ms':5000,'post_window_ms':2000}
        self.event_frames=list(self.frame_buffer)
        self.stop_image=self.image.copy();self.stop_rows=self.rows.copy()
    @property
    def stop_capture_complete(self):
        return self.stop_event is not None and self.now>=self.stop_event['control_time_ms']+2000
    def wheels(self):
        p=self.p;c=math.cos(self.h);s=math.sin(self.h);result=[]
        for longitudinal in (0,p['wheelbase']):
            for lateral in (-p['track']/2,p['track']/2):
                result.append((self.x+longitudinal*c-lateral*s,self.y+longitudinal*s+lateral*c))
        return result
    def outside_wheels(self):
        # A full tire footprint must lie outside inner road plus edge; conservative approximate disk radius.
        return sum(self.track.road_distance(x,y)>self.p['road_width']/2+self.p['edge_width']+
                   max(self.p['tire_width']/2,self.p['wheel_diameter']/2) for x,y in self.wheels())
    def tick(self):
        if self.replay:return
        p=self.p;self.now+=10
        if self.now+1e-6>=self.next_frame:
            if not p['drop_camera']:self._frame()
            self.next_frame=self.now+1000/p['fps']
        step=np.array([self.left_v,self.right_v])*self.dt
        self.count_total+=step/(math.pi*p['wheel_diameter'])*4352
        counts=np.trunc(self.count_total).astype(int);delta=counts-self.last_counts;self.last_counts=counts
        self.measured=delta*math.pi*p['wheel_diameter']/4352/self.dt*1000
        forward_um=int(np.sum(delta)*math.pi*p['wheel_diameter']/4352/2*1e6)
        # Release then press C15 after >=3 camera frames; a slow FPS still observes real startup gating.
        start=self.started and 160<=self.now<240
        self.native.tick(self.now,not p['drop_feedback'],start,self.stop_requested,
                         int(self.measured[0]),int(self.measured[1]),forward_um)
        self.data,self.rows=self.native.data()
        self.capture_frame();self.latch_stop()
        if p['controller']=='native':
            command=self.data[11];duties=self.data[12:14];active=bool(self.data[2])
        else:
            # Explicit ground-truth reference for model checks, never represented as vehicle C control.
            i,error,progress=self.track.nearest(self.x,self.y,getattr(self,'route_progress',0))
            look=self.track.at(min(self.track.length,progress+.35));dx=look[0]-self.x;dy=look[1]-self.y
            lateral=-math.sin(self.h)*dx+math.cos(self.h)*dy
            desired=math.atan2(2*p['wheelbase']*lateral,max(.02,dx*dx+dy*dy))
            command=np.clip(math.degrees(desired)/p['steer_max_deg']*1000,-1000,1000)
            active=self.started and not self.stop_requested
            duties=np.full(2,min(200,p['curve_speed']/p['motor_gain']+p['motor_deadzone']) if active else 0)
        self.applied_duties=duties.copy();self.applied_command=float(command)
        target=p['servo_center']+(command/1000*p['servo_range'] if active else 0)
        if self.now/1000-self.last_servo+1e-9>=p['servo_interval']:
            self.pulse+=float(np.clip(target-self.pulse,-p['servo_step'],p['servo_step']));self.last_servo=self.now/1000
        angle=math.radians((self.pulse-p['servo_center'])/p['servo_range']*p['steer_max_deg'])
        self.steer+=(angle-self.steer)*(1-math.exp(-self.dt/p['servo_tau']))
        desired_speed=np.maximum(0,duties-p['motor_deadzone'])*p['motor_gain']/1000
        target_v=float(np.mean(desired_speed));tau=p['motor_tau'] if target_v>0 else p['coast_tau']
        self.v+=(target_v-self.v)*(1-math.exp(-self.dt/tau))
        # Ackermann no-slip constraints: average rear speed drives chassis; wheel mismatch becomes scrub.
        # Separate free motor speeds are not allowed to produce impossible independent yaw.
        omega=self.v*math.tan(self.steer)/p['wheelbase']
        self.left_v=self.v-omega*p['track']/2;self.right_v=self.v+omega*p['track']/2
        self.x,self.y,self.h=integrate_pose(self.x,self.y,self.h,self.v,self.steer,p['wheelbase'],self.dt)
        self.distance+=max(0,self.v)*self.dt
        i,error,progress=self.track.nearest(self.x,self.y,self.route_progress);self.route_progress=max(self.route_progress,progress)
        self.max_error=max(self.max_error,abs(error))
        outside=self.outside_wheels();self.max_outside=max(self.max_outside,outside)
        end=self.track.end;cross=(self.x-end[0])*math.cos(end[2])+(self.y-end[1])*math.sin(end[2])
        if self.p['scenario'] in ('circle','full'):
            reached=progress>=self.track.length-.05 and cross>=0 and self.distance>=.8*self.track.length and math.hypot(self.x-end[0],self.y-end[1])<self.track.width/2
        else:reached=progress>=self.track.length-.05 and cross>=0 and abs(error)<self.track.width/2
        # Completion is a virtual position marker; invalid two-tire departure rejects the run.
        if reached and self.max_outside<2:self.completed=True
        if self.now%50==0:self.trajectory.append((self.x,self.y));self.record()
    def record(self):
        _,error,progress=self.track.nearest(self.x,self.y,getattr(self,'route_progress',0))
        path=self.path_diagnostic(False)
        self.history.append({'time_s':self.now/1000,'x_m':self.x,'y_m':self.y,'heading_deg':math.degrees(self.h),
          'speed_mm_s':self.v*1000,'target_mm_s':float(self.data[14]) if self.p['controller']=='native' else (self.p['curve_speed'] if self.started and not self.stop_requested else 0),'pulse_us':self.pulse,
          'steer_deg':math.degrees(self.steer),'Q':int(self.data[3]),'valid':int(self.data[4]),
          'near_px':int(self.data[5]),'far_px':int(self.data[6]),'element':int(self.data[7]),
          'outer_side':int(self.data[8]),'outer_supported':int(self.data[9]),'path_reason':int(self.data[10]),
          'command':int(self.data[11]),'left_pwm':int(self.data[12]),'right_pwm':int(self.data[13]),
          'applied_command':self.applied_command,'applied_left_pwm':float(self.applied_duties[0]),'applied_right_pwm':float(self.applied_duties[1]),
          'left_mm_s':self.left_v*1000,'right_mm_s':self.right_v*1000,'state':int(self.data[0]),
          'fault':int(self.data[1]),'error_m':error,'progress_m':progress,'distance_m':self.distance,
          'outside_wheels':self.outside_wheels(),'completed':self.completed,'mode':self.p['controller'],
          'minimum_confidence':path['minimum'],'path_accepted':path['accepted'],
          'camera_frame_id':self.frame_id,'camera_frame_time_ms':self.frame_time_ms,
          'brightness_offset':self.brightness_offset,**self.native.features})
    def result(self):
        return {'perturbation':self.perturbation.record(),'mode':'replay' if self.replay else self.p['controller'],'completed':self.completed,'time_s':self.now/1000,
         'distance_m':self.distance,'route_length_m':self.track.length,'max_error_m':self.max_error,
         'max_outside_wheels':self.max_outside,
         'state':('图像回放' if self.replay else ('参考完成' if self.completed else '参考运行')) if self.replay or self.p['controller']=='reference' else STATES[int(self.data[0])],
         'fault':None if self.replay or self.p['controller']=='reference' else FAULTS[int(self.data[1])],
         'shadow_c_state':STATES[int(self.data[0])],'shadow_c_fault':FAULTS[int(self.data[1])],
         'algorithm_build':self.native.version,
         'stop_evidence':self.stop_event,'stop_capture_complete':self.stop_capture_complete,
         'closure_connector_m':getattr(self.track,'closure_gap',0),
         'predicted_width_row30':predicted_width(self.p,30),'predicted_width_row116':predicted_width(self.p,116),
         'vehicle_geometry':{'model':'C1 03-361-1','overall_length_m':self.p['length'],
            'plate_length_m':self.p['wheelbase']+self.p['front_overhang']+self.p['rear_overhang'],
            'wheelbase_m':self.p['wheelbase'],'outer_width_straight_m':self.p['track']+self.p['tire_width'],
            'camera_from_rear_axle_m':self.p['camera_offset']},
         'model_status':'C1产品图外形近似、实测轴线定位；相机条件拟合、执行机构未标定'}
    def export(self,label='run'):
        if not self.history or self.history[-1]['time_s']!=self.now/1000:self.record()
        label=''.join(c for c in label if c.isalnum() or c in '-_')[:40] or 'run'
        folder=ROOT/'runs'/(time.strftime('%Y%m%d-%H%M%S')+'-'+label+'-'+str(time.time_ns())[-6:]);folder.mkdir(parents=True)
        (folder/'parameters.json').write_text(json.dumps(self.p,ensure_ascii=False,indent=2),encoding='utf-8')
        (folder/'result.json').write_text(json.dumps(self.result(),ensure_ascii=False,indent=2),encoding='utf-8')
        with (folder/'telemetry.csv').open('w',newline='',encoding='utf-8-sig') as f:
            writer=csv.DictWriter(f,fieldnames=list(self.history[0]));writer.writeheader();writer.writerows(self.history)
        Image.fromarray(self.image).save(folder/'last_frame.png')
        frames=self.event_frames if self.stop_event is not None else list(self.frame_buffer)
        target=folder/'frames';target.mkdir();index=[]
        for packet in frames:
            meta=packet['meta'].copy();name=f"{meta['frame_id']:06d}-{meta['frame_time_ms']:09d}"
            Image.fromarray(packet['image']).save(target/(name+'.png'))
            np.save(target/(name+'-rows.npy'),packet['rows'])
            meta['image']='frames/'+name+'.png';meta['rows']='frames/'+name+'-rows.npy';index.append(meta)
        (folder/'frames.json').write_text(json.dumps(index,ensure_ascii=False,indent=2),encoding='utf-8')
        if self.stop_event is not None:
            evidence=self.stop_event.copy();evidence['post_capture_complete']=self.stop_capture_complete
            evidence['post_frame_count']=sum(m['frame_time_ms']>evidence['control_time_ms'] for m in index)
            (folder/'stop_event.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf-8')
            Image.fromarray(self.stop_image).save(folder/'stop_trigger_frame.png')
            np.save(folder/'stop_trigger_rows.npy',self.stop_rows)
        return folder
