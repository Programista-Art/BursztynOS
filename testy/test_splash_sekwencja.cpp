// Sekwencjonowanie etapow ekranu powitalnego zalezne od prezentacji (host, ASan/UBSan).
// Model "Ekran" rozdziela obliczenie klatki od jej udanej publikacji i po kazdej
// prezentowanej klatce sprawdza niezmienniki:
//   * w jednej prezentowanej klatce pojawia sie najwyzej jeden NOWY etap,
//   * etap k pojawia sie dopiero, gdy etap k-1 byl pokazany >= ODSTEP_ETAPOW_US
//     i w pelni widoczny >= ODSTEP_ETAPOW_US - wejscie(k-1),
//   * "Niezalezny system" nigdy nie startuje przed ~1 s widocznosci
//     "Polski system operacyjny".
// Druga czesc sprawdza prawdziwy modul (powitanie_zrodlo/powitanie_publikacja):
// render bez publikacji nie przesuwa etapow.
// Opcjonalny argument KATALOG zapisuje klatki PPM scenariusza z przestojem.
#define main suite_powitania_main
#include "test_powitanie.cpp"
#undef main
#include <string>
using namespace Powitanie;

static const char* const NAZWY[LICZBA_ETAPOW]={"logo","tytul","separator","linia1","linia2"};

struct Ekran {
    Sekwencja s;
    unsigned pokazane=0;                       // etapy, ktore juz trafily na ekran
    uint64_t pierwszy[LICZBA_ETAPOW]{}, pierwszy_pelny[LICZBA_ETAPOW]{};
    bool byl_pelny[LICZBA_ETAPOW]{};
    unsigned klatki=0, nowe_w_klatce_max=0;
    std::vector<unsigned> kolejnosc;
    const char* katalog=nullptr; std::string nazwa;
    explicit Ekran(uint64_t t0,bool gotowy=false,uint64_t gotowy_od=0) {
        s.rozpocznij(true,t0);s.gotowy=gotowy;s.gotowy_od=gotowy_od;
        zaprezentuj(t0,false);
    }
    // Obliczenie klatki, ktora NIE trafia na ekran (np. odrzucona publikacja).
    void tylko_oblicz(uint64_t t){s.aktualizuj(t);}
    // Obliczenie + udana publikacja.
    void zaprezentuj(uint64_t t,bool aktualizuj=true) {
        if(aktualizuj)s.aktualizuj(t);
        if(!s.aktywny())return;
        const unsigned nowe=s.widoczne&~pokazane;
        unsigned ile=0;for(unsigned e=0;e<LICZBA_ETAPOW;++e)if((nowe>>e)&1u)++ile;
        if(ile>nowe_w_klatce_max)nowe_w_klatce_max=ile;
        assert(ile<=1);
        for(unsigned e=0;e<LICZBA_ETAPOW;++e) {
            if((nowe>>e)&1u) {
                kolejnosc.push_back(e);
                const uint64_t teraz=s.ostatni;
                pierwszy[e]=teraz;
                if(e>0) {
                    assert((pokazane>>(e-1))&1u);         // poprzedni etap byl na ekranie
                    assert(teraz-pierwszy[e-1]>=ODSTEP_ETAPOW_US);
                    assert(byl_pelny[e-1]);
                    assert(teraz-pierwszy_pelny[e-1]>=ODSTEP_ETAPOW_US-wejscie_etapu(e-1));
                }
                if(e==ETAP_LINIA2)assert(teraz-pierwszy[ETAP_LINIA1]>=ODSTEP_ETAPOW_US);
            }
            if(((s.pelne>>e)&1u)&&!byl_pelny[e]){byl_pelny[e]=true;pierwszy_pelny[e]=s.ostatni;}
        }
        pokazane|=s.widoczne;
        s.potwierdz(s.ostatni);
        ++klatki;
        if(katalog) {
            static std::vector<uint32_t> px;px.assign(640*360,0);
            rysuj(px.data(),640,360,640,100,s,true);
            char plik[512];
            snprintf(plik,sizeof plik,"%s/%s-%03u-%07.3fs.ppm",katalog,nazwa.c_str(),klatki,
                     double(s.ostatni-s.start)/1e6);
            if(FILE* f=fopen(plik,"wb")) {
                fprintf(f,"P6\n640 360\n255\n");
                for(uint32_t c:px){const unsigned char rgb[3]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)};fwrite(rgb,1,3,f);}
                fclose(f);
            }
        }
    }
    bool wszystkie() const {return pokazane==(1u<<LICZBA_ETAPOW)-1u;}
};

static void raport(const char* nazwa,const Ekran& e) {
    printf("  %-34s klatek %4u, kolejnosc:",nazwa,e.klatki);
    for(unsigned k:e.kolejnosc)printf(" %s@%.2fs",NAZWY[k],double(e.pierwszy[k]-e.s.start)/1e6);
    printf("\n");
}

int main(int argc,char** argv) {
    const char* katalog=argc>1?argv[1]:nullptr;
    printf("Sekwencjonowanie zalezne od prezentacji:\n");

    // 1. Normalny start: regularne prezentacje co ~33 ms.
    {
        Ekran e(0);
        for(uint64_t t=33333;t<=6000000;t+=33333)e.zaprezentuj(t);
        assert(e.wszystkie()&&e.s.faza==Faza::Komplet);
        raport("normalny start (30 Hz)",e);
        // Opoznienie wzgledem osi nominalnej to najwyzej ~1 klatka na etap.
        for(unsigned k=ETAP_TYTUL;k<LICZBA_ETAPOW;++k)
            assert(e.pierwszy[k]>=nominalny_start(k)&&e.pierwszy[k]<=nominalny_start(k)+k*33333+1);
    }
    // 2. Przestoj startu: po pierwszej klatce kilka sekund bez prezentacji.
    {
        Ekran e(0);
        if(katalog){e.katalog=katalog;e.nazwa="przestoj";}
        for(uint64_t t=8000000;t<=14000000;t+=50000)e.zaprezentuj(t);
        assert(e.wszystkie());
        assert(e.pierwszy[ETAP_TYTUL]==8000000);
        assert(e.pierwszy[ETAP_SEPARATOR]-e.pierwszy[ETAP_TYTUL]>=ODSTEP_ETAPOW_US);
        assert(e.pierwszy[ETAP_LINIA2]-e.pierwszy[ETAP_LINIA1]>=ODSTEP_ETAPOW_US);
        raport("przestoj 8 s po logo",e);
    }
    // 3. Dluga przerwa miedzy pierwszym a drugim podtytulem.
    {
        Ekran e(0);
        uint64_t t=50000;
        for(;!(e.pokazane&(1u<<ETAP_LINIA1));t+=50000)e.zaprezentuj(t);
        const uint64_t l1=e.pierwszy[ETAP_LINIA1];
        assert(!e.byl_pelny[ETAP_LINIA1]);        // pokazany dopiero pierwszy krok wejscia
        // 6 s bez prezentacji, potem wznowienie.
        e.zaprezentuj(l1+6000000);
        assert(!(e.pokazane&(1u<<ETAP_LINIA2)));   // linia2 NIE w pierwszej klatce po przerwie
        assert(e.byl_pelny[ETAP_LINIA1]);
        for(t=l1+6000000+50000;t<=l1+9000000;t+=50000)e.zaprezentuj(t);
        assert(e.wszystkie());
        assert(e.pierwszy[ETAP_LINIA2]-e.pierwszy_pelny[ETAP_LINIA1]>=ODSTEP_ETAPOW_US-WEJSCIE_TEKSTU_US);
        raport("przerwa 6 s miedzy podtytulami",e);
    }
    // 4. Wiele zaleglych terminow: dlugo liczone klatki, ktore nie trafiaja na ekran.
    {
        Ekran e(0);
        for(uint64_t t=50000;t<=12000000;t+=50000)e.tylko_oblicz(t);
        assert(e.s.etap<=ETAP_TYTUL&&!e.s.pokazany[ETAP_TYTUL]); // stan nie przeskoczyl
        assert(e.s.alfa_tytul>0&&e.s.alfa_tytul<255);          // stoi na pierwszym kroku wejscia
        e.zaprezentuj(12000000);
        assert(e.pokazane==((1u<<ETAP_LOGO)|(1u<<ETAP_TYTUL)));
        for(uint64_t t=12050000;t<=18000000;t+=50000)e.zaprezentuj(t);
        assert(e.wszystkie());
        raport("12 s klatek bez publikacji",e);
    }
    // 5. Przestoj hosta (skok zegara) w srodku sekwencji i wznowienie.
    {
        Ekran e(0);
        for(uint64_t t=50000;t<=2200000;t+=50000)e.zaprezentuj(t);
        assert(e.pokazane&(1u<<ETAP_SEPARATOR));
        for(uint64_t t=30000000;t<=36000000;t+=50000)e.zaprezentuj(t);
        assert(e.wszystkie()&&e.pierwszy[ETAP_LINIA1]>=30000000);
        assert(e.pierwszy[ETAP_LINIA2]-e.pierwszy[ETAP_LINIA1]>=ODSTEP_ETAPOW_US);
        raport("przestoj hosta 2,2 s -> 30 s",e);
    }
    // 6. Szybka gotowosc pulpitu: pozostale etapy pominiete, bez wymuszania.
    {
        Ekran e(0,true,500000);
        for(uint64_t t=33333;t<=MINIMUM_US+FADE_US+100000;t+=33333)e.zaprezentuj(t);
        assert(e.s.stan==Stan::Zakonczony);
        assert(!(e.pokazane&(1u<<ETAP_SEPARATOR))&&!(e.pokazane&(1u<<ETAP_LINIA1))&&!(e.pokazane&(1u<<ETAP_LINIA2)));
        raport("pulpit gotowy po 0,5 s",e);
    }
    // 7. Powtorzone wywolania z ta sama chwila: jeden etap, bez kumulacji.
    {
        Ekran e(0);
        for(int i=0;i<20;++i)e.zaprezentuj(ODSTEP_ETAPOW_US);
        assert(e.s.etap==ETAP_TYTUL&&e.pokazane==((1u<<ETAP_LOGO)|(1u<<ETAP_TYTUL)));
        const uint64_t sygn=e.s.sygnatura();
        for(int i=0;i<20;++i){e.tylko_oblicz(ODSTEP_ETAPOW_US);assert(e.s.sygnatura()==sygn);}
        // Regresja zegara jest ignorowana (monotonicznosc).
        e.zaprezentuj(ODSTEP_ETAPOW_US-500000);assert(e.s.ostatni==ODSTEP_ETAPOW_US&&e.s.etap==ETAP_TYTUL);
        raport("20x ta sama chwila",e);
    }
    // 8. Zegar blisko konca zakresu uint64_t.
    {
        const uint64_t t0=UINT64_MAX-20000000;
        Ekran e(t0);
        for(uint64_t t=t0+50000;t<=UINT64_MAX-1000000;t+=50000)e.zaprezentuj(t);
        assert(e.wszystkie()&&e.s.faza==Faza::Komplet);
        e.zaprezentuj(UINT64_MAX);assert(e.s.aktywny());
        raport("zegar przy UINT64_MAX",e);
    }
    // 9. Losowe przerwy (deterministyczny LCG): niezmienniki w kazdej klatce.
    {
        uint64_t x=0x9E3779B97F4A7C15ull;
        for(int run=0;run<300;++run) {
            Ekran e(0);uint64_t t=0;
            while(!e.wszystkie()&&t<120000000) {
                x=x*6364136223846793005ull+1442695040888963407ull;
                const unsigned r=unsigned(x>>33);
                t+=(r%10==0)?(r%7000000):(16000+r%60000);   // czasem przerwa do 7 s
                if(r%5==0)e.tylko_oblicz(t);else e.zaprezentuj(t);
            }
            assert(e.wszystkie()&&e.nowe_w_klatce_max==1);
        }
        printf("  %-34s 300 przebiegow, najwyzej 1 nowy etap na klatke\n","losowe przerwy i odrzucone klatki");
    }

    // Prawdziwy modul: render bez publikacji nie potwierdza etapu.
    width=320;height=240;
    std::vector<uint32_t> scena(320*240,0x101010);
    auto render=[&](){return powitanie_zrodlo(reinterpret_cast<uint8_t*>(scena.data()),320,240,1280,0,0,320,240,100);};
    now=0;powitanie_inicjalizuj(true);render();powitanie_publikacja();
    assert(sekwencja.pokazany[ETAP_LOGO]&&sekwencja.pokazany_od[ETAP_LOGO]==0);
    for(uint64_t t=50000;t<=9000000;t+=50000){now=t;render();}   // brak publikacji
    assert(sekwencja.etap==ETAP_TYTUL&&!sekwencja.pokazany[ETAP_TYTUL]);
    assert(sekwencja.alfa_separator==0&&sekwencja.alfa_linia1==0&&sekwencja.alfa_linia2==0);
    // Odrzucona prezentacja: etap czeka na potwierdzenie, a kompozytor ponawia klatke.
    {
        const unsigned przed=full;now=9000000;powitanie_tick();
        assert(sekwencja.oczekuje_potwierdzenia()&&full>przed);
    }
    now=9050000;render();powitanie_publikacja();
    assert(sekwencja.pokazany[ETAP_TYTUL]&&sekwencja.pokazany_od[ETAP_TYTUL]==9050000);
    // Publikacja klatki wyrenderowanej przed startem etapu nie potwierdza go:
    // maska pochodzi z ostatnio wyrenderowanej klatki, nie z biezacego stanu.
    for(uint64_t t=9100000;t<=11000000;t+=50000){now=t;render();powitanie_publikacja();}
    const bool sep_pokazany=sekwencja.pokazany[ETAP_SEPARATOR];
    assert(sep_pokazany&&sekwencja.pokazany_od[ETAP_SEPARATOR]>=9050000+ODSTEP_ETAPOW_US);
    powitanie_wycisz();assert(live==0);

    printf("Splash presentation-aware sequencing: %u/%u PASS (ASan/UBSan)\n",checks,checks);
    return 0;
}
