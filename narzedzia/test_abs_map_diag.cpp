#include "../abs_map_diag.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "../input_device.h"
static uint64_t now_ns=100;
static int logical_x=0,logical_y=0;
static unsigned geom_snapshots=0;
static std::vector<std::string> logs;
void klog_serial(const char* s){logs.emplace_back(s);}
bool hpet_dostepny(){return true;}
uint64_t czas_monotoniczny_ns(){return now_ns;}
int grafika_pobierz_szerokosc(){return 1280;}
int grafika_pobierz_wysokosc(){return 720;}
extern "C" int grafika_pobierz_mysz_x(){return logical_x;}
extern "C" int grafika_pobierz_mysz_y(){return logical_y;}
void grafika_abs_map_geometria(){++geom_snapshots;}
extern "C" void input_abs_map_diag_obsluz();
extern "C" void zaktualizuj_mysze_pozycja_zrodlo(int x,int y,uint8_t,const char*){logical_x=x;logical_y=y;}
extern "C" void zaktualizuj_mysze_zrodlo(int,int,uint8_t,const char*){}
extern "C" void zaktualizuj_kolo_myszy(int,int,uint8_t){}
extern "C" void klawiatura_obsluz_klawisz(uint16_t,uint32_t){}
int main() {
    AbsMapSample a;
    for(int i=0;i<1000;++i) {
        a.record(i,i*2,true,true,0,999,0,1998,i/10,i/20,1280,720,100+i);
        assert(!a.due(100+i));
    }
    assert(a.stalled_x>0&&a.stalled_y>0);
    assert(a.endpoints==15); // both endpoints retained despite throttling
    assert(a.low_x==0&&a.high_x==999&&a.low_y==0&&a.high_y==1998);
    assert(a.due(1000000000));a.consumed(1000000000);
    assert(!a.due(2000000000)); // idle generates no repeated log
    a.record(999,1998,true,false,0,999,0,1998,1279,719,1280,720,1100000000);
    assert(!a.due(1100000000));
    assert(a.due(2000000000)); // corner still logged if no further packets arrive
    assert(a.endpoints==2&&a.near_edge);
    a.consumed(2000000000);
    a.record(50,60,true,true,0,999,0,1998,64,21,1280,720,2100000000);
    assert(a.low_x==50&&a.high_x==50&&a.low_y==60&&a.high_y==60&&a.endpoints==0);
    input_reset_all();InputDevice dev{};
    dev.kinds=INPUT_DEVICE_KIND_TABLET;dev.bus_type=INPUT_BUS_VIRTIO;
    dev.abs_x={101,60001,0,0,0};dev.abs_y={503,9503,0,0,0};
    const char* name="QEMU Virtio Tablet";for(unsigned i=0;name[i];++i)dev.name[i]=name[i];
    assert(input_register_device(&dev));logs.clear();
    auto send=[&](int x,int y){InputEvent es[]={{INPUT_EV_ABS,INPUT_ABS_X,uint32_t(x)},
        {INPUT_EV_ABS,INPUT_ABS_Y,uint32_t(y)},{INPUT_EV_SYN,INPUT_SYN_REPORT,0}};
        input_dispatch_events(&dev,es,3);};
    send(101,503);send(60001,9503);assert(logical_x==1279&&logical_y==719);
    input_abs_map_diag_obsluz();assert(logs.empty());
    now_ns=1000000000;input_abs_map_diag_obsluz();
    assert(logs.size()==1&&geom_snapshots==1);
    assert(logs[0].find("mapped=1279,719")!=std::string::npos);
    assert(logs[0].find("hit_x_min_max=1,1")!=std::string::npos);
    assert(logs[0].find("hit_y_min_max=1,1")!=std::string::npos);
    assert(logs[0].find("observed_x=101,60001")!=std::string::npos);
    now_ns=2000000000;input_abs_map_diag_obsluz();assert(logs.size()==1);
    // A contracted incoming range is reported even though no guest edge is near.
    send(20000,3000);send(30000,4000);input_abs_map_diag_obsluz();
    assert(logs.size()==2&&logs.back().find("observed_x=20000,30000")!=std::string::npos);
    assert(logs.back().find("hit_x_min_max=0,0")!=std::string::npos);
    input_reset_all();
    puts("ABS diagnostics PASS: throttling, raw extrema, endpoints, rounding stalls, idle and reset");
}
