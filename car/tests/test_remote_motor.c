#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "zf_driver_gpio.h"
#include "zf_driver_pwm.h"
#include "motor_control.h"

static uint8_t start_key;
static uint8_t stop_key;
static uint8_t direction1;
static uint8_t direction2;
static uint32_t duty1;
static uint32_t duty2;
static uint32_t interrupt_mask;
static uint32_t now_ms;
static uint32_t zero_since_ms;
static uint8_t in_tick;
static unsigned encoder_resets;
static unsigned pwm_calls;
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

/* 仅替代硬件IO和CMSIS边界；所有按键、邮箱及计时逻辑使用真实模块。 */
void gpio_init(gpio_pin_enum pin, gpio_dir_enum direction, uint8_t level, uint32_t mode)
{
    CHECK(interrupt_mask == 0U, "GPIO initialization must not mask interrupts");
    if (pin == C9 || pin == C7)
    {
        CHECK(direction == GPO && level == GPIO_HIGH && mode == GPO_PUSH_PULL,
              "both confirmed forward directions initialize high");
        if (pin == C9) direction1 = level;
        else direction2 = level;
    }
    else if (pin == C12 || pin == C14)
    {
        CHECK(direction == GPI && mode == GPI_PULL_UP, "C12 and S3 are pull-up inputs");
    }
    else
    {
        CHECK((pin == C8 || pin == C6) && direction == GPO &&
              level == GPIO_LOW && mode == GPO_PUSH_PULL, "PWM pins preload low");
    }
}

uint8_t gpio_get_level(gpio_pin_enum pin)
{
    CHECK(pin == C12 || pin == C14, "motor backend must not sample C13 or steering key");
    return pin == C12 ? start_key : stop_key;
}

void gpio_set_level(gpio_pin_enum pin, uint8_t level)
{
    uint8_t previous = pin == C9 ? direction1 : direction2;
    CHECK(in_tick && interrupt_mask == 0U, "ongoing direction writes belong to unmasked PIT");
    CHECK(pin == C9 || pin == C7, "only motor direction pins may change");
    CHECK(duty1 == 0U && duty2 == 0U, "both PWM channels must be zero before any DIR write");
    if (previous != level && nonzero_commands != 0U)
        CHECK(now_ms - zero_since_ms >= 100U, "reversal requires at least 100 ms at zero PWM");
    if (pin == C9) direction1 = level;
    else direction2 = level;
}

void pwm_init(pwm_channel_enum channel, uint32_t frequency, uint32_t duty)
{
    CHECK(interrupt_mask == 0U, "PWM initialization must not mask interrupts");
    CHECK((channel == PWM2_MODULE1_CHA_C8 || channel == PWM2_MODULE0_CHA_C6) &&
          frequency == 17000U && duty == 0U, "initial PWM must be 17 kHz zero");
    if (channel == PWM2_MODULE1_CHA_C8) duty1 = duty;
    else duty2 = duty;
}

void pwm_set_duty(pwm_channel_enum channel, uint32_t duty)
{
    uint8_t was_nonzero = (duty1 != 0U || duty2 != 0U) ? 1U : 0U;
    CHECK(in_tick && interrupt_mask == 0U, "ongoing PWM writes belong to unmasked PIT");
    CHECK(channel == PWM2_MODULE1_CHA_C8 || channel == PWM2_MODULE0_CHA_C6,
          "PWM updates use only motor channels");
    CHECK((uint64_t)duty * 1000U <= (uint64_t)PWM_DUTY_MAX * 100U,
          "all remote commands are capped at ten percent");
    if (duty != 0U) ++nonzero_commands;
    if (channel == PWM2_MODULE1_CHA_C8) duty1 = duty;
    else duty2 = duty;
    if (was_nonzero && duty1 == 0U && duty2 == 0U) zero_since_ms = now_ms;
    ++pwm_calls;
}

void encoder_feedback_reset_totals(void)
{
    CHECK(in_tick && duty1 == 0U && duty2 == 0U,
          "encoder totals may reset only in PIT while both outputs are zero");
    ++encoder_resets;
}

uint32_t __get_PRIMASK(void) { ++mask_saves; return interrupt_mask; }
void __disable_irq(void) { ++mask_disables; interrupt_mask = 1U; }
void __set_PRIMASK(uint32_t value) { ++mask_restores; interrupt_mask = value; }

static motor_control_status_t status(void)
{
    motor_control_status_t out;
    motor_control_get_status(&out);
    return out;
}

static void ticks(unsigned count)
{
    unsigned i;
    for (i = 0U; i < count; ++i)
    {
        now_ms += 10U;
        in_tick = 1U;
        motor_control_tick_10ms();
        in_tick = 0U;
    }
}

static void reset(uint8_t boot_start)
{
    start_key = boot_start;
    stop_key = GPIO_HIGH;
    direction1 = direction2 = GPIO_HIGH;
    duty1 = duty2 = 999U;
    interrupt_mask = 0U;
    now_ms = zero_since_ms = 0U;
    in_tick = 0U;
    encoder_resets = pwm_calls = nonzero_commands = 0U;
    mask_saves = mask_disables = mask_restores = 0U;
    motor_control_init();
    CHECK(duty1 == 0U && duty2 == 0U && status().ready == 0U,
          "startup is physically zero with ready gate closed");
    CHECK(status().remote_armed == 0U && status().remote_wait_neutral == 0U &&
          status().requested_permille == 0 && status().applied_permille == 0 &&
          status().arm_generation == 0U, "reinitialization clears every remote command and arm");
}

static void press_after_release(void)
{
    start_key = GPIO_HIGH;
    ticks(4U);
    start_key = GPIO_LOW;
    ticks(4U);
}

static void submit(int16_t throttle, uint8_t neutral)
{
    unsigned before = pwm_calls;
    motor_control_submit_remote(throttle, neutral);
    CHECK(pwm_calls == before, "main command mailbox never writes PWM directly");
}

static void check_zero(motor_test_state_t expected)
{
    motor_control_status_t out = status();
    CHECK(out.state == expected, "zero-output state must expose the expected reason");
    CHECK(duty1 == 0U && duty2 == 0U && out.duty_permille == 0U &&
          out.applied_permille == 0, "both physical channels and signed status must be zero");
}

#ifndef EXPECT_CONFIG_ERROR
static void check_drive(int16_t signed_duty)
{
    motor_control_status_t out = status();
    uint32_t expected_duty = (uint32_t)((uint64_t)PWM_DUTY_MAX * 100U / 1000U);
    uint8_t expected_direction = signed_duty > 0 ? GPIO_HIGH : GPIO_LOW;
    CHECK(out.state == MOTOR_REMOTE_DRIVING && out.remote_armed && !out.remote_wait_neutral,
          "only armed neutral-confirmed state may drive");
    CHECK(out.requested_permille == signed_duty && out.applied_permille == signed_duty &&
          out.duty_permille == 100U, "status reports clamped requested and applied signed command");
    CHECK(duty1 == expected_duty && duty2 == expected_duty &&
          direction1 == expected_direction && direction2 == expected_direction,
          "both channels follow the confirmed forward or reverse direction");
}

static void arm_and_neutral(void)
{
    reset(GPIO_HIGH);
    motor_control_remote_set_link(1U);
    motor_control_enable_test();
    press_after_release();
    check_zero(MOTOR_REMOTE_WAIT_NEUTRAL);
    CHECK(status().remote_link && status().remote_armed && status().remote_wait_neutral &&
          status().arm_generation == 1U, "physical C12 arms once and requires a new neutral");
    submit(0, 1U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_READY);
    CHECK(!status().remote_wait_neutral, "new neutral acknowledgment opens the drive gate");
}

static void test_link_ready_and_new_neutral_barriers(void)
{
    reset(GPIO_LOW);
    motor_control_remote_set_link(1U);
    submit(0, 1U);
    press_after_release();
    submit(100, 0U);
    ticks(10U);
    CHECK(status().remote_armed == 0U && nonzero_commands == 0U,
          "link, C12 and commands cannot bypass startup ready gate");
    motor_control_enable_test();
    ticks(100U);
    CHECK(status().remote_armed == 0U, "C12 held across enable must not arm");
    submit(0, 1U);
    press_after_release();
    check_zero(MOTOR_REMOTE_WAIT_NEUTRAL);
    CHECK(status().arm_generation == 1U && status().remote_wait_neutral,
          "a neutral submitted before C12 must be discarded at the arm boundary");
    submit(100, 0U);
    ticks(1U);
    submit(-100, 0U);
    ticks(1U);
    submit(100, 1U);
    ticks(1U);
    submit(0, 0U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_WAIT_NEUTRAL);
    CHECK(nonzero_commands == 0U, "W, S or a nonzero acknowledgment cannot unlock neutral gate");
    submit(0, 1U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_READY);
    submit(32767, 0U);
    ticks(1U);
    check_drive(100);
    reset(GPIO_HIGH);
    motor_control_enable_test();
    submit(0, 1U);
    press_after_release();
    submit(100, 0U);
    ticks(1U);
    CHECK(status().remote_armed == 0U && nonzero_commands == 0U,
          "C12 with no available link must not arm or launch bench mode");
}

#ifndef EXPECT_SHORT_LEASE
static void test_lease_without_main_or_steering_refresh(void)
{
    unsigned i;
    arm_and_neutral();
    submit(100, 0U);
    ticks(1U);
    CHECK(status().command_age_ms == 0U, "new applied drive command starts a fresh lease");
    for (i = 0U; i < 29U; ++i)
    {
        /* 转向事件不提交电机邮箱；重复连接发布和读取状态也不得续期。 */
        motor_control_remote_set_link(1U);
        (void)status();
        ticks(1U);
    }
    check_drive(100);
    CHECK(status().command_age_ms == 290U, "drive lease retains the last ten milliseconds");
    ticks(1U);
    check_zero(MOTOR_REMOTE_TIMEOUT);
    CHECK(status().remote_armed && !status().remote_wait_neutral &&
          status().requested_permille == 0 && status().command_age_ms == 300U,
          "PIT expires the command at 300 ms and clears its target while keeping local arm");
    ticks(100U);
    check_zero(MOTOR_REMOTE_TIMEOUT);
    submit(100, 0U);
    ticks(1U);
    check_drive(100);
    ticks(29U);
    submit(100, 0U);
    ticks(1U);
    CHECK(status().command_age_ms == 0U, "only a fresh drive command renews the lease");
    ticks(29U);
    check_drive(100);
    ticks(1U);
    check_zero(MOTOR_REMOTE_TIMEOUT);
}

static void test_c12_debounce_disarm_and_new_generation(void)
{
    reset(GPIO_LOW);
    motor_control_enable_test();
    motor_control_remote_set_link(1U);
    start_key = GPIO_HIGH;
    ticks(2U);
    start_key = GPIO_LOW;
    ticks(10U);
    CHECK(status().remote_armed == 0U, "short release bounce cannot arm C12");
    start_key = GPIO_HIGH;
    ticks(4U);
    start_key = GPIO_LOW;
    ticks(3U);
    CHECK(status().remote_armed == 0U, "press shorter than debounce window cannot arm");
    ticks(1U);
    check_zero(MOTOR_REMOTE_WAIT_NEUTRAL);
    submit(0, 1U);
    ticks(1U);
    submit(100, 0U);
    ticks(1U);
    check_drive(100);
    press_after_release();
    check_zero(MOTOR_REMOTE_DISARMED);
    CHECK(status().remote_armed == 0U && status().requested_permille == 0,
          "a fresh C12 press while armed disarms and cancels all output");
    submit(0, 1U);
    submit(100, 0U);
    ticks(100U);
    CHECK(status().remote_armed == 0U, "held C12 and stale mailbox must not rearm");
    submit(0, 1U);
    press_after_release();
    check_zero(MOTOR_REMOTE_WAIT_NEUTRAL);
    CHECK(status().arm_generation == 2U, "each genuine rearm publishes a new receive flush generation");
    submit(100, 0U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_WAIT_NEUTRAL);
    submit(0, 1U);
    ticks(1U);
    submit(100, 0U);
    ticks(1U);
    check_drive(100);
}

static void test_s3_priority_and_servo_latch(void)
{
    reset(GPIO_HIGH);
    motor_control_remote_set_link(1U);
    motor_control_enable_test();
    ticks(4U);
    start_key = GPIO_LOW;
    ticks(3U);
    submit(0, 1U);
    stop_key = GPIO_LOW;
    ticks(1U);
    check_zero(MOTOR_REMOTE_DISARMED);
    CHECK(status().arm_generation == 0U && !status().remote_armed && nonzero_commands == 0U,
          "raw S3 on the C12 debounce deadline wins before any arm or pulse");
    stop_key = GPIO_HIGH;
    ticks(100U);
    CHECK(!status().remote_armed, "releasing S3 must consume a previously pending C12 press");
    CHECK(motor_control_take_servo_stop() == 1U && motor_control_take_servo_stop() == 0U,
          "S3 is latched for main even when it was released during a long main stall");
    press_after_release();
    submit(0, 1U);
    ticks(1U);
    submit(100, 0U);
    ticks(1U);
    check_drive(100);
    submit(-100, 0U);
    stop_key = GPIO_LOW;
    ticks(1U);
    check_zero(MOTOR_REMOTE_DISARMED);
    stop_key = GPIO_HIGH;
    submit(0, 1U);
    submit(100, 0U);
    ticks(100U);
    check_zero(MOTOR_REMOTE_DISARMED);
    CHECK(!status().remote_armed, "commands after S3 cannot rearm or replay an old direction");
}

static void test_latched_link_loss_and_mailbox_discard(void)
{
    arm_and_neutral();
    submit(100, 0U);
    ticks(1U);
    check_drive(100);
    motor_control_remote_set_link(0U);
    motor_control_remote_set_link(1U);
    submit(0, 1U);
    submit(100, 0U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_DISARMED);
    CHECK(status().remote_link && !status().remote_armed && status().requested_permille == 0,
          "false-true link pulse between PITs must still latch disarm and discard commands");
    ticks(100U);
    check_zero(MOTOR_REMOTE_DISARMED);
    press_after_release();
    check_zero(MOTOR_REMOTE_WAIT_NEUTRAL);
    submit(100, 0U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_WAIT_NEUTRAL);
    submit(0, 1U);
    ticks(1U);
    submit(100, 0U);
    ticks(1U);
    check_drive(100);
    motor_control_remote_set_link(0U);
    ticks(1U);
    CHECK(!status().remote_link && !status().remote_armed,
          "link remaining unavailable must also disarm by the next PIT");
    press_after_release();
    motor_control_remote_set_link(1U);
    ticks(100U);
    CHECK(!status().remote_armed, "holding C12 through reconnect cannot rearm");
}

static void test_neutral_latch_beats_coalesced_drive(void)
{
    arm_and_neutral();
    submit(100, 0U);
    ticks(1U);
    submit(0, 1U);
    submit(100, 0U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_READY);
    CHECK(status().requested_permille == 0, "Down then W in one mailbox interval must clear drive");
    ticks(100U);
    check_zero(MOTOR_REMOTE_READY);
    submit(100, 0U);
    ticks(1U);
    check_drive(100);
    submit(100, 0U);
    submit(0, 1U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_READY);
    submit(-100, 0U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_REVERSING);
    submit(0, 1U);
    submit(-100, 0U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_READY);
    ticks(100U);
    check_zero(MOTOR_REMOTE_READY);
    CHECK(status().requested_permille == 0 && direction1 == GPIO_HIGH && direction2 == GPIO_HIGH,
          "neutral cancels a queued reversal and stale S must never change DIR later");
}

static void test_reverse_gap_signed_clamp_and_replacement(void)
{
    arm_and_neutral();
    submit(32767, 0U);
    ticks(1U);
    check_drive(100);
    submit(-32768, 0U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_REVERSING);
    CHECK(status().requested_permille == -100 && direction1 == GPIO_HIGH && direction2 == GPIO_HIGH,
          "opposite request clamps safely before negation and starts a zero-output gap");
    ticks(9U);
    check_zero(MOTOR_REMOTE_REVERSING);
    CHECK(direction1 == GPIO_HIGH && direction2 == GPIO_HIGH, "DIR stays unchanged for first 90 ms");
    ticks(1U);
    check_drive(-100);
    submit(100, 0U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_REVERSING);
    ticks(5U);
    submit(-100, 0U);
    ticks(1U);
    check_drive(-100);
    CHECK(direction1 == GPIO_LOW && direction2 == GPIO_LOW,
          "a fresh return to the already applied direction cancels the pending reversal");
    submit(100, 0U);
    ticks(1U);
    stop_key = GPIO_LOW;
    ticks(1U);
    stop_key = GPIO_HIGH;
    ticks(100U);
    check_zero(MOTOR_REMOTE_DISARMED);
    CHECK(direction1 == GPIO_LOW && direction2 == GPIO_LOW, "S3 cancels future DIR changes during a gap");
}

static void test_zero_command_cannot_bypass_reverse_gap(void)
{
    arm_and_neutral();
    submit(100, 0U);
    ticks(1U);
    submit(0, 1U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_READY);
    ticks(5U);
    submit(-100, 0U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_REVERSING);
    ticks(3U);
    check_zero(MOTOR_REMOTE_REVERSING);
    ticks(1U);
    check_drive(-100);
    submit(0, 0U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_READY);
    ticks(10U);
    submit(100, 0U);
    ticks(1U);
    check_drive(100);
    ticks(30U);
    check_zero(MOTOR_REMOTE_TIMEOUT);
    submit(-100, 0U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_REVERSING);
    ticks(8U);
    check_zero(MOTOR_REMOTE_REVERSING);
    ticks(1U);
    check_drive(-100);
}
#endif

static void test_short_lease_expires_during_reverse_gap(void)
{
#ifdef EXPECT_SHORT_LEASE
    arm_and_neutral();
    submit(100, 0U);
    ticks(1U);
    check_drive(100);
    submit(-100, 0U);
    ticks(1U);
    check_zero(MOTOR_REMOTE_REVERSING);
    ticks(4U);
    check_zero(MOTOR_REMOTE_REVERSING);
    ticks(1U);
    check_zero(MOTOR_REMOTE_TIMEOUT);
    ticks(100U);
    check_zero(MOTOR_REMOTE_TIMEOUT);
    CHECK(status().requested_permille == 0 && direction1 == GPIO_HIGH && direction2 == GPIO_HIGH,
          "lease expiry during reverse wait must erase the future direction and PWM command");
#endif
}
#endif

static void test_mailbox_and_snapshot_preserve_irq_mask(void)
{
    motor_control_status_t out;
    unsigned before;
    reset(GPIO_HIGH);
    interrupt_mask = 1U;
    before = mask_saves;
    motor_control_enable_test();
    motor_control_remote_set_link(1U);
    motor_control_remote_set_link(0U);
    motor_control_remote_set_link(1U);
    submit(0, 1U);
    motor_control_get_status(&out);
    CHECK(motor_control_take_servo_stop() == 0U, "empty stop latch is readable with IRQ masked");
    CHECK(interrupt_mask == 1U && mask_saves >= before + 7U,
          "all shared snapshots and mailbox setters preserve the caller's PRIMASK");
    CHECK(mask_saves == mask_disables && mask_saves == mask_restores,
          "every critical section restores the saved interrupt mask");
    interrupt_mask = 0U;
    motor_control_get_status(0);
    stop_key = GPIO_LOW;
    ticks(1U);
    interrupt_mask = 1U;
    CHECK(motor_control_take_servo_stop() == 1U && interrupt_mask == 1U,
          "S3 relay consumption also preserves preexisting IRQ mask");
    interrupt_mask = 0U;
}

int main(void)
{
#ifdef EXPECT_CONFIG_ERROR
    reset(GPIO_HIGH);
    motor_control_enable_test();
    motor_control_remote_set_link(1U);
    press_after_release();
    submit(0, 1U);
    ticks(1U);
    submit(100, 0U);
    ticks(100U);
    check_zero(MOTOR_TEST_CONFIG_ERROR);
    CHECK(!status().ready && !status().remote_armed && nonzero_commands == 0U,
          "invalid tick configuration never opens remote output");
    stop_key = GPIO_LOW;
    ticks(1U);
    check_zero(MOTOR_TEST_CONFIG_ERROR);
    CHECK(motor_control_take_servo_stop() == 1U, "configuration fault still relays S3 to servo");
#else
    test_link_ready_and_new_neutral_barriers();
#ifndef EXPECT_SHORT_LEASE
    test_lease_without_main_or_steering_refresh();
    test_c12_debounce_disarm_and_new_generation();
    test_s3_priority_and_servo_latch();
    test_latched_link_loss_and_mailbox_discard();
    test_neutral_latch_beats_coalesced_drive();
    test_reverse_gap_signed_clamp_and_replacement();
    test_zero_command_cannot_bypass_reverse_gap();
#endif
    test_short_lease_expires_during_reverse_gap();
#endif
    test_mailbox_and_snapshot_preserve_irq_mask();
    printf("PASS: %u remote motor checks\n", checks);
    return 0;
}
