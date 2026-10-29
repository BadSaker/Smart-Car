/* Desktop hardware boundary. Vendor algorithms are copied byte-for-byte. */
#include "autonomous_drive.h"
#include "autonomous_config.h"
#include "servo_config.h"
#include <string.h>
#include <math.h>
#define API __declspec(dllexport)
static autonomous_drive_t drive;
static track_vision_result_t vision;
static unsigned frame_sequence, frame_ms, feedback_sequence;

#include "zf_driver_gpio.h"
#include "source/src/servo_control.c"
static unsigned probe_ms,probe_generation,probe_next,probe_period=10;
static unsigned probe_pause_start,probe_pause_end;
static int probe_hardware;
static float probe_last_pulse=1480;
void gpio_init(gpio_pin_enum p,gpio_dir_enum d,uint8_t v,uint32_t c){(void)p;(void)d;(void)v;(void)c;}
uint8_t gpio_get_level(gpio_pin_enum p){(void)p;return GPIO_HIGH;}
void pwm_init(pwm_channel_enum p,uint32_t h,uint32_t d){(void)p;(void)h;if(d)probe_last_pulse=(float)d*2;}
void pwm_set_duty(pwm_channel_enum p,uint32_t d){(void)p;if(d)probe_last_pulse=(float)d*2;}
API void probe_setup(int hardware,unsigned period,unsigned begin,unsigned end){
 probe_hardware=hardware;probe_period=period;probe_pause_start=begin;probe_pause_end=end;
 probe_ms=probe_generation=probe_next=0;probe_last_pulse=SERVO_REFERENCE_CENTER_US;
 servo_control_init(0);
}
static void probe_service(unsigned now){
 if(now<probe_next || (now>=probe_pause_start && now<probe_pause_end))return;
 probe_next=now+probe_period;
 servo_control_autonomous_apply(drive.status.active,drive.status.steering_permille,now);
 if(drive.status.active && servo_control_get_status()->output_enabled){
  probe_ms=now;probe_generation=drive.status.run_generation;
 }
}
API void probe_values(float *v){
 const servo_control_status_t *s=servo_control_get_status();
 v[0]=s->current_pulse_us;v[1]=probe_last_pulse;v[2]=(float)probe_ms;
 v[3]=(float)probe_generation;v[4]=s->output_enabled;v[5]=(float)frame_ms;
}

API void sim_motion(float *o) {
    const autonomous_drive_status_t *s=&drive.status;
    o[0]=s->requested_steering_permille;o[1]=s->steering_rate_permille_s;o[2]=s->speed_limit_mm_s;
    o[3]=s->left_target_mm_s;o[4]=s->right_target_mm_s;
    o[5]=s->left_filtered_mm_s;o[6]=s->right_filtered_mm_s;
    o[7]=s->left_pid_request;o[8]=s->right_pid_request;o[9]=s->left_integral;o[10]=s->right_integral;
    o[11]=AUTO_TURN_DIFFERENTIAL_RATIO;
}
API void sim_reset(void) {
    memset(&vision,0,sizeof vision);frame_sequence=frame_ms=feedback_sequence=0;
    track_vision_reset();autonomous_drive_init(&drive);
}
API void sim_vision(const unsigned char *gray,unsigned now,unsigned distance) {
    track_vision_process(gray,now,distance,&vision);
    frame_sequence++;frame_ms=now;
}
API void sim_tick(unsigned now,int feedback_fresh,int start,int stop,
                  int left_speed,int right_speed,int step_um) {
    autonomous_drive_input_t i;memset(&i,0,sizeof i);
    if(feedback_fresh)feedback_sequence++;
    i.now_ms=now;i.ready=1;i.start_pressed=(unsigned char)start;i.stop_pressed=(unsigned char)stop;
    i.frame_sequence=frame_sequence;i.frame_ms=frame_ms;i.feedback_sequence=feedback_sequence;
    i.servo_generation=probe_hardware?probe_generation:drive.status.run_generation;i.servo_service_ms=probe_hardware?probe_ms:now;
    i.vision_valid=vision.valid;i.confidence=vision.confidence;i.geometry=vision.geometry;
    i.zebra=vision.zebra && vision.zebra_near_row!=255 && vision.zebra_near_row>=AUTO_ZEBRA_MIN_ROW;
    i.element_fault=(vision.fault & TRACK_FAULT_ELEMENT_TIMEOUT)!=0;
    i.element_slow=vision.element>=TRACK_ELEMENT_CROSS;i.curve=vision.element==TRACK_ELEMENT_CURVE;
    i.element=(unsigned char)vision.element;i.vision_near_supported=vision.near_path_supported;
    i.curve_outer_side=(unsigned char)vision.curve_outer_side;
    i.curve_outer_near_rows=vision.curve_outer_near_rows;i.curve_outer_far_rows=vision.curve_outer_far_rows;
    i.curve_outer_supported=i.curve && vision.valid && !vision.fault && vision.curve_outer_supported &&
        (vision.curve_outer_side==TRACK_SIDE_LEFT || vision.curve_outer_side==TRACK_SIDE_RIGHT);
    i.curve_outer_reject_reason=vision.curve_outer_reject==TRACK_OUTER_REJECT_NEAR?AUTO_PATH_OUTER_NEAR:
        vision.curve_outer_reject==TRACK_OUTER_REJECT_FAR?AUTO_PATH_OUTER_FAR:
        vision.curve_outer_reject==TRACK_OUTER_REJECT_CONTINUITY?AUTO_PATH_OUTER_CONTINUITY:AUTO_PATH_ACCEPTED;
    i.element_supported=vision.valid && !vision.fault && (vision.element==TRACK_ELEMENT_CROSS ||
        vision.element==TRACK_ELEMENT_ROUNDABOUT_ENTER || vision.element==TRACK_ELEMENT_ROUNDABOUT_INSIDE ||
        vision.element==TRACK_ELEMENT_ROUNDABOUT_EXIT ||
        (vision.element==TRACK_ELEMENT_ROUNDABOUT_APPROACH && vision.ring_entry_supported));
    i.near_error_px=vision.near_error_px;i.far_error_px=vision.far_error_px;
    i.left_speed_mm_s=left_speed;i.right_speed_mm_s=right_speed;i.forward_step_um=step_um;
    autonomous_drive_tick(&drive,&i);
    probe_service(now);
}
API void sim_data(float *o,unsigned char *rows) {
    autonomous_drive_status_t *s=&drive.status;
    o[0]=(float)s->state;o[1]=(float)s->fault;o[2]=(float)s->active;
    o[3]=(float)vision.confidence;o[4]=(float)vision.valid;o[5]=(float)vision.near_error_px;
    o[6]=(float)vision.far_error_px;o[7]=(float)vision.element;o[8]=(float)vision.curve_outer_side;
    o[9]=(float)vision.curve_outer_supported;o[10]=(float)s->path_reason;
    o[11]=(float)s->steering_permille;o[12]=(float)s->left_duty_permille;o[13]=(float)s->right_duty_permille;
    o[14]=s->target_speed_mm_s;o[15]=(float)s->distance_mm;o[16]=(float)s->minimum_confidence;
    o[17]=(float)vision.threshold;o[18]=(float)vision.curve_outer_near_rows;o[19]=(float)vision.curve_outer_far_rows;
    o[20]=(float)vision.fault;o[21]=(float)s->diagnostic_steering_permille;
    memcpy(rows,vision.left,120);memcpy(rows+120,vision.right,120);
    memcpy(rows+240,vision.center,120);memcpy(rows+360,vision.row_flags,120);
}
/* 诊断通道独立于控制输入；记录原始证据和补线支持，不提供赛道真值。 */
API void sim_features(float *o) {
    o[0]=(float)vision.side;
    o[1]=(float)vision.raw_near_error_px;o[2]=(float)vision.raw_far_error_px;
    o[3]=(float)vision.left_opening_rows;o[4]=(float)vision.right_opening_rows;
    o[5]=(float)vision.joint_opening_rows;o[6]=(float)vision.left_fork;o[7]=(float)vision.right_fork;
    o[8]=(float)vision.opening_left_bottom;o[9]=(float)vision.opening_right_bottom;
    o[10]=(float)vision.near_path_supported;o[11]=(float)vision.curve_outer_near_rows;
    o[12]=(float)vision.curve_outer_far_rows;o[13]=(float)vision.curve_outer_reject;
    o[14]=(float)vision.element_bridged;
    o[15]=(float)vision.ring_left_reject;o[16]=(float)vision.ring_right_reject;
    o[17]=(float)vision.left_island;o[18]=(float)vision.right_island;
    o[19]=(float)vision.ring_entry_supported;o[20]=(float)vision.fault_element;
    o[21]=(float)vision.element_diagnostic.entry_ms;o[22]=(float)vision.element_diagnostic.entry_mm;
    o[23]=(float)vision.element_diagnostic.elapsed_ms;o[24]=(float)vision.element_diagnostic.traveled_mm;
    o[25]=(float)vision.element_diagnostic.recovery_frames;o[26]=(float)vision.element_diagnostic.exit_evidence;
    o[27]=(float)vision.element_diagnostic.limit_reason;
    o[28]=(float)vision.element_diagnostic.fault_entry_ms;o[29]=(float)vision.element_diagnostic.fault_entry_mm;
    o[30]=(float)vision.element_diagnostic.fault_elapsed_ms;o[31]=(float)vision.element_diagnostic.fault_traveled_mm;
    o[32]=(float)vision.element_diagnostic.history_count;
    { unsigned k;for(k=0;k<TRACK_ELEMENT_HISTORY_SIZE;++k) {
        const track_element_event_t *e=&vision.element_diagnostic.history[k];
        o[33+k*4]=(float)e->time_ms;o[34+k*4]=(float)e->distance_mm;
        o[35+k*4]=(float)e->from;o[36+k*4]=(float)e->to;
    } }
    o[65]=vision.observation_confidence;o[66]=vision.preview_first_row;o[67]=vision.preview_last_row;
    o[68]=vision.geometry.valid;o[69]=vision.geometry.target_right_mm;o[70]=vision.geometry.target_forward_mm;
    o[71]=vision.geometry.fit_rms_mm;o[72]=vision.geometry.model_kind;o[73]=vision.geometry.contour_points;
    o[74]=vision.geometry.visible_samples;o[75]=vision.geometry.inferred_samples;o[76]=vision.geometry.reject_reason;
}
/* 仅图像渲染，结果随后作为灰度输入；不向控制器传递地图身份。
 * 与NumPy参考采样保持double运算顺序、双线性权重与偶数舍入。 */
API void sim_camera(const unsigned char *raster,unsigned width,unsigned height,
                    const double *longitudinal,const double *right,const unsigned char *valid,
                    unsigned samples,const double *p,unsigned char *image) {
    unsigned pixel,k;const unsigned pixels=188U*120U;
    for(pixel=0;pixel<pixels;++pixel) {
        double sum=0.0;
        for(k=0;k<samples;++k) {
            unsigned i=k*pixels+pixel;double value=p[9];
            if(valid[i]) {
                double wx=p[0]+p[4]*p[2]+longitudinal[i]*p[2]+right[i]*p[3]-p[5]*p[3];
                double wy=p[1]+p[4]*p[3]+longitudinal[i]*p[3]-right[i]*p[2]+p[5]*p[2];
                double gx=(wx-p[6])/p[8],gy=(wy-p[7])/p[8];
                if(gx>=0.0 && gy>=0.0 && gx<width-1U && gy<height-1U) {
                    unsigned ix=(unsigned)floor(gx),iy=(unsigned)floor(gy);
                    double ax=gx-ix,ay=gy-iy;
                    value=(1.0-ax)*(1.0-ay)*raster[iy*width+ix]+ax*(1.0-ay)*raster[iy*width+ix+1U]
                        +(1.0-ax)*ay*raster[(iy+1U)*width+ix]+ax*ay*raster[(iy+1U)*width+ix+1U];
                }
            }
            sum+=value;
        }
        image[pixel]=(unsigned char)nearbyint(sum/samples);
    }
}
