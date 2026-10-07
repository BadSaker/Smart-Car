#ifndef AUTOCAR_ENCODER_FEEDBACK_H
#define AUTOCAR_ENCODER_FEEDBACK_H

#include <stdint.h>

typedef struct
{
    /* QTMR原始带符号增量，不代表已经标定的车辆前进方向。 */
    int16_t delta_counts;
    /* 饱和累计值；反向计数可离开限值，但饱和标志保持到重置。 */
    int32_t total_counts;
    int32_t counts_per_second;
    /* 带原始方向符号的轮速，分别以0.1rpm和mm/s为单位。 */
    int32_t wheel_rpm_x10;
    int32_t speed_mm_s;
    uint8_t total_saturated;
} encoder_channel_feedback_t;

typedef struct
{
    encoder_channel_feedback_t left;
    encoder_channel_feedback_t right;
    /* 每个采样增加一次，uint32_t自然回绕；累计值重置不重置序号。 */
    uint32_t sample_sequence;
} encoder_feedback_snapshot_t;

/* 启动阶段、启用PIT0前调用一次，配置STEP/DIR并清零一次硬件计数器。 */
void encoder_feedback_init(void);

/* 仅由PIT0每10ms调用，是运行期反馈的唯一写入上下文。
 * 硬件16位计数自由运行，按模65536求差，不逐拍清零。
 * 有效采样要求每10ms实际计数变化绝对值<32768；超限会产生不可检测的歧义。
 * 模差恰为0x8000时约定返回-32768，无法据此判定真实方向。
 * 该约定值和其速率只供诊断，不能作为满足采样前提的证据。 */
void encoder_feedback_tick_10ms(void);

/* 仅启动阶段或PIT0中调用；电机测试须在启动PWM前调用。
 * 清除软件累计、旧瞬时值和饱和标志，并读取当前硬件值建立新基线。
 * 不清零运行中的硬件计数器；不允许主循环与PIT0并发写入。 */
void encoder_feedback_reset_totals(void);

/* 主循环取得一致快照；保存并恢复既有PRIMASK，短临界区内只复制内存。
 * 不读外设、不等待；out为NULL时直接返回，不改变中断状态。 */
void encoder_feedback_get_snapshot(encoder_feedback_snapshot_t *out);

#endif /* AUTOCAR_ENCODER_FEEDBACK_H */
