#pragma once
#include <stdint.h>
#include <stddef.h>
#include "desktop_entries.h"
#include "desktop_anchor.h"

#ifndef BURSZTYNDIAGDESKTOPLAYOUT
#define BURSZTYNDIAGDESKTOPLAYOUT 0
#endif

// Text settings through the existing BWS file API. No filesystem internals.
namespace DesktopSettings {
constexpr const char* Path="/ustawienia/uklad-pulpitu.cfg";
constexpr int Capacity=DesktopEntries::ModelCapacity;
constexpr size_t IdCapacity=512, BufferCapacity=Capacity*(3*IdCapacity+128)+256;
constexpr int CoordinateMax=1000000;
struct Key {char type;const char* id;}; // L: catalog ID; F: desktop-relative path
struct Record {char type=0;char id[IdCapacity]{};int x=0,y=0;Desktop::Anchor anchor;};
inline bool equal(const char* a,const char* b) {
    while(*a && *a==*b){++a;++b;}return *a==*b;
}
inline bool identity(const char* id) {
    if(!id || !*id || equal(id,".") || equal(id,".."))return false;
    size_t n=0;
    for(;n<IdCapacity && id[n];++n)if(id[n]=='/' || id[n]=='\\' || static_cast<unsigned char>(id[n])<32 || id[n]==127)return false;
    if(n==IdCapacity)return false;
    // Strict UTF-8: no overlong forms, surrogates, truncated continuations or
    // code points outside Unicode. Persistent identity never uses label clipping.
    for(size_t p=0;p<n;) {
        unsigned char c=id[p++];if(c<128)continue;
        unsigned bytes=0;uint32_t cp=0,min=0;
        if(c>=0xc2 && c<=0xdf){bytes=1;cp=c&31;min=0x80;}
        else if(c>=0xe0 && c<=0xef){bytes=2;cp=c&15;min=0x800;}
        else if(c>=0xf0 && c<=0xf4){bytes=3;cp=c&7;min=0x10000;}
        else return false;
        if(bytes>n-p)return false;
        while(bytes--){unsigned char b=id[p++];if((b&192)!=128)return false;cp=(cp<<6)|(b&63);}
        if(cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return false;
    }
    return true;
}
inline bool number(const char* s,size_t n,int& value) {
    if(!n)return false;
    value=0;
    for(size_t i=0;i<n;++i) {
        if(s[i]<'0'||s[i]>'9'||value>(CoordinateMax-(s[i]-'0'))/10)return false;
        value=value*10+s[i]-'0';
    }
    return true;
}
inline bool coordinate(const char* s,size_t n,int& value,bool signedAnchor) {
    const bool negative=signedAnchor && n && s[0]=='-';
    if(!number(s+negative,n-negative,value))return false;
    if(negative)value=-value;
    return true;
}
inline int hex(char c) {return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;}
inline bool decode(const char* s,size_t n,char* out) {
    size_t p=0;
    for(size_t i=0;i<n;++i) {
        unsigned char c=s[i];
        if(c=='%') {if(i+2>=n || hex(s[i+1])<0 || hex(s[i+2])<0)return false;c=hex(s[i+1])*16+hex(s[i+2]);i+=2;}
        if(!c || p+1>=IdCapacity)return false;
        out[p++]=c;
    }
    out[p]=0;return identity(out);
}
inline uint32_t checksum(const char* s,size_t n) {
    uint32_t hash=2166136261U;for(size_t i=0;i<n;++i){hash^=static_cast<unsigned char>(s[i]);hash*=16777619U;}return hash;
}
// All large buffers and records are heap-backed in the WM.
struct State {
    Record records[Capacity]{};
    char buffer[BufferCapacity]{};
    Record scratch{};
    int count=0,savedDpi=100;
    bool loaded=false,dirty=false,applied=false,anchorCoordinates=false,gridCoordinates=false;
    int matchedLaunchers=0,matchedFiles=0,stale=0;
    void reset() {anchorCoordinates=gridCoordinates=false;count=0;savedDpi=100;matchedLaunchers=matchedFiles=stale=0;}
    bool parse(size_t size) {
        reset();
        constexpr char header[]="BursztynDesktopLayout=1\n";
        size_t h=sizeof(header)-1;
        if(size<h+13 || size>=BufferCapacity)return false;
        for(size_t i=0;i<h;++i)if(buffer[i]!=header[i])return false;
        // Mandatory terminator rejects interrupted/truncated whole-file writes.
        if(buffer[size-13]!='e'||buffer[size-12]!='n'||buffer[size-11]!='d'||buffer[size-10]!='='||buffer[size-1]!='\n')return false;
        uint32_t expected=0;
        for(size_t i=size-9;i<size-1;++i){int digit=hex(buffer[i]);if(digit<0)return false;expected=(expected<<4)|digit;}
        if(expected!=checksum(buffer,size-13))return false;
        for(size_t pos=h;pos<size-13;) {
            size_t end=pos;while(end<size-13 && buffer[end]!='\n')++end;
            if(end==size-13){reset();return false;}
            const char* line=buffer+pos;size_t n=end-pos;pos=end+1;
            if(n>4 && line[0]=='d'&&line[1]=='p'&&line[2]=='i'&&line[3]=='=') {
                int dpi=0;
                if(!number(line+4,n-4,dpi) || dpi<50 || dpi>400){reset();return false;}
                savedDpi=dpi;
                continue;
            }
            if(n==9 && line[0]=='a') {
                bool match=true;for(size_t j=0;j<n;++j)if(line[j]!="anchors=1"[j])match=false;
                if(match){anchorCoordinates=true;continue;}
            }
            if(n==6 && line[0]=='g' && line[1]=='r' && line[2]=='i' && line[3]=='d' && line[4]=='=' && line[5]=='1') {gridCoordinates=true;continue;}
            size_t dimension=0;
            if(n>=6 && line[0]=='w' && line[1]=='i' && line[2]=='d' && line[3]=='t' && line[4]=='h' && line[5]=='=')dimension=6;
            if(n>=7 && line[0]=='h' && line[1]=='e' && line[2]=='i' && line[3]=='g' && line[4]=='h' && line[5]=='t' && line[6]=='=')dimension=7;
            if(dimension) {
                int value=0;if(!number(line+dimension,n-dimension,value) || value<=0 || value>8192){reset();return false;}
                continue;
            }
            if(n<3 || (line[0]!='L' && line[0]!='F') || line[1]!='\t')continue;
            size_t a=2;while(a<n && line[a]!='\t')++a;
            size_t b=a+1;while(b<n && line[b]!='\t')++b;
            size_t c=b+1;while(c<n && line[c]!='\t')++c;
            if(a==n || b>=n)continue;
            scratch=Record{};scratch.type=line[0];
            if(!decode(line+2,a-2,scratch.id)||!coordinate(line+a+1,b-a-1,scratch.x,anchorCoordinates)||!coordinate(line+b+1,c-b-1,scratch.y,anchorCoordinates))continue;
            if(gridCoordinates) {
                // Existing trailing flags precede the optional exact grid form.
                size_t start=c+1;while(start<n && line[start]!='\t')++start;
                int values[5]{};bool valid=start<n;
                for(int k=0;k<5 && valid;++k) {
                    size_t end=++start;while(end<n && line[end]!='\t')++end;
                    valid=coordinate(line+start,end-start,values[k],true);start=end;
                    if(k<4 && end==n)valid=false;
                }
                if(!valid || values[4]<100 || values[4]>400 ||
                   values[0]<-8192 || values[0]>8192 || values[1]<-8192 || values[1]>8192)continue;
                scratch.anchor={values[0],values[1],values[2],values[3],uint32_t(values[4])};
            }
            bool duplicate=false;
            for(int i=0;i<count;++i)if(records[i].type==scratch.type && equal(records[i].id,scratch.id))duplicate=true;
            // First valid record wins, regardless of unknown trailing flags.
            if(!duplicate) {
                if(count==Capacity){reset();return false;}
                records[count++]=scratch;
            }
        }
        return true;
    }
    template<class Call> bool load(Call call) {
        loaded=true;dirty=false;reset();
        uint64_t size=call(44,reinterpret_cast<uint64_t>(Path),0,0);
        if(size<=1 || size-1>=BufferCapacity)return false;
        return call(5,reinterpret_cast<uint64_t>(Path),reinterpret_cast<uint64_t>(buffer),size-1)==1 && parse(size-1);
    }
    template<class Layout,class Keys> int apply(Layout& layout,Keys key) {
        matchedLaunchers=matchedFiles=0;
        // Saved entries reserve space before auto-placement of missing entries.
        for(int i=0;i<layout.count;++i)layout.items[i].placed=false;
        for(int i=0;i<layout.count;++i) {
            auto k=key(i);
            for(int r=0;r<count;++r)if(records[r].type==k.type && equal(records[r].id,k.id)) {
                int x=int(int64_t(records[r].x)*layout.dpi/savedDpi);
                int y=int(int64_t(records[r].y)*layout.dpi/savedDpi);
                if(records[r].anchor.dpi) {layout.items[i].anchor=records[r].anchor;layout.items[i].custom=true;layout.restore_anchor(i);}
                else if(anchorCoordinates)layout.saved_anchor(i,records[r].x,records[r].y,savedDpi);
                else layout.visual_position(i,x,y);
                if(k.type=='L')++matchedLaunchers;else ++matchedFiles;
                break;
            }
        }
        stale=count-matchedLaunchers-matchedFiles;
        int corrected=layout.reflow();
        // Migrate legacy text-bound coordinates once at load, before a live
        // family change can make their visual offset ambiguous on next boot.
        dirty=count>0 && !gridCoordinates;return corrected;
    }
    template<class Layout,class Keys> size_t serialize(const Layout& layout,Keys key) {
        if(layout.count<0 || layout.count>Capacity || layout.workWidth<=0 || layout.workBottom<=0)return 0;
        size_t pos=0;bool ok=true;
        auto put=[&](char c){if(pos+1>=BufferCapacity){ok=false;return;}buffer[pos++]=c;};
        auto text=[&](const char* s){while(*s)put(*s++);};
        auto num=[&](int n){if(n<0){put('-');n=-n;}char digits[10];int c=0;do{digits[c++]=char('0'+n%10);n/=10;}while(n);while(c)put(digits[--c]);};
        text("BursztynDesktopLayout=1\nanchors=1\ngrid=1\nwidth=");num(layout.workWidth);text("\nheight=");num(layout.workBottom);text("\ndpi=");num(layout.dpi);put('\n');
        for(int i=0;i<layout.count;++i)if(layout.items[i].placed || layout.items[i].anchor.dpi) {
            auto k=key(i);auto r=layout.cell(i);
            auto anchor=layout.items[i].anchor;
            if(!anchor.dpi)anchor=Desktop::capture_anchor(r.x,r.y,layout.x0,layout.y0,layout.cw,layout.ch,layout.dpi);
            if(anchor.col < -8192 || anchor.col>8192 || anchor.row < -8192 || anchor.row>8192 ||
               anchor.dpi<100 || anchor.dpi>400 || anchor.offsetX < -CoordinateMax || anchor.offsetX>CoordinateMax ||
               anchor.offsetY < -CoordinateMax || anchor.offsetY>CoordinateMax)return 0;
            r.x=layout.x0+anchor.col*layout.cw+Desktop::transform_offset(anchor.offsetX,anchor.dpi,layout.dpi);
            r.y=layout.y0+anchor.row*layout.ch+Desktop::transform_offset(anchor.offsetY,anchor.dpi,layout.dpi);
            if((k.type!='L' && k.type!='F') || !identity(k.id)||r.x < -CoordinateMax||r.y < -CoordinateMax||r.x>CoordinateMax||r.y>CoordinateMax)return 0;
            put(k.type);put('\t');
            for(size_t b=0;k.id[b];++b) {
                unsigned char c=k.id[b];
                if(c=='%' || c=='\t' || c=='\n' || c=='\r') {put('%');put("0123456789ABCDEF"[c>>4]);put("0123456789ABCDEF"[c&15]);}
                else put(c);
            }
            put('\t');num(r.x);put('\t');num(r.y);text("\t0\t");
            num(anchor.col);put('\t');num(anchor.row);put('\t');num(anchor.offsetX);put('\t');num(anchor.offsetY);put('\t');num(anchor.dpi);put('\n');
        }
        uint32_t hash=checksum(buffer,pos);text("end=");
        for(int shift=28;shift>=0;shift-=4)put("0123456789ABCDEF"[(hash>>shift)&15]);
        put('\n');buffer[pos]=0;return ok?pos:0;
    }
    template<class Layout,class Keys,class Call> bool save(const Layout& layout,Keys key,Call call) {
        if(!dirty)return true;
        // Consume the attempt: failures wait for another meaningful transaction.
        dirty=false;
        size_t size=serialize(layout,key);if(!size)return false;
        if(call(44,reinterpret_cast<uint64_t>(Path),0,0)<=1) {
            // Existing directories are accepted; file creation still enforces PZB.
            (void)call(46,reinterpret_cast<uint64_t>("/ustawienia"),0,0);
            (void)call(2,reinterpret_cast<uint64_t>(Path),0,0);
        }
        if(call(3,reinterpret_cast<uint64_t>(Path),reinterpret_cast<uint64_t>(buffer),size)!=1)return false;
        // BWS write commits PSF changes. No atomic rename/replace is claimed.
        uint32_t hash=checksum(buffer,size);
        if(call(44,reinterpret_cast<uint64_t>(Path),0,0)!=size+1 ||
            call(5,reinterpret_cast<uint64_t>(Path),reinterpret_cast<uint64_t>(buffer),size)!=1)return false;
        return checksum(buffer,size)==hash;
    }
};
}
