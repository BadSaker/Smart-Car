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
static unsigned checks,review_event,review_stopped,review_positive_after_stop;
static void review_inject(unsigned event);
#define CHECK(c) do {++checks;if(!(c)){printf("line %d\n",__LINE__);fflush(stdout);assert(c);}}while(0)
void gpio_init(gpio_pin_enum p,gpio_dir_enum d,uint8_t v,uint32_t m){(void)p;(void)d;(void)v;(void)m;}
uint8_t gpio_get_level(gpio_pin_enum p){return keys[p];}
void gpio_set_level(gpio_pin_enum p,uint8_t v){(void)p;(void)v;}
void pwm_init(pwm_channel_enum p,uint32_t f,uint32_t d){(void)f;duties[p]=d;}
void pwm_set_duty(pwm_channel_enum p,uint32_t d){duties[p]=d;if(review_stopped && p==PWM4_MODULE2_CHA_C30 && d)++review_positive_after_stop;}
uint32_t __get_PRIMASK(void){return mask;}
void __disable_irq(void){mask=1;}
void __set_PRIMASK(uint32_t m){mask=m;if(!m && review_event){unsigned event=review_event;review_event=0;review_inject(event);}}
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

/* 在快照恢复中断时运行真实PIT控制入口，仅替换硬件边界。 */
static void review_inject(unsigned event)
{
    now+=10;
    ++feedback.sample_sequence;
    if(event==1)keys[C12]=0;
    autonomous_control_tick_10ms(now);
    motor_control_tick_10ms();
    if(event==1)review_stopped=1;
}
static void review_prepare(void)
{
    memset(keys,1,sizeof keys);
    motor_control_init();servo_control_init(0);autonomous_control_init();
    motor_control_enable_test();step(10,1,1);
}
int main(int argc,char **argv)
{
    autonomous_drive_status_t state;
    uint8_t returned;
    CHECK(argc==2);review_prepare();
    if(strcmp(argv[1],"stop")==0){
        keys[C15]=0;step(4,1,1);keys[C15]=1;step(85,1,1);
        CHECK(autonomous_control_is_active());
        CHECK(servo_control_get_status()->current_pulse_us<1480);
        now+=20;review_event=1;
        returned=autonomous_control_service(now);
        autonomous_control_get_status(&state);
        printf("stop: active=%u service_return=%u servo_enabled=%u pulse=%u positive_writes_after_stop=%u\n",
            state.active,returned,servo_control_get_status()->output_enabled,
            servo_control_get_status()->current_pulse_us,review_positive_after_stop);
        fflush(stdout);
        CHECK(!state.active);
        CHECK(!servo_control_get_status()->output_enabled);
        CHECK(returned==0);
    }else{
        keys[C15]=0;step(3,1,0);
        CHECK(!autonomous_control_is_active());
        review_event=2;
        returned=autonomous_control_service(now);
        autonomous_control_get_status(&state);
        printf("start: active=%u service_return=%u servo_enabled=%u generation=%lu\n",
            state.active,returned,servo_control_get_status()->output_enabled,
            (unsigned long)state.run_generation);
        fflush(stdout);
        CHECK(state.active);
        CHECK(returned==1);
    }
    return 0;
}
