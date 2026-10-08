#ifndef AUTOCAR_AUTONOMOUS_CONTROL_H
#define AUTOCAR_AUTONOMOUS_CONTROL_H
#include "autonomous_drive.h"
#include "track_vision.h"
/* 初始化在PIT启动前调用；C15取代舵机摆动测试，C14/S3最高优先停车。 */
void autonomous_control_init(void);
/* 仅主循环完成图像处理后提交摘要。frame_ms来自CSI完成时刻，不用处理结束时间。 */
void autonomous_control_publish_vision(const track_vision_result_t *vision,uint32_t sequence,uint32_t frame_ms);
/* 仅PIT：encoder_feedback_tick之后、motor_control_tick之前调用。 */
void autonomous_control_tick_10ms(uint32_t now_ms);
/* 仅主循环：更新舵机并确认服务时刻；不联网、不刷屏。 */
uint8_t autonomous_control_service(uint32_t now_ms);
/* 短临界区快照；保存调用者PRIMASK，不在临界区等待硬件。 */
void autonomous_control_get_status(autonomous_drive_status_t *out);
uint8_t autonomous_control_is_active(void);
#endif
