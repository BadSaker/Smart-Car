#ifndef AUTOCAR_CAMERA_DEBUG_CONFIG_H
#define AUTOCAR_CAMERA_DEBUG_CONFIG_H

/* 逐飞助手1.2.7：Wi-Fi SPI以STA/TCP客户端连接电脑TCP服务器。
 * RT1064 V3.0主板：Wi-Fi SPI接P7，普通IPS200 SPI接P6。
 * 不使用串口图传或心跳诊断模式。 */
#define CAMERA_DEBUG_GRAY            0
#define CAMERA_DEBUG_BINARY          1
#define CAMERA_DEBUG_BOUNDARIES_ONLY 2
#ifndef CAMERA_DEBUG_ENABLED
#define CAMERA_DEBUG_ENABLED         1
#endif
/* C13/S4为Wi-Fi软件开关；默认关闭，先稳定释放再按下才切换。
 * 关闭时保持模块复位，不切断模块电源。 */
#ifndef CAMERA_DEBUG_WIFI_BOOT_ENABLED
#define CAMERA_DEBUG_WIFI_BOOT_ENABLED 0U
#endif
#define CAMERA_DEBUG_WIFI_KEY_PIN      C13
#define CAMERA_DEBUG_WIFI_KEY_ACTIVE   0U
#define CAMERA_DEBUG_WIFI_KEY_DEBOUNCE_MS 30U
#if CAMERA_DEBUG_WIFI_BOOT_ENABLED != 0 && CAMERA_DEBUG_WIFI_BOOT_ENABLED != 1
#error "Wi-Fi启动开关只允许0或1"
#endif
#ifndef CAMERA_DEBUG_IMAGE_MODE
#define CAMERA_DEBUG_IMAGE_MODE      CAMERA_DEBUG_GRAY
#endif
/* 发送首帧，之后每处理N帧发送一次；屏幕始终显示灰度。 */
#ifndef CAMERA_DEBUG_FRAME_DIVIDER
#define CAMERA_DEBUG_FRAME_DIVIDER   1U
#endif

/* 本机网络凭据：不要把密码复制到日志或共享报告。
 * 供应商命令缓冲为64字节，SSID与密码合计不得超过59字节。
 * 空密码表示开放网络；复位前先开启电脑端侦听。 */
#ifndef CAMERA_DEBUG_WIFI_SSID
#define CAMERA_DEBUG_WIFI_SSID "DESKTOP-AMQIB6T 7163"
#endif
#ifndef CAMERA_DEBUG_WIFI_PASSWORD
#define CAMERA_DEBUG_WIFI_PASSWORD "R1529k\\3"
#endif
#ifndef CAMERA_DEBUG_WIFI_SERVER_IP
#define CAMERA_DEBUG_WIFI_SERVER_IP "192.168.137.1"
#endif
#ifndef CAMERA_DEBUG_WIFI_SERVER_PORT
#define CAMERA_DEBUG_WIFI_SERVER_PORT "8080"
#endif
#ifndef CAMERA_DEBUG_WIFI_LOCAL_PORT
#define CAMERA_DEBUG_WIFI_LOCAL_PORT "0"
#endif

/* 只重试当前协议块尚未发送的后缀。 */
#ifndef CAMERA_DEBUG_SEND_RETRY_LIMIT
#define CAMERA_DEBUG_SEND_RETRY_LIMIT 2U
#endif
#ifndef CAMERA_DEBUG_SEND_RETRY_DELAY_MS
#define CAMERA_DEBUG_SEND_RETRY_DELAY_MS 20U
#endif
/* 回退间隔从失败调用返回后开始计算，而不是从调用开始计算。 */
#ifndef CAMERA_DEBUG_RECONNECT_INTERVAL_MS
#define CAMERA_DEBUG_RECONNECT_INTERVAL_MS 5000U
#endif
#if CAMERA_DEBUG_SEND_RETRY_LIMIT < 0 || CAMERA_DEBUG_SEND_RETRY_LIMIT > 10
#error "Send retry limit must be between 0 and 10"
#endif
#if CAMERA_DEBUG_SEND_RETRY_DELAY_MS < 1 || CAMERA_DEBUG_SEND_RETRY_DELAY_MS > 1000
#error "Send retry delay must be between 1 and 1000 ms"
#endif
#if CAMERA_DEBUG_RECONNECT_INTERVAL_MS < 1000 || CAMERA_DEBUG_RECONNECT_INTERVAL_MS > 60000
#error "Reconnect interval must be between 1000 and 60000 ms"
#endif

#if CAMERA_DEBUG_IMAGE_MODE < CAMERA_DEBUG_GRAY || CAMERA_DEBUG_IMAGE_MODE > CAMERA_DEBUG_BOUNDARIES_ONLY
#error "Unsupported camera debug image mode"
#endif
#if CAMERA_DEBUG_FRAME_DIVIDER < 1
#error "CAMERA_DEBUG_FRAME_DIVIDER must be at least 1"
#endif
#endif
