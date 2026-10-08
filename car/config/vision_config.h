#ifndef AUTOCAR_VISION_CONFIG_H
#define AUTOCAR_VISION_CONFIG_H

/* 以下均为合成图初值，需用固定安装角度的实车灰度录像标定。
 * 行0在远处、行119在近处；宽度是道路两黑边内沿之间的像素数。 */
#define VISION_TOP_ROW                 30
#define VISION_BOTTOM_ROW              116
#define VISION_CENTER_COL              93
#define VISION_WIDTH_TOP_PX            36
#define VISION_WIDTH_BOTTOM_PX         122
#define VISION_NEAR_FIRST_ROW          86
#define VISION_NEAR_LAST_ROW           108
#define VISION_FAR_FIRST_ROW           45
#define VISION_FAR_LAST_ROW            70
#define VISION_MIN_CONTRAST            25U
#define VISION_THRESHOLD_MIN           20U
#define VISION_THRESHOLD_MAX           220U
#define VISION_SEED_SEARCH_PX          14
#define VISION_EDGE_WINDOW_PX          18
#define VISION_MAX_ROW_STEP_PX         6
#define VISION_MIN_WIDTH_PERCENT       55
#define VISION_MAX_WIDTH_PERCENT       135
#define VISION_MIN_MEASURED_ROWS        22U
#define VISION_MIN_CONFIDENCE           35U
#define VISION_CURVE_ERROR_PX           6
#define VISION_MAX_UNSUPPORTED_ROWS     12U

/* 元素采用拓扑顺序、多帧确认及里程限界；时间只作故障上限。 */
#define VISION_OPENING_MIN_ROWS         12U
#define VISION_OPENING_MARGIN_ROWS      4
#define VISION_OPPOSITE_CONTINUITY_PERCENT 80U
#define VISION_RING_CONFIRM_FRAMES      3U
#define VISION_RING_ENTRY_ROW           94U
#define VISION_RING_ENTRY_BIAS_PX       22
#define VISION_RING_EXIT_BIAS_PX        28
#define VISION_RING_ENTER_MIN_MM        160U
#define VISION_RING_INSIDE_MIN_MM       900U
#define VISION_RING_EXIT_MIN_MM         180U
#define VISION_RING_PHASE_MAX_MM        4000U
#define VISION_RING_PHASE_TIMEOUT_MS    60000U
#define VISION_RING_COOLDOWN_MM         450U
#define VISION_RECOVERY_FRAMES          3U
#define VISION_CROSS_CONFIRM_FRAMES     2U
#define VISION_CROSS_MAX_MM             1000U
#define VISION_CROSS_TIMEOUT_MS         15000U
#define VISION_CANDIDATE_MAX_GAP_MS      180U
#define VISION_ZEBRA_MIN_TRANSITIONS    6U
#define VISION_ZEBRA_MIN_ROWS           3U
#define VISION_ZEBRA_FIRST_ROW          50
#define VISION_ZEBRA_LAST_ROW           112
#define VISION_ZEBRA_MIN_RUN_PX         2U

#if VISION_TOP_ROW < 2 || VISION_BOTTOM_ROW > 117 || VISION_TOP_ROW >= VISION_BOTTOM_ROW
#error "视觉处理行范围必须位于2..117且由远到近"
#endif
#if VISION_WIDTH_TOP_PX < 8 || VISION_WIDTH_BOTTOM_PX >= 186 || VISION_WIDTH_TOP_PX > VISION_WIDTH_BOTTOM_PX
#error "透视宽度必须为有效的由远到近增大像素宽度"
#endif
#endif
