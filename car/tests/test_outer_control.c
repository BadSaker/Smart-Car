/* 复用硬件桩，用实际44/167灰度图贯通视觉、mailbox、控制与舵机输出。 */
#define main legacy_integration_main
#include "test_autonomous_control.c"
#undef main
#include "servo_config.h"

static uint8_t outer_gray[188*120];
static void gray_frame(void)
{
    unsigned n;track_vision_result_t v;
    track_vision_process(outer_gray,now,100,&v);
    autonomous_control_publish_vision(&v,++frame_sequence,now);
    for(n=0;n<3;++n) {
        now+=10;++feedback.sample_sequence;
        feedback.left.speed_mm_s=AUTO_LEFT_ENCODER_SIGN*80;
        feedback.right.speed_mm_s=AUTO_RIGHT_ENCODER_SIGN*80;
        feedback.left.delta_counts=AUTO_LEFT_ENCODER_SIGN*17;
        feedback.right.delta_counts=AUTO_RIGHT_ENCODER_SIGN*17;
        autonomous_control_tick_10ms(now);motor_control_tick_10ms();autonomous_control_service(now);
    }
}
static void ring_gate(void)
{
    track_vision_result_t v;autonomous_drive_status_t s;unsigned n;
    memset(keys,1,sizeof keys);motor_control_init();servo_control_init(now);autonomous_control_init();
    motor_control_enable_test();step(5,1,1);keys[C15]=0;step(4,1,1);keys[C15]=1;step(85,1,1);
    CHECK(autonomous_control_is_active());
    memset(&v,0,sizeof v);v.valid=1;v.confidence=40;v.element=TRACK_ELEMENT_ROUNDABOUT_APPROACH;
    v.ring_entry_supported=1;v.near_path_supported=1;
    for(n=0;n<6;++n) {
        now+=10;++feedback.sample_sequence;autonomous_control_publish_vision(&v,++frame_sequence,now);
        autonomous_control_tick_10ms(now);autonomous_control_service(now);
    }
    autonomous_control_get_status(&s);CHECK(s.active && s.minimum_confidence==35);
    v.ring_entry_supported=0;
    now+=10;++feedback.sample_sequence;autonomous_control_publish_vision(&v,++frame_sequence,now);
    autonomous_control_tick_10ms(now);autonomous_control_get_status(&s);
    CHECK(s.active && s.minimum_confidence==45 && s.left_duty_permille==0 && s.right_duty_permille==0);
    for(n=0;n<2;++n) {
        now+=10;++feedback.sample_sequence;autonomous_control_publish_vision(&v,++frame_sequence,now);
        autonomous_control_tick_10ms(now);autonomous_control_service(now);
    }
    autonomous_control_get_status(&s);CHECK(!s.active && s.fault==AUTO_FAULT_PATH_LOST);
}
static void element_fault_freeze(void)
{
    track_vision_result_t v;autonomous_drive_status_t s;autonomous_control_trace_t before,after;
    memset(keys,1,sizeof keys);motor_control_init();servo_control_init(now);autonomous_control_init();
    motor_control_enable_test();step(5,1,1);keys[C15]=0;step(4,1,1);keys[C15]=1;step(85,1,1);
    memset(&v,0,sizeof v);v.valid=0;v.confidence=37;v.element=TRACK_ELEMENT_ROUNDABOUT_EXIT;
    v.fault=TRACK_FAULT_ELEMENT_TIMEOUT;v.fault_element=TRACK_ELEMENT_ROUNDABOUT_EXIT;
    v.element_diagnostic.limit_reason=1;v.element_diagnostic.fault_entry_mm=100;
    v.element_diagnostic.fault_traveled_mm=4001;
    now+=10;++feedback.sample_sequence;autonomous_control_publish_vision(&v,++frame_sequence,now);
    autonomous_control_tick_10ms(now);autonomous_control_service(now);
    autonomous_control_get_status(&s);autonomous_control_get_trace(&before);
    CHECK(s.fault==AUTO_FAULT_ELEMENT && !s.active && s.confidence==37 && s.element==TRACK_ELEMENT_ROUNDABOUT_EXIT);
    CHECK(before.frozen && before.fault_frame.element_diagnostic.fault_traveled_mm==4001);
    step(10,1,1);autonomous_control_get_status(&s);autonomous_control_get_trace(&after);
    CHECK(s.fault==AUTO_FAULT_ELEMENT && s.confidence==37 && !s.vision_valid && s.element==TRACK_ELEMENT_ROUNDABOUT_EXIT);
    CHECK(!memcmp(&before,&after,sizeof before));
    keys[C15]=0;step(4,1,1);keys[C15]=1;step(85,1,1);
    autonomous_control_get_status(&s);autonomous_control_get_trace(&after);
    CHECK(s.active && !after.frozen && !after.has_first_bad && after.run_generation>before.run_generation);
}
int main(int argc,char **argv)
{
    unsigned frame;autonomous_drive_status_t s;FILE *fixture;
    autonomous_control_trace_t evidence;
    uint8_t saved_gray[188*120],fault_confidence;
    int16_t steering_before_loss;
    memset(keys,1,sizeof keys);motor_control_init();servo_control_init(0);autonomous_control_init();
    keys[C15]=0;step(8,1,1);CHECK(!autonomous_control_is_active());
    motor_control_enable_test();step(10,1,1);CHECK(!autonomous_control_is_active());
    keys[C15]=1;step(5,1,1);keys[C15]=0;step(4,1,1);keys[C15]=1;step(85,1,1);
    CHECK(autonomous_control_is_active());
    /* 使用恒定地面宽度的冻结合成相机帧，避免把斜向像素半宽当法向道路。 */
    CHECK(argc==2);fixture=fopen(argv[1],"rb");CHECK(fixture!=NULL);
    CHECK(fread(outer_gray,1,sizeof outer_gray,fixture)==sizeof outer_gray);fclose(fixture);
    track_vision_reset();
    for(frame=0;frame<(unsigned)(AUTO_CURVE_OUTER_SPEED_MM_S/(AUTO_ACCEL_MM_S2*0.03f))+5U;++frame)gray_frame();
    autonomous_control_get_status(&s);
    CHECK(s.active && s.confidence>=70 && s.minimum_confidence==40 && s.curve_outer_supported);
    CHECK(s.curve_outer_near_rows==23 && s.curve_outer_far_rows>=20);
    CHECK(s.target_speed_mm_s==AUTO_CURVE_OUTER_SPEED_MM_S && s.steering_permille<0 && s.steering_permille>=-1000);
    CHECK(servo_control_get_status()->target_pulse_us<SERVO_REFERENCE_CENTER_US &&
          servo_control_get_status()->target_pulse_us>=SERVO_REFERENCE_CENTER_US-SERVO_AUTONOMOUS_EXCURSION_US);
    CHECK(servo_control_get_status()->current_pulse_us==servo_control_get_status()->target_pulse_us);
    /* 远处单行缺失不再耗尽可见走廊的裕量；真正遮挡整个前向窗口仍须撤输出。 */
    memset(outer_gray+34*188,25,188);gray_frame();
    autonomous_control_get_status(&s);
    CHECK(s.active && s.confidence>=70 && s.left_duty_permille>0 && s.right_duty_permille>0);
    memcpy(saved_gray,outer_gray,sizeof outer_gray);steering_before_loss=s.steering_permille;
    memset(outer_gray+45*188,25,41*188);gray_frame();
    autonomous_control_get_status(&s);fault_confidence=s.confidence;
    CHECK(s.active && s.left_duty_permille==0 && s.right_duty_permille==0 && s.steering_permille==steering_before_loss);
    CHECK(s.path_reason==AUTO_PATH_NEAR && s.diagnostic_steering_permille==steering_before_loss);
    autonomous_control_get_trace(&evidence);
    CHECK(evidence.has_last_good && evidence.has_first_bad && !evidence.frozen);
    CHECK(!evidence.first_bad.valid && evidence.first_bad.steering_permille==steering_before_loss);
    { uint32_t first_sequence=evidence.first_bad.sequence;
    gray_frame();gray_frame();autonomous_control_get_status(&s);
    autonomous_control_get_trace(&evidence);
    CHECK(evidence.frozen && evidence.fault==AUTO_FAULT_PATH_LOST);
    CHECK(evidence.first_bad.sequence==first_sequence && evidence.fault_frame.sequence>first_sequence);
    }
    CHECK(!s.active && s.fault==AUTO_FAULT_PATH_LOST && s.confidence==fault_confidence);
    /* 停车后图像恢复不得自动启动或覆盖触发帧证据。 */
    memcpy(outer_gray,saved_gray,sizeof outer_gray);
    for(frame=0;frame<4;++frame)gray_frame();
    autonomous_control_get_status(&s);
    CHECK(!s.active && s.confidence==fault_confidence && s.path_reason==AUTO_PATH_NEAR);
    autonomous_control_get_trace(&evidence);CHECK(evidence.frozen && evidence.fault_frame.confidence==fault_confidence);
    ring_gate();
    element_fault_freeze();
    printf("%u real gray outer-control checks passed\n",checks);return 0;
}
