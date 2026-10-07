#include "remote_command.h"
#include <stddef.h>
#include <string.h>

typedef struct
{
    const char *text;
    uint8_t length;
    remote_command_type_t command;
} remote_command_binding_t;

/*
 * 文本来源：逐飞助手说明书V1.4第47至49页的八键名称与CRLF示例。
 * 动作含义为本项目映射，不代表上位机自带停止、回中或连续发送功能。
 */
static const remote_command_binding_t remote_command_bindings[] =
{
    {"W",     1U, REMOTE_COMMAND_FORWARD},
    {"S",     1U, REMOTE_COMMAND_BACKWARD},
    {"A",     1U, REMOTE_COMMAND_LEFT},
    {"D",     1U, REMOTE_COMMAND_RIGHT},
    {"Up",    2U, REMOTE_COMMAND_CENTER},
    {"Down",  4U, REMOTE_COMMAND_STOP},
    {"Left",  4U, REMOTE_COMMAND_LEFT},
    {"Right", 5U, REMOTE_COMMAND_RIGHT}
};

static uint8_t remote_command_decode(const remote_command_parser_t *parser,
                                     remote_command_type_t *command)
{
    uint32_t index;
    for (index = 0U;
         index < sizeof(remote_command_bindings) / sizeof(remote_command_bindings[0]);
         ++index)
    {
        const remote_command_binding_t *binding = &remote_command_bindings[index];
        if (parser->line_length == binding->length &&
            memcmp(parser->line, binding->text, binding->length) == 0)
        {
            *command = binding->command;
            return 1U;
        }
    }
    return 0U;
}

static void remote_command_finish_line(remote_command_parser_t *parser,
                                       remote_command_handler_t handler,
                                       void *context)
{
    remote_command_type_t command = REMOTE_COMMAND_STOP;
    uint8_t valid = !parser->discard_line && remote_command_decode(parser, &command);

    /* 完整CRLF才结束一行；先清接收状态，回调看到的是已消费的事件。 */
    parser->line_length = 0U;
    parser->saw_cr = 0U;
    parser->discard_line = 0U;
    if (valid)
    {
        if (parser->valid_count < UINT32_MAX)
            ++parser->valid_count;
        if (handler != NULL)
            handler(command, context);
    }
    else if (parser->rejected_count < UINT32_MAX)
    {
        ++parser->rejected_count;
    }
}

void remote_command_init(remote_command_parser_t *parser)
{
    if (parser != NULL)
        memset(parser, 0, sizeof(*parser));
}

void remote_command_feed(remote_command_parser_t *parser, const uint8_t *data,
                         uint32_t length, remote_command_handler_t handler,
                         void *context)
{
    uint32_t index;
    if (parser == NULL || data == NULL || length == 0U)
        return;

    for (index = 0U; index < length; ++index)
    {
        uint8_t byte = data[index];
        if (byte == '\n')
        {
            if (parser->saw_cr)
                remote_command_finish_line(parser, handler, context);
            else
                parser->discard_line = 1U;
            parser->saw_cr = 0U;
        }
        else if (byte == '\r')
        {
            if (parser->saw_cr)
                parser->discard_line = 1U;
            parser->saw_cr = 1U;
        }
        else
        {
            /*
             * 不能在坏字节后重新识别后缀，例如二进制帧中的Up或W。
             * 丢弃状态也继续跟踪CRLF，以便跨接收分片安全恢复。
             */
            if (parser->saw_cr || byte < 0x20U || byte > 0x7EU ||
                parser->line_length >= REMOTE_COMMAND_LINE_CAPACITY)
                parser->discard_line = 1U;
            parser->saw_cr = 0U;
            if (!parser->discard_line)
                parser->line[parser->line_length++] = byte;
        }
    }
}
