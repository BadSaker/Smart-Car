#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "zf_driver_gpio.h"
#include "zf_driver_pwm.h"
#include "motor_test_config.h"
#include "motor_control.h"

static uint8_t start_key;
static uint8_t stop_key;
static uint32_t duty1;
static uint32_t duty2;
static uint32_t interrupt_mask;
static unsigned direction_initializations;
static unsigned key_initializations;
static unsigned pwm_initializations;
static unsigned encoder_resets;
static unsigned nonzero_commands;
static unsigned mask_saves;
static unsigned mask_disables;
static unsigned mask_restores;
static unsigned checks;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, message); \
        exit(1); \
    } \
} while (0)

/* 桩记录实际GPIO/PWM命令；不另写状态机来代替被测模块。 */
void gpio_init(gpio_pin_enum pin, gpio_dir_enum direction, uint8_t level, uint32_t mode)
{
    CHECK(interrupt_mask == 0U, "peripheral initialization must not hold interrupt mask");
    if (pin == C9 || pin == C7)
    {
        CHECK(direction == GPO && level == GPIO_HIGH && mode == GPO_PUSH_PULL,
              "both module channels use reference high direction without asserting forward");
        ++direction_initializations;
    }
    else if (pin == C12 || pin == C14)
    {
        CHECK(direction == GPI && mode == GPI_PULL_UP, "S5 and S3 require pull-up inputs");
        ++key_initializations;
    }
    else
    {
        CHECK((pin == C8 || pin == C6) && direction == GPO &&
              level == GPIO_LOW && mode == GPO_PUSH_PULL, "PWM pins may only be preloaded low");
    }
}

uint8_t gpio_get_level(gpio_pin_enum pin)
{
    CHECK(pin == C12 || pin == C14, "must not touch servo start or Wi-Fi key");
    return pin == C12 ? start_key : stop_key;
}

void gpio_set_level(gpio_pin_enum pin, uint8_t level)
{
    (void)pin;
    (void)level;
    CHECK(0, "direction must remain unchanged throughout motor test");
}

void pwm_init(pwm_channel_enum channel, uint32_t frequency, uint32_t duty)
{
    CHECK(interrupt_mask == 0U, "PWM initialization must not mask interrupts");
    CHECK((channel == PWM2_MODULE1_CHA_C8 || channel == PWM2_MODULE0_CHA_C6) &&
          frequency == 17000U && duty == 0U, "boot PWM must be 17 kHz with zero command");
    ++pwm_initializations;
    if (channel == PWM2_MODULE1_CHA_C8) duty1 = duty;
    else duty2 = duty;
}

void pwm_set_duty(pwm_channel_enum channel, uint32_t duty)
{
    CHECK(channel == PWM2_MODULE1_CHA_C8 || channel == PWM2_MODULE0_CHA_C6,
          "PWM update must only touch module M1 C8 or M2 C6");
    CHECK((uint64_t)duty * 1000U <= (uint64_t)PWM_DUTY_MAX * 150U,
          "all commands must remain within hard 15 percent limit");
    if (duty != 0U)
    {
        CHECK(encoder_resets > 0U, "encoder totals must reset before first nonzero PWM");
        ++nonzero_commands;
    }
    if (channel == PWM2_MODULE1_CHA_C8) duty1 = duty;
    else duty2 = duty;
}

void encoder_feedback_reset_totals(void)
{
    CHECK(duty1 == 0U && duty2 == 0U, "new run must reset totals before energizing either channel");
    ++encoder_resets;
}

uint32_t __get_PRIMASK(void)
{
    ++mask_saves;
    return interrupt_mask;
}

void __disable_irq(void)
{
    ++mask_disables;
    interrupt_mask = 1U;
}

void __set_PRIMASK(uint32_t value)
{
    ++mask_restores;
    interrupt_mask = value;
}

static motor_control_status_t status(void)
{
    motor_control_status_t out;
    motor_control_get_status(&out);
    return out;
}

static void ticks(unsigned count)
{
    unsigned i;
    for (i = 0U; i < count; ++i) motor_control_tick_10ms();
}

static void reset(uint8_t boot_start, uint8_t boot_stop)
{
    start_key = boot_start;
    stop_key = boot_stop;
    duty1 = 999U;
    duty2 = 999U;
    interrupt_mask = 0U;
    direction_initializations = 0U;
    key_initializations = 0U;
    pwm_initializations = 0U;
    encoder_resets = 0U;
    nonzero_commands = 0U;
    mask_saves = mask_disables = mask_restores = 0U;
    motor_control_init();
    CHECK(direction_initializations == 2U && key_initializations == 2U && pwm_initializations == 2U,
          "initialize exactly two directions, two PWM channels and both motor keys");
    CHECK(duty1 == 0U && duty2 == 0U && status().ready == 0U &&
          status().duty_permille == 0U && status().run_id == 0U, "boot must have no motion or ready gate");
}

static void press_after_release(void)
{
    start_key = GPIO_HIGH;
    ticks(4U);
    start_key = GPIO_LOW;
    ticks(4U);
}

static void test_main_critical_sections_and_stop_latch(void)
{
    motor_control_status_t out;
    unsigned prior_saves;
    reset(GPIO_HIGH, GPIO_HIGH);
    interrupt_mask = 1U;
    prior_saves = mask_saves;
    motor_control_enable_test();
    CHECK(interrupt_mask == 1U && mask_saves > prior_saves,
          "enable must preserve pre-existing IRQ mask");
    motor_control_get_status(&out);
    CHECK(interrupt_mask == 1U, "snapshot must preserve pre-existing IRQ mask");
    CHECK(motor_control_take_servo_stop() == 0U && interrupt_mask == 1U,
          "empty latch read must preserve pre-existing IRQ mask");
    interrupt_mask = 0U;
    stop_key = GPIO_LOW;
    ticks(1U);
    stop_key = GPIO_HIGH;
    ticks(100U);
    interrupt_mask = 1U;
    CHECK(motor_control_take_servo_stop() == 1U && interrupt_mask == 1U,
          "S3 press and release during blocked main must remain latched");
    CHECK(motor_control_take_servo_stop() == 0U && interrupt_mask == 1U,
          "main consumes the latched event once");
    CHECK(mask_saves == mask_disables && mask_saves == mask_restores,
          "all main critical sections must restore their saved mask");
    interrupt_mask = 0U;
    motor_control_get_status(0);
    reset(GPIO_HIGH, GPIO_LOW);
    ticks(1U);
    stop_key = GPIO_HIGH;
    motor_control_enable_test();
    CHECK(motor_control_take_servo_stop() == 1U, "enable must not erase a stop recorded during startup");
}

#ifndef EXPECT_CONFIG_ERROR
static void check_output(motor_test_state_t state, uint8_t channel1, uint8_t channel2)
{
    uint32_t expected = (uint32_t)((uint64_t)PWM_DUTY_MAX * MOTOR_TEST_DUTY_PERMILLE / 1000U);
    motor_control_status_t out = status();
    CHECK(out.state == state, "test must follow the specified channel and gap sequence");
    CHECK(duty1 == (channel1 ? expected : 0U) && duty2 == (channel2 ? expected : 0U),
          "selected channels must carry configured duty and all others zero");
    CHECK(out.duty_permille == ((channel1 || channel2) ? MOTOR_TEST_DUTY_PERMILLE : 0U),
          "snapshot duty must report commanded output, including zero gaps");
}

static void test_startup_gate_and_held_key(void)
{
    reset(GPIO_HIGH, GPIO_HIGH);
    ticks(1000U);
    press_after_release();
    CHECK(encoder_resets == 0U && nonzero_commands == 0U && status().state == MOTOR_TEST_IDLE,
          "presses before main initialization completes must not start motors");
    motor_control_enable_test();
    CHECK(status().ready == 1U, "valid main enable exposes ready gate");
    ticks(1000U);
    CHECK(encoder_resets == 0U, "C12 held through ready gate must not start automatically");
    press_after_release();
    check_output(MOTOR_TEST_CHANNEL1, 1U, 0U);
    CHECK(status().elapsed_ms == 0U && status().run_id == 1U && encoder_resets == 1U,
          "new run begins at zero elapsed and resets totals exactly once");
    reset(GPIO_LOW, GPIO_HIGH);
    motor_control_enable_test();
    ticks(1000U);
    CHECK(encoder_resets == 0U && duty1 == 0U && duty2 == 0U,
          "boot held start cannot bypass stable release requirement");
}

static void test_bounce_and_active_cancel(void)
{
    reset(GPIO_LOW, GPIO_HIGH);
    motor_control_enable_test();
    start_key = GPIO_HIGH;
    ticks(2U);
    start_key = GPIO_LOW;
    ticks(10U);
    CHECK(encoder_resets == 0U, "short release bounce must not arm start");
    start_key = GPIO_HIGH;
    ticks(4U);
    start_key = GPIO_LOW;
    ticks(3U);
    CHECK(encoder_resets == 0U, "less than 30 ms sampled press must not start");
    start_key = GPIO_HIGH;
    ticks(1U);
    start_key = GPIO_LOW;
    ticks(3U);
    CHECK(encoder_resets == 0U, "start bounce must restart debounce interval");
    ticks(1U);
    check_output(MOTOR_TEST_CHANNEL1, 1U, 0U);
    ticks(20U);
    CHECK(encoder_resets == 1U, "held start must not retrigger");
    press_after_release();
    check_output(MOTOR_TEST_STOPPED, 0U, 0U);
    ticks(1000U);
    CHECK(encoder_resets == 1U && status().telemetry_visible == 0U,
          "active cancellation must stay stopped and expire telemetry even with C12 held");
    press_after_release();
    check_output(MOTOR_TEST_CHANNEL1, 1U, 0U);
    CHECK(encoder_resets == 2U && status().run_id == 2U,
          "new stable release and press may start a new run after cancellation");
}

static void test_stop_priority_and_pending_cancel(void)
{
    reset(GPIO_HIGH, GPIO_HIGH);
    motor_control_enable_test();
    ticks(4U);
    start_key = GPIO_LOW;
    ticks(3U);
    stop_key = GPIO_LOW;
    ticks(1U);
    check_output(MOTOR_TEST_STOPPED, 0U, 0U);
    CHECK(encoder_resets == 0U && nonzero_commands == 0U,
          "S3 on the pending start deadline wins without even a transient PWM pulse");
    stop_key = GPIO_HIGH;
    ticks(100U);
    CHECK(encoder_resets == 0U, "S3 release must not replay pending C12 press");
    press_after_release();
    ticks(100U);
    stop_key = GPIO_LOW;
    ticks(1U);
    check_output(MOTOR_TEST_STOPPED, 0U, 0U);
    CHECK(status().telemetry_visible == 1U, "stopped run remains observable");
    ticks(499U);
    CHECK(status().telemetry_visible == 1U, "stop result remains visible for first 4990 ms");
    ticks(1U);
    CHECK(status().telemetry_visible == 0U, "held S3 must not extend five-second result hold forever");
    stop_key = GPIO_HIGH;
    ticks(100U);
    CHECK(encoder_resets == 1U, "held C12 after S3 must not restart");
    press_after_release();
    CHECK(encoder_resets == 2U, "fresh physical press after S3 may restart");
}

static void test_finite_sequence_without_main(void)
{
    reset(GPIO_HIGH, GPIO_HIGH);
    motor_control_enable_test();
    press_after_release();
    check_output(MOTOR_TEST_CHANNEL1, 1U, 0U);
    ticks(199U);
    check_output(MOTOR_TEST_CHANNEL1, 1U, 0U);
    ticks(1U);
    check_output(MOTOR_TEST_GAP1, 0U, 0U);
    CHECK(status().elapsed_ms == 2000U, "M1 receives exactly two seconds");
    ticks(99U);
    check_output(MOTOR_TEST_GAP1, 0U, 0U);
    ticks(1U);
    check_output(MOTOR_TEST_CHANNEL2, 0U, 1U);
    ticks(199U);
    check_output(MOTOR_TEST_CHANNEL2, 0U, 1U);
    ticks(1U);
    check_output(MOTOR_TEST_GAP2, 0U, 0U);
    CHECK(status().elapsed_ms == 5000U, "M2 finishes after five seconds total");
    ticks(99U);
    check_output(MOTOR_TEST_GAP2, 0U, 0U);
    ticks(1U);
    check_output(MOTOR_TEST_BOTH, 1U, 1U);
    ticks(199U);
    check_output(MOTOR_TEST_BOTH, 1U, 1U);
    ticks(1U);
    check_output(MOTOR_TEST_COMPLETE, 0U, 0U);
    CHECK(status().elapsed_ms == 8000U && status().telemetry_visible == 1U,
          "all phases finish after eight seconds using PIT ticks without main service");
    ticks(499U);
    CHECK(status().telemetry_visible == 1U, "completion remains visible for 4990 ms");
    ticks(1U);
    CHECK(status().telemetry_visible == 0U, "completion hold expires after five seconds");
    ticks(5000U);
    CHECK(encoder_resets == 1U && status().run_id == 1U && status().elapsed_ms == 8000U,
          "held start cannot auto-repeat after completion or telemetry expiry");
    press_after_release();
    CHECK(encoder_resets == 2U && status().elapsed_ms == 0U,
          "fresh press after completion resets totals and elapsed time");
}
#endif

int main(void)
{
    test_main_critical_sections_and_stop_latch();
#ifdef EXPECT_CONFIG_ERROR
    reset(GPIO_HIGH, GPIO_HIGH);
    motor_control_enable_test();
    press_after_release();
    ticks(1000U);
    CHECK(status().state == MOTOR_TEST_CONFIG_ERROR && status().ready == 0U &&
          duty1 == 0U && duty2 == 0U && nonzero_commands == 0U && encoder_resets == 0U,
          "invalid configured duty or tick period must remain zero with visible error state");
    stop_key = GPIO_LOW;
    ticks(1U);
    CHECK(status().state == MOTOR_TEST_CONFIG_ERROR && motor_control_take_servo_stop() == 1U,
          "invalid config must preserve error state while still relaying S3");
#else
    test_startup_gate_and_held_key();
    test_bounce_and_active_cancel();
    test_stop_priority_and_pending_cancel();
    test_finite_sequence_without_main();
#endif
    printf("PASS: %u motor checks\n", checks);
    return 0;
}
