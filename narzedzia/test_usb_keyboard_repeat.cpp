#define BURSZTYN_VIRTIO_HOST_TEST 1
#include "../sterowniki/usb/usb_hid.h"
#include <vector>
#include <cassert>
#include <cstdio>
static uint64_t now;
uint64_t host_czas_monotoniczny_ns(){return now;}
static std::vector<InputEvent> events;
extern "C" uint16_t input_usb_hid_usage_to_linux(uint8_t usage){return usage==80?INPUT_KEY_LEFT:0;}
extern "C" uint16_t input_usb_hid_mod_to_linux(uint8_t){return 0;}
bool input_register_device(InputDevice* dev){dev->registered=true;return true;}
void input_dispatch_events(InputDevice*,const InputEvent* list,size_t n){for(size_t i=0;i<n;++i)if(list[i].type==INPUT_EV_KEY)events.push_back(list[i]);}
int main(){
 InputDevice dev{};uint8_t previous[8]{};uint8_t left[8]={0,0,80};uint8_t empty[8]{};
 auto report=[&](uint64_t time,const uint8_t* data){now=time;events.clear();usb_hid_keyboard_report(dev,previous,data,8);};
 report(0,left);assert(events.size()==1&&events[0].code==INPUT_KEY_LEFT&&events[0].value==1);
 for(auto t:{20000000ULL,80000000ULL,100000000ULL,399999999ULL}){report(t,left);assert(events.empty());}
 report(400000000,left);assert(events.size()==1&&events[0].value==2);
 report(410000000,left);assert(events.empty());
 report(433000000,left);assert(events.size()==1&&events[0].value==2);
 report(440000000,empty);assert(events.size()==1&&events[0].value==0);
 report(450000000,left);assert(events.size()==1&&events[0].value==1);
 report(460000000,left);assert(events.empty());
 report(470000000,empty);assert(events.size()==1&&events[0].value==0);
 puts("USB HID: one tap, unchanged reports, delayed repeat, repeat rate and release PASS");
}
