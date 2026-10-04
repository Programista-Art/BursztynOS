// Reuse existing UI mocks; execute only this independent hover measurement.
#define main existing_ui_suite_main
#include "test_kontrolki4.cpp"
#undef main
#include <cassert>
struct DirtyCounter : Kontrolka {
    unsigned calls=0;
    uint64_t area=0;
    void uniewaznij_dziecko(Kontrolka*,Prostokat r) override {
        ++calls;area+=uint64_t(r.szerokosc)*r.wysokosc;
    }
    void reset(){calls=0;area=0;}
};
int main() {
    const unsigned scales[]={100U,125U};
    for(unsigned scale: scales) {
        DirtyCounter root;root.ustaw_skale_dpi(SkalaUI::procent(scale));
        Menu menu;menu.dodaj_element("Kopiuj",1);menu.dodaj_element("Wklej",2);
        auto* popup=new WidokMenuPopup;root.dodaj_kontrolke(popup);
        popup->ustaw(&menu,nullptr);popup->rozmiesc(Prostokat(0,0,200,100));
        ZdarzenieMyszki e{};e.typ=TypZdarzenia::RuchMyszy;e.x=20;e.y=15;
        root.reset();popup->obsluz_ruch_myszy(e);
        unsigned entry=root.calls;auto entry_area=root.area;
        root.reset();for(int i=0;i<100;++i){e.x=20+i%10;popup->obsluz_ruch_myszy(e);}
        assert(root.calls==0);
        printf("Hover %u%% popup: enter=%u area=%llu, 100 same-row moves=0 invalidations\n",scale,entry,(unsigned long long)entry_area);
        auto* scroll=new PasekPrzewijania;root.dodaj_kontrolke(scroll);
        scroll->ustaw_zakres(0,1000);scroll->rozmiesc(Prostokat(0,0,20,200));
        e.x=10;e.y=5;root.reset();scroll->obsluz_ruch_myszy(e);
        entry=root.calls;entry_area=root.area;
        root.reset();for(int i=0;i<100;++i)scroll->obsluz_ruch_myszy(e);
        assert(root.calls==0);
        printf("Hover %u%% scrollbar: enter=%u area=%llu, 100 same-part moves=0 invalidations\n",scale,entry,(unsigned long long)entry_area);
        auto* button=new Przycisk("Test");root.dodaj_kontrolke(button);
        button->rozmiesc(Prostokat(0,0,100,30));root.reset();button->obsluz_wejscie_myszy();
        entry=root.calls;entry_area=root.area;root.reset();
        for(int i=0;i<100;++i)button->obsluz_ruch_myszy(e);
        assert(root.calls==0);
        printf("Hover %u%% button: enter=%u area=%llu, 100 inside moves=0 invalidations\n",scale,entry,(unsigned long long)entry_area);
    }
    puts("Cursor hover host PASS");
}
