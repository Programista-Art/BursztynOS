// Production GPU reboot quiesce with isolated queue/clock hooks; no emulator.
#define main welcome_fixture_main
#define checks welcome_checks
#include "test_powitanie.cpp"
#undef checks
#undef main
#include "cykl_wylaczania_grafiki.h"
#define BURSZTYN_VIRTIO_HOST_TEST
#include "../sterowniki/virtio/virtio_gpu.cpp"
#include <cassert>
static uint64_t mock_clock=0;
static unsigned commands=0,logs=0,checks=0;
static bool invalid_completion=false,used_completion=false,complete_submit=false,repeating_completion=false;
static unsigned popped=0;
void host_klog(const char*){++logs;}
uint64_t host_czas_monotoniczny_ns(){return mock_clock;}
bool virtqueue_has_used(const VirtQueue*){return used_completion;}
int virtqueue_pop_used(VirtQueue*,void**,uint32_t*){++popped;if(invalid_completion)return -2;if(!repeating_completion)used_completion=false;return 0;}
int virtqueue_submit(VirtQueue*,const VirtqBuffer*,uint32_t,void*){++commands;if(invalid_completion||complete_submit){used_completion=true;return 0;}return -1;}
bool virtqueue_kick(VirtQueue*,bool){return true;}
#undef assert
#define assert(c) do{++checks;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
static bool screen_locked=false;
static unsigned teardown_order=0;
extern "C" void zablokuj_ekran(){assert(!screen_locked);screen_locked=true;teardown_order=1;}
extern "C" void odblokuj_ekran(){assert(screen_locked&&teardown_order==3);screen_locked=false;}
void skladacz_obrazu_wycisz(){assert(screen_locked&&teardown_order==1&&live==0&&!powitanie_aktywne());teardown_order=2;}
void grafika_wycisz(){assert(screen_locked&&teardown_order==2);teardown_order=3;}
static void bad_completion_probe(){
    VirtqAvail avail{};VirtQueue queue{};queue.avail=&avail;queue.queue_size=16;
    std::vector<uint8_t> cmd(VIRTIO_GPU_CMD_BUFFER_SIZE);
    g_gpu_driver_count=1;auto& drv=g_gpu_drivers[0];drv={};
    drv.initialized=true;drv.controlq=&queue;drv.scanout_set=true;drv.cmd_vaddr=cmd.data();
    invalid_completion=true;used_completion=false;mock_clock=0;
    puts("GPU quiesce: invalid completion after disabling scanout");fflush(stdout);
    virtio_gpu_quiesce_for_reboot();
    assert(drv.has_error&&drv.lock==0&&virtio_gpu_is_quiescing());
    invalid_completion=false;used_completion=false;
}
static void drain_contracts(){
    VirtqAvail avail{};VirtQueue queue{};queue.avail=&avail;queue.queue_size=16;
    std::vector<uint8_t> cmd(VIRTIO_GPU_CMD_BUFFER_SIZE);
    auto& drv=g_gpu_drivers[0];
    for(unsigned mode=0;mode<4;++mode){
        drv={};drv.initialized=true;drv.controlq=&queue;
        drv.scanout_set=mode!=1;drv.cmd_vaddr=cmd.data();
        if(mode==3){drv.scanout_set=false;drv.initial_scanout_pending=true;drv.old_resource_id=1;}
        invalid_completion=mode==1;used_completion=mode==1;
        complete_submit=mode!=1;repeating_completion=mode==2;
        unsigned before=popped,before_commands=commands;
        virtio_gpu_quiesce_for_reboot();
        assert(drv.lock==0&&drv.has_error==(mode==1||mode==2));
        assert(popped-before==(mode==2?16U:1U));
        assert(commands-before_commands==(mode==1?0U:1U));
    }
    invalid_completion=used_completion=complete_submit=repeating_completion=false;
}
int main(int argc,char**){
    if(argc>1){bad_completion_probe();return 0;}
    VirtqAvail avail{};VirtQueue queue{};queue.avail=&avail;
    g_gpu_driver_count=1;auto& drv=g_gpu_drivers[0];drv={};
    drv.initialized=true;drv.controlq=&queue;drv.lock=1;
    puts("GPU quiesce: held lock, unavailable monotonic clock");fflush(stdout);
    virtio_gpu_quiesce_for_reboot();
    assert(virtio_gpu_is_quiescing()&&drv.has_error&&drv.lock==1);
    assert(commands==0&&logs==1); // no touching queue owned by another reader
    drv={};drv.initialized=true;drv.controlq=&queue;
    virtio_gpu_quiesce_for_reboot();
    assert(drv.lock==0&&!drv.has_error&&commands==0);
    // Pending completion with absent clock remains bounded too.
    avail.idx=1;virtio_gpu_quiesce_for_reboot();
    assert(drv.lock==0&&drv.has_error&&commands==0);
    // Active splash -> Windows-like 3 s scheduling gap -> exact power helper ->
    // real GPU quiesce, for idle, held-lock/frozen-clock and pending completion.
    width=320;height=240;std::vector<uint32_t> scene(320*240,0x234567);
    for(unsigned mode=0;mode<3;++mode){
        now=0;powitanie_inicjalizuj(true);
        auto present=[&](){powitanie_zrodlo(reinterpret_cast<uint8_t*>(scene.data()),320,240,1280,0,0,320,240,100);powitanie_publikacja();};
        advance(100000);present();advance(3100000);present();
        // Po 3,2 s splash jest nadal aktywny i dotarl do fazy paska postepu (dawny stan "komunikat==1"
        // zniknal z modelu po przeprojektowaniu animacji: Sekwencja ma teraz Faza/Stan).
        assert(powitanie_aktywne()&&live==1&&sekwencja.stan==Powitanie::Stan::Widoczny&&sekwencja.faza==Powitanie::Faza::Pasek);
        drv={};drv.initialized=true;drv.controlq=&queue;drv.lock=mode==1;
        avail.idx=mode==2;mock_clock=mode==0?7500000000ULL:0;
        wycisz_start_przed_restartem();
        assert(!screen_locked&&teardown_order==3&&live==0&&!powitanie_aktywne());
        virtio_gpu_quiesce_for_reboot();
        assert(virtio_gpu_is_quiescing()&&drv.has_error==(mode!=0)&&commands==0);
        assert(drv.lock==(mode==1));
    }
    bad_completion_probe();
    drain_contracts();
    printf("GPU reboot quiesce clock/lock/pending: %u/%u PASS\n",checks,checks);
}
