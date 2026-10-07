#ifndef AUTOCAR_SERVO_CONTROL_H
#define AUTOCAR_SERVO_CONTROL_H

#include <stdint.h>

typedef enum
{
    SERVO_CONTROL_IDLE = 0,
    SERVO_CONTROL_SUPPLY_ERROR,
    SERVO_CONTROL_CENTER,
    SERVO_CONTROL_TO_LOW,
    SERVO_CONTROL_HOLD_LOW,
    SERVO_CONTROL_TO_HIGH,
    SERVO_CONTROL_HOLD_HIGH,
    SERVO_CONTROL_RETURN_CENTER,
    SERVO_CONTROL_COMPLETE,
    SERVO_CONTROL_STOPPED,
    SERVO_CONTROL_REMOTE
} servo_control_state_t;

typedef enum
{
    SERVO_CONTROL_START_OK = 0,
    SERVO_CONTROL_START_SUPPLY_ERROR,
    SERVO_CONTROL_START_STOP_HELD,
    SERVO_CONTROL_START_NOT_ARMED,
    SERVO_CONTROL_START_BUSY
} servo_control_result_t;

typedef struct
{
    servo_control_state_t state;
    uint16_t current_pulse_us;
    uint16_t target_pulse_us;
    uint8_t output_enabled;
} servo_control_status_t;

/* 主循环启动时调用一次。将C30设为低电平，无有效PWM；按键上拉。
 * now_ms为递增毫秒计数，允许uint32_t自然回绕；不是阻塞延时。 */
void servo_control_init(uint32_t now_ms);

/* 在无阻塞的主循环中频繁调用；响应延迟受最长单步处理（含刷屏）耗时限制。
 * S3低电平立即撤销信号；S2稳定释放后新按下30ms才开始一次测试。
 * 不在中断中调用；它和状态读取必须由同一主循环串行执行。 */
void servo_control_poll(uint32_t now_ms);

/* 仅接受poll已消抖的新按下事件，供poll内部启动；不可绕过物理按键。
 * 启动失败不产生PWM；忙时不重启轨迹，停止键优先于启动。 */
servo_control_result_t servo_control_start_test(uint32_t now_ms);

/* 仅由主循环串行调用：enabled由上层连接、解锁、确认和超时门控决定。
 * steering_permille限幅到[-1000,1000]；正值向左增大脉宽，负值向右减小。
 * 使用已确认1480us中位及1420..1540us窗口；每次启用先从中位输出，
 * 后续最多每20ms变化2us，迟到不追赶。S3原始低电平和供电配置保护优先。
 * enabled=0仅撤销本接口占用的输出；停止后须由上层重新授权enabled=1。
 * 遥控占用期间拒绝S2台架测试；本模块不判断网络租约或在中断中操作PWM。 */
void servo_control_remote_apply(uint8_t enabled, int16_t steering_permille, uint32_t now_ms);

/* 立即将信号脚切为GPIO低电平并撤销待执行启动，不切断舵机电源。
 * S-U400官方说明书指出失去输入信号仍保持，故不能保证卸力。
 * 停止后台架测试必须释放启动键并重新按下，遥控须由上层重新授权。 */
void servo_control_stop(void);

/* 只读调试快照；脉宽代表命令值而非示波器测量值，0表示没有PWM。
 * COMPLETE保持参考中位，STOPPED不输出PWM。指针在模块生命周期内有效。 */
const servo_control_status_t *servo_control_get_status(void);

#endif /* AUTOCAR_SERVO_CONTROL_H */
