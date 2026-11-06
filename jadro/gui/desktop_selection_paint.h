#pragma once
#include "bws_gui_batch.h"
#include "bursztyn_ui/rysowanie.h"

// Reused patch storage; no full-screen shadow, no rectangle history. Regions
// deliberately remain separate: joining four border strips dirties the whole
// rubber-band interior. Overlap is harmless because patches are replacements
// of the same final scene, never source-over onto a published pixel.
class DesktopSelectionPaint {
    BwsGuiRegion regions_[BWS_GUI_BATCH_MAX]{};
    uint32_t count_=0;
    uint64_t used_=0,capacity_=0;
    uint32_t* pixels_=nullptr;
    bool valid_=true;
public:
    ~DesktopSelectionPaint(){delete[] pixels_;}
    void begin(){count_=0;used_=0;valid_=true;}
    bool add(Desktop::Prostokat r,Desktop::Prostokat clip) {
        r=r.przeciecie(clip);
        if(r.czy_pusty())return valid_;
        for(int y=r.y;y<r.dolny();y+=2048)for(int x=r.x;x<r.prawy();x+=2048) {
            int w=r.prawy()-x,h=r.dolny()-y;if(w>2048)w=2048;if(h>2048)h=2048;
            uint64_t n=uint64_t(w)*h;
            if(count_==BWS_GUI_BATCH_MAX || used_+n>BWS_GUI_BATCH_PIXELS_MAX)return valid_=false;
            regions_[count_++]={x,y,w,h,used_};used_+=n;
        }
        return valid_;
    }
    bool border(Desktop::Prostokat r,int t,Desktop::Prostokat clip) {
        if(r.czy_pusty())return valid_;
        if(t>r.szerokosc)t=r.szerokosc;
        if(t>r.wysokosc)t=r.wysokosc;
        add({r.x,r.y,r.szerokosc,t},clip);
        add({r.x,r.dolny()-t,r.szerokosc,t},clip);
        add({r.x,r.y,t,r.wysokosc},clip);
        return add({r.prawy()-t,r.y,t,r.wysokosc},clip);
    }
    using Draw=void (*)(void*,Desktop::Prostokat);
    __attribute__((noinline)) bool finish(BursztynUI::KontekstRysowania& ctx,Draw draw,void* data,uint32_t generation) {
        if(!valid_)return false;
        if(!count_)return true;
        if(used_>capacity_) {
            uint64_t next_capacity=capacity_?capacity_:4096;
            while(next_capacity<used_)next_capacity*=2;
            auto* next=new uint32_t[next_capacity];
            if(!next)return false;
            delete[] pixels_;pixels_=next;capacity_=next_capacity;
        }
        for(uint32_t i=0;i<count_;++i) {
            auto& p=regions_[i];auto* out=pixels_+p.pixels;
            for(uint64_t n=0;n<uint64_t(p.width)*p.height;++n)out[n]=0;
            BursztynUI::Prostokat r{p.x,p.y,p.width,p.height};
            ctx.ustaw_cel(out,r);ctx.dodaj_przyciecie(r);
            draw(data,Desktop::Prostokat{p.x,p.y,p.width,p.height});
            ctx.zdejmij_przyciecie();ctx.ustaw_cel(nullptr,{});
        }
        // Convert offsets only once the growable backing allocation is final.
        for(uint32_t i=0;i<count_;++i)regions_[i].pixels=reinterpret_cast<uint64_t>(pixels_+regions_[i].pixels);
        return gui_zastap_regiony(regions_,count_,generation);
    }
    uint64_t pixels()const{return used_;}
    uint64_t capacity()const{return capacity_;}
    uint32_t count()const{return count_;}
};
