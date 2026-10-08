/* 合成灰度帧验证真实扫线和连续元素状态，不直接注入内部特征。 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "track_vision.h"

static uint8_t image[188U * 120U];
static track_vision_result_t result;
static unsigned checks;
static unsigned failures;
#define CHECK(c, label) do { ++checks; if (!(c)) { ++failures; \
    printf("FAIL %s at %d (valid=%u confidence=%u element=%d side=%d near=%d far=%d fault=%u)\n", \
    label, __LINE__, result.valid, result.confidence, result.element, result.side, \
    result.near_error_px, result.far_error_px, result.fault); } } while (0)

/* 固定独立几何：30行宽36px，116行宽122px，中心93px。 */
static void lane(int bend, int shift, int dark, int bright)
{
    int row, col;
    memset(image, dark, sizeof(image));
    for (row = 0; row < 120; ++row) {
        int width = 6 + row;
        int center = 93 + shift + bend * (120-row) * (120-row) / 240;
        for (col = center - width/2; col <= center + width/2; ++col)
            if (col >= 0 && col < 188) image[row*188+col] = (uint8_t)bright;
    }
}

static void opening(int side, int top, int bottom)
{
    int row, col;
    for (row=top; row<=bottom; ++row) {
        if (side <= 0) for (col=0; col<93; ++col) image[row*188+col]=220;
        if (side >= 0) for (col=93; col<188; ++col) image[row*188+col]=220;
    }
}

static void frames(unsigned count, uint32_t now, uint32_t distance)
{
    unsigned i;
    for (i=0; i<count; ++i)
        track_vision_process(image, now+i*25U, distance+i*10U, &result);
}

static void test_lane_and_invalid(void)
{
    uint8_t before[sizeof(image)];
    int row;
    track_vision_reset();
    lane(0, 0, 20, 220); memcpy(before, image, sizeof(image));
    frames(1, 0, 0);
    CHECK(result.valid && result.confidence >= 80, "straight geometry has strong support");
    CHECK(abs(result.near_error_px)<=1 && abs(result.far_error_px)<=1, "straight centered errors");
    CHECK(memcmp(image, before, sizeof(image))==0, "gray buffer remains unchanged");
    for(row=0; row<120; ++row)
        CHECK(result.left[row]<188 && result.right[row]<188 && result.center[row]<188,
              "all coordinates stay in image");
    lane(0, 15, 20, 220); frames(1,100,50);
    CHECK(result.valid && result.near_error_px>=13, "right offset error has right-positive sign");
    lane(-1,0,20,220); frames(1,200,100);
    CHECK(result.valid && result.far_error_px < result.near_error_px-5, "left curve retains ahead geometry");
    CHECK(result.element==TRACK_ELEMENT_CURVE, "curve classification follows geometry");
    lane(0,0,55,150); frames(1,300,150);
    CHECK(result.valid && abs(result.near_error_px)<=1, "lower exposure remains usable");
    lane(0,0,20,220); memset(image+80U*188U,20,40U*188U); frames(1,350,175);
    CHECK(!result.valid, "distant road alone cannot validate a blind near field");
    memset(image,255,sizeof(image)); frames(1,400,200);
    CHECK(!result.valid, "white board never becomes a valid track");
    memset(image,0,sizeof(image)); frames(1,450,210);
    CHECK(!result.valid, "black board never becomes a valid track");
    track_vision_process(NULL,500,220,&result);
    CHECK(!result.valid && (result.fault & TRACK_FAULT_PATH_LOST), "missing gray input invalidates path");
    track_vision_process(image,550,230,NULL);
}

static void test_side_and_zebra(void)
{
    int row,col;
    track_vision_reset(); lane(0,0,20,220);
    opening(-1,0,119); frames(4,0,0);
    CHECK(result.valid && abs(result.near_error_px)<=4, "single right edge uses perspective half width");
    CHECK(result.element==TRACK_ELEMENT_STRAIGHT, "unbounded single side loss is not roundabout");
    track_vision_reset(); lane(0,0,20,220);
    for(row=45;row<80;++row) for(col=3;col<36;++col)
        if((row-62)*(row-62)+(col-19)*(col-19)<225) image[row*188+col]=220;
    frames(8,0,0);
    CHECK(result.element==TRACK_ELEMENT_STRAIGHT && abs(result.far_error_px)<=1,
          "separated roadside circle cannot attract path");
    track_vision_reset(); lane(0,0,20,220); opening(-1,47,81);
    memset(image+43U*188U,20,4U*188U); frames(6,100,20);
    CHECK(result.element==TRACK_ELEMENT_STRAIGHT,
          "black occlusion is not a recovered upper corner");
    track_vision_reset(); lane(0,0,20,220); opening(-1,47,81);
    for(row=47;row<=81;++row) if((row/3)%2==0)
        for(col=93+(6+row)/2-16;col<188;++col) image[row*188+col]=20;
    frames(6,100,20);
    CHECK(result.element!=TRACK_ELEMENT_ROUNDABOUT_APPROACH,
          "broken opposite boundary cannot confirm a ring entrance");
    track_vision_reset(); lane(0,0,20,220); opening(-1,47,81); frames(4,300,100);
    lane(0,0,20,220); frames(4,500,280);
    CHECK(result.element==TRACK_ELEMENT_STRAIGHT,
          "vanished approach recovers before committed entry");
    track_vision_reset();
    lane(0,0,20,220);
    for(row=87;row<=103;++row) for(col=42;col<=144;++col)
        if(((col-42)/7)%2==0) image[row*188+col]=20;
    frames(3,300,100);
    CHECK(result.zebra && result.zebra_near_row>=100, "multiple stripes report near zebra position");
    CHECK(result.valid && abs(result.near_error_px)<=3, "zebra stripes do not drag path into stripe edge");
    lane(0,0,20,220); frames(1,400,200);
    CHECK(!result.zebra, "zebra evidence is frame local");
}

static void test_cross(void)
{
    track_vision_reset(); lane(0,0,20,220); frames(1,0,0);
    opening(0,48,93); frames(4,100,50);
    CHECK(result.element==TRACK_ELEMENT_CROSS && result.valid, "paired side gaps enter bounded cross");
    CHECK(abs(result.near_error_px)<=3 && abs(result.far_error_px)<=3, "cross preserves incoming direction");
    lane(0,0,20,220); opening(0,60,112); frames(2,250,350);
    CHECK(result.element==TRACK_ELEMENT_CROSS && result.valid,
          "confirmed cross bridges near gap while measured far road remains");
    frames(1,500,1600);
    CHECK(!result.valid && (result.fault & TRACK_FAULT_ELEMENT_TIMEOUT), "cross cannot extend indefinitely");
    lane(0,0,20,220); frames(4,600,1650);
    CHECK(!result.valid, "element timeout stays latched until explicit reset");
    track_vision_reset(); frames(1,700,1700);
    CHECK(result.valid && result.fault==0, "reset releases latched element fault");
}

static void test_cross_visibility(void)
{
    track_vision_reset(); lane(0,0,20,220); opening(0,48,93); frames(3,100,50);
    CHECK(result.element==TRACK_ELEMENT_CROSS && result.valid,
          "visibility regression starts from a confirmed cross");
    lane(0,0,20,220); memset(image,20,85U*188U); frames(1,200,100);
    CHECK(!result.valid && (result.fault&TRACK_FAULT_PATH_LOST),
          "confirmed cross rejects a fully black far field despite usable near edges");
    CHECK(!(result.row_flags[50]&TRACK_ROW_PATH_VALID),
          "cross reconstruction does not invent a path through black far pixels");
    lane(0,0,20,220); memset(image+80U*188U,20,40U*188U); frames(1,250,110);
    CHECK(!result.valid, "confirmed cross still rejects a black near field");
    memset(image,255,sizeof(image)); frames(1,300,120);
    CHECK(!result.valid, "confirmed cross still rejects a white board");
    memset(image,0,sizeof(image)); frames(1,350,130);
    CHECK(!result.valid, "confirmed cross still rejects a black board");
    lane(0,0,20,220); opening(0,60,112); frames(1,400,140);
    CHECK(result.element==TRACK_ELEMENT_CROSS && result.valid,
          "visible far road and white near opening restore bounded cross path");
}

static void test_ring(int side)
{
    track_vision_reset(); lane(0,0,20,220); frames(1,0,0);
    opening(side,47,81); frames(4,100,50);
    CHECK(result.element==TRACK_ELEMENT_ROUNDABOUT_APPROACH, "bounded single opening confirms approach");
    CHECK(result.side==(side<0?TRACK_SIDE_LEFT:TRACK_SIDE_RIGHT), "roundabout side is explicit");
    CHECK(abs(result.far_error_px)<=3, "approach still follows incoming road");
    lane(0,0,20,220); opening(side,43,103); frames(3,250,200);
    CHECK(result.element==TRACK_ELEMENT_ROUNDABOUT_ENTER, "opening reaching near rows triggers entry");
    CHECK(side*result.far_error_px>8, "entry guides path toward confirmed branch");
    lane(-side,0,20,220); frames(4,360,430);
    CHECK(result.element==TRACK_ELEMENT_ROUNDABOUT_ENTER,
          "opposite curvature does not confirm inside");
    lane(side,0,20,220); frames(4,470,550);
    CHECK(result.element==TRACK_ELEMENT_ROUNDABOUT_INSIDE, "curved measured track confirms inside");
    lane(0,0,20,220); frames(6,650,1500);
    CHECK(result.element==TRACK_ELEMENT_ROUNDABOUT_INSIDE, "nearby straight path alone cannot trigger exit");
    lane(side,0,20,220); opening(-side,46,96); frames(4,900,1800);
    CHECK(result.element==TRACK_ELEMENT_ROUNDABOUT_EXIT, "new outer opening after ring travel permits exit");
    CHECK(side*result.far_error_px<4, "exit path guides toward the confirmed outer branch");
    lane(0,0,20,220); frames(4,1100,2130);
    CHECK(result.element==TRACK_ELEMENT_STRAIGHT && result.side==TRACK_SIDE_NONE,
          "exit recovers both stable boundaries");
    track_vision_reset(); lane(0,0,20,220); opening(side,47,81); frames(4,0,0);
    frames(1,61000,100);
    CHECK(!result.valid && (result.fault & TRACK_FAULT_ELEMENT_TIMEOUT), "roundabout stalled state times out");
}

int main(void)
{
    test_lane_and_invalid(); test_side_and_zebra(); test_cross(); test_cross_visibility();
    test_ring(-1); test_ring(1);
    printf("Track vision: %u passed, %u failed (%u total)\n",checks-failures,failures,checks);
    return failures?1:0;
}
