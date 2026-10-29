// Production modeset service, framebuffer swap, compositor and input adapters.
#define ekran_skala fixture_ekran_skala
#define BURSZTYN_POINTER_GRAPHICS_FIXTURE 1
#include "test_splash_production_path.cpp"
#undef ekran_skala
#include "../ekran.cpp"
#include "../mysz_input.h"
extern "C" void WybudzProcesyOczekujaceNaMysz(){}
volatile bool ekran_zajety=false;
extern "C" void zablokuj_ekran(){assert(!ekran_zajety);ekran_zajety=true;}
extern "C" void odblokuj_ekran(){assert(ekran_zajety);ekran_zajety=false;}
void bws_aktualizuj_ekran(uint32_t s){assert(ui_skala_poprawna(s));}
bool psf_czy_gotowy(){return false;}
bool zapewnij_katalog(const char*){return false;}
bool pobierz_metadane_tworu(const char*,psf_metadane*){return false;}
bool utworz_plik(const char*){return false;}
bool zapisz_do_pliku(const char*,const char*,uint32_t){return false;}
bool psf_flush_cache(){return false;}
uint32_t rozmiar_pliku(const char*){return 0;}
bool czytaj_fragment_pliku(const char*,uint64_t,char*,uint32_t,uint32_t*){return false;}
bool tapety_przygotuj_ekran(uint32_t* dst,uint32_t w,uint32_t h,size_t cap){if(cap<size_t(w)*h*4)return false;std::fill(dst,dst+size_t(w)*h,0xff203040U);return true;}
int main(){
 std::vector<uint32_t> native(1280*720,0);init(native,true);
 powitanie_inicjalizuj(false);
 input_reset_all();input_set_pointer_ready(true);
 InputDevice physical{},virtio{};
 for(auto* dev:{&physical,&virtio}){dev->kinds=INPUT_DEVICE_KIND_MOUSE;dev->pointer_mode=INPUT_POINTER_RELATIVE;dev->bus_type=dev==&physical?INPUT_BUS_PS2:INPUT_BUS_VIRTIO;assert(input_register_device(dev));}
 InputDevice tablet{};tablet.kinds=INPUT_DEVICE_KIND_TABLET;tablet.pointer_mode=INPUT_POINTER_ABSOLUTE;tablet.bus_type=INPUT_BUS_VIRTIO;
 tablet.abs_x.min=tablet.abs_y.min=0;tablet.abs_x.max=tablet.abs_y.max=65535;assert(input_register_device(&tablet));
 // Virtual GPU is the production driver backed by the existing DMA fixture.
 VirtioPciDevice device{};assert(setup_mock_gpu_device(&device));assert(virtio_gpu_init(&device));
 auto* gpu=virtio_gpu_get_driver(0);assert(gpu && gpu->initialized);
 assert(grafika_aktywuj_virtio_gpu(gpu));
 ekran_inicjalizuj(); // Production boot publishes the initial display generation.
 struct Target{int w,h;uint32_t scale;};
 for(auto target:{Target{1920,1080,125},{1280,720,100},{1280,720,125},{1920,1080,125},{1920,1080,100},{1280,720,200},{1920,1080,100}}){
  TrybEkranu mode{uint32_t(target.w),uint32_t(target.h),32,uint32_t(target.w*4),0,0};
  auto previous=grafika_geometria_ekranu();
  assert(zastosuj(mode,target.scale,0,false,false)==EKRAN_OK);
  auto g=grafika_geometria_ekranu();
  assert(g.generation!=previous.generation);assert(g.width==target.w && g.height==target.h && g.scale==target.scale);
  assert(g.cursor_max_x==target.w-1 && g.cursor_max_y==target.h-1);
  assert(g.work_height==target.h-ui_skaluj(40,target.scale));assert(g.taskbar_y==g.work_height);
  assert(grafika_pobierz_mysz_x()>=0 && grafika_pobierz_mysz_x()<target.w);
  assert(grafika_pobierz_mysz_y()>=0 && grafika_pobierz_mysz_y()<target.h);
  // No clock/service/poll between transaction return and first input report.
  for(auto* dev:{&physical,&virtio})for(auto corner:{std::pair<int,int>{0,0},{target.w-1,0},{0,target.h-1},{target.w-1,target.h-1},{target.w/2,target.h-1}}){
   InputEvent events[]={{INPUT_EV_REL,INPUT_REL_X,uint32_t(corner.first-grafika_pobierz_mysz_x())},{INPUT_EV_REL,INPUT_REL_Y,uint32_t(corner.second-grafika_pobierz_mysz_y())},{INPUT_EV_SYN,0,0}};
   input_dispatch_events(dev,events,3);assert(grafika_pobierz_mysz_x()==corner.first && grafika_pobierz_mysz_y()==corner.second);
  }
  for(auto corner:{std::pair<int,int>{0,0},{target.w-1,0},{0,target.h-1},{target.w-1,target.h-1},{target.w/2,target.h-1}}){
   const auto raw=[](int pixel,int extent){return uint32_t((int64_t(pixel)*65535+extent-2)/(extent-1));};
   InputEvent events[]={{INPUT_EV_ABS,INPUT_ABS_X,raw(corner.first,target.w)},{INPUT_EV_ABS,INPUT_ABS_Y,raw(corner.second,target.h)},{INPUT_EV_SYN,0,0}};
   input_dispatch_events(&tablet,events,3);assert(grafika_pobierz_mysz_x()==corner.first && grafika_pobierz_mysz_y()==corner.second);
  }
 }
 const auto previous=grafika_geometria_ekranu();
 TrybEkranu smaller{1280,720,32,5120,0,0};
 zaktualizuj_mysze_pozycja_zrodlo(1800,900,0,"shrink-fixture");
 assert(zastosuj(smaller,125,0,false,true)==EKRAN_OCZEKUJE);
 assert(grafika_pobierz_mysz_x()==1279 && grafika_pobierz_mysz_y()==719);
 auto temporary=grafika_geometria_ekranu();assert(cofnij());
 auto restored=grafika_geometria_ekranu();assert(restored.generation!=temporary.generation);
 assert(restored.width==previous.width && restored.height==previous.height && restored.scale==previous.scale);
 input_reset_all();StanBuforowEkranu last{};grafika_pobierz_stan(&last);grafika_wymien_stan({});grafika_zwolnij_stan(&last);
 printf("Display transaction / immediate physical edges: %u checks PASS\n",checks);
}
