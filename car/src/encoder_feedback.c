#include "encoder_feedback.h"
#include "encoder_config.h"
#include "fsl_common.h"
#include <limits.h>

static volatile encoder_feedback_snapshot_t encoder_snapshot;
static uint16_t encoder_left_previous;
static uint16_t encoder_right_previous;

static int32_t encoder_round_divide(int64_t numerator, int64_t denominator)
{
    /* 当前硬件参数的中间量均在int64_t范围内；半单位按远离零对称取整。 */
    if (numerator < 0)
        return -(int32_t)((-numerator + denominator / 2) / denominator);
    return (int32_t)((numerator + denominator / 2) / denominator);
}

static void encoder_update_channel(volatile encoder_channel_feedback_t *channel,
                                   uint16_t current, uint16_t previous)
{
    uint16_t difference = (uint16_t)(current - previous);
    int32_t delta = difference < 32768U ? (int32_t)difference : (int32_t)difference - 65536;
    int64_t total = (int64_t)channel->total_counts + delta;
    int64_t counts_per_second = (int64_t)delta * 1000 / ENCODER_FEEDBACK_PERIOD_MS;
    int64_t encoder_counts_per_wheel_ratio = (int64_t)ENCODER_PULSES_PER_REV *
                                            ENCODER_STEP_EDGE_FACTOR * ENCODER_TO_WHEEL_RATIO_NUM;

    channel->delta_counts = (int16_t)delta;
    if (total > INT32_MAX)
    {
        channel->total_counts = INT32_MAX;
        channel->total_saturated = 1U;
    }
    else if (total < INT32_MIN)
    {
        channel->total_counts = INT32_MIN;
        channel->total_saturated = 1U;
    }
    else channel->total_counts = (int32_t)total;

    channel->counts_per_second = (int32_t)counts_per_second;
    /* 比例表示编码器转数/轮转数；保留分子分母，不先截断每圈计数。 */
    channel->wheel_rpm_x10 = encoder_round_divide(
        counts_per_second * 600 * ENCODER_TO_WHEEL_RATIO_DEN,
        encoder_counts_per_wheel_ratio);
    channel->speed_mm_s = encoder_round_divide(
        counts_per_second * ENCODER_WHEEL_CIRCUMFERENCE_UM * ENCODER_TO_WHEEL_RATIO_DEN,
        encoder_counts_per_wheel_ratio * 1000);
}

void encoder_feedback_init(void)
{
    /* 当前库最终启动kQTMR_PriSrcRiseEdgeSecDir：方向由硬件逐沿判定。 */
    encoder_dir_init(ENCODER_LEFT_CHANNEL, ENCODER_LEFT_STEP_PIN, ENCODER_LEFT_DIRECTION_PIN);
    encoder_dir_init(ENCODER_RIGHT_CHANNEL, ENCODER_RIGHT_STEP_PIN, ENCODER_RIGHT_DIRECTION_PIN);
    encoder_clear_count(ENCODER_LEFT_CHANNEL);
    encoder_clear_count(ENCODER_RIGHT_CHANNEL);
    encoder_snapshot.sample_sequence = 0U;
    encoder_feedback_reset_totals();
}

void encoder_feedback_tick_10ms(void)
{
    uint16_t left_current = (uint16_t)encoder_get_count(ENCODER_LEFT_CHANNEL);
    uint16_t right_current = (uint16_t)encoder_get_count(ENCODER_RIGHT_CHANNEL);

    encoder_update_channel(&encoder_snapshot.left, left_current, encoder_left_previous);
    encoder_update_channel(&encoder_snapshot.right, right_current, encoder_right_previous);
    encoder_left_previous = left_current;
    encoder_right_previous = right_current;
    ++encoder_snapshot.sample_sequence;
}

void encoder_feedback_reset_totals(void)
{
    const encoder_channel_feedback_t empty = {0};
    /* 自由运行计数器只读取基线，读取后到来的边沿留给下一采样。 */
    encoder_left_previous = (uint16_t)encoder_get_count(ENCODER_LEFT_CHANNEL);
    encoder_right_previous = (uint16_t)encoder_get_count(ENCODER_RIGHT_CHANNEL);
    encoder_snapshot.left = empty;
    encoder_snapshot.right = empty;
}

void encoder_feedback_get_snapshot(encoder_feedback_snapshot_t *out)
{
    uint32_t interrupt_mask;
    if (out == 0) return;

    interrupt_mask = __get_PRIMASK();
    __disable_irq();
    *out = encoder_snapshot;
    __set_PRIMASK(interrupt_mask);
}
