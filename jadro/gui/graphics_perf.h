#pragma once
#include <stdint.h>
#include "sterowniki/czas/hpet.h"

// Cumulative, bounded, read-only debugger telemetry. No serial I/O, allocation,
// scheduler changes or user ABI. Times are guest monotonic elapsed ns (including
// preemption), not hardware CPU cycle counts or host photon timestamps.
namespace GraphicsPerf {
enum Counter { Frames, ComposeNs, PresentNs, Rects, Pixels, FullFrames,
    CopyNs, CopyBytes, GpuWaitNs, GpuCommands, Transfers, TransferPixels,
    Flushes, FlushPixels, FramebufferAttempts, FramebufferCreates, Scanouts,
    HostPolls, HostHintChanges, ModeRequests, ModeCommits, Inputs,
    InputSamples, InputNs, InputMaxNs, CursorPresents, Count };
struct Frame { uint64_t end_ns, compose_ns, present_ns, pixels, rects, input_ns; };
struct Snapshot {
    uint64_t version=1;
    uint64_t counters[Count]{};
    uint64_t frame_sequence=0;
    Frame frames[128]{};
};
inline Snapshot graphics_perf{};
enum ModeSource : uint64_t { Boot=1, Config=2, Guest=3, Host=4, Rollback=5 };
struct ModeEvent {uint64_t time_ns,source,width,height,scale;};
struct ModeHistory {uint64_t sequence=0;ModeEvent events[32]{};};
inline ModeHistory graphics_modes{};
inline ModeHistory graphics_hints{};
inline void hint(uint32_t w,uint32_t h){
    const auto seq=graphics_hints.sequence;
    graphics_hints.events[seq%32]={hpet_dostepny()?czas_monotoniczny_ns():0,6,w,h,0};
    __atomic_store_n(&graphics_hints.sequence,seq+1,__ATOMIC_RELEASE);
}
inline uint64_t mode_source=Boot;
inline void mode(ModeSource source,uint32_t w,uint32_t h,uint32_t scale){
    const auto seq=graphics_modes.sequence;
    graphics_modes.events[seq%32]={hpet_dostepny()?czas_monotoniczny_ns():0,source,w,h,scale};
    mode_source=source;
    __atomic_store_n(&graphics_modes.sequence,seq+1,__ATOMIC_RELEASE);
}
struct Resources {uint64_t live_bytes=0,peak_bytes=0,allocations=0,frees=0;};
inline Resources graphics_resources{};
inline void allocated(uint64_t bytes){
    graphics_resources.live_bytes+=bytes;++graphics_resources.allocations;
    if(graphics_resources.live_bytes>graphics_resources.peak_bytes)graphics_resources.peak_bytes=graphics_resources.live_bytes;
}
inline void freed(uint64_t bytes){graphics_resources.live_bytes-=bytes;++graphics_resources.frees;}
inline uint64_t first_input_ns=0;
inline uint64_t now(){return hpet_dostepny()?czas_monotoniczny_ns():0;}
inline void add(Counter c,uint64_t n=1){__atomic_add_fetch(&graphics_perf.counters[c],n,__ATOMIC_RELAXED);}
inline void input(){
    add(Inputs);uint64_t empty=0,t=now();
    __atomic_compare_exchange_n(&first_input_ns,&empty,t,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED);
}
inline uint64_t take_input(){return __atomic_exchange_n(&first_input_ns,0,__ATOMIC_RELAXED);}
inline uint64_t latency(uint64_t input,uint64_t end){
    if(!input||end<input)return 0;
    uint64_t n=end-input;add(InputSamples);add(InputNs,n);
    auto* p=&graphics_perf.counters[InputMaxNs];auto old=__atomic_load_n(p,__ATOMIC_RELAXED);
    while(n>old&&!__atomic_compare_exchange_n(p,&old,n,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){}
    return n;
}
inline void frame(uint64_t end,uint64_t compose,uint64_t present,uint64_t pixels,uint64_t rects,uint64_t input){
    add(Frames);add(ComposeNs,compose);add(PresentNs,present);add(Pixels,pixels);add(Rects,rects);
    const auto seq=graphics_perf.frame_sequence;
    graphics_perf.frames[seq%128]={end,compose,present,pixels,rects,latency(input,end)};
    __atomic_store_n(&graphics_perf.frame_sequence,seq+1,__ATOMIC_RELEASE);
}
struct Timer {
    Counter counter;uint64_t start;
    explicit Timer(Counter c):counter(c),start(now()){}
    ~Timer(){const auto end=now();if(start&&end>=start)add(counter,end-start);}
};
}
