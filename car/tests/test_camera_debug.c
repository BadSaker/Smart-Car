/* 使用真实应用适配与逐飞协议验证图传，只替换串口及Wi-Fi硬件接口。 */
#include "zf_common_headfile.h"
#include "camera_debug.h"
#include "camera_debug_config.h"
#include "camera_display.h"
#include "servo_control.h"
#include "motor_control.h"
#include "encoder_feedback.h"
#include "remote_control.h"
#include "autonomous_control.h"

/* 仅提供屏幕状态行需要的只读桩；舵机行为由独立真实模块测试覆盖。 */
static servo_control_status_t servo_status;
const servo_control_status_t *servo_control_get_status(void) { return &servo_status; }
static autonomous_drive_status_t auto_status;
void autonomous_control_get_status(autonomous_drive_status_t *out) { if(out)*out=auto_status; }
static motor_control_status_t motor_status;
static remote_control_status_t remote_status;
void remote_control_get_status(remote_control_status_t *out) { if (out) *out = remote_status; }
static encoder_feedback_snapshot_t encoder_status;
void motor_control_get_status(motor_control_status_t *out) { if (out) *out = motor_status; }
void encoder_feedback_get_snapshot(encoder_feedback_snapshot_t *out) { if (out) *out = encoder_status; }
static char telemetry_line0[40], telemetry_line1[40];

static uint8 wire[100000];
static uint32 wire_length;
static unsigned write_calls;
static int selected_transport = -1;
static unsigned spi_initializations, socket_connections, preview_count, display_dir_set;
static uint8 preview[MT9V03X_H * MT9V03X_W];
static int failure;
static unsigned disconnect_calls, delay_calls;
static uint32 old_session_length;
static int disconnect_failure;
static uint8 gray[MT9V03X_H][MT9V03X_W];
static uint8 binary_image[MT9V03X_H][MT9V03X_W];
static uint8 left[MT9V03X_H], middle[MT9V03X_H], right[MT9V03X_H];
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static uint8 key_level = GPIO_HIGH, reset_level = GPIO_HIGH, module_ready = GPIO_HIGH;
static unsigned control_read_calls;
static unsigned key_initializations, reset_assertions;
static char network_text[40];
void gpio_init(int pin, int direction, uint8 level, uint32 mode)
{
    if (pin == C13) {
        assert(direction == GPI && mode == GPI_PULL_UP && level == GPIO_HIGH);
        ++key_initializations;
    } else {
        assert(pin == WIFI_SPI_RST_PIN && direction == GPO && mode == GPO_PUSH_PULL);
        reset_level = level;
        if (level == GPIO_LOW) ++reset_assertions;
    }
}
uint8 gpio_get_level(int pin) { assert(pin == C13 || pin == B17); return pin == C13 ? key_level : module_ready; }

static uint32 capture(int transport, const uint8 *data, uint32 size)
{
    selected_transport = transport;
    ++write_calls;
    if ((failure == 3 && write_calls >= 2) || (failure == 4 && write_calls >= 1)) return size;
    if (failure == 5 && write_calls == 2) {
        assert(size > 17);
        memcpy(wire + wire_length, data, 17);
        wire_length += 17;
        return size - 17; /* 重试必须从这些字节之后开始，不能重复发送。 */
    }
    if (failure == 6 && write_calls == 2) return size;
    if (failure == 7 && write_calls >= 2) {
        uint32 part = size < 17 ? size : 17;
        memcpy(wire + wire_length, data, part); wire_length += part;
        return size - part;
    }
    if (failure == 8 && write_calls == 2) return size + 1;
    if (failure >= 11 && failure <= 14 && write_calls >= (unsigned)(failure - 8)) return size;
    if (wire_length + size > sizeof(wire)) abort();
    memcpy(wire + wire_length, data, size);
    wire_length += size;
    return 0;
}
uint32 debug_send_buffer(const uint8 *p, uint32 n) { return capture(0, p, n); }
uint32 wifi_uart_send_buffer(const uint8 *p, uint32 n) { return capture(1, p, n); }
#define UNUSED_WRITER(name) uint32 name(const uint8 *p, uint32 n) { return capture(9,p,n); }
UNUSED_WRITER(wireless_uart_send_buffer)
UNUSED_WRITER(bluetooth_ch9141_send_buffer)
uint32 wifi_spi_send_buffer(const uint8 *p, uint32 n) { return capture(2,p,n); }
UNUSED_WRITER(ble6a20_send_buffer)
#define EMPTY_READER(name) uint32 name(uint8 *p, uint32 n) { (void)p; (void)n; return 0; }
EMPTY_READER(debug_read_ring_buffer)
EMPTY_READER(wifi_uart_read_buffer)
EMPTY_READER(wireless_uart_read_buffer)
EMPTY_READER(bluetooth_ch9141_read_buffer)
uint32 wifi_spi_read_buffer(uint8 *p, uint32 n)
{
    ++control_read_calls;
    if (n < 3U) return 0;
    memcpy(p, "W\r\n", 3); return 3;
}
EMPTY_READER(ble6a20_read_buffer)
void uart_init(int port, uint32 baud, int tx, int rx)
{
    assert(port == DEBUG_UART_INDEX && tx == DEBUG_UART_TX_PIN && rx == DEBUG_UART_RX_PIN);
    (void)baud;
}
void uart_rx_interrupt(int port, uint32 enable) { assert(port == DEBUG_UART_INDEX && enable == DEBUG_UART_USE_INTERRUPT); }
uint8 wifi_uart_init(char *ssid, char *password, int mode)
{
    assert(strcmp(ssid,"test-network") == 0 && strcmp(password,"test-password") == 0);
    assert(mode == WIFI_UART_STATION);
    return failure == 1;
}
uint8 wifi_uart_connect_tcp_servers(char *ip, char *port, int mode)
{
    assert(strcmp(ip,"192.168.1.2") == 0 && strcmp(port,"8080") == 0);
    assert(mode == WIFI_UART_COMMAND);
    return failure == 2;
}

uint8 wifi_spi_init(char *ssid, char *password)
{
    ++spi_initializations;
    reset_level = GPIO_HIGH; /* 真实供应商初始化会重新释放模块复位。 */
    assert(strcmp(ssid,"test-network") == 0);
#ifdef TEST_WIFI_OPEN
    assert(password == NULL);
#else
    assert(password && strcmp(password,"test-password") == 0);
#endif
    return failure == 1;
}
uint8 wifi_spi_socket_connect(char *kind, char *ip, char *port, char *local)
{
    ++socket_connections;
    assert(strcmp(kind,"TCP") == 0 && strcmp(ip,"192.168.1.2") == 0);
    assert(strcmp(port,"8080") == 0 && strcmp(local,"0") == 0);
    if (failure == 2) return 1;
    if (socket_connections > 1) {
        old_session_length = wire_length;
        wire_length = 0; write_calls = 0; /* 新建TCP字节流。 */
    }
    return 0;
}
uint8 wifi_spi_socket_disconnect(void) { ++disconnect_calls; return (uint8)disconnect_failure; }
void system_delay_ms(uint32 duration) { assert(duration == 20); ++delay_calls; }
void ips200_init(int kind) { assert(kind == IPS200_TYPE_SPI && display_dir_set); }
void ips200_set_dir(int dir) { assert(dir == IPS200_CROSSWISE || dir == IPS200_CROSSWISE_180); display_dir_set = 1; }
void ips200_set_font(int font) { (void)font; }
void ips200_set_color(uint16 pen,uint16 background) { (void)pen;(void)background; }
void ips200_clear(void) {}
void ips200_show_string(uint16 x,uint16 y,const char *s)
{
    assert(x+strlen(s)*8U<=320 && y+16<=240);
    if (y == 224) snprintf(network_text, sizeof(network_text), "%s", s);
    if (y == 0) snprintf(telemetry_line0, sizeof(telemetry_line0), "%s", s);
    if (y == 16) snprintf(telemetry_line1, sizeof(telemetry_line1), "%s", s);
}
void ips200_show_gray_image(uint16 x,uint16 y,const uint8 *p,uint16 w,uint16 h,uint16 dw,uint16 dh,uint8 threshold)
{
    assert(p && w==188 && h==120 && threshold==0);
    assert(dw && dh && x+dw<=320 && y+dh<=240);
    assert((uint32)dw*h==(uint32)dh*w); /* 保持摄像头画面宽高比例。 */
    ++preview_count;
    memcpy(preview,p,sizeof(preview));
}

#ifdef TEST_WIFI_SWITCH
static void press_switch(uint32 start)
{
    key_level = GPIO_HIGH; camera_debug_poll(start); camera_debug_poll(start + 30U);
    key_level = GPIO_LOW; camera_debug_poll(start + 31U); camera_debug_poll(start + 61U);
}

static int test_wifi_switch(int scenario)
{
    uint32 i;
    unsigned initializations;
    failure = 0;
    if (scenario == 1) key_level = GPIO_LOW;
    camera_display_init();
    if (scenario == 6) {
        CHECK(camera_debug_init(NULL, middle, right) != 0);
        press_switch(0);
        CHECK(camera_debug_status == CAMERA_DEBUG_CONFIG_ERROR && spi_initializations == 0);
        press_switch(100);
        CHECK(camera_debug_status == CAMERA_DEBUG_DISABLED && reset_level == GPIO_LOW);
        puts("PASS: invalid boundary configuration cannot start network from key"); return 0;
    }
    CHECK(camera_debug_init(left, middle, right) == 0);
    CHECK(camera_debug_status == CAMERA_DEBUG_DISABLED);
    CHECK(spi_initializations == 0 && socket_connections == 0 && write_calls == 0);
    CHECK(key_initializations == 1 && reset_level == GPIO_LOW && reset_assertions == 1);
    CHECK(strstr(network_text, "OFF") != NULL && strstr(network_text, "C13") != NULL);
#ifdef TEST_WIFI_SWITCH_INVALID
    press_switch(0);
    CHECK(camera_debug_status == CAMERA_DEBUG_CONFIG_ERROR && spi_initializations == 0);
    press_switch(100);
    CHECK(camera_debug_status == CAMERA_DEBUG_DISABLED);
    camera_debug_capture_frame(gray[0]); camera_debug_send_frame(binary_image[0]);
    CHECK(preview_count == 1 && write_calls == 0 && socket_connections == 0);
    puts("PASS: invalid credentials remain offline until requested and never reach hardware"); return 0;
#endif
    if (scenario == 0) {
        for (i = 0; i < 1000; ++i) {
            camera_debug_poll(i * 1000U);
            camera_debug_capture_frame(gray[0]); camera_debug_send_frame(binary_image[0]);
        }
        CHECK(preview_count == 1000 && spi_initializations == 0 && socket_connections == 0);
        CHECK(write_calls == 0 && disconnect_calls == 0 && delay_calls == 0);
        CHECK(camera_debug_error_count == 0 && camera_debug_reconnect_count == 0);
        puts("PASS: boot OFF keeps 1000 previews with zero network calls"); return 0;
    }
    if (scenario == 1) {
        camera_debug_poll(0); camera_debug_poll(1000);
        CHECK(spi_initializations == 0);
        key_level = GPIO_HIGH; camera_debug_poll(1001); camera_debug_poll(1030);
        key_level = GPIO_LOW; camera_debug_poll(1031); camera_debug_poll(1100);
        CHECK(spi_initializations == 0); /* 不足30ms的释放不能使长按变成启动。 */
        key_level = GPIO_HIGH; camera_debug_poll(1200); camera_debug_poll(1230);
        key_level = GPIO_LOW; camera_debug_poll(1231); camera_debug_poll(1260);
        CHECK(spi_initializations == 0);
        camera_debug_poll(1261);
        CHECK(spi_initializations == 1 && camera_debug_status == CAMERA_DEBUG_READY);
        camera_debug_poll(100000);
        CHECK(spi_initializations == 1 && camera_debug_status == CAMERA_DEBUG_READY);
        puts("PASS: boot-held key, bounce, stable release and long-press single toggle"); return 0;
    }
    if (scenario == 4) {
        press_switch(UINT32_MAX - 15U);
        CHECK(spi_initializations == 1 && camera_debug_status == CAMERA_DEBUG_READY);
        puts("PASS: key debounce works across uint32 timestamp wrap"); return 0;
    }
    if (scenario == 3) {
        failure = 2;
        press_switch(0);
        CHECK(camera_debug_status == CAMERA_DEBUG_CONNECT_ERROR && spi_initializations == 1);
        camera_debug_poll(100000); /* 模拟阻塞连接在很久以后才返回。 */
        camera_debug_poll(104999);
        CHECK(spi_initializations == 1 && camera_debug_reconnect_count == 0);
        camera_debug_poll(105000);
        CHECK(spi_initializations == 2 && camera_debug_reconnect_count == 1);
        press_switch(200000);
        CHECK(camera_debug_status == CAMERA_DEBUG_DISABLED && reset_level == GPIO_LOW);
        initializations = spi_initializations;
        camera_debug_poll(300000); camera_debug_poll(400000);
        CHECK(spi_initializations == initializations && disconnect_calls == 0);
        puts("PASS: failed key connection arms backoff after return; OFF cancels recovery"); return 0;
    }
    press_switch(0);
    CHECK(camera_debug_status == CAMERA_DEBUG_READY && spi_initializations == 1);
    memset(gray, 0x33, sizeof(gray));
    camera_debug_capture_frame(gray[0]);
    if (scenario == 5) {
        failure = 3; camera_debug_send_frame(binary_image[0]);
        CHECK(camera_debug_status == CAMERA_DEBUG_SEND_ERROR && camera_debug_error_count == 1);
    }
    disconnect_failure = 1;
    press_switch(100);
    CHECK(camera_debug_status == CAMERA_DEBUG_DISABLED && reset_level == GPIO_LOW);
    CHECK(disconnect_calls == 0); /* 关闭不能调用可能阻塞的网络断开函数。 */
    initializations = spi_initializations;
    camera_debug_poll(10000); camera_debug_send_frame(binary_image[0]);
    CHECK(spi_initializations == initializations && camera_debug_reconnect_count == 0);
    failure = 0;
    press_switch(20000);
    CHECK(camera_debug_status == CAMERA_DEBUG_READY && spi_initializations == 2);
    camera_debug_send_frame(binary_image[0]); CHECK(wire_length == 0);
    memset(gray, 0x77, sizeof(gray)); camera_debug_capture_frame(gray[0]);
    camera_debug_send_frame(binary_image[0]);
    CHECK(wire_length == 22936 && wire[0] == 0xaa && wire[8] == 0x77);
    CHECK(camera_debug_frames_sent == 1 && camera_debug_error_count == (unsigned)(scenario == 5));
    puts("PASS: OFF clears pending/failed frame and ON starts fresh complete stream"); return 0;
}
#endif

int main(int argc, char **argv)
{
    uint32 i, payload_size, dot_offset;
    const uint8 dot_header[] = {0xaa, 0x03, 0x03, 8, 120, 0, 7, 0};
    uint8 camera_header[] = {0xaa, 0x02, 0x43, 8, 188, 0, 120, 0};
    if (argc > 1) failure = atoi(argv[1]);
#ifdef TEST_WIFI_SWITCH
    return test_wifi_switch(failure);
#endif
    camera_display_init(); /* 真实驱动在初始化时读取屏幕方向。 */
#ifdef TEST_WIFI_CONTROL_RX
    {
        uint8 rx[64];
        CHECK(camera_debug_read_control(rx, sizeof(rx)) < 0 && control_read_calls == 0);
        CHECK(camera_debug_init(left,middle,right) == 0);
        module_ready = 0;
        CHECK(camera_debug_read_control(rx, sizeof(rx)) < 0 && control_read_calls == 0);
        module_ready = 1;
        CHECK(camera_debug_read_control(NULL, 1) < 0 && camera_debug_read_control(rx, 0) < 0);
        CHECK(camera_debug_read_control(rx, sizeof(rx)) == 3 && control_read_calls == 1);
        CHECK(memcmp(rx, "W\r\n", 3) == 0);
        camera_debug_status = CAMERA_DEBUG_SEND_ERROR;
        CHECK(camera_debug_read_control(rx, sizeof(rx)) < 0 && control_read_calls == 1);
        puts("PASS: RX only when link and module ready, correct bytes and invalid arguments"); return 0;
    }
#endif
#ifdef TEST_AUTONOMOUS_DISPLAY
    auto_status.active=1;auto_status.state=AUTO_DRIVE_RUNNING;auto_status.confidence=95;
    auto_status.distance_mm=123;auto_status.left_duty_permille=85;auto_status.right_duty_permille=87;
    camera_display_statistics();CHECK(strstr(telemetry_line0,"AUTO:RUN")!=NULL);
    CHECK(strstr(telemetry_line1,"85/87")!=NULL);
    auto_status.active=0;auto_status.state=AUTO_DRIVE_FAULT;auto_status.fault=AUTO_FAULT_CAMERA_STALE;
    camera_display_statistics();CHECK(strstr(telemetry_line0,"CAMERA")!=NULL);
    motor_status.remote_armed=1;motor_status.remote_link=1;
    camera_display_statistics();CHECK(strstr(telemetry_line0,"RC:")!=NULL);
    puts("PASS: autonomous status and remote display arbitration");return 0;
#endif
#ifdef TEST_REMOTE_DISPLAY
    motor_status.remote_link = 1;
    motor_status.remote_armed = 1;
    motor_status.remote_wait_neutral = 1;
    camera_display_statistics();
    CHECK(strstr(telemetry_line0, "Down") != NULL);
    motor_status.remote_wait_neutral = 0;
    motor_status.applied_permille = -100;
    servo_status.current_pulse_us = 1420;
    remote_status.received_commands = UINT32_MAX;
    camera_display_statistics();
    CHECK(strstr(telemetry_line0, "-100") && strstr(telemetry_line1, "1420"));
    motor_status.remote_armed = 0;
    camera_display_statistics(); CHECK(strstr(telemetry_line0, "C12") != NULL);
    puts("PASS: remote arm/neutral/drive and steering display"); return 0;
#endif
#ifdef TEST_MOTOR_TELEMETRY
    motor_status.telemetry_visible = 1;
    motor_status.state = MOTOR_TEST_BOTH;
    encoder_status.left.delta_counts = INT16_MIN;
    encoder_status.left.counts_per_second = -3276800;
    encoder_status.left.total_counts = INT32_MIN;
    encoder_status.left.total_saturated = 1;
    encoder_status.right.delta_counts = INT16_MAX;
    encoder_status.right.counts_per_second = 3276700;
    encoder_status.right.total_counts = INT32_MAX;
    camera_display_statistics();
    CHECK(strstr(telemetry_line0, "L! d:-32768") && strstr(telemetry_line0, "n:-2147483648"));
    CHECK(strstr(telemetry_line1, "R d:32767") && strstr(telemetry_line1, "n:2147483647 B"));
    encoder_status.sample_sequence = 100;
    encoder_status.left.wheel_rpm_x10 = -1;
    encoder_status.left.speed_mm_s = -2;
    encoder_status.right.wheel_rpm_x10 = INT32_MIN;
    encoder_status.right.speed_mm_s = INT32_MIN;
    camera_display_statistics();
    CHECK(strstr(telemetry_line0, "-0.1rpm -2mm/s"));
    CHECK(strstr(telemetry_line1, "-214748364.8rpm -2147483648mm/s B"));
    camera_display_frame(gray[0]); CHECK(preview_count == 1);
    motor_status.telemetry_visible = 0;
    camera_display_statistics();
    CHECK(strstr(telemetry_line0, "TX:") && strstr(telemetry_line1, "S:"));
    motor_status.state = MOTOR_TEST_CONFIG_ERROR;
    camera_display_statistics(); CHECK(strstr(telemetry_line0, "MOTOR CONFIG ERROR"));
    puts("PASS: encoder telemetry bounds, signed speed, phase, image and original status restoration");
    return 0;
#endif
    for (i=0; i<sizeof(gray); ++i) {
        ((uint8 *)gray)[i] = (uint8)(i % 251U);
        ((uint8 *)binary_image)[i] = (i & 1U) ? 255 : 0;
    }
    for (i=0; i<MT9V03X_H; ++i) {
        left[i]=(uint8)(3+i%17); middle[i]=(uint8)(80+i%21); right[i]=(uint8)(170+i%17);
    }
    {
        uint8 result = camera_debug_init(left,middle,right);
#if !CAMERA_DEBUG_ENABLED
        CHECK(result == 0 && camera_debug_status == CAMERA_DEBUG_DISABLED);
        camera_debug_capture_frame(gray[0]);
        camera_debug_send_frame(binary_image[0]);
        CHECK(wire_length == 0 && preview_count == 1);
        puts("PASS: local preview works with network disabled"); return 0;
#elif defined(TEST_WIFI_MISSING_CONFIG)
        CHECK(result != 0 && camera_debug_status == CAMERA_DEBUG_CONFIG_ERROR);
        CHECK(spi_initializations == 0 && socket_connections == 0);
        camera_debug_capture_frame(gray[0]); camera_debug_send_frame(binary_image[0]);
        camera_debug_poll(0); camera_debug_poll(60000);
        CHECK(spi_initializations == 0 && socket_connections == 0);
        CHECK(wire_length == 0 && preview_count == 1);
        puts("PASS: invalid network configuration rejected; preview still works"); return 0;
#else
        if (failure == 1 || failure == 2) {
            CHECK(result != 0);
            CHECK(camera_debug_status == (failure == 1 ? CAMERA_DEBUG_INIT_ERROR : CAMERA_DEBUG_CONNECT_ERROR));
            camera_debug_capture_frame(gray[0]); camera_debug_send_frame(binary_image[0]);
            CHECK(wire_length == 0 && preview_count == 1);
            CHECK(spi_initializations == 1 && socket_connections == (unsigned)(failure == 2));
            CHECK(camera_debug_error_count == 1);
            camera_debug_poll(1000); camera_debug_poll(5999);
            CHECK(spi_initializations == 1);
            camera_debug_poll(6000); /* 恢复失败后必须重新计算回退间隔。 */
            CHECK(spi_initializations == 2 && camera_debug_reconnect_count == 1);
            CHECK(camera_debug_error_count == 2);
            camera_debug_poll(50000); /* 模拟长时间阻塞的连接尝试。 */
            CHECK(spi_initializations == 2);
            camera_debug_poll(54999); CHECK(spi_initializations == 2);
            failure = 0;
            camera_debug_poll(55000);
            CHECK(spi_initializations == 3 && camera_debug_reconnect_count == 2);
            CHECK(camera_debug_status == CAMERA_DEBUG_READY && camera_debug_error_count == 2);
            camera_debug_send_frame(binary_image[0]); CHECK(wire_length == 0);
            camera_debug_capture_frame(gray[0]); camera_debug_send_frame(binary_image[0]);
            CHECK(camera_debug_frames_sent == 1 && wire_length > 8);
            puts("PASS: init/connect failure recovers with completion-based backoff; preview survives"); return 0;
        }
        CHECK(result == 0 && camera_debug_status == CAMERA_DEBUG_READY);
        CHECK(spi_initializations == 1 && socket_connections == 1);
#endif
    }
    camera_debug_send_frame(binary_image[0]);
    CHECK(wire_length == 0); /* 不得发送未准备好的帧或过期帧。 */
    camera_debug_capture_frame(gray[0]);
    CHECK(preview_count == 1 && memcmp(preview, gray, sizeof(preview)) == 0);
    memset(gray, 0xee, sizeof(gray)); /* 适配模块必须拥有稳定的图像副本。 */
    camera_debug_send_frame(binary_image[0]);
    if (failure >= 11 && failure <= 14) {
        const uint32 prefix[] = {22568,22576,22696,22816};
        const camera_debug_stage_t stage[] = {CAMERA_DEBUG_STAGE_BOUNDARY_HEADER,CAMERA_DEBUG_STAGE_LEFT,CAMERA_DEBUG_STAGE_MIDDLE,CAMERA_DEBUG_STAGE_RIGHT};
        CHECK(camera_debug_status == CAMERA_DEBUG_SEND_ERROR && camera_debug_error_count == 1);
        CHECK(camera_debug_last_error_stage == stage[failure-11]);
        CHECK(camera_debug_last_remaining == (failure == 11 ? 8U : 120U));
        CHECK(wire_length == prefix[failure-11]);
        CHECK(write_calls == (unsigned)(failure-6));
        puts("PASS: failure stage identifies the boundary block without sending later blocks"); return 0;
    }
    if (failure == 3 || failure == 4 || failure == 7 || failure == 8) {
        unsigned expected_calls = failure == 4 ? 3U : failure == 8 ? 2U : 4U;
        uint32 partial_size = failure == 4 ? 0U : failure == 7 ? 59U : 8U;
        CHECK(camera_debug_status == CAMERA_DEBUG_SEND_ERROR && write_calls == expected_calls);
        CHECK(camera_debug_error_count == 1 && camera_debug_frames_sent == 0);
        CHECK(camera_debug_last_error_stage == (failure == 4 ? CAMERA_DEBUG_STAGE_IMAGE_HEADER : CAMERA_DEBUG_STAGE_IMAGE));
        CHECK(camera_debug_last_remaining == (failure == 4 ? 8U : failure == 7 ? 22509U : failure == 8 ? 22561U : 22560U));
        CHECK(wire_length == partial_size);
        camera_debug_capture_frame(gray[0]); camera_debug_send_frame(binary_image[0]);
        CHECK(write_calls == expected_calls && preview_count == 2);
        /* 无符号时间戳回绕不能提前触发或阻止恢复。 */
        camera_debug_poll(UINT32_MAX - 1000U);
        camera_debug_poll(3998U); CHECK(spi_initializations == 1);
        failure = 0;
        camera_debug_poll(3999U);
        CHECK(spi_initializations == 2 && socket_connections == 2 && disconnect_calls == 1);
        CHECK(old_session_length == partial_size && wire_length == 0);
        CHECK(camera_debug_status == CAMERA_DEBUG_READY && camera_debug_error_count == 1);
        CHECK(camera_debug_reconnect_count == 1);
        camera_debug_send_frame(binary_image[0]); CHECK(wire_length == 0); /* 不发送过期帧。 */
        camera_debug_capture_frame(gray[0]); camera_debug_send_frame(binary_image[0]);
        CHECK(camera_debug_frames_sent == 1 && wire_length == 22936U);
        CHECK(memcmp(wire,camera_header,8) == 0);
        for (i=0; i<22560U; ++i) CHECK(wire[8+i] == 0xee);
        CHECK(memcmp(wire+22568,dot_header,8) == 0);
        CHECK(memcmp(wire+22576,left,120) == 0 && memcmp(wire+22696,middle,120) == 0 && memcmp(wire+22816,right,120) == 0);
        camera_debug_poll(90000U); CHECK(spi_initializations == 2);
        puts("PASS: bounded suffix retries, failure metadata, wrap-safe reconnect and clean new frame"); return 0;
    }
    if (failure == 5 || failure == 6) {
        CHECK(camera_debug_status == CAMERA_DEBUG_READY && camera_debug_error_count == 0);
        CHECK(camera_debug_send_retry_count == 1 && delay_calls == 1 && camera_debug_reconnect_count == 0);
    }
    CHECK(selected_transport == 2); /* 必须使用SPI通路，不能误走旧串口。 */
#if CAMERA_DEBUG_IMAGE_MODE == CAMERA_DEBUG_BOUNDARIES_ONLY
    payload_size = 0; camera_header[2] = 0x53;
#else
    payload_size = 188U * 120U;
#endif
    CHECK(wire_length == 8 + payload_size + 8 + 3*120);
    CHECK(memcmp(wire,camera_header,8) == 0);
    for (i=0; i<payload_size; ++i) {
#if CAMERA_DEBUG_IMAGE_MODE == CAMERA_DEBUG_BINARY
        CHECK(wire[8+i] == ((i & 1U) ? 255 : 0));
#else
        CHECK(wire[8+i] == (uint8)(i % 251U));
#endif
    }
    dot_offset = 8 + payload_size;
    CHECK(memcmp(wire+dot_offset,dot_header,8) == 0);
    CHECK(memcmp(wire+dot_offset+8,left,120) == 0);
    CHECK(memcmp(wire+dot_offset+128,middle,120) == 0);
    CHECK(memcmp(wire+dot_offset+248,right,120) == 0);
    {
        uint32 previous = wire_length;
        camera_debug_send_frame(binary_image[0]); CHECK(wire_length == previous);
#if CAMERA_DEBUG_FRAME_DIVIDER == 2
        camera_debug_capture_frame(gray[0]); camera_debug_send_frame(binary_image[0]);
        CHECK(wire_length == previous);
        camera_debug_capture_frame(gray[0]); camera_debug_send_frame(binary_image[0]);
        CHECK(wire_length == 2 * previous && camera_debug_frames_sent == 2);
#endif
    }
    if (failure == 10) {
        CHECK(camera_debug_frames_sent == 1);
        write_calls = 0; failure = 3;
        camera_debug_capture_frame(gray[0]); camera_debug_send_frame(binary_image[0]);
        CHECK(camera_debug_frames_sent == 1 && camera_debug_error_count == 1);
        camera_debug_poll(100); failure = 0; disconnect_failure = 1;
        camera_debug_poll(5100);
        CHECK(disconnect_calls == 1 && spi_initializations == 2 && socket_connections == 2);
        CHECK(camera_debug_status == CAMERA_DEBUG_READY && wire_length == 0);
        CHECK(camera_debug_frames_sent == 1 && camera_debug_error_count == 1 && camera_debug_reconnect_count == 1);
        camera_debug_capture_frame(gray[0]); camera_debug_send_frame(binary_image[0]);
        CHECK(camera_debug_frames_sent == 2 && wire_length == 22936U && memcmp(wire,camera_header,8) == 0);
        puts("PASS: failed graceful close still resets connection; cumulative success count survives"); return 0;
    }
    if (failure == 9) {
        for (i=0; i<1000U; ++i) {
            wire_length = 0; write_calls = 0; failure = 5;
            camera_debug_capture_frame(gray[0]); camera_debug_send_frame(binary_image[0]);
            CHECK(wire_length == 22936U && memcmp(wire,camera_header,8) == 0);
            CHECK(memcmp(wire+8,gray,sizeof(gray)) == 0);
            CHECK(memcmp(wire+22568,dot_header,8) == 0);
        }
        CHECK(camera_debug_frames_sent == 1001U && camera_debug_send_retry_count == 1000U);
        CHECK(camera_debug_error_count == 0 && spi_initializations == 1);
        puts("PASS: 1000 transient partial sends keep complete frames flowing"); return 0;
    }
    puts("PASS: packet bytes, image snapshot, real boundaries and transport");
    return 0;
}
