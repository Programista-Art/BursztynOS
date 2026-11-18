#include "bursztyn_ui_host_fixture.h"
#include "../programy/ustawienia/themes_ui.h"
#include "start_menu_ui.h"
#include <string>
#include <cassert>
using namespace BursztynUI;
bool czytaj_fragment_pliku(const char*,uint64_t,char*,uint32_t,uint32_t*){return false;}

static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);abort();}}while(0)
static proces_t process{};
static std::string directory;
static BursztynMotyw extra_theme{};
static bool extra_theme_available=false;
static uint64_t hook(uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t){
 if(n==6){CHECK(!strcmp(reinterpret_cast<char*>(a),MOTYW_SCIEZKA_KATALOG));if(directory.size()+1>c)return 0;memcpy(reinterpret_cast<void*>(b),directory.c_str(),directory.size()+1);return 1;}
 if(n==87){
  if(extra_theme_available && a==MOTYW_CMD_OPIS && b && c && !strcmp(reinterpret_cast<const char*>(b),extra_theme.id)){
   *reinterpret_cast<BursztynMotyw*>(c)=extra_theme;return 1;
  }
  return motyw_bws_polecenie(&process,a,b,c);
 }
 return 0;
}
static void key(StartMenu::Surface& s,uint32_t k){ZdarzenieKlawiatury e{};e.kod_klawisza=k;CHECK(s.events.popups.obsluz_klawisz(e));}
static void event(StartMenu::Surface& s,uint32_t type,int x=0,int y=0){bws_zdarzenie e{};e.typ=type;e.x=x;e.y=y;s.handle(e);}
// [TEST 9/9] z programy/test/test.cpp: dawne stale oczekiwania kontra
// faktyczne motywy wbudowane. Odtwarza liczbe porazek z logu natywnego
// (aktywny motyw uzytkownika != domyslny) i sprawdza nowe kryteria.
static void native_theme_expectation_cases(){
 BursztynMotyw nocny{};CHECK(motyw_parsuj(PLIK_BURSZTYN_NOCNY,strlen(PLIK_BURSZTYN_NOCNY),&nocny));
 CHECK(memcmp(&nocny,&g_domyslny_motyw_fallback,sizeof(nocny))==0);          // wzorzec testu = wbudowany plik
 CHECK(nocny.ostrzezenie==0x00D97706U && nocny.zaznaczenie==0x00314A63U);   // dawny test oczekiwal 0xF2A900
 struct Stary{const char* pole;uint32_t BursztynMotyw::*m;uint32_t v;};
 const Stary stare[]={{"pulpit",&BursztynMotyw::pulpit,0x111315},{"oknotlo",&BursztynMotyw::oknotlo,0x191C20},
  {"panel",&BursztynMotyw::panel,0x20242A},{"podniesiony",&BursztynMotyw::podniesiony,0x262B31},{"ramka",&BursztynMotyw::ramka,0x343A42},
  {"tekst",&BursztynMotyw::tekst,0xF2F3F5},{"tekstdrugorzedny",&BursztynMotyw::tekstdrugorzedny,0xA7ADB5},{"wylaczony",&BursztynMotyw::wylaczony,0x6F7680},
  {"akcent",&BursztynMotyw::akcent,0xF2A900},{"akcentnajechany",&BursztynMotyw::akcentnajechany,0xFFBC32},{"akcentaktywny",&BursztynMotyw::akcentaktywny,0xD99100},
  {"blad",&BursztynMotyw::blad,0xE85D5D},{"sukces",&BursztynMotyw::sukces,0x51B87A},{"ostrzezenie",&BursztynMotyw::ostrzezenie,0xF2A900},
  {"informacja",&BursztynMotyw::informacja,0x58A6E7},{"poletekstowe",&BursztynMotyw::poletekstowe,0x15181C},{"zaznaczenie",&BursztynMotyw::zaznaczenie,0xF2A900},
  {"przycisk",&BursztynMotyw::przycisk,0x262B31},{"przycisknajechany",&BursztynMotyw::przycisknajechany,0x343A42},{"tekstakcentu",&BursztynMotyw::tekstakcentu,0x111315}};
 struct Wbudowany{const char* tekst;unsigned stare_porazki;};
 const Wbudowany wbudowane[]={{PLIK_BURSZTYN_NOCNY,2},{PLIK_BALTYCKA_NOC,277},{PLIK_GRAFIT,382},{PLIK_BURSZTYN_KLASYCZNY,277},{PLIK_BURSZTYN_JASNY,278}};
 for(const auto& w:wbudowane){
  BursztynMotyw m{};CHECK(motyw_parsuj(w.tekst,strlen(w.tekst),&m));CHECK(motyw_waliduj(&m));
  const bool id=!strcmp(m.id,"bursztynnocny");
  unsigned f=!id;                                                         // id poczatkowe
  for(const auto& st:stare)f+=m.*st.m!=st.v;                                // 20 kolorow
  f+=(m.rogokna!=10)+(m.rogpanelu!=8)+(m.rogprzycisku!=7)+(m.rogpola!=6)+(m.rogmenu!=8)+(m.rogpodpowiedzi!=6)+(m.gruboscramki!=1)+(m.cienokna!=1);
  f+=2*((m.pulpit!=0x111315)+(m.akcent!=0xF2A900));                         // akcesory + BWS86 v2
  f+=(m.pulpit!=0x111315)+(m.akcent!=0xF2A900)+!id;                         // ABI v1
  f+=50*(!id+(m.pulpit!=0x111315)+(m.akcent!=0xF2A900)+(m.panel!=0x20242A)+(m.ramka!=0x343A42)+(m.rogokna!=10)+(m.rogpanelu!=8));
  CHECK(f==w.stare_porazki);
  printf("TEST 9/9 dawne oczekiwania, aktywny %-18s -> %u porazek\n",m.id,f);
  // Nowe kryteria: aktywny motyw z akcesorami (motyw_kolor) zgodny z samym soba.
  test_ustaw_aktywny_motyw(&m);
  CHECK(motyw_waliduj_id(m.id));
  CHECK(motyw_kolor(MOTYW_KOLOR_PULPIT)==m.pulpit && motyw_kolor(MOTYW_KOLOR_AKCENT)==m.akcent &&
        motyw_kolor(MOTYW_KOLOR_OSTRZEZENIE)==m.ostrzezenie && motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE)==m.zaznaczenie &&
        motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU)==m.tekstakcentu);
  CHECK(motyw_promien(MOTYW_ROG_OKNA)==m.rogokna && motyw_promien(MOTYW_ROG_PODPOWIEDZI)==m.rogpodpowiedzi);
 }
 test_ustaw_aktywny_motyw(nullptr);
}
int main(){
 native_theme_expectation_cases();
 {
  StartMenu::Surface s;s.open({0,0,1280,680},10);s.open_power();
  struct Probe:Kontrolka{size_t offset(){return reinterpret_cast<char*>(&obszar_ekranu_)-reinterpret_cast<char*>(this);}} probe;
  auto* base=reinterpret_cast<const unsigned char*>(&s);
  size_t button_slot=0;for(size_t i=0;i+sizeof(void*)<=sizeof(s);i+=alignof(void*)){void* p=nullptr;memcpy(&p,base+i,sizeof(p));if(p==s.dziecko(2)){button_slot=i;break;}}
  auto* popup=s.events.popups.fokus_popupu();
  auto* first=s.power_menu().element(0)->obraz_ikony()-1;
  size_t manager_offset=0;
  for(size_t i=0;i+sizeof(void*)<=sizeof(WidokMenuPopup);i+=alignof(void*)){void* p=nullptr;memcpy(&p,reinterpret_cast<const unsigned char*>(popup)+i,sizeof(p));if(p==&s.events.popups){manager_offset=i;break;}}
  CHECK(manager_offset>0);
  FILE* f=fopen(".bsf-test/power-observer.json","w");
  fprintf(f,"{\"button_slot\":%zu,\"popup_offset\":%zu,\"rect_offset\":%zu,\"icons_offset\":%zu,\"icon_size\":%zu,\"pixel_offset\":%zu,\"manager_offset\":%zu}\n",button_slot,reinterpret_cast<const unsigned char*>(popup)-base,probe.offset(),reinterpret_cast<const unsigned char*>(first)-base,sizeof(IkonaPNG),size_t(24),manager_offset);
  fclose(f);s.close();
 }
 process.pid=7;process.poziom_zaufania=PZB_ZAUFANE;process.uprawnienia=PRAWO_SYSTEM_CONFIG;
 g_test_bws_hook=hook;motyw_inicjalizuj();g_aktywny_test_motyw=motyw_pobierz_aktywny();
 directory="[PLIK] grafit.motyw\n[PLIK] baltyckanoc.motyw\n[PLIK] baltycka_noc.motyw\n[PLIK] bursztynklasyczny.motyw\n[PLIK] bursztynjasny.motyw\n[PLIK] bursztynnocny.motyw\n[PLIK] missing.motyw\n[PLIK] .bad.motyw\n[KAT]  ignored.motyw\n";
 {
 SettingsUI::ThemesPage page;page.load();CHECK(page.count==5);CHECK(page.selected==4);
 const char* original=g_aktywny_motyw.id;
 char committed[32]{};strcpy(committed,original);
 page.select(0);page.start_preview();CHECK(page.transaction.active());CHECK(!strcmp(g_aktywny_motyw.id,"grafit"));
 page.select(1);CHECK(page.transaction.active());CHECK(!strcmp(g_aktywny_motyw.id,"baltyckanoc"));CHECK(!strcmp(g_motyw_przed_podgladem.id,committed));
 page.zaktualizuj_motyw();CHECK(page.selected==1);CHECK(page.transaction.active());
 page.krok_animacji(test_monotonic_ns+15000000000ULL);CHECK(!page.transaction.active());CHECK(!strcmp(g_aktywny_motyw.id,committed));
 page.select(0);page.start_preview();CHECK(page.transaction.apply(page.entries[0].id));motyw_sprawdz_timeout_podgladu(999999);CHECK(!strcmp(g_aktywny_motyw.id,"grafit"));
 page.select(1);page.start_preview();page.cancel_preview();CHECK(!strcmp(g_aktywny_motyw.id,"grafit"));
 page.start_preview();motyw_posprzataj_proces(7);CHECK(!strcmp(g_aktywny_motyw.id,"grafit"));page.cancel_preview();
 CHECK(!page.transaction.preview("missing",test_monotonic_ns));CHECK(!page.transaction.preview("../invalid",test_monotonic_ns));
 page.start_preview();
 }
 CHECK(!g_podglad_aktywny);CHECK(!strcmp(g_aktywny_motyw.id,"grafit"));
 {SettingsUI::ThemesPage restarted;restarted.load();CHECK(restarted.selected==0);}
 directory="";{SettingsUI::ThemesPage empty;empty.load();CHECK(empty.count==0);CHECK(!empty.valid());}
 {
  CHECK(motyw_zaladuj_z_pliku("bursztynjasny",&extra_theme));strcpy(extra_theme.id,"fixture_extra");
  strcpy(extra_theme.nazwa,"Additional theme fixture");extra_theme.przycisk=0x395127;extra_theme.akcent=0xc7d26b;
  extra_theme_available=true;directory="[PLIK] grafit.motyw\n[PLIK] fixture_extra.motyw\n";
  SettingsUI::ThemesPage enumeration;enumeration.load();CHECK(enumeration.count==2);
  CHECK(!strcmp(enumeration.entries[1].id,"fixture_extra"));
  auto* previous=gui_pobierz_motyw();test_ustaw_aktywny_motyw(&extra_theme);
  StartMenu::Surface s;s.open({0,0,1280,680},10);
  CHECK(s.dziecko(2)->styl().normalny.tlo==extra_theme.przycisk);s.close();
  test_ustaw_aktywny_motyw(previous);extra_theme_available=false;
 }
 // The WM retains Start while closed. Theme broadcasts must reach its next
 // opening as well as an already visible surface.
 {
  StartMenu::Surface retained;
  for(const char* id:{"baltyckanoc","grafit","bursztynklasyczny","bursztynjasny","bursztynnocny"}){
   CHECK(motyw_ustaw_z_pliku(id,false));Motyw::pobierz_domyslny().zaktualizuj_z_systemu();
   retained.refresh();retained.open({0,0,1280,680},10);
   auto* button=retained.dziecko(2);
   CHECK(!button->czy_ma_fokus());
   CHECK(button->wyznacz_biezacy_styl_stanu().tlo==motyw_kolor(MOTYW_KOLOR_PRZYCISK));
   retained.close();
  }
 }
 // Exercise invalidation through the actual window and both scroll views.
 {
  directory="[PLIK] grafit.motyw\n[PLIK] baltyckanoc.motyw\n";
  Okno window("Ustawienia",950,540);auto& view=window.dodaj<WidokPrzewijany>();
  auto& inner=view.dodaj<SettingsUI::ThemesViewport>();auto& page=*inner.page;page.load();view.ustaw_rozmiar_zawartosci({950,512});
  window.przelicz_uklad();inner.rozmiesc({12,12,900,470});KontekstRysowania ctx;window.rysuj(ctx,true);
  page.start_preview();CHECK(page.shown_seconds==15);CHECK(!strcmp(page.status->tekst(),"Podgląd aktywny: 15 s"));window.rysuj(ctx);
  uint64_t started=test_monotonic_ns;
  for(unsigned second=1;second<15;++second){
   test_monotonic_ns=started+second*1000000000ULL;page.krok_animacji(test_monotonic_ns);
   CHECK(page.shown_seconds==15-second);
   char expected[64];snprintf(expected,sizeof(expected),"Podgląd aktywny: %u s",15-second);CHECK(!strcmp(page.status->tekst(),expected));
   auto dirty=view.pobierz_brudny_obszar();CHECK(dirty==page.status->obszar_ekranu());
   auto full=window.liczba_pelnych_odrysowan();window.rysuj(ctx);CHECK(window.liczba_pelnych_odrysowan()==full);CHECK(window.ostatni_obszar_odrysowania()==dirty);CHECK(!window.czy_wymaga_odrysowania());
   page.krok_animacji(test_monotonic_ns+500000000ULL);CHECK(!window.czy_wymaga_odrysowania());
  }
  test_monotonic_ns=started+15000000000ULL;page.krok_animacji(test_monotonic_ns);
  CHECK(!page.transaction.active());CHECK(page.shown_seconds==0);CHECK(!strstr(page.status->tekst(),"aktywny:"));window.rysuj(ctx);
  page.start_preview();test_monotonic_ns+=3000000000ULL;page.krok_animacji(test_monotonic_ns);CHECK(page.shown_seconds==12);
  page.select(1);CHECK(page.shown_seconds==15);CHECK(!strcmp(page.status->tekst(),"Podgląd aktywny: 15 s"));
  page.apply_selected();CHECK(!page.transaction.active());CHECK(page.shown_seconds==0);CHECK(!strcmp(page.status->tekst(),"Zapisano motyw."));window.rysuj(ctx);
  page.krok_animacji(test_monotonic_ns+30000000000ULL);CHECK(!window.czy_wymaga_odrysowania());
  page.start_preview();page.cancel_preview();CHECK(page.shown_seconds==0);CHECK(!page.transaction.active());CHECK(!strstr(page.status->tekst(),"aktywny:"));
  page.start_preview();test_monotonic_ns+=15000000000ULL;
  page.select(0);CHECK(!page.transaction.active());CHECK(page.shown_seconds==0);
  CHECK(!strstr(page.status->tekst(),"aktywny:"));
 }
 for(const char* id:{"bursztynnocny","baltyckanoc","grafit","bursztynklasyczny","bursztynjasny"}){
 CHECK(motyw_ustaw_z_pliku(id,false));Motyw::pobierz_domyslny().zaktualizuj_z_systemu();
 for(unsigned scale:{100U,125U,150U,200U})for(auto size:{Rozmiar(1280,680),Rozmiar(1920,1040)}){
 ui_skala_procent=scale;
 StartMenu::Surface s;s.open({0,0,size.szerokosc,size.wysokosc},10);auto outer=s.obszar();auto bottom=s.shutdown_bounds();
 CHECK(bottom.szerokosc==bottom.wysokosc);CHECK(bottom.szerokosc==36*ui_skala());
 CHECK(outer.prawy()-bottom.prawy()==8*ui_skala());CHECK(outer.dolny()-bottom.dolny()==8*ui_skala());
 auto* icon=static_cast<PrzyciskObrazkowy*>(s.dziecko(2));CHECK(!icon->tekst()[0]);CHECK(icon->ikona_id()==102);CHECK(icon->tryb()==TrybPrzyciskuObrazkowego::TylkoIkona);
 CHECK(s.power_menu().liczba_elementow()==4);CHECK(!s.power_menu().element(0)->czy_wlaczony());CHECK(!s.power_menu().element(1)->czy_wlaczony());
 for(const char* q:{"xyz","usta",""}){s.model.filter(q);s.rozmiesc(outer);CHECK(s.obszar()==outer);CHECK(s.shutdown_bounds()==bottom);
 CHECK(s.empty_message_visible()==(s.model.count==0));
 if(!s.model.count){auto empty=s.empty_message_bounds(),results=s.result_bounds();CHECK(empty.x==results.x+8*ui_skala());CHECK(empty.y>=results.y&&empty.y<results.y+s.row_height);CHECK(empty.dolny()<=results.dolny());}
 }
 key(s,9);key(s,32);CHECK(s.power_open());auto popup=s.power_popup_bounds();CHECK(popup.y>=0);CHECK(bottom.y-popup.dolny()==10*ui_skala());CHECK(popup.prawy()==bottom.prawy());CHECK(popup.dolny()<=size.wysokosc);CHECK(popup.prawy()<=size.szerokosc);CHECK(s.obszar()==outer);
 key(s,0x48);key(s,13);CHECK(s.take_action()==18);CHECK(!s.czy_otwarta());
 s.open({0,0,size.szerokosc,size.wysokosc},10);key(s,9);key(s,13);key(s,27);CHECK(!s.power_open());CHECK(s.czy_otwarta());
 key(s,13);CHECK(s.power_open());s.refresh();CHECK(s.power_open());
 key(s,27);CHECK(!s.power_open());
 event(s,BWS_ZDARZENIE_MYSZ_DOWN,bottom.x+bottom.szerokosc/2,bottom.y+bottom.wysokosc/2);
 event(s,BWS_ZDARZENIE_MYSZ_UP,bottom.x+bottom.szerokosc/2,bottom.y+bottom.wysokosc/2);
 CHECK(s.power_open());
 auto old_pixels=test_popup_pixels;
 CHECK(motyw_ustaw_z_pliku(!strcmp(id,"grafit")?"bursztynjasny":"grafit",false));Motyw::pobierz_domyslny().zaktualizuj_z_systemu();s.refresh();CHECK(s.power_open());CHECK(test_popup_pixels!=old_pixels);
 CHECK(motyw_ustaw_z_pliku(id,false));Motyw::pobierz_domyslny().zaktualizuj_z_systemu();s.refresh();CHECK(test_popup_pixels==old_pixels);key(s,27);
 auto power=s.shutdown_bounds();event(s,BWS_ZDARZENIE_MYSZ_RUCH,power.x+5,power.y+5);
 test_monotonic_ns+=299000000ULL;event(s,BWS_ZDARZENIE_TIMER);CHECK(!s.power_open());
 test_monotonic_ns+=1000000ULL;event(s,BWS_ZDARZENIE_TIMER);CHECK(s.power_open());
 popup=s.power_popup_bounds();
 event(s,BWS_ZDARZENIE_MYSZ_RUCH,power.x+power.szerokosc/2,power.y-4*ui_skala());
 test_monotonic_ns+=349000000ULL;event(s,BWS_ZDARZENIE_TIMER);CHECK(s.power_open());
 event(s,BWS_ZDARZENIE_MYSZ_RUCH,popup.x+10,popup.y+10);test_monotonic_ns+=1000000000ULL;event(s,BWS_ZDARZENIE_TIMER);CHECK(s.power_open());
 // Disabled lock accepts no action, including pointer release.
 event(s,BWS_ZDARZENIE_MYSZ_DOWN,popup.x+10,popup.y+10);event(s,BWS_ZDARZENIE_MYSZ_UP,popup.x+10,popup.y+10);CHECK(s.take_action()==-1);
 event(s,BWS_ZDARZENIE_MYSZ_RUCH,1200,10);test_monotonic_ns+=349000000ULL;event(s,BWS_ZDARZENIE_TIMER);CHECK(s.power_open());test_monotonic_ns+=1000000ULL;event(s,BWS_ZDARZENIE_TIMER);CHECK(!s.power_open());
 s.open_power();event(s,BWS_ZDARZENIE_MYSZ_DOWN,size.szerokosc-1,0);CHECK(!s.power_open());CHECK(!s.czy_otwarta());
 s.open({0,0,size.szerokosc,size.wysokosc},10);s.open_power();s.close();CHECK(!s.power_open());CHECK(!s.czy_otwarta());
 }
 }
 // Bound the transient independently of Start, including nonzero work origin.
 ui_skala_procent=100;
 {
  StartMenu::Surface s;s.open({400,31,160,680},410);auto start=s.obszar();s.open_power();auto popup=s.power_popup_bounds(),button=s.shutdown_bounds();
  CHECK(popup.szerokosc>=150&&popup.szerokosc<=152);CHECK(popup.x>=404);CHECK(popup.prawy()<=556);CHECK(!popup.przecina(button));CHECK(s.obszar()==start);
  auto* view=static_cast<WidokMenuPopup*>(s.events.popups.fokus_popupu());
  auto* item=const_cast<Menu&>(s.power_menu()).znajdz(18);item->ustaw_tekst("Uruchom ponownie — bardzo długa nazwa Łódź źółć");
  CHECK(view->wymagany_rozmiar().szerokosc>popup.szerokosc);s.refresh();CHECK(s.power_popup_bounds().szerokosc==152);CHECK(s.obszar()==start);s.close();
 }
 printf("Settings themes and Start power: %u checks PASS\n",checks);
}
