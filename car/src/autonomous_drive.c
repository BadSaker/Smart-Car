#include "autonomous_drive.h"
#include "autonomous_config.h"
#include <string.h>
#include <stdlib.h>

static float minimum(float a,float b) { return a<b?a:b; }
static uint8_t minimum_confidence(const autonomous_drive_t *d,const autonomous_drive_input_t *i)
{
    return d->status.active && i->element_supported ? AUTO_ELEMENT_MIN_CONFIDENCE : AUTO_MIN_CONFIDENCE;
}
static uint16_t duty_slew(uint16_t previous,float target)
{
    uint16_t duty=(uint16_t)(target<0?0:target>AUTO_MAX_DUTY_PERMILLE?AUTO_MAX_DUTY_PERMILLE:target);
    if(duty>previous+AUTO_DUTY_STEP_PER_TICK) return previous+AUTO_DUTY_STEP_PER_TICK;
    /* 停止与降速不受上升斜率限制。 */
    return duty;
}
static void zero_output(autonomous_drive_t *d)
{
    d->status.left_duty_permille=0;d->status.right_duty_permille=0;
    d->status.target_speed_mm_s=0;d->status.steering_permille=0;
    control_pid_reset(&d->left_pid);control_pid_reset(&d->right_pid);
    control_pid_reset(&d->steering_pid);d->steering_initialized=0;
}
static void stop_drive(autonomous_drive_t *d,autonomous_state_t state,autonomous_fault_t fault)
{
    zero_output(d);d->status.active=0;d->status.state=state;d->status.fault=fault;
    d->key_armed=0;d->key_stable=1;d->key_sample=1;
}
void autonomous_drive_init(autonomous_drive_t *d)
{
    control_pid_config_t speed={AUTO_SPEED_KP,AUTO_SPEED_KI,AUTO_SPEED_KD,0.0f,
        AUTO_MAX_DUTY_PERMILLE,AUTO_SPEED_INTEGRAL_LIMIT,AUTO_SPEED_DERIVATIVE_TAU_S};
    control_pid_config_t steering={AUTO_STEERING_KP,AUTO_STEERING_KI,AUTO_STEERING_KD,
        -AUTO_STEERING_LIMIT_PERMILLE,AUTO_STEERING_LIMIT_PERMILLE,100.0f,AUTO_STEERING_DERIVATIVE_TAU_S};
    if(!d)return;
    memset(d,0,sizeof(*d));d->key_stable=1;d->key_sample=1;
    d->config_valid=control_pid_init(&d->left_pid,&speed) && control_pid_init(&d->right_pid,&speed) &&
        control_pid_init(&d->steering_pid,&steering);
    if(!d->config_valid)stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_CONFIG);
}
static uint8_t key_pressed(autonomous_drive_t *d,const autonomous_drive_input_t *i)
{
    if(i->start_pressed!=d->key_sample){d->key_sample=i->start_pressed;d->key_time_ms=i->now_ms;}
    if((uint32_t)(i->now_ms-d->key_time_ms)<AUTO_KEY_DEBOUNCE_MS)return 0;
    if(!d->key_sample){d->key_stable=0;d->key_armed=1;return 0;}
    if(!d->key_stable){d->key_stable=1;if(d->key_armed){d->key_armed=0;return 1;}}
    return 0;
}
static void begin_drive(autonomous_drive_t *d,const autonomous_drive_input_t *i)
{
    uint32_t generation=d->status.run_generation+1U;
    zero_output(d);d->status.run_generation=generation;d->status.active=1;
    d->status.state=AUTO_DRIVE_STARTING;d->status.fault=AUTO_FAULT_NONE;
    d->status.distance_mm=0;d->distance_remainder_um=0;d->status.start_marker_seen=0;
    d->started_ms=i->now_ms;d->marker_present=0;d->marker_armed=1;
    d->zebra_frames=0;d->clear_frames=0;d->wrong_left_ms=0;d->wrong_right_ms=0;
    d->stall_left_ms=0;d->stall_right_ms=0;d->bad_frames=0;
    d->left_filtered=(float)i->left_speed_mm_s;d->right_filtered=(float)i->right_speed_mm_s;
}
static void update_marker(autonomous_drive_t *d,const autonomous_drive_input_t *i)
{
    if(i->zebra){d->clear_frames=0;if(d->zebra_frames<255)++d->zebra_frames;}
    else {d->zebra_frames=0;if(d->clear_frames<255)++d->clear_frames;}
    if(!i->zebra && d->clear_frames>=AUTO_ZEBRA_CLEAR_FRAMES &&
       (!d->status.start_marker_seen || d->status.distance_mm-d->marker_distance_mm>=AUTO_ZEBRA_CLEAR_DISTANCE_MM))
    {d->marker_present=0;d->marker_armed=1;}
    if(d->zebra_frames<AUTO_ZEBRA_CONFIRM_FRAMES || !d->marker_armed || d->marker_present)return;
    if(!d->status.start_marker_seen){d->marker_present=1;d->marker_armed=0;d->status.start_marker_seen=1;d->marker_ms=i->now_ms;d->marker_distance_mm=d->status.distance_mm;}
    else if(d->status.state==AUTO_DRIVE_RUNNING &&
            (uint32_t)(i->now_ms-d->marker_ms)>=AUTO_MIN_LAP_TIME_MS &&
            d->status.distance_mm-d->marker_distance_mm>=AUTO_MIN_LAP_DISTANCE_MM)
    {d->marker_present=1;d->marker_armed=0;d->status.state=AUTO_DRIVE_FINISHING;d->finish_ms=i->now_ms;d->finish_distance_mm=d->status.distance_mm;}
}
static uint8_t feedback_guard(autonomous_drive_t *d,const autonomous_drive_input_t *i)
{
    if(i->left_speed_mm_s>AUTO_MAX_MEASURED_SPEED_MM_S || i->left_speed_mm_s< -AUTO_MAX_MEASURED_SPEED_MM_S ||
       i->right_speed_mm_s>AUTO_MAX_MEASURED_SPEED_MM_S || i->right_speed_mm_s< -AUTO_MAX_MEASURED_SPEED_MM_S)
    {stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_OVERSPEED);return 0;}
    d->wrong_left_ms=i->left_speed_mm_s< -AUTO_WRONG_DIRECTION_MM_S?d->wrong_left_ms+AUTO_TICK_MS:0;
    d->wrong_right_ms=i->right_speed_mm_s< -AUTO_WRONG_DIRECTION_MM_S?d->wrong_right_ms+AUTO_TICK_MS:0;
    if(d->wrong_left_ms>=AUTO_WRONG_DIRECTION_MS || d->wrong_right_ms>=AUTO_WRONG_DIRECTION_MS)
    {stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_ENCODER_DIRECTION);return 0;}
    d->stall_left_ms=d->status.left_duty_permille>=AUTO_STALL_DUTY_PERMILLE && i->left_speed_mm_s<AUTO_STALL_SPEED_MM_S?d->stall_left_ms+AUTO_TICK_MS:0;
    d->stall_right_ms=d->status.right_duty_permille>=AUTO_STALL_DUTY_PERMILLE && i->right_speed_mm_s<AUTO_STALL_SPEED_MM_S?d->stall_right_ms+AUTO_TICK_MS:0;
    if(d->stall_left_ms>=AUTO_STALL_TIME_MS || d->stall_right_ms>=AUTO_STALL_TIME_MS)
    {stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_STALL);return 0;}
    return 1;
}
void autonomous_drive_tick(autonomous_drive_t *d,const autonomous_drive_input_t *i)
{
    uint8_t fresh_frame,fresh_feedback,press;
    float target,error,period,left_output,right_output,left_target,right_target,turn_ratio;
    if(!d || !i)return;
    fresh_frame=i->frame_sequence!=d->frame_sequence;
    fresh_feedback=i->feedback_sequence!=d->feedback_sequence;
    if(fresh_feedback){d->feedback_sequence=i->feedback_sequence;d->feedback_ms=i->now_ms;}
    if(fresh_frame){
        d->frame_sequence=i->frame_sequence;d->status.confidence=i->confidence;
        if(i->vision_valid && i->confidence>=minimum_confidence(d,i) && !i->element_fault){
            d->bad_frames=0;if(d->good_frames<255)++d->good_frames;
        }else{d->good_frames=0;d->zebra_frames=0;d->clear_frames=0;if(d->bad_frames<255)++d->bad_frames;}
    }
    /* 原始停车键优先，停止后必须释放并重新按启动键。 */
    if(i->stop_pressed || (d->status.active && i->cancel_pressed)){
        stop_drive(d,AUTO_DRIVE_STOPPED,AUTO_FAULT_NONE);d->key_time_ms=i->now_ms;return;
    }
    press=key_pressed(d,i);
    if(press){
        if(d->status.active){stop_drive(d,AUTO_DRIVE_STOPPED,AUTO_FAULT_NONE);return;}
        if(!d->config_valid){stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_CONFIG);return;}
        if(!i->ready || i->remote_busy || i->cancel_pressed || d->good_frames<AUTO_START_GOOD_FRAMES ||
           !i->vision_valid || i->confidence<minimum_confidence(d,i) || i->element_fault ||
           (uint32_t)(i->now_ms-i->frame_ms)>AUTO_FRAME_MAX_AGE_MS ||
           (uint32_t)(i->now_ms-d->feedback_ms)>AUTO_FEEDBACK_MAX_AGE_MS){
            stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_NOT_READY);return;
        }
        begin_drive(d,i);
    }
    if(!d->status.active)return;
    d->status.left_speed_mm_s=i->left_speed_mm_s;d->status.right_speed_mm_s=i->right_speed_mm_s;
    if((uint32_t)(i->now_ms-i->frame_ms)>AUTO_FRAME_MAX_AGE_MS){stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_CAMERA_STALE);return;}
    if((uint32_t)(i->now_ms-d->feedback_ms)>AUTO_FEEDBACK_MAX_AGE_MS){stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_ENCODER_STALE);return;}
    if((uint32_t)(i->now_ms-d->started_ms)>AUTO_SERVO_MAX_AGE_MS &&
       (i->servo_generation!=d->status.run_generation || (uint32_t)(i->now_ms-i->servo_service_ms)>AUTO_SERVO_MAX_AGE_MS))
    {stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_SERVO_STALE);return;}
    if(i->element_fault){stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_ELEMENT);return;}
    if(d->bad_frames>=AUTO_BAD_FRAMES_TO_STOP){stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_PATH_LOST);return;}
    if((uint32_t)(i->now_ms-d->started_ms)>AUTO_MAX_RUN_TIME_MS){stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_RUN_TIMEOUT);return;}
    if(!feedback_guard(d,i))return;
    if(fresh_feedback && i->forward_step_um>0){
        uint32_t sum=d->distance_remainder_um+(uint32_t)i->forward_step_um;
        d->status.distance_mm+=sum/1000U;d->distance_remainder_um=sum%1000U;
    }
    if(fresh_frame && i->vision_valid && i->confidence>=minimum_confidence(d,i))update_marker(d,i);
    if(d->status.state==AUTO_DRIVE_FINISHING){
        if(d->status.distance_mm-d->finish_distance_mm>=AUTO_FINISH_ADVANCE_MM){stop_drive(d,AUTO_DRIVE_FINISHED,AUTO_FAULT_NONE);return;}
        if((uint32_t)(i->now_ms-d->finish_ms)>AUTO_FINISH_MAX_TIME_MS){stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_FINISH_TIMEOUT);return;}
    }
    if(d->status.state==AUTO_DRIVE_STARTING){
        if((uint32_t)(i->now_ms-d->started_ms)<AUTO_CENTER_SETTLE_MS || i->servo_generation!=d->status.run_generation){zero_output(d);return;}
        d->status.state=AUTO_DRIVE_RUNNING;
    }
    /* 第一帧低可信即撤销PWM；连续低可信才锁存故障，恢复不继承积分。 */
    if(!i->vision_valid || i->confidence<minimum_confidence(d,i)){zero_output(d);return;}
    if(fresh_frame){
        error=AUTO_STEERING_NEAR_WEIGHT*i->near_error_px+AUTO_STEERING_FAR_WEIGHT*i->far_error_px;
        period=d->steering_initialized?(float)(uint32_t)(i->frame_ms-d->steering_frame_ms)/1000.0f:0.02f;
        if(period<0.001f)period=0.001f;
        d->status.steering_permille=(int16_t)control_pid_step(&d->steering_pid,0.0f,error,0.0f,period);
        d->steering_frame_ms=i->frame_ms;d->steering_initialized=1;
    }
    target=AUTO_CRUISE_SPEED_MM_S;
    if(i->curve || abs(i->near_error_px)>12 || abs(i->far_error_px)>16)target=minimum(target,AUTO_CURVE_SPEED_MM_S);
    if(i->element_slow || i->confidence<65)target=minimum(target,AUTO_ELEMENT_SPEED_MM_S);
    if(d->status.state==AUTO_DRIVE_FINISHING)target=minimum(target,AUTO_FINISH_SPEED_MM_S);
    d->status.target_speed_mm_s=minimum(target,d->status.target_speed_mm_s+AUTO_ACCEL_MM_S2*0.01f);
    d->left_filtered+=AUTO_SPEED_FILTER_ALPHA*((float)i->left_speed_mm_s-d->left_filtered);
    d->right_filtered+=AUTO_SPEED_FILTER_ALPHA*((float)i->right_speed_mm_s-d->right_filtered);
    turn_ratio=AUTO_TURN_DIFFERENTIAL_RATIO*d->status.steering_permille/1000.0f;
    left_target=d->status.target_speed_mm_s*(1.0f-turn_ratio);
    right_target=d->status.target_speed_mm_s*(1.0f+turn_ratio);
    left_output=control_pid_step(&d->left_pid,left_target,d->left_filtered,left_target*AUTO_SPEED_FEEDFORWARD,0.01f);
    right_output=control_pid_step(&d->right_pid,right_target,d->right_filtered,right_target*AUTO_SPEED_FEEDFORWARD,0.01f);
    if(!d->left_pid.valid || !d->right_pid.valid || !d->steering_pid.valid){stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_CONFIG);return;}
    d->status.left_duty_permille=duty_slew(d->status.left_duty_permille,left_output);
    d->status.right_duty_permille=duty_slew(d->status.right_duty_permille,right_output);
}
