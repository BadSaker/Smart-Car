#ifndef AUTOCAR_AUTONOMOUS_DRIVE_H
#define AUTOCAR_AUTONOMOUS_DRIVE_H
#include <stdint.h>
#include "control_pid.h"
typedef enum { AUTO_DRIVE_IDLE=0, AUTO_DRIVE_STARTING, AUTO_DRIVE_RUNNING,
    AUTO_DRIVE_FINISHING, AUTO_DRIVE_FINISHED, AUTO_DRIVE_STOPPED, AUTO_DRIVE_FAULT } autonomous_state_t;
typedef enum { AUTO_FAULT_NONE=0, AUTO_FAULT_CONFIG, AUTO_FAULT_CAMERA_STALE,
    AUTO_FAULT_PATH_LOST, AUTO_FAULT_ENCODER_STALE, AUTO_FAULT_ENCODER_DIRECTION,
    AUTO_FAULT_OVERSPEED, AUTO_FAULT_STALL, AUTO_FAULT_SERVO_STALE,
    AUTO_FAULT_ELEMENT, AUTO_FAULT_RUN_TIMEOUT, AUTO_FAULT_FINISH_TIMEOUT,
    AUTO_FAULT_NOT_READY } autonomous_fault_t;
/* 所有输入必须为本次10ms采样；图像字段可沿用最近帧，但序号与完成时间不能伪刷新。 */
typedef struct {
    uint32_t now_ms, frame_sequence, frame_ms, feedback_sequence;
    uint32_t servo_generation, servo_service_ms;
    uint8_t ready, remote_busy, start_pressed, stop_pressed, cancel_pressed;
    uint8_t vision_valid, confidence, zebra, element_fault, element_slow, element_supported, curve;
    int16_t near_error_px, far_error_px;
    int32_t left_speed_mm_s, right_speed_mm_s;
    int32_t forward_step_um; /* 左右编码器原始增量按方向和传动比换算后的平均微米。 */
} autonomous_drive_input_t;
typedef struct {
    autonomous_state_t state;
    autonomous_fault_t fault;
    uint8_t active, start_marker_seen, confidence;
    uint32_t run_generation, distance_mm;
    uint16_t left_duty_permille, right_duty_permille;
    int16_t steering_permille;
    int32_t left_speed_mm_s, right_speed_mm_s;
    float target_speed_mm_s;
} autonomous_drive_status_t;
/* 状态仅由一个10ms执行上下文写入；硬件适配层提供快照，不在此读写GPIO。 */
typedef struct {
    autonomous_drive_status_t status;
    control_pid_t left_pid, right_pid, steering_pid;
    uint32_t frame_sequence, feedback_sequence, feedback_ms, started_ms, marker_ms;
    uint32_t marker_distance_mm, finish_ms, finish_distance_mm, distance_remainder_um;
    uint32_t key_time_ms, wrong_left_ms, wrong_right_ms, stall_left_ms, stall_right_ms;
    uint32_t steering_frame_ms;
    float left_filtered, right_filtered;
    uint8_t config_valid, key_sample, key_stable, key_armed, good_frames, bad_frames;
    uint8_t marker_present, marker_armed, zebra_frames, clear_frames, steering_initialized;
} autonomous_drive_t;
void autonomous_drive_init(autonomous_drive_t *drive);
void autonomous_drive_tick(autonomous_drive_t *drive, const autonomous_drive_input_t *input);
#endif
