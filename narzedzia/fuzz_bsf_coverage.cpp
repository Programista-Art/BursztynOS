// GCC sanitizer-coverage driver for the real Ring 3 BSF parser/rasterizer.
// Compile this driver without coverage instrumentation to avoid recursion.
#include "../bsf/bsf.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <string>
#include <filesystem>
static unsigned char coverage[65536],seen[65536];
extern "C" void __sanitizer_cov_trace_pc(){
 uintptr_t pc=reinterpret_cast<uintptr_t>(__builtin_return_address(0));
 coverage[((pc>>4)^(pc>>20))&65535]=1;
}
static unsigned run(const std::vector<unsigned char>& data){
 memset(coverage,0,sizeof coverage);
 BSF::Face face;
 if(face.open(data.data(),data.size(),1)){
  BSF::Cache cache;
  uint32_t cps[]={'A',0x301,0x3a9,0x416,0x1f600,0x10ffff};
  for(auto cp:cps){face.glyph(cp);cache.get(face,cp,16);}
  if(data.size()>4){uint32_t cp=uint32_t(data[0])<<16|uint32_t(data[1])<<8|data[2];face.glyph(cp);cache.get(face,cp,20);}
 }
 BSF::MarkPen pen;int x=0,y=0;
 for(size_t i=0;i<data.size()&&i<256;){uint32_t cp;size_t n=BSF::utf8(reinterpret_cast<const char*>(data.data()+i),data.size()-i,cp);if(!n)abort();pen.place(cp,0,x,y,8,16,8);i+=n;}
 unsigned added=0;for(unsigned i=0;i<65536;++i)if(coverage[i]&&!seen[i]){seen[i]=1;++added;}
 return added;
}
int main(int argc,char** argv){
 if(argc!=4){fprintf(stderr,"usage: fuzz-bsf seed.ttf output-directory executions\n");return 2;}
 std::ifstream in(argv[1],std::ios::binary);std::vector<unsigned char> seed((std::istreambuf_iterator<char>(in)),{});
 if(seed.empty()||seed.size()>BSF::MaxFile)return 2;
 std::filesystem::create_directories(argv[2]);
 std::vector<std::vector<unsigned char>> corpus{seed};size_t bytes=seed.size();
 unsigned edges=run(seed),random=0x425346,count=unsigned(strtoul(argv[3],nullptr,10));
 auto next=[&](){random=random*1664525+1013904223;return random;};
 for(unsigned i=0;i<count;++i){
  auto input=corpus[next()%corpus.size()];
  if(i%7==0)input.resize(next()%(input.size()+1));
  else if(!input.empty())for(unsigned j=0,n=1+next()%8;j<n;++j){size_t p=next()%input.size();if(i%3==0)p%=input.size()<4096?input.size():4096;input[p]^=1u<<(next()%8);}
  unsigned added=run(input);edges+=added;
  if(added&&corpus.size()<64&&input.size()<=64*1024*1024-bytes){
   std::string name=std::string(argv[2])+"/coverage-"+std::to_string(i)+".ttf";
   std::ofstream out(name,std::ios::binary);out.write((const char*)input.data(),input.size());
   bytes+=input.size();corpus.push_back(std::move(input));
  }
 }
 printf("BSF GCC trace-pc coverage-guided campaign: executions=%u coverage-buckets=%u retained-inputs=%zu retained-bytes=%zu seed=0x425346 PASS\n",count+1,edges,corpus.size(),bytes);
}
