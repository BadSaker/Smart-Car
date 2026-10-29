from pathlib import Path
import sys,json,csv,hashlib
r=Path.cwd();t=r/'tmp/reality-gap-20261028';sys.path.insert(0,str(r/'simulation'))
from parameters import defaults
from dynamics import WheelDynamics
motor=[]
for gain in [1.25,3.75]:
 p=defaults();p.update(motor_gain=gain,traction_left=0.,traction_right=0.)
 m=WheelDynamics(p)
 for _ in range(200):m.step([1000,1000],0,.01)
 motor.append({'gain_assumption':gain,'traction':0,'pwm_permille':1000,'speed_guard_mm_s':2500,'free_wheel_mm_s':[v*1000 for v in m.wheel],'chassis_mm_s':m.speed*1000})
(t/'motor-ceiling.json').write_text(json.dumps(motor,indent=2),encoding='utf-8');print('Motor ceiling',motor)
for folder in ['main40','main60','main60-low','servo40','frame40','hardware10']:
 a=json.loads((t/folder/'result.json').read_text(encoding='utf-8'))
 print(folder,{k:a[k] for k in ['time_s','fault','min_tire_corner_margin_mm','stability_10ms']})
rows=list(csv.DictReader((t/'servo40/telemetry-10ms.csv').open(encoding='utf-8-sig')))
for row in rows:
 if 32970<=int(row['time_ms'])<=33050 and int(row['time_ms'])%20==10:print({k:row[k] for k in ['time_ms','speed_mm_s','command','actual_angle_deg','steering_gap_deg','q','valid','service_age_ms','frame_age_ms','left_pwm','right_pwm']})
old=json.loads((t/'delay-aware/result.json').read_text(encoding='utf-8'));new=json.loads((t/'delay-aware-reviewed/result.json').read_text(encoding='utf-8'))
assert old['time_s']==new['time_s'] and old['distance_m']==new['distance_m']
assert old['stop_evidence']['path']['frame_fresh'] and not new['stop_evidence']['path']['frame_fresh']
assert not new['stop_evidence']['path']['accepted'] and new['stop_evidence']['frame_time_ms']==9990
(t/'probe-review-verification.json').write_text(json.dumps({'review_regression_passed':True,'before_frame_fresh':True,'after_frame_fresh':False,'after_c_frame_time_ms':9990,'same_failure_time_and_distance':True},indent=2),encoding='utf-8')
print('Review evidence correction PASS')
