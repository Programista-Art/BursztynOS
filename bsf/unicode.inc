// Native UAX #29 extended grapheme boundaries, Unicode 15.1.0.
#include "unicode_data.inc"
namespace {
unsigned property(const uint8_t* table,size_t bytes,uint32_t cp){
 size_t lo=0,hi=bytes/7;
 auto value=[](const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16);};
 while(lo<hi){size_t m=lo+(hi-lo)/2;const auto* p=table+m*7;
  if(cp<value(p))hi=m;else if(cp>value(p+3))lo=m+1;else return p[6];}
 return 0;
}
unsigned grapheme_property(uint32_t cp){
 if(cp>=0xac00&&cp<=0xd7a3)return (cp-0xac00)%28?13:12;
 return property(unicode_gcb,sizeof(unicode_gcb),cp);
}
bool pictographic(uint32_t cp){return property(unicode_ep,sizeof(unicode_ep),cp)!=0;}
unsigned conjunct(uint32_t cp){return property(unicode_incb,sizeof(unicode_incb),cp);}
}
unsigned combining_class(uint32_t cp){return property(unicode_ccc,sizeof(unicode_ccc),cp);}
bool default_ignorable(uint32_t cp){return property(unicode_ignorable,sizeof(unicode_ignorable),cp)!=0;}
bool combining(uint32_t cp){return combining_class(cp)!=0;}
bool terminal_attachment(uint32_t cp){
 unsigned p=grapheme_property(cp);return p==4||p==5||p==8;
}
size_t next_cluster(const char* text,size_t at,size_t size){
 if(!text||at>=size)return size;
 uint32_t cp;at+=utf8(text+at,size-at,cp);
 unsigned previous=grapheme_property(cp),ri=previous==6?1:0;
 bool ep=pictographic(cp),zwj_ep=false;
 unsigned ic=conjunct(cp);bool consonant=ic==1,linker=false;
 while(at<size){
  uint32_t next;size_t n=utf8(text+at,size-at,next);
  unsigned current=grapheme_property(next),inc=conjunct(next);
  bool join=false;
  if(previous==1&&current==2)join=true; // GB3
  else if(previous==1||previous==2||previous==3||current==1||current==2||current==3)join=false;
  else if(previous==9&&(current==9||current==10||current==12||current==13))join=true;
  else if((previous==12||previous==10)&&(current==10||current==11))join=true;
  else if((previous==13||previous==11)&&current==11)join=true;
  else if(current==4||current==5||current==8||previous==7)join=true;
  else if(consonant&&linker&&inc==1)join=true; // GB9c
  else if(previous==5&&zwj_ep&&pictographic(next))join=true;
  else if(previous==6&&current==6&&(ri&1))join=true;
  if(!join)break;
  bool next_zwj_ep=current==5&&ep;
  ep=pictographic(next)||(current==4&&ep);zwj_ep=next_zwj_ep;
  if(inc==1){consonant=true;linker=false;}
  else if(inc==3){linker|=consonant;}
  else if(inc!=2){consonant=false;linker=false;}
  ri=current==6?ri+1:0;previous=current;at+=n;
 }
 return at;
}
size_t previous_cluster(const char* text,size_t at){
 if(!text||!at)return 0;
 size_t previous=0,current=0;
 while(current<at){previous=current;current=next_cluster(text,current,at);}
 return previous;
}
size_t cluster_floor(const char* text,size_t size,size_t offset){
 if(offset>size)offset=size;
 size_t at=0;
 while(at<offset){size_t next=next_cluster(text,at,size);if(next>offset)break;at=next;}
 return at;
}
bool cluster_boundary(const char* text,size_t size,size_t offset){return offset<=size&&cluster_floor(text,size,offset)==offset;}
size_t cluster_count(const char* text,size_t size){if(!text)return 0;size_t n=0;for(size_t at=0;at<size;at=next_cluster(text,at,size))++n;return n;}
