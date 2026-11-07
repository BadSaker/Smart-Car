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
int main(int argc,char **argv)
{
    autonomous_drive_status_t s;FILE *fixture;uint8_t saved[188*120];int16_t steering;
    memset(keys,1,sizeof keys);motor_control_init();servo_control_init(0);autonomous_control_init();
    motor_control_enable_test();step(5,1,1);keys[C15]=0;step(4,1,1);keys[C15]=1;step(85,1,1);
    CHECK(argc==2);fixture=fopen(argv[1],"rb");CHECK(fixture!=NULL);
    CHECK(fread(outer_gray,1,sizeof outer_gray,fixture)==sizeof outer_gray);fclose(fixture);
    track_vision_reset();for(unsigned n=0;n<settle_ticks()/3+1;++n)gray_frame();autonomous_control_get_status(&s);
    CHECK(s.active && s.has_path && s.target_speed_mm_s==AUTO_CURVE_OUTER_SPEED_MM_S && s.steering_permille<0);
    CHECK(servo_control_get_status()->current_pulse_us==servo_control_get_status()->target_pulse_us);
    memcpy(saved,outer_gray,sizeof saved);steering=s.steering_permille;
    memset(outer_gray,25,sizeof outer_gray);gray_frame();autonomous_control_get_status(&s);
    CHECK(s.active && !s.has_path && s.target_speed_mm_s>0 && s.planned_speed_mm_s>0 && s.target_speed_mm_s<=AUTO_CRUISE_SPEED_MM_S && s.steering_permille==steering);
    CHECK(s.left_duty_permille>0 && s.right_duty_permille>0);
    memcpy(outer_gray,saved,sizeof saved);gray_frame();autonomous_control_get_status(&s);
    CHECK(s.active && s.has_path && s.planned_speed_mm_s==AUTO_CURVE_SPEED_MM_S);
    gray_frame();gray_frame();autonomous_control_get_status(&s);
    CHECK(s.active && s.planned_speed_mm_s==AUTO_CURVE_OUTER_SPEED_MM_S && s.target_speed_mm_s==AUTO_CURVE_OUTER_SPEED_MM_S);
    keys[C14]=0;gray_frame();CHECK(!autonomous_control_is_active());
    printf("%u gray-to-PID integration checks passed\n",checks);return 0;
}
