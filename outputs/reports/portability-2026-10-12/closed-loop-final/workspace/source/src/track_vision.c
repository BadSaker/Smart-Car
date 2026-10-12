/* 本模块自主实现，不含新复制的第三方源码。
 * 阈值方法依据Otsu类间方差思想：doi:10.1109/TSMC.1979.4310076。
 * 公开智能车项目仅用于比较扫描线、八邻域和元素分阶段处理的取舍。
 * 环岛拓扑、透视宽度、曝光和每帧耗时仍须用本车录像/实测验收。 */
#include "track_vision.h"
#include "vision_config.h"
#include <string.h>

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
    uint8_t curve_seen;
    uint8_t cross_near_seen;
    uint8_t exit_seen;
    uint8_t exit_candidate;
    uint8_t ring_path_valid,ring_path[120];
    uint32_t exit_seen_ms,exit_seen_mm;
    uint8_t fault;
    uint8_t bridge_active,bridge_recovery;
    uint32_t bridge_ms,bridge_mm;
    int16_t incoming_near;
    int16_t incoming_far;
} vision_state_t;

typedef struct {
    uint8_t left_open[TRACK_VISION_HEIGHT];
    uint8_t right_open[TRACK_VISION_HEIGHT];
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
    uint32_t total=0, sum=0, low_count=0, low_sum=0;
    uint32_t best_count=0, best_sum=0;
    float best_score=-1.0f;
    int row,col,index;
    uint8_t lowest=255,highest=0;
    for (row=VISION_TOP_ROW; row<=VISION_BOTTOM_ROW; row+=2)
        for(col=2;col<186;col+=2) {
            uint8_t value=gray[row*188+col];
            ++bins[value>>3]; ++total; sum+=value>>3;
            if(value<lowest) lowest=value;
            if(value>highest) highest=value;
        }
    *contrast=(uint8_t)(highest-lowest);
    for(index=0;index<31;++index) {
        float separation,score;
        low_count+=bins[index]; low_sum+=(uint32_t)index*bins[index];
        if(low_count==0 || low_count==total) continue;
        separation=(float)low_sum/(float)low_count-
            (float)(sum-low_sum)/(float)(total-low_count);
        score=(float)low_count*(float)(total-low_count)*separation*separation;
        if(score>best_score) { best_score=score; best_count=low_count; best_sum=low_sum; }
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
    vision.phase=phase;vision.phase_ms=now_ms;vision.phase_mm=distance_mm;
    vision.recovery=0;vision.approach_clear=0;
    vision.cross_near_seen=0;
    vision.exit_candidate=0;
    if(phase!=TRACK_ELEMENT_ROUNDABOUT_INSIDE) {
        vision.exit_seen=0;vision.ring_path_valid=0;
    }
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

static void vision_elements(track_vision_result_t *out,const vision_features_t *features,
                            uint32_t now_ms,uint32_t distance_mm)
{
    uint32_t traveled=distance_mm-vision.phase_mm;
    int curved=vision_abs(out->far_error_px-out->near_error_px)>=VISION_CURVE_ERROR_PX;
    int normal=features->paired_rows>=(VISION_BOTTOM_ROW-VISION_TOP_ROW)*3/4 && !curved && out->valid;
    int left=features->left_gap && !features->right_gap && features->left_supported;
    int right=features->right_gap && !features->left_gap && features->right_supported;
    int cross=features->left_gap && features->right_gap;
    int incoming_straight=vision_abs(out->far_error_px-out->near_error_px)<=VISION_RING_APPROACH_MAX_CURVE_PX &&
        vision_abs(out->near_error_px)<=VISION_RING_APPROACH_MAX_NEAR_PX;
    unsigned joint_run=0,joint_best=0,near_open=0,near_paired=0;
    int row;
    for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
        joint_run=features->left_open[row] && features->right_open[row]?joint_run+1U:0U;
        if(joint_run>joint_best)joint_best=joint_run;
        if(row>=VISION_NEAR_FIRST_ROW && row<=VISION_NEAR_LAST_ROW) {
            if(features->left_open[row] && features->right_open[row])++near_open;
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
        if(traveled>VISION_CROSS_MAX_MM || now_ms-vision.phase_ms>VISION_CROSS_TIMEOUT_MS)
            vision.fault|=TRACK_FAULT_ELEMENT_TIMEOUT;
        if(near_open>=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)/2)vision.cross_near_seen=1;
        /* 双侧开口已到达近场，随后近场双边重新闭合，即已越过十字。
         * 出口的远场可以紧接弯道，不能要求整幅图恢复直道。 */
        vision.recovery=vision_increment(vision.recovery,(normal && !cross) ||
            (vision.cross_near_seen && out->valid && near_open==0 &&
             near_paired*100U>=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)*VISION_CURVE_OUTER_NEAR_MIN_PERCENT));
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
        int same_fork=vision.side==TRACK_SIDE_LEFT?
            (features->left_fork && features->left_island):(features->right_fork && features->right_island);
        uint8_t bottom=vision.side==TRACK_SIDE_LEFT?features->left_bottom:features->right_bottom;
        if(vision.phase!=TRACK_ELEMENT_ROUNDABOUT_APPROACH &&
           (traveled>VISION_RING_PHASE_MAX_MM || now_ms-vision.phase_ms>VISION_RING_PHASE_TIMEOUT_MS))
            vision.fault|=TRACK_FAULT_ELEMENT_TIMEOUT;
        switch(vision.phase) {
        case TRACK_ELEMENT_ROUNDABOUT_APPROACH:
            /* 限界先于确认提交；历史分叉不能授权当前不可见的入口。 */
            if(traveled>VISION_RING_APPROACH_MAX_MM || now_ms-vision.phase_ms>VISION_RING_APPROACH_MAX_MS) {
                vision.side=TRACK_SIDE_NONE;
                vision_change(TRACK_ELEMENT_STRAIGHT,now_ms,distance_mm);
                vision_clear_candidates();break;
            }
            vision.recovery=vision_increment(vision.recovery,same_gap && same_fork && out->valid && bottom>=VISION_RING_ENTRY_ROW);
            vision.approach_clear=vision_increment(vision.approach_clear,out->valid && (!same_gap || !same_fork || cross));
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
                vision.curve_seen=1;vision.exit_seen=0;vision.ring_path_valid=0;vision_change(TRACK_ELEMENT_ROUNDABOUT_INSIDE,now_ms,distance_mm);
            }
            break;
        case TRACK_ELEMENT_ROUNDABOUT_INSIDE:
            /* 完成入环后，必须看到新的外侧分叉，单独直道恢复不能出环。 */
            /* 出口预告和相对图像路径共用一份总寿命，不能被中间几帧
             * 高Q重置。没有当前近场开口时，旧预告不能无限授权出环。 */
            if(vision.exit_seen && (distance_mm-vision.exit_seen_mm>VISION_RING_EXIT_MEMORY_MAX_MM ||
                                   now_ms-vision.exit_seen_ms>VISION_RING_EXIT_MEMORY_MAX_MS)) {
                vision.exit_seen=0;vision.ring_path_valid=0;vision.recovery=0;vision.exit_candidate=0;
            } else if(!vision.exit_seen) {
                vision.exit_candidate=vision_increment(vision.exit_candidate,vision.ring_path_valid &&
                    (outer_gap || outer_widening) && out->valid && traveled>=VISION_RING_INSIDE_MIN_MM);
                if(vision.exit_candidate>=VISION_RING_CONFIRM_FRAMES) {
                    vision.exit_seen=1;vision.exit_seen_ms=now_ms;vision.exit_seen_mm=distance_mm;
                }
            }
            vision.recovery=vision_increment(vision.recovery,vision.exit_seen &&
                (vision.side==TRACK_SIDE_LEFT?features->right_open[VISION_RING_EXIT_NEAR_ROW]:features->left_open[VISION_RING_EXIT_NEAR_ROW]) &&
                out->near_path_supported && features->measured_rows>=VISION_MIN_MEASURED_ROWS);
            if(vision.recovery>=VISION_RING_CONFIRM_FRAMES)
                vision_change(TRACK_ELEMENT_ROUNDABOUT_EXIT,now_ms,distance_mm);
            break;
        case TRACK_ELEMENT_ROUNDABOUT_EXIT:
            vision.recovery=vision_increment(vision.recovery,normal && !left && !right);
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
        vision.candidate_left=vision_increment(vision.candidate_left,left && features->left_fork && features->left_island && incoming_straight && out->valid && !out->zebra && !vision.cooldown);
        vision.candidate_right=vision_increment(vision.candidate_right,right && features->right_fork && features->right_island && incoming_straight && out->valid && !out->zebra && !vision.cooldown);
        if(vision.candidate_cross>=VISION_CROSS_CONFIRM_FRAMES) {
            vision.incoming_near=out->near_error_px;vision.incoming_far=out->far_error_px;
            (void)vision_cross_direction(out,0);
            vision_change(TRACK_ELEMENT_CROSS,now_ms,distance_mm);vision_clear_candidates();
        } else if(vision.candidate_left>=VISION_RING_CONFIRM_FRAMES ||
                  vision.candidate_right>=VISION_RING_CONFIRM_FRAMES) {
            vision.side=vision.candidate_left>=VISION_RING_CONFIRM_FRAMES?TRACK_SIDE_LEFT:TRACK_SIDE_RIGHT;
            vision.incoming_near=out->near_error_px;vision.incoming_far=out->far_error_px;
            vision_change(TRACK_ELEMENT_ROUNDABOUT_APPROACH,now_ms,distance_mm);
            vision_clear_candidates();
        }
    }
    out->element=vision.phase;
    out->side=vision.side;
    if(vision.phase==TRACK_ELEMENT_STRAIGHT) out->element=curved?TRACK_ELEMENT_CURVE:TRACK_ELEMENT_STRAIGHT;
    out->fault|=vision.fault;
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

static void vision_element_path(const uint8_t *gray,track_vision_result_t *out,const vision_features_t *features)
{
    int row;
    if(out->element==TRACK_ELEMENT_CROSS) {
        if(!vision_cross_far_supported(out,features)) out->valid=0;
        vision_cross_update_direction(gray,out);
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
            if((!selected || (out->row_flags[row]&(TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED))!=
                    (TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED)) &&
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
            memcpy(vision.ring_path,out->center,TRACK_VISION_HEIGHT);vision.ring_path_valid=1;
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
                out->center[row]=(uint8_t)vision_clip(out->center[row]+direction*bias,2,185);
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
static void vision_element_bridge(const uint8_t *gray,track_vision_result_t *out,
                                  uint32_t now_ms,uint32_t distance_mm)
{
    uint8_t white[TRACK_VISION_HEIGHT]={0};
    unsigned near_white=0,far_white=0,near_edges=0,far_edges=0,anchors=0,score,support;
    unsigned outer_near=0,outer_far=0,far_white_percent=VISION_ELEMENT_WHITE_PERCENT;
    uint8_t outer_mask=out->curve_outer_side==TRACK_SIDE_LEFT?TRACK_ROW_LEFT_MEASURED:TRACK_ROW_RIGHT_MEASURED;
    const unsigned near_count=VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1;
    const unsigned far_count=VISION_FAR_LAST_ROW-VISION_FAR_FIRST_ROW+1;
    const unsigned far_evidence_count=VISION_FAR_LAST_ROW-VISION_TOP_ROW+1;
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
            }
            if(evidence)++anchors;
            if(row>=VISION_NEAR_FIRST_ROW && row<=VISION_NEAR_LAST_ROW){++near_white;near_edges+=evidence;}
            if(row>=VISION_FAR_FIRST_ROW && row<=VISION_FAR_LAST_ROW)++far_white;
            if(row<=VISION_FAR_LAST_ROW)far_edges+=evidence;
            if(out->curve_outer_side!=TRACK_SIDE_NONE && (out->row_flags[row]&outer_mask) &&
               !(out->row_flags[row]&TRACK_ROW_ZEBRA)) {
                if(row>=VISION_NEAR_FIRST_ROW && row<=VISION_NEAR_LAST_ROW)++outer_near;
                if(row>=VISION_FAR_FIRST_ROW && row<=VISION_FAR_LAST_ROW)++outer_far;
            }
        } else out->row_flags[row]&=(uint8_t)~TRACK_ROW_PATH_VALID;
    }
    /* 环内经连续性验证的实测外边沿用原弯道覆盖要求；白区补线不能
     * 反过来否定已有效的单边弯道，也不能为无实测支持的黑区放行。 */
    if(out->element==TRACK_ELEMENT_ROUNDABOUT_INSIDE && out->valid &&
       outer_near*100U>=near_count*VISION_CURVE_OUTER_NEAR_MIN_PERCENT &&
       outer_far*100U>=far_count*VISION_CURVE_OUTER_FAR_MIN_PERCENT)
        far_white_percent=VISION_CURVE_OUTER_FAR_MIN_PERCENT;
    if(out->element==TRACK_ELEMENT_ROUNDABOUT_INSIDE && vision.exit_seen && vision.ring_path_valid)
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
    if(out->element==TRACK_ELEMENT_ROUNDABOUT_INSIDE && vision.exit_seen && vision.ring_path_valid) {
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
            if(row>=VISION_FAR_FIRST_ROW && row<=VISION_FAR_LAST_ROW) ++far_rows;
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

void track_vision_process(const uint8_t *gray, uint32_t now_ms,
                          uint32_t distance_mm, track_vision_result_t *out)
{
    vision_features_t features;
    uint8_t contrast;
    int row;
    if(out==0) return;
    memset(out,0,sizeof(*out));out->zebra_near_row=255;
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
    vision_element_path(gray,out,&features);
    vision_element_bridge(gray,out,now_ms,distance_mm);
    out->near_error_px=vision_error(out,VISION_NEAR_FIRST_ROW,VISION_NEAR_LAST_ROW);
    out->far_error_px=vision_error(out,VISION_FAR_FIRST_ROW,VISION_FAR_LAST_ROW);
    if(out->fault&TRACK_FAULT_ELEMENT_TIMEOUT) out->valid=0;
    if(!out->valid) out->fault|=TRACK_FAULT_PATH_LOST;
    vision_curve_outer_support(out);
}
