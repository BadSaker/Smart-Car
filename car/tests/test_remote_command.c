#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "remote_command.h"

typedef struct
{
    remote_command_type_t commands[32];
    uint32_t count;
} capture_t;

static uint32_t null_context_calls;

static void check(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void capture(remote_command_type_t command, void *context)
{
    capture_t *result = (capture_t *)context;
    check(result != NULL, "callback context is preserved");
    check(result->count < 32U, "capture capacity");
    result->commands[result->count++] = command;
}

static void capture_null(remote_command_type_t command, void *context)
{
    check(context == NULL, "NULL context is passed unchanged");
    check(command == REMOTE_COMMAND_STOP, "NULL-context callback receives STOP");
    ++null_context_calls;
}

static void feed_text(remote_command_parser_t *parser, const char *text, capture_t *result)
{
    remote_command_feed(parser, (const uint8_t *)text, (uint32_t)strlen(text), capture, result);
}

static void expect_commands(const capture_t *result, const remote_command_type_t *expected,
                            uint32_t count)
{
    uint32_t index;
    check(result->count == count, "one ordered callback per accepted line");
    for (index = 0U; index < count; ++index)
        check(result->commands[index] == expected[index], "exact key-to-command mapping");
}

static void test_mapping(void)
{
    remote_command_parser_t parser;
    capture_t result = {{0}, 0U};
    const remote_command_type_t expected[] = {
        REMOTE_COMMAND_FORWARD, REMOTE_COMMAND_BACKWARD,
        REMOTE_COMMAND_LEFT, REMOTE_COMMAND_RIGHT,
        REMOTE_COMMAND_CENTER, REMOTE_COMMAND_STOP,
        REMOTE_COMMAND_LEFT, REMOTE_COMMAND_RIGHT
    };
    remote_command_init(&parser);
    feed_text(&parser, "W\r\nS\r\nA\r\nD\r\nUp\r\nDown\r\nLeft\r\nRight\r\n", &result);
    expect_commands(&result, expected, 8U);
    check(parser.valid_count == 8U && parser.rejected_count == 0U,
          "exact keys increment only valid count");
}

static void test_chunks(void)
{
    const char *stream = "W\r\nRight\r\nDown\r\nLeft\r\n";
    const remote_command_type_t expected[] = {
        REMOTE_COMMAND_FORWARD, REMOTE_COMMAND_RIGHT,
        REMOTE_COMMAND_STOP, REMOTE_COMMAND_LEFT
    };
    uint32_t length = (uint32_t)strlen(stream);
    uint32_t split;
    for (split = 0U; split <= length; ++split)
    {
        remote_command_parser_t parser;
        capture_t result = {{0}, 0U};
        remote_command_init(&parser);
        remote_command_feed(&parser, (const uint8_t *)stream, split, capture, &result);
        remote_command_feed(&parser, (const uint8_t *)stream + split,
                            length - split, capture, &result);
        expect_commands(&result, expected, 4U);
        check(parser.valid_count == 4U && parser.rejected_count == 0U,
              "all split positions preserve framing");
    }
    {
        remote_command_parser_t parser;
        capture_t result = {{0}, 0U};
        remote_command_init(&parser);
        for (split = 0U; split < length; ++split)
            remote_command_feed(&parser, (const uint8_t *)stream + split, 1U, capture, &result);
        expect_commands(&result, expected, 4U);
    }
}

static void test_incomplete(void)
{
    remote_command_parser_t parser;
    capture_t result = {{0}, 0U};
    const remote_command_type_t expected[] = {REMOTE_COMMAND_CENTER};
    remote_command_init(&parser);
    feed_text(&parser, "U", &result);
    feed_text(&parser, "p", &result);
    check(result.count == 0U && parser.valid_count == 0U, "text alone is not a command");
    feed_text(&parser, "\r", &result);
    check(result.count == 0U && parser.valid_count == 0U, "CR alone is not a delimiter");
    feed_text(&parser, "\n", &result);
    expect_commands(&result, expected, 1U);
}

static void test_unknown(void)
{
    remote_command_parser_t parser;
    capture_t result = {{0}, 0U};
    const remote_command_type_t expected[] = {REMOTE_COMMAND_STOP};
    remote_command_init(&parser);
    feed_text(&parser, "\r\nw\r\nUP\r\n W\r\nW \r\nStop\r\nCtrl+Q\r\nWW\r\nDown\r\n", &result);
    expect_commands(&result, expected, 1U);
    check(parser.valid_count == 1U && parser.rejected_count == 8U,
          "unknown lines are rejected without trimming or case folding");
}

static void test_malformed(void)
{
    const char *invalid[] = {
        "W\nS\r\n", "W\rS\r\n", "W\r\r\n",
        "\nUp\r\n", "D\r\nW\rDown\r\n"
    };
    uint32_t index;
    for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
    {
        remote_command_parser_t parser;
        capture_t result = {{0}, 0U};
        remote_command_init(&parser);
        feed_text(&parser, invalid[index], &result);
        check(result.count == (index == 4U ? 1U : 0U),
              "malformed line must not restart at its command-looking suffix");
        check(parser.rejected_count == 1U, "one rejected count per malformed CRLF line");
        feed_text(&parser, "Down\r", &result);
        check(parser.rejected_count == 1U, "unfinished next line is not rejected");
        feed_text(&parser, "\n", &result);
        check(result.commands[result.count - 1U] == REMOTE_COMMAND_STOP,
              "valid line after malformed CRLF recovers");
    }
}

static void test_binary(void)
{
    uint32_t byte;
    /* 逐字节扰动覆盖NUL、控制符和非ASCII，禁止有效后缀逃逸。 */
    for (byte = 0U; byte <= 255U; ++byte)
    {
        if ((byte >= 32U && byte <= 126U) || byte == 13U || byte == 10U)
            continue;
        {
            remote_command_parser_t parser;
            capture_t result = {{0}, 0U};
            const uint8_t prefix[] = {(uint8_t)byte, 'W', '\r', '\n'};
            remote_command_init(&parser);
            remote_command_feed(&parser, prefix, sizeof(prefix), capture, &result);
            feed_text(&parser, "Down\r\n", &result);
            check(result.count == 1U && result.commands[0] == REMOTE_COMMAND_STOP,
                  "non-printable or non-ASCII prefix poisons the entire line");
            check(parser.rejected_count == 1U, "binary line rejected once");
        }
    }
    {
        remote_command_parser_t parser;
        capture_t result = {{0}, 0U};
        const uint8_t data[] = {0x55U, 0x20U, 0x01U, 0x00U, 'U', 'p', '\r', '\n',
                                'U', 'p', '\r', '\n'};
        remote_command_init(&parser);
        remote_command_feed(&parser, data, sizeof(data), capture, &result);
        check(result.count == 1U && result.commands[0] == REMOTE_COMMAND_CENTER,
              "0x55 binary header is not an Up text command");
        check(parser.valid_count == 1U && parser.rejected_count == 1U,
              "binary and exact Up have distinct outcomes");
    }
}

static void test_long(void)
{
    struct
    {
        uint32_t before;
        remote_command_parser_t parser;
        uint32_t after;
    } guarded;
    capture_t result = {{0}, 0U};
    uint8_t noise[4096];
    guarded.before = 0x12345678U;
    guarded.after = 0x89ABCDEFU;
    memset(noise, 'x', sizeof(noise));
    remote_command_init(&guarded.parser);
    remote_command_feed(&guarded.parser, noise, sizeof(noise), capture, &result);
    check(result.count == 0U && guarded.parser.rejected_count == 0U,
          "unclosed overlong line has no event or completed-line count");
    feed_text(&guarded.parser, "W\r", &result);
    feed_text(&guarded.parser, "\nDown\r\n", &result);
    check(result.count == 1U && result.commands[0] == REMOTE_COMMAND_STOP,
          "overlong line drops command suffix then recovers at CRLF");
    check(guarded.parser.valid_count == 1U && guarded.parser.rejected_count == 1U,
          "one long line is one rejection");
    check(guarded.before == 0x12345678U && guarded.after == 0x89ABCDEFU,
          "long input must not overwrite adjacent memory");
}

static void test_null(void)
{
    remote_command_parser_t parser;
    capture_t result = {{0}, 0U};
    remote_command_init(NULL);
    remote_command_feed(NULL, (const uint8_t *)"W\r\n", 3U, capture, &result);
    remote_command_init(&parser);
    feed_text(&parser, "Up\r", &result);
    remote_command_feed(&parser, NULL, 50U, capture, &result);
    remote_command_feed(&parser, NULL, 0U, capture, &result);
    remote_command_feed(&parser, (const uint8_t *)"Down\r\n", 0U, capture, &result);
    check(result.count == 0U && parser.valid_count == 0U && parser.rejected_count == 0U,
          "NULL and zero-length calls are no-ops");
    feed_text(&parser, "\n", &result);
    check(result.count == 1U && result.commands[0] == REMOTE_COMMAND_CENTER,
          "NULL and zero-length calls retain the pending CR");
}

static void test_null_handler(void)
{
    remote_command_parser_t parser;
    capture_t result = {{0}, 0U};
    remote_command_init(&parser);
    remote_command_feed(&parser, (const uint8_t *)"W\r\n?\r\n", 6U, NULL, NULL);
    check(parser.valid_count == 1U && parser.rejected_count == 1U,
          "NULL callback still consumes and counts complete lines");
    feed_text(&parser, "Down\r\n", &result);
    check(result.count == 1U && result.commands[0] == REMOTE_COMMAND_STOP,
          "a later callback never replays previous commands");
}

static void test_reset(void)
{
    remote_command_parser_t parser;
    capture_t result = {{0}, 0U};
    remote_command_init(&parser);
    feed_text(&parser, "W\r\n?\r\nUp\r", &result);
    remote_command_init(&parser);
    check(parser.valid_count == 0U && parser.rejected_count == 0U, "reset clears both counters");
    result.count = 0U;
    feed_text(&parser, "Down\r\n", &result);
    check(result.count == 1U && result.commands[0] == REMOTE_COMMAND_STOP,
          "reset drops incomplete prior line");
    feed_text(&parser, "W\n", &result);
    remote_command_init(&parser);
    feed_text(&parser, "S\r\n", &result);
    check(result.count == 2U && result.commands[1] == REMOTE_COMMAND_BACKWARD,
          "reset clears discard state");
}

static void test_saturation(void)
{
    remote_command_parser_t parser;
    capture_t result = {{0}, 0U};
    remote_command_init(&parser);
    parser.valid_count = UINT32_MAX - 1U;
    parser.rejected_count = UINT32_MAX - 1U;
    feed_text(&parser, "W\r\n?\r\nS\r\n?\r\nDown\r\n", &result);
    check(parser.valid_count == UINT32_MAX && parser.rejected_count == UINT32_MAX,
          "diagnostic counters saturate without wrapping");
    check(result.count == 3U && result.commands[2] == REMOTE_COMMAND_STOP,
          "saturated counters never suppress subsequent events");
}

static void test_contexts(void)
{
    remote_command_parser_t first;
    remote_command_parser_t second;
    capture_t first_result = {{0}, 0U};
    capture_t second_result = {{0}, 0U};
    remote_command_init(&first);
    remote_command_init(&second);
    feed_text(&first, "Lef", &first_result);
    feed_text(&second, "Right\r\n", &second_result);
    feed_text(&first, "t\r\n", &first_result);
    check(first_result.count == 1U && first_result.commands[0] == REMOTE_COMMAND_LEFT,
          "first parser owns its partial text and callback context");
    check(second_result.count == 1U && second_result.commands[0] == REMOTE_COMMAND_RIGHT,
          "second parser owns independent state");
    remote_command_feed(&second, (const uint8_t *)"Down\r\n", 6U, capture_null, NULL);
    check(null_context_calls == 1U, "NULL context does not prevent callback");
}

int main(int argc, char **argv)
{
    check(argc == 2, "choose one test case");
    if (strcmp(argv[1], "mapping") == 0) test_mapping();
    else if (strcmp(argv[1], "chunks") == 0) test_chunks();
    else if (strcmp(argv[1], "incomplete") == 0) test_incomplete();
    else if (strcmp(argv[1], "unknown") == 0) test_unknown();
    else if (strcmp(argv[1], "malformed") == 0) test_malformed();
    else if (strcmp(argv[1], "binary") == 0) test_binary();
    else if (strcmp(argv[1], "long") == 0) test_long();
    else if (strcmp(argv[1], "null") == 0) test_null();
    else if (strcmp(argv[1], "null-handler") == 0) test_null_handler();
    else if (strcmp(argv[1], "reset") == 0) test_reset();
    else if (strcmp(argv[1], "saturation") == 0) test_saturation();
    else if (strcmp(argv[1], "contexts") == 0) test_contexts();
    else check(0, "unknown test case");
    printf("PASS: remote command %s\n", argv[1]);
    return 0;
}
