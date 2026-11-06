#define REAL_KEYBOARD 1
#include "testy/pointing_host_stubs.h"
#include "sterowniki/usb/usb_hid.h"
#include <deque>
#include <vector>
#include <thread>
#include <atomic>
#include "mysz.cpp"
extern "C" void obsluga_przerwania_klawiatury();
extern "C" InputDevice* ps2_klawiatura_pobierz_urzadzenie();
struct Byte{uint8_t status,value;};static std::deque<Byte> output;
static bool controller_absent{},keyboard_no_ack{};
static bool to_mouse{},config_write{},drop_ack{},keyboard_injected{};
static uint8_t controller_config=0x45,last_config_write=0;
static uint8_t test_id=3;static unsigned reads{},resends{};
static std::vector<uint8_t> commands;
uint8_t ps2_test_read(uint16_t port){
    if(port==0x64)return controller_absent?0xff:output.empty()?0:output.front().status;
    CHECK(port==0x60&&!output.empty());uint8_t b=output.front().value;output.pop_front();++reads;return b;
}
void ps2_test_write(uint16_t port,uint8_t b){
    if(port==0x64){if(b==0xD4)to_mouse=true;if(b==0x20)output.push_back({1,controller_config});if(b==0x60)config_write=true;return;}
    if(config_write){config_write=false;last_config_write=controller_config=b;return;}
    if(!to_mouse){CHECK(b==0xF4);if(!keyboard_no_ack)output.push_back({1,0xFA});return;}
    CHECK(to_mouse);to_mouse=false;commands.push_back(b);
    if(drop_ack)return;
    if(!keyboard_injected){output.push_back({1,0x1E});keyboard_injected=true;}
    if(resends){--resends;output.push_back({0x21,0xFE});return;}
    output.push_back({0x21,0xFA});if(b==0xF2)output.push_back({0x21,test_id});
}
int main(){
    input_reset_all();ps2_klawiatura_pobierz_urzadzenie();resends=1;
    output.push_back({0x21,0xAA});output.push_back({0x21,0});output.push_back({1,0x10});
    InicjalizujMyszPS2();CHECK(mysz_zainicjalizowana);CHECK(parser.id==3);CHECK(output.empty());CHECK(parser.index==0);CHECK(input_get_pointer_owner()==&s_ps2_mouse_device);
    CHECK(commands[0]==0xF5&&commands[1]==0xF5);CHECK(commands.back()==0xF4);
    // Hardware/IRQ12 may be ready before the cursor. The existing SPSC queue
    // must retain the real report; no synthetic movement or byte discard.
    input_set_pointer_ready(false);
    for(uint8_t b:{uint8_t(8),uint8_t(1),uint8_t(1),uint8_t(0)})output.push_back({0x21,b});
    obsluga_przerwania_myszy();input_service_all();CHECK(px==640&&py==360);CHECK(pending_head!=pending_tail);
    input_set_pointer_ready(true);input_service_all();CHECK(px==641&&py==359);CHECK(pending_head==pending_tail);
    px=640;py=360;
    const unsigned before=reads;output.push_back({0x21,0x38});obsluga_przerwania_klawiatury();CHECK(reads==before);CHECK(output.size()==1);
    output.push_back({0x21,0xFF});output.push_back({0x21,0xFF});output.push_back({0x21,0xFF});
    const unsigned moved=motions;obsluga_przerwania_myszy();CHECK(output.empty());CHECK(motions==moved);CHECK(parser.index==0);input_service_all();CHECK(px==639&&py==361);
    output.push_back({1,0x9E});output.push_back({0x21,8});const unsigned prior=reads;obsluga_przerwania_myszy();CHECK(reads==prior);obsluga_przerwania_klawiatury();CHECK(reads==prior+1);obsluga_przerwania_myszy();CHECK(parser.index==1);
    output.push_back({0xE1,0xFF});obsluga_przerwania_myszy();CHECK(parser.index==0);
    // Full ring drops complete reports and never writes outside the fixed buffer.
    for(unsigned i=0;i<1000;++i){output.push_back({0x21,9});output.push_back({0x21,1});output.push_back({0x21,1});output.push_back({0x21,0});obsluga_przerwania_myszy();}
    output.push_back({0x21,8});output.push_back({0x21,0});output.push_back({0x21,0});output.push_back({0x21,0});obsluga_przerwania_myszy();
    CHECK(pending_head<64&&pending_tail<64);
    // The latest dropped report releases buttons, even if no more packets arrive.
    s_ps2_mouse_device.buttons_state=1;s_ps2_mouse_device.prev_buttons_state=1;last_buttons=1;
    input_service_all();CHECK(pending_head==pending_tail);CHECK(last_buttons==0);
    std::atomic<bool> finished{false};
    std::thread producer([&]{for(unsigned i=0;i<10000;++i){
        przyjmij_bajt_pakietu(0x21,8);przyjmij_bajt_pakietu(0x21,1);
        przyjmij_bajt_pakietu(0x21,1);przyjmij_bajt_pakietu(0x21,0);
    }finished.store(true,std::memory_order_release);});
    while(!finished.load(std::memory_order_acquire))input_service_all();
    producer.join();input_service_all();
    CHECK(pending_head==pending_tail);CHECK(parser.index==0);
    input_reset_all();drop_ack=true;InicjalizujMyszPS2();CHECK(!mysz_zainicjalizowana);CHECK(!s_ps2_mouse_device.registered);
    drop_ack=false;test_id=5;InicjalizujMyszPS2();CHECK(!mysz_zainicjalizowana);
    for(uint8_t id:{uint8_t(0),uint8_t(4)}){test_id=id;InicjalizujMyszPS2();CHECK(mysz_zainicjalizowana&&parser.id==id);input_reset_all();}
    input_reset_all();output.clear();
    for(uint8_t config:{uint8_t(0x04),uint8_t(0x44)}){
        controller_config=config;CHECK(ps2_inicjalizuj_klawiature());
        CHECK((last_config_write&0x40U)==(config&0x40U));CHECK(last_config_write&1U);input_reset_all();
    }
    CHECK(ps2_inicjalizuj_klawiature());
    CHECK(input_find_device_by_name("ps2-keyboard")!=nullptr);
    input_reset_all();keyboard_no_ack=true;CHECK(!ps2_inicjalizuj_klawiature());
    CHECK(input_find_device_by_name("ps2-keyboard")==nullptr);
    controller_absent=true;CHECK(!ps2_inicjalizuj_klawiature());CHECK(!InicjalizujMyszPS2());
    const uint8_t usb_report[8]={0,0,4,0,0,0,0,0};usb_hid_przetworz_raport(usb_report);
    CHECK(input_find_device_by_name("usb-keyboard")!=nullptr);CHECK(input_pointer_geometry_ready());
    controller_absent=false;keyboard_no_ack=false;input_reset_all();
    printf("i8042/IRQ1/IRQ12/initialization: %u checks PASS\n",checks);
}
