// Production graphics initializer, native HALs, compositor, welcome renderer,
// PCI queue and GPU command path. Only hardware/physical mapping is simulated.
#include <algorithm>
#define main existing_transport_suite_main
#define grafika_pobierz_boot_rozmiar unused_boot_size_mock
#define grafika_pobierz_szerokosc unused_width_mock
#define grafika_pobierz_wysokosc unused_height_mock
#define grafika_pobierz_mysz_x unused_mouse_x_mock
#define grafika_pobierz_mysz_y unused_mouse_y_mock
#define zaktualizuj_klawiature_gui unused_keyboard_mock
#define zaktualizuj_klawisz_fizyczny_gui unused_physical_key_mock
#define zaktualizuj_tekst_gui unused_text_mock
#define zaktualizuj_mysze_zrodlo unused_mouse_mock
#define zaktualizuj_mysze_wzgledne_zrodlo unused_relative_mouse_mock
#define zaktualizuj_mysze_pozycja_zrodlo unused_absolute_mock
#define zaktualizuj_mysze_precyzyjna_zrodlo unused_precision_mock
#define zaktualizuj_kolo_myszy unused_wheel_mock
#define klog_serial unused_log_mock
#define host_czas_monotoniczny_ns unused_transport_clock_mock
#define BURSZTYN_HOST_KERNEL_CLOCK_PROVIDED 1 // defined below (test_now)
#include "test_virtio_pci.cpp"
#undef main
#undef grafika_pobierz_boot_rozmiar
#undef grafika_pobierz_szerokosc
#undef grafika_pobierz_wysokosc
#undef grafika_pobierz_mysz_x
#undef grafika_pobierz_mysz_y
#undef zaktualizuj_klawiature_gui
#undef zaktualizuj_klawisz_fizyczny_gui
#undef zaktualizuj_tekst_gui
#undef zaktualizuj_mysze_zrodlo
#undef zaktualizuj_mysze_wzgledne_zrodlo
#undef zaktualizuj_mysze_pozycja_zrodlo
#undef zaktualizuj_mysze_precyzyjna_zrodlo
#undef zaktualizuj_kolo_myszy
#undef klog_serial
#undef host_czas_monotoniczny_ns
#include "../sterowniki/grafika/grafika.h"
#include "ekran_powitalny.h"
#include "skladacz_obrazu.h"
#include "pamiec.h"
#include "diagnostyka_klatek_startu.h"
#include "../sterowniki/grafika/uefi_gop.h"
#include "../sterowniki/grafika/bochs_vbe.h"
#include "motyw.h"
static unsigned checks=0;
#undef assert
#define assert(c) do {++checks;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
static uint64_t test_now=0;
static uint64_t allocation_delay=0;
static unsigned native_rows=0;
void grafika_host_wiersz_opublikowany(uint32_t){++native_rows;}
static std::vector<void*> graphical_allocs;
int aktualny_pid=0;
bool hpet_dostepny(){return true;}
uint64_t czas_monotoniczny_us(){return test_now;}
uint64_t czas_monotoniczny_ns(){return test_now*1000;}
uint64_t host_czas_monotoniczny_ns(){return test_now*1000;}
uint64_t scheduler_pobierz_tick(){return 0;}
bool scheduler_dodaj_zdarzenie(int,const bws_zdarzenie*){return true;}
uint32_t ekran_skala(){return 100;}
bool bufory_grafiki_inicjalizuj(){return true;}
void* bufor_grafiki_alokuj(size_t n){if(allocation_delay){test_now+=allocation_delay;allocation_delay=0;}auto* p=malloc(n);if(p)graphical_allocs.push_back(p);return p;}
void bufor_grafiki_zwolnij(void* p){if(!p)return;auto it=std::find(graphical_allocs.begin(),graphical_allocs.end(),p);assert(it!=graphical_allocs.end());graphical_allocs.erase(it);free(p);}
const BursztynMotyw* motyw_pobierz_aktywny(){return &g_domyslny_motyw_fallback;}
bool bochs_wykryj(uint64_t,const TrybEkranu&,uint64_t*){return false;}
bool bochs_sprawdz_tryb(const TrybEkranu&){return false;}
bool bochs_ustaw_tryb(const TrybEkranu&){return false;}
extern "C" void grafika_host_serial(const char* text){g_klog_lines.emplace_back(text?text:"");}
extern void grafika_host_bezposrednia_kopia(uint8_t*);
static void init(std::vector<uint32_t>& native,bool efi) {
    alignas(8) uint8_t tags[72]{};
    uint32_t total=efi?64:56;memcpy(tags,&total,4);
    TagFramebufferMB2 fb{8,38,reinterpret_cast<uint64_t>(native.data()),5120,1280,720,32,1,0};
    memcpy(tags+8,&fb,sizeof(fb));const uint8_t rgb[]={16,8,8,8,0,8};memcpy(tags+40,rgb,6);
    if(efi){uint32_t tag=12,size=8;memcpy(tags+48,&tag,4);memcpy(tags+52,&size,4);}
    uint32_t end_size=8;memcpy(tags+(efi?60:52),&end_size,4);
    InicjalizujGrafike(reinterpret_cast<uint64_t>(tags));
}
static void check_image(const std::vector<uint32_t>& p,int w,int h,uint64_t elapsed) {
    // Dokladna zgodnosc pikselowa z rendererem jest sprawdzana w
    // test-splash-snapshots, ktory ma dostep do stanu modulu. Tutaj licza sie
    // niezmienniki realnie opublikowanej klatki: czarne tlo, logotyp od pierwszej
    // klatki, brak podtytulow przed ich etapem i pelny rozmiar ukladu.
    const auto g=Powitanie::geometria(w,h,100);
    auto ink=[&](Powitanie::Obszar r){
        for(int y=r.y<0?0:r.y;y<r.y+r.h&&y<h;++y)
            for(int x=r.x<0?0:r.x;x<r.x+r.w&&x<w;++x)
                if(p[y*w+x]!=Powitanie::TLO)return true;
        return false;};
    (void)elapsed;
    assert(p.front()==Powitanie::TLO);
    // Logotyp ma atrament od pierwszej publikacji.
    assert(ink(g.logo));
    // Obszar ponizej ukladu jest zawsze czystym tlem (brak starych pikseli).
    const int dol=g.linia2.r.y+g.linia2.r.h+1;
    for(int y=dol;y<h;++y)
        for(int x=0;x<w;++x)assert(p[size_t(y)*w+x]==Powitanie::TLO);
    // Nic nie jest rysowane poza wysrodkowanym prostokatem ukladu.
    assert(p[size_t(g.dirty.y/2)*w+g.separator.x/2]==Powitanie::TLO);
}
#ifndef BURSZTYN_POINTER_GRAPHICS_FIXTURE
int main(){
    for(bool efi:{false,true})for(uint64_t delay:{0ULL,500000ULL,1500000ULL,3000000ULL}) {
        std::vector<uint32_t> native(1280*720,0xE750A1),blank(1280*720,0);
        test_now=0;native_rows=0;gpu_scanout_model.reset();init(native,efi);
        assert(!PrezentacjaStartu::dozwolona()&&!powitanie_aktywne());
        assert(native==std::vector<uint32_t>(1280*720,0xE750A1));
        // Every real publication entrance is exercised before policy selection.
        PrzeniesNaEkran();PrzeniesFragmentNaEkran(0,0,1280,720);
        grafika_host_bezposrednia_kopia(reinterpret_cast<uint8_t*>(blank.data()));
        GOP_PostawPiksel(100,100,0);
        skladacz_obrazu_oznacz_dirty();skladacz_obrazu_obsluz_dirty();
        assert(native==std::vector<uint32_t>(1280*720,0xE750A1));
        assert(KlatkiStartuDiag::liczniki().visible==0);
        allocation_delay=delay;powitanie_inicjalizuj(true);
        assert(powitanie_pierwsza_klatka_gotowa()&&!PrezentacjaStartu::dozwolona());
        assert(KlatkiStartuDiag::liczniki().visible==0&&native_rows==0);
        assert(native==std::vector<uint32_t>(1280*720,0xE750A1));
        VirtioPciDevice dev{};assert(setup_mock_gpu_device(&dev));assert(virtio_gpu_init(&dev));
        auto* gpu=virtio_gpu_get_driver(0);assert(gpu&&gpu->initial_scanout_pending);
        assert(gpu_scanout_model.display_resets==0);
        const auto& diagnostic=KlatkiStartuDiag::info();
        assert(!KlatkiStartuDiag::ink(blank.data(),1280,720,1280,diagnostic.title));
        assert(!KlatkiStartuDiag::ink(blank.data(),1280,720,1280,diagnostic.message));
        assert(gpu_scanout_model.visible.empty());
        assert(!virtio_gpu_cmd_set_scanout(gpu,0,gpu->resource_id,0,0,gpu->fb_width,gpu->fb_height));
        assert(grafika_aktywuj_virtio_gpu(gpu));
        assert(native_rows==0&&native==std::vector<uint32_t>(1280*720,0xE750A1));
        if(gpu_scanout_model.visible.size()!=2||gpu_scanout_model.invalid) {
            fprintf(stderr,"Initial visible events=%zu invalid=%d\n",gpu_scanout_model.visible.size(),gpu_scanout_model.invalid);
            for(const auto& line:g_klog_lines)fprintf(stderr,"%s\n",line.c_str());
        }
        assert(gpu_scanout_model.visible.size()==2&&!gpu_scanout_model.invalid);
        assert(gpu->flush_count==1&&gpu->transfer_count==1);
        for(const auto& p:gpu_scanout_model.visible){assert(p.complete);check_image(p.pixels,p.w,p.h,0);}
        if(!efi&&delay==0) {
            const auto previous=gpu_scanout_model.visible.size();
            g_mock.gpu_fail_cmd_type=VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
            assert(!grafika_zmien_sprzet({1024,768,32,4096,0,0}));
            assert(!PrezentacjaStartu::dozwolona()&&gpu_scanout_model.visible.size()==previous);
            g_mock.gpu_fail_cmd_type=0;PrzeniesNaEkran();
            assert(PrezentacjaStartu::dozwolona()&&gpu_scanout_model.visible.size()==previous+1);
            check_image(gpu_scanout_model.visible.back().pixels,1280,720,0);
        }
        // The startup configuration restore path used to attach the new empty
        // host resource here, even after the initial scanout had been fixed.
        const uint32_t visible_resource=gpu_scanout_model.resource;
        const auto before=gpu_scanout_model.visible.size();
        StanBuforowEkranu old{},next{};grafika_pobierz_stan(&old);
        TrybEkranu mode{1024,768,32,4096,0,0};assert(grafika_przygotuj_ekran(mode,&next));
        assert(grafika_zmien_sprzet(mode));
        assert(gpu->initial_scanout_pending&&gpu_scanout_model.resource==visible_resource);
        assert(gpu_scanout_model.visible.size()==before);
        assert(!virtio_gpu_cmd_set_scanout(gpu,0,gpu->resource_id,0,0,1024,768));
        virtio_gpu_free_stale_resource(gpu);assert(gpu->old_resource_id==visible_resource);
        grafika_wymien_stan(next);skladacz_reset_ekranu();
        if(!efi&&delay==0) {
            g_mock.gpu_fail_cmd_type=VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
            test_now=1500000;skladacz_obrazu_zloz_klatke();
            assert(gpu_scanout_model.visible.size()==before&&gpu_scanout_model.resource==visible_resource);
            assert(gpu->initial_scanout_pending&&!gpu->first_transfer_complete);
            virtio_gpu_free_stale_resource(gpu);assert(gpu->old_resource_id==visible_resource);
            g_mock.gpu_fail_cmd_type=0;
        }
        test_now=delay+1500000;skladacz_obrazu_zloz_klatke();
        assert(gpu_scanout_model.visible.size()==before+2&&!gpu_scanout_model.invalid);
        for(size_t i=before;i<gpu_scanout_model.visible.size();++i) {
            const auto& p=gpu_scanout_model.visible[i];assert(p.complete&&p.w==1024&&p.h==768);
            check_image(p.pixels,p.w,p.h,1500000);
        }
        virtio_gpu_free_stale_resource(gpu);assert(gpu->old_resource_id==0);
        grafika_zwolnij_stan(&old);
        // One production compositor transaction produces one logical full commit.
        const auto flushes=gpu->flush_count;test_now=delay+4400000;
        skladacz_obrazu_oznacz_dirty();skladacz_obrazu_obsluz_dirty();
        assert(gpu->flush_count==flushes+1);check_image(gpu_scanout_model.visible.back().pixels,1024,768,4400000);
        assert(!gpu_scanout_model.invalid);
        auto logs=KlatkiStartuDiag::liczniki().visible;
        for(unsigned i=0;i<20;++i)PrzeniesNaEkran();
        assert(KlatkiStartuDiag::liczniki().visible==12&&logs<12);
        if(!efi&&delay==0) {
            auto* log=fopen("/tmp/bursztyn-runtime-publications.log","w");assert(log);
            for(const auto& line:g_klog_lines)
                if(line.find("[SPLASH RUNTIME]")!=std::string::npos)fprintf(log,"%s\n",line.c_str());
            fclose(log);
        }
        powitanie_wycisz();
        // With the optional splash surface gone, exercise the production
        // dirty RAM -> VirtIO backing copy path rather than its full-frame
        // startup publication path.
        const auto dirty_copy_visible_before=gpu_scanout_model.visible.size();
        grafika_rozpocznij_skladanie();
        grafika_odtworz_tlo_regionu(20,30,32,16);
        grafika_zapisz_surowy_piksel(20,30,0x00123456U);
        grafika_prezentuj_region(20,30,32,16);
        grafika_zakoncz_scene();
        assert(gpu_scanout_model.visible.size()==dirty_copy_visible_before+1);
        assert(gpu_scanout_model.visible.back().pixels[30U*1024U+20U]==0x00123456U);
        PrezentacjaStartu::zakoncz_inicjalizacje();
        assert(virtio_gpu_reset(gpu));virtio_gpu_reset_all();gpu_scanout_model.reset(false);
        StanBuforowEkranu final{};grafika_pobierz_stan(&final);
        grafika_wymien_stan({});grafika_zwolnij_stan(&final);
        assert(graphical_allocs.empty()&&g_host_fb_allocs.empty()&&g_host_dma_allocs.empty());
    }
    // No VirtIO device: metadata and off-screen rendering retain firmware;
    // explicitly selecting native fallback then copies the complete source once.
    for(bool efi:{false,true}) {
        test_now=0;native_rows=0;std::vector<uint32_t> native(1280*720,0xE750A1);
        init(native,efi);powitanie_inicjalizuj(true);assert(native_rows==0);
        grafika_zakoncz_wybor_backendu();check_image(native,1280,720,0);
        assert(native_rows==720&&KlatkiStartuDiag::liczniki().visible==1);
        powitanie_wycisz();StanBuforowEkranu last{};grafika_pobierz_stan(&last);
        grafika_wymien_stan({});grafika_zwolnij_stan(&last);
        assert(graphical_allocs.empty());
    }
    printf("Production init/native/compositor/GPU scanout/startup modeset: %u/%u PASS\n",checks,checks);
}

#endif
