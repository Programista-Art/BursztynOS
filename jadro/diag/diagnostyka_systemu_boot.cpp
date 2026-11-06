// Kernel-only adapters. All Ring 3 reads use the pointer-free ABI store.
#include "diagnostyka_systemu.h"
#include "diagnostyka_systemu_format.h"
#include "psf.h"
#include "heap.h"
#include "grafika.h"
#include "loader.h"
#include "instalator.h"
#include "blokowy.h"
#include "network_device.h"
#include "sterowniki/czas/hpet.h"
#include "sterowniki/dzwiek/hda_transport.h"
#include "sterowniki/dzwiek/hda_wewnetrzny.h"
namespace {
bool services_ready{},desktop{},launched{},hda_boot_finished{};uint32_t hda_boot_active=UINT32_MAX;
uint64_t desktop_time{},last_refresh{},last_save{};
uint32_t saved_generation{};unsigned saves{};
bool preferences_loaded{};DiagPreferences boot_preferences{};
constexpr size_t BOOT_REPORT_CAPACITY=1024*1024;
void refresh(){
 for(uint32_t i=0;i<hda_transport_liczba();++i){HdaDiagKontroler h{};if(!hda_transport_stan(i,&h))continue;
  const uint32_t state=h.inicjalizacja==HDA_INIT_BLAD?DIAG_INIT_FAILED:
   h.gotowy&&(h.test_dostepny||hda_boot_active==i)?DIAG_OK:
   h.gotowy?DIAG_UNSUPPORTED:hda_boot_active==i?DIAG_WARNING:
   hda_boot_finished?DIAG_TIMEOUT:DIAG_INACTIVE;
  auto d=diag_device(DIAG_HDA,"Kontroler HDA","hda",state,true,
   state==DIAG_OK?"Kontroler, kodek i ścieżka audio gotowe":
   state==DIAG_INIT_FAILED?"Błąd inicjalizacji HDA":
   state==DIAG_TIMEOUT?"Inicjalizacja HDA nie zakończyła się w limicie czasu":
   state==DIAG_WARNING?"Transport HDA chwilowo niedostępny; sprawdź stan sterownika":
   h.gotowy?"Transport gotowy; brak obsługiwanej ścieżki odtwarzania":"Inicjalizacja transportu HDA trwa");
  d.parent=diag_pci_parent(h.bus,h.slot,h.func);
  d.transport=DIAG_TRANSPORT_PCI;d.bus=h.bus;d.pci_device=h.slot;d.function=h.func;d.pci_valid=1;
  d.vendor=h.vendor_id;d.device=h.device_id;d.bar=h.bar0;d.hardware_status=h.etap_bledu;
  uint16_t mask=h.responsive_codec_mask;while(mask){d.value+=mask&1;mask>>=1;}
  if(state==DIAG_INIT_FAILED||state==DIAG_TIMEOUT||state==DIAG_UNSUPPORTED)d.code=DIAG_CODE_HDA_INIT;
  diag_publish(i,d);
 }
 if(!hda_transport_liczba())diag_simple(DIAG_HDA,"Dźwięk HDA","hda",DIAG_NOT_FOUND,false,"Nie wykryto kontrolera HDA");
}
void save_boot(){
 DiagSummary summary{};diag_get_summary(&summary);
 auto* boot_report=static_cast<char*>(kmalloc(BOOT_REPORT_CAPACITY));
 if(!boot_report){diag_log(DIAG_SYSTEM,DIAG_WARNING,0,"Brak bufora raportu; diagnostyka pozostaje w pamięci");return;}
 struct Release {void* p;~Release(){kfree(p);}} release{boot_report};
 DiagText text(boot_report,BOOT_REPORT_CAPACITY);diag_report_summary(text,summary);
 DiagDevice d{};for(uint32_t i=0;i<summary.devices;++i)if(diag_get_device(i,&d))diag_report_device(text,d);
 text.add("\nLog uruchamiania\n");DiagLog l{};
 for(uint32_t i=0;i<summary.logs;++i)if(diag_get_log(i,&l)){text.scalar("#",l.sequence);text.add(" ");text.add(l.text);text.add("\n");}
 if(text.truncated){diag_log(DIAG_SYSTEM,DIAG_WARNING,DIAG_CODE_STORE_FULL,"Raport rozruchu przekracza limit bufora");return;}
 if(!psf_czy_gotowy())return;
 (void)utworz_katalog("/logi");(void)utworz_plik(DIAG_BOOT_PATH);
 if(zapisz_do_pliku(DIAG_BOOT_PATH,boot_report,uint32_t(text.used))) {
  saved_generation=summary.generation;wypisz_log("[DIAG] Boot report saved: /logi/BOOTLOG.TXT");
 }else diag_log(DIAG_FILESYSTEM,DIAG_WARNING,DIAG_CODE_FILESYSTEM,"Nie udało się zapisać BOOTLOG.TXT; dane pozostają w pamięci");
}
}
void diag_hda_boot_result(bool ready){hda_boot_finished=true;hda_boot_active=ready?hda_wew_kontroler_odtwarzania():UINT32_MAX;}
void diag_boot_services_ready(){
 services_ready=true;
 auto fs=diag_device(DIAG_FILESYSTEM,"System plików PSF / BSP2","psf",psf_czy_gotowy()?DIAG_OK:DIAG_INIT_FAILED,
  psf_czy_gotowy(),psf_czy_gotowy()?
    (instalator_system_zainstalowany()?"PSF / BSP2 zamontowany z dysku":"PSF aktywny w RAM; pliki nie przetrwają restartu Live"):
    "System plików niedostępny");
 fs.value=instalator_system_zainstalowany()?1:0;
 fs.transport=instalator_system_zainstalowany()?DIAG_TRANSPORT_PCI:DIAG_TRANSPORT_MEMORY;
 fs.flags|=DIAG_FLAG_SYSTEM_REQUIRED;if(fs.status!=DIAG_OK)fs.code=DIAG_CODE_FILESYSTEM;diag_publish(0,fs);
 for(uint32_t i=0;i<blokowy_liczba_dyskow();++i){const auto* b=blokowy_dysk(i);if(!b)continue;
  const uint32_t cat=DIAG_STORAGE;
  auto d=diag_device(cat,b->model[0]?b->model:"Dysk blokowy",b->device_type==BlockDeviceType::VirtIO?"virtio-blk":"block",
   DIAG_OK,true,b->read_only?"Dysk zarejestrowany; tylko odczyt":"Dysk zarejestrowany; odczyt i zapis");
  d.transport=b->device_type==BlockDeviceType::VirtIO?DIAG_TRANSPORT_VIRTIO:DIAG_TRANSPORT_PCI;
  d.value=b->total_sectors;diag_publish(i,d);
 }
 diag_simple(DIAG_STORAGE,"Pamięć masowa","block",blokowy_liczba_dyskow()?DIAG_OK:DIAG_NOT_FOUND,
  blokowy_liczba_dyskow()!=0,blokowy_liczba_dyskow()?"Dostępny dysk blokowy":"Brak dysku blokowego; PSF może działać w RAM",UINT64_MAX);
 // Class-based, read-only adapters for hardware without an active native driver.
 // Reuse the PCI inventory; do not rescan config space from this subsystem.
 DiagSummary inventory{};diag_get_summary(&inventory);
 for(uint32_t i=0;i<inventory.devices;++i){DiagDevice pci{};if(!diag_get_device(i,&pci)||pci.category!=DIAG_PCI)continue;
  const uint32_t cls=uint32_t(pci.value);uint32_t category=DIAG_SYSTEM;
  const char* name=nullptr;const char* driver=nullptr;const char* message=nullptr;
  if((cls>>8)==0x0108){category=DIAG_STORAGE;name="Kontroler NVMe";driver="nvme";message="Kontroler NVMe wykryty; brak natywnego sterownika NVMe";}
  else if((cls>>16)==2){
   bool claimed=false;for(uint32_t j=0;j<inventory.devices;++j){DiagDevice active{};
    if(diag_get_device(j,&active)&&active.pci_valid&&active.category!=DIAG_PCI&&active.bus==pci.bus&&
       active.pci_device==pci.pci_device&&active.function==pci.function){claimed=true;break;}}
   if(!claimed){category=DIAG_NETWORK;name="Kontroler sieciowy bez obsługi";driver="network";message="Kontroler wykryty; żaden sterownik nie zgłosił jego obsługi";}
  }else if(cls==0x0c0320||cls==0x0c0300){category=DIAG_USB;name=cls==0x0c0320?"Kontroler EHCI":"Kontroler UHCI";
   driver=cls==0x0c0320?"ehci":"uhci";message="Kontroler wykryty; ten transport USB nie jest obsługiwany";}
  if(name){auto d=diag_device(category,name,driver,DIAG_DRIVER_MISSING,true,message);
   d.parent=pci.id;d.transport=DIAG_TRANSPORT_PCI;d.bus=pci.bus;d.pci_device=pci.pci_device;d.function=pci.function;
   d.pci_valid=1;d.vendor=pci.vendor;d.device=pci.device;diag_publish((1ULL<<63)|pci.id,d);}
 }
 if(!net_get_device_count())diag_simple(DIAG_NETWORK,"Sieć","network",DIAG_NOT_FOUND,false,"Brak zarejestrowanej karty sieciowej");
 // No ACPI namespace interpreter exists. This is an explicit detection limitation.
 diag_simple(DIAG_TOUCHPAD,"Touchpad — wykrywanie niedostępne","hidi2c",DIAG_UNSUPPORTED,false,
  "Brak enumeracji urządzeń ACPI i sterownika HID-over-I2C; mysz USB działa niezależnie");
 refresh();save_boot();
}
void diag_desktop_ready(){__atomic_store_n(&desktop,true,__ATOMIC_RELEASE);}
void diag_service(){
 if(!services_ready)return;
 const uint64_t now=czas_monotoniczny_ns();
 if(!last_refresh||now-last_refresh>=1000000000ULL){last_refresh=now;refresh();}
 if(!__atomic_load_n(&desktop,__ATOMIC_ACQUIRE))return;
 if(!desktop_time)desktop_time=now;
 DiagSummary s{};diag_get_summary(&s);
 // Save two settled snapshots after asynchronous USB enumeration. Bounded writes.
 if(saves<2&&now-desktop_time>=(saves?30000000000ULL:5000000000ULL)&&now-last_save>=5000000000ULL){
  last_save=now;++saves;if(saved_generation!=s.generation||saves==1)save_boot();
 }
 if(launched||instalator_tryb())return;
 if(!preferences_loaded){
  char config[129]{};uint32_t n=0;
  psf_metadane meta{};
  if(psf_czy_gotowy()&&pobierz_metadane_tworu(DIAG_CONFIG_PATH,&meta)&&meta.typ==TYP_PLIK&&meta.rozmiar<=128)
      (void)czytaj_fragment_pliku(DIAG_CONFIG_PATH,0,config,128,&n);
  boot_preferences=diag_parse_preferences(config,n);preferences_loaded=true;
  wypisz_log(boot_preferences.autostart?"[DIAG] Startup preference: autostart=1":"[DIAG] Startup preference: autostart=0");
  wypisz_log(boot_preferences.problems?"[DIAG] Startup preference: problems=1":"[DIAG] Startup preference: problems=0");
 }
 const auto& preferences=boot_preferences;
 if(!preferences.autostart&&!preferences.problems){launched=true;return;}
 if(!preferences.autostart&&now-desktop_time>=15000000000ULL&&!s.serious){launched=true;return;}
 // Normal startup after the desktop has painted; failure-only startup allows
 // 15 seconds for deferred root-port enumeration and input activation.
 if(diag_should_autostart(preferences,s,now-desktop_time)){
  launched=true;
  if(bws_uruchom_program_z_pliku(DIAG_APP_PATH,PZB_UZYTKOWNIK,
       PRAWO_GUI|PRAWO_PLIKI_CZYTAJ|PRAWO_PLIKI_ZAPISZ,false))
    wypisz_log("[DIAG] Diagnostyka systemu launched in Ring 3");
  else diag_log(DIAG_SYSTEM,DIAG_WARNING,0,"Nie udało się uruchomić aplikacji Diagnostyka systemu");
 }
}
