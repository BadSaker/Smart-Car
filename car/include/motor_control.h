#ifndef AUTOCAR_MOTOR_CONTROL_H
#define AUTOCAR_MOTOR_CONTROL_H

#include <stdint.h>

typedef enum
{
    MOTOR_TEST_IDLE = 0,
    MOTOR_TEST_CHANNEL1,
    MOTOR_TEST_GAP1,
    MOTOR_TEST_CHANNEL2,
    MOTOR_TEST_GAP2,
    MOTOR_TEST_BOTH,
    MOTOR_TEST_COMPLETE,
    MOTOR_TEST_STOPPED,
    MOTOR_TEST_CONFIG_ERROR,
    MOTOR_REMOTE_DISARMED,
    MOTOR_REMOTE_WAIT_NEUTRAL,
    MOTOR_REMOTE_READY,
    MOTOR_REMOTE_DRIVING,
    MOTOR_REMOTE_REVERSING,
    MOTOR_REMOTE_TIMEOUT,
    MOTOR_AUTONOMOUS_DRIVING,
    MOTOR_AUTONOMOUS_WAITING,
    MOTOR_AUTONOMOUS_STOPPED
} motor_test_state_t;

typedef struct
{
    motor_test_state_t state;
    uint8_t ready;
    uint8_t telemetry_visible;
    uint16_t duty_permille;
    uint32_t elapsed_ms;
    uint32_t run_id;
    uint8_t remote_link;
    uint8_t remote_armed;
    uint8_t remote_wait_neutral;
    int16_t requested_permille;
    int16_t applied_permille;
    uint16_t command_age_ms;
    uint32_t arm_generation;
    uint8_t autonomous_active;
    uint16_t channel1_duty_permille;
    uint16_t channel2_duty_permille;
} motor_control_status_t;

/* 主循环初始化时调用一次：参考DIR、17kHz零占空比、C12/S3上拉。
 * 此时未开放本地启动/武装。配置无效时保持零命令并进入CONFIG_ERROR。 */
void motor_control_init(void);

/* 摄像头及外设初始化完成后由主循环调用；重复调用无副作用。
 * 开放本地按键及自动驾驶门控，不直接产生运动命令；
 * C12仍须此后稳定释放再新按下，自动驾驶启动键由上层管理。
 * REMOTE_CONTROL_ENABLED=0时保留有限台架测试，=1时C12武装/解除武装。 */
void motor_control_enable_test(void);

/* 仅PIT0每10ms调用：按键消抖、邮箱消费、计时和PWM更新均在此推进。
 * S3低电平不消抖，优先零PWM并解除所有权，清除C12待决按下。
 * 自动驾驶占用时C12原始低电平同样停车；两者锁存阻断直到提交active=0。
 * 自动驾驶先于遥控断链检查，遥控邮箱在此期间消费后丢弃。
 * 遥控断链必须重新C12武装及新Down确认；300ms租约超时只清除本次油门，
 * 保留武装以接受后续新命令；反向前两路PWM归零至少100ms。
 * 主循环阻塞不会暂停计时，关中断或更高优先级阻塞仍会延后处理。
 * 零PWM不是供电切断，可能制动，不能据此保证滑行、高阻或物理停转。 */
void motor_control_tick_10ms(void);

/* 仅同一PIT0在motor_control_tick_10ms之前调用一次，不直接写硬件。
 * active非零申请自动驾驶所有权；M1为左轮、M2为右轮，单位千分比，
 * 各自按MOTOR_TEST_MAX_DUTY_PERMILLE硬限幅且仅允许已确认的前进DIR。
 * 接管及退出均清除遥控武装和旧目标；恢复遥控须新C12再新Down。
 * 遥控后退切入时仍需100ms零PWM间隔。active=0撤销申请并解除停车锁存。
 * 两种REMOTE_CONTROL_ENABLED模式均可用；反馈/视觉超时由上层PIT控制。
 * 调用方必须每次PIT明确提交当前目标，不从主循环直接调用本接口。 */
void motor_control_autonomous_set(uint8_t active, uint16_t m1_permille, uint16_t m2_permille);

/* 主循环原子地取走S3停止标志；短按后主循环才恢复时仍能收到一次。
 * 返回1时调用原舵机停止接口；舵机动作仍取决于主循环恢复时刻。 */
uint8_t motor_control_take_servo_stop(void);

/* 主循环获取一致快照；out为空时不操作。短临界区保留调用前PRIMASK。
 * duty_permille为实际已发出PWM的绝对千分比；遥控正值前进、负值后退，
 * requested_permille为已限幅目标，applied_permille为已发出带符号命令。
 * command_age_ms为当前/刚超时行驶命令的PIT年龄，停止/解除武装时清零。
 * elapsed_ms为本轮台架/武装/自动驾驶累计时长；run_id在这三种新启动时递增。
 * arm_generation仅在遥控新武装时递增，主循环据此丢弃旧网络接收数据。
 * telemetry_visible只由旧台架模式维护，遥控模式保持0并由遥控界面显示。
 * autonomous_active在自动驾驶持有所有权时为1，包括零目标和换向等待；
 * channel1/2_duty_permille分别表示M1/M2实际PWM千分比，所有模式均维护。
 * 自动驾驶时duty_permille/applied_permille为双路最大正值，requested保持0。 */
void motor_control_get_status(motor_control_status_t *out);

/* 主循环提交连接可用状态；短临界区保留PRIMASK且不直接写硬件。
 * 断链事件独立锁存，即使同一PIT间隔内恢复也会停止遥控、解除武装和丢弃旧邮箱；
 * 自动驾驶所有权有效时只消费事件，不影响自动输出。
 * 重复相同连接值无续租或重新武装效果；模式0时无操作。 */
void motor_control_remote_set_link(uint8_t available);

/* 主循环提交带符号千分比油门，PIT按配置限幅；短临界区保留PRIMASK。
 * 武装前/当次武装PIT内的邮箱都丢弃；此后须新0油门且neutral_ack!=0才允许行驶。
 * 任意0油门锁存停止并取消换向等待，不会被同一消费周期的后续非零命令覆盖。
 * 只有新非零油门续期行驶租约；转向事件不得调用本接口，模式0时无操作。 */
void motor_control_submit_remote(int16_t throttle_permille, uint8_t neutral_ack);

#endif /* AUTOCAR_MOTOR_CONTROL_H */
