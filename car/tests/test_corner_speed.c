#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "autonomous_drive.h"
#include "autonomous_config.h"
static autonomous_drive_t d;static autonomous_drive_input_t i;
static void ticks(unsigned count){while(count--){i.now_ms+=10;i.frame_ms=i.now_ms;++i.frame_sequence;++i.feedback_sequence;i.servo_generation=d.status.run_generation;autonomous_drive_tick(&d,&i);}}
static void target(float value){ticks(1000);printf("element=%d target=%.2f expected=%.2f\n",i.element,d.status.target_speed_mm_s,value);fflush(stdout);assert(d.status.active && d.status.fault==0);assert(fabsf(d.status.target_speed_mm_s-value)<3);}
int main(void){autonomous_drive_init(&d);memset(&i,0,sizeof i);i.ready=i.has_path=1;i.zebra_near_row=255;i.left_speed_mm_s=i.right_speed_mm_s=500;
ticks(5);i.start_pressed=1;ticks(4);i.start_pressed=0;
i.geometry.valid=1;i.geometry.target_forward_mm=400;
i.element=TRACK_ELEMENT_CROSS;target(fminf(AUTO_CRUISE_SPEED_MM_S,AUTO_ELEMENT_SPEED_MM_S));
i.element=TRACK_ELEMENT_STRAIGHT;target(AUTO_CRUISE_SPEED_MM_S);
i.element=TRACK_ELEMENT_CURVE;target(fminf(AUTO_CRUISE_SPEED_MM_S,AUTO_CURVE_SPEED_MM_S));
i.curve_outer_side=TRACK_SIDE_LEFT;target(fminf(AUTO_CRUISE_SPEED_MM_S,fminf(AUTO_CURVE_SPEED_MM_S,AUTO_CURVE_OUTER_SPEED_MM_S)));
i.curve_outer_side=TRACK_SIDE_RIGHT;target(fminf(AUTO_CRUISE_SPEED_MM_S,fminf(AUTO_CURVE_SPEED_MM_S,AUTO_CURVE_OUTER_SPEED_MM_S)));
i.curve_outer_side=TRACK_SIDE_NONE;
for(unsigned e=TRACK_ELEMENT_ROUNDABOUT_APPROACH;e<=TRACK_ELEMENT_ROUNDABOUT_EXIT;++e){i.element=e;target(fminf(AUTO_CRUISE_SPEED_MM_S,AUTO_ELEMENT_SPEED_MM_S));}
i.element=TRACK_ELEMENT_STRAIGHT;i.geometry.target_right_mm=160;i.geometry.target_forward_mm=sqrtf(400*400-160*160);target(fminf(AUTO_CRUISE_SPEED_MM_S,sqrtf(AUTO_LATERAL_ACCEL_MM_S2*500)));
i.geometry.target_right_mm=-160;target(fminf(AUTO_CRUISE_SPEED_MM_S,sqrtf(AUTO_LATERAL_ACCEL_MM_S2*500)));
i.element=TRACK_ELEMENT_CURVE;i.geometry.target_right_mm=400*400/(2.0f*300);i.geometry.target_forward_mm=sqrtf(400*400-i.geometry.target_right_mm*i.geometry.target_right_mm);target(fminf(AUTO_CRUISE_SPEED_MM_S,fminf(AUTO_CURVE_SPEED_MM_S,sqrtf(AUTO_LATERAL_ACCEL_MM_S2*300))));
i.element=TRACK_ELEMENT_STRAIGHT;i.geometry.target_right_mm=0;i.geometry.target_forward_mm=400;target(AUTO_CRUISE_SPEED_MM_S);
i.zebra_visible=1;target(AUTO_CRUISE_SPEED_MM_S);i.zebra_visible=0;
i.geometry.valid=0;i.has_path=0;target(AUTO_CRUISE_SPEED_MM_S);
i.left_speed_mm_s=4001;ticks(1);assert(!d.status.active && d.status.fault==AUTO_FAULT_OVERSPEED && !d.status.left_duty_permille && !d.status.right_duty_permille);
puts("corner targets, lateral cap, release, no vision stop and hard fault: PASS");return 0;}
