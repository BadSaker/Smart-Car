#ifndef AUTOCAR_REMOTE_CONTROL_H
#define AUTOCAR_REMOTE_CONTROL_H

#include <stdint.h>

typedef struct
{
    uint32_t received_commands;
    uint32_t rejected_commands;
    int16_t steering_permille;
    uint8_t draining;
} remote_control_status_t;

/* 主循环初始化一次；启用模式先撤销连接授权并清空会话状态。
 * 禁用模式没有网络、电机或舵机调用。 */
void remote_control_init(void);

/* 每圈主循环至多读取一个有界块；clock_ms必须为可回绕的单调毫秒时钟。
 * 读取前后分别取时，阻塞超过配置阈值时丢弃本块并撤销授权。
 * 新连接或本地解锁后先排空，只有实际读到0才结束；忙返回不代表排空。
 * clock_ms为空时不读取，并按接收失效处理。
 * poll读取前后与service共享主循环服务时间；间隔超过配置阈值先撤权并排空，
 * 之后须重新本地武装与确认；持续服务但没有输入不会触发此保护。
 * 文本协议没有发送时间戳，此保护不能证明迟到TCP数据的端到端年龄。 */
void remote_control_poll(uint32_t (*clock_ms)(void));

/* 仅主循环调用；按当前连接、武装、确认及活动时限控制舵机。
 * 转向活动不续期电机租约；电机超时由PIT控制模块独立处理。
 * now_ms须来自与poll一致的时钟；不得在中断或其他线程重入。 */
void remote_control_service(uint32_t now_ms);

/* 主循环读取累计诊断与转向命令；out为空时不操作。
 * rejected_commands还累计异常读取或主循环服务间隔超限的失效次数。 */
void remote_control_get_status(remote_control_status_t *out);

#endif
