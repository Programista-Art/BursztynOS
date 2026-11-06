#include "testy/pointing_host_stubs.h"
#include "ps2_mouse_parser.h"
#include "sterowniki/usb/hid_pointer.h"
#include "testy/pointing_test_descriptors.h"
#include <algorithm>
#include <random>
#include "narzedzia/virtualbox_tablet_descriptor.h"

void ps2_tests(){
    Ps2MouseParser p{};Ps2MouseReport r{};CHECK(p.configure(0));p.streaming=true;
    for(int x=-128;x<=127;++x)for(int y=-128;y<=127;++y){
        const uint8_t header=uint8_t(8|(x<0?16:0)|(y<0?32:0));
        CHECK(!p.feed(0x21,header,r));CHECK(!p.feed(0x21,uint8_t(x),r));
        CHECK(p.feed(0x21,uint8_t(y),r));CHECK(r.dx==x&&r.dy==-y);
        CHECK(r.dx>=-128&&r.dx<=127&&r.dy>=-127&&r.dy<=128);
    }
    for(unsigned b=0;b<8;++b){CHECK(!p.feed(0x21,uint8_t(8|b),r));CHECK(!p.feed(0x21,0,r));CHECK(p.feed(0x21,0,r));CHECK(r.buttons==b);}
    for(uint8_t overflow:{uint8_t(0x40),uint8_t(0x80),uint8_t(0xC0)}){
        p.feed(0x21,uint8_t(8|overflow),r);p.feed(0x21,127,r);CHECK(!p.feed(0x21,127,r));CHECK(!p.index);
    }
    for(unsigned b=0;b<256;++b)if(!(b&8)){CHECK(!p.feed(0x21,uint8_t(b),r));CHECK(!p.index);}
    p.feed(0x21,8,r);CHECK(p.index==1);CHECK(!p.feed(1,0xFF,r));CHECK(p.index==1);
    CHECK(!p.feed(0x21,1,r));CHECK(p.index==2);CHECK(p.feed(0x21,1,r));CHECK(r.dx==1&&r.dy==-1);
    p.feed(0x21,8,r);CHECK(!p.feed(0xE1,0xFF,r));CHECK(!p.index);
    p.feed(0x21,8,r);p.feed(0x21,0xFF,r);CHECK(!p.feed(0x21,0,r));CHECK(!p.index);
    p.streaming=false;for(uint8_t reply:{uint8_t(0xFA),uint8_t(0xFE),uint8_t(0xAA),uint8_t(0),uint8_t(3),uint8_t(4)})CHECK(!p.feed(0x21,reply,r));CHECK(!p.index);
    for(uint8_t id:{uint8_t(3),uint8_t(4)}){CHECK(p.configure(id));p.streaming=true;
        p.feed(0x21,0x38,r);p.feed(0x21,0xFF,r);CHECK(!p.feed(0x21,0xFF,r));CHECK(p.index==3);
        CHECK(p.feed(0x21,id==3?0xFF:0x3F,r));CHECK(r.dx==-1&&r.dy==1&&r.wheel==-1);CHECK(r.buttons==(id==4?24:0));
    }
    std::mt19937 noise(99);
    for(uint8_t id:{uint8_t(0),uint8_t(3),uint8_t(4)}) {
        CHECK(p.configure(id));p.streaming=true;
        for(unsigned trial=0;trial<128;++trial) {
            for(unsigned i=0;i<64;++i)if(p.feed(0x21,uint8_t(noise()),r))
                CHECK(r.dx>=-128&&r.dx<=127&&r.dy>=-127&&r.dy<=128);
            // Inert trailing bytes finish/reject a partial noise packet;
            // the next complete valid report must be accepted unchanged.
            for(unsigned i=0;i<4;++i)p.feed(0x21,0,r);
            CHECK(p.index==0);CHECK(!p.feed(0x21,0x0B,r));CHECK(!p.feed(0x21,2,r));
            bool complete=p.feed(0x21,3,r);
            if(id){CHECK(!complete);complete=p.feed(0x21,1,r);}
            CHECK(complete);CHECK(r.dx==2&&r.dy==-3&&r.buttons==3);
        }
    }
    CHECK(!p.configure(5));CHECK(!p.streaming);
}
void hid_tests(){
    for(bool numbered:{false,true})for(unsigned padding:{0U,5U,8U})for(int16_t min:{int16_t(0),int16_t(100),int16_t(-1000)}) {
        auto d=tablet_descriptor(numbered,padding,min,2000);HidPointerLayout l{};CHECK(hid_pointer_compile(d.data(),d.size(),l));CHECK(l.absolute);CHECK(l.x.min==min&&l.x.max==2000);
        for(int val:{min==0?0:int(min)-1,int(min),(int(min)+2000)/2,2000,2001}){
            auto r=tablet_report(l,val,val,7,-1);InputEvent ev[65];size_t n=0;CHECK(hid_pointer_decode(l,r.data(),r.size(),ev,65,n));CHECK(n==7);
            CHECK(ev[0].value==1&&ev[1].value==1&&ev[2].value==1);CHECK(int32_t(ev[3].value)==-1);
            CHECK(int32_t(ev[4].value)==std::clamp(val,int(min),2000));
            CHECK(!hid_pointer_decode(l,r.data(),r.size()-1,ev,65,n));CHECK(n==0);
            r.push_back(0xFF);CHECK(hid_pointer_decode(l,r.data(),r.size(),ev,65,n));
            CHECK(!hid_pointer_decode(l,r.data(),r.size(),ev,2,n));
        }
        for(size_t len=0;len<d.size();++len)CHECK(!hid_pointer_compile(d.data(),len,l));
    }
    {
        HidPointerLayout l{};CHECK(hid_pointer_compile(virtualbox_tablet_descriptor,sizeof(virtualbox_tablet_descriptor),l));
        CHECK(l.absolute&&l.field_count==9&&l.bits[0]==64);
        uint8_t report[]={7,0xFF,1,0,0xFF,0x7F,0xFF,0x7F};InputEvent ev[65];size_t n=0;
        CHECK(hid_pointer_decode(l,report,sizeof(report),ev,65,n));CHECK(n==10);
        CHECK(int32_t(ev[5].value)==-1&&ev[6].code==INPUT_REL_HWHEEL&&ev[6].value==1);
        CHECK(ev[7].value==32767&&ev[8].value==32767);
        auto d=tablet_descriptor(false,5,0,255,false,8);CHECK(hid_pointer_compile(d.data(),d.size(),l));
        auto r=tablet_report(l,255,128,5);CHECK(hid_pointer_decode(l,r.data(),r.size(),ev,65,n));CHECK(ev[4].value==255&&ev[5].value==128);
        // 32-bit signed extremes remain representable, including INT32_MIN.
        d=tablet_descriptor(false,5,-1000,2000,false,32);
        for(size_t i=0;i+2<d.size();++i)if(d[i]==0x16&&d[i+1]==0x18&&d[i+2]==0xFC){
            d.erase(d.begin()+i,d.begin()+i+6);d.insert(d.begin()+i,{0x17,0,0,0,0x80,0x27,0xFF,0xFF,0xFF,0x7F});break;}
        CHECK(hid_pointer_compile(d.data(),d.size(),l));r=tablet_report(l,INT_MIN,INT_MAX);
        CHECK(hid_pointer_decode(l,r.data(),r.size(),ev,65,n));CHECK(int32_t(ev[4].value)==INT_MIN&&int32_t(ev[5].value)==INT_MAX);
        // Report ID table and report length limits reject attempted expansion.
        d=tablet_descriptor(true);d.resize(d.size()-2);
        for(unsigned id=8;id<24;++id)d.insert(d.end(),{0x85,uint8_t(id),0x81,3});
        d.insert(d.end(),{0xC0,0xC0});CHECK(!hid_pointer_compile(d.data(),d.size(),l));
        d=tablet_descriptor();d.resize(d.size()-2);
        for(unsigned i=0;i<2;++i)d.insert(d.end(),{0x75,32,0x95,128,0x81,3});
        d.insert(d.end(),{0xC0,0xC0});CHECK(!hid_pointer_compile(d.data(),d.size(),l));
    }
    {
        auto d=tablet_descriptor();HidPointerLayout l{};InputEvent ev[65];size_t n=0;
        // Axes precede buttons and wheel: decoding must follow offsets, not structs.
        const uint8_t axis_usage[]={9,0x30};
        const auto axis=std::search(d.begin(),d.end(),axis_usage,axis_usage+2);
        const size_t offset=size_t(axis-d.begin());
        std::vector<uint8_t> reordered(d.begin(),d.begin()+10);
        reordered.insert(reordered.end(),d.begin()+offset,d.end()-2);
        reordered.insert(reordered.end(),d.begin()+10,d.begin()+offset);
        reordered.insert(reordered.end(),{0xC0,0xC0});
        CHECK(hid_pointer_compile(reordered.data(),reordered.size(),l));
        auto r=tablet_report(l,1234,2345,7,-1);CHECK(hid_pointer_decode(l,r.data(),r.size(),ev,65,n));
        CHECK(ev[0].code==INPUT_ABS_X&&ev[0].value==1234&&ev[1].value==2345);
        CHECK(ev[2].code==INPUT_BTN_LEFT&&ev[2].value==1&&ev[4].value==1);
        // Eight one-bit buttons fill a byte; unsupported button usages 6-8
        // still contribute to the following axes' bit offsets.
        d=tablet_descriptor(false,0);
        for(size_t i=0;i+1<d.size();++i){if(d[i]==0x29&&d[i+1]==3)d[i+1]=8;
            if(d[i]==0x95&&d[i+1]==3)d[i+1]=8;}
        CHECK(hid_pointer_compile(d.data(),d.size(),l));CHECK(l.field_count==8);
        r=tablet_report(l,1234,2345,31);CHECK(hid_pointer_decode(l,r.data(),r.size(),ev,65,n));
        CHECK(ev[6].value==1234&&ev[7].value==2345);
        for(unsigned i=0;i<5;++i)CHECK(ev[i].value==1);
        d=tablet_descriptor();d.insert(d.begin()+10,{0xA4,0x05,0xFF,0x75,7,0xB4});
        CHECK(hid_pointer_compile(d.data(),d.size(),l));CHECK(l.absolute);
        // Item count exhaustion without growing report offsets or allocations.
        d=tablet_descriptor();d.resize(d.size()-2);
        for(unsigned i=0;i<1024;++i)d.insert(d.end(),{0x65,0});
        d.insert(d.end(),{0xC0,0xC0});CHECK(!hid_pointer_compile(d.data(),d.size(),l));
    }
    auto relative=tablet_descriptor(false,5,-127,127,true);HidPointerLayout l{};CHECK(hid_pointer_compile(relative.data(),relative.size(),l));CHECK(!l.absolute);
    auto report=tablet_report(l,-1,1);InputEvent ev[65];size_t n=0;CHECK(hid_pointer_decode(l,report.data(),report.size(),ev,65,n));CHECK(ev[4].type==INPUT_EV_REL&&ev[4].code==INPUT_REL_X);
    report=tablet_report(l,200,1);CHECK(!hid_pointer_decode(l,report.data(),report.size(),ev,65,n));CHECK(n==0);
    auto d=tablet_descriptor();
    // Huge global values, invalid logical ranges, malformed long item and stack.
    for(auto prefix:std::vector<std::vector<uint8_t>>{{0x77,0xFF,0xFF,0xFF,0xFF},{0x97,0xFF,0xFF,0xFF,0xFF},{0x75,33},{0x95,129},{0x85,0},{0xFE,4},{0xB4},{0xA4,0xA4,0xA4,0xA4,0xA4}}){
        prefix.insert(prefix.end(),d.begin(),d.end());CHECK(!hid_pointer_compile(prefix.data(),prefix.size(),l));}
    auto invalid=tablet_descriptor(false,5,5,5);CHECK(!hid_pointer_compile(invalid.data(),invalid.size(),l));
    invalid.resize(4097);CHECK(!hid_pointer_compile(invalid.data(),invalid.size(),l));
    // Unknown Usage Page 0xff is ignored, while still consuming its input bits.
    auto unknown=d;unknown.insert(unknown.end()-2,{0x06,0xFF,0,9,1,0x75,8,0x95,1,0x81,2});CHECK(hid_pointer_compile(unknown.data(),unknown.size(),l));CHECK(l.field_count==6);
    auto multi=tablet_descriptor(true);multi.resize(multi.size()-2);
    multi.insert(multi.end(),{0x85,9,9,0x30,9,0x31,0x81,2,0xC0,0xC0});
    CHECK(hid_pointer_compile(multi.data(),multi.size(),l));CHECK(l.id_count==2);
    for(uint8_t id:{uint8_t(7),uint8_t(9)}){report=tablet_report(l,200,300,0,0,id);CHECK(hid_pointer_decode(l,report.data(),report.size(),ev,65,n));}
    report[0]=42;CHECK(!hid_pointer_decode(l,report.data(),report.size(),ev,65,n));
    // Bounded fuzz: malformed lengths/items never escape buffers.
    std::mt19937 rng(789);for(unsigned i=0;i<4000;++i){auto fuzz=d;fuzz[rng()%fuzz.size()]=uint8_t(rng());
        if(hid_pointer_compile(fuzz.data(),fuzz.size(),l)){uint8_t r[512]{};hid_pointer_decode(l,r,sizeof(r),ev,65,n);}CHECK(l.field_count<=64);}
}
void usb_tests(){
    auto d=usb_configuration();UsbHidInterface u{};CHECK(usb_hid_find_interface(d.data(),d.size(),u));CHECK(u.endpoint==0x81&&u.report_length==64&&u.packet_size==8);
    for(size_t n=0;n<d.size();++n)CHECK(!usb_hid_find_interface(d.data(),n,u));
    for(unsigned offset:{0U,9U,18U,27U}){auto bad=d;bad[offset]=1;CHECK(!usb_hid_find_interface(bad.data(),bad.size(),u));}
    for(unsigned offset:{23U,25U,28U,29U,30U,33U}){auto bad=d;bad[offset]=0;CHECK(!usb_hid_find_interface(bad.data(),bad.size(),u));}
    auto bad=d;bad[23]=0xFF;CHECK(!usb_hid_find_interface(bad.data(),bad.size(),u));
    bad=d;bad[25]=0xFF;bad[26]=0xFF;CHECK(!usb_hid_find_interface(bad.data(),bad.size(),u));
}
void ownership_tests(){
    for(auto bounds:std::vector<std::pair<int,int>>{{1280,720},{1920,1080},{1,1},{INT_MAX,INT_MAX}}){
        width=bounds.first;height=bounds.second;px=width/2;py=height/2;
        input_reset_all();InputDevice ps2{},usb{},virtio{};ps2.kinds=INPUT_DEVICE_KIND_MOUSE;ps2.bus_type=INPUT_BUS_PS2;
        CHECK(input_register_device(&ps2));CHECK(input_get_pointer_owner()==&ps2);
        const int oldx=px,oldy=py;auto d=tablet_descriptor(false,0,-1000,2000);HidPointerLayout l{};CHECK(hid_pointer_compile(d.data(),d.size(),l));
        uint8_t invalid[]={0};CHECK(!hid_pointer_publish(usb,l,invalid,sizeof(invalid),1));CHECK(input_get_pointer_owner()==&ps2);CHECK(px==oldx&&py==oldy);
        auto r=tablet_report(l,-1000,-1000,7);CHECK(hid_pointer_publish(usb,l,r.data(),r.size(),1));CHECK(input_get_pointer_owner()==&usb);CHECK(px==0&&py==0&&last_buttons==7);
        const unsigned before=motions,click_before=clicks;
        InputEvent es[]={{INPUT_EV_REL,INPUT_REL_X,100},{INPUT_EV_REL,INPUT_REL_Y,100},{INPUT_EV_KEY,INPUT_BTN_LEFT,1},{INPUT_EV_SYN,0,0}};
        input_dispatch_events(&ps2,es,4);CHECK(motions==before+1&&clicks==click_before);CHECK(input_pointer_is_active(&ps2)&&input_pointer_is_active(&usb));
        r=tablet_report(l,2000,2000);CHECK(hid_pointer_publish(usb,l,r.data(),r.size(),1));CHECK(px==width-1&&py==height-1);
        r=tablet_report(l,500,500);CHECK(hid_pointer_publish(usb,l,r.data(),r.size(),1));CHECK(px==(width-1)/2&&py==(height-1)/2);
        virtio.kinds=INPUT_DEVICE_KIND_TABLET;virtio.bus_type=INPUT_BUS_VIRTIO;CHECK(input_register_device(&virtio));CHECK(input_get_pointer_owner()==&virtio);
        CHECK(px==(width-1)/2&&py==(height-1)/2);
        r=tablet_report(l,-1000,-1000);CHECK(hid_pointer_publish(usb,l,r.data(),r.size(),1));CHECK(px==0&&py==0);
        CHECK(input_unregister_device(&virtio));CHECK(input_get_pointer_owner()==&usb);hid_pointer_remove(usb);CHECK(input_get_pointer_owner()==&ps2);CHECK(px==0&&py==0);
        input_dispatch_events(&ps2,es,4);CHECK(motions>before);
        input_reset_all();
    }
    width=0;height=0;InputDevice usb{};auto d=tablet_descriptor();HidPointerLayout l{};CHECK(hid_pointer_compile(d.data(),d.size(),l));auto r=tablet_report(l,32767,32767);
    const int oldx=px,oldy=py;CHECK(hid_pointer_publish(usb,l,r.data(),r.size(),1));CHECK(px==oldx&&py==oldy);input_reset_all();width=1280;height=720;
}
void readiness_tests(){
    input_reset_all();InputDevice ps2{},usb{},other{},virtio{};
    ps2.bus_type=INPUT_BUS_PS2;ps2.kinds=INPUT_DEVICE_KIND_MOUSE;
    CHECK(input_register_device(&ps2));CHECK(input_get_pointer_owner()==&ps2);
    auto desc=tablet_descriptor(true);HidPointerLayout layout{};
    CHECK(hid_pointer_compile(desc.data(),desc.size(),layout));
    CHECK(hid_pointer_register(usb,layout,101));
    CHECK(usb.pointer_state==INPUT_POINTER_REGISTERED_INACTIVE);
    CHECK(input_get_pointer_owner()==&ps2);
    CHECK(!input_prove_pointer(&usb));CHECK(input_arm_pointer(&usb));
    CHECK(usb.pointer_state==INPUT_POINTER_ARMED);CHECK(input_get_pointer_owner()==&ps2);
    InputEvent move[]={{INPUT_EV_REL,INPUT_REL_X,1},{INPUT_EV_SYN,0,0}};
    unsigned before=motions;input_dispatch_events(&ps2,move,2);CHECK(motions==before+1);
    uint8_t bad[]={42,0};CHECK(!hid_pointer_publish(usb,layout,bad,sizeof(bad),101));
    CHECK(input_get_pointer_owner()==&ps2);
    hid_pointer_remove(usb);CHECK(input_get_pointer_owner()==&ps2);
    // A structurally valid button-only report ID cannot prove absolute axes.
    auto split=tablet_descriptor(true);const uint8_t usage[]={9,0x30};
    const auto at=std::search(split.begin(),split.end(),usage,usage+2);
    split.insert(at,{0x85,9});HidPointerLayout split_layout{};
    CHECK(hid_pointer_compile(split.data(),split.size(),split_layout));
    auto buttons=tablet_report(split_layout,0,0,0,0,7);
    CHECK(!hid_pointer_publish(usb,split_layout,buttons.data(),buttons.size(),101));
    CHECK(input_get_pointer_owner()==&ps2);
    auto axes=tablet_report(split_layout,0,0,0,0,9);
    CHECK(hid_pointer_publish(usb,split_layout,axes.data(),axes.size(),101));
    CHECK(hid_pointer_publish(usb,split_layout,buttons.data(),buttons.size(),101));
    hid_pointer_remove(usb);CHECK(input_get_pointer_owner()==&ps2);
    // A failed first submission/removal cannot displace the fallback.
    CHECK(hid_pointer_register(usb,layout,101));hid_pointer_remove(usb);
    CHECK(input_get_pointer_owner()==&ps2);
    CHECK(hid_pointer_register(usb,layout,101));CHECK(input_arm_pointer(&usb));
    px=0;py=0;auto report=tablet_report(layout,0,0,0); // stationary report proves readiness
    CHECK(hid_pointer_publish(usb,layout,report.data(),report.size(),101));
    CHECK(px==0&&py==0);CHECK(usb.pointer_state==INPUT_POINTER_PROVEN_ACTIVE);CHECK(input_get_pointer_owner()==&usb);
    size_t count=input_get_device_count();
    CHECK(hid_pointer_publish(usb,layout,report.data(),report.size(),101));
    CHECK(input_get_device_count()==count);CHECK(input_get_pointer_owner()==&usb);
    before=motions;input_dispatch_events(&ps2,move,2);CHECK(motions==before+1);CHECK(input_pointer_is_active(&ps2)&&input_pointer_is_active(&usb));
    CHECK(hid_pointer_register(other,layout,102));CHECK(input_arm_pointer(&other));
    CHECK(hid_pointer_publish(other,layout,report.data(),report.size(),102));
    CHECK(input_get_pointer_owner()==&usb); // equal priority uses registry order
    hid_pointer_remove(usb);CHECK(input_get_pointer_owner()==&other);
    hid_pointer_remove(other);CHECK(input_get_pointer_owner()==&ps2);
    virtio.bus_type=INPUT_BUS_VIRTIO;virtio.kinds=INPUT_DEVICE_KIND_MOUSE;
    CHECK(input_register_device(&virtio));CHECK(input_get_pointer_owner()==&virtio);
    CHECK(input_unregister_device(&virtio));CHECK(input_get_pointer_owner()==&ps2);
    input_reset_all();
}
int main(){unsigned start=checks;ps2_tests();printf("PS/2 parser: %u checks PASS\n",checks-start);start=checks;hid_tests();printf("HID descriptor/report: %u checks PASS\n",checks-start);start=checks;usb_tests();printf("USB configuration: %u checks PASS\n",checks-start);start=checks;ownership_tests();readiness_tests();printf("InputDevice ownership/mapping: %u checks PASS\n",checks-start);}
