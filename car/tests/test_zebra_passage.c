#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "track_vision.h"
#include "vision_config.h"
#include "autonomous_drive.h"
#include "autonomous_config.h"

static uint8_t image[188*120];
static track_vision_result_t result;
static unsigned checks,failures;
#define CHECK(c,label) do {++checks;if(!(c)){++failures;printf("FAIL %s line=%d Z=%u rows=%u near=%u V=%u Q=%u G=%u\n",label,__LINE__,result.zebra,result.zebra_rows,result.zebra_near_row,result.has_path,result.has_path,result.has_path);}}while(0)

/* 独立44/167px直道，条纹按道路横向比例绘制，留出真实道路边界白色间隔。
 * top/bottom是标记的投影范围；移到119覆盖底部，不能靠正常底部行锚定。 */
static void scene(int top,int bottom,int shift,int tilt,int kind)
{
    int row,col;
    memset(image,20,sizeof image);
    for(row=0;row<120;++row) {
        int width=44+(row-30)*123/86,center=93+shift;
        int left=center-width/2,right=center+width/2;
        for(col=left;col<=right;++col)if(col>=0 && col<188) {
            int across=(col-left)*1000/width;
            int slant=tilt*(col-center)/width;
            image[row*188+col]=220;
            if(row>=top+slant && row<=bottom+slant && across>=70 && across<=930) {
                if(kind==1 || (across/65)%2==0)image[row*188+col]=20;
            }
        }
    }
}
static void process(unsigned time,unsigned distance)
{track_vision_process(image,time,distance,&result);}

static void test_passage(void)
{
    int shift,tilt,bottom;
    for(shift=-10;shift<=10;shift+=10)for(tilt=-8;tilt<=8;tilt+=8) {
        track_vision_reset();
        for(bottom=55;bottom<=145;bottom+=3) {
            scene(bottom-30,bottom,shift,tilt,0);
            process(20U*(unsigned)(bottom-54),3U*(unsigned)(bottom-54));
            CHECK(result.has_path,"moving stripe retains usable path");
            CHECK(abs(result.near_error_px-shift)<=5,"stripe does not steer into white slot");
            if(bottom>=90 && bottom<=118)CHECK(result.zebra,"near stripe remains recognized");
        }
        scene(150,180,shift,tilt,0);process(3000,500);
        CHECK(result.has_path && !result.zebra,"clear road releases stripe evidence");
    }
    track_vision_reset();scene(86,119,0,0,0);process(0,0);
    CHECK(result.zebra && result.has_path,"start with stripe covering bottom");
    track_vision_reset();scene(50,119,0,0,0);process(0,0);
    CHECK(result.zebra && result.has_path,"long visible stripe retains true boundaries");
    /* 实际黑色道路边线是有限宽度；其外部蓝色背景可能在阈值上变为白色。 */
    {int border,background,row,col;
        for(border=4;border<=20;border+=4)for(background=153;background<=220;background+=67) {
            scene(50,119,0,0,0);
            for(row=0;row<120;++row) {
                int width=44+(row-30)*123/86;
                for(col=0;col<188;++col)
                    if(col<93-width/2-border || col>93+width/2+border)image[row*188+col]=(uint8_t)background;
            }
            track_vision_reset();process(0,0);
            CHECK(result.zebra && result.has_path,"finite black road border with bright outer background");
        }
    }
}
static void test_negatives(void)
{
    track_vision_reset();scene(85,119,0,0,1);process(0,0);
    CHECK(!result.zebra,"solid black block is not a crossing");
    track_vision_reset();scene(150,180,0,0,0);
    memset(image+85*188,20,20*188);process(0,0);
    CHECK(!result.zebra,"full black transverse band is rejected");
    memset(image,220,sizeof image);track_vision_reset();process(0,0);
    CHECK(!result.has_path,"white board not drivable");
    memset(image,20,sizeof image);track_vision_reset();process(0,0);
    CHECK(!result.has_path,"black board not drivable");

}
static autonomous_drive_t drive;
static autonomous_drive_input_t input;
static void control_frame(int zebra,int valid,int step)
{
    input.now_ms+=20;input.frame_ms=input.now_ms;++input.frame_sequence;++input.feedback_sequence;
    input.zebra=(uint8_t)zebra;input.has_path=(uint8_t)valid;
    input.zebra_near_row=zebra?117:255;
    input.forward_step_um=step;input.servo_generation=drive.status.run_generation;
    autonomous_drive_tick(&drive,&input);
}
static void control_start(void)
{
    unsigned n;
    autonomous_drive_init(&drive);memset(&input,0,sizeof input);input.ready=1;
    input.left_speed_mm_s=input.right_speed_mm_s=100;
    for(n=0;n<5;++n)control_frame(0,1,0);
    input.start_pressed=1;for(n=0;n<3;++n)control_frame(0,1,0);
    input.start_pressed=0;for(n=0;n<30;++n)control_frame(0,1,0);
    CHECK(drive.status.state==AUTO_DRIVE_RUNNING,"controller starts");
}
static void test_marker_control(void)
{
    unsigned n;
    control_start();
    control_frame(1,1,1000);
    CHECK(drive.status.target_speed_mm_s<=AUTO_FINISH_SPEED_MM_S,"first approach slows before near confirmation");
    control_frame(1,1,1000);control_frame(1,1,1000);
    CHECK(drive.status.start_marker_seen && drive.status.state==AUTO_DRIVE_RUNNING,"first marker continues");
    for(n=0;n<500;++n)control_frame(0,1,12000);
    for(n=0;n<3;++n)control_frame(1,1,1000);
    CHECK(drive.status.state==AUTO_DRIVE_FINISHING,"return locks finish reference");
    for(n=0;n<6;++n)control_frame(1,1,100000);
    CHECK(drive.status.state==AUTO_DRIVE_FINISHING,"visible stripe cannot be reported as completed crossing");
    for(n=0;n<5;++n)control_frame(0,1,1000);
    CHECK(drive.status.state==AUTO_DRIVE_FINISHING && drive.status.finish_advance_mm<10,
          "visible travel never counts toward clear-tail distance");
    for(n=0;n<AUTO_FINISH_ADVANCE_MM/10+1 && drive.status.active;++n)control_frame(0,1,10000);
    CHECK(drive.status.state==AUTO_DRIVE_FINISHED && !drive.status.active,"clear stripe and sufficient advance finish");
    CHECK(!drive.status.left_duty_permille && !drive.status.right_duty_permille,"finish removes both outputs");
    control_start();
    for(n=0;n<3;++n)control_frame(1,0,1000);
    CHECK(drive.status.active && drive.status.start_marker_seen,"stripe alone cannot authorize lap confirmation");
    control_start();input.zebra_visible=1;input.zebra_near_row=60;
    control_frame(0,1,1000);
    CHECK(drive.status.marker_phase==1 && !drive.status.start_marker_seen,"far candidate slows without counting start");
    CHECK(drive.status.target_speed_mm_s<=AUTO_FINISH_SPEED_MM_S,"far candidate already limits speed");
    input.now_ms+=100+1;++input.feedback_sequence;
    autonomous_drive_tick(&drive,&input);
    CHECK(drive.status.active && drive.status.fault==AUTO_FAULT_NONE,"frozen marker does not lock recognition stop");
    control_start();control_frame(1,1,1000);
    for(n=0;n<5;++n){input.now_ms+=10;++input.feedback_sequence;autonomous_drive_tick(&drive,&input);}
    CHECK(!drive.status.start_marker_seen,"repeated frame does not accumulate marker confirmation");
    for(n=0;n<160;++n)control_frame(1,1,10000);
    CHECK(drive.status.active && drive.status.fault==AUTO_FAULT_NONE,"persistent recognition does not lock stop");
    control_start();
    for(n=0;n<3;++n)control_frame(1,1,1000);
    for(n=0;n<500;++n)control_frame(0,1,12000);
    for(n=0;n<3;++n)control_frame(1,1,1000);
    for(n=0;n<5;++n)control_frame(0,1,1000);
    input.now_ms+=20;++input.feedback_sequence;input.forward_step_um=500000;
    autonomous_drive_tick(&drive,&input);
    CHECK(drive.status.state==AUTO_DRIVE_FINISHING,"distance alone on reused clear frame cannot trigger DONE");
    input.now_ms+=100+1;++input.feedback_sequence;input.forward_step_um=0;
    autonomous_drive_tick(&drive,&input);
    CHECK(drive.status.active && drive.status.state==AUTO_DRIVE_FINISHING,"frozen image cannot fake DONE or lock a recognition stop");
}

static void gray_control(unsigned step)
{
    process(input.now_ms+20,drive.status.distance_mm);
    input.zebra_visible=result.zebra;input.zebra_near_row=result.zebra_near_row;
    input.near_error_px=result.near_error_px;input.far_error_px=result.far_error_px;
    input.element=(uint8_t)result.element;
    input.now_ms+=20;input.frame_ms=input.now_ms;++input.frame_sequence;++input.feedback_sequence;
    input.servo_generation=drive.status.run_generation;
    input.has_path=result.has_path;
    input.zebra=result.zebra && result.zebra_near_row!=255 && result.zebra_near_row>=80;
    input.forward_step_um=(int32_t)step;
    autonomous_drive_tick(&drive,&input);
}
static void test_gray_control(void)
{
    int direction,bottom,n;
    for(direction=-1;direction<=1;direction+=2) {
        control_start();track_vision_reset();
        for(bottom=55;bottom<=150;++bottom) {
            scene(bottom-30,bottom,5*direction,8*direction,0);gray_control(3000);
            CHECK(drive.status.active && drive.status.state==AUTO_DRIVE_RUNNING,"first gray crossing keeps running");
        }
        CHECK(drive.status.start_marker_seen,"first gray crossing records start");
        scene(150,180,0,0,0);
        for(n=0;n<600;++n)gray_control(10000);
        CHECK(drive.status.state==AUTO_DRIVE_RUNNING,"clear lap continues");
        for(bottom=55;bottom<=150;++bottom) {
            scene(bottom-30,bottom,-5*direction,-8*direction,0);gray_control(3000);
            CHECK(drive.status.active,"return crosses stripe before stopping");
        }
        scene(150,180,0,0,0);
        for(n=0;n<(int)(AUTO_FINISH_ADVANCE_MM/3+10) && drive.status.active;++n)gray_control(3000);
        CHECK(drive.status.state==AUTO_DRIVE_FINISHED,"gray sequence reaches DONE after clear and advance");
    }
    /* 启动时已覆盖近场，没有可依赖的行驶历史。 */
    autonomous_drive_init(&drive);memset(&input,0,sizeof input);input.ready=1;
    input.left_speed_mm_s=input.right_speed_mm_s=100;track_vision_reset();
    scene(86,119,0,0,0);
    for(n=0;n<5;++n)gray_control(0);
    input.start_pressed=1;for(n=0;n<3;++n)gray_control(0);
    input.start_pressed=0;for(n=0;n<30;++n)gray_control(0);
    CHECK(drive.status.state==AUTO_DRIVE_RUNNING && drive.status.start_marker_seen,"close start establishes current-frame path");
}
int main(void)
{
    test_passage();test_negatives();test_marker_control();test_gray_control();
    printf("zebra passage: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
