#include "dekoder_obrazu.h"
#include <vector>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <cassert>
static std::map<void*,size_t> blocks;
static size_t live=0,peak=0,calls=0,fail=0,checks=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);abort();}}while(0)
static void* allocate(size_t n){++calls;if(fail&&calls==fail)return nullptr;auto* p=malloc(n);if(p){blocks[p]=n;live+=n;if(live>peak)peak=live;}return p;}
static void release(void* p){if(p){CHECK(blocks.count(p));live-=blocks[p];blocks.erase(p);free(p);}}
static uint32_t crc(const unsigned char* p,size_t n){uint32_t c=~0U;for(size_t i=0;i<n;++i){c^=p[i];for(int b=0;b<8;++b)c=c>>1^(0xedb88320U&(0U-(c&1)));}return ~c;}
static void be(std::vector<unsigned char>& d,size_t pos,uint32_t n){for(int i=0;i<4;++i)d[pos+i]=n>>(24-8*i);}
static void invalid(const std::vector<unsigned char>& d,BladObrazu expected=BladObrazu::OK){DekodowanyObraz o{};auto e=dekoduj_ikone_png(d.data(),d.size(),&o,allocate,release);CHECK(e!=BladObrazu::OK);if(expected!=BladObrazu::OK)CHECK(e==expected);CHECK(!o.piksele);CHECK(live==0);}
int main(int argc,char** argv){
 for(const char* n:{"zasilania","zablokuj","uspij","zamknij","uruchomponownie"}){
  std::ifstream f(std::string("ikonki/przycisk")+n+".png",std::ios::binary);std::vector<unsigned char>d{std::istreambuf_iterator<char>(f),{}};CHECK(d.size()>45);
  DekodowanyObraz o{};calls=0;CHECK(dekoduj_ikone_png(d.data(),d.size(),&o,allocate,release)==BladObrazu::OK);CHECK(o.szerokosc==128&&o.wysokosc==128);CHECK(o.bajty==65536);
  bool transparent=false,partial=false,opaque=false;for(size_t i=3;i<o.bajty;i+=4){transparent|=o.piksele[i]==0;partial|=o.piksele[i]>0&&o.piksele[i]<255;opaque|=o.piksele[i]==255;}
  CHECK(transparent&&partial&&opaque);size_t allocation_count=calls;release(o.piksele);CHECK(live==0);
  for(size_t failure=1;failure<=allocation_count;++failure){calls=0;fail=failure;invalid(d);fail=0;}
  for(size_t length=0;length<d.size();length+=37){auto broken=d;broken.resize(length);invalid(broken);}
  auto broken=d;broken[29]^=0x10;invalid(broken,BladObrazu::Uszkodzony);
  broken=d;be(broken,16,129);be(broken,29,crc(broken.data()+12,17));size_t before=calls;invalid(broken,BladObrazu::ZaDuzy);CHECK(calls==before);
  broken=d;be(broken,20,0xffffffffU);be(broken,29,crc(broken.data()+12,17));invalid(broken,BladObrazu::ZaDuzy);
  broken=d;be(broken,33,0xffffffffU);invalid(broken);
  broken=d;broken.push_back(0);invalid(broken);
 }
 if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<unsigned char>d{std::istreambuf_iterator<char>(f),{}};CHECK(!d.empty());invalid(d);}
 invalid(std::vector<unsigned char>(256*1024+1),BladObrazu::ZaDuzy);
 CHECK(peak<=2*1024*1024+65536);CHECK(blocks.empty());
 printf("Bounded PNG: %zu checks PASS, peak decoder+copy heap=%zu bytes\n",checks,peak);
}
