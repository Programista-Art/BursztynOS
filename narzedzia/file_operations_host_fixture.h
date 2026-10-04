#pragma once
#include "bursztyn_ui_host_fixture.h"
#include <map>
#include <string>
static std::map<std::string,bool> files;
static BwsSchowekPlikow file_clip{};
static bool deny_operation=false;
static int deletes=0, launches=0, checks=0;
static std::string launched_app,launched_file;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}} while(0)

static std::string destination(const char* source,const char* dir) {
    const char* name=strrchr(source,'/');return std::string(dir)+(strcmp(dir,"/")?"/":"")+(name?name+1:source);
}
static bool add_file(const char* path,bool directory) {
    if(deny_operation || files.count(path))return false;
    auto parent=std::string(path).substr(0,std::string(path).rfind('/'));if(parent.empty())parent="/";
    if(!files.count(parent) || !files[parent])return false;
    files[path]=directory;return true;
}
static uint64_t explorer_bws(uint64_t nr,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t) {
    const char* path=reinterpret_cast<const char*>(a1);
    if(nr==46)return add_file(path,true);
    if(nr==6) {
        if(!files.count(path) || !files[path])return 0;
        std::string prefix=std::string(path)+(strcmp(path,"/")?"/":""),listing;
        for(auto& f:files)if(f.first.compare(0,prefix.size(),prefix)==0) {
            auto name=f.first.substr(prefix.size());if(name.empty() || name.find('/')!=std::string::npos)continue;
            listing+=(f.second?"[KAT]  ":"[PLIK] ")+name+"\n";
        }
        if(listing.size()+1>a3)return 0;
        memcpy(reinterpret_cast<void*>(a2),listing.c_str(),listing.size()+1);return 1;
    }
    if(nr==47 && files.count(path)) {
        auto* m=reinterpret_cast<BwsMetadanePliku*>(a2);*m={};m->wersja=BWS_METADANE_WERSJA;
        m->typ=files[path]?BWS_TYP_KATALOG:BWS_TYP_PLIK;m->flagi=BWS_META_ROZMIAR_DOSTEPNY|BWS_META_PZB_DOSTEPNY;return 1;
    }
    return 0;
}
bool utworz(const char* path) {return add_file(path,false);}
bool usun_twor_uzytkownika(const char* path) {
    if(deny_operation || !files.count(path))return false;
    files.erase(path);++deletes;return true;
}
bool zmien_nazwe_uzytkownika(const char* path,const char* name) {
    auto dir=std::string(path).substr(0,std::string(path).rfind('/'));if(dir.empty())dir="/";
    auto target=dir+(dir=="/"?"":"/")+name;
    if(deny_operation || !files.count(path) || files.count(target))return false;
    files[target]=files[path];files.erase(path);return true;
}
bool kopiuj_twor_uzytkownika(const char* path,const char* dir) {
    if(deny_operation || !files.count(path) || !files.count(dir) || !files[dir])return false;
    auto target=destination(path,dir);if(files.count(target))return false;files[target]=files[path];return true;
}
bool przenies_twor_uzytkownika(const char* path,const char* dir) {
    if(!kopiuj_twor_uzytkownika(path,dir))return false;files.erase(path);return true;
}
bool pobierz_schowek_plikow(BwsSchowekPlikow* out) {*out=file_clip;return file_clip.operacja!=BWS_SCHOWEK_PUSTY;}
bool ustaw_schowek_plikow(const char* path,BwsOperacjaSchowka operation) {
    if(!files.count(path))return false;file_clip={};file_clip.wersja=BWS_SCHOWEK_WERSJA;file_clip.generacja=7;
    file_clip.operacja=operation;snprintf(file_clip.sciezka,sizeof(file_clip.sciezka),"%s",path);return true;
}
bool wyczysc_schowek_plikow(uint64_t generation) {if(file_clip.generacja!=generation)return false;file_clip={};return true;}
bool uruchom_program_z_argumentem_uzytkownika(const char* app,const char* path) {++launches;launched_app=app;launched_file=path;return true;}
bool uruchom_program_uzytkownika(const char*) {return true;}
wynik_otwarcia_skojarzonego otworz_plik_skojarzony(const char*) {return OTWORZ_PLIK_URUCHOMIONO;}
bool czytaj(const char*,void*,uint32_t,uint32_t*) {return false;}
bool czytaj_plik(const char*,char*,uint32_t) {return false;}
bool gui_rejestruj_cele_drop(const BwsCelDrop*,uint32_t) {return true;}
int oblicz_szerokosc_tekstu(const char* t,int) {return CzcionkaUI::zmierz_tekst(t,1);}
void gui_rysuj_okno(int,int,int,int,const char*) {}
void gui_wypisz_tekst_kolor(int,int,uint32_t,const char*) {}

