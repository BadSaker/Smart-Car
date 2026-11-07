/* 用确定性时间和传感器输入验证真实控制状态机，不复制实现算法。 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "autonomous_drive.h"
#include "autonomous_config.h"
#include "vision_config.h"
static autonomous_drive_t car;
static autonomous_drive_input_t in;
static unsigned checks, simulated_ticks;
#define CHECK(c) do { ++checks; if (!(c)) { printf("line %d\n",__LINE__); fflush(stdout); exit(1); } } while(0)
static void tick(unsigned n, int camera, int feedback, int servo)
{
    while(n--) {
        in.now_ms += 10U; ++simulated_ticks;
        if (feedback) ++in.feedback_sequence;
        if (camera && (simulated_ticks % 2U == 0U)) { ++in.frame_sequence; in.frame_ms=in.now_ms; }
        if(servo) { in.servo_generation=car.status.run_generation; }
        autonomous_drive_tick(&car,&in);
        CHECK(car.status.left_duty_permille <= AUTO_MAX_DUTY_PERMILLE);
        CHECK(car.status.right_duty_permille <= AUTO_MAX_DUTY_PERMILLE);
    }
}
static void reset(void)
{
    simulated_ticks=0; autonomous_drive_init(&car); memset(&in,0,sizeof(in));
    in.ready=1;in.has_path=1;in.zebra_near_row=255;
    in.left_speed_mm_s=150;in.right_speed_mm_s=150;
    in.forward_step_um=1500;
    tick(5,1,1,1);
}
static void start(void)
{
    in.start_pressed=1;tick(4,1,1,1);in.start_pressed=0;
    tick(180,1,1,1); CHECK(car.status.state==AUTO_DRIVE_RUNNING);
}
static void fault(autonomous_fault_t why)
{
    CHECK(car.status.state==AUTO_DRIVE_FAULT); CHECK(car.status.fault==why);
    CHECK(car.status.left_duty_permille==0 && car.status.right_duty_permille==0);
}
static void test_direct_control(void)
{
    reset();start();CHECK(car.status.target_speed_mm_s==AUTO_CRUISE_SPEED_MM_S);
    for(int side=-1;side<=1;++side) {
        in.near_error_px=in.far_error_px=(int16_t)(side*10);in.has_path=1;tick(40,1,1,1);
        CHECK(car.status.steering_permille==-(int)(10*AUTO_STEERING_KP)*side);
        CHECK(car.status.target_speed_mm_s==AUTO_CRUISE_SPEED_MM_S);
        CHECK(car.status.left_duty_permille==(uint16_t)car.status.left_pid_request);
        CHECK(car.status.right_duty_permille==(uint16_t)car.status.right_pid_request);
        in.has_path=0;tick(2,1,1,1);CHECK(car.status.steering_permille==-(int)(10*AUTO_STEERING_KP)*side);
        CHECK(car.status.target_speed_mm_s==AUTO_CRUISE_SPEED_MM_S);
    }
}
static void test_blocked_main_loop(void)
{
    const unsigned pauses[]={10,30,100};unsigned k;
    for(k=0;k<sizeof pauses/sizeof pauses[0];++k){
        uint32_t sequence,stamp;
        reset();start();sequence=in.frame_sequence;stamp=in.frame_ms;
        tick(pauses[k],0,1,0);
        CHECK(car.status.state==AUTO_DRIVE_RUNNING && car.status.fault==AUTO_FAULT_NONE);
        CHECK(in.frame_sequence==sequence && in.frame_ms==stamp);
        tick(2,1,1,1);CHECK(car.status.active && in.frame_sequence!=sequence);
        /* 真实编码器超时和停车仍生效。 */
        tick(5,0,0,0);fault(AUTO_FAULT_ENCODER_STALE);
    }
    reset();tick(2,1,1,1);tick(100,0,1,0);in.start_pressed=1;tick(4,0,1,0);
    CHECK(car.status.state==AUTO_DRIVE_STARTING);
    tick(60,0,1,0);CHECK(car.status.state==AUTO_DRIVE_STARTING);
    CHECK(car.status.left_duty_permille==0 && car.status.right_duty_permille==0);
    tick(1,0,1,1);CHECK(car.status.state==AUTO_DRIVE_RUNNING);
    in.stop_pressed=1;tick(1,0,1,0);CHECK(!car.status.active);
}
int main(void)
{
    test_blocked_main_loop();
#ifdef TEST_TIMING_POLICY
    test_direct_control();
    printf("%u timing policy checks passed\n",checks);return 0;
#endif
    test_direct_control();
    reset(); CHECK(!car.status.active);in.start_pressed=1;in.stop_pressed=1;tick(8,1,1,1);
    CHECK(!car.status.active);in.stop_pressed=0;tick(20,1,1,1);CHECK(!car.status.active);
    in.start_pressed=0;tick(5,1,1,1);start();CHECK(car.status.active);
    in.stop_pressed=1;tick(1,1,1,1);CHECK(!car.status.active);CHECK(car.status.left_duty_permille==0);
    reset();start();in.cancel_pressed=1;tick(1,1,1,1);CHECK(!car.status.active);
    reset();in.remote_busy=1;in.start_pressed=1;tick(5,1,1,1);CHECK(!car.status.active);
    reset();start();tick(20,0,1,1);CHECK(car.status.active);
    tick(50,1,1,1);CHECK(car.status.active);
    reset();start();tick(20,1,0,1);fault(AUTO_FAULT_ENCODER_STALE);
    reset();start();tick(20,1,1,0);CHECK(car.status.active);
    reset();start();in.has_path=0;tick(8,1,1,1);CHECK(car.status.active && car.status.target_speed_mm_s==AUTO_CRUISE_SPEED_MM_S);
    reset();start();in.left_speed_mm_s=-100;in.forward_step_um=0;tick(20,1,1,1);fault(AUTO_FAULT_ENCODER_DIRECTION);
    reset();start();in.left_speed_mm_s=AUTO_MAX_MEASURED_SPEED_MM_S+1;tick(1,1,1,1);fault(AUTO_FAULT_OVERSPEED);
    reset();start();in.left_speed_mm_s=0;in.forward_step_um=0;tick(250,1,1,1);fault(AUTO_FAULT_STALL);
    reset();start();in.near_error_px=25;in.far_error_px=30;tick(10,1,1,1);
    CHECK(car.status.steering_permille<0);CHECK(car.status.target_speed_mm_s<=AUTO_CRUISE_SPEED_MM_S);
    reset();start();in.left_speed_mm_s=(int)AUTO_CRUISE_SPEED_MM_S;in.right_speed_mm_s=(int)AUTO_CRUISE_SPEED_MM_S;tick(100,1,1,1);
    in.near_error_px=25;in.far_error_px=30;tick(30,1,1,1);
    CHECK(car.status.left_duty_permille>car.status.right_duty_permille);
    reset();in.now_ms=0xfffffe00U;tick(5,1,1,1);start();CHECK(car.status.active);
    /* 同一次起跑斑马线连续可见不能算第二圈；离开且足够里程后才允许终点。 */
    reset();start();in.zebra=1;in.zebra_near_row=117;tick(10,1,1,1);CHECK(car.status.start_marker_seen);
    tick(30,1,1,1);CHECK(car.status.state==AUTO_DRIVE_RUNNING);
    in.zebra=0;tick(4000,1,1,1);CHECK(car.status.state==AUTO_DRIVE_RUNNING);
    in.zebra=1;tick(10,1,1,1);CHECK(car.status.state==AUTO_DRIVE_FINISHING);
    in.zebra=0;in.forward_step_um=1000;
    while(car.status.finish_advance_mm<AUTO_FINISH_ADVANCE_MM-1)tick(1,1,1,1);
    CHECK(car.status.state==AUTO_DRIVE_FINISHING);
    tick(1,1,1,1);CHECK(car.status.state==AUTO_DRIVE_FINISHED);
    CHECK(car.status.left_duty_permille==0);tick(50,1,1,1);CHECK(!car.status.active);
    reset();start();in.zebra=1;tick(8,1,1,1);in.zebra=0;
    while(car.status.distance_mm-car.marker_distance_mm<4980)tick(1,1,1,1);
    in.zebra=1;tick(6,1,1,1);CHECK(car.status.state==AUTO_DRIVE_RUNNING);
    tick(20,1,1,1);CHECK(car.status.state==AUTO_DRIVE_FINISHING);
    /* 同帧和同一反馈序号不能制造新证据或虚增里程。 */
    reset();start();{uint32_t distance=car.status.distance_mm;tick(2,0,0,1);CHECK(car.status.distance_mm==distance);}
    in.start_pressed=1;tick(4,1,1,1);CHECK(!car.status.active);
    reset();start();in.right_speed_mm_s=-100;tick(20,1,1,1);fault(AUTO_FAULT_ENCODER_DIRECTION);
    reset();start();in.right_speed_mm_s=AUTO_MAX_MEASURED_SPEED_MM_S+1;tick(1,1,1,1);fault(AUTO_FAULT_OVERSPEED);
    reset();start();car.left_pid.config.kp=0.0f/0.0f;tick(1,1,1,1);fault(AUTO_FAULT_CONFIG);
    reset();start();in.right_speed_mm_s=0;in.forward_step_um=0;tick(250,1,1,1);fault(AUTO_FAULT_STALL);
    reset();start();in.now_ms=car.started_ms+AUTO_MAX_RUN_TIME_MS+1;tick(1,1,1,1);fault(AUTO_FAULT_RUN_TIMEOUT);
    for(int side=0;side<2;++side)for(int sign=-1;sign<=1;sign+=2){reset();start();if(side)in.right_speed_mm_s=sign*4001;else in.left_speed_mm_s=sign*4001;tick(1,1,1,1);fault(AUTO_FAULT_OVERSPEED);}
    printf("%u autonomous controller checks passed\n",checks);return 0;
}
