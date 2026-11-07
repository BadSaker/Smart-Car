/* 稀疏真实边线仍产生偏差；黑帧不伪造居中，不存在评分门槛。 */
#include "track_vision.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned char gray[120*188];
static track_vision_result_t v;
#define CHECK(x) do { if(!(x)){printf("line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
int main(void){
 track_vision_reset();memset(gray,0,sizeof gray);
 for(int row=30;row<=116;row++) {
  if(!((row>=96 && row<=99)||(row>=56 && row<=59)))continue;
  int width=44+(row-30)*123/86;
  for(int col=103-width/2;col<=103+width/2 && col<186;col++)gray[row*188+col]=240;
 }
 track_vision_process(gray,20,0,&v);CHECK(v.has_path);CHECK(v.near_error_px>0);
 int near=v.near_error_px,far=v.far_error_px;
 memset(gray,0,sizeof gray);track_vision_process(gray,40,0,&v);
 CHECK(!v.has_path);CHECK(v.near_error_px==near && v.far_error_px==far);
 puts("direct vision checks passed");return 0;
}
