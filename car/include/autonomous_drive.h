#ifndef AUTOCAR_AUTONOMOUS_DRIVE_H
#define AUTOCAR_AUTONOMOUS_DRIVE_H
#include "control_pid.h"
#include "track_vision.h"
#include <stdint.h>
typedef enum { AUTO_DRIVE_IDLE=0,AUTO_DRIVE_STARTING,AUTO_DRIVE_RUNNING,AUTO_DRIVE_FINISHING,
    AUTO_DRIVE_FINISHED,AUTO_DRIVE_STOPPED,AUTO_DRIVE_FAULT } autonomous_state_t;
/* 保留已发布硬故障编号，已撤销的视觉故障不再占用接口。 */
typedef enum { AUTO_FAULT_NONE=0,AUTO_FAULT_CONFIG=1,AUTO_FAULT_ENCODER_STALE=4,
    AUTO_FAULT_ENCODER_DIRECTION=5,AUTO_FAULT_OVERSPEED=6,AUTO_FAULT_STALL=7,
    AUTO_FAULT_RUN_TIMEOUT=10,AUTO_FAULT_NOT_READY=12 } autonomous_fault_t;
typedef enum { AUTO_MARKER_NONE=0,AUTO_MARKER_APPROACH,AUTO_MARKER_CROSSING,AUTO_MARKER_CLEAR } autonomous_marker_phase_t;
/* 视觉无新路径时沿用上一偏差；has_path仅说明本帧是否有观测。 */
typedef struct {
    track_path_geometry_t geometry;
    uint32_t now_ms,frame_sequence,frame_ms,feedback_sequence,servo_generation;
    uint8_t ready,remote_busy,start_pressed,stop_pressed,cancel_pressed;
    uint8_t has_path,element,zebra,zebra_visible,zebra_near_row;
    int16_t near_error_px,far_error_px;
    int32_t left_speed_mm_s,right_speed_mm_s,forward_step_um;
} autonomous_drive_input_t;
typedef struct {
    autonomous_state_t state;autonomous_fault_t fault;
    uint8_t active,has_path,element,start_marker_seen,marker_phase,zebra_visible,finish_exit_confirmed;
    uint32_t run_generation,distance_mm,finish_advance_mm;
    uint16_t left_duty_permille,right_duty_permille;
    int16_t steering_permille,near_error_px,far_error_px;
    int32_t left_speed_mm_s,right_speed_mm_s;
    float target_speed_mm_s,left_target_mm_s,right_target_mm_s;
    float left_filtered_mm_s,right_filtered_mm_s,left_pid_request,right_pid_request,left_integral,right_integral;
} autonomous_drive_status_t;
typedef struct {
    autonomous_drive_status_t status;
    control_pid_t left_pid,right_pid,steering_pid;
    uint32_t frame_sequence,feedback_sequence,feedback_ms,started_ms,marker_ms,marker_distance_mm;
    uint32_t distance_remainder_um,key_time_ms,wrong_left_ms,wrong_right_ms,stall_left_ms,stall_right_ms;
    uint32_t steering_frame_ms,finish_clear_mm;
    float finish_speed_cap,left_filtered,right_filtered;
    uint8_t finish_tail_seen,finish_clear_pending,config_valid,key_sample,key_stable,key_armed;
    uint8_t marker_present,marker_armed,zebra_frames,clear_frames,steering_initialized,steering_geometry;
} autonomous_drive_t;
void autonomous_drive_init(autonomous_drive_t *drive);
void autonomous_drive_tick(autonomous_drive_t *drive,const autonomous_drive_input_t *input);
#endif
