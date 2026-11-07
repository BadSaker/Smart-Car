#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "autonomous_drive.h"
#include "autonomous_config.h"
static autonomous_drive_t d;static autonomous_drive_input_t i;
static void tick(void){i.now_ms+=10;i.frame_ms=i.now_ms;++i.frame_sequence;++i.feedback_sequence;i.servo_generation=d.status.run_generation;autonomous_drive_tick(&d,&i);}
int main(void){autonomous_drive_init(&d);memset(&i,0,sizeof i);i.ready=i.has_path=1;i.left_speed_mm_s=i.right_speed_mm_s=600;
for(int n=0;n<200;++n){i.start_pressed=n>=5 && n<9;tick();}
i.near_error_px=i.far_error_px=-40;i.geometry.valid=1;i.geometry.target_right_mm=50;i.geometry.target_forward_mm=400;tick();
printf("right geometry command=%d\n",d.status.steering_permille);fflush(stdout);assert(d.status.steering_permille>=-194 && d.status.steering_permille<=-190);
i.geometry.target_right_mm=-50;tick();assert(d.status.steering_permille>=190 && d.status.steering_permille<=194);
i.geometry.valid=0;i.has_path=0;tick();assert(d.status.steering_permille>=190 && d.status.steering_permille<=194);
i.has_path=1;i.near_error_px=i.far_error_px=10;tick();assert(d.status.steering_permille==-(int)(10*AUTO_STEERING_KP));
i.stop_pressed=1;tick();assert(!d.status.active && !d.status.left_duty_permille && !d.status.right_duty_permille);
puts("geometry priority, both directions, hold, PID fallback and stop: PASS");return 0;}
