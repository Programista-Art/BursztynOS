// Isolated production splash + exact shared reboot teardown ordering.
#define main suite_powitania_main
#include "test_powitanie.cpp"
#undef main
#include "cykl_wylaczania_grafiki.h"
#include <mutex>
#include <thread>
#include <atomic>
static std::mutex ekran_mutex;
static bool locked=false;
static unsigned order=0;
extern "C" void zablokuj_ekran(){ekran_mutex.lock();locked=true;order=1;}
extern "C" void odblokuj_ekran(){assert(locked&&order==3);locked=false;ekran_mutex.unlock();}
void skladacz_obrazu_wycisz(){assert(locked&&order==1&&!powitanie_aktywne()&&live==0);order=2;}
void grafika_wycisz(){assert(locked&&order==2);order=3;}
int main(){
    width=320;height=240;clock_ok=true;
    std::vector<uint32_t> image(320*240,0x123456);
    // Concurrent presentation and teardown are serialized by the existing
    // screen ownership contract, including consumption of the returned pointer.
    std::atomic<bool> stop{false};
    std::thread renderer([&]{
        while(!stop.load()) {
            std::lock_guard<std::mutex> guard(ekran_mutex);
            powitanie_tick();
            auto* p=powitanie_zrodlo(reinterpret_cast<uint8_t*>(image.data()),320,240,1280,0,0,320,240,100);
            volatile uint8_t sample=p[0];(void)sample;
        }
    });
    for(unsigned cycle=0;cycle<100;++cycle){
        {std::lock_guard<std::mutex> guard(ekran_mutex);now=cycle;powitanie_inicjalizuj(true);
         powitanie_zrodlo(reinterpret_cast<uint8_t*>(image.data()),320,240,1280,0,0,320,240,100);
         powitanie_publikacja();
         assert(live==1);}
        wycisz_start_przed_restartem();
        {std::lock_guard<std::mutex> guard(ekran_mutex);assert(!locked&&live==0&&!powitanie_aktywne());}
        std::this_thread::yield();
    }
    stop.store(true);renderer.join();
    assert(live==0);
    printf("Power teardown/concurrent splash: %u/%u PASS; 100 generations\n",checks,checks);
}
