// Notepad editing latency benchmark (BSF 2.0 vs 2.1 comparison). Uses only
// the public API that exists in both versions; build without sanitizers:
//   make bench-notepad-bidi
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
#include <string>
static std::map<std::string,std::string> files;
static uint64_t io(uint64_t nr,uint64_t path,uint64_t dest,uint64_t,uint64_t){
 if(nr==6){const char* list="[P] DejaVuSans.ttf\n[P] DejaVuSans-Bold.ttf\n[P] DejaVuSansMono.ttf\n";memcpy((void*)dest,list,strlen(list)+1);return 1;}
 if(nr==4){auto f=files.find((const char*)path);if(f==files.end())return 0;*(uint32_t*)dest=f->second.size();return 1;}return 0;
}
bool czytaj_plik(const char* path,char* out,uint32_t capacity){auto f=files.find(path);if(f==files.end()||f->second.size()>capacity)return false;memcpy(out,f->second.data(),f->second.size());return true;}
bool utworz(const char* path){return files.emplace(path,std::string()).second;}
bool zapisz_plik(const char* path,const char* text,uint32_t n){if(!files.count(path))return false;files[path]=std::string(text,n);return true;}
void wypisz(const char*){}
static void key(PoleWielowierszowe& f,unsigned code){ZdarzenieKlawiatury e{};e.kod_klawisza=code;f.obsluz_wcisniecie_klawisza(e);}
static long long us(std::chrono::steady_clock::time_point a,std::chrono::steady_clock::time_point b){return std::chrono::duration_cast<std::chrono::microseconds>(b-a).count();}
int main(){
 g_test_bws_hook=io;
 for(const char* name:{"DejaVuSans.ttf","DejaVuSans-Bold.ttf","DejaVuSansMono.ttf"}){std::ifstream input(std::string("czcionki/ttf/")+name,std::ios::binary);files[std::string("/czcionki/ttf/")+name]=std::string(std::istreambuf_iterator<char>(input),{});}
 auto field=std::make_unique<PoleWielowierszowe>("",65536);g_notatnik.memo=field.get();field->rozmiesc({0,0,800,600});
 PoleWielowierszowe& f=*field;
 struct Case{const char* name;std::string text;};
 std::string mixed,ascii,polish;
 while(mixed.size()<20000)mixed+="שלום עולם مرحبا Hello 123 ";
 while(ascii.size()<20000)ascii+="The quick brown fox jumps over the lazy dog. ";
 while(polish.size()<20000)polish+="Zażółć gęślą jaźń, pchnąć w tę łódź jeża. ";
 std::string multi;for(int i=0;i<200;++i)multi+="שורה "+std::to_string(i)+" مرحبا Hello world\n";
 Case cases[]={{"ascii-20k",ascii},{"polish-20k",polish},{"mixed-20k",mixed},{"mixed-200-paragraphs",multi}};
 for(auto& c:cases){
  f.ustaw_tekst(c.text.c_str());f.ustaw_zaznaczenie(c.text.size()/2,c.text.size()/2);
  auto t0=std::chrono::steady_clock::now();for(int i=0;i<2000;++i)key(f,i%2?0x4b:0x4d);
  auto t1=std::chrono::steady_clock::now();for(int i=0;i<200;++i)key(f,0x50);for(int i=0;i<200;++i)key(f,0x48);
  auto t2=std::chrono::steady_clock::now();
  for(int i=0;i<50;++i){ZdarzenieWprowadzaniaTekstu in{};in.punkt_kodowy=c.name[0]=='m'?0x05D0:'x';f.obsluz_wprowadzenie_tekstu(in);}
  auto t3=std::chrono::steady_clock::now();
  printf("%s: horizontal %lld us/key, vertical %lld us/key, insert %lld us/key\n",c.name,us(t0,t1)/2000,us(t1,t2)/400,us(t2,t3)/50);
 }
 return 0;
}
