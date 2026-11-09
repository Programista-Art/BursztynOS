// Host-only observer geometry from the actual production Settings component.
#define BSF_NATIVE 1
#define _start settings_layout_native_entry
#include "../programy/ustawienia/ustawienia.cpp"
#undef _start
#include "bursztyn_ui_host_fixture.h"
#include "../bsf/runtime.cpp"
#include <fstream>
#include <iterator>
#include <string>

static std::map<std::string,std::string> font_files;
static BSF::Config active;
static std::string listing;
static uint64_t io(uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t d){
 if(n==92){active.checksum=BSF::checksum(active);*reinterpret_cast<BSF::Config*>(b)=active;return 1;}
 if(n==6){if(listing.size()+1>c)return 0;memcpy(reinterpret_cast<void*>(b),listing.c_str(),listing.size()+1);return 1;}
 if(n==4){auto f=font_files.find(reinterpret_cast<const char*>(a));if(f==font_files.end())return 0;*reinterpret_cast<uint32_t*>(b)=f->second.size();return 1;}
 if(n==59){auto f=font_files.find(reinterpret_cast<const char*>(a));if(f==font_files.end()||b>f->second.size()||d>f->second.size()-b)return 0;memcpy(reinterpret_cast<void*>(c),f->second.data()+b,d);return d+1;}
 return 0;
}
bool czytaj_plik(const char* path,char* out,uint32_t cap){auto f=font_files.find(path);if(f==font_files.end()||f->second.size()>cap)return false;memcpy(out,f->second.data(),f->second.size());return true;}
bool czytaj_fragment_pliku(const char* path,uint64_t offset,char* out,uint32_t cap,uint32_t* read){auto f=font_files.find(path);if(f==font_files.end()||offset>f->second.size())return false;auto n=f->second.size()-offset;if(n>cap)n=cap;memcpy(out,f->second.data()+offset,n);if(read)*read=n;return true;}
bool utworz(const char*){return false;}
bool zapisz_plik(const char*,const char*,uint32_t){return false;}
void wypisz(const char*){}
bool ipc_send(uint64_t,const BwsIpcWyslij*){return false;}
bool ipc_receive(uint64_t,BwsIpcOdbierz*){return false;}
static void rect(FILE* file,const char* name,Prostokat r,Prostokat origin){fprintf(file,"\"%s\":[%d,%d,%d,%d],",name,r.x-origin.x,r.y-origin.y,r.szerokosc,r.wysokosc);}
int main(){
 g_test_bws_hook=io;
 for(const char* name:{"DejaVuSans.ttf","DejaVuSans-Bold.ttf","DejaVuSansMono.ttf","DejaVuSerifCondensed-Italic.ttf"}){
  std::ifstream input(std::string("czcionki/ttf/")+name,std::ios::binary);
  font_files[std::string("/czcionki/ttf/")+name]=std::string(std::istreambuf_iterator<char>(input),{});
  listing+=std::string("[F] ")+name+"\n";
 }
 for(unsigned size:{12u,14u,16u,18u,20u,24u}){
  BSF::shutdown();active=BSF::Config{};active.size=size;active.generation=size;active.checksum=BSF::checksum(active);
  Settings window;window.init();window.show(3);window.przelicz_uklad();
  auto* viewport=static_cast<SettingsUI::Viewport*>(window.kontrolka_glowna());
  auto* page=static_cast<SettingsUI::FontPage*>(window.font_page);
  char path[160];snprintf(path,sizeof(path),".bsf-test/live-font-layout-%u.json",size);
  FILE* output=fopen(path,"w");if(!output)return 1;
  Prostokat origin{};fprintf(output,"{");
  rect(output,"nav",viewport->body->nav[3]->obszar_ekranu(),origin);
  rect(output,"source",page->import_path->obszar_ekranu(),origin);
  rect(output,"install",page->install->obszar_ekranu(),origin);
  rect(output,"refresh",page->refresh->obszar_ekranu(),origin);
  rect(output,"families",page->families->obszar_ekranu(),origin);
  rect(output,"apply",page->apply->obszar_ekranu(),origin);
  rect(output,"font_size",page->size->obszar_ekranu(),origin);
  fprintf(output,"\"size\":%u}\n",size);fclose(output);
 }
 BSF::shutdown();
 FILE* observer=fopen(".bsf-test/font-observer.json","w");if(!observer)return 1;
 fprintf(observer,"{\"loaded_stride\":%zu,\"loaded_bytes\":%zu,\"loaded_size\":%zu,\"loaded_id\":%zu,\"cache_slots\":%zu,\"glyph_stride\":%zu,\"glyph_face\":%zu}\n",sizeof(BSF::Loaded),offsetof(BSF::Loaded,bytes),offsetof(BSF::Loaded,size),offsetof(BSF::Loaded,face)+offsetof(BSF::Face,id),offsetof(BSF::Cache,slots_),sizeof(BSF::Glyph),offsetof(BSF::Glyph,face));
 fclose(observer);
}
