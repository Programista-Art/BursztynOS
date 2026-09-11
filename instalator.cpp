#include "instalator.h"
#include "blokowy.h"
#include "ahci.h"
#include "psf.h"
#include "grafika.h"
#include "scheduler.h"
#include "tapety.h"
#include "sterowniki/usb/xhci.h"
#include "sterowniki/czas/hpet.h"

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
Storage::FatInstallState fat_state{};
Storage::GptInstallState gpt_state{};
bool gpt_started=false;
bool fat_started=false;
bool wallpapers_started=false;
uint64_t wallpaper_bytes_done=0,wallpaper_bytes_total=0;
bool system_started=false;
uint64_t system_bytes_done=0,system_bytes_total=0;
uint32_t verify_phase=0;
uint64_t verify_system_done=0,verify_system_total=0,verify_wall_done=0,verify_wall_total=0;
uint64_t verify_meta_done=0,verify_meta_total=0;
uint64_t perf_stage_us=0,perf_longest_us=0,perf_irq=0,perf_switches=0;
uint64_t perf_stage_irq_start=0,perf_stage_switch_start=0;
uint32_t bsp_phase=0;uint64_t bsp_done=0,bsp_total=0;
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
const char* progress_name(uint32_t value){
    static const char* names[]={"PREPARE","GPT","ESP","BSP2FORMAT","SYSTEMFILES","WALLPAPERS","VERIFY","COMPLETE"};
    return value<=POSTEP_COMPLETE?names[value]:"INVALID";
}
void log_progress(uint32_t percent,uint32_t progressStage){
    char line[80]="[INSTALL] progress=";size_t p=sizeof("[INSTALL] progress=")-1;char digits[4];size_t n=0;
    do{digits[n++]=char('0'+percent%10);percent/=10;}while(percent&&n<sizeof(digits));
    while(n&&p+1<sizeof(line))line[p++]=digits[--n];
    const char mid[]=" stage=";for(size_t i=0;mid[i]&&p+1<sizeof(line);++i)line[p++]=mid[i];
    const char*name=progress_name(progressStage);for(size_t i=0;name[i]&&p+1<sizeof(line);++i)line[p++]=name[i];
    line[p]=0;klog_serial(line);
}
void append_u64(char*line,size_t cap,size_t*p,uint64_t value){
    char digits[24];size_t n=0;do{digits[n++]=char('0'+value%10);value/=10;}while(value&&n<sizeof(digits));
    while(n&&*p+1<cap)line[(*p)++]=digits[--n];
}
void append_text(char*line,size_t cap,size_t*p,const char*value){
    for(size_t i=0;value[i]&&*p+1<cap;++i)line[(*p)++]=value[i];
}
void log_perf(uint32_t progressStage,bool interrupts){
    char line[192]="[INSTALL PERF] stage=";size_t p=sizeof("[INSTALL PERF] stage=")-1;append_text(line,sizeof(line),&p,progress_name(progressStage));
    append_text(line,sizeof(line),&p," durationus=");append_u64(line,sizeof(line),&p,perf_stage_us);
    append_text(line,sizeof(line),&p," longestblockus=");append_u64(line,sizeof(line),&p,perf_longest_us);
    append_text(line,sizeof(line),&p," interrupts=");append_u64(line,sizeof(line),&p,interrupts?1:0);
    append_text(line,sizeof(line),&p," timerirq=");append_u64(line,sizeof(line),&p,perf_irq);
    append_text(line,sizeof(line),&p," switches=");append_u64(line,sizeof(line),&p,perf_switches);
    line[p]=0;klog_serial(line);

    char latency[112]="[INSTALL LATENCY] stage=";
    p=sizeof("[INSTALL LATENCY] stage=")-1;
    append_text(latency,sizeof(latency),&p,progress_name(progressStage));
    append_text(latency,sizeof(latency),&p," maxnonyieldus=");
    append_u64(latency,sizeof(latency),&p,perf_longest_us);
    latency[p]=0;
    klog_serial(latency);
}
void set_progress(uint32_t percent,uint32_t progressStage,uint32_t done=0,uint32_t total=0,bool complete=false){
    if(!instalator_etap_postepu_poprawny(progress_stage,progressStage))return;
    const uint32_t old=progress;const uint32_t old_stage=progress_stage;
    progress=instalator_postep_nowy(progress,percent,complete);progress_stage=progressStage;
    progress_done=done;progress_total=total;
    const bool changed_stage=progress_stage!=old_stage;
    const bool crossed_ten=progress/10!=old/10;
    if(changed_stage||crossed_ten||progress==100)log_progress(progress,progress_stage);
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
        if(!gpt_started){
            klog_serial("[INSTALL] progress=0 stage=GPT");
            if(!ahci_sprawdz_tozsamosc(selected)){fail(INST_DYSK_BLAD,"Walidacja: tożsamość dysku zmieniła się.");return;}
            gpt_started=Storage::gpt_install_start(d,layout,disk_uuid,&gpt_state);
        }
        if(!gpt_started||!Storage::gpt_install_step(&gpt_state)){
            fail(INST_IO,"GPT: zapis / FLUSH / odczyt kontrolny nie powiódł się.");return;}
        set_progress(instalator_podpostep(0,12,gpt_state.completed_units,gpt_state.total_units),POSTEP_GPT,
                     gpt_state.completed_units,gpt_state.total_units);
        if(gpt_state.complete){
            if (!Storage::rozruch_weryfikuj_uklad_dysku(d)) {
                fail(INST_DYSK_BLAD, "Układ GPT narusza zarezerwowane sektory stanu rozruchu (LBA 2046/2047).");
                return;
            }
            table={};for(unsigned i=0;i<16;++i)table.uuid[i]=disk_uuid[i];set_progress(12,POSTEP_ESP);stage=INST_ESP;
        }
    }else if(stage==INST_ESP){
        if(!fat_started){
            klog_serial("[INSTALL] progress=12 stage=ESP");
            fat_started=Storage::fat_install_start(layout.esp,boot_efi,boot_kernel,
                {(const uint8_t*)config,sizeof(config)-1},&fat_state);
        }
        if(!fat_started||!Storage::fat_install_step(&fat_state)){
            fail(INST_IO,"ESP FAT32 / pliki rozruchowe: zapis lub weryfikacja nie powiodła się.");return;}
        set_progress(instalator_podpostep(12,30,fat_state.completed_units,fat_state.total_units),
                     POSTEP_ESP,fat_state.completed_units,fat_state.total_units);
        if(fat_state.complete){set_progress(30,POSTEP_BSP2);stage=INST_BSP;}
    }else if(stage==INST_BSP){
        if(bsp_phase==0){
            klog_serial("[INSTALL] progress=30 stage=BSP2FORMAT");
            if(!psf_formatuj_instalacje_start(layout.bsp,&bsp_total)){fail(INST_FORMAT,"BSP2: przygotowanie formatu.");return;}
            bsp_phase=1;
        }else if(bsp_phase==1){
            bool done=false;if(!psf_formatuj_instalacje_step(&bsp_done,&done)){fail(INST_FORMAT,"BSP2: zapis metadanych.");return;}
            if(done)bsp_phase=2;
        }else if(bsp_phase==2){
            if(!psf_znacznik_instalacji(disk_uuid,false)||!psf_weryfikuj_dysk_start(&bsp_total)){
                fail(INST_FORMAT,"BSP2: znacznik w toku / przygotowanie odczytu kontrolnego.");return;}
            bsp_done=0;bsp_phase=3;
        }else{
            bool done=false;if(!psf_weryfikuj_dysk_step(&bsp_done,&done)){fail(INST_FORMAT,"BSP2: odczyt kontrolny metadanych.");return;}
            if(done)bsp_phase=4;
        }
        const uint32_t bsp_percent=bsp_phase<3
            ?instalator_podpostep(30,42,uint32_t(bsp_done),uint32_t(bsp_total))
            :instalator_podpostep(42,45,uint32_t(bsp_done),uint32_t(bsp_total));
        set_progress(bsp_percent,POSTEP_BSP2,uint32_t(bsp_done),uint32_t(bsp_total));
        if(bsp_phase==4){set_progress(45,POSTEP_SYSTEM);stage=INST_SYSTEM;}
    }else if(stage==INST_SYSTEM){
        if(!system_started){
            klog_serial("[INSTALL] progress=45 stage=SYSTEMFILES");
            system_started=kernel_instaluj_system_start(&system_bytes_total);
        }
        bool done=false;
        if(!system_started||system_bytes_total>UINT32_MAX||
           !kernel_instaluj_system_step(&system_bytes_done,&files_done,&files_total,&done)){
            fail(INST_PLIKI,"Pliki systemowe: kopiowanie lub FLUSH nie powiodły się.");return;}
        set_progress(instalator_podpostep(45,75,uint32_t(system_bytes_done),uint32_t(system_bytes_total)),
                     POSTEP_SYSTEM,uint32_t(system_bytes_done),uint32_t(system_bytes_total));
        if(done){set_progress(75,POSTEP_TAPETY,files_done,files_total);stage=INST_TAPETY;}
    }else if(stage==INST_TAPETY){
        if(!wallpapers_started){
            klog_serial("[INSTALL] progress=75 stage=WALLPAPERS");
            wallpapers_started=tapety_instaluj_start(mbi_saved,&wallpaper_bytes_total);
        }
        bool done=false;
        if(!wallpapers_started||wallpaper_bytes_total>UINT32_MAX||
           !tapety_instaluj_step(&wallpaper_bytes_done,&wallpapers_done,&wallpapers_total,&done)){
            fail(INST_PLIKI,"Tapety: synchronizacja lub zapis nie powiodły się.");return;}
        set_progress(instalator_podpostep(75,92,uint32_t(wallpaper_bytes_done),uint32_t(wallpaper_bytes_total)),
                     POSTEP_TAPETY,uint32_t(wallpaper_bytes_done),uint32_t(wallpaper_bytes_total));
        if(done){set_progress(92,POSTEP_WERYFIKACJA,wallpapers_done,wallpapers_total);stage=INST_WERYFIKACJA;}
    }else if(stage==INST_WERYFIKACJA){
        if(verify_phase==0){
            klog_serial("[INSTALL] progress=92 stage=VERIFY");
            if(!psf_flush_cache()||!psf_weryfikuj_dysk_start(&verify_meta_total)||
               !kernel_weryfikuj_system_start(&verify_system_total)||!tapety_weryfikuj_start(&verify_wall_total)){
                fail(INST_IO,"Weryfikacja: metadane / przygotowanie odczytu kontrolnego.");return;}
            verify_phase=1;
        }else if(verify_phase==1){
            bool done=false;if(!psf_weryfikuj_dysk_step(&verify_meta_done,&done)){
                fail(INST_IO,"Weryfikacja: metadane BSP2.");return;}
            if(done)verify_phase=2;
        }else if(verify_phase==2){
            bool done=false;if(!kernel_weryfikuj_system_step(&verify_system_done,&done)){
                fail(INST_IO,"Weryfikacja: pliki systemowe.");return;}
            if(done)verify_phase=3;
        }else{
            bool done=false;if(!tapety_weryfikuj_step(&verify_wall_done,&done)){
                fail(INST_IO,"Weryfikacja: tapety / CRC.");return;}
            if(done)verify_phase=4;
        }
        const uint64_t total=verify_meta_total*4096+verify_system_total+verify_wall_total;
        const uint64_t done=verify_meta_done*4096+verify_system_done+verify_wall_done;
        if(total>UINT32_MAX||done>UINT32_MAX){fail(INST_IO,"Weryfikacja: licznik bajtów poza zakresem.");return;}
        set_progress(instalator_podpostep(92,99,uint32_t(done),uint32_t(total)),POSTEP_WERYFIKACJA,
                     uint32_t(done),uint32_t(total));
        if(verify_phase==4){set_progress(99,POSTEP_WERYFIKACJA);stage=INST_ZNACZNIK;}
    }else if(stage==INST_ZNACZNIK){
        if (!Storage::rozruch_weryfikuj_uklad_dysku(layout.bsp.device)) {
            fail(INST_DYSK_BLAD, "Układ dysku niezgodny z wymaganiami zarezerwowanych sektorów stanu rozruchu (LBA 2046/2047).");
            return;
        }

        struct bursztyn_rozruch_stan stan_poczatkowy{};
        rozruch_stan_poczatkowy(&stan_poczatkowy, 0, 1, 0);

        if (!Storage::rozruch_zapisz_sektory(layout.bsp.device, &stan_poczatkowy)) {
            fail(INST_IO, "Zapis sektorów stanu rozruchu (LBA 2046/2047) nie powiódł się."); return;
        }
        bursztyn_rozruch_stan stan_odczyt{};
        if (!Storage::rozruch_odczytaj_sektory(layout.bsp.device, &stan_odczyt) ||
            stan_odczyt.slot_potwierdzony != BURSZTYN_SLOT_A) {
            fail(INST_IO, "Weryfikacja sektorów stanu rozruchu nie powiodła się."); return;
        }

        (void)zapewnij_katalog("/aktualizacje");
        (void)usun_twor("/aktualizacje/stan_rozruchu.k0");
        (void)usun_twor("/aktualizacje/stan_rozruchu.k1");
        if (!utworz_plik("/aktualizacje/stan_rozruchu.k0") ||
            !zapisz_do_pliku("/aktualizacje/stan_rozruchu.k0", (const char*)&stan_poczatkowy, sizeof(stan_poczatkowy)) ||
            !utworz_plik("/aktualizacje/stan_rozruchu.k1") ||
            !zapisz_do_pliku("/aktualizacje/stan_rozruchu.k1", (const char*)&stan_poczatkowy, sizeof(stan_poczatkowy))) {
            fail(INST_IO, "Zapis kopii stanu rozruchu na BSP2 nie powiódł się."); return;
        }

        char p_potw[4]{}, p_oczek[16]{}; uint32_t p_prob = 99;
        if (!Storage::esp_odczytaj_grubenv(layout.esp, p_potw, p_oczek, &p_prob) ||
            p_potw[0] != 'A' || p_prob != 0) {
            fail(INST_IO, "Weryfikacja bloku grubenv na ESP nie powiodła się."); return;
        }

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
void instalator_zamknij_proces(int pid){
    if(pid!=owner)return;
    if(stage>INST_GOTOWY&&stage<INST_KONIEC){
        fail(INST_ZAJETY,"Proces instalatora zakonczyl sie podczas transakcji; dalsze zapisy zatrzymane.");
    }
    owner=-1;owner_cr3=0;nonce=0;
    blokowy_zezwol_instalatorowi(0,false);
}
bool instalator_obsluz(const proces_t&p,const InstalatorZadanie&r,InstalatorOdpowiedz*out){
    if(!out||r.wersja!=1)return false;
    if(!mode||p.pid!=uint64_t(owner)||p.cr3!=owner_cr3||!pzb_ma_co_najmniej_poziom(&p,PZB_ZAUFANE)||!pzb_ma_prawa(&p,PRAWO_SYSTEM_CONFIG)){
        *out={};out->wersja=1;out->status=INST_UPRAWNIENIA;return true;}
    if(r.operacja==INST_STAN||r.operacja==INST_DYSK){snapshot(out,r.dysk);return true;}
    if(r.operacja==INST_PODGLAD && stage==INST_GOTOWY){
        status=0;error[0]=0;selected=UINT32_MAX;nonce=0;
        progress=0;progress_stage=POSTEP_PRZYGOTOWANIE;progress_done=progress_total=0;
        gpt_state={};gpt_started=false;fat_state={};fat_started=false;
        wallpapers_started=false;wallpaper_bytes_done=wallpaper_bytes_total=0;
        system_started=false;system_bytes_done=system_bytes_total=0;
        verify_phase=0;verify_meta_done=verify_meta_total=0;
        verify_system_done=verify_system_total=verify_wall_done=verify_wall_total=0;
        perf_stage_us=perf_longest_us=perf_irq=perf_switches=0;
        bsp_phase=0;bsp_done=bsp_total=0;
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
        else {status=0;set_progress(0,POSTEP_GPT);stage=INST_GPT;
            perf_stage_irq_start=scheduler_liczba_irq_timera();
            perf_stage_switch_start=scheduler_liczba_przelaczen();
            klog_serial("[INSTALL] Explicit final confirmation accepted. No disk sectors written before this point.");}
    }else if(r.operacja==INST_KROK&&stage>INST_GOTOWY&&stage<INST_KONIEC&&r.token==nonce&&r.dysk==selected){
        if(layout.bsp.device!=blokowy_dysk(selected)){
            fail(INST_DYSK_BLAD,"Transakcja utraciła powiązanie z wybranym dyskiem.");
        }else{
            const uint32_t old_stage=progress_stage;const uint64_t start=czas_monotoniczny_us();
            const uint64_t irq_before=scheduler_liczba_irq_timera(),switch_before=scheduler_liczba_przelaczen();
            uint64_t flags=0;asm volatile("pushfq; popq %0":"=r"(flags));
            blokowy_zezwol_instalatorowi(selected,true);perform();blokowy_zezwol_instalatorowi(selected,false);
            const uint64_t elapsed=czas_monotoniczny_us()-start;perf_stage_us+=elapsed;
            if(elapsed>perf_longest_us)perf_longest_us=elapsed;
            perf_irq+=scheduler_liczba_irq_timera()-irq_before;
            perf_switches+=scheduler_liczba_przelaczen()-switch_before;
            if(progress_stage!=old_stage){
                perf_irq=scheduler_liczba_irq_timera()-perf_stage_irq_start;
                perf_switches=scheduler_liczba_przelaczen()-perf_stage_switch_start;
                log_perf(old_stage,(flags&(1ULL<<9))!=0);
                perf_stage_us=perf_longest_us=perf_irq=perf_switches=0;
                perf_stage_irq_start=scheduler_liczba_irq_timera();
                perf_stage_switch_start=scheduler_liczba_przelaczen();
            }
        }
    }else status=INST_ZAJETY;
    snapshot(out,r.dysk);return true;
}
