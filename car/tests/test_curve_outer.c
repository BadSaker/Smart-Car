/* 用真实灰度图验证内侧出画面后的外侧选择，不注入视觉内部状态。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "track_vision.h"
#include "vision_config.h"

static uint8_t image[188*120];
static track_vision_result_t result;
static unsigned checks,failures;
#define CHECK(c,label) do { ++checks; if(!(c)) { ++failures; \
    printf("FAIL %s line %d Q=%u near=%d far=%d outer=%d\n",label,__LINE__, \
    result.confidence,result.near_error_px,result.far_error_px,result.curve_outer_side); } } while(0)

/* 像素级单边/缺边夹具；斜向恒定像素半宽不等于恒定地面法向半宽。
 * 此处验证实测位、可见性与方向；地面路径精度由冻结灰度和闭环评分验证。 */
static int width(int row) { return 44+(row-30)*123/86; }
static int center(int row,int direction,int bend) { return 93+direction*(18+bend*(116-row)/86); }
static void lane(int direction,int bend,int missing_inner,int fake_inner,int stripe)
{
    int row,col;
    memset(image,25,sizeof image);
    /* 扫线ROI到116，但轮迹核验使用完整相机画面，不能把117..119画成黑带。 */
    for(row=30;row<=119;++row) {
        int half=width(row)/2,mid=center(row,direction,bend);
        int left=mid-half,right=mid+half;
        if(missing_inner) { if(direction>0)right=187;else left=0; }
        if(fake_inner && row>=45 && row<=70) {
            if(direction>0)right=(mid-half+width(row)-24<184)?mid-half+width(row)-24:184;
            else left=(mid+half-width(row)+24>3)?mid+half-width(row)+24:3;
        }
        for(col=left;col<=right;++col) if(col>=0 && col<188) image[row*188+col]=220;
    }
    if(stripe)memset(image+55*188,25,3*188);
}
static void process(uint32_t time) { track_vision_process(image,time,time/10,&result); }

static void test_outer(int direction,int fake,int stripe)
{
    unsigned frame;
    int row;
    track_side_t outer=direction>0?TRACK_SIDE_LEFT:TRACK_SIDE_RIGHT;
    track_vision_reset();lane(direction,70,1,fake,stripe);process(0);
    CHECK(result.curve_outer_side==TRACK_SIDE_NONE,"first candidate never forces outer");
    for(frame=1;frame<VISION_CURVE_OUTER_CONFIRM_FRAMES+2;++frame)process(frame*33);
    CHECK(result.valid && result.confidence>=45,"single continuous outer supports driving");
    CHECK(result.element==TRACK_ELEMENT_CURVE,"ordinary bend remains a curve");
    if(VISION_CURVE_OUTER_ENABLED) {
        CHECK(result.curve_outer_side==outer,"confirmed direction selects correct outer");
        CHECK(result.curve_outer_supported && result.curve_outer_near_rows==23,
              "continuous measured outer enables reliable support");
        for(row=45;row<=70;++row) if(!stripe || row<55 || row>57) {
            uint8_t measured=direction>0?TRACK_ROW_LEFT_MEASURED:TRACK_ROW_RIGHT_MEASURED;
            uint8_t inner=direction>0?TRACK_ROW_RIGHT_MEASURED:TRACK_ROW_LEFT_MEASURED;
            CHECK((result.row_flags[row]&measured) && !(result.row_flags[row]&inner),"fake inner cannot count as measured");
            CHECK(result.row_flags[row]&TRACK_ROW_RECONSTRUCTED,"outer path is marked reconstructed");
            CHECK(image[row*188+result.center[row]]>result.threshold,"reconstructed path remains in current visible road");
        }
    } else CHECK(result.curve_outer_side==TRACK_SIDE_NONE,"configuration can disable preference");
    CHECK(direction*result.near_error_px>0 && direction*result.far_error_px>0,"curve error has correct sign");
    if(result.geometry.valid)CHECK(direction*result.geometry.target_right_mm>0,"metric target points toward the visible road");
    lane(direction,0,0,0,0);process(200);
    CHECK(result.curve_outer_side==TRACK_SIDE_NONE,"two visible straight edges immediately release preference");
}

int main(void)
{
    int direction,fake,stripe;
    for(direction=-1;direction<=1;direction+=2)
        for(fake=0;fake<2;++fake)for(stripe=0;stripe<2;++stripe)test_outer(direction,fake,stripe);
    track_vision_reset();lane(1,70,1,0,0);process(0);process(250);
    CHECK(result.curve_outer_side==TRACK_SIDE_NONE,"frame gap discards old confirmation");
    track_vision_process(NULL,260,0,&result);
    CHECK(!result.valid && result.curve_outer_side==TRACK_SIDE_NONE,"missing frame cannot steer from history");
    lane(1,70,1,0,0);process(270);
    CHECK(result.curve_outer_side==TRACK_SIDE_NONE,"missing frame breaks consecutive confirmation");
    memset(image,0,sizeof image);process(300);
    CHECK(!result.valid && result.curve_outer_side==TRACK_SIDE_NONE,"black board stays invalid");
    memset(image,255,sizeof image);process(330);
    CHECK(!result.valid && result.curve_outer_side==TRACK_SIDE_NONE,"white board stays invalid");
    /* 近场全部遮挡，即使远处仍有外边线也不得提供可行驶路径。 */
    track_vision_reset();lane(-1,70,1,0,0);memset(image+80*188,25,40*188);
    process(0);process(33);process(66);
    CHECK(!result.valid,"blind near field remains invalid");
    /* 外侧只有零碎行可见，不得累计成连续外侧确认。 */
    track_vision_reset();lane(1,70,1,0,0);
    for(direction=30;direction<=116;direction+=3)memset(image+direction*188,220,188);
    process(0);process(33);process(66);
    CHECK(result.curve_outer_side==TRACK_SIDE_NONE,"fragmented outer cannot enable preference");
    if(VISION_CURVE_OUTER_ENABLED) {
        int missing;
        for(missing=6;missing<=7;++missing) {
            track_vision_reset();lane(1,70,1,0,0);
            memset(image+86*188,220,missing*188);
            process(0);process(33);process(66);
            CHECK(result.curve_outer_near_rows==23-missing,"near coverage counts only measured outer");
            CHECK((result.curve_outer_reject==TRACK_OUTER_REJECT_NONE)==(missing==6),
                  "17 near rows pass coverage and 16 fail; path clearance is checked separately");
            if(missing==7)CHECK(result.curve_outer_reject==TRACK_OUTER_REJECT_NEAR,"near rejection is reported");
        }
        for(missing=13;missing<=14;++missing) {
            track_vision_reset();lane(1,70,1,0,0);
            memset(image+45*188,25,missing*188);
            process(0);process(33);process(66);
            unsigned measured=0;int row;
            for(row=result.preview_first_row;row<=result.preview_last_row;++row)
                if((result.row_flags[row]&TRACK_ROW_LEFT_MEASURED) && (result.row_flags[row]&TRACK_ROW_PATH_VALID))++measured;
            CHECK(result.curve_outer_far_rows==measured,"far coverage counts current measured outer in the selected window");
            CHECK(result.preview_last_row<86 && result.curve_outer_supported,"visible closer preview remains separate from near field");
        }
        track_vision_reset();lane(1,70,1,0,0);
        memset(image+45*188,25,13*188);memset(image+30*188,25,4*188);
        process(0);process(33);process(66);
        CHECK(result.valid && result.confidence>=70 && result.curve_outer_supported,
              "current continuous corridor has margin without pretending the inner edge was measured");
        memset(image+45*188,25,41*188);process(100);
        CHECK(!result.valid && !result.curve_outer_supported,"black forward field cannot be replaced by near support");
    }
    printf("Curve outer: %u passed, %u failed (%u total)\n",checks-failures,failures,checks);
    return failures?1:0;
}
