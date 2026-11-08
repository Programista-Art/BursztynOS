#pragma once
#include <stddef.h>
#include <stdint.h>
#include "limits.h"
namespace BSF {
// PSF names occupy 56 bytes including NUL. UTF-8 is validated, never normalized.
inline bool font_filename(const char* name,size_t n){
  if(!name||n<=4||n>55)return false;
  for(size_t at=0;at<n;){
    unsigned c=uint8_t(name[at]);
    if(c<128){if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'||c==' '))return false;++at;continue;}
    unsigned k=c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
    if(!k||k>n-at)return false;
    uint32_t cp=c&((1u<<(7-k))-1);
    for(unsigned i=1;i<k;++i){unsigned b=uint8_t(name[at+i]);if((b&0xc0)!=0x80)return false;cp=(cp<<6)|(b&63);}
    if((k==2&&cp<0x80)||(k==3&&cp<0x800)||(k==4&&cp<0x10000)||cp<0xa0||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
    at+=k;
  }
  const char* end=name+n-4;
  return end[0]=='.'&&(end[1]=='t'||end[1]=='T')&&(end[2]=='t'||end[2]=='T')&&(end[3]=='f'||end[3]=='F');
}
// Read-only public GUI assets. This never grants general filesystem access.
inline bool trusted_asset(const char* path,bool directory=false){
  if(!path)return false;
  const char prefix[]="/czcionki/ttf";
  for(size_t i=0;i<sizeof(prefix)-1;++i)if(!path[i]||path[i]!=prefix[i])return false;
  if(directory)return path[13]==0||(path[13]=='/'&&path[14]==0);
  if(path[13]!='/')return false;
  size_t n=0;while(n<=55&&path[14+n])++n;
  return font_filename(path+14,n);
}
}
