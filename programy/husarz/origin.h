#pragma once
#include "resource.h"
namespace Husarz {
struct Origin {
    char host[256]{};uint16_t port{};bool secure{},valid{};
    bool same(const Origin &o)const{return valid&&o.valid&&secure==o.secure&&port==o.port&&equal(host,o.host);}
    static Origin parse(const char *url){
        Origin out;auto *u=new Url;
        if(u&&parse_absolute_url(url,*u)){for(unsigned i=0;i<sizeof(u->host);++i)out.host[i]=u->host[i];out.port=u->port;out.secure=u->secure;out.valid=true;}delete u;return out;
    }
};
}
