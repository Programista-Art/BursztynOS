// Reuse hardware/DMA fixture only. Link real graphics, InputDevice and HID.
#define BURSZTYN_POINTER_GRAPHICS_FIXTURE 1
#include "test_splash_production_path.cpp"
#include "sterowniki/usb/hid_pointer.h"
#include "pointing_test_descriptors.h"
#include "virtualbox_tablet_descriptor.h"
extern "C" bool zaktualizuj_klawisz_fizyczny_gui(uint32_t,bool,uint32_t);
extern "C" bool zaktualizuj_klawiature_gui(char);
extern "C" bool zaktualizuj_tekst_gui(uint32_t,uint32_t);
void ZwolnijCiagleRamki(void*,size_t){assert(false);}
extern "C" void WybudzProcesyOczekujaceNaMysz(){}
int main(){
    std::vector<uint32_t> native(1280*720,0);
    init(native,true);input_reset_all();input_set_pointer_ready(true);
    InputDevice ps2{};ps2.kinds=INPUT_DEVICE_KIND_MOUSE;ps2.bus_type=INPUT_BUS_PS2;ps2.pointer_mode=INPUT_POINTER_RELATIVE;
    assert(input_register_device(&ps2));
    zaktualizuj_mysze_pozycja_zrodlo(640,360,0,"test-setup");
    assert(grafika_pobierz_mysz_x()==640&&grafika_pobierz_mysz_y()==360);
    for(auto delta:std::vector<std::pair<int,int>>{{1,0},{-1,0},{0,1},{0,-1},{10,10},{-10,-10},{127,127},{-128,-128}}){
        int x=grafika_pobierz_mysz_x(),y=grafika_pobierz_mysz_y();
        InputEvent e[]={{INPUT_EV_REL,INPUT_REL_X,uint32_t(delta.first)},{INPUT_EV_REL,INPUT_REL_Y,uint32_t(delta.second)},{INPUT_EV_SYN,0,0}};
        input_dispatch_events(&ps2,e,3);
        assert(grafika_pobierz_mysz_x()==std::clamp(x+delta.first,0,1279));
        assert(grafika_pobierz_mysz_y()==std::clamp(y+delta.second,0,719));
    }
    InputDevice usb{};HidPointerLayout l{};
    assert(hid_pointer_compile(virtualbox_tablet_descriptor,sizeof(virtualbox_tablet_descriptor),l));
    assert(hid_pointer_register(usb,l,1));assert(input_get_pointer_owner()==&ps2);
    int x=grafika_pobierz_mysz_x(),y=grafika_pobierz_mysz_y();assert(input_arm_pointer(&usb));assert(input_get_pointer_owner()==&ps2);
    assert(grafika_pobierz_mysz_x()==x&&grafika_pobierz_mysz_y()==y);
    for(int value:{0,16384,32767,16384}){
        auto report=tablet_report(l,value,value);assert(hid_pointer_publish(usb,l,report.data(),report.size(),1));
        assert(grafika_pobierz_mysz_x()==int(int64_t(value)*1279/32767));
        assert(grafika_pobierz_mysz_y()==int(int64_t(value)*719/32767));
    }
    x=grafika_pobierz_mysz_x();y=grafika_pobierz_mysz_y();hid_pointer_remove(usb);
    assert(input_get_pointer_owner()==&ps2);assert(grafika_pobierz_mysz_x()==x&&grafika_pobierz_mysz_y()==y);
    InputDevice virtio{};virtio.kinds=INPUT_DEVICE_KIND_MOUSE;virtio.bus_type=INPUT_BUS_VIRTIO;
    virtio.pointer_mode=INPUT_POINTER_RELATIVE;assert(input_register_device(&virtio));
    for(auto delta:std::vector<std::pair<int32_t,int32_t>>{{3,-2},{-3,2},{INT32_MIN,INT32_MIN},{INT32_MAX,INT32_MAX},{-1,1}}){
        zaktualizuj_mysze_pozycja_zrodlo(640,360,0,"virtio-test-setup");
        InputEvent e[]={{INPUT_EV_REL,INPUT_REL_X,uint32_t(delta.first)},{INPUT_EV_REL,INPUT_REL_Y,uint32_t(delta.second)},{INPUT_EV_SYN,0,0}};
        input_dispatch_events(&virtio,e,3);
        assert(grafika_pobierz_mysz_x()==std::clamp(int64_t(640)+delta.first,int64_t(0),int64_t(1279)));
        assert(grafika_pobierz_mysz_y()==std::clamp(int64_t(360)+delta.second,int64_t(0),int64_t(719)));
        assert(input_pointer_is_active(&virtio) && input_pointer_is_active(&ps2));
        const int vx=grafika_pobierz_mysz_x(),vy=grafika_pobierz_mysz_y();
        const InputEvent physical[]={{INPUT_EV_REL,INPUT_REL_X,1},{INPUT_EV_REL,INPUT_REL_Y,uint32_t(-1)},{INPUT_EV_SYN,0,0}};
        input_dispatch_events(&ps2,physical,3);
        assert(grafika_pobierz_mysz_x()==std::clamp(vx+1,0,1279));
        assert(grafika_pobierz_mysz_y()==std::clamp(vy-1,0,719));
    }
    // Real system Alt+Tab routing must switch even when no application polls.
    assert(utworz_warstwe(1,0,0,1280,680,0)==1); // untitled desktop is skipped
    assert(utworz_warstwe(2,100,100,400,300,10)==2);
    assert(utworz_warstwe(3,150,150,400,300,11)==3);
    assert(skladacz_obrazu_ustaw_tytul(2,"Husarz"));
    assert(skladacz_obrazu_ustaw_tytul(3,"Notatnik"));
    powitanie_inicjalizuj(false);
    bws_gui_aktywuj_warstwe(2);assert(bws_gui_aktywny_pid()==2);
    assert(zaktualizuj_klawisz_fizyczny_gui(15,true,BWS_MOD_ALT));
    assert(bws_gui_aktywny_pid()==3);
    assert(zaktualizuj_klawiature_gui('\t'));assert(zaktualizuj_tekst_gui(9,BWS_MOD_ALT));
    assert(zaktualizuj_klawisz_fizyczny_gui(15,false,0));
    assert(zaktualizuj_klawisz_fizyczny_gui(BWS_KEY_SOURCE_USB|43,true,BWS_MOD_ALT));
    assert(bws_gui_aktywny_pid()==2);
    assert(zaktualizuj_klawisz_fizyczny_gui(BWS_KEY_SOURCE_USB|43,false,BWS_MOD_ALT));
    assert(bws_gui_minimalizuj_warstwe(3));
    assert(zaktualizuj_klawisz_fizyczny_gui(15,true,BWS_MOD_ALT|BWS_MOD_SHIFT));
    assert(bws_gui_aktywny_pid()==3);
    GuiOknoInfo restored[SKLADACZ_MAKS_WARSTW]{};
    unsigned restored_count=skladacz_obrazu_snapshot_okien(restored,SKLADACZ_MAKS_WARSTW,3);
    bool restored_state=false;for(unsigned i=0;i<restored_count;++i)if(restored[i].pid==3)restored_state=restored[i].stan==GUI_OKNO_NORMALNE;
    assert(restored_state);
    assert(zaktualizuj_klawisz_fizyczny_gui(15,false,0));
    assert(zaktualizuj_klawisz_fizyczny_gui(15,true,BWS_MOD_CTRL|BWS_MOD_ALT));
    assert(bws_gui_aktywny_pid()==3);
    assert(zaktualizuj_klawisz_fizyczny_gui(15,false,0));
    usun_warstwe(3);usun_warstwe(2);usun_warstwe(1);
    input_reset_all();StanBuforowEkranu last{};grafika_pobierz_stan(&last);grafika_wymien_stan({});grafika_zwolnij_stan(&last);
    assert(graphical_allocs.empty());printf("Real graphics/InputDevice/HID pointer integration: %u checks PASS\n",checks);
}
