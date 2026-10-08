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
    uint8_t recovery;
    uint8_t approach_clear;
    uint8_t curve_seen;
    uint8_t fault;
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
} vision_features_t;

static vision_state_t vision;

static int vision_abs(int value) { return value < 0 ? -value : value; }

static int vision_clip(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

static int vision_width(int row)
{
    return VISION_WIDTH_TOP_PX + (row-VISION_TOP_ROW) *
        (VISION_WIDTH_BOTTOM_PX-VISION_WIDTH_TOP_PX) /
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

static void vision_scan(const uint8_t *gray,track_vision_result_t *out,vision_features_t *features)
{
    int row,predicted=VISION_CENTER_COL;
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
                for(col=seed;col>=2;--col)
                    if(!vision_white(gray,row,col-1,out->threshold) &&
                       !vision_white(gray,row,col-2,out->threshold)) { left=col;has_left=1;break; }
                for(col=seed;col<=185;++col)
                    if(!vision_white(gray,row,col+1,out->threshold) &&
                       !vision_white(gray,row,col+2,out->threshold)) { right=col;has_right=1;break; }
                /* 开口必须向道路外侧增宽；内侧黑块或急弯不作为开口。 */
                features->left_open[row]=(uint8_t)(expected_left>4 &&
                    (!has_left || left<expected_left-VISION_EDGE_WINDOW_PX));
                features->right_open[row]=(uint8_t)(expected_right<183 &&
                    (!has_right || right>expected_right+VISION_EDGE_WINDOW_PX));
                if(features->left_open[row]) has_left=0;
                if(features->right_open[row]) has_right=0;
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

static void vision_change(track_element_t phase,uint32_t now_ms,uint32_t distance_mm)
{
    vision.phase=phase;vision.phase_ms=now_ms;vision.phase_mm=distance_mm;
    vision.recovery=0;vision.approach_clear=0;
}

static void vision_clear_candidates(void)
{
    vision.candidate_left=0;vision.candidate_right=0;vision.candidate_cross=0;
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
    if(now_ms-vision.last_frame_ms>VISION_CANDIDATE_MAX_GAP_MS) {
        vision_clear_candidates();vision.recovery=0;
    }
    vision.last_frame_ms=now_ms;
    if(vision.phase==TRACK_ELEMENT_CROSS) {
        if(traveled>VISION_CROSS_MAX_MM || now_ms-vision.phase_ms>VISION_CROSS_TIMEOUT_MS)
            vision.fault|=TRACK_FAULT_ELEMENT_TIMEOUT;
        vision.recovery=vision_increment(vision.recovery,normal && !cross);
        if(vision.recovery>=VISION_RECOVERY_FRAMES) vision_change(TRACK_ELEMENT_STRAIGHT,now_ms,distance_mm);
    } else if(vision.phase>=TRACK_ELEMENT_ROUNDABOUT_APPROACH) {
        int same_gap=vision.side==TRACK_SIDE_LEFT?left:right;
        int outer_gap=vision.side==TRACK_SIDE_LEFT?right:left;
        uint8_t bottom=vision.side==TRACK_SIDE_LEFT?features->left_bottom:features->right_bottom;
        if(traveled>VISION_RING_PHASE_MAX_MM || now_ms-vision.phase_ms>VISION_RING_PHASE_TIMEOUT_MS)
            vision.fault|=TRACK_FAULT_ELEMENT_TIMEOUT;
        switch(vision.phase) {
        case TRACK_ELEMENT_ROUNDABOUT_APPROACH:
            vision.recovery=vision_increment(vision.recovery,same_gap && out->valid && bottom>=VISION_RING_ENTRY_ROW);
            vision.approach_clear=vision_increment(vision.approach_clear,normal && !same_gap);
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
                (vision.side==TRACK_SIDE_LEFT ? out->far_error_px-out->near_error_px<=-VISION_CURVE_ERROR_PX :
                 out->far_error_px-out->near_error_px>=VISION_CURVE_ERROR_PX));
            if(traveled>=VISION_RING_ENTER_MIN_MM && vision.recovery>=VISION_RING_CONFIRM_FRAMES) {
                vision.curve_seen=1;vision_change(TRACK_ELEMENT_ROUNDABOUT_INSIDE,now_ms,distance_mm);
            }
            break;
        case TRACK_ELEMENT_ROUNDABOUT_INSIDE:
            /* 完成入环后，必须看到新的外侧分叉，单独直道恢复不能出环。 */
            vision.recovery=vision_increment(vision.recovery,outer_gap && out->valid && vision.curve_seen &&
                traveled>=VISION_RING_INSIDE_MIN_MM);
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
        vision.candidate_left=vision_increment(vision.candidate_left,left && out->valid && !out->zebra && !vision.cooldown);
        vision.candidate_right=vision_increment(vision.candidate_right,right && out->valid && !out->zebra && !vision.cooldown);
        if(vision.candidate_cross>=VISION_CROSS_CONFIRM_FRAMES) {
            vision.incoming_near=out->near_error_px;vision.incoming_far=out->far_error_px;
            vision_change(TRACK_ELEMENT_CROSS,now_ms,distance_mm);vision_clear_candidates();
        } else if(vision.candidate_left>=VISION_RING_CONFIRM_FRAMES ||
                  vision.candidate_right>=VISION_RING_CONFIRM_FRAMES) {
            vision.side=vision.candidate_left>=VISION_RING_CONFIRM_FRAMES?TRACK_SIDE_LEFT:TRACK_SIDE_RIGHT;
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

static void vision_element_path(track_vision_result_t *out,const vision_features_t *features)
{
    int row;
    if(out->element==TRACK_ELEMENT_CROSS) {
        if(!vision_cross_far_supported(out,features)) out->valid=0;
        for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row)
            if((out->row_flags[row]&(TRACK_ROW_LEFT_MEASURED|TRACK_ROW_RIGHT_MEASURED))==0) {
                if(features->left_open[row] && features->right_open[row]) {
                    int error=vision.incoming_near + (VISION_NEAR_FIRST_ROW-row)*
                        (vision.incoming_far-vision.incoming_near)/(VISION_NEAR_FIRST_ROW-VISION_FAR_FIRST_ROW);
                    out->center[row]=(uint8_t)vision_clip(VISION_CENTER_COL+error,2,185);
                    out->row_flags[row]|=TRACK_ROW_PATH_VALID|TRACK_ROW_RECONSTRUCTED;
                } else if(!(out->row_flags[row]&TRACK_ROW_ZEBRA)) {
                    out->row_flags[row]&=(uint8_t)~TRACK_ROW_PATH_VALID;
                }
            }
    } else if(out->element==TRACK_ELEMENT_ROUNDABOUT_ENTER) {
        int direction=out->side==TRACK_SIDE_LEFT?-1:1;
        for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
            int bias=VISION_RING_ENTRY_BIAS_PX*(VISION_BOTTOM_ROW-row)/(VISION_BOTTOM_ROW-VISION_FAR_FIRST_ROW);
            out->center[row]=(uint8_t)vision_clip(out->center[row]+direction*bias,2,185);
            out->row_flags[row]|=TRACK_ROW_RECONSTRUCTED;
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
            if(selected) {
                out->center[row]=(uint8_t)vision_clip(center,2,185);
                out->row_flags[row]|=TRACK_ROW_RECONSTRUCTED;
            }
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

static int vision_cross_near_open(const vision_features_t *features)
{
    unsigned opened=0;
    int row;
    if(vision.phase!=TRACK_ELEMENT_CROSS) return 0;
    for(row=VISION_NEAR_FIRST_ROW;row<=VISION_NEAR_LAST_ROW;++row)
        if(features->left_open[row] && features->right_open[row]) ++opened;
    return opened>=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)/2;
}

void track_vision_reset(void)
{
    memset(&vision,0,sizeof(vision));
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
    if(gray==0) { out->fault=TRACK_FAULT_PATH_LOST|vision.fault;return; }
    out->threshold=vision_threshold(gray,&contrast);
    if(contrast<VISION_MIN_CONTRAST) {
        out->fault=TRACK_FAULT_PATH_LOST|vision.fault;
        /* 黑白板无几何证据，不推进元素，但仍执行时间故障检查。 */
        memset(&features,0,sizeof(features));
        vision_elements(out,&features,now_ms,distance_mm);return;
    }
    vision_scan(gray,out,&features);
    features.left_gap=vision_gap(features.left_open,out,TRACK_ROW_LEFT_MEASURED,&features.left_bottom);
    features.right_gap=vision_gap(features.right_open,out,TRACK_ROW_RIGHT_MEASURED,&features.right_bottom);
    features.left_supported=vision_opposite_continuous(features.left_open,out,TRACK_SIDE_LEFT);
    features.right_supported=vision_opposite_continuous(features.right_open,out,TRACK_SIDE_RIGHT);
    out->confidence=(uint8_t)((features.measured_rows+features.paired_rows)*50U/
        (VISION_BOTTOM_ROW-VISION_TOP_ROW+1));
    out->valid=(uint8_t)(features.measured_rows>=VISION_MIN_MEASURED_ROWS &&
        out->confidence>=VISION_MIN_CONFIDENCE &&
        (vision_near_supported(out) || vision_cross_near_open(&features)));
    out->near_error_px=vision_error(out,VISION_NEAR_FIRST_ROW,VISION_NEAR_LAST_ROW);
    out->far_error_px=vision_error(out,VISION_FAR_FIRST_ROW,VISION_FAR_LAST_ROW);
    vision_elements(out,&features,now_ms,distance_mm);
    vision_element_path(out,&features);
    out->near_error_px=vision_error(out,VISION_NEAR_FIRST_ROW,VISION_NEAR_LAST_ROW);
    out->far_error_px=vision_error(out,VISION_FAR_FIRST_ROW,VISION_FAR_LAST_ROW);
    if(out->fault&TRACK_FAULT_ELEMENT_TIMEOUT) out->valid=0;
    if(!out->valid) out->fault|=TRACK_FAULT_PATH_LOST;
}
