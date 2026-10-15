#ifndef AUTOCAR_TRACK_VISION_H
#define AUTOCAR_TRACK_VISION_H

#include <stdint.h>

#define TRACK_VISION_WIDTH 188U
#define TRACK_VISION_HEIGHT 120U

typedef enum {
    TRACK_ELEMENT_STRAIGHT = 0,
    TRACK_ELEMENT_CURVE,
    TRACK_ELEMENT_CROSS,
    TRACK_ELEMENT_ROUNDABOUT_APPROACH,
    TRACK_ELEMENT_ROUNDABOUT_ENTER,
    TRACK_ELEMENT_ROUNDABOUT_INSIDE,
    TRACK_ELEMENT_ROUNDABOUT_EXIT
} track_element_t;

#define TRACK_ELEMENT_HISTORY_SIZE 8U
typedef struct {
    uint32_t time_ms,distance_mm;
    uint8_t from,to;
} track_element_event_t;
typedef struct {
    uint32_t entry_ms,entry_mm,elapsed_ms,traveled_mm;
    uint32_t fault_entry_ms,fault_entry_mm,fault_elapsed_ms,fault_traveled_mm;
    uint8_t recovery_frames,exit_evidence,limit_reason,history_count;
    /* exit_evidence:1近场已见开口、2近场闭合、4双边覆盖、8有效路径、16正常直道。
     * limit_reason:1距离、2时间；故障上下文在reset前锁存。 */
    track_element_event_t history[TRACK_ELEMENT_HISTORY_SIZE];
} track_element_diagnostic_t;

typedef enum {
    TRACK_SIDE_NONE = 0,
    TRACK_SIDE_LEFT,
    TRACK_SIDE_RIGHT
} track_side_t;

/* 行有效位：实测边线与重建路径分别标记；不能把补线视为实测证据。 */
#define TRACK_ROW_LEFT_MEASURED  0x01U
#define TRACK_ROW_RIGHT_MEASURED 0x02U
#define TRACK_ROW_PATH_VALID     0x04U
#define TRACK_ROW_RECONSTRUCTED  0x08U
#define TRACK_ROW_ZEBRA          0x10U

#define TRACK_FAULT_NONE            0U
#define TRACK_FAULT_ELEMENT_TIMEOUT 0x01U
#define TRACK_FAULT_PATH_LOST       0x02U

typedef enum { TRACK_OUTER_REJECT_NONE=0, TRACK_OUTER_REJECT_NEAR,
    TRACK_OUTER_REJECT_FAR, TRACK_OUTER_REJECT_CONTINUITY } track_outer_reject_t;

#define TRACK_PATH_MIN_TARGET_MM 320.0f
#define TRACK_PATH_MAX_TARGET_MM 780.0f

/* 同一帧的几何目标及其通行证据。右为正，单位毫米；不是像素误差。
 * valid仅表示该目标经过路径检查，仍须服从总valid、Q及元素故障门控。 */
typedef struct {
    float target_right_mm,target_forward_mm,fit_rms_mm;
    uint16_t contour_points,visible_samples,inferred_samples;
    uint8_t valid,model_kind,reject_reason;
} track_path_geometry_t;

typedef struct {
    uint8_t valid;
    uint8_t confidence; /* 0..100，表示路径支持度，不是统计概率。 */
    uint8_t observation_confidence; /* 第一遍实测边线覆盖评分，不含补线或记忆。 */
    uint8_t preview_first_row,preview_last_row; /* 本帧可见的实际远预瞄窗口。 */
    track_path_geometry_t geometry;
    uint8_t zebra;
    uint8_t zebra_near_row; /* 最靠近车辆的斑马线证据行；无证据时255。 */
    uint8_t zebra_rows;
    uint8_t fault; /* 元素故障锁存到reset；普通路径丢失随当前帧更新。 */
    uint8_t threshold;
    int16_t near_error_px; /* 原点左上、列向右、行向下；相对93列右正。 */
    int16_t far_error_px;
    track_element_t element;
    track_side_t side;
    track_side_t curve_outer_side; /* 普通弯道或已确认环内实际用于重建的外侧；NONE表示未启用。 */
    uint8_t near_path_supported;
    uint8_t curve_outer_supported;
    uint8_t curve_outer_near_rows, curve_outer_far_rows;
    track_outer_reject_t curve_outer_reject;
    uint8_t left[TRACK_VISION_HEIGHT];
    uint8_t right[TRACK_VISION_HEIGHT];
    uint8_t center[TRACK_VISION_HEIGHT];
    uint8_t row_flags[TRACK_VISION_HEIGHT];
    /* 第一遍原始证据，供诊断使用；外侧补线不改写这些字段。 */
    int16_t raw_near_error_px,raw_far_error_px;
    uint8_t left_opening_rows,right_opening_rows,joint_opening_rows;
    uint8_t left_fork,right_fork;
    uint8_t opening_left_bottom,opening_right_bottom;
    uint8_t element_bridged; /* 当前帧使用已确认元素的限距白区通行证据。 */
    /* 环岛入口拒绝位：1缺口、2对侧、4前向分支、8岛边、16来向、32路径。
     * 记录当前原始证据，不授予控制通行许可。 */
    uint8_t ring_left_reject,ring_right_reject;
    uint8_t left_island,right_island;
    uint8_t ring_entry_supported;
    track_element_t fault_element; /* 元素超时首次发生的阶段，reset前保留。 */
    track_element_diagnostic_t element_diagnostic;
} track_vision_result_t;

/* 在主循环启动/退出自动模式时调用；清除候选、元素、宽度历史和故障。
 * 与process共享静态状态，二者必须串行调用，不可在中断并发调用。 */
void track_vision_reset(void);

/* 主循环每个新帧调用一次。gray指向连续188*120字节，函数不修改灰度。
 * now_ms允许uint32回绕；distance_mm是前进累计毫米，允许uint32回绕。
 * 无堆分配、无硬件访问；空gray输出失效，空out直接返回且不推进状态。
 * 44/167透视端点为用户实测，其余门限及弯道效果仍需实车验证。 */
void track_vision_process(const uint8_t *gray, uint32_t now_ms,
                          uint32_t distance_mm, track_vision_result_t *out);

#endif
