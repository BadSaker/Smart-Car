#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "remote_control.h"
#include "remote_control_config.h"
#include "camera_debug.h"
#include "motor_control.h"
#include "servo_control.h"

volatile camera_debug_status_t camera_debug_status;
volatile uint32_t camera_debug_error_count;
volatile uint32_t camera_debug_reconnect_count;

static motor_control_status_t motor;
static uint32_t now_ms;
static uint32_t clock_calls;
static uint32_t read_calls;
static uint32_t motor_status_calls;
static uint32_t link_calls;
static uint8_t links[128];
static uint32_t submit_calls;
static int16_t submitted_duty[32];
static uint8_t submitted_ack[32];
static uint32_t servo_calls;
static uint8_t servo_enabled;
static int16_t servo_steering;
static uint32_t servo_now;
static const uint8_t *read_data;
static int32_t read_result;
static uint32_t read_elapsed;
static uint8_t change_error_during_read;
static uint8_t change_reconnect_during_read;
static uint8_t change_arm_during_read;
static uint8_t disconnect_during_read;

static void check(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static uint32_t clock_ms(void)
{
    ++clock_calls;
    return now_ms;
}

static void service_at(uint32_t time)
{
    now_ms = time;
    remote_control_service(time);
}

void motor_control_get_status(motor_control_status_t *out)
{
    ++motor_status_calls;
    if (out != NULL)
        *out = motor;
}

void motor_control_remote_set_link(uint8_t available)
{
    check(link_calls < sizeof(links), "bounded link history");
    links[link_calls++] = available;
    motor.remote_link = available;
    if (!available)
    {
        /* 边界模型体现断链锁存的可观察结果，不模拟PIT实现细节。 */
        motor.remote_armed = 0U;
        motor.remote_wait_neutral = 1U;
        motor.requested_permille = 0;
        motor.applied_permille = 0;
    }
}

void motor_control_submit_remote(int16_t duty_permille, uint8_t neutral_ack)
{
    check(submit_calls < 32U, "bounded motor submission history");
    submitted_duty[submit_calls] = duty_permille;
    submitted_ack[submit_calls++] = neutral_ack;
    motor.requested_permille = duty_permille;
    if (neutral_ack && duty_permille == 0 && motor.remote_armed)
        motor.remote_wait_neutral = 0U;
}

void servo_control_remote_apply(uint8_t enabled, int16_t steering_permille, uint32_t time)
{
    ++servo_calls;
    servo_enabled = enabled;
    servo_steering = steering_permille;
    servo_now = time;
}

int32_t camera_debug_read_control(uint8_t *buffer, uint32_t capacity)
{
    uint32_t copy_length = read_result > 0 ? (uint32_t)read_result : 0U;
    ++read_calls;
    check(buffer != NULL && capacity == 64U, "one bounded 64-byte read per poll");
    if (copy_length > capacity)
        copy_length = capacity;
    if (copy_length > 0U)
    {
        check(read_data != NULL, "test data supplied");
        memcpy(buffer, read_data, copy_length);
    }
    now_ms += read_elapsed;
    if (change_error_during_read)
        ++camera_debug_error_count;
    if (change_reconnect_during_read)
        ++camera_debug_reconnect_count;
    if (change_arm_during_read)
    {
        ++motor.arm_generation;
        motor.remote_armed = 1U;
        motor.remote_wait_neutral = 1U;
    }
    if (disconnect_during_read)
        camera_debug_status = CAMERA_DEBUG_DISABLED;
    return read_result;
}

static void prepare_read(const uint8_t *data, int32_t result, uint32_t elapsed)
{
    read_data = data;
    read_result = result;
    read_elapsed = elapsed;
    change_error_during_read = 0U;
    change_reconnect_during_read = 0U;
    change_arm_during_read = 0U;
    disconnect_during_read = 0U;
}

static void poll_result(int32_t result)
{
    prepare_read(NULL, result, 0U);
    remote_control_poll(clock_ms);
}

static void poll_text(const char *text)
{
    prepare_read((const uint8_t *)text, (int32_t)strlen(text), 0U);
    remote_control_poll(clock_ms);
}

/* 以不超过200ms的主循环步距等待，不把输入静默伪装成主循环阻塞。 */
static void advance_polling_to(uint32_t target)
{
    while (now_ms != target)
    {
        uint32_t remaining = (uint32_t)(target - now_ms);
        now_ms += remaining > 200U ? 200U : remaining;
        poll_result(-1);
    }
}

static void advance_servicing_to(uint32_t target)
{
    while (now_ms != target)
    {
        uint32_t remaining = (uint32_t)(target - now_ms);
        service_at(now_ms + (remaining > 200U ? 200U : remaining));
    }
}

static remote_control_status_t status(void)
{
    remote_control_status_t result;
    remote_control_get_status(&result);
    return result;
}

static void reset_fixture(void)
{
    memset(&motor, 0, sizeof(motor));
    now_ms = 0U;
    clock_calls = 0U;
    read_calls = 0U;
    motor_status_calls = 0U;
    link_calls = 0U;
    submit_calls = 0U;
    servo_calls = 0U;
    servo_enabled = 0U;
    servo_steering = 0;
    servo_now = 0U;
    camera_debug_status = CAMERA_DEBUG_READY;
    camera_debug_error_count = 0U;
    camera_debug_reconnect_count = 0U;
    prepare_read(NULL, 0, 0U);
    remote_control_init();
}

static void local_arm(void)
{
    ++motor.arm_generation;
    motor.remote_armed = 1U;
    motor.remote_wait_neutral = 1U;
}

static void begin_armed_at(uint32_t start_ms)
{
    reset_fixture();
    now_ms = start_ms;
    poll_result(0);
    local_arm();
    poll_result(0);
    poll_text("Down\r\n");
    check(submit_calls == 1U && submitted_duty[0] == 0 && submitted_ack[0] == 1U,
          "fresh local arm must accept a neutral acknowledgement after drain");
    check(motor.remote_armed && !motor.remote_wait_neutral, "fixture is armed and acknowledged");
    submit_calls = 0U;
    servo_calls = 0U;
}

static void begin_armed(void)
{
    begin_armed_at(0U);
}

static void test_initial(void)
{
    uint32_t calls;
    reset_fixture();
    check(status().draining, "initial stream starts in draining state");
    calls = read_calls;
    poll_text("Down\r\nW\r\n");
    check(read_calls == calls + 1U, "poll performs only one network read");
    check(status().draining && status().received_commands == 0U && submit_calls == 0U,
          "old initial bytes must not be parsed");
    poll_result(-1);
    check(status().draining, "busy is not an empty FIFO");
    poll_result(0);
    check(!status().draining, "actual empty read ends initial drain");
    poll_text("W\r\n");
    check(status().received_commands == 1U && submit_calls == 0U,
          "unarmed fresh commands are counted but never submitted");
}

static void test_arm(void)
{
    reset_fixture();
    poll_result(0);
    poll_text("U");
    local_arm();
    poll_text("p\r\nW\r\n");
    check(status().draining && status().received_commands == 0U && submit_calls == 0U,
          "arm generation drops queued text and pre-arm partial prefix");
    poll_result(-1);
    check(status().draining, "arm drain remains pending while hardware is busy");
    poll_result(0);
    poll_text("W\r\n");
    check(submit_calls == 0U, "drive remains gated by neutral acknowledgement");
    poll_text("Down\r\nW\r\n");
    check(submit_calls == 1U && submitted_duty[0] == 0 && submitted_ack[0] == 1U,
          "only neutral acknowledgement acts in its coalesced chunk");
    poll_text("W\r\n");
    check(submit_calls == 2U && submitted_duty[1] == 100,
          "only a later new chunk can drive after acknowledgement");
    motor.arm_generation = UINT32_MAX;
    service_at(now_ms);
    poll_result(0);
    motor.arm_generation = 0U;
    poll_text("S\r\n");
    check(status().draining && submit_calls == 2U, "arm generation wrap is still a session change");
}

static void test_mapping(void)
{
    begin_armed();
    poll_text("W\r\nS\r\n");
    check(submit_calls == 2U && submitted_duty[0] == 100 && submitted_duty[1] == -100,
          "W/S submit bounded signed throttle");
    check(submitted_ack[0] == 0U && submitted_ack[1] == 0U, "drive is not a neutral acknowledgement");
    poll_text("A\r\n");
    service_at(now_ms);
    check(servo_enabled && servo_steering == 1000, "A requests positive left steering");
    poll_text("D\r\n");
    service_at(now_ms);
    check(servo_enabled && servo_steering == -1000, "D requests negative right steering");
    poll_text("Left\r\n");
    service_at(now_ms);
    check(servo_enabled && servo_steering == 1000, "Left aliases left steering");
    poll_text("Right\r\n");
    service_at(now_ms);
    check(servo_enabled && servo_steering == -1000, "Right aliases right steering");
    poll_text("Up\r\n");
    service_at(now_ms);
    check(servo_enabled && servo_steering == 0, "Up requests center");
    poll_text("Down\r\n");
    check(submit_calls == 3U && submitted_duty[2] == 0 && submitted_ack[2] == 1U,
          "Down submits STOP and neutral acknowledgement");
}

static void test_parser(void)
{
    begin_armed();
    poll_text("Ri");
    poll_text("ght\r");
    check(status().received_commands == 1U, "partial text has no recognized event");
    poll_text("\nW\r\n");
    check(status().received_commands == 3U && submit_calls == 1U && submitted_duty[0] == 100,
          "split Right followed by W is parsed in stream order");
    service_at(now_ms);
    check(servo_enabled && servo_steering == -1000, "coalesced drive preserves a recent turn");
    poll_text("w\r\n W\r\nW\rS\r\n");
    check(status().rejected_commands == 3U && submit_calls == 1U,
          "real parser rejects case, spaces, and malformed CR without action");
    camera_debug_status = CAMERA_DEBUG_DISABLED;
    service_at(now_ms);
    check(status().received_commands == 3U && status().rejected_commands == 3U,
          "stream reset preserves cumulative diagnostics");
}

static void test_stop(void)
{
    begin_armed();
    poll_text("A\r\n");
    motor.remote_wait_neutral = 1U;
    poll_text("Down\r\nW\r\nA\r\nDown\r\n");
    check(submit_calls == 1U && submitted_duty[0] == 0 && submitted_ack[0] == 1U,
          "Down precedes wait gate and suppresses all later commands in its chunk");
    check(status().steering_permille == 0, "STOP wins over later steering in same chunk");
    check(status().received_commands == 6U, "suppressed coalesced events are still counted");
    service_at(now_ms);
    check(servo_enabled && servo_steering == 0, "acknowledged STOP activity centers steering");
    submit_calls = 0U;
    poll_text("W\r\nDown\r\nW\r\n");
    check(submit_calls == 2U && submitted_duty[0] == 100 && submitted_duty[1] == 0,
          "STOP is the last submitted action when drive precedes it");
}

static void test_steering(void)
{
    begin_armed();
    poll_text("W\r\n");
    now_ms = 200U;
    poll_text("A\r\n");
    now_ms = 400U;
    poll_text("Right\r\nUp\r\n");
    service_at(now_ms);
    check(submit_calls == 1U && submitted_duty[0] == 100,
          "steering never submits throttle or neutral and cannot renew drive lease");
    check(servo_enabled && servo_steering == 0, "authorized steering refreshes only steering activity");
}

static void test_gap(void)
{
    begin_armed();
    now_ms = 100U;
    poll_text("A\r\n");
    service_at(now_ms);
    check(servo_enabled && servo_steering == 1000, "turn established before idle gap");
    advance_polling_to(1601U);
    poll_text("W\r\n");
    service_at(now_ms);
    check(servo_enabled && servo_steering == 0 && submit_calls == 1U,
          "new drive after more than 1500ms starts centered even without an intervening service");
    advance_polling_to(3102U);
    poll_text("?\r\n");
    service_at(now_ms);
    check(!servo_enabled && status().steering_permille == 0,
          "unknown text does not refresh authorized activity");
}

static void test_wrap(void)
{
    uint32_t event_time = UINT32_MAX - 100U;
    begin_armed_at(event_time);
    poll_text("Left\r\n");
    advance_servicing_to(event_time + 1500U);
    check(servo_enabled && servo_steering == 1000, "1500ms steering lease includes exact boundary across wrap");
    service_at(event_time + 1501U);
    check(!servo_enabled && servo_steering == 0 && status().steering_permille == 0,
          "1501ms expires across uint32 clock wrap");
}

static void test_link(void)
{
    uint32_t reads;
    begin_armed();
    poll_text("A\r\n");
    service_at(now_ms);
    check(servo_enabled, "steering active before link loss");
    reads = read_calls;
    camera_debug_status = CAMERA_DEBUG_SEND_ERROR;
    remote_control_poll(clock_ms);
    check(read_calls == reads && !motor.remote_link && !motor.remote_armed,
          "non-READY never reads and disarms link");
    service_at(now_ms);
    check(!servo_enabled && status().draining && status().steering_permille == 0,
          "link loss clears steering and starts drain");
    camera_debug_status = CAMERA_DEBUG_READY;
    poll_text("Down\r\nW\r\n");
    check(motor.remote_link && !motor.remote_armed && status().draining && submit_calls == 0U,
          "reconnection restores link but never replays or rearms");
    poll_result(0);
    poll_text("W\r\n");
    check(submit_calls == 0U, "reconnected unarmed commands remain ignored");
}

static void test_counters(void)
{
    uint32_t iteration;
    for (iteration = 0U; iteration < 2U; ++iteration)
    {
        uint32_t calls;
        begin_armed();
        poll_text("U");
        calls = link_calls;
        if (iteration == 0U)
            ++camera_debug_error_count;
        else
            ++camera_debug_reconnect_count;
        service_at(now_ms);
        check(link_calls >= calls + 2U && links[calls] == 0U && links[calls + 1U] == 1U,
              "changed camera counters latch link-down even if status is READY");
        check(!motor.remote_armed && status().draining && !servo_enabled,
              "hidden failed session cannot retain arm or steering activity");
        poll_text("p\r\nW\r\n");
        check(submit_calls == 0U && status().received_commands == 1U,
              "old session partial and queued text discarded");
        poll_result(0);
        check(!status().draining, "counter-change session eventually drains");
    }
}

static void test_read_session(void)
{
    uint32_t iteration;
    for (iteration = 0U; iteration < 3U; ++iteration)
    {
        begin_armed();
        prepare_read((const uint8_t *)"W\r\n", 3, 1U);
        change_error_during_read = iteration == 0U;
        change_reconnect_during_read = iteration == 1U;
        disconnect_during_read = iteration == 2U;
        remote_control_poll(clock_ms);
        check(submit_calls == 0U && !motor.remote_armed && status().draining,
              "session changes during blocking read invalidate returned commands");
        service_at(now_ms);
        check(!servo_enabled, "session change cannot retain steering authority");
    }
    begin_armed();
    prepare_read(NULL, 0, 1U);
    change_reconnect_during_read = 1U;
    remote_control_poll(clock_ms);
    check(status().draining, "empty result from a changed session cannot complete its new drain");
}

static void test_read_arm(void)
{
    begin_armed();
    prepare_read((const uint8_t *)"Down\r\nW\r\n", 9, 1U);
    change_arm_during_read = 1U;
    remote_control_poll(clock_ms);
    check(submit_calls == 0U && status().draining && motor.remote_wait_neutral,
          "local arm during read rejects all bytes gathered under the earlier generation");
    poll_result(0);
    poll_text("W\r\n");
    check(submit_calls == 0U, "new generation still requires a future neutral acknowledgement");
}

static void test_latency(void)
{
    uint32_t calls;
    begin_armed();
    now_ms = 100U;
    calls = clock_calls;
    prepare_read((const uint8_t *)"A\r\n", 3, 50U);
    remote_control_poll(clock_ms);
    check(clock_calls == calls + 2U, "clock sampled exactly before and after one read");
    advance_servicing_to(1650U);
    check(servo_enabled && servo_steering == 1000 && servo_now == 1650U,
          "authorized activity uses post-read time, not stale pre-read time");
    service_at(1651U);
    check(!servo_enabled, "50ms read is accepted but its activity still expires");
    begin_armed();
    prepare_read((const uint8_t *)"W\r\n", 3, 51U);
    remote_control_poll(clock_ms);
    check(submit_calls == 0U && !motor.remote_link && !motor.remote_armed,
          "read exceeding 50ms discards commands and disarms");
    check(status().draining && status().rejected_commands == 1U,
          "slow read counted once and requires new drain");
    service_at(now_ms);
    check(!servo_enabled, "slow read cannot refresh steering");
    begin_armed_at(UINT32_MAX - 20U);
    prepare_read((const uint8_t *)"W\r\n", 3, 51U);
    remote_control_poll(clock_ms);
    check(submit_calls == 0U && !motor.remote_armed, "read timeout uses wrap-safe subtraction");
}

static void test_invalid_read(void)
{
    uint8_t data[64];
    memset(data, 'W', sizeof(data));
    begin_armed();
    prepare_read(data, 65, 0U);
    remote_control_poll(clock_ms);
    check(submit_calls == 0U && !motor.remote_link && !motor.remote_armed &&
          status().draining && status().rejected_commands == 1U,
          "oversized reported length fails closed without reading outside buffer");
    begin_armed();
    prepare_read(NULL, -2, 0U);
    remote_control_poll(clock_ms);
    check(submit_calls == 0U && !motor.remote_armed && status().rejected_commands == 1U,
          "unexpected negative reader result fails closed");
}

static void test_null_clock(void)
{
    uint32_t reads;
    begin_armed();
    reads = read_calls;
    remote_control_poll(NULL);
    check(read_calls == reads && !motor.remote_link && !motor.remote_armed && status().draining,
          "NULL clock cannot authorize an untimed blocking read");
    remote_control_get_status(NULL);
}

static void test_disarm(void)
{
    begin_armed();
    poll_text("A\r\n");
    service_at(now_ms);
    check(servo_enabled, "servo enabled before simulated S3 disarm");
    motor.remote_armed = 0U;
    service_at(now_ms + 1U);
    check(!servo_enabled && status().steering_permille == 0, "S3 disarm clears steering activity");
    poll_text("W\r\nA\r\n");
    service_at(now_ms + 2U);
    check(!servo_enabled && submit_calls == 0U, "later packets cannot reactivate a disarmed vehicle");
    local_arm();
    poll_result(0);
    service_at(now_ms + 3U);
    check(!servo_enabled, "new arm alone cannot reuse old steering activity");
    poll_text("Down\r\n");
    service_at(now_ms + 4U);
    check(servo_enabled && servo_steering == 0, "new explicit acknowledgement may start centered activity");
}

static void test_main_gap_poll(void)
{
    uint32_t reads;
    begin_armed();
    poll_text("A\r\nW\r");
    service_at(now_ms);
    check(servo_enabled && servo_steering == 1000, "turn active before a stalled main loop");
    reads = read_calls;
    now_ms = 301U;
    prepare_read((const uint8_t *)"\nW\r\n", 4, 0U);
    remote_control_poll(clock_ms);
    check(read_calls == reads && submit_calls == 0U && !motor.remote_link &&
          !motor.remote_armed && status().draining,
          "main gap rejects queued drive before reading and revokes local authorization");
    check(status().steering_permille == 0 && status().rejected_commands == 1U,
          "main gap clears old steering and partial text and records rejection");
    service_at(now_ms);
    check(!servo_enabled, "service after stall cannot reactivate old steering");
    poll_text("\nW\r\nDown\r\n");
    check(status().draining && submit_calls == 0U, "stalled session bytes are only drained");
    poll_result(0);
    poll_text("Down\r\nW\r\n");
    check(submit_calls == 0U, "remote acknowledgement cannot replace a new local arm");
    local_arm();
    poll_result(0);
    poll_text("W\r\n");
    check(submit_calls == 0U, "new local arm still requires a fresh Down");
    poll_text("Down\r\n");
    poll_text("W\r\n");
    check(submit_calls == 2U && submitted_duty[0] == 0 && submitted_duty[1] == 100,
          "only new local arm plus future neutral acknowledgement restores drive");
}

static void test_main_gap_service(void)
{
    begin_armed();
    poll_text("A\r\n");
    service_at(now_ms);
    check(servo_enabled, "servo active before a service stall");
    service_at(301U);
    check(!servo_enabled && servo_steering == 0 && !motor.remote_link &&
          !motor.remote_armed && status().draining,
          "service checks its gap before READY synchronization or output enable");
    service_at(302U);
    check(!servo_enabled && !motor.remote_armed,
          "a later normal service must not restore pre-stall authorization");
}

static void test_main_gap_boundary(void)
{
    uint32_t reads;
    begin_armed();
    poll_text("A\r\n");
    service_at(300U);
    check(motor.remote_armed && servo_enabled, "exact 300ms main service gap remains allowed");
    service_at(601U);
    check(!motor.remote_armed && !servo_enabled, "301ms main service gap revokes authorization");

    begin_armed();
    now_ms = 300U;
    poll_text("W\r\n");
    check(submit_calls == 1U && motor.remote_armed, "exact 300ms poll gap accepts a fresh event");
    reads = read_calls;
    now_ms = 601U;
    poll_text("W\r\n");
    check(submit_calls == 1U && read_calls == reads && !motor.remote_armed,
          "301ms poll gap drops queued input before performing a read");

    begin_armed_at(UINT32_MAX - 100U);
    service_at(199U);
    check(motor.remote_armed, "exact 300ms main service gap permits uint32 wrap");
    service_at(500U);
    check(!motor.remote_armed && !servo_enabled, "301ms main gap revokes across uint32 wrap");
}

static void test_main_silent_polling(void)
{
    uint32_t index;
    begin_armed();
    for (index = 0U; index < 20U; ++index)
    {
        now_ms += 200U;
        poll_result((index & 1U) ? 0 : -1);
        check(motor.remote_armed, "regular polling with no commands must not be treated as a main stall");
    }
    service_at(now_ms);
    check(motor.remote_armed && !servo_enabled && submit_calls == 0U,
          "silence expires steering activity without revoking the local arm");
    poll_text("W\r\n");
    check(submit_calls == 1U && submitted_duty[0] == 100,
          "point commands remain usable after silent but regularly serviced time");
}

static void test_main_post_read_sample(void)
{
    begin_armed();
    now_ms = 250U;
    prepare_read((const uint8_t *)"A\r\n", 3, 50U);
    remote_control_poll(clock_ms);
    check(now_ms == 300U, "read fixture advances clock by 50ms");
    service_at(600U);
    check(motor.remote_armed && servo_enabled && servo_steering == 1000,
          "post-read time also updates the shared main-service gap reference");
}

static void test_disabled(void)
{
    remote_control_status_t result;
    reset_fixture();
    motor.remote_armed = 1U;
    poll_text("W\r\nDown\r\n");
    remote_control_poll(NULL);
    service_at(123U);
    remote_control_get_status(&result);
    check(read_calls == 0U && clock_calls == 0U && link_calls == 0U &&
          motor_status_calls == 0U && submit_calls == 0U && servo_calls == 0U,
          "disabled mode performs no network, clock, motor or servo calls");
    check(result.received_commands == 0U && result.rejected_commands == 0U &&
          result.steering_permille == 0 && result.draining == 0U,
          "disabled diagnostics are inert");
}

int main(int argc, char **argv)
{
    check(argc == 2, "choose one test case");
    if (!REMOTE_CONTROL_ENABLED)
    {
        check(strcmp(argv[1], "disabled") == 0, "disabled test profile");
        test_disabled();
    }
    else if (strcmp(argv[1], "initial") == 0) test_initial();
    else if (strcmp(argv[1], "arm") == 0) test_arm();
    else if (strcmp(argv[1], "mapping") == 0) test_mapping();
    else if (strcmp(argv[1], "parser") == 0) test_parser();
    else if (strcmp(argv[1], "stop") == 0) test_stop();
    else if (strcmp(argv[1], "steering") == 0) test_steering();
    else if (strcmp(argv[1], "gap") == 0) test_gap();
    else if (strcmp(argv[1], "wrap") == 0) test_wrap();
    else if (strcmp(argv[1], "link") == 0) test_link();
    else if (strcmp(argv[1], "counters") == 0) test_counters();
    else if (strcmp(argv[1], "read-session") == 0) test_read_session();
    else if (strcmp(argv[1], "read-arm") == 0) test_read_arm();
    else if (strcmp(argv[1], "latency") == 0) test_latency();
    else if (strcmp(argv[1], "invalid-read") == 0) test_invalid_read();
    else if (strcmp(argv[1], "null-clock") == 0) test_null_clock();
    else if (strcmp(argv[1], "disarm") == 0) test_disarm();
    else if (strcmp(argv[1], "main-poll-gap") == 0) test_main_gap_poll();
    else if (strcmp(argv[1], "main-service-gap") == 0) test_main_gap_service();
    else if (strcmp(argv[1], "main-boundary") == 0) test_main_gap_boundary();
    else if (strcmp(argv[1], "main-silent") == 0) test_main_silent_polling();
    else if (strcmp(argv[1], "main-post-read") == 0) test_main_post_read_sample();
    else check(0, "unknown test case");
    printf("PASS: remote control %s\n", argv[1]);
    return 0;
}
