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
    uint8_t contour_enter_frames;
    uint8_t cross_near_seen;
    uint8_t cross_provisional;
    uint32_t cross_near_mm;
    uint8_t exit_seen,exit_near_seen;
    uint8_t exit_candidate;
    uint8_t ring_path_valid,ring_path_seen,ring_path[120];
    uint32_t ring_path_saved_ms,ring_path_saved_mm;
    uint32_t exit_seen_ms,exit_seen_mm;
    uint8_t incoming_valid;
    uint32_t incoming_ms;
    int16_t incoming_near;
    int16_t incoming_far;
    uint8_t zebra_seen;
    uint32_t zebra_ms,zebra_mm;
    uint32_t island_left_ms,island_right_ms,island_left_mm,island_right_mm;
    uint8_t island_left_seen,island_right_seen;
    uint8_t blob_side,blob_bottom;
    uint8_t blob_edge_x,blob_edge_y;
    uint32_t blob_ms,blob_mm;
    int16_t zebra_center,last_near,last_far;
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
    uint8_t left_blob,right_blob;
    uint8_t left_blob_bottom,right_blob_bottom;
} vision_features_t;

static vision_state_t vision;
static uint8_t island_seed_side,island_seed_x,island_seed_y;
static unsigned vision_outer_near_percent(void){return VISION_CURVE_OUTER_NEAR_MIN_PERCENT;}

static unsigned vision_ring_frames(void){return VISION_RING_CONFIRM_FRAMES;}
static int vision_island_span(void){return VISION_RING_ISLAND_MIN_SPAN;}
static int vision_island_convex(void){return VISION_RING_ISLAND_CONVEX_PX;}
static int vision_island_rows(void){return VISION_RING_ISLAND_CONVEX_ROWS;}
static int vision_zebra_edge_rows(void){return VISION_ZEBRA_EDGE_ROWS;}
static int vision_obstacle_min_rows(void){return VISION_OBSTACLE_MIN_ROWS;}
/* 仅当前帧的区域；历史只用于有当前条纹证据的底部裁切，不重放旧黑区。 */
static uint8_t zebra_region[TRACK_VISION_HEIGHT];
static int zebra_region_first,zebra_region_last;
static uint8_t zebra_single_allowed;

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
static int vision_zebra_row_level(const uint8_t *gray,int row,int center,uint8_t threshold,unsigned minimum_transitions)
{
    int half=vision_width(row)/2-5;
    int left=vision_clip(center-half,2,185),right=vision_clip(center+half,2,185);
    /* 负中心只供区外锚点检查：整行重复条纹不能充当正常道路。 */
    if(center<0){left=2;right=185;}
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
    return transitions>=minimum_transitions &&
        black*5U>(unsigned)(right-left+1) &&
        black*5U<(unsigned)(right-left+1)*4U;
}
static int vision_zebra_row(const uint8_t *gray,int row,int center,uint8_t threshold)
{
    return vision_zebra_row_level(gray,row,center,threshold,VISION_ZEBRA_MIN_TRANSITIONS);
}

static int vision_zebra_edges(const uint8_t *gray,int row,int predicted,uint8_t threshold,int *left,int *right);
static void vision_zebra_region(const uint8_t *gray,track_vision_result_t *out,
                                uint32_t now_ms,uint32_t distance_mm)
{
    int row,first=-1,best_first=-1,best_last=-1,count=0,best=0,preview=0;
    int center=vision.zebra_center?vision.zebra_center:VISION_CENTER_COL;
    zebra_single_allowed=0;
    memset(zebra_region,0,sizeof zebra_region);
    zebra_region_first=120;zebra_region_last=-1;
    out->zebra_far_row=255;
    for(row=VISION_ZEBRA_FIRST_ROW;row<=VISION_ZEBRA_LAST_ROW+1;++row) {
        int match=row<=VISION_ZEBRA_LAST_ROW &&
            (vision_zebra_row(gray,row,VISION_CENTER_COL,out->threshold) ||
             (vision_zebra_row(gray,row,center,out->threshold)));
        int weak=row<=VISION_ZEBRA_LAST_ROW &&
            vision_zebra_row_level(gray,row,center,out->threshold,VISION_ZEBRA_PREVIEW_TRANSITIONS);
        preview=weak?preview+1:0;
        if(preview>=(int)VISION_ZEBRA_PREVIEW_ROWS)out->zebra_candidate=1;
        if(match){if(!count)first=row;++count;}
        else {if(count>best){best=count;best_first=first;best_last=row-1;}count=0;}
    }
    if(best<(int)VISION_ZEBRA_MIN_ROWS) {
        /* 末端只剩1..2行时须有刚刚确认过的标记，且当前条纹确实贴着图像底部。 */
        if(!(best>0 && best_last==VISION_ZEBRA_LAST_ROW && vision.zebra_seen &&
             now_ms-vision.zebra_ms<=VISION_ZEBRA_CLIPPED_MAX_MS &&
             distance_mm-vision.zebra_mm<=VISION_ZEBRA_CLIPPED_MAX_MM)) {
            return;
        }
        out->zebra_clipped=1;
    } else {vision.zebra_seen=1;vision.zebra_ms=now_ms;vision.zebra_mm=distance_mm;}
    out->zebra=1;out->zebra_rows=(uint8_t)best;
    out->zebra_far_row=(uint8_t)best_first;out->zebra_near_row=(uint8_t)best_last;
    zebra_region_first=vision_clip(best_first-vision_zebra_edge_rows(),VISION_TOP_ROW,117);
    zebra_region_last=vision_clip(best_last+vision_zebra_edge_rows(),VISION_TOP_ROW,117);
    for(row=zebra_region_first;row<=zebra_region_last;++row)zebra_region[row]=1;
    {
        unsigned anchors=0;uint8_t anchored=0;
        zebra_single_allowed=1;
        /* 一侧外沿丢失时，当前帧条纹区外还须有连续真实外沿道路锚点。
         * 没有区外锚点的满屏条码不能借单边重建变成道路。 */
        for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
            int l=0,r=187;
            if((row<best_first-2 || row>best_last+2) &&
               !vision_zebra_row_level(gray,row,-1,out->threshold,VISION_ZEBRA_PREVIEW_TRANSITIONS) &&
               vision_white(gray,row,center,out->threshold) &&
               vision_zebra_edges(gray,row,center,out->threshold,&l,&r)!=0)++anchors;
            else anchors=0;
            if(anchors>=3){anchored=1;break;}
        }
        zebra_single_allowed=anchored;
    }
}

/* 边线外可由黑边过渡到较亮背景，但不应继续出现多组条纹。
 * 稳定色段至少2px，避免孤立噪点把单一背景误判成条码。 */
static int vision_zebra_outside(const uint8_t *gray,int row,int edge,int direction,uint8_t threshold)
{
    int col,last=0,stable=0;
    unsigned run=0,transitions=0,samples=0;
    for(col=edge+direction;col>=0 && col<188;col+=direction) {
        int white=vision_white(gray,row,col,threshold);
        ++samples;
        if(white==last)++run;else {last=white;run=1;}
        if(run==VISION_ZEBRA_MIN_RUN_PX && white!=stable){++transitions;stable=white;}
        if(transitions>(zebra_single_allowed?2U:VISION_ZEBRA_OUTSIDE_MAX_TRANSITIONS))return 0;
    }
    return samples>=4;
}

/* 从预测车道外侧向内找白区外沿，避免把内部白缝当作整条道路。
 * 双边须符合透视宽度，单边只用于另一边已出画；外部背景不能继续呈条纹。 */
static int vision_zebra_edges(const uint8_t *gray,int row,int predicted,uint8_t threshold,int *left,int *right)
{
    int half=vision_width(row)/2,col,l=-1,r=-1;
    int margin=VISION_ZEBRA_EDGE_SEARCH_PX;
    for(col=vision_clip(predicted-half-margin,2,185);col<=vision_clip(predicted-half+margin,2,185);++col)
        if(!vision_white(gray,row,col-1,threshold) && !vision_white(gray,row,col-2,threshold) &&
            vision_white(gray,row,col,threshold) && vision_white(gray,row,col+1,threshold)){
            if(vision_zebra_outside(gray,row,col,-1,threshold)){l=col;break;}
        }
    for(col=vision_clip(predicted+half+margin,2,185);col>=vision_clip(predicted+half-margin,2,185);--col)
        if(!vision_white(gray,row,col+1,threshold) && !vision_white(gray,row,col+2,threshold) &&
            vision_white(gray,row,col,threshold) && vision_white(gray,row,col-1,threshold)){
            if(vision_zebra_outside(gray,row,col,1,threshold)){r=col;break;}
        }
    if(l<0 && r<0)return 0;
    if(l>=0 && r>=0) {
        if((r-l)*100<vision_width(row)*VISION_ZEBRA_WIDTH_MIN_PERCENT ||
           (r-l)*100>vision_width(row)*VISION_ZEBRA_WIDTH_MAX_PERCENT ||
           vision_abs((l+r)/2-predicted)>VISION_ZEBRA_EDGE_SEARCH_PX)return 0;
        *left=l;*right=r;return 3;
    }
    /* 单边仅在另一边按透视预测确已出画时使用，内侧条纹不能代替丢失边界。 */
    if(l>=0 && l+2*half>=185){*left=l;*right=vision_clip(l+2*half,0,187);return 1;}
    if(r>=0 && r-2*half<=2){*right=r;*left=vision_clip(r-2*half,0,187);return 2;}
    /* 宽松模式允许已确认条纹区以一条真实外沿约束道路；仍验证该
     * 边外不继续重复条纹，以及按标定宽度恢复的中心接近来向预测。 */
    if(zebra_single_allowed) {
        if(l>=0 && vision_abs(l+half-predicted)<=margin) {
            *left=l;*right=vision_clip(l+2*half,0,187);return 1;
        }
        if(r>=0 && vision_abs(r-half-predicted)<=margin) {
            *right=r;*left=vision_clip(r-2*half,0,187);return 2;
        }
    }
    return 0;
}

static int vision_zebra_pixel(const track_vision_result_t *out,int row,int col)
{
    int source=vision_clip(row,VISION_TOP_ROW,VISION_BOTTOM_ROW);
    return row>=zebra_region_first && row<=zebra_region_last &&
        (out->row_flags[source]&TRACK_ROW_ZEBRA) &&
        (out->row_flags[source]&TRACK_ROW_PATH_VALID) &&
        col>out->left[source]+2 && col<out->right[source]-2;
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
        if(zebra_region[row]) {
            int edges;
            flags=TRACK_ROW_ZEBRA|TRACK_ROW_RECONSTRUCTED|TRACK_ROW_PATH_VALID;
            left=vision_clip(expected_left,0,187); right=vision_clip(expected_right,0,187);
            edges=vision_zebra_edges(gray,row,predicted,out->threshold,&left,&right);
            if(edges) {
                flags|=(uint8_t)edges;
                features->raw_mask[row]=(uint8_t)edges;features->raw_left[row]=(uint8_t)left;features->raw_right[row]=(uint8_t)right;
                ++features->measured_rows;if(edges==3)++features->paired_rows;unsupported=0;
                center=edges==3?(left+right)/2:edges==1?left+half:right-half;
            } else {
                ++unsupported;
                if(unsupported>VISION_MAX_UNSUPPORTED_ROWS)flags&=(uint8_t)~TRACK_ROW_PATH_VALID;
            }
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
    unsigned required=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)*vision_outer_near_percent();
    /* 候选尚未确认时仍需正常过弯；元素分类使用第一遍原始证据。 */
    if(VISION_CURVE_OUTER_ENABLED &&
       (vision.phase<=TRACK_ELEMENT_CURVE || vision.phase==TRACK_ELEMENT_ROUNDABOUT_APPROACH ||
        vision.phase==TRACK_ELEMENT_ROUNDABOUT_INSIDE)) {
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
/* 三个新处理帧中的投票允许一帧证据缺口；时距上界由入口记忆约束。 */
static uint8_t vision_ring_vote(uint8_t history,int condition)
{
    return (uint8_t)(((history<<1)|(condition?1U:0U))&7U);
}
static int vision_ring_confirmed(uint8_t history)
{
    return (history&1U)+((history>>1)&1U)+((history>>2)&1U)>=vision_ring_frames();
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
    if(best<vision_island_span() || first>bottom+VISION_OPENING_MARGIN_ROWS)return 0;
    span=last-first;
    for(row=first+1;row<last;++row) {
        int chord=edge[first]*(last-row)+edge[last]*(row-first);
        int deviation=direction*((int)edge[row]*span-chord);
        convex=deviation>=vision_island_convex()*span?convex+1:0;
        if(convex>=vision_island_rows())return 1;
    }
    return 0;
}

/* 灰岛位于白色道路内部时，扫线缺口可能没有两端闭合。
 * 直接关联连续暗区横截面，要求中部比上下端明显鼓出；矩形砖、条纹
 * 与单调透视边不提供此证据。不申请整幅连通域缓存。 */
static void vision_island_blob(const uint8_t *gray,track_vision_result_t *out,vision_features_t *f)
{
    int row,run=0,first_width=0,last_width=0,widest=0,center_sum=0,top_closed=0;
    int previous_left=0,previous_right=0,previous_center=0;
    if(out->zebra || f->measured_rows<6)return;
    for(row=VISION_RING_BRANCH_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
        int col=8,left=0,right=0,best_width=0,center;
        while(col<180) {
            int start,width;
            if(vision_white(gray,row,col,out->threshold)){++col;continue;}
            start=col;while(col<180 && !vision_white(gray,row,col,out->threshold))++col;
            width=col-start;
            if(start>11 && col<177 && width>=4 && width<=130 &&
               vision_white(gray,row,start-3,out->threshold) &&
               vision_white(gray,row,col+2,out->threshold) && width>best_width) {
                left=start;right=col-1;best_width=width;
            }
        }
        center=(left+right)/2;
        if(best_width && run && (left>previous_right+2 || right<previous_left-2 ||
                                 vision_abs(center-previous_center)>VISION_MAX_ROW_STEP_PX))run=0;
        if(!best_width || !run) {
            if(run>=VISION_RING_BLOB_MIN_ROWS && top_closed && vision_white(gray,row,previous_center,out->threshold) &&
               widest>=first_width+VISION_RING_BLOB_CURVE_PX && widest>=last_width+VISION_RING_BLOB_CURVE_PX) {
                int island_center=center_sum/run;
                if(island_center<VISION_CENTER_COL-6) {
                    f->left_blob=1;f->left_blob_bottom=(uint8_t)(row-1);
                    island_seed_side=TRACK_SIDE_LEFT;island_seed_x=(uint8_t)previous_right;island_seed_y=(uint8_t)(row-1);
                } else if(island_center>VISION_CENTER_COL+6) {
                    f->right_blob=1;f->right_blob_bottom=(uint8_t)(row-1);
                    island_seed_side=TRACK_SIDE_RIGHT;island_seed_x=(uint8_t)previous_left;island_seed_y=(uint8_t)(row-1);
                }
            }
            run=0;widest=0;center_sum=0;first_width=best_width;
            top_closed=best_width && row>VISION_RING_BRANCH_TOP_ROW &&
                vision_white(gray,row-1,center,out->threshold);
        }
        if(best_width) {
            ++run;center_sum+=center;last_width=best_width;
            if(best_width>widest)widest=best_width;
            previous_left=left;previous_right=right;previous_center=center;
        }
    }
}

static int vision_ring_evidence(const vision_features_t *f,track_side_t side,int island)
{
    int gap=side==TRACK_SIDE_LEFT?f->left_gap:f->right_gap;
    int fork=side==TRACK_SIDE_LEFT?f->left_fork:f->right_fork;
    int blob=side==TRACK_SIDE_LEFT?f->left_blob:f->right_blob;
    /* 开口+弯曲边缘可能只是普通弯道；独立分叉或封闭灰岛才能分流。 */
    return gap+2*fork+island+4*blob>=4;
}

static void vision_change(track_element_t phase,uint32_t now_ms,uint32_t distance_mm)
{
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
           (out->row_flags[row]&TRACK_ROW_PATH_VALID) && !(out->row_flags[row]&TRACK_ROW_ZEBRA) &&
           (!paired_only || ((out->right[row]-out->left[row])*100>=vision_width(row)*65 &&
             (out->right[row]-out->left[row])*100<=vision_width(row)*135))) {
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
    /* 十字内沿锁定来向关联前向出口，禁止横支路重写来向。 */
    for(row=VISION_TOP_ROW;row<=VISION_FAR_LAST_ROW;++row) {
        int col=2,found=0,best_delta=VISION_CROSS_EXIT_SEARCH_PX+1,width=vision_width(row);
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
    if(vision_abs(near_error-vision.incoming_near)>VISION_CROSS_EXIT_SEARCH_PX ||
       vision_abs(far_error-vision.incoming_far)>VISION_CROSS_EXIT_SEARCH_PX)return;
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
    int normal=features->paired_rows>=(VISION_BOTTOM_ROW-VISION_TOP_ROW)*3/4 && !curved && out->has_path;
    int left=(features->left_gap || features->left_blob) && !features->right_blob &&
        (features->left_supported || (features->left_blob && features->measured_rows>=6));
    int right=(features->right_gap || features->right_blob) && !features->left_blob &&
        (features->right_supported || (features->right_blob && features->measured_rows>=6));
    int bounded_cross=features->left_gap && features->right_gap;
    int cross=bounded_cross;
    int incoming_straight=vision_abs(out->far_error_px-out->near_error_px)<=VISION_RING_APPROACH_MAX_CURVE_PX &&
        vision_abs(out->near_error_px)<=VISION_RING_APPROACH_MAX_NEAR_PX;
    unsigned joint_run=0,joint_best=0,near_open=0,near_paired=0,near_any_open=0;
    int row,left_island,right_island,left_entry,right_entry;
    int weak_direction=vision_abs(out->far_error_px-out->near_error_px)<=VISION_RING_WEAK_DIRECTION_MAX_PX;
    /* 分支展开会污染远场双边平均。来向只能由当前近场实测双边或
     * 最近180ms内的同类证据确定，不能由单条横向支路重新定义。 */
    if(vision.phase!=TRACK_ELEMENT_CROSS && vision.phase<TRACK_ELEMENT_ROUNDABOUT_APPROACH) {
        if(out->left_opening_rows<6 && out->right_opening_rows<6 &&
           vision_cross_direction(out,1))vision.incoming_ms=now_ms;
        else if(now_ms-vision.incoming_ms>VISION_CANDIDATE_MAX_GAP_MS)vision.incoming_valid=0;
    }
    if(vision.incoming_valid && now_ms-vision.incoming_ms<=VISION_CANDIDATE_MAX_GAP_MS)
        incoming_straight=vision_abs(vision.incoming_far-vision.incoming_near)<=VISION_RING_APPROACH_MAX_CURVE_PX &&
            vision_abs(vision.incoming_near)<=VISION_RING_APPROACH_MAX_NEAR_PX;
    if((features->left_island || features->left_blob) && left && (out->near_path_supported || out->has_path)) {
        vision.island_left_seen=1;vision.island_left_ms=now_ms;vision.island_left_mm=distance_mm;
    }
    if((features->right_island || features->right_blob) && right && (out->near_path_supported || out->has_path)) {
        vision.island_right_seen=1;vision.island_right_ms=now_ms;vision.island_right_mm=distance_mm;
    }
    left_island=features->left_island || (vision.island_left_seen &&
        now_ms-vision.island_left_ms<=VISION_RING_EVIDENCE_MAX_MS &&
        distance_mm-vision.island_left_mm<=VISION_RING_EVIDENCE_MAX_MM);
    right_island=features->right_island || (vision.island_right_seen &&
        now_ms-vision.island_right_ms<=VISION_RING_EVIDENCE_MAX_MS &&
        distance_mm-vision.island_right_mm<=VISION_RING_EVIDENCE_MAX_MM);
    left_entry=left && vision_ring_evidence(features,TRACK_SIDE_LEFT,left_island) &&
        ((incoming_straight && weak_direction) || features->left_blob) && (out->near_path_supported || out->has_path || features->measured_rows>=6) && !out->zebra;
    right_entry=right && vision_ring_evidence(features,TRACK_SIDE_RIGHT,right_island) &&
        ((incoming_straight && weak_direction) || features->right_blob) && (out->near_path_supported || out->has_path || features->measured_rows>=6) && !out->zebra;


    /* 拒绝位显示实际采用的证据组合；灰岛支路不再显示原硬门槛全缺失。 */
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
        if(!vision.cross_near_seen && near_open>=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)/2) {
            vision.cross_near_seen=1;vision.cross_near_mm=distance_mm;
        }
        /* 仅凭远场拓宽进入的是暂定十字。近场双开口确认之前，当前连续的
         * 单侧缺口、分叉、岛边和对侧支持可修正身份，避免早判屏蔽真正环岛。
         * 已确认近场十字或超过重判距离后不允许改道；不借旧缓存补足证据。 */
        if(((vision.cross_provisional && !vision.cross_near_seen) || features->left_blob || features->right_blob) &&
           traveled<=VISION_CROSS_RECONSIDER_MAX_MM &&
           (!vision.cooldown || distance_mm-vision.cooldown_mm>=VISION_RING_COOLDOWN_MM)) {
            vision.candidate_left=vision_ring_vote(vision.candidate_left,left_entry);
            vision.candidate_right=vision_ring_vote(vision.candidate_right,right_entry);
            if((vision_ring_confirmed(vision.candidate_left) && left_entry) ||
               (vision_ring_confirmed(vision.candidate_right) && right_entry)) {
                vision.side=vision_ring_confirmed(vision.candidate_left) && left_entry?TRACK_SIDE_LEFT:TRACK_SIDE_RIGHT;
                vision.incoming_near=out->near_error_px;vision.incoming_far=out->far_error_px;
                vision_change(TRACK_ELEMENT_ROUNDABOUT_APPROACH,now_ms,distance_mm);
                vision_clear_candidates();
                goto elements_complete;
            }
        }
        /* 双侧开口已到达近场，随后近场双边重新闭合，即已越过十字。
         * 出口的远场可以紧接弯道，不能要求整幅图恢复直道。 */
        vision.recovery=vision_increment(vision.recovery,(normal && !cross) ||
            (near_open==0 &&
             (near_paired*100U>=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)*vision_outer_near_percent() ||
              (vision.cross_provisional && (traveled>=VISION_CROSS_CURVE_RECHECK_MM ||
                 now_ms-vision.phase_ms>=VISION_CROSS_CURVE_RECHECK_MS) &&
               features->measured_rows>=VISION_MIN_MEASURED_ROWS &&
               vision_abs(out->raw_far_error_px-out->raw_near_error_px)>=VISION_CURVE_ERROR_PX))));
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
                (VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)*vision_outer_near_percent();
        if(vision.phase==TRACK_ELEMENT_ROUNDABOUT_INSIDE)vision_current_exit_cue=(uint8_t)(outer_gap || outer_widening);
        int same_fork=vision.side==TRACK_SIDE_LEFT?
            vision_ring_evidence(features,TRACK_SIDE_LEFT,left_island):
            vision_ring_evidence(features,TRACK_SIDE_RIGHT,right_island);
        uint8_t blob=vision.side==TRACK_SIDE_LEFT?features->left_blob:features->right_blob;
        uint8_t bottom=vision.side==TRACK_SIDE_LEFT?
            (blob?features->left_blob_bottom:features->left_bottom):
            (blob?features->right_blob_bottom:features->right_bottom);
        switch(vision.phase) {
        case TRACK_ELEMENT_ROUNDABOUT_APPROACH:
            /* 限界先于确认提交；历史分叉不能授权当前不可见的入口。 */
            if(traveled>VISION_RING_APPROACH_MAX_MM || now_ms-vision.phase_ms>VISION_RING_APPROACH_MAX_MS) {
                vision.side=TRACK_SIDE_NONE;
                vision_change(TRACK_ELEMENT_STRAIGHT,now_ms,distance_mm);
                vision_clear_candidates();break;
            }
            vision.recovery=vision_increment(vision.recovery,same_gap && same_fork &&
                (out->near_path_supported || out->has_path || features->measured_rows>=6) &&
                bottom>=(blob?
                    VISION_RING_BLOB_ENTRY_ROW:VISION_RING_ENTRY_ROW));
            /* 近端岛边暂未完整可见时保留有界候选；正式入环仍要求same_fork。 */
            vision.approach_clear=vision_increment(vision.approach_clear,out->has_path &&
                (!same_gap || !same_fork));

            if(vision.recovery>=VISION_RING_ENTRY_CONFIRM_FRAMES) {
                vision_change(TRACK_ELEMENT_ROUNDABOUT_ENTER,now_ms,distance_mm);
            } else if(vision.approach_clear>=VISION_RECOVERY_FRAMES) {
                vision.side=TRACK_SIDE_NONE;
                vision_change(TRACK_ELEMENT_STRAIGHT,now_ms,distance_mm);
                vision_clear_candidates();
            }
            break;
        case TRACK_ELEMENT_ROUNDABOUT_ENTER:
            vision.recovery=vision_increment(vision.recovery,out->has_path &&
                (vision.side==TRACK_SIDE_LEFT ?
                 (out->far_error_px<=-VISION_CURVE_ERROR_PX && out->far_error_px-out->near_error_px<=-VISION_CURVE_ERROR_PX) :
                 (out->far_error_px>=VISION_CURVE_ERROR_PX && out->far_error_px-out->near_error_px>=VISION_CURVE_ERROR_PX)));
            if(traveled>=VISION_RING_ENTER_MIN_MM && vision.recovery>=vision_ring_frames()) {
                vision.exit_seen=0;vision.exit_near_seen=0;vision.ring_path_valid=0;vision.ring_path_seen=0;vision_change(TRACK_ELEMENT_ROUNDABOUT_INSIDE,now_ms,distance_mm);
            }
            break;
        case TRACK_ELEMENT_ROUNDABOUT_INSIDE:
            /* 完成入环后，必须看到新的外侧分叉，单独直道恢复不能出环。 */
            /* 出口预告和图像路径共用关联期限；正式出环仍依据当前近场开口。 */
            if(vision.exit_seen && (distance_mm-vision.exit_seen_mm>VISION_RING_EXIT_MEMORY_MAX_MM ||
                                   now_ms-vision.exit_seen_ms>VISION_RING_EXIT_MEMORY_MAX_MS)) {
                vision.exit_seen=0;vision.exit_near_seen=0;vision.ring_path_valid=0;vision.recovery=0;vision.exit_candidate=0;
            }
            if(vision.exit_seen && (vision.side==TRACK_SIDE_LEFT?features->right_open[VISION_RING_EXIT_NEAR_ROW]:features->left_open[VISION_RING_EXIT_NEAR_ROW]))vision.exit_near_seen=1;
            vision.recovery=vision_increment(vision.recovery,vision.exit_seen && vision.exit_near_seen &&
                out->near_path_supported && features->measured_rows>=VISION_MIN_MEASURED_ROWS);
            if(vision.recovery>=vision_ring_frames())
                vision_change(TRACK_ELEMENT_ROUNDABOUT_EXIT,now_ms,distance_mm);
            break;
        case TRACK_ELEMENT_ROUNDABOUT_EXIT:
                /* 出口已在上一阶段确认；近场双边闭合并连续恢复即可结束出环。
             * 远处紧接连续弯道时，不要求整幅恢复直道。仍保留最短出环
             * 里程、当前有效路径、无近场开口及三帧确认。 */
            vision.recovery=vision_increment(vision.recovery,(normal && !left && !right) ||
                (out->has_path && near_any_open==0 &&
                 near_paired*100U>=(VISION_NEAR_LAST_ROW-VISION_NEAR_FIRST_ROW+1)*vision_outer_near_percent()));
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
        vision.candidate_cross=vision_increment(vision.candidate_cross,cross &&
            (out->near_path_supported || features->measured_rows>=6) && !out->zebra);
        vision.candidate_left=vision_ring_vote(vision.candidate_left,left &&
            (features->left_fork || features->left_blob) && (incoming_straight || features->left_blob) &&
            (out->near_path_supported || out->has_path || (features->left_blob && features->measured_rows>=6)) && !out->zebra && !vision.cooldown);
        vision.candidate_right=vision_ring_vote(vision.candidate_right,right &&
            (features->right_fork || features->right_blob) && (incoming_straight || features->right_blob) &&
            (out->near_path_supported || out->has_path || (features->right_blob && features->measured_rows>=6)) && !out->zebra && !vision.cooldown);
        if(vision.candidate_cross>=VISION_CROSS_CONFIRM_FRAMES &&
           !(vision_ring_confirmed(vision.candidate_left) && left_entry) &&
           !(vision_ring_confirmed(vision.candidate_right) && right_entry)) {
            if(!vision.incoming_valid || now_ms-vision.incoming_ms>VISION_CANDIDATE_MAX_GAP_MS) {
                vision.incoming_near=out->near_error_px;vision.incoming_far=out->far_error_px;
                /* 首次斜向十字若双边已出画面，用最近端实测边初始化。
                 * 已有双边来向时不进入此分支，单边不能覆盖历史方向。 */
                (void)vision_cross_direction(out,0);
            }
            vision_change(TRACK_ELEMENT_CROSS,now_ms,distance_mm);vision.cross_provisional=(uint8_t)!bounded_cross;
            vision_clear_candidates();
        } else if((vision_ring_confirmed(vision.candidate_left) && left_entry) ||
                  (vision_ring_confirmed(vision.candidate_right) && right_entry)) {
            vision.side=vision_ring_confirmed(vision.candidate_left) && left_entry?TRACK_SIDE_LEFT:TRACK_SIDE_RIGHT;
            if(!vision.incoming_valid || now_ms-vision.incoming_ms>VISION_CANDIDATE_MAX_GAP_MS) {
                vision.incoming_near=out->near_error_px;vision.incoming_far=out->far_error_px;
            }

            vision_change(TRACK_ELEMENT_ROUNDABOUT_APPROACH,now_ms,distance_mm);
            vision_clear_candidates();
        }
    }
elements_complete:
    out->element=vision.phase;
    out->side=vision.side;
    if(vision.phase==TRACK_ELEMENT_STRAIGHT) out->element=curved?TRACK_ELEMENT_CURVE:TRACK_ELEMENT_STRAIGHT;
}

static float geometry_ground_a,geometry_ground_b,geometry_row_slope;
static uint8_t geometry_ground_ready,geometry_ground_valid;

/* 独立于赛道元素，只保存单侧偏移；原始边线和灰度均不改写。 */
static struct {uint8_t side;float offset_mm;uint32_t last_mm,clear_after_mm;} obstacle;
static uint8_t obstacle_left[120],obstacle_right[120],obstacle_rows[120];
static float obstacle_center[120];
static void vision_obstacle_detect(const uint8_t *gray,const track_vision_result_t *out,
                                   uint32_t distance_mm)
{
    uint8_t left[120]={0},right[120]={0},suspect[120]={0};
    int row,col,side=0,top=0,bottom=0,matches=0;
    float offset=0;
    memset(obstacle_rows,0,sizeof obstacle_rows);
    for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
        int seed=out->center[row],first,last;
        if(seed<2 || seed>185 || !vision_white(gray,row,seed,out->threshold) || zebra_region[row])continue;
        first=last=seed;
        for(col=seed;col>=2;--col) {
            first=col;
            if(!vision_white(gray,row,col-1,out->threshold) && !vision_white(gray,row,col-2,out->threshold))break;
        }
        for(col=seed;col<=185;++col) {
            last=col;
            if(!vision_white(gray,row,col+1,out->threshold) && !vision_white(gray,row,col+2,out->threshold))break;
        }
        if(first<3 || last>184 || last-first<vision_width(row)/4)continue;
        left[row]=(uint8_t)first;right[row]=(uint8_t)last;
        suspect[row]=(uint8_t)((last-first)*100<vision_width(row)*94);
        if((last-first)*100>vision_width(row)*120)left[row]=right[row]=0;
    }
    for(row=VISION_BOTTOM_ROW;row>=VISION_TOP_ROW;--row) {
        int end,start,a,b,s=0,good=0,r;float needed=0;
        if(!suspect[row] || !right[row])continue;
        end=row;
        while(row>=VISION_TOP_ROW && suspect[row] && right[row])--row;
        start=row+1;a=start-1;b=end+1;
        if(end-start+1<vision_obstacle_min_rows() || end-start+1>VISION_OBSTACLE_MAX_ROWS ||
           a<VISION_TOP_ROW || b>VISION_BOTTOM_ROW || !right[a] || !right[b] || suspect[a] || suspect[b])continue;
        for(r=start;r<=end;++r) {
            float t=(float)(r-a)/(b-a),l=left[a]+t*(left[b]-left[a]),h=right[a]+t*(right[b]-right[a]);
            float width=(float)vision_width(r),dl=left[r]-l,dr=h-right[r],tol=2+width*.04f;
            int ls=dl>width*.08f && fabsf(dr)<=tol;
            int rs=dr>width*.08f && fabsf(dl)<=tol;
            int free_l=left[r],free_r=right[r];float need;
            obstacle_left[r]=(uint8_t)free_l;obstacle_right[r]=(uint8_t)free_r;
            obstacle_center[r]=(l+h)*.5f;
            if(ls==rs || (s && s!=(ls?1:2)))continue;
            s=ls?1:2;++good;
            need=(s==1?(free_l-obstacle_center[r]):(obstacle_center[r]-free_r))*VISION_ROAD_WIDTH_MM/width+
                 VEHICLE_HALF_TIRE_OUTER_MM+VISION_OBSTACLE_MARGIN_MM;
            if(need>needed)needed=need;
        }
        if((unsigned)good*100U<(unsigned)(end-start+1)*VISION_OBSTACLE_SUPPORT_PERCENT || !s)continue;
        if(++matches>1)continue;
        side=s;top=start;bottom=end;offset=fmaxf(0,needed);
    }
    if(side) {
        if(!obstacle.side) {
            obstacle.side=(uint8_t)side;
        }
        if(obstacle.side==side) {
            float far=geometry_ground_a/vision_width(top)+geometry_ground_b;
            obstacle.offset_mm=offset;obstacle.last_mm=distance_mm;
            obstacle.clear_after_mm=(uint32_t)fmaxf(0,far)+VISION_OBSTACLE_REAR_CLEAR_MM;
            for(row=top;row<=bottom;++row)obstacle_rows[row]=1;
        }
    }
    if(obstacle.side && !side && distance_mm-obstacle.last_mm>obstacle.clear_after_mm+VISION_OBSTACLE_RETURN_MM) {
        obstacle.side=0;obstacle.offset_mm=0;
    }
}
static float vision_smoothstep(float t)
{
    return t*t*t*(10+t*(-15+6*t));
}

static void vision_obstacle_path(track_vision_result_t *out,uint32_t distance_mm)
{
    if(!obstacle.side)return;
    float offset=obstacle.offset_mm;
    uint32_t traveled=distance_mm-obstacle.last_mm;
    if(traveled>obstacle.clear_after_mm)offset*=1-vision_smoothstep(fminf(1,(float)(traveled-obstacle.clear_after_mm)/VISION_OBSTACLE_RETURN_MM));
    for(int row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
        float width=vision_width(row),y=geometry_ground_a/width+geometry_ground_b;
        float center=out->center[row];
        if(out->row_flags[row]&(obstacle.side==1?TRACK_ROW_RIGHT_MEASURED:TRACK_ROW_LEFT_MEASURED))center=obstacle.side==1?out->right[row]-width*.5f:out->left[row]+width*.5f;
        if(obstacle_rows[row])center=obstacle_center[row];
        center+=(obstacle.side==1?1:-1)*offset*vision_smoothstep(fminf(1,fmaxf(0,y/VISION_OBSTACLE_TRANSITION_MM)))*width/VISION_ROAD_WIDTH_MM;
        out->center[row]=(uint8_t)vision_clip((int)lroundf(center),2,185);
        out->row_flags[row]|=TRACK_ROW_RECONSTRUCTED|TRACK_ROW_PATH_VALID;
    }
}
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

static int contour_goal_valid,contour_model_valid;

static float contour_model_x,contour_model_y,contour_model_radius,contour_model_error;

static float contour_goal_x,contour_goal_y;

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
    contour_goal_valid=0;contour_model_valid=0;
    if(out->element==TRACK_ELEMENT_ROUNDABOUT_APPROACH) {
        /* 入环必须跟随已识别灰岛，不能借下方三角支路的圆弧提交入环。 */
        if(island_seed_side!=side)return 0;
        x=island_seed_x;y=island_seed_y;bx=x-sign;by=y;
        vertical=1;
        goto trace_again;
    }
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
            contour_model_valid=contour_model_radius>=150 && contour_model_radius<=1500 && contour_model_error<VISION_GEOMETRY_FIT_ERROR_MM;

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

static int vision_cross_anchor(const track_vision_result_t *out,int row)
{
    int predicted=VISION_CENTER_COL+vision.incoming_near+(VISION_NEAR_FIRST_ROW-row)*
        (vision.incoming_far-vision.incoming_near)/(VISION_NEAR_FIRST_ROW-VISION_FAR_FIRST_ROW);
    return (out->row_flags[row]&3U) &&
        (row>=VISION_NEAR_FIRST_ROW || vision_abs(out->center[row]-predicted)<=VISION_EDGE_WINDOW_PX);
}

static int vision_cross_curve(const track_vision_result_t *out,uint32_t now_ms)
{
    return vision.phase==TRACK_ELEMENT_CROSS && vision.cross_provisional && !vision.cross_near_seen &&
        now_ms-vision.phase_ms>=VISION_CROSS_CURVE_RECHECK_MS && out->near_path_supported &&
        vision_abs(out->raw_far_error_px-out->raw_near_error_px)>=VISION_CURVE_ERROR_PX;
}

static void vision_element_path(const uint8_t *gray,track_vision_result_t *out,const vision_features_t *features,
                                uint32_t now_ms,uint32_t distance_mm)
{
    int row;
    if(out->element==TRACK_ELEMENT_CROSS) {
        if(vision_cross_curve(out,now_ms))return;
        vision_cross_update_direction(gray,out);
        for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
            int lo=row-1,hi=row+1;
            int error=vision.incoming_near + (VISION_NEAR_FIRST_ROW-row)*
                (vision.incoming_far-vision.incoming_near)/(VISION_NEAR_FIRST_ROW-VISION_FAR_FIRST_ROW);
            int center=vision_clip(VISION_CENTER_COL+error,2,185);
            /* 十字只授权补缺口；实测弯道和已经关联的出口不能被直线覆盖。 */
            if(vision_cross_anchor(out,row) &&
               !(features->left_open[row] && features->right_open[row]))continue;
            while(lo>=VISION_TOP_ROW && !vision_cross_anchor(out,lo))--lo;
            while(hi<=VISION_BOTTOM_ROW && !vision_cross_anchor(out,hi))++hi;
            if(lo>=VISION_TOP_ROW && hi<=VISION_BOTTOM_ROW)
                center=(out->center[lo]*(hi-row)+out->center[hi]*(row-lo))/(hi-lo);
            if(vision_white(gray,row,center,out->threshold)) {
                out->center[row]=(uint8_t)center;
                out->row_flags[row]|=TRACK_ROW_PATH_VALID|TRACK_ROW_RECONSTRUCTED;
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
           out->has_path &&
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

void track_vision_reset(void)
{
    memset(&vision,0,sizeof(vision));
    island_seed_side=0;
    memset(&obstacle,0,sizeof obstacle);memset(obstacle_rows,0,sizeof obstacle_rows);

    vision_geometry_calibration();contour_goal_valid=0;contour_model_valid=0;
}

static void vision_observations(track_vision_result_t *out,const vision_features_t *features)
{
    out->near_path_supported=(uint8_t)vision_near_supported(out);
    out->has_path=features->measured_rows!=0;
    out->near_error_px=vision_error(out,VISION_NEAR_FIRST_ROW,VISION_NEAR_LAST_ROW);
    out->far_error_px=vision_error(out,VISION_FAR_FIRST_ROW,VISION_FAR_LAST_ROW);
}

static void vision_metric_pixel_path(const uint8_t *gray,const track_vision_result_t *out)
{
    int row,best_row=-1;unsigned used=0;
    float best=1e9f,sx=0,sy=0;
    if(!out->has_path || (out->element>=TRACK_ELEMENT_ROUNDABOUT_ENTER && !obstacle.side))return;
    for(row=VISION_FAR_FIRST_ROW;row<=VISION_BOTTOM_ROW;++row) {
        float width,x,y,radial,error;
        if(!(out->row_flags[row]&TRACK_ROW_PATH_VALID) ||
            (!vision_white(gray,row,out->center[row],out->threshold) && !vision_zebra_pixel(out,row,out->center[row])))continue;
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
            (!vision_white(gray,row,out->center[row],out->threshold) && !vision_zebra_pixel(out,row,out->center[row])))continue;
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
    memset(out,0,sizeof(*out));out->zebra_near_row=255;out->zebra_far_row=255;
    out->near_error_px=vision.last_near;out->far_error_px=vision.last_far;
    memset(zebra_region,0,sizeof zebra_region);zebra_region_first=120;zebra_region_last=-1;
    vision_current_exit_cue=0;
    island_seed_side=0;
    vision_cache_expire(now_ms,distance_mm);
    contour_goal_valid=0;contour_model_valid=0;
    if(!geometry_ground_ready)vision_geometry_calibration();
    out->preview_first_row=VISION_FAR_FIRST_ROW;out->preview_last_row=VISION_FAR_LAST_ROW;
    for(row=0;row<120;++row) {
        out->center[row]=VISION_CENTER_COL;out->right[row]=187;
    }
    if(gray==0) {
        vision.blob_side=TRACK_SIDE_NONE;
        vision_clear_candidates();vision.recovery=0;vision.approach_clear=0;vision.exit_candidate=0;
        vision.curve_outer_candidate=TRACK_SIDE_NONE;vision.curve_outer_frames=0;
        return;
    }
    out->threshold=vision_threshold(gray,&contrast);
    if(contrast<VISION_MIN_CONTRAST) {
        vision.blob_side=TRACK_SIDE_NONE;
        vision.curve_outer_candidate=TRACK_SIDE_NONE;vision.curve_outer_frames=0;

        /* 黑白板没有边界观测，不生成新路径偏差。 */
        memset(&features,0,sizeof(features));
        vision_elements(out,&features,now_ms,distance_mm);return;
    }
    vision_zebra_region(gray,out,now_ms,distance_mm);
    vision_scan(gray,out,&features,TRACK_SIDE_NONE);
    vision_obstacle_detect(gray,out,distance_mm);
    features.left_gap=vision_gap(features.left_open,out,TRACK_ROW_LEFT_MEASURED,&features.left_bottom);
    features.right_gap=vision_gap(features.right_open,out,TRACK_ROW_RIGHT_MEASURED,&features.right_bottom);
    features.left_supported=vision_opposite_continuous(features.left_open,out,TRACK_SIDE_LEFT);
    features.right_supported=vision_opposite_continuous(features.right_open,out,TRACK_SIDE_RIGHT);
    features.left_fork=vision_forward_branch(gray,out,&features,TRACK_SIDE_LEFT);
    features.right_fork=vision_forward_branch(gray,out,&features,TRACK_SIDE_RIGHT);
    features.left_island=vision_near_island(out,&features,TRACK_SIDE_LEFT);
    features.right_island=vision_near_island(out,&features,TRACK_SIDE_RIGHT);
    vision_observations(out,&features);
    vision_island_blob(gray,out,&features);
    if(features.left_blob || features.right_blob) {
        vision.blob_side=(uint8_t)(features.left_blob?TRACK_SIDE_LEFT:TRACK_SIDE_RIGHT);
        vision.blob_bottom=features.left_blob?features.left_blob_bottom:features.right_blob_bottom;
        vision.blob_ms=now_ms;vision.blob_mm=distance_mm;
        vision.blob_edge_x=island_seed_x;vision.blob_edge_y=island_seed_y;
    } else if(vision.phase==TRACK_ELEMENT_ROUNDABOUT_APPROACH && vision.blob_side &&
              now_ms-vision.blob_ms<=VISION_RING_ISLAND_TRACK_MS &&
              distance_mm-vision.blob_mm<=VISION_RING_ISLAND_TRACK_MM) {
        int y,x,best=100000,chosen_x=0,chosen_y=0,sign=vision.blob_side==TRACK_SIDE_LEFT?-1:1;
        /* 灰岛接近/出画后会失去封闭形状。只在上次岛边邻域寻找当前
         * 同向黑白边界，不能用整幅另一块暗区延长身份。 */
        for(y=vision_clip(vision.blob_edge_y-20,12,116);y<=vision_clip(vision.blob_edge_y+30,12,116);++y)
          for(x=vision_clip(vision.blob_edge_x-30,3,184);x<=vision_clip(vision.blob_edge_x+30,3,184);++x) {
            int dx=x-vision.blob_edge_x,dy=y-vision.blob_edge_y,score=dx*dx+dy*dy;
            if(!vision_white(gray,y,x,out->threshold) && !vision_white(gray,y,x+sign,out->threshold) &&
               vision_white(gray,y,x-sign,out->threshold) && vision_white(gray,y,x-2*sign,out->threshold) && score<best) {
                best=score;chosen_x=x;chosen_y=y;
            }
          }
        if(best<=30*30) {
            island_seed_side=vision.blob_side;island_seed_x=(uint8_t)chosen_x;island_seed_y=(uint8_t)chosen_y;
            vision.blob_edge_x=island_seed_x;vision.blob_edge_y=island_seed_y;
            if(vision.blob_side==TRACK_SIDE_LEFT){features.left_blob=1;features.left_blob_bottom=island_seed_y;}
            else {features.right_blob=1;features.right_blob_bottom=island_seed_y;}
        }
    } else if(vision.blob_side && !out->zebra && features.measured_rows>=6 &&
              now_ms-vision.blob_ms<=VISION_RING_EVIDENCE_MAX_MS &&
              distance_mm-vision.blob_mm<=VISION_RING_EVIDENCE_MAX_MM) {
        unsigned open=0;
        for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row)
            open+=vision.blob_side==TRACK_SIDE_LEFT?features.left_open[row]:features.right_open[row];
        if(open>=VISION_OPENING_MIN_ROWS) {
            if(vision.blob_side==TRACK_SIDE_LEFT){features.left_blob=1;features.left_blob_bottom=vision.blob_bottom;}
            else {features.right_blob=1;features.right_blob_bottom=vision.blob_bottom;}
        }
    }
    out->raw_near_error_px=out->near_error_px;out->raw_far_error_px=out->far_error_px;

    out->opening_left_bottom=features.left_bottom;out->opening_right_bottom=features.right_bottom;
    for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
        if(features.left_open[row])++out->left_opening_rows;
        if(features.right_open[row])++out->right_opening_rows;
    }
    vision_elements(out,&features,now_ms,distance_mm);
    {
        track_side_t outer=vision_curve_outer(out,&features,now_ms);
        if(outer!=TRACK_SIDE_NONE) {
            vision_scan(gray,out,&features,outer);
            vision_observations(out,&features);
        }
    }
    if(vision.phase==TRACK_ELEMENT_STRAIGHT)
        out->element=vision_abs(out->far_error_px-out->near_error_px)>=VISION_CURVE_ERROR_PX?
            TRACK_ELEMENT_CURVE:TRACK_ELEMENT_STRAIGHT;
    vision_element_path(gray,out,&features,now_ms,distance_mm);
    contour_goal_valid=0;
    if(out->element>=TRACK_ELEMENT_ROUNDABOUT_APPROACH) {
        track_vision_result_t traced=*out;
        for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
            if(features.raw_mask[row]&1U){traced.left[row]=features.raw_left[row];traced.row_flags[row]|=TRACK_ROW_LEFT_MEASURED;}
            if(features.raw_mask[row]&2U){traced.right[row]=features.raw_right[row];traced.row_flags[row]|=TRACK_ROW_RIGHT_MEASURED;}
        }
        int found=vision_contour_path(gray,&traced,out->element<=TRACK_ELEMENT_ROUNDABOUT_ENTER?out->side:(out->side==TRACK_SIDE_LEFT?TRACK_SIDE_RIGHT:TRACK_SIDE_LEFT));
        if(!found && out->element==TRACK_ELEMENT_ROUNDABOUT_ENTER && distance_mm-vision.phase_mm>=VISION_RING_ENTER_MIN_MM)found=vision_contour_path(gray,&traced,out->side==TRACK_SIDE_LEFT?TRACK_SIDE_RIGHT:TRACK_SIDE_LEFT);
        if(found)for(row=VISION_TOP_ROW;row<=VISION_BOTTOM_ROW;++row) {
            out->center[row]=traced.center[row];out->row_flags[row]|=traced.row_flags[row]&(TRACK_ROW_PATH_VALID|TRACK_ROW_RECONSTRUCTED);
        }
    }
    else if((out->element==TRACK_ELEMENT_CURVE || vision_cross_curve(out,now_ms)) && VISION_CURVE_OUTER_ENABLED) {
        track_side_t chosen=out->curve_outer_side;
        if(chosen==TRACK_SIDE_NONE)chosen=out->far_error_px>out->near_error_px?TRACK_SIDE_LEFT:TRACK_SIDE_RIGHT;
        vision_contour_path(gray,out,chosen);
    }

    if(contour_goal_valid && out->element>=TRACK_ELEMENT_ROUNDABOUT_APPROACH) {
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
    vision_obstacle_path(out,distance_mm);
    /* 障碍选路改变中线后，按修正后的像素路径重新生成地面目标。 */
    if(obstacle.side)contour_goal_valid=0;
    if(!contour_goal_valid && geometry_ground_valid)vision_metric_pixel_path(gray,out);
    if(contour_goal_valid) {
        out->geometry.valid=1;
        out->geometry.target_right_mm=contour_goal_x;
        out->geometry.target_forward_mm=contour_goal_y;
    }
    out->near_error_px=vision_error(out,VISION_NEAR_FIRST_ROW,VISION_NEAR_LAST_ROW);
    out->far_error_px=vision_preview_error(out);
    if(vision.phase==TRACK_ELEMENT_ROUNDABOUT_APPROACH) {
        /* 已确认拓扑后，用当前实际可通行的转弯轮迹提交入环，
         * 不再只等灰岛末端跨过固定图像行。没有路径不推进状态。 */
        int current_island=vision.side==TRACK_SIDE_LEFT?features.left_blob:features.right_blob;
        vision.contour_enter_frames=vision_increment(vision.contour_enter_frames,
            current_island && island_seed_side==vision.side && contour_model_valid==1 && contour_goal_valid && out->has_path &&
            (vision.side==TRACK_SIDE_LEFT?contour_goal_x<-20:contour_goal_x>20));
        if(vision.contour_enter_frames>=VISION_RING_ENTRY_CONFIRM_FRAMES) {
            vision_change(TRACK_ELEMENT_ROUNDABOUT_ENTER,now_ms,distance_mm);out->element=vision.phase;
            vision.contour_enter_frames=0;
        }
    } else if(vision.phase==TRACK_ELEMENT_ROUNDABOUT_ENTER) {
        vision.contour_enter_frames=vision_increment(vision.contour_enter_frames,
            contour_goal_valid && (vision.side==TRACK_SIDE_LEFT?contour_goal_x<0:contour_goal_x>0) &&
            fabsf(atanf(2*VEHICLE_WHEELBASE_MM*contour_goal_x/(contour_goal_x*contour_goal_x+contour_goal_y*contour_goal_y)))>=0.11435397f);
        if(distance_mm-vision.phase_mm>=VISION_RING_ENTER_MIN_MM && vision.contour_enter_frames>=3) {
            vision.exit_seen=0;vision.exit_near_seen=0;vision.ring_path_valid=0;vision.ring_path_seen=0;
            vision_change(TRACK_ELEMENT_ROUNDABOUT_INSIDE,now_ms,distance_mm);out->element=vision.phase;
        }
    } else vision.contour_enter_frames=0;
    /* 出口分叉来自原始图像，可靠性采用本帧最终路径；低原始双边覆盖
     * 不应否定已经过密集轮迹核验的轮廓。三帧、最短里程和近端开口仍必需。 */
    if(vision.phase==TRACK_ELEMENT_ROUNDABOUT_INSIDE && !vision.exit_seen) {
        vision.exit_candidate=vision_increment(vision.exit_candidate,vision.ring_path_seen &&
            vision_current_exit_cue && out->has_path && distance_mm-vision.phase_mm>=VISION_RING_INSIDE_MIN_MM);
        if(vision.exit_candidate>=vision_ring_frames()) {
            vision.exit_seen=1;vision.exit_seen_ms=now_ms;vision.exit_seen_mm=distance_mm;
            if(vision.side==TRACK_SIDE_LEFT?features.right_open[VISION_RING_EXIT_NEAR_ROW]:features.left_open[VISION_RING_EXIT_NEAR_ROW])vision.exit_near_seen=1;
        }
    }
    /* Observability is descriptive. Neither sparse edges nor a missing frame reduce drive power. */
    int near=0,far=0;
    for(row=VISION_NEAR_FIRST_ROW;row<=VISION_NEAR_LAST_ROW;++row)near+=(out->row_flags[row]&3U)!=0;
    for(row=out->preview_first_row;row<=out->preview_last_row;++row)far+=(out->row_flags[row]&3U)!=0;
    out->has_path=near || far;
    if(near)vision.last_near=out->near_error_px;
    if(far)vision.last_far=out->far_error_px;
    if(near && !far)vision.last_far=vision.last_near;
    if(far && !near)vision.last_near=vision.last_far;
    out->near_error_px=vision.last_near;out->far_error_px=vision.last_far;
    if(out->has_path)vision.zebra_center=(int16_t)vision_clip(out->center[VISION_NEAR_FIRST_ROW],32,155);
}
