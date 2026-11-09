// Production compositor, with the same bounded host display used by its suite.
#define main existing_compositor_suite_main
#include "test_compositor_cursor.cpp"
#undef main
static unsigned checks=0;
#undef assert
#define assert(c) do {++checks;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
static void compose() {
    skladacz_obrazu_oznacz_dirty();skladacz_obrazu_obsluz_dirty();
}
int main() {
    for(unsigned dpi:{100U,125U}) {
        scale=dpi;sw=1280;sh=720;overlay={};lx=0;ly=0;
        assert(!skladacz_obrazu_publikuj_klatke(-1));
        assert(!skladacz_obrazu_publikuj_klatke(3));
        // A registered desktop remains writable but cannot expose partial content.
        assert(utworz_warstwe(1,0,0,sw,sh,0)==1);
        auto* root=pobierz_warstwe(1);assert(root);
        root->bufor_pikseli[300*sw+300]=0x008A5A00;
        compose();assert(scene[300*sw+300]==0x123456);
        assert(!skladacz_obrazu_czy_widoczna(1));
        assert(skladacz_obrazu_publikuj_klatke(1));
        assert(skladacz_obrazu_czy_widoczna(1));
        root->bufor_pikseli[300*sw+300]=0;compose();
        int w=ui_skaluj(300,dpi),h=ui_skaluj(200,dpi);
        assert(utworz_warstwe(2,200,200,w,h,10)==2);
        assert(skladacz_obrazu_ustaw_tytul(2,"Arbitrary application"));
        auto* app=pobierz_warstwe(2);assert(app);
        bool cleared=true;for(int i=0;i<w*h;++i)cleared &= app->bufor_pikseli[i]==0;
        assert(cleared);
        // Even obsolete pixels deliberately put in a pending buffer cannot escape.
        app->bufor_pikseli[100*w+100]=0x008A5A00;
        compose();assert(scene[300*sw+300]==0x123456);
        GuiOknoInfo windows[16]{};
        assert(skladacz_obrazu_snapshot_okien(windows,16,2)==2);
        assert(windows[1].widoczne==0);
        auto id=skladacz_obrazu_id_okna(2);
        assert(skladacz_obrazu_przywroc(id));
        assert(!skladacz_obrazu_czy_widoczna(2));
        compose();assert(scene[300*sw+300]==0x123456);
        // First complete modern frame publishes client + current themed chrome.
        compositor_theme.oknotlo=dpi==100?0x00191C20U:0x00EDF1F5U;
        compositor_theme.panel=dpi==100?0x0020242AU:0x00D7DFE7U;
        skladacz_obrazu_odswiez_dekoracje();
        compose();assert(scene[300*sw+300]==0x123456);
        auto theme=motyw_pobierz_aktywny();
        for(int i=0;i<w*h;++i)app->bufor_pikseli[i]=theme->oknotlo;
        assert(skladacz_obrazu_publikuj_klatke(2));
        assert(!skladacz_obrazu_publikuj_klatke(2));
        compose();assert(scene[300*sw+300]==theme->oknotlo);
        assert(skladacz_obrazu_czy_widoczna(2));
        assert(scene[204*sw+200+w/2]==theme->panel);
        assert(skladacz_obrazu_minimalizuj(2));compose();
        assert(scene[300*sw+300]==0x123456);
        assert(skladacz_obrazu_przywroc(id));compose();
        assert(scene[300*sw+300]==theme->oknotlo);
        // Failed allocation/geometry must preserve a previously committed surface.
        assert(utworz_warstwe(2,0,0,sw+1,h,10)<0);
        assert(skladacz_obrazu_czy_widoczna(2));
        // Reuse, resize and PID reuse must require another completed frame.
        assert(utworz_warstwe(2,200,200,w,h,10)==2);
        assert(pobierz_warstwe(2));assert(!skladacz_obrazu_czy_widoczna(2));
        compose();assert(scene[300*sw+300]==0x123456);
        assert(skladacz_obrazu_publikuj_klatke(2));
        assert(zmien_geometrie_warstwy(2,200,200,w+10,h+10));
        assert(!skladacz_obrazu_czy_widoczna(2));
        compose();assert(scene[300*sw+300]==0x123456);
        usun_warstwe(2);
        assert(!skladacz_obrazu_przywroc(id));
        assert(utworz_warstwe(2,200,200,w,h,10)==2);
        assert(skladacz_obrazu_id_okna(2)!=id);
        assert(!skladacz_obrazu_przywroc(id));
        assert(!skladacz_obrazu_czy_widoczna(2));
        usun_warstwe(2);usun_warstwe(1);skladacz_reset_ekranu();
    }
    printf("Startup compositor visibility: %u/%u PASS (100%%/125%%)\n",checks,checks);
}
