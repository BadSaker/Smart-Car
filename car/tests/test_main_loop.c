#include <assert.h>
#include <stdio.h>
#define main firmware_main
#include "../src/main.c"
#undef main

volatile uint32_t app_uptime_ms;
volatile uint8_t mt9v03x_finish_flag;
uint8_t mt9v03x_image[120][188];
csi_handle_t csi_handle;
static autonomous_drive_status_t state;
static uint32_t irq_mask,published_sequence,expected_stamp,send_delay;
static unsigned pending_services,sends,services;
static uint8_t submitted,applied;
static char events[100];static unsigned event_count;
static void event(char c){assert(event_count<sizeof events-1);events[event_count++]=c;events[event_count]=0;}
uint32_t __get_PRIMASK(void){return irq_mask;}
void __disable_irq(void){irq_mask=1;}
void __set_PRIMASK(uint32_t m){irq_mask=m;}
void clock_init(int n){(void)n;}
void debug_init(void){}
void system_delay_ms(uint32_t n){app_uptime_ms+=n;}
void pit_ms_init(int c,int n){(void)c;(void)n;}
void interrupt_global_enable(int n){(void)n;}
int mt9v03x_init(void){return 0;}
void camera_display_init(void){}
void camera_display_message(const char *s){(void)s;}
void servo_control_init(uint32_t n){(void)n;}
void servo_control_stop(void){}
void encoder_feedback_init(void){}
void motor_control_init(void){}
void motor_control_enable_test(void){}
uint8_t motor_control_take_servo_stop(void){return 0;}
void remote_control_init(void){}
void track_vision_reset(void){}
void autonomous_control_init(void){}
void autonomous_control_get_status(autonomous_drive_status_t *s){*s=state;}
uint8_t autonomous_control_is_active(void){return state.active;}
void autonomous_control_set_network_busy(uint8_t b){(void)b;}
uint8_t autonomous_control_service(uint32_t n){
    (void)n;++services;assert(!irq_mask);
    if(submitted && state.active && !applied){
        /* 模拟先等PIT，再由下一次主循环服务应用该结果。 */
        ++pending_services;
        if(pending_services==2)event('T');
        if(pending_services==3){applied=1;event('A');}
    }
    return state.active;
}
void remote_control_service(uint32_t n){(void)n;}
void remote_control_poll(uint32_t (*clock_ms)(void)){assert(clock_ms()==app_uptime_ms);event('R');}
void track_vision_process(const uint8_t *p,uint32_t n,uint32_t d,track_vision_result_t *v){
    (void)d;assert(p==image[0] && n==expected_stamp);memset(v,0,sizeof *v);v->threshold=99;event('V');
}
void autonomous_control_publish_vision(const track_vision_result_t *v,uint32_t s,uint32_t n){
    assert(v->threshold==99 && n==expected_stamp);submitted=1;applied=0;pending_services=0;published_sequence=s;event('P');
}
uint8_t camera_debug_init(uint8_t *l,uint8_t *m,uint8_t *r){assert(l && m && r);return 0;}
void camera_debug_configure_scheduler(uint32_t (*clock_ms)(void),uint8_t (*driving)(void),void (*busy)(uint8_t)){
    assert(clock_ms==NULL && driving==autonomous_control_is_active && busy==autonomous_control_set_network_busy);
}
void camera_debug_capture_frame(const uint8_t *p){assert(p==image[0] && submitted);event('D');}
void binaryzation_process(unsigned char *p,unsigned short h,unsigned short w,unsigned int t){
    assert(p==image[0] && h==120 && w==188 && t==99);event('B');
}
void camera_debug_send_frame(const uint8_t *p){
    assert(p==image[0] && submitted);
    event('S');++sends;app_uptime_ms+=send_delay;
}
void camera_debug_poll(uint32_t n){assert(n==app_uptime_ms);event('N');}
int main(void){
    const uint32_t delays[]={100,300,1000};unsigned k;
    app_init();
    state.active=1;state.run_generation=1;
    for(k=0;k<sizeof delays/sizeof delays[0];++k){
        unsigned before=services;
        event_count=0;events[0]=0;submitted=0;
        camera_frame_sequence=k+1;expected_stamp=camera_frame_completed_ms=7;
        app_uptime_ms=2000;mt9v03x_finish_flag=1;send_delay=delays[k];
        app_run_once();
        assert(strcmp(events,"VPDTBSAN")==0);
        assert(sends==k+1 && app_uptime_ms==2000+delays[k] && services==before+5);
    }
    state.active=0;event_count=0;events[0]=0;mt9v03x_finish_flag=1;
    app_run_once();assert(strcmp(events,"VPDBSNR")==0);
    event_count=0;events[0]=0;mt9v03x_finish_flag=0;
    app_run_once();assert(strcmp(events,"NR")==0);
    puts("PASS: real main loop does not wait for current-frame execution before display/full send; aged frames retained; 100/300/1000 ms sends; idle polling");
    return 0;
}
