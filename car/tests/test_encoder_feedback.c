#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "encoder_feedback.h"
#include "zf_driver_encoder.h"

static uint16_t hardware_counts[2];
static uint32_t irq_mask;
static unsigned int init_calls[2];
static unsigned int clear_calls[2];
static unsigned int read_calls;
static unsigned int get_mask_calls;
static unsigned int disable_calls;
static unsigned int restore_calls;
static uint16_t after_read_increment[2];
static uint8_t tick_before_disable;
static uint8_t tick_after_restore;

#ifdef TEST_RATIO_ONE_TO_ONE
#define TEST_COUNTS_PER_WHEEL_REV 1024
#define TEST_RATE_TIE_COUNTS 16
#define TEST_SPEED_TIE_COUNTS 2560
#else
#define TEST_COUNTS_PER_WHEEL_REV 4352
#define TEST_RATE_TIE_COUNTS 68
#define TEST_SPEED_TIE_COUNTS 10880
#endif

static void check(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static unsigned int channel_index(encoder_index_enum channel)
{
    check(channel == QTIMER1_ENCODER1 || channel == QTIMER1_ENCODER2,
          "unexpected encoder channel");
    return channel == QTIMER1_ENCODER1 ? 0U : 1U;
}

int16 encoder_get_count(encoder_index_enum channel)
{
    unsigned int index = channel_index(channel);
    uint16_t raw = hardware_counts[index];
    ++read_calls;
    /* 模拟读取寄存器刚结束便到来的边沿，下一采样必须保留。 */
    hardware_counts[index] = (uint16_t)(raw + after_read_increment[index]);
    after_read_increment[index] = 0U;
    return (int16_t)(raw <= 32767U ? (int32_t)raw : (int32_t)raw - 65536);
}

void encoder_clear_count(encoder_index_enum channel)
{
    unsigned int index = channel_index(channel);
    ++clear_calls[index];
    hardware_counts[index] = 0U;
}

void encoder_dir_init(encoder_index_enum channel, encoder_channel1_enum step,
                      encoder_channel2_enum direction)
{
    unsigned int index = channel_index(channel);
    if (index == 0U)
        check(step == QTIMER1_ENCODER1_CH1_C0 && direction == QTIMER1_ENCODER1_CH2_C1,
              "left ENC1 must use C0 STEP / C1 DIR");
    else
        check(step == QTIMER1_ENCODER2_CH1_C2 && direction == QTIMER1_ENCODER2_CH2_C24,
              "right ENC2 must use C2 STEP / C24 DIR");
    ++init_calls[index];
}

uint32_t __get_PRIMASK(void)
{
    ++get_mask_calls;
    return irq_mask;
}

void __disable_irq(void)
{
    ++disable_calls;
    /* 模拟屏蔽生效前最后一次PIT更新，快照必须能看到这一拍。 */
    if (tick_before_disable)
    {
        tick_before_disable = 0U;
        hardware_counts[0] = (uint16_t)(hardware_counts[0] + 7U);
        encoder_feedback_tick_10ms();
    }
    irq_mask = 1U;
}

void __set_PRIMASK(uint32_t value)
{
    check(irq_mask == 1U, "snapshot must disable interrupts before restore");
    ++restore_calls;
    irq_mask = value;
    /* 模拟恢复中断后紧接着来的PIT，已复制的快照不得被污染。 */
    if (tick_after_restore && irq_mask == 0U)
    {
        tick_after_restore = 0U;
        hardware_counts[0] = (uint16_t)(hardware_counts[0] + 9U);
        encoder_feedback_tick_10ms();
    }
}

static encoder_feedback_snapshot_t sample(int32_t left_delta, int32_t right_delta)
{
    encoder_feedback_snapshot_t snapshot;
    hardware_counts[0] = (uint16_t)(hardware_counts[0] + left_delta);
    hardware_counts[1] = (uint16_t)(hardware_counts[1] + right_delta);
    encoder_feedback_tick_10ms();
    encoder_feedback_get_snapshot(&snapshot);
    return snapshot;
}

static void test_init(void)
{
    encoder_feedback_snapshot_t snapshot;
    hardware_counts[0] = 4321U;
    hardware_counts[1] = 5432U;
    encoder_feedback_init();
    encoder_feedback_get_snapshot(&snapshot);
    check(init_calls[0] == 1U && init_calls[1] == 1U, "initialize both direction encoders");
    check(clear_calls[0] == 1U && clear_calls[1] == 1U, "clear hardware only at startup");
    check(snapshot.left.total_counts == 0 && snapshot.right.total_counts == 0,
          "startup clears software totals");
    check(snapshot.sample_sequence == 0U, "startup sequence is zero");
    snapshot = sample(0, 0);
    check(snapshot.left.delta_counts == 0 && snapshot.right.delta_counts == 0,
          "startup baseline must not become movement");
}

static void test_raw(void)
{
    encoder_feedback_snapshot_t snapshot;
    encoder_feedback_init();
    hardware_counts[0] = 12U;
    hardware_counts[1] = 65529U;
    encoder_feedback_tick_10ms();
    encoder_feedback_get_snapshot(&snapshot);
    check(snapshot.left.delta_counts == 12, "left raw +12 must remain +12");
    check(snapshot.right.delta_counts == -7, "right raw -7 must remain -7");
    check(snapshot.left.total_counts == 12 && snapshot.right.total_counts == -7,
          "raw counts accumulate with hardware signs");
    check(snapshot.left.counts_per_second == 1200 && snapshot.right.counts_per_second == -700,
          "10ms counts convert to counts per second");
    snapshot = sample(-20, 20);
    check(snapshot.left.delta_counts == -20 && snapshot.right.delta_counts == 20,
          "crossing zero wraps in both directions");
    check(snapshot.left.total_counts == -8 && snapshot.right.total_counts == 13,
          "reverse direction updates accumulated totals");
    snapshot = sample(32767, -32767);
    check(snapshot.left.delta_counts == 32767 && snapshot.right.delta_counts == -32767,
          "largest unambiguous signed deltas");
    snapshot = sample(10, -16);
    check(snapshot.left.delta_counts == 10 && snapshot.right.delta_counts == -16,
          "crossing signed counter boundary preserves small delta");
    snapshot = sample(0, 0);
    check(snapshot.left.delta_counts == 0 && snapshot.right.delta_counts == 0,
          "stationary delta is zero");
    check(snapshot.left.counts_per_second == 0 && snapshot.right.wheel_rpm_x10 == 0 &&
          snapshot.left.speed_mm_s == 0, "stationary rates are zero");
    snapshot = sample(32768, -32768);
    check(snapshot.left.delta_counts == -32768 && snapshot.right.delta_counts == -32768,
          "exact half-range uses documented negative tie policy");
    check(snapshot.sample_sequence == 6U, "one sequence increment per sample");
    check(clear_calls[0] == 1U && clear_calls[1] == 1U, "ticks never clear hardware");
}

static void test_rates(void)
{
    encoder_feedback_snapshot_t snapshot;
    int32_t sum_counts_per_second = 0;
    int32_t sum_rpm_x10 = 0;
    int32_t sum_speed_mm_s = 0;
    int tick;
    encoder_feedback_init();
    snapshot = sample(TEST_COUNTS_PER_WHEEL_REV, -TEST_COUNTS_PER_WHEEL_REV);
    check(snapshot.left.wheel_rpm_x10 == 60000 && snapshot.right.wheel_rpm_x10 == -60000,
          "one wheel turn in 10ms is signed 6000 rpm");
    check(snapshot.left.speed_mm_s == 20106 && snapshot.right.speed_mm_s == -20106,
          "64mm wheel circumference converts signed speed");
    snapshot = sample(TEST_RATE_TIE_COUNTS, -TEST_RATE_TIE_COUNTS);
    check(snapshot.left.wheel_rpm_x10 == 938 && snapshot.right.wheel_rpm_x10 == -938,
          "rpm ties round away from zero symmetrically");
    snapshot = sample(TEST_SPEED_TIE_COUNTS, -TEST_SPEED_TIE_COUNTS);
    check(snapshot.left.speed_mm_s == 50266 && snapshot.right.speed_mm_s == -50266,
          "speed ties round away from zero symmetrically");
    encoder_feedback_reset_totals();
    for (tick = 0; tick < 100; ++tick)
    {
#ifdef TEST_RATIO_ONE_TO_ONE
        int counts = tick < 24 ? 11 : 10;
#else
        int counts = tick < 52 ? 44 : 43;
#endif
        snapshot = sample(counts, -counts);
        sum_counts_per_second += snapshot.left.counts_per_second;
        sum_rpm_x10 += snapshot.left.wheel_rpm_x10;
        sum_speed_mm_s += snapshot.left.speed_mm_s;
    }
    check(snapshot.left.total_counts == TEST_COUNTS_PER_WHEEL_REV &&
          snapshot.right.total_counts == -TEST_COUNTS_PER_WHEEL_REV,
          "one second contains one wheel revolution");
    check(sum_counts_per_second / 100 == TEST_COUNTS_PER_WHEEL_REV,
          "one-second mean count rate matches the wheel revolution fixture");
    check((sum_rpm_x10 + 50) / 100 == 600, "one wheel revolution per second is 60 rpm");
    check((sum_speed_mm_s + 50) / 100 == 201, "one wheel revolution per second is 201 mm/s");
}

static void test_reset(void)
{
    encoder_feedback_snapshot_t snapshot;
    unsigned int reads;
    encoder_feedback_init();
    snapshot = sample(80, -90);
    hardware_counts[0] = 54321U;
    hardware_counts[1] = 12345U;
    reads = read_calls;
    encoder_feedback_reset_totals();
    encoder_feedback_get_snapshot(&snapshot);
    check(read_calls == reads + 2U, "reset captures both current hardware baselines");
    check(hardware_counts[0] == 54321U && hardware_counts[1] == 12345U,
          "reset must not clear running hardware counters");
    check(snapshot.left.total_counts == 0 && snapshot.right.total_counts == 0 &&
          snapshot.left.delta_counts == 0 && snapshot.right.delta_counts == 0,
          "reset clears totals and the previous sampling window");
    check(snapshot.left.counts_per_second == 0 && snapshot.right.wheel_rpm_x10 == 0 &&
          snapshot.left.speed_mm_s == 0, "reset clears old instantaneous rates");
    check(snapshot.sample_sequence == 1U, "reset preserves sample sequence");
    snapshot = sample(3, -4);
    check(snapshot.left.total_counts == 3 && snapshot.right.total_counts == -4,
          "post-reset sample is relative to reset baseline");
    check(clear_calls[0] == 1U && clear_calls[1] == 1U, "reset never clears hardware");
}

static void test_read_edge(void)
{
    encoder_feedback_snapshot_t snapshot;
    encoder_feedback_init();
    hardware_counts[0] = 10U;
    hardware_counts[1] = 20U;
    after_read_increment[0] = 3U;
    after_read_increment[1] = 5U;
    encoder_feedback_tick_10ms();
    encoder_feedback_get_snapshot(&snapshot);
    check(snapshot.left.delta_counts == 10 && snapshot.right.delta_counts == 20,
          "first sample contains edges observed at read time");
    snapshot = sample(0, 0);
    check(snapshot.left.delta_counts == 3 && snapshot.right.delta_counts == 5,
          "edges after register read survive into the next sample");
    check(snapshot.left.total_counts == 13 && snapshot.right.total_counts == 25,
          "free-running counters lose no simulated read-boundary edges");
}

static void test_saturation(void)
{
    encoder_feedback_snapshot_t snapshot;
    unsigned int tick;
    encoder_feedback_init();
    for (tick = 0; tick < 65538U; ++tick) snapshot = sample(32767, -32767);
    check(snapshot.left.total_counts == 2147483646 && snapshot.right.total_counts == -2147483646,
          "totals remain exact immediately before int32 saturation");
    check(!snapshot.left.total_saturated && !snapshot.right.total_saturated,
          "valid int32 totals have no saturation flag");
    snapshot = sample(32767, -32767);
    check(snapshot.left.total_counts == INT32_MAX && snapshot.right.total_counts == INT32_MIN,
          "both accumulation limits saturate without signed overflow");
    check(snapshot.left.total_saturated && snapshot.right.total_saturated,
          "saturation flags report loss of total precision");
    snapshot = sample(-11, 11);
    check(snapshot.left.total_counts == INT32_MAX - 11 && snapshot.right.total_counts == INT32_MIN + 11,
          "reverse deltas resume from clamped totals");
    check(snapshot.left.total_saturated && snapshot.right.total_saturated,
          "saturation flags stay latched after reversing");
    encoder_feedback_reset_totals();
    encoder_feedback_get_snapshot(&snapshot);
    check(!snapshot.left.total_saturated && !snapshot.right.total_saturated,
          "explicit reset clears saturation flags");
}

static void test_snapshot(void)
{
    encoder_feedback_snapshot_t snapshot;
    unsigned int reads;
    unsigned int masks;
    unsigned int disables;
    unsigned int restores;
    encoder_feedback_init();
    (void)sample(12, -7);
    reads = read_calls;
    masks = get_mask_calls;
    disables = disable_calls;
    restores = restore_calls;
    irq_mask = 1U;
    encoder_feedback_get_snapshot(&snapshot);
    check(irq_mask == 1U, "snapshot preserves previously disabled interrupts");
    check(get_mask_calls == masks + 1U && disable_calls == disables + 1U &&
          restore_calls == restores + 1U, "snapshot uses one short saved-mask critical section");
    check(read_calls == reads, "snapshot performs no hardware IO");
    irq_mask = 0U;
    tick_before_disable = 1U;
    tick_after_restore = 1U;
    encoder_feedback_get_snapshot(&snapshot);
    check(irq_mask == 0U, "snapshot restores previously enabled interrupts");
    check(snapshot.sample_sequence == 2U && snapshot.left.total_counts == 19,
          "copy lies between interrupt disable and restore");
    encoder_feedback_get_snapshot(&snapshot);
    check(snapshot.sample_sequence == 3U && snapshot.left.total_counts == 28,
          "a later snapshot sees the update after restore");
    masks = get_mask_calls;
    disables = disable_calls;
    restores = restore_calls;
    encoder_feedback_get_snapshot(NULL);
    check(get_mask_calls == masks && disable_calls == disables && restore_calls == restores,
          "NULL output has no interrupt side effects");
}

int main(int argc, char **argv)
{
    check(argc == 2, "choose one test case");
    if (strcmp(argv[1], "raw") == 0) test_raw();
    else if (strcmp(argv[1], "init") == 0) test_init();
    else if (strcmp(argv[1], "rates") == 0) test_rates();
    else if (strcmp(argv[1], "reset") == 0) test_reset();
    else if (strcmp(argv[1], "read-edge") == 0) test_read_edge();
    else if (strcmp(argv[1], "saturation") == 0) test_saturation();
    else if (strcmp(argv[1], "snapshot") == 0) test_snapshot();
    else check(0, "unknown test case");
    printf("PASS: encoder %s\n", argv[1]);
    return 0;
}
