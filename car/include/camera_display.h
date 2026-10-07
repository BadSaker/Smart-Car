#ifndef AUTOCAR_CAMERA_DISPLAY_H
#define AUTOCAR_CAMERA_DISPLAY_H
#include <stdint.h>
#include "camera_debug.h"
/* 仅主循环调用；普通IPS200 SPI使用RT1064 V3.0主板P6上排对应8针。 */
void camera_display_init(void);
void camera_display_message(const char *text);
void camera_display_network_status(camera_debug_status_t status);
void camera_display_statistics(void);
/* 显示原始灰度，不修改调用者提供的稳定图像副本。 */
void camera_display_frame(const uint8_t *gray_image);
#endif
