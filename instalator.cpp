#include "instalator.h"
#include "blokowy.h"
#include "ahci.h"
#include "psf.h"
#include "grafika.h"
#include "scheduler.h"
#include "tapety.h"
#include "sterowniki/usb/xhci.h"

namespace {
bool mode=false,installed=false,uefi=false;
int owner=-1;
uint64_t owner_cr3=0,mbi_saved=0,nonce=0;
Storage::BootFile boot_efi{},boot_kernel{};
Storage::Layout layout{};
Storage::Table table{};
uint8_t disk_uuid[16]{};
uint32_t selected=UINT32_MAX,stage=INST_GOTOWY,status=0,fingerprint=0;
uint32_t progress=0,progress_stage=POSTEP_PRZYGOTOWANIE,progress_done=0,progress_total=0;
uint32_t files_done=0,files_total=0,wallpapers_done=0,wallpapers_total=0;
char error[160]{};
const char config[]=
    "set default=0\nset gfxmode=auto\nset gfxpayload=keep\n"
    "insmod all_video\ninsmod gfxterm\nterminal_output gfxterm\n"
    "menuentry 'Bursztyn OS' {\n multiboot2 /boot/system_operacyjny.bin bursztyn.installed=1\n boot\n}\n"
    "menuentry 'Bursztyn OS testy automatyczne' {\n multiboot2 /boot/system_operacyjny.bin bursztyn.installed=1 bursztyn.test=automated\n boot\n}\n"
    "set timeout_style=hidden\nset timeout=0\n";
void text_copy(char*d,const char*s,size_t n){size_t i=0;for(;i+1<n&&s[i];++i)d[i]=s[i];d[i]=0;}
bool token(const char*s,size_t n,const char*t){
    size_t len=0;while(t[len])++len;
    for(size_t i=0;i<n&&s[i];){size_t end=i;while(end<n&&s[end]&&s[end]!=' ')++end;
        if(end-i==len&&Storage::equal(s+i,t,len))return true;
        i=end+1;}return false;
}
bool name(const char*s,size_t n,const char*t){size_t i=0;while(i<n&&t[i]&&s[i]==t[i])++i;return i<n&&!t[i]&&!s[i];}
bool fingerprint_disk(Storage::Device*d,uint32_t*out){
    if(!d||d->sectors<68||!out)return false;
    Storage::Partition all{d,0,d->sectors};uint8_t b[512];uint32_t c=0;
    for(unsigned i=0;i<68;++i){uint64_t lba=i<34?i:d->sectors-68+i;
        if(!Storage::read(all,lba,1,b))return false;
        c=(c<<1)|(c>>31);c^=Storage::crc32(b,512);}
    *out=c;return true;
}
void fail(uint32_t code,const char*why){status=code;stage=INST_BLAD;text_copy(error,why,sizeof(error));klog_serial("[INSTALL] Instalacja nie powiodla sie:");klog_serial(why);}
void set_progress(uint32_t percent,uint32_t progressStage,uint32_t done=0,uint32_t total=0,bool complete=false){
    if(!instalator_etap_postepu_poprawny(progress_stage,progressStage))return;
    progress=instalator_postep_nowy(progress,percent,complete);progress_stage=progressStage;
    progress_done=done;progress_total=total;
}
void snapshot(InstalatorOdpowiedz*out,uint32_t id){
    *out={};out->wersja=1;out->status=status;out->etap=stage;out->liczba_dyskow=ahci_liczba_dyskow();out->dysk=id;
    out->uefi=uefi;out->gop=grafika_mozliwosci_ekranu().backend==EKRAN_GOP;
    out->xhci=xhci_dostepny();out->token=nonce;out->zapisy=ahci_licznik_zapisow();
    out->procent=progress;out->postep_etap=progress_stage;
    out->wykonane=progress_done;out->razem=progress_total;
    out->pliki_wykonane=files_done;out->pliki_razem=files_total;
    out->tapety_wykonane=wallpapers_done;out->tapety_razem=wallpapers_total;
    text_copy(out->blad,error,sizeof(out->blad));
    AhciDysk d{};if(!ahci_dysk(id,&d))return;
    out->sektory=d.sektory;out->sektor=d.sektor_bajty;out->odczyt=d.odczyt;out->zapis=d.zapis;
    text_copy(out->model,d.model,sizeof(out->model));text_copy(out->serial,d.serial,sizeof(out->serial));
    auto*dev=blokowy_dysk(id);out->nosnik_instalacyjny=Storage::source_iso(dev);
    Storage::Layout l{};if(Storage::plan(dev,&l)){out->esp_mib=Storage::EspBytes/Storage::MiB;out->bsp_mib=l.bsp.count/2048;out->nieprzydzielone_mib=l.unallocated/2048;}
    if(Storage::gpt_read(dev,&table))for(uint32_t i=0;i<table.count;++i)if(Storage::equal(table.entries[i].type,Storage::BspType,16))out->bursztyn=1;
}
bool payloads(){return boot_efi.data&&boot_efi.size>512&&boot_efi.data[0]=='M'&&boot_efi.data[1]=='Z'&&
    boot_kernel.data&&boot_kernel.size>64&&Storage::equal(boot_kernel.data,"\177ELF",4);}
void perform(){
    auto*d=layout.bsp.device;
    if(stage==INST_GPT){
        klog_serial("[INSTALL] progress=0 stage=GPT");
        if(!ahci_sprawdz_tozsamosc(selected)){fail(INST_DYSK_BLAD,"Walidacja: tożsamość dysku zmieniła się.");return;}
        // Unpublish any stale complete superblock BEFORE new GPT references it.
        uint8_t zero[4096]{};
        if(!Storage::flush(layout.bsp)||!Storage::write_verify(layout.bsp,0,8,zero)||!Storage::flush(layout.bsp)||
           !Storage::gpt_write(d,layout,disk_uuid)||!Storage::gpt_read(d,&table)||!Storage::equal(table.uuid,disk_uuid,16)){
            fail(INST_IO,"GPT: zapis / FLUSH / odczyt kontrolny nie powiódł się.");return;}
        set_progress(12,POSTEP_ESP);stage=INST_ESP;
    }else if(stage==INST_ESP){
        klog_serial("[INSTALL] progress=12 stage=ESP");
        if(!Storage::fat_install(layout.esp,boot_efi,boot_kernel,{(const uint8_t*)config,sizeof(config)-1})){
            fail(INST_IO,"ESP FAT32 / pliki rozruchowe: zapis lub weryfikacja nie powiodła się.");return;}
        set_progress(30,POSTEP_BSP2);stage=INST_BSP;
    }else if(stage==INST_BSP){
        klog_serial("[INSTALL] progress=30 stage=BSP2FORMAT");
        if(!psf_formatuj_instalacje(layout.bsp)||!psf_znacznik_instalacji(disk_uuid,false)||!psf_weryfikuj_dysk()){
            fail(INST_FORMAT,"BSP2: format / znacznik w toku / odczyt kontrolny.");return;}
        set_progress(45,POSTEP_SYSTEM);stage=INST_SYSTEM;
    }else if(stage==INST_SYSTEM){
        klog_serial("[INSTALL] progress=45 stage=SYSTEMFILES");
        if(!kernel_instaluj_system()){
            fail(INST_PLIKI,"Pliki systemowe: kopiowanie lub FLUSH nie powiodły się.");return;}
        const uint32_t files=kernel_liczba_plikow_instalacji();
        files_done=files_total=files;
        set_progress(75,POSTEP_TAPETY,files,files);stage=INST_TAPETY;
    }else if(stage==INST_TAPETY){
        klog_serial("[INSTALL] progress=75 stage=WALLPAPERS");
        if(!tapety_instaluj_sprawdz(mbi_saved)){
            fail(INST_PLIKI,"Tapety: synchronizacja lub CRC nie powiodły się.");return;}
        wallpapers_done=tapety_liczba_zainstalowanych();
        wallpapers_total=tapety_liczba_wymaganych();
        set_progress(92,POSTEP_WERYFIKACJA,wallpapers_done,wallpapers_total);
        stage=INST_WERYFIKACJA;
    }else if(stage==INST_WERYFIKACJA){
        klog_serial("[INSTALL] progress=92 stage=VERIFY");
        if(!psf_flush_cache()||!psf_weryfikuj_dysk()||!kernel_weryfikuj_system()||!tapety_weryfikuj_instalacje()){
            fail(INST_IO,"Weryfikacja: metadane / pliki systemowe / inwentarz tapet.");return;}
        set_progress(99,POSTEP_WERYFIKACJA);stage=INST_ZNACZNIK;
    }else if(stage==INST_ZNACZNIK){
        if(!psf_znacznik_instalacji(disk_uuid,true)||!Storage::complete(layout.bsp,disk_uuid)){
            fail(INST_IO,"Znacznik ukończenia: FLUSH / CRC / odczyt kontrolny.");return;}
        set_progress(100,POSTEP_COMPLETE,0,0,true);stage=INST_KONIEC;
        klog_serial("[INSTALL] progress=100 stage=COMPLETE");
        klog_serial("[INSTALL] COMPLETE verified; AUTOMATED GUEST INSTALL TEST is not human hardware verification.");
    }
}
}
bool instalator_tryb(){return mode;}
bool instalator_system_zainstalowany(){return installed;}
void instalator_boot(uint64_t mbi){
    mbi_saved=mbi;if(!mbi)return;
    uint32_t total=*(const uint32_t*)mbi;if(total<16||total>1024*1024||mbi>UINT64_MAX-total)return;
    for(uint64_t off=8;off+8<=total;){auto*p=(const uint32_t*)(mbi+off);uint32_t typ=p[0],size=p[1];
        if(size<8||size>total-off)return;
        if(typ==12||typ==20)uefi=true;
        if(typ==1){const char*s=(const char*)(p+2);size_t n=size-8;
            mode=token(s,n,"bursztyn.install=1");installed=token(s,n,"bursztyn.installed=1");}
        if(typ==3&&size>16&&p[3]>p[2]){const char*s=(const char*)(p+4);Storage::BootFile f{(const uint8_t*)uintptr_t(p[2]),p[3]-p[2]};
            if(f.size<=64*Storage::MiB){if(name(s,size-16,"installer.efi"))boot_efi=f;if(name(s,size-16,"installer.kernel"))boot_kernel=f;}}
        if(!typ)break;
        off+=(uint64_t(size)+7)&~7ULL;
    }
    if(mode)installed=false;
}
void instalator_autoryzuj(int pid){owner=pid;owner_cr3=pid>0?tablica_procesow[pid].cr3:0;}
void instalator_zamknij_proces(int pid){if(pid==owner){owner=-1;owner_cr3=0;nonce=0;blokowy_zezwol_instalatorowi(0,false);}}
bool instalator_obsluz(const proces_t&p,const InstalatorZadanie&r,InstalatorOdpowiedz*out){
    if(!out||r.wersja!=1)return false;
    if(!mode||p.pid!=uint64_t(owner)||p.cr3!=owner_cr3||!pzb_ma_co_najmniej_poziom(&p,PZB_ZAUFANE)||!pzb_ma_prawa(&p,PRAWO_SYSTEM_CONFIG)){
        *out={};out->wersja=1;out->status=INST_UPRAWNIENIA;return true;}
    if(r.operacja==INST_STAN||r.operacja==INST_DYSK){snapshot(out,r.dysk);return true;}
    if(r.operacja==INST_PODGLAD && stage==INST_GOTOWY){
        status=0;error[0]=0;selected=UINT32_MAX;nonce=0;
        progress=0;progress_stage=POSTEP_PRZYGOTOWANIE;progress_done=progress_total=0;
        files_done=files_total=wallpapers_done=wallpapers_total=0;
        auto*d=blokowy_dysk(r.dysk);
        if(!uefi||!payloads())status=INST_BRAK_PLIKOW;
        else if(!d||!Storage::plan(d,&layout))status=INST_ZA_MALY;
        else if(Storage::source_iso(d))status=INST_ZRODLO;
        else if(!fingerprint_disk(d,&fingerprint)||!ahci_sprawdz_tozsamosc(r.dysk))status=INST_DYSK_BLAD;
        else {selected=r.dysk;uint32_t lo,hi;asm volatile("rdtsc":"=a"(lo),"=d"(hi));nonce=(uint64_t(hi)<<32|lo)|1;
            for(unsigned i=0;i<16;++i)disk_uuid[i]=uint8_t((nonce>>(i%8*8))^(fingerprint>>(i%4*8))^(i*37));
            disk_uuid[7]=(disk_uuid[7]&15)|0x40;disk_uuid[8]=(disk_uuid[8]&63)|0x80;}
    }else if(r.operacja==INST_POTWIERDZ && stage==INST_GOTOWY){
        uint32_t current=0;
        if(!nonce||r.token!=nonce||r.dysk!=selected||!r.zgoda_zrodlo||
           !instalator_potwierdzenie_poprawne(r.potwierdzenie,sizeof(r.potwierdzenie)))status=INST_POTWIERDZENIE;
        else if(!payloads()||Storage::source_iso(layout.bsp.device)||!ahci_sprawdz_tozsamosc(selected)||
                !fingerprint_disk(layout.bsp.device,&current)||current!=fingerprint)status=INST_DYSK_BLAD;
        else {status=0;set_progress(0,POSTEP_GPT);stage=INST_GPT;klog_serial("[INSTALL] Explicit final confirmation accepted. No disk sectors written before this point.");}
    }else if(r.operacja==INST_KROK&&stage>INST_GOTOWY&&stage<INST_KONIEC&&r.token==nonce&&r.dysk==selected){
        blokowy_zezwol_instalatorowi(selected,true);perform();blokowy_zezwol_instalatorowi(selected,false);
    }else status=INST_ZAJETY;
    snapshot(out,r.dysk);return true;
}
