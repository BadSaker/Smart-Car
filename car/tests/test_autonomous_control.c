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
static unsigned checks,servo_writes;
#define CHECK(c) do {++checks;if(!(c)){printf("line %d\n",__LINE__);fflush(stdout);exit(1);}}while(0)
void gpio_init(gpio_pin_enum p,gpio_dir_enum d,uint8_t v,uint32_t m){(void)p;(void)d;(void)v;(void)m;}
uint8_t gpio_get_level(gpio_pin_enum p){return keys[p];}
void gpio_set_level(gpio_pin_enum p,uint8_t v){(void)p;(void)v;}
void pwm_init(pwm_channel_enum p,uint32_t f,uint32_t d){(void)f;duties[p]=d;}
void pwm_set_duty(pwm_channel_enum p,uint32_t d){duties[p]=d;if(p==PWM4_MODULE2_CHA_C30)++servo_writes;}
uint32_t __get_PRIMASK(void){return mask;}
void __disable_irq(void){mask=1;}
void __set_PRIMASK(uint32_t m){mask=m;}
void encoder_feedback_get_snapshot(encoder_feedback_snapshot_t *s){*s=feedback;}
void encoder_feedback_reset_totals(void){}
static void step(unsigned n,int camera,int service)
{
    while(n--){
        track_vision_result_t vision;
        memset(&vision,0,sizeof vision);vision.has_path=1;
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
static void publish(track_vision_result_t *v)
{
    now+=10;++feedback.sample_sequence;
    autonomous_control_publish_vision(v,++frame_sequence,now);
    autonomous_control_tick_10ms(now);motor_control_tick_10ms();autonomous_control_service(now);
}
int main(void)
{
    autonomous_drive_status_t s;autonomous_control_trace_t t;
    track_vision_result_t v;memset(&v,0,sizeof v);v.has_path=1;
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
    step(200,1,1);autonomous_control_get_status(&s);CHECK(s.state==AUTO_DRIVE_RUNNING);
    CHECK(duties[PWM2_MODULE1_CHA_C8]>0 && duties[PWM2_MODULE0_CHA_C6]>0);
    CHECK(servo_control_get_status()->current_pulse_us<1480);
    for(unsigned e=0;e<=TRACK_ELEMENT_ROUNDABOUT_EXIT;++e){
        v.element=(track_element_t)e;v.near_error_px=v.far_error_px=10;for(unsigned n=0;n<40;++n)publish(&v);
        autonomous_control_get_status(&s);CHECK(s.active && s.target_speed_mm_s==AUTO_CRUISE_SPEED_MM_S && s.steering_permille==-(int)(10*AUTO_STEERING_KP));
    }
    v.has_path=0;publish(&v);autonomous_control_get_status(&s);
    CHECK(s.active && !s.has_path && s.target_speed_mm_s==AUTO_CRUISE_SPEED_MM_S && s.steering_permille==-(int)(10*AUTO_STEERING_KP));
    motor_control_remote_set_link(0);step(3,1,1);CHECK(autonomous_control_is_active());
    for(unsigned k=10;k<=100;k*=10){
        uint32_t stamp=now,seq=frame_sequence;step(k,0,0);
        autonomous_control_get_status(&s);autonomous_control_get_trace(&t);
        CHECK(s.active && s.fault==AUTO_FAULT_NONE && !t.frozen);
        CHECK(t.latest.frame_ms==stamp && t.latest.sequence==seq);
        step(1,1,1);
    }
    v.has_path=1;v.near_error_px=v.far_error_px=-80;publish(&v);
    CHECK(servo_control_get_status()->current_pulse_us==servo_control_get_status()->target_pulse_us);
    CHECK(servo_control_get_status()->current_pulse_us>1480);
    for(unsigned k=0;k<2;++k){
        uint32_t seq=k?0:UINT32_MAX;
        autonomous_control_publish_vision(&v,seq,now);step(1,0,1);
        autonomous_control_get_status(&s);CHECK(s.active && s.steering_permille==1000);
    }
    v.geometry.valid=1;v.geometry.target_right_mm=50;v.geometry.target_forward_mm=400;
    v.near_error_px=v.far_error_px=-80;publish(&v);autonomous_control_get_status(&s);
    CHECK(s.steering_permille>=-194 && s.steering_permille<=-190);
    CHECK(servo_control_get_status()->current_pulse_us<1480);
    v.geometry.valid=0;
    feedback.left.speed_mm_s=AUTO_LEFT_ENCODER_SIGN*4001;
    feedback.left.delta_counts=AUTO_LEFT_ENCODER_SIGN*289;++feedback.sample_sequence;now+=10;
    autonomous_control_tick_10ms(now);autonomous_control_get_trace(&t);autonomous_control_get_status(&s);
    CHECK(t.frozen && t.fault_frame.motion.fault==AUTO_FAULT_OVERSPEED);
    CHECK(t.fault_frame.motion.left_speed_mm_s==4001);
    CHECK(t.fault_frame.raw_left_delta_counts==AUTO_LEFT_ENCODER_SIGN*289);
    CHECK(!s.active && !s.left_duty_permille && !s.right_duty_permille);
    {autonomous_trace_frame_t saved=t.fault_frame;step(5,1,1);autonomous_control_get_trace(&t);CHECK(!memcmp(&saved,&t.fault_frame,sizeof saved));}
    keys[C14]=0;step(1,1,1);CHECK(!autonomous_control_is_active());
    CHECK(!duties[PWM2_MODULE0_CHA_C6] && !duties[PWM2_MODULE1_CHA_C8]);
    CHECK(!servo_control_get_status()->output_enabled);
    mask=1;autonomous_control_get_status(&s);CHECK(mask==1);mask=0;
    keys[C14]=1;step(5,1,1);keys[C15]=0;step(4,1,1);keys[C15]=1;step(60,1,1);
    v.zebra=1;v.zebra_near_row=60;publish(&v);autonomous_control_get_status(&s);autonomous_control_get_trace(&t);
    CHECK(s.active && s.marker_phase==1 && !s.start_marker_seen && s.target_speed_mm_s<=AUTO_FINISH_SPEED_MM_S);
    CHECK(!t.frozen && t.latest.motion.zebra_visible);
    keys[C14]=0;step(250,1,1);autonomous_control_get_status(&s);
    CHECK(!s.active && s.state==AUTO_DRIVE_STOPPED);
    printf("%u autonomous integration checks passed\n",checks);return 0;
}
