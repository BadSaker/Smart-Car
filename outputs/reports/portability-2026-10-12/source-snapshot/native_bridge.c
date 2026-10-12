/* Desktop hardware boundary. Vendor algorithms are copied byte-for-byte. */
#include "autonomous_drive.h"
#include "autonomous_config.h"
#include <string.h>
#define API __declspec(dllexport)
static autonomous_drive_t drive;
static track_vision_result_t vision;
static unsigned frame_sequence, frame_ms, feedback_sequence;
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
    i.servo_generation=drive.status.run_generation;i.servo_service_ms=now;
    i.vision_valid=vision.valid;i.confidence=vision.confidence;
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
        vision.element==TRACK_ELEMENT_ROUNDABOUT_EXIT);
    i.near_error_px=vision.near_error_px;i.far_error_px=vision.far_error_px;
    i.left_speed_mm_s=left_speed;i.right_speed_mm_s=right_speed;i.forward_step_um=step_um;
    autonomous_drive_tick(&drive,&i);
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
}
