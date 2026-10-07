#ifndef AUTOCAR_CAMERA_DEBUG_H
#define AUTOCAR_CAMERA_DEBUG_H

#include <stdint.h>
#include "camera_debug_config.h"

typedef enum
{
    CAMERA_DEBUG_DISABLED,
    CAMERA_DEBUG_READY,
    CAMERA_DEBUG_CONFIG_ERROR,
    CAMERA_DEBUG_INIT_ERROR,
    CAMERA_DEBUG_CONNECT_ERROR,
    CAMERA_DEBUG_SEND_ERROR,
    CAMERA_DEBUG_INITIALIZING,
    CAMERA_DEBUG_CONNECTING
} camera_debug_status_t;

typedef enum
{
    CAMERA_DEBUG_STAGE_NONE,
    CAMERA_DEBUG_STAGE_CONFIG,
    CAMERA_DEBUG_STAGE_WIFI,
    CAMERA_DEBUG_STAGE_TCP,
    CAMERA_DEBUG_STAGE_IMAGE_HEADER,
    CAMERA_DEBUG_STAGE_IMAGE,
    CAMERA_DEBUG_STAGE_BOUNDARY_HEADER,
    CAMERA_DEBUG_STAGE_LEFT,
    CAMERA_DEBUG_STAGE_MIDDLE,
    CAMERA_DEBUG_STAGE_RIGHT
} camera_debug_stage_t;

extern volatile camera_debug_status_t camera_debug_status;
extern volatile uint32_t camera_debug_frames_sent;
extern volatile uint32_t camera_debug_error_count;
extern volatile uint32_t camera_debug_send_retry_count;
extern volatile uint32_t camera_debug_reconnect_count;
extern volatile camera_debug_stage_t camera_debug_last_error_stage;
extern volatile uint32_t camera_debug_last_remaining;

/* 返回0表示初始化成功或主动禁用，非0表示失败。
 * 默认只初始化C13按键并保持Wi-Fi模块复位，不联网。
 * 边线数组必须持续有效，每行保存一个X坐标。 */
uint8_t camera_debug_init(uint8_t *left, uint8_t *middle, uint8_t *right);
/* 每圈主循环调用，没有相机新帧时也必须调用。
 * now_ms为单调递增的uint32毫秒时钟，允许自然回绕。
 * C13稳定释放后新按下切换Wi-Fi，关闭时跳过网络工作。
 * 开启和恢复使用供应商同步接口，会暂停主循环并延后按键响应。
 * 出错后等待回退间隔，再关闭并复位模块，重新建立TCP连接。 */
void camera_debug_poll(uint32_t now_ms);
/* 仅主循环调用：图像修改前显示原始灰度并保存图传副本。
 * 网络失败或禁用时仍可调用本地预览，同步网络调用期间主循环会等待。 */
void camera_debug_capture_frame(const uint8_t *gray_image);
/* 仅主循环调用，在提取边线后、向图像叠加绘制前使用。
 * 二值视图每像素仍用一个灰度字节（0或255），不打包成单比特。
 * 发送为阻塞调用；部分发送只重试未发后缀。失败帧丢弃，
 * 必须由poll重新连接后才能发送后续新帧。 */
void camera_debug_send_frame(const uint8_t *binary_image);

/* 主循环唯一遥控接收入口：负值表示链路不可用或模块忙，0表示本次未读到数据。
 * 只在READY且INT就绪时调用原接收接口；原接口仍可能同步等待。
 * 调用者必须在返回后重新取时钟，丢弃耗时过长的结果。 */
int32_t camera_debug_read_control(uint8_t *buffer, uint32_t capacity);

#endif
