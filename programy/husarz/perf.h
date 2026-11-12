#pragma once
#include <stdint.h>
#ifndef HUSARZ_PERF
#define HUSARZ_PERF 0
#endif
namespace Husarz {
enum class Stage : uint8_t { Idle, Html, Css, Style, Layout, Table, Paint, Publication, Navigation, Resize, FirstLayout, Count };
#if HUSARZ_PERF
struct Performance {
    uint64_t selector_checks{}, selector_matches{}, layout_work{}, table_work{}, table_replans{};
    uint64_t full_reflows{}, partial_reflows{}, full_paints{}, partial_paints{};
    uint64_t paint_candidates{}, paint_visible{}, paint_culled{}, dirty_rectangles{};
    uint64_t resource_completions{}, cache_hits{}, cache_misses{};
    uint64_t shaped_paints{};
    uint64_t resize_received{}, resize_coalesced{}, slices{}, max_slice_work{},max_slice_ns{};
    uint64_t duration[unsigned(Stage::Count)]{}, maximum[unsigned(Stage::Count)]{};
};
inline uint64_t (*perf_clock)(){};
inline void (*perf_log)(const char*){};
inline void *perf_context{};inline uint64_t (*perf_context_time)(void*){};
inline const char *stage_name(Stage s) {
    const char *names[]={"idle","html","css","style","layout","table","paint","publication","navigation","resize","firstlayout"};
    return names[unsigned(s)];
}
inline void perf_number(char *out,unsigned &n,uint64_t v) {
    char b[24]; unsigned k=0; do { b[k++]=char('0'+v%10); v/=10; } while(v);
    while(k) out[n++]=b[--k];
}
inline void perf_word(char *out,unsigned &n,const char *s) { while(*s) out[n++]=*s++; }
struct PerfScope {
    Performance &stats; Stage stage; uint64_t start;void *context;
    uint64_t read()const{return context && perf_context_time?perf_context_time(context):perf_clock?perf_clock():0;}
    PerfScope(Performance &p,Stage s):stats(p),stage(s),start(0),context(perf_context){start=read();}
    ~PerfScope() {
        if(!start || !perf_clock)return;
        uint64_t elapsed=read()-start; auto i=unsigned(stage);
        stats.duration[i]+=elapsed; if(elapsed>stats.maximum[i])stats.maximum[i]=elapsed;
        if(!context && elapsed>=50000000 && perf_log) {
            char line[192];unsigned n=0;perf_word(line,n,"[HUSARZ PERF WARNING] stage=");perf_word(line,n,stage_name(stage));
            perf_word(line,n," durationms=");perf_number(line,n,elapsed/1000000);
            perf_word(line,n," layoutwork=");perf_number(line,n,stats.layout_work);line[n++]='\n';line[n]=0;perf_log(line);
        }
    }
};
#define HUSARZ_COUNT(d, field, amount) ((d).perf.field += (amount))
#define HUSARZ_SCOPE(d, stage) ::Husarz::PerfScope husarz_perf_scope((d).perf, ::Husarz::Stage::stage)
#else
#define HUSARZ_COUNT(d, field, amount) ((void)0)
#define HUSARZ_SCOPE(d, stage) ((void)0)
#endif
}
