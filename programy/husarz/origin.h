#pragma once
#include "resource.h"
namespace Husarz {
struct Origin {
    char host[256]{};uint16_t port{};bool secure{},valid{};
    bool same(const Origin &o)const{return valid&&o.valid&&secure==o.secure&&port==o.port&&equal(host,o.host);}
    static Origin parse(const char *url){
        Origin out;size_t n=length(url,2048),i=0;
        if(n>=7&&equal_ci(url,7,"http://")){i=7;out.port=80;}
        else if(n>=8&&equal_ci(url,8,"https://")){i=8;out.secure=true;out.port=443;}else return out;
        unsigned count=0;while(i<n&&url[i]!='/'&&url[i]!='?'&&url[i]!='#'&&url[i]!=':'){
            char c=lower(url[i++]);if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='.')||count==255)return out;out.host[count++]=c;
        }
        if(!count||out.host[count-1]=='.'||out.host[0]=='.')return out;
        if(i<n&&url[i]==':'){++i;unsigned port=0,start=unsigned(i);while(i<n&&url[i]>='0'&&url[i]<='9'){port=port*10+unsigned(url[i++]-'0');if(port>65535)return out;}if(i==start||!port)return out;out.port=uint16_t(port);}
        if(i<n&&url[i]!='/'&&url[i]!='?'&&url[i]!='#')return out;
        for(;i<n;++i)if(uint8_t(url[i])<=32||url[i]=='\\')return out;
        out.valid=true;return out;
    }
};
}
