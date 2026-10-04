#pragma once
#include <stdint.h>
#ifndef BURSZTYNDIAGABSMAP
#define BURSZTYNDIAGABSMAP 0
#endif
// Diagnostic-only accumulator. No coordinate correction or input ownership.
struct AbsMapSample {
    int32_t raw_x=0,raw_y=0,min_x=0,max_x=0,min_y=0,max_y=0;
    int mapped_x=0,mapped_y=0,width=0,height=0;
    int32_t low_x=0,high_x=0,low_y=0,high_y=0;
    uint64_t started=0,last_report=0;
    uint32_t reports=0,stalled_x=0,stalled_y=0,endpoints=0;
    bool valid_x=false,valid_y=false,near_edge=false;
    void record(int32_t x,int32_t y,bool changed_x,bool changed_y,
                int32_t xmin,int32_t xmax,int32_t ymin,int32_t ymax,
                int mx,int my,int w,int h,uint64_t now) {
        if(!reports) {started=now;low_x=high_x=x;low_y=high_y=y;}
        if(changed_x) {
            if(valid_x&&raw_x!=x&&mapped_x==mx)++stalled_x;
            if(!valid_x||x<low_x)low_x=x;
            if(!valid_x||x>high_x)high_x=x;
            valid_x=true;
            if(x==xmin)endpoints|=1;
            if(x==xmax)endpoints|=2;
        }
        if(changed_y) {
            if(valid_y&&raw_y!=y&&mapped_y==my)++stalled_y;
            if(!valid_y||y<low_y)low_y=y;
            if(!valid_y||y>high_y)high_y=y;
            valid_y=true;
            if(y==ymin)endpoints|=4;
            if(y==ymax)endpoints|=8;
        }
        raw_x=x;raw_y=y;min_x=xmin;max_x=xmax;min_y=ymin;max_y=ymax;
        mapped_x=mx;mapped_y=my;width=w;height=h;
        near_edge|=mx<=4||my<=4||mx>=w-5||my>=h-5;
        ++reports;
    }
    bool due(uint64_t now) const {
        // Aggregate all reports, including restricted interior motion. Endpoints
        // survive throttling and are emitted even if the pointer stops at a corner.
        return reports&&now>=last_report&&now-last_report>=1000000000ULL;
    }
    void consumed(uint64_t now) {
        last_report=now;reports=stalled_x=stalled_y=endpoints=0;near_edge=false;
    }
};
