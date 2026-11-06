// Real PSF internals for bounded fault fixtures; no copies of bootstrap logic.
#include "../jadro/fs/psf.cpp"
#include "psf_bootstrap.h"
#include "aktualizacja.h"
#include "desktop_entries.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

static unsigned checks=0, scenarios=0, boot_device_lookups=0;
static bool installed=false;
static std::vector<std::string> logs;
void* kmalloc(size_t n){return std::malloc(n);}
void wypisz_log(const char* s){logs.emplace_back(s);}
bool pobierz_czas_rtc_bezpiecznie(czas_rtc*){return false;}
bool blokowy_zapis_instalatora(){return false;}
bool instalator_system_zainstalowany(){return installed;}
Storage::Device* blokowy_dysk_systemowy(){++boot_device_lookups;return nullptr;}
bool blokowy_esp_systemowy(Storage::Partition*){++boot_device_lookups;return false;}
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static FILE* report=nullptr;

struct Ram {
    static constexpr size_t bytes=16*1024*1024, pad=64;
    std::vector<uint8_t> area;
    Ram():area(bytes+2*pad,0xa5) {
        ++scenarios;psf_ustaw_partycje(nullptr);psf_wlacz_translacje_slotow(true);psf_ustaw_aktywny_slot(0);
        psf_test_bootstrap_brak_bloku=false;
        inicjalizuj_psf(area.data()+pad,bytes);
        CHECK(psf_czy_gotowy());CHECK(dysk_superblok->id_korzenia==1);
        CHECK(pobierz_wezel(1)->typ==TYP_KATALOG);CHECK(pobierz_wezel(1)->rozmiar_w_bajtach==0);
    }
    ~Ram(){
        for(size_t i=0;i<pad;++i){CHECK(area[i]==0xa5);CHECK(area[pad+bytes+i]==0xa5);}
        CHECK(stan_startu==nullptr);CHECK(!psf_test_lock_held());
        psf_ustaw_partycje(nullptr);
    }
    uint8_t* data(){return area.data()+pad;}
};
static bool step(const char* path,PsfStanStartu& s){
    bool ok=zapewnij_katalog_startowy(path,&s);
    CHECK(!psf_test_lock_held());CHECK(stan_startu==nullptr);
    return ok;
}
static void tree(const char* scenario,bool trace=false){
    for(size_t i=0;i<LICZBA_KATALOGOW_SYSTEMOWYCH;++i){
        PsfStanStartu s{};CHECK(step(KATALOGI_SYSTEMOWE[i],s));CHECK(s.blad==PsfBladStartu::Ok);
        CHECK(katalog_istnieje(KATALOGI_SYSTEMOWE[i]));CHECK(s.gotowy);
        if(trace){
            const auto before=logs.size();psf_start_log(i,KATALOGI_SYSTEMOWE[i],!installed,s);
            if(BURSZTYNDIAGPSFBOOTSTRAP){if(report)std::fprintf(report,"%s %s\n",scenario,logs.back().c_str());}
            else CHECK(logs.size()==before);
        }
    }
}
static void legacy_failure(){
    Ram ram;size_t failure=LICZBA_KATALOGOW_SYSTEMOWYCH;
    for(size_t i=0;i<LICZBA_KATALOGOW_SYSTEMOWYCH;++i){
        char tiny[1]{};
        bool old=wylistuj_katalog(KATALOGI_SYSTEMOWE[i],tiny,1);
        if(!old && (!utworz_katalog(KATALOGI_SYSTEMOWE[i]) || !wylistuj_katalog(KATALOGI_SYSTEMOWE[i],tiny,1))){failure=i;break;}
    }
    CHECK(failure==18);CHECK(!std::strcmp(KATALOGI_SYSTEMOWE[failure],"/system"));
    CHECK(rozwiaz_sciezke("/system",nullptr,false)==rozwiaz_sciezke("/sloty/a/system",nullptr,false));
    auto* w=pobierz_wezel(rozwiaz_sciezke("/system",nullptr,false));CHECK(w->typ==TYP_KATALOG);CHECK(w->rozmiar_w_bajtach==5);
    CHECK(!utworz_katalog("/system"));
    PsfStanStartu s{};CHECK(step("/system",s));CHECK(s.istnial);CHECK(s.tworzenie==PsfEtapStartu::NieProbowano);
    CHECK(s.wezel==5);CHECK(s.rodzic==3);CHECK(s.wpisyRodzica==2);CHECK(s.blad==PsfBladStartu::Ok);
    if(BURSZTYNDIAGPSFBOOTSTRAP)CHECK(!strcmp(s.sciezkaFizyczna,"/sloty/a/system"));
    char tiny[1]{};CHECK(!wylistuj_katalog("/system",tiny,1)); // preserve complete-list contract
}
static void live_boot(){
    Ram ram;logs.clear();boot_device_lookups=0;
    const auto before=Storage::crc32(ram.data(),Ram::bytes);
    aktualizacja_sprawdz_rozruch(0); // Actual Live early return; no A/B disk reads.
    CHECK(boot_device_lookups==0);CHECK(psf_czy_gotowy());CHECK(before==Storage::crc32(ram.data(),Ram::bytes));
    CHECK(std::any_of(logs.begin(),logs.end(),[](const std::string& s){return s.find("Pominiecie walidacji partycji A/B LBA 2046/2047")!=std::string::npos;}));
    tree("fresh-Live",true);
    aktualizacja_przygotuj_system_plikow();
    CHECK(czy_katalog_istnieje("/ustawienia"));CHECK(czy_katalog_istnieje("/uzytkownicy/Pulpit"));
    tree("duplicate-Live",true);
    // All paths exist, including nonempty slot aliases; CREATE still rejects duplicates.
    for(const char* path:KATALOGI_SYSTEMOWE){PsfStanStartu s{};CHECK(step(path,s));CHECK(s.istnial);CHECK(s.tworzenie==PsfEtapStartu::NieProbowano);CHECK(!utworz_katalog(path));}
}
static void live_slot_b(){
    Ram ram;logs.clear();boot_device_lookups=0;
    const char command[]="bursztyn.slot=B";
    std::array<uint64_t,8> mbi{};auto* bytes=reinterpret_cast<uint8_t*>(mbi.data());
    auto* fields=reinterpret_cast<uint32_t*>(bytes);fields[0]=64;
    fields[2]=1;fields[3]=8+sizeof(command);memcpy(bytes+16,command,sizeof(command));
    const size_t end=(8+fields[3]+7)&~size_t(7);auto* tag=reinterpret_cast<uint32_t*>(bytes+end);tag[1]=8;
    aktualizacja_sprawdz_rozruch(reinterpret_cast<uint64_t>(mbi.data()));
    CHECK(boot_device_lookups==0);CHECK(psf_pobierz_aktywny_slot()==1);tree("Live-slot-B");
    CHECK(rozwiaz_sciezke("/system",nullptr,false)==rozwiaz_sciezke("/sloty/b/system",nullptr,false));
}
static void last_bootstrap_inode(){
    Ram ram;tree("last-inode-fixture");CHECK(usun_twor("/aktualizacje/przygotowanie"));
    uint64_t free=0;
    for(uint64_t i=1;i<=calkowita_liczba_wezlow;++i){auto* w=pobierz_wezel(i);if(w->typ==TYP_WOLNY){if(!free)free=i;else w->typ=TYP_PLIK;}}
    CHECK(free);tree("one-inode-bootstrap");tree("zero-inodes-existing-bootstrap");
    PsfStanStartu s{};CHECK(!step("/no-more-inodes",s));CHECK(s.blad==PsfBladStartu::BrakWezlow);
}
static void partial_and_user_data(){
    Ram ram;
    for(size_t i=0;i<LICZBA_KATALOGOW_SYSTEMOWYCH/2;++i){PsfStanStartu s{};CHECK(step(KATALOGI_SYSTEMOWE[i],s));}
    tree("half-present",true);CHECK(utworz_plik("/unrelated"));CHECK(utworz_plik("/zażółć-gęślą-jaźń"));
    std::string longname="/"+std::string(PSF_MAX_NAZWA-1,'x');CHECK(utworz_plik(longname.c_str()));
    CHECK(utworz_plik("/ustawienia/uklad-pulpitu.cfg"));const char layout[]="old-layout-bytes\n";CHECK(zapisz_do_pliku("/ustawienia/uklad-pulpitu.cfg",layout,sizeof(layout)));
    for(int i=0;i<DesktopEntries::FileCapacity+35;++i){std::string p=std::string(DesktopEntries::Directory)+"/overflow-"+std::to_string(i);CHECK(utworz_plik(p.c_str()));}
    CHECK(usun_twor("/tymczasowe"));tree("desktop-overflow");
    char b[sizeof(layout)]{};CHECK(czytaj_z_pliku("/ustawienia/uklad-pulpitu.cfg",b,sizeof(b)));CHECK(!memcmp(layout,b,sizeof(b)));
    psf_metadane m{};CHECK(pobierz_metadane_tworu(longname.c_str(),&m));CHECK(pobierz_metadane_tworu("/zażółć-gęślą-jaźń",&m));
    for(int i=0;i<DesktopEntries::FileCapacity+35;++i){std::string p=std::string(DesktopEntries::Directory)+"/overflow-"+std::to_string(i);CHECK(pobierz_metadane_tworu(p.c_str(),&m));}
    CHECK(usun_twor("/ustawienia/uklad-pulpitu.cfg"));tree("layout-absent");
}
static void resource_limits(){
    {Ram ram;
        for(uint64_t i=2;i<calkowita_liczba_wezlow;++i)pobierz_wezel(i)->typ=TYP_PLIK;
        PsfStanStartu s{};CHECK(step("/last",s));CHECK(s.alokacjaWezla==PsfEtapStartu::Ok);
        if(BURSZTYNDIAGPSFBOOTSTRAP){CHECK(s.wolneWezly==1);}CHECK(!step("/none",s));CHECK(s.blad==PsfBladStartu::BrakWezlow);if(BURSZTYNDIAGPSFBOOTSTRAP){CHECK(s.wolneWezly==0);}
        CHECK(step("/last",s));CHECK(s.tworzenie==PsfEtapStartu::NieProbowano);
    }
    {Ram ram;
        for(uint64_t i=0;i+1<calkowita_liczba_blokow_danych;++i)CHECK(ustaw_stan_bloku_danych(i,true));
        PsfStanStartu s{};CHECK(step("/last",s));CHECK(s.alokacjaBloku==PsfEtapStartu::Ok);if(BURSZTYNDIAGPSFBOOTSTRAP){CHECK(s.wolneBloki==1);}
        // A second empty directory consumes an inode and a free entry, no new block.
        CHECK(step("/last-slot",s));if(BURSZTYNDIAGPSFBOOTSTRAP){CHECK(s.wolneBloki==0);}CHECK(s.alokacjaBloku==PsfEtapStartu::NieProbowano);
        auto before=rozwiaz_sciezke("/last/missing",nullptr,false);CHECK(!before);
        auto id=rozwiaz_sciezke("/last",nullptr,false);auto entries=pobierz_wezel(id)->rozmiar_w_bajtach;
        CHECK(!step("/last/missing",s));CHECK(s.blad==PsfBladStartu::BrakBlokow);CHECK(pobierz_wezel(id)->rozmiar_w_bajtach==entries);
        CHECK(!rozwiaz_sciezke("/last/missing",nullptr,false));
    }
    {Ram ram;PsfStanStartu s{};psf_test_bootstrap_brak_bloku=true;
        CHECK(!step("/injected",s));CHECK(s.blad==PsfBladStartu::BrakBlokow);CHECK(s.alokacjaBloku==PsfEtapStartu::Blad);CHECK(s.wpis==PsfEtapStartu::Blad);
        CHECK(pobierz_wezel(1)->rozmiar_w_bajtach==0);CHECK(pobierz_wezel(2)->typ==TYP_WOLNY);
        psf_test_bootstrap_brak_bloku=false;CHECK(step("/injected",s));
    }
    {Ram ram;for(uint64_t i=0;i<calkowita_liczba_blokow_danych;++i)CHECK(ustaw_stan_bloku_danych(i,true));
        PsfStanStartu s{};CHECK(!step("/none",s));CHECK(s.blad==PsfBladStartu::BrakBlokow);CHECK(pobierz_wezel(2)->typ==TYP_WOLNY);
    }
}
static void full_directory(){
    Ram ram;CHECK(utworz_plik("/existing-file"));
    // BSP2 fixture fills all 490*64 physical entry slots with distinct names.
    // Entries share an existing file inode (accepted by BSP2), to exercise the
    // directory's physical bound independently of this RAM geometry's 1024 inodes.
    auto* root=pobierz_wezel(1);const auto file=rozwiaz_sciezke("/existing-file",nullptr,false);
    for(size_t k=0;k<PSF_MAX_BLOKOW_W_WEZLE;++k){
        if(root->wskazniki_blokow[k]==BARK_BLOKU)root->wskazniki_blokow[k]=zaalokuj_wolny_blok_danych();
        CHECK(root->wskazniki_blokow[k]!=BARK_BLOKU);auto* e=pobierz_wpisy_katalogowe(root->wskazniki_blokow[k]);CHECK(e);
        for(size_t j=0;j<wpisow_katalogowych_na_blok();++j){e[j].id_wezla=file;snprintf(e[j].nazwa,sizeof(e[j].nazwa),"unrelated-%05zu",k*64+j);}
    }
    root->rozmiar_w_bajtach=PSF_MAX_BLOKOW_W_WEZLE*wpisow_katalogowych_na_blok();
    auto* last=pobierz_wpisy_katalogowe(root->wskazniki_blokow[PSF_MAX_BLOKOW_W_WEZLE-1]);
    last[63]={};--root->rozmiar_w_bajtach;
    PsfStanStartu s{};CHECK(step("/sloty",s));CHECK(s.wpis==PsfEtapStartu::Ok);CHECK(s.alokacjaBloku==PsfEtapStartu::NieProbowano);
    CHECK(root->rozmiar_w_bajtach==31360);CHECK(!step("/jadro",s));CHECK(s.blad==PsfBladStartu::LimitKatalogu);CHECK(s.alokacjaWezla==PsfEtapStartu::Ok);CHECK(s.wpis==PsfEtapStartu::Blad);
    CHECK(root->rozmiar_w_bajtach==31360);CHECK(pobierz_wezel(s.wezel)->typ==TYP_WOLNY);
    CHECK(step("/sloty",s));CHECK(s.istnial);
}
static void malformed(){
    {Ram ram;pobierz_wezel(1)->typ=TYP_PLIK;PsfStanStartu s{};CHECK(!step("/ustawienia",s));CHECK(s.blad==PsfBladStartu::ZlyKorzen);}
    {Ram ram;dysk_superblok->id_korzenia=calkowita_liczba_wezlow+1;PsfStanStartu s{};CHECK(!step("/ustawienia",s));CHECK(s.blad==PsfBladStartu::ZlyKorzen);}
    {Ram ram;pobierz_wezel(1)->wskazniki_blokow[0]=calkowita_liczba_blokow_danych+1;PsfStanStartu s{};CHECK(!step("/ustawienia",s));CHECK(s.blad==PsfBladStartu::ZlyKorzen);}
    {Ram ram;CHECK(utworz_plik("/ustawienia"));PsfStanStartu s{};CHECK(!step("/ustawienia",s));CHECK(s.blad==PsfBladStartu::NieKatalog);CHECK(s.istnial);CHECK(s.tworzenie==PsfEtapStartu::NieProbowano);
        char panic[192]{};psf_start_panic(panic,sizeof(panic),"/ustawienia",s);CHECK(!strcmp(panic,"Nie mozna przygotowac katalogu /ustawienia PSF_NIE_KATALOG"));
        std::array<char,9> shortmsg;shortmsg.fill('x');psf_start_panic(shortmsg.data()+1,7,"/ustawienia",s);CHECK(shortmsg[0]=='x');CHECK(shortmsg[8]=='x');CHECK(shortmsg[7]==0);
    }
    {Ram ram;PsfStanStartu s{};CHECK(!step("/missing/child",s));CHECK(s.blad==PsfBladStartu::BrakRodzica);
        CHECK(!step("/bad//path",s));CHECK(s.blad==PsfBladStartu::ZlaSciezka);psf_gotowy=false;CHECK(!step("/ustawienia",s));CHECK(s.blad==PsfBladStartu::NieGotowy);
    }
}
// Real BlockDevice operations: inject failures on precise filesystem blocks.
struct Disk {
    std::vector<uint8_t> bytes;Storage::Device dev{};
    uint64_t fail_block=UINT64_MAX,read_failure=UINT64_MAX;
    unsigned writes=0,flushes=0;bool fail_flush=false;
    static constexpr uint64_t start=128;
    Disk():bytes(20*1024*1024,0x5a){dev.driver_data=this;dev.ops=&ops;dev.total_sectors=bytes.size()/512;dev.device_type=BlockDeviceType::VirtIO;}
    static BlockError rd(BlockDevice* d,uint64_t lba,uint32_t n,void* out){auto& s=*static_cast<Disk*>(d->driver_data);if(lba<=s.read_failure && s.read_failure<lba+n)return BlockError::IOError;memcpy(out,s.bytes.data()+lba*512,n*512);return BlockError::Ok;}
    static BlockError wr(BlockDevice* d,uint64_t lba,uint32_t n,const void* in){auto& s=*static_cast<Disk*>(d->driver_data);++s.writes;
        CHECK(lba>=start);CHECK(lba+n<=start+Ram::bytes/512);
        const auto fail=start+s.fail_block*8;
        if(s.fail_block!=UINT64_MAX && lba<=fail && fail<lba+n)return BlockError::IOError;
        memcpy(s.bytes.data()+lba*512,in,n*512);return BlockError::Ok;}
    static BlockError fl(BlockDevice* d){auto& s=*static_cast<Disk*>(d->driver_data);++s.flushes;return s.fail_flush?BlockError::Timeout:BlockError::Ok;}
    static const BlockOperations ops;
    Storage::Partition partition(){return {&dev,start,Ram::bytes/512};}
    void mount(Ram& ram){
        auto p=partition();psf_ustaw_partycje(&p);nosnik_blokowy_dostepny=true;CHECK(sformatuj_ram_dysk(true));
        CHECK(nosnik_blokowy_dostepny);CHECK(zapis_trwaly_dozwolony);
        inicjalizuj_psf(ram.data(),Ram::bytes);CHECK(psf_czy_gotowy());CHECK(nosnik_blokowy_dostepny);
    }
    void sentinels(){for(size_t i=0;i<start*512;++i)CHECK(bytes[i]==0x5a);for(size_t i=(start+Ram::bytes/512)*512;i<bytes.size();++i)CHECK(bytes[i]==0x5a);}
};
const BlockOperations Disk::ops={Disk::rd,Disk::wr,Disk::fl,nullptr};
static void disks(){
    {Ram ram;Disk d;d.mount(ram);installed=true;tree("persistent-first",true);unsigned writes=d.writes,flush=d.flushes;
        tree("persistent-existing");CHECK(d.writes==writes);CHECK(d.flushes==flush);
        auto p=d.partition();psf_ustaw_partycje(&p);inicjalizuj_psf(ram.data(),Ram::bytes);CHECK(psf_czy_gotowy());tree("persistent-reboot",true);
        CHECK(d.writes==writes);CHECK(d.flushes==flush);d.sentinels();installed=false;
    }
    for(unsigned fault=0;fault<3;++fault){Ram ram;Disk d;d.mount(ram);
        d.fail_block=fault==0?dysk_superblok->start_danych:dysk_superblok->start_wezlow+1;
        if(fault==2){d.fail_block=UINT64_MAX;d.fail_flush=true;}
        PsfStanStartu s{};CHECK(!step("/failed-write",s));CHECK(s.blad==(fault==2?PsfBladStartu::Flush:PsfBladStartu::Zapis));
        CHECK(s.tworzenie==PsfEtapStartu::Ok);CHECK(s.alokacjaWezla==PsfEtapStartu::Ok);CHECK(s.wpis==PsfEtapStartu::Ok);
        CHECK(s.trwaly);CHECK(s.backend==BlockDeviceType::VirtIO);
        if(fault==2){CHECK(s.flush==PsfEtapStartu::Blad);CHECK(s.zapis==PsfEtapStartu::Ok);CHECK(s.bladBlokowy==BlockError::Timeout);CHECK(d.flushes==2);}
        else{CHECK(s.zapis==PsfEtapStartu::Blad);CHECK(s.blokZapisu==d.fail_block);CHECK(s.flush==PsfEtapStartu::NieProbowano);CHECK(s.bladBlokowy==BlockError::IOError);CHECK(d.flushes==1);}
        // Generic PSF commits memory and retains dirty state on write failure.
        CHECK(czy_katalog_istnieje("/failed-write"));CHECK(!zapis_trwaly_dozwolony);CHECK(liczba_brudnych_blokow>0);d.sentinels();
    }
    {Ram ram;Disk d;d.mount(ram);tree("duplicate-fixture");
        auto* root=pobierz_wezel(1);auto* e=pobierz_wpisy_katalogowe(root->wskazniki_blokow[0]);
        size_t free=0;while(e[free].id_wezla)++free;CHECK(free<64);e[free]=e[0];++root->rozmiar_w_bajtach;
        oznacz_wezel_jako_brudny(1);oznacz_blok_jako_brudny(absolutny_blok_danych(root->wskazniki_blokow[0]));CHECK(psf_flush_cache());
        inicjalizuj_psf(ram.data(),Ram::bytes);CHECK(psf_czy_gotowy());tree("duplicate-entry-reboot");
        CHECK(pobierz_wezel(1)->rozmiar_w_bajtach==free);CHECK(!stan_startu);d.sentinels();
    }
    {Ram ram;Disk d;d.mount(ram);tree("unreadable-root-fixture");
        const auto block=pobierz_wezel(1)->wskazniki_blokow[0];wyczysc_cache_blokow();d.read_failure=Disk::start+absolutny_blok_danych(block)*8;
        PsfStanStartu s{};CHECK(!step("/ustawienia",s));CHECK(s.blad==PsfBladStartu::Odczyt);CHECK(s.tworzenie==PsfEtapStartu::NieProbowano);d.sentinels();
    }
}
int main(){
    report=std::fopen(".installertest/psf-bootstrap-trace.log","w");CHECK(report);
    legacy_failure();live_boot();live_slot_b();last_bootstrap_inode();partial_and_user_data();resource_limits();full_directory();malformed();disks();
    CHECK(!stan_startu);CHECK(!psf_test_lock_held());std::fclose(report);
    std::printf("PSF/bootstrap/Live production host: %u checks PASS, %u scenarios, ASan/UBSan\n",checks,scenarios);
}
