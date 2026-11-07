#include "autonomous_drive.h"
#include "autonomous_config.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static void zero_output(autonomous_drive_t *d)
{
    d->status.left_duty_permille=0;d->status.right_duty_permille=0;
    d->status.target_speed_mm_s=0;d->status.steering_permille=0;
    d->status.left_target_mm_s=d->status.right_target_mm_s=0;
    d->status.left_integral=d->status.right_integral=0;
    d->status.left_pid_request=d->status.right_pid_request=0;
    control_pid_reset(&d->left_pid);control_pid_reset(&d->right_pid);
    control_pid_reset(&d->steering_pid);d->steering_initialized=0;d->steering_geometry=0;
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
    if(!(AUTO_CRUISE_SPEED_MM_S>0 && isfinite(AUTO_CRUISE_SPEED_MM_S) &&
         VEHICLE_WHEELBASE_MM>0 && VEHICLE_MAX_STEER_RAD>0 && VEHICLE_MAX_STEER_RAD<1.5f &&
         isfinite(AUTO_TURN_DIFFERENTIAL_RATIO) && AUTO_TURN_DIFFERENTIAL_RATIO>=0 && AUTO_TURN_DIFFERENTIAL_RATIO<1 &&
         isfinite(AUTO_ACCEL_MM_S2) && AUTO_ACCEL_MM_S2>=0 && AUTO_FINISH_DECEL_MM_S2>0))d->config_valid=0;
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
    d->status.marker_phase=AUTO_MARKER_NONE;d->status.finish_advance_mm=0;
    d->started_ms=i->now_ms;d->marker_present=0;d->marker_armed=1;
    d->zebra_frames=0;d->clear_frames=0;d->wrong_left_ms=0;d->wrong_right_ms=0;
    d->stall_left_ms=0;d->stall_right_ms=0;
    d->status.finish_exit_confirmed=0;
    d->finish_tail_seen=d->finish_clear_pending=0;
    d->finish_speed_cap=0;
    d->left_filtered=(float)i->left_speed_mm_s;d->right_filtered=(float)i->right_speed_mm_s;
}
static void update_marker(autonomous_drive_t *d,const autonomous_drive_input_t *i)
{
    uint8_t visible=i->zebra_visible || i->zebra;
    /* 起终点只累计新图像中的标记证据。 */
    if(i->zebra){if(d->zebra_frames<255)++d->zebra_frames;}else d->zebra_frames=0;
    if(visible) {
        d->clear_frames=0;
        if(d->status.marker_phase==AUTO_MARKER_NONE || d->status.marker_phase==AUTO_MARKER_CLEAR) {
            d->status.marker_phase=AUTO_MARKER_APPROACH;
        }
        if(i->zebra)d->status.marker_phase=AUTO_MARKER_CROSSING;
    } else {
        if(d->clear_frames<255)++d->clear_frames;
        if(d->clear_frames>=AUTO_ZEBRA_CLEAR_FRAMES && d->status.marker_phase)
            d->status.marker_phase=AUTO_MARKER_CLEAR;
    }
    if(!visible && d->clear_frames>=AUTO_ZEBRA_CLEAR_FRAMES &&
       (!d->status.start_marker_seen || d->status.distance_mm-d->marker_distance_mm>=AUTO_ZEBRA_CLEAR_DISTANCE_MM))
    {d->marker_present=0;d->marker_armed=1;d->status.marker_phase=AUTO_MARKER_NONE;}
    if(d->zebra_frames<AUTO_ZEBRA_CONFIRM_FRAMES || !d->marker_armed || d->marker_present)return;
    if(!d->status.start_marker_seen){d->marker_present=1;d->marker_armed=0;d->status.start_marker_seen=1;d->marker_ms=i->now_ms;d->marker_distance_mm=d->status.distance_mm;}
    else if(d->status.state==AUTO_DRIVE_RUNNING &&
            (uint32_t)(i->now_ms-d->marker_ms)>=AUTO_MIN_LAP_TIME_MS &&
            d->status.distance_mm-d->marker_distance_mm>=AUTO_MIN_LAP_DISTANCE_MM)
    {
        d->marker_present=1;d->marker_armed=0;d->status.state=AUTO_DRIVE_FINISHING;
        d->finish_speed_cap=fmaxf(0,d->status.target_speed_mm_s);
        d->finish_tail_seen=d->finish_clear_pending=0;d->status.finish_exit_confirmed=0;
    }
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
    d->status.has_path=i->has_path;d->status.element=i->element;
    d->status.near_error_px=i->near_error_px;d->status.far_error_px=i->far_error_px;
    d->status.zebra_visible=i->zebra_visible || i->zebra;
    if(fresh_feedback){d->feedback_sequence=i->feedback_sequence;d->feedback_ms=i->now_ms;}
    if(fresh_frame)d->frame_sequence=i->frame_sequence;
    /* S3仅停车，不再切换视觉模式；停止后释放并重新按启动键。 */
    if(i->stop_pressed || (d->status.active && i->cancel_pressed)){
        stop_drive(d,AUTO_DRIVE_STOPPED,AUTO_FAULT_NONE);d->key_time_ms=i->now_ms;return;
    }
    press=key_pressed(d,i);
    if(press){
        if(d->status.active){stop_drive(d,AUTO_DRIVE_STOPPED,AUTO_FAULT_NONE);return;}
        if(!d->config_valid){stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_CONFIG);return;}
        if(!i->ready || i->remote_busy || i->cancel_pressed ||
           (uint32_t)(i->now_ms-d->feedback_ms)>AUTO_FEEDBACK_MAX_AGE_MS){
            stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_NOT_READY);return;
        }
        begin_drive(d,i);
    }
    if(!d->status.active)return;
    d->status.left_speed_mm_s=i->left_speed_mm_s;d->status.right_speed_mm_s=i->right_speed_mm_s;
    if((uint32_t)(i->now_ms-d->feedback_ms)>AUTO_FEEDBACK_MAX_AGE_MS){stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_ENCODER_STALE);return;}
    if((uint32_t)(i->now_ms-d->started_ms)>AUTO_MAX_RUN_TIME_MS){stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_RUN_TIMEOUT);return;}
    if(!feedback_guard(d,i))return;
    if(fresh_feedback && i->forward_step_um>0){
        uint32_t sum=d->distance_remainder_um+(uint32_t)i->forward_step_um;
        d->status.distance_mm+=sum/1000U;d->distance_remainder_um=sum%1000U;
    }
    if(fresh_frame)update_marker(d,i);
    if(d->status.state==AUTO_DRIVE_FINISHING){
        if(fresh_frame) {
            if(i->zebra || i->zebra_visible) {
                if(i->zebra_near_row!=255 && i->zebra_near_row>=AUTO_FINISH_EXIT_ROW)d->finish_tail_seen=1;
                d->finish_clear_pending=0;d->status.finish_exit_confirmed=0;
            } else if(d->finish_tail_seen) {
                if(!d->finish_clear_pending) {
                    d->finish_clear_pending=1;d->finish_clear_mm=d->status.distance_mm;
                }
                if(d->clear_frames>=AUTO_ZEBRA_CLEAR_FRAMES) {
                    d->status.finish_exit_confirmed=1;
                }
            } else {d->finish_clear_pending=0;d->status.finish_exit_confirmed=0;}
        }
        d->status.finish_advance_mm=d->finish_clear_pending?d->status.distance_mm-d->finish_clear_mm:0;
        if(fresh_frame && d->status.finish_exit_confirmed &&
           d->status.finish_advance_mm>=AUTO_FINISH_ADVANCE_MM &&
           d->clear_frames>=AUTO_ZEBRA_CLEAR_FRAMES && !i->zebra && !i->zebra_visible) {
            stop_drive(d,AUTO_DRIVE_FINISHED,AUTO_FAULT_NONE);return;
        }
    }
    if(d->status.state==AUTO_DRIVE_STARTING){
        if((uint32_t)(i->now_ms-d->started_ms)<AUTO_CENTER_SETTLE_MS || i->servo_generation!=d->status.run_generation){zero_output(d);return;}
        d->status.state=AUTO_DRIVE_RUNNING;
    }
    /* 恢复原几何转向：地面目标直接算舵角；没有目标时才使用像素PID。 */
    if(fresh_frame) {
        float x=i->geometry.target_right_mm,y=i->geometry.target_forward_mm;
        float distance_squared=x*x+y*y;
        if(i->geometry.valid && isfinite(x) && isfinite(y) && y>0 &&
           isfinite(distance_squared) && distance_squared>0) {
            float command=-atanf(2.0f*VEHICLE_WHEELBASE_MM*x/distance_squared)*1000.0f/VEHICLE_MAX_STEER_RAD;
            d->status.steering_permille=(int16_t)fmaxf(-AUTO_STEERING_LIMIT_PERMILLE,fminf(AUTO_STEERING_LIMIT_PERMILLE,command));
            control_pid_reset(&d->steering_pid);d->steering_geometry=1;
        } else if(i->has_path) {
            if(d->steering_geometry){control_pid_reset(&d->steering_pid);d->steering_initialized=0;d->steering_geometry=0;}
            float near_weight=AUTO_STEERING_NEAR_WEIGHT,far_weight=AUTO_STEERING_FAR_WEIGHT;
            int near_magnitude=abs(i->near_error_px);
            if((int32_t)i->near_error_px*i->far_error_px<0 && near_magnitude>AUTO_STEERING_NEAR_PRIORITY_START_PX) {
                float blend=(float)(near_magnitude-AUTO_STEERING_NEAR_PRIORITY_START_PX)/
                    (AUTO_STEERING_NEAR_PRIORITY_FULL_PX-AUTO_STEERING_NEAR_PRIORITY_START_PX);
                if(blend>1.0f)blend=1.0f;
                near_weight+=far_weight*blend;far_weight*=1.0f-blend;
            }
            error=near_weight*i->near_error_px+far_weight*i->far_error_px;
            period=d->steering_initialized?(float)(uint32_t)(i->frame_ms-d->steering_frame_ms)*.001f:.02f;
            if(period<.001f)period=.001f;
            d->status.steering_permille=(int16_t)control_pid_step(&d->steering_pid,0,error,0,period);
            d->steering_frame_ms=i->frame_ms;d->steering_initialized=1;
        }
    }
    target=AUTO_CRUISE_SPEED_MM_S;
    /* 起终点动作独立于正常循迹；不根据路径质量切换速度。 */
    if(d->status.state==AUTO_DRIVE_FINISHING) {
        float remaining=(float)AUTO_FINISH_ADVANCE_MM-d->status.finish_advance_mm;
        float cap=sqrtf(2*AUTO_FINISH_DECEL_MM_S2*fmaxf(0,remaining));
        if(cap<d->finish_speed_cap)d->finish_speed_cap=cap;
        target=fminf(target,d->finish_speed_cap);
    } else if((d->status.marker_phase==AUTO_MARKER_APPROACH || d->status.marker_phase==AUTO_MARKER_CROSSING) &&
              !(d->status.start_marker_seen && (uint32_t)(i->now_ms-d->marker_ms)>=AUTO_MIN_LAP_TIME_MS &&
                d->status.distance_mm-d->marker_distance_mm>=AUTO_MIN_LAP_DISTANCE_MM))
        target=fminf(target,AUTO_FINISH_SPEED_MM_S);
    d->left_filtered+=AUTO_SPEED_FILTER_ALPHA*((float)i->left_speed_mm_s-d->left_filtered);
    d->right_filtered+=AUTO_SPEED_FILTER_ALPHA*((float)i->right_speed_mm_s-d->right_filtered);
    turn_ratio=AUTO_TURN_DIFFERENTIAL_RATIO*
        tanf(VEHICLE_MAX_STEER_RAD*d->status.steering_permille/1000.0f)/tanf(VEHICLE_MAX_STEER_RAD);
    left_target=target*(1-turn_ratio);right_target=target*(1+turn_ratio);
    /* 只在差速分配后限制单轮目标上升，复用上一拍目标，不重复平滑PWM。 */
    if(AUTO_ACCEL_MM_S2>0) {
        float step=AUTO_ACCEL_MM_S2*(AUTO_TICK_MS*.001f);
        left_target=fminf(left_target,d->status.left_target_mm_s+step);
        right_target=fminf(right_target,d->status.right_target_mm_s+step);
    }
    d->status.left_target_mm_s=left_target;d->status.right_target_mm_s=right_target;
    d->status.target_speed_mm_s=(left_target+right_target)*.5f;
    d->status.left_filtered_mm_s=d->left_filtered;d->status.right_filtered_mm_s=d->right_filtered;
    left_output=control_pid_step(&d->left_pid,left_target,d->left_filtered,left_target*AUTO_SPEED_FEEDFORWARD,0.01f);
    right_output=control_pid_step(&d->right_pid,right_target,d->right_filtered,right_target*AUTO_SPEED_FEEDFORWARD,0.01f);
    if(!d->left_pid.valid || !d->right_pid.valid || !d->steering_pid.valid){stop_drive(d,AUTO_DRIVE_FAULT,AUTO_FAULT_CONFIG);return;}
    /* 单轮目标直接进入速度PID，输出只保留PID自身幅值限制。 */
    d->status.left_duty_permille=(uint16_t)left_output;
    d->status.right_duty_permille=(uint16_t)right_output;
    d->status.left_pid_request=left_output;d->status.right_pid_request=right_output;
    d->status.left_integral=d->left_pid.integral;d->status.right_integral=d->right_pid.integral;
}
