#pragma once
#include "resource.h"
namespace Husarz {
struct Origin {
    char host[256]{};uint16_t port{};bool secure{},valid{};
    bool same(const Origin &o)const{return valid&&o.valid&&secure==o.secure&&port==o.port&&equal(host,o.host);}
    // RFC 6454 serialization ("https://host[:port]", default port omitted)
    // for the request "Origin" header. False when invalid or too long.
    bool serialize(char *out,unsigned cap)const{
        if(!out||!cap)return false;out[0]=0;if(!valid)return false;
        const char *scheme=secure?"https://":"http://";unsigned n=0;
        auto put=[&](char c){if(n+1>=cap)return false;out[n++]=c;return true;};
        for(const char *p=scheme;*p;++p)if(!put(*p)){out[0]=0;return false;}
        for(unsigned i=0;i<sizeof(host)&&host[i];++i){char c=host[i];if(c>='A'&&c<='Z')c=char(c-'A'+'a');if(!put(c)){out[0]=0;return false;}}
        if(port&&port!=(secure?443:80)){char d[6];unsigned k=0;unsigned v=port;do{d[k++]=char('0'+v%10);v/=10;}while(v);
            if(!put(':')){out[0]=0;return false;}while(k)if(!put(d[--k])){out[0]=0;return false;}}
        out[n]=0;return true;
    }
    static Origin parse(const char *url){
        Origin out;auto *u=new Url;
        if(u&&parse_absolute_url(url,*u)){for(unsigned i=0;i<sizeof(u->host);++i)out.host[i]=u->host[i];out.port=u->port;out.secure=u->secure;out.valid=true;}delete u;return out;
    }
};
}
