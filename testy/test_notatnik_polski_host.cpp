// Notatnik: polskie wprowadzanie (AltGr) przez prawdziwy sterownik klawiatury,
// edycja Unicode, natywne dialogi Otworz/Zapisz jako, menu kontekstowe i
// ochrona niezapisanych zmian. Deterministyczny test hosta (ASan/UBSan), bez QEMU.
//
// Sciezka zdarzen: klawiatura_obsluz_klawisz (sterowniki/wejscie/klawiatura.cpp,
// kody Linux jak z virtio-input/USB/PS2) -> zaktualizuj_*_gui (strumien BWS) ->
// OknoNotatnika::obsluz_zdarzenie -> BursztynUI Okno -> PoleWielowierszowe.
#define BSF_NATIVE 1
#define NOTATNIK_ALOKATOR_TESTOWY 1
#define _start notepad_test_entry
#define __dso_handle notepad_test_dso_handle
#include "../programy/notatnik/notatnik.cpp"
#undef _start
#undef __dso_handle
#include "../testy/bursztyn_ui_host_fixture.h"
#include "../jadro/bws/okna_podrzedne.h"
#include <string>
#include <map>
#include <set>
#include <vector>
#include <fstream>
#include <iterator>
#include <functional>
#include <memory>

static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);abort();}}while(0)

// ---------------------------------------------------------------------------
// Makieta systemu plikow (BWS 4, 6, 46, 47 oraz API plikow Ring 3)
// ---------------------------------------------------------------------------
struct Wpis { bool katalog{false}; std::string dane; };
static std::map<std::string,Wpis> fs;
static bool awaria_zapisu=false, uszkodzony_zapis=false, awaria_tworzenia=false;
static unsigned liczba_zapisow=0;

// Wstrzykiwanie bledow per operacja i sciezka (deterministyczne).
enum class Awaria { Brak, Odmowa, Czesciowy, Uszkodzony };
static std::function<Awaria(const std::string& op,const std::string& sciezka)> wtracenie;
static size_t pojemnosc_dysku=SIZE_MAX;       // limit sumy bajtow plikow (brak miejsca)
static bool awaria_alokacji=false, awaria_usuwania=false;
// Migawki stanu plikow po kazdej mutacji: symulacja przerwania w dowolnym punkcie.
static bool rejestruj_migawki=false;
static std::vector<std::map<std::string,std::string>> migawki;

static std::set<std::string> chronione;   // katalogi systemowe: odmowa tworzenia podkatalogow
static unsigned utworzone_katalogi=0;
static std::string rodzic(const std::string& p){size_t s=p.rfind('/');return s==0?"/":p.substr(0,s);}
static void dodaj_katalog(const std::string& p){fs[p].katalog=true;}
static void dodaj_plik(const std::string& p,const std::string& d){fs[p]=Wpis{false,d};}
static Awaria awaria_dla(const char* op,const char* p){return wtracenie?wtracenie(op,p):Awaria::Brak;}
static void migawka(){if(!rejestruj_migawki)return;std::map<std::string,std::string> m;
    for(auto& [k,v]:fs)if(!v.katalog)m[k]=v.dane;migawki.push_back(std::move(m));}
static size_t zajete_bez(const std::string& p){size_t z=0;for(auto& [k,v]:fs)if(!v.katalog&&k!=p)z+=v.dane.size();return z;}

static uint64_t hook(uint64_t nr,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t){
    const char* path=reinterpret_cast<const char*>(a1);
    if(nr==4){auto f=fs.find(path?path:"");if(f==fs.end()||f->second.katalog)return 0;*reinterpret_cast<uint32_t*>(a2)=uint32_t(f->second.dane.size());return 1;}
    if(nr==47){auto f=fs.find(path?path:"");if(f==fs.end())return 0;auto* m=reinterpret_cast<BwsMetadanePliku*>(a2);*m={};
        m->typ=f->second.katalog?BWS_TYP_KATALOG:BWS_TYP_PLIK;m->rozmiar=f->second.dane.size();return 1;}
    if(nr==46){std::string p(path);const bool status=a2==BWS_STATUS_PLIKOWY;
        // Jak PZB: katalogi systemowe (poziom jadra) nie przyjmuja nowych podkatalogow.
        WynikPlikowy w=WynikPlikowy::Ok;
        if(chronione.count(rodzic(p)))w=WynikPlikowy::BrakUprawnien;
        else if(!fs.count(rodzic(p))||!fs[rodzic(p)].katalog)w=WynikPlikowy::Brak;
        else if(fs.count(p))w=WynikPlikowy::Istnieje;
        if(w==WynikPlikowy::Ok){dodaj_katalog(p);++utworzone_katalogi;}
        return status?uint64_t(w):(w==WynikPlikowy::Ok?1:0);}
    if(nr==6){std::string dir(path);auto d=fs.find(dir);if(d==fs.end()||!d->second.katalog)return 0;
        std::string out;std::string pref=dir=="/"?"/":dir+"/";
        for(auto& [k,v]:fs){if(k.size()<=pref.size()||k.compare(0,pref.size(),pref))continue;std::string rest=k.substr(pref.size());
            if(rest.find('/')!=std::string::npos)continue;out+=(v.katalog?"[KAT]  ":"[PLIK] ")+rest+"\n";}
        if(out.size()+1>a3)return 0;memcpy(reinterpret_cast<char*>(a2),out.c_str(),out.size()+1);return 1;}
    return 0;
}
bool czytaj_plik(const char* p,char* out,uint32_t cap){auto f=fs.find(p);if(f==fs.end()||f->second.katalog||f->second.dane.size()>cap)return false;memcpy(out,f->second.dane.data(),f->second.dane.size());return true;}
bool utworz(const char* p){if(awaria_tworzenia||awaria_dla("utworz",p)!=Awaria::Brak||fs.count(p)||!fs.count(rodzic(p))||!fs[rodzic(p)].katalog)return false;
    dodaj_plik(p,"");migawka();return true;}
// Jak PSF zapisz_do_pliku: zapis w miejscu; brak miejsca/blad w trakcie zostawia
// plik czesciowo nadpisany (prefiks nowych danych, reszta starych) i zwraca false.
bool zapisz_plik(const char* p,const char* d,uint32_t n){++liczba_zapisow;auto f=fs.find(p);if(awaria_zapisu||f==fs.end()||f->second.katalog)return false;
    const Awaria a=awaria_dla("zapisz",p);if(a==Awaria::Odmowa)return false;
    std::string nowe(d,n);const size_t wolne=pojemnosc_dysku-std::min(pojemnosc_dysku,zajete_bez(p));
    if(a==Awaria::Czesciowy||n>wolne){size_t ile=a==Awaria::Czesciowy?n/2:wolne;std::string& stare=f->second.dane;
        std::string wynik=nowe.substr(0,ile);if(stare.size()>ile&&n>ile)wynik+=stare.substr(ile,std::min(stare.size(),size_t(n))-ile);
        stare=wynik;migawka();return false;}
    f->second.dane=nowe;if((uszkodzony_zapis||a==Awaria::Uszkodzony)&&n)f->second.dane.back()^=0x20;migawka();return true;}
bool usun_twor_uzytkownika(const char* p){if(awaria_usuwania||awaria_dla("usun",p)!=Awaria::Brak)return false;auto f=fs.find(p);
    if(f==fs.end()||f->second.katalog)return false;fs.erase(f);migawka();return true;}
static char* notatnik_alokuj(uint32_t n){return awaria_alokacji?nullptr:new char[n];}
// BWS 101 (okna podrzedne): rejestrator zlecen Notatnika w miejsce jadra.
struct ZlecenieOkna{uint32_t rodzaj;std::string tytul,tresc;};
static std::vector<ZlecenieOkna> zlecenia_okien;
static uint32_t wynik_okna_podrzednego=1;
uint32_t gui_okno_podrzedne_otworz(uint32_t rodzaj,const char* tytul,const char* tresc,uint32_t dl){
    zlecenia_okien.push_back({rodzaj,tytul?tytul:"",std::string(tresc?tresc:"",dl)});return wynik_okna_podrzednego;}
static std::string argument_startowy;
bool pobierz_argument_startowy(char* out,uint32_t cap){if(argument_startowy.size()+1>cap)return false;memcpy(out,argument_startowy.c_str(),argument_startowy.size()+1);return true;}
void wypisz(const char*){}

// ---------------------------------------------------------------------------
// Strumien BWS ze sterownika klawiatury
// ---------------------------------------------------------------------------
static std::vector<bws_zdarzenie> strumien;
extern "C" bool zaktualizuj_klawisz_fizyczny_gui(uint32_t code,bool down,uint32_t mods){bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;e.kod=code;e.x=down;e.przyciski=mods;strumien.push_back(e);return true;}
extern "C" bool zaktualizuj_tekst_gui(uint32_t cp,uint32_t mods){bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_TEXT_INPUT;e.kod=cp;e.przyciski=mods;strumien.push_back(e);return true;}
extern "C" bool zaktualizuj_klawiature_gui(char c){bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_KLAWISZ;e.kod=uint8_t(c);strumien.push_back(e);return true;}
extern "C" void scheduler_wybudz_klawiature(){}
extern "C" void klawiatura_obsluz_klawisz(uint16_t,uint32_t);
extern "C" void klawiatura_zresetuj_stan(void);

enum : uint16_t { K_ESC=1,K_BS=14,K_TAB=15,K_ENTER=28,K_LCTRL=29,K_LSHIFT=42,K_LALT=56,K_SPACE=57,K_CAPS=58,
    K_HOME=102,K_UP=103,K_PGUP=104,K_LEFT=105,K_RIGHT=106,K_END=107,K_DOWN=108,K_PGDN=109,K_DEL=111,K_RALT=100 };
static uint16_t kod_litery(char c){static const char* r="qwertyuiop";static const char* s="asdfghjkl";static const char* t="zxcvbnm";
    for(int i=0;r[i];++i)if(r[i]==c)return uint16_t(16+i);for(int i=0;s[i];++i)if(s[i]==c)return uint16_t(30+i);
    for(int i=0;t[i];++i)if(t[i]==c)return uint16_t(44+i);return 0;}

static OknoNotatnika* g_okno=nullptr;
static void wyslij(const bws_zdarzenie& e){g_okno->obsluz_zdarzenie(e);}
// Zdarzenia ze sterownika trafiaja do okna w kolejnosci produkcji.
static void klawisz(uint16_t k,uint32_t v){strumien.clear();klawiatura_obsluz_klawisz(k,v);auto kopia=strumien;for(auto& e:kopia)wyslij(e);}
static void nacisnij(uint16_t k){klawisz(k,1);klawisz(k,0);}
static void z_modyfikatorem(uint16_t mod,uint16_t k){klawisz(mod,1);nacisnij(k);klawisz(mod,0);}
static void skrot_ctrl(char litera){z_modyfikatorem(K_LCTRL,kod_litery(litera));}

struct Polski { uint32_t male, wielkie; char litera; };
static const Polski POLSKIE[9]={{0x105,0x104,'a'},{0x107,0x106,'c'},{0x119,0x118,'e'},{0x142,0x141,'l'},{0x144,0x143,'n'},
    {0xf3,0xd3,'o'},{0x15b,0x15a,'s'},{0x17a,0x179,'x'},{0x17c,0x17b,'z'}};

// Wpisuje tekst UTF-8 tak, jak uzytkownik na ukladzie Polski Programisty.
static void wpisz(const char* s){
    for(size_t i=0;s[i];){
        uint32_t cp=0;size_t n=CzcionkaUI::dekoduj_utf8(s,i,&cp);CHECK(n>0);i+=n;
        bool gotowe=false;
        for(auto& p:POLSKIE)if(cp==p.male||cp==p.wielkie){
            klawisz(K_RALT,1);if(cp==p.wielkie)klawisz(K_LSHIFT,1);nacisnij(kod_litery(p.litera));
            if(cp==p.wielkie)klawisz(K_LSHIFT,0);klawisz(K_RALT,0);gotowe=true;}
        if(gotowe)continue;
        if(cp>='a'&&cp<='z'){nacisnij(kod_litery(char(cp)));continue;}
        if(cp>='A'&&cp<='Z'){z_modyfikatorem(K_LSHIFT,kod_litery(char(cp-'A'+'a')));continue;}
        if(cp>='1'&&cp<='9'){nacisnij(uint16_t(cp-'1'+2));continue;}
        if(cp=='0'){nacisnij(11);continue;}
        if(cp==' '){nacisnij(K_SPACE);continue;}
        if(cp=='\n'){nacisnij(K_ENTER);continue;}
        if(cp==','){nacisnij(51);continue;}
        if(cp=='.'){nacisnij(52);continue;}
        if(cp=='!'){z_modyfikatorem(K_LSHIFT,2);continue;}
        if(cp=='?'){z_modyfikatorem(K_LSHIFT,53);continue;}
        if(cp=='-'){nacisnij(12);continue;}
        // Pismo spoza ukladu (np. hebrajskie): jak z metody wprowadzania.
        bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_TEXT_INPUT;e.kod=cp;wyslij(e);
    }
}

static std::string tekst(){return std::string(g_notatnik.memo->tekst(),g_notatnik.memo->dlugosc());}
static ZarzadcaPopupow& popupy(){return g_okno->zarzadca_popupow();}
static bool tytul_zawiera(const char* frag){return strstr(g_okno->tytul(),frag)!=nullptr;}
static Punkt punkt_tekstu(size_t linia,int x){
    Prostokat v=g_notatnik.memo->prostokat_do_ekranu(g_notatnik.memo->prostokat_widoku_tekstu());
    return Punkt(v.x+x,v.y+int(linia)*g_notatnik.memo->wysokosc_jednej_linii()+2);  // juz w ekranie
}
static void mysz(uint32_t typ,Punkt p){bws_zdarzenie e{};e.typ=typ;e.x=p.x;e.y=p.y;e.timestamp=0;wyslij(e);}
// Klikniecia z narastajacym znacznikiem czasu w ns (jak HPET w BWS): kolejne
// pojedyncze klikniecia sa od siebie o 1 s, podwojne o 100 ms.
static uint64_t czas_myszy=10000000000ULL;
static void klik_ts(Punkt p,uint64_t ts){bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_MYSZ_DOWN;e.x=p.x;e.y=p.y;e.timestamp=ts;wyslij(e);
    e.typ=BWS_ZDARZENIE_MYSZ_UP;wyslij(e);}
static void klik(Punkt p){czas_myszy+=1000000000ULL;klik_ts(p,czas_myszy);}
static void dwuklik(Punkt p){czas_myszy+=1000000000ULL;klik_ts(p,czas_myszy);klik_ts(p,czas_myszy+100000000ULL);}
static Punkt srodek(Prostokat r){return Punkt(r.x+r.szerokosc/2,r.y+r.wysokosc/2);}
static Punkt punkt_wiersza(Tabela* t,const char* nazwa){
    for(size_t i=0;i<t->liczba_wierszy();++i){const char* n=t->komorka(i,0);const char* bez=strncmp(n,"[KAT] ",6)?n:n+6;
        if(!strcmp(bez,nazwa)){Prostokat o=t->obszar_ekranu(),d=t->prostokat_danych();int h=t->wysokosc_wiersza();
            return Punkt(o.x+d.x+30,o.y+d.y+int(i)*h+h/2);}}
    fprintf(stderr,"brak wiersza %s\n",nazwa);abort();}
static Punkt punkt_narzedzia(PasekNarzedzi* p,size_t i){Prostokat o=p->obszar_ekranu(),r=p->element(i)->prostokat();
    CHECK(p->element(i)->czy_na_pasku());return Punkt(o.x+r.x+r.szerokosc/2,o.y+r.y+r.wysokosc/2);}
static Punkt punkt_miejsca(Drzewo* d,int wiersz){Prostokat o=d->obszar_ekranu();return Punkt(o.x+40,o.y+1+wiersz*20+10);}

static Punkt punkt_narzedzia_nazwa(PasekNarzedzi* p,const char* tekst){
    for(size_t i=0;i<p->liczba_elementow();++i)if(p->element(i)->tekst()&&!strcmp(p->element(i)->tekst(),tekst))return punkt_narzedzia(p,i);
    fprintf(stderr,"brak narzedzia %s\n",tekst);abort();}
static OknoDialogowe* gorny_dialog(){return static_cast<OknoDialogowe*>(popupy().powierzchnia(popupy().liczba_powierzchni()-1));}
static bool komunikat_zawiera(const char* frag){OknoDialogowe* k=gorny_dialog();return k&&strstr(k->wiadomosc(),frag)!=nullptr;}
static size_t liczba_wpisow_fs(){return fs.size();}
static void wyczysc_dokument(){g_notatnik.memo->ustaw_tekst("");g_notatnik.memo->wyczysc_historie();g_notatnik.zmodyfikowany=false;
    g_notatnik.biezaca_sciezka[0]=0;g_notatnik.aktualizuj_tytul();g_okno->ustaw_fokus(g_notatnik.memo);}

int main(){
    g_test_bws_hook=hook;
    for(const char* name:{"DejaVuSans.ttf","DejaVuSans-Bold.ttf","DejaVuSansMono.ttf"}){
        std::ifstream in(std::string("czcionki/ttf/")+name,std::ios::binary);
        dodaj_plik(std::string("/czcionki/ttf/")+name,std::string(std::istreambuf_iterator<char>(in),{}));}
    dodaj_katalog("/");dodaj_katalog("/czcionki");dodaj_katalog("/czcionki/ttf");
    dodaj_katalog("/uzytkownicy");dodaj_katalog("/uzytkownicy/Dokumenty");dodaj_katalog("/uzytkownicy/Dokumenty/Prywatne ąę");
    const std::string ZAZOLC="Zażółć gęślą jaźń.txt";
    const std::string TRESC="Zażółć gęślą jaźń\r\nMixed: abc 123, ĄĆĘŁŃÓŚŹŻ!\r\nשלום עולם\r\n";
    dodaj_plik("/uzytkownicy/Dokumenty/"+ZAZOLC,TRESC);
    dodaj_plik("/uzytkownicy/Dokumenty/obraz.bmp","BM");
    CHECK(BSF::ready());

    OknoNotatnika okno("Notatnik - Nowy dokument",680,500);g_okno=&okno;
    zbuduj_notatnik(okno);g_notatnik.nowy_dokument();
    okno.przelicz_uklad();
    PoleWielowierszowe& memo=*g_notatnik.memo;
    CHECK(okno.fokus()==&memo);

    // 1. Brak paska narzedzi: korzen = menu glowne, edytor, pasek stanu. Edytor
    //    zaczyna sie bezposrednio pod menu i zajmuje odzyskana wysokosc.
    {Kontrolka* korzen=memo.rodzic();CHECK(korzen&&korzen->liczba_dzieci()==3);
     CHECK(korzen->dziecko(0)==g_notatnik.menu&&korzen->dziecko(1)==&memo&&korzen->dziecko(2)==g_notatnik.pasek_stanu);
     for(size_t i=0;i<korzen->liczba_dzieci();++i)CHECK(dynamic_cast<PasekNarzedzi*>(korzen->dziecko(i))==nullptr);
     const Prostokat m=g_notatnik.menu->obszar(),e=memo.obszar(),sb=g_notatnik.pasek_stanu->obszar(),k=korzen->obszar();
     CHECK(e.y==m.y+m.wysokosc+2);CHECK(sb.y==e.y+e.wysokosc+2);CHECK(m.y==2&&sb.y+sb.wysokosc==k.wysokosc-2);   // wspolrzedne dzieci wzgledem korzenia
     CHECK(e.szerokosc==m.szerokosc);CHECK(e.wysokosc>k.wysokosc-m.wysokosc-sb.wysokosc-12);
     CHECK(korzen->sprawdz_trafienie(Punkt(k.x+e.x+10,k.y+e.y+1))==&memo);     // pierwszy piksel pod menu = edytor
     CHECK(korzen->sprawdz_trafienie(Punkt(k.x+m.x+10,k.y+m.y+m.wysokosc-1))==g_notatnik.menu);}
    // Menu glowne: Plik, Edycja, Widok, Pomoc z poleceniami dokumentu.
    Menu& pasek_menu=g_notatnik.menu->menu();CHECK(pasek_menu.liczba_elementow()==4);
    const char* menu_glowne[4]={"Plik","Edycja","Widok","Pomoc"};
    for(size_t i=0;i<4;++i)CHECK(!strcmp(pasek_menu.element(i)->tekst(),menu_glowne[i])&&pasek_menu.element(i)->podmenu());
    auto pozycja=[&](size_t m,const char* t)->ElementMenu*{Menu* p=pasek_menu.element(m)->podmenu();
        for(size_t i=0;i<p->liczba_elementow();++i)if(!p->element(i)->czy_separator()&&!strcmp(p->element(i)->tekst(),t))return p->element(i);return nullptr;};
    for(const char* t:{"Nowy","Otwórz...","Zapisz","Zapisz jako...","Zakończ"})CHECK(pozycja(0,t));
    CHECK(!strcmp(pozycja(0,"Nowy")->skrot(),"Ctrl+N")&&!strcmp(pozycja(0,"Zapisz jako...")->skrot(),"Ctrl+Shift+S"));
    CHECK(!pozycja(0,"Zakończ")->skrot()||!pozycja(0,"Zakończ")->skrot()[0]);   // Alt+F4 nie jest obslugiwane: brak etykiety
    for(const char* t:{"Cofnij","Ponów","Wytnij","Kopiuj","Wklej","Zaznacz wszystko"})CHECK(pozycja(1,t));
    CHECK(pozycja(2,"Zawijanie wierszy"));
    // Pomoc: natywne okna podrzedne (BWS 101) o wlasnych rodzajach Notatnika.
    {zlecenia_okien.clear();pozycja(3,"Sterowanie")->wykonaj();pozycja(3,"O Notatniku")->wykonaj();
     CHECK(zlecenia_okien.size()==2);
     CHECK(zlecenia_okien[0].rodzaj==OknaPodrzedne::RODZAJ_STEROWANIE_NOTATNIK&&zlecenia_okien[0].tytul=="Sterowanie");
     CHECK(zlecenia_okien[1].rodzaj==OknaPodrzedne::RODZAJ_O_NOTATNIKU&&zlecenia_okien[1].tytul=="O Notatniku");
     for(auto& z:zlecenia_okien){CHECK(OknaPodrzedne::tresc_poprawna(z.tresc.data(),z.tresc.size()));
         CHECK(OknaPodrzedne::tytul_poprawny(z.tytul.data(),z.tytul.size()));CHECK(DialogPliku::czy_poprawny_utf8(z.tresc.c_str()));
         CHECK(z.tresc.find("Alt+F4")==std::string::npos);CHECK(z.tresc.rfind("# ",0)==0);
         static TrescUklad::Uklad u;auto miernik=[](const char*,size_t n){return int(n)*7;};u=TrescUklad::Uklad{};
         TrescUklad::zbuduj(u,z.tresc.data(),z.tresc.size(),360,18,6,miernik);
         CHECK(!u.przepelnienie&&u.liczba>5);}
     for(const char* t:{"Ctrl+N","Ctrl+O","Ctrl+S","Ctrl+Shift+S","Ctrl+Z","Ctrl+Y","Ctrl+A","AltGr","ą ć ę ł ń ó ś ź ż","Ą Ć Ę Ł Ń Ó Ś Ź Ż","Page Up"})
         CHECK(zlecenia_okien[0].tresc.find(t)!=std::string::npos);
     CHECK(zlecenia_okien[1].tresc.find("Bursztyn OS")!=std::string::npos);
     // Ponowne wybranie: kolejne zlecenie tego samego rodzaju (jadro aktywuje istniejace okno, wynik 2).
     wynik_okna_podrzednego=2;pozycja(3,"Sterowanie")->wykonaj();CHECK(zlecenia_okien.size()==3&&zlecenia_okien[2].rodzaj==OknaPodrzedne::RODZAJ_STEROWANIE_NOTATNIK);
     wynik_okna_podrzednego=0;pozycja(3,"O Notatniku")->wykonaj();CHECK(std::string(g_notatnik.pasek_stanu->tekst(0)).find("Nie można otworzyć okna pomocy")!=std::string::npos);
     wynik_okna_podrzednego=1;CHECK(okno.fokus()==&memo);CHECK(!popupy().czy_otwarty());}
    CHECK(g_notatnik.menu_kontekstowe && memo.menu_kontekstowe()==g_notatnik.menu_kontekstowe);
    for(auto* e:g_notatnik.kontekst)CHECK(e!=nullptr);
    CHECK(g_notatnik.menu_cofnij&&g_notatnik.menu_ponow&&g_notatnik.menu_wytnij&&g_notatnik.menu_kopiuj);

    // 2. Sterownik: AltGr i AltGr+Shift daja wszystkie 18 polskich liter z bitem
    // ALTGR; strumien zawiera klawisz fizyczny, TEXT_INPUT i bajty UTF-8 (echo).
    for(auto& p:POLSKIE)for(int duza=0;duza<2;++duza){
        klawiatura_zresetuj_stan();strumien.clear();
        klawiatura_obsluz_klawisz(K_RALT,1);if(duza)klawiatura_obsluz_klawisz(K_LSHIFT,1);
        strumien.clear();klawiatura_obsluz_klawisz(kod_litery(p.litera),1);
        CHECK(strumien.size()==4);CHECK(strumien[0].typ==BWS_ZDARZENIE_KLAWISZ_FIZYCZNY);
        CHECK((strumien[0].przyciski&BWS_MOD_ALTGR)&&(strumien[0].przyciski&BWS_MOD_ALT));
        CHECK(strumien[1].typ==BWS_ZDARZENIE_TEXT_INPUT&&strumien[1].kod==(duza?p.wielkie:p.male));
        CHECK(strumien[1].przyciski&BWS_MOD_ALTGR);
        CHECK(strumien[2].typ==BWS_ZDARZENIE_KLAWISZ&&strumien[3].typ==BWS_ZDARZENIE_KLAWISZ);
        char utf8[5]{};CzcionkaUI::zakoduj_utf8(duza?p.wielkie:p.male,utf8);
        CHECK(uint8_t(utf8[0])==strumien[2].kod&&uint8_t(utf8[1])==strumien[3].kod);
        // Caps Lock odwraca wielkosc tak jak dla liter ASCII.
        klawiatura_obsluz_klawisz(kod_litery(p.litera),0);klawiatura_obsluz_klawisz(K_CAPS,1);klawiatura_obsluz_klawisz(K_CAPS,0);
        strumien.clear();klawiatura_obsluz_klawisz(kod_litery(p.litera),1);
        CHECK(strumien.size()==4&&strumien[1].kod==(duza?p.male:p.wielkie));
        klawiatura_zresetuj_stan();
    }
    // Lewy Alt nie jest AltGr: brak polskiej litery i brak bitu ALTGR.
    strumien.clear();klawiatura_obsluz_klawisz(K_LALT,1);klawiatura_obsluz_klawisz(30,1);
    for(auto& e:strumien){CHECK(!(e.przyciski&BWS_MOD_ALTGR));if(e.typ==BWS_ZDARZENIE_TEXT_INPUT)CHECK(e.kod=='a');}
    klawiatura_zresetuj_stan();strumien.clear();

    // 3. Pisanie w Notatniku: fokus zostaje w edytorze mimo AltGr (przyczyna
    // bledu: prawy Alt aktywowal MenuGlowne), bez podwojnych znakow z echa.
    wpisz("Zażółć gęślą jaźń");CHECK(okno.fokus()==&memo);CHECK(tekst()=="Zażółć gęślą jaźń");
    CHECK(tytul_zawiera("Nowy dokument *"));
    wpisz("\nĄĆĘŁŃÓŚŹŻ ąćęłńóśźż\nabc 123, Hello! Część?");
    CHECK(tekst()=="Zażółć gęślą jaźń\nĄĆĘŁŃÓŚŹŻ ąćęłńóśźż\nabc 123, Hello! Część?");
    CHECK(okno.fokus()==&memo);
    // Powtarzanie klawisza (value=2) z AltGr wstawia kolejne znaki.
    klawisz(K_RALT,1);klawisz(kod_litery('l'),1);klawisz(kod_litery('l'),2);klawisz(kod_litery('l'),2);klawisz(kod_litery('l'),0);klawisz(K_RALT,0);
    CHECK(tekst().size()>=6&&tekst().substr(tekst().size()-6)=="łłł");
    // Ctrl+AltGr (hosty wysylajace LCtrl+RAlt): litera, a nie Ctrl+A/C/X/V/Z/S/N/O.
    std::string przed=tekst();
    for(auto& p:POLSKIE){klawisz(K_LCTRL,1);klawisz(K_RALT,1);nacisnij(kod_litery(p.litera));klawisz(K_RALT,0);klawisz(K_LCTRL,0);}
    CHECK(tekst()==przed+"ąćęłńóśźż");CHECK(!popupy().czy_otwarty());CHECK(!memo.czy_ma_zaznaczenie());
    // Pozostale warianty kolejnosci: RAlt przed LCtrl, Shift (wielkie), AltGr puszczony przed litera,
    // prawy Ctrl. Zadna kombinacja nie otwiera dialogu (Nowy/Otworz/Zapisz) ani menu.
    przed=tekst();
    for(auto& p:POLSKIE){klawisz(K_RALT,1);klawisz(K_LCTRL,1);klawisz(K_LSHIFT,1);nacisnij(kod_litery(p.litera));
        klawisz(K_LSHIFT,0);klawisz(K_LCTRL,0);klawisz(K_RALT,0);}
    CHECK(tekst()==przed+"ĄĆĘŁŃÓŚŹŻ");
    przed=tekst();
    for(auto& p:POLSKIE){klawisz(K_RALT,1);klawisz(kod_litery(p.litera),1);klawisz(K_RALT,0);klawisz(kod_litery(p.litera),0);}
    klawisz(97,1);klawisz(K_RALT,1);nacisnij(kod_litery('s'));klawisz(K_RALT,0);klawisz(97,0);
    CHECK(tekst()==przed+"ąćęłńóśźżś");
    CHECK(okno.fokus()==&memo);CHECK(!popupy().czy_otwarty());CHECK(!g_notatnik.dialog_pliku);CHECK(g_notatnik.zmodyfikowany);
    CHECK(liczba_zapisow==0);CHECK(!g_notatnik.biezaca_sciezka[0]);
    // Utrata i powrot fokusu okna (okno pomocy, pasek zadan): edytor odzyskuje fokus, AltGr dziala dalej.
    {bws_zdarzenie b{};b.typ=BWS_ZDARZENIE_BLUR;wyslij(b);}CHECK(okno.fokus()==nullptr);
    {bws_zdarzenie f{};f.typ=BWS_ZDARZENIE_FOCUS;wyslij(f);}CHECK(okno.fokus()==&memo);
    przed=tekst();klawisz(K_LCTRL,1);klawisz(K_RALT,1);nacisnij(kod_litery('z'));klawisz(K_RALT,0);klawisz(K_LCTRL,0);
    wpisz("ą");CHECK(tekst()==przed+"żą");
    // Przy FOCUS z fokusem w innej kontrolce (np. menu) nic nie jest nadpisywane.
    {bws_zdarzenie b{};b.typ=BWS_ZDARZENIE_BLUR;wyslij(b);}okno.ustaw_fokus(g_notatnik.menu);
    {bws_zdarzenie f{};f.typ=BWS_ZDARZENIE_FOCUS;wyslij(f);}CHECK(okno.fokus()==g_notatnik.menu);okno.ustaw_fokus(&memo);
    // Lewy Alt nadal aktywuje pasek menu; Esc albo ponowny Alt wraca do edytora.
    nacisnij(K_LALT);CHECK(okno.fokus()==g_notatnik.menu);nacisnij(K_ESC);CHECK(okno.fokus()==&memo);
    nacisnij(K_LALT);CHECK(okno.fokus()==g_notatnik.menu);nacisnij(K_LALT);CHECK(okno.fokus()==&memo);
    // Alt, a potem zwykle pisanie: tekst trafia do edytora, nie ginie w pasku menu.
    przed=tekst();nacisnij(K_LALT);CHECK(okno.fokus()==g_notatnik.menu);wpisz("xą");CHECK(okno.fokus()==&memo);
    CHECK(tekst()==przed+"xą");CHECK(!popupy().czy_otwarty());
    // Nawigacja klawiatura w pasku menu nadal dziala: Alt, Strzalka w dol otwiera Plik, Esc zamyka i wraca.
    nacisnij(K_LALT);nacisnij(K_DOWN);CHECK(popupy().czy_otwarty());CHECK(okno.fokus()==g_notatnik.menu);
    nacisnij(K_ESC);CHECK(!popupy().czy_otwarty());CHECK(okno.fokus()==&memo);

    // Dokladny scenariusz natywny (QEMU + XTest, docs/notatnik-2026-11-17): klik w "Pomoc",
    // Strzalka w dol + Enter -> okno podrzedne (BLUR), zamkniecie okna (FOCUS), pisanie z AltGr.
    // Przed poprawka fokus zostawal w MenuGlowne: litery otwieraly menu, edytor pozostawal pusty.
    {Prostokat pm=g_notatnik.menu->prostokat_do_ekranu(g_notatnik.menu->prostokat_glowny(3));
     const Punkt pomoc(pm.x+pm.szerokosc/2,pm.y+pm.wysokosc/2);
     zlecenia_okien.clear();przed=tekst();
     mysz(BWS_ZDARZENIE_MYSZ_DOWN,pomoc);mysz(BWS_ZDARZENIE_MYSZ_UP,pomoc);
     CHECK(popupy().czy_otwarty());CHECK(okno.fokus()==g_notatnik.menu);
     nacisnij(K_DOWN);nacisnij(K_ENTER);
     CHECK(zlecenia_okien.size()==1&&zlecenia_okien[0].rodzaj==OknaPodrzedne::RODZAJ_STEROWANIE_NOTATNIK);
     CHECK(!popupy().czy_otwarty());CHECK(okno.fokus()==&memo);            // menu zamkniete: fokus w edytorze
     {bws_zdarzenie b{};b.typ=BWS_ZDARZENIE_BLUR;wyslij(b);}            // okno pomocy przejmuje fokus
     {bws_zdarzenie f{};f.typ=BWS_ZDARZENIE_FOCUS;wyslij(f);}           // zamkniete: fokus wraca do Notatnika
     CHECK(okno.fokus()==&memo);
     wpisz("Za");for(char l:{'z','o','l','c'}){klawisz(K_RALT,1);nacisnij(kod_litery(l));klawisz(K_RALT,0);}
     for(char l:{'a','s'}){klawisz(K_LCTRL,1);klawisz(K_RALT,1);nacisnij(kod_litery(l));klawisz(K_RALT,0);klawisz(K_LCTRL,0);}
     CHECK(tekst()==przed+"Zażółćąś");CHECK(!popupy().czy_otwarty());CHECK(zlecenia_okien.size()==1);
     // Klik w menu i klik obok (zamkniecie bez wyboru): fokus rowniez wraca do edytora.
     mysz(BWS_ZDARZENIE_MYSZ_DOWN,pomoc);mysz(BWS_ZDARZENIE_MYSZ_UP,pomoc);CHECK(popupy().czy_otwarty());
     nacisnij(K_ESC);CHECK(!popupy().czy_otwarty());CHECK(okno.fokus()==&memo);
     // BLUR, gdy fokus jest w pasku menu: po powrocie fokus trafia do edytora, nie do menu.
     nacisnij(K_LALT);CHECK(okno.fokus()==g_notatnik.menu);
     {bws_zdarzenie b{};b.typ=BWS_ZDARZENIE_BLUR;wyslij(b);}{bws_zdarzenie f{};f.typ=BWS_ZDARZENIE_FOCUS;wyslij(f);}
     CHECK(okno.fokus()==&memo);}

    // 4. Edycja wielobajtowa: Backspace/Delete/strzalki/Home/End/Ctrl+strzalki.
    wyczysc_dokument();wpisz("zażółć gęślą");
    nacisnij(K_BS);CHECK(tekst()=="zażółć gęśl");
    nacisnij(K_LEFT);nacisnij(K_LEFT);nacisnij(K_DEL);CHECK(tekst()=="zażółć gęl");
    wpisz("ś");CHECK(tekst()=="zażółć gęśl");
    nacisnij(K_HOME);CHECK(memo.pozycja_kursora()==0);
    nacisnij(K_RIGHT);nacisnij(K_RIGHT);CHECK(memo.pozycja_kursora()==2);  // "za"
    nacisnij(K_RIGHT);CHECK(memo.pozycja_kursora()==4);                     // "ż" = 2 bajty
    nacisnij(K_RIGHT);CHECK(memo.pozycja_kursora()==6);                     // "ó" = 2 bajty
    nacisnij(K_END);CHECK(memo.pozycja_kursora()==tekst().size());
    z_modyfikatorem(K_LCTRL,K_LEFT);CHECK(memo.pozycja_kursora()==strlen("zażółć "));
    z_modyfikatorem(K_LCTRL,K_HOME);CHECK(memo.pozycja_kursora()==0);
    z_modyfikatorem(K_LCTRL,K_RIGHT);CHECK(memo.pozycja_kursora()==strlen("zażółć "));
    // Zaznaczenie klawiatura: Shift+End, Ctrl+X/Ctrl+V przez schowek (UTF-8 nietkniety).
    klawisz(K_LSHIFT,1);nacisnij(K_END);klawisz(K_LSHIFT,0);
    CHECK(memo.czy_ma_zaznaczenie());CHECK(memo.koniec_zaznaczenia()-memo.poczatek_zaznaczenia()==strlen("gęśl"));
    skrot_ctrl('x');CHECK(tekst()=="zażółć ");CHECK(std::string(g_mock_clipboard)=="gęśl");
    z_modyfikatorem(K_LCTRL,K_HOME);skrot_ctrl('v');CHECK(tekst()=="gęślzażółć ");
    skrot_ctrl('a');CHECK(memo.poczatek_zaznaczenia()==0&&memo.koniec_zaznaczenia()==tekst().size());
    skrot_ctrl('c');CHECK(std::string(g_mock_clipboard)=="gęślzażółć ");
    nacisnij(K_END);
    skrot_ctrl('z');CHECK(tekst()=="zażółć ");
    skrot_ctrl('y');CHECK(tekst()=="gęślzażółć ");
    // Page Up / Page Down w wielowierszowym dokumencie.
    {std::string dlugi;for(int i=0;i<200;++i)dlugi+="wiersz ąę "+std::to_string(i)+"\n";memo.ustaw_tekst(dlugi.c_str());
     memo.ustaw_zaznaczenie(0,0);nacisnij(K_PGDN);size_t r,c;memo.pozycja_logiczna(r,c);CHECK(r>5);
     nacisnij(K_PGUP);memo.pozycja_logiczna(r,c);CHECK(r==1);}
    // Mysz: przeciagniecie zaznacza od poczatku linii.
    wyczysc_dokument();wpisz("ąbc dęf\ndruga");
    mysz(BWS_ZDARZENIE_MYSZ_DOWN,punkt_tekstu(0,0));mysz(BWS_ZDARZENIE_MYSZ_RUCH,punkt_tekstu(0,400));mysz(BWS_ZDARZENIE_MYSZ_UP,punkt_tekstu(0,400));
    CHECK(okno.fokus()==&memo);CHECK(memo.poczatek_zaznaczenia()==0);CHECK(memo.koniec_zaznaczenia()==strlen("ąbc dęf"));

    // 5. Menu kontekstowe BursztynUI: prawy przycisk nie zmienia zaznaczenia ani karetki.
    const size_t sel_a=memo.poczatek_zaznaczenia(),sel_b=memo.koniec_zaznaczenia(),kar=memo.pozycja_kursora();
    clipboard_set("",nullptr,0);
    mysz(BWS_ZDARZENIE_MYSZ_PRAWY_DOWN,punkt_tekstu(1,20));mysz(BWS_ZDARZENIE_MYSZ_PRAWY_UP,punkt_tekstu(1,20));
    CHECK(popupy().czy_otwarty());CHECK(g_notatnik.menu_kontekstowe->czy_otwarte());
    CHECK(memo.poczatek_zaznaczenia()==sel_a&&memo.koniec_zaznaczenia()==sel_b&&memo.pozycja_kursora()==kar);
    CHECK(g_notatnik.kontekst[2]->czy_wlaczony()&&g_notatnik.kontekst[3]->czy_wlaczony()); // Wytnij/Kopiuj
    CHECK(!g_notatnik.kontekst[4]->czy_wlaczony());                                       // pusty schowek
    CHECK(g_notatnik.kontekst[0]->czy_wlaczony()&&!g_notatnik.kontekst[1]->czy_wlaczony()); // Cofnij tak, Ponow nie
    {Prostokat r=popupy().prostokat_zarejestrowany_ekranu(),w=popupy().obszar_roboczy_ekranu();
     CHECK(r.x>=w.x&&r.y>=w.y&&r.prawy()<=w.prawy()&&r.dolny()<=w.dolny());}
    nacisnij(K_ESC);CHECK(!popupy().czy_otwarty());CHECK(memo.czy_ma_zaznaczenie());CHECK(okno.fokus()==&memo);
    // Polecenie z menu: Kopiuj, potem Wklej staje sie dostepne.
    mysz(BWS_ZDARZENIE_MYSZ_PRAWY_DOWN,punkt_tekstu(0,10));CHECK(popupy().czy_otwarty());
    g_notatnik.kontekst[3]->wykonaj();popupy().zamknij();CHECK(std::string(g_mock_clipboard)=="ąbc dęf");
    memo.ustaw_zaznaczenie(memo.dlugosc(),memo.dlugosc());
    mysz(BWS_ZDARZENIE_MYSZ_PRAWY_DOWN,punkt_tekstu(1,10));
    CHECK(!g_notatnik.kontekst[2]->czy_wlaczony()&&!g_notatnik.kontekst[3]->czy_wlaczony()); // brak zaznaczenia
    CHECK(g_notatnik.kontekst[4]->czy_wlaczony());
    // Klik poza menu zamyka je bez zmiany tekstu.
    {std::string t=tekst();Prostokat r=popupy().prostokat_zarejestrowany_ekranu();
     mysz(BWS_ZDARZENIE_MYSZ_DOWN,Punkt(r.x>40?r.x-30:r.prawy()+30,r.y+5));CHECK(!popupy().czy_otwarty());CHECK(tekst()==t);}
    // Menu przy prawej/dolnej krawedzi miesci sie w obszarze roboczym.
    {Prostokat w=popupy().obszar_roboczy_ekranu();g_notatnik.menu_kontekstowe->pokaz_dla(memo,w.prawy()-2,w.dolny()-2);
     Prostokat r=popupy().prostokat_zarejestrowany_ekranu();CHECK(r.prawy()<=w.prawy()&&r.dolny()<=w.dolny());nacisnij(K_ESC);CHECK(!popupy().czy_otwarty());}
    // Klawisz Menu (E0 5D) otwiera to samo menu z klawiatury.
    {bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;e.x=1;e.kod=BWS_KEY_SOURCE_E0|0x5D;wyslij(e);CHECK(g_notatnik.menu_kontekstowe->czy_otwarte());nacisnij(K_ESC);}
    // Skroty dzialaja niezaleznie od menu kontekstowego.
    skrot_ctrl('a');skrot_ctrl('x');CHECK(tekst().empty());skrot_ctrl('z');CHECK(tekst()=="ąbc dęf\ndruga");

    // 6. Otworz: Ctrl+O -> natywny DialogPliku, katalogi, filtr, UTF-8 nazwy.
    wyczysc_dokument();
    skrot_ctrl('o');CHECK(popupy().czy_otwarty());DialogPliku* d=g_notatnik.dialog_pliku;CHECK(d&&d->czy_otwarta());
    CHECK(d->tryb()==TrybDialoguPliku::Otworz);CHECK(!strcmp(d->biezacy_katalog(),"/uzytkownicy"));
    CHECK(d->liczba_filtrow()==3);
    // Nawigacja klawiatura: nazwa katalogu w polu + Enter wchodzi do katalogu.
    d->pole_nazwy()->ustaw_tekst("Dokumenty");nacisnij(K_ENTER);
    CHECK(!strcmp(d->biezacy_katalog(),"/uzytkownicy/Dokumenty"));CHECK(popupy().czy_otwarty());
    // Lista: katalog, plik tekstowy z polska nazwa; .bmp ukryty filtrem dokumentow.
    {Tabela* t=d->tabela_plikow();bool zazolc=false,bmp=false,kat=false;
     for(size_t i=0;i<t->liczba_wierszy();++i){const char* n=t->komorka(i,0);if(n==ZAZOLC)zazolc=true;if(!strcmp(n,"obraz.bmp"))bmp=true;if(strstr(n,"Prywatne ąę"))kat=true;}
     CHECK(zazolc&&!bmp&&kat);
     d->ustaw_aktywny_filtr(2);bmp=false;for(size_t i=0;i<t->liczba_wierszy();++i)if(!strcmp(t->komorka(i,0),"obraz.bmp"))bmp=true;CHECK(bmp);
     d->ustaw_aktywny_filtr(0);}
    // Poziom wyzej i z powrotem.
    CHECK(d->przejdz_poziom_wyzej());CHECK(!strcmp(d->biezacy_katalog(),"/uzytkownicy"));
    CHECK(d->przejdz_do_katalogu("/uzytkownicy/Dokumenty"));
    // Nazwa wpisana w polu dialogu przez AltGr (ten sam potok wejscia).
    // Fokus startowy dialogu jest w polu nazwy; Ctrl+A + pisanie zastepuje nazwe.
    CHECK(popupy().fokus_popupu()==d->pole_nazwy());skrot_ctrl('a');
    wpisz("Zażółć gęślą jaźń.txt");CHECK(d->pole_nazwy()->tekst()==ZAZOLC);
    nacisnij(K_ENTER);CHECK(!popupy().czy_otwarty());
    CHECK(tekst()==TRESC);CHECK(std::string(g_notatnik.biezaca_sciezka)=="/uzytkownicy/Dokumenty/"+ZAZOLC);
    CHECK(tytul_zawiera(ZAZOLC.c_str()));CHECK(!tytul_zawiera("*"));CHECK(!memo.czy_mozna_cofnac());
    CHECK(memo.liczba_wierszy()==4);
    // Status: wiersz/kolumna liczone w znakach, CRLF nie jest kolumna.
    memo.ustaw_zaznaczenie(strlen("Zażółć gęślą jaźń"),strlen("Zażółć gęślą jaźń"));g_notatnik.aktualizuj_stan();
    {size_t r,c;memo.pozycja_logiczna(r,c);CHECK(r==1&&c==18);}
    nacisnij(K_DOWN);{size_t r,c;memo.pozycja_logiczna(r,c);CHECK(r==2);}
    // Anulowanie dialogu nie zmienia dokumentu.
    skrot_ctrl('o');CHECK(popupy().czy_otwarty());nacisnij(K_ESC);CHECK(!popupy().czy_otwarty());
    CHECK(tekst()==TRESC);CHECK(std::string(g_notatnik.biezaca_sciezka)=="/uzytkownicy/Dokumenty/"+ZAZOLC);
    // Plik, ktorego nie mozna otworzyc: komunikat, dokument nietkniety.
    dodaj_plik("/uzytkownicy/Dokumenty/binarny.txt",std::string("a\0b",3));
    CHECK(!g_notatnik.otworz_plik("/uzytkownicy/Dokumenty/binarny.txt"));CHECK(popupy().czy_otwarty());nacisnij(K_ENTER);
    CHECK(!g_notatnik.otworz_plik("/uzytkownicy/Dokumenty/brak.txt"));nacisnij(K_ENTER);
    dodaj_plik("/uzytkownicy/Dokumenty/duzy.txt",std::string(70000,'x'));
    CHECK(!g_notatnik.otworz_plik("/uzytkownicy/Dokumenty/duzy.txt"));nacisnij(K_ENTER);
    CHECK(!popupy().czy_otwarty());CHECK(tekst()==TRESC);

    // 7. Zapis istniejacego pliku: Ctrl+S bez dialogu, zawartosc UTF-8/CRLF bajt w bajt.
    nacisnij(K_END);wpisz(" ż");CHECK(tytul_zawiera("*"));
    std::string oczekiwany=tekst();skrot_ctrl('s');CHECK(!popupy().czy_otwarty());
    CHECK(fs["/uzytkownicy/Dokumenty/"+ZAZOLC].dane==oczekiwany);CHECK(!tytul_zawiera("*"));
    CHECK(oczekiwany.find("\r\n")!=std::string::npos);
    // Nieudany zapis: blad, dokument dalej zmodyfikowany, plik bez zmian.
    wpisz("!");awaria_zapisu=true;skrot_ctrl('s');CHECK(popupy().czy_otwarty());CHECK(tytul_zawiera("*"));
    CHECK(g_notatnik.zmodyfikowany);CHECK(fs["/uzytkownicy/Dokumenty/"+ZAZOLC].dane==oczekiwany);nacisnij(K_ENTER);
    awaria_zapisu=false;
    // Zapis zgloszony jako udany, ale niezgodny przy weryfikacji: nadal niezapisany.
    uszkodzony_zapis=true;skrot_ctrl('s');CHECK(g_notatnik.zmodyfikowany);CHECK(popupy().czy_otwarty());nacisnij(K_ENTER);
    uszkodzony_zapis=false;skrot_ctrl('s');CHECK(!g_notatnik.zmodyfikowany);CHECK(fs["/uzytkownicy/Dokumenty/"+ZAZOLC].dane==tekst());
    // Ponowne otwarcie zapisanego pliku daje identyczna tresc.
    {std::string zapis=tekst();wyczysc_dokument();CHECK(g_notatnik.otworz_plik(("/uzytkownicy/Dokumenty/"+ZAZOLC).c_str()));CHECK(tekst()==zapis);}

    // 8. Zapisz jako: nowy dokument, Ctrl+S otwiera dialog, rozszerzenie z filtra.
    skrot_ctrl('n');CHECK(tekst().empty());CHECK(!g_notatnik.biezaca_sciezka[0]);
    wpisz("Nowa notatka: łódź");
    skrot_ctrl('s');d=g_notatnik.dialog_pliku;CHECK(d&&d->czy_otwarta()&&d->tryb()==TrybDialoguPliku::Zapisz);
    CHECK(!strcmp(d->biezacy_katalog(),"/uzytkownicy"));CHECK(!strcmp(d->pole_nazwy()->tekst(),"dokument.txt"));
    CHECK(popupy().fokus_popupu()==d->pole_nazwy());CHECK(d->pole_nazwy()->czy_ma_zaznaczenie());
    CHECK(d->przejdz_do_katalogu("/uzytkownicy/Dokumenty/Prywatne ąę"));
    d->pole_nazwy()->ustaw_tekst("Notatka źródłowa");nacisnij(K_ENTER);CHECK(!popupy().czy_otwarty());
    const std::string NOWA="/uzytkownicy/Dokumenty/Prywatne ąę/Notatka źródłowa.txt";
    CHECK(fs.count(NOWA)&&fs[NOWA].dane=="Nowa notatka: łódź");CHECK(std::string(g_notatnik.biezaca_sciezka)==NOWA);
    CHECK(tytul_zawiera("Notatka źródłowa.txt"));CHECK(!g_notatnik.zmodyfikowany);
    // Zapisz jako na istniejacy plik: pytanie o zastapienie; Esc = nie nadpisuj.
    wpisz(" 2");z_modyfikatorem(K_LSHIFT,0);  // brak efektu
    klawisz(K_LCTRL,1);klawisz(K_LSHIFT,1);nacisnij(kod_litery('s'));klawisz(K_LSHIFT,0);klawisz(K_LCTRL,0);
    d=g_notatnik.dialog_pliku;CHECK(d&&d->czy_otwarta());
    CHECK(d->przejdz_do_katalogu("/uzytkownicy/Dokumenty"));d->pole_nazwy()->ustaw_tekst(ZAZOLC.c_str());
    const std::string stary=fs["/uzytkownicy/Dokumenty/"+ZAZOLC].dane;
    nacisnij(K_ENTER);CHECK(popupy().liczba_powierzchni()==2);                 // dialog + potwierdzenie
    nacisnij(K_ESC);CHECK(fs["/uzytkownicy/Dokumenty/"+ZAZOLC].dane==stary);CHECK(d->czy_otwarta());
    CHECK(std::string(g_notatnik.biezaca_sciezka)==NOWA);CHECK(g_notatnik.zmodyfikowany);
    nacisnij(K_ENTER);CHECK(popupy().liczba_powierzchni()==2);nacisnij(K_ENTER);  // Tak (domyslny)
    CHECK(!popupy().czy_otwarty());CHECK(fs["/uzytkownicy/Dokumenty/"+ZAZOLC].dane==tekst());
    CHECK(std::string(g_notatnik.biezaca_sciezka)=="/uzytkownicy/Dokumenty/"+ZAZOLC);CHECK(!g_notatnik.zmodyfikowany);
    // Walidacja sciezek przed wywolaniami systemu plikow.
    unsigned zapisy=liczba_zapisow;
    CHECK(!g_notatnik.zapisz_plik_jako("/brak/katalogu.txt"));nacisnij(K_ENTER);
    CHECK(!g_notatnik.zapisz_plik_jako("wzgledna.txt"));nacisnij(K_ENTER);
    CHECK(!g_notatnik.zapisz_plik_jako("/uzytkownicy/zła|nazwa.txt"));nacisnij(K_ENTER);
    CHECK(!g_notatnik.zapisz_plik_jako("/uzytkownicy/Dokumenty"));nacisnij(K_ENTER);
    CHECK(!g_notatnik.zapisz_plik_jako("/uzytkownicy/\xff\xfe.txt"));nacisnij(K_ENTER);
    awaria_tworzenia=true;CHECK(!g_notatnik.zapisz_plik_jako("/uzytkownicy/nowy.txt"));nacisnij(K_ENTER);awaria_tworzenia=false;
    CHECK(liczba_zapisow==zapisy);CHECK(!popupy().czy_otwarty());

    // 9. Ochrona niezapisanych zmian przed Nowy/Otworz/zamknieciem/otwarciem z systemu.
    wpisz(" zmiana");const std::string niezapisany=tekst();
    skrot_ctrl('n');CHECK(popupy().czy_otwarty());CHECK(g_notatnik.pytanie_zmian->czy_otwarta());
    nacisnij(K_ESC);CHECK(!popupy().czy_otwarty());CHECK(tekst()==niezapisany);
    skrot_ctrl('o');CHECK(g_notatnik.pytanie_zmian->czy_otwarta());nacisnij(K_ESC);CHECK(tekst()==niezapisany);
    okno.zamknij();CHECK(g_notatnik.pytanie_zmian->czy_otwarta());nacisnij(K_ESC);CHECK(tekst()==niezapisany);
    argument_startowy="/uzytkownicy/Dokumenty/Prywatne ąę/Notatka źródłowa.txt";
    {bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_OTWORZ_PLIK;wyslij(e);}CHECK(g_notatnik.pytanie_zmian->czy_otwarta());
    nacisnij(K_ESC);CHECK(tekst()==niezapisany);
    // "Nie" przy Otworz: dialog, a potem anulowanie nadal nie traci dokumentu.
    skrot_ctrl('o');g_notatnik.pytanie_zmian->zamknij_z_wynikiem(WynikDialogu::Nie);g_notatnik.przetworz_oczekujace();
    CHECK(g_notatnik.dialog_pliku->czy_otwarta());nacisnij(K_ESC);CHECK(tekst()==niezapisany);CHECK(g_notatnik.zmodyfikowany);
    // "Tak" przy Nowy dla pliku z nazwa: zapis, potem nowy dokument.
    skrot_ctrl('n');g_notatnik.pytanie_zmian->zamknij_z_wynikiem(WynikDialogu::Tak);g_notatnik.przetworz_oczekujace();
    CHECK(fs["/uzytkownicy/Dokumenty/"+ZAZOLC].dane==niezapisany);CHECK(tekst().empty());CHECK(!g_notatnik.zmodyfikowany);
    // "Tak" dla nienazwanego dokumentu: Zapisz jako; anulowanie zatrzymuje Nowy.
    wpisz("bez nazwy ę");skrot_ctrl('n');g_notatnik.pytanie_zmian->zamknij_z_wynikiem(WynikDialogu::Tak);g_notatnik.przetworz_oczekujace();
    CHECK(g_notatnik.dialog_pliku->czy_otwarta()&&g_notatnik.dialog_pliku->tryb()==TrybDialoguPliku::Zapisz);
    nacisnij(K_ESC);CHECK(tekst()=="bez nazwy ę");CHECK(g_notatnik.zmodyfikowany);
    // ... a po zapisie wykonuje Nowy.
    skrot_ctrl('n');g_notatnik.pytanie_zmian->zamknij_z_wynikiem(WynikDialogu::Tak);g_notatnik.przetworz_oczekujace();
    g_notatnik.dialog_pliku->pole_nazwy()->ustaw_tekst("bez nazwy.txt");nacisnij(K_ENTER);
    CHECK(fs["/uzytkownicy/bez nazwy.txt"].dane=="bez nazwy ę");CHECK(tekst().empty());
    // "Nie" przy otwarciu z systemu: plik otwarty.
    wpisz("x");{bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_OTWORZ_PLIK;wyslij(e);}
    g_notatnik.pytanie_zmian->zamknij_z_wynikiem(WynikDialogu::Nie);g_notatnik.przetworz_oczekujace();
    CHECK(tekst()=="Nowa notatka: łódź");CHECK(tytul_zawiera("Notatka źródłowa.txt"));
    // Brak duplikatow dialogow: drugi Ctrl+O przy otwartym dialogu nic nie robi.
    skrot_ctrl('o');DialogPliku* pierwszy=g_notatnik.dialog_pliku;skrot_ctrl('o');CHECK(g_notatnik.dialog_pliku==pierwszy);
    CHECK(popupy().liczba_powierzchni()==1);nacisnij(K_ESC);

    // 9b. Przegladanie myszka przez prawdziwy potok zdarzen okna (popup):
    // podwojne klikniecie katalogu/pliku, strzalki Wstecz/W gore, miejsca,
    // zaznaczony katalog + przycisk akcji, Zapisz jako do biezacego katalogu.
    // Przed poprawka ZarzadcaPopupow nie generowal podwojnego klikniecia, wiec
    // lista w dialogu nie reagowala na mysz, a drzewo miejsc tylko na Enter.
    {const std::string DOK="/uzytkownicy/Dokumenty",PRYW=DOK+"/Prywatne ąę";
     CHECK(!g_notatnik.zmodyfikowany);skrot_ctrl('o');d=g_notatnik.dialog_pliku;CHECK(d&&d->czy_otwarta());
     CHECK(d->biezacy_katalog()==PRYW);
     PasekNarzedzi* pn=d->pasek_narzedzi();Tabela* t=d->tabela_plikow();
     // Strzalki z zestawu ikon zamiast napisow; tekst tylko jako podpowiedz/menu nadmiaru.
     CHECK(pn->element(0)->piksele_ikony()==IkonyEksplorator::nav_wstecz&&pn->element(0)->bok_ikony()==IkonyEksplorator::BOK);
     CHECK(pn->element(1)->piksele_ikony()==IkonyEksplorator::nav_wgore);
     CHECK(!strcmp(pn->element(0)->podpowiedz(),"Wstecz")&&!strcmp(pn->element(1)->podpowiedz(),"Poziom wyżej"));
     CHECK(pn->element(0)->prostokat().szerokosc<CzcionkaUI::zmierz_tekst("Wstecz",1)+16);   // bez napisu
     CHECK(!pn->element(0)->czy_wlaczony()&&pn->element(1)->czy_wlaczony());               // brak historii
     klik(punkt_narzedzia(pn,1));CHECK(d->biezacy_katalog()==DOK);CHECK(pn->element(0)->czy_wlaczony());
     // Podwojne klikniecie katalogu: wejscie; Wstecz: powrot.
     dwuklik(punkt_wiersza(t,"Prywatne ąę"));CHECK(d->biezacy_katalog()==PRYW);CHECK(popupy().czy_otwarty());
     klik(punkt_narzedzia(pn,0));CHECK(d->biezacy_katalog()==DOK);
     // Dwa pojedyncze klikniecia (> 400 ms) nie sa podwojnym: tylko zaznaczenie.
     klik(punkt_wiersza(t,"Prywatne ąę"));klik(punkt_wiersza(t,"Prywatne ąę"));CHECK(d->biezacy_katalog()==DOK);
     // Zaznaczony katalog + przycisk Otworz: wejscie do katalogu, nie otwarcie pliku.
     klik(srodek(d->przycisk_akcji()->obszar_ekranu()));CHECK(d->biezacy_katalog()==PRYW);CHECK(popupy().czy_otwarty());
     // Backspace na liscie: poziom wyzej.
     klik(punkt_narzedzia(pn,1));CHECK(d->biezacy_katalog()==DOK);
     klik(punkt_wiersza(t,"Prywatne ąę"));CHECK(popupy().fokus_popupu()==t);nacisnij(K_BS);CHECK(d->biezacy_katalog()==std::string("/uzytkownicy"));
     // Miejsca: pojedyncze klikniecie (Glowny=0, Uzytkownicy=1, ...).
     klik(punkt_miejsca(d->drzewo_miejsc(),0));CHECK(d->biezacy_katalog()==std::string("/"));CHECK(!pn->element(1)->czy_wlaczony());
     klik(punkt_miejsca(d->drzewo_miejsc(),1));CHECK(d->biezacy_katalog()==std::string("/uzytkownicy"));
     // Miejsce, ktore nie istnieje: komunikat, katalog bez zmian.
     klik(punkt_miejsca(d->drzewo_miejsc(),2));CHECK(d->biezacy_katalog()==std::string("/uzytkownicy"));
     CHECK(popupy().liczba_powierzchni()==2);nacisnij(K_ENTER);CHECK(popupy().liczba_powierzchni()==1);
     // Sciezka wpisana w polu nazwy: katalog -> nawigacja (pole wyczyszczone).
     d->pole_nazwy()->ustaw_tekst("Dokumenty/Prywatne ąę/");nacisnij(K_ENTER);CHECK(d->biezacy_katalog()==PRYW);
     CHECK(!strcmp(d->pole_nazwy()->tekst(),""));
     d->pole_nazwy()->ustaw_tekst("..");nacisnij(K_ENTER);CHECK(d->biezacy_katalog()==DOK);
     // Podwojne klikniecie pliku w Otworz: plik otwarty, dialog zamkniety.
     dwuklik(punkt_wiersza(t,ZAZOLC.c_str()));CHECK(!popupy().czy_otwarty());
     CHECK(std::string(g_notatnik.biezaca_sciezka)==DOK+"/"+ZAZOLC);CHECK(tekst()==fs[DOK+"/"+ZAZOLC].dane);

     // Zapisz jako: nawigacja myszka, zapis do biezacego (otwartego) katalogu.
     klawisz(K_LCTRL,1);klawisz(K_LSHIFT,1);nacisnij(kod_litery('s'));klawisz(K_LSHIFT,0);klawisz(K_LCTRL,0);
     d=g_notatnik.dialog_pliku;CHECK(d&&d->czy_otwarta()&&d->tryb()==TrybDialoguPliku::Zapisz);pn=d->pasek_narzedzi();t=d->tabela_plikow();
     CHECK(d->biezacy_katalog()==DOK);
     klik(punkt_narzedzia(pn,1));CHECK(d->biezacy_katalog()==std::string("/uzytkownicy"));
     dwuklik(punkt_wiersza(t,"Dokumenty"));CHECK(d->biezacy_katalog()==DOK);
     dwuklik(punkt_wiersza(t,"Prywatne ąę"));CHECK(d->biezacy_katalog()==PRYW);CHECK(popupy().czy_otwarty());
     CHECK(!strcmp(d->pole_nazwy()->tekst(),ZAZOLC.c_str()));       // nazwa pliku zachowana przy nawigacji
     d->pole_nazwy()->ustaw_tekst("z myszy");klik(srodek(d->przycisk_akcji()->obszar_ekranu()));
     CHECK(!popupy().czy_otwarty());CHECK(fs.count(PRYW+"/z myszy.txt")&&fs[PRYW+"/z myszy.txt"].dane==tekst());
     CHECK(std::string(g_notatnik.biezaca_sciezka)==PRYW+"/z myszy.txt");
     // Nazwa katalogu wpisana przy Zapisz: wejscie (bez dopisania .txt), nazwa pliku przywrocona.
     klawisz(K_LCTRL,1);klawisz(K_LSHIFT,1);nacisnij(kod_litery('s'));klawisz(K_LSHIFT,0);klawisz(K_LCTRL,0);
     d=g_notatnik.dialog_pliku;pn=d->pasek_narzedzi();t=d->tabela_plikow();CHECK(d->biezacy_katalog()==PRYW);
     klik(punkt_narzedzia(pn,1));d->pole_nazwy()->ustaw_tekst("Prywatne ąę");nacisnij(K_ENTER);
     CHECK(d->biezacy_katalog()==PRYW);CHECK(!strcmp(d->pole_nazwy()->tekst(),"z myszy.txt"));CHECK(popupy().czy_otwarty());
     // Nowy katalog: zaznaczony na liscie, nazwa pliku bez zmian; Zapisz wchodzi, drugi Zapisz zapisuje.
     CHECK(d->utworz_nowy_katalog("Nowy ź"));CHECK(!strcmp(d->pole_nazwy()->tekst(),"z myszy.txt"));
     CHECK(fs.count(PRYW+"/Nowy ź")&&fs[PRYW+"/Nowy ź"].katalog);
     klik(srodek(d->przycisk_akcji()->obszar_ekranu()));CHECK(d->biezacy_katalog()==PRYW+"/Nowy ź");CHECK(popupy().czy_otwarty());
     klik(srodek(d->przycisk_akcji()->obszar_ekranu()));CHECK(!popupy().czy_otwarty());
     CHECK(fs.count(PRYW+"/Nowy ź/z myszy.txt"));CHECK(std::string(g_notatnik.biezaca_sciezka)==PRYW+"/Nowy ź/z myszy.txt");
     // Odswiez nie gubi katalogu ani nazwy.
     klawisz(K_LCTRL,1);klawisz(K_LSHIFT,1);nacisnij(kod_litery('s'));klawisz(K_LSHIFT,0);klawisz(K_LCTRL,0);
     d=g_notatnik.dialog_pliku;pn=d->pasek_narzedzi();CHECK(d->biezacy_katalog()==PRYW+"/Nowy ź");
     klik(punkt_narzedzia(pn,2));CHECK(d->biezacy_katalog()==PRYW+"/Nowy ź");CHECK(d->tabela_plikow()->liczba_wierszy()==1);
     // Podwojne klikniecie istniejacego pliku w Zapisz: pytanie o zastapienie; Esc = bez zapisu.
     dwuklik(punkt_wiersza(d->tabela_plikow(),"z myszy.txt"));CHECK(popupy().liczba_powierzchni()==2);
     nacisnij(K_ESC);CHECK(popupy().liczba_powierzchni()==1);nacisnij(K_ESC);CHECK(!popupy().czy_otwarty());
     // Przywrocenie stanu dla dalszej czesci testu.
     fs.erase(PRYW+"/Nowy ź/z myszy.txt");fs.erase(PRYW+"/Nowy ź");fs.erase(PRYW+"/z myszy.txt");
     argument_startowy="/uzytkownicy/Dokumenty/Prywatne ąę/Notatka źródłowa.txt";
     CHECK(g_notatnik.otworz_plik(argument_startowy.c_str()));CHECK(tekst()=="Nowa notatka: łódź");}

    // 9c. Nowy katalog i skrot Dokumenty przez prawdziwe zdarzenia (mysz/klawiatura).
    // Przyczyny bledow: fokus startowy dialogu nazwy byl na przycisku "Utworz"
    // (wpisywane znaki ginely), skrot Dokumenty wskazywal nieistniejacy
    // /uzytkownicy/Dokumenty, a odmowa PZB dawala ogolny komunikat.
    {const std::string DOM=KatalogiUzytkownika::Domowy,DOKU=KatalogiUzytkownika::Dokumenty,ZG="Zażółć gęślą jaźń";
     dodaj_katalog(DOM);dodaj_katalog(DOKU);dodaj_katalog("/system");dodaj_plik("/system/konfig.ini","x");
     chronione={"/","/uzytkownicy","/system"};
     wyczysc_dokument();
     // Nowy dokument: dialog startuje w Dokumentach uzytkownika.
     skrot_ctrl('o');d=g_notatnik.dialog_pliku;CHECK(d&&d->czy_otwarta());CHECK(d->biezacy_katalog()==DOKU);
     PasekNarzedzi* pn=d->pasek_narzedzi();Tabela* t=d->tabela_plikow();
     // Skrot "Dokumenty" (wiersz 3) z innego katalogu.
     klik(punkt_miejsca(d->drzewo_miejsc(),1));CHECK(d->biezacy_katalog()==std::string("/uzytkownicy"));
     klik(punkt_miejsca(d->drzewo_miejsc(),3));CHECK(d->biezacy_katalog()==DOKU);CHECK(popupy().liczba_powierzchni()==1);
     // Katalog chroniony (System): przegladanie dozwolone, tworzenie odrzucone z bledem PZB.
     klik(punkt_miejsca(d->drzewo_miejsc(),4));CHECK(d->biezacy_katalog()==std::string("/system"));
     klik(punkt_narzedzia_nazwa(pn,"Nowy katalog"));DialogNowegoKatalogu* nk=d->dialog_nowego_katalogu();
     CHECK(nk&&nk->czy_otwarta());CHECK(popupy().liczba_powierzchni()==2);
     CHECK(popupy().fokus_popupu()==nk->pole_nazwy());CHECK(!strcmp(nk->pole_nazwy()->tekst(),"Nowy katalog"));
     CHECK(nk->pole_nazwy()->czy_ma_zaznaczenie());
     {size_t przed=liczba_wpisow_fs();wpisz("Zablokowany");nacisnij(K_ENTER);
      CHECK(popupy().liczba_powierzchni()==3);CHECK(komunikat_zawiera("Brak uprawnień"));
      nacisnij(K_ENTER);CHECK(popupy().liczba_powierzchni()==2);CHECK(nk->czy_otwarta());
      nacisnij(K_ESC);CHECK(popupy().liczba_powierzchni()==1);CHECK(liczba_wpisow_fs()==przed);CHECK(!fs.count("/system/Zablokowany"));}
     // Dokumenty: nazwa z polskimi znakami wpisana z klawiatury, Enter tworzy katalog w biezacym katalogu.
     klik(punkt_miejsca(d->drzewo_miejsc(),3));CHECK(d->biezacy_katalog()==DOKU);
     klik(punkt_narzedzia_nazwa(pn,"Nowy katalog"));CHECK(popupy().fokus_popupu()==nk->pole_nazwy());
     wpisz(ZG.c_str());CHECK(nk->pole_nazwy()->tekst()==ZG);nacisnij(K_ENTER);
     CHECK(popupy().liczba_powierzchni()==1);CHECK(fs.count(DOKU+"/"+ZG)&&fs[DOKU+"/"+ZG].katalog);
     CHECK(!fs.count(DOKU+"/"+ZG+".txt"));CHECK(d->biezacy_katalog()==DOKU);
     // Nowy katalog zaznaczony na liscie; podwojne klikniecie od razu do niego wchodzi.
     CHECK(t->zaznaczony_wiersz()>=0&&strstr(t->komorka(size_t(t->zaznaczony_wiersz()),0),ZG.c_str()));
     dwuklik(punkt_wiersza(t,ZG.c_str()));CHECK(d->biezacy_katalog()==DOKU+"/"+ZG);CHECK(t->liczba_wierszy()==0);
     klik(punkt_narzedzia(pn,0));CHECK(d->biezacy_katalog()==DOKU);
     // Propozycja nazwy i kolejna nazwa przy zajetej: "Nowy katalog", potem "Nowy katalog (2)".
     klik(punkt_narzedzia_nazwa(pn,"Nowy katalog"));nacisnij(K_ENTER);CHECK(fs.count(DOKU+"/Nowy katalog"));
     klik(punkt_narzedzia_nazwa(pn,"Nowy katalog"));CHECK(!strcmp(nk->pole_nazwy()->tekst(),"Nowy katalog (2)"));
     // Anuluj (mysz) nie zmienia systemu plikow.
     {size_t przed=liczba_wpisow_fs();unsigned u=utworzone_katalogi;
      klik(srodek(nk->przycisk_anuluj()->obszar_ekranu()));CHECK(popupy().liczba_powierzchni()==1);
      CHECK(liczba_wpisow_fs()==przed&&utworzone_katalogi==u);
      klik(punkt_narzedzia_nazwa(pn,"Nowy katalog"));nacisnij(K_ESC);CHECK(liczba_wpisow_fs()==przed&&utworzone_katalogi==u);}
     // Duplikat (katalog i plik) oraz nazwy niepoprawne: ostrzezenie, nic nie powstaje, dialog nazwy zostaje.
     dodaj_plik(DOKU+"/plik.txt","p");
     {unsigned u=utworzone_katalogi;
      const std::string zle[]={ZG,"plik.txt","a/b","..",".","x\\y","   ",std::string(56,'d'),"\x01zly","\xc3\x28","\xc0\x80"};
      for(const std::string& z:zle){klik(punkt_narzedzia_nazwa(pn,"Nowy katalog"));nk->pole_nazwy()->ustaw_tekst(z.c_str());
       nacisnij(K_ENTER);CHECK(popupy().liczba_powierzchni()==3);nacisnij(K_ENTER);CHECK(nk->czy_otwarta());nacisnij(K_ESC);
       CHECK(popupy().liczba_powierzchni()==1);}
      CHECK(utworzone_katalogi==u);CHECK(fs[DOKU+"/plik.txt"].dane=="p"&&!fs[DOKU+"/plik.txt"].katalog);
      // 55 bajtow (limit PSF) jest poprawne, spacje na brzegach obciete.
      klik(punkt_narzedzia_nazwa(pn,"Nowy katalog"));nk->pole_nazwy()->ustaw_tekst((" "+std::string(53,'e')+"ą ").c_str());
      nacisnij(K_ENTER);CHECK(popupy().liczba_powierzchni()==1);CHECK(fs.count(DOKU+"/"+std::string(53,'e')+"ą"));
      CHECK(utworzone_katalogi==u+1);}
     // Brak katalogu nadrzednego (usuniety w tle): rzeczywisty blad z BWS.
     dwuklik(punkt_wiersza(t,ZG.c_str()));CHECK(d->biezacy_katalog()==DOKU+"/"+ZG);
     fs.erase(DOKU+"/"+ZG);klik(punkt_narzedzia_nazwa(pn,"Nowy katalog"));nacisnij(K_ENTER);
     CHECK(popupy().liczba_powierzchni()==3);CHECK(komunikat_zawiera("Nie znaleziono"));nacisnij(K_ENTER);nacisnij(K_ESC);
     dodaj_katalog(DOKU+"/"+ZG);
     // Przycisk "Utworz" mysza.
     klik(punkt_narzedzia_nazwa(pn,"Nowy katalog"));nk->pole_nazwy()->ustaw_tekst("Mysz ą");
     klik(srodek(nk->przycisk_utworz()->obszar_ekranu()));CHECK(popupy().liczba_powierzchni()==1);
     CHECK(fs.count(DOKU+"/"+ZG+"/Mysz ą")&&fs[DOKU+"/"+ZG+"/Mysz ą"].katalog);
     // Rozroznienie bledow nawigacji: brak katalogu vs element nie bedacy katalogiem.
     d->pole_nazwy()->ustaw_tekst("/nie/istnieje/");nacisnij(K_ENTER);CHECK(popupy().liczba_powierzchni()==2);nacisnij(K_ENTER);
     CHECK(d->przejdz_do_katalogu("/brak")==false);CHECK(komunikat_zawiera("nie istnieje"));nacisnij(K_ENTER);
     CHECK(d->przejdz_do_katalogu((DOKU+"/plik.txt").c_str())==false);CHECK(komunikat_zawiera("nie jest katalogiem"));nacisnij(K_ENTER);
     nacisnij(K_ESC);CHECK(!popupy().czy_otwarty());

     // Zapisz jako: Nowy katalog nie zmienia nazwy pliku; zapis i ponowne otwarcie pliku z polska nazwa.
     wpisz("Treść w nowym katalogu: ąćęłńóśźż");
     skrot_ctrl('s');d=g_notatnik.dialog_pliku;CHECK(d&&d->tryb()==TrybDialoguPliku::Zapisz);CHECK(d->biezacy_katalog()==DOKU);
     pn=d->pasek_narzedzi();t=d->tabela_plikow();nk=nullptr;
     d->pole_nazwy()->ustaw_tekst("Łódź – notatka");
     dwuklik(punkt_wiersza(t,ZG.c_str()));CHECK(d->biezacy_katalog()==DOKU+"/"+ZG);
     klik(punkt_narzedzia_nazwa(pn,"Nowy katalog"));nk=d->dialog_nowego_katalogu();CHECK(popupy().fokus_popupu()==nk->pole_nazwy());
     wpisz("Nowy katalog");nacisnij(K_ENTER);CHECK(popupy().liczba_powierzchni()==1);
     CHECK(!strcmp(d->pole_nazwy()->tekst(),"Łódź – notatka"));
     const std::string NK=DOKU+"/"+ZG+"/Nowy katalog";CHECK(fs.count(NK)&&fs[NK].katalog);
     dwuklik(punkt_wiersza(t,"Nowy katalog"));CHECK(d->biezacy_katalog()==NK);CHECK(!strcmp(d->pole_nazwy()->tekst(),"Łódź – notatka"));
     klik(srodek(d->przycisk_akcji()->obszar_ekranu()));CHECK(!popupy().czy_otwarty());
     const std::string PL=NK+"/Łódź – notatka.txt";
     CHECK(fs.count(PL)&&fs[PL].dane==tekst());CHECK(std::string(g_notatnik.biezaca_sciezka)==PL);CHECK(tytul_zawiera("Łódź – notatka.txt"));
     {const std::string zapis=tekst();wyczysc_dokument();
      skrot_ctrl('o');d=g_notatnik.dialog_pliku;CHECK(d->biezacy_katalog()==DOKU);t=d->tabela_plikow();
      dwuklik(punkt_wiersza(t,ZG.c_str()));dwuklik(punkt_wiersza(t,"Nowy katalog"));
      dwuklik(punkt_wiersza(t,"Łódź – notatka.txt"));CHECK(!popupy().czy_otwarty());CHECK(tekst()==zapis);
      CHECK(tytul_zawiera("Łódź – notatka.txt"));}
     // Sprzatanie: kolejne sekcje oczekuja poprzedniego ukladu.
     for(auto it=fs.begin();it!=fs.end();)if(!it->first.compare(0,DOM.size(),DOM)||!it->first.compare(0,7,"/system"))it=fs.erase(it);else ++it;
     chronione.clear();
     CHECK(g_notatnik.otworz_plik(argument_startowy.c_str()));CHECK(tekst()=="Nowa notatka: łódź");}

    // 10. Duzy plik: >2048 linii wizualnych bez ucinania; puste pliki; dlugie linie.
    {std::string duzy;for(int i=0;i<6000;++i)duzy+="l"+std::to_string(i)+" ą\n";CHECK(duzy.size()<65535);
     dodaj_plik("/uzytkownicy/duzy.txt",duzy);CHECK(g_notatnik.otworz_plik("/uzytkownicy/duzy.txt"));CHECK(tekst()==duzy);
     CHECK(memo.liczba_wierszy()==6001);z_modyfikatorem(K_LCTRL,K_END);size_t r,c;memo.pozycja_logiczna(r,c);CHECK(r==6001);
     nacisnij(K_UP);memo.pozycja_logiczna(r,c);CHECK(r==6000);wpisz("ż");{const std::string t=tekst(),k="\nżl5999 ą\n";CHECK(t.size()>k.size()&&t.compare(t.size()-k.size(),k.size(),k)==0);}
     g_notatnik.przelacz_zawijanie();z_modyfikatorem(K_LCTRL,K_HOME);memo.pozycja_logiczna(r,c);CHECK(r==1);g_notatnik.przelacz_zawijanie();}
    {dodaj_plik("/uzytkownicy/pusty.txt","");CHECK(g_notatnik.otworz_plik("/uzytkownicy/pusty.txt"));CHECK(tekst().empty());
     wpisz("ą");skrot_ctrl('s');CHECK(fs["/uzytkownicy/pusty.txt"].dane=="ą");
     nacisnij(K_BS);skrot_ctrl('s');CHECK(fs["/uzytkownicy/pusty.txt"].dane.empty());}
    {std::string linia;for(int i=0;i<3000;++i)linia+="ąę";memo.ustaw_tekst(linia.c_str());memo.ustaw_zaznaczenie(0,0);
     z_modyfikatorem(K_LCTRL,K_END);CHECK(memo.pozycja_kursora()==linia.size());
     for(int i=0;i<100;++i)nacisnij(K_BS);CHECK(memo.dlugosc()==linia.size()-200);CHECK(memo.pozycja_kursora()==memo.dlugosc());
     CHECK(BSF::cluster_boundary(memo.tekst(),memo.dlugosc(),memo.pozycja_kursora()));}
    // Powtarzane edycje i cofanie nie psuja UTF-8.
    wyczysc_dokument();for(int i=0;i<200;++i){wpisz("żółw ");if(i%3==0)nacisnij(K_BS);if(i%7==0)skrot_ctrl('z');}
    {const std::string t=tekst();size_t p=0;while(p<t.size()){uint32_t cp;size_t n=CzcionkaUI::dekoduj_utf8(t.c_str(),p,&cp);CHECK(n>0&&cp!=0xFFFD);p+=n;}}

    // 10b. Pasek stanu: szerokosci z metryk czcionki, bez ucinania liczb, waskie okna.
    {PasekStanu& sb=*g_notatnik.pasek_stanu;
     CHECK(sb.liczba_sekcji()==4&&!strcmp(sb.tekst(3),"UTF-8"));
     // pelny: sekcje 1..3 widoczne; zawsze: brak nachodzenia, w granicach paska,
     // widoczne sekcje informacyjne pokazuja pelny tekst.
     auto sprawdz=[&](bool pelny){const int W=sb.obszar_ekranu().szerokosc;int koniec=0;
        CHECK(sb.czy_sekcja_widoczna_w_ukladzie(1));
        for(size_t i=0;i<sb.liczba_sekcji();++i){if(!sb.czy_sekcja_widoczna_w_ukladzie(i)){CHECK(!pelny||i==0);continue;}
            const Prostokat r=sb.prostokat_sekcji(i);CHECK(r.x>=koniec&&r.szerokosc>0&&r.x+r.szerokosc<=W-2);koniec=r.x+r.szerokosc;
            if(i>0){char out[256];sb.tekst_wyswietlany(i,out,sizeof(out));
                // Pelny tekst; skrot z "…" tylko gdy sama sekcja jest szersza niz caly pasek.
                if(strcmp(out,sb.tekst(i))){CHECK(r.x+r.szerokosc==W-2);const std::string o=out;
                    CHECK(o.size()>=3&&o.compare(o.size()-3,3,"\xE2\x80\xA6")==0);
                    CHECK(CzcionkaUI::zmierz_tekst(out,sb.skala_dpi())<=r.szerokosc);continue;}
                CHECK(CzcionkaUI::zmierz_tekst(sb.tekst(i),sb.skala_dpi())<=r.szerokosc);}}};
     auto linie=[&](size_t n){memo.ustaw_tekst(std::string(n-1,'\n').c_str());z_modyfikatorem(K_LCTRL,K_END);g_notatnik.aktualizuj_stan();
        CHECK(std::string(sb.tekst(2))=="Linii: "+std::to_string(n));CHECK(std::string(sb.tekst(1))=="Wiersz: "+std::to_string(n)+", Kol: 1");};
     for(size_t n:{size_t(1),size_t(999),size_t(10000)}){linie(n);sprawdz(true);}
     // 100000 linii przekracza limit dokumentu (64 KiB), wiec sam tekst sekcji.
     sb.ustaw_tekst(1,"Wiersz: 100000, Kol: 100000");sb.ustaw_tekst(2,"Linii: 100000");sprawdz(true);
     const int szer_100k=sb.prostokat_sekcji(2).szerokosc;linie(10);CHECK(sb.prostokat_sekcji(2).szerokosc<szer_100k);
     // Wzorzec: kolejne cyfry do 4 nie zmieniaja szerokosci sekcji (bez skakania).
     {const int w1=sb.prostokat_sekcji(2).szerokosc;linie(999);CHECK(sb.prostokat_sekcji(2).szerokosc==w1);}
     // Stan operacji: rozciagany, dlugi komunikat skrocony z wielokropkiem na granicy znaku.
     sb.ustaw_tekst(0,"Zapisano: /uzytkownicy/domyslny/Dokumenty/Zażółć gęślą jaźń/bardzo długa nazwa pliku ąę.txt");
     {char out[256];sb.tekst_wyswietlany(0,out,sizeof(out));const std::string o=out;
      if(o!=sb.tekst(0)){CHECK(o.size()>=3&&o.compare(o.size()-3,3,"\xE2\x80\xA6")==0);}
      size_t p=0;while(p<o.size()){uint32_t cp;size_t n=CzcionkaUI::dekoduj_utf8(o.c_str(),p,&cp);CHECK(n>0&&cp!=0xFFFD);p+=n;}
      CHECK(CzcionkaUI::zmierz_tekst(out,sb.skala_dpi())<=sb.prostokat_sekcji(0).szerokosc);}
     // Dlugi tekst w sekcji: bufor 192 B bez rozcinania UTF-8.
     {std::string dl;for(int i=0;i<150;++i)dl+="ż";sb.ustaw_tekst(0,dl.c_str());const std::string t=sb.tekst(0);
      CHECK(t.size()<=191&&t.size()%2==0);for(size_t i=0;i<t.size();i+=2)CHECK(t.compare(i,2,"ż")==0);}
     // Rozne szerokosci okna: od szerokiego do bardzo waskiego.
     for(int w:{1280,680,420,300,220,160}){okno.ustaw_rozmiar(w,400);okno.przelicz_uklad();sprawdz(w>=420);}
     okno.ustaw_rozmiar(220,400);okno.przelicz_uklad();
     CHECK(!sb.czy_sekcja_widoczna_w_ukladzie(3));          // kodowanie znika pierwsze, pozycja zostaje
     // Wieksza czcionka systemowa i skala UI: szerokosci z nowych metryk.
     okno.ustaw_rozmiar(680,500);okno.przelicz_uklad();linie(10000);
     const int przed=sb.prostokat_sekcji(1).szerokosc;
     BSF::preview(BSF::default_id(),26);sb.po_zmianie_czcionki();okno.przelicz_uklad();
     CHECK(sb.prostokat_sekcji(1).szerokosc>przed);sprawdz(true);
     for(int w:{1280,500,300}){okno.ustaw_rozmiar(w,500);okno.przelicz_uklad();sprawdz(false);}
     BSF::end_preview();okno.ustaw_rozmiar(680,500);
     sb.ustaw_skale_dpi(SkalaUI::procent(150));okno.przelicz_uklad();sprawdz(true);
     CHECK(sb.prostokat_sekcji(1).szerokosc>przed);
     sb.ustaw_skale_dpi(SkalaUI::procent(100));sb.po_zmianie_czcionki();okno.przelicz_uklad();
     CHECK(sb.prostokat_sekcji(1).szerokosc==przed);sprawdz(true);
     wyczysc_dokument();g_notatnik.aktualizuj_stan();}

    // 11. Pismo od prawej do lewej nadal dziala obok polskiego.
    wyczysc_dokument();wpisz("ąę שלום ść");CHECK(tekst()=="ąę שלום ść");
    skrot_ctrl('a');skrot_ctrl('c');CHECK(std::string(g_mock_clipboard)=="ąę שלום ść");
    nacisnij(K_HOME);for(int i=0;i<12;++i)nacisnij(K_RIGHT);CHECK(memo.pozycja_kursora()==tekst().size());

    // 12. Bezpieczny zapis: kopia bezpieczenstwa, przywracanie, przerwania.
    {
     const std::string D="/uzytkownicy/Bezpieczny";dodaj_katalog(D);
     const std::string X=D+"/Żółta łódź.txt",B0=D+"/~Żółta łódź.txt.kopia",B1=D+"/~Żółta łódź.txt.kopia1";
     const std::string OLD="stara treść\r\nąę źż\r\n",NEW="nowa treść ść\r\nżółw ĄĘ\r\nkoniec dłuższej wersji";
     auto pliki=[&](){std::vector<std::string> v;for(auto& [k,w]:fs)if(!w.katalog&&rodzic(k)==D)v.push_back(k);return v;};
     auto czysc=[&](){for(auto& k:pliki())fs.erase(k);wtracenie=nullptr;pojemnosc_dysku=SIZE_MAX;awaria_alokacji=awaria_usuwania=false;
         migawki.clear();rejestruj_migawki=false;};
     auto komunikat=[&](const char* frag){OknoDialogowe* k=g_notatnik.komunikat;if(!k||!k->czy_otwarta())return false;
         const bool ok=!frag||strstr(k->wiadomosc(),frag);k->zamknij_z_wynikiem(WynikDialogu::OK);return ok;};
     auto zapisz=[&](const std::string& p,const std::string& doc){g_notatnik.memo->ustaw_tekst(doc.c_str());g_notatnik.zmodyfikowany=true;
         return g_notatnik.zapisz_plik_jako(p.c_str());};
     // Niezmiennik po kazdej mutacji: plik docelowy ma pelna stara albo pelna nowa
     // tresc, albo pelna stara tresc jest w zweryfikowanej kopii obok.
     auto bezpieczne_migawki=[&](const std::string& cel,const std::string& stara,const std::string& nowa){
         if(migawki.empty())return false;
         for(auto& m:migawki){auto x=m.find(cel);const bool cel_ok=x!=m.end()&&(x->second==stara||x->second==nowa);bool kopia_ok=false;
             for(auto& [k,v]:m)if(rodzic(k)==rodzic(cel)&&k.find("/~")!=std::string::npos&&v==stara)kopia_ok=true;
             if(!cel_ok&&!kopia_ok)return false;}
         return true;};
     auto wtrac=[&](const std::string& cel,const char* op,Awaria a,int ile){auto licznik=std::make_shared<int>(ile);
         wtracenie=[=](const std::string& o,const std::string& p){if(o==op&&p==cel&&*licznik!=0){if(*licznik>0)--*licznik;return a;}return Awaria::Brak;};};
     unsigned zapisy;

     // a) Sukces: kopia powstaje i jest weryfikowana PRZED nadpisaniem, potem usuwana.
     czysc();dodaj_plik(X,OLD);rejestruj_migawki=true;
     CHECK(zapisz(X,NEW));CHECK(fs[X].dane==NEW);CHECK(pliki().size()==1);CHECK(!g_notatnik.zmodyfikowany);CHECK(!popupy().czy_otwarty());
     CHECK(std::string(g_notatnik.biezaca_sciezka)==X);CHECK(!tytul_zawiera("*"));
     CHECK(migawki.size()>=4);CHECK(migawki[1].count(B0)&&migawki[1][B0]==OLD&&migawki[1][X]==OLD);  // utworz, zapis kopii, ...
     CHECK(bezpieczne_migawki(X,OLD,NEW));
     // Ta sama tresc: brak zapisu i brak kopii.
     zapisy=liczba_zapisow;rejestruj_migawki=false;CHECK(zapisz(X,NEW));CHECK(liczba_zapisow==zapisy);CHECK(pliki().size()==1);

     // b) Czesciowy zapis (przerwany w polowie): oryginal przywrocony z pamieci, kopia usunieta.
     czysc();dodaj_plik(X,OLD);rejestruj_migawki=true;wtrac(X,"zapisz",Awaria::Czesciowy,1);
     CHECK(!zapisz(X,NEW));CHECK(fs[X].dane==OLD);CHECK(pliki().size()==1);CHECK(g_notatnik.zmodyfikowany);CHECK(tytul_zawiera("*"));
     CHECK(komunikat("Przywrócono poprzednią zawartość"));CHECK(bezpieczne_migawki(X,OLD,NEW));
     {bool byl_czesciowy=false;for(auto& m:migawki)if(m.count(X)&&m[X]!=OLD&&m[X]!=NEW)byl_czesciowy=true;CHECK(byl_czesciowy);}
     CHECK(tekst()==NEW);  // dokument w edytorze nietkniety

     // c) Zapis i przywracanie nieudane: kopia zostaje, uzytkownik dostaje jej sciezke.
     czysc();dodaj_plik(X,OLD);rejestruj_migawki=true;wtrac(X,"zapisz",Awaria::Czesciowy,-1);
     CHECK(!zapisz(X,NEW));CHECK(fs[X].dane!=OLD);CHECK(fs.count(B0)&&fs[B0].dane==OLD);CHECK(g_notatnik.zmodyfikowany);
     CHECK(komunikat(("zachowana w pliku kopii: "+B0).c_str()));CHECK(bezpieczne_migawki(X,OLD,NEW));
     // Nastepny zapis nie rusza pozostawionej kopii (uzywa kolejnej nazwy) i sie udaje.
     wtracenie=nullptr;CHECK(zapisz(X,NEW));CHECK(fs[X].dane==NEW);CHECK(fs[B0].dane==OLD);CHECK(!fs.count(B1));CHECK(!g_notatnik.zmodyfikowany);
     // Otwarcie pliku z pozostawiona kopia: ostrzezenie, kopia nietknieta.
     CHECK(g_notatnik.otworz_plik(X.c_str()));CHECK(g_notatnik.komunikat->typ_dialogu()==TypDialogu::Ostrzezenie);
     CHECK(komunikat("~Żółta łódź.txt.kopia"));CHECK(fs[B0].dane==OLD);
     fs.erase(B0);CHECK(g_notatnik.otworz_plik(X.c_str()));CHECK(!popupy().czy_otwarty());

     // d) Brak miejsca na kopie: nic nie zmienione, wlasna niepelna kopia usunieta.
     czysc();dodaj_plik(X,OLD);pojemnosc_dysku=zajete_bez("")+OLD.size()/2;
     CHECK(!zapisz(X,NEW));CHECK(fs[X].dane==OLD);CHECK(pliki().size()==1);CHECK(komunikat("Nie można utworzyć kopii"));
     // Kopia sie miesci, nowa (dluzsza) wersja juz nie: przywrocenie, kopia usunieta.
     czysc();dodaj_plik(X,OLD);pojemnosc_dysku=zajete_bez("")+OLD.size()+4;rejestruj_migawki=true;
     CHECK(!zapisz(X,NEW));CHECK(fs[X].dane==OLD);CHECK(pliki().size()==1);CHECK(komunikat("Przywrócono"));CHECK(bezpieczne_migawki(X,OLD,NEW));

     // e) Niezgodnosc przy odczycie kontrolnym: pliku docelowego i kopii.
     czysc();dodaj_plik(X,OLD);wtrac(X,"zapisz",Awaria::Uszkodzony,1);
     CHECK(!zapisz(X,NEW));CHECK(fs[X].dane==OLD);CHECK(pliki().size()==1);CHECK(komunikat("Przywrócono"));
     czysc();dodaj_plik(X,OLD);wtrac(B0,"zapisz",Awaria::Uszkodzony,-1);zapisy=liczba_zapisow;
     CHECK(!zapisz(X,NEW));CHECK(fs[X].dane==OLD);CHECK(pliki().size()==1);CHECK(komunikat("Nie można utworzyć kopii"));
     // Kopia nie moze byc utworzona (uprawnienia/limit): plik docelowy nawet nie jest otwierany do zapisu.
     czysc();dodaj_plik(X,OLD);wtrac(B0,"utworz",Awaria::Odmowa,-1);zapisy=liczba_zapisow;
     CHECK(!zapisz(X,NEW));CHECK(fs[X].dane==OLD);CHECK(liczba_zapisow==zapisy);CHECK(komunikat("Plik nie został zmieniony"));

     // f) Brak pamieci na kopie oryginalu: odmowa bez zadnego zapisu.
     czysc();dodaj_plik(X,OLD);awaria_alokacji=true;zapisy=liczba_zapisow;
     CHECK(!zapisz(X,NEW));CHECK(liczba_zapisow==zapisy);CHECK(fs[X].dane==OLD);CHECK(komunikat("Brak pamięci"));CHECK(g_notatnik.zmodyfikowany);

     // g) Plik uzytkownika o nazwie kopii nie jest nadpisywany ani usuwany.
     czysc();dodaj_plik(X,OLD);dodaj_plik(B0,"moje dane");CHECK(zapisz(X,NEW));CHECK(fs[B0].dane=="moje dane");CHECK(!fs.count(B1));
     CHECK(komunikat(nullptr)==false);
     // Wszystkie nazwy kopii zajete: odmowa, nic nie zmienione.
     czysc();dodaj_plik(X,OLD);dodaj_plik(B0,"0");for(int i=1;i<10;++i)dodaj_plik(B0+std::to_string(i),std::to_string(i));
     CHECK(!zapisz(X,NEW));CHECK(fs[X].dane==OLD);CHECK(pliki().size()==11);CHECK(fs[B0+"9"].dane=="9");CHECK(komunikat("wolnej nazwy"));

     // h) Kopii nie da sie usunac po udanym zapisie: sukces, kopia z oryginalem zostaje, komunikat w pasku.
     czysc();dodaj_plik(X,OLD);wtrac(B0,"usun",Awaria::Odmowa,-1);
     CHECK(zapisz(X,NEW));CHECK(fs[X].dane==NEW);CHECK(fs[B0].dane==OLD);CHECK(!g_notatnik.zmodyfikowany);
     CHECK(std::string(g_notatnik.pasek_stanu->tekst(0)).find("nie usunięto kopii")!=std::string::npos);

     // i) Pusty oryginal: bez kopii; nieudany zapis przywraca pusty plik.
     czysc();dodaj_plik(X,"");wtrac(X,"zapisz",Awaria::Czesciowy,1);
     CHECK(!zapisz(X,NEW));CHECK(fs[X].dane.empty());CHECK(pliki().size()==1);CHECK(komunikat("Przywrócono"));
     czysc();dodaj_plik(X,OLD);CHECK(zapisz(X,""));CHECK(fs[X].dane.empty());CHECK(pliki().size()==1);

     // j) Nowy plik: niekompletny wlasny plik jest usuwany, inne pliki nietkniete.
     const std::string N=D+"/Nowy ść.txt";
     for(Awaria a:{Awaria::Czesciowy,Awaria::Odmowa,Awaria::Uszkodzony}){
         czysc();dodaj_plik(X,OLD);wtrac(N,"zapisz",a,-1);
         CHECK(!zapisz(N,NEW));CHECK(!fs.count(N));CHECK(fs[X].dane==OLD);CHECK(pliki().size()==1);CHECK(komunikat("Niekompletny plik został usunięty"));
         CHECK(std::string(g_notatnik.biezaca_sciezka)!=N);CHECK(g_notatnik.zmodyfikowany);}
     czysc();pojemnosc_dysku=zajete_bez("")+5;CHECK(!zapisz(N,NEW));CHECK(!fs.count(N));CHECK(komunikat(nullptr));
     czysc();wtrac(N,"zapisz",Awaria::Czesciowy,-1);awaria_usuwania=true;CHECK(!zapisz(N,NEW));CHECK(fs.count(N));
     CHECK(komunikat("nie udało się usunąć"));
     czysc();CHECK(zapisz(N,NEW));CHECK(fs[N].dane==NEW);CHECK(std::string(g_notatnik.biezaca_sciezka)==N);

     // k) Zapisz jako na inny istniejacy plik z bledem: sciezka dokumentu bez zmian.
     czysc();dodaj_plik(X,OLD);CHECK(zapisz(N,"pierwsza"));wtrac(X,"zapisz",Awaria::Czesciowy,1);
     CHECK(!zapisz(X,NEW));CHECK(std::string(g_notatnik.biezaca_sciezka)==N);CHECK(fs[X].dane==OLD);CHECK(fs[N].dane=="pierwsza");komunikat(nullptr);

     // l) Dlugie nazwy z polskimi znakami: nazwa kopii skrocona na granicy UTF-8, <= 55 bajtow.
     {std::string dluga;while(dluga.size()<48)dluga+="ż";dluga+=".txt";const std::string L=D+"/"+dluga;
      czysc();dodaj_plik(L,OLD);rejestruj_migawki=true;CHECK(zapisz(L,NEW));CHECK(fs[L].dane==NEW);CHECK(pliki().size()==1);
      bool widziana=false;for(auto& m:migawki)for(auto& [k,v]:m)if(k!=L&&rodzic(k)==D){widziana=true;const std::string nazwa=k.substr(D.size()+1);
          CHECK(nazwa.size()<=55);CHECK(nazwa[0]=='~');CHECK(nazwa.compare(nazwa.size()-6,6,".kopia")==0);
          CHECK(DialogPliku::czy_poprawny_utf8(nazwa.c_str()));}
      CHECK(widziana);CHECK(bezpieczne_migawki(L,OLD,NEW));}

     // m) Nadpisanie duzego (> 64 KiB) pliku przez Zapisz jako: kopia z pamieci sterty.
     {const std::string W=D+"/wielki.log",duzy(70000,'w');czysc();dodaj_plik(W,duzy);rejestruj_migawki=true;wtrac(W,"zapisz",Awaria::Czesciowy,1);
      CHECK(!zapisz(W,NEW));CHECK(fs[W].dane==duzy);CHECK(pliki().size()==1);CHECK(komunikat("Przywrócono"));CHECK(bezpieczne_migawki(W,duzy,NEW));
      wtracenie=nullptr;CHECK(zapisz(W,NEW));CHECK(fs[W].dane==NEW);CHECK(pliki().size()==1);}

     // n) Przerwanie w dowolnym punkcie (wszystkie scenariusze z bledem i bez):
     //    kazda migawka spelnia niezmiennik, a stan koncowy jest stara lub nowa trescia.
     for(int tryb=0;tryb<4;++tryb){
         czysc();dodaj_plik(X,OLD);rejestruj_migawki=true;
         if(tryb==1)wtrac(X,"zapisz",Awaria::Czesciowy,1);
         if(tryb==2)wtrac(X,"zapisz",Awaria::Czesciowy,-1);
         if(tryb==3)wtrac(X,"zapisz",Awaria::Uszkodzony,-1);
         const bool ok=zapisz(X,NEW);komunikat(nullptr);CHECK(ok==(tryb==0));CHECK(bezpieczne_migawki(X,OLD,NEW));
         CHECK(ok?fs[X].dane==NEW:(fs[X].dane==OLD||(fs.count(B0)&&fs[B0].dane==OLD)));CHECK(g_notatnik.zmodyfikowany==!ok);}

     // o) Pelny przeplyw klawiatury: Ctrl+S z bledem i ponowna proba.
     czysc();dodaj_plik(X,OLD);CHECK(g_notatnik.otworz_plik(X.c_str()));nacisnij(K_END);wpisz(" ą");
     wtrac(X,"zapisz",Awaria::Czesciowy,1);skrot_ctrl('s');CHECK(popupy().czy_otwarty());CHECK(tytul_zawiera("*"));CHECK(fs[X].dane==OLD);
     nacisnij(K_ENTER);CHECK(!popupy().czy_otwarty());skrot_ctrl('s');CHECK(!tytul_zawiera("*"));CHECK(fs[X].dane==tekst());CHECK(pliki().size()==1);
     czysc();
    }

    g_notatnik.zmodyfikowany=false;  // koniec testu bez pytania przy zamknieciu
    delete g_notatnik.dialog_pliku;g_notatnik.dialog_pliku=nullptr;
    delete g_notatnik.pytanie_zmian;g_notatnik.pytanie_zmian=nullptr;
    delete g_notatnik.komunikat;g_notatnik.komunikat=nullptr;
    printf("Notatnik: polskie AltGr, edycja UTF-8, dialogi plikow, menu kontekstowe: %u checks PASS\n",checks);
    fflush(stdout);_Exit(0);
}
