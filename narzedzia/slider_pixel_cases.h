#pragma once
#include <algorithm>
static std::vector<uint64_t> slider_pixel_cases() {
    std::vector<uint64_t> signatures;
    for(auto source:{PLIK_BURSZTYN_JASNY,PLIK_BURSZTYN_NOCNY,PLIK_GRAFIT,
                     PLIK_BALTYCKA_NOC,PLIK_BURSZTYN_KLASYCZNY}) {
        BursztynMotyw theme{};CHECK(motyw_parsuj(source,strlen(source),&theme));
        test_ustaw_aktywny_motyw(&theme);
        Motyw::pobierz_domyslny().zaktualizuj_z_systemu();
        for(unsigned dpi:{100U,125U,150U,200U}) for(int state=0;state<5;++state) {
            Suwak slider; // Creation must already resolve the active theme.
            slider.ustaw_skale_dpi(SkalaUI::procent(dpi));
            const Prostokat bounds{0,0,ui_skaluj(220,dpi),ui_skaluj(30,dpi)};
            slider.rozmiesc(bounds);
            if(state==1)slider.obsluz_wejscie_myszy();
            if(state==2){ZdarzenieMyszki e{};e.przycisk=static_cast<int>(PrzyciskMyszy::Lewy);e.x=bounds.szerokosc/2;e.y=bounds.wysokosc/2;slider.obsluz_wcisniecie_myszy(e);}
            if(state==3)slider.obsluz_uzyskanie_skupienia();
            if(state==4)slider.ustaw_wlaczona(false);
            KontekstRysowania ctx(bounds);
            std::vector<uint32_t> pixels(size_t(bounds.szerokosc)*bounds.wysokosc,0xff123456);
            ctx.ustaw_cel(pixels.data(),bounds);
            auto paint=[&](int value){slider.ustaw_wartosc(value);slider.rysuj(ctx);};
            paint(50);
            auto initial=pixels;
            uint64_t hash=1469598103934665603ULL;
            for(auto pixel:pixels){hash^=pixel;hash*=1099511628211ULL;}
            signatures.push_back(hash);
            // Shapes in an ordinary enabled slider must not create transparent
            // holes which a zero-skipping BWS blit would retain as stale pixels.
            CHECK(std::count(pixels.begin(),pixels.end(),0U)==0);
            for(int i=0;i<100;++i)paint(50);
            CHECK(pixels==initial);
            paint(60);paint(50);CHECK(pixels==initial);
            for(int value:{0,1,25,50,75,99,100}) {
                paint(value);auto once=pixels;
                for(int i=0;i<5;++i)paint(value);
                CHECK(pixels==once);
                CHECK(std::count(pixels.begin(),pixels.end(),0U)==0);
            }
        }
    }
    test_ustaw_aktywny_motyw(nullptr);
    return signatures;
}
