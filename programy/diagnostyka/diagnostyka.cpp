#define BURSZTYN_UI_CZCIONKA_SYSTEMOWA 1
#include "../../bursztyn_gui.h"
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "../../loader.h"
#include "model.h"
using namespace BursztynUI;
namespace {
DiagAppModel model;
DiagDevice staging[DIAG_MAX_DEVICES];
char report[1024*1024];
DiagPreferences preferences;
const uint32_t summary_categories[]={DIAG_CPU,DIAG_MEMORY,DIAG_ACPI,DIAG_GRAPHICS,DIAG_STORAGE,DIAG_USB,DIAG_KEYBOARD,DIAG_MOUSE,DIAG_TOUCHPAD,DIAG_HDA,DIAG_NETWORK};
const char* summary_names[]={"Procesor","Pamięć","ACPI","Grafika","Dyski","USB","Klawiatura","Mysz","Touchpad","Dźwięk","Sieć"};
template<class T> bool read(uint32_t cmd,T* out,uint32_t index=0){
 return bws_wywolaj(BWS_DIAGNOSTYKA_SYSTEMU,(uint64_t(DIAG_ABI)<<32)|cmd,
  reinterpret_cast<uint64_t>(out),sizeof(T),index)==1;
}
bool refresh(){
 for(unsigned retry=0;retry<3;++retry){DiagSummary s{},after{};
  if(!read(DIAG_GET_SUMMARY,&s)||s.version!=DIAG_ABI||s.size!=sizeof(s)||s.devices>DIAG_MAX_DEVICES||s.logs>DIAG_MAX_LOG)return false;
  bool ok=true;for(uint32_t i=0;i<s.devices;++i)if(!read(DIAG_GET_DEVICE,&staging[i],i)){ok=false;break;}
  if(!ok||!read(DIAG_GET_SUMMARY,&after))return false;
  if(s.generation!=after.generation)continue;
  for(uint32_t i=model.valid?model.summary.logs:0;i<s.logs;++i)if(!read(DIAG_GET_LOG,&model.logs[i],i))return false;
  for(uint32_t i=0;i<s.devices;++i)model.devices[i]=staging[i];
  model.summary=s;model.valid=true;model.rebuild();return true;
 }return false;
}
bool persistent_fs(){
 if(!model.valid)return false;
 for(uint32_t i=0;i<model.summary.devices;++i)if(model.devices[i].category==DIAG_FILESYSTEM)
  return model.devices[i].status==DIAG_OK&&model.devices[i].value==1;
 return false;
}
bool save_text(const char* path,const char* text,size_t length){
 const uint64_t created=bws_wywolaj(BWS_UTWORZ_PLIK,reinterpret_cast<uint64_t>(path),BWS_STATUS_PLIKOWY,0,0);
 if(created!=uint64_t(WynikPlikowy::Ok)&&created!=uint64_t(WynikPlikowy::Istnieje))return false;
 if(bws_wywolaj(BWS_ZAPISZ_PLIK,reinterpret_cast<uint64_t>(path),reinterpret_cast<uint64_t>(text),length,0)!=1)return false;
 BwsMetadanePliku m{};return pobierz_metadane_pliku(path,&m)&&(m.flagi&BWS_META_ROZMIAR_DOSTEPNY)&&m.rozmiar==length;
}
void load_preferences(){char b[129]{};uint32_t n=0;if(pobierz_rozmiar_pliku(DIAG_CONFIG_PATH,&n)&&n<=128&&czytaj_plik(DIAG_CONFIG_PATH,b,n))preferences=diag_parse_preferences(b,n);}
class Data : public DostawcaDanychTabeli {
public:
 explicit Data(unsigned m):mode(m){}unsigned mode;
 size_t liczba_wierszy()override{if(!model.valid)return 0;return mode==0?11:mode==1?model.summary.devices:mode==2?model.driver_count:model.visible_count;}
 size_t liczba_kolumn()override{return mode==0?3:mode==1?5:mode==2?5:3;}
 const char* naglowek_kolumny(size_t c)override{
  const char* a[]={"Składnik","Stan","Informacja"};const char* b[]={"Urządzenie","Stan","Transport","PCI / USB","Informacja"};
  const char* d[]={"Sterownik","Urządzenie","Stan","Transport","Informacja"};const char* l[]={"#","Kategoria","Komunikat"};
  return mode==0?a[c]:mode==1?b[c]:mode==2?d[c]:l[c];
 }
 int szerokosc_kolumny(size_t c)override{
  const int a[]={150,210,650},b[]={260,190,80,160,650},d[]={120,230,190,80,650},l[]={55,110,1600};
  return mode==0?a[c]:mode==1?b[c]:mode==2?d[c]:l[c];
 }
 const char* pobierz_komorke(size_t row,size_t col,char* buffer,size_t cap)override{
  if(row>=liczba_wierszy()||col>=liczba_kolumn())return "";
  DiagText t(buffer,cap);
  if(mode==0){uint32_t cat=summary_categories[row];
   if(col==0)return summary_names[row];
   if(col==1)return diag_status_text(model.summary.categories[cat]);
   if(cat==DIAG_KEYBOARD)return model.summary.keyboard?"Dostępne źródło wejścia":"Podłącz klawiaturę USB; sprawdź USB i Sterowniki. INPUT-KBD-0001";
   if(cat==DIAG_MOUSE)return model.summary.pointer?"Dostępny wskaźnik":"Podłącz mysz USB; sprawdź USB i Sterowniki. INPUT-PTR-0001";
   if(cat==DIAG_TOUCHPAD)return "Wykrywanie ACPI / HID-over-I2C niedostępne. Mysz USB jest niezależna.";
   if(cat==DIAG_NETWORK&&model.summary.categories[DIAG_VIRTIO_NET]==DIAG_OK)cat=DIAG_VIRTIO_NET;
   for(uint32_t i=0;i<model.summary.devices;++i)if(model.devices[i].category==cat){
    if(cat==DIAG_CPU)return model.devices[i].name;
    if(cat==DIAG_MEMORY){t.number(model.devices[i].value/(1024*1024));t.add(" MiB dostępnych wg firmware");return buffer;}
    return model.devices[i].message;}
   return "Brak opublikowanych danych sterownika";
  }
  if(mode==3){const auto& l=model.logs[model.visible_logs[row]];
   if(col==0){t.number(l.sequence);return buffer;}if(col==1)return diag_category_text(l.category);return l.text;}
  const uint32_t idx=mode==1?model.order[row]:model.drivers[row];const auto& d=model.devices[idx];
  if(mode==2){if(col==0)return d.driver;if(col==1)return d.name;if(col==2)return diag_status_text(d.status);if(col==3)return diag_transport_text(d.transport);}
  else {if(col==0){for(unsigned n=0;n<model.depth(idx);++n)t.add("  ");t.add(d.name);return buffer;}
   if(col==1)return diag_status_text(d.status);
   if(col==2)return diag_transport_text(d.transport);
   if(col==3){if(d.pci_valid){t.number(d.bus,16);t.scalar(":",d.pci_device,16);t.scalar(".",d.function,16);t.add(" ");}
    if(d.vendor||d.device){t.number(d.vendor,16);t.add(":");t.number(d.device,16);}return buffer;}}
  t.add(d.message);if(d.code){t.add(" [");t.add(diag_code_text(d.code));t.add("]");}return buffer;
 }
};
class Window;
class Tick : public Kontrolka {
public:Window* window{};uint64_t last{};
 bool czy_wymaga_animacji()const override{return true;}
 void krok_animacji(uint64_t ns)override;
};
class Window : public Okno {
public:
 Window():Okno("Diagnostyka systemu",1040,650),status_data(0),devices_data(1),drivers_data(2),logs_data(3){}
 ~Window()override{zarzadca_popupow().zamknij(PowodZamknieciaPopup::WlascicielZamkniety);auto* p=kontrolka_glowna();ustaw_kontrolke_glowna(nullptr);delete p;}
 Data status_data,devices_data,drivers_data,logs_data;
 PasekInformacyjny* banner{};PoleWielowierszowe* details{};TabelaWirtualna* tables[4]{};
 PoleWyboru* normal{};PoleWyboru* problems{};
 void message(const char* s,TypKomunikatu t=TypKomunikatu::Informacja){banner->ustaw_tekst(s);banner->ustaw_typ_komunikatu(t);}
 void update(){
  if(!refresh()){message("Dane diagnostyczne chwilowo niedostępne — spróbuj Odśwież",TypKomunikatu::Ostrzezenie);return;}
  char line[192];DiagText t(line,sizeof(line));t.add("Stan systemu: ");t.add(diag_status_text(model.summary.status));
  t.scalar("   Błędy: ",model.summary.errors);t.scalar("   Ostrzeżenia: ",model.summary.warnings);
  if(model.summary.dropped_devices||model.summary.dropped_logs)t.add("   Zapis niepełny (limit)");
  message(line,model.summary.serious?TypKomunikatu::Blad:model.summary.errors||model.summary.warnings?TypKomunikatu::Ostrzezenie:TypKomunikatu::Sukces);
  for(auto* table:tables)table->uniewaznij();
 }
 void preferences_changed(){
  const DiagPreferences proposed{normal->czy_zaznaczone(),problems->czy_zaznaczone()};
  char text[32]{};
  // Positions are generated rather than coupled to translated labels.
  DiagText t(text,sizeof(text));t.add("autostart=");t.add(proposed.autostart?"1":"0");t.add("\nproblems=");t.add(proposed.problems?"1":"0");t.add("\n");
  if(save_text(DIAG_CONFIG_PATH,text,t.used)){preferences=proposed;wypisz(preferences.autostart?"[DIAG APP] Saved autostart=1\n":"[DIAG APP] Saved autostart=0\n");wypisz(preferences.problems?"[DIAG APP] Saved problems=1\n":"[DIAG APP] Saved problems=0\n");message(persistent_fs()?"Ustawienia zapisane. Zmiana obowiązuje przy następnym uruchomieniu.":
    "Zapisano w RAM. Ustawienia nie przetrwają restartu systemu Live.",persistent_fs()?TypKomunikatu::Sukces:TypKomunikatu::Ostrzezenie);}
  else message("Nie zapisano ustawień. Sprawdź prawa i dostępność systemu plików.",TypKomunikatu::Blad);
 }
 void init(){
  load_preferences();auto& root=dodaj<PanelDokowany>();root.ustaw_wypelnienie(Marginesy(8));root.ustaw_odstep(6);
  banner=&root.dodaj<PasekInformacyjny>(Dokowanie::Gora,TypKomunikatu::Informacja,"Pobieranie stanu systemu");banner->ustaw_przycisk_zamkniecia(false);
  auto& actions=root.dodaj<Siatka>(Dokowanie::Dol);actions.ustaw_preferowany_rozmiar(Rozmiar(-1,36));actions.dodaj_wiersz();
  for(int i=0;i<3;++i)actions.dodaj_kolumne();
  const char* buttons[]={"Odśwież","Zapisz raport","Kopiuj raport"};
  for(int i=0;i<3;++i){auto* b=new Przycisk(buttons[i]);b->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);actions.dodaj_do_siatki(b,0,i);
   if(i==0)b->przy_kliknieciu([](Przycisk*,void* p){static_cast<Window*>(p)->update();},this);
   else if(i==1)b->przy_kliknieciu([](Przycisk*,void* p){auto* w=static_cast<Window*>(p);size_t n=0;
    if(!model.report(report,sizeof(report),&n)){w->message("Raport przekracza limit lub dane są niedostępne",TypKomunikatu::Blad);return;}
    (void)utworz_katalog_uzytkownika("/logi");
    const bool ok=save_text(DIAG_REPORT_PATH,report,n);if(ok)wypisz("[DIAG APP] Report saved and size verified: /logi/DIAG.TXT\n");w->message(ok?(persistent_fs()?"Raport zapisany: /logi/DIAG.TXT":"Raport zapisany w RAM: /logi/DIAG.TXT. Skopiuj go przed restartem."):
    "Nie udało się zapisać raportu",ok?(persistent_fs()?TypKomunikatu::Sukces:TypKomunikatu::Ostrzezenie):TypKomunikatu::Blad);},this);
   else b->przy_kliknieciu([](Przycisk*,void* p){size_t n=0;auto* w=static_cast<Window*>(p);
    const bool ok=model.report(report,sizeof(report),&n)&&clipboard_set("text/plain",report,n+1);
    if(ok)wypisz("[DIAG APP] Report copied to clipboard\n");
    w->message(ok?"Raport skopiowany do schowka":"Nie udało się skopiować raportu",ok?TypKomunikatu::Sukces:TypKomunikatu::Blad);},this);
  }
  normal=&root.dodaj<PoleWyboru>(Dokowanie::Dol,"Pokazuj Diagnostykę systemu przy uruchomieniu",preferences.autostart);
  problems=&root.dodaj<PoleWyboru>(Dokowanie::Dol,"Uruchamiaj przy wykryciu problemów sprzętowych",preferences.problems);
  normal->przy_przelaczeniu([](PoleWyboru*,bool,void* p){static_cast<Window*>(p)->preferences_changed();},this);
  problems->przy_przelaczeniu([](PoleWyboru*,bool,void* p){static_cast<Window*>(p)->preferences_changed();},this);
  details=&root.dodaj<PoleWielowierszowe>(Dokowanie::Dol,"",2048);
  details->ustaw_tekst("Wybierz urządzenie, sterownik lub log. Zaznacz tekst i użyj Ctrl+C. Log rozruchu: /logi/BOOTLOG.TXT");
  details->ustaw_tylko_do_odczytu(true);details->ustaw_zawijanie_wierszy(true);details->ustaw_preferowany_rozmiar(Rozmiar(-1,80));
  auto& ticker=root.dodaj<Tick>(Dokowanie::Gora);ticker.window=this;ticker.ustaw_preferowany_rozmiar(Rozmiar(0,0));
  auto& tabs=root.dodaj<Karty>(Dokowanie::Wypelnij);
  const char* titles[]={"Stan systemu","Urządzenia","Sterowniki","Log uruchamiania"};Data* providers[]={&status_data,&devices_data,&drivers_data,&logs_data};
  for(int i=0;i<4;++i){auto* page=new PanelDokowany();page->ustaw_wypelnienie(Marginesy(0));
   if(i==3){auto& filter=page->dodaj<ListaRozwijana>(Dokowanie::Gora);filter.ustaw_preferowany_rozmiar(Rozmiar(-1,32));
    const char* names[]={"Wszystko","Błędy","Ostrzeżenia","USB","PCI","Input","Dźwięk","Dyski","Sieć"};for(auto n:names)filter.dodaj_element(n);filter.ustaw_wybrany_indeks(0);
    filter.przy_zmianie_wyboru([](ListaRozwijana*,int n,void* p){if(n<0||n>8)return;model.filter=unsigned(n);model.rebuild();static_cast<Window*>(p)->tables[3]->ustaw_dostawce(&static_cast<Window*>(p)->logs_data);},this);}
   tables[i]=&page->dodaj<TabelaWirtualna>(Dokowanie::Wypelnij);tables[i]->ustaw_dostawce(providers[i]);tabs.dodaj_karte(titles[i],page);
  }
  tables[1]->przy_zmianie_wyboru([](TabelaWirtualna*,size_t row,void* p){if(row>=model.summary.devices)return;
   const auto& d=model.devices[model.order[row]];char b[512];DiagText t(b,sizeof(b));diag_report_device(t,d);static_cast<Window*>(p)->details->ustaw_tekst(b);},this);
  tables[2]->przy_zmianie_wyboru([](TabelaWirtualna*,size_t row,void* p){if(row>=model.driver_count)return;
   const auto& d=model.devices[model.drivers[row]];char b[512];DiagText t(b,sizeof(b));diag_report_device(t,d);static_cast<Window*>(p)->details->ustaw_tekst(b);},this);
  tables[3]->przy_zmianie_wyboru([](TabelaWirtualna*,size_t row,void* p){if(row>=model.visible_count)return;
   const auto& l=model.logs[model.visible_logs[row]];char b[512];DiagText t(b,sizeof(b));t.add(l.text);
   if(l.code){t.add(" [");t.add(diag_code_text(l.code));t.add("]");}static_cast<Window*>(p)->details->ustaw_tekst(b);},this);
  tabs.przy_zmianie_karty([](Karty*,int i,void*){const char* traces[]={"[DIAG APP] View=system\n","[DIAG APP] View=devices\n","[DIAG APP] View=drivers\n","[DIAG APP] View=log\n"};if(i>=0&&i<4)wypisz(traces[i]);});
  update();wypisz("[DIAG APP] Ring 3 diagnostic window initialized\n");
 }
};
void Tick::krok_animacji(uint64_t ns){if(!last)wypisz("[DIAG APP] First frame submitted\n");if(window&&ns-last>=1000000000ULL){last=ns;DiagSummary s{};
 if(read(DIAG_GET_SUMMARY,&s)&&(!model.valid||s.generation!=model.summary.generation||s.logs!=model.summary.logs||s.errors!=model.summary.errors||s.warnings!=model.summary.warnings||s.dropped_devices!=model.summary.dropped_devices||s.dropped_logs!=model.summary.dropped_logs))window->update();}Kontrolka::krok_animacji(ns);}
}
extern "C" __attribute__((noreturn)) void _start(){AplikacjaGUI app;auto* w=new Window;w->init();(void)app.uruchom(*w);delete w;bws_zakoncz_proces();}
