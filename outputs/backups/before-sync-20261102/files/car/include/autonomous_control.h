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
/* 主循环同步建链期间禁止新起跑；不替代运行期视觉和舵机保护。 */
void autonomous_control_set_network_busy(uint8_t busy);
typedef struct {
    uint32_t now_ms,frame_ms,sequence;
    uint8_t confidence,minimum_confidence,valid,element;
    uint8_t ring_left_reject,ring_right_reject,left_island,right_island,entry_supported,fault_element;
    uint8_t left_fork,right_fork,left_opening_rows,right_opening_rows;
    int16_t near_error_px,far_error_px,steering_permille;
    int16_t raw_near_error_px,raw_far_error_px;
    track_element_diagnostic_t element_diagnostic;
    /* 触发本拍的原始反馈，以及本拍控制前真正执行过的目标/输出/积分。 */
    uint32_t feedback_sequence, servo_service_ms;
    int16_t raw_left_delta_counts, raw_right_delta_counts;
    int32_t left_speed_mm_s, right_speed_mm_s;
    uint16_t servo_pulse_us;
    autonomous_drive_status_t motion;
} autonomous_trace_frame_t;
typedef struct {
    autonomous_trace_frame_t last_good,first_bad,fault_frame;
    uint32_t run_generation;
    autonomous_fault_t fault;
    uint8_t has_last_good,has_first_bad,frozen;
} autonomous_control_trace_t;
/* 调试器/主循环只读；首次拒绝帧与故障帧分开，重新起跑清除。 */
void autonomous_control_get_trace(autonomous_control_trace_t *out);
#endif
