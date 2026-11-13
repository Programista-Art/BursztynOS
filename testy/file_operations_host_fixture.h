#pragma once
#include "bursztyn_ui_host_fixture.h"
#include <map>
#include <string>
static std::map<std::string,bool> files;
static BwsSchowekPlikow file_clip{};
static BwsSchowekPlikow file_clip_items[BWS_SCHOWEK_MAX_PLIKOW]{};
static uint64_t fixture_clip_generation=7;
static bool deny_operation=false;
static std::string deny_path;
static int deletes=0, launches=0, checks=0;
static std::string launched_app,launched_file;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}} while(0)

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
    if(nr==10){++launches;launched_app=path;return 1;}
    // Generic file-action fixture models no settings storage; dedicated layout
    // tests install an I/O hook, including capture ordering and failure modes.
    if(nr==46 && !strcmp(path,"/ustawienia"))return 0;
    if(nr==2 || nr==46) {
        if(a2==BWS_STATUS_PLIKOWY) {
            if(files.count(path))return uint64_t(WynikPlikowy::Istnieje);
            if(deny_operation)return uint64_t(WynikPlikowy::BrakUprawnien);
        }
        return add_file(path,nr==46);
    }
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
    if(deny_operation || deny_path==path || !files.count(path))return false;
    files.erase(path);++deletes;return true;
}
WynikPlikowy usun_drzewo_uzytkownika(const char* path) {
    if(!files.count(path))return WynikPlikowy::Brak;
    if(deny_operation || deny_path==path)return WynikPlikowy::BrakUprawnien;
    std::string prefix=std::string(path)+"/";
    for(auto& f:files)if(f.first.compare(0,prefix.size(),prefix)==0 && f.first==deny_path)
        return WynikPlikowy::BrakUprawnien;
    for(auto i=files.begin();i!=files.end();) {
        if(i->first==path || i->first.compare(0,prefix.size(),prefix)==0){i=files.erase(i);++deletes;}
        else ++i;
    }
    return WynikPlikowy::Ok;
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
    if(deny_path==path || !kopiuj_twor_uzytkownika(path,dir))return false;files.erase(path);return true;
}
bool pobierz_schowek_plikow(BwsSchowekPlikow* out) {*out=file_clip;return file_clip.operacja!=BWS_SCHOWEK_PUSTY;}
bool ustaw_schowek_plikow(const char* path,BwsOperacjaSchowka operation) {
    if(!files.count(path))return false;file_clip={};file_clip.wersja=BWS_SCHOWEK_WERSJA;file_clip.generacja=++fixture_clip_generation;file_clip.zarezerwowane=1;
    file_clip.operacja=operation;snprintf(file_clip.sciezka,sizeof(file_clip.sciezka),"%s",path);file_clip_items[0]=file_clip;return true;
}
bool wyczysc_schowek_plikow(uint64_t generation) {if(file_clip.generacja!=generation)return false;file_clip={};return true;}
bool ustaw_schowek_wielu_plikow(const char (*paths)[BWS_DROP_SCIEZKA_MAX],uint32_t count,BwsOperacjaSchowka op) {
    if(!count || count>BWS_SCHOWEK_MAX_PLIKOW || deny_operation)return false;
    for(uint32_t i=0;i<count;++i)if(!files.count(paths[i]))return false;
    ++fixture_clip_generation;
    for(uint32_t i=0;i<count;++i) {
        auto& r=file_clip_items[i];r={};r.wersja=BWS_SCHOWEK_WERSJA;r.operacja=op;r.generacja=fixture_clip_generation;r.zarezerwowane=count;
        snprintf(r.sciezka,sizeof(r.sciezka),"%s",paths[i]);
    }
    file_clip=file_clip_items[0];return true;
}
bool pobierz_element_schowka_plikow(BwsSchowekPlikow* out,uint32_t i,uint64_t gen) {
    if(gen!=file_clip.generacja || i>=file_clip.zarezerwowane)return false;*out=file_clip_items[i];return true;
}
bool usun_prefiks_schowka_plikow(uint64_t gen,uint32_t count) {
    if(gen!=file_clip.generacja || count>file_clip.zarezerwowane)return false;
    unsigned left=file_clip.zarezerwowane-count;if(!left)return wyczysc_schowek_plikow(gen);
    ++fixture_clip_generation;
    for(unsigned i=0;i<left;++i){file_clip_items[i]=file_clip_items[i+count];file_clip_items[i].zarezerwowane=left;file_clip_items[i].generacja=fixture_clip_generation;}
    file_clip=file_clip_items[0];return true;
}
bool uruchom_program_z_argumentem_uzytkownika(const char* app,const char* path) {++launches;launched_app=app;launched_file=path;return true;}
bool uruchom_program_uzytkownika(const char* path) {++launches;launched_app=path;return true;}
uint8_t pobierz_blad_uruchomienia() {return 0;}
wynik_otwarcia_skojarzonego otworz_plik_skojarzony(const char*) {return OTWORZ_PLIK_URUCHOMIONO;}
bool czytaj(const char*,void*,uint32_t,uint32_t*) {return false;}
bool czytaj_plik(const char*,char*,uint32_t) {return false;}
bool gui_rejestruj_cele_drop(const BwsCelDrop*,uint32_t) {return true;}
int oblicz_szerokosc_tekstu(const char* t,int scale) {return CzcionkaUI::zmierz_tekst(t,SkalaUI::procent(scale));}
void gui_rysuj_okno(int,int,int,int,const char*) {}
void gui_wypisz_tekst_kolor(int,int,uint32_t,const char*) {}

