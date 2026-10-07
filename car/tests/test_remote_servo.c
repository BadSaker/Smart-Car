#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "zf_driver_gpio.h"
#include "zf_driver_pwm.h"
#include "servo_config.h"
#include "servo_control.h"

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
    CHECK(duty == 0U || (duty >= PWM_DUTY_MAX * 71U / 1000U &&
                        duty <= PWM_DUTY_MAX * 77U / 1000U),
          "hardware commands stay inside approved 1420..1540us range");
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

static void test_supply_and_stop_gates(void)
{
    unsigned disables;
    reset(0U);
    servo_control_remote_apply(0U, 1000, 10U);
    CHECK(pwm_starts == 0U && !status()->output_enabled,
          "disabled command cannot start PWM");
    servo_control_remote_apply(1U, 1000, 20U);
#if SERVO_SUPPLY_MV < 6000 || SERVO_SUPPLY_MV > 7400
    CHECK(status()->state == SERVO_CONTROL_SUPPLY_ERROR && pwm_starts == 0U && signal_is_low,
          "configured invalid supply rejects remote output");
    servo_control_remote_apply(1U, -1000, 10000U);
    CHECK(pwm_starts == 0U && commanded_duty == 0U,
          "repeated commands cannot override supply rejection");
#else
    CHECK(status()->state == SERVO_CONTROL_REMOTE && status()->output_enabled,
          "valid supply accepts explicit remote enable");
#endif
    stop_key = GPIO_LOW;
    servo_control_remote_apply(1U, -1000, 10001U);
    CHECK(status()->state == SERVO_CONTROL_STOPPED && !status()->output_enabled &&
          signal_is_low && commanded_duty == 0U,
          "raw S3 takes priority over remote command and supply state");
    disables = signal_disables;
    servo_control_remote_apply(1U, 1000, 10002U);
    CHECK(signal_disables == disables && !status()->output_enabled,
          "held S3 does not restart output or repeatedly rewrite stop hardware");
    stop_key = GPIO_HIGH;
    servo_control_remote_apply(0U, 1000, 10003U);
    servo_control_poll(11000U);
    CHECK(!status()->output_enabled && signal_is_low,
          "S3 release alone cannot resume a revoked remote command");
}

#if SERVO_SUPPLY_MV >= 6000 && SERVO_SUPPLY_MV <= 7400
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

/* 可发现符号翻转、输入溢出、归一化比例和端点限幅错误。 */
static void test_normalized_mapping_and_bounds(void)
{
    static const struct { int16_t steering; uint16_t pulse; } cases[] = {
        { INT16_MIN, 1420U }, { -1001, 1420U }, { -1000, 1420U },
        { -500, 1450U }, { -1, 1480U }, { 0, 1480U }, { 1, 1480U },
        { 500, 1510U }, { 1000, 1540U }, { 1001, 1540U }, { INT16_MAX, 1540U }
    };
    unsigned index;
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index)
    {
        uint32_t now_ms;
        uint16_t previous = 1480U;
        reset(0U);
        servo_control_remote_apply(1U, cases[index].steering, 0U);
        CHECK(status()->current_pulse_us == 1480U && status()->target_pulse_us == cases[index].pulse,
              "positive steering is left/larger pulse; negative is right/smaller pulse");
        for (now_ms = 20U; now_ms <= 600U; now_ms += 20U)
        {
            uint16_t current;
            unsigned difference;
            servo_control_remote_apply(1U, cases[index].steering, now_ms);
            current = status()->current_pulse_us;
            difference = current > previous ? current - previous : previous - current;
            CHECK(current >= 1420U && current <= 1540U && difference <= 2U,
                  "each remote movement respects approved pulse bounds and 2us slew");
            CHECK(status()->state == SERVO_CONTROL_REMOTE && status()->output_enabled,
                  "reaching target remains in remote mode");
            previous = current;
        }
        CHECK(status()->current_pulse_us == cases[index].pulse && pwm_starts == 1U,
              "remote command reaches the clamped target without restarting PWM");
    }
}

/* 可发现首次跳到目标、同一时刻多步、迟到追赶和目标替换重置时钟。 */
static void test_center_slew_no_backlog_and_target_replacement(void)
{
    unsigned updates;
    reset(100U);
    servo_control_remote_apply(1U, 1000, 100U);
    CHECK(status()->current_pulse_us == 1480U && pwm_updates == 0U,
          "first enable initializes center before any motion");
    servo_control_remote_apply(1U, 1000, 119U);
    CHECK(status()->current_pulse_us == 1480U, "no movement before 20ms");
    servo_control_remote_apply(1U, 1000, 120U);
    CHECK(status()->current_pulse_us == 1482U, "exact 20ms deadline advances only 2us");
    updates = pwm_updates;
    servo_control_remote_apply(1U, -1000, 120U);
    servo_control_remote_apply(1U, -1000, 139U);
    CHECK(status()->current_pulse_us == 1482U && status()->target_pulse_us == 1420U &&
          pwm_updates == updates, "new target changes direction without an early extra step");
    servo_control_remote_apply(1U, -1000, 140U);
    CHECK(status()->current_pulse_us == 1480U, "target changes do not starve scheduled steps");
    servo_control_remote_apply(1U, -1000, 10000U);
    CHECK(status()->current_pulse_us == 1478U, "late service produces one step without catch-up");
    updates = pwm_updates;
    servo_control_remote_apply(1U, -1000, 10000U);
    servo_control_remote_apply(1U, -1000, 10019U);
    CHECK(status()->current_pulse_us == 1478U && pwm_updates == updates,
          "late step resets timing; same timestamp cannot consume backlog");
    servo_control_remote_apply(1U, -1000, 10020U);
    CHECK(status()->current_pulse_us == 1476U, "normal timing resumes after late service");
    servo_control_remote_apply(1U, 0, 10040U);
    servo_control_remote_apply(1U, 0, 10060U);
    CHECK(status()->current_pulse_us == 1480U && status()->state == SERVO_CONTROL_REMOTE,
          "replacement neutral target stops at calibrated center");
}

static void test_clock_wrap(void)
{
    uint32_t now_ms = UINT32_MAX - 10U;
    reset(now_ms);
    servo_control_remote_apply(1U, 1000, now_ms);
    servo_control_remote_apply(1U, 1000, now_ms + 19U);
    CHECK(status()->current_pulse_us == 1480U, "wrap does not shorten 20ms interval");
    servo_control_remote_apply(1U, 1000, now_ms + 20U);
    CHECK(status()->current_pulse_us == 1482U, "clock wrap preserves first step timing");
    servo_control_remote_apply(1U, -1000, now_ms + 40U);
    CHECK(status()->current_pulse_us == 1480U, "clock wrap preserves target replacement");
}

static void test_disable_without_ownership_preserves_bench(void)
{
    unsigned disables;
    reset(0U);
    disables = signal_disables;
    servo_control_remote_apply(0U, 1000, 0U);
    CHECK(status()->state == SERVO_CONTROL_IDLE && signal_disables == disables,
          "inactive remote adapter leaves idle bench state intact");
    (void)press_after_release(0U);
    CHECK(status()->state == SERVO_CONTROL_CENTER && pwm_starts == 1U,
          "legacy physical S2 still starts the bench test");
    servo_control_remote_apply(0U, -1000, 62U);
    CHECK(status()->state == SERVO_CONTROL_CENTER && status()->output_enabled &&
          signal_disables == disables, "disabled remote never clobbers an unowned bench test");
    stop_key = GPIO_LOW;
    servo_control_remote_apply(0U, 0, 63U);
    CHECK(status()->state == SERVO_CONTROL_STOPPED && !status()->output_enabled,
          "raw S3 stops the bench even when remote enable is false");
}

static void test_remote_ownership_blocks_bench_and_pending_press(void)
{
    unsigned disables;
    reset(0U);
    servo_control_poll(30U);
    start_key = GPIO_LOW;
    servo_control_poll(31U);
    servo_control_remote_apply(1U, 1000, 32U);
    servo_control_poll(61U);
    CHECK(status()->state == SERVO_CONTROL_REMOTE && pwm_starts == 1U,
          "pending S2 debounce cannot replace remote ownership");
    CHECK(servo_control_start_test(62U) == SERVO_CONTROL_START_BUSY,
          "public bench start cannot bypass remote ownership");
    (void)press_after_release(70U);
    servo_control_poll(10000U);
    CHECK(status()->state == SERVO_CONTROL_REMOTE && pwm_starts == 1U &&
          status()->current_pulse_us == 1480U,
          "legacy poll neither sweeps nor advances a remote command");
    servo_control_remote_apply(0U, 1000, 10001U);
    CHECK(status()->state == SERVO_CONTROL_STOPPED && signal_is_low &&
          status()->current_pulse_us == 0U && status()->target_pulse_us == 0U,
          "remote disable cancels output and clears command snapshot");
    disables = signal_disables;
    servo_control_remote_apply(0U, 1000, 10002U);
    servo_control_poll(10100U);
    CHECK(signal_disables == disables && pwm_starts == 1U && !status()->output_enabled,
          "repeated disable and held S2 cannot resume output after handoff");
    (void)press_after_release(10200U);
    CHECK(pwm_starts == 2U && status()->state == SERVO_CONTROL_CENTER,
          "fresh physical release/press works after remote ownership ends");
}

static void test_remote_takes_over_running_bench_at_center(void)
{
    uint32_t started_ms;
    reset(0U);
    started_ms = press_after_release(0U);
    servo_control_poll(started_ms + 500U);
    servo_control_poll(started_ms + 520U);
    CHECK(status()->current_pulse_us < 1480U, "bench is moving before takeover");
    servo_control_remote_apply(1U, 1000, started_ms + 521U);
    CHECK(status()->state == SERVO_CONTROL_REMOTE && status()->current_pulse_us == 1480U &&
          status()->target_pulse_us == 1540U && pwm_starts == 2U,
          "first remote enable takes ownership at center even after a bench run");
    servo_control_stop();
    servo_control_poll(started_ms + 600U);
    CHECK(status()->state == SERVO_CONTROL_STOPPED && pwm_starts == 2U,
          "explicit stop clears remote ownership and pending bench activation");
    servo_control_remote_apply(1U, -1000, started_ms + 601U);
    CHECK(status()->state == SERVO_CONTROL_REMOTE && status()->current_pulse_us == 1480U &&
          status()->target_pulse_us == 1420U && pwm_starts == 3U,
          "a new explicit remote authorization restarts from center");
}

static void test_local_stop_cannot_queue_held_s2(void)
{
    reset(0U);
    start_key = GPIO_LOW;
    servo_control_remote_apply(1U, 1000, 0U);
    stop_key = GPIO_LOW;
    servo_control_poll(1U);
    CHECK(status()->state == SERVO_CONTROL_STOPPED && !status()->output_enabled,
          "legacy poll retains immediate S3 priority while remote owns output");
    CHECK(servo_control_start_test(2U) == SERVO_CONTROL_START_STOP_HELD,
          "physical stop has priority over bench busy checks");
    stop_key = GPIO_HIGH;
    servo_control_remote_apply(0U, 1000, 3U);
    servo_control_poll(1000U);
    CHECK(pwm_starts == 1U && signal_is_low, "releasing S3 does not reuse held S2");
    (void)press_after_release(1100U);
    CHECK(pwm_starts == 2U, "only a new S2 release/press can restart bench output");
}
#endif

int main(void)
{
    test_supply_and_stop_gates();
#if SERVO_SUPPLY_MV >= 6000 && SERVO_SUPPLY_MV <= 7400
    test_normalized_mapping_and_bounds();
    test_center_slew_no_backlog_and_target_replacement();
    test_clock_wrap();
    test_disable_without_ownership_preserves_bench();
    test_remote_ownership_blocks_bench_and_pending_press();
    test_remote_takes_over_running_bench_at_center();
    test_local_stop_cannot_queue_held_s2();
#endif
    printf("PASS: %u checks\n", checks);
    return 0;
}
