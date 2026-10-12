#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "zf_driver_gpio.h"
#include "zf_driver_pwm.h"
#include "servo_config.h"
#include "servo_control.h"
#include "autonomous_config.h"

static uint8_t start_key;
static uint8_t stop_key;
static uint8_t signal_is_low;
static uint32_t commanded_duty;
static unsigned pwm_starts;
static unsigned pwm_updates;
static unsigned signal_disables;
static unsigned checks;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, message); \
        exit(1); \
    } \
} while (0)

/* 仅替换无法在主机运行的GPIO/PWM驱动；控制与状态机使用真实模块。 */
void gpio_init(gpio_pin_enum pin, gpio_dir_enum direction, uint8_t level, uint32_t mode)
{
    if (pin == C30)
    {
        CHECK(direction == GPO && level == GPIO_LOW && mode == GPO_PUSH_PULL,
              "disabled servo signal must actively drive low");
        signal_is_low = 1U;
        ++signal_disables;
    }
    else
    {
        CHECK((pin == C14 || pin == C15) && direction == GPI && mode == GPI_PULL_UP,
              "physical keys retain input pull-ups");
    }
}

uint8_t gpio_get_level(gpio_pin_enum pin)
{
    CHECK(pin == C14 || pin == C15, "only S2/S3 are sampled");
    return pin == C15 ? start_key : stop_key;
}

void pwm_init(pwm_channel_enum channel, uint32_t frequency, uint32_t duty)
{
    CHECK(channel == PWM4_MODULE2_CHA_C30 && frequency == 50U,
          "remote output uses connector 1 at 50Hz");
    CHECK(duty == PWM_DUTY_MAX * 74U / 1000U,
          "every new remote session starts at calibrated 1480us center");
    signal_is_low = 0U;
    commanded_duty = duty;
    ++pwm_starts;
}

void pwm_set_duty(pwm_channel_enum channel, uint32_t duty)
{
    CHECK(channel == PWM4_MODULE2_CHA_C30, "updates use connector 1");
    CHECK(duty == 0U || (duty >= (uint64_t)PWM_DUTY_MAX * SERVO_OUTPUT_MIN_PULSE_US / 20000U &&
                        duty <= ((uint64_t)PWM_DUTY_MAX * SERVO_OUTPUT_MAX_PULSE_US + 19999U) / 20000U),
          "hardware commands stay inside configured output window");
    commanded_duty = duty;
    ++pwm_updates;
}

static const servo_control_status_t *status(void)
{
    return servo_control_get_status();
}

static void reset(uint32_t now_ms)
{
    start_key = GPIO_HIGH;
    stop_key = GPIO_HIGH;
    signal_is_low = 0U;
    commanded_duty = 0U;
    pwm_starts = 0U;
    pwm_updates = 0U;
    signal_disables = 0U;
    servo_control_init(now_ms);
    CHECK(signal_is_low && !status()->output_enabled && pwm_starts == 0U,
          "initialization never starts remote output");
}

int main(void)
{
    unsigned n;
    reset(0);servo_control_autonomous_apply(1,1000,0);
    CHECK(status()->current_pulse_us==1480,"auto starts centered");
    for(n=1;n<=100;++n)servo_control_autonomous_apply(1,1000,n*AUTO_SERVO_INTERVAL_MS);
    CHECK(status()->current_pulse_us==SERVO_REFERENCE_CENTER_US+SERVO_AUTONOMOUS_EXCURSION_US,"auto reaches configured left endpoint");
    servo_control_remote_apply(0,0,2020);
    CHECK(status()->output_enabled,"remote disable cannot release auto owner");
    servo_control_remote_apply(1,-1000,2040);
    CHECK(status()->target_pulse_us==SERVO_REFERENCE_CENTER_US+SERVO_AUTONOMOUS_EXCURSION_US,"remote target cannot overwrite auto owner");
    servo_control_autonomous_apply(1,-1000,10000);
    CHECK(status()->current_pulse_us==SERVO_REFERENCE_CENTER_US+SERVO_AUTONOMOUS_EXCURSION_US-AUTO_SERVO_STEP_US,"late auto service cannot catch up with a jump");
    for(n=1;n<=100;++n)servo_control_autonomous_apply(1,-1000,10000+n*AUTO_SERVO_INTERVAL_MS);
    CHECK(status()->current_pulse_us==SERVO_REFERENCE_CENTER_US-SERVO_AUTONOMOUS_EXCURSION_US,"auto reaches configured right endpoint");
    stop_key=GPIO_LOW;servo_control_autonomous_apply(1,1000,12020);
    CHECK(!status()->output_enabled,"S3 overrides auto steering");
    stop_key=GPIO_HIGH;servo_control_autonomous_apply(0,0,12040);
    CHECK(!status()->output_enabled,"disabled auto stays stopped");
    printf("%u autonomous servo checks passed\n",checks);return 0;
}
