#pragma once
#include <stdint.h>
namespace Husarz {
// Optional wall-time guard, calibrated without waiting. The GUI loop already
// supplies BWS60/HPET time. Avoid invoking a kernel syscall at every checkpoint.
// Rate/backwards checks discard uncertain estimates; work units always remain
// the deterministic limit, including migration or changing virtual CPU rates.
struct WorkClock {
    uint64_t anchor_ns{},anchor_cycles{},rate_khz{},last{};bool valid{};
    void sample(uint64_t now,uint64_t cycles){
        if(!now || !anchor_ns || now<=anchor_ns || cycles<=anchor_cycles || now-anchor_ns>UINT64_C(5000000000)){
            anchor_ns=now;anchor_cycles=cycles;if(now>last)last=now;valid=false;return;
        }
        uint64_t ns=now-anchor_ns,delta=cycles-anchor_cycles;
        if(ns<5000000)return;
        if(delta>UINT64_MAX/1000000){valid=false;anchor_ns=now;anchor_cycles=cycles;return;}
        uint64_t candidate=delta*1000000/ns;
        bool plausible=candidate>=100000 && candidate<=10000000;
        bool stable=!rate_khz || (candidate>=rate_khz*4/5 && candidate<=rate_khz*6/5);
        valid=plausible && stable;rate_khz=plausible?candidate:0;
        anchor_ns=now;anchor_cycles=cycles;if(now>last)last=now;
    }
    uint64_t read(uint64_t cycles){
        if(!valid || cycles<anchor_cycles)return last;
        uint64_t delta=cycles-anchor_cycles;if(delta>rate_khz*5000)return last;
        uint64_t result=anchor_ns+(delta/rate_khz)*1000000+(delta%rate_khz)*1000000/rate_khz;
        if(result>last)last=result;
        return last;
    }
};
inline uint64_t work_cycles(){uint32_t lo,hi;asm volatile("lfence; rdtsc":"=a"(lo),"=d"(hi)::"memory");return (uint64_t(hi)<<32)|lo;}
inline WorkClock native_work_clock{};
inline uint64_t native_work_now(){return native_work_clock.read(work_cycles());}
}
