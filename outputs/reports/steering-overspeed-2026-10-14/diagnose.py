"""只观察当前C源码；负载模型是机制探针，不能当作实车事件回放。"""
import ctypes as C,json,math,subprocess
from pathlib import Path
TASK=Path(__file__).resolve().parent;ROOT=TASK.parents[1];SOURCE=TASK/'source'
cmd=[str(ROOT/'simulation/toolchain/mingw64/bin/gcc.exe'),'-std=c99','-shared','-O2','-Wall','-Wextra','-Werror',
     '-I'+str(SOURCE/'src'),'-I'+str(SOURCE/'include'),'-I'+str(SOURCE/'config'),str(TASK/'probe.c'),
     str(SOURCE/'src/control_pid.c'),'-o',str(TASK/'probe.dll')]
r=subprocess.run(cmd,capture_output=True,text=True,errors='replace');assert r.returncode==0,r.stderr
(TASK/'compile.log').write_text(r.stdout+r.stderr,encoding='utf-8')
lib=C.CDLL(str(TASK/'probe.dll'));lib.step.argtypes=[C.c_uint,C.c_int,C.c_float,C.c_float,*([C.c_int]*5),C.POINTER(C.c_float)]
keys=['steering','center_target','left_target','right_target','left_pwm','right_pwm','left_integral','right_integral','left_pid_request','right_pid_request','fault','state']
def tick(ms,vl,vr,x=0,y=400,geometry=0,near=0,far=0,valid=1):
 v=(C.c_float*12)();lib.step(ms,geometry,x,y,near,far,round(vl),round(vr),valid,v)
 return dict(zip(keys,v))

lib.reset();vl=vr=0;timeline=[]
for ms in range(0,12500,10):
 geometry=10000<=ms<11000;x=200 if geometry else 0
 q=tick(ms,vl,vr,x,320,geometry=int(geometry))
 for name in ('left','right'):
  old=vl if name=='left' else vr;target=max(0,q[name+'_pwm']-15)*1.25
  v=old+(target-old)*(1-math.exp(-.01/.18))
  if name=='left':vl=v
  else:vr=v
 if ms in (9990,10000,10010,10990,11000,11010):timeline.append({'ms':ms,**q})

# Startup: PID积分仍在输出斜率限制之后未实现的驱动力上增长。
lib.reset();vl=vr=0;rate_limited_integrating=[]
for ms in range(0,7000,10):
 q=tick(ms,vl,vr)
 target=max(0,q['left_pwm']-15)*1.25;vl=vr=vl+(target-vl)*(1-math.exp(-.01/.18))
 if q['left_pid_request']-q['left_pwm']>30 and q['left_integral']>5:rate_limited_integrating.append({'ms':ms,**q})

# 一帧原始轮速越界立即触发。此项不判断编码器数值是否等于实际地速。
lib.reset()
for ms in range(0,8000,10):q=tick(ms,1200,1200)
overspeed=tick(8000,2501,1200)

# 独立轮速的一阶机械响应+转弯负载解除；用作缺失工况的敏感性探针。
load_results=[]
for gain,tau in ((1.25,.18),(2.34375,.18),(3.6,.18),(3.6,.05),(3.6,.02),(5.,.05),(5.,.02)):
 lib.reset();vl=vr=0;peak=0;stop=None;before=None;trace=[]
 for ms in range(0,19000,10):
  loaded=10000<=ms<15000;q=tick(ms,vl,vr,x=200,y=320,geometry=int(loaded))
  if ms==14990:before={'left_speed':vl,'right_speed':vr,**q}
  if 15000<=ms<=15800:trace.append({'ms':ms,'left_speed':vl,'right_speed':vr,**q})
  if q['fault']:stop={'ms':ms,'left_speed':vl,'right_speed':vr,'fault':q['fault']};break
  factor=.35 if loaded else 1.
  vl+=(max(0,q['left_pwm']-15)*gain*factor-vl)*(1-math.exp(-.01/tau))
  vr+=(max(0,q['right_pwm']-15)*gain*factor-vr)*(1-math.exp(-.01/tau))
  if ms>=15000:peak=max(peak,vl,vr)
 load_results.append({'gain':gain,'tau_s':tau,'load_factor':.35,'peak_after_release_mm_s':peak,'fault':stop,'before_release':before,'trace':trace})
result={'target_transition':timeline,'slew_limited_integrating_samples':len(rate_limited_integrating),
 'slew_examples':rate_limited_integrating[:3]+rate_limited_integrating[-3:],
 'single_sample_overspeed':overspeed,'load_release':load_results}
(TASK/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({**{k:v for k,v in result.items() if k!='load_release'},'load_release':[{k:v for k,v in x.items() if k not in ('trace','before_release')} for x in load_results]},indent=2))
