#include "wave_telemetry.h"
#include "autonomous_control.h"
#include "autonomous_config.h"
#include "motor_control.h"
#include "encoder_feedback.h"
#include "fsl_common.h"
#include <string.h>

static float samples[WAVE_TELEMETRY_CAPACITY][WAVE_TELEMETRY_CHANNELS];
static volatile uint16_t read_index,write_index,count;
static volatile uint32_t dropped;
static volatile uint8_t enabled,have_epoch;
static uint32_t epoch;

void wave_telemetry_reset(void)
{
    uint32_t mask=__get_PRIMASK();__disable_irq();
    enabled=have_epoch=0;read_index=write_index=count=0;dropped=0;
    __set_PRIMASK(mask);
}
void wave_telemetry_set_enabled(uint8_t value)
{
    uint32_t mask=__get_PRIMASK();__disable_irq();
    enabled=value?1U:0U;
    if(!enabled){dropped+=count;read_index=write_index=count=0;have_epoch=0;}
    __set_PRIMASK(mask);
}
void wave_telemetry_capture(uint32_t now_ms)
{
    autonomous_drive_status_t a;
    motor_control_status_t m;
    encoder_feedback_snapshot_t e;
    float *out;
    if(!enabled)return;
    if(!have_epoch){epoch=now_ms;have_epoch=1;}
    if(count==WAVE_TELEMETRY_CAPACITY){++dropped;return;}
    /* 唯一生产者位于PIT，两个状态源本拍已更新；主循环不能抢占PIT。 */
    autonomous_control_get_status(&a);motor_control_get_status(&m);
    encoder_feedback_get_snapshot(&e);
    out=samples[write_index];
#if AUTO_MOTOR1_IS_LEFT
    out[0]=(float)m.channel1_duty_permille;out[1]=(float)m.channel2_duty_permille;
#else
    out[0]=(float)m.channel2_duty_permille;out[1]=(float)m.channel1_duty_permille;
#endif
    out[2]=(float)e.left.speed_mm_s*AUTO_LEFT_ENCODER_SIGN;
    out[3]=(float)e.right.speed_mm_s*AUTO_RIGHT_ENCODER_SIGN;
    out[4]=a.left_target_mm_s;out[5]=a.right_target_mm_s;
    out[6]=(float)(uint32_t)(now_ms-epoch);out[7]=(float)((unsigned)a.state+100U*(unsigned)a.fault);
    write_index=(uint16_t)((write_index+1U)%WAVE_TELEMETRY_CAPACITY);++count;
}
uint8_t wave_telemetry_pop(float data[WAVE_TELEMETRY_CHANNELS])
{
    uint32_t mask;
    uint8_t result=0;
    if(!data)return 0;
    mask=__get_PRIMASK();__disable_irq();
    if(count){memcpy(data,samples[read_index],sizeof samples[0]);
        read_index=(uint16_t)((read_index+1U)%WAVE_TELEMETRY_CAPACITY);--count;result=1;}
    __set_PRIMASK(mask);return result;
}
uint32_t wave_telemetry_dropped(void){return dropped;}
uint16_t wave_telemetry_queued(void){return count;}
