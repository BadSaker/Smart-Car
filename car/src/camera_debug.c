/* Application adaptation of the SeekFree Assistant MT9V03X example.
 * Original example: Copyright (c) 2022 SEEKFREE 逐飞科技.
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See car/library/LICENSE and docs/seekfree-assistant-debugging.md for provenance.
 * This adapter keeps the existing V3.11.2 protocol library unchanged. */
#include "zf_common_headfile.h"
#include "camera_debug.h"
#include "camera_display.h"
#include "remote_control_config.h"

volatile camera_debug_status_t camera_debug_status = CAMERA_DEBUG_DISABLED;
volatile uint32_t camera_debug_frames_sent = 0;
volatile uint32_t camera_debug_error_count = 0;
volatile uint32_t camera_debug_send_retry_count = 0;
volatile uint32_t camera_debug_reconnect_count = 0;
volatile camera_debug_stage_t camera_debug_last_error_stage = CAMERA_DEBUG_STAGE_NONE;
volatile uint32_t camera_debug_last_remaining = 0;
static uint8_t display_timer_armed;
static uint32_t display_updated_ms;
static uint32_t (*scheduler_clock)(void);
static uint8_t (*scheduler_driving)(void);
static void (*scheduler_network_busy)(uint8_t);

void camera_debug_configure_scheduler(uint32_t (*clock_ms)(void), uint8_t (*driving)(void),
                                      void (*network_busy)(uint8_t))
{
    scheduler_clock=clock_ms;scheduler_driving=driving;scheduler_network_busy=network_busy;
}

#if CAMERA_DEBUG_ENABLED
#if MT9V03X_W > 255 || MT9V03X_H > 255
#error "The application boundary arrays use 8-bit coordinates"
#endif

extern seekfree_assistant_transfer_callback_function seekfree_assistant_transfer_callback;
static seekfree_assistant_transfer_callback_function transport_write;
static uint32_t frame_sequence;
static uint8_t frame_available;
static uint8_t block_index;
static uint8_t retry_timer_armed;
static uint32_t retry_started_ms;
/* 保存完整逐飞协议帧，发送期间不引用下一幅图或可变边线。 */
static uint8_t queued_frame[MT9V03X_W*MT9V03X_H+3*MT9V03X_H+128];
static uint32_t queued_size,queued_offset,queue_progress_ms,queue_poll_ms;
static uint8_t queue_building;
static uint8_t *boundary_left, *boundary_middle, *boundary_right;
static uint8_t wifi_enabled;
static uint8_t wifi_key_sample, wifi_key_stable, wifi_key_armed, wifi_key_timer_armed;
static uint32_t wifi_key_changed_ms;
#if CAMERA_DEBUG_IMAGE_MODE != CAMERA_DEBUG_BOUNDARIES_ONLY
static uint8_t transmit_image[MT9V03X_H][MT9V03X_W];
#endif

static uint8_t valid_port(const char *text, uint32 minimum)
{
    uint32 value = 0;
    size_t i, length = strlen(text);
    if (length == 0U || length > 5U) return 0;
    for (i = 0; i < length; ++i)
    {
        if (text[i] < '0' || text[i] > '9') return 0;
        value = value * 10U + (uint32)(text[i] - '0');
    }
    return value >= minimum && value <= 65535U;
}

static uint8_t valid_ipv4(const char *text)
{
    uint32 part = 0, digits = 0, dots = 0;
    if (strlen(text) > 15U) return 0;
    for (;; ++text)
    {
        if (*text >= '0' && *text <= '9')
        {
            part = part * 10U + (uint32)(*text - '0');
            if (++digits > 3U || part > 255U) return 0;
        }
        else if (*text == '.' || *text == '\0')
        {
            if (digits == 0U) return 0;
            if (*text == '\0') return dots == 3U;
            if (++dots > 3U) return 0;
            part = 0; digits = 0;
        }
        else return 0;
    }
}

static uint8_t valid_credentials(const char *ssid, const char *password)
{
    size_t ssid_length = strlen(ssid), password_length = strlen(password);
    /* 两组CRLF与字符串结束符也必须放入供应商64字节命令缓冲。 */
    return ssid_length > 0U && ssid_length <= 32U &&
           ssid_length + password_length <= 59U &&
           strpbrk(ssid, "\r\n") == NULL && strpbrk(password, "\r\n") == NULL;
}

static void record_error(camera_debug_status_t status, camera_debug_stage_t stage, uint32 remaining)
{
    camera_debug_status = status;
    camera_debug_last_error_stage = stage;
    camera_debug_last_remaining = remaining;
    ++camera_debug_error_count;
    retry_timer_armed = 0;
    camera_display_network_status(status);
    camera_display_statistics();
}

static camera_debug_stage_t transfer_stage(uint8_t index)
{
    if (index == 0U) return CAMERA_DEBUG_STAGE_IMAGE_HEADER;
#if CAMERA_DEBUG_IMAGE_MODE != CAMERA_DEBUG_BOUNDARIES_ONLY
    if (index == 1U) return CAMERA_DEBUG_STAGE_IMAGE;
    --index;
#endif
    switch (index)
    {
        case 1: return CAMERA_DEBUG_STAGE_BOUNDARY_HEADER;
        case 2: return CAMERA_DEBUG_STAGE_LEFT;
        case 3: return CAMERA_DEBUG_STAGE_MIDDLE;
        default: return CAMERA_DEBUG_STAGE_RIGHT;
    }
}

/* 驱动返回未发送的后缀长度，已确认字节不能重复发送。
 * 供应商协议不处理回调错误，因此失败后阻止本帧后续协议块。 */
static uint32 camera_debug_transfer(const uint8 *data, uint32 length)
{
    uint32 pending = length, retries = 0;
    camera_debug_stage_t stage = transfer_stage(block_index++);
    if(scheduler_clock) {
        if(!queue_building || camera_debug_status!=CAMERA_DEBUG_READY ||
           length>sizeof queued_frame-queued_size) {
            record_error(CAMERA_DEBUG_SEND_ERROR,stage,length);return length;
        }
        memcpy(queued_frame+queued_size,data,length);queued_size+=length;return 0;
    }
    if (camera_debug_status != CAMERA_DEBUG_READY || transport_write == NULL)
        return length;
    while (pending != 0U)
    {
        uint32 remaining = transport_write(data, pending);
        if (remaining > pending)
        {
            /* 回调结果越界，不能继续信任已发送偏移。 */
            record_error(CAMERA_DEBUG_SEND_ERROR, stage, remaining);
            return pending;
        }
        data += pending - remaining;
        pending = remaining;
        if (pending == 0U) return 0;
        if (retries == CAMERA_DEBUG_SEND_RETRY_LIMIT) break;
        ++retries;
        ++camera_debug_send_retry_count;
        system_delay_ms(CAMERA_DEBUG_SEND_RETRY_DELAY_MS);
    }
    record_error(CAMERA_DEBUG_SEND_ERROR, stage, pending);
    return pending;
}

static uint8_t connect_transport_sync(void)
{
    char ssid[] = CAMERA_DEBUG_WIFI_SSID;
    char password[] = CAMERA_DEBUG_WIFI_PASSWORD;
    char server_ip[] = CAMERA_DEBUG_WIFI_SERVER_IP;
    char server_port[] = CAMERA_DEBUG_WIFI_SERVER_PORT;
    char local_port[] = CAMERA_DEBUG_WIFI_LOCAL_PORT;
    queued_size=queued_offset=0;queue_building=0;
    frame_available = 0;
    transport_write = NULL;
    frame_sequence = 0;
    if (!valid_credentials(ssid, password) || !valid_ipv4(server_ip) ||
        !valid_port(server_port, 1U) ||
        (strcmp(local_port, "0") != 0 && !valid_port(local_port, 2048U)))
    {
        record_error(CAMERA_DEBUG_CONFIG_ERROR, CAMERA_DEBUG_STAGE_CONFIG, 0);
        return 1;
    }
    camera_debug_status = CAMERA_DEBUG_INITIALIZING;
    camera_display_network_status(camera_debug_status);
    /* wifi_spi_init通过硬件RST清除旧连接及模块中残留的半帧数据。 */
    if (wifi_spi_init(ssid, password[0] != '\0' ? password : NULL))
    {
        record_error(CAMERA_DEBUG_INIT_ERROR, CAMERA_DEBUG_STAGE_WIFI, 0);
        return 1;
    }
    camera_debug_status = CAMERA_DEBUG_CONNECTING;
    camera_display_network_status(camera_debug_status);
    if (wifi_spi_socket_connect("TCP", server_ip, server_port, local_port))
    {
        record_error(CAMERA_DEBUG_CONNECT_ERROR, CAMERA_DEBUG_STAGE_TCP, 0);
        return 1;
    }
    seekfree_assistant_interface_init(SEEKFREE_ASSISTANT_WIFI_SPI);
    transport_write = seekfree_assistant_transfer_callback;
    seekfree_assistant_transfer_callback = camera_debug_transfer;
#if CAMERA_DEBUG_IMAGE_MODE == CAMERA_DEBUG_BOUNDARIES_ONLY
    seekfree_assistant_camera_information_config(SEEKFREE_ASSISTANT_MT9V03X, NULL, MT9V03X_W, MT9V03X_H);
#else
    seekfree_assistant_camera_information_config(SEEKFREE_ASSISTANT_MT9V03X, transmit_image[0], MT9V03X_W, MT9V03X_H);
#endif
    seekfree_assistant_camera_boundary_config(X_BOUNDARY, MT9V03X_H, boundary_left, boundary_middle, boundary_right, NULL, NULL, NULL);
    retry_timer_armed = 0;
    camera_debug_status = CAMERA_DEBUG_READY;
    camera_display_network_status(camera_debug_status);
    return 0;
}

static uint8_t connect_transport(void)
{
    uint8_t result;
    /* 先阻止新起跑，再核对已有运行；PIT在两次操作之间也不能穿过交接。 */
    if(scheduler_network_busy)scheduler_network_busy(1);
    if(scheduler_driving && scheduler_driving()) {
        camera_debug_status=CAMERA_DEBUG_INITIALIZING;
        if(scheduler_network_busy)scheduler_network_busy(0);
        return 0;
    }
    result=connect_transport_sync();
    if(scheduler_network_busy)scheduler_network_busy(0);
    return result;
}

/* 只在主循环切换；不能在中断中复位正在发送的模块。 */
static uint8_t camera_debug_set_wifi_enabled(uint8_t enabled)
{
    wifi_enabled = enabled;
    frame_available = 0;
    transport_write = NULL;
    retry_timer_armed = 0;
    queued_size=queued_offset=0;
    if (!enabled)
    {
        /* 本地GPIO保持复位，避免关闭时还调用阻塞式网络断开/查询。
         * 下次开启由原wifi_spi_init重新配置并释放复位。 */
        gpio_init(WIFI_SPI_RST_PIN, GPO, GPIO_LOW, GPO_PUSH_PULL);
        camera_debug_status = CAMERA_DEBUG_DISABLED;
        camera_display_network_status(camera_debug_status);
        return 0;
    }
    if (boundary_left == NULL || boundary_middle == NULL || boundary_right == NULL)
    {
        record_error(CAMERA_DEBUG_CONFIG_ERROR, CAMERA_DEBUG_STAGE_CONFIG, 0);
        return 1;
    }
    /* 建链包含100ms复位等待及秒级命令，不能在行驶中调用。 */
    if(scheduler_driving && scheduler_driving()) {
        camera_debug_status=CAMERA_DEBUG_INITIALIZING;return 0;
    }
    return connect_transport();
}

static uint8_t camera_debug_poll_key(uint32_t now_ms)
{
    uint8_t sample = gpio_get_level(CAMERA_DEBUG_WIFI_KEY_PIN);
    if (!wifi_key_timer_armed)
    {
        wifi_key_timer_armed = 1;
        wifi_key_sample = sample;
        wifi_key_changed_ms = now_ms;
        return 0;
    }
    if (sample != wifi_key_sample)
    {
        wifi_key_sample = sample;
        wifi_key_changed_ms = now_ms;
    }
    if ((uint32_t)(now_ms - wifi_key_changed_ms) < CAMERA_DEBUG_WIFI_KEY_DEBOUNCE_MS) return 0;
    if (sample != CAMERA_DEBUG_WIFI_KEY_ACTIVE)
    {
        wifi_key_stable = sample;
        wifi_key_armed = 1;
    }
    else if (wifi_key_stable != sample)
    {
        wifi_key_stable = sample;
        if (wifi_key_armed)
        {
            wifi_key_armed = 0;
            (void)camera_debug_set_wifi_enabled(!wifi_enabled);
            return 1;
        }
    }
    return 0;
}
#endif

uint8_t camera_debug_init(uint8_t *left, uint8_t *middle, uint8_t *right)
{
    camera_debug_status = CAMERA_DEBUG_DISABLED;
    camera_debug_frames_sent = 0;
    camera_debug_error_count = 0;
    camera_debug_send_retry_count = 0;
    camera_debug_reconnect_count = 0;
    camera_debug_last_error_stage = CAMERA_DEBUG_STAGE_NONE;
    camera_debug_last_remaining = 0;
    display_timer_armed = 0;
#if CAMERA_DEBUG_ENABLED
    retry_timer_armed = 0;
    frame_available = 0;
    transport_write = NULL;
    boundary_left = left; boundary_middle = middle; boundary_right = right;
    wifi_key_timer_armed = 0;
    wifi_key_armed = 0;
    wifi_key_stable = CAMERA_DEBUG_WIFI_KEY_ACTIVE;
    gpio_init(CAMERA_DEBUG_WIFI_KEY_PIN, GPI, GPIO_HIGH, GPI_PULL_UP);
    (void)camera_debug_set_wifi_enabled(0);
    if (left == NULL || middle == NULL || right == NULL)
    {
        record_error(CAMERA_DEBUG_CONFIG_ERROR, CAMERA_DEBUG_STAGE_CONFIG, 0);
        return 1;
    }
#if CAMERA_DEBUG_WIFI_BOOT_ENABLED
    return camera_debug_set_wifi_enabled(1);
#else
    return 0;
#endif
#else
    (void)left; (void)middle; (void)right;
    camera_display_network_status(camera_debug_status);
    return 0;
#endif
}

void camera_debug_poll(uint32_t now_ms)
{
#if CAMERA_DEBUG_ENABLED
    /* 按键触发的连接可能阻塞；返回后不能用调用前时间锚定失败回退。 */
    if (camera_debug_poll_key(now_ms)) return;
    if(scheduler_clock && wifi_enabled && camera_debug_status==CAMERA_DEBUG_READY &&
       (uint32_t)(now_ms-queue_poll_ms)>=1U) {
        uint32_t pending,remaining=queued_size-queued_offset;
        queue_poll_ms=now_ms;
        /* 底层只检查一次INT并执行一次至多4088字节的事务，绝不等模块。 */
        pending=wifi_spi_try_send_buffer(remaining?queued_frame+queued_offset:NULL,remaining);
        if(pending>remaining) {
            record_error(CAMERA_DEBUG_SEND_ERROR,CAMERA_DEBUG_STAGE_IMAGE,pending);
        } else if(remaining) {
            if(pending<remaining)queue_progress_ms=now_ms;
            queued_offset+=remaining-pending;
            if(queued_offset==queued_size) {
                queued_size=queued_offset=0;++camera_debug_frames_sent;
            } else if((uint32_t)(now_ms-queue_progress_ms)>=1000U) {
                record_error(CAMERA_DEBUG_SEND_ERROR,CAMERA_DEBUG_STAGE_IMAGE,pending);
            }
        }
        if(camera_debug_status==CAMERA_DEBUG_SEND_ERROR) {
            /* 已发半帧不得与新帧拼接；先复位隔离旧TCP，再等停车后建链。 */
            gpio_init(WIFI_SPI_RST_PIN,GPO,GPIO_LOW,GPO_PUSH_PULL);
            queued_size=queued_offset=0;
        }
    }
#endif
    if (!display_timer_armed || (uint32_t)(now_ms - display_updated_ms) >= 250U)
    {
        display_timer_armed = 1;
        display_updated_ms = now_ms;
        camera_display_statistics();
    }
#if CAMERA_DEBUG_ENABLED
    if (!wifi_enabled) return; // 关闭时只处理本地按键和统计，不查询或重连网络。
    if(scheduler_driving && scheduler_driving())return;
    if(scheduler_clock && camera_debug_status==CAMERA_DEBUG_INITIALIZING) {
        (void)connect_transport();return;
    }
    if (camera_debug_status == CAMERA_DEBUG_INIT_ERROR ||
        camera_debug_status == CAMERA_DEBUG_CONNECT_ERROR ||
        camera_debug_status == CAMERA_DEBUG_SEND_ERROR)
    {
        if (!retry_timer_armed)
        {
            /* 从失败的阻塞调用返回后的首次轮询开始计算回退时间。 */
            retry_started_ms = now_ms;
            retry_timer_armed = 1;
        }
        else if ((uint32_t)(now_ms - retry_started_ms) >= CAMERA_DEBUG_RECONNECT_INTERVAL_MS)
        {
            uint8_t close_old_socket = camera_debug_status == CAMERA_DEBUG_SEND_ERROR;
            retry_timer_armed = 0;
            ++camera_debug_reconnect_count;
            camera_debug_status = CAMERA_DEBUG_INITIALIZING;
            camera_display_network_status(camera_debug_status);
            camera_display_statistics();
            /* 先尝试正常关闭；即使关闭失败，也继续执行硬件复位。 */
            if (close_old_socket && !scheduler_clock) (void)wifi_spi_socket_disconnect();
            (void)connect_transport();
            display_timer_armed = 0;
        }
    }
#endif
}

void camera_debug_capture_frame(const uint8_t *gray_image)
{
    /* 二值化前显示原始灰度；此处的显示条件不依赖无线状态。 */
    camera_display_frame(gray_image);
#if CAMERA_DEBUG_ENABLED
    if (camera_debug_status != CAMERA_DEBUG_READY || gray_image == NULL) return;
#if CAMERA_DEBUG_IMAGE_MODE == CAMERA_DEBUG_GRAY
    memcpy(transmit_image[0], gray_image, sizeof(transmit_image));
#endif
    frame_available = 1;
#else
    (void)gray_image;
#endif
}

void camera_debug_send_frame(const uint8_t *binary_image)
{
#if CAMERA_DEBUG_ENABLED
    if (camera_debug_status != CAMERA_DEBUG_READY || !frame_available) return;
    frame_available = 0;
    /* 正在发送的快照保持不动；跳过尚未发送的新帧，不排队旧画面。 */
    if(scheduler_clock && queued_size)return;
    if (frame_sequence++ % CAMERA_DEBUG_FRAME_DIVIDER != 0U) return;
#if CAMERA_DEBUG_IMAGE_MODE == CAMERA_DEBUG_BINARY
    if (binary_image == NULL) return;
    memcpy(transmit_image[0], binary_image, sizeof(transmit_image));
#else
    (void)binary_image;
#endif
    block_index = 0;
    if(scheduler_clock) {
        queued_offset=queued_size=0;queue_building=1;queue_progress_ms=scheduler_clock();
    }
    seekfree_assistant_camera_send();
    queue_building=0;
    if (!scheduler_clock && camera_debug_status == CAMERA_DEBUG_READY) ++camera_debug_frames_sent;
#else
    (void)binary_image;
#endif
}

int32_t camera_debug_read_control(uint8_t *buffer, uint32_t capacity)
{
#if CAMERA_DEBUG_ENABLED && REMOTE_CONTROL_ENABLED
    if (buffer == NULL || capacity == 0U || camera_debug_status != CAMERA_DEBUG_READY) return -1;
    if (capacity > REMOTE_CONTROL_RX_CHUNK_SIZE) capacity = REMOTE_CONTROL_RX_CHUNK_SIZE;
    if(scheduler_clock)return (int32_t)wifi_spi_read_buffer_cached(buffer,capacity);
    if (!gpio_get_level(WIFI_SPI_INT_PIN)) return -1;
    return (int32_t)wifi_spi_read_buffer(buffer, capacity);
#else
    (void)buffer; (void)capacity;
    return -1;
#endif
}
