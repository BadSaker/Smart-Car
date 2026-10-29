#ifndef PROBE_PLATFORM_H
#define PROBE_PLATFORM_H
#include <stdint.h>
typedef enum { C14=46,C15=47,C30=62 } gpio_pin_enum;
typedef enum { GPI=0,GPO=1 } gpio_dir_enum;
typedef enum { PWM4_MODULE2_CHA_C30=185 } pwm_channel_enum;
#define GPIO_LOW 0
#define GPIO_HIGH 1
#define GPI_PULL_UP 22
#define GPO_PUSH_PULL 33
#define PWM_DUTY_MAX 10000
void gpio_init(gpio_pin_enum,gpio_dir_enum,uint8_t,uint32_t);
uint8_t gpio_get_level(gpio_pin_enum);
void pwm_init(pwm_channel_enum,uint32_t,uint32_t);
void pwm_set_duty(pwm_channel_enum,uint32_t);
#endif
