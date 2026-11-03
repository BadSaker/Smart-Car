#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "autonomous_control.h"
#include "motor_control.h"
#include "servo_control.h"
#include "encoder_feedback.h"
#include "autonomous_config.h"
static uint8_t keys[32];static uint32_t duties[3],mask,now,frame_sequence;
static encoder_feedback_snapshot_t feedback;
static unsigned checks;
#define CHECK(c) do {++checks;if(!(c)){printf("line %d\n",__LINE__);fflush(stdout);exit(1);}}while(0)
void gpio_init(gpio_pin_enum p,gpio_dir_enum d,uint8_t v,uint32_t m){(void)p;(void)d;(void)v;(void)m;}
uint8_t gpio_get_level(gpio_pin_enum p){return keys[p];}
void gpio_set_level(gpio_pin_enum p,uint8_t v){(void)p;(void)v;}
void pwm_init(pwm_channel_enum p,uint32_t f,uint32_t d){(void)f;duties[p]=d;}
void pwm_set_duty(pwm_channel_enum p,uint32_t d){duties[p]=d;}
uint32_t __get_PRIMASK(void){return mask;}
void __disable_irq(void){mask=1;}
void __set_PRIMASK(uint32_t m){mask=m;}
void encoder_feedback_get_snapshot(encoder_feedback_snapshot_t *s){*s=feedback;}
void encoder_feedback_reset_totals(void){}
static void step(unsigned n,int camera,int service)
{
    while(n--){
        track_vision_result_t vision;
        memset(&vision,0,sizeof vision);vision.valid=1;vision.confidence=95;
        vision.near_error_px=15;vision.far_error_px=20;
        now+=10;++feedback.sample_sequence;
        feedback.left.speed_mm_s=AUTO_LEFT_ENCODER_SIGN*100;feedback.right.speed_mm_s=AUTO_RIGHT_ENCODER_SIGN*120;
        feedback.left.delta_counts=AUTO_LEFT_ENCODER_SIGN*22;feedback.right.delta_counts=AUTO_RIGHT_ENCODER_SIGN*26;
        if(camera)autonomous_control_publish_vision(&vision,++frame_sequence,now);
        autonomous_control_tick_10ms(now);motor_control_tick_10ms();
        if(service)autonomous_control_service(now);
        CHECK(duties[PWM2_MODULE0_CHA_C6]<=AUTO_MAX_DUTY_PERMILLE*PWM_DUTY_MAX/1000U &&
              duties[PWM2_MODULE1_CHA_C8]<=AUTO_MAX_DUTY_PERMILLE*PWM_DUTY_MAX/1000U);
    }
}
int main(void)
{
    autonomous_drive_status_t s;
    /* 同步建链时按启动只能拒绝；解除门控并重新按下后正常起跑。 */
    memset(keys,1,sizeof keys);motor_control_init();servo_control_init(0);autonomous_control_init();
    motor_control_enable_test();step(10,1,1);
    autonomous_control_set_network_busy(1);keys[C15]=0;step(4,1,1);
    autonomous_control_get_status(&s);CHECK(!s.active && s.fault==AUTO_FAULT_NOT_READY);
    autonomous_control_set_network_busy(0);keys[C15]=1;step(4,1,1);
    keys[C15]=0;step(4,1,1);keys[C15]=1;step(60,1,1);
    CHECK(autonomous_control_is_active());
    keys[C14]=0;step(1,1,1);keys[C14]=1;step(4,1,1);
    memset(keys,1,sizeof keys);motor_control_init();servo_control_init(0);autonomous_control_init();
    keys[C15]=0;step(8,1,1);CHECK(!autonomous_control_is_active());
    motor_control_enable_test();step(10,1,1);CHECK(!autonomous_control_is_active());
    keys[C15]=1;step(5,1,1);keys[C15]=0;step(4,1,1);keys[C15]=1;
    step(85,1,1);autonomous_control_get_status(&s);CHECK(s.state==AUTO_DRIVE_RUNNING);
    CHECK(duties[PWM2_MODULE1_CHA_C8]>0 && duties[PWM2_MODULE0_CHA_C6]>0);
    CHECK(servo_control_get_status()->current_pulse_us<1480);
    /* 摘要经真实mailbox传递，可靠普通外侧Q40允许运行且使用配置速度。 */
    {
        unsigned n;track_vision_result_t v;
        memset(&v,0,sizeof v);v.valid=1;v.confidence=40;v.element=TRACK_ELEMENT_CURVE;
        v.near_path_supported=1;v.curve_outer_side=TRACK_SIDE_LEFT;v.curve_outer_supported=1;
        v.curve_outer_near_rows=17;v.curve_outer_far_rows=13;v.near_error_px=20;v.far_error_px=40;
        /* 提速后从起步目标到外侧目标仍须经过真实加速斜坡。 */
        for(n=0;n<(unsigned)(AUTO_CURVE_OUTER_SPEED_MM_S*1.5f/(AUTO_ACCEL_MM_S2*0.01f))+10U;++n) {
            now+=10;++feedback.sample_sequence;
            autonomous_control_publish_vision(&v,++frame_sequence,now);
            autonomous_control_tick_10ms(now);motor_control_tick_10ms();autonomous_control_service(now);
        }
        autonomous_control_get_status(&s);
        CHECK(s.active && s.minimum_confidence==AUTO_CURVE_OUTER_MIN_CONFIDENCE);
        CHECK(s.curve_outer_near_rows==17 && s.curve_outer_far_rows==13 && s.curve_outer_supported);
        CHECK(s.target_speed_mm_s==(AUTO_CURVE_OUTER_SPEED_MM_S<AUTO_ELEMENT_SPEED_MM_S?
              AUTO_CURVE_OUTER_SPEED_MM_S:AUTO_ELEMENT_SPEED_MM_S) && s.steering_permille<0);
    }
    motor_control_remote_set_link(0);step(3,1,1);CHECK(autonomous_control_is_active());
    step(20,0,0);autonomous_control_get_status(&s);CHECK(!s.active);
    CHECK(duties[PWM2_MODULE0_CHA_C6]==0 && duties[PWM2_MODULE1_CHA_C8]==0);
    step(30,1,1);CHECK(!autonomous_control_is_active());
    keys[C15]=0;step(4,1,1);keys[C15]=1;step(70,1,1);CHECK(autonomous_control_is_active());
    {
        uint8_t gray[188*120];track_vision_result_t v;unsigned y,x,k;
        track_vision_reset();
        for(k=0;k<6;++k){
            memset(gray,20,sizeof gray);
            for(y=0;y<120;++y)for(x=93-(6+y)/2;x<=93+(6+y)/2;++x)gray[y*188+x]=220;
            for(y=k<3?48:60;y<=(k<3?93:112);++y)memset(gray+y*188,220,188);
            now+=10;++feedback.sample_sequence;
            track_vision_process(gray,now,k<3?50:350,&v);
            autonomous_control_publish_vision(&v,++frame_sequence,now);
            autonomous_control_tick_10ms(now);motor_control_tick_10ms();autonomous_control_service(now);
        }
        CHECK(v.valid && v.confidence>=AUTO_ELEMENT_MIN_CONFIDENCE+15 && v.element==TRACK_ELEMENT_CROSS);
        CHECK(autonomous_control_is_active());
    }
    {
        track_vision_result_t v;memset(&v,0,sizeof v);
        v.valid=1;v.confidence=95;v.near_path_supported=1;
        v.near_error_px=v.far_error_px=-80;
        v.geometry.valid=1;v.geometry.target_right_mm=50;v.geometry.target_forward_mm=400;
        now+=10;++feedback.sample_sequence;autonomous_control_publish_vision(&v,++frame_sequence,now);
        autonomous_control_tick_10ms(now);autonomous_control_service(now);autonomous_control_get_status(&s);
        CHECK(s.active && s.requested_steering_permille<=-190 && s.requested_steering_permille>=-194);
    }
    {
        autonomous_control_trace_t evidence;autonomous_drive_status_t before;
        autonomous_control_get_status(&before);
        feedback.left.speed_mm_s=AUTO_LEFT_ENCODER_SIGN*(AUTO_MAX_MEASURED_SPEED_MM_S+1);
        feedback.left.delta_counts=AUTO_LEFT_ENCODER_SIGN*289;++feedback.sample_sequence;now+=10;
        autonomous_control_tick_10ms(now);autonomous_control_get_trace(&evidence);autonomous_control_get_status(&s);
        CHECK(evidence.frozen && evidence.fault==AUTO_FAULT_OVERSPEED);
        CHECK(evidence.fault_frame.left_speed_mm_s==AUTO_MAX_MEASURED_SPEED_MM_S+1);
        CHECK(evidence.fault_frame.raw_left_delta_counts==AUTO_LEFT_ENCODER_SIGN*289);
        CHECK(evidence.fault_frame.motion.left_duty_permille==before.left_duty_permille);
        CHECK(evidence.fault_frame.motion.left_target_mm_s==before.left_target_mm_s);
        CHECK(s.left_duty_permille==0 && s.right_duty_permille==0);
    }
    keys[C14]=0;step(1,1,1);CHECK(!autonomous_control_is_active());
    CHECK(duties[PWM2_MODULE0_CHA_C6]==0 && duties[PWM2_MODULE1_CHA_C8]==0);
    CHECK(!servo_control_get_status()->output_enabled);
    mask=1;autonomous_control_get_status(&s);CHECK(mask==1);
    printf("%u autonomous integration checks passed\n",checks);return 0;
}
