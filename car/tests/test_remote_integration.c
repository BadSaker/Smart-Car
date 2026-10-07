#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zf_driver_gpio.h"
#include "zf_driver_pwm.h"
#include "camera_debug.h"
#include "motor_control.h"
#include "servo_control.h"
#include "remote_control.h"
#include "remote_control_config.h"

volatile camera_debug_status_t camera_debug_status;
volatile uint32_t camera_debug_error_count;
volatile uint32_t camera_debug_reconnect_count;
static uint8_t motor_key = GPIO_HIGH;
static uint8_t servo_key = GPIO_HIGH;
static uint8_t stop_key = GPIO_HIGH;
static uint8_t direction1;
static uint8_t direction2;
static uint32_t duty1;
static uint32_t duty2;
static uint32_t servo_duty;
static uint8_t servo_signal_low;
static uint8_t in_tick;
static uint32_t interrupt_mask;
static uint32_t now_ms;
static unsigned encoder_resets;
static unsigned servo_starts;
static unsigned consumed_stops;
static unsigned checks;
static uint8_t rx_bytes[256];
static uint32_t rx_length;
static uint32_t rx_chunk_limit = 64U;
static uint8_t rx_busy;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d at %ums: %s\n", __LINE__, (unsigned)now_ms, message); \
        exit(1); \
    } \
} while (0)

/* 边界桩记录真实模块写出的信号，并约束电机只在PIT、舵机只在主循环更新。 */
void gpio_init(gpio_pin_enum pin, gpio_dir_enum direction, uint8_t level, uint32_t mode)
{
    CHECK(!in_tick && interrupt_mask == 0U, "GPIO initialization belongs to unmasked main");
    if (pin == C30)
    {
        CHECK(direction == GPO && level == GPIO_LOW && mode == GPO_PUSH_PULL,
              "servo stop actively drives the signal low");
        servo_signal_low = 1U;
    }
    else if (pin == C9 || pin == C7)
    {
        CHECK(direction == GPO && level == GPIO_HIGH && mode == GPO_PUSH_PULL,
              "both confirmed forward direction pins initialize high");
        if (pin == C9) direction1 = level;
        else direction2 = level;
    }
    else if (pin == C12 || pin == C14 || pin == C15)
        CHECK(direction == GPI && mode == GPI_PULL_UP, "local keys retain pull-ups");
    else
        CHECK((pin == C8 || pin == C6) && direction == GPO && level == GPIO_LOW,
              "motor PWM pins preload low");
}

uint8_t gpio_get_level(gpio_pin_enum pin)
{
    CHECK(pin == C12 || pin == C14 || pin == C15, "only control keys are sampled");
    if (pin == C12) return motor_key;
    return pin == C14 ? stop_key : servo_key;
}

void gpio_set_level(gpio_pin_enum pin, uint8_t level)
{
    CHECK(in_tick && interrupt_mask == 0U && (pin == C9 || pin == C7),
          "only PIT updates motor direction pins");
    CHECK(duty1 == 0U && duty2 == 0U, "both motors are zero before direction writes");
    if (pin == C9) direction1 = level;
    else direction2 = level;
}

void pwm_init(pwm_channel_enum channel, uint32_t frequency, uint32_t duty)
{
    CHECK(!in_tick && interrupt_mask == 0U, "PWM initialization belongs to unmasked main");
    if (channel == PWM4_MODULE2_CHA_C30)
    {
        CHECK(frequency == 50U && duty == 740U, "new servo ownership starts at 1480us center");
        servo_duty = duty;
        servo_signal_low = 0U;
        ++servo_starts;
    }
    else
    {
        CHECK((channel == PWM2_MODULE1_CHA_C8 || channel == PWM2_MODULE0_CHA_C6) &&
              frequency == 17000U && duty == 0U, "both motors initialize at 17kHz zero PWM");
        if (channel == PWM2_MODULE1_CHA_C8) duty1 = duty;
        else duty2 = duty;
    }
}

void pwm_set_duty(pwm_channel_enum channel, uint32_t duty)
{
    CHECK(interrupt_mask == 0U, "hardware PWM updates do not mask interrupts");
    if (channel == PWM4_MODULE2_CHA_C30)
    {
        CHECK(!in_tick && (duty == 0U || (duty >= 710U && duty <= 770U)),
              "main-only servo output stays inside approved pulse range");
        servo_duty = duty;
    }
    else
    {
        CHECK(in_tick && duty <= 1000U &&
              (channel == PWM2_MODULE1_CHA_C8 || channel == PWM2_MODULE0_CHA_C6),
              "PIT-only motor output stays within ten percent");
        if (channel == PWM2_MODULE1_CHA_C8) duty1 = duty;
        else duty2 = duty;
    }
}

void encoder_feedback_reset_totals(void)
{
    CHECK(in_tick && duty1 == 0U && duty2 == 0U, "arming resets totals in PIT before motion");
    ++encoder_resets;
}

uint32_t __get_PRIMASK(void) { return interrupt_mask; }
void __disable_irq(void) { interrupt_mask = 1U; }
void __set_PRIMASK(uint32_t mask) { interrupt_mask = mask; }

/* 接收桩保留FIFO分片语义：忙是-1，只有真实空队列才返回0。 */
int32_t camera_debug_read_control(uint8_t *buffer, uint32_t capacity)
{
    uint32_t length;
    CHECK(!in_tick && interrupt_mask == 0U && buffer != NULL && capacity > 0U,
          "network receive belongs to unmasked main");
    if (rx_busy || camera_debug_status != CAMERA_DEBUG_READY) return -1;
    length = rx_length;
    if (length > capacity) length = capacity;
    if (length > rx_chunk_limit) length = rx_chunk_limit;
    memcpy(buffer, rx_bytes, length);
    memmove(rx_bytes, rx_bytes + length, rx_length - length);
    rx_length -= length;
    return (int32_t)length;
}

static void receive_text(const char *text)
{
    size_t length = strlen(text);
    CHECK(length <= sizeof(rx_bytes) - rx_length, "test RX queue capacity is sufficient");
    memcpy(rx_bytes + rx_length, text, length);
    rx_length += (uint32_t)length;
}

static motor_control_status_t motor_status(void)
{
    motor_control_status_t out;
    motor_control_get_status(&out);
    return out;
}

static remote_control_status_t remote_status(void)
{
    remote_control_status_t out;
    remote_control_get_status(&out);
    return out;
}

static uint32_t clock_ms(void) { return now_ms; }

/* 与main.c一致：服务前先消费PIT停止锁存，poll返回后再服务一次。 */
static void service_controls(void)
{
    if (motor_control_take_servo_stop())
    {
        ++consumed_stops;
        servo_control_stop();
    }
    remote_control_service(now_ms);
}

static void main_iteration(void)
{
    service_controls();
    remote_control_poll(clock_ms);
    service_controls();
}

static void ticks(unsigned count, uint8_t run_main)
{
    unsigned index;
    for (index = 0U; index < count; ++index)
    {
        now_ms += 10U;
        in_tick = 1U;
        motor_control_tick_10ms();
        in_tick = 0U;
        if (run_main) main_iteration();
    }
}

static void finish_drain(void)
{
    unsigned remaining = 140U;
    do
    {
        main_iteration();
        CHECK(remaining-- > 0U, "receive drain must finish after finite queued bytes");
    } while (rx_length != 0U || remote_status().draining);
}

static void press_arm_key(uint8_t run_main)
{
    motor_key = GPIO_HIGH;
    ticks(4U, run_main);
    motor_key = GPIO_LOW;
    ticks(4U, run_main);
}

static void expect_forward(void)
{
    motor_control_status_t out = motor_status();
    CHECK(out.remote_armed && !out.remote_wait_neutral && out.state == MOTOR_REMOTE_DRIVING &&
          out.requested_permille == 100 && out.applied_permille == 100,
          "fresh W reaches the real armed and neutral-confirmed motor backend");
    CHECK(duty1 == 1000U && duty2 == 1000U && direction1 == GPIO_HIGH && direction2 == GPIO_HIGH,
          "W drives both confirmed forward channels at ten percent");
}

/* 一个连续会话覆盖排空、分片命令、独立租约、重连和跨主循环停顿的S3锁存。 */
static void test_remote_pipeline(void)
{
    unsigned index;
    unsigned starts_before_gap;
    unsigned starts_before_stop;
    uint32_t arm_epoch;
    uint32_t command_count;
    uint32_t rejected_before_gap;
    uint16_t pulse_before_step;

    camera_debug_status = CAMERA_DEBUG_READY;
    motor_control_init();
    servo_control_init(now_ms);
    remote_control_init();
    motor_control_enable_test();
    finish_drain();
    ticks(4U, 1U);
    CHECK(!motor_status().remote_armed && duty1 == 0U && duty2 == 0U && servo_signal_low,
          "connected boot does not arm or energize any actuator");

    /* 解锁时故意保留旧Down/W/D，多块排空和忙状态都不能当作新命令。 */
    receive_text("Down\r\nW\r\nD\r\n");
    motor_key = GPIO_LOW;
    ticks(4U, 0U);
    arm_epoch = motor_status().arm_generation;
    CHECK(arm_epoch == 1U && motor_status().remote_armed && motor_status().remote_wait_neutral &&
          encoder_resets == 1U && duty1 == 0U && duty2 == 0U,
          "local C12 creates an arm epoch that requires a new neutral confirmation");
    rx_busy = 1U;
    main_iteration();
    CHECK(remote_status().draining && motor_status().remote_wait_neutral,
          "busy receive after arm is not evidence that old bytes were drained");
    rx_busy = 0U;
    rx_chunk_limit = 2U;
    finish_drain();
    ticks(1U, 1U);
    CHECK(motor_status().remote_wait_neutral && duty1 == 0U && duty2 == 0U && servo_signal_low,
          "old Down and W cannot pass the arm-epoch RX flush");
    rx_chunk_limit = 64U;

    /* 新Down跨三个接收块；仅完整CRLF提交确认，PIT消费前仍不允许输出。 */
    command_count = remote_status().received_commands;
    receive_text("Do");
    main_iteration();
    ticks(1U, 1U);
    receive_text("wn\r");
    main_iteration();
    ticks(1U, 1U);
    CHECK(motor_status().remote_wait_neutral && remote_status().received_commands == command_count,
          "partial Down cannot acknowledge neutral");
    receive_text("\n");
    main_iteration();
    CHECK(motor_status().remote_wait_neutral && remote_status().received_commands == command_count + 1U,
          "complete Down is parsed but motor readiness still belongs to the next PIT tick");
    ticks(1U, 1U);
    CHECK(motor_status().state == MOTOR_REMOTE_READY && !motor_status().remote_wait_neutral &&
          duty1 == 0U && duty2 == 0U, "new Down reaches PIT and confirms zero-output readiness");
    receive_text("W\r\n");
    main_iteration();
    CHECK(duty1 == 0U && duty2 == 0U, "main parser submits a mailbox without writing motor PWM");
    ticks(1U, 1U);
    expect_forward();
    CHECK(servo_control_get_status()->output_enabled && servo_duty == 740U,
          "authorized remote activity starts the actual servo at calibrated center");

    receive_text("D\r\n");
    main_iteration();
    CHECK(remote_status().steering_permille == -1000 &&
          servo_control_get_status()->target_pulse_us == 1420U,
          "D maps through real parser and adapter to the confirmed right pulse");
    pulse_before_step = servo_control_get_status()->current_pulse_us;
    for (index = 1U; index <= 29U; ++index)
    {
        ticks(1U, 0U);
        if (index == 2U)
        {
            main_iteration();
            CHECK(servo_control_get_status()->current_pulse_us == pulse_before_step - 2U,
                  "right steering advances only one 2us step after 20ms");
        }
        if (index == 10U || index == 20U)
        {
            receive_text(index == 10U ? "A\r\n" : "D\r\n");
            main_iteration();
            CHECK(motor_status().command_age_ms == index * 10U,
                  "A and D events must not refresh the motor W lease");
        }
    }
    expect_forward();
    CHECK(motor_status().command_age_ms == 290U, "W remains valid through 290ms without main service");
    ticks(1U, 0U);
    CHECK(motor_status().state == MOTOR_REMOTE_TIMEOUT && motor_status().command_age_ms == 300U &&
          duty1 == 0U && duty2 == 0U, "PIT expires W at 300ms even while main is blocked");
    ticks(1U, 1U);
    CHECK(duty1 == 0U && duty2 == 0U, "main service cannot replay expired W after resuming");
    /* 每10ms继续服务主循环，只停止输入，避免与本机主循环停顿保护混淆。 */
    ticks(REMOTE_CONTROL_STEERING_TIMEOUT_MS / 10U + 1U, 1U);
    CHECK(duty1 == 0U && duty2 == 0U && servo_signal_low && !servo_control_get_status()->output_enabled &&
          remote_status().steering_permille == 0 && motor_status().remote_armed,
          "idle main iterations cannot restart old drive or steering activity");

    /* 主循环停顿400ms期间保留旧W/D；PIT到期先停电机，恢复主循环先撤权排空。 */
    receive_text("W\r\n");
    main_iteration();
    ticks(1U, 1U);
    expect_forward();
    CHECK(servo_control_get_status()->target_pulse_us == 1480U,
          "fresh W after steering inactivity cannot revive an old right-turn target");
    receive_text("D\r\n");
    main_iteration();
    CHECK(servo_control_get_status()->output_enabled &&
          servo_control_get_status()->target_pulse_us == 1420U,
          "right steering is active before main-loop stall");
    starts_before_gap = servo_starts;
    command_count = remote_status().received_commands;
    rejected_before_gap = remote_status().rejected_commands;
    receive_text("W\r\nD\r\n");
    ticks(29U, 0U);
    expect_forward();
    ticks(1U, 0U);
    CHECK(motor_status().state == MOTOR_REMOTE_TIMEOUT && motor_status().command_age_ms == 300U &&
          duty1 == 0U && duty2 == 0U && rx_length != 0U,
          "PIT expires drive while stale W/D remain unread during blocked main");
    ticks(10U, 0U);
    main_iteration();
    CHECK(servo_signal_low && !servo_control_get_status()->output_enabled &&
          remote_status().steering_permille == 0 && servo_starts == starts_before_gap &&
          remote_status().received_commands == command_count &&
          remote_status().rejected_commands == rejected_before_gap + 1U,
          "resuming after 400ms revokes activity before old queued W/D can dispatch");
    finish_drain();
    ticks(1U, 1U);
    CHECK(!motor_status().remote_armed && duty1 == 0U && duty2 == 0U && servo_signal_low &&
          servo_starts == starts_before_gap && remote_status().received_commands == command_count,
          "main-gap revocation reaches PIT and drained old commands cannot restart outputs");
    receive_text("W\r\nD\r\n");
    main_iteration();
    ticks(1U, 1U);
    CHECK(!motor_status().remote_armed && duty1 == 0U && duty2 == 0U && servo_signal_low,
          "even fresh W/D require local C12 rearming after a main-loop stall");
    press_arm_key(1U);
    finish_drain();
    CHECK(motor_status().arm_generation == arm_epoch + 1U && motor_status().remote_wait_neutral,
          "main-gap recovery creates a new arm epoch that requires a fresh Down");
    arm_epoch = motor_status().arm_generation;
    receive_text("W\r\nD\r\n");
    main_iteration();
    ticks(1U, 1U);
    CHECK(motor_status().remote_wait_neutral && duty1 == 0U && duty2 == 0U && servo_signal_low,
          "C12 alone cannot restore drive or steering before the new Down confirmation");
    receive_text("Down\r\n");
    main_iteration();
    ticks(1U, 1U);
    CHECK(motor_status().state == MOTOR_REMOTE_READY && !motor_status().remote_wait_neutral &&
          duty1 == 0U && duty2 == 0U && servo_control_get_status()->target_pulse_us == 1480U,
          "fresh Down after rearm restores only neutral readiness, never old W or D");

    /* 断链后把旧Down/W留在接收队列，恢复链路仍必须本地重新解锁。 */
    receive_text("W\r\n");
    main_iteration();
    ticks(1U, 1U);
    expect_forward();
    camera_debug_status = CAMERA_DEBUG_CONNECT_ERROR;
    main_iteration();
    ticks(1U, 1U);
    CHECK(!motor_status().remote_armed && duty1 == 0U && duty2 == 0U && servo_signal_low,
          "network loss disarms motors and revokes servo output");
    receive_text("Down\r\nW\r\n");
    camera_debug_status = CAMERA_DEBUG_READY;
    finish_drain();
    ticks(1U, 1U);
    CHECK(!motor_status().remote_armed && motor_status().arm_generation == arm_epoch &&
          duty1 == 0U && duty2 == 0U, "network recovery and old bytes never rearm automatically");
    receive_text("W\r\n");
    main_iteration();
    ticks(1U, 1U);
    CHECK(!motor_status().remote_armed && duty1 == 0U && duty2 == 0U,
          "even a new W requires a fresh local arm after reconnect");
    press_arm_key(1U);
    finish_drain();
    CHECK(motor_status().arm_generation == arm_epoch + 1U && motor_status().remote_wait_neutral,
          "new local C12 press begins a new neutral-confirmation epoch");
    receive_text("W\r\n");
    main_iteration();
    ticks(1U, 1U);
    CHECK(motor_status().remote_wait_neutral && duty1 == 0U && duty2 == 0U,
          "new arm still rejects W until a post-flush Down arrives");
    receive_text("Down\r\n");
    main_iteration();
    ticks(1U, 1U);
    CHECK(!motor_status().remote_wait_neutral && duty1 == 0U && duty2 == 0U,
          "fresh Down clears wait without releasing an earlier rejected W");
    receive_text("W\r\nD\r\n");
    main_iteration();
    ticks(1U, 1U);
    expect_forward();
    CHECK(servo_control_get_status()->output_enabled, "servo is active before blocked-main stop test");

    /* S3按下和释放全部发生在主循环停顿期间；恢复后必须先消费锁存且不能复活旧W。 */
    starts_before_stop = servo_starts;
    servo_key = GPIO_LOW;
    receive_text("W\r\n");
    stop_key = GPIO_LOW;
    ticks(1U, 0U);
    CHECK(!motor_status().remote_armed && duty1 == 0U && duty2 == 0U &&
          servo_control_get_status()->output_enabled,
          "PIT immediately stops motors while servo awaits resumed main");
    stop_key = GPIO_HIGH;
    ticks(1U, 0U);
    main_iteration();
    CHECK(consumed_stops == 1U && servo_signal_low && servo_duty == 0U &&
          !servo_control_get_status()->output_enabled && servo_starts == starts_before_stop,
          "resumed main consumes short S3 stop before remote service and never reactivates servo");
    finish_drain();
    ticks(10U, 1U);
    CHECK(!motor_status().remote_armed && duty1 == 0U && duty2 == 0U && servo_signal_low &&
          servo_starts == starts_before_stop && consumed_stops == 1U,
          "held C12/S2, released S3 and stale queued W cannot restart either actuator");
    CHECK(remote_status().rejected_commands == rejected_before_gap + 1U,
          "only the deliberate main-loop stall records a rejection; wire commands remain valid");
}

int main(void)
{
    test_remote_pipeline();
    printf("PASS: %u integration checks\n", checks);
    return 0;
}
