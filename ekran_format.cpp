#include "ekran_format.h"
namespace {
bool rowne(const char*a,size_t n,const char*b){size_t i=0;while(i<n&&b[i]&&a[i]==b[i])++i;return i==n&&!b[i];}
}
bool ekran_parsuj(const char* s,size_t n,EkranKonfiguracja* out){
    if(!s||!out||!n||n>EKRAN_CFG_MAX)return false;
    uint32_t values[5]={};uint32_t seen=0;
    const char* keys[]={"version","szerokosc","wysokosc","bpp","skala"};
    size_t p=0;
    while(p<n){
        const size_t start=p;while(p<n&&s[p]!='='&&s[p]!='\n'&&s[p])++p;
        if(p==n||s[p]!='=')return false;
        unsigned key=0;for(;key<5&&!rowne(s+start,p-start,keys[key]);++key){}
        if(key==5||(seen&(1U<<key)))return false;
        ++p;const size_t digits=p;uint32_t v=0;
        while(p<n&&s[p]>='0'&&s[p]<='9'){
            uint32_t d=s[p++]-'0';if(v>(UINT32_MAX-d)/10)return false;v=v*10+d;
        }
        if (p == digits || (p < n && s[p] != '\n')) return false;
        if (p < n) {
            ++p;
        }
        seen |= 1U << key;
        values[key] = v;
    }
    if(seen!=31||values[0]!=1||!ui_skala_poprawna(values[4]))return false;
    if(values[1]>UINT32_MAX/4)return false;
    TrybEkranu t{values[1],values[2],values[3],values[1]*4,0,0};size_t bytes=0;
    if(!ekran_rozmiar(t,&bytes))return false;
    *out={values[1],values[2],values[3],values[4]};return true;
}
size_t ekran_serializuj(const EkranKonfiguracja& c,char*out,size_t cap){
    if(!out||!cap)return 0;
    size_t pos=0;bool ok=true;
    auto put=[&](char ch){if(pos+1<cap)out[pos++]=ch;else ok=false;};
    auto field=[&](const char*k,uint32_t v){while(*k)put(*k++);put('=');char rev[10];unsigned n=0;
        do{rev[n++]=char('0'+v%10);v/=10;}while(v);while(n)put(rev[--n]);put('\n');};
    field("version",1);field("szerokosc",c.szerokosc);field("wysokosc",c.wysokosc);field("bpp",c.bpp);field("skala",c.skala);
    out[pos]=0;return ok?pos:0;
}
