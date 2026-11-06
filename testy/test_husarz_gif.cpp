#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "../obrazy/dekoder_obrazu.h"
static unsigned checks;static size_t allocated,peak;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static void *allocate(size_t n){if(n>8*1024*1024 || allocated>8*1024*1024-n)return nullptr;auto *p=static_cast<size_t*>(std::malloc(n+sizeof(size_t)));if(!p)return nullptr;*p=n;allocated+=n;if(allocated>peak)peak=allocated;return p+1;}
static void release(void *p){if(p){auto *n=static_cast<size_t*>(p)-1;allocated-=*n;std::free(n);}}
static BladObrazu decode(const std::vector<uint8_t>&bytes){DekodowanyObraz out{};auto result=dekoduj_obraz_z_alfa(bytes.data(),bytes.size(),&out,allocate,release);if(result==BladObrazu::OK){CHECK(out.piksele && out.bajty==size_t(out.szerokosc)*out.wysokosc*4);CHECK(out.szerokosc<=OBRAZ_MAX_BOK && out.wysokosc<=OBRAZ_MAX_BOK);release(out.piksele);}else CHECK(!out.piksele && !out.bajty);CHECK(!allocated);return result;}
int main(){
 for(const char *name:{"static.gif","transparent.gif","animated.gif"}){
  std::ifstream f(std::string("narzedzia/fixtures/husarz/gif/")+name,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});CHECK(!bytes.empty());CHECK(rozpoznaj_obraz(bytes.data(),bytes.size())==FormatObrazu::GIF);
  uint32_t w=0,h=0;CHECK(sprawdz_gif(bytes.data(),bytes.size(),&w,&h)==BladObrazu::OK);CHECK(w==24 && h==16);CHECK(decode(bytes)==BladObrazu::OK);
  DekodowanyObraz out{};CHECK(dekoduj_obraz_z_alfa(bytes.data(),bytes.size(),&out,allocate,release)==BladObrazu::OK);
  CHECK(out.piksele[3]==(std::strcmp(name,"static.gif")?0:255));if(!std::strcmp(name,"static.gif")){CHECK(out.piksele[0]==120 && out.piksele[1]==80 && out.piksele[2]==40);}release(out.piksele);CHECK(!allocated);
  for(size_t n=0;n<bytes.size();++n){auto truncated=bytes;truncated.resize(n);CHECK(decode(truncated)!=BladObrazu::OK);}
  auto oversized=bytes;oversized[6]=255;oversized[7]=255;CHECK(decode(oversized)!=BladObrazu::OK);
  uint32_t random=7;for(unsigned i=0;i<1500;++i){random=random*1664525+1013904223;auto corrupt=bytes;corrupt[random%bytes.size()]^=uint8_t(1U<<((random>>16)&7));decode(corrupt);}
 }
 std::printf("Husarz GIF: %u checks PASS; first frame only; peak workspace=%zu bytes\n",checks,peak);
}
