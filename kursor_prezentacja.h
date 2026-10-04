#pragma once
#include "skladacz_obrazu.h"

// Scene stays cursor-free between transactions. The scratch background belongs
// to the screen-locked renderer, never to the input producer.
inline GuiDirtyRect kursor_clip(GuiDirtyRect r, int sw, int sh) {
    int64_t x0=r.x,y0=r.y,x1=x0+r.width,y1=y0+r.height;
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>sw)x1=sw;
    if(y1>sh)y1=sh;
    if(r.width<=0||r.height<=0||x0>=x1||y0>=y1)return {};
    return {int(x0),int(y0),int(x1-x0),int(y1-y0)};
}
inline bool kursor_przecina(GuiDirtyRect a,GuiDirtyRect b) {
    return a.width>0&&a.height>0&&b.width>0&&b.height>0&&
        int64_t(a.x)<int64_t(b.x)+b.width&&int64_t(b.x)<int64_t(a.x)+a.width&&
        int64_t(a.y)<int64_t(b.y)+b.height&&int64_t(b.y)<int64_t(a.y)+a.height;
}
inline GuiDirtyRect kursor_union(GuiDirtyRect a,GuiDirtyRect b) {
    if(a.width<=0||a.height<=0)return b;
    if(b.width<=0||b.height<=0)return a;
    int x=a.x<b.x?a.x:b.x,y=a.y<b.y?a.y:b.y;
    int64_t right=int64_t(a.x)+a.width,bottom=int64_t(a.y)+a.height;
    if(int64_t(b.x)+b.width>right)right=int64_t(b.x)+b.width;
    if(int64_t(b.y)+b.height>bottom)bottom=int64_t(b.y)+b.height;
    return {x,y,int(right-x),int(bottom-y)};
}

// Called with the caller's IRQ exclusion. Latest position, bounded storage.
struct KursorLatest {
    bool pending=false;
    int x=0,y=0;
    uint64_t coalesced=0;
    void queue(int nx,int ny) { if(pending)++coalesced;x=nx;y=ny;pending=true; }
    void retry(int nx,int ny) { if(!pending){x=nx;y=ny;pending=true;} }
    bool take(int& nx,int& ny) { if(!pending)return false;nx=x;ny=y;pending=false;return true; }
};

// Presents no intermediate cursor-free state. Dirty scene regions intersecting
// the cursor transaction are absorbed into its final union; unrelated regions
// retain their small transfers. All geometry is clipped before access/present.
template<class Pixel,class Present,class Stage>
GuiDirtyRect kursor_prezentuj_transakcje(uint32_t* scene,int stride,int sw,int sh,
    GuiDirtyRect old,int left,int top,int kw,int kh,uint32_t* scratch,
    const GuiDirtyRect* dirty,uint32_t count,Pixel pixel,Present present,Stage stage) {
    old=kursor_clip(old,sw,sh);
    GuiDirtyRect next=kursor_clip({left,top,kw,kh},sw,sh);
    GuiDirtyRect final=kursor_union(old,next);
    bool absorbed[SKLADACZ_MAKS_DIRTY_RECT]{};
    // Transitive merge prevents a later scene transfer erasing the overlay.
    bool changed;
    do {
        changed=false;
        for(uint32_t i=0;i<count;++i) {
            auto r=kursor_clip(dirty[i],sw,sh);
            if(!absorbed[i]&&kursor_przecina(r,final)) {
                absorbed[i]=true;final=kursor_union(final,r);changed=true;
            }
        }
    } while(changed);
    stage(0); // old background already lives in the clean scene; no transfer
    for(int y=0;y<next.height;++y)for(int x=0;x<next.width;++x)
        scratch[y*next.width+x]=scene[(next.y+y)*stride+next.x+x];
    for(int y=0;y<next.height;++y)for(int x=0;x<next.width;++x) {
        uint32_t c;
        if(pixel(next.x+x-left,next.y+y-top,c))scene[(next.y+y)*stride+next.x+x]=c;
    }
    stage(1);
    stage(2);
    for(uint32_t i=0;i<count;++i)if(!absorbed[i]) {
        auto r=kursor_clip(dirty[i],sw,sh);if(r.width>0)present(r);
    }
    if(final.width>0)present(final);
    stage(3);
    for(int y=0;y<next.height;++y)for(int x=0;x<next.width;++x)
        scene[(next.y+y)*stride+next.x+x]=scratch[y*next.width+x];
    return next;
}
