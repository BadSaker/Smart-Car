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
    result.has_path,result.near_error_px,result.far_error_px,result.curve_outer_side); } } while(0)

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
    int row;
    track_side_t outer=direction>0?TRACK_SIDE_LEFT:TRACK_SIDE_RIGHT;
    track_vision_reset();lane(direction,70,1,fake,stripe);process(0);process(33);process(66);
    CHECK(result.has_path,"single continuous outer supports driving");
    CHECK(result.element==TRACK_ELEMENT_CURVE,"ordinary bend remains a curve");
    if(VISION_CURVE_OUTER_ENABLED) {
        CHECK(result.curve_outer_side==outer,"confirmed direction selects correct outer");
        for(row=45;row<=70;++row) if(!stripe || row<55 || row>57) {
            uint8_t measured=direction>0?TRACK_ROW_LEFT_MEASURED:TRACK_ROW_RIGHT_MEASURED;
            uint8_t inner=direction>0?TRACK_ROW_RIGHT_MEASURED:TRACK_ROW_LEFT_MEASURED;
            CHECK((result.row_flags[row]&measured) && !(result.row_flags[row]&inner),"fake inner cannot count as measured");
            CHECK(result.row_flags[row]&TRACK_ROW_RECONSTRUCTED,"outer path is marked reconstructed");
            CHECK(image[row*188+result.center[row]]>result.threshold,"reconstructed path remains in current visible road");
        }
    } else CHECK(result.curve_outer_side==TRACK_SIDE_NONE,"configuration can disable preference");
    CHECK(direction*result.near_error_px>0 && direction*result.far_error_px>0,"curve error has correct sign");
    lane(direction,0,0,0,0);process(200);
    CHECK(result.curve_outer_side==TRACK_SIDE_NONE,"two visible straight edges immediately release preference");
}

int main(void)
{
    int direction,fake,stripe;
    for(direction=-1;direction<=1;direction+=2)
        for(fake=0;fake<2;++fake)for(stripe=0;stripe<2;++stripe)test_outer(direction,fake,stripe);
    track_vision_reset();lane(1,70,1,0,0);process(0);
    int near=result.near_error_px,far=result.far_error_px;
    track_vision_process(NULL,260,0,&result);
    CHECK(!result.has_path && result.near_error_px==near && result.far_error_px==far,"missing frame holds observed direction");
    memset(image,0,sizeof image);process(300);CHECK(!result.has_path,"black frame has no observation");
    memset(image,255,sizeof image);process(330);CHECK(!result.has_path,"white frame has no observation");
    printf("Curve outer: %u passed, %u failed (%u total)\n",checks-failures,failures,checks);
    return failures?1:0;
}
