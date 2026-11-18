#define BSF_NATIVE 1
#define _start notepad_test_entry
#define __dso_handle notepad_test_dso_handle
#include "../programy/notatnik/notatnik.cpp"
#undef _start
#undef __dso_handle
#include "../testy/bursztyn_ui_host_fixture.h"
#include <string>
#include <memory>
#include <chrono>
#include <fstream>
#include <iterator>
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
static void key(PoleWielowierszowe& field,unsigned code){ZdarzenieKlawiatury e{};e.kod_klawisza=code;field.obsluz_wcisniecie_klawisza(e);CHECK(e.obsluzone);}
int main(){
 g_test_bws_hook=io;
 for(const char* name:{"DejaVuSans.ttf","DejaVuSans-Bold.ttf","DejaVuSansMono.ttf"}){std::ifstream input(std::string("czcionki/ttf/")+name,std::ios::binary);files[std::string("/czcionki/ttf/")+name]=std::string(std::istreambuf_iterator<char>(input),{});}
 CHECK(BSF::ready());
 auto field=std::make_unique<PoleWielowierszowe>("",65536);g_notatnik.memo=field.get();field->rozmiesc({0,0,800,600});
 const char* samples[]={"", "Polski Zażółć gęślą jaźń. English Hello world.\nРусский Привет мир. Українська Привіт світ.\nΕλληνικά Καλημέρα κόσμε. ἀᾀῷ\n", "a\xcc\x88\xcc\x81x", "\xf0\x9f\x87\xb5\xf0\x9f\x87\xb1\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb", "before\r\nafter\r\n", "\xff\xc0\xaf\xe2\x82x"};
 for(const char* sample:samples){
  std::string original(sample);files["/input.txt"]=original;
  CHECK(g_notatnik.otworz_plik("/input.txt"));CHECK(std::string(field->tekst(),field->dlugosc())==original);
  CHECK(g_notatnik.zapisz_plik_jako("/saved.txt"));CHECK(files["/saved.txt"]==original);
  field->ustaw_zaznaczenie(0,0);size_t at=0;
  while(at<original.size()){key(*field,0x4d);at=BSF::next_cluster(original.data(),at,original.size());CHECK(field->pozycja_kursora()==at);}
  while(at){key(*field,0x4b);at=BSF::previous_cluster(original.data(),at);CHECK(field->pozycja_kursora()==at);}
  field->ustaw_zaznaczenie(original.size(),original.size());key(*field,8);
  size_t end=BSF::previous_cluster(original.data(),original.size());CHECK(std::string(field->tekst())==original.substr(0,end));
  field->ustaw_tekst(sample);field->ustaw_zaznaczenie(0,0);key(*field,0x53);
  size_t first=BSF::next_cluster(original.data(),0,original.size());CHECK(std::string(field->tekst())==original.substr(first));
  field->ustaw_tekst(sample);field->zaznacz_wszystko();field->kopiuj_do_schowka();
  CHECK(std::string(g_mock_clipboard)==original);
  field->ustaw_tekst("");field->wklej_ze_schowka();CHECK(std::string(field->tekst())==original);
  CHECK(g_notatnik.zapisz_plik_jako("/edited.txt"));CHECK(g_notatnik.otworz_plik("/edited.txt"));CHECK(std::string(field->tekst())==original);
 }
 {
 std::string value="Polski Zażółć gęślą jaźń. English Hello world. Русский Привет мир. Українська Привіт світ. Ελληνικά Καλημέρα κόσμε. ἀᾀῷ 😀 | ä́";
 field->ustaw_tekst(value.c_str());field->ustaw_zaznaczenie(value.size(),value.size());key(*field,0x4b);CHECK(field->pozycja_kursora()==value.size()-5);
 key(*field,0x53);CHECK(std::string(field->tekst())==value.substr(0,value.size()-5));
 ZdarzenieWprowadzaniaTekstu input{};input.punkt_kodowy='z';field->obsluz_wprowadzenie_tekstu(input);CHECK(std::string(field->tekst())==value.substr(0,value.size()-5)+"z");
 }
 field->ustaw_tekst("\xcc\x81");field->ustaw_zaznaczenie(0,0);field->dopisz_tekst("A");CHECK(BSF::cluster_boundary(field->tekst(),field->dlugosc(),field->pozycja_kursora()));
 std::string large(60000,'a');field->ustaw_zawijanie_wierszy(false);field->ustaw_tekst(large.c_str());field->ustaw_zaznaczenie(large.size(),large.size());
 auto begin=std::chrono::steady_clock::now();for(unsigned i=0;i<60000;++i)key(*field,0x4b);
 auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();CHECK(field->pozycja_kursora()==0);
 CHECK(g_notatnik.zapisz_plik_jako("/large.txt"));CHECK(files["/large.txt"]==large);
 files["/too-large.txt"]=std::string(65536,'a');CHECK(!g_notatnik.otworz_plik("/too-large.txt"));
 files["/nul.txt"]=std::string("a\0b",3);CHECK(!g_notatnik.otworz_plik("/nul.txt"));
 g_notatnik.memo=nullptr;BSF::shutdown();
 printf("Notepad production edit/save/reopen: %u checks PASS; 60000 backward keys ns=%lld; index bytes=%zu\n",checks,(long long)ns,sizeof(BSF::BoundaryIndex<>));
}
