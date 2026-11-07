#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wave_telemetry.h"
#include "autonomous_control.h"
#include "motor_control.h"
#include "encoder_feedback.h"
#include "autonomous_config.h"
static uint32_t mask;
static autonomous_drive_status_t a;
static motor_control_status_t m;
static encoder_feedback_snapshot_t e;
uint32_t __get_PRIMASK(void){return mask;}
void __disable_irq(void){mask=1;}
void __set_PRIMASK(uint32_t v){mask=v;}
void autonomous_control_get_status(autonomous_drive_status_t *out){*out=a;}
void motor_control_get_status(motor_control_status_t *out){*out=m;}
void encoder_feedback_get_snapshot(encoder_feedback_snapshot_t *out){*out=e;}
int main(void){
    float d[8];unsigned n;
    wave_telemetry_reset();wave_telemetry_capture(1);assert(!wave_telemetry_pop(d));
    a.left_speed_mm_s=1234;a.right_speed_mm_s=-55;a.left_target_mm_s=800;a.right_target_mm_s=700;
    e.left.speed_mm_s=1234*AUTO_LEFT_ENCODER_SIGN;e.right.speed_mm_s=-55*AUTO_RIGHT_ENCODER_SIGN;
    a.left_duty_permille=999;a.right_duty_permille=888;a.state=AUTO_DRIVE_RUNNING;
    m.channel1_duty_permille=321;m.channel2_duty_permille=123;
    wave_telemetry_set_enabled(1);wave_telemetry_capture(0xfffffff0U);
    assert(wave_telemetry_pop(d));
#ifdef TEST_MOTOR1_LEFT
    assert(d[0]==321 && d[1]==123);
#else
    assert(d[0]==123 && d[1]==321); /* 已确认：M2/模块0左，M1/模块1右。 */
#endif
    assert(d[2]==1234 && d[3]==-55 && d[4]==800 && d[5]==700 && d[6]==0 && d[7]==2);
    wave_telemetry_capture(4);assert(wave_telemetry_pop(d));assert(d[6]==20);
    for(n=0;n<205;n++)wave_telemetry_capture(14+10*n);
    assert(wave_telemetry_queued()==200 && wave_telemetry_dropped()==5);
    for(n=0;n<200;n++){assert(wave_telemetry_pop(d));assert(d[6]==30+10*n);}
    assert(!wave_telemetry_pop(d));
    a.state=AUTO_DRIVE_FAULT;a.fault=AUTO_FAULT_OVERSPEED;m.channel1_duty_permille=m.channel2_duty_permille=0;
    e.left.speed_mm_s=12*AUTO_LEFT_ENCODER_SIGN;e.right.speed_mm_s=34*AUTO_RIGHT_ENCODER_SIGN;
    wave_telemetry_capture(2074);assert(wave_telemetry_pop(d));assert(d[0]==0 && d[1]==0 && d[7]==606);
    assert(d[2]==12 && d[3]==34); /* 停车后控制状态轮速可能冻结，波形仍采新编码器。 */
    wave_telemetry_capture(2084);wave_telemetry_set_enabled(0);
    assert(!wave_telemetry_queued());wave_telemetry_capture(2094);assert(!wave_telemetry_pop(d));
    mask=1;wave_telemetry_set_enabled(1);assert(mask==1);wave_telemetry_capture(3000);
    assert(wave_telemetry_pop(d) && mask==1);wave_telemetry_reset();assert(mask==1);
    assert(wave_telemetry_dropped()==0 && wave_telemetry_queued()==0);
    puts("PASS wave channels, final PWM, 200 point FIFO, gaps, rollover, zero PWM, PRIMASK");return 0;
}
