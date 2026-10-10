#include "narzedzia/pointing_host_stubs.h"
#include "ps2_mouse_parser.h"
#include "sterowniki/usb/hid_pointer.h"
#include "narzedzia/pointing_test_descriptors.h"
#include "narzedzia/virtualbox_tablet_descriptor.h"
#include <algorithm>
#include <vector>
static void relative(InputDevice& d,int x,int y){InputEvent e[]={{INPUT_EV_REL,INPUT_REL_X,uint32_t(x)},{INPUT_EV_REL,INPUT_REL_Y,uint32_t(y)},{INPUT_EV_SYN,0,0}};input_dispatch_events(&d,e,3);}
static void absolute(InputDevice& d,int x,int y){InputEvent e[]={{INPUT_EV_ABS,INPUT_ABS_X,uint32_t(x)},{INPUT_EV_ABS,INPUT_ABS_Y,uint32_t(y)},{INPUT_EV_SYN,0,0}};input_dispatch_events(&d,e,3);}
static int expected(int current,int delta,int dimension){return int(std::clamp(int64_t(current)+int64_t(delta),int64_t(0),int64_t(dimension)-1));}
static InputDevice mouse(){InputDevice d{};d.kinds=INPUT_DEVICE_KIND_MOUSE;d.bus_type=INPUT_BUS_PS2;d.pointer_mode=INPUT_POINTER_RELATIVE;return d;}
static void signed_movement(){
    for(auto bounds:std::vector<std::pair<int,int>>{{1280,720},{1920,1080},{1,1},{INT_MAX,INT_MAX}}){
        width=bounds.first;height=bounds.second;
        InputDevice p=mouse();CHECK(input_register_device(&p));
        for(int x:{0,width/2,width-1})for(int y:{0,height/2,height-1}){
            px=x;py=y;
            for(auto delta:std::vector<std::pair<int,int>>{{1,0},{-1,0},{0,1},{0,-1},{10,10},{-10,-10},{127,127},{-128,-128}}){
                const int ex=expected(px,delta.first,width),ey=expected(py,delta.second,height);
                relative(p,delta.first,delta.second);CHECK(px==ex&&py==ey);CHECK(px>=0&&px<width&&py>=0&&py<height);
            }
            for(auto delta:std::vector<std::pair<int,int>>{{0,-1},{0,1},{-1,0},{1,0}})for(unsigned i=0;i<2500;++i){
                const int ex=expected(px,delta.first,width),ey=expected(py,delta.second,height);
                relative(p,delta.first,delta.second);CHECK(px==ex&&py==ey);
            }
            for(unsigned i=0;i<1000;++i){const int dx=i%2?1:-1,dy=i%2?-1:1;
                const int ex=expected(px,dx,width),ey=expected(py,dy,height);relative(p,dx,dy);CHECK(px==ex&&py==ey);}
        }
        px=width/2;py=height/2;
        for(int delta:{INT_MIN,INT_MAX}){const int ex=expected(px,delta,width),ey=expected(py,delta,height);relative(p,delta,delta);CHECK(px==ex&&py==ey);}
        input_reset_all();
    }
    width=1280;height=720;px=640;py=360;InputDevice p=mouse();CHECK(input_register_device(&p));
    relative(p,1,-1);CHECK(px==641&&py==359);relative(p,-1,1);CHECK(px==640&&py==360);
    // Physical PS/2 upward is +Y on the wire; the unchanged parser alone
    // normalizes it to screen-down -Y. Graphics' legacy adapter cancels itself.
    Ps2MouseParser parser{};Ps2MouseReport report{};CHECK(parser.configure(0));parser.streaming=true;
    CHECK(!parser.feed(0x21,8,report));CHECK(!parser.feed(0x21,0,report));CHECK(parser.feed(0x21,1,report));
    CHECK(report.dy==-1);relative(p,report.dx,report.dy);CHECK(py==359);
    parser.feed(0x21,0x28,report);parser.feed(0x21,0,report);CHECK(parser.feed(0x21,255,report));
    CHECK(report.dy==1);relative(p,report.dx,report.dy);CHECK(py==360);input_reset_all();
}
static void early_geometry(){
    InputDevice p=mouse();CHECK(input_register_device(&p));width=height=0;px=640;py=360;
    relative(p,1,-1);CHECK(px==640&&py==360);CHECK(!input_pointer_geometry_ready());
    width=1280;height=720;input_service_all();CHECK(px==641&&py==359);
    input_set_pointer_ready(false);relative(p,-10,10);input_service_all();CHECK(px==641&&py==359);
    input_set_pointer_ready(true);input_service_all();CHECK(px==631&&py==369);input_service_all();CHECK(px==631&&py==369);
    width=0;height=720;relative(p,1,1);input_service_all();CHECK(px==631&&py==369);
    width=1280;height=0;input_service_all();CHECK(px==631&&py==369);
    height=720;input_service_all();CHECK(px==632&&py==370);
    input_set_pointer_ready(false);relative(p,10,10);
    HidPointerLayout l{};CHECK(hid_pointer_compile(virtualbox_tablet_descriptor,sizeof(virtualbox_tablet_descriptor),l));
    InputDevice u{};CHECK(hid_pointer_register(u,l,11));CHECK(input_get_pointer_owner()==&p);
    auto r=tablet_report(l,16384,16384,1);CHECK(hid_pointer_publish(u,l,r.data(),r.size(),11));CHECK(input_get_pointer_owner()==&u);CHECK(px==632&&py==370);
    input_set_pointer_ready(true);input_service_all();CHECK(px==639&&py==359&&last_buttons==1);
    const int x=px,y=py;hid_pointer_remove(u);CHECK(input_get_pointer_owner()==&p);CHECK(px==x&&py==y);
    input_service_all();CHECK(px==x&&py==y);relative(p,1,-1);CHECK(px==x+1&&py==y-1);
    input_reset_all();
}
static void api_modes(){
    InputDevice p=mouse();CHECK(input_register_device(&p));px=640;py=360;relative(p,10,10);CHECK(px==650&&py==370);
    absolute(p,0,0);CHECK(px==650&&py==370); // mismatched event cannot reinterpret a relative device
    InputDevice t{};t.bus_type=INPUT_BUS_VIRTIO;t.kinds=INPUT_DEVICE_KIND_TABLET;t.pointer_mode=INPUT_POINTER_ABSOLUTE;
    t.abs_x={0,1279,0,0,0};t.abs_y={0,719,0,0,0};CHECK(input_register_device(&t));CHECK(px==650&&py==370);
    absolute(t,10,10);CHECK(px==10&&py==10);relative(t,-1,-1);CHECK(px==10&&py==10);
    relative(p,10,10);absolute(p,500,500);CHECK(px==10&&py==10);
    InputDevice other=t;other.registered=false;other.device_id=2;CHECK(input_register_device(&other));absolute(other,1000,500);CHECK(px==10&&py==10);
    CHECK(!input_register_device(&t));CHECK(px==10&&py==10);
    CHECK(input_unregister_device(&t));CHECK(px==10&&py==10);CHECK(input_get_pointer_owner()==&other);
    CHECK(input_unregister_device(&other));CHECK(px==10&&py==10);relative(p,0,-1);CHECK(px==10&&py==9);
    InputEvent malformed[]={{INPUT_EV_ABS,INPUT_ABS_X,UINT32_MAX},{INPUT_EV_KEY,INPUT_BTN_LEFT,UINT32_MAX},{INPUT_EV_REL,65535,UINT32_MAX},{65535,0,0},{INPUT_EV_SYN,0,0}};
    input_dispatch_events(&p,malformed,5);CHECK(px==10&&py==9);CHECK(p.buttons_state==0);
    CHECK(input_unregister_device(&p));relative(p,500,500);CHECK(px==10&&py==9);
    input_reset_all();
    auto desc=tablet_descriptor(false,5,-127,127,true);HidPointerLayout l{};CHECK(hid_pointer_compile(desc.data(),desc.size(),l));InputDevice usb{};
    CHECK(hid_pointer_register(usb,l,8));CHECK(usb.pointer_mode==INPUT_POINTER_RELATIVE);px=640;py=360;
    auto r=tablet_report(l,-1,-1);CHECK(hid_pointer_publish(usb,l,r.data(),r.size(),8));CHECK(px==639&&py==359);
    hid_pointer_remove(usb);input_reset_all();
    // A device advertising both capabilities still declares both explicitly;
    // each wire event selects its mode. Preserve existing hybrid VirtIO support.
    InputDevice hybrid{};hybrid.kinds=INPUT_DEVICE_KIND_MOUSE|INPUT_DEVICE_KIND_TABLET;
    hybrid.bus_type=INPUT_BUS_VIRTIO;hybrid.pointer_mode=INPUT_POINTER_RELATIVE_AND_ABSOLUTE;
    hybrid.abs_x={0,1279,0,0,0};hybrid.abs_y={0,719,0,0,0};CHECK(input_register_device(&hybrid));
    px=640;py=360;relative(hybrid,-1,-1);CHECK(px==639&&py==359);
    absolute(hybrid,10,10);CHECK(px==10&&py==10);input_reset_all();
    // The same numbers are distinct event semantics, independent of bus.
    p=mouse();CHECK(input_register_device(&p));px=640;py=360;relative(p,10,10);CHECK(px==650&&py==370);
    CHECK(input_unregister_device(&p));p.pointer_mode=INPUT_POINTER_ABSOLUTE;p.abs_x={0,1279,0,0,0};p.abs_y={0,719,0,0,0};
    CHECK(input_register_device(&p));px=640;py=360;absolute(p,10,10);CHECK(px==10&&py==10);input_reset_all();
}
void input_set_pointer_ready(bool){}
int main(){signed_movement();printf("Known-good HEAD signed movement: %u checks PASS\n",checks);}
