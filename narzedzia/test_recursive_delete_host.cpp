// Real BWS authorization, real PSF traversal/primitive, RAM/faulting BlockDevice.
#include "../syscalls.cpp"
#include "../psf.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
void* kmalloc(size_t n){return std::malloc(n);}
void kfree(void* p){std::free(p);}
void wypisz_log(const char*){}
bool instalator_tryb(){return false;}
bool blokowy_zapis_instalatora(){return false;}
bool pobierz_czas_rtc_bezpiecznie(czas_rtc*){return false;}
bool skopiuj_string_z_uzytkownika(char* d,const char* s,size_t n){if(!s||strlen(s)>=n)return false;strcpy(d,s);return true;}
bool skopiuj_do_przestrzeni_uzytkownika(void* d,const void* s,size_t n){memcpy(d,s,n);return true;}
bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void* p,size_t){return p!=nullptr;}
bool scheduler_dodaj_zdarzenie(int,const bws_zdarzenie*){return true;}
uint64_t scheduler_pobierz_tick(){return 1;}
static unsigned notifications=0,checks=0,fixtures=0;
extern "C" void bws_powiadom_zmiane_pliku(const char*,uint32_t){++notifications;}
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);abort();}}while(0)
static proces_t caller(){proces_t p{};p.poziom_zaufania=PZB_UZYTKOWNIK;p.uprawnienia=PRAWO_PLIKI_ZAPISZ;return p;}
struct Ram {
    std::vector<uint8_t> bytes;
    Ram(size_t size=16*1024*1024):bytes(size+128,0xa5){
        ++fixtures;psf_ustaw_partycje(nullptr);psf_wlacz_translacje_slotow(false);
        inicjalizuj_psf(bytes.data()+64,size);CHECK(psf_czy_gotowy());notifications=0;
    }
    ~Ram(){for(size_t i=0;i<64;++i){CHECK(bytes[i]==0xa5);CHECK(bytes[bytes.size()-64+i]==0xa5);}CHECK(!psf_test_lock_held());psf_ustaw_partycje(nullptr);}
    size_t size()const{return bytes.size()-128;}
    uint8_t* data(){return bytes.data()+64;}
};
static bool exists(const char* path){psf_metadane m{};return pobierz_metadane_tworu(path,&m);}
static WynikPlikowy remove(proces_t& p,const char* path){return WynikPlikowy(bws_usun_twor(p,uint64_t(path),BWS_USUN_DRZEWO));}
static void basic(){
    for(int files:{0,1,10,500}) {
        Ram ram;auto p=caller();CHECK(utworz_katalog("/tree"));
        for(int i=0;i<files;++i) {
            const std::string path="/tree/f"+std::to_string(i);
            CHECK(utworz_plik(path.c_str()));CHECK(zapisz_do_pliku(path.c_str(),"zawartość",11));
        }
        if(files){CHECK(!usun_twor("/tree"));CHECK(!bws_usun_twor(p,uint64_t("/tree")));
            CHECK(bws_usun_twor(p,uint64_t("/tree"),BWS_STATUS_PLIKOWY)==uint64_t(WynikPlikowy::Niepusty));}
        CHECK(remove(p,"/tree")==WynikPlikowy::Ok);CHECK(!exists("/tree"));CHECK(notifications==1);
        CHECK(remove(p,"/tree")==WynikPlikowy::Brak);CHECK(notifications==1);
    }
    for(int depth:{1,2,8,32,33}) {
        Ram ram;auto p=caller();std::string path="/tree";CHECK(utworz_katalog(path.c_str()));
        for(int i=0;i<depth;++i){path+="/d";CHECK(utworz_katalog(path.c_str()));}
        auto crc=Storage::crc32(ram.data(),ram.size());
        auto result=remove(p,"/tree");
        if(depth==33){CHECK(result==WynikPlikowy::Limit);CHECK(exists(path.c_str()));CHECK(Storage::crc32(ram.data(),ram.size())==crc);CHECK(notifications==0);}
        else {CHECK(result==WynikPlikowy::Ok);CHECK(!exists("/tree"));CHECK(notifications==1);}
    }
    {Ram ram;auto p=caller();CHECK(utworz_katalog("/tree"));CHECK(utworz_katalog("/tree/żółć"));
        CHECK(utworz_katalog("/tree/żółć/nested"));CHECK(utworz_plik("/tree/żółć/nested/Zażółć.txt"));
        std::string longest(55,'a');CHECK(utworz_plik(("/tree/"+longest).c_str()));CHECK(utworz_plik("/outside"));
        CHECK(remove(p,"//tree///")==WynikPlikowy::Ok);CHECK(!exists("/tree"));CHECK(exists("/outside"));CHECK(notifications==1);
        CHECK(bws_usun_twor(p,uint64_t("/outside"))==1);CHECK(!exists("/outside"));}
}
static void policies(){
    {Ram ram;auto p=caller();CHECK(utworz_katalog("/tree"));CHECK(utworz_plik("/tree/a"));CHECK(utworz_plik_z_pzb("/tree/protected",PZB_JADRO));
        auto crc=Storage::crc32(ram.data(),ram.size());CHECK(remove(p,"/tree")==WynikPlikowy::BrakUprawnien);
        CHECK(Storage::crc32(ram.data(),ram.size())==crc);CHECK(exists("/tree/a"));CHECK(notifications==0);}
    {Ram ram;auto p=caller();CHECK(utworz_katalog("/tree"));p.uprawnienia=0;
        CHECK(remove(p,"/tree")==WynikPlikowy::BrakUprawnien);CHECK(exists("/tree"));CHECK(notifications==0);}
    {Ram ram;auto p=caller();CHECK(utworz_katalog_z_pzb("/ustawienia",PZB_USLUGI));CHECK(utworz_plik("/ustawienia/layout"));
        CHECK(remove(p,"/ustawienia")==WynikPlikowy::BrakUprawnien);CHECK(exists("/ustawienia/layout"));}
    {Ram ram;for(const char* path:{"/system","/jadro","/sterowniki","/programy","/sloty"})CHECK(utworz_katalog(path));
        for(uint8_t trust:{uint8_t(PZB_JADRO),uint8_t(PZB_USLUGI),uint8_t(PZB_UZYTKOWNIK),uint8_t(PZB_PIASKOWNICA)}) {
            auto p=caller();p.poziom_zaufania=trust;
            for(const char* path:{"/system","/jadro","/sterowniki","/programy","/sloty","/"})CHECK(remove(p,path)==WynikPlikowy::Chroniony);
        }
        CHECK(notifications==0);}
    {Ram ram;auto p=caller();CHECK(utworz_katalog("/tree"));CHECK(utworz_plik("/outside"));
        for(const char* path:{"/tree/../outside","/tree/./x","relative","/tree/..","/tree/\\outside"})
            CHECK(remove(p,path)==WynikPlikowy::NieprawidlowaNazwa);
        CHECK(exists("/outside"));CHECK(exists("/tree"));CHECK(notifications==0);}
    // Explicit reauthorization during execution can fail after another child.
    {Ram ram;CHECK(utworz_katalog("/tree"));CHECK(utworz_plik("/tree/a"));CHECK(utworz_plik("/tree/b"));
        struct Context {proces_t p=caller();unsigned a=0;std::vector<std::string> paths;} ctx;
        auto auth=[](const char* path,const psf_metadane& m,const psf_metadane& parent,void* ptr){
            auto& c=*static_cast<Context*>(ptr);c.paths.emplace_back(path);
            if(!strcmp(path,"/tree/a") && ++c.a==2)return WynikPlikowy::BrakUprawnien;
            return autoryzacja_drzewa(path,m,parent,&c.p);
        };
        CHECK(psf_usun_drzewo("/tree",auth,&ctx)==WynikPlikowy::Czesciowo);
        CHECK(exists("/tree/a"));CHECK(!exists("/tree/b"));CHECK(exists("/tree"));CHECK(ctx.a==2);}
}
static void corruption(){
    for(unsigned fault=0;fault<7;++fault) {
        Ram ram;auto p=caller();CHECK(utworz_katalog("/tree"));CHECK(utworz_plik("/tree/a"));CHECK(utworz_katalog("/outside"));
        const auto tree=rozwiaz_sciezke("/tree",nullptr,false),child=rozwiaz_sciezke("/tree/a",nullptr,false);
        auto* dir=pobierz_wezel(tree);auto* entries=pobierz_wpisy_katalogowe(dir->wskazniki_blokow[0]);
        if(fault==0){entries[1]=entries[0];++dir->rozmiar_w_bajtach;}
        if(fault==1){entries[0].id_wezla=dysk_superblok->id_korzenia;}
        if(fault==2){CHECK(dodaj_wpis_do_katalogu(rozwiaz_sciezke("/outside",nullptr,false),child,"alias"));}
        if(fault==3){entries[0].nazwa[0]='.';entries[0].nazwa[1]='.';entries[0].nazwa[2]=0;}
        if(fault==4){CHECK(utworz_plik("/tree/b"));entries[1].nazwa[0]='a';}
        if(fault==5){pobierz_wezel(child)->wskazniki_blokow[0]=dir->wskazniki_blokow[0];}
        if(fault==6){pobierz_wezel(child)->wskazniki_blokow[0]=pobierz_wezel(rozwiaz_sciezke("/",nullptr,false))->wskazniki_blokow[0];}
        auto crc=Storage::crc32(ram.data(),ram.size());CHECK(remove(p,"/tree")==WynikPlikowy::Uszkodzony);
        CHECK(Storage::crc32(ram.data(),ram.size())==crc);CHECK(exists("/outside"));CHECK(notifications==0);
    }
    {Ram ram;auto p=caller();CHECK(utworz_katalog("/tree"));CHECK(utworz_plik("/tree/a"));
        pobierz_wezel(rozwiaz_sciezke("/tree/a",nullptr,false))->typ=TYP_WOLNY;
        CHECK(remove(p,"/tree")==WynikPlikowy::Uszkodzony);CHECK(exists("/tree"));CHECK(notifications==0);}
}
struct Disk {
    std::vector<uint8_t> bytes;Storage::Device dev{};
    uint64_t fail_block=UINT64_MAX;unsigned flushes=0,fail_flush=0;
    Disk():bytes(20*1024*1024,0x5a){dev.driver_data=this;dev.ops=&ops;dev.total_sectors=bytes.size()/512;dev.device_type=BlockDeviceType::VirtIO;}
    static BlockError rd(BlockDevice* d,uint64_t lba,uint32_t n,void* out){auto& s=*static_cast<Disk*>(d->driver_data);memcpy(out,s.bytes.data()+lba*512,n*512);return BlockError::Ok;}
    static BlockError wr(BlockDevice* d,uint64_t lba,uint32_t n,const void* in){auto& s=*static_cast<Disk*>(d->driver_data);
        const uint64_t fail=128+s.fail_block*8;if(s.fail_block!=UINT64_MAX && lba<=fail && fail<lba+n)return BlockError::IOError;
        memcpy(s.bytes.data()+lba*512,in,n*512);return BlockError::Ok;}
    static BlockError fl(BlockDevice* d){auto& s=*static_cast<Disk*>(d->driver_data);return ++s.flushes==s.fail_flush?BlockError::Timeout:BlockError::Ok;}
    static const BlockOperations ops;
    void mount(Ram& ram){auto partition=Storage::Partition{&dev,128,ram.size()/512};psf_ustaw_partycje(&partition);nosnik_blokowy_dostepny=true;
        CHECK(sformatuj_ram_dysk(true));inicjalizuj_psf(ram.data(),ram.size());CHECK(psf_czy_gotowy());CHECK(nosnik_blokowy_dostepny);}
};
const BlockOperations Disk::ops={Disk::rd,Disk::wr,Disk::fl,nullptr};
static void io(){
    for(int fault:{0,1,2}) {
        Ram ram;Disk disk;disk.mount(ram);auto p=caller();CHECK(utworz_katalog("/tree"));CHECK(utworz_plik("/tree/a"));
        if(fault==0)disk.fail_block=dysk_superblok->start_wezlow+(rozwiaz_sciezke("/tree/a",nullptr,false)-1)/wezlow_na_blok();
        else disk.fail_flush=disk.flushes+(fault==1?1:2);
        notifications=0;CHECK(remove(p,"/tree")==WynikPlikowy::Czesciowo);CHECK(notifications==1);
        CHECK(!exists("/tree/a"));CHECK(fault==2?!exists("/tree"):exists("/tree"));
    }
}
static void resource_bound(){
    Ram ram(128*1024*1024);
    // Valid BSP2 geometry with more inodes than the formatter's default 1024.
    // Format is unchanged: move its empty data region before creating fixtures.
    dysk_superblok->start_danych=dysk_superblok->start_wezlow+8192;
    calkowita_liczba_wezlow=8192;
    calkowita_liczba_blokow_danych=dysk_superblok->ilosc_blokow-dysk_superblok->start_danych;
    wyzeruj_pamiec(ram.data()+dysk_superblok->start_wezlow*PSF_ROZMIAR_BLOKU,8192*PSF_ROZMIAR_BLOKU);
    wyzeruj_pamiec(ram.data()+PSF_ROZMIAR_BLOKU,PSF_ROZMIAR_BLOKU);
    for(uint64_t id=1;id<=calkowita_liczba_wezlow;++id)wyzeruj_wezel(pobierz_wezel(id),TYP_WOLNY);
    dysk_superblok->id_korzenia=zaalokuj_wolny_wezel(TYP_KATALOG);CHECK(dysk_superblok->id_korzenia!=0);
    auto p=caller();CHECK(utworz_katalog("/tree"));
    // Spread the bounded plan across directories; one BSP2 directory has a
    // smaller fixed entry capacity than the whole recursive operation.
    for(unsigned d=0;d<8;++d)CHECK(utworz_katalog(("/tree/d"+std::to_string(d)).c_str()));
    const uint32_t files=PSF_USUN_MAX_WEZLOW-8;
    for(uint32_t i=0;i<files;++i)CHECK(utworz_plik(("/tree/d"+std::to_string(i/512)+"/f"+std::to_string(i)).c_str()));
    CHECK(remove(p,"/tree")==WynikPlikowy::Limit);CHECK(exists("/tree/d7/f4087"));CHECK(notifications==0);
    CHECK(usun_twor("/tree/d7/f4087"));CHECK(remove(p,"/tree")==WynikPlikowy::Ok);CHECK(!exists("/tree"));CHECK(notifications==1);
}
static void create_status(){Ram ram;auto p=caller();
    CHECK(bws_utworz_katalog(p,uint64_t("/folder"),BWS_STATUS_PLIKOWY)==uint64_t(WynikPlikowy::Ok));
    CHECK(bws_utworz_katalog(p,uint64_t("/folder"),BWS_STATUS_PLIKOWY)==uint64_t(WynikPlikowy::Istnieje));
    CHECK(bws_utworz_plik(p,uint64_t("/folder/file"),BWS_STATUS_PLIKOWY)==uint64_t(WynikPlikowy::Ok));
    CHECK(bws_utworz_plik(p,uint64_t("/folder/file"),BWS_STATUS_PLIKOWY)==uint64_t(WynikPlikowy::Istnieje));
    CHECK(psf_utworz_wynik("/folder/..",false,PZB_UZYTKOWNIK)==WynikPlikowy::NieprawidlowaNazwa);
    CHECK(psf_utworz_wynik("/missing/file",false,PZB_UZYTKOWNIK)==WynikPlikowy::Brak);
    for(uint64_t id=1;id<=calkowita_liczba_wezlow;++id)if(pobierz_wezel(id)->typ==TYP_WOLNY)pobierz_wezel(id)->typ=TYP_PLIK;
    CHECK(psf_utworz_wynik("/full",false,PZB_UZYTKOWNIK)==WynikPlikowy::BrakMiejsca);
    p.uprawnienia=0;CHECK(bws_utworz_katalog(p,uint64_t("/denied"),BWS_STATUS_PLIKOWY)==uint64_t(WynikPlikowy::BrakUprawnien));
}
int main(){basic();policies();corruption();io();resource_bound();create_status();
    printf("Recursive delete / PZB / PSF / I/O: %u/%u PASS (%u fixtures)\n",checks,checks,fixtures);
}
