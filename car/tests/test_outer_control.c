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
int main(void)
{
    int row,col;unsigned frame;autonomous_drive_status_t s;
    memset(keys,1,sizeof keys);motor_control_init();servo_control_init(0);autonomous_control_init();
    keys[C15]=0;step(8,1,1);CHECK(!autonomous_control_is_active());
    motor_control_enable_test();step(10,1,1);CHECK(!autonomous_control_is_active());
    keys[C15]=1;step(5,1,1);keys[C15]=0;step(4,1,1);keys[C15]=1;step(85,1,1);
    CHECK(autonomous_control_is_active());
    memset(outer_gray,25,sizeof outer_gray);
    for(row=30;row<=116;++row) {
        int width=44+(row-30)*123/86;
        int center=93+18+70*(116-row)/86;
        for(col=center-width/2;col<188;++col)if(col>=0)outer_gray[row*188+col]=220;
    }
    memset(outer_gray+45*188,25,13*188);memset(outer_gray+30*188,25,4*188);
    track_vision_reset();
    for(frame=0;frame<30;++frame)gray_frame();
    autonomous_control_get_status(&s);
    CHECK(s.active && s.confidence==40 && s.minimum_confidence==40 && s.curve_outer_supported);
    CHECK(s.curve_outer_near_rows==23 && s.curve_outer_far_rows==13);
    CHECK(s.target_speed_mm_s==AUTO_CURVE_OUTER_SPEED_MM_S && s.steering_permille==-1000);
    CHECK(servo_control_get_status()->target_pulse_us==SERVO_REFERENCE_CENTER_US-SERVO_AUTONOMOUS_EXCURSION_US);
    CHECK(servo_control_get_status()->current_pulse_us==servo_control_get_status()->target_pulse_us);
    /* 再缺一行得到Q39：第一帧撤输出，第三个新帧锁存LOST。 */
    memset(outer_gray+34*188,25,188);gray_frame();
    autonomous_control_get_status(&s);
    CHECK(s.active && s.left_duty_permille==0 && s.right_duty_permille==0 && s.steering_permille==0);
    CHECK(s.path_reason==AUTO_PATH_Q_LOW && s.diagnostic_steering_permille==-1000);
    gray_frame();gray_frame();autonomous_control_get_status(&s);
    CHECK(!s.active && s.fault==AUTO_FAULT_PATH_LOST && s.confidence==39 && s.minimum_confidence==40);
    /* 停车后图像恢复不得自动启动或覆盖触发帧证据。 */
    for(col=0;col<188;++col)outer_gray[34*188+col]=col>=93+18+70*(116-34)/86-(44+(34-30)*123/86)/2?220:25;
    for(frame=0;frame<4;++frame)gray_frame();
    autonomous_control_get_status(&s);
    CHECK(!s.active && s.confidence==39 && s.path_reason==AUTO_PATH_Q_LOW);
    printf("%u real gray outer-control checks passed\n",checks);return 0;
}
