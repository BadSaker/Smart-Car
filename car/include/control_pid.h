#ifndef AUTOCAR_CONTROL_PID_H
#define AUTOCAR_CONTROL_PID_H

#include <stdint.h>

/* 并联位置式PID参数；设定和测量必须使用相同输入单位。 */
typedef struct
{
    float kp;                /* 输出单位/输入单位，可为有限有符号增益。 */
    float ki;                /* 输出单位/(输入单位*秒)，不是已乘周期的系数。 */
    float kd;                /* 输出单位*秒/输入单位。 */
    float output_min;        /* 总输出下限，必须严格小于output_max。 */
    float output_max;        /* 总输出上限，包含前馈。 */
    float integral_limit;    /* 积分项绝对上限，输出单位；0关闭积分累积。 */
    float derivative_tau_s;  /* 微分一阶滤波时间常数，秒；0为未滤波差分。 */
} control_pid_config_t;

/* 状态仅由本模块写入；静态/栈分配均可，同一实例不能并发调用。 */
typedef struct
{
    control_pid_config_t config;
    float integral;          /* 已接受的积分状态，输出单位。 */
    float derivative;        /* 测量微分产生的滤波输出，输出单位。 */
    float previous_measurement;
    uint8_t has_previous_measurement;
    uint8_t initialized;
    uint8_t valid;           /* 最近初始化/复位/步进是否有效，失败为0。 */
} control_pid_t;

/* 全部参数须有限，积分限幅和时间常数须非负；失败清状态并返回0。
 * 必须先初始化。重新调参使用本接口，允许config指向pid->config。 */
uint8_t control_pid_init(control_pid_t *pid, const control_pid_config_t *config);

/* 清积分和微分历史，保留配置；停机、切换控制模式时由调用方执行。
 * 首次有效测量仅建立微分基线；NULL无操作。 */
void control_pid_reset(control_pid_t *pid);

/* 在单一控制上下文中每个有效采样调用一次，dt_s为实际周期且必须>0秒。
 * 积分采用当前误差矩形积分；微分作用于测量，采用后向欧拉一阶滤波。
 * 先计算限幅积分候选和总输出；饱和且积分增量加深饱和时拒绝保存候选，
 * 本次仍返回候选总输出的限幅值，避免纯积分在越界采样时无法达到限幅。
 * 前馈使用输出单位，并参与饱和判断；下游须采用与此处一致的执行器限幅。
 * 非有限输入/中间结果、非法周期或配置会清动态状态、valid=0并返回0；
 * 该安全返回值不受output_min约束。有效配置在下次合法采样自动恢复，
 * 首次微分为0。调用方应检查valid并据此停止执行器，不能依赖零值代替故障。
 * 不得用fast-math或有限数学假设编译，否则NaN/Inf检查可能被优化掉。 */
float control_pid_step(control_pid_t *pid, float setpoint, float measurement,
                       float feedforward, float dt_s);

#endif
