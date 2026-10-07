#include "remote_control.h"
#include "remote_control_config.h"
#include <stddef.h>
#include <string.h>

static remote_control_status_t remote_status;

#if REMOTE_CONTROL_ENABLED
#include "remote_command.h"
#include "camera_debug.h"
#include "motor_control.h"
#include "servo_control.h"

typedef struct
{
    uint32_t now_ms;
    uint32_t arm_generation;
    uint8_t stop_seen;
    uint8_t generation_changed;
} remote_control_dispatch_t;

static remote_command_parser_t remote_parser;
static uint32_t observed_error_count;
static uint32_t observed_reconnect_count;
static uint32_t observed_arm_generation;
static uint32_t activity_ms;
static uint32_t last_service_ms;
static uint8_t arm_generation_known;
static uint8_t link_available;
static uint8_t activity_valid;
static uint8_t service_time_known;

static void remote_control_clear_activity(void)
{
    remote_status.steering_permille = 0;
    activity_valid = 0U;
}

/* 新会话不继承半行或转向；诊断累计值保留到显式初始化。 */
static void remote_control_reset_stream(void)
{
    uint32_t valid_count = remote_parser.valid_count;
    uint32_t rejected_count = remote_parser.rejected_count;
    remote_command_init(&remote_parser);
    remote_parser.valid_count = valid_count;
    remote_parser.rejected_count = rejected_count;
    remote_status.draining = 1U;
    remote_control_clear_activity();
}

/*
 * READY可能已跨过一次阻塞重连，故同时比较错误/重连计数。
 * 返回1表示本次发现会话变化，调用方必须丢弃之前开始读取的数据。
 */
static uint8_t remote_control_sync_state(motor_control_status_t *motor)
{
    uint32_t errors = camera_debug_error_count;
    uint32_t reconnects = camera_debug_reconnect_count;
    uint8_t changed = errors != observed_error_count ||
                      reconnects != observed_reconnect_count;
    observed_error_count = errors;
    observed_reconnect_count = reconnects;

    if (camera_debug_status != CAMERA_DEBUG_READY)
    {
        if (link_available)
            motor_control_remote_set_link(0U);
        link_available = 0U;
        remote_control_reset_stream();
        motor_control_get_status(motor);
        observed_arm_generation = motor->arm_generation;
        arm_generation_known = 1U;
        return 1U;
    }

    if (!link_available || changed)
    {
        /* 即便本次已是READY，也先锁存失链；后端不会因此自动重新武装。 */
        motor_control_remote_set_link(0U);
        remote_control_reset_stream();
        motor_control_remote_set_link(1U);
        link_available = 1U;
        changed = 1U;
    }

    motor_control_get_status(motor);
    if (!arm_generation_known || motor->arm_generation != observed_arm_generation)
    {
        observed_arm_generation = motor->arm_generation;
        arm_generation_known = 1U;
        remote_control_reset_stream();
        changed = 1U;
    }
    return changed;
}

static void remote_control_reject_read(void)
{
    if (remote_parser.rejected_count < UINT32_MAX)
        ++remote_parser.rejected_count;
    motor_control_remote_set_link(0U);
    link_available = 0U;
    remote_control_reset_stream();
}

/*
 * 本机主循环停顿可能让此前排队的数据显得新鲜，恢复时先撤权再排空。
 * 只比较本机服务间隔；文本协议无发送时间戳，不能推断迟到TCP包的端到端年龄。
 * 无命令的正常poll/service也更新时间，输入静默本身不撤销本地武装。
 */
static uint8_t remote_control_check_main_gap(uint32_t now_ms)
{
    uint8_t expired = service_time_known &&
        (uint32_t)(now_ms - last_service_ms) > REMOTE_CONTROL_MAIN_LOOP_TIMEOUT_MS;
    last_service_ms = now_ms;
    service_time_known = 1U;
    if (expired)
    {
        remote_control_reject_read();
        return 1U;
    }
    return 0U;
}

static void remote_control_dispatch(remote_command_type_t command, void *context)
{
    remote_control_dispatch_t *dispatch = (remote_control_dispatch_t *)context;
    motor_control_status_t motor;

    if (dispatch->stop_seen)
        return;
    if (command == REMOTE_COMMAND_STOP)
        dispatch->stop_seen = 1U;

    motor_control_get_status(&motor);
    if (motor.arm_generation != dispatch->arm_generation)
    {
        /* 不在解析器回调中重置正在遍历的状态，整块结束后统一丢弃。 */
        dispatch->generation_changed = 1U;
        dispatch->stop_seen = 1U;
        return;
    }
    if (!motor.remote_link || !motor.remote_armed)
        return;
    if (command != REMOTE_COMMAND_STOP && motor.remote_wait_neutral)
        return;

    if (!activity_valid ||
        (uint32_t)(dispatch->now_ms - activity_ms) > REMOTE_CONTROL_STEERING_TIMEOUT_MS)
        remote_status.steering_permille = 0;

    switch (command)
    {
        case REMOTE_COMMAND_FORWARD:
            motor_control_submit_remote(REMOTE_CONTROL_MAX_DUTY_PERMILLE, 0U);
            break;
        case REMOTE_COMMAND_BACKWARD:
            motor_control_submit_remote(-REMOTE_CONTROL_MAX_DUTY_PERMILLE, 0U);
            break;
        case REMOTE_COMMAND_LEFT:
            remote_status.steering_permille = 1000;
            break;
        case REMOTE_COMMAND_RIGHT:
            remote_status.steering_permille = -1000;
            break;
        case REMOTE_COMMAND_CENTER:
            remote_status.steering_permille = 0;
            break;
        case REMOTE_COMMAND_STOP:
            /* STOP在等待确认阶段也有效，且同一读取块后续事件只计数。 */
            remote_status.steering_permille = 0;
            motor_control_submit_remote(0, 1U);
            break;
        default:
            return;
    }
    activity_ms = dispatch->now_ms;
    activity_valid = 1U;
}
#endif

void remote_control_init(void)
{
    memset(&remote_status, 0, sizeof(remote_status));
#if REMOTE_CONTROL_ENABLED
    remote_command_init(&remote_parser);
    observed_error_count = camera_debug_error_count;
    observed_reconnect_count = camera_debug_reconnect_count;
    observed_arm_generation = 0U;
    arm_generation_known = 0U;
    link_available = 0U;
    activity_ms = 0U;
    last_service_ms = 0U;
    activity_valid = 0U;
    service_time_known = 0U;
    remote_status.draining = 1U;
    motor_control_remote_set_link(0U);
#endif
}

void remote_control_poll(uint32_t (*clock_ms)(void))
{
#if REMOTE_CONTROL_ENABLED
    uint8_t buffer[REMOTE_CONTROL_RX_CHUNK_SIZE];
    motor_control_status_t motor;
    remote_control_dispatch_t dispatch;
    uint32_t before_ms;
    uint32_t after_ms;
    int32_t received;

    if (clock_ms == NULL)
    {
        remote_control_reject_read();
        return;
    }

    before_ms = clock_ms();
    if (remote_control_check_main_gap(before_ms))
        return;
    (void)remote_control_sync_state(&motor);
    if (!link_available)
        return;

    received = camera_debug_read_control(buffer, sizeof(buffer));
    after_ms = clock_ms();
    if (remote_control_check_main_gap(after_ms))
        return;
    if ((uint32_t)(after_ms - before_ms) > REMOTE_CONTROL_RX_MAX_BLOCK_MS ||
        received < -1 || received > (int32_t)sizeof(buffer))
    {
        remote_control_reject_read();
        return;
    }

    /* 读取期间本地武装或网络代次变化，连返回0也不能结束新会话排空。 */
    if (remote_control_sync_state(&motor) || !link_available)
        return;
    if (received < 0)
        return;

    if (remote_status.draining)
    {
        if (received == 0)
            remote_status.draining = 0U;
        return;
    }
    if (received == 0)
        return;

    dispatch.now_ms = after_ms;
    dispatch.arm_generation = motor.arm_generation;
    dispatch.stop_seen = 0U;
    dispatch.generation_changed = 0U;
    remote_command_feed(&remote_parser, buffer, (uint32_t)received,
                        remote_control_dispatch, &dispatch);
    if (dispatch.generation_changed)
        remote_control_reset_stream();
#else
    (void)clock_ms;
#endif
}

void remote_control_service(uint32_t now_ms)
{
#if REMOTE_CONTROL_ENABLED
    motor_control_status_t motor;
    uint8_t enabled = 0U;
    if (remote_control_check_main_gap(now_ms))
    {
        /* 不先恢复READY连接，防止本轮重新启用停顿前的舵机状态。 */
        servo_control_remote_apply(0U, 0, now_ms);
        return;
    }
    (void)remote_control_sync_state(&motor);
    if (!link_available || !motor.remote_link || !motor.remote_armed)
    {
        remote_control_clear_activity();
    }
    else if (motor.remote_wait_neutral || remote_status.draining)
    {
        remote_status.steering_permille = 0;
    }
    else if (activity_valid &&
             (uint32_t)(now_ms - activity_ms) <= REMOTE_CONTROL_STEERING_TIMEOUT_MS)
    {
        enabled = 1U;
    }
    else
    {
        remote_control_clear_activity();
    }
    servo_control_remote_apply(enabled, remote_status.steering_permille, now_ms);
#else
    (void)now_ms;
#endif
}

void remote_control_get_status(remote_control_status_t *out)
{
    if (out == NULL)
        return;
#if REMOTE_CONTROL_ENABLED
    remote_status.received_commands = remote_parser.valid_count;
    remote_status.rejected_commands = remote_parser.rejected_count;
#endif
    *out = remote_status;
}
