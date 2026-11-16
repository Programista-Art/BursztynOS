// Prawdziwy modul produkcyjny i renderer masek TTF. Bez emulatora i duplikatu stanu.
#define main existing_welcome_suite_main
#include "test_powitanie.cpp"
#undef main
#include "diagnostyka_klatek_startu.h"
using namespace Powitanie;

// Probki czasu (us) obejmujace wszystkie etapy animacji i hold.
static constexpr uint64_t samples[]={
    0,300000,600000,1000000,1200000,1500000,2000000,2200000,2450000,
    3000000,3250000,3500000,4000000,4250000,4500000,6000000,9000000
};
static uint64_t frame_hash(const uint32_t* p,int pitch=1280) {
    return KlatkiStartuDiag::hash(p,1280,720,pitch);
}
int main() {
    width=1280;height=720;
    const int pitch=1288;const uint32_t pad=0xDEADBEEF;
    std::vector<uint32_t> scene(size_t(pitch)*height,0x253749);
    auto start=[&](unsigned dpi){now=0;powitanie_inicjalizuj(true);
        powitanie_zrodlo(reinterpret_cast<uint8_t*>(scene.data()),width,height,pitch*4,0,0,width,height,dpi);
        powitanie_publikacja();powitanie_tick();};
    for(unsigned dpi:{100U,125U}) {
        // Uklad: brak nachodzenia elementow i stala os pozioma.
        const auto g=geometria(width,height,dpi);
        assert(g.logo_rdzen.y>=0);
        assert(g.tytul.r.y>g.logo_rdzen.y+g.logo_rdzen.h);
        assert(g.separator.y>=g.tytul.r.y+g.tytul.r.h);
        assert(g.linia1.r.y>g.separator.y+g.separator.h);
        assert(g.linia2.r.y>=g.linia1.r.y+g.linia1.r.h);
        assert(g.linia2.r.y+g.linia2.r.h<=height);
        printf("Uklad %u%%: logo %d,%d %dx%d | tytul %d,%d %dx%d | separator %d,%d %dx%d | l1 %d,%d %dx%d | l2 %d,%d %dx%d\n",
            dpi,g.logo_rdzen.x,g.logo_rdzen.y,g.logo_rdzen.w,g.logo_rdzen.h,
            g.tytul.r.x,g.tytul.r.y,g.tytul.r.w,g.tytul.r.h,
            g.separator.x,g.separator.y,g.separator.w,g.separator.h,
            g.linia1.r.x,g.linia1.r.y,g.linia1.r.w,g.linia1.r.h,
            g.linia2.r.x,g.linia2.r.y,g.linia2.r.w,g.linia2.r.h);

        // Kazda probka (pulpit niegotowy, wiec splash pozostaje widoczny):
        // kompletna klatka, tlo poza ukladem, idempotentny render stanu.
        for(uint64_t us:samples) {
            start(dpi);
            now=us;
            auto* out=reinterpret_cast<uint32_t*>(powitanie_zrodlo(
                reinterpret_cast<uint8_t*>(scene.data()),width,height,pitch*4,0,0,width,height,dpi));
            assert(out!=scene.data()&&powitanie_pierwsza_klatka_gotowa());
            assert(out[0]==TLO&&out[pitch-1]==TLO);
            assert(out[size_t(height-1)*pitch]==TLO);
            bool poza_czyste=true;
            for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
                const bool w_srodku=x>=g.dirty.x&&x<g.dirty.x+g.dirty.w&&
                                    y>=g.dirty.y&&y<g.dirty.y+g.dirty.h;
                if(!w_srodku&&out[size_t(y)*pitch+x]!=TLO){poza_czyste=false;y=height;break;}
            }
            assert(poza_czyste);
            std::vector<uint32_t> powtorka(scene.size(),pad);
            rysuj(powtorka.data(),width,height,pitch,dpi,sekwencja,true);
            assert(frame_hash(out,pitch)==frame_hash(powtorka.data(),pitch));
            powitanie_wycisz();assert(live==0);
        }
        // Pulpit gotowy od poczatku: po MINIMUM_US+FADE_US splash jest usuniety.
        start(dpi);powitanie_pulpit_gotowy();
        now=MINIMUM_US+FADE_US;
        assert(powitanie_zrodlo(reinterpret_cast<uint8_t*>(scene.data()),width,height,pitch*4,0,0,width,height,dpi)
               ==reinterpret_cast<uint8_t*>(scene.data())&&!powitanie_aktywne());
        powitanie_wycisz();
        // Kolejne klatki produkcyjne dziela te sama alokacje. Bufor nigdy nie
        // jest czyszczony, takze na granicach wlacznie/wylacznie.
        start(dpi);uint32_t* backing=nullptr;
        for(uint64_t us:{1800000ULL,2000000ULL,2500000ULL,3400000ULL,3600000ULL,4200000ULL,5000000ULL}) {
            now=us;
            auto* out=reinterpret_cast<uint32_t*>(powitanie_zrodlo(
                reinterpret_cast<uint8_t*>(scene.data()),width,height,pitch*4,0,0,width,height,dpi));
            if(!backing)backing=out;assert(out==backing);
            std::vector<uint32_t> fresh(scene.size(),0);
            rysuj(fresh.data(),width,height,pitch,dpi,sekwencja,true);
            assert(frame_hash(out,pitch)==frame_hash(fresh.data(),pitch));
        }
        powitanie_wycisz();assert(live==0);

        // Brudna pamiec z poprzedniej geometrii jest zawsze odtwarzana w calosci.
        std::vector<uint32_t> reuse(scene.size(),0xBADA55),fresh(scene.size(),pad);
        Sekwencja state;state.rozpocznij(true,0);state.przelicz(5000000);
        rysuj(reuse.data(),width,height,pitch,dpi,state,false);
        rysuj(fresh.data(),width,height,pitch,dpi,state,true);
        assert(frame_hash(reuse.data(),pitch)==frame_hash(fresh.data(),pitch));
        bool padding=true;
        for(int y=0;y<height;++y)for(int x=width;x<pitch;++x)padding&=fresh[y*pitch+x]==pad;
        assert(padding);

        // Probka po przestoju renderuje tylko biezacy stan absolutny.
        start(dpi);now=4400000;
        auto* out=reinterpret_cast<uint32_t*>(powitanie_zrodlo(
            reinterpret_cast<uint8_t*>(scene.data()),width,height,pitch*4,0,0,width,height,dpi));
        assert(sekwencja.alfa_linia1==255&&sekwencja.alfa_linia2>0);
        std::vector<uint32_t> bezposredni(scene.size(),0);
        Sekwencja direct;direct.rozpocznij(true,0);direct.przelicz(4400000);
        rysuj(bezposredni.data(),width,height,pitch,dpi,direct,true);
        assert(frame_hash(out,pitch)==frame_hash(bezposredni.data(),pitch));
        powitanie_wycisz();
    }
    printf("Production splash snapshots/centering/reusable backing/absolute: %u/%u PASS\n",checks,checks);
    return 0;
}
