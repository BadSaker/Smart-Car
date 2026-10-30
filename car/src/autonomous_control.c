#include "autonomous_control.h"
#include "autonomous_config.h"
#include "encoder_feedback.h"
#include "encoder_config.h"
#include "motor_control.h"
#include "servo_control.h"
#include "zf_driver_gpio.h"
#include "fsl_common.h"
#include <string.h>

typedef struct {
    uint32_t sequence,time_ms;
    uint8_t valid,confidence,zebra,element_fault,element_slow,element_supported,curve;
    uint8_t near_supported,outer_supported,outer_side,outer_near_rows,outer_far_rows,element;
    autonomous_path_reason_t outer_reject_reason;
    int16_t near_error_px,far_error_px;
    track_path_geometry_t geometry;
    autonomous_trace_frame_t diagnostic;
} autonomous_vision_mailbox_t;
static autonomous_drive_t drive;
static volatile autonomous_drive_status_t published_status;
static volatile autonomous_vision_mailbox_t vision_mailbox;
static volatile uint32_t servo_generation,servo_service_ms;
static uint8_t servo_owned;
static volatile uint8_t network_busy;
static volatile autonomous_control_trace_t trace;

void autonomous_control_init(void)
{
    autonomous_drive_init(&drive);
    published_status=drive.status;
    memset((void *)&vision_mailbox,0,sizeof vision_mailbox);
    servo_generation=0;servo_service_ms=0;servo_owned=0;
    network_busy=0;
    memset((void *)&trace,0,sizeof trace);
    gpio_init(AUTO_START_KEY_PIN,GPI,GPIO_HIGH,GPI_PULL_UP);
}
void autonomous_control_publish_vision(const track_vision_result_t *v,uint32_t sequence,uint32_t frame_ms)
{
    autonomous_vision_mailbox_t m;
    uint32_t mask;
    if(!v)return;
    m.sequence=sequence;m.time_ms=frame_ms;m.valid=v->valid;m.confidence=v->confidence;
    m.zebra=v->zebra && v->zebra_near_row!=255 && v->zebra_near_row>=AUTO_ZEBRA_MIN_ROW;
    m.element_fault=(v->fault & TRACK_FAULT_ELEMENT_TIMEOUT)!=0;
    m.element_slow=v->element>=TRACK_ELEMENT_CROSS;
    m.curve=v->element==TRACK_ELEMENT_CURVE;
    m.element=(uint8_t)v->element;m.near_supported=v->near_path_supported;
    m.outer_side=(uint8_t)v->curve_outer_side;m.outer_near_rows=v->curve_outer_near_rows;
    m.outer_far_rows=v->curve_outer_far_rows;m.geometry=v->geometry;
    m.outer_supported=m.curve && v->valid && !v->fault && v->curve_outer_supported &&
        (v->curve_outer_side==TRACK_SIDE_LEFT || v->curve_outer_side==TRACK_SIDE_RIGHT);
    switch(v->curve_outer_reject) {
        case TRACK_OUTER_REJECT_NEAR:m.outer_reject_reason=AUTO_PATH_OUTER_NEAR;break;
        case TRACK_OUTER_REJECT_FAR:m.outer_reject_reason=AUTO_PATH_OUTER_FAR;break;
        case TRACK_OUTER_REJECT_CONTINUITY:m.outer_reject_reason=AUTO_PATH_OUTER_CONTINUITY;break;
        default:m.outer_reject_reason=AUTO_PATH_ACCEPTED;break;
    }
    m.element_supported=v->valid && !v->fault &&
        (v->element==TRACK_ELEMENT_CROSS || v->element==TRACK_ELEMENT_ROUNDABOUT_ENTER ||
         v->element==TRACK_ELEMENT_ROUNDABOUT_INSIDE || v->element==TRACK_ELEMENT_ROUNDABOUT_EXIT ||
         (v->element==TRACK_ELEMENT_ROUNDABOUT_APPROACH && v->ring_entry_supported));
    m.near_error_px=v->near_error_px;m.far_error_px=v->far_error_px;
    memset(&m.diagnostic,0,sizeof m.diagnostic);
    m.diagnostic.frame_ms=frame_ms;m.diagnostic.sequence=sequence;
    m.diagnostic.ring_left_reject=v->ring_left_reject;m.diagnostic.ring_right_reject=v->ring_right_reject;
    m.diagnostic.left_island=v->left_island;m.diagnostic.right_island=v->right_island;
    m.diagnostic.entry_supported=v->ring_entry_supported;m.diagnostic.fault_element=(uint8_t)v->fault_element;
    m.diagnostic.left_fork=v->left_fork;m.diagnostic.right_fork=v->right_fork;
    m.diagnostic.left_opening_rows=v->left_opening_rows;m.diagnostic.right_opening_rows=v->right_opening_rows;
    m.diagnostic.raw_near_error_px=v->raw_near_error_px;m.diagnostic.raw_far_error_px=v->raw_far_error_px;
    m.diagnostic.element_diagnostic=v->element_diagnostic;
    mask=__get_PRIMASK();__disable_irq();vision_mailbox=m;__set_PRIMASK(mask);
}
void autonomous_control_tick_10ms(uint32_t now_ms)
{
    autonomous_drive_input_t i;
    autonomous_vision_mailbox_t v=vision_mailbox;
    encoder_feedback_snapshot_t feedback;
    motor_control_status_t motor;
    int64_t counts;
    encoder_feedback_get_snapshot(&feedback);motor_control_get_status(&motor);
    memset(&i,0,sizeof i);i.now_ms=now_ms;
    i.ready=motor.ready && motor.state!=MOTOR_TEST_CONFIG_ERROR && !network_busy;
    i.remote_busy=motor.remote_armed || (!motor.autonomous_active &&
        (motor.channel1_duty_permille!=0 || motor.channel2_duty_permille!=0));
    i.start_pressed=gpio_get_level(AUTO_START_KEY_PIN)==GPIO_LOW;
    i.stop_pressed=gpio_get_level(AUTO_STOP_KEY_PIN)==GPIO_LOW;
    i.cancel_pressed=gpio_get_level(AUTO_CANCEL_KEY_PIN)==GPIO_LOW;
    i.feedback_sequence=feedback.sample_sequence;
    i.left_speed_mm_s=AUTO_LEFT_ENCODER_SIGN*feedback.left.speed_mm_s;
    i.right_speed_mm_s=AUTO_RIGHT_ENCODER_SIGN*feedback.right.speed_mm_s;
    counts=(int64_t)AUTO_LEFT_ENCODER_SIGN*feedback.left.delta_counts+
           (int64_t)AUTO_RIGHT_ENCODER_SIGN*feedback.right.delta_counts;
    /* 含两轮平均；保持有符号64位中间量，轮径/传动比只读取现有编码器配置。 */
    i.forward_step_um=(int32_t)(counts*ENCODER_WHEEL_CIRCUMFERENCE_UM*ENCODER_TO_WHEEL_RATIO_DEN/
        (2LL*ENCODER_PULSES_PER_REV*ENCODER_STEP_EDGE_FACTOR*ENCODER_TO_WHEEL_RATIO_NUM));
    i.frame_sequence=v.sequence;i.frame_ms=v.time_ms;i.vision_valid=v.valid;i.confidence=v.confidence;
    i.zebra=v.zebra;i.element_fault=v.element_fault;i.element_slow=v.element_slow;i.element_supported=v.element_supported;i.curve=v.curve;
    i.vision_near_supported=v.near_supported;i.curve_outer_supported=v.outer_supported;
    i.curve_outer_side=v.outer_side;i.curve_outer_near_rows=v.outer_near_rows;
    i.curve_outer_far_rows=v.outer_far_rows;i.element=v.element;i.curve_outer_reject_reason=v.outer_reject_reason;
    i.near_error_px=v.near_error_px;i.far_error_px=v.far_error_px;i.geometry=v.geometry;
    i.servo_generation=servo_generation;i.servo_service_ms=servo_service_ms;
    {
        int16_t steering_before=drive.status.steering_permille;
        autonomous_trace_frame_t frame=v.diagnostic;
        frame.motion=drive.status;
        frame.feedback_sequence=feedback.sample_sequence;frame.servo_service_ms=servo_service_ms;
        frame.raw_left_delta_counts=feedback.left.delta_counts;frame.raw_right_delta_counts=feedback.right.delta_counts;
        frame.left_speed_mm_s=i.left_speed_mm_s;frame.right_speed_mm_s=i.right_speed_mm_s;
        frame.servo_pulse_us=servo_control_get_status()->current_pulse_us;
        autonomous_drive_tick(&drive,&i);
        if(trace.run_generation!=drive.status.run_generation) {
            memset((void *)&trace,0,sizeof trace);trace.run_generation=drive.status.run_generation;
        }
        if(!trace.frozen && (drive.status.active || drive.status.state==AUTO_DRIVE_FAULT)) {
            frame.now_ms=now_ms;frame.confidence=i.confidence;frame.valid=i.vision_valid;frame.element=i.element;
            frame.minimum_confidence=drive.status.minimum_confidence;
            frame.near_error_px=i.near_error_px;frame.far_error_px=i.far_error_px;
            frame.steering_permille=steering_before;
            if(drive.status.state==AUTO_DRIVE_FAULT) {
                if(!trace.has_first_bad){trace.first_bad=frame;trace.has_first_bad=1;}
                trace.fault_frame=frame;trace.fault=drive.status.fault;trace.frozen=1;
            } else if(i.vision_valid && i.confidence>=drive.status.minimum_confidence &&
                      !i.element_fault && now_ms-i.frame_ms<=AUTO_FRAME_MAX_AGE_MS) {
                frame.steering_permille=drive.status.steering_permille;
                trace.last_good=frame;trace.has_last_good=1;trace.has_first_bad=0;
            } else if(!trace.has_first_bad) {trace.first_bad=frame;trace.has_first_bad=1;}
        }
    }
#if AUTO_MOTOR1_IS_LEFT
    motor_control_autonomous_set(drive.status.active,drive.status.left_duty_permille,drive.status.right_duty_permille);
#else
    motor_control_autonomous_set(drive.status.active,drive.status.right_duty_permille,drive.status.left_duty_permille);
#endif
    published_status=drive.status;
}
void autonomous_control_get_status(autonomous_drive_status_t *out)
{
    uint32_t mask;
    if(!out)return;
    mask=__get_PRIMASK();__disable_irq();*out=published_status;__set_PRIMASK(mask);
}
void autonomous_control_get_trace(autonomous_control_trace_t *out)
{
    uint32_t mask;
    if(!out)return;
    mask=__get_PRIMASK();__disable_irq();*out=trace;__set_PRIMASK(mask);
}
uint8_t autonomous_control_is_active(void)
{
    return published_status.active;
}
void autonomous_control_set_network_busy(uint8_t busy)
{
    network_busy=busy?1U:0U;
}
uint8_t autonomous_control_service(uint32_t now_ms)
{
    autonomous_drive_status_t s,current;
    uint32_t mask;
    autonomous_control_get_status(&s);
    if(s.active){
        servo_control_autonomous_apply(1,s.steering_permille,now_ms);servo_owned=1;
    }else if(servo_owned){servo_control_autonomous_apply(0,0,now_ms);servo_owned=0;}
    /* 硬件调用期间PIT可改变模式，不能向外返回快照中的旧所有权。
     * 若停车/重启发生在本次写入中，立即撤销旧舵角，下一次从新中位开始。 */
    autonomous_control_get_status(&current);
    if(!current.active || current.run_generation!=s.run_generation){
        if(servo_owned){servo_control_autonomous_apply(0,0,now_ms);servo_owned=0;}
    }else if(s.active && servo_control_get_status()->output_enabled){
        mask=__get_PRIMASK();__disable_irq();
        if(published_status.active && published_status.run_generation==s.run_generation){
            servo_generation=s.run_generation;servo_service_ms=now_ms;
        }
        __set_PRIMASK(mask);
    }
    return autonomous_control_is_active();
}
