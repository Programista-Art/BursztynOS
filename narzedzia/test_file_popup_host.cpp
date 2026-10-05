#include "file_operations_host_fixture.h"
#include "../plik_popup_ui.h"
#include <cmath>
using namespace BursztynUI;
// Independent circle predicate: the renderer uses integer-square-root scanlines.
static bool round_inside(Prostokat r,int radius,int x,int y) {
    if(!r.zawiera(x,y))return false;
    radius=std::min(radius,std::min(r.szerokosc,r.wysokosc)/2);
    int px=std::min(x-r.x,r.prawy()-1-x),py=std::min(y-r.y,r.dolny()-1-y);
    if(px>=radius || py>=radius)return true;
    int dx=radius-px,dy=radius-1-py;return dx*dx+dy*dy<=radius*radius;
}
static void corners(PowierzchniaPopup& dialog,int borderWidth=1) {
    auto body=dialog.obszar();int radius=int(dialog.promien_zaokraglenia())*dialog.skala_dpi();
    int shadow=motyw_cien_okna()?2*dialog.skala_dpi():0;
    Prostokat shade{body.x+shadow,body.y+shadow,body.szerokosc,body.wysokosc};
    auto reg=test_popup_rect;
    CHECK(reg.x==body.x && reg.y==body.y);
    CHECK(reg.szerokosc==body.szerokosc+shadow && reg.wysokosc==body.wysokosc+shadow);
    int thickness=borderWidth*dialog.skala_dpi();
    Prostokat inner{body.x+thickness,body.y+thickness,body.szerokosc-2*thickness,body.wysokosc-2*thickness};
    int extent=radius+shadow+2;
    for(int corner=0;corner<4;++corner)for(int yy=0;yy<extent;++yy)for(int xx=0;xx<extent;++xx) {
        int x=reg.x+((corner&1)?reg.szerokosc-1-xx:xx);
        int y=reg.y+((corner&2)?reg.wysokosc-1-yy:yy);
        bool inBody=round_inside(body,radius,x,y),inShadow=shadow && round_inside(shade,radius,x,y);
        auto pixel=test_popup_pixels[(y-reg.y)*reg.szerokosc+x-reg.x];
        CHECK(dialog.zawiera_ksztalt({x,y})==inBody);
        if(!inBody && !inShadow)CHECK(pixel==0);
        else CHECK(pixel!=0);
        if(!inBody && inShadow)CHECK(pixel==(motyw_kolor(MOTYW_KOLOR_RAMKA)|0xff000000U));
        if(inBody && !round_inside(inner,std::max(0,radius-thickness),x,y))
            CHECK(pixel==(motyw_kolor(MOTYW_KOLOR_RAMKA)|0xff000000U));
    }
    CHECK(test_popup_pixels[0]==0); // no rectangular border behind the top left
}
int main() {
    g_test_bws_hook=explorer_bws;files={{"/",true},{"/uzytkownicy",true},{"/uzytkownicy/Pulpit",true}};
    for(unsigned scale:{100U,125U})for(auto resolution:{std::pair<int,int>{1280,720},{1920,1080}}) {
        ui_skala_procent=scale;Prostokat work{0,0,resolution.first,resolution.second-ui_skaluj(40,scale)};
        for(auto source:{PLIK_BURSZTYN_JASNY,PLIK_BURSZTYN_NOCNY,PLIK_GRAFIT,PLIK_BALTYCKA_NOC,PLIK_BURSZTYN_KLASYCZNY}) {
            BursztynMotyw theme{};CHECK(motyw_parsuj(source,strlen(source),&theme));test_ustaw_aktywny_motyw(&theme);
            for(const char* title:{"Operacja plikowa","Utwórz folder","Utwórz plik","Zmień nazwę","Przenieś","Potwierdzenie"}) {
                PlikiUI::DialogPlikowy d;bool confirm=!strcmp(title,"Potwierdzenie");
                CHECK(d.otworz(title,confirm?nullptr:"zażółć","Nazwa elementu:",work,work));CHECK(test_popup_registered);
                corners(d);
                auto old=test_popup_pixels;
                CHECK(d.events.popups.odrysuj_popupy(true));CHECK(test_popup_pixels==old);
                if(!confirm) {
                    auto r=d.obszar();auto field=d.dziecko(2)->obszar();
                    bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_MYSZ_PRAWY_DOWN;e.x=r.x+field.x+10;e.y=r.y+field.y+10;
                    auto* focus=d.events.popups.fokus_popupu();CHECK(d.zdarzenie(e));CHECK(d.czy_otwarta());
                    CHECK(!strcmp(d.tekst(),"zażółć"));CHECK(d.events.popups.fokus_popupu()==focus);CHECK(d.wynik()==0);
                    e.typ=BWS_ZDARZENIE_MYSZ_PRAWY_UP;CHECK(d.zdarzenie(e));CHECK(d.czy_otwarta());
                }
                // The modal consumes clicks on transparent corners too.
                ZdarzenieMyszki click{};click.x=d.obszar().x;click.y=d.obszar().y;click.przycisk=1;
                CHECK(d.events.popups.obsluz_wcisniecie(click));CHECK(d.czy_otwarta());
                d.zamknij();CHECK(!test_popup_registered);
            }
            PlikiUI::WyborKatalogu chooser;
            CHECK(chooser.otworz("element","/uzytkownicy/Pulpit",work,work));corners(chooser,2);chooser.zamknij();CHECK(!test_popup_registered);
        }
    }
    test_ustaw_aktywny_motyw(nullptr);
    printf("Rounded file popups: %d/%d PASS (corners, shadows, modal input, 100/125%%)\n",checks,checks);
}
