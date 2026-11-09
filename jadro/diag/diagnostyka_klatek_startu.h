#pragma once
#include <stdint.h>
#include <stddef.h>
#include "prezentacja_startu.h"
#ifndef BURSZTYNDIAGSPLASHRUNTIMEFRAMES
#define BURSZTYNDIAGSPLASHRUNTIMEFRAMES 0
#endif
#if BURSZTYNDIAGSPLASHRUNTIMEFRAMES
extern void klog_serial(const char*);
#endif

namespace KlatkiStartuDiag {
struct Rect { int x=0,y=0,w=0,h=0; };
struct Info {
    uint64_t frame=0;
    bool active=false, ready=false;
    Rect title{}, message{};
    uint32_t background=0;
    const void* backbuffer=nullptr;
    unsigned message_index=0;
};
inline Info& info() { static Info i;return i; }
struct Liczniki { unsigned events=0, visible=0, blocked=0; };
inline Liczniki& liczniki() { static Liczniki l;return l; }
inline const char*& sciezka() { static const char* p="direct";return p; }
struct Sciezka {
    const char* old;
    explicit Sciezka(const char* p):old(nullptr) {
        if constexpr(BURSZTYNDIAGSPLASHRUNTIMEFRAMES){old=sciezka();sciezka()=p;}else (void)p;
    }
    ~Sciezka() { if constexpr(BURSZTYNDIAGSPLASHRUNTIMEFRAMES)sciezka()=old; }
};
inline void reset() { info()={};liczniki()={}; }
inline uint64_t hash(const uint32_t* p,int w,int h,int pitch) {
    uint64_t v=14695981039346656037ULL;
    if(!p||w<=0||h<=0||pitch<w)return 0;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        uint32_t c=p[size_t(y)*pitch+x];
        for(unsigned b=0;b<4;++b){v^=(c>>(b*8))&255;v*=1099511628211ULL;}
    }
    return v;
}
inline bool ink(const uint32_t* p,int w,int h,int pitch,Rect r) {
    if(!p||w<=0||h<=0||pitch<w)return false;
    for(int y=r.y<0?0:r.y;y<h&&int64_t(y)<int64_t(r.y)+r.h;++y)
        for(int x=r.x<0?0:r.x;x<w&&int64_t(x)<int64_t(r.x)+r.w;++x)
            // Uniform zeroed/uninitialized resources contain no text even
            // when their fill colour differs from the intended splash colour.
            if(p[size_t(y)*pitch+x]!=info().background&&p[size_t(y)*pitch+x]!=p[0])return true;
    return false;
}
inline void event(const char* producer,const char* backend,uint64_t now,
                  const void* front,const void* back,uint32_t resource,Rect dirty,
                  const uint32_t* pixels,int w,int h,int pitch,bool visible) {
#if BURSZTYNDIAGSPLASHRUNTIMEFRAMES
    auto& l=liczniki();
    if(l.visible>=12||l.events>=64)return;
    ++l.events;if(visible)++l.visible;
    char msg[768];size_t n=0;
    auto txt=[&](const char* s){while(*s&&n+1<sizeof(msg))msg[n++]=*s++;};
    auto num=[&](uint64_t v){char d[24];unsigned k=0;do{d[k++]=char('0'+v%10);v/=10;}while(v);while(k&&n+1<sizeof(msg))msg[n++]=d[--k];};
    auto coord=[&](int v){if(v<0){txt("-");num(uint64_t(-int64_t(v)));}else num(v);};
    txt("[SPLASH RUNTIME] event=");num(l.events);txt(" publication=");num(l.visible);
    txt(" visible=");num(visible);txt(" now_us=");num(now);
    txt(" producer=");txt(producer);txt(" path=");txt(sciezka());txt(" backend=");txt(backend);
    txt(" front=");num(reinterpret_cast<uintptr_t>(front));txt(" back=");num(reinterpret_cast<uintptr_t>(back));
    txt(" resource=");num(resource);txt(" dirty=");coord(dirty.x);txt(",");coord(dirty.y);txt(",");coord(dirty.w);txt(",");coord(dirty.h);
    txt(" active=");num(info().active);txt(" firstframeready=");num(info().ready);
    txt(" gate=");num(PrezentacjaStartu::dozwolona());
    txt(" title_pixels=");num(ink(pixels,w,h,pitch,info().title));
    txt(" first_message_pixels=");num(ink(pixels,w,h,pitch,info().message));
    txt(" message_index=");num(info().message_index);
    txt(" hash=");num(hash(pixels,w,h,pitch));
    txt(" generation=");num(PrezentacjaStartu::stan().generacja);txt(" frame=");num(info().frame);msg[n]=0;
    ::klog_serial(msg);
#else
    (void)producer;(void)backend;(void)now;(void)front;(void)back;(void)resource;
    (void)dirty;(void)pixels;(void)w;(void)h;(void)pitch;(void)visible;
#endif
}
}
