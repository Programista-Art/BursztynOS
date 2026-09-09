#include "../../loader.h"
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "../../instalator_format.h"
using namespace BursztynUI;
extern "C" void _start();
extern "C" {
__attribute__((section(".naglowek"),used)) NaglowekBur naglowek={
    {'B','U','R',0},(uint64_t)&_start,0x1000,0x30000,0x1001000,0x31000,0x20000,0x1031000};
}
namespace {
void number(char*out,const char*prefix,uint64_t n,const char*suffix=""){
    unsigned p=0;while(*prefix&&p<200)out[p++]=*prefix++;char r[24];unsigned k=0;
    do{r[k++]=char('0'+n%10);n/=10;}while(n);while(k)out[p++]=r[--k];
    while(*suffix&&p<240)out[p++]=*suffix++;out[p]=0;
}
bool same(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
class Wizard : public Panel {
    Okno*window;uint64_t ep;InstalatorOdpowiedz state{};uint32_t page=0,selected=UINT32_MAX,offset=0;
    uint64_t confirmation=0;bool busy=false;uint64_t last=0;
    Etykieta*lines[12]{};Przycisk*disks[4]{};Przycisk*next,*back,*more;
    PoleTekstowe*word;PoleWyboru*source;
    bool query(uint32_t op,uint32_t id=UINT32_MAX){
        InstalatorZadanie r{};r.wersja=1;r.operacja=op;r.odpowiedz=ep;r.dysk=id;r.token=confirmation;
        r.zgoda_zrodlo=source&&source->czy_zaznaczone();
        if(word)for(unsigned i=0;i<15&&word->tekst()[i];++i)r.potwierdzenie[i]=word->tekst()[i];
        BwsIpcWyslij s{};s.wersja=1;s.dane=(uint64_t)&r;s.rozmiar=sizeof(r);
        BwsIpcOdbierz a{};a.wersja=1;
        if(!ipc_send(INSTALATOR_IPC,&s)||!ipc_receive(ep,&a)||a.nadawca_pid||a.flagi||a.rozmiar!=sizeof(state))return false;
        for(unsigned i=0;i<sizeof(state);++i)((uint8_t*)&state)[i]=a.dane[i];return state.wersja==1;
    }
    void line(unsigned i,const char*s){lines[i]->ustaw_tekst(s);lines[i]->pokaz();}
    void confirm_enabled(){next->ustaw_wlaczona(page!=4||(same(word->tekst(),"BURSTZTYN")&&source->czy_zaznaczone()));}
    void render(){
        for(auto*l:lines)l->ukryj();for(auto*b:disks)b->ukryj();word->ukryj();source->ukryj();more->ukryj();
        next->pokaz();back->ustaw_widoczna(page>0&&page<5);next->ustaw_wlaczona(true);next->ustaw_tekst("Dalej");
        char text[256]{};
        if(page==0){query(INST_STAN);line(0,"Witamy w instalatorze Bursztyn OS");
            line(2,state.uefi?"Urządzenie startowe: UEFI x86-64":"Start BIOS — instalacja wymaga uruchomienia w UEFI");
            number(text,"Wykryte dyski SATA / AHCI: ",state.liczba_dyskow);line(3,text);
            line(4,state.gop?"Grafika: UEFI GOP":"Grafika: bieżący framebuffer (np. Bochs VBE)");
            line(5,state.xhci?"USB xHCI: aktywny, urządzenia HID":"USB xHCI: brak aktywnego sterownika; sprawdź klawiaturę i mysz");
            line(7,"NVMe jeszcze nieobsługiwany");line(8,"IDE i USB mass storage nie są obsługiwane jako dyski docelowe.");
            line(10,"Instalacja usuwa dane. Zrób kopię zapasową przed kontynuacją.");next->ustaw_wlaczona(state.uefi&&state.liczba_dyskow);
        }else if(page==1){line(0,"Wybierz dysk docelowy");line(1,"Wybór i podgląd nie zapisują żadnych sektorów.");
            for(unsigned i=0;i<4&&offset+i<state.liczba_dyskow;++i){query(INST_DYSK,offset+i);char prefix[100]{};
                unsigned k=0;for(;state.model[k]&&k<40;++k)prefix[k]=state.model[k];prefix[k++]=' ';prefix[k++]='|';prefix[k++]=' ';prefix[k]=0;
                number(text,prefix,state.sektor==512?state.sektory/2048:0," MiB | SATA / AHCI");
                disks[i]->ustaw_tekst(state.nosnik_instalacyjny?"Nośnik instalacyjny — chroniony":text);
                disks[i]->ustaw_wlaczona(state.zapis&&!state.nosnik_instalacyjny);disks[i]->ustaw_znacznik(offset+i);disks[i]->pokaz();}
            more->ustaw_widoczna(state.liczba_dyskow>4);
            if(selected!=UINT32_MAX){query(INST_DYSK,selected);line(8,state.model);
                number(text,"Sektor logiczny: ",state.sektor," B | odczyt / zapis / ATA FLUSH");line(9,text);
                line(10,state.bursztyn?"Wykryto partycję Bursztyn OS (zostanie usunięta).":"Brak rozpoznanej partycji Bursztyn OS.");}
            next->ustaw_wlaczona(selected!=UINT32_MAX);
        }else if(page==2){line(0,"Sposób instalacji");line(2,"Użyj całego dysku — usuń istniejący podział i utwórz nowy GPT.");
            line(4,"Własny rozmiar Bursztyn OS: niedostępny w tej wersji.");
            line(5,"Instalacja w wolnym miejscu istniejącego dysku nie jest obsługiwana.");
            line(6,"Instalator nie przesuwa ani nie zmniejsza istniejących partycji.");
            line(8,"Limit BSP2: 2048 MiB. Pozostałe miejsce pozostanie nieprzydzielone.");
            next->ustaw_tekst("Użyj całego dysku");
        }else if(page==3||page==4){line(0,"UWAGA — wszystkie dane na tym dysku zostaną usunięte");query(INST_DYSK,selected);line(2,state.model);
            number(text,"Dysk: ",state.sektory/2048," MiB | SATA / AHCI | sektor 512 B");line(3,text);
            number(text,"EFI System Partition: ",state.esp_mib," MiB (FAT32)");line(4,text);
            number(text,"Bursztyn OS: ",state.bsp_mib," MiB (BSP2)");line(5,text);
            number(text,"Pozostałe miejsce pozostanie nieprzydzielone: ",state.nieprzydzielone_mib," MiB");line(6,text);
            if(page==3)next->ustaw_tekst("Potwierdź wymazanie");
            else {line(7,"Nie można niezawodnie wskazać fizycznego źródła rozruchu.");
                line(8,"Sprawdź wybrany model i wpisz BURSTZTYN, aby zezwolić na wymazanie.");
                source->pokaz();word->pokaz();next->ustaw_tekst("Wymaż dysk i zainstaluj");confirm_enabled();}
        }else if(page==5){line(0,"Instalowanie Bursztyn OS");
            const char*stages[]={"Przygotowanie","GPT — zapis, FLUSH i weryfikacja","ESP FAT32 — GRUB i kernel","Formatowanie BSP2","Programy i tapety","Weryfikacja plików z dysku","Zapisywanie znacznika ukończenia"};
            line(3,state.etap<7?stages[state.etap]:"Kończenie");line(6,"Nie wyłączaj komputera i nie odłączaj dysku.");next->ukryj();
        }else if(page==6){line(0,"Gotowe — instalacja zweryfikowana");line(3,"Wyłącz komputer, odłącz nośnik instalacyjny i uruchom z dysku.");
            line(5,"Rozruch: UEFI / EFI/BOOT/BOOTX64.EFI (bez zmiany NVRAM).");
            line(8,"Testy QEMU nie stanowią ręcznej weryfikacji na fizycznym komputerze.");next->ustaw_tekst("Zamknij");
        }else{line(0,"Instalacja nie powiodła się");line(3,state.blad[0]?state.blad:"Walidacja dysku, uprawnień lub plików instalacyjnych nie powiodła się.");
            number(text,"Kod błędu: ",state.status);line(5,text);line(7,"Dalsze zapisy zostały zatrzymane. Nie uruchamiaj niepełnej instalacji.");next->ustaw_tekst("Zamknij");}
        window->przelicz_uklad();uniewaznij();
    }
    void advance(){
        if(page>=6)gui_zakoncz_aplikacje();
        if(page==2){if(!query(INST_PODGLAD,selected)||state.status){page=7;render();return;}confirmation=state.token;}
        if(page==4){if(!query(INST_POTWIERDZ,selected)||state.status){page=7;render();return;}busy=true;}
        ++page;render();
    }
public:
    Wizard(Okno*w):Panel(Orientacja::Pionowa),window(w),ep(ipc_create()){
        ustaw_uklad_automatyczny(false);ustaw_wypelnienie(Marginesy(12));
        for(unsigned i=0;i<12;++i){lines[i]=&dodaj<Etykieta>("");lines[i]->ustaw_pozycje(16,12+32*i);lines[i]->ustaw_preferowany_rozmiar(Rozmiar(780,28));}
        for(unsigned i=0;i<4;++i){disks[i]=&dodaj<Przycisk>("");disks[i]->ustaw_pozycje(16,82+48*i);disks[i]->ustaw_preferowany_rozmiar(Rozmiar(780,40));
            disks[i]->przy_kliknieciu([](Przycisk*b,void*p){auto*s=(Wizard*)p;s->selected=b->znacznik();s->render();},this);}
        more=&dodaj<Przycisk>("Kolejne dyski");more->ustaw_pozycje(580,285);more->przy_kliknieciu([](Przycisk*,void*p){auto*s=(Wizard*)p;s->offset+=4;if(s->offset>=s->state.liczba_dyskow)s->offset=0;s->render();},this);
        word=&dodaj<PoleTekstowe>("BURSTZTYN");word->ustaw_pozycje(16,370);word->ustaw_preferowany_rozmiar(Rozmiar(330,36));
        word->przy_zmianie([](PoleTekstowe*,const char*,void*p){((Wizard*)p)->confirm_enabled();},this);
        source=&dodaj<PoleWyboru>("Potwierdzam: wybrany dysk nie jest nośnikiem instalacyjnym.");source->ustaw_pozycje(16,330);
        source->przy_przelaczeniu([](PoleWyboru*,bool,void*p){((Wizard*)p)->confirm_enabled();},this);
        back=&dodaj<Przycisk>("Wróć");back->ustaw_pozycje(16,450);back->ustaw_preferowany_rozmiar(Rozmiar(120,40));
        back->przy_kliknieciu([](Przycisk*,void*p){auto*s=(Wizard*)p;if(s->page&&s->page<5){--s->page;s->word->ustaw_tekst("");s->source->ustaw_zaznaczone(false);s->render();}},this);
        next=&dodaj<Przycisk>("Dalej");next->ustaw_pozycje(500,450);next->ustaw_preferowany_rozmiar(Rozmiar(295,40));
        next->przy_kliknieciu([](Przycisk*,void*p){((Wizard*)p)->advance();},this);
    }
    void rozmiesc(Prostokat r) override{
        obszar_=r;
        Punkt parent=rodzic_?rodzic_->obszar_ekranu().pozycja():Punkt(0,0);
        obszar_ekranu_=Prostokat(parent+r.pozycja(),r.rozmiar());
        for(unsigned i=0;i<12;++i)lines[i]->rozmiesc(Prostokat(16,12+32*i,r.szerokosc-32,28));
        for(unsigned i=0;i<4;++i)disks[i]->rozmiesc(Prostokat(16,82+48*i,r.szerokosc-32,40));
        more->rozmiesc(Prostokat(r.szerokosc-220,285,200,36));
        source->rozmiesc(Prostokat(16,330,r.szerokosc-32,32));
        word->rozmiesc(Prostokat(16,370,330,36));
        back->rozmiesc(Prostokat(16,r.wysokosc-55,120,40));
        next->rozmiesc(Prostokat(r.szerokosc-315,r.wysokosc-55,295,40));
    }
    void start(){render();}
    bool czy_wymaga_animacji()const override{return busy;}
    void krok_animacji(uint64_t now)override{
        if(!busy||now-last<100000000ULL)return;last=now;
        if(!query(INST_KROK,selected)||state.status||state.etap==INST_BLAD){busy=false;page=7;}
        else if(state.etap==INST_KONIEC){busy=false;page=6;}
        render();
    }
};
}
extern "C" __attribute__((noreturn)) void _start(){
    AplikacjaGUI app;Okno window("Instalator Bursztyn OS",840,570);
    auto*wizard=new Wizard(&window);window.ustaw_kontrolke_glowna(wizard);wizard->start();window.wysrodkuj();
    app.uruchom(window);gui_zakoncz_aplikacje();
}
