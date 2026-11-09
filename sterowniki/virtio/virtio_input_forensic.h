#pragma once
#include <stdint.h>
#include <stddef.h>

#ifndef BURSZTYNDIAGVIRTIOINPUTFORENSIC
#define BURSZTYNDIAGVIRTIOINPUTFORENSIC 0
#endif

// The recorder, counters, samples and clock reads are identical in both builds.
// Only dump() depends on the diagnostic flag. No device/queue access here.
namespace VirtioInputForensic {
enum Outcome : uint8_t { Retained, Published, Discarded, Empty };
enum SynAction : uint8_t { None, Report, Dropped, Unsupported, Resync };
struct Event {
    uint64_t ns;
    uint32_t bdf,used_len;
    uint16_t queue,used_idx,used_element,descriptor,type,code;
    uint8_t raw[8];
    int32_t value,before_x,before_y,after_x,after_y,dx,dy;
    int cursor_x,cursor_y,new_x,new_y,width,height;
    uint32_t kinds;
    uint8_t role,lifecycle,mode,readiness,proven,phases,action,outcome;
    bool anomaly;
};
struct Transition { uint64_t ns;const char* label;uint64_t a,b; };
struct Counts {
    uint64_t events[4]{}; // Before registration, proof, geometry, desktop.
    uint64_t reports[4][3]{}; // Discarded, queued, published in each phase.
    uint64_t empty_reports[4]{};
    uint64_t total_events{},total_reports{};
};
struct Source {
    uint32_t bdf;
    Event events[160]{}; // First 128 plus at most 32 later anomaly samples.
    Transition transitions[48]{};
    unsigned count{},emitted{},transitions_count{},transitions_emitted{},anomalies{};
    Counts counts{},interactive_counts{};
    bool summary{},summary_emitted{};
    uint64_t interactive_ns{};
};
inline Source sources[8]{};
inline unsigned source_count=0;
inline Source* source(uint32_t bdf) {
    for(unsigned i=0;i<source_count;++i)if(sources[i].bdf==bdf)return &sources[i];
    if(source_count==8)return nullptr;
    auto* result=&sources[source_count++];*result={};result->bdf=bdf;return result;
}
inline void transition(uint32_t bdf,uint64_t ns,const char* label,uint64_t a=0,uint64_t b=0) {
    auto* s=source(bdf);if(s && s->transitions_count<48)s->transitions[s->transitions_count++]={ns,label,a,b};
}
inline void event(const Event& e) {
    auto* s=source(e.bdf);if(!s)return;
    ++s->counts.total_events;
    for(unsigned i=0;i<4;++i)if(e.phases&(1U<<i))++s->counts.events[i];
    if(e.type==0 && e.code==0) {
        ++s->counts.total_reports;
        const unsigned outcome=e.outcome==Discarded?0:e.outcome==Published?2:1;
        for(unsigned i=0;i<4;++i)if(e.phases&(1U<<i)) {
            if(e.outcome==Empty)++s->counts.empty_reports[i];
            else ++s->counts.reports[i][outcome];
        }
    }
    if(s->counts.total_events<=128 || (e.anomaly && s->anomalies<32)) {
        if(s->counts.total_events>128)++s->anomalies;
        s->events[s->count++]=e;
    }
}
inline void interactive(uint32_t bdf,uint64_t ns) {
    auto* s=source(bdf);if(!s || s->summary)return;
    s->interactive_counts=s->counts;s->interactive_ns=ns;s->summary=true;
}
inline void reset(uint32_t bdf) {
    auto* s=source(bdf);if(s){*s={};s->bdf=bdf;}
}

inline void dump() {
#if BURSZTYNDIAGVIRTIOINPUTFORENSIC
    extern void emit(const char*);
    struct Line {
        char buf[1792]{};size_t size{};
        void text(const char* p){while(p && *p && size+1<sizeof(buf))buf[size++]=*p++;}
        void number(int64_t n){uint64_t v=n<0?uint64_t(-(n+1))+1:uint64_t(n);if(n<0)text("-");char b[24];unsigned k=0;do{b[k++]=char('0'+v%10);v/=10;}while(v);while(k && size+1<sizeof(buf))buf[size++]=b[--k];}
        void field(const char* p,int64_t v){text(" ");text(p);text("=");number(v);}
        void hex(uint64_t n,unsigned digits){static const char* h="0123456789abcdef";while(digits && size+1<sizeof(buf))buf[size++]=h[(n>>(4*--digits))&15];}
        void bdf(uint32_t v){text(" bdf=");hex(v>>16,2);text(":");hex((v>>8)&255,2);text(".");hex(v&255,1);}
        ~Line(){emit(buf);}
    };
    unsigned budget=2;
    for(unsigned i=0;i<source_count && budget;++i) {
        auto& s=sources[i];
        if(s.summary && !s.summary_emitted) {
            --budget;s.summary_emitted=true;Line l;l.text("[VINPUT FORENSIC INTERACTIVE]");l.bdf(s.bdf);l.field("ns",s.interactive_ns);
            static const char* phases[]={"before_registration","before_PROVENACTIVE","before_geometry","before_desktop"};
            for(unsigned p=0;p<4;++p){l.text(" ");l.text(phases[p]);l.text("(events/discarded/queued/published/empty)=");l.number(s.interactive_counts.events[p]);for(unsigned n=0;n<3;++n){l.text("/");l.number(s.interactive_counts.reports[p][n]);}l.text("/");l.number(s.interactive_counts.empty_reports[p]);}
            l.field("total_events",s.interactive_counts.total_events);l.field("complete_reports",s.interactive_counts.total_reports);
        }
        while(budget && s.transitions_emitted<s.transitions_count) {
            --budget;const auto& t=s.transitions[s.transitions_emitted++];Line l;l.text("[VINPUT FORENSIC LIFE]");l.bdf(s.bdf);l.field("ns",t.ns);l.text(" ");l.text(t.label);l.field("a",t.a);l.field("b",t.b);
        }
        while(budget && s.emitted<s.count) {
            --budget;const auto& e=s.events[s.emitted++];Line l;l.text("[VINPUT FORENSIC EVENT]");l.bdf(e.bdf);l.field("ns",e.ns);
            l.field("role",e.role);l.field("lifecycle",e.lifecycle);l.field("queue",e.queue);l.field("used_idx",e.used_idx);l.field("used_element",e.used_element);l.field("descriptor",e.descriptor);l.field("used_len",e.used_len);
            l.text(" role_name=");l.text(e.role==1?"keyboard":e.role==2?"relative_mouse":e.role==4?"absolute_pointer":"combined");
            l.text(" bytes=");for(auto byte:e.raw)l.hex(byte,2);
            l.field("type",e.type);l.field("code",e.code);l.field("signed_value",e.value);l.field("mode",e.mode);l.field("kinds",e.kinds);l.field("ready",e.readiness);l.field("proven",e.proven);
            l.field("relx_before",e.before_x);l.field("rely_before",e.before_y);l.field("relx_after",e.after_x);l.field("rely_after",e.after_y);
            static const char* actions[]={"none","SYN_REPORT","SYN_DROPPED","unsupported_SYN","resync"};
            static const char* outcomes[]={"retained","published","discarded","empty"};
            l.text(" syn_action=");l.text(actions[e.action]);l.text(" outcome=");l.text(outcomes[e.outcome]);
            l.field("published_dx",e.dx);l.field("published_dy",e.dy);l.field("cursor_x",e.cursor_x);l.field("cursor_y",e.cursor_y);l.field("new_x",e.new_x);l.field("new_y",e.new_y);
            l.field("width",e.width);l.field("height",e.height);l.field("phases",e.phases);l.field("anomaly",e.anomaly);
        }
    }
#endif
}
} // namespace VirtioInputForensic
