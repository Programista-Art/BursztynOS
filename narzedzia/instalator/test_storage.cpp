// Host exercises the production partition/GPT/FAT and BSP2 code with a faulting backend.
#include "../../storage_format.h"
#include "../../psf.h"
#include "../../zegar-rtc.h"
#include "../../instalator_format.h"
#include "../../tapety.h"
#include <array>
#include <map>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cassert>
using namespace Storage;
void* kmalloc(size_t n){return std::malloc(n);}
void wypisz_log(const char*){}
bool pobierz_czas_rtc_bezpiecznie(czas_rtc*){return false;}
bool authorized=true;
bool blokowy_zapis_instalatora(){return authorized;}
struct Fake {
    std::map<uint64_t,std::array<uint8_t,512>> data;
    uint64_t calls=0,sectors=0,fail_after=UINT64_MAX,low=0,high=UINT64_MAX;
    bool flush_fail=false,read_fail=false;
    Device dev;
    explicit Fake(uint64_t n):dev{this,n,512,true,rd,wr,fl}{}
    static bool rd(void*c,uint64_t s,uint32_t n,void*b){auto&f=*(Fake*)c;
        if(f.read_fail||!range(s,n,f.dev.sectors))return false;
        for(unsigned i=0;i<n;++i){auto it=f.data.find(s+i);if(it==f.data.end())std::memset((uint8_t*)b+512*i,0,512);else std::memcpy((uint8_t*)b+512*i,it->second.data(),512);}return true;}
    static bool wr(void*c,uint64_t s,uint32_t n,const void*b){auto&f=*(Fake*)c;
        assert(range(s,n,f.dev.sectors));assert(s>=f.low&&n<=f.high-s);
        if(f.calls++>=f.fail_after)return false;
        f.sectors+=n;for(unsigned i=0;i<n;++i)std::memcpy(f.data[s+i].data(),(const uint8_t*)b+512*i,512);return true;}
    static bool fl(void*c){return !((Fake*)c)->flush_fail;}
};
unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
int main(){
    CHECK(!instalator_potwierdzenie_poprawne("",16));
    CHECK(!instalator_potwierdzenie_poprawne("B",16));
    CHECK(!instalator_potwierdzenie_poprawne("BU",16));
    CHECK(!instalator_potwierdzenie_poprawne("BURSZT",16));
    CHECK(instalator_potwierdzenie_poprawne("BURSZTYN",16));
    CHECK(!instalator_potwierdzenie_poprawne("BURSZTY",16));
    CHECK(!instalator_potwierdzenie_poprawne("bursztyn",16));
    CHECK(!instalator_potwierdzenie_poprawne("BURSZTYNX",16));
    CHECK(!instalator_mozna_zainstalowac(true,true,false,true,false));
    CHECK(!instalator_mozna_zainstalowac(true,true,true,true,false));
    CHECK(!instalator_mozna_zainstalowac(true,true,false,true,true));
    CHECK(instalator_mozna_zainstalowac(true,true,true,true,true));
    CHECK(!instalator_mozna_zainstalowac(false,true,true,true,true));
    CHECK(!instalator_mozna_zainstalowac(true,false,true,true,true));
    CHECK(instalator_mozna_zainstalowac(true,true,true,false,false));
    CHECK(instalator_postep_nowy(0,0,false)==0);
    CHECK(instalator_postep_nowy(45,30,false)==45);
    CHECK(instalator_postep_nowy(99,150,false)==99);
    CHECK(instalator_postep_nowy(99,100,true)==100);
    CHECK(instalator_postep_nowy(63,100,false)==99);
    CHECK(instalator_etap_postepu_poprawny(POSTEP_GPT,POSTEP_ESP));
    CHECK(!instalator_etap_postepu_poprawny(POSTEP_TAPETY,POSTEP_SYSTEM));
    CHECK(instalator_podpostep(45,75,0,13)==45);
    CHECK(instalator_podpostep(45,75,7,13)>=45&&instalator_podpostep(45,75,7,13)<=75);
    CHECK(instalator_podpostep(75,92,12,18)>=75&&instalator_podpostep(75,92,12,18)<=92);
    CHECK(instalator_podpostep(75,92,1,0)==75);
    CHECK(std::strcmp(tapety_komunikat_braku_pakietu(true,true),
        "[TAPETA] Brak tapety.pak w trybie zainstalowanym; uzywam zasobow BSP2.")==0);
    CHECK(std::strstr(tapety_komunikat_braku_pakietu(false,true),"Ostrzezenie")!=nullptr);
    CHECK(crc32("123456789",9)==0xCBF43926);
    Fake disk(4ULL*1024*1024*1024/512);Layout l{};Table t{};uint8_t uuid[16]={1,2,3,4,5,6,7,0x40,0x80,10,11,12,13,14,15,16};
    CHECK(plan(&disk.dev,&l));CHECK(l.esp.start==2048&&l.esp.count==1048576&&l.bsp.start%2048==0&&l.bsp.count==BspMaxBytes/512);
    CHECK(!gpt_read(&disk.dev,&t));CHECK(disk.calls==0);CHECK(!source_iso(&disk.dev));
    Fake small(10000),large((6ULL*1024*1024*1024*1024)/512);Layout big{};
    CHECK(!plan(&small.dev,&big));CHECK(plan(&large.dev,&big));CHECK(big.unallocated>UINT32_MAX);
    CHECK(gpt_write(&large.dev,big,uuid));CHECK(gpt_read(&large.dev,&t));CHECK(large.data.count(large.dev.sectors-1)==1);
    disk.dev.sector_size=4096;CHECK(!plan(&disk.dev,&big));CHECK(!gpt_read(&disk.dev,&t));disk.dev.sector_size=512;
    CHECK(gpt_write(&disk.dev,l,uuid));CHECK(gpt_read(&disk.dev,&t));CHECK(t.count==2&&equal(t.entries[1].type,BspType,16));
    GptInstallState gpt_state{};CHECK(gpt_install_start(&disk.dev,l,uuid,&gpt_state));
    uint32_t gpt_poprzednio=0;
    while(!gpt_state.complete){
        const uint64_t calls_before=disk.calls;
        CHECK(gpt_install_step(&gpt_state));
        CHECK(disk.calls-calls_before<=1);
        CHECK(gpt_state.completed_units==gpt_poprzednio+1||gpt_state.complete);
        CHECK(gpt_state.completed_units<=gpt_state.total_units);
        gpt_poprzednio=gpt_state.completed_units;
    }
    CHECK(gpt_state.completed_units==gpt_state.total_units);CHECK(gpt_read(&disk.dev,&t));
    const auto original=disk.data;uint64_t before=disk.calls;
    disk.data[1][16]^=1;CHECK(!gpt_read(&disk.dev,&t));CHECK(disk.calls==before);disk.data=original;
    disk.data[2][40]^=1;CHECK(!gpt_read(&disk.dev,&t));disk.data=original;
    disk.data[disk.dev.sectors-1][16]^=1;CHECK(!gpt_read(&disk.dev,&t));disk.data=original;
    uint8_t buf[4096]{};
    CHECK(!write(l.bsp,l.bsp.count,1,buf));CHECK(!write(l.bsp,UINT64_MAX,1,buf));CHECK(!write(l.bsp,0,0,buf));CHECK(!write(l.bsp,0,1,nullptr));
    Partition overflow{&disk.dev,UINT64_MAX-5,16};CHECK(!write(overflow,0,1,buf));CHECK(disk.calls==before);
    Layout invalid=l;invalid.bsp.start=UINT64_MAX;CHECK(!gpt_write(&disk.dev,invalid,uuid));CHECK(disk.calls==before);
    BootFile f{buf,4096};Partition missing=l.esp;missing.count=8192;
    CHECK(!fat_install(missing,f,f,f));CHECK(!fat_install(l.esp,{},f,f));CHECK(disk.calls==before);
    disk.fail_after=disk.calls;CHECK(!fat_install(l.esp,f,f,f));disk.fail_after=UINT64_MAX;
    FatInstallState fat_state{};
    CHECK(fat_install_start(l.esp,f,f,f,&fat_state));CHECK(fat_state.total_units>100);
    uint32_t fat_poprzednio=0;
    while(!fat_state.complete){
        CHECK(fat_install_step(&fat_state));
        CHECK(fat_state.completed_units==fat_poprzednio+1);
        CHECK(fat_state.completed_units<=fat_state.total_units);
        fat_poprzednio=fat_state.completed_units;
    }
    CHECK(fat_state.completed_units==fat_state.total_units);
    CHECK(fat_install(l.esp,f,f,f));CHECK(disk.data[l.esp.start][82]=='F');
    CHECK(gpt_write(&disk.dev,l,uuid));CHECK(gpt_read(&disk.dev,&t)); // reinstall GPT
    // Exact production BSP2 formatting, with sentinels immediately outside partition.
    auto&left=disk.data[l.bsp.start-1];left.fill(0xA7);auto&right=disk.data[l.bsp.start+l.bsp.count];right.fill(0x5D);
    const auto outside=disk.data;
    disk.low=l.bsp.start;disk.high=l.bsp.start+l.bsp.count;
    std::vector<uint8_t> ram(16*1024*1024);psf_ustaw_partycje(nullptr);before=disk.calls;
    inicjalizuj_psf(ram.data(),ram.size());CHECK(disk.calls==before);CHECK(psf_czy_gotowy());
    CHECK(utworz_plik("/ram"));std::vector<char> bytes(2*1024*1024,'R');
    CHECK(zapisz_do_pliku("/ram",bytes.data(),bytes.size()));char first=0;uint32_t got=0;
    CHECK(czytaj_fragment_pliku("/ram",0,&first,1,&got)&&first=='R');CHECK(disk.calls==before);
    authorized=false;CHECK(!psf_formatuj_instalacje(l.bsp));authorized=true;
    disk.fail_after=disk.calls;CHECK(!psf_formatuj_instalacje(l.bsp));disk.fail_after=UINT64_MAX;
    CHECK(psf_formatuj_instalacje(l.bsp));CHECK(psf_znacznik_instalacji(uuid,false));CHECK(!complete(l.bsp,uuid));
    CHECK(utworz_plik("/system"));CHECK(zapisz_do_pliku("/system",bytes.data(),bytes.size()));CHECK(psf_weryfikuj_dysk());
    CHECK(czytaj_fragment_pliku("/system",0,&first,1,&got)&&first=='R');
    CHECK(psf_znacznik_instalacji(uuid,true));CHECK(complete(l.bsp,uuid));
    CHECK(psf_weryfikuj_dysk());CHECK(complete(l.bsp,uuid));
    for(const auto&item:disk.data)if(item.first<l.bsp.start||item.first>=l.bsp.start+l.bsp.count){CHECK(outside.count(item.first));CHECK(outside.at(item.first)==item.second);}
    CHECK(left==outside.at(l.bsp.start-1));CHECK(right==outside.at(l.bsp.start+l.bsp.count));
    disk.data[l.bsp.start+l.bsp.count-1][100]^=1;CHECK(!complete(l.bsp,uuid));CHECK(psf_znacznik_instalacji(uuid,true));
    disk.flush_fail=true;CHECK(!psf_flush_cache());disk.flush_fail=false;
    uint64_t format_done=0,format_total=0,format_previous=0;bool format_complete=false;
    CHECK(psf_formatuj_instalacje_start(l.bsp,&format_total));CHECK(format_total>0);
    while(!format_complete){
        const uint64_t calls_before=disk.calls;
        CHECK(psf_formatuj_instalacje_step(&format_done,&format_complete));
        CHECK(disk.calls-calls_before<=1);
        CHECK(format_done==format_previous+1);
        CHECK(format_done<=format_total);
        format_previous=format_done;
    }
    CHECK(format_done==format_total);CHECK(psf_czy_gotowy());
    CHECK(rozmiar_pliku("/system")==0); // no stale metadata after reinstall
    CHECK(utworz_plik("/bad"));disk.fail_after=disk.calls;CHECK(!zapisz_do_pliku("/bad",bytes.data(),bytes.size()));disk.fail_after=UINT64_MAX;

    // Testy A/B: uklad ESP, jadro B, grubenv i sektory stanu rozruchu
    disk.low=0; disk.high=UINT64_MAX;
    EspLayout el{};
    CHECK(esp_odczytaj_uklad(l.esp, &el));
    CHECK(el.jadro_a_lba > l.esp.start && el.jadro_b_lba > el.jadro_a_lba);
    CHECK(el.grubenv_lba > el.jadro_b_lba);

    char potw[16]{}, oczek[16]{}; uint32_t proby = 99;
    CHECK(esp_odczytaj_grubenv(l.esp, potw, oczek, &proby));
    CHECK(potw[0] == 'A' && std::strcmp(oczek, "BRAK") == 0 && proby == 0);

    CHECK(esp_zapisz_grubenv(l.esp, "A", "B", 1));
    CHECK(esp_odczytaj_grubenv(l.esp, potw, oczek, &proby));
    CHECK(potw[0] == 'A' && std::strcmp(oczek, "B") == 0 && proby == 1);

    std::vector<uint8_t> new_kernel(4096, 0x5A);
    CHECK(esp_zapisz_jadro_slotu(l.esp, 1, new_kernel.data(), uint32_t(new_kernel.size())));
    std::vector<uint8_t> read_kernel(4096, 0);
    uint32_t odczytano_k = 0;
    CHECK(esp_odczytaj_jadro_slotu(l.esp, 1, read_kernel.data(), uint32_t(read_kernel.size()), &odczytano_k));
    CHECK(odczytano_k == 4096 && read_kernel == new_kernel);

    bursztyn_rozruch_stan rstate{};
    rozruch_stan_poczatkowy(&rstate, 0, 1, 0);
    CHECK(rozruch_zapisz_sektory(&disk.dev, &rstate));
    bursztyn_rozruch_stan rread{};
    CHECK(rozruch_odczytaj_sektory(&disk.dev, &rread));
    CHECK(rread.slot_potwierdzony == BURSZTYN_SLOT_A && rread.generacja == 1);

    std::printf("Storage/BSP2 host: %u PASS, 0 FAIL (includes outside-partition sentinel, faults, >2 TiB GPT, A/B ESP)\n",checks);
}
