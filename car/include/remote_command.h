#ifndef AUTOCAR_REMOTE_COMMAND_H
#define AUTOCAR_REMOTE_COMMAND_H

#include <stdint.h>

#define REMOTE_COMMAND_LINE_CAPACITY 12U

/* 车辆动作映射为本项目选择；逐飞助手只发送按键名称文本。 */
typedef enum
{
    REMOTE_COMMAND_FORWARD = 0,
    REMOTE_COMMAND_BACKWARD,
    REMOTE_COMMAND_LEFT,
    REMOTE_COMMAND_RIGHT,
    REMOTE_COMMAND_CENTER,
    REMOTE_COMMAND_STOP
} remote_command_type_t;

typedef void (*remote_command_handler_t)(remote_command_type_t command, void *context);

typedef struct
{
    uint8_t line[REMOTE_COMMAND_LINE_CAPACITY];
    uint8_t line_length;
    uint8_t saw_cr;
    uint8_t discard_line;
    uint32_t valid_count;
    uint32_t rejected_count;
} remote_command_parser_t;

/* 清除半行、错误状态与计数。parser为空时不操作。 */
void remote_command_init(remote_command_parser_t *parser);

/*
 * 主循环中的单一调用者按字节流喂入；本模块无驱动、时钟、心跳或控制输出。
 * 仅接受 W/S/A/D/Up/Down/Left/Right 加严格CRLF，保留跨调用半行。
 * 超长、非可打印ASCII或异常CR/LF令整行失效，直到下一组CRLF恢复。
 * 未知行（含空行）不回调；完整行的有效/拒绝计数分别饱和于UINT32_MAX。
 * parser或data为空、length为0时不操作且保留状态；handler为空仍解析和计数。
 * context原样传给同步回调（允许为空）；回调不得重入同一个解析器。
 * 调用者持有结构体且负责串行访问，不应直接改写内部接收状态。
 */
void remote_command_feed(remote_command_parser_t *parser, const uint8_t *data,
                         uint32_t length, remote_command_handler_t handler,
                         void *context);

#endif
