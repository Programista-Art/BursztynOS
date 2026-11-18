// BSF 2.1 Notepad: paragraph-level bidi on the production Notepad
// (programy/notatnik + PoleWielowierszowe + BSF runtime with real fonts).
// Paragraph direction across wrapped lines, explicit direction, alignment,
// visual caret navigation (no oscillation, every position once), Home/End,
// Up/Down preferred x, mouse hit testing, multi-span selection, clipboard,
// save/reopen and undo byte integrity, malformed UTF-8, long-paragraph
// latency. ASan/UBSan.
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
bool usun_twor_uzytkownika(const char* path){return files.erase(path)==1;}
uint32_t gui_okno_podrzedne_otworz(uint32_t,const char*,const char*,uint32_t){return 1;}
void wypisz(const char*){}
static void key(PoleWielowierszowe& f,unsigned code,uint32_t mods=0){ZdarzenieKlawiatury e{};e.kod_klawisza=code;e.modyfikatory=mods;f.obsluz_wcisniecie_klawisza(e);CHECK(e.obsluzone);}
static void click(PoleWielowierszowe& f,int x,int y,uint32_t mods=0){ZdarzenieMyszki e{};e.x=x;e.y=y;e.przycisk=1;e.modyfikatory=mods;f.obsluz_wcisniecie_myszy(e);ZdarzenieMyszki u=e;f.obsluz_zwolnienie_myszy(u);}
static void drag(PoleWielowierszowe& f,int x0,int y0,int x1,int y1){ZdarzenieMyszki e{};e.x=x0;e.y=y0;e.przycisk=1;f.obsluz_wcisniecie_myszy(e);ZdarzenieMyszki m{};m.x=x1;m.y=y1;m.przycisk=1;f.obsluz_ruch_myszy(m);ZdarzenieMyszki u=m;f.obsluz_zwolnienie_myszy(u);}
static std::string text(PoleWielowierszowe& f){return std::string(f.tekst(),f.dlugosc());}
// (byte, visual line, x) of the caret
struct Pos{size_t b,l;int x;bool operator<(const Pos&o)const{return b!=o.b?b<o.b:l<o.l;}};
static Pos pos(PoleWielowierszowe& f){return {f.pozycja_kursora(),f.linia_wizualna_kursora(),f.x_kursora()};}
// Walk the whole document with one arrow key from one end: visual x is
// strictly monotone on each line, the caret never revisits a (byte,line),
// all grapheme boundaries are visited, and the walk ends at the far end.
static size_t walk(PoleWielowierszowe& f,unsigned code,const std::string& s,bool all=true){
 std::set<Pos> seen;std::set<size_t> bytes;Pos p=pos(f);seen.insert(p);bytes.insert(p.b);size_t steps=0;
 for(;;){key(f,code);Pos q=pos(f);
  if(q.b==p.b&&q.l==p.l)break; // document edge: no movement
  if(q.l==p.l)CHECK(code==0x4d?q.x>p.x:q.x<p.x);
  if(seen.count(q))fprintf(stderr,"revisit code=%x p=(%zu,%zu,%d) q=(%zu,%zu,%d) steps=%zu text=%s\n",code,p.b,p.l,p.x,q.b,q.l,q.x,steps,s.c_str());
  CHECK(!seen.count(q));seen.insert(q);bytes.insert(q.b);p=q;CHECK(++steps<=4*s.size()+8);}
 // Coverage: every distinct visual caret position (line, x) of every
 // grapheme boundary is visited. Two logical offsets that share one visual
 // position (e.g. an RTL space next to an LTR number) are shown once.
 if(all){std::set<std::pair<size_t,int>> visited;for(auto& v:seen)visited.insert({v.l,v.x});
  size_t keep=f.pozycja_kursora();
  for(size_t b=0;;b=BSF::next_cluster(s.data(),b,s.size())){f.ustaw_zaznaczenie(b,b);Pos e=pos(f);
   if(!visited.count({e.l,e.x}))fprintf(stderr,"missing b=%zu (l=%zu x=%d) code=%x text=%s\n",b,e.l,e.x,code,s.c_str());
   CHECK(visited.count({e.l,e.x}));if(b>=s.size())break;}
  f.ustaw_zaznaczenie(keep,keep);}
 return steps;
}
int main(){
 g_test_bws_hook=io;
 for(const char* name:{"DejaVuSans.ttf","DejaVuSans-Bold.ttf","DejaVuSansMono.ttf"}){std::ifstream input(std::string("czcionki/ttf/")+name,std::ios::binary);files[std::string("/czcionki/ttf/")+name]=std::string(std::istreambuf_iterator<char>(input),{});}
 CHECK(BSF::ready());
 auto field=std::make_unique<PoleWielowierszowe>("",65536);g_notatnik.memo=field.get();field->rozmiesc({0,0,420,400});
 PoleWielowierszowe& f=*field;
 const Prostokat view=f.prostokat_widoku_tekstu();const int lh=f.wysokosc_jednej_linii();
 // ---- A1/A2: one direction per logical paragraph, right alignment -------
 std::string rtl_para="שלום לכולם, זהו טקסט עברי ארוך מאוד שנועד לבדוק גלישה של שורות Hello world from Polska 2026 ועוד מילים בעברית כדי לגלוש שוב ושוב בסוף.";
 f.ustaw_tekst(rtl_para.c_str());
 bool english_start=false;
 // Find a view width at which a continuation line starts with Latin text.
 for(int width=420;width>=140&&!english_start;width-=4){
  f.rozmiesc({0,0,width,400});
  CHECK(f.liczba_linii_wizualnych()>=2);
  for(size_t li=0;li<f.liczba_linii_wizualnych();++li){
   CHECK(f.linia_zlozona(li));CHECK(f.poziom_linii(li)==1);
   size_t a=f.poczatek_linii_wizualnej(li);
   if(li&&(rtl_para.compare(a,5,"Hello")==0||rtl_para.compare(a,5,"world")==0||rtl_para.compare(a,4,"from")==0||rtl_para.compare(a,6,"Polska")==0))english_start=true;
   CHECK(f.przesuniecie_linii(li)>=0&&f.przesuniecie_linii(li)<=f.prostokat_widoku_tekstu().szerokosc);
  }
 }
 CHECK(english_start); // a continuation line starts with Latin yet stays RTL
 const Prostokat wview=f.prostokat_widoku_tekstu();
 // Right alignment of every visual line of the RTL paragraph.
 for(size_t li=0;li<f.liczba_linii_wizualnych();++li){
  f.ustaw_zaznaczenie(f.poczatek_linii_wizualnej(li),f.poczatek_linii_wizualnej(li));
  CHECK(f.linia_wizualna_kursora()==li);
  // A line whose logical start is Hebrew has it at the right edge; a line
  // starting with an LTR run places that run first (rightmost), with its
  // logical start at the run's left edge.
  if(uint8_t(rtl_para[f.poczatek_linii_wizualnej(li)])==0xD7)CHECK(f.x_kursora()>=wview.szerokosc-24);
  else CHECK(f.x_kursora()>f.przesuniecie_linii(li));
 }
 f.rozmiesc({0,0,420,400});
 // Mixed LTR paragraph stays left aligned; a following RTL paragraph is
 // right aligned (direction is per logical paragraph, not per document).
 std::string doc="Polski tekst z liczbą 123 i słowem שלום.\nمرحبا بالعالم 2026، Hello!\r\nEnglish again.";
 f.ustaw_tekst(doc.c_str());
 CHECK(f.liczba_wierszy()==3);
 CHECK(f.poziom_linii(0)==0&&f.przesuniecie_linii(0)==0);
 CHECK(f.poziom_linii(1)==1&&f.przesuniecie_linii(1)>0);
 CHECK(f.poziom_linii(2)==0&&!f.linia_zlozona(2)); // plain LTR keeps the legacy fast path
 // ---- explicit direction overrides detection -------------------------
 f.ustaw_kierunek_tekstu(KierunekTekstu::OdPrawej);
 for(size_t li=0;li<f.liczba_linii_wizualnych();++li)CHECK(f.poziom_linii(li)==1&&f.linia_zlozona(li));
 f.ustaw_kierunek_tekstu(KierunekTekstu::OdLewej);
 for(size_t li=0;li<f.liczba_linii_wizualnych();++li)CHECK(f.poziom_linii(li)==0&&f.przesuniecie_linii(li)==0);
 f.ustaw_kierunek_tekstu(KierunekTekstu::Automatyczny);
 CHECK(text(f)==doc); // direction never touches the logical bytes
 // ---- A3/A4: visual navigation across mixed, wrapped paragraphs ------
 std::string nav="abc שלום 123 עולם def\nمرحبا بالعالم لا Polska 12 وداعا\nשורה עם ניקוד: שָׁלוֹם וְעוֹד e\xcc\x81 \xe2\x81\xa7abc\xe2\x81\xa9 סוף";
 f.ustaw_tekst(nav.c_str());
 // Paragraphs of opposite direction: a held key terminates without
 // revisiting and enters the next paragraph forward (LTR -> RTL with Right).
 f.ustaw_zaznaczenie(0,0);walk(f,0x4d,nav,false);
 CHECK(f.linia_wizualna_kursora()>=1);
 // Left from the end of the RTL paragraph (forward in RTL) reaches the
 // third paragraph; Up/Down and the other key reach everything else.
 f.ustaw_zaznaczenie(nav.find('\n')+1,nav.find('\n')+1);walk(f,0x4b,nav,false);
 CHECK(f.linia_wizualna_kursora()>=2);
 // A single-direction mixed paragraph is fully covered by both keys.
 std::string one="abc שלום 123 עולם def (x) e\xcc\x81 \xe2\x81\xa7abc\xe2\x81\xa9 لا تمام";
 f.ustaw_tekst(one.c_str());f.ustaw_zaznaczenie(0,0);size_t right=walk(f,0x4d,one);
 // The line ends with an RTL run, so the rightmost caret is that run's
 // logical start, not the end of the text.
 CHECK(f.pozycja_kursora()==one.find("\xd9\x84\xd8\xa7"));size_t left=walk(f,0x4b,one);CHECK(f.pozycja_kursora()==0);
 CHECK(right==left);
 f.ustaw_tekst(nav.c_str());
 // Same walk on narrow, wrapped text (soft-wrap boundaries, both directions).
 f.rozmiesc({0,0,180,400});
 f.ustaw_tekst(rtl_para.c_str());CHECK(f.liczba_linii_wizualnych()>=6);
 f.ustaw_zaznaczenie(0,0);
 // In an RTL paragraph Left moves forward (reading order).
 size_t w1=walk(f,0x4b,rtl_para);CHECK(f.linia_wizualna_kursora()+1==f.liczba_linii_wizualnych());
 size_t w2=walk(f,0x4d,rtl_para);CHECK(f.pozycja_kursora()==0);CHECK(w1==w2);
 // Home/End on a wrapped RTL line: End keeps the caret on that line.
 for(size_t li=0;li+1<f.liczba_linii_wizualnych();++li){
  f.ustaw_zaznaczenie(f.poczatek_linii_wizualnej(li),f.poczatek_linii_wizualnej(li));CHECK(f.linia_wizualna_kursora()==li);
  key(f,0x4f);CHECK(f.pozycja_kursora()==f.koniec_linii_wizualnej(li));CHECK(f.linia_wizualna_kursora()==li);CHECK(f.afinicja_kursora_wstecz());
  CHECK(f.x_kursora()<=f.przesuniecie_linii(li)+2); // logical end of an RTL line is at its left edge
  key(f,0x47);CHECK(f.pozycja_kursora()==f.poczatek_linii_wizualnej(li)&&f.linia_wizualna_kursora()==li);
 }
 // Ctrl+Home/Ctrl+End.
 key(f,0x4f,BWS_MOD_CTRL);CHECK(f.pozycja_kursora()==rtl_para.size());key(f,0x47,BWS_MOD_CTRL);CHECK(f.pozycja_kursora()==0);
 // Up/Down keep the preferred visual x (within one glyph).
 f.ustaw_zaznaczenie(0,0);for(int i=0;i<6;++i)key(f,0x4b);int px=f.x_kursora();
 for(size_t i=1;i<f.liczba_linii_wizualnych();++i){key(f,0x50);CHECK(f.linia_wizualna_kursora()==i);CHECK(std::abs(f.x_kursora()-px)<=24);}
 for(size_t i=f.liczba_linii_wizualnych()-1;i-->0;){key(f,0x48);CHECK(f.linia_wizualna_kursora()==i);CHECK(std::abs(f.x_kursora()-px)<=24);}
 // Ctrl+Left/Right move by words without splitting graphemes.
 f.ustaw_tekst("שָׁלוֹם עוֹלָם abc");f.ustaw_zaznaczenie(0,0);
 key(f,0x4b,BWS_MOD_CTRL);CHECK(BSF::cluster_boundary(f.tekst(),f.dlugosc(),f.pozycja_kursora())&&f.pozycja_kursora()>0);
 key(f,0x4d,BWS_MOD_CTRL);CHECK(f.pozycja_kursora()==0);
 // ---- mouse hit testing equals caret geometry -------------------------
 f.rozmiesc({0,0,420,400});
 f.ustaw_tekst(nav.c_str());
 for(size_t b=0;b<=nav.size();b=b==nav.size()?b+1:BSF::next_cluster(nav.data(),b,nav.size())){
  f.ustaw_zaznaczenie(b,b);Pos p=pos(f);
  click(f,view.x+p.x,view.y+int(p.l)*lh+lh/2);
  Pos q=pos(f);CHECK(q.l==p.l);CHECK(q.x==p.x); // same visual position (offsets may differ only if they coincide)
 }
 // ---- A5: mixed-direction selection = several visual spans -----------
 std::string sel="abc שלום def";
 f.ustaw_tekst(sel.c_str());
 // logical "c שׁ" -> bytes [2, 6): 'c' ' ' 'ש' is split into two spans visually
 f.ustaw_zaznaczenie(2,8);int x0[8],x1[8];size_t ns=f.spany_zaznaczenia(0,x0,x1,8);
 CHECK(ns==2);
 for(size_t k=0;k<ns;++k)CHECK(x0[k]<x1[k]);
 CHECK(x1[0]<=x0[1]||x1[1]<=x0[0]); // disjoint
 // Selection across paragraphs and wrapped lines, then copy = logical bytes.
 f.ustaw_tekst(nav.c_str());f.ustaw_zaznaczenie(5,nav.size()-7);
 size_t spans=0;for(size_t li=0;li<f.liczba_linii_wizualnych();++li)spans+=f.spany_zaznaczenia(li,x0,x1,8);
 CHECK(spans>=f.liczba_linii_wizualnych()); // at least one span per selected line
 f.kopiuj_do_schowka();CHECK(f.poczatek_zaznaczenia()==4); // offsets snap to grapheme starts
 CHECK(std::string(g_mock_clipboard)==nav.substr(4,f.koniec_zaznaczenia()-4));
 // Shift+arrow extends the logical selection.
 f.ustaw_zaznaczenie(0,0);key(f,0x4d,BWS_MOD_SHIFT);key(f,0x4d,BWS_MOD_SHIFT);CHECK(f.czy_ma_zaznaczenie()&&f.poczatek_zaznaczenia()==0);
 // Mouse drag selection inside the RTL paragraph.
 f.ustaw_tekst(rtl_para.c_str());
 drag(f,view.x+view.szerokosc-4,view.y+lh/2,view.x+view.szerokosc/2,view.y+lh/2);
 CHECK(f.czy_ma_zaznaczenie());CHECK(f.poczatek_zaznaczenia()==0); // RTL: right edge = logical start
 // ---- A6: clipboard, cut/paste, save/reopen, undo/redo byte-exact -----
 const char* samples[]={ "שלום עולם\nمرحبا بالعالم لا\r\n123 abc \xe2\x81\xa6אבג\xe2\x81\xa9 \xe2\x80\x8f!",
  "שָׁלוֹם בְּעִבְרִית", "لا إله ﷲ ئۆ", "a\xcc\x88\xcc\x81 \xd7\x90\xd6\xb8", "\xff\xc0\xaf\xd7\x90\xe2\x82 \xd8", "\xe2\x80\xa9para\xe2\x80\xa9\xd7\x90"};
 for(const char* sample:samples){
  std::string original(sample);files["/bidi.txt"]=original;
  CHECK(g_notatnik.otworz_plik("/bidi.txt"));CHECK(text(f)==original);
  // walk both ways over every paragraph (incl. malformed bytes)
  f.ustaw_zaznaczenie(0,0);for(size_t i=0;i<4*original.size()+8;++i)key(f,0x4d);
  for(size_t i=0;i<4*original.size()+8;++i)key(f,0x4b);
  CHECK(text(f)==original);
  f.zaznacz_wszystko();f.kopiuj_do_schowka();CHECK(std::string(g_mock_clipboard)==original);
  f.wytnij_do_schowka();CHECK(text(f).empty());f.wklej_ze_schowka();CHECK(text(f)==original);
  CHECK(f.cofnij());CHECK(text(f).empty());CHECK(f.ponow());CHECK(text(f)==original);
  CHECK(g_notatnik.zapisz_plik_jako("/bidi-saved.txt"));CHECK(files["/bidi-saved.txt"]==original);
  CHECK(g_notatnik.otworz_plik("/bidi-saved.txt"));CHECK(text(f)==original);
  // Backspace from the end removes exactly one grapheme.
  f.ustaw_zaznaczenie(original.size(),original.size());key(f,8);
  CHECK(text(f)==original.substr(0,BSF::previous_cluster(original.data(),original.size())));
 }
 // ---- A7: long mixed-direction paragraph latency ----------------------
 std::string longp;while(longp.size()<20000)longp+="שלום עולם مرحبا Hello 123 ";
 f.ustaw_tekst(longp.c_str());f.ustaw_zaznaczenie(0,0);
 auto t0=std::chrono::steady_clock::now();for(int i=0;i<2000;++i)key(f,0x4b);
 auto t1=std::chrono::steady_clock::now();
 for(int i=0;i<100;++i){ZdarzenieWprowadzaniaTekstu in{};in.punkt_kodowy=0x05D0;f.obsluz_wprowadzenie_tekstu(in);}
 auto t2=std::chrono::steady_clock::now();
 auto caret_us=std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count()/2000;
 auto edit_us=std::chrono::duration_cast<std::chrono::microseconds>(t2-t1).count()/100;
 auto st=f.statystyki_ukladu();
 CHECK(st.trafienia>st.chybienia); // caret keys reuse the paragraph layout
 printf("Notepad BSF 2.1 bidi: %u checks PASS; long mixed paragraph %zu B: caret %lld us/key, insert %lld us/key; layout cache hits=%llu misses=%llu\n",
  checks,longp.size(),(long long)caret_us,(long long)edit_us,(unsigned long long)st.trafienia,(unsigned long long)st.chybienia);
 return 0;
}
