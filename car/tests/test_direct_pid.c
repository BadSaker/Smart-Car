/* 真实控制器：目标与转向直接执行，现有硬故障保持。 */
#include "autonomous_drive.h"
#include "autonomous_config.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
static autonomous_drive_t d;
static autonomous_drive_input_t i;
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){printf("line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static void tick(void){i.now_ms+=10;i.frame_ms=i.now_ms;++i.frame_sequence;++i.feedback_sequence;i.servo_generation=d.status.run_generation;autonomous_drive_tick(&d,&i);}
static void start(void){autonomous_drive_init(&d);memset(&i,0,sizeof i);i.ready=i.has_path=1;i.zebra_near_row=255;i.left_speed_mm_s=i.right_speed_mm_s=1200;for(int n=0;n<5;n++)tick();i.start_pressed=1;for(int n=0;n<4;n++)tick();i.start_pressed=0;while(d.status.state==AUTO_DRIVE_STARTING)tick();CHECK(d.status.active);}
int main(int argc,char **argv){
 start();
 if(argc>1 && strcmp(argv[1],"steering")==0){i.near_error_px=i.far_error_px=10;tick();CHECK(d.status.steering_permille==-(int)(10*AUTO_STEERING_KP));}
 else {
 float step=AUTO_ACCEL_MM_S2>0?AUTO_ACCEL_MM_S2*.01f:AUTO_CRUISE_SPEED_MM_S;
 CHECK(fabsf(d.status.left_target_mm_s-step)<.01f);
 for(int n=0;n<150;n++){
  float left=d.status.left_target_mm_s,right=d.status.right_target_mm_s;
  i.near_error_px=i.far_error_px=n>40?20:0;tick();
  CHECK(d.status.left_target_mm_s-left<=step+.01f);
  CHECK(d.status.right_target_mm_s-right<=step+.01f);
 }
 i.stop_pressed=1;tick();CHECK(!d.status.active && !d.status.left_duty_permille && !d.status.right_duty_permille);
}
 printf("%u direct control checks passed\n",checks);return 0;
}
