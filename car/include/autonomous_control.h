#ifndef AUTOCAR_AUTONOMOUS_CONTROL_H
#define AUTOCAR_AUTONOMOUS_CONTROL_H
#include "autonomous_drive.h"
#include "track_vision.h"
void autonomous_control_init(void);
void autonomous_control_publish_vision(const track_vision_result_t *vision,uint32_t sequence,uint32_t frame_ms);
void autonomous_control_tick_10ms(uint32_t now_ms);
uint8_t autonomous_control_service(uint32_t now_ms);
void autonomous_control_get_status(autonomous_drive_status_t *out);
uint8_t autonomous_control_is_active(void);
void autonomous_control_set_network_busy(uint8_t busy);
/* 调参只记录实际偏差、目标、输出、反馈以及首次硬故障。 */
typedef struct {
 uint32_t now_ms,frame_ms,sequence;
 int16_t raw_left_delta_counts,raw_right_delta_counts;
 autonomous_drive_status_t motion;
} autonomous_trace_frame_t;
typedef struct {
 autonomous_trace_frame_t latest,fault_frame;
 uint32_t run_generation;uint8_t frozen;
} autonomous_control_trace_t;
void autonomous_control_get_trace(autonomous_control_trace_t *out);
#endif
