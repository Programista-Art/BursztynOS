// BSF 2.1 BursztynUI single-line field (production PoleTekstowe inside the
// production Notepad binary + BSF runtime with real fonts): visual RTL
// navigation, Shift selection, multiple visual highlight spans, Home/End,
// mouse hit testing and drag selection, horizontal scrolling, grapheme
// edits, logical clipboard, explicit direction, password isolation, plain
// LTR geometry unchanged, font catalog refresh. ASan/UBSan.
#define BSF_NATIVE 1
#define _start notepad_test_entry
#define __dso_handle notepad_test_dso_handle
#include "../programy/notatnik/notatnik.cpp"
#undef _start
#undef __dso_handle
#include "../testy/bursztyn_ui_host_fixture.h"
#include <chrono>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);abort();}}while(0)
static std::map<std::string,std::string> files;
static uint64_t io(uint64_t nr,uint64_t path,uint64_t dest,uint64_t,uint64_t){
 if(nr==6){const char* list="[P] DejaVuSans.ttf\n[P] DejaVuSans-Bold.ttf\n[P] DejaVuSansMono.ttf\n";memcpy((void*)dest,list,strlen(list)+1);return 1;}
 if(nr==4){auto f=files.find((const char*)path);if(f==files.end())return 0;*(uint32_t*)dest=f->second.size();return 1;}return 0;
}
bool czytaj_plik(const char* path,char* out,uint32_t capacity){auto f=files.find(path);if(f==files.end()||f->second.size()>capacity)return false;memcpy(out,f->second.data(),f->second.size());return true;}
bool utworz(const char* path){return files.emplace(path,std::string()).second;}
bool zapisz_plik(const char* path,const char* text,uint32_t n){if(!files.count(path))return false;files[path]=std::string(text,n);return true;}
void wypisz(const char*){}

static void key(PoleTekstowe& f,unsigned code,uint32_t mods=0){ZdarzenieKlawiatury e{};e.kod_klawisza=code;e.modyfikatory=mods;f.obsluz_wcisniecie_klawisza(e);CHECK(e.obsluzone);}
static void type(PoleTekstowe& f,uint32_t cp){ZdarzenieWprowadzaniaTekstu e{};e.punkt_kodowy=cp;f.obsluz_wprowadzenie_tekstu(e);}
static std::string text(PoleTekstowe& f){return std::string(f.tekst());}
// Walk with one key from one end; x strictly monotone, no revisits,
// stops at the visual edge, covers every distinct caret x.
static size_t walk(PoleTekstowe& f,unsigned code){
 std::set<int> xs;std::set<size_t> bytes;int x=f.x_kursora();xs.insert(x);bytes.insert(f.pozycja_kursora());size_t steps=0;
 for(;;){size_t before=f.pozycja_kursora();key(f,code);int nx=f.x_kursora();
  if(f.pozycja_kursora()==before){CHECK(nx==x);break;}
  CHECK(code==0x4d?nx>x:nx<x);CHECK(!xs.count(nx));xs.insert(nx);bytes.insert(f.pozycja_kursora());x=nx;CHECK(++steps<300);}
 std::string s=text(f);size_t keep=f.pozycja_kursora();
 for(size_t b=0;;b=BSF::next_cluster(s.data(),b,s.size())){f.ustaw_pozycje_kursora(b);CHECK(xs.count(f.x_kursora()));if(b>=s.size())break;}
 f.ustaw_pozycje_kursora(keep);
 return steps;
}
int main(){
 g_test_bws_hook=io;
 for(const char* name:{"DejaVuSans.ttf","DejaVuSans-Bold.ttf","DejaVuSansMono.ttf"}){std::ifstream input(std::string("czcionki/ttf/")+name,std::ios::binary);files[std::string("/czcionki/ttf/")+name]=std::string(std::istreambuf_iterator<char>(input),{});}
 CHECK(BSF::ready());
 PoleTekstowe f("");f.rozmiesc({0,0,320,32});
 const int pad=f.wypelnienie().lewy;
 // ---- plain LTR keeps the BSF 2.0 geometry (fast path, no layout) ------
 f.ustaw_tekst("Zażółć gęślą jaźń");
 CHECK(!f.czy_uklad_zlozony());
 for(size_t b=0;b<=strlen(f.tekst());b=b==strlen(f.tekst())?b+1:BSF::next_cluster(f.tekst(),b,strlen(f.tekst())))
  {f.ustaw_pozycje_kursora(b);CHECK(f.x_kursora()==CzcionkaUI::pozycja_x_z_bajtu(f.tekst(),b));}
 // ---- RTL value: right aligned, visual Left/Right ----------------------
 std::string he="שלום עולם";
 f.ustaw_tekst(he.c_str());CHECK(f.czy_uklad_zlozony()&&f.poziom_bazowy()==1);
 f.ustaw_pozycje_kursora(0);int x0=f.x_kursora();
 CHECK(x0>=f.obszar().szerokosc-2*pad-24); // logical start at the right edge
 f.ustaw_pozycje_kursora(he.size());CHECK(f.x_kursora()<x0);
 // Left in RTL = forward (logical increase), visually leftwards.
 f.ustaw_pozycje_kursora(0);key(f,0x4b);CHECK(f.pozycja_kursora()==BSF::next_cluster(he.data(),0,he.size()));
 f.ustaw_pozycje_kursora(0);key(f,0x4d);CHECK(f.pozycja_kursora()==0); // right edge: stays
 f.ustaw_pozycje_kursora(0);size_t a=walk(f,0x4b);size_t b=walk(f,0x4d);CHECK(a==b&&f.pozycja_kursora()==0);
 // Mixed value: Arabic, Latin, digits, ligature, marks, isolate.
 std::string mixed="abc مرحبا لا 123 שָׁלוֹם \xe2\x81\xa7xyz\xe2\x81\xa9 def";
 f.ustaw_tekst(mixed.c_str());CHECK(f.czy_uklad_zlozony()&&f.poziom_bazowy()==0);
 f.ustaw_pozycje_kursora(0);a=walk(f,0x4d);b=walk(f,0x4b);CHECK(a==b);
 // ---- Shift selection + multiple visual spans --------------------------
 std::string sel="abc שלום def";f.ustaw_tekst(sel.c_str());
 f.ustaw_zaznaczenie(2,8);int s0[8],s1[8];size_t ns=f.spany_zaznaczenia(s0,s1,8);
 CHECK(ns==2&&(s1[0]<=s0[1]||s1[1]<=s0[0]));
 f.ustaw_pozycje_kursora(0);key(f,0x4d,BWS_MOD_SHIFT);key(f,0x4d,BWS_MOD_SHIFT);key(f,0x4d,BWS_MOD_SHIFT);key(f,0x4d,BWS_MOD_SHIFT);key(f,0x4d,BWS_MOD_SHIFT);
 CHECK(f.czy_ma_zaznaczenie()&&f.poczatek_zaznaczenia()==0);
 // Without Shift, Right collapses to the visually right side.
 f.ustaw_zaznaczenie(0,sel.size());key(f,0x4d);CHECK(f.pozycja_kursora()==sel.size()&&!f.czy_ma_zaznaczenie());
 f.ustaw_tekst(he.c_str());f.ustaw_zaznaczenie(0,he.size());key(f,0x4d);CHECK(f.pozycja_kursora()==0); // RTL: right = logical start
 // ---- Home/End are logical ---------------------------------------------
 f.ustaw_tekst(mixed.c_str());f.ustaw_pozycje_kursora(7);key(f,0x47);CHECK(f.pozycja_kursora()==0);key(f,0x4f);CHECK(f.pozycja_kursora()==mixed.size());
 key(f,0x47,BWS_MOD_SHIFT);CHECK(f.poczatek_zaznaczenia()==0&&f.koniec_zaznaczenia()==mixed.size());
 // ---- mouse: clicking at a caret x selects that visual position -------
 for(size_t p=0;p<=mixed.size();p=p==mixed.size()?p+1:BSF::next_cluster(mixed.data(),p,mixed.size())){
  f.ustaw_pozycje_kursora(p);int x=f.x_kursora();
  ZdarzenieMyszki e{};e.x=pad+x-f.przewiniecie();e.y=10;e.przycisk=1;f.obsluz_wcisniecie_myszy(e);ZdarzenieMyszki u=e;f.obsluz_zwolnienie_myszy(u);
  CHECK(f.x_kursora()==x);
 }
 // Drag selection between two clicks gives the logical range of the hit offsets.
 {ZdarzenieMyszki d{};d.x=pad+1;d.y=10;d.przycisk=1;f.obsluz_wcisniecie_myszy(d);ZdarzenieMyszki m{};m.x=pad+80;m.y=10;m.przycisk=1;f.obsluz_ruch_myszy(m);ZdarzenieMyszki u=m;f.obsluz_zwolnienie_myszy(u);
  CHECK(f.czy_ma_zaznaczenie());CHECK(BSF::cluster_boundary(f.tekst(),strlen(f.tekst()),f.poczatek_zaznaczenia())&&BSF::cluster_boundary(f.tekst(),strlen(f.tekst()),f.koniec_zaznaczenia()));}
 // ---- grapheme edits and logical clipboard -----------------------------
 f.ustaw_tekst("שָׁלוֹם");f.ustaw_pozycje_kursora(strlen(f.tekst()));key(f,8);
 CHECK(text(f)==std::string("שָׁלוֹם").substr(0,BSF::previous_cluster("שָׁלוֹם",strlen("שָׁלוֹם"))));
 f.ustaw_tekst(mixed.c_str());f.ustaw_pozycje_kursora(0);key(f,0x53);CHECK(text(f)==mixed.substr(1));
 f.ustaw_tekst(mixed.c_str());f.zaznacz_wszystko();CHECK(f.kopiuj_do_schowka());CHECK(std::string(g_mock_clipboard)==mixed);
 f.ustaw_tekst("");CHECK(f.wklej_ze_schowka());CHECK(text(f)==mixed);
 f.ustaw_zaznaczenie(4,14);CHECK(f.wytnij_do_schowka());CHECK(std::string(g_mock_clipboard)==mixed.substr(4,10));CHECK(text(f)==mixed.substr(0,4)+mixed.substr(14));
 // typing Arabic letters keeps logical order
 f.ustaw_tekst("");for(uint32_t cp:{0x0645u,0x0631u,0x062Du,0x0628u,0x0627u})type(f,cp);CHECK(text(f)=="مرحبا");
 // ---- horizontal scrolling keeps the caret visible --------------------
 PoleTekstowe narrow("");narrow.rozmiesc({0,0,90,32});
 std::string longrtl="שלום עולם זה טקסט ארוך מאוד בעברית ومعه عربية أيضاً";
 narrow.ustaw_tekst(longrtl.c_str());int kw=narrow.obszar().szerokosc-2*pad;
 for(unsigned code:{0x4bu,0x4du})for(int i=0;i<80;++i){key(narrow,code);int vx=narrow.x_kursora()-narrow.przewiniecie();CHECK(vx>=0&&vx<=kw);}
 // ---- explicit direction -----------------------------------------------
 f.ustaw_tekst("abc!");f.ustaw_kierunek_tekstu(KierunekTekstu::OdPrawej);CHECK(f.czy_uklad_zlozony()&&f.poziom_bazowy()==1);
 f.ustaw_pozycje_kursora(0);CHECK(f.x_kursora()>40); // right aligned
 f.ustaw_tekst(he.c_str());f.ustaw_kierunek_tekstu(KierunekTekstu::OdLewej);CHECK(f.poziom_bazowy()==0);
 // LTR base: the Hebrew run is left aligned, its logical end at x=0.
 f.ustaw_pozycje_kursora(he.size());CHECK(f.x_kursora()<8);
 f.ustaw_kierunek_tekstu(KierunekTekstu::Automatyczny);CHECK(text(f)==he);
 // ---- password fields never lay out the real value --------------------
 PoleTekstowe pw("");pw.rozmiesc({0,0,200,32});pw.ustaw_tryb_hasla(true);pw.ustaw_tekst("סיסמה123");
 auto before=BSF::text_statistics().owned_builds;CHECK(!pw.czy_uklad_zlozony());pw.ustaw_pozycje_kursora(2);(void)pw.x_kursora();
 CHECK(BSF::text_statistics().owned_builds==before);
 // ---- font catalog refresh: rebuild, no stale layout -------------------
 f.ustaw_tekst(mixed.c_str());f.ustaw_pozycje_kursora(5);int xr=f.x_kursora();
 BSF::rescan();CHECK(BSF::ready());CHECK(f.x_kursora()==xr);auto st=BSF::text_statistics();(void)st;
 // ---- labels: complex text through the shared engine; LTR unchanged ----
 CHECK(CzcionkaUI::zmierz_tekst("Polski tekst")==BSF::measure_text("Polski tekst",ui_skaluj(BSF::ui_size(),100)));
 printf("BursztynUI BSF 2.1 single-line field: %u checks PASS (visual RTL navigation, multi-span selection, hit testing, scrolling, clipboard, direction API, password isolation)\n",checks);
 return 0;
}
