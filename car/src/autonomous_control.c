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
    track_path_geometry_t geometry;
    uint32_t sequence,time_ms;
    uint8_t has_path,element,zebra,zebra_visible,zebra_near_row;
    int16_t near_error_px,far_error_px;
} autonomous_vision_mailbox_t;
static autonomous_drive_t drive;
static volatile autonomous_drive_status_t published_status;
static volatile autonomous_vision_mailbox_t vision_mailbox;
static volatile uint32_t servo_generation;
static uint8_t servo_owned;
static volatile uint8_t network_busy;
static volatile autonomous_control_trace_t trace;

void autonomous_control_init(void)
{
    autonomous_drive_init(&drive);
    published_status=drive.status;
    memset((void *)&vision_mailbox,0,sizeof vision_mailbox);
    servo_generation=0;servo_owned=0;
    network_busy=0;
    memset((void *)&trace,0,sizeof trace);
    gpio_init(AUTO_START_KEY_PIN,GPI,GPIO_HIGH,GPI_PULL_UP);
}
void autonomous_control_publish_vision(const track_vision_result_t *v,uint32_t sequence,uint32_t frame_ms)
{
    autonomous_vision_mailbox_t m;
    uint32_t mask;
    if(!v)return;
    m.geometry=v->geometry;
    m.sequence=sequence;m.time_ms=frame_ms;m.has_path=v->has_path;m.element=(uint8_t)v->element;
    m.near_error_px=v->near_error_px;m.far_error_px=v->far_error_px;
    m.zebra=v->zebra && v->zebra_near_row!=255 && v->zebra_near_row>=AUTO_ZEBRA_MIN_ROW;
    m.zebra_visible=v->zebra || v->zebra_candidate;m.zebra_near_row=v->zebra_near_row;
    mask=__get_PRIMASK();__disable_irq();
    vision_mailbox=m;
    __set_PRIMASK(mask);
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
    i.geometry=v.geometry;
    i.frame_sequence=v.sequence;i.frame_ms=v.time_ms;i.has_path=v.has_path;i.element=v.element;
    i.near_error_px=v.near_error_px;i.far_error_px=v.far_error_px;
    i.zebra=v.zebra;i.zebra_visible=v.zebra_visible;i.zebra_near_row=v.zebra_near_row;
    i.servo_generation=servo_generation;
    autonomous_drive_tick(&drive,&i);
    if(trace.run_generation!=drive.status.run_generation) {
        memset((void *)&trace,0,sizeof trace);trace.run_generation=drive.status.run_generation;
    }
    trace.latest.now_ms=now_ms;trace.latest.frame_ms=v.time_ms;trace.latest.sequence=v.sequence;
    trace.latest.raw_left_delta_counts=feedback.left.delta_counts;
    trace.latest.raw_right_delta_counts=feedback.right.delta_counts;
    trace.latest.motion=drive.status;
    if(!trace.frozen && drive.status.state==AUTO_DRIVE_FAULT){trace.fault_frame=trace.latest;trace.frozen=1;}
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
/* 主循环执行最近PID结果，不等待图像消费/执行握手。 */
uint8_t autonomous_control_service(uint32_t now_ms)
{
    autonomous_drive_status_t s,current;
    uint32_t mask;
    autonomous_control_get_status(&s);
    if(s.active){servo_control_autonomous_apply(1,s.steering_permille,now_ms);servo_owned=1;}
    else if(servo_owned){servo_control_autonomous_apply(0,0,now_ms);servo_owned=0;}
    /* 硬件调用期间PIT可能停车；旧会话不得重新占用舵机。 */
    autonomous_control_get_status(&current);
    if(!current.active || current.run_generation!=s.run_generation){
        if(servo_owned){servo_control_autonomous_apply(0,0,now_ms);servo_owned=0;}
    }else if(s.active && servo_control_get_status()->output_enabled){
        mask=__get_PRIMASK();__disable_irq();
        if(published_status.active && published_status.run_generation==s.run_generation)servo_generation=s.run_generation;
        __set_PRIMASK(mask);
    }
    return autonomous_control_is_active();
}
