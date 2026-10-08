#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "zf_driver_gpio.h"
#include "zf_driver_pwm.h"
#include "motor_control.h"
#include "remote_control_config.h"

/* 桩只替代硬件边界；真实后端必须在PIT内完成所有持续PWM/DIR写入。 */
static uint8_t start_key, stop_key, direction1, direction2, in_tick;
static uint32_t duty1, duty2, now_ms, zero_since_ms, interrupt_mask;
static unsigned writes, nonzero_writes, checks;
#define CHECK(c, m) do { ++checks; if (!(c)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, m); exit(1); } } while (0)

void gpio_init(gpio_pin_enum pin, gpio_dir_enum mode, uint8_t level, uint32_t config)
{
    (void)mode; (void)config;
    if (pin == C9) direction1 = level;
    if (pin == C7) direction2 = level;
}
uint8_t gpio_get_level(gpio_pin_enum pin)
{
    CHECK(pin == C12 || pin == C14, "后端只采样C12和S3");
    return pin == C12 ? start_key : stop_key;
}
void gpio_set_level(gpio_pin_enum pin, uint8_t level)
{
    uint8_t before = pin == C9 ? direction1 : direction2;
    CHECK(in_tick && !interrupt_mask, "DIR由未屏蔽的PIT唯一更新");
    CHECK(!duty1 && !duty2, "切DIR前必须双路PWM为零");
    if (before != level && nonzero_writes)
        CHECK(now_ms - zero_since_ms >= 100U, "切换方向前必须真实归零100ms");
    if (pin == C9) direction1 = level; else direction2 = level;
}
void pwm_init(pwm_channel_enum channel, uint32_t frequency, uint32_t duty)
{
    CHECK(frequency == 17000U && duty == 0U, "上电初始化零PWM");
    if (channel == PWM2_MODULE1_CHA_C8) duty1 = duty; else duty2 = duty;
}
void pwm_set_duty(pwm_channel_enum channel, uint32_t duty)
{
    uint8_t before = duty1 || duty2;
    CHECK(in_tick && !interrupt_mask, "持续PWM由未屏蔽的PIT唯一更新");
    CHECK((uint64_t)duty * 1000U <= (uint64_t)PWM_DUTY_MAX * 150U,
          "自动驾驶后端硬限幅为150千分比");
    if (channel == PWM2_MODULE1_CHA_C8) duty1 = duty; else duty2 = duty;
    if (before && !duty1 && !duty2) zero_since_ms = now_ms;
    if (duty) ++nonzero_writes;
    ++writes;
}
void encoder_feedback_reset_totals(void)
{
    CHECK(in_tick && !duty1 && !duty2, "编码器复位不可发生在驱动中");
}
uint32_t __get_PRIMASK(void) { return interrupt_mask; }
void __disable_irq(void) { interrupt_mask = 1U; }
void __set_PRIMASK(uint32_t value) { interrupt_mask = value; }

static motor_control_status_t status(void)
{
    motor_control_status_t out;
    motor_control_get_status(&out);
    return out;
}
static void tick(uint8_t active, uint16_t m1, uint16_t m2)
{
    unsigned before = writes;
    now_ms += 10U;
    in_tick = 1U;
    motor_control_autonomous_set(active, m1, m2);
    CHECK(writes == before, "目标提交不能直接写PWM");
    motor_control_tick_10ms();
    in_tick = 0U;
}
static void ticks(unsigned count, uint8_t active, uint16_t m1, uint16_t m2)
{
    while (count--) tick(active, m1, m2);
}
static void reset(void)
{
    start_key = stop_key = direction1 = direction2 = GPIO_HIGH;
    duty1 = duty2 = now_ms = zero_since_ms = interrupt_mask = 0U;
    writes = nonzero_writes = in_tick = 0U;
    motor_control_init();
}
#ifndef EXPECT_CONFIG_ERROR
static void physical_press(void)
{
    start_key = GPIO_HIGH; ticks(4U, 0U, 0U, 0U);
    start_key = GPIO_LOW; ticks(4U, 0U, 0U, 0U);
}
#endif
static void check_duty(uint16_t m1, uint16_t m2)
{
    if (duty1 != (uint32_t)((uint64_t)PWM_DUTY_MAX * m1 / 1000U) ||
        duty2 != (uint32_t)((uint64_t)PWM_DUTY_MAX * m2 / 1000U))
        fprintf(stderr, "t=%lu state=%d expected=%u/%u actual=%lu/%lu\n",
            (unsigned long)now_ms, (int)status().state, m1, m2,
            (unsigned long)duty1, (unsigned long)duty2);
    CHECK(duty1 == (uint32_t)((uint64_t)PWM_DUTY_MAX * m1 / 1000U), "M1输出正确");
    CHECK(duty2 == (uint32_t)((uint64_t)PWM_DUTY_MAX * m2 / 1000U), "M2输出正确");
    CHECK(status().channel1_duty_permille == m1 && status().channel2_duty_permille == m2,
          "状态分别报告双路实际输出");
}

#ifndef EXPECT_CONFIG_ERROR
static void test_auto_without_link_and_independent_clamp(void)
{
    reset();
    motor_control_enable_test();
    tick(1U, 70U, 120U);
    check_duty(70U, 120U);
    CHECK(status().autonomous_active && status().state == MOTOR_AUTONOMOUS_DRIVING,
          "自动输出持有明确所有权");
    CHECK(direction1 == GPIO_HIGH && direction2 == GPIO_HIGH, "自动驾驶仅前进");
    motor_control_remote_set_link(1U);
    motor_control_remote_set_link(0U);
    motor_control_submit_remote(-100, 0U);
    ticks(40U, 1U, 65535U, 30U);
    check_duty(150U, 30U);
    CHECK(!status().remote_armed, "auto占用期间不保留遥控武装");
    tick(1U, 0U, 0U);
    check_duty(0U, 0U);
    CHECK(status().autonomous_active && status().state == MOTOR_AUTONOMOUS_WAITING,
          "零目标仍持有自动所有权");
    tick(1U, 0U, 50U);
    check_duty(0U, 50U);
    tick(1U, 25U, 50U);
    check_duty(25U, 50U);
    tick(0U, 150U, 150U);
    check_duty(0U, 0U);
    CHECK(!status().autonomous_active, "退出立即释放所有权");
    ticks(20U, 0U, 0U, 0U);
    check_duty(0U, 0U);
}
static void test_startup_gate_and_stop_latches(void)
{
    reset();
    tick(1U, 100U, 100U);
    check_duty(0U, 0U);
    CHECK(!status().autonomous_active, "S3撤销自动所有权");
    tick(0U, 0U, 0U);
    motor_control_enable_test();
    tick(1U, 100U, 100U);
    stop_key = GPIO_LOW;
    tick(1U, 100U, 100U);
    check_duty(0U, 0U);
    stop_key = GPIO_HIGH;
    ticks(20U, 1U, 100U, 100U);
    check_duty(0U, 0U);
    CHECK(motor_control_take_servo_stop() == 1U, "S3停止事件仍交付舵机");
    tick(0U, 0U, 0U);
    tick(1U, 100U, 80U);
    check_duty(100U, 80U);
    start_key = GPIO_LOW;
    tick(1U, 100U, 80U);
    check_duty(0U, 0U);
    start_key = GPIO_HIGH;
    ticks(20U, 1U, 100U, 80U);
    check_duty(0U, 0U);
    tick(0U, 0U, 0U);
    tick(1U, 80U, 100U);
    check_duty(80U, 100U);
}
static void test_stop_beats_new_auto_and_local_mode_takeover(void)
{
    reset(); motor_control_enable_test();
    start_key = stop_key = GPIO_LOW;
    tick(1U, 150U, 150U);
    CHECK(nonzero_writes == 0U && motor_control_take_servo_stop() == 1U,
          "同周期S3优先于新auto目标及C12");
    start_key = stop_key = GPIO_HIGH;
    ticks(20U, 1U, 150U, 150U);
    CHECK(nonzero_writes == 0U, "S3释放不会启动被阻断的目标");
    tick(0U, 0U, 0U);
    motor_control_remote_set_link(1U);
    physical_press();
#if REMOTE_CONTROL_ENABLED
    motor_control_submit_remote(0, 1U); tick(0U, 0U, 0U);
    motor_control_submit_remote(100, 0U); tick(0U, 0U, 0U);
    check_duty(100U, 100U);
#else
    check_duty(100U, 0U);
#endif
    start_key = GPIO_HIGH;
    tick(1U, 50U, 75U);
    check_duty(50U, 75U);
    CHECK(!status().remote_armed, "接管撤销旧模式允许");
    tick(0U, 0U, 0U);
    ticks(1000U, 0U, 0U, 0U);
    check_duty(0U, 0U);
}
static void test_exit_discards_remote_and_requires_fresh_arm(void)
{
    reset();
    motor_control_enable_test();
    motor_control_remote_set_link(1U);
    tick(1U, 100U, 100U);
    motor_control_submit_remote(100, 0U);
    tick(0U, 0U, 0U);
    check_duty(0U, 0U);
    motor_control_submit_remote(0, 1U);
    tick(0U, 0U, 0U);
    motor_control_submit_remote(100, 0U);
    ticks(10U, 0U, 0U, 0U);
    check_duty(0U, 0U);
    physical_press();
#if REMOTE_CONTROL_ENABLED
    CHECK(status().remote_armed && status().remote_wait_neutral, "退出auto后新C12仍须新Down");
    motor_control_submit_remote(100, 0U); tick(0U, 0U, 0U);
    check_duty(0U, 0U);
    motor_control_submit_remote(0, 1U); tick(0U, 0U, 0U);
    motor_control_submit_remote(100, 0U); tick(0U, 0U, 0U);
    check_duty(100U, 100U);
#else
    check_duty(100U, 0U);
#endif
}
#if REMOTE_CONTROL_ENABLED
static void test_remote_reverse_to_auto_preserves_gap(void)
{
    reset(); motor_control_enable_test(); motor_control_remote_set_link(1U);
    physical_press();
    motor_control_submit_remote(0, 1U); tick(0U, 0U, 0U);
    motor_control_submit_remote(-100, 0U); tick(0U, 0U, 0U);
    check_duty(100U, 100U);
    CHECK(direction1 == GPIO_LOW && direction2 == GPIO_LOW, "测试确实建立遥控后退");
    start_key = GPIO_HIGH;
    tick(1U, 70U, 130U);
    check_duty(0U, 0U);
    ticks(9U, 1U, 70U, 130U);
    check_duty(0U, 0U);
    CHECK(status().autonomous_active && status().state == MOTOR_AUTONOMOUS_WAITING,
          "换向间隔期间维持自动所有权");
    tick(1U, 70U, 130U);
    check_duty(70U, 130U);
    CHECK(direction1 == GPIO_HIGH && direction2 == GPIO_HIGH, "归零100ms后才能切前进");
    tick(0U, 0U, 0U);
    physical_press();
    motor_control_submit_remote(0, 1U); tick(0U, 0U, 0U);
    motor_control_submit_remote(-100, 0U); tick(0U, 0U, 0U);
    /* 新C12消抖80ms、Down10ms及本次10ms已构成完整100ms零输出。 */
    check_duty(100U, 100U);
    ticks(20U, 0U, 0U, 0U);
    check_duty(100U, 100U);
}
#endif
#endif
int main(void)
{
#ifdef EXPECT_CONFIG_ERROR
    reset(); motor_control_enable_test(); ticks(40U, 1U, 150U, 150U);
    check_duty(0U, 0U);
    CHECK(status().state == MOTOR_TEST_CONFIG_ERROR, "非法周期保持配置错误");
    stop_key = GPIO_LOW; tick(1U, 150U, 150U);
    CHECK(motor_control_take_servo_stop() == 1U, "非法配置仍锁存S3");
#else
    test_auto_without_link_and_independent_clamp();
    test_startup_gate_and_stop_latches();
    test_stop_beats_new_auto_and_local_mode_takeover();
    test_exit_discards_remote_and_requires_fresh_arm();
#if REMOTE_CONTROL_ENABLED
    test_remote_reverse_to_auto_preserves_gap();
#endif
#endif
    printf("PASS: %u autonomous motor checks\n", checks);
    return 0;
}
