#include "../bsf/runtime.h"
#include "../bsf/asset_path.h"
#include "../jadro/gui/bursztyn_gui.h"
#include <map>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
static unsigned checks,fail_array_at,fail_array_size;static bool short_read;
void* operator new[](size_t n){if((fail_array_at&&n>=fail_array_at)||(fail_array_size&&n==fail_array_size))return nullptr;return malloc(n);}
void* operator new[](size_t n,const std::nothrow_t&)noexcept{if((fail_array_at&&n>=fail_array_at)||(fail_array_size&&n==fail_array_size))return nullptr;return malloc(n);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{free(p);}
void operator delete[](void* p)noexcept{free(p);}void operator delete[](void* p,size_t)noexcept{free(p);}
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);abort();}}while(0)
static std::map<std::string,std::string> files;
static std::string directory;
static BSF::Config config;
void wypisz(const char* text){fputs(text,stderr);}
bool wylistuj_katalog_uzytkownika(const char*,char* out,uint32_t n){if(directory.size()>=n)return false;memcpy(out,directory.c_str(),directory.size()+1);return true;}
bool pobierz_rozmiar_pliku(const char* path,uint32_t* n){auto it=files.find(path);if(it==files.end())return false;*n=it->second.size();return true;}
bool czytaj_plik(const char* path,char* out,uint32_t n){auto it=files.find(path);if(it==files.end()||n!=it->second.size())return false;memcpy(out,it->second.data(),n);return true;}
extern "C" uint64_t bws_wywolaj(uint64_t fn,uint64_t a,uint64_t b,uint64_t c,uint64_t d){
 if(fn==92&&b&&c==sizeof(config)){if(!a){*(BSF::Config*)b=config;return 1;}config=*(BSF::Config*)b;return 1;}
 if(fn==59){CHECK(BSF::trusted_asset((const char*)a));CHECK(d<=65536);auto it=files.find((const char*)a);if(it==files.end()||b>it->second.size()||d>it->second.size()-b)return 0;if(short_read)return d;memcpy((void*)c,it->second.data()+b,d);return d+1;}return 0;
}
static uint32_t u32(const std::string& s,size_t p){return BSF::Bytes{(const uint8_t*)s.data(),s.size()}.u32(p);}
static unsigned u16(const std::string& s,size_t p){return BSF::Bytes{(const uint8_t*)s.data(),s.size()}.u16(p);}
static void put(std::string& s,size_t p,uint32_t v){for(unsigned i=0;i<4;++i)s[p+i]=char(v>>(24-i*8));}
static uint32_t sum(const std::string& s,size_t p,size_t n){uint32_t value=0;for(size_t at=0;at<n;at+=4){uint32_t word=0;for(unsigned k=0;k<4;++k)word=(word<<8)|(at+k<n?uint8_t(s[p+at+k]):0);value+=word;}return value;}
static void checksums(std::string& s){size_t head=0;for(unsigned i=0;i<u16(s,4);++i)if(s.compare(12+i*16,4,"head")==0)head=u32(s,20+i*16);CHECK(head);put(s,head+8,0);for(unsigned i=0;i<u16(s,4);++i){size_t p=12+i*16;put(s,p+4,sum(s,u32(s,p+8),u32(s,p+12)));}put(s,head+8,0xb1b0afba-sum(s,0,s.size()));CHECK(sum(s,0,s.size())==0xb1b0afba);}
static std::string named(std::string seed,const std::string& name){CHECK(name.size()==11);std::string old,newname;for(char c:std::string("DejaVu Sans")){old+=char(0);old+=c;}for(char c:name){newname+=char(0);newname+=c;}for(size_t p=seed.find(old);p!=std::string::npos;p=seed.find(old,p+newname.size()))seed.replace(p,old.size(),newname);checksums(seed);return seed;}
static std::string padded(std::string seed,size_t n){CHECK(n>=seed.size()&&n%4==0);size_t directory_end=12+16*u16(seed,4),padding=n-seed.size();seed.insert(directory_end,padding,'\0');for(unsigned i=0;i<u16(seed,4);++i){size_t p=20+i*16;put(seed,p,u32(seed,p)+padding);}checksums(seed);return seed;}
using Clock=std::chrono::steady_clock;
int main(){
 config.checksum=BSF::checksum(config);std::ifstream input("czcionki/ttf/DejaVuSans.ttf",std::ios::binary);std::string seed(std::istreambuf_iterator<char>(input),{});CHECK(!seed.empty());
 for(unsigned n:{32u,64u,128u}){
  BSF::shutdown();files.clear();directory.clear();
  for(unsigned i=0;i<n;++i){char name[20],path[80];snprintf(name,sizeof(name),"Family %04u",i);snprintf(path,sizeof(path),"/czcionki/ttf/font-%03u.ttf",i);files[path]=named(seed,name);directory="[F] "+std::string(path+14)+"\n"+directory;}
  auto start=Clock::now();CHECK(BSF::enumerate()==n);auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();
  for(unsigned i=0;i<n;++i){char name[20];snprintf(name,sizeof(name),"Family %04u",i);CHECK(std::string(BSF::info(i)->family)==name);}
  auto first=BSF::info(0)->id;BSF::rescan();CHECK(BSF::enumerate()==n&&BSF::info(0)->id==first);
  printf("catalog=%u discover_ns=%lld catalog_bytes=%zu io_bytes=%llu\n",n,(long long)ns,sizeof(BSF::FontInfo)*BSF::MaxFiles,(unsigned long long)BSF::resources().io_bytes);
 }
 BSF::shutdown();files.clear();directory.clear();
 files["/czcionki/ttf/small.ttf"]=seed;directory="[F] small.ttf\n";CHECK(BSF::ready());auto* reference=BSF::glyph('A',20);CHECK(reference);int advance=reference->advance,w=reference->width,h=reference->height;std::vector<uint8_t> pixels(reference->alpha,reference->alpha+w*h);
 auto stable=BSF::default_id();files["/czcionki/ttf/first.ttf"]=named(seed,"A Font 0000");directory+="[F] first.ttf\n";
 BSF::rescan();CHECK(BSF::enumerate()==2);CHECK(BSF::default_id()==stable);CHECK(BSF::info(0)->id!=stable);
 BSF::shutdown();fail_array_at=sizeof(BSF::FontInfo)*BSF::MaxFiles;CHECK(!BSF::ready());fail_array_at=0;BSF::rescan();CHECK(BSF::ready());
 fail_array_size=1024*sizeof(char*);BSF::rescan();CHECK(BSF::enumerate()==0);CHECK(BSF::resources().directory_error);fail_array_size=0;BSF::rescan();CHECK(BSF::ready());
 BSF::shutdown();files.clear();directory.clear();
 for(auto size:{3u*1024*1024,BSF::MaxFile}){
  auto font=padded(named(seed,"Large Font0"),size);BSF::Face f;CHECK(f.open(font.data(),font.size(),1));CHECK(f.glyph('A')!=0);
  files["/czcionki/ttf/large.ttf"]=std::move(font);directory="[F] large.ttf\n";
  auto begin=Clock::now();BSF::rescan();CHECK(BSF::enumerate()==1);auto discovery=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count();begin=Clock::now();auto* g=BSF::glyph('A',20,false,"Large Font0");CHECK(g&&g->advance==advance&&g->width==w&&g->height==h);CHECK(!memcmp(g->alpha,pixels.data(),pixels.size()));
  auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count();auto r=BSF::resources();CHECK(r.resident_bytes==size&&r.peak_resident_bytes<=BSF::MaxLoadedBytes);
  printf("file=%u discover_ns=%lld load_raster_ns=%lld resident=%u peak=%u\n",size,(long long)discovery,(long long)elapsed,r.resident_bytes,r.peak_resident_bytes);
  short_read=true;BSF::rescan();CHECK(BSF::enumerate()==0);short_read=false;BSF::rescan();CHECK(BSF::ready("Large Font0"));
  BSF::shutdown();fail_array_at=1024*1024;CHECK(!BSF::ready("Large Font0"));fail_array_at=0;BSF::rescan();CHECK(BSF::ready("Large Font0"));
 }
 // BSF 2.1 rejected files above MaxFile. BSF 2.2 reads such files per table
 // (BWS 59 ranges), so they load with only their tables resident; files above
 // MaxFontFile are still rejected.
 BSF::shutdown();files["/czcionki/ttf/large.ttf"].push_back(0);BSF::rescan();CHECK(BSF::enumerate()==1);
 {auto* g=BSF::glyph('A',20,false,"Large Font0");CHECK(g&&g->advance==advance&&g->width==w&&g->height==h);CHECK(!memcmp(g->alpha,pixels.data(),pixels.size()));
  auto r=BSF::resources();CHECK(r.resident_bytes>0&&r.resident_bytes<seed.size()+4096&&r.peak_resident_bytes<=BSF::MaxResidentFontBytes);}
 BSF::shutdown();files["/czcionki/ttf/large.ttf"].resize(size_t(BSF::MaxFontFile)+1);BSF::rescan();CHECK(BSF::enumerate()==0);
 CHECK(BSF::font_filename("Ω font.TTF",strlen("Ω font.TTF")));CHECK(!BSF::font_filename("../x.ttf",8));CHECK(!BSF::font_filename("\xff.ttf",5));
 BSF::shutdown();printf("BSF expanded catalog/large-file/budget/failure tests: %u checks PASS\n",checks);
}
