#ifndef AUTOCAR_TRACK_VISION_H
#define AUTOCAR_TRACK_VISION_H
#include <stdint.h>
#define TRACK_VISION_WIDTH 188U
#define TRACK_VISION_HEIGHT 120U
typedef enum {TRACK_ELEMENT_STRAIGHT=0,TRACK_ELEMENT_CURVE,TRACK_ELEMENT_CROSS,
 TRACK_ELEMENT_ROUNDABOUT_APPROACH,TRACK_ELEMENT_ROUNDABOUT_ENTER,TRACK_ELEMENT_ROUNDABOUT_INSIDE,TRACK_ELEMENT_ROUNDABOUT_EXIT} track_element_t;
typedef enum {TRACK_SIDE_NONE=0,TRACK_SIDE_LEFT,TRACK_SIDE_RIGHT} track_side_t;
#define TRACK_ROW_LEFT_MEASURED 0x01U
#define TRACK_ROW_RIGHT_MEASURED 0x02U
#define TRACK_ROW_PATH_VALID 0x04U
#define TRACK_ROW_RECONSTRUCTED 0x08U
#define TRACK_ROW_ZEBRA 0x10U
/* 几何目标：相对后轴，右/前为正，单位mm。valid仅表示有目标，不是动力许可。 */
typedef struct { float target_right_mm,target_forward_mm; uint8_t valid; } track_path_geometry_t;
/* has_path是有无实际边线观测，不是评分许可；无观测沿用上次偏差。 */
typedef struct {
 track_path_geometry_t geometry;
 uint8_t has_path,threshold,preview_first_row,preview_last_row;
 int16_t near_error_px,far_error_px;
 track_element_t element;track_side_t side,curve_outer_side;
 uint8_t zebra,zebra_candidate,zebra_near_row,zebra_far_row,zebra_rows,zebra_clipped;
 uint8_t left[120],right[120],center[120],row_flags[120];
 uint8_t near_path_supported,left_opening_rows,right_opening_rows;
 uint8_t opening_left_bottom,opening_right_bottom;
 int16_t raw_near_error_px,raw_far_error_px;
} track_vision_result_t;
void track_vision_reset(void);
/* 每张188×120新灰度帧调用一次；不操作硬件、不修改输入。 */
void track_vision_process(const uint8_t *gray,uint32_t now_ms,uint32_t distance_mm,track_vision_result_t *out);
#endif
