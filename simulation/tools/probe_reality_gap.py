"""诊断用执行边界对照：真实C算法/舵机代码，GPIO/PWM只用主机桩。

不修改car、不改默认模型、不把假设的延迟/电机参数当成实测。
每次输出独立编译输入、10ms遥测和实际采用的实验参数。
"""
from pathlib import Path
import argparse
import csv
import ctypes as C
import hashlib
import json
import math
import os
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

PLATFORM = r'''#ifndef PROBE_PLATFORM_H
#define PROBE_PLATFORM_H
#include <stdint.h>
typedef enum { C14=46,C15=47,C30=62 } gpio_pin_enum;
typedef enum { GPI=0,GPO=1 } gpio_dir_enum;
typedef enum { PWM4_MODULE2_CHA_C30=185 } pwm_channel_enum;
#define GPIO_LOW 0
#define GPIO_HIGH 1
#define GPI_PULL_UP 22
#define GPO_PUSH_PULL 33
#define PWM_DUTY_MAX 10000
void gpio_init(gpio_pin_enum,gpio_dir_enum,uint8_t,uint32_t);
uint8_t gpio_get_level(gpio_pin_enum);
void pwm_init(pwm_channel_enum,uint32_t,uint32_t);
void pwm_set_duty(pwm_channel_enum,uint32_t);
#endif
'''

SERVO_PROBE = r'''
#include "zf_driver_gpio.h"
#include "source/src/servo_control.c"
static unsigned probe_ms,probe_generation,probe_next,probe_period=10;
static unsigned probe_pause_start,probe_pause_end;
static int probe_hardware;
static float probe_last_pulse=1480;
void gpio_init(gpio_pin_enum p,gpio_dir_enum d,uint8_t v,uint32_t c){(void)p;(void)d;(void)v;(void)c;}
uint8_t gpio_get_level(gpio_pin_enum p){(void)p;return GPIO_HIGH;}
void pwm_init(pwm_channel_enum p,uint32_t h,uint32_t d){(void)p;(void)h;if(d)probe_last_pulse=(float)d*1000000/(SERVO_PWM_FREQUENCY_HZ*PWM_DUTY_MAX);}
void pwm_set_duty(pwm_channel_enum p,uint32_t d){(void)p;if(d)probe_last_pulse=(float)d*1000000/(SERVO_PWM_FREQUENCY_HZ*PWM_DUTY_MAX);}
API unsigned probe_frame_age_limit(void){return AUTO_FRAME_MAX_AGE_MS;}
API void probe_setup(int hardware,unsigned period,unsigned begin,unsigned end){
 probe_hardware=hardware;probe_period=period;probe_pause_start=begin;probe_pause_end=end;
 probe_ms=probe_generation=probe_next=0;probe_last_pulse=SERVO_REFERENCE_CENTER_US;
 servo_control_init(0);
}
static void probe_service(unsigned now){
 if(now<probe_next || (now>=probe_pause_start && now<probe_pause_end))return;
 probe_next=now+probe_period;
 servo_control_autonomous_apply(drive.status.active,drive.status.steering_permille,now);
 if(drive.status.active && servo_control_get_status()->output_enabled){
  probe_ms=now;probe_generation=drive.status.run_generation;
 }
}
API void probe_values(float *v){
 const servo_control_status_t *s=servo_control_get_status();
 v[0]=s->current_pulse_us;v[1]=probe_last_pulse;v[2]=(float)probe_ms;
 v[3]=(float)probe_generation;v[4]=s->output_enabled;v[5]=(float)frame_ms;
}
'''


def prepare_workspace(output):
    import parameters
    import source_layout
    from toolchain import find_compiler
    work = output / 'workspace'
    work.mkdir(parents=True, exist_ok=True)
    # Extra sources are exact copies supplied to the existing native compiler.
    source_layout.SOURCE_NAMES['src'] += ('servo_control.c',)
    source_layout.SOURCE_NAMES['include'] += ('servo_control.h',)
    driver = (ROOT.parent/'car/library/zf_driver/zf_driver_pwm.h').read_text(encoding='ascii',errors='ignore')
    match = re.search(r'^#define\s+PWM_DUTY_MAX\s+\((\d+)\)', driver, flags=re.M)
    if not match:
        raise ValueError('无法读取当前PWM_DUTY_MAX，拒绝猜测接口量化')
    platform = PLATFORM.replace('#define PWM_DUTY_MAX 10000', '#define PWM_DUTY_MAX '+match[1])
    for header in ('zf_driver_gpio.h', 'zf_driver_pwm.h'):
        (work / header).write_text(platform, encoding='utf-8')
    text = (ROOT / 'native_bridge.c').read_text(encoding='utf-8')
    anchor = 'static unsigned frame_sequence, frame_ms, feedback_sequence;'
    assert text.count(anchor) == 1
    text = text.replace(anchor, anchor + '\n/* Platform SHA256 '+hashlib.sha256(platform.encode()).hexdigest()+' */\n' + SERVO_PROBE)
    old = 'i.servo_generation=drive.status.run_generation;i.servo_service_ms=now;'
    assert text.count(old) == 1
    text = text.replace(old, 'i.servo_generation=probe_hardware?probe_generation:drive.status.run_generation;'
                        'i.servo_service_ms=probe_hardware?probe_ms:now;')
    old = 'autonomous_drive_tick(&drive,&i);'
    assert text.count(old) == 1
    text = text.replace(old, old + '\n    probe_service(now);')
    (work / 'native_bridge.c').write_text(text, encoding='utf-8')
    parameters.ROOT = work
    os.environ['SIMULATION_CC'] = str(find_compiler())
    # Include the task-local driver stubs, not the real MMIO driver headers.
    import native
    native.COMPILE_OPTIONS += ['-I../..']
    for file in ROOT.glob('*.py'):
        dest = work / 'runtime' / file.name
        dest.parent.mkdir(exist_ok=True)
        shutil.copy2(file, dest)
    shutil.copy2(Path(__file__),work/'probe_reality_gap.py')
    return source_layout.source_files()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--direction', choices=['AB', 'BA'], default='AB')
    parser.add_argument('--seconds', type=float, default=65)
    parser.add_argument('--hardware-servo', action='store_true')
    parser.add_argument('--service-ms', type=int, default=10)
    parser.add_argument('--main-loop-ms', type=int, help='有效图像提交/舵机服务间隔的简化对照，非板上主循环耗时实测')
    parser.add_argument('--frame-period-ms', type=int, help='仅限制处理帧周期，用于与舵机周期分开对照')
    parser.add_argument('--pause-ms', type=int, default=0)
    parser.add_argument('--inject-ms', type=int, default=10000)
    parser.add_argument('--image-delay-ms', type=int, default=0)
    parser.add_argument('--frame-age-aware', action='store_true')
    parser.add_argument('--motor-gain', type=float)
    args = parser.parse_args()
    if args.main_loop_ms is not None:
        args.hardware_servo = True
        args.service_ms = args.main_loop_ms
    if args.service_ms < 10 or args.service_ms % 10 or args.seconds <= 0:
        parser.error('service-ms须为至少10的10ms倍数，seconds须为正数')
    if min(args.pause_ms, args.inject_ms, args.image_delay_ms) < 0:
        parser.error('时间参数不得为负')
    if args.frame_period_ms is not None and (args.frame_period_ms < 10 or args.frame_period_ms % 10):
        parser.error('frame-period-ms须为至少10的10ms倍数')
    if args.frame_period_ms is not None and args.main_loop_ms is not None:
        parser.error('frame-period-ms与main-loop-ms不能同时设置')
    if args.output.exists():
        parser.error('输出目录已存在，请使用新的实验目录，防止覆盖证据')
    import parameters
    p = parameters.load(args.config.resolve())
    p.update(direction='A→B' if args.direction == 'AB' else 'B→A', seed_auto=0)
    if args.main_loop_ms is not None:
        p['fps'] = 1000/args.main_loop_ms
    if args.frame_period_ms is not None:
        p['fps'] = 1000/args.frame_period_ms
    if p['controller'] != 'native':
        parser.error('诊断必须使用native控制器')
    if p['dynamics_model'] != 'independent':
        parser.error('执行边界诊断必须使用independent轮速模型')
    if args.motor_gain is not None:
        p['motor_gain'] = args.motor_gain
    parameters.validate(p)
    output = args.output.resolve()
    sources = prepare_workspace(output)
    from engine import Simulation
    from tools.validate_control import run_is_acceptable
    sim = Simulation(p)
    sim.started = True
    lib = sim.native.lib
    lib.probe_setup.argtypes = [C.c_int, C.c_uint, C.c_uint, C.c_uint]
    lib.probe_setup(args.hardware_servo, args.service_ms, args.inject_ms,
                    args.inject_ms + args.pause_ms)
    lib.probe_values.argtypes = [C.POINTER(C.c_float)]
    lib.probe_frame_age_limit.restype = C.c_uint
    frame_age_limit = lib.probe_frame_age_limit()
    values = (C.c_float * 6)()
    original_vision = sim.native.vision
    consumed = dict(sequence=1, stamp=0, image=sim.image.copy())

    def vision(image, now, distance):
        age = args.image_delay_ms if now >= args.inject_ms else 0
        # Real main.c drops a copied frame already older than AUTO_FRAME_MAX_AGE_MS.
        if args.frame_age_aware and age > frame_age_limit:
            return
        stamp = max(0, now-age) if args.frame_age_aware else now
        original_vision(image, stamp, distance)
        consumed.update(sequence=consumed['sequence']+1,stamp=stamp,image=image.copy())

    sim.native.vision = vision
    original_step = sim.dynamics.step
    before_steer = 0.0

    def step(duties, steer, dt):
        lib.probe_values(values)
        if args.hardware_servo:
            # Disabled PWM: hold last mechanical target, explicitly an assumption.
            # This is not the stock model's automatic recentering after a fault.
            sim.pulse = float(values[1])
            angle = math.radians((sim.pulse-p['servo_center'])/p['servo_range']*p['steer_max_deg'])
            sim.steer = before_steer+(angle-before_steer)*(1-math.exp(-dt/p['servo_tau']))
            steer = sim.steer
        original_step(duties, steer, dt)

    sim.dynamics.step = step
    telemetry = []
    first_bad = None
    peak = {'command_minus_actual_deg': 0., 'wheel_target_vs_steer_mismatch_mm_s': 0.}
    for _ in range(math.ceil(args.seconds*100)):
        now = sim.now+10
        sim.p['drop_camera'] = args.inject_ms <= now < args.inject_ms+args.pause_ms
        sim.p['camera_latency_ms'] = args.image_delay_ms if now >= args.inject_ms else 0
        before_steer = sim.steer
        sim.tick()
        lib.probe_values(values)
        requested_angle = float(sim.data[11])/1000*p['steer_max_deg']
        gap = abs(requested_angle-math.degrees(sim.steer))
        ratio = math.tan(sim.steer)*p['track']/(2*p['wheelbase'])
        differential = sim.native.motion['right_target_mm_s']-sim.native.motion['left_target_mm_s']
        mismatch = abs(differential-2*float(sim.data[14])*ratio)
        row = dict(time_ms=sim.now, state=int(sim.data[0]), fault=int(sim.data[1]),
                   progress_m=sim.route_progress, speed_mm_s=sim.v*1000,
                   command=float(sim.data[11]), requested=sim.native.motion['requested_steering_permille'],
                   model_pulse_us=sim.pulse, c_pulse_us=float(values[0]),
                   pwm_nominal_pulse_us=float(values[1]), actual_angle_deg=math.degrees(sim.steer),
                   steering_gap_deg=gap, wheel_target_steer_mismatch_mm_s=mismatch,
                   service_age_ms=sim.now-int(values[2]), frame_age_ms=sim.now-int(values[5]),
                   rendered_frame_id=sim.frame_id, consumed_frame_id=consumed['sequence'],
                   consumed_frame_time_ms=consumed['stamp'],
                   q=int(sim.data[3]), valid=int(sim.data[4]),
                   min_q=int(sim.data[16]), element=int(sim.data[7]),
                   left_pwm=float(sim.data[12]), right_pwm=float(sim.data[13]),
                   left_speed_mm_s=sim.left_v*1000, right_speed_mm_s=sim.right_v*1000,
                   measured_left_mm_s=float(sim.measured[0]), measured_right_mm_s=float(sim.measured[1]),
                   **sim.native.motion)
        telemetry.append(row)
        assert row['frame_age_ms'] == sim.now-consumed['stamp']
        if row['state'] == 2:
            peak['command_minus_actual_deg'] = max(peak['command_minus_actual_deg'], gap)
            peak['wheel_target_vs_steer_mismatch_mm_s'] = max(peak['wheel_target_vs_steer_mismatch_mm_s'], mismatch)
        bad = row['fault'] or (row['state'] == 2 and (not row['valid'] or min(row['left_pwm'],row['right_pwm']) == 0))
        if first_bad is None and bad:
            first_bad = row.copy()
        if sim.completed or sim.stop_event or sim.max_route_deviation > sim.track.width/2+.02:
            break
    result = sim.result()
    result['observation_end'] = ('fault' if sim.stop_event else 'completed' if sim.completed
                                 else 'off_route' if sim.max_route_deviation>sim.track.width/2+.02 else 'time_limit')
    if result['stop_evidence'] is not None:
        evidence = result['stop_evidence']
        evidence.update(frame_id=consumed['sequence'],frame_time_ms=consumed['stamp'],
                        rendered_frame_id=sim.frame_id,rendered_frame_time_ms=sim.frame_time_ms)
        path = evidence['path']
        fresh = sim.now-consumed['stamp'] <= frame_age_limit
        path.update(frame_id=consumed['sequence'],frame_time_ms=consumed['stamp'],frame_fresh=fresh)
        if not fresh:
            path.update(accepted=False,reason='摄像头超时')
        evidence['note']='仅导出故障时刻；未采集完整前5秒/后2秒窗口'
    result.update(pass_run=run_is_acceptable(result, p), first_bad=first_bad, peak=peak,
                  experiment={k: str(v) if isinstance(v, Path) else v for k,v in vars(args).items()},
                  physical_assumptions='周期/延迟/电机增益为假设；PWM脉宽为驱动接口名义值，未模拟计数器二次量化/50Hz重载；停止后机械角保持是假设')
    (output/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    (output/'parameters.json').write_text(json.dumps(p,ensure_ascii=False,indent=2),encoding='utf-8')
    (output/'source-sha256.json').write_text(json.dumps({str(x.relative_to(ROOT.parent)):hashlib.sha256(x.read_bytes()).hexdigest() for x in sources},indent=2),encoding='utf-8')
    with (output/'telemetry-10ms.csv').open('w',encoding='utf-8-sig',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(telemetry[0]));writer.writeheader();writer.writerows(telemetry)
    from PIL import Image
    Image.fromarray(consumed['image']).save(output/'last-frame.png')
    Image.fromarray(sim.image).save(output/'last-rendered-frame.png')
    print(json.dumps({k:result[k] for k in ['pass_run','completed','time_s','fault','peak','first_bad']},ensure_ascii=True),flush=True)
    return 0  # A reproduced vehicle failure is a successful diagnostic execution.


if __name__ == '__main__':
    raise SystemExit(main())
