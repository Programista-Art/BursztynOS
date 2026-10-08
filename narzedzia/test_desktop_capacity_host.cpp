// Production layout/parser code: bounded placement, overflow, transitions and fuzz.
#include "../desktop_layout.h"
#include "../desktop_layout_settings.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <climits>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();}}while(0)
using Model=Desktop::Layout<DesktopEntries::ModelCapacity>;
static int measure(const char* text,int dpi) {
    int n=0,pos=0;while(text[pos]){uint32_t cp;int b=BursztynUI::CzcionkaUI::dekoduj_utf8(text,pos,&cp);if(!b)break;pos+=b;++n;}
    return ui_skaluj(n*8,dpi);
}
static void labels(Model& m) {
    auto g=pulpit_metryki(m.dpi);
    for(int i=0;i<m.count;++i)m.items[i].label=Desktop::wrap("f000",g.etykieta_szer,g.wysokosc_linii,g.odstep_linii,16,m.dpi,measure);
}
static int placed(const Model& m){int n=0;for(int i=0;i<m.count;++i)n+=m.items[i].placed;return n;}
static void invariant(const Model& m) {
    CHECK(m.count>=0 && m.count<=Model::capacity);
    CHECK(m.searchIterations<=uint64_t(m.count)*m.candidate_limit());
    int accounted=0;
    for(int i=0;i<m.count;++i) {
        ++accounted;
        if(!m.items[i].placed){CHECK(!m.items[i].selected);continue;}
        CHECK(m.visible(i));
        for(int j=0;j<i;++j)if(m.items[j].placed)CHECK(!Desktop::intersects(m.visual_bounds(i),m.visual_bounds(j)));
    }
    CHECK(accounted==m.count);
}
static void setup(Model& m,int count,int w,int h,unsigned dpi) {
    m={};m.geometry(w,h-ui_skaluj(40,dpi),ui_skaluj(3,dpi),dpi);m.count=count;labels(m);
    for(int i=0;i<count;++i){m.items[i].col=m.cols?i%m.cols:0;m.items[i].row=m.cols?i/m.cols:0;}
    m.reflow();invariant(m);
}
static std::string document(const std::string& body){char end[32];snprintf(end,sizeof(end),"end=%08X\n",DesktopSettings::checksum(body.data(),body.size()));return body+end;}
int main() {
    auto mp=std::make_unique<Model>();auto& m=*mp;
    CHECK(m.set_count(Model::capacity));CHECK(!m.set_count(Model::capacity+1));CHECK(m.count==Model::capacity);CHECK(!m.set_count(-1));
    for(unsigned dpi:{100U,125U}) {
        const int capacity=dpi==100?77:45;
        for(int count:{0,1,capacity-1,capacity,capacity+1,capacity+40,Model::capacity}) {
            setup(m,count,1280,720,dpi);CHECK(placed(m)==(count<capacity?count:capacity));
            auto previous=m;CHECK(m.reflow()==0);CHECK(m.newlyPlaced==0);invariant(m);
            for(int i=0;i<count;++i)if(previous.items[i].placed){auto a=previous.visual_bounds(i),b=m.visual_bounds(i);CHECK(a.x==b.x && a.y==b.y);}
        }
        setup(m,Model::capacity,1280,720,dpi);
        std::vector<int> waiting;for(int i=0;i<m.count;++i)if(!m.items[i].placed)waiting.push_back(i);
        auto first=m.visual_bounds(0);m.items[0].placed=false;m.items[0].label.height=m.workBottom+1;
        m.reflow();CHECK(m.newlyPlaced==1);CHECK(m.items[waiting[0]].placed);CHECK(placed(m)==capacity);invariant(m);
        // Remove several occupying items from the logical model; only the first
        // waiting identities fill the holes, without touching valid positions.
        setup(m,Model::capacity,1280,720,dpi);waiting.clear();
        for(int i=0;i<m.count;++i)if(!m.items[i].placed)waiting.push_back(i);
        for(int i=0;i<3;++i){m.items[i].placed=false;m.items[i].label.height=m.workBottom+1;}
        m.reflow();CHECK(m.newlyPlaced==3);for(int i=0;i<3;++i)CHECK(m.items[waiting[i]].placed);invariant(m);
        // Packed destination and extreme pointer deltas cannot partly translate
        // a group or overflow distance calculations.
        setup(m,capacity+1,1280,720,dpi);m.click(0,false);m.click(1,true);auto a=m.visual_bounds(0),b=m.visual_bounds(1);
        CHECK(m.move_visual_group(INT_MAX,INT_MIN));auto aa=m.visual_bounds(0),bb=m.visual_bounds(1);
        CHECK(aa.x-a.x==bb.x-b.x && aa.y-a.y==bb.y-b.y);invariant(m);
        m.clear_selection();m.click(capacity,false);CHECK(m.selected_count()==0);
        // Expanding places waiting items, preserving every valid icon.
        setup(m,Model::capacity,1280,720,dpi);auto old=m;m.geometry(1920,1080-ui_skaluj(40,dpi),ui_skaluj(3,dpi),dpi);m.reflow();
        CHECK(placed(m)>capacity);for(int i=0;i<old.count;++i)if(old.items[i].placed){auto a=old.visual_bounds(i),b=m.visual_bounds(i);CHECK(a.x==b.x && a.y==b.y);}invariant(m);
        m.geometry(640,480-ui_skaluj(40,dpi),ui_skaluj(3,dpi),dpi);m.reflow();invariant(m);CHECK(placed(m)<capacity);
        m.geometry(1280,720-ui_skaluj(40,dpi)-100,ui_skaluj(3,dpi),dpi);m.reflow();invariant(m);
        // Every launcher receives a chance before user entries; valid positions
        // still reserve their space before waiting priority is consulted.
        setup(m,Model::capacity,1280,720,dpi);
        for(int i=0;i<m.count;++i){m.items[i].placed=false;m.items[i].launcher=i>=DesktopEntries::FileCapacity;}
        m.reflow();for(int i=DesktopEntries::FileCapacity;i<m.count;++i)CHECK(m.items[i].placed);invariant(m);
        (void)first;
    }
    auto state=std::make_unique<DesktopSettings::State>();auto reload=std::make_unique<DesktopSettings::State>();
    std::vector<std::string> names(Model::capacity);for(int i=0;i<Model::capacity;++i)names[i]="żółć-"+std::to_string(i);
    auto key=[&](int i){return DesktopSettings::Key{'F',names[i].c_str()};};
    setup(m,Model::capacity,1280,720,125);size_t size=state->serialize(m,key);CHECK(size>0 && size<DesktopSettings::BufferCapacity);
    std::string saved(state->buffer,size);memcpy(reload->buffer,saved.data(),size);CHECK(reload->parse(size));CHECK(reload->count==placed(m));
    auto old=m;reload->apply(m,key);invariant(m);for(int i=0;i<m.count;++i){CHECK(m.items[i].placed==old.items[i].placed);if(m.items[i].placed){auto a=m.visual_bounds(i),b=old.visual_bounds(i);CHECK(a.x==b.x && a.y==b.y);}}
    CHECK(state->serialize(m,key)==size);CHECK(saved==state->buffer);
    auto parse=[&](const std::string& body){auto text=document("BursztynDesktopLayout=1\n"+body);memcpy(state->buffer,text.data(),text.size());return state->parse(text.size());};
    CHECK(parse("F\ta\t-1\t0\nF\tb\t2147483647\t0\nF\tc\t999999999999999999\t0\nF\td\t1000001\t0\n"));CHECK(state->count==0);
    CHECK(parse("F\ta\t0\t0\nF\ta\t12\t12\nF\tb\t0\t0\nL\tunknown\t0\t0\n"));CHECK(state->count==3);
    CHECK(parse("F\t%C0%AF\t0\t0\nF\t%ED%A0%80\t0\t0\nF\t%F4%90%80%80\t0\t0\nF\t%E2\t0\t0\n"));CHECK(state->count==0);
    std::string many;for(int i=0;i<Model::capacity+1;++i)many+="F\t"+std::to_string(i)+"\t0\t0\n";
    CHECK(!parse(many));CHECK(state->count==0);
    CHECK(!DesktopSettings::identity(std::string(512,'a').c_str()));CHECK(DesktopSettings::identity((std::string(500,'a')+"żółć").c_str()));
    std::mt19937 rng(0x42555253);
    for(int run=0;run<600;++run) {
        unsigned dpi=100+25*(rng()%5);int width=1+rng()%2400,height=1+rng()%1400;
        setup(m,rng()%(Model::capacity+1),width,height,dpi);
        for(int i=0;i<m.count;++i) {
            auto& p=m.items[i];p.placed=rng()%2;p.selected=true;p.custom=true;p.launcher=i%11==0;
            p.col=int(rng()%120)-30;p.row=int(rng()%100)-25;p.offsetX=int(rng()%1000)-500;p.offsetY=int(rng()%800)-400;
            if(i%17==0){p.col=INT_MAX;p.offsetY=INT_MIN;}
            if(i%7==0){auto g=pulpit_metryki(dpi);p.label=Desktop::wrap("żółć dłuższa nazwa o podobnym początku",g.etykieta_szer,g.wysokosc_linii,g.odstep_linii,16,dpi,measure);}
        }
        const int logical=m.count;m.reflow();CHECK(m.count==logical);invariant(m);
        auto previous=m;m.reflow();CHECK(m.newlyPlaced==0);invariant(m);
        for(int i=0;i<m.count;++i)if(previous.items[i].placed){auto a=previous.visual_bounds(i),b=m.visual_bounds(i);CHECK(a.x==b.x && a.y==b.y);}
        if(m.workWidth>0 && m.workBottom>0){auto bytes=state->serialize(m,key);CHECK(bytes>0 && bytes<DesktopSettings::BufferCapacity);}
    }
    m.geometry(INT_MAX,INT_MAX,INT_MIN,UINT_MAX);CHECK(m.workWidth==8192 && m.workBottom==8192);m.reflow();invariant(m);
    printf("Desktop geometry/capacity/persistence fuzz: %u/%u PASS (600 deterministic layouts)\n",checks,checks);
}
