#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "autonomous_control.h"
#include "motor_control.h"
#include "servo_control.h"
#include "encoder_feedback.h"
static uint8_t keys[32];static uint32_t duties[3],mask,now,frame_sequence;
static encoder_feedback_snapshot_t feedback;
static unsigned checks;
#define CHECK(c) do {++checks;if(!(c)){printf("line %d\n",__LINE__);fflush(stdout);assert(c);}}while(0)
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
        feedback.left.speed_mm_s=100;feedback.right.speed_mm_s=120;
        feedback.left.delta_counts=22;feedback.right.delta_counts=26;
        if(camera)autonomous_control_publish_vision(&vision,++frame_sequence,now);
        autonomous_control_tick_10ms(now);motor_control_tick_10ms();
        if(service)autonomous_control_service(now);
        CHECK(duties[PWM2_MODULE0_CHA_C6]<=1000 && duties[PWM2_MODULE1_CHA_C8]<=1000);
    }
}
int main(void)
{
    autonomous_drive_status_t s;
    memset(keys,1,sizeof keys);motor_control_init();servo_control_init(0);autonomous_control_init();
    keys[C15]=0;step(8,1,1);CHECK(!autonomous_control_is_active());
    motor_control_enable_test();step(10,1,1);CHECK(!autonomous_control_is_active());
    keys[C15]=1;step(5,1,1);keys[C15]=0;step(4,1,1);keys[C15]=1;
    step(85,1,1);autonomous_control_get_status(&s);CHECK(s.state==AUTO_DRIVE_RUNNING);
    CHECK(duties[PWM2_MODULE1_CHA_C8]>0 && duties[PWM2_MODULE0_CHA_C6]>0);
    CHECK(servo_control_get_status()->current_pulse_us<1480);
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
        CHECK(v.valid && v.confidence==39 && v.element==TRACK_ELEMENT_CROSS);
        CHECK(autonomous_control_is_active());
    }
    keys[C14]=0;step(1,1,1);CHECK(!autonomous_control_is_active());
    CHECK(duties[PWM2_MODULE0_CHA_C6]==0 && duties[PWM2_MODULE1_CHA_C8]==0);
    CHECK(!servo_control_get_status()->output_enabled);
    mask=1;autonomous_control_get_status(&s);CHECK(mask==1);
    printf("%u autonomous integration checks passed\n",checks);return 0;
}
