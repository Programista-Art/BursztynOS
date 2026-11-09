#include "input_device.h"
#include "pointer_coordinates.h"
#include "sterowniki/usb/hid_pointer.h"
#include "testy/pointing_test_descriptors.h"
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <algorithm>
static unsigned checks=0,diagnostics=0;
static int width=1280,height=720,px=640,py=360;
static uint8_t buttons=0;
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
void klog_serial(const char*){++diagnostics;}
int grafika_pobierz_szerokosc(){return width;}
int grafika_pobierz_wysokosc(){return height;}
extern "C" int grafika_pobierz_mysz_x(){return px;}
extern "C" int grafika_pobierz_mysz_y(){return py;}
static void record(const char* kind,int64_t x,int64_t y,uint8_t b){printf("GUI %s %lld %lld %u %d %d\n",kind,(long long)x,(long long)y,b,px,py);buttons=b;}
extern "C" void zaktualizuj_mysze_zrodlo(int dx,int dy,uint8_t b,const char*){
    const int64_t x=int64_t(px)+dx,y=int64_t(py)-dy;
    if(width>0&&height>0){px=int(std::clamp(x,int64_t(0),int64_t(width)-1));py=int(std::clamp(y,int64_t(0),int64_t(height)-1));}
    record("relative",dx,dy,b);
}
extern "C" void zaktualizuj_mysze_pozycja_zrodlo(int x,int y,uint8_t b,const char*){px=x;py=y;record("absolute",x,y,b);}
extern "C" void zaktualizuj_mysze_wzgledne_zrodlo(int32_t dx,int32_t dy,uint8_t b,const char*){
    const int64_t x=int64_t(px)+dx,y=int64_t(py)+dy;
    if(width>0&&height>0){px=int(std::clamp(x,int64_t(0),int64_t(width)-1));py=int(std::clamp(y,int64_t(0),int64_t(height)-1));}
    record("relative",dx,-int64_t(dy),b);
}
extern "C" void zaktualizuj_kolo_myszy(int x,int y,uint8_t b){record("wheel",x,y,b);}
extern "C" void klawiatura_obsluz_klawisz(uint16_t,uint32_t){}
static void state(const char* stage,const InputDevice& t,const InputDevice& u){
    printf("STATE %s cursor=%d,%d buttons=%u touch=%u,%u,%u,%u usb=%u,%u,%u preferred=%u last=%u\n",stage,px,py,buttons,
        t.registered,t.pointer_state,t.contact_active,t.published_buttons,u.registered,u.pointer_state,u.published_buttons,
        input_get_pointer_owner()?input_get_pointer_owner()->device_id:0,input_get_last_pointer_source()?input_get_last_pointer_source()->device_id:0);
}
static void relative(InputDevice& d,int x,int y){const InputEvent e[]={{INPUT_EV_REL,INPUT_REL_X,uint32_t(x)},
    {INPUT_EV_REL,INPUT_REL_Y,uint32_t(y)},{INPUT_EV_SYN,0,0}};input_dispatch_events(&d,e,3);}
static void absolute(InputDevice& d,int x,int y,bool contact){const InputEvent e[]={{INPUT_EV_ABS,INPUT_ABS_X,uint32_t(x)},
    {INPUT_EV_ABS,INPUT_ABS_Y,uint32_t(y)},{INPUT_EV_KEY,INPUT_BTN_TOUCH,uint32_t(contact)},{INPUT_EV_SYN,0,0}};input_dispatch_events(&d,e,4);}
static InputDevice touchpad(){InputDevice d{};d.device_id=100;d.bus_type=INPUT_BUS_PS2;d.kinds=INPUT_DEVICE_KIND_TABLET;
    d.pointer_mode=INPUT_POINTER_ABSOLUTE;d.abs_contact_required=true;d.abs_x={0,1279,0,0,0};d.abs_y={0,719,0,0,0};return d;}
static void boot_report(InputDevice& u,const HidPointerLayout& l,int x,int y,uint8_t b=0){
    uint8_t report[]={b,uint8_t(x),uint8_t(y)};CHECK(hid_pointer_publish(u,l,report,sizeof(report),200));}
static void coexistence(){
    input_reset_all();input_set_pointer_ready(true);width=1280;height=720;px=640;py=360;buttons=0;
    auto t=touchpad();InputDevice u{};u.usb_controller=INPUT_USB_CONTROLLER_XHCI;HidPointerLayout l{};
    CHECK(hid_boot_mouse_layout(l));CHECK(input_register_device(&t));CHECK(!input_pointer_is_active(&t));CHECK(px==640&&py==360);
    absolute(t,400,200,true);CHECK(px==400&&py==200);CHECK(input_pointer_is_active(&t));state("touchpad first report",t,u);
    CHECK(hid_pointer_register(u,l,200));CHECK(!input_pointer_is_active(&u));CHECK(px==400&&py==200);state("USB registration",t,u);
    CHECK(input_arm_pointer(&u));CHECK(px==400&&py==200);uint8_t invalid[]={0,1};
    CHECK(!hid_pointer_publish(u,l,invalid,sizeof(invalid),200));CHECK(!input_pointer_is_active(&u));
    boot_report(u,l,0,0);CHECK(px==400&&py==200);CHECK(input_pointer_is_active(&t)&&input_pointer_is_active(&u));
    state("USB stationary proof",t,u);
    absolute(t,400,200,false);boot_report(u,l,-20,30);CHECK(px==380&&py==230);
    for(unsigned i=0;i<8;++i){absolute(t,400,200,false);CHECK(px==380&&py==230);}
    state("inactive contact cannot overwrite",t,u);
    absolute(t,1000,600,true);CHECK(px==1000&&py==600);boot_report(u,l,10,-15);CHECK(px==1010&&py==585);
    absolute(t,500,250,true);CHECK(px==500&&py==250);boot_report(u,l,-7,-9);CHECK(px==493&&py==241);
    CHECK(input_pointer_is_active(&t)&&input_pointer_is_active(&u));state("alternating sources",t,u);
    // A finger resting at unchanged coordinates must not fight USB movement.
    for(unsigned i=0;i<8;++i){absolute(t,500,250,true);CHECK(px==493&&py==241);}
    absolute(t,500,250,false);CHECK(px==493&&py==241);
    absolute(t,500,250,true);CHECK(px==500&&py==250); // a new contact is intentional
    boot_report(u,l,-7,-9);CHECK(px==493&&py==241);
    // A held touchpad contact is not released by another source's empty buttons.
    CHECK(buttons==1);boot_report(u,l,1,1);CHECK(buttons==1);
    absolute(t,500,250,false);CHECK(buttons==0);boot_report(u,l,0,0,2);CHECK(buttons==2);
    absolute(t,10,10,true);CHECK(buttons==3);boot_report(u,l,0,0);CHECK(buttons==1);
    const int x=px,y=py;hid_pointer_remove(u);CHECK(px==x&&py==y);CHECK(input_pointer_is_active(&t));
    absolute(t,600,300,true);CHECK(px==600&&py==300);state("USB removed",t,u);
    CHECK(hid_pointer_register(u,l,200));CHECK(px==600&&py==300);boot_report(u,l,0,0);CHECK(px==600&&py==300);
    absolute(t,600,300,false);boot_report(u,l,-127,-127);CHECK(px==473&&py==173);state("USB reconnected",t,u);
    boot_report(u,l,-127,-127);boot_report(u,l,-127,-127);boot_report(u,l,-127,-127);CHECK(px==92&&py==0);
    boot_report(u,l,-127,0);CHECK(px==0&&py==0);absolute(t,1279,719,true);boot_report(u,l,127,127);CHECK(px==1279&&py==719);
    state("clamped edges",t,u);
    input_reset_all();
}
static void geometry(){
    for(bool usb_first:{false,true}){
        input_reset_all();width=height=0;px=640;py=360;buttons=0;input_set_pointer_ready(false);
        auto t=touchpad();InputDevice u{};HidPointerLayout l{};CHECK(hid_boot_mouse_layout(l));
        CHECK(input_register_device(&t));CHECK(hid_pointer_register(u,l,200));
        if(usb_first){boot_report(u,l,-10,-20);absolute(t,300,200,true);}
        else {absolute(t,300,200,true);boot_report(u,l,-10,-20);}
        CHECK(px==640&&py==360);CHECK(input_pointer_is_active(&t)&&input_pointer_is_active(&u));
        width=1280;input_service_all();CHECK(px==640&&py==360);height=720;input_service_all();CHECK(px==640&&py==360);
        input_set_pointer_ready(true);input_service_all();CHECK(px==(usb_first?300:290)&&py==(usb_first?200:180));
        state(usb_first?"deferred USB then touchpad":"deferred touchpad then USB",t,u);
        const int x=px,y=py;input_service_all();CHECK(px==x&&py==y);
        input_reset_all();
    }
    // Removing a source cancels only its queued reports, even on address reuse.
    input_set_pointer_ready(false);auto t=touchpad();InputDevice u{};HidPointerLayout l{};CHECK(hid_boot_mouse_layout(l));
    CHECK(input_register_device(&t));CHECK(hid_pointer_register(u,l,200));absolute(t,500,300,true);boot_report(u,l,100,100);
    hid_pointer_remove(u);CHECK(hid_pointer_register(u,l,200));input_set_pointer_ready(true);input_service_all();CHECK(px==500&&py==300);
    CHECK(!input_pointer_is_active(&u));state("cancel disconnected pending reports",t,u);input_reset_all();
}
static void contactless_tablet(){
    input_set_pointer_ready(true);width=1280;height=720;px=640;py=360;
    auto t=touchpad();t.abs_contact_required=false;InputDevice u{};HidPointerLayout l{};CHECK(hid_boot_mouse_layout(l));
    CHECK(input_register_device(&t));CHECK(hid_pointer_register(u,l,200));
    const InputEvent report[]={{INPUT_EV_ABS,INPUT_ABS_X,700},{INPUT_EV_ABS,INPUT_ABS_Y,400},{INPUT_EV_SYN,0,0}};
    input_dispatch_events(&t,report,3);CHECK(px==700&&py==400);boot_report(u,l,20,10);CHECK(px==720&&py==410);
    for(unsigned i=0;i<10;++i){input_dispatch_events(&t,report,3);CHECK(px==720&&py==410);}
    state("stationary contactless tablet",t,u);input_reset_all();
}
static void signed_and_modes(){
    for(uint32_t bus:{uint32_t(INPUT_BUS_PS2),uint32_t(INPUT_BUS_VIRTIO)}){
        InputDevice r{};r.bus_type=bus;r.kinds=INPUT_DEVICE_KIND_MOUSE;CHECK(input_register_device(&r));px=640;py=360;
        relative(r,-17,29);CHECK(px==623&&py==389);relative(r,INT_MAX,INT_MAX);CHECK(px==1279&&py==719);
        relative(r,INT_MIN,INT_MIN);CHECK(px==0&&py==0);CHECK(input_pointer_is_active(&r));input_reset_all();
    }
    // A descriptor-defined Tip Switch was previously discarded by the parser.
    auto descriptor=tablet_descriptor(false,0,0,1279);descriptor.resize(descriptor.size()-2);
    descriptor.insert(descriptor.end(),{5,13,9,0x42,0x15,0,0x25,1,0x75,1,0x95,1,0x81,2,0xc0,0xc0});
    HidPointerLayout l{};CHECK(hid_pointer_compile(descriptor.data(),descriptor.size(),l));CHECK(l.contact);
    InputDevice t{},u{};CHECK(hid_pointer_register(t,l,100));CHECK(t.abs_contact_required);
    const int x=px,y=py;auto report=tablet_report(l,600,300);CHECK(hid_pointer_publish(t,l,report.data(),report.size(),100));
    CHECK(px==x&&py==y); // tip=0 never publishes these coordinates
    for(unsigned i=0;i<l.field_count;++i)if(l.fields[i].page==13){const unsigned bit=l.fields[i].offset;report[bit/8]|=uint8_t(1U<<(bit%8));}
    CHECK(hid_pointer_publish(t,l,report.data(),report.size(),100));CHECK(px==600&&py==168);
    state("descriptor Tip Switch",t,u);input_reset_all();
}
int main(){coexistence();geometry();contactless_tablet();signed_and_modes();printf("Input coexistence: %u checks PASS\n",checks);}
