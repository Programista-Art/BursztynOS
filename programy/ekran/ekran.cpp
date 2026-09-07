/* Ustawienia ekranu: pojedyncze okno, prawdziwe mozliwosci HAL przez BWS69/70. */
#include "../../loader.h"
#include "../../bursztyn_ui/bursztyn_ui.h"

using namespace BursztynUI;

extern "C" void _start();
static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 0x1000ULL;
static constexpr uint64_t BUR_TEKST_ROZMIAR = 0x30000ULL;
static constexpr uint64_t BUR_TEKST_WIRTUALNY = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE = 0x31000ULL;
static constexpr uint64_t BUR_DANE_ROZMIAR = 0x20000ULL;
static constexpr uint64_t BUR_DANE_WIRTUALNY = 0x1031000ULL;

extern "C" {
__attribute__((section(".naglowek"), used)) NaglowekBur naglowek = {
    {'B','U','R','\0'}, (uint64_t)&_start,
    BUR_TEKST_PRZESUNIECIE, BUR_TEKST_ROZMIAR, BUR_TEKST_WIRTUALNY,
    BUR_DANE_PRZESUNIECIE, BUR_DANE_ROZMIAR, BUR_DANE_WIRTUALNY
};
}
static_assert(sizeof(NaglowekBur) == 60U, "Naglowek .bur musi miec 60 bajtow");

#include "klient.h"
namespace {
uint64_t endpoint=0;
EkranOdpowiedz stan{};
ListaRozwijana *tryby=nullptr,*skale=nullptr;
Etykieta *info=nullptr,*backend=nullptr,*potwierdzenie=nullptr;
Panel* panel_potwierdzenia=nullptr;
Okno* okno_glowne=nullptr;
Przycisk *zastosuj_btn=nullptr,*domyslne_btn=nullptr;
void tekst(char*out,size_t cap,const char*prefix,uint32_t a,const char*middle="",uint32_t b=0,const char*suffix=""){
    size_t p=0;auto str=[&](const char*s){while(*s&&p+1<cap)out[p++]=*s++;};
    auto num=[&](uint32_t n){char r[10];unsigned i=0;do{r[i++]=char('0'+n%10);n/=10;}while(n);while(i&&p+1<cap)out[p++]=r[--i];};
    str(prefix);num(a);str(middle);if(b)num(b);str(suffix);out[p]=0;
}
void synchronizuj(bool wybor){
    if(wybor){
        int idx=0;
        for(unsigned i=0;i<stan.liczba_trybow;++i)if(ekran_ten_sam(stan.tryby[i],stan.aktywny))idx=i;
        tryby->ustaw_wybrany_indeks(idx);skale->ustaw_wybrany_indeks((stan.skala-100)/25);
    }
    const bool pending=stan.transakcja!=0;
    tryby->ustaw_wlaczona(!pending&&stan.mozliwosci.mozna_zmienic_runtime);
    skale->ustaw_wlaczona(!pending);zastosuj_btn->ustaw_wlaczona(!pending);domyslne_btn->ustaw_wlaczona(!pending);
    char b[160]{};
    tekst(b,sizeof(b),"Bieżąca rozdzielczość: ",stan.aktywny.szerokosc," × ",stan.aktywny.wysokosc);
    info->ustaw_tekst(b);
    const bool was=panel_potwierdzenia->czy_widoczna();
    panel_potwierdzenia->ustaw_widoczna(pending);
    if(pending){tekst(b,sizeof(b),"Zachować te ustawienia ekranu? Pozostało ",stan.pozostalo_sekund," s");potwierdzenie->ustaw_tekst(b);}
    if(was!=pending)okno_glowne->przelicz_uklad();
}
void wynik(uint32_t status){
    if(status==EKRAN_OK||status==EKRAN_OCZEKUJE)return;
    info->ustaw_tekst(status==EKRAN_PAMIEC?"Za mało pamięci na bufory ekranu.":
        status==EKRAN_ZAPIS?"Nie udało się zapisać ustawień.":
        status==EKRAN_NIEOBSLUGIWANY?"Ten tryb nie jest obsługiwany przez sterownik.":
        status==EKRAN_ZAJETY?"Trwa potwierdzanie innej zmiany.":"Nie udało się zastosować ustawień.");
}
void zastosuj(Przycisk*,void*){
    const int i=tryby->wybrany_indeks(),s=skale->wybrany_indeks();
    if(i<0||i>=int(stan.liczba_trybow)||s<0||s>4)return;
    EkranZadanie r{};r.operacja=EKRAN_ZASTOSUJ;r.szerokosc=stan.tryby[i].szerokosc;r.wysokosc=stan.tryby[i].wysokosc;r.bpp=32;r.skala=100+s*25;
    if(!ekran_zapytaj(endpoint,r,&stan)){info->ustaw_tekst("Brak odpowiedzi usługi ekranu.");return;}
    synchronizuj(true);wynik(stan.status);
}
void potwierdz(Przycisk*,void* dane){
    EkranZadanie r{};r.operacja=dane?EKRAN_PRZYWROC:EKRAN_ZACHOWAJ;r.transakcja=stan.transakcja;
    if(ekran_zapytaj(endpoint,r,&stan)){synchronizuj(true);wynik(stan.status);}
}
void anuluj(Przycisk*,void*){
    if(stan.transakcja)potwierdz(nullptr,reinterpret_cast<void*>(1));
    else okno_glowne->zamknij();
}
void domyslne(Przycisk*,void*){
    // Firmware mode remains the safe default; scale is independently 100%.
    tryby->ustaw_wybrany_indeks(0);skale->ustaw_wybrany_indeks(0);
}
class OknoEkranu:public Okno{
public:
    OknoEkranu():Okno("Ustawienia ekranu",700,520){}
    void obsluz_zdarzenie(const bws_zdarzenie&e) override {
        Okno::obsluz_zdarzenie(e);
        if(e.typ==BWS_ZDARZENIE_EKRAN||(e.typ==BWS_ZDARZENIE_TIMER&&stan.transakcja)){
            EkranZadanie r{};r.operacja=EKRAN_STAN;
            if(ekran_zapytaj(endpoint,r,&stan))synchronizuj(true);
        }
    }
};
}
extern "C" __attribute__((noreturn)) void _start(){
    AplikacjaGUI app;OknoEkranu okno;okno_glowne=&okno;app.ustaw_glowne_okno(&okno);
    endpoint=ipc_create();EkranZadanie req{};req.operacja=EKRAN_STAN;
    if(!ekran_zapytaj(endpoint,req,&stan))gui_zakoncz_aplikacje();
    auto*root=static_cast<Panel*>(okno.kontrolka_glowna());root->ustaw_wypelnienie(Marginesy(12));root->ustaw_odstep(8);
    root->dodaj<Etykieta>("Monitor główny");
    backend=&root->dodaj<Etykieta>(stan.mozliwosci.backend==EKRAN_BOCHS?"Sterownik: Bochs VBE":stan.mozliwosci.backend==EKRAN_GOP?"Sterownik: UEFI GOP":"Sterownik: VESA");
    root->dodaj<Etykieta>("Rozdzielczość");tryby=&root->dodaj<ListaRozwijana>();tryby->ustaw_preferowany_rozmiar(Rozmiar(-1,30));
    for(unsigned i=0;i<stan.liczba_trybow;++i){
        const auto&t=stan.tryby[i];char b[80]{};
        const char*f=t.szerokosc==1920&&t.wysokosc==1080?" (Full HD)":t.szerokosc==2560&&t.wysokosc==1440?" (QHD)":t.szerokosc==3840&&t.wysokosc==2160?" (4K UHD)":t.szerokosc==1280&&t.wysokosc==720?" (HD)":"";
        tekst(b,sizeof(b),"",t.szerokosc," × ",t.wysokosc,f);tryby->dodaj_element(b);
    }
    root->dodaj<Etykieta>("Skala interfejsu");skale=&root->dodaj<ListaRozwijana>();skale->ustaw_preferowany_rozmiar(Rozmiar(-1,30));
    const char* procenty[]={"100%","125%","150%","175%","200%"};
    for(auto*s:procenty)skale->dodaj_element(s);
    info=&root->dodaj<Etykieta>("");
    auto&limit=root->dodaj<Etykieta>(stan.mozliwosci.mozna_zmienic_runtime?"Zmiana rozdzielczości dostępna bez restartu.":"Bieżący sterownik nie obsługuje zmiany rozdzielczości podczas pracy systemu.");
    limit.ustaw_zawijanie(true);
    panel_potwierdzenia=&root->dodaj<Panel>(Orientacja::Pionowa);panel_potwierdzenia->ustaw_odstep(6);
    potwierdzenie=&panel_potwierdzenia->dodaj<Etykieta>("");potwierdzenie->ustaw_zawijanie(true);
    auto&confirm=panel_potwierdzenia->dodaj<Panel>(Orientacja::Pozioma);confirm.ustaw_odstep(8);
    confirm.dodaj<Przycisk>("Zachowaj").przy_kliknieciu(potwierdz,nullptr);
    confirm.dodaj<Przycisk>("Przywróć").przy_kliknieciu(potwierdz,reinterpret_cast<void*>(1));
    auto&buttons=root->dodaj<Panel>(Orientacja::Pozioma);buttons.ustaw_odstep(8);
    zastosuj_btn=&buttons.dodaj<Przycisk>("Zastosuj");zastosuj_btn->przy_kliknieciu(zastosuj);
    buttons.dodaj<Przycisk>("Anuluj").przy_kliknieciu(anuluj);
    domyslne_btn=&buttons.dodaj<Przycisk>("Przywróć domyślne");domyslne_btn->przy_kliknieciu(domyslne);
    synchronizuj(true);okno.wysrodkuj();app.uruchom();gui_zakoncz_aplikacje();
}
