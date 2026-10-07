#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "zf_driver_gpio.h"
#include "zf_driver_pwm.h"
#include "servo_config.h"
#include "servo_control.h"

static uint8_t start_key = GPIO_HIGH;
static uint8_t stop_key = GPIO_HIGH;
static uint8_t signal_is_gpio_low;
static uint32_t commanded_duty;
static unsigned pwm_starts;
static unsigned pwm_updates;
static unsigned key_initializations;
static unsigned checks;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, message); \
        exit(1); \
    } \
} while (0)

/* 桩只替换硬件IO：记录真实模块发出的配置与PWM命令。 */
void gpio_init(gpio_pin_enum pin, gpio_dir_enum direction, uint8_t level, uint32_t mode)
{
    if (pin == C30)
    {
        CHECK(direction == GPO && level == GPIO_LOW && mode == GPO_PUSH_PULL,
              "servo disabled pin must actively drive low");
        signal_is_gpio_low = 1U;
    }
    else
    {
        CHECK((pin == C15 || pin == C14) && direction == GPI && mode == GPI_PULL_UP,
              "board S2/S3 must use input pull-ups");
        ++key_initializations;
    }
}

uint8_t gpio_get_level(gpio_pin_enum pin)
{
    CHECK(pin == C15 || pin == C14, "only board S2/S3 may be sampled");
    return pin == C15 ? start_key : stop_key;
}

void pwm_init(pwm_channel_enum channel, uint32_t frequency, uint32_t duty)
{
    CHECK(channel == PWM4_MODULE2_CHA_C30 && frequency == 50U,
          "start must configure servo connector 1 at 50 Hz");
    CHECK(duty <= PWM_DUTY_MAX, "initial duty must fit library scale");
    signal_is_gpio_low = 0U;
    commanded_duty = duty;
    ++pwm_starts;
}

void pwm_set_duty(pwm_channel_enum channel, uint32_t duty)
{
    CHECK(channel == PWM4_MODULE2_CHA_C30 && duty <= PWM_DUTY_MAX,
          "duty update must target connector 1 within library scale");
    commanded_duty = duty;
    ++pwm_updates;
}

static const servo_control_status_t *status(void)
{
    return servo_control_get_status();
}

static void reset(uint32_t now_ms, uint8_t boot_start, uint8_t boot_stop)
{
    start_key = boot_start;
    stop_key = boot_stop;
    signal_is_gpio_low = 0U;
    commanded_duty = 0U;
    pwm_starts = 0U;
    pwm_updates = 0U;
    key_initializations = 0U;
    servo_control_init(now_ms);
    CHECK(signal_is_gpio_low && !status()->output_enabled && commanded_duty == 0U,
          "boot must keep signal low with no valid PWM");
    CHECK(key_initializations == 2U, "both physical keys must be initialized");
}

/* 手动按键行为辅助：先确认稳定释放，再按下30ms，绝不直接绕过poll。 */
static uint32_t press_after_release(uint32_t now_ms)
{
    start_key = GPIO_HIGH;
    servo_control_poll(now_ms);
    servo_control_poll(now_ms + 30U);
    start_key = GPIO_LOW;
    servo_control_poll(now_ms + 31U);
    servo_control_poll(now_ms + 61U);
    return now_ms + 61U;
}

static void test_supply_and_no_automatic_start(void)
{
    reset(0U, GPIO_HIGH, GPIO_HIGH);
    servo_control_poll(5000U);
    CHECK(pwm_starts == 0U && !status()->output_enabled,
          "time passing alone must never start output");
#if SERVO_SUPPLY_MV < 6000 || SERVO_SUPPLY_MV > 7400
    CHECK(status()->state == SERVO_CONTROL_SUPPLY_ERROR, "invalid supply must be visible");
    CHECK(servo_control_start_test(5001U) == SERVO_CONTROL_START_SUPPLY_ERROR,
          "public API must reject configured invalid supply");
    (void)press_after_release(5100U);
    CHECK(status()->state == SERVO_CONTROL_SUPPLY_ERROR && pwm_starts == 0U && signal_is_gpio_low,
          "manual press cannot bypass supply gate");
#else
    CHECK(status()->state == SERVO_CONTROL_IDLE, "valid supply begins idle");
    CHECK(servo_control_start_test(5001U) == SERVO_CONTROL_START_NOT_ARMED,
          "public API must not bypass a fresh debounced physical press");
    (void)press_after_release(5100U);
    CHECK(status()->state == SERVO_CONTROL_CENTER && status()->output_enabled,
          "fresh debounced press starts center hold");
#if PWM_DUTY_MAX == 20000
    CHECK(commanded_duty == 1480U, "1480 us must use the configured 20000 duty scale");
#else
    CHECK(commanded_duty == 740U, "1480 us at 50 Hz must produce 740/10000 duty");
#endif
#endif
}

#if SERVO_SUPPLY_MV >= 6000 && SERVO_SUPPLY_MV <= 7400
static void test_boot_held_and_debounce(void)
{
    reset(0U, GPIO_LOW, GPIO_HIGH);
    servo_control_poll(30U);
    servo_control_poll(500U);
    CHECK(pwm_starts == 0U, "start held during boot must not activate PWM");
    start_key = GPIO_HIGH;
    servo_control_poll(510U);
    start_key = GPIO_LOW;
    servo_control_poll(520U);
    servo_control_poll(550U);
    CHECK(pwm_starts == 0U, "release bounce must not arm");
    start_key = GPIO_HIGH;
    servo_control_poll(600U);
    servo_control_poll(630U);
    start_key = GPIO_LOW;
    servo_control_poll(640U);
    servo_control_poll(669U);
    CHECK(pwm_starts == 0U, "short start press must not activate PWM");
    start_key = GPIO_HIGH;
    servo_control_poll(670U);
    start_key = GPIO_LOW;
    servo_control_poll(675U);
    servo_control_poll(704U);
    CHECK(pwm_starts == 0U, "start bounce must reset debounce time");
    servo_control_poll(705U);
    CHECK(pwm_starts == 1U, "stable new start press activates once");
    servo_control_poll(900U);
    CHECK(pwm_starts == 1U, "holding start must not retrigger");
}

static void test_stop_priority_and_rearm(void)
{
    reset(0U, GPIO_HIGH, GPIO_HIGH);
    servo_control_poll(30U);
    start_key = GPIO_LOW;
    servo_control_poll(31U);
    stop_key = GPIO_LOW;
    servo_control_poll(61U);
    CHECK(pwm_starts == 0U && signal_is_gpio_low && status()->state == SERVO_CONTROL_STOPPED,
          "simultaneous stop must win over debounced start");
    CHECK(servo_control_start_test(62U) == SERVO_CONTROL_START_STOP_HELD,
          "public start cannot override a held stop");
    stop_key = GPIO_HIGH;
    servo_control_poll(70U);
    servo_control_poll(200U);
    CHECK(pwm_starts == 0U, "release stop while start held must not restart");
    (void)press_after_release(300U);
    CHECK(pwm_starts == 1U && status()->output_enabled, "fresh release/press rearms after stop");
    stop_key = GPIO_LOW;
    servo_control_poll(362U);
    CHECK(signal_is_gpio_low && commanded_duty == 0U && !status()->output_enabled,
          "one stop sample must remove signal without debounce or PWM reload delay");
    CHECK(status()->current_pulse_us == 0U && status()->target_pulse_us == 0U,
          "stopped snapshot must not imply PWM is active");
    stop_key = GPIO_HIGH;
    servo_control_poll(363U);
    servo_control_poll(1000U);
    CHECK(pwm_starts == 1U, "stop bounce and held start must not restart");
}

static void test_stop_api_consumes_pending_press(void)
{
    reset(0U, GPIO_HIGH, GPIO_HIGH);
    servo_control_poll(30U);
    start_key = GPIO_LOW;
    servo_control_poll(31U);
    servo_control_stop();
    servo_control_poll(61U);
    CHECK(pwm_starts == 0U && signal_is_gpio_low,
          "explicit stop must cancel a press awaiting debounce");
    (void)press_after_release(100U);
    CHECK(pwm_starts == 1U, "release and new press can restart after API stop");
    servo_control_stop();
    CHECK(commanded_duty == 0U && signal_is_gpio_low && status()->state == SERVO_CONTROL_STOPPED,
          "stop API must remove an active signal");
}

static void test_no_backlog_jump(void)
{
    uint32_t start_ms;
    unsigned writes;
    reset(0U, GPIO_HIGH, GPIO_HIGH);
    start_ms = press_after_release(0U);
    servo_control_poll(start_ms + 499U);
    CHECK(status()->state == SERVO_CONTROL_CENTER && status()->current_pulse_us == 1480U,
          "center hold must last at least 500ms");
    servo_control_poll(start_ms + 500U);
    CHECK(status()->state == SERVO_CONTROL_TO_LOW && status()->target_pulse_us == 1420U,
          "first motion target is lower pulse, not an assumed direction");
    writes = pwm_updates;
    servo_control_poll(start_ms + 519U);
    CHECK(pwm_updates == writes, "less than 20ms must not issue a step");
    servo_control_poll(start_ms + 10000U);
    CHECK(status()->current_pulse_us == 1480U - SERVO_TEST_STEP_US,
          "late poll may issue only one bounded step");
    writes = pwm_updates;
    servo_control_poll(start_ms + 10000U);
    servo_control_poll(start_ms + 10019U);
    CHECK(pwm_updates == writes, "late poll resets interval without backlog catch-up");
}

static void test_one_trip_clamps_and_holds_center(void)
{
    uint32_t now_ms;
    uint16_t previous;
    unsigned reached_low = 0U;
    unsigned reached_high = 0U;
    unsigned remaining = 1000U;
    reset(0U, GPIO_HIGH, GPIO_HIGH);
    now_ms = press_after_release(0U);
    previous = status()->current_pulse_us;
    while (status()->state != SERVO_CONTROL_COMPLETE && remaining-- > 0U)
    {
        uint16_t current;
        uint16_t difference;
        now_ms += 20U;
        servo_control_poll(now_ms);
        current = status()->current_pulse_us;
        difference = current > previous ? current - previous : previous - current;
        CHECK(current >= 1420U && current <= 1540U, "every pulse must stay inside small reference range");
        CHECK(difference <= SERVO_TEST_STEP_US, "every movement must respect pulse slew bound");
        if (current == 1420U) reached_low = 1U;
        if (current == 1540U) reached_high = 1U;
        previous = current;
    }
    CHECK(status()->state == SERVO_CONTROL_COMPLETE && reached_low && reached_high,
          "one test must reach both clamped endpoints then finish");
    CHECK(status()->output_enabled && status()->current_pulse_us == 1480U &&
          status()->target_pulse_us == 1480U, "completed trip holds reference center");
    servo_control_poll(now_ms + 100000U);
    CHECK(pwm_starts == 1U && status()->state == SERVO_CONTROL_COMPLETE,
          "held start must never automatically repeat the finished trip");
    (void)press_after_release(now_ms + 100100U);
    CHECK(pwm_starts == 2U && status()->state == SERVO_CONTROL_CENTER,
          "only a new released/debounced press starts another trip");
}

static void test_busy_press_is_not_queued(void)
{
    uint32_t now_ms;
    unsigned remaining = 1000U;
    reset(0U, GPIO_HIGH, GPIO_HIGH);
    now_ms = press_after_release(0U);
    now_ms = press_after_release(now_ms + 10U);
    CHECK(pwm_starts == 1U, "press during a running trip must not restart it");
    while (status()->state != SERVO_CONTROL_COMPLETE && remaining-- > 0U)
    {
        now_ms += 20U;
        servo_control_poll(now_ms);
    }
    servo_control_poll(now_ms + 10000U);
    CHECK(status()->state == SERVO_CONTROL_COMPLETE && pwm_starts == 1U,
          "busy press must not queue an automatic second trip");
}

static void test_endpoint_holds(void)
{
    uint32_t now_ms;
    unsigned remaining = 1000U;
    unsigned writes;
    reset(0U, GPIO_HIGH, GPIO_HIGH);
    now_ms = press_after_release(0U);
    while (status()->state != SERVO_CONTROL_HOLD_LOW && remaining-- > 0U)
    {
        now_ms += 20U;
        servo_control_poll(now_ms);
    }
    CHECK(status()->state == SERVO_CONTROL_HOLD_LOW, "lower endpoint must enter an observable hold");
    writes = pwm_updates;
    servo_control_poll(now_ms + 299U);
    CHECK(status()->state == SERVO_CONTROL_HOLD_LOW && status()->current_pulse_us == 1420U &&
          pwm_updates == writes, "lower endpoint must hold for 300ms without extra movement");
    servo_control_poll(now_ms + 300U);
    CHECK(status()->state == SERVO_CONTROL_TO_HIGH && status()->target_pulse_us == 1540U,
          "lower hold transitions to upper pulse target");
    now_ms += 300U;
    while (status()->state != SERVO_CONTROL_HOLD_HIGH && remaining-- > 0U)
    {
        now_ms += 20U;
        servo_control_poll(now_ms);
    }
    CHECK(status()->state == SERVO_CONTROL_HOLD_HIGH, "upper endpoint must enter an observable hold");
    writes = pwm_updates;
    servo_control_poll(now_ms + 299U);
    CHECK(status()->state == SERVO_CONTROL_HOLD_HIGH && status()->current_pulse_us == 1540U &&
          pwm_updates == writes, "upper endpoint must hold for 300ms without extra movement");
    servo_control_poll(now_ms + 300U);
    CHECK(status()->state == SERVO_CONTROL_RETURN_CENTER && status()->target_pulse_us == 1480U,
          "upper hold transitions back to reference center");
}

static void test_clock_wrap(void)
{
    uint32_t now_ms = UINT32_MAX - 20U;
    uint32_t start_ms;
    reset(now_ms, GPIO_HIGH, GPIO_HIGH);
    start_ms = press_after_release(now_ms);
    CHECK(pwm_starts == 1U, "release/press debounce must work across uint32 wrap");
    servo_control_poll(start_ms + 500U);
    servo_control_poll(start_ms + 520U);
    CHECK(status()->current_pulse_us == 1480U - SERVO_TEST_STEP_US,
          "motion scheduling must continue after clock wrap");

    now_ms = UINT32_MAX - 600U;
    reset(now_ms, GPIO_HIGH, GPIO_HIGH);
    start_ms = press_after_release(now_ms);
    servo_control_poll(start_ms + 500U);
    servo_control_poll(start_ms + 520U);
    servo_control_poll(start_ms + 540U);
    CHECK(status()->current_pulse_us == 1480U - 2U * SERVO_TEST_STEP_US,
          "20ms motion interval must cross uint32 wrap normally");
}
#endif

int main(void)
{
    test_supply_and_no_automatic_start();
#if SERVO_SUPPLY_MV >= 6000 && SERVO_SUPPLY_MV <= 7400
    test_boot_held_and_debounce();
    test_stop_priority_and_rearm();
    test_stop_api_consumes_pending_press();
    test_no_backlog_jump();
    test_one_trip_clamps_and_holds_center();
    test_busy_press_is_not_queued();
    test_endpoint_holds();
    test_clock_wrap();
#endif
    printf("PASS: %u checks\n", checks);
    return 0;
}
