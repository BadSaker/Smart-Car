#include <assert.h>
#include <stdio.h>
#include "zf_driver_gpio.h"
#include "zf_driver_pwm.h"
#include "motor_control.h"
static uint8_t c12=1,c14=1;
static uint32_t mask,duty1,duty2;
void gpio_init(gpio_pin_enum p,gpio_dir_enum d,uint8_t v,uint32_t c){(void)p;(void)d;(void)v;(void)c;}
uint8_t gpio_get_level(gpio_pin_enum p){return p==C12?c12:c14;}
void gpio_set_level(gpio_pin_enum p,uint8_t v){(void)p;(void)v;}
void pwm_init(pwm_channel_enum p,uint32_t f,uint32_t d){(void)p;(void)f;assert(d==0);}
void pwm_set_duty(pwm_channel_enum p,uint32_t d){if(p==PWM2_MODULE1_CHA_C8)duty1=d;else duty2=d;}
uint32_t __get_PRIMASK(void){return mask;}
void __disable_irq(void){mask=1;}
void __set_PRIMASK(uint32_t v){mask=v;}
void encoder_feedback_reset_totals(void){}
static void ticks(unsigned n){while(n--)motor_control_tick_10ms();}
int main(void){
    motor_control_status_t s;
    motor_control_init();motor_control_enable_test();motor_control_remote_set_link(1);
    ticks(5);c12=0;ticks(5);motor_control_submit_remote(0,1);ticks(1);
    motor_control_submit_remote(100,0);ticks(1);
    motor_control_get_status(&s);assert(!s.remote_armed && !duty1 && !duty2);
    c12=1;motor_control_autonomous_set(1,321,123);ticks(1);
    assert(duty1==3210 && duty2==1230);
    c12=0;ticks(20);assert(duty1==3210 && duty2==1230);
    c14=0;ticks(1);assert(!duty1 && !duty2);
    assert(motor_control_take_servo_stop());c14=1;ticks(5);assert(!duty1 && !duty2);
    motor_control_autonomous_set(0,0,0);ticks(1);
    motor_control_autonomous_set(1,60,70);ticks(1);assert(duty1==600 && duty2==700);
    puts("PASS C12 has no drive/cancel effect; C14 latched stop retained");return 0;
}
