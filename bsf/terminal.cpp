#include "terminal.h"
namespace BSF {
namespace {
#include "terminal_width.inc"
bool wide(uint32_t cp) {
  size_t lo=0,hi=sizeof(terminal_wide)/sizeof(*terminal_wide);
  while(lo<hi){size_t mid=lo+(hi-lo)/2;
    if(cp<terminal_wide[mid][0])hi=mid;
    else if(cp>terminal_wide[mid][1])lo=mid+1;
    else return true;
  }return false;
}
}
unsigned terminal_cells(const char* text,size_t size){
  unsigned cells=0;
  for(size_t at=0;at<size;){uint32_t cp;at+=utf8(text+at,size-at,cp);
    if(terminal_attachment(cp)||default_ignorable(cp)||cp<32||(cp>=127&&cp<160))continue;
    unsigned w=wide(cp)?2:1;if(w>cells)cells=w;
  }return cells;
}
}
