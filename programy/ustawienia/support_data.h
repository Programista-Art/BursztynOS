#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../../bsf/bsf.h"
namespace SupportData {
constexpr size_t MaxFile=64*1024,MaxEntries=256,MaxName=160;
inline constexpr const char* Paths[]={"/dane/sponsorze/mecenasi.txt","/dane/sponsorze/sponsorzy.txt","/dane/sponsorze/wkladtworczy.txt"};
inline constexpr const char* Titles[]={"MECENASI","SPONSORZY","WKŁAD TWÓRCZY I RZECZOWY"};
enum class Status {Ready,Missing,ReadError,TooLarge,TooMany,NoMemory};
struct Category {
    char names[MaxEntries][MaxName+1]{};
    size_t count=0,rejected=0;
    bool loaded=false;
    Status status=Status::Missing;
};
inline bool parse(const char* data,size_t size,Category& out){
    out.count=out.rejected=0;
    if(size>MaxFile){out.status=Status::TooLarge;return false;}
    size_t at=0;
    if(size>=3 && uint8_t(data[0])==0xef && uint8_t(data[1])==0xbb && uint8_t(data[2])==0xbf)at=3;
    while(at<size){
        size_t start=at;while(at<size && data[at]!='\n' && data[at]!='\r')++at;
        size_t end=at;
        if(at<size && data[at]=='\r')++at;
        if(at<size && data[at]=='\n')++at;
        while(start<end && (data[start]==' '||data[start]=='\t'))++start;
        while(end>start && (data[end-1]==' '||data[end-1]=='\t'))--end;
        if(start==end)continue;
        bool valid=end-start<=MaxName;
        for(size_t p=start;valid && p<end;){
            uint32_t cp;size_t n=BSF::utf8(data+p,end-p,cp);
            if(!n || (cp==0xfffd && !(n==3 && uint8_t(data[p])==0xef && uint8_t(data[p+1])==0xbf && uint8_t(data[p+2])==0xbd)) || cp<32 || cp==127){valid=false;break;}
            p+=n;
        }
        if(!valid){++out.rejected;continue;}
        if(out.count==MaxEntries){out.status=Status::TooMany;return false;}
        auto* dst=out.names[out.count++];size_t n=end-start;
        for(size_t i=0;i<n;++i){dst[i]=data[start+i];}
        dst[n]=0;
    }
    out.status=Status::Ready;out.loaded=true;return true;
}
// The reader supplies an exact byte count; the persistent model never borrows
// pointers into its temporary bounded buffer. Categories replace independently.
template<class Size,class Read> bool reload(Category& current,unsigned index,Size size,Read read){
    if(index>=3)return false;
    uint32_t bytes=0;
    if(!size(Paths[index],&bytes)){current.status=Status::Missing;return false;}
    if(bytes>MaxFile){current.status=Status::TooLarge;return false;}
    auto* raw=new char[MaxFile];auto* next=new Category;
    if(!raw||!next){delete[] raw;delete next;current.status=Status::NoMemory;return false;}
    size_t got=0;
    bool ok=!bytes || (read(Paths[index],raw,bytes,&got)&&got==bytes);
    uint32_t after=0;
    if(ok && (!size(Paths[index],&after) || after!=bytes))ok=false;
    if(!ok)current.status=Status::ReadError;
    else if(parse(raw,bytes,*next))current=*next;
    else{current.status=next->status;ok=false;}
    delete[] raw;delete next;return ok;
}
inline const char* message(const Category& c){
    switch(c.status){
    case Status::Missing:return c.loaded?"Nie znaleziono pliku. Zachowano poprzednie wpisy.":"Brak danych. Nie znaleziono pliku danych.";
    case Status::ReadError:return "Błąd odczytu. Zachowano poprzednie wpisy.";
    case Status::TooLarge:return "Plik przekracza 64 KiB. Zachowano poprzednie wpisy.";
    case Status::TooMany:return "Za dużo wpisów (limit 256). Zachowano poprzednią listę.";
    case Status::NoMemory:return "Brak pamięci. Zachowano poprzednie wpisy.";
    default:return !c.count?(c.rejected?"Brak wpisów. Pominięto błędne wiersze.":"Brak wpisów"):c.rejected?"Pominięto błędne lub zbyt długie wiersze.":"Dziękujemy za wsparcie projektu Bursztyn OS.";
    }
}
}
