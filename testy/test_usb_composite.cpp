#define REAL_KEYBOARD 1
#include "testy/pointing_host_stubs.h"
static unsigned keyboard_events{};
extern "C" void klawiatura_obsluz_klawisz(uint16_t,uint32_t){++keyboard_events;}
#include "sterowniki/usb/hid_pointer.h"
#include "sterowniki/usb/usb_hid.h"
#include <vector>
#include <cstring>
static void add(std::vector<uint8_t>& b,std::initializer_list<uint8_t> d){b.insert(b.end(),d);}
static std::vector<uint8_t> composite(){
    std::vector<uint8_t> b={9,2,0,0,4,1,0,0x80,50};
    add(b,{9,4,0,0,1,0xff,0,0,0});add(b,{7,5,1,2,64,0,0});
    add(b,{9,4,2,0,2,3,1,1,0});add(b,{9,0x21,0x11,1,0,1,0x22,63,0});
    add(b,{7,5,2,3,8,0,10});add(b,{7,5,0x82,3,8,0,10});
    add(b,{9,4,7,0,1,3,1,2,0});add(b,{9,0x21,0x11,1,0,1,0x22,52,0});add(b,{7,5,0x85,3,8,0,10});
    add(b,{9,4,9,0,1,3,0,0,0});add(b,{9,0x21,0x11,1,0,1,0x22,20,0});add(b,{7,5,0x86,3,16,0,4});
    b[2]=uint8_t(b.size());b[3]=uint8_t(b.size()>>8);return b;
}
int main(){
    auto b=composite();UsbHidConfiguration c{};
    CHECK(usb_hid_parse_configuration(b.data(),b.size(),c));CHECK(c.count==3);
    CHECK(c.interfaces[0].interface_number==2&&c.interfaces[0].endpoint==0x82);
    CHECK(c.interfaces[1].interface_number==7&&c.interfaces[1].endpoint==0x85);
    CHECK(c.interfaces[2].interface_number==9&&c.interfaces[2].endpoint==0x86);
    UsbHidInterface first{};CHECK(usb_hid_find_interface(b.data(),b.size(),first));CHECK(first.interface_number==2);
    // Every truncated descriptor is rejected; no partial result escapes.
    for(size_t i=0;i<b.size();++i){CHECK(!usb_hid_parse_configuration(b.data(),i,c));CHECK(c.count==0);}
    for(uint8_t bad:{uint8_t(0),uint8_t(1),uint8_t(255)}){auto broken=b;broken[9]=bad;
        CHECK(!usb_hid_parse_configuration(broken.data(),broken.size(),c));CHECK(c.count==0);}
    // Validate trailing descriptors even after valid HID interfaces.
    for(uint8_t bad:{uint8_t(0),uint8_t(1),uint8_t(8)}){auto broken=b;broken[broken.size()-7]=bad;
        CHECK(!usb_hid_parse_configuration(broken.data(),broken.size(),c));}
    // Alternate settings are logged but only active alternate zero is collected.
    auto alternate=b;alternate[28]=1;CHECK(usb_hid_parse_configuration(alternate.data(),alternate.size(),c));CHECK(c.count==2);
    // Unsupported endpoint does not hide later useful HID interfaces.
    for(uint8_t ep:{uint8_t(0x80),uint8_t(0x92),uint8_t(2)}){auto bad=b;bad[52]=ep;
        CHECK(usb_hid_parse_configuration(bad.data(),bad.size(),c));CHECK(c.count==2);CHECK(c.interfaces[0].interface_number==7);}
    auto bulk=b;bulk[53]=2;CHECK(usb_hid_parse_configuration(bulk.data(),bulk.size(),c));CHECK(c.count==2);
    const uint8_t consumer[]={5,0x0c,9,1,0xa1,1,0xc0};
    CHECK(hid_report_roles(consumer,sizeof(consumer))&HID_ROLE_CONSUMER);
    const uint8_t keyboard_descriptor[]={5,1,9,6,0xa1,1,0xc0};
    CHECK(hid_report_roles(keyboard_descriptor,sizeof(keyboard_descriptor))&HID_ROLE_KEYBOARD);
    // Independent keyboard histories and rollover preserve held state.
    input_reset_all();InputDevice a{},d{};uint8_t pa[8]{},pd[8]{};
    const uint8_t down[8]={0,0,4,0,0,0,0,0},up[8]{},rollover[8]={0,0,1,1,1,1,1,1};
    usb_hid_keyboard_report(a,pa,down,8);usb_hid_keyboard_report(d,pd,up,8);
    CHECK(a.registered&&d.registered);CHECK(pa[2]==4&&pd[2]==0);
    usb_hid_keyboard_report(a,pa,rollover,8);CHECK(pa[2]==4);
    usb_hid_keyboard_report(d,pd,down,7);CHECK(pd[2]==0);
    usb_hid_keyboard_report(a,pa,up,8);CHECK(pa[2]==0);
    const uint8_t old_keys[8]={0,0,4,5,6,7,8,9},new_keys[8]={255,0,10,11,12,13,14,15};
    usb_hid_keyboard_report(a,pa,old_keys,8);keyboard_events=0;
    usb_hid_keyboard_report(a,pa,new_keys,8);CHECK(keyboard_events==20);
    const auto valid_events=keyboard_events;usb_hid_keyboard_report(a,pa,rollover,8);
    CHECK(keyboard_events==valid_events);usb_hid_keyboard_report(a,pa,up,8);CHECK(keyboard_events==valid_events+14);
    input_reset_all();printf("USB composite parser/keyboard isolation: %u checks PASS\n",checks);
}
