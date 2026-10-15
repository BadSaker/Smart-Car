from pathlib import Path
import json, math
from ctypes import c_float
ROOT=Path(__file__).resolve().parent
DEFAULTS={
 'scenario':'full','direction':'A→B','controller':'native','show_ab':True,
 'length':0.28,'wheelbase':0.19,'track':0.153,'body_width':0.14,'tire_width':0.027,
 'front_overhang':0.040,'rear_overhang':0.015,'plate_height':0.026,'plate_thickness':0.004,
 'wheel_diameter':0.064,'camera_offset':0.08,'height':0.297,'pitch':31.468962853276164,
 'camera_lateral':0.0,'camera_yaw':0.0,'camera_roll':0.0,
 'hfov':89.66951494024845,'fy_ratio':1.0,'cx':93.0,'cy':59.5,'k1':0.0,'fps':50.0,'road_width':0.5,'edge_width':0.,
 'radius':0.5,'steer_max_deg':34.74318015830361,'servo_center':1480.0,'servo_range':150.0,
 'servo_step':20.0,'servo_interval':0.02,'servo_tau':0.08,
 'motor_gain':1.25,'motor_deadzone':15.0,'motor_tau':0.18,'coast_tau':0.25,
 'cruise':300.0,'curve_speed':250.0,'outer_speed':220.0,'element_speed':220.0,'max_duty':400,'speed_guard':2500,
 'kp':29.0,'ki':0.0,'kd':0.0,
 'speed_kp':0.5,'speed_ki':1.0,'min_q':45,'outer_q':40,'outer_enabled':1,
 'speed_ff':.5,'speed_tracking':10.,'accel':200.,'decel':800.,
 'steering_rate':5000.,'steering_accel':50000.,'lateral_accel':2000.,
 'dynamics_model':'independent','wheel_inertia_ratio':.04,'tire_stiffness':60.,
 'traction_left':.9,'traction_right':.9,'wheel_load_left':0.,'wheel_load_right':0.,'curve_load':0.,
 'width_top':44,'width_bottom':167,'offset':0.0,'heading_error':0.0,
 'gray_road':255,'gray_edge':20,'gray_background':153,'noise':0.0,'seed':42,
 'drop_camera':False,'drop_feedback':False,'blackout':False,
 'camera_mode':'fixed_height_fit','fitted_pitch':31.468962853276164,'fitted_hfov':89.66951494024845,
 'nominal_lens_angle_deg':120.0,'nominal_lens_angle_definition':'unknown',
 'camera_mount_gap':0.035,
 'reference_widths':[44.,59.,97.,120.,156.,167.],
 'camera_samples':2,'camera_sampling':'bilinear',
 'disturbance_level':0,'seed_auto':0,'start_jitter':.005,'heading_jitter':1.,'brightness_jitter':3.
 ,'camera_latency_ms':0,'camera_blur_px':0.
}
def fit_camera(height=0.27,width=0.5,cy=59.5,top=44,bottom=167,fy_ratio=1.0):
    slope=(bottom-top)/86.0
    cosine=slope*height/width*fy_ratio
    if not 0<cosine<1: raise ValueError('两点宽度与高度/道路宽度不相容，无法按当前透视假设拟合。')
    pitch=math.acos(cosine)
    focal=(top*height/width-(30-cy)/fy_ratio*cosine)/math.sin(pitch)
    if focal<=0: raise ValueError('拟合焦距非正，请检查尺寸和主点。')
    return {'pitch':math.degrees(pitch),'hfov':math.degrees(2*math.atan(94/focal)),
            'focal':focal,'slope':slope,'assumptions':'平地、内沿宽度、fy/fx已知、cy已知、无畸变；条件拟合，非实测'}
def defaults():
    # 高度为实测；角度为固定高度下的条件拟合，镜头标称角单独保留。
    p=DEFAULTS.copy();p['reference_widths']=DEFAULTS['reference_widths'].copy();return p
def validate(p):
    from perturbation import preset
    level=p.get('disturbance_level',0)
    if not isinstance(level,(int,float)) or not math.isfinite(level) or int(level)!=level or not 0<=level<=4:raise ValueError('扰动等级必须为0～4的整数')
    preset(p)
    switch=p.get('outer_enabled')
    if not isinstance(switch,(int,float)) or not math.isfinite(switch) or not (switch==0 or switch>=1):raise ValueError('外侧补线输入0或大于等于1的数字')
    p['outer_enabled']=int(switch>=1)
    ranges={'length':(.05,1),'wheelbase':(.05,.8),'track':(.05,.5),'body_width':(.05,.5),
      'tire_width':(.005,.1),'wheel_diameter':(.02,.2),'camera_offset':(-.3,1),
      'front_overhang':(0,.3),'rear_overhang':(0,.3),'plate_height':(.005,.15),'plate_thickness':(.001,.03),
      'height':(.03,1),'pitch':(1,85),'hfov':(20,165),'fy_ratio':(.5,2),'cx':(60,125),'cy':(20,100),'k1':(-.01,.01),
      'camera_lateral':(-.3,.3),'camera_yaw':(-90,90),'camera_roll':(-45,45),
      'fps':(1,100),'camera_latency_ms':(0,500),'camera_blur_px':(0,3),'road_width':(.2,1),'edge_width':(0,.08),'radius':(.3,2),
      'steer_max_deg':(2,60),'servo_center':(1000,2000),'servo_range':(10,300),'servo_step':(1,40),
      'servo_interval':(.01,.1),'servo_tau':(.01,1),'motor_gain':(.1,5),'motor_deadzone':(0,100),
      'motor_tau':(.02,2),'coast_tau':(.02,3),'max_duty':(1,1000),'speed_guard':(1,2147483647),
      'speed_ff':(0,2),'speed_tracking':(0,100),'accel':(1,5000),'decel':(1,10000),
      'steering_rate':(100,20000),'steering_accel':(1000,500000),'lateral_accel':(100,10000),
      'wheel_inertia_ratio':(.005,.3),'tire_stiffness':(1,500),'traction_left':(0,3),'traction_right':(0,3),
      'wheel_load_left':(0,10000),'wheel_load_right':(0,10000),'curve_load':(0,10000),
      'kp':(0,100),'ki':(0,20),'kd':(0,10),'speed_kp':(0,5),'speed_ki':(0,5),
      'min_q':(1,100),'outer_q':(1,100),'width_top':(8,150),'width_bottom':(8,185),
      'offset':(-.5,.5),'heading_error':(-90,90),'noise':(0,50),'seed':(0,2147483647),
      'gray_road':(0,255),'gray_edge':(0,255),'gray_background':(0,255),
      'camera_mount_gap':(.020,.120),'disturbance_level':(0,4),'seed_auto':(0,1),
      'start_jitter':(0,.2),'heading_jitter':(0,15),'brightness_jitter':(0,50)}
    for key,(lo,hi) in ranges.items():
        if key not in p or not isinstance(p[key],(int,float)) or not math.isfinite(p[key]) or not lo<=p[key]<=hi:
            raise ValueError(f'{key} 必须在 {lo}～{hi} 之间')
    for key in ('cruise','curve_speed','outer_speed','element_speed'):
        value=p.get(key)
        # 不设置比赛速度上限；只受真实C接口float表示范围约束。
        if not isinstance(value,(int,float)) or not math.isfinite(value) or not 0<value<=3.4028234663852886e38 or c_float(value).value<=0:
            raise ValueError(f'{key} 必须是可由C float表示的正有限速度（mm/s）')
    for key in ('seed','min_q','outer_q','width_top','width_bottom','outer_enabled','disturbance_level','seed_auto','max_duty','speed_guard','servo_step','servo_center','servo_range'):
        if int(p[key])!=p[key]: raise ValueError(f'{key} 必须为整数')
    if p['width_top']>p['width_bottom']: raise ValueError('远端宽度不能大于近端宽度')
    if p['outer_q']>p['min_q']: raise ValueError('外侧Q门限不得高于普通Q门限')
    if p['outer_speed']>p['curve_speed']: raise ValueError('外侧速度不得高于弯道速度')
    if p['outer_enabled'] not in (0,1):raise ValueError('外侧补线开关必须为0或1')
    if p['body_width']>p['track']+p['tire_width']:raise ValueError('车身宽度不能超过当前轮胎外沿宽度')
    if p['height']<=p['plate_height']+p['plate_thickness']+.02:raise ValueError('镜头高度必须高于底板顶面至少20mm')
    if p['scenario'] not in ('full','midterm','straight','circle'):raise ValueError('未知场景')
    widths=p.get('reference_widths')
    if not isinstance(widths,list) or len(widths)!=6 or not all(isinstance(v,(int,float)) and math.isfinite(v) and 0<v<=188 for v in widths):raise ValueError('六行实测宽度必须为0～188之间的六个数字')
    if p['controller'] not in ('native','reference'):raise ValueError('未知控制模式')
    if p.get('dynamics_model') not in ('independent','legacy'):raise ValueError('未知动力学模型')
    if p['track']*math.tan(math.radians(p['steer_max_deg']))>=2*p['wheelbase']:
        raise ValueError('前进动力学要求转弯半径大于半轮距')
    if p['direction'] not in ('A→B','B→A'):raise ValueError('未知方向')
    if p.get('camera_samples',1) not in (1,2,3):raise ValueError('像素采样边长必须为1、2或3')
    if p.get('camera_sampling','nearest') not in ('nearest','bilinear'):raise ValueError('未知图像采样方法')
    return p
def save(path,p):
    Path(path).write_text(json.dumps(p,ensure_ascii=False,indent=2),encoding='utf-8')
def load(path):
    values=json.loads(Path(path).read_text(encoding='utf-8'));p=defaults();p.update(values)
    # 旧实验保持最近邻单点采样；新配置明确记录采样方式。
    if 'camera_samples' not in values:p['camera_samples']=1
    if 'camera_sampling' not in values:p['camera_sampling']='nearest'
    # 旧参数文件保留原先未暴露的100mm/s元素速度和200‰上限。
    if 'element_speed' not in values:p['element_speed']=100.
    if 'max_duty' not in values:p['max_duty']=200
    if 'disturbance_level' not in values and p['noise']>0:p['disturbance_level']=4
    return validate(p)
