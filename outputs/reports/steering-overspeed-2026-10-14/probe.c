#include "autonomous_drive.h"
#include "control_pid.h"
#include <string.h>
static autonomous_drive_t car;
static autonomous_drive_input_t input;
static float left_target,right_target,left_request,right_request;
static float observed_step(control_pid_t *pid,float target,float measurement,float ff,float dt)
{
    float output=control_pid_step(pid,target,measurement,ff,dt);
    if(pid==&car.left_pid){left_target=target;left_request=output;}
    if(pid==&car.right_pid){right_target=target;right_request=output;}
    return output;
}
#define control_pid_step observed_step
#include "autonomous_drive.c"
#undef control_pid_step

__declspec(dllexport) void reset(void)
{
    autonomous_drive_init(&car);memset(&input,0,sizeof input);
    input.ready=input.vision_valid=input.vision_near_supported=1;input.confidence=95;
    left_target=right_target=left_request=right_request=0;
}
__declspec(dllexport) void step(unsigned ms,int geometry,float x,float y,int near_px,int far_px,
                               int left,int right,int valid,float *result)
{
    input.now_ms=input.servo_service_ms=ms;++input.feedback_sequence;
    if(ms%20==0){++input.frame_sequence;input.frame_ms=ms;}
    input.servo_generation=car.status.run_generation;
    input.start_pressed=ms>=160 && ms<240;
    input.geometry.valid=(unsigned char)geometry;input.geometry.target_right_mm=x;input.geometry.target_forward_mm=y;
    input.near_error_px=(int16_t)near_px;input.far_error_px=(int16_t)far_px;
    input.left_speed_mm_s=left;input.right_speed_mm_s=right;input.vision_valid=(unsigned char)valid;
    autonomous_drive_tick(&car,&input);
    result[0]=car.status.steering_permille;result[1]=car.status.target_speed_mm_s;
    result[2]=left_target;result[3]=right_target;
    result[4]=car.status.left_duty_permille;result[5]=car.status.right_duty_permille;
    result[6]=car.left_pid.integral;result[7]=car.right_pid.integral;
    result[8]=left_request;result[9]=right_request;
    result[10]=car.status.fault;result[11]=car.status.state;
}
