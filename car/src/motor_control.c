#include "motor_control.h"
#include "motor_test_config.h"
#include "remote_control_config.h"
#include "encoder_feedback.h"
#include "zf_driver_gpio.h"
#include "zf_driver_pwm.h"

static volatile motor_control_status_t motor_status;
static volatile uint8_t motor_initialized;
static volatile uint8_t motor_servo_stop_pending;
static volatile uint8_t motor_start_sample;
static volatile uint8_t motor_start_stable;
static volatile uint8_t motor_start_armed;
static volatile uint16_t motor_key_stable_ms;
#if REMOTE_CONTROL_ENABLED
/* 主循环是邮箱写端，PIT是唯一输出端；临界区只拷贝固定大小标量。 */
typedef struct
{
    uint32_t generation;
    int16_t throttle_permille;
    uint8_t link;
    uint8_t link_lost;
    uint8_t stop;
    uint8_t neutral_ack;
    uint8_t fresh;
} motor_remote_input_t;

static volatile uint32_t motor_remote_generation;
static volatile int16_t motor_remote_throttle;
static volatile uint8_t motor_remote_link;
static volatile uint8_t motor_remote_link_lost;
static volatile uint8_t motor_remote_stop;
static volatile uint8_t motor_remote_neutral_ack;
static uint32_t motor_remote_consumed_generation;
static uint16_t motor_remote_zero_ms;
static int8_t motor_remote_last_direction;
#else
static uint16_t motor_phase_elapsed_ms;
static uint16_t motor_result_remaining_ms;
static uint32_t motor_test_pwm_duty;
#endif

static uint8_t motor_config_is_valid(void)
{
#if REMOTE_CONTROL_ENABLED
    return (MOTOR_CONTROL_TICK_MS == 10U &&
            REMOTE_CONTROL_MAX_DUTY_PERMILLE >= 1 &&
            REMOTE_CONTROL_MAX_DUTY_PERMILLE <= MOTOR_TEST_MAX_DUTY_PERMILLE &&
            REMOTE_CONTROL_DRIVE_LEASE_MS >= 10U &&
            REMOTE_CONTROL_DRIVE_LEASE_MS <= 65535U &&
            REMOTE_CONTROL_DIRECTION_GAP_MS >= 10U &&
            REMOTE_CONTROL_DIRECTION_GAP_MS <= 65535U) ? 1U : 0U;
#else
    return (MOTOR_CONTROL_TICK_MS == 10U &&
            MOTOR_TEST_DUTY_PERMILLE >= 1 &&
            MOTOR_TEST_DUTY_PERMILLE <= MOTOR_TEST_MAX_DUTY_PERMILLE) ? 1U : 0U;
#endif
}

static void motor_reset_start_key(void)
{
    /* 撤销旧按下和释放历史；上电按住或初始化期间按住都不能自动启动。 */
    motor_start_sample = MOTOR_TEST_KEY_ACTIVE_LEVEL;
    motor_start_stable = MOTOR_TEST_KEY_ACTIVE_LEVEL;
    motor_start_armed = 0U;
    motor_key_stable_ms = 0U;
}

#if !REMOTE_CONTROL_ENABLED
static uint8_t motor_test_is_running(void)
{
    return (motor_status.state >= MOTOR_TEST_CHANNEL1 &&
            motor_status.state <= MOTOR_TEST_BOTH) ? 1U : 0U;
}

static void motor_write_output(uint8_t channel1, uint8_t channel2)
{
    pwm_set_duty(MOTOR_CHANNEL1_PWM, channel1 ? motor_test_pwm_duty : 0U);
    pwm_set_duty(MOTOR_CHANNEL2_PWM, channel2 ? motor_test_pwm_duty : 0U);
    motor_status.duty_permille = (channel1 || channel2) ? (uint16_t)MOTOR_TEST_DUTY_PERMILLE : 0U;
}

static void motor_finish(motor_test_state_t state)
{
    motor_write_output(0U, 0U);
    motor_status.state = state;
    motor_phase_elapsed_ms = 0U;
    motor_reset_start_key();
    if (motor_status.run_id != 0U)
    {
        motor_status.telemetry_visible = 1U;
        motor_result_remaining_ms = MOTOR_TEST_RESULT_HOLD_MS;
    }
}

#endif

void motor_control_init(void)
{
    uint32_t interrupt_mask = __get_PRIMASK();
    __disable_irq();
    motor_initialized = 0U;
    __set_PRIMASK(interrupt_mask);

    /* 初始化外设不持有全局中断屏蔽；门控使并发PIT不能更新未初始化PWM。 */
    gpio_init(MOTOR_CHANNEL1_PWM_PIN, GPO, GPIO_LOW, GPO_PUSH_PULL);
    gpio_init(MOTOR_CHANNEL2_PWM_PIN, GPO, GPIO_LOW, GPO_PUSH_PULL);
    gpio_init(MOTOR_CHANNEL1_DIRECTION_PIN, GPO, MOTOR_CHANNEL1_REFERENCE_LEVEL, GPO_PUSH_PULL);
    gpio_init(MOTOR_CHANNEL2_DIRECTION_PIN, GPO, MOTOR_CHANNEL2_REFERENCE_LEVEL, GPO_PUSH_PULL);
    pwm_init(MOTOR_CHANNEL1_PWM, MOTOR_TEST_PWM_FREQUENCY_HZ, 0U);
    pwm_init(MOTOR_CHANNEL2_PWM, MOTOR_TEST_PWM_FREQUENCY_HZ, 0U);
    gpio_init(MOTOR_TEST_START_KEY_PIN, GPI, GPIO_HIGH, GPI_PULL_UP);
    gpio_init(MOTOR_TEST_STOP_KEY_PIN, GPI, GPIO_HIGH, GPI_PULL_UP);

#if !REMOTE_CONTROL_ENABLED
    motor_test_pwm_duty = motor_config_is_valid() ?
        (uint32_t)((uint64_t)PWM_DUTY_MAX * MOTOR_TEST_DUTY_PERMILLE / 1000U) : 0U;
#endif
    interrupt_mask = __get_PRIMASK();
    __disable_irq();
#if REMOTE_CONTROL_ENABLED
    motor_status.state = motor_config_is_valid() ? MOTOR_REMOTE_DISARMED : MOTOR_TEST_CONFIG_ERROR;
    motor_status.telemetry_visible = 0U;
    motor_remote_generation = 0U;
    motor_remote_consumed_generation = 0U;
    motor_remote_throttle = 0;
    motor_remote_link = 0U;
    motor_remote_link_lost = 0U;
    motor_remote_stop = 0U;
    motor_remote_neutral_ack = 0U;
    motor_remote_zero_ms = 0U;
    motor_remote_last_direction = 0;
#else
    motor_status.state = motor_config_is_valid() ? MOTOR_TEST_IDLE : MOTOR_TEST_CONFIG_ERROR;
    motor_status.telemetry_visible = motor_config_is_valid() ? 0U : 1U;
    motor_phase_elapsed_ms = 0U;
    motor_result_remaining_ms = 0U;
#endif
    motor_status.ready = 0U;
    motor_status.duty_permille = 0U;
    motor_status.elapsed_ms = 0U;
    motor_status.run_id = 0U;
    motor_servo_stop_pending = 0U;
    motor_status.remote_link = 0U;
    motor_status.remote_armed = 0U;
    motor_status.remote_wait_neutral = 0U;
    motor_status.requested_permille = 0;
    motor_status.applied_permille = 0;
    motor_status.command_age_ms = 0U;
    motor_status.arm_generation = 0U;
    motor_reset_start_key();
    motor_initialized = 1U;
    __set_PRIMASK(interrupt_mask);
}

void motor_control_enable_test(void)
{
    uint32_t interrupt_mask = __get_PRIMASK();
    __disable_irq();
    if (motor_initialized && motor_config_is_valid() && !motor_status.ready)
    {
        motor_reset_start_key();
        motor_status.ready = 1U;
    }
    __set_PRIMASK(interrupt_mask);
}

static uint8_t motor_start_key_pressed(void)
{
    uint8_t sample = gpio_get_level(MOTOR_TEST_START_KEY_PIN);
    if (sample != motor_start_sample)
    {
        motor_start_sample = sample;
        motor_key_stable_ms = 0U;
    }
    else if (motor_key_stable_ms < MOTOR_TEST_KEY_DEBOUNCE_MS)
    {
        motor_key_stable_ms += 10U;
    }
    if (motor_key_stable_ms < MOTOR_TEST_KEY_DEBOUNCE_MS) return 0U;
    if (sample != MOTOR_TEST_KEY_ACTIVE_LEVEL)
    {
        motor_start_stable = sample;
        motor_start_armed = 1U;
    }
    else if (motor_start_stable != MOTOR_TEST_KEY_ACTIVE_LEVEL)
    {
        motor_start_stable = sample;
        if (motor_start_armed)
        {
            motor_start_armed = 0U;
            return 1U;
        }
    }
    return 0U;
}

#if !REMOTE_CONTROL_ENABLED
static void motor_begin_test(void)
{
    motor_status.state = MOTOR_TEST_CHANNEL1;
    motor_status.elapsed_ms = 0U;
    ++motor_status.run_id;
    motor_status.telemetry_visible = 1U;
    motor_phase_elapsed_ms = 0U;
    motor_result_remaining_ms = 0U;
    /* 只复位编码器软件累计；硬件计数器持续运行。必须先于非零PWM。 */
    encoder_feedback_reset_totals();
    motor_write_output(1U, 0U);
}

void motor_control_tick_10ms(void)
{
    if (!motor_initialized) return;
    if (motor_result_remaining_ms != 0U)
    {
        motor_result_remaining_ms -= 10U;
        if (motor_result_remaining_ms == 0U) motor_status.telemetry_visible = 0U;
    }

    /* 停止优先且不消抖。持续按住也只保持命令为零，不重复刷新结果保留时间。 */
    if (gpio_get_level(MOTOR_TEST_STOP_KEY_PIN) == MOTOR_TEST_KEY_ACTIVE_LEVEL)
    {
        motor_servo_stop_pending = 1U;
        motor_reset_start_key();
        if (motor_status.state != MOTOR_TEST_STOPPED && motor_status.state != MOTOR_TEST_CONFIG_ERROR)
            motor_finish(MOTOR_TEST_STOPPED);
        return;
    }
    if (!motor_status.ready) return;

    if (motor_start_key_pressed())
    {
        if (motor_test_is_running())
        {
            motor_finish(MOTOR_TEST_STOPPED);
            return;
        }
        if (motor_status.state == MOTOR_TEST_IDLE || motor_status.state == MOTOR_TEST_COMPLETE ||
            motor_status.state == MOTOR_TEST_STOPPED)
        {
            motor_begin_test();
            return;
        }
    }
    if (motor_test_is_running())
    {
        motor_status.elapsed_ms += 10U;
        motor_phase_elapsed_ms += 10U;
    }
    switch (motor_status.state)
    {
        case MOTOR_TEST_CHANNEL1:
            if (motor_phase_elapsed_ms >= MOTOR_TEST_CHANNEL_DURATION_MS)
            {
                motor_write_output(0U, 0U);
                motor_status.state = MOTOR_TEST_GAP1;
                motor_phase_elapsed_ms = 0U;
            }
            break;
        case MOTOR_TEST_GAP1:
            if (motor_phase_elapsed_ms >= MOTOR_TEST_GAP_DURATION_MS)
            {
                motor_write_output(0U, 1U);
                motor_status.state = MOTOR_TEST_CHANNEL2;
                motor_phase_elapsed_ms = 0U;
            }
            break;
        case MOTOR_TEST_CHANNEL2:
            if (motor_phase_elapsed_ms >= MOTOR_TEST_CHANNEL_DURATION_MS)
            {
                motor_write_output(0U, 0U);
                motor_status.state = MOTOR_TEST_GAP2;
                motor_phase_elapsed_ms = 0U;
            }
            break;
        case MOTOR_TEST_GAP2:
            if (motor_phase_elapsed_ms >= MOTOR_TEST_GAP_DURATION_MS)
            {
                motor_write_output(1U, 1U);
                motor_status.state = MOTOR_TEST_BOTH;
                motor_phase_elapsed_ms = 0U;
            }
            break;
        case MOTOR_TEST_BOTH:
            if (motor_phase_elapsed_ms >= MOTOR_TEST_CHANNEL_DURATION_MS)
                motor_finish(MOTOR_TEST_COMPLETE);
            break;
        case MOTOR_TEST_IDLE:
        case MOTOR_TEST_COMPLETE:
        case MOTOR_TEST_STOPPED:
        case MOTOR_TEST_CONFIG_ERROR:
            break;
        default:
            motor_write_output(0U, 0U);
            motor_status.state = MOTOR_TEST_CONFIG_ERROR;
            motor_status.ready = 0U;
            motor_status.telemetry_visible = 1U;
            motor_reset_start_key();
            break;
    }
}

#else
static motor_remote_input_t motor_remote_take_input(void)
{
    motor_remote_input_t input;
    uint32_t interrupt_mask = __get_PRIMASK();
    __disable_irq();
    input.generation = motor_remote_generation;
    input.throttle_permille = motor_remote_throttle;
    input.link = motor_remote_link;
    input.link_lost = motor_remote_link_lost;
    input.stop = motor_remote_stop;
    input.neutral_ack = motor_remote_neutral_ack;
    motor_remote_link_lost = 0U;
    motor_remote_stop = 0U;
    motor_remote_neutral_ack = 0U;
    __set_PRIMASK(interrupt_mask);
    input.fresh = input.generation != motor_remote_consumed_generation ? 1U : 0U;
    motor_remote_consumed_generation = input.generation;
    return input;
}

static void motor_remote_zero_output(void)
{
    /* 只在实际非零输出归零的时刻重置计时，后续停止不能抹去已累计的零输出间隔。 */
    if (motor_status.applied_permille != 0) motor_remote_zero_ms = 0U;
    pwm_set_duty(MOTOR_CHANNEL1_PWM, 0U);
    pwm_set_duty(MOTOR_CHANNEL2_PWM, 0U);
    motor_status.applied_permille = 0;
    motor_status.duty_permille = 0U;
}

static void motor_remote_disarm(void)
{
    motor_remote_zero_output();
    motor_status.requested_permille = 0;
    motor_status.command_age_ms = 0U;
    motor_status.remote_armed = 0U;
    motor_status.remote_wait_neutral = 0U;
    if (motor_status.state != MOTOR_TEST_CONFIG_ERROR) motor_status.state = MOTOR_REMOTE_DISARMED;
    motor_reset_start_key();
}

static void motor_remote_arm(void)
{
    motor_remote_zero_output();
    motor_status.requested_permille = 0;
    motor_status.command_age_ms = 0U;
    motor_status.remote_armed = 1U;
    motor_status.remote_wait_neutral = 1U;
    motor_status.state = MOTOR_REMOTE_WAIT_NEUTRAL;
    motor_status.elapsed_ms = 0U;
    ++motor_status.arm_generation;
    ++motor_status.run_id;
    encoder_feedback_reset_totals();
}

static void motor_remote_apply_request(void)
{
    int16_t requested = motor_status.requested_permille;
    int8_t direction = requested > 0 ? 1 : -1;
    uint16_t magnitude;
    uint32_t duty;
    if (motor_remote_last_direction != 0 && direction != motor_remote_last_direction)
    {
        /* 先实际归零再检查间隔；上一次零输出的历史不能为仍在转动的本次换向抵扣。 */
        if (motor_status.applied_permille != 0) motor_remote_zero_output();
        if (motor_remote_zero_ms < REMOTE_CONTROL_DIRECTION_GAP_MS)
        {
            motor_status.state = MOTOR_REMOTE_REVERSING;
            return;
        }
    }
    if (direction != motor_remote_last_direction)
    {
        /* 用户已确认两路参考HIGH均为前进；反向取反，切换时两路PWM都必须为零。 */
        gpio_set_level(MOTOR_CHANNEL1_DIRECTION_PIN, direction > 0 ?
            MOTOR_CHANNEL1_REFERENCE_LEVEL : (uint8_t)!MOTOR_CHANNEL1_REFERENCE_LEVEL);
        gpio_set_level(MOTOR_CHANNEL2_DIRECTION_PIN, direction > 0 ?
            MOTOR_CHANNEL2_REFERENCE_LEVEL : (uint8_t)!MOTOR_CHANNEL2_REFERENCE_LEVEL);
        motor_remote_last_direction = direction;
    }
    magnitude = requested > 0 ? (uint16_t)requested : (uint16_t)(-requested);
    duty = (uint32_t)((uint64_t)PWM_DUTY_MAX * magnitude / 1000U);
    if (motor_status.applied_permille != requested)
    {
        pwm_set_duty(MOTOR_CHANNEL1_PWM, duty);
        pwm_set_duty(MOTOR_CHANNEL2_PWM, duty);
    }
    motor_status.applied_permille = requested;
    motor_status.duty_permille = magnitude;
    motor_status.state = MOTOR_REMOTE_DRIVING;
}

void motor_control_tick_10ms(void)
{
    motor_remote_input_t input;
    if (!motor_initialized) return;
    input = motor_remote_take_input();
    motor_status.remote_link = input.link;
    if (motor_status.applied_permille == 0 && motor_remote_zero_ms < REMOTE_CONTROL_DIRECTION_GAP_MS)
    {
        uint32_t zero_ms = (uint32_t)motor_remote_zero_ms + 10U;
        motor_remote_zero_ms = zero_ms < REMOTE_CONTROL_DIRECTION_GAP_MS ?
            (uint16_t)zero_ms : (uint16_t)REMOTE_CONTROL_DIRECTION_GAP_MS;
    }

    /* S3原始电平先于武装、连接恢复和邮箱；同时清掉C12待决按下。 */
    if (gpio_get_level(MOTOR_TEST_STOP_KEY_PIN) == MOTOR_TEST_KEY_ACTIVE_LEVEL)
    {
        motor_servo_stop_pending = 1U;
        motor_remote_disarm();
        return;
    }
    if (input.link_lost || !input.link)
    {
        motor_remote_disarm();
        return;
    }
    if (!motor_status.ready) return;
    if (motor_start_key_pressed())
    {
        if (motor_status.remote_armed) motor_remote_disarm();
        else motor_remote_arm();
        /* 本次快照已消费：武装前或同一采样周期的旧Down/W/S都不能穿过此屏障。 */
        return;
    }
    if (!motor_status.remote_armed) return;
    motor_status.elapsed_ms += 10U;

    /* STOP单独锁存，不能被同一主循环批次后到的W/S覆盖；只允许以后的新命令再启动。 */
    if (input.stop)
    {
        motor_remote_zero_output();
        motor_status.requested_permille = 0;
        motor_status.command_age_ms = 0U;
        if (input.neutral_ack) motor_status.remote_wait_neutral = 0U;
        motor_status.state = motor_status.remote_wait_neutral ? MOTOR_REMOTE_WAIT_NEUTRAL : MOTOR_REMOTE_READY;
        return;
    }
    if (motor_status.remote_wait_neutral) return;

    if (input.fresh)
    {
        int16_t requested = input.throttle_permille;
        if (requested > REMOTE_CONTROL_MAX_DUTY_PERMILLE) requested = REMOTE_CONTROL_MAX_DUTY_PERMILLE;
        if (requested < -REMOTE_CONTROL_MAX_DUTY_PERMILLE) requested = -REMOTE_CONTROL_MAX_DUTY_PERMILLE;
        motor_status.requested_permille = requested;
        motor_status.command_age_ms = 0U;
    }
    else if (motor_status.requested_permille != 0)
    {
        uint32_t age_ms = (uint32_t)motor_status.command_age_ms + 10U;
        motor_status.command_age_ms = age_ms <= 65535U ? (uint16_t)age_ms : 65535U;
    }
    if (motor_status.requested_permille == 0) return;
    if (motor_status.command_age_ms >= REMOTE_CONTROL_DRIVE_LEASE_MS)
    {
        motor_remote_zero_output();
        motor_status.requested_permille = 0;
        motor_status.state = MOTOR_REMOTE_TIMEOUT;
        return;
    }
    motor_remote_apply_request();
}
#endif

void motor_control_remote_set_link(uint8_t available)
{
#if REMOTE_CONTROL_ENABLED
    uint32_t interrupt_mask = __get_PRIMASK();
    __disable_irq();
    available = available ? 1U : 0U;
    if (motor_remote_link != available)
    {
        if (!available) motor_remote_link_lost = 1U;
        motor_remote_link = available;
    }
    __set_PRIMASK(interrupt_mask);
#else
    (void)available;
#endif
}

void motor_control_submit_remote(int16_t throttle_permille, uint8_t neutral_ack)
{
#if REMOTE_CONTROL_ENABLED
    uint32_t interrupt_mask = __get_PRIMASK();
    __disable_irq();
    motor_remote_throttle = throttle_permille;
    ++motor_remote_generation;
    if (throttle_permille == 0)
    {
        motor_remote_stop = 1U;
        if (neutral_ack) motor_remote_neutral_ack = 1U;
    }
    __set_PRIMASK(interrupt_mask);
#else
    (void)throttle_permille;
    (void)neutral_ack;
#endif
}

uint8_t motor_control_take_servo_stop(void)
{
    uint8_t pending;
    uint32_t interrupt_mask = __get_PRIMASK();
    __disable_irq();
    pending = motor_servo_stop_pending;
    motor_servo_stop_pending = 0U;
    __set_PRIMASK(interrupt_mask);
    return pending;
}

void motor_control_get_status(motor_control_status_t *out)
{
    uint32_t interrupt_mask;
    if (out == 0) return;
    interrupt_mask = __get_PRIMASK();
    __disable_irq();
    *out = motor_status;
    __set_PRIMASK(interrupt_mask);
}
