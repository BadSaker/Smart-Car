#include "servo_control.h"
#include "servo_config.h"
#include "autonomous_config.h"
#include "zf_driver_gpio.h"
#include "zf_driver_pwm.h"

static servo_control_status_t servo_status;
static uint32_t servo_phase_started_ms;
static uint32_t servo_last_step_ms;
static uint32_t servo_key_changed_ms;
static uint8_t servo_start_sample;
static uint8_t servo_start_stable;
static uint8_t servo_start_armed;
static uint8_t servo_pwm_initialized;
static uint8_t servo_remote_owned;

static uint8_t servo_supply_is_valid(void)
{
    return (SERVO_SUPPLY_MV >= SERVO_SUPPLY_MIN_MV &&
            SERVO_SUPPLY_MV <= SERVO_SUPPLY_MAX_MV) ? 1U : 0U;
}

static uint32_t servo_pulse_to_duty(uint16_t pulse_us)
{
    uint64_t duty;

    /* 只允许参考测试窗口；使用库的占空比尺度，不套用全行程角度公式。 */
    if (pulse_us < SERVO_TEST_MIN_PULSE_US) pulse_us = SERVO_TEST_MIN_PULSE_US;
    if (pulse_us > SERVO_TEST_MAX_PULSE_US) pulse_us = SERVO_TEST_MAX_PULSE_US;
    duty = ((uint64_t)pulse_us * SERVO_PWM_FREQUENCY_HZ * PWM_DUTY_MAX + 500000U) / 1000000U;
    if (duty > PWM_DUTY_MAX) duty = PWM_DUTY_MAX;
    return (uint32_t)duty;
}

static void servo_disable_output(void)
{
    /* 库的零占空比经PWM整周期重载；切回低电平GPIO不等这个重载。
     * C30由本模块独占，仍有舵机供电，不能等同硬件急停或切断转矩。 */
    if (servo_pwm_initialized) pwm_set_duty(SERVO_PWM_CHANNEL, 0U);
    gpio_init(SERVO_SIGNAL_PIN, GPO, GPIO_LOW, GPO_PUSH_PULL);
    servo_status.output_enabled = 0U;
    servo_status.current_pulse_us = 0U;
    servo_status.target_pulse_us = 0U;
}

static uint8_t servo_test_is_running(void)
{
    return (servo_status.state >= SERVO_CONTROL_CENTER &&
            servo_status.state <= SERVO_CONTROL_RETURN_CENTER) ? 1U : 0U;
}

void servo_control_init(uint32_t now_ms)
{
    servo_pwm_initialized = 0U;
    servo_remote_owned = 0U;
    servo_disable_output();
    gpio_init(SERVO_START_KEY_PIN, GPI, GPIO_HIGH, GPI_PULL_UP);
    gpio_init(SERVO_STOP_KEY_PIN, GPI, GPIO_HIGH, GPI_PULL_UP);
    servo_start_sample = gpio_get_level(SERVO_START_KEY_PIN);
    /* 即便上电时按键已抬起，也先观察30ms稳定释放；按住上电不启动。 */
    servo_start_stable = SERVO_KEY_ACTIVE_LEVEL;
    servo_start_armed = 0U;
    servo_key_changed_ms = now_ms;
    servo_phase_started_ms = now_ms;
    servo_last_step_ms = now_ms;
    servo_status.state = servo_supply_is_valid() ? SERVO_CONTROL_IDLE : SERVO_CONTROL_SUPPLY_ERROR;
}

servo_control_result_t servo_control_start_test(uint32_t now_ms)
{
    if (gpio_get_level(SERVO_STOP_KEY_PIN) == SERVO_KEY_ACTIVE_LEVEL)
    {
        servo_control_stop();
        return SERVO_CONTROL_START_STOP_HELD;
    }
    if (!servo_supply_is_valid())
    {
        servo_disable_output();
        servo_start_armed = 0U;
        servo_status.state = SERVO_CONTROL_SUPPLY_ERROR;
        return SERVO_CONTROL_START_SUPPLY_ERROR;
    }
    if (servo_remote_owned) return SERVO_CONTROL_START_BUSY;
    if (!servo_start_armed || servo_start_stable != SERVO_KEY_ACTIVE_LEVEL ||
        servo_start_sample != SERVO_KEY_ACTIVE_LEVEL ||
        gpio_get_level(SERVO_START_KEY_PIN) != SERVO_KEY_ACTIVE_LEVEL)
    {
        return SERVO_CONTROL_START_NOT_ARMED;
    }
    /* 忙时也消费本次按下，不能把它排队为结束后的自动重试。 */
    servo_start_armed = 0U;
    if (servo_test_is_running()) return SERVO_CONTROL_START_BUSY;

    pwm_init(SERVO_PWM_CHANNEL, SERVO_PWM_FREQUENCY_HZ,
             servo_pulse_to_duty(SERVO_REFERENCE_CENTER_US));
    servo_pwm_initialized = 1U;
    servo_status.current_pulse_us = SERVO_REFERENCE_CENTER_US;
    servo_status.target_pulse_us = SERVO_REFERENCE_CENTER_US;
    servo_status.output_enabled = 1U;
    servo_status.state = SERVO_CONTROL_CENTER;
    servo_phase_started_ms = now_ms;
    servo_last_step_ms = now_ms;
    return SERVO_CONTROL_START_OK;
}

void servo_control_stop(void)
{
    servo_disable_output();
    servo_remote_owned = 0U;
    servo_status.state = SERVO_CONTROL_STOPPED;
    servo_start_armed = 0U;
    /* 撤销尚未消抖完成的按下；下一次必须重新观察稳定释放。 */
    servo_start_sample = SERVO_KEY_ACTIVE_LEVEL;
    servo_start_stable = SERVO_KEY_ACTIVE_LEVEL;
}

static void servo_begin_motion(servo_control_state_t state, uint16_t target_us, uint32_t now_ms)
{
    servo_status.state = state;
    servo_status.target_pulse_us = target_us;
    servo_last_step_ms = now_ms;
}

static void servo_advance_motion(uint32_t now_ms, uint16_t step_us, uint32_t interval_ms)
{
    uint16_t current = servo_status.current_pulse_us;
    uint16_t target = servo_status.target_pulse_us;

    if ((uint32_t)(now_ms - servo_last_step_ms) < interval_ms) return;
    /* 一次调用最多走一步，迟到时从现在重计时，不追赶积压产生突跳。 */
    servo_last_step_ms = now_ms;
    if (current < target)
    {
        current = ((uint32_t)(target - current) <= step_us) ?
                  target : (uint16_t)(current + step_us);
    }
    else if (current > target)
    {
        current = ((uint32_t)(current - target) <= step_us) ?
                  target : (uint16_t)(current - step_us);
    }
    servo_status.current_pulse_us = current;
    pwm_set_duty(SERVO_PWM_CHANNEL, servo_pulse_to_duty(current));
    if (current == target && servo_status.state != SERVO_CONTROL_REMOTE && servo_status.state != SERVO_CONTROL_AUTONOMOUS)
    {
        servo_phase_started_ms = now_ms;
        if (servo_status.state == SERVO_CONTROL_TO_LOW) servo_status.state = SERVO_CONTROL_HOLD_LOW;
        else if (servo_status.state == SERVO_CONTROL_TO_HIGH) servo_status.state = SERVO_CONTROL_HOLD_HIGH;
        else servo_status.state = SERVO_CONTROL_COMPLETE;
    }
}

void servo_control_poll(uint32_t now_ms)
{
    uint8_t start_sample;

    /* 停止不等消抖，宁可因抖动停止；不能让同次启动事件抢先产生PWM。 */
    if (gpio_get_level(SERVO_STOP_KEY_PIN) == SERVO_KEY_ACTIVE_LEVEL)
    {
        if (servo_status.state != SERVO_CONTROL_STOPPED) servo_control_stop();
        return;
    }
    /* 遥控期间不采集S2启动事件，也不由台架poll推进遥控轨迹。 */
    if (servo_remote_owned) return;

    start_sample = gpio_get_level(SERVO_START_KEY_PIN);
    if (start_sample != servo_start_sample)
    {
        servo_start_sample = start_sample;
        servo_key_changed_ms = now_ms;
    }
    if ((uint32_t)(now_ms - servo_key_changed_ms) >= SERVO_KEY_DEBOUNCE_MS)
    {
        if (start_sample != SERVO_KEY_ACTIVE_LEVEL)
        {
            servo_start_stable = start_sample;
            servo_start_armed = 1U;
        }
        else if (servo_start_stable != SERVO_KEY_ACTIVE_LEVEL)
        {
            servo_start_stable = start_sample;
            (void)servo_control_start_test(now_ms);
        }
    }

    switch (servo_status.state)
    {
        case SERVO_CONTROL_CENTER:
            if ((uint32_t)(now_ms - servo_phase_started_ms) >= SERVO_TEST_CENTER_HOLD_MS)
                servo_begin_motion(SERVO_CONTROL_TO_LOW, SERVO_TEST_MIN_PULSE_US, now_ms);
            break;
        case SERVO_CONTROL_HOLD_LOW:
            if ((uint32_t)(now_ms - servo_phase_started_ms) >= SERVO_TEST_ENDPOINT_HOLD_MS)
                servo_begin_motion(SERVO_CONTROL_TO_HIGH, SERVO_TEST_MAX_PULSE_US, now_ms);
            break;
        case SERVO_CONTROL_HOLD_HIGH:
            if ((uint32_t)(now_ms - servo_phase_started_ms) >= SERVO_TEST_ENDPOINT_HOLD_MS)
                servo_begin_motion(SERVO_CONTROL_RETURN_CENTER, SERVO_REFERENCE_CENTER_US, now_ms);
            break;
        case SERVO_CONTROL_TO_LOW:
        case SERVO_CONTROL_TO_HIGH:
        case SERVO_CONTROL_RETURN_CENTER:
            servo_advance_motion(now_ms, SERVO_TEST_STEP_US, SERVO_TEST_STEP_INTERVAL_MS);
            break;
        default:
            break;
    }
}

const servo_control_status_t *servo_control_get_status(void)
{
    return &servo_status;
}

static void servo_control_apply(uint8_t enabled, int16_t steering_permille, uint32_t now_ms, uint8_t owner)
{
    int32_t target_us;

    /* 无论远端是否启用，本地停止原始低电平始终优先。 */
    if (gpio_get_level(SERVO_STOP_KEY_PIN) == SERVO_KEY_ACTIVE_LEVEL)
    {
        if (servo_status.state != SERVO_CONTROL_STOPPED) servo_control_stop();
        return;
    }
    if (!enabled)
    {
        /* 未占用时不能清除台架测试；占用结束后也不重复改写硬件。 */
        if (servo_remote_owned == owner) servo_control_stop();
        return;
    }
    if (!servo_supply_is_valid())
    {
        if (servo_status.state != SERVO_CONTROL_SUPPLY_ERROR || servo_remote_owned)
            servo_control_stop();
        servo_status.state = SERVO_CONTROL_SUPPLY_ERROR;
        return;
    }

    if (steering_permille < -1000) steering_permille = -1000;
    if (steering_permille > 1000) steering_permille = 1000;
    /* 用户已确认：大脉宽向左，小脉宽向右；用有符号乘法避免负输入溢出。 */
    target_us = (int32_t)SERVO_REFERENCE_CENTER_US +
                (int32_t)SERVO_TEST_EXCURSION_US * steering_permille / 1000;
    if (target_us < (int32_t)SERVO_TEST_MIN_PULSE_US) target_us = SERVO_TEST_MIN_PULSE_US;
    if (target_us > (int32_t)SERVO_TEST_MAX_PULSE_US) target_us = SERVO_TEST_MAX_PULSE_US;

    if (!servo_remote_owned)
    {
        /* 模式交接撤销旧S2事件；首次PWM只输出中位，不直接跳至遥控目标。 */
        servo_start_armed = 0U;
        servo_start_sample = SERVO_KEY_ACTIVE_LEVEL;
        servo_start_stable = SERVO_KEY_ACTIVE_LEVEL;
        pwm_init(SERVO_PWM_CHANNEL, SERVO_PWM_FREQUENCY_HZ,
                 servo_pulse_to_duty(SERVO_REFERENCE_CENTER_US));
        servo_pwm_initialized = 1U;
        servo_remote_owned = owner;
        servo_status.state = owner == 2U ? SERVO_CONTROL_AUTONOMOUS : SERVO_CONTROL_REMOTE;
        servo_status.current_pulse_us = SERVO_REFERENCE_CENTER_US;
        servo_status.target_pulse_us = (uint16_t)target_us;
        servo_status.output_enabled = 1U;
        servo_last_step_ms = now_ms;
        return;
    }

    servo_status.target_pulse_us = (uint16_t)target_us;
    /* 遥控固定2us/20ms，不受台架试验步长变化影响；新目标不重置步进时钟。 */
    servo_advance_motion(now_ms, owner == 2U ? AUTO_SERVO_STEP_US : 2U,
                         owner == 2U ? AUTO_SERVO_INTERVAL_MS : 20U);
}

/* 自动驾驶由主循环所有权门控，遥控接口无权覆盖其转向。 */
void servo_control_remote_apply(uint8_t enabled, int16_t steering_permille, uint32_t now_ms)
{
    if (servo_remote_owned == 2U) return;
    servo_control_apply(enabled, steering_permille, now_ms, 1U);
}
void servo_control_autonomous_apply(uint8_t enabled, int16_t steering_permille, uint32_t now_ms)
{
    if (enabled && servo_remote_owned == 1U) servo_control_stop();
    servo_control_apply(enabled, steering_permille, now_ms, 2U);
}
