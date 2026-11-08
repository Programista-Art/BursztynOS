#include "../bsf/bsf.h"
#include "../bsf/edit.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
int main(){
 std::ifstream f("narzedzia/unicode/15.1.0/GraphemeBreakTest.txt");if(!f)return 2;
 std::string line;unsigned cases=0;
 while(std::getline(f,line)){
  line=line.substr(0,line.find('#'));std::istringstream row(line);std::string word,text;
  std::vector<size_t> expected;
  while(row>>word){
   if(word=="÷"){expected.push_back(text.size());continue;}if(word=="×")continue;
   unsigned cp=std::stoul(word,nullptr,16);
   if(cp<128)text+=char(cp);else{
    unsigned n=cp<2048?2:cp<65536?3:4;text+=char((n==2?0xc0:n==3?0xe0:0xf0)|(cp>>(6*(n-1))));
    for(unsigned k=n-1;k;--k)text+=char(0x80|((cp>>(6*(k-1)))&63));}
  }
  if(expected.empty())continue;++cases;
  BSF::BoundaryIndex<> index;index.rebuild(text.data(),text.size());
  std::vector<size_t> actual{0};size_t at=0;
  while(at<text.size()){size_t next=BSF::next_cluster(text.data(),at,text.size());if(next<=at)std::abort();at=next;actual.push_back(at);}
  if(actual!=expected){std::fprintf(stderr,"FAIL case %u: %s\n",cases,line.c_str());return 1;}
  if(BSF::cluster_count(text.data(),text.size())+1!=expected.size())return 3;
  for(size_t i=0;i<expected.size();++i)if(index.rank(text.data(),expected[i])!=i)return 13;
  for(size_t i=1;i<expected.size();++i)if(BSF::previous_cluster(text.data(),expected[i])!=expected[i-1])return 4;
  for(size_t i=0;i<=text.size();++i){bool boundary=false;for(auto p:expected)boundary|=p==i;
   if(BSF::cluster_boundary(text.data(),text.size(),i)!=boundary)return 5;
   if(index.floor(text.data(),i)!=BSF::cluster_floor(text.data(),text.size(),i))return 11;
   if(index.previous(text.data(),i)!=(i?BSF::cluster_floor(text.data(),text.size(),i-1):0))return 12;
  }
 }
 for(unsigned value=0;value<256;++value){char invalid[4]={char(value),char(0xff),char(0x80),0};
  for(size_t at=0;at<3;){auto next=BSF::next_cluster(invalid,at,3);if(next<=at||next>3)return 6;at=next;}}
 uint32_t random=0x15f10;
 for(unsigned sample=0;sample<10000;++sample){
  char bytes[64];size_t size=sample%64;
  for(size_t i=0;i<size;++i){random=random*1664525+1013904223;bytes[i]=char(random>>24);}
  for(size_t at=0;at<size;){auto next=BSF::next_cluster(bytes,at,size);
   if(next<=at||next>size||BSF::previous_cluster(bytes,next)!=at||!BSF::cluster_boundary(bytes,size,next))return 10;
   at=next;}
 }
 BSF::MarkPen pen;int x=10,y=20;pen.place('e',10,x,y,10,8,12);
 x=0;y=0;pen.place(0x301,22,x,y,4,3,0);if(x!=13||y!=16)return 7;
 x=0;y=0;pen.place(0x308,22,x,y,6,2,0);if(y!=13)return 8;
 x=0;y=0;pen.place(0x327,22,x,y,4,3,0);if(y!=28)return 9;
 std::printf("Unicode 15.1.0 official GraphemeBreakTest: %u cases PASS; next/previous/count/all byte boundaries and malformed UTF-8 / 10000 deterministic mutation inputs PASS\n",cases);
}
