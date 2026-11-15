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
        // Aktualna polityka geometrii: okno moze byc szersze/wyzsze od ekranu i lezec czesciowo poza nim (do 2x ekranu na osi),
        // ale nie dowolnie. Nieprawidlowa geometria nie moze zniszczyc ostatnio opublikowanej powierzchni.
        {
            const uint32_t before=app->bufor_pikseli[w/2+(h/2)*w];
            auto still_committed=[&]{
                return skladacz_obrazu_czy_widoczna(2)&&pobierz_warstwe(2)&&
                       pobierz_warstwe(2)->szerokosc==w&&pobierz_warstwe(2)->wysokosc==h&&
                       pobierz_warstwe(2)->bufor_pikseli[w/2+(h/2)*w]==before;
            };
            // Odrzucane: zero, ujemne, 2x ekranu + 1 na kazdej osi, przepelnienia int, nadmierna alokacja.
            assert(utworz_warstwe(2,0,0,0,h,10)<0);            assert(still_committed());
            assert(utworz_warstwe(2,0,0,w,0,10)<0);            assert(still_committed());
            assert(utworz_warstwe(2,0,0,-5,h,10)<0);           assert(still_committed());
            assert(utworz_warstwe(2,0,0,w,-1,10)<0);           assert(still_committed());
            assert(utworz_warstwe(2,0,0,2*sw+1,h,10)<0);       assert(still_committed());
            assert(utworz_warstwe(2,0,0,w,2*sh+1,10)<0);       assert(still_committed());
            assert(utworz_warstwe(2,0,0,0x7FFFFFFF,0x7FFFFFFF,10)<0); assert(still_committed());
            assert(utworz_warstwe(2,0,0,0x40000000,4,10)<0);   assert(still_committed());
            assert(!zmien_geometrie_warstwy(2,0,0,2*sw+1,h));  assert(still_committed());
            assert(!zmien_geometrie_warstwy(2,0,0,w,2*sh+1));  assert(still_committed());
            assert(!zmien_geometrie_warstwy(2,0,0,0,0));       assert(still_committed());
            assert(!zmien_geometrie_warstwy(2,0,0,-1,-1));     assert(still_committed());
            // Dozwolone: ekran+1, dokladnie 2x ekranu, wspolrzedne ujemne i poza ekranem (kompozytor przycina, nie pisze poza scene).
            assert(utworz_warstwe(3,-40,-30,sw+1,h,11)==3);
            assert(utworz_warstwe(3,-100000,-100000,sw+1,h,11)==3);
            assert(utworz_warstwe(3,sw-3,sh-3,w,h,11)==3);
            assert(skladacz_obrazu_publikuj_klatke(3));compose();   // czesciowo poza ekranem: brak zapisu poza scene (ASan)
            assert(utworz_warstwe(3,-sw,-sh,2*sw,2*sh,11)==3);
            assert(skladacz_obrazu_publikuj_klatke(3));compose();
            assert(utworz_warstwe(3,0,0,2*sw,2*sh,11)==3);
            usun_warstwe(3);
            assert(skladacz_obrazu_czy_widoczna(2));
            // Rozmiar krawedziowy w gore: 2*sw-1 / 2*sh-1 jeszcze dozwolone
            assert(zmien_geometrie_warstwy(2,200,200,2*sw-1,h));
            assert(zmien_geometrie_warstwy(2,200,200,w,h));
            assert(skladacz_obrazu_czy_widoczna(2));        // zmiana rozmiaru zachowuje ostatnia klatke (nie chowa okna)
        }
        // Reuse, resize and PID reuse must require another completed frame.
        assert(utworz_warstwe(2,200,200,w,h,10)==2);
        assert(pobierz_warstwe(2));assert(!skladacz_obrazu_czy_widoczna(2));
        compose();assert(scene[300*sw+300]==0x123456);
        assert(skladacz_obrazu_publikuj_klatke(2));
        for(int i=0;i<w*h;++i)pobierz_warstwe(2)->bufor_pikseli[i]=theme->oknotlo^0x00010101U;   // znana tresc poprzedniej klatki
        assert(zmien_geometrie_warstwy(2,200,200,w+10,h+10));
        // Zatwierdzona polityka: zmiana rozmiaru zachowuje ostatnia klatke i nie chowa okna ...
        assert(skladacz_obrazu_czy_widoczna(2));
        {
            auto* r=pobierz_warstwe(2);assert(r&&r->szerokosc==w+10&&r->wysokosc==h+10);
            assert(r->bufor_pikseli[(h/2)*(w+10)+w/2]==(theme->oknotlo^0x00010101U));   // stara tresc w lewym gornym rogu
            // ... a powiekszony pas jest KRYJACY w kolorze tla okna (nie wylacznie zera => brak tapety w oknie).
            for(int y=0;y<h+10;++y)for(int x=0;x<w+10;++x)
                if(x>=w||y>=h)assert(r->bufor_pikseli[y*(w+10)+x]==(theme->oknotlo&0x00FFFFFFU));
        }
        compose();
        assert(scene[(200+h/2)*sw+200+w+5]==theme->oknotlo);   // wnetrze rozszerzenia na scenie = tlo okna, nie tapeta 0x123456
        // Do czasu nowej klatki aplikacji nadal trzeba opublikowac klatke (flaga resetowana), pozniej juz nie.
        assert(skladacz_obrazu_publikuj_klatke(2));assert(!skladacz_obrazu_publikuj_klatke(2));
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
