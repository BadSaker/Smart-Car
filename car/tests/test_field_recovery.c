#include "autonomous_drive.h"
#include "autonomous_config.h"
#include "vision_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static autonomous_drive_t d;
static autonomous_drive_input_t i;
static unsigned checks;
#define CHECK(c) do {++checks;if(!(c)){printf("field recovery line %d state=%d fault=%d\n",__LINE__,d.status.state,d.status.fault);exit(1);}}while(0)
static void tick(int frame,int feedback)
{
    i.now_ms+=10;
    if(frame){++i.frame_sequence;i.frame_ms=i.now_ms;}
    if(feedback)++i.feedback_sequence;
    i.servo_generation=d.status.run_generation;
    autonomous_drive_tick(&d,&i);
}
static void start(void)
{
    unsigned n;
    autonomous_drive_init(&d);memset(&i,0,sizeof i);
    i.ready=i.vision_valid=i.vision_near_supported=1;
    i.confidence=90;i.observation_confidence=65;
    i.left_speed_mm_s=i.right_speed_mm_s=1000;i.zebra_near_row=255;
    for(n=0;n<5;++n)tick(1,1);
    i.start_pressed=1;for(n=0;n<4;++n)tick(1,1);
    i.start_pressed=0;for(n=0;n<300;++n)tick(1,1);
    CHECK(d.status.active && d.status.state==AUTO_DRIVE_RUNNING);
}
static void test_speed_cut_and_recovery(void)
{
    unsigned wheel,n;
    for(wheel=0;wheel<2;++wheel) {
        uint32_t distance;uint16_t other;
        start();distance=d.status.distance_mm;
        other=wheel?d.status.left_duty_permille:d.status.right_duty_permille;
        i.forward_step_um=50000;
        if(wheel)i.right_speed_mm_s=2501;else i.left_speed_mm_s=2501;
        tick(1,1);
        CHECK(d.status.active && d.status.fault==AUTO_FAULT_NONE);
        CHECK(d.status.speed_limited==(1U<<wheel));
        CHECK((wheel?d.status.right_duty_permille:d.status.left_duty_permille)==0);
        CHECK((wheel?d.status.left_duty_permille:d.status.right_duty_permille)<=other);
        CHECK(d.status.distance_mm==distance && !d.status.odometry_trusted);
        if(wheel)i.right_speed_mm_s=2200;else i.left_speed_mm_s=2200;
        tick(1,1);tick(1,0);
        CHECK(d.status.speed_limited==(1U<<wheel));
        tick(1,1);CHECK(d.status.speed_limited==(1U<<wheel));
        tick(1,1);CHECK(!d.status.speed_limited && !d.status.odometry_trusted);
        CHECK((wheel?d.status.right_duty_permille:d.status.left_duty_permille)<=AUTO_DUTY_STEP_PER_TICK);
        CHECK(d.status.distance_mm==distance);
        tick(1,1);CHECK(d.status.odometry_trusted && d.status.distance_mm==distance+50);
        for(n=0;n<3;++n)tick(1,1);
        CHECK(d.status.active);
    }
    start();i.left_speed_mm_s=AUTO_MAX_MEASURED_SPEED_MM_S;tick(1,1);
    CHECK(d.status.active && d.status.left_duty_permille==0);
    i.left_speed_mm_s=AUTO_MAX_MEASURED_SPEED_MM_S+1;tick(1,1);
    CHECK(!d.status.active && d.status.fault==AUTO_FAULT_OVERSPEED);
    CHECK(!d.status.left_duty_permille && !d.status.right_duty_permille);
    start();i.right_speed_mm_s=-2501;tick(1,1);
    CHECK(d.status.active && d.status.right_duty_permille==0);
    i.stop_pressed=1;tick(1,1);CHECK(d.status.state==AUTO_DRIVE_STOPPED);
}
static void test_weak_path_and_live_q(void)
{
    unsigned n;int16_t last;
    start();last=d.status.requested_steering_permille;
    i.vision_valid=0;i.vision_near_supported=0;i.confidence=0;
    i.geometry.valid=1;i.geometry.target_right_mm=NAN;i.geometry.target_forward_mm=300;
    {uint16_t before=d.status.left_duty_permille;tick(1,1);
     CHECK(d.status.left_duty_permille+AUTO_DUTY_FALL_STEP_PER_TICK>=before);}
    i.left_speed_mm_s=i.right_speed_mm_s=150;
    for(n=0;n<1700;++n)tick(1,1);
    CHECK(d.status.active && d.status.fault==AUTO_FAULT_NONE && d.status.recovering_path);
    CHECK(d.status.speed_limit_mm_s<=AUTO_RECOVERY_SPEED_MM_S);
    CHECK(d.status.target_speed_mm_s<=AUTO_RECOVERY_SPEED_MM_S+.1f);
    CHECK(d.status.requested_steering_permille==last);
    CHECK(isfinite(d.status.left_pid_request) && isfinite(d.status.right_pid_request));
    CHECK(d.status.left_duty_permille>0 && d.status.right_duty_permille>0);
    i.geometry.valid=0;i.vision_valid=i.vision_near_supported=1;i.confidence=88;
    i.observation_confidence=53;tick(1,1);
    CHECK(!d.status.recovering_path && d.status.confidence==88 && d.status.live_confidence==88);
    CHECK(d.status.live_observation_confidence==53);
    i.element_fault=1;i.geometry_reject=2;for(n=0;n<20;++n)tick(1,1);
    CHECK(d.status.active && d.status.recovering_path);
    i.stop_pressed=1;tick(1,1);CHECK(!d.status.active && !d.status.left_duty_permille);
}
static void test_button_profile(void)
{
    unsigned n;start();i.stop_pressed=1;
    for(n=0;n<500;++n)tick(1,1);
    CHECK(!d.status.active && d.status.state==AUTO_DRIVE_STOPPED);
    CHECK(!d.status.left_duty_permille && !d.status.right_duty_permille);
    i.stop_pressed=0;i.vision_valid=i.vision_near_supported=i.confidence=0;
    for(n=0;n<5;++n)tick(1,1);
    i.start_pressed=1;for(n=0;n<4;++n)tick(1,1);
    i.start_pressed=0;for(n=0;n<60;++n)tick(1,1);
    CHECK(d.status.active && d.status.state==AUTO_DRIVE_RUNNING && d.status.recovering_path);
    CHECK(d.status.speed_limit_mm_s<=AUTO_RECOVERY_SPEED_MM_S);
}
static void return_marker(void)
{
    unsigned n;start();
    i.zebra=i.zebra_visible=1;i.zebra_near_row=117;
    for(n=0;n<3;++n)tick(1,1);
    CHECK(d.status.start_marker_seen);
    i.zebra=i.zebra_visible=0;i.zebra_near_row=255;i.forward_step_um=10000;
    for(n=0;n<810;++n)tick(1,1);
    CHECK(d.status.active && d.status.target_speed_mm_s>1000);
    i.zebra=i.zebra_visible=1;i.zebra_near_row=117;
    for(n=0;n<3;++n)tick(1,1);
    CHECK(d.status.state==AUTO_DRIVE_FINISHING);
    CHECK(d.status.speed_limit_mm_s>AUTO_FINISH_SPEED_MM_S);
}
static void test_finish_brakes_from_clear_tail(void)
{
    unsigned n;float previous;uint32_t distance;
    return_marker();
    for(n=0;n<6;++n)tick(1,1);
    CHECK(!d.status.finish_exit_confirmed && d.status.finish_advance_mm==0);
    i.zebra=i.zebra_visible=0;i.zebra_near_row=255;
    for(n=0;n<5;++n)tick(1,1);
    CHECK(d.status.finish_exit_confirmed && d.status.finish_advance_mm==40);
    previous=d.status.speed_limit_mm_s;
    while(d.status.active) {
        tick(1,1);
        CHECK(d.status.speed_limit_mm_s<=previous+.01f);
        previous=d.status.speed_limit_mm_s;
        CHECK(d.status.finish_advance_mm<=AUTO_FINISH_ADVANCE_MM);
    }
    CHECK(d.status.state==AUTO_DRIVE_FINISHED && d.status.finish_advance_mm==AUTO_FINISH_ADVANCE_MM);
    CHECK(!d.status.left_duty_permille && !d.status.right_duty_permille);
    return_marker();i.zebra=i.zebra_visible=0;i.zebra_near_row=255;
    tick(1,1);distance=d.status.distance_mm;
    i.left_speed_mm_s=3000;
    for(n=0;n<8;++n)tick(1,1);
    CHECK(d.status.distance_mm==distance && !d.status.finish_exit_confirmed);
    return_marker();i.zebra=i.zebra_visible=0;i.zebra_near_row=255;
    for(n=0;n<5;++n)tick(1,1);
    for(n=0;n<26 && d.status.active;++n)tick(0,1);
    CHECK(d.status.active && d.status.state==AUTO_DRIVE_FINISHING && d.status.fault==AUTO_FAULT_NONE);
}
static void test_continuous_quality(void)
{
    static uint8_t gray[188*120];track_vision_result_t v;int row,col;
    memset(gray,20,sizeof gray);
    for(row=0;row<120;++row) {
        int width=44+(row-30)*123/86;
        for(col=93-width/2;col<=93+width/2;++col)if(col>=0 && col<188)gray[row*188+col]=220;
    }
    /* 局部外边缺失但当前另一边和路面仍存在，不应只输出100或0。 */
    for(row=46;row<64;++row)for(col=0;col<94;++col)gray[row*188+col]=220;
    track_vision_reset();track_vision_process(gray,0,0,&v);
    CHECK(v.observation_confidence>0 && v.observation_confidence<100);
    CHECK(v.confidence>0 && v.confidence<100);
    memset(gray,20,sizeof gray);track_vision_process(gray,20,0,&v);
    CHECK(!v.valid && v.confidence==0);
}
int main(void)
{
    test_speed_cut_and_recovery();test_weak_path_and_live_q();test_button_profile();
    test_finish_brakes_from_clear_tail();test_continuous_quality();
    printf("field recovery: %u checks passed\n",checks);return 0;
}
