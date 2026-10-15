/* 本模块自主实现，不含新复制的第三方源码。
 * 阈值方法依据Otsu类间方差思想：doi:10.1109/TSMC.1979.4310076。
 * 公开智能车项目仅用于比较扫描线、八邻域和元素分阶段处理的取舍。
 * 环岛拓扑、透视宽度、曝光和每帧耗时仍须用本车录像/实测验收。 */
#include "track_vision.h"
#include "vision_config.h"
#include <string.h>
#include <math.h>

typedef struct {
    track_element_t phase;
    track_side_t side;
    uint32_t phase_ms;
    uint32_t phase_mm;
    uint32_t last_frame_ms;
    uint32_t cooldown_mm;
    uint8_t cooldown;
    uint8_t candidate_left;
    uint8_t candidate_right;
    uint8_t candidate_cross;
    track_side_t curve_outer_candidate;
    uint8_t curve_outer_frames;
    uint8_t recovery;
    uint8_t approach_clear;
    uint8_t curve_seen,contour_enter_frames;
    uint8_t cross_near_seen;
    uint8_t cross_provisional;
    uint32_t cross_near_mm;
    uint8_t exit_seen,exit_near_seen;
    uint8_t exit_candidate;
    uint8_t ring_path_valid,ring_path_seen,ring_path[120];
    uint32_t ring_path_saved_ms,ring_path_saved_mm;
    uint32_t exit_seen_ms,exit_seen_mm;
    uint8_t fault;
    track_element_t fault_element;
    track_element_diagnostic_t diagnostic;
    uint8_t history_next;
    uint8_t incoming_valid;
    uint32_t incoming_ms;
    uint8_t bridge_active,bridge_recovery;
    uint32_t bridge_ms,bridge_mm;
    int16_t incoming_near;
    int16_t incoming_far;
} vision_state_t;

typedef struct {
    uint8_t left_open[TRACK_VISION_HEIGHT];
    uint8_t right_open[TRACK_VISION_HEIGHT];
    uint8_t raw_left[TRACK_VISION_HEIGHT],raw_right[TRACK_VISION_HEIGHT],raw_mask[TRACK_VISION_HEIGHT];
    uint8_t left_gap;
    uint8_t right_gap;
    uint8_t left_bottom;
    uint8_t right_bottom;
    uint8_t left_supported;
    uint8_t right_supported;
    uint8_t measured_rows;
    uint8_t paired_rows;
    uint8_t left_fork;
    uint8_t right_fork;
    uint8_t left_island,right_island;
} vision_features_t;

static vision_state_t vision;

static int vision_abs(int value) { return value < 0 ? -value : value; }

static int vision_clip(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

static int vision_width(int row)
{
    /* 分叉检查会外推到30行以上，必须保持有符号算术；仿真覆盖宏带U。 */
    return (int)VISION_WIDTH_TOP_PX + (row-VISION_TOP_ROW) *
        ((int)VISION_WIDTH_BOTTOM_PX-(int)VISION_WIDTH_TOP_PX) /
        (VISION_BOTTOM_ROW-VISION_TOP_ROW);
}

/* 用32级直方图求类间方差，再取两类均值中点，避免阈值恰等于暗类。
 * 只在道路ROI采样；直方图和累计均有固定上界，无动态内存。 */
static uint8_t vision_threshold(const uint8_t *gray, uint8_t *contrast)
{
    uint16_t bins[32] = {0};
    uint32_t total = 0, sum = 0, low_count = 0, low_sum = 0;
    uint32_t best_count = 0, best_sum = 0;
    float best_score = -1.0f;
    int row, col, index;
    uint8_t lowest = 255, highest = 0;
    for (row=VISION_TOP_ROW; row<=VISION_BOTTOM_ROW; row+=2)
        for(col=2;col<186;col+=2) {
            uint8_t value=gray[row*188+col];
            ++bins[value>>3];
            ++total;
            sum+=value>>3;
            if(value<lowest) lowest=value;
            if(value>highest) highest=value;
        }
    *contrast=(uint8_t)(highest-lowest);
    for(index=0;index<31;++index) {
        float separation,score;
        low_count+=bins[index];
        low_sum+=(uint32_t)index*bins[index];
        if(low_count==0 || low_count==total) continue;
        separation=(float)low_sum/(float)low_count-
            (float)(sum-low_sum)/(float)(total-low_count);
        score=(float)low_count*(float)(total-low_count)*separation*separation;
        if(score>best_score) {
            best_score=score;
            best_count=low_count;
            best_sum=low_sum;
        }
    }
    if(best_count==0 || best_count==total) return 127;
    return (uint8_t)vision_clip((int)((best_sum/best_count +
        (sum-best_sum)/(total-best_count))*4U+4U),
        VISION_THRESHOLD_MIN,VISION_THRESHOLD_MAX);
}

static int vision_white(const uint8_t *gray,int row,int col,uint8_t threshold)
{
    return gray[row*188+col]>threshold;
}

/* 斑马线要求横向多个持续至少2px的黑白段，且黑色占比适中。 */
static int vision_zebra_row(const uint8_t *gray,int row,int center,uint8_t threshold)
{
    int half=vision_width(row)/2-5;
    int left=vision_clip(center-half,2,185),right=vision_clip(center+half,2,185);
    int col,last=vision_white(gray,row,left,threshold);
    unsigned run=0,transitions=0,black=0;
    int last_stable=last;
    for(col=left;col<=right;++col) {
        int white=vision_white(gray,row,col,threshold);
        if(!white) ++black;
        if(white==last) ++run; else { run=1; last=white; }
        if(run==VISION_ZEBRA_MIN_RUN_PX && white!=last_stable) {
            ++transitions; last_stable=white;
        }
    }
    return transitions>=VISION_ZEBRA_MIN_TRANSITIONS &&
        black*5U>(unsigned)(right-left+1) &&
        black*5U<(unsigned)(right-left+1)*4U;
}

static void vision_scan(const uint8_t *gray,track_vision_result_t *out,vision_features_t *features,
                        track_side_t preferred_outer)
{
    int row,predicted=VISION_CENTER_COL;
    int previous_outer=0,previous_outer_row=-1;
    unsigned unsupported=0;
    memset(features,0,sizeof(*features));
    for(row=VISION_BOTTOM_ROW;row>=VISION_TOP_ROW;--row) {
        int half=vision_width(row)/2;
        int seed=predicted,left=0,right=187,col,offset;
        int has_left=0,has_right=0,found=0,center=predicted;
        int expected_left=predicted-half,expected_right=predicted+half;
        uint8_t flags=0;
        if(row>=VISION_ZEBRA_FIRST_ROW && row<=VISION_ZEBRA_LAST_ROW &&
           vision_zebra_row(gray,row,predicted,out->threshold)) {
            flags=TRACK_ROW_ZEBRA|TRACK_ROW_RECONSTRUCTED|TRACK_ROW_PATH_VALID;
            ++out->zebra_rows;
            if(out->zebra_near_row==255) out->zebra_near_row=(uint8_t)row;
            left=vision_clip(expected_left,0,187); right=vision_clip(expected_right,0,187);
        } else {
            for(offset=0;offset<=VISION_SEED_SEARCH_PX;++offset) {
                if(predicted-offset>=2 && vision_white(gray,row,predicted-offset,out->threshold)) {
                    seed=predicted-offset;found=1;break;
                }
                if(predicted+offset<=185 && vision_white(gray,row,predicted+offset,out->threshold)) {
                    seed=predicted+offset;found=1;break;
                }
            }
            if(found) {
                int use_outer=0;
                for(col=seed;col>=2;--col)
                    if(!vision_white(gray,row,col-1,out->threshold) &&
                       !vision_white(gray,row,col-2,out->threshold)) { left=col;has_left=1;break; }
                for(col=seed;col<=185;++col)
                    if(!vision_white(gray,row,col+1,out->threshold) &&
                       !vision_white(gray,row,col+2,out->threshold)) { right=col;has_right=1;break; }
                /* 保存真实像素边界；开口判断可取消它的循迹资格，但不能
                 * 抹掉它在已确认节点中提供的可见轮廓证据。 */
                features->raw_left[row]=(uint8_t)left;features->raw_right[row]=(uint8_t)right;
                features->raw_mask[row]=(uint8_t)((has_left?1:0)|(has_right?2:0));
                /* 只信任相邻行连续、且与预测路径接近的实测外侧。
                 * 预计内侧已离开画面时，背景形成的假内侧不能参与双边平均。 */
                if(preferred_outer!=TRACK_SIDE_NONE) {
                    int outer=preferred_outer==TRACK_SIDE_LEFT?left:right;
                    int present=preferred_outer==TRACK_SIDE_LEFT?has_left:has_right;
                    int outer_center=preferred_outer==TRACK_SIDE_LEFT?outer+half:outer-half;
                    int row_gap=previous_outer_row-row;
                    int inner_offscreen=preferred_outer==TRACK_SIDE_LEFT?
                        outer+vision_width(row)>=187-VISION_CURVE_OUTER_BORDER_MARGIN_PX:
                        outer-vision_width(row)<=VISION_CURVE_OUTER_BORDER_MARGIN_PX;
                    int inner_open=preferred_outer==TRACK_SIDE_LEFT?
                        (!has_right || right>expected_right+VISION_EDGE_WINDOW_PX):
                        (!has_left || left<expected_left-VISION_EDGE_WINDOW_PX);
                    /* 弯道同一图像行的横截面并非直道宽度；实际向外展开的
                     * 白区也能证明内侧不可见，仍需连续实测外边来重建。 */
                    inner_offscreen=inner_offscreen || inner_open;
                    use_outer=present && inner_offscreen && row_gap>0 &&
                        row_gap<=(int)VISION_MAX_UNSUPPORTED_ROWS+1 &&
                        vision_abs(outer-previous_outer)<=VISION_MAX_ROW_STEP_PX*row_gap &&
                        vision_abs(outer_center-predicted)<=VISION_CURVE_OUTER_MAX_CENTER_ERROR_PX;
                    if(present) { previous_outer=outer;previous_outer_row=row; }
                    if(use_outer) {
                        if(preferred_outer==TRACK_SIDE_LEFT) has_right=0;
                        else has_left=0;
                        out->curve_outer_side=preferred_outer;
                    }
                }
                /* 开口必须向道路外侧增宽；内侧黑块或急弯不作为开口。 */
                features->left_open[row]=(uint8_t)(expected_left>4 &&
                    (!has_left || left<expected_left-VISION_EDGE_WINDOW_PX));
                features->right_open[row]=(uint8_t)(expected_right<183 &&
                    (!has_right || right>expected_right+VISION_EDGE_WINDOW_PX));
                if(features->left_open[row]) has_left=0;
                if(features->right_open[row]) has_right=0;
                /* 外侧通过上面的连续性与出画面验证后，保留其原始实测证据。 */
                if(use_outer) {
                    if(preferred_outer==TRACK_SIDE_LEFT) { has_left=1;features->left_open[row]=0; }
                    else { has_right=1;features->right_open[row]=0; }
                }
                if(has_left && has_right && (right-left)*100<vision_width(row)*VISION_MIN_WIDTH_PERCENT) {
                    has_left=0;has_right=0;
                }
                if(has_left && has_right && (right-left)*100>vision_width(row)*VISION_MAX_WIDTH_PERCENT) {
                    has_left=0;has_right=0;
                }
            }
            if(has_left) flags|=TRACK_ROW_LEFT_MEASURED;
            if(has_right) flags|=TRACK_ROW_RIGHT_MEASURED;
            if(has_left && has_right) {
                center=(left+right)/2; ++features->paired_rows;
            } else if(has_left) {
                center=left+half;right=vision_clip(left+2*half,0,187);
            } else if(has_right) {
                center=right-half;left=vision_clip(right-2*half,0,187);
            }
            if(has_left || has_right) {
                ++features->measured_rows;unsupported=0;
                flags|=TRACK_ROW_PATH_VALID;
                if(!(has_left && has_right)) flags|=TRACK_ROW_RECONSTRUCTED;
                center=vision_clip(center,predicted-VISION_MAX_ROW_STEP_PX,predicted+VISION_MAX_ROW_STEP_PX);
            } else {
                ++unsupported;flags|=TRACK_ROW_RECONSTRUCTED;
                if(unsupported<=VISION_MAX_UNSUPPORTED_ROWS) flags|=TRACK_ROW_PATH_VALID;
                left=vision_clip(expected_left,0,187);right=vision_clip(expected_right,0,187);
            }
        }
        center=vision_clip(center,2,185);
        out->left[row]=(uint8_t)left;out->right[row]=(uint8_t)right;
        out->center[row]=(uint8_t)center;out->row_flags[row]=flags;
        predicted=center;
    }
    out->zebra=(uint8_t)(out->zebra_rows>=VISION_ZEBRA_MIN_ROWS);
    if(!out->zebra) out->zebra_near_row=255;
}

/* 缺口须同时有近端与远端闭合证据；整边丢失不等于环岛。
 * 两端检查和最小连续行数抑制远处圆形、窄裂缝及单帧反光。 */
static uint8_t vision_gap(const uint8_t *opening,const track_vision_result_t *out,
                          uint8_t measured_mask,uint8_t *bottom)
{
    int row,start=-1,end=-1;
    unsigned best=0;
    for(row=VISION_TOP_ROW+VISION_OPENING_MARGIN_ROWS;
        row<=VISION_BOTTOM_ROW-VISION_OPENING_MARGIN_ROWS;++row) {
        if(opening[row]) {
            if(start<0) start=row;
        } else if(start>=0) {
            unsigned length=(unsigned)(row-start);
            if(length>=VISION_OPENING_MIN_ROWS && length>best &&
               !opening[start-1] && !opening[start-2] &&
               !opening[row+1] && !opening[row+2] &&
               (out->row_flags[start-1]&measured_mask) &&
               (out->row_flags[start-2]&measured_mask) &&
               (out->row_flags[row+1]&measured_mask) &&
               (out->row_flags[row+2]&measured_mask)) { best=length;end=row-1; }
            start=-1;
        }
    }
    if(end>=0) { *bottom=(uint8_t)end;return 1; }
    return 0;
}

static int16_t vision_error(const track_vision_result_t *out,int first,int last)
{
    int row,sum=0,count=0;
    for(row=first;row<=last;++row) if(out->row_flags[row]&TRACK_ROW_PATH_VALID) {
        sum+=(int)out->center[row]-VISION_CENTER_COL;++count;
    }
    return count ? (int16_t)(sum/count) : 0;
}

/* 远处弯道可出画面，但较近的前向走廊仍可见。窗口最多下移15行，
 * 始终与近场分离；不能用仅有车头下方白区的图像替代前向路径。 */
static void vision_choose_preview(const uint8_t *gray,track_vision_result_t *out)
{
    int first,row;
    const int span=VISION_FAR_LAST_ROW-VISION_FAR_FIRST_ROW;
    for(first=VISION_FAR_FIRST_ROW;first+span<VISION_NEAR_FIRST_ROW;++first) {
        unsigned white=0;
        for(row=first;row<=first+span;++row)
            if((out->row_flags[row]&TRACK_ROW_ZEBRA) || vision_white(gray,row,out->center[row],out->threshold))++white;
        if(white*100U>=(unsigned)(span+1)*VISION_ELEMENT_WHITE_PERCENT) {
            out->preview_first_row=(uint8_t)first;out->preview_last_row=(uint8_t)(first+span);return;
        }
    }
}

/* 换窗后保持原误差参考位置，避免仅因窗口移动而突然减小转向。
 * 这是当前近远点的有限外推，重建像素仍只在实际白区内显示。 */
static int16_t vision_preview_error(const track_vision_result_t *out)
{
    int near_error=vision_error(out,VISION_NEAR_FIRST_ROW,VISION_NEAR_LAST_ROW);
    int far_error=vision_error(out,out->preview_first_row,out->preview_last_row);
    int near_row=VISION_NEAR_FIRST_ROW+VISION_NEAR_LAST_ROW;
    int span=near_row-out->preview_first_row-out->preview_last_row;
    int reference=near_row-VISION_FAR_FIRST_ROW-VISION_FAR_LAST_ROW;
    return (int16_t)vision_clip(near_error+(far_error-near_error)*reference/span,2-VISION_CENTER_COL,185-VISION_CENTER_COL);
}

/* 第一遍保留原始扫线/元素证据；只有普通弯道有连续内侧缺失证据，
 * 才允许第二遍选择外边线。每帧重新验证，不能靠历史方向盲行。 */
static unsigned vision_outer_run(const track_vision_result_t *out,
                                 const vision_features_t *features,track_side_t side)
{
    unsigned run=0,best=0;
    int row,previous=0;
    if(side!=TRACK_SIDE_NONE) for(row=VISION_BOTTOM_ROW;row>=VISION_TOP_ROW;--row) {
        uint8_t outer_mask=side==TRACK_SIDE_LEFT?TRACK_ROW_LEFT_MEASURED:TRACK_ROW_RIGHT_MEASURED;
        uint8_t inner_mask=side==TRACK_SIDE_LEFT?TRACK_ROW_RIGHT_MEASURED:TRACK_ROW_LEFT_MEASURED;
        int outer=side==TRACK_SIDE_LEFT?out->left[row]:out->right[row];
        int inner=side==TRACK_SIDE_LEFT?out->right[row]:out->left[row];
        int offscreen=side==TRACK_SIDE_LEFT?
            outer+vision_width(row)>=187-VISION_CURVE_OUTER_BORDER_MARGIN_PX:
            outer-vision_width(row)<=VISION_CURVE_OUTER_BORDER_MARGIN_PX;
        int inner_open=side==TRACK_SIDE_LEFT?features->right_open[row]:features->left_open[row];
        int inner_missing=!(out->row_flags[row]&inner_mask) ||
            (side==TRACK_SIDE_LEFT?inner>=187-VISION_CURVE_OUTER_BORDER_MARGIN_PX:
                                   inner<=VISION_CURVE_OUTER_BORDER_MARGIN_PX);
        if((out->row_flags[row]&outer_mask) && !(out->row_flags[row]&TRACK_ROW_ZEBRA) &&
           (offscreen || inner_open) && inner_missing) {
            run=run && vision_abs(outer-previous)>VISION_MAX_ROW_STEP_PX?1U:run+1U;
            previous=outer;if(run>best)best=run;
        } else run=0;
    }
    return best;
}

static unsigned vision_outer_near_rows(const track_vision_result_t *out,track_side_t side)
{
    unsigned count=0;
    uint8_t mask=side==TRACK_SIDE_LEFT?TRACK_ROW_LEFT_MEASURED:TRACK_ROW_RIGHT_MEASURED;
    int row;
    if(side==TRACK_SIDE_NONE)return 0;
    for(row=VISION_NEAR_FIRST_ROW;row<=VISION_NEAR_LAST_ROW;++row)
        if((out->row_flags[row]&mask) && (out->row_flags[row]&TRACK_ROW_PATH_VALID) &&
           !(out->row_flags[row]&TRACK_ROW_ZEBRA))++count;
    return count;
}

static track_side_t vision_curve_outer(const track_vision_result_t *out,
                                      const vision_features_t *features,uint32_t now_ms)
{
    track_side_t side=TRACK_SIDE_NONE,previous=vision.curve_outer_candidate;
    int near_error=vision_error(out,VISION_NEAR_FIRST_ROW,VISION_NEAR_LAST_ROW);
    int far_error=vision_error(out,VISION_FAR_FIRST_ROW,VISION_FAR_LAST_ROW);
    unsigned required=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)*VISION_CURVE_OUTER_NEAR_MIN_PERCENT;
    /* 候选尚未确认时仍需正常过弯；元素分类使用第一遍原始证据。 */
    if(VISION_CURVE_OUTER_ENABLED &&
       (vision.phase<=TRACK_ELEMENT_CURVE || vision.phase==TRACK_ELEMENT_ROUNDABOUT_APPROACH ||
        vision.phase==TRACK_ELEMENT_ROUNDABOUT_INSIDE) && !vision.fault) {
        if(vision.phase==TRACK_ELEMENT_ROUNDABOUT_INSIDE && vision.side!=TRACK_SIDE_NONE)
            side=vision.side==TRACK_SIDE_LEFT?TRACK_SIDE_RIGHT:TRACK_SIDE_LEFT;
        else if(far_error>=VISION_CURVE_OUTER_MIN_ERROR_PX && far_error-near_error>=VISION_CURVE_ERROR_PX)
            side=TRACK_SIDE_LEFT;
        else if(far_error<=-VISION_CURVE_OUTER_MIN_ERROR_PX && near_error-far_error>=VISION_CURVE_ERROR_PX)
            side=TRACK_SIDE_RIGHT;
        /* 远处横向支路会使远误差反号。新外侧没有近场支持时，只有旧侧
         * 本帧仍有充分近场及连续实测边，才维持它；不凭历史盲目补线。 */
        if(vision.phase!=TRACK_ELEMENT_ROUNDABOUT_INSIDE && previous!=TRACK_SIDE_NONE && previous!=side &&
           vision_abs(far_error-near_error)>=VISION_CURVE_ERROR_PX &&
           vision.curve_outer_frames>=VISION_CURVE_OUTER_CONFIRM_FRAMES &&
           vision_outer_near_rows(out,side)*100U<required &&
           vision_outer_near_rows(out,previous)*100U>=required &&
           vision_outer_run(out,features,previous)>=VISION_CURVE_OUTER_MIN_ROWS)side=previous;
    }
    if(vision_outer_run(out,features,side)<VISION_CURVE_OUTER_MIN_ROWS)side=TRACK_SIDE_NONE;
    if(side==TRACK_SIDE_NONE || side!=vision.curve_outer_candidate ||
       now_ms-vision.last_frame_ms>VISION_CANDIDATE_MAX_GAP_MS) vision.curve_outer_frames=0;
    vision.curve_outer_candidate=side;
    if(side!=TRACK_SIDE_NONE && vision.curve_outer_frames<255) ++vision.curve_outer_frames;
    return vision.curve_outer_frames>=VISION_CURVE_OUTER_CONFIRM_FRAMES?side:TRACK_SIDE_NONE;
}

static uint8_t vision_opposite_continuous(const uint8_t *opening,
                                        const track_vision_result_t *out,track_side_t side)
{
    unsigned total=0,continuous=0;
    int row;
    uint8_t mask=side==TRACK_SIDE_LEFT?TRACK_ROW_RIGHT_MEASURED:TRACK_ROW_LEFT_MEASURED;
    const uint8_t *edge=side==TRACK_SIDE_LEFT?out->right:out->left;
    for(row=VISION_TOP_ROW+1;row<=VISION_BOTTOM_ROW;++row) if(opening[row]) {
        ++total;
        if((out->row_flags[row]&mask) && (out->row_flags[row-1]&mask) &&
           vision_abs((int)edge[row]-edge[row-1])<=VISION_MAX_ROW_STEP_PX) ++continuous;
    }
    return (uint8_t)(total>0 && continuous*100U>=total*VISION_OPPOSITE_CONTINUITY_PERCENT);
}

static uint8_t vision_increment(uint8_t count,int condition)
{
    return condition ? (uint8_t)(count<255?count+1:255) : 0;
}

/* 缺口远端还须连通一个正常宽度的前向走廊。普通弯道顶部是回弯的
 * 横向白带，不能将它与真正入口的“直行+侧向分叉”视为同一证据。 */
static uint8_t vision_forward_branch(const uint8_t *gray,const track_vision_result_t *out,
                                    const vision_features_t *features,track_side_t side)
{
    const uint8_t *opening=side==TRACK_SIDE_LEFT?features->left_open:features->right_open;
    int bottom=side==TRACK_SIDE_LEFT?features->left_bottom:features->right_bottom;
    int top=bottom,row,run=0,best=0;
    int near_row=VISION_NEAR_FIRST_ROW,last_row=VISION_NEAR_LAST_ROW;
    int slope=(int)out->center[near_row]-out->center[last_row];
    int anchor_row=last_row,anchor_col=out->center[last_row],span=near_row-last_row;
    int missing=0;
    if(!bottom)return 0;
    if(vision.phase==TRACK_ELEMENT_ROUNDABOUT_APPROACH && vision.side==side) {
        /* 靠近侧口后，近场单边补线会偏向支路，不能用它重定义直行分支。
         * 仍检查当前像素，只沿候选确认时的入射方向寻找前向走廊。 */
        anchor_row=VISION_NEAR_FIRST_ROW;anchor_col=VISION_CENTER_COL+vision.incoming_near;
        slope=vision.incoming_far-vision.incoming_near;
        span=VISION_FAR_FIRST_ROW-VISION_NEAR_FIRST_ROW;
    }
    while(top>VISION_RING_BRANCH_TOP_ROW && opening[top-1])--top;
    /* 背景中的另一段赛道不能充当前向分支；从开口到远端走廊要白区连通。 */
    for(row=bottom;row>=top-8 && row>=VISION_RING_BRANCH_TOP_ROW;--row) {
        int center=anchor_col+(row-anchor_row)*slope/span;
        int col,connected=0;
        center=vision_clip(center,2,185);
        for(col=center-2;col<=center+2;++col)
            if(vision_white(gray,row,col,out->threshold)){connected=1;break;}
        missing=connected?0:missing+1;
        if(missing>=2)return 0;
    }
    for(row=top-8;row<top;++row) {
        int expected=vision_width(row),center,left,right;
        if(row<VISION_RING_BRANCH_TOP_ROW || expected<8)continue;
        center=anchor_col+(row-anchor_row)*slope/span;
        center=vision_clip(center,2,185);
        if(!vision_white(gray,row,center,out->threshold)){run=0;continue;}
        left=right=center;
        while(left>1 && vision_white(gray,row,left-1,out->threshold))--left;
        while(right<186 && vision_white(gray,row,right+1,out->threshold))++right;
        if(left>1 && right<186 && (right-left)*100>=expected*55 &&
           (right-left)*100<=expected*135 &&
           vision_abs((left+right)/2-center)<=expected/3+3) {
            ++run;if(run>best)best=run;
        } else run=0;
    }
    return (uint8_t)(best>=(int)VISION_RING_BRANCH_MIN_ROWS);
}

/* 分叉近端须邻接凸向道路的岛边。反向汇入口的近端通常仍是直边，
 * 灰岛位于缺口远端；仅看到“直行+侧口”不足以确定驶入方向。 */
static uint8_t vision_near_island(const track_vision_result_t *out,
                                  const vision_features_t *features,track_side_t side)
{
    int bottom=side==TRACK_SIDE_LEFT?features->left_bottom:features->right_bottom;
    const uint8_t *edge=side==TRACK_SIDE_LEFT?out->left:out->right;
    uint8_t mask=side==TRACK_SIDE_LEFT?TRACK_ROW_LEFT_MEASURED:TRACK_ROW_RIGHT_MEASURED;
    int row,first=-1,last=-1,run=0,best=0,convex=0,span,direction=side==TRACK_SIDE_LEFT?1:-1;
    if(!bottom)return 0;
    for(row=bottom+2;row<=VISION_BOTTOM_ROW;++row) {
        if((out->row_flags[row]&mask) && !(out->row_flags[row]&TRACK_ROW_ZEBRA)) {
            run=run && vision_abs(edge[row]-edge[row-1])>VISION_MAX_ROW_STEP_PX?1:run+1;
            if(run>best){best=run;last=row;first=row-run+1;}
        } else run=0;
    }
    if(best<VISION_RING_ISLAND_MIN_SPAN || first>bottom+VISION_OPENING_MARGIN_ROWS)return 0;
    span=last-first;
    for(row=first+1;row<last;++row) {
        int chord=edge[first]*(last-row)+edge[last]*(row-first);
        int deviation=direction*((int)edge[row]*span-chord);
        convex=deviation>=VISION_RING_ISLAND_CONVEX_PX*span?convex+1:0;
        if(convex>=VISION_RING_ISLAND_CONVEX_ROWS)return 1;
    }
    return 0;
}

static void vision_change(track_element_t phase,uint32_t now_ms,uint32_t distance_mm)
{
    track_element_event_t *event=&vision.diagnostic.history[vision.history_next];
    event->time_ms=now_ms;event->distance_mm=distance_mm;event->from=(uint8_t)vision.phase;event->to=(uint8_t)phase;
    vision.history_next=(uint8_t)((vision.history_next+1U)%TRACK_ELEMENT_HISTORY_SIZE);
    if(vision.diagnostic.history_count<TRACK_ELEMENT_HISTORY_SIZE)++vision.diagnostic.history_count;
    vision.phase=phase;vision.phase_ms=now_ms;vision.phase_mm=distance_mm;
    vision.recovery=0;vision.approach_clear=0;
    vision.cross_near_seen=0;
    vision.cross_provisional=0;
    vision.exit_candidate=0;
    if(phase!=TRACK_ELEMENT_ROUNDABOUT_INSIDE && phase!=TRACK_ELEMENT_ROUNDABOUT_EXIT) {
        vision.exit_seen=0;vision.exit_near_seen=0;vision.ring_path_valid=0;vision.ring_path_seen=0;
    }
}

/* 保存时刻是唯一起点；正常帧、出环阶段切换都不为旧坐标重新计龄。 */
static void vision_cache_expire(uint32_t now_ms,uint32_t distance_mm)
{
    if(vision.ring_path_valid &&
       ((uint32_t)(now_ms-vision.ring_path_saved_ms)>VISION_RING_PATH_MAX_MS ||
        (uint32_t)(distance_mm-vision.ring_path_saved_mm)>VISION_RING_PATH_MAX_MM))
        vision.ring_path_valid=0;
    if(vision.exit_seen &&
       ((uint32_t)(now_ms-vision.exit_seen_ms)>VISION_RING_EXIT_MEMORY_MAX_MS ||
        (uint32_t)(distance_mm-vision.exit_seen_mm)>VISION_RING_EXIT_MEMORY_MAX_MM)) {
        vision.exit_seen=0;vision.exit_near_seen=0;vision.exit_candidate=0;vision.recovery=0;
    }
}

static void vision_limit_fault(uint32_t now_ms,uint32_t distance_mm,uint8_t reason)
{
    if(!(vision.fault&TRACK_FAULT_ELEMENT_TIMEOUT)) {
        vision.fault_element=vision.phase;vision.diagnostic.limit_reason=reason;
        vision.diagnostic.fault_entry_ms=vision.phase_ms;vision.diagnostic.fault_entry_mm=vision.phase_mm;
        vision.diagnostic.fault_elapsed_ms=now_ms-vision.phase_ms;
        vision.diagnostic.fault_traveled_mm=distance_mm-vision.phase_mm;
    }
    vision.fault|=TRACK_FAULT_ELEMENT_TIMEOUT;
}

static void vision_diagnostic(track_vision_result_t *out,uint32_t now_ms,uint32_t distance_mm)
{
    unsigned i,start;
    out->element_diagnostic=vision.diagnostic;
    out->element_diagnostic.entry_ms=vision.phase_ms;out->element_diagnostic.entry_mm=vision.phase_mm;
    out->element_diagnostic.elapsed_ms=now_ms-vision.phase_ms;
    out->element_diagnostic.traveled_mm=distance_mm-vision.phase_mm;
    out->element_diagnostic.recovery_frames=vision.recovery;
    start=vision.diagnostic.history_count==TRACK_ELEMENT_HISTORY_SIZE?vision.history_next:0U;
    for(i=0;i<vision.diagnostic.history_count;++i)
        out->element_diagnostic.history[i]=vision.diagnostic.history[(start+i)%TRACK_ELEMENT_HISTORY_SIZE];
}

static void vision_clear_candidates(void)
{
    vision.candidate_left=0;vision.candidate_right=0;vision.candidate_cross=0;
}

/* 从最近的实测走廊估计入射方向，不让远处横向支路定义十字出口。 */
static int vision_cross_direction(const track_vision_result_t *out,int paired_only)
{
    int row,count=0,sx=0,sy=0,sxx=0,sxy=0,denominator;
    float slope,intercept;
    for(row=VISION_NEAR_LAST_ROW-7;row<=VISION_BOTTOM_ROW;++row)
        if((out->row_flags[row]&(TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED)) &&
           (!paired_only || (out->row_flags[row]&(TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED))==
                (TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED)) &&
           (out->row_flags[row]&TRACK_ROW_PATH_VALID) && !(out->row_flags[row]&TRACK_ROW_ZEBRA)) {
            ++count;sx+=row;sy+=out->center[row];sxx+=row*row;sxy+=row*out->center[row];
        }
    if(count<6)return 0;
    denominator=count*sxx-sx*sx;if(denominator<=0)return 0;
    slope=(float)(count*sxy-sx*sy)/denominator;intercept=((float)sy-slope*sx)/count;
    vision.incoming_near=(int16_t)(vision_clip((int)(intercept+slope*VISION_NEAR_FIRST_ROW),2,185)-VISION_CENTER_COL);
    vision.incoming_far=(int16_t)(vision_clip((int)(intercept+slope*VISION_FAR_FIRST_ROW),2,185)-VISION_CENTER_COL);
    vision.incoming_valid=1;
    return 1;
}

/* 十字内的单条近边可能是横向支路边缘，不能重定义入射方向。
 * 近场无双边走廊时，从当前灰度中关联连续、正常宽度的前向出口。 */
static void vision_cross_update_direction(const uint8_t *gray,track_vision_result_t *out)
{
    uint8_t lefts[TRACK_VISION_HEIGHT]={0},rights[TRACK_VISION_HEIGHT]={0},centers[TRACK_VISION_HEIGHT]={0};
    int row,run=0,best=0,best_end=0,previous=0;
    int count=0,sx=0,sy=0,sxx=0,sxy=0,denominator,near_error,far_error;
    float slope,intercept;
    if(vision_cross_direction(out,1))return;
    for(row=VISION_TOP_ROW;row<=VISION_FAR_LAST_ROW;++row) {
        int col=2,found=0,best_delta=VISION_EDGE_WINDOW_PX+1,width=vision_width(row);
        int predicted=VISION_CENTER_COL+vision.incoming_near+(VISION_NEAR_FIRST_ROW-row)*
            (vision.incoming_far-vision.incoming_near)/(VISION_NEAR_FIRST_ROW-VISION_FAR_FIRST_ROW);
        while(col<=185) {
            int left,right,center,delta;
            if(!vision_white(gray,row,col,out->threshold)){++col;continue;}
            left=col;
            while(col<=185 && vision_white(gray,row,col,out->threshold))++col;
            right=col-1;center=(left+right)/2;delta=vision_abs(center-predicted);
            if(left>2 && right<185 && (right-left)*100>=width*55 && (right-left)*100<=width*135 &&
               !vision_white(gray,row,left-2,out->threshold) && !vision_white(gray,row,right+2,out->threshold) && delta<best_delta) {
                lefts[row]=(uint8_t)left;rights[row]=(uint8_t)right;centers[row]=(uint8_t)center;
                best_delta=delta;found=1;
            }
        }
        if(found) {
            run=run && vision_abs(centers[row]-previous)>VISION_MAX_ROW_STEP_PX?1:run+1;
            previous=centers[row];if(run>best){best=run;best_end=row;}
        } else run=0;
    }
    if(best<6)return;
    for(row=best_end-best+1;row<=best_end;++row) {
        ++count;sx+=row;sy+=centers[row];sxx+=row*row;sxy+=row*centers[row];
    }
    denominator=count*sxx-sx*sx;if(denominator<=0)return;
    slope=(float)(count*sxy-sx*sy)/denominator;intercept=((float)sy-slope*sx)/count;
    near_error=vision_clip((int)(intercept+slope*VISION_NEAR_FIRST_ROW),2,185)-VISION_CENTER_COL;
    far_error=vision_clip((int)(intercept+slope*VISION_FAR_FIRST_ROW),2,185)-VISION_CENTER_COL;
    if(vision_abs(near_error-vision.incoming_near)>VISION_EDGE_WINDOW_PX ||
       vision_abs(far_error-vision.incoming_far)>VISION_EDGE_WINDOW_PX)return;
    vision.incoming_near=(int16_t)near_error;vision.incoming_far=(int16_t)far_error;
    for(row=best_end-best+1;row<=best_end;++row) {
        out->left[row]=lefts[row];out->right[row]=rights[row];out->center[row]=centers[row];
        out->row_flags[row]=TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED|TRACK_ROW_PATH_VALID;
    }
}

static uint8_t vision_current_exit_cue;
static void vision_elements(track_vision_result_t *out,const vision_features_t *features,
                            uint32_t now_ms,uint32_t distance_mm)
{
    uint32_t traveled=distance_mm-vision.phase_mm;
    int curved=vision_abs(out->far_error_px-out->near_error_px)>=VISION_CURVE_ERROR_PX;
    int normal=features->paired_rows>=(VISION_BOTTOM_ROW-VISION_TOP_ROW)*3/4 && !curved && out->valid;
    int left=features->left_gap && !features->right_gap && features->left_supported;
    int right=features->right_gap && !features->left_gap && features->right_supported;
    int bounded_cross=features->left_gap && features->right_gap;
    int cross=bounded_cross;
    int incoming_straight=vision_abs(out->far_error_px-out->near_error_px)<=VISION_RING_APPROACH_MAX_CURVE_PX &&
        vision_abs(out->near_error_px)<=VISION_RING_APPROACH_MAX_NEAR_PX;
    unsigned joint_run=0,joint_best=0,near_open=0,near_paired=0,near_any_open=0;
    int row;
    /* 分支展开会污染远场双边平均。来向只能由当前近场实测双边或
     * 最近180ms内的同类证据确定，不能由单条横向支路重新定义。 */
    if(vision.phase!=TRACK_ELEMENT_CROSS && vision.phase<TRACK_ELEMENT_ROUNDABOUT_APPROACH) {
        if(vision_cross_direction(out,1))vision.incoming_ms=now_ms;
        else if(now_ms-vision.incoming_ms>VISION_CANDIDATE_MAX_GAP_MS)vision.incoming_valid=0;
    }
    if(vision.incoming_valid && now_ms-vision.incoming_ms<=VISION_CANDIDATE_MAX_GAP_MS)
        incoming_straight=vision_abs(vision.incoming_far-vision.incoming_near)<=VISION_RING_APPROACH_MAX_CURVE_PX &&
            vision_abs(vision.incoming_near)<=VISION_RING_APPROACH_MAX_NEAR_PX;
    out->left_island=features->left_island;out->right_island=features->right_island;
    out->ring_left_reject=(uint8_t)((!features->left_gap?1:0)|(!features->left_supported?2:0)|
        (!features->left_fork?4:0)|(!features->left_island?8:0)|(!incoming_straight?16:0)|(!out->valid?32:0));
    out->ring_right_reject=(uint8_t)((!features->right_gap?1:0)|(!features->right_supported?2:0)|
        (!features->right_fork?4:0)|(!features->right_island?8:0)|(!incoming_straight?16:0)|(!out->valid?32:0));
    for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
        joint_run=features->left_open[row] && features->right_open[row]?joint_run+1U:0U;
        if(joint_run>joint_best)joint_best=joint_run;
        if(row>=VISION_NEAR_FIRST_ROW && row<=VISION_NEAR_LAST_ROW) {
            if(features->left_open[row] && features->right_open[row])++near_open;
            if(features->left_open[row] || features->right_open[row])++near_any_open;
            if((out->row_flags[row]&3U)==3U)++near_paired;
        }
    }
    /* 斜向接近时一端闭合边可能出画面；近场仍有实测锚点且连续双侧
     * 同时拓宽，才允许进入有限的十字通行状态。 */
    if(out->near_path_supported && joint_best>=VISION_CROSS_WIDE_MIN_ROWS &&
       out->left_opening_rows>=VISION_OPENING_MIN_ROWS && out->right_opening_rows>=VISION_OPENING_MIN_ROWS)cross=1;
    if(now_ms-vision.last_frame_ms>VISION_CANDIDATE_MAX_GAP_MS) {
        vision_clear_candidates();vision.recovery=0;vision.exit_candidate=0;
        vision.curve_outer_frames=0;vision.curve_outer_candidate=TRACK_SIDE_NONE;
    }
    vision.last_frame_ms=now_ms;
    if(vision.phase==TRACK_ELEMENT_CROSS) {
        int distance_expired=traveled>VISION_CROSS_MAX_MM ||
            (vision.cross_near_seen && distance_mm-vision.cross_near_mm>VISION_CROSS_NEAR_MAX_MM);
        if(distance_expired || now_ms-vision.phase_ms>VISION_CROSS_TIMEOUT_MS) {
            vision_limit_fault(now_ms,distance_mm,(uint8_t)((distance_expired?1:0)|
                (now_ms-vision.phase_ms>VISION_CROSS_TIMEOUT_MS?2:0)));
        }
        if(!vision.cross_near_seen && near_open>=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)/2) {
            vision.cross_near_seen=1;vision.cross_near_mm=distance_mm;
        }
        /* 仅凭远场拓宽进入的是暂定十字。近场双开口确认之前，当前连续的
         * 单侧缺口、分叉、岛边和对侧支持可修正身份，避免早判屏蔽真正环岛。
         * 已确认近场十字或超过重判距离后不允许改道；不借旧缓存补足证据。 */
        if(vision.cross_provisional && !vision.cross_near_seen && !vision.fault &&
           traveled<=VISION_CROSS_RECONSIDER_MAX_MM &&
           (!vision.cooldown || distance_mm-vision.cooldown_mm>=VISION_RING_COOLDOWN_MM)) {
            vision.candidate_left=vision_increment(vision.candidate_left,left && features->left_fork &&
                features->left_island && incoming_straight && out->valid && !out->zebra);
            vision.candidate_right=vision_increment(vision.candidate_right,right && features->right_fork &&
                features->right_island && incoming_straight && out->valid && !out->zebra);
            if(vision.candidate_left>=VISION_RING_CONFIRM_FRAMES || vision.candidate_right>=VISION_RING_CONFIRM_FRAMES) {
                vision.side=vision.candidate_left>=VISION_RING_CONFIRM_FRAMES?TRACK_SIDE_LEFT:TRACK_SIDE_RIGHT;
                vision.incoming_near=out->near_error_px;vision.incoming_far=out->far_error_px;
                vision_change(TRACK_ELEMENT_ROUNDABOUT_APPROACH,now_ms,distance_mm);
                vision_clear_candidates();out->ring_entry_supported=1;
                goto elements_complete;
            }
        }
        /* 双侧开口已到达近场，随后近场双边重新闭合，即已越过十字。
         * 出口的远场可以紧接弯道，不能要求整幅图恢复直道。 */
        vision.recovery=vision_increment(vision.recovery,(normal && !cross) ||
            (vision.cross_near_seen && out->valid && near_open==0 &&
             near_paired*100U>=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)*VISION_CURVE_OUTER_NEAR_MIN_PERCENT));
        vision.diagnostic.exit_evidence=(uint8_t)((vision.cross_near_seen?1:0)|(near_open==0?2:0)|
            (near_paired*100U>=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)*VISION_CURVE_OUTER_NEAR_MIN_PERCENT?4:0)|
            (out->valid?8:0)|(normal?16:0));
        if(vision.recovery>=VISION_RECOVERY_FRAMES) vision_change(TRACK_ELEMENT_STRAIGHT,now_ms,distance_mm);
    } else if(vision.phase>=TRACK_ELEMENT_ROUNDABOUT_APPROACH) {
        int same_gap=vision.side==TRACK_SIDE_LEFT?left:right;
        /* 已确认环内时内边本来可能出画面；出口只要求外侧的新有界缺口。
         * 入环候选的对侧连续性条件不能照搬到出口，仍保留有效路径和里程门限。 */
        int outer_gap=vision.side==TRACK_SIDE_LEFT?features->right_gap:features->left_gap;
        track_side_t outer_side=vision.side==TRACK_SIDE_LEFT?TRACK_SIDE_RIGHT:TRACK_SIDE_LEFT;
        unsigned outer_open_rows=vision.side==TRACK_SIDE_LEFT?out->right_opening_rows:out->left_opening_rows;
        int outer_widening=outer_open_rows>=VISION_RING_EXIT_WIDEN_ROWS &&
            vision_outer_near_rows(out,outer_side)*100U>=
                (VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)*VISION_CURVE_OUTER_NEAR_MIN_PERCENT;
        if(vision.phase==TRACK_ELEMENT_ROUNDABOUT_INSIDE)vision_current_exit_cue=(uint8_t)(outer_gap || outer_widening);
        int same_fork=vision.side==TRACK_SIDE_LEFT?
            (features->left_fork && features->left_island):(features->right_fork && features->right_island);
        uint8_t bottom=vision.side==TRACK_SIDE_LEFT?features->left_bottom:features->right_bottom;
        if(vision.phase!=TRACK_ELEMENT_ROUNDABOUT_APPROACH &&
           (traveled>VISION_RING_PHASE_MAX_MM || now_ms-vision.phase_ms>VISION_RING_PHASE_TIMEOUT_MS))
        {
            vision_limit_fault(now_ms,distance_mm,(uint8_t)((traveled>VISION_RING_PHASE_MAX_MM?1:0)|
                (now_ms-vision.phase_ms>VISION_RING_PHASE_TIMEOUT_MS?2:0)));
        }
        switch(vision.phase) {
        case TRACK_ELEMENT_ROUNDABOUT_APPROACH:
            /* 限界先于确认提交；历史分叉不能授权当前不可见的入口。 */
            if(traveled>VISION_RING_APPROACH_MAX_MM || now_ms-vision.phase_ms>VISION_RING_APPROACH_MAX_MS) {
                vision.side=TRACK_SIDE_NONE;
                vision_change(TRACK_ELEMENT_STRAIGHT,now_ms,distance_mm);
                vision_clear_candidates();break;
            }
            vision.recovery=vision_increment(vision.recovery,same_gap && same_fork && out->valid && bottom>=VISION_RING_ENTRY_ROW);
            /* 近端岛边暂未完整可见时保留有界候选；正式入环仍要求same_fork。 */
            vision.approach_clear=vision_increment(vision.approach_clear,out->valid &&
                (!same_gap || !(vision.side==TRACK_SIDE_LEFT?features->left_fork:features->right_fork) || cross));
            out->ring_entry_supported=(uint8_t)(same_gap && same_fork && out->valid && !cross);
            if(vision.recovery>=VISION_RING_CONFIRM_FRAMES) {
                vision_change(TRACK_ELEMENT_ROUNDABOUT_ENTER,now_ms,distance_mm);
            } else if(vision.approach_clear>=VISION_RECOVERY_FRAMES) {
                vision.side=TRACK_SIDE_NONE;
                vision_change(TRACK_ELEMENT_STRAIGHT,now_ms,distance_mm);
                vision_clear_candidates();
            }
            break;
        case TRACK_ELEMENT_ROUNDABOUT_ENTER:
            vision.recovery=vision_increment(vision.recovery,out->valid &&
                (vision.side==TRACK_SIDE_LEFT ?
                 (out->far_error_px<=-VISION_CURVE_ERROR_PX && out->far_error_px-out->near_error_px<=-VISION_CURVE_ERROR_PX) :
                 (out->far_error_px>=VISION_CURVE_ERROR_PX && out->far_error_px-out->near_error_px>=VISION_CURVE_ERROR_PX)));
            if(traveled>=VISION_RING_ENTER_MIN_MM && vision.recovery>=VISION_RING_CONFIRM_FRAMES) {
                vision.curve_seen=1;vision.exit_seen=0;vision.exit_near_seen=0;vision.ring_path_valid=0;vision.ring_path_seen=0;vision_change(TRACK_ELEMENT_ROUNDABOUT_INSIDE,now_ms,distance_mm);
            }
            break;
        case TRACK_ELEMENT_ROUNDABOUT_INSIDE:
            /* 完成入环后，必须看到新的外侧分叉，单独直道恢复不能出环。 */
            /* 出口预告和相对图像路径共用一份总寿命，不能被中间几帧
             * 高Q重置。没有当前近场开口时，旧预告不能无限授权出环。 */
            if(vision.exit_seen && (distance_mm-vision.exit_seen_mm>VISION_RING_EXIT_MEMORY_MAX_MM ||
                                   now_ms-vision.exit_seen_ms>VISION_RING_EXIT_MEMORY_MAX_MS)) {
                vision.exit_seen=0;vision.exit_near_seen=0;vision.ring_path_valid=0;vision.recovery=0;vision.exit_candidate=0;
            }
            if(vision.exit_seen && (vision.side==TRACK_SIDE_LEFT?features->right_open[VISION_RING_EXIT_NEAR_ROW]:features->left_open[VISION_RING_EXIT_NEAR_ROW]))vision.exit_near_seen=1;
            vision.recovery=vision_increment(vision.recovery,vision.exit_seen && vision.exit_near_seen &&
                out->near_path_supported && features->measured_rows>=VISION_MIN_MEASURED_ROWS);
            if(vision.recovery>=VISION_RING_CONFIRM_FRAMES)
                vision_change(TRACK_ELEMENT_ROUNDABOUT_EXIT,now_ms,distance_mm);
            break;
        case TRACK_ELEMENT_ROUNDABOUT_EXIT:
            vision.diagnostic.exit_evidence=(uint8_t)((near_any_open==0?2:0)|
                (near_paired*100U>=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)*VISION_CURVE_OUTER_NEAR_MIN_PERCENT?4:0)|
                (out->valid?8:0)|(normal?16:0));
            /* 出口已在上一阶段确认；近场双边闭合并连续恢复即可结束出环。
             * 远处紧接连续弯道时，不要求整幅恢复直道。仍保留最短出环
             * 里程、当前有效路径、无近场开口及三帧确认。 */
            vision.recovery=vision_increment(vision.recovery,(normal && !left && !right) ||
                (out->valid && near_any_open==0 &&
                 near_paired*100U>=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)*VISION_CURVE_OUTER_NEAR_MIN_PERCENT));
            if(traveled>=VISION_RING_EXIT_MIN_MM && vision.recovery>=VISION_RECOVERY_FRAMES) {
                vision.side=TRACK_SIDE_NONE;vision.cooldown=1;vision.cooldown_mm=distance_mm;
                vision_change(TRACK_ELEMENT_STRAIGHT,now_ms,distance_mm);
                vision_clear_candidates();
            }
            break;
        default: break;
        }
    } else {
        if(vision.cooldown && distance_mm-vision.cooldown_mm>=VISION_RING_COOLDOWN_MM) vision.cooldown=0;
        vision.candidate_cross=vision_increment(vision.candidate_cross,cross && out->valid && !out->zebra);
        vision.candidate_left=vision_increment(vision.candidate_left,left && features->left_fork && incoming_straight && out->valid && !out->zebra && !vision.cooldown);
        vision.candidate_right=vision_increment(vision.candidate_right,right && features->right_fork && incoming_straight && out->valid && !out->zebra && !vision.cooldown);
        if(vision.candidate_cross>=VISION_CROSS_CONFIRM_FRAMES) {
            if(!vision.incoming_valid || now_ms-vision.incoming_ms>VISION_CANDIDATE_MAX_GAP_MS) {
                vision.incoming_near=out->near_error_px;vision.incoming_far=out->far_error_px;
                /* 首次斜向十字若双边已出画面，用最近端实测边初始化。
                 * 已有双边来向时不进入此分支，单边不能覆盖历史方向。 */
                (void)vision_cross_direction(out,0);
            }
            vision_change(TRACK_ELEMENT_CROSS,now_ms,distance_mm);vision.cross_provisional=(uint8_t)!bounded_cross;
            vision_clear_candidates();
        } else if((vision.candidate_left>=VISION_RING_CONFIRM_FRAMES && features->left_island) ||
                  (vision.candidate_right>=VISION_RING_CONFIRM_FRAMES && features->right_island)) {
            vision.side=vision.candidate_left>=VISION_RING_CONFIRM_FRAMES && features->left_island?TRACK_SIDE_LEFT:TRACK_SIDE_RIGHT;
            if(!vision.incoming_valid || now_ms-vision.incoming_ms>VISION_CANDIDATE_MAX_GAP_MS) {
                vision.incoming_near=out->near_error_px;vision.incoming_far=out->far_error_px;
            }
            out->ring_entry_supported=1;
            vision_change(TRACK_ELEMENT_ROUNDABOUT_APPROACH,now_ms,distance_mm);
            vision_clear_candidates();
        }
    }
elements_complete:
    out->element=vision.phase;
    out->side=vision.side;
    if(vision.phase==TRACK_ELEMENT_STRAIGHT) out->element=curved?TRACK_ELEMENT_CURVE:TRACK_ELEMENT_STRAIGHT;
    out->fault|=vision.fault;
    out->fault_element=vision.fault_element;
    vision_diagnostic(out,now_ms,distance_mm);
}

static int vision_cross_far_supported(const track_vision_result_t *out,
                                      const vision_features_t *features)
{
    unsigned supported=0;
    int row;
    for(row=VISION_FAR_FIRST_ROW;row<=VISION_FAR_LAST_ROW;++row)
        if((out->row_flags[row]&(TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED)) ||
           (features->left_open[row] && features->right_open[row])) ++supported;
    /* 近场边线和上一帧方向不能代替远场可见性。白区开口可有限外推，
     * 黑区、遮挡或任意丢线都不提供十字通行证据。 */
    return supported>=(VISION_FAR_LAST_ROW-VISION_FAR_FIRST_ROW+2)/2;
}


static float geometry_ground_a,geometry_ground_b,geometry_row_slope;
static uint8_t geometry_ground_ready,geometry_ground_valid;
static void vision_geometry_calibration(void)
{
    float cosine,sine,focal;
    geometry_ground_ready=1;geometry_ground_valid=0;
    geometry_row_slope=(float)(VISION_WIDTH_BOTTOM_PX-VISION_WIDTH_TOP_PX)/(VISION_BOTTOM_ROW-VISION_TOP_ROW);
    cosine=geometry_row_slope*VISION_CAMERA_HEIGHT_MM/VISION_ROAD_WIDTH_MM;
    if(!(cosine>0.0f && cosine<1.0f))return;
    sine=sqrtf(1.0f-cosine*cosine);
    focal=(VISION_WIDTH_TOP_PX*VISION_CAMERA_HEIGHT_MM/VISION_ROAD_WIDTH_MM-
        (VISION_TOP_ROW-VISION_PRINCIPAL_ROW)*cosine)/sine;
    if(!(focal>0.0f))return;
    geometry_ground_a=VISION_ROAD_WIDTH_MM*focal/cosine;
    geometry_ground_b=VISION_CAMERA_FORWARD_MM-VISION_CAMERA_HEIGHT_MM*sine/cosine;
    geometry_ground_valid=isfinite(geometry_ground_a) && isfinite(geometry_ground_b);
}

static int contour_goal_valid,contour_model_valid,contour_model_inner;
static float contour_line_nx,contour_line_ny,contour_line_min,contour_line_max;
static float contour_model_x,contour_model_y,contour_model_radius,contour_model_error;
/* 仅登记本帧局部轮廓；圆的另一侧不能凭半径相符就被当作道路。 */
static uint8_t contour_arc_support[360];
static int vision_arc_bin(float x,float y)
{
    int bin=(int)floorf(atan2f(y-contour_model_y,x-contour_model_x)*57.295779513f);
    return (bin+360)%360;
}
static int vision_model_contains(float x,float y) {
    float d,lower,upper,margin;
    if(!contour_model_valid)return 0;
    margin=10+2*contour_model_error;
    if(contour_model_valid==2) {
        float along=(x-contour_model_x)*(-contour_line_ny)+(y-contour_model_y)*contour_line_nx;
        d=(x-contour_model_x)*contour_line_nx+(y-contour_model_y)*contour_line_ny;
        return d>=margin && d<=VISION_ROAD_WIDTH_MM-margin && along>=contour_line_min-VISION_GEOMETRY_EXTRAPOLATE_MM && along<=contour_line_max+VISION_GEOMETRY_EXTRAPOLATE_MM;
    }
    {
        int bin=vision_arc_bin(x,y),delta,found=0;
        int budget=(int)floorf(VISION_GEOMETRY_EXTRAPOLATE_MM/contour_model_radius*57.295779513f)-1;
        if(budget<0)budget=0; /* 吸收一度bin量化误差，不能放宽150mm限额。 */
        for(delta=0;delta<=budget;++delta)
            if(contour_arc_support[(bin+delta)%360] || contour_arc_support[(bin-delta+360)%360]){found=1;break;}
        if(!found)return 0;
    }
    d=hypotf(x-contour_model_x,y-contour_model_y);
    lower=contour_model_inner?contour_model_radius:contour_model_radius-VISION_ROAD_WIDTH_MM;
    upper=contour_model_inner?contour_model_radius+VISION_ROAD_WIDTH_MM:contour_model_radius;
    return d>=lower+margin && d<=upper-margin;
}

static float contour_goal_x,contour_goal_y;
static unsigned contour_points,geometry_visible,geometry_inferred;
static uint8_t geometry_reject;
/* 水平扫线看不到近水平边界时，以当前纵向黑白交界作轮廓起点。
 * 后续仍跟踪真实相邻像素并验收地面跨度，不能把几列交点当多条边。 */
static int vision_vertical_seed(const uint8_t *gray,const track_vision_result_t *out,
                                int *x,int *y,int *bx,int *by)
{
    int row,col=VISION_CENTER_COL;
    if(out->element<TRACK_ELEMENT_ROUNDABOUT_ENTER)return 0;
    for(row=VISION_BOTTOM_ROW;row>=VISION_RING_BRANCH_TOP_ROW+1;--row)
        if(!vision_white(gray,row,col,out->threshold) && !vision_white(gray,row-1,col,out->threshold) &&
           vision_white(gray,row+1,col,out->threshold) && vision_white(gray,row+2,col,out->threshold)) {
            *x=col;*y=row;*bx=col;*by=row+1;return 1;
        }
    return 0;
}
static int vision_contour_path(const uint8_t *gray,track_vision_result_t *out,track_side_t side)
{
    static const int dx[8]={1,1,0,-1,-1,-1,0,1},dy[8]={0,1,1,1,0,-1,-1,-1};
    /* 视觉接口本身有串行状态，不可重入；工作数组放静态区，避免占用8KB主栈。 */
    static float xs[300],ys[300],sums[120];
    static unsigned cnt[120];
    int row,x=0,y=0,bx=0,by=0,n=0,i,startx=0,starty=0,first=1,prevrow=-1,created=0,vertical=0;
    float prevcol=0,best_error=1e9f;
    float min_x=1e9f,max_x=-1e9f,min_y=1e9f,max_y=-1e9f,span;
    int sign=side==TRACK_SIDE_LEFT?-1:1;
    contour_goal_valid=0;contour_model_valid=0;contour_points=0;
    {
    int firstrow=VISION_BOTTOM_ROW,step=-1,limit=VISION_TOP_ROW;
    int bottom=side==TRACK_SIDE_LEFT?out->opening_left_bottom:out->opening_right_bottom;
    if(out->element==TRACK_ELEMENT_ROUNDABOUT_ENTER && bottom>0 && bottom<VISION_BOTTOM_ROW-2){firstrow=bottom+2;step=1;limit=VISION_BOTTOM_ROW;}
    for(row=firstrow;step>0?row<=limit:row>=limit;row+=step) {
        unsigned mask=side==TRACK_SIDE_LEFT?TRACK_ROW_LEFT_MEASURED:TRACK_ROW_RIGHT_MEASURED;
        if(out->row_flags[row]&mask) {
            x=side==TRACK_SIDE_LEFT?out->left[row]-1:out->right[row]+1;y=row;
            if(x>=2 && x<=185 && !vision_white(gray,y,x,out->threshold))break;
        }
    }
    }
    if(row<VISION_TOP_ROW || row>VISION_BOTTOM_ROW) {
        if(!vision_vertical_seed(gray,out,&x,&y,&bx,&by))return 0;
        vertical=1;
    } else {bx=x-sign;by=y;}
trace_again:
    n=0;first=1;startx=x;starty=y;
    prevrow=-1;created=0;best_error=1e9f;
    min_x=min_y=1e9f;max_x=max_y=-1e9f;
    memset(sums,0,sizeof sums);memset(cnt,0,sizeof cnt);
    while(n<300) {
        int index=0,k,found=0,previous_index=0;
        float width=VISION_WIDTH_TOP_PX+(y-VISION_TOP_ROW)*geometry_row_slope;
        if(x<=1 || x>=186 || y<12 || y>117 || width<10)break;
        xs[n]=(x-93)*VISION_ROAD_WIDTH_MM/width;ys[n]=geometry_ground_a/width+geometry_ground_b;++n;
        for(k=0;k<8;++k)if(x+dx[k]==bx && y+dy[k]==by){index=k;break;}
        previous_index=index;
        for(k=1;k<=8;++k) {
            int q=(index+sign*k+16)%8,nx=x+dx[q],ny=y+dy[q];
            if(nx<1 || nx>186 || ny<1 || ny>118)break;
            if(!vision_white(gray,ny,nx,out->threshold)) {
                bx=x+dx[previous_index];by=y+dy[previous_index];x=nx;y=ny;found=1;break;
            }
            previous_index=q;
        }
        if(!found || (!first && x==startx && y==starty))break;
        first=0;
    }
    contour_model_valid=0;
    contour_points=(unsigned)n;
    if(n<15)goto try_vertical;
    for(i=0;i<n;++i) {
        if(xs[i]<min_x)min_x=xs[i];
        if(xs[i]>max_x)max_x=xs[i];
        if(ys[i]<min_y)min_y=ys[i];
        if(ys[i]>max_y)max_y=ys[i];
    }
    span=hypotf(max_x-min_x,max_y-min_y);
    if(span<VISION_GEOMETRY_MIN_SPAN_MM)goto try_vertical;
    do {
        unsigned used=0;
        float local_min_x=1e9f,local_max_x=-1e9f,local_min_y=1e9f,local_max_y=-1e9f;
        float mx=0,my=0,sxx=0,syy=0,sxy=0,sxq=0,syq=0,sq=0,det,a,b,error=0;
        for(i=0;i<n;++i)if(xs[i]*xs[i]+ys[i]*ys[i]<VISION_GEOMETRY_FIT_RANGE_MM*VISION_GEOMETRY_FIT_RANGE_MM){
            mx+=xs[i];my+=ys[i];++used;
            local_min_x=fminf(local_min_x,xs[i]);local_max_x=fmaxf(local_max_x,xs[i]);
            local_min_y=fminf(local_min_y,ys[i]);local_max_y=fmaxf(local_max_y,ys[i]);
        }
        /* 局部拟合仅用于视野外检查；边界较远时仍可验证完全可见的路径。 */
        if(used<15 || hypotf(local_max_x-local_min_x,local_max_y-local_min_y)<VISION_GEOMETRY_MIN_SPAN_MM)break;
        mx/=used;my/=used;
        for(i=0;i<n;++i)if(xs[i]*xs[i]+ys[i]*ys[i]<VISION_GEOMETRY_FIT_RANGE_MM*VISION_GEOMETRY_FIT_RANGE_MM) {
            float x=(xs[i]-mx)/1000,y=(ys[i]-my)/1000,q=x*x+y*y;
            sxx+=x*x;syy+=y*y;sxy+=x*y;sxq+=x*q;syq+=y*q;sq+=q;
        }
        det=sxx*syy-sxy*sxy;
        if(det>.000001f) {
            a=(sxq*syy-syq*sxy)/det;b=(syq*sxx-sxq*sxy)/det;
            contour_model_x=mx+a*500;contour_model_y=my+b*500;
            contour_model_radius=sqrtf(sq/used+(a*a+b*b)*.25f)*1000;
            for(i=0;i<n;++i)if(xs[i]*xs[i]+ys[i]*ys[i]<VISION_GEOMETRY_FIT_RANGE_MM*VISION_GEOMETRY_FIT_RANGE_MM) {
                float e=hypotf(xs[i]-contour_model_x,ys[i]-contour_model_y)-contour_model_radius;error+=e*e;
            }
            contour_model_error=sqrtf(error/used);
            {
                float orientation=0;
                for(i=3;i<n-3;++i)if(xs[i]*xs[i]+ys[i]*ys[i]<VISION_GEOMETRY_FIT_RANGE_MM*VISION_GEOMETRY_FIT_RANGE_MM) {
                    float tx=xs[i+3]-xs[i-3],ty=ys[i+3]-ys[i-3];
                    orientation+=(xs[i]-contour_model_x)*(-sign*ty)+(ys[i]-contour_model_y)*(sign*tx);
                }
                contour_model_inner=orientation>0;
            }
            contour_model_valid=contour_model_radius>=150 && contour_model_radius<=1500 && contour_model_error<VISION_GEOMETRY_FIT_ERROR_MM;
            memset(contour_arc_support,0,sizeof contour_arc_support);
            if(contour_model_valid)for(i=0;i<n;++i)
                if(xs[i]*xs[i]+ys[i]*ys[i]<VISION_GEOMETRY_FIT_RANGE_MM*VISION_GEOMETRY_FIT_RANGE_MM &&
                   fabsf(hypotf(xs[i]-contour_model_x,ys[i]-contour_model_y)-contour_model_radius)<=VISION_GEOMETRY_FIT_ERROR_MM)
                    contour_arc_support[vision_arc_bin(xs[i],ys[i])]=1;
        }
        if(!contour_model_valid) {
            float angle=.5f*atan2f(2*sxy,sxx-syy),nx=-sinf(angle),ny=cosf(angle),e2=0;
            float low=1e9f,high=-1e9f;
            float orientation=0;
            for(i=3;i<n-3;++i)if(xs[i]*xs[i]+ys[i]*ys[i]<VISION_GEOMETRY_FIT_RANGE_MM*VISION_GEOMETRY_FIT_RANGE_MM)
                orientation+=nx*(-sign*(ys[i+3]-ys[i-3]))+ny*(sign*(xs[i+3]-xs[i-3]));
            if(orientation<0){nx=-nx;ny=-ny;}
            for(i=0;i<n;++i)if(xs[i]*xs[i]+ys[i]*ys[i]<VISION_GEOMETRY_FIT_RANGE_MM*VISION_GEOMETRY_FIT_RANGE_MM) {
                float x=xs[i]-mx,y=ys[i]-my,e=x*nx+y*ny,along=-x*ny+y*nx;
                e2+=e*e;if(along<low)low=along;if(along>high)high=along;
            }
            contour_model_error=sqrtf(e2/used);
            if(contour_model_error<VISION_GEOMETRY_FIT_ERROR_MM && high-low>=150) {
                contour_model_valid=2;contour_model_x=mx;contour_model_y=my;
                contour_line_nx=nx;contour_line_ny=ny;contour_line_min=low;contour_line_max=high;
            }
        }
    } while(0);
    for(i=3;i<n-3;++i) {
        float tx=xs[i+3]-xs[i-3],ty=ys[i+3]-ys[i-3],norm=sqrtf(tx*tx+ty*ty),px,py,width,col,dist;
        int target,j;
        if(norm<5)continue;
        px=xs[i]-sign*(VISION_ROAD_WIDTH_MM*.5f)*ty/norm;py=ys[i]+sign*(VISION_ROAD_WIDTH_MM*.5f)*tx/norm;
        if(py<80 || py>1200)continue;
        width=geometry_ground_a/(py-geometry_ground_b);col=93+px*width/VISION_ROAD_WIDTH_MM;
        target=(int)roundf((width-VISION_WIDTH_TOP_PX)/geometry_row_slope+VISION_TOP_ROW);
        if(target<30 || target>116 || col<2 || col>185 || !vision_white(gray,target,(int)col,out->threshold)){prevrow=-1;continue;}
        dist=sqrtf(px*px+py*py);
        if(fabsf(dist-VISION_GEOMETRY_LOOKAHEAD_MM)<best_error && ty>-5 &&
           (out->element<TRACK_ELEMENT_ROUNDABOUT_ENTER || out->element==TRACK_ELEMENT_ROUNDABOUT_EXIT || px*(out->side==TRACK_SIDE_LEFT?-1:1)>10)) {
            best_error=fabsf(dist-VISION_GEOMETRY_LOOKAHEAD_MM);contour_goal_x=px;contour_goal_y=py;
        }
        sums[target]+=col;++cnt[target];
        if(prevrow>target && prevrow-target<=12 && fabsf(col-prevcol)<=10*(prevrow-target))
            for(j=target+1;j<prevrow;++j){sums[j]+=col+(prevcol-col)*(j-target)/(prevrow-target);++cnt[j];}
        prevrow=target;prevcol=col;
    }
    for(row=30;row<=116;++row)if(cnt[row]) {
        int center=(int)roundf(sums[row]/cnt[row]);
        if(vision_white(gray,row,center,out->threshold))++created;
    }
    /* 缺少局部目标时交回当前像素走廊，不把远处交叉支路当本车预瞄。 */
    contour_goal_valid=created>0 && best_error<180.0f && contour_goal_x*contour_goal_x+contour_goal_y*contour_goal_y>=VISION_GEOMETRY_MIN_TARGET_MM*VISION_GEOMETRY_MIN_TARGET_MM;
    if(!contour_goal_valid)goto try_vertical;
    /* 当前目标附近的实测双边优先于单边半宽模型，也不受入环偏置强推。
     * 保留真实走廊方向：若它仍指向反侧，后续入环确认必须继续等待。 */
    {
        float sx=0,sy=0;unsigned pairs=0;
        float w=geometry_ground_a/(contour_goal_y-geometry_ground_b);
        int target=(int)roundf((w-VISION_WIDTH_TOP_PX)/geometry_row_slope+VISION_TOP_ROW);
        for(row=target-2;row<=target+2;++row)if(row>=VISION_TOP_ROW && row<=VISION_BOTTOM_ROW &&
            (out->row_flags[row]&(TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED))==
            (TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED) && !(out->row_flags[row]&TRACK_ROW_ZEBRA) &&
            ((int)out->right[row]-out->left[row])*100>=vision_width(row)*VISION_MIN_WIDTH_PERCENT &&
            ((int)out->right[row]-out->left[row])*100<=vision_width(row)*VISION_MAX_WIDTH_PERCENT) {
            float width=VISION_WIDTH_TOP_PX+(row-VISION_TOP_ROW)*geometry_row_slope;
            sx+=(((float)out->left[row]+out->right[row])*.5f-VISION_CENTER_COL)*VISION_ROAD_WIDTH_MM/width;
            sy+=geometry_ground_a/width+geometry_ground_b;++pairs;
        }
        if(pairs>=3) {
            contour_goal_x=sx/pairs;contour_goal_y=sy/pairs;
            if(contour_goal_x*contour_goal_x+contour_goal_y*contour_goal_y<VISION_GEOMETRY_MIN_TARGET_MM*VISION_GEOMETRY_MIN_TARGET_MM ||
               contour_goal_x*contour_goal_x+contour_goal_y*contour_goal_y>VISION_GEOMETRY_MAX_TARGET_MM*VISION_GEOMETRY_MAX_TARGET_MM)
                goto try_vertical;
        }
    }
    for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row)if(cnt[row]) {
        int center=(int)roundf(sums[row]/cnt[row]);
        if((out->row_flags[row]&(TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED))==
           (TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED))continue;
        if(vision_white(gray,row,center,out->threshold)) {
            out->center[row]=(uint8_t)center;out->row_flags[row]|=TRACK_ROW_PATH_VALID|TRACK_ROW_RECONSTRUCTED;
        }
    }
    return 1;
try_vertical:
    contour_goal_valid=0;
    if(!vertical && vision_vertical_seed(gray,out,&x,&y,&bx,&by)) {vertical=1;goto trace_again;}
    return 0;
}

static void vision_element_path(const uint8_t *gray,track_vision_result_t *out,const vision_features_t *features,
                                uint32_t now_ms,uint32_t distance_mm)
{
    int row;
    if(out->element==TRACK_ELEMENT_CROSS) {
        vision_cross_update_direction(gray,out);
        if(!vision_cross_far_supported(out,features)) out->valid=0;
        for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row)
            if(features->left_open[row] || features->right_open[row] ||
               (out->row_flags[row]&(TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED))==0) {
                if(features->left_open[row] || features->right_open[row]) {
                    int error=vision.incoming_near + (VISION_NEAR_FIRST_ROW-row)*
                        (vision.incoming_far-vision.incoming_near)/(VISION_NEAR_FIRST_ROW-VISION_FAR_FIRST_ROW);
                    int center=vision_clip(VISION_CENTER_COL+error,2,185);
                    if(vision_white(gray,row,center,out->threshold)) {
                        out->center[row]=(uint8_t)center;
                        out->row_flags[row]|=TRACK_ROW_PATH_VALID|TRACK_ROW_RECONSTRUCTED;
                    } else out->row_flags[row]&=(uint8_t)~TRACK_ROW_PATH_VALID;
                } else if(!(out->row_flags[row]&TRACK_ROW_ZEBRA)) {
                    out->row_flags[row]&=(uint8_t)~TRACK_ROW_PATH_VALID;
                }
            }
    } else if(out->element==TRACK_ELEMENT_ROUNDABOUT_ENTER) {
        int direction=out->side==TRACK_SIDE_LEFT?-1:1;
        for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
            int opening=out->side==TRACK_SIDE_LEFT?features->left_open[row]:features->right_open[row];
            if(opening) {
                int bias=VISION_RING_ENTRY_BIAS_PX*(VISION_BOTTOM_ROW-row)/(VISION_BOTTOM_ROW-VISION_FAR_FIRST_ROW);
                int center=vision_clip(out->center[row]+direction*bias,2,185);
                /* 只在当前可见的侧向白区选支，不偏移已经闭合的真实走廊。 */
                if(vision_white(gray,row,center,out->threshold)) {
                    out->center[row]=(uint8_t)center;
                    out->row_flags[row]|=TRACK_ROW_RECONSTRUCTED|TRACK_ROW_PATH_VALID;
                }
            }
        }
    } else if(out->element==TRACK_ELEMENT_ROUNDABOUT_INSIDE) {
        /* 环内优先固定内侧边，外侧新出现的直道分支不参与双边平均。 */
        for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
            int center=out->center[row];
            int selected=0;
            if(out->side==TRACK_SIDE_LEFT && (out->row_flags[row]&TRACK_ROW_LEFT_MEASURED)) {
                center=out->left[row]+vision_width(row)/2;selected=1;
            } else if(out->side==TRACK_SIDE_RIGHT && (out->row_flags[row]&TRACK_ROW_RIGHT_MEASURED)) {
                center=out->right[row]-vision_width(row)/2;selected=1;
            }
            if(!selected &&
               vision.exit_seen && vision.ring_path_valid &&
               vision_white(gray,row,vision.ring_path[row],out->threshold)) {
                center=vision.ring_path[row];selected=1;
            }
            if(selected) {
                out->center[row]=(uint8_t)vision_clip(center,2,185);
                out->row_flags[row]|=TRACK_ROW_RECONSTRUCTED;
            }
        }
        /* 只保存外侧尚未展开、当前确实向岛内转弯的可靠图像路径。
         * 保存后仍逐行验证当前白像素；上述统一寿命约束所有重放帧。 */
        if(!vision.exit_seen && (out->side==TRACK_SIDE_LEFT?out->right_opening_rows:out->left_opening_rows)<=VISION_RING_CACHE_MAX_OPEN_ROWS &&
           out->valid && out->confidence>=VISION_RING_CACHE_MIN_CONFIDENCE &&
           (out->side==TRACK_SIDE_LEFT?out->far_error_px<=-VISION_CURVE_ERROR_PX:out->far_error_px>=VISION_CURVE_ERROR_PX)) {
            memcpy(vision.ring_path,out->center,TRACK_VISION_HEIGHT);vision.ring_path_valid=1;vision.ring_path_seen=1;
            vision.ring_path_saved_ms=now_ms;vision.ring_path_saved_mm=distance_mm;
        }
    } else if(out->element==TRACK_ELEMENT_ROUNDABOUT_EXIT) {
        int direction=out->side==TRACK_SIDE_LEFT?1:-1;
        uint8_t outer=out->side==TRACK_SIDE_LEFT?TRACK_ROW_RIGHT_MEASURED:TRACK_ROW_LEFT_MEASURED;
        uint8_t inner=out->side==TRACK_SIDE_LEFT?TRACK_ROW_LEFT_MEASURED:TRACK_ROW_RIGHT_MEASURED;
        /* 仅在已确认出口的缺边行引向外侧；双边恢复后直接采用实测走廊。 */
        for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row)
            if(!(out->row_flags[row]&outer) && (out->row_flags[row]&inner)) {
                int bias=VISION_RING_EXIT_BIAS_PX*(VISION_BOTTOM_ROW-row)/
                    (VISION_BOTTOM_ROW-VISION_FAR_FIRST_ROW);
                if(vision.ring_path_valid && vision_white(gray,row,vision.ring_path[row],out->threshold))
                    out->center[row]=vision.ring_path[row];
                else out->center[row]=(uint8_t)vision_clip(out->center[row]+direction*bias,2,185);
                out->row_flags[row]|=TRACK_ROW_RECONSTRUCTED;
            }
    }
}

static int vision_near_supported(const track_vision_result_t *out)
{
    unsigned valid=0,measured=0,anchor=0;
    int row;
    for(row=VISION_NEAR_FIRST_ROW;row<=VISION_NEAR_LAST_ROW;++row) {
        if(out->row_flags[row]&TRACK_ROW_PATH_VALID) ++valid;
        if(out->row_flags[row]&(TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED)) ++measured;
    }
    for(row=VISION_NEAR_LAST_ROW+1;row<=VISION_BOTTOM_ROW;++row)
        if(out->row_flags[row]&(TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED)) ++anchor;
    return valid>=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)/2 &&
        (measured>=4U || (out->zebra && anchor>=3U));
}

/* 已确认节点会暂时缺边。只把当前像素仍为白色、且有实测边界锚定的
 * 连续路径用于短距离过渡；无边白板、黑色前路和超时均不能获得许可。 */
static void vision_element_bridge(const uint8_t *gray,track_vision_result_t *out,const vision_features_t *features,
                                  uint32_t now_ms,uint32_t distance_mm)
{
    uint8_t white[TRACK_VISION_HEIGHT]={0};
    unsigned near_white=0,far_white=0,near_edges=0,far_edges=0,anchors=0,score,support;
    unsigned outer_near=0,outer_far=0,far_white_percent=VISION_ELEMENT_WHITE_PERCENT;
    uint8_t outer_mask=out->curve_outer_side==TRACK_SIDE_LEFT?TRACK_ROW_LEFT_MEASURED:TRACK_ROW_RIGHT_MEASURED;
    const unsigned near_count=VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1;
    const unsigned far_count=out->preview_last_row-out->preview_first_row+1;
    const unsigned far_evidence_count=out->preview_last_row-VISION_TOP_ROW+1;
    int row,previous=out->center[VISION_BOTTOM_ROW];
    if(out->element==TRACK_ELEMENT_STRAIGHT || out->element==TRACK_ELEMENT_CURVE ||
       out->element==TRACK_ELEMENT_ROUNDABOUT_APPROACH) {
        vision.bridge_active=0;vision.bridge_recovery=0;return;
    }
    if(out->fault){out->valid=0;return;}
    for(row=VISION_BOTTOM_ROW;row>=VISION_TOP_ROW;--row) {
        unsigned evidence=0;
        int center=vision_clip(out->center[row],previous-VISION_MAX_ROW_STEP_PX,previous+VISION_MAX_ROW_STEP_PX);
        if((out->row_flags[row]&TRACK_ROW_ZEBRA) || vision_white(gray,row,center,out->threshold)) {
            white[row]=1;previous=center;
            if(center!=out->center[row])out->row_flags[row]|=TRACK_ROW_RECONSTRUCTED;
            out->center[row]=(uint8_t)center;
            if(!(out->row_flags[row]&TRACK_ROW_ZEBRA)) {
                if((out->row_flags[row]&TRACK_ROW_LEFT_MEASURED) && out->left[row]<=center &&
                   center-out->left[row]<=vision_width(row))++evidence;
                if((out->row_flags[row]&TRACK_ROW_RIGHT_MEASURED) && out->right[row]>=center &&
                   out->right[row]-center<=vision_width(row))++evidence;
                /* 入环白区中一条真实边可能被开口标记屏蔽；此时横向
                 * 截面比直道更宽，允许两倍透视宽度内的当前边界作锚点。 */
                if(!evidence && out->element==TRACK_ELEMENT_ROUNDABOUT_ENTER) {
                    if((features->raw_mask[row]&1U) && features->raw_left[row]<=center &&
                       center-features->raw_left[row]<=2*vision_width(row))++evidence;
                    if((features->raw_mask[row]&2U) && features->raw_right[row]>=center &&
                       features->raw_right[row]-center<=2*vision_width(row))++evidence;
                }
            }
            if(evidence)++anchors;
            if(row>=VISION_NEAR_FIRST_ROW && row<=VISION_NEAR_LAST_ROW){++near_white;near_edges+=evidence;}
            if(row>=out->preview_first_row && row<=out->preview_last_row)++far_white;
            if(row<=out->preview_last_row)far_edges+=evidence;
            if(out->curve_outer_side!=TRACK_SIDE_NONE && (out->row_flags[row]&outer_mask) &&
               !(out->row_flags[row]&TRACK_ROW_ZEBRA)) {
                if(row>=VISION_NEAR_FIRST_ROW && row<=VISION_NEAR_LAST_ROW)++outer_near;
                if(row>=out->preview_first_row && row<=out->preview_last_row)++outer_far;
            }
        } else out->row_flags[row]&=(uint8_t)~TRACK_ROW_PATH_VALID;
    }
    /* 环内经连续性验证的实测外边沿用原弯道覆盖要求；白区补线不能
     * 反过来否定已有效的单边弯道，也不能为无实测支持的黑区放行。 */
    if(out->element==TRACK_ELEMENT_ROUNDABOUT_INSIDE && out->valid &&
       outer_near*100U>=near_count*VISION_CURVE_OUTER_NEAR_MIN_PERCENT &&
       outer_far*100U>=far_count*VISION_CURVE_OUTER_FAR_MIN_PERCENT)
        far_white_percent=VISION_CURVE_OUTER_FAR_MIN_PERCENT;
    if((out->element==TRACK_ELEMENT_ROUNDABOUT_INSIDE || out->element==TRACK_ELEMENT_ROUNDABOUT_EXIT) && vision.exit_seen && vision.ring_path_valid)
        far_white_percent=VISION_CURVE_OUTER_FAR_MIN_PERCENT;
    if(near_white*100U<near_count*VISION_ELEMENT_WHITE_PERCENT ||
       far_white*100U<far_count*far_white_percent) {
        out->valid=0;vision.bridge_recovery=0;return;
    }
    if(out->valid) {
        if(vision.bridge_recovery<255)++vision.bridge_recovery;
        if(vision.bridge_recovery>=VISION_RECOVERY_FRAMES)vision.bridge_active=0;
        return;
    }
    vision.bridge_recovery=0;
    support=25U*near_edges/near_count;
    score=25U*far_edges/far_evidence_count;if(score>support)support=score;
    if(support>25U)support=25U;
    score=support+10U*near_white/near_count+15U*far_white/far_count;
    if((out->element==TRACK_ELEMENT_ROUNDABOUT_INSIDE || out->element==TRACK_ELEMENT_ROUNDABOUT_EXIT) && vision.exit_seen && vision.ring_path_valid) {
        unsigned vertical=0;int col;
        /* 横向白区边界只证明当前仍有道路轮廓，不冒充左右实测边线。
         * 有限记忆、近远白像素和轮廓必须同时存在，才给最低通行评分。 */
        for(col=VISION_CENTER_COL-(int)VISION_WIDTH_TOP_PX/2;col<=VISION_CENTER_COL+(int)VISION_WIDTH_TOP_PX/2;col+=4)
          for(row=VISION_TOP_ROW+1;row<=VISION_NEAR_LAST_ROW;++row) {
            if(vision_white(gray,row,col,out->threshold) && !vision_white(gray,row-1,col,out->threshold)){++vertical;break;}
        }
        if(vertical>=VISION_ELEMENT_GAP_MIN_ANCHORS) {
            anchors+=vertical;if(score<VISION_MIN_CONFIDENCE)score=VISION_MIN_CONFIDENCE;
        }
    }
    if(anchors<VISION_ELEMENT_GAP_MIN_ANCHORS || score<VISION_MIN_CONFIDENCE)return;
    if(!vision.bridge_active){vision.bridge_active=1;vision.bridge_ms=now_ms;vision.bridge_mm=distance_mm;}
    if(distance_mm-vision.bridge_mm>VISION_ELEMENT_GAP_MAX_MM || now_ms-vision.bridge_ms>VISION_ELEMENT_GAP_MAX_MS)return;
    out->valid=1;out->element_bridged=1;
    if(out->confidence<score)out->confidence=(uint8_t)score;
    for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row)
        if(white[row])out->row_flags[row]|=TRACK_ROW_PATH_VALID|TRACK_ROW_RECONSTRUCTED;
}

static int vision_cross_near_open(const vision_features_t *features)
{
    unsigned opened=0;
    int row;
    if(vision.phase!=TRACK_ELEMENT_CROSS) return 0;
    for(row=VISION_NEAR_FIRST_ROW;row<=VISION_NEAR_LAST_ROW;++row)
        if(features->left_open[row] && features->right_open[row]) ++opened;
    return opened>=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)/2;
}

/* 只统计外侧实测位；同一行补出的内侧不提供覆盖证据。
 * 覆盖与连续性每帧重算，不能将外侧方向标志本身视为行驶许可。 */
static void vision_curve_outer_support(track_vision_result_t *out)
{
    unsigned near_rows=0,far_rows=0,run=0,best=0;
    uint8_t measured;
    int row,previous=0;
    if(out->curve_outer_side==TRACK_SIDE_NONE) return;
    measured=out->curve_outer_side==TRACK_SIDE_LEFT?TRACK_ROW_LEFT_MEASURED:TRACK_ROW_RIGHT_MEASURED;
    for(row=VISION_BOTTOM_ROW;row>=VISION_TOP_ROW;--row) {
        int edge=out->curve_outer_side==TRACK_SIDE_LEFT?out->left[row]:out->right[row];
        if((out->row_flags[row]&measured) && (out->row_flags[row]&TRACK_ROW_PATH_VALID) &&
           !(out->row_flags[row]&TRACK_ROW_ZEBRA)) {
            if(row>=VISION_NEAR_FIRST_ROW && row<=VISION_NEAR_LAST_ROW) ++near_rows;
            if(row>=out->preview_first_row && row<=out->preview_last_row) ++far_rows;
            run=run && vision_abs(edge-previous)>VISION_MAX_ROW_STEP_PX?1U:run+1U;
            previous=edge;if(run>best)best=run;
        } else run=0;
    }
    out->curve_outer_near_rows=(uint8_t)near_rows;out->curve_outer_far_rows=(uint8_t)far_rows;
    if(near_rows*100U<(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)*VISION_CURVE_OUTER_NEAR_MIN_PERCENT)
        out->curve_outer_reject=TRACK_OUTER_REJECT_NEAR;
    else if(far_rows*100U<(VISION_FAR_LAST_ROW-VISION_FAR_FIRST_ROW+1)*VISION_CURVE_OUTER_FAR_MIN_PERCENT)
        out->curve_outer_reject=TRACK_OUTER_REJECT_FAR;
    else if(best<VISION_CURVE_OUTER_MIN_ROWS) out->curve_outer_reject=TRACK_OUTER_REJECT_CONTINUITY;
    out->curve_outer_supported=VISION_CURVE_OUTER_ENABLED && out->valid && !out->fault &&
        out->element==TRACK_ELEMENT_CURVE && out->near_path_supported &&
        out->curve_outer_reject==TRACK_OUTER_REJECT_NONE;
}

void track_vision_reset(void)
{
    memset(&vision,0,sizeof(vision));
    vision_geometry_calibration();contour_goal_valid=0;contour_model_valid=0;
}

static void vision_path_quality(track_vision_result_t *out,const vision_features_t *features)
{
    out->confidence=(uint8_t)((features->measured_rows+features->paired_rows)*50U/
        (VISION_BOTTOM_ROW-VISION_TOP_ROW+1));
    if(out->curve_outer_side!=TRACK_SIDE_NONE) {
        unsigned near_rows=0,far_rows=0,score;
        uint8_t edge=out->curve_outer_side==TRACK_SIDE_LEFT?TRACK_ROW_LEFT_MEASURED:TRACK_ROW_RIGHT_MEASURED;
        int row;
        for(row=VISION_FAR_FIRST_ROW;row<=VISION_NEAR_LAST_ROW;++row)
            if((out->row_flags[row]&edge) && (out->row_flags[row]&TRACK_ROW_PATH_VALID) &&
               !(out->row_flags[row]&TRACK_ROW_ZEBRA)) {
                if(row>=VISION_NEAR_FIRST_ROW)++near_rows;
                if(row<=VISION_FAR_LAST_ROW)++far_rows;
            }
        /* 只有连续实测外边覆盖实际控制窗口，才补充这一计分。单边最高50，
         * 不把重建内边计为实测；近远覆盖门限及后续连续性门限保持不变。 */
        if(near_rows*100U>=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)*VISION_CURVE_OUTER_NEAR_MIN_PERCENT &&
           far_rows*100U>=(VISION_FAR_LAST_ROW-VISION_FAR_FIRST_ROW+1)*VISION_CURVE_OUTER_FAR_MIN_PERCENT) {
            score=25U*near_rows/(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)+
                  25U*far_rows/(VISION_FAR_LAST_ROW-VISION_FAR_FIRST_ROW+1);
            if(score>out->confidence)out->confidence=(uint8_t)score;
        }
    }
    out->near_path_supported=(uint8_t)vision_near_supported(out);
    out->valid=(uint8_t)(features->measured_rows>=VISION_MIN_MEASURED_ROWS &&
        out->confidence>=VISION_MIN_CONFIDENCE &&
        (out->near_path_supported || vision_cross_near_open(features)));
    out->near_error_px=vision_error(out,VISION_NEAR_FIRST_ROW,VISION_NEAR_LAST_ROW);
    out->far_error_px=vision_error(out,VISION_FAR_FIRST_ROW,VISION_FAR_LAST_ROW);
}

/* 可通行评分与原始观测覆盖分开。一个可靠外边已可约束车道路径，
 * 不因缺少第二条实测边而把评分上限锁在50；补线本身不增加边界计数。 */
static void vision_path_reliability(const uint8_t *gray,track_vision_result_t *out,const vision_features_t *features,
                                    uint32_t now_ms,uint32_t distance_mm)
{
    unsigned near_white=0,far_white=0,near_anchor=0,far_anchor=0,paired=0,jitter=0,count=0;
    const unsigned nc=VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1,fc=out->preview_last_row-out->preview_first_row+1;
    unsigned ns,fs,score,coverage;
    int row;
    if(!out->valid)return;
    for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
        int center=out->center[row];unsigned evidence=0;
        int white=(out->row_flags[row]&TRACK_ROW_ZEBRA) || vision_white(gray,row,center,out->threshold);
        if(!white || !(out->row_flags[row]&TRACK_ROW_PATH_VALID))continue;
        if((out->row_flags[row]&TRACK_ROW_LEFT_MEASURED) && out->left[row]<=center && center-out->left[row]<=2*vision_width(row))++evidence;
        else if((features->raw_mask[row]&1U) && features->raw_left[row]<=center && center-features->raw_left[row]<=2*vision_width(row))++evidence;
        if((out->row_flags[row]&TRACK_ROW_RIGHT_MEASURED) && out->right[row]>=center && out->right[row]-center<=2*vision_width(row))++evidence;
        else if((features->raw_mask[row]&2U) && features->raw_right[row]>=center && features->raw_right[row]-center<=2*vision_width(row))++evidence;
        if(evidence==2)++paired;

        if(row>=VISION_NEAR_FIRST_ROW && row<=VISION_NEAR_LAST_ROW){++near_white;if(evidence)++near_anchor;}
        if(row>=out->preview_first_row && row<=out->preview_last_row){++far_white;if(evidence)++far_anchor;}
        if(row>VISION_FAR_FIRST_ROW && row<VISION_NEAR_LAST_ROW &&
           (out->row_flags[row-1]&TRACK_ROW_PATH_VALID) && (out->row_flags[row+1]&TRACK_ROW_PATH_VALID)) {
            jitter+=(unsigned)vision_abs((int)out->center[row-1]+out->center[row+1]-2*center);++count;
        }
    }
    /* 近场可见不能替代前向可见；黑色远场不能因近场高分被放行。 */
    if(near_white*100U<nc*75U || far_white*100U<fc*50U){out->valid=0;return;}
    ns=near_anchor*100U/(nc*70U/100U);if(ns>100U)ns=100U;
    fs=far_anchor*100U/(fc*50U/100U);if(fs>100U)fs=100U;
    coverage=ns<fs?ns:fs;
    score=15U+55U*coverage/100U+15U*near_white/nc+15U*far_white/fc;
    if(paired<(nc+fc)/2 && score>=10U)score-=10U;
    if(out->element>=TRACK_ELEMENT_CROSS && out->element!=TRACK_ELEMENT_ROUNDABOUT_APPROACH) {
        unsigned measured=0,near_clear=0,far_clear=0,vertical=0,geometry,clearance;
        int col;
        for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
            int center=out->center[row],half=vision_width(row)*18/100;
            unsigned mask=out->row_flags[row];
            if(!(mask&TRACK_ROW_PATH_VALID) || !vision_white(gray,row,center,out->threshold))continue;
            if(mask&(TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED))++measured;
            if(center-half>=2 && center+half<=185 && vision_white(gray,row,center-half,out->threshold) && vision_white(gray,row,center+half,out->threshold)) {
                if(row>=VISION_NEAR_FIRST_ROW && row<=VISION_NEAR_LAST_ROW)++near_clear;
                if(row>=out->preview_first_row && row<=out->preview_last_row)++far_clear;
            }
        }
        for(col=71;col<=115;col+=4)for(row=VISION_TOP_ROW+1;row<=VISION_NEAR_LAST_ROW;++row)
            if(vision_white(gray,row,col,out->threshold) && !vision_white(gray,row-1,col,out->threshold)){++vertical;break;}
        /* 当前边界只提供几何覆盖，不能把重建像素计作实测。 */
        geometry=(measured+vertical)*100U/30U;if(geometry>100U)geometry=100U;
        clearance=20U*near_clear/nc+20U*far_clear/fc+10U*near_white/nc+10U*far_white/fc;
        score=clearance+20U*geometry/100U+20U;
        if(vision.bridge_active) {
            unsigned age=(distance_mm-vision.bridge_mm)*15U/VISION_ELEMENT_GAP_MAX_MM;
            unsigned elapsed=(now_ms-vision.bridge_ms)*15U/VISION_ELEMENT_GAP_MAX_MS;
            if(elapsed>age)age=elapsed;
            if(age>20U)age=20U;
            score=score>age?score-age:0;
        }
    }
    if(count) {unsigned penalty=jitter*3U/count;if(penalty>15U)penalty=15U;score=score>penalty?score-penalty:0;}
    out->confidence=(uint8_t)(score>100U?100U:score);
}

/* 将已经过当前像素/锚点验证的走廊转换为同一地面目标。
 * 普通路与十字不再在“像素增益”和“毫米曲率”之间来回改变转向含义。
 * 此处只生成候选，后续仍必须通过与轮廓目标相同的完整轮迹检查。 */
static void vision_metric_pixel_path(const uint8_t *gray,const track_vision_result_t *out)
{
    int row,best_row=-1;unsigned used=0;
    float best=1e9f,sx=0,sy=0;
    if(!out->valid || out->fault || out->zebra || out->element>=TRACK_ELEMENT_ROUNDABOUT_APPROACH)return;
    for(row=VISION_FAR_FIRST_ROW;row<=VISION_BOTTOM_ROW;++row) {
        float width,x,y,radial,error;
        if(!(out->row_flags[row]&TRACK_ROW_PATH_VALID) ||
           !vision_white(gray,row,out->center[row],out->threshold))continue;
        width=VISION_WIDTH_TOP_PX+(row-VISION_TOP_ROW)*geometry_row_slope;
        x=(out->center[row]-VISION_CENTER_COL)*VISION_ROAD_WIDTH_MM/width;
        y=geometry_ground_a/width+geometry_ground_b;radial=hypotf(x,y);
        if(y<=0 || radial<VISION_GEOMETRY_MIN_TARGET_MM || radial>VISION_GEOMETRY_MAX_TARGET_MM)continue;
        error=fabsf(radial-VISION_GEOMETRY_LOOKAHEAD_MM);
        if(error<best){best=error;best_row=row;}
    }
    if(best_row<0 || best>80)return;
    for(row=best_row-2;row<=best_row+2;++row) {
        float width;
        if(row<VISION_TOP_ROW || row>VISION_BOTTOM_ROW || !(out->row_flags[row]&TRACK_ROW_PATH_VALID) ||
           !vision_white(gray,row,out->center[row],out->threshold))continue;
        width=VISION_WIDTH_TOP_PX+(row-VISION_TOP_ROW)*geometry_row_slope;
        sx+=(out->center[row]-VISION_CENTER_COL)*VISION_ROAD_WIDTH_MM/width;
        sy+=geometry_ground_a/width+geometry_ground_b;++used;
    }
    if(used<3)return;
    contour_goal_x=sx/used;contour_goal_y=sy/used;
    contour_goal_valid=hypotf(contour_goal_x,contour_goal_y)>=VISION_GEOMETRY_MIN_TARGET_MM &&
        hypotf(contour_goal_x,contour_goal_y)<=VISION_GEOMETRY_MAX_TARGET_MM;
    /* 不借另一条失败轮廓的圆/线拟合证明新目标的视野外区域。 */
    if(contour_goal_valid)contour_model_valid=0;
}

void track_vision_process(const uint8_t *gray, uint32_t now_ms,
                          uint32_t distance_mm, track_vision_result_t *out)
{
    vision_features_t features;
    uint8_t contrast;
    int row;
    if(out==0) return;
    memset(out,0,sizeof(*out));out->zebra_near_row=255;
    vision_current_exit_cue=0;
    vision_cache_expire(now_ms,distance_mm);
    contour_goal_valid=0;contour_model_valid=0;contour_points=0;geometry_visible=geometry_inferred=0;geometry_reject=0;
    if(!geometry_ground_ready)vision_geometry_calibration();
    if(!geometry_ground_valid){out->fault=TRACK_FAULT_PATH_LOST;out->geometry.reject_reason=1;return;}
    out->preview_first_row=VISION_FAR_FIRST_ROW;out->preview_last_row=VISION_FAR_LAST_ROW;
    for(row=0;row<120;++row) {
        out->center[row]=VISION_CENTER_COL;out->right[row]=187;
    }
    if(gray==0) {
        vision_clear_candidates();vision.recovery=0;vision.approach_clear=0;vision.exit_candidate=0;
        vision.curve_outer_candidate=TRACK_SIDE_NONE;vision.curve_outer_frames=0;
        out->fault=TRACK_FAULT_PATH_LOST|vision.fault;return;
    }
    out->threshold=vision_threshold(gray,&contrast);
    if(contrast<VISION_MIN_CONTRAST) {
        vision.curve_outer_candidate=TRACK_SIDE_NONE;vision.curve_outer_frames=0;
        out->fault=TRACK_FAULT_PATH_LOST|vision.fault;
        /* 黑白板无几何证据，不推进元素，但仍执行时间故障检查。 */
        memset(&features,0,sizeof(features));
        vision_elements(out,&features,now_ms,distance_mm);return;
    }
    vision_scan(gray,out,&features,TRACK_SIDE_NONE);
    features.left_gap=vision_gap(features.left_open,out,TRACK_ROW_LEFT_MEASURED,&features.left_bottom);
    features.right_gap=vision_gap(features.right_open,out,TRACK_ROW_RIGHT_MEASURED,&features.right_bottom);
    features.left_supported=vision_opposite_continuous(features.left_open,out,TRACK_SIDE_LEFT);
    features.right_supported=vision_opposite_continuous(features.right_open,out,TRACK_SIDE_RIGHT);
    features.left_fork=vision_forward_branch(gray,out,&features,TRACK_SIDE_LEFT);
    features.right_fork=vision_forward_branch(gray,out,&features,TRACK_SIDE_RIGHT);
    features.left_island=vision_near_island(out,&features,TRACK_SIDE_LEFT);
    features.right_island=vision_near_island(out,&features,TRACK_SIDE_RIGHT);
    vision_path_quality(out,&features);
    out->observation_confidence=out->confidence;
    out->raw_near_error_px=out->near_error_px;out->raw_far_error_px=out->far_error_px;
    out->left_fork=features.left_fork;out->right_fork=features.right_fork;
    out->opening_left_bottom=features.left_bottom;out->opening_right_bottom=features.right_bottom;
    for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
        if(features.left_open[row])++out->left_opening_rows;
        if(features.right_open[row])++out->right_opening_rows;
        if(features.left_open[row] && features.right_open[row])++out->joint_opening_rows;
    }
    vision_elements(out,&features,now_ms,distance_mm);
    {
        track_side_t outer=vision_curve_outer(out,&features,now_ms);
        if(outer!=TRACK_SIDE_NONE) {
            out->zebra_rows=0;out->zebra_near_row=255;
            vision_scan(gray,out,&features,outer);
            vision_path_quality(out,&features);
        }
    }
    if(vision.phase==TRACK_ELEMENT_STRAIGHT)
        out->element=vision_abs(out->far_error_px-out->near_error_px)>=VISION_CURVE_ERROR_PX?
            TRACK_ELEMENT_CURVE:TRACK_ELEMENT_STRAIGHT;
    vision_element_path(gray,out,&features,now_ms,distance_mm);
    contour_goal_valid=0;
    if(out->element>=TRACK_ELEMENT_ROUNDABOUT_ENTER) {
        track_vision_result_t traced=*out;
        for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
            if(features.raw_mask[row]&1U){traced.left[row]=features.raw_left[row];traced.row_flags[row]|=TRACK_ROW_LEFT_MEASURED;}
            if(features.raw_mask[row]&2U){traced.right[row]=features.raw_right[row];traced.row_flags[row]|=TRACK_ROW_RIGHT_MEASURED;}
        }
        int found=vision_contour_path(gray,&traced,out->element==TRACK_ELEMENT_ROUNDABOUT_ENTER?out->side:(out->side==TRACK_SIDE_LEFT?TRACK_SIDE_RIGHT:TRACK_SIDE_LEFT));
        if(!found && out->element==TRACK_ELEMENT_ROUNDABOUT_ENTER && distance_mm-vision.phase_mm>=VISION_RING_ENTER_MIN_MM)found=vision_contour_path(gray,&traced,out->side==TRACK_SIDE_LEFT?TRACK_SIDE_RIGHT:TRACK_SIDE_LEFT);
        if(found)for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
            out->center[row]=traced.center[row];out->row_flags[row]|=traced.row_flags[row]&(TRACK_ROW_PATH_VALID|TRACK_ROW_RECONSTRUCTED);
        }
    }
    else if(out->element==TRACK_ELEMENT_CURVE && VISION_CURVE_OUTER_ENABLED) {
        track_side_t chosen=out->curve_outer_side;
        if(chosen==TRACK_SIDE_NONE)chosen=out->far_error_px>out->near_error_px?TRACK_SIDE_LEFT:TRACK_SIDE_RIGHT;
        vision_contour_path(gray,out,chosen);
    }

    if(contour_goal_valid && out->element>=TRACK_ELEMENT_ROUNDABOUT_ENTER) {
        float curvature=2*contour_goal_x/(contour_goal_x*contour_goal_x+contour_goal_y*contour_goal_y);
        curvature=fmaxf(-tanf(VEHICLE_MAX_STEER_RAD)/VEHICLE_WHEELBASE_MM,
                         fminf(tanf(VEHICLE_MAX_STEER_RAD)/VEHICLE_WHEELBASE_MM,curvature));
        for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
            float w=vision_width(row),y=geometry_ground_a/w+geometry_ground_b,ky=curvature*y,x;
            int col;
            if(fabsf(ky)>=.95f)continue;
            x=fabsf(curvature)>.00001f?(1-sqrtf(1-ky*ky))/curvature:0;
            col=(int)roundf(93+x*w/VISION_ROAD_WIDTH_MM);
            if(col>=2 && col<=185 && vision_white(gray,row,col,out->threshold)) {
                out->center[row]=(uint8_t)col;out->row_flags[row]|=TRACK_ROW_PATH_VALID|TRACK_ROW_RECONSTRUCTED;
            }
        }
    }
    vision_choose_preview(gray,out);
    vision_element_bridge(gray,out,&features,now_ms,distance_mm);
    vision_path_reliability(gray,out,&features,now_ms,distance_mm);
    if(!contour_goal_valid)vision_metric_pixel_path(gray,out);
    if(contour_goal_valid && !out->fault) {
        unsigned good=0,total=0,inferred=0;int step,axle,across,failed=0,shortened=0;
        float radial=sqrtf(contour_goal_x*contour_goal_x+contour_goal_y*contour_goal_y);
        float curvature=2*contour_goal_x/(radial*radial);
        float arc_length;
        curvature=fmaxf(-tanf(VEHICLE_MAX_STEER_RAD)/VEHICLE_WHEELBASE_MM,fminf(tanf(VEHICLE_MAX_STEER_RAD)/VEHICLE_WHEELBASE_MM,curvature));
check_sweep:
        good=total=inferred=0;failed=0;
        arc_length=fabsf(curvature)<.00001f?radial:2*asinf(curvature*radial*.5f)/curvature;
        int steps=(int)ceilf(arc_length/VISION_GEOMETRY_SWEEP_STEP_MM);if(steps<1)steps=1;
        for(step=0;step<=steps;++step) {
            float along=arc_length*step/steps,theta=curvature*along,x,y;
            if(fabsf(curvature)<.00001f){x=0;y=along;theta=0;}
            else {x=(1-cosf(theta))/curvature;y=sinf(theta)/curvature;}
            for(axle=0;axle<=1;++axle)for(across=-1;across<=1;++across) {
                float px=x+axle*VEHICLE_WHEELBASE_MM*sinf(theta)+across*VEHICLE_HALF_TIRE_OUTER_MM*cosf(theta);
                float py=y+axle*VEHICLE_WHEELBASE_MM*cosf(theta)-across*VEHICLE_HALF_TIRE_OUTER_MM*sinf(theta),w;
                int cx,cy,u,v,white=0;++total;
                if(py<120){--total;continue;}
                w=geometry_ground_a/(py-geometry_ground_b);cx=(int)roundf(93+px*w/VISION_ROAD_WIDTH_MM);cy=(int)roundf((w-VISION_WIDTH_TOP_PX)/geometry_row_slope+VISION_TOP_ROW);
                if(cy>117){--total;continue;}
                if(cx<2 || cx>185 || cy<12) {
                    if(vision_model_contains(px,py)){++good;++inferred;}else failed|=2;
                    continue;
                }
                for(u=-1;u<=1;++u)for(v=-1;v<=1;++v)if(vision_white(gray,cy+v,cx+u,out->threshold))++white;
                if(white>=5)++good;else failed|=1;
            }
        }
        /* 仅因未知视野外区域失败时，缩短到仍满足320mm的可见目标重新检查。
         * 当前图像内的黑块不能通过缩短预瞄规避。曲率不变，控制与核验一致。 */
        if(failed==2 && !shortened && radial>VISION_GEOMETRY_MIN_TARGET_MM) {
            radial=VISION_GEOMETRY_MIN_TARGET_MM+1.0f;shortened=1;goto check_sweep;
        }
        if(!failed && total>0) {
            if(shortened) {
                float theta=curvature*arc_length;
                contour_goal_x=fabsf(curvature)<.00001f?0:(1-cosf(theta))/curvature;
                contour_goal_y=fabsf(curvature)<.00001f?radial:sinf(theta)/curvature;
            }
            out->valid=1;
            if(out->element>=TRACK_ELEMENT_ROUNDABOUT_ENTER)out->element_bridged=1;
            out->confidence=(uint8_t)(good*100U/total-15U*inferred/total);
            geometry_visible=good-inferred;geometry_inferred=inferred;
        } else {out->valid=0;contour_goal_valid=0;geometry_reject=2;}
    }
    out->near_error_px=vision_error(out,VISION_NEAR_FIRST_ROW,VISION_NEAR_LAST_ROW);
    out->far_error_px=vision_preview_error(out);
    if(contour_goal_valid && out->valid) {
        out->geometry.valid=1;out->geometry.target_right_mm=contour_goal_x;
        out->geometry.target_forward_mm=contour_goal_y;
    }
    out->geometry.contour_points=(uint16_t)contour_points;
    out->geometry.model_kind=(uint8_t)contour_model_valid;
    out->geometry.fit_rms_mm=contour_model_valid?contour_model_error:0;
    out->geometry.visible_samples=(uint16_t)geometry_visible;
    out->geometry.inferred_samples=(uint16_t)geometry_inferred;
    out->geometry.reject_reason=geometry_reject;
    /* 没有已核验的几何轨迹时，像素补线也必须在实际前向控制距离内连续。
     * 排除远端不可见前缀；三行连续缺路不能被其他白区的总分抵消。
     * 已确认斑马线是可通行标记，单独保留其原有处理。 */
    if(out->valid && !out->geometry.valid) {
        unsigned started=0,gap=0;
        for(row=out->preview_first_row;row<=VISION_BOTTOM_ROW;++row) {
            int col=out->center[row],white=0,dx,dy;
            float width=VISION_WIDTH_TOP_PX+(row-VISION_TOP_ROW)*geometry_row_slope;
            float ahead=geometry_ground_a/width+geometry_ground_b;
            if(ahead>VISION_GEOMETRY_LOOKAHEAD_MM+VEHICLE_WHEELBASE_MM)continue;
            for(dx=-1;dx<=1;++dx)for(dy=-1;dy<=1;++dy)
                if(vision_white(gray,row+dy,vision_clip(col+dx,0,187),out->threshold))++white;
            if(white>=5 || (out->row_flags[row]&TRACK_ROW_ZEBRA)){started=1;gap=0;}
            else if(started && ++gap>=3) {out->valid=0;out->geometry.reject_reason=3;break;}
        }
    }
    if(vision.phase==TRACK_ELEMENT_ROUNDABOUT_ENTER) {
        vision.contour_enter_frames=vision_increment(vision.contour_enter_frames,
            out->geometry.valid && (vision.side==TRACK_SIDE_LEFT?contour_goal_x<0:contour_goal_x>0) &&
            fabsf(atanf(2*VEHICLE_WHEELBASE_MM*contour_goal_x/(contour_goal_x*contour_goal_x+contour_goal_y*contour_goal_y)))>=0.11435397f);
        if(distance_mm-vision.phase_mm>=VISION_RING_ENTER_MIN_MM && vision.contour_enter_frames>=3) {
            vision.curve_seen=1;vision.exit_seen=0;vision.exit_near_seen=0;vision.ring_path_valid=0;vision.ring_path_seen=0;
            vision_change(TRACK_ELEMENT_ROUNDABOUT_INSIDE,now_ms,distance_mm);out->element=vision.phase;
        }
    } else vision.contour_enter_frames=0;
    /* 出口分叉来自原始图像，可靠性采用本帧最终路径；低原始双边覆盖
     * 不应否定已经过密集轮迹核验的轮廓。三帧、最短里程和近端开口仍必需。 */
    if(vision.phase==TRACK_ELEMENT_ROUNDABOUT_INSIDE && !vision.exit_seen) {
        vision.exit_candidate=vision_increment(vision.exit_candidate,vision.ring_path_seen &&
            vision_current_exit_cue && out->valid && !out->fault && distance_mm-vision.phase_mm>=VISION_RING_INSIDE_MIN_MM);
        if(vision.exit_candidate>=VISION_RING_CONFIRM_FRAMES) {
            vision.exit_seen=1;vision.exit_seen_ms=now_ms;vision.exit_seen_mm=distance_mm;
            if(vision.side==TRACK_SIDE_LEFT?features.right_open[VISION_RING_EXIT_NEAR_ROW]:features.left_open[VISION_RING_EXIT_NEAR_ROW])vision.exit_near_seen=1;
        }
    }
    if(out->fault&TRACK_FAULT_ELEMENT_TIMEOUT) out->valid=0;
    if(!out->valid) {out->fault|=TRACK_FAULT_PATH_LOST;out->geometry.valid=0;out->confidence=0;}
    vision_curve_outer_support(out);
}
