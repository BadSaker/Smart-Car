#include "track_vision.c"
#define API __declspec(dllexport)
API void reset(void){track_vision_reset();}
API void run(const unsigned char *g,unsigned ms,unsigned mm,float *v,unsigned char *r){track_vision_result_t o;track_vision_process(g,ms,mm,&o);
v[0]=(float)o.valid;
v[1]=(float)o.confidence;
v[2]=(float)o.observation_confidence;
v[3]=(float)o.threshold;
v[4]=(float)o.fault;
v[5]=(float)o.element;
v[6]=(float)o.side;
v[7]=(float)o.near_error_px;
v[8]=(float)o.far_error_px;
v[9]=(float)o.raw_near_error_px;
v[10]=(float)o.raw_far_error_px;
v[11]=(float)o.near_path_supported;
v[12]=(float)o.curve_outer_side;
v[13]=(float)o.curve_outer_supported;
v[14]=(float)o.curve_outer_near_rows;
v[15]=(float)o.curve_outer_far_rows;
v[16]=(float)o.curve_outer_reject;
v[17]=(float)o.left_opening_rows;
v[18]=(float)o.right_opening_rows;
v[19]=(float)o.left_fork;
v[20]=(float)o.right_fork;
v[21]=(float)o.left_island;
v[22]=(float)o.right_island;
v[23]=(float)o.ring_left_reject;
v[24]=(float)o.ring_right_reject;
v[25]=(float)o.ring_entry_supported;
v[26]=(float)o.zebra;
v[27]=(float)o.zebra_near_row;
v[28]=(float)o.zebra_far_row;
v[29]=(float)o.zebra_rows;
v[30]=(float)o.zebra_reject;
v[31]=(float)o.geometry.valid;
v[32]=(float)o.geometry.reject_reason;
v[33]=(float)obstacle.side;
v[34]=(float)obstacle.candidate;
v[35]=(float)obstacle.frames;
v[36]=(float)obstacle.blocked;
v[37]=(float)obstacle.offset_mm;
v[38]=(float)vision.candidate_left;
v[39]=(float)vision.candidate_right;
v[40]=(float)vision.candidate_cross;
v[41]=(float)vision.approach_clear;
v[42]=(float)vision.recovery;
v[43]=(float)vision.phase;
v[44]=(float)vision.side;
v[45]=(float)vision.diagnostic.limit_reason;
v[46]=(float)vision.curve_outer_frames;
memcpy(r,o.left,120);memcpy(r+120,o.right,120);memcpy(r+240,o.center,120);memcpy(r+360,o.row_flags,120);}

API void probe(const unsigned char *g,float *v,unsigned char *r){
 track_vision_result_t o;vision_features_t f;unsigned char contrast;int y;
 track_vision_reset();memset(&o,0,sizeof o);
 for(y=0;y<120;y++){o.center[y]=93;o.right[y]=187;}
 o.threshold=vision_threshold(g,&contrast);vision_zebra_region(g,&o,0,0);
 vision_scan(g,&o,&f,TRACK_SIDE_NONE);
 v[0]=o.threshold;v[1]=f.measured_rows;v[2]=f.paired_rows;
 memcpy(r,o.left,120);memcpy(r+120,o.right,120);memcpy(r+240,o.center,120);memcpy(r+360,o.row_flags,120);
}
