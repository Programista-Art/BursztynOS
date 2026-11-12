#pragma once
#include <stddef.h>
#include <stdint.h>
#include "limits.h"
namespace BSF {
// Font directories (BSF 2.2). Each directory accepts exactly one extension;
// the outline format is still identified from validated data, never from
// the extension.
struct FontDirectory { const char* path; const char* ext; };
constexpr FontDirectory FontDirectories[]={{"/czcionki/ttf","ttf"},{"/czcionki/otf","otf"},
  {"/czcionki/ttc","ttc"},{"/czcionki/otc","otc"}};
constexpr unsigned FontDirectoryCount=4;
inline bool lower_equal(char a,char b){if(a>='A'&&a<='Z')a=char(a+32);return a==b;}
// PSF names occupy 56 bytes including NUL. UTF-8 is validated, never normalized.
inline bool font_filename(const char* name,size_t n,const char* ext){
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
  return end[0]=='.'&&lower_equal(end[1],ext[0])&&lower_equal(end[2],ext[1])&&lower_equal(end[3],ext[2]);
}
inline bool font_filename(const char* name,size_t n){return font_filename(name,n,"ttf");}
// Index of the font directory whose extension matches the file name.
inline int font_directory_for(const char* name,size_t n){
  for(unsigned d=0;d<FontDirectoryCount;++d)if(font_filename(name,n,FontDirectories[d].ext))return int(d);
  return -1;
}
// Read-only public GUI assets. This never grants general filesystem access.
// All four directory names have the same length ("/czcionki/xxx" = 13).
inline bool trusted_asset(const char* path,bool directory=false){
  if(!path)return false;
  for(unsigned d=0;d<FontDirectoryCount;++d){
    const char* prefix=FontDirectories[d].path;size_t i=0;
    while(prefix[i]&&path[i]==prefix[i])++i;
    if(prefix[i]||i!=13)continue;
    if(directory)return path[13]==0||(path[13]=='/'&&path[14]==0);
    if(path[13]!='/')return false;
    size_t n=0;while(n<=55&&path[14+n])++n;
    return font_filename(path+14,n,FontDirectories[d].ext);
  }
  return false;
}
}
