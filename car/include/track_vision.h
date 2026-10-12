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

typedef struct {
    uint8_t valid;
    uint8_t confidence; /* 0..100，表示路径支持度，不是统计概率。 */
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
