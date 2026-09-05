#pragma once

static uint64_t k3_area(const Prostokat& r) {
    return r.czy_pusty() ? 0 : static_cast<uint64_t>(r.szerokosc)*r.wysokosc;
}
static uint64_t k3_dirty(const Kontrolka& k) {
    uint64_t n=0;for(size_t i=0;i<k.brudne_regiony_.liczba;++i)n+=k3_area(k.brudne_regiony_.prostokaty[i]);return n;
}
static void k3_paint(Kontrolka& k) {
    ObszaryBrudne r=k.brudne_regiony_;
    for(size_t i=0;i<r.liczba;++i) { KontekstRysowania c;c.dodaj_przyciecie(r.prostokaty[i]);k.rysuj(c); }
    k.wyczysc_brudny_obszar();
}
static void k3_click(Okno& w,int x,int y,uint64_t t) {
    bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_MYSZ_DOWN;e.x=w.x()+x;e.y=w.y()+y;e.przyciski=1;e.timestamp=t;
    w.obsluz_zdarzenie(e);e.typ=BWS_ZDARZENIE_MYSZ_UP;e.przyciski=0;w.obsluz_zdarzenie(e);
}
static void wykonaj_stabilizacje_k3() {
    bws_log("\n[K3 STABILIZACJA] start\n");
    uint32_t fail=bws_test_blad;
    uint64_t live=gui_liczba_aktywnych_alokacji_sterty();
    {
        WidokDrzewiastoKolumnowy w;w.rozmiesc(Prostokat(0,0,420,240));w.dodaj_kolumne("Nazwa",250);
        auto a=w.dodaj_korzen("Pierwszy"),b=w.dodaj_korzen("Drugi");
        for(int i=0;i<50;++i)w.dodaj_korzen("Wiersz");
        ZdarzenieMyszki e{};e.x=100;e.y=35;w.obsluz_ruch_myszy(e);w.wyczysc_brudny_obszar();w.zeruj_pomiary();
        uint64_t alloc=gui_liczba_alokacji_sterty();
        for(int i=0;i<5000;++i)w.obsluz_ruch_myszy(e);
        bws_sprawdz_msg(w.pomiary_.dirty_calls==0&&w.pomiary_.layout_passes==0&&gui_liczba_alokacji_sterty()==alloc,"K3 tree stable 5000 zero dirty/layout/alloc");
        bws_log_u64("[K3 STABLE TREE] mouse_events=",w.pomiary_.mouse_events);
        for(int i=0;i<1000;++i) {
            w.wyczysc_brudny_obszar();e.y=(i&1)?35:55;w.obsluz_ruch_myszy(e);
            bws_sprawdz_msg(k3_dirty(w)<=2*406*20,"K3 tree hover only two rows");
            w.wyczysc_brudny_obszar();w.ustaw_wybrany_wezel((i&1)?a:b);
            bws_sprawdz_msg(k3_dirty(w)<=2*406*20&&w.pobierz_brudny_obszar().y>=26,"K3 tree selection excludes header/background");
            k3_paint(w);
            bws_sprawdz(w.liczba_ostatnio_narysowanych_wierszy()<=2);
        }
        bws_sprawdz(w.pomiary_.layout_passes==0&&gui_liczba_alokacji_sterty()==alloc);
        // A dirty row must leave sentinel pixels elsewhere untouched, with the canonical text renderer.
        auto* pixels=static_cast<uint32_t*>(gui_malloc(420*240*4));
        if(pixels) {
            for(int i=0;i<420*240;++i)pixels[i]=0x00123456;
            KontekstRysowania c;c.ustaw_cel(pixels,Prostokat(0,0,420,240));c.dodaj_przyciecie(Prostokat(1,26,405,20));w.rysuj(c);
            bws_sprawdz(pixels[10*420+10]==0x00123456&&pixels[100*420+10]==0x00123456&&pixels[30*420+10]!=0x00123456);
            gui_free(pixels);
        } else bws_sprawdz(false);
    }
    {
        InspektorWlasciwosci w;w.rozmiesc(Prostokat(0,0,360,220));
        for(int i=0;i<1000;++i)w.dodaj_liczbe(i+1,"Wartość",i,"Kategoria");
        w.rozpocznij_edycje(0);auto* editor=w.aktywny_edytor();
        ZdarzenieRolki e{};e.delta_y=-1;w.obsluz_rolke(e);
        bws_sprawdz_msg(!w.czy_edytuje()&&!editor->czy_widoczna(),"K3 inspector scrolling cancels editor safely");
        w.wyczysc_brudny_obszar();w.zeruj_pomiary();w.pasek_pionowy().zeruj_pomiary();editor->zeruj_pomiary();
        uint64_t alloc=gui_liczba_alokacji_sterty();
        uint64_t maxrows=0;
        for(int i=0;i<1000;++i) {
            e.delta_y=(i&1)?1:-1;w.obsluz_rolke(e);k3_paint(w);
            if(w.liczba_ostatnio_narysowanych()>maxrows)maxrows=w.liczba_ostatnio_narysowanych();
            bws_sprawdz(w.pasek_pionowy().czy_widoczna()&&w.liczba_ostatnio_narysowanych()<=9);
        }
        bws_sprawdz_msg(gui_liczba_alokacji_sterty()==alloc&&w.pomiary_.layout_passes==0&&editor->pomiary_.layout_passes==0,"K3 inspector 1000 wheels no allocations/layout/editor recreation");
        bws_log_u64("[K3 INSPECTOR] wheel_allocations=",gui_liczba_alokacji_sterty()-alloc);
        bws_log_u64("[K3 INSPECTOR] max_painted_rows=",maxrows);
    }
    {
        Okno w("K3 input",420,300);auto* root=static_cast<Panel*>(w.kontrolka_glowna());
        auto& p=root->dodaj<PrzyciskPodzielony>("Wykonaj");p.ustaw_preferowany_rozmiar(Rozmiar(180,36));
        auto& toggle=root->dodaj<Przelacznik>();
        struct C { int menu=0,primary=0,toggle=0; } counts;
        p.przy_kliknieciu([](PrzyciskPodzielony*,void* d){++static_cast<C*>(d)->primary;},&counts);
        p.menu().dodaj_element("Akcja",1,[](ElementMenu&,void* d){++static_cast<C*>(d)->menu;},&counts);
        toggle.przy_zmianie([](Przelacznik*,bool,void* d){++static_cast<C*>(d)->toggle;},&counts);
        w.przelicz_uklad();KontekstRysowania ctx;w.rysuj(ctx,true);
        uint64_t layouts=w.liczba_przejsc_ukladu(),alloc=gui_liczba_alokacji_sterty();
        for(int i=0;i<1000;++i) {
            auto r=p.obszar_ekranu();k3_click(w,r.prawy()-8,r.y+12,1000000+i*1000000ULL);
            bws_sprawdz(p.czy_menu_otwarte()&&!w.przechwycenie_myszy());
            auto menu=w.zarzadca_popupow().prostokat_zarejestrowany_ekranu();
            k3_click(w,menu.x-w.x()+30,menu.y-w.y()+10,1001000+i*1000000ULL);
            k3_click(w,r.x+20,r.y+12,1002000+i*1000000ULL);
            bws_sprawdz_msg(counts.menu==i+1&&counts.primary==i+1&&!p.czy_menu_otwarte()&&!w.przechwycenie_myszy()&&!w.zarzadca_popupow().czy_przechwytuje_mysz()&&p.wcisnieta_czesc()==CzescPrzyciskuPodzielonego::Brak,"K3 split menu-primary 1000 capture/callbacks");
            w.rysuj(ctx);
        }
        bws_sprawdz(gui_liczba_alokacji_sterty()==alloc&&w.liczba_przejsc_ukladu()==layouts);
        toggle.zeruj_pomiary();alloc=gui_liczba_alokacji_sterty();
        for(int i=0;i<1000;++i) {
            auto r=toggle.obszar_ekranu();k3_click(w,r.x+10,r.y+10,10000000+i*1000000ULL);w.rysuj(ctx);
            bws_sprawdz(!toggle.czy_wcisnieta()&&!w.przechwycenie_myszy());
        }
        bws_sprawdz_msg(counts.toggle==1000&&toggle.pomiary_.layout_passes==0&&gui_liczba_alokacji_sterty()==alloc,"K3 toggle 1000 exact callbacks zero allocations/layout");
        bws_log_u64("[K3 INPUT] primary_callbacks=",counts.primary);bws_log_u64("[K3 INPUT] menu_callbacks=",counts.menu);
        bws_log_u64("[K3 INPUT] toggle_callbacks=",counts.toggle);
        toggle.wyczysc_brudny_obszar();toggle.zeruj_pomiary();toggle.ustaw_wartosc(toggle.wartosc());
        bws_sprawdz(toggle.pomiary_.dirty_calls==0&&counts.toggle==1000);
    }
    {
        Okno w("K3 picker",640,480);auto* root=static_cast<Panel*>(w.kontrolka_glowna());
        auto& p=root->dodaj<SelektorKoloru>();p.ustaw_preferowany_rozmiar(Rozmiar(240,36));w.przelicz_uklad();
        int callbacks=0;p.przy_zmianie([](SelektorKoloru*,uint32_t,void* d){++*static_cast<int*>(d);},&callbacks);
        uint64_t alloc=gui_liczba_alokacji_sterty();
        const uint32_t colors[]={SelektorKoloru::WIDOCZNA_CZERN,0xFFFFFF,0xFF0000,0x00FF00,0x0000FF};
        for(uint32_t c:colors) {
            bws_sprawdz(p.otworz_popup());bws_sprawdz(p.ustaw_rgb((c>>16)&255,(c>>8)&255,c&255));
            bws_sprawdz(p.kolor_podgladu()==c);bws_sprawdz(p.zatwierdz_popup()&&p.pobierz_kolor()==c);
        }
        bws_sprawdz(p.otworz_popup());p.ustaw_hsv(120,255,255);bws_sprawdz(p.kolor_podgladu()==0x00FF00);
        p.pole_hex().ustaw_tekst("#1234AB");bws_sprawdz(p.kolor_podgladu()==0x1234AB);
        p.pole_rgb(0).ustaw_tekst("255");bws_sprawdz(p.kolor_podgladu()==0xFF34AB);
        p.pole_hex().ustaw_tekst("#GGGGGG");bws_sprawdz(!p.zatwierdz_popup()&&p.czy_popup_otwarty());
        p.pole_hex().ustaw_tekst("#000000");p.pole_rgb(0).ustaw_tekst("999");bws_sprawdz(!p.zatwierdz_popup());
        bws_sprawdz(!p.ustaw_rgb(-1,0,0)&&!p.ustaw_rgb(0,256,0)&&!p.ustaw_hsv(360,0,0));
        p.anuluj_popup();bws_sprawdz(p.pobierz_kolor()==0x0000FF&&!p.czy_popup_otwarty());
        for(int i=0;i<100;++i) { p.otworz_popup();p.ustaw_hsv(i,255,255);p.anuluj_popup(); }
        p.otworz_popup();ZdarzenieKlawiatury esc{};esc.kod_klawisza=27;w.zarzadca_popupow().obsluz_klawisz(esc);
        bws_sprawdz(!p.czy_popup_otwarty()&&!w.zarzadca_popupow().czy_przechwytuje_mysz());
        p.otworz_popup();w.minimalizuj();bws_sprawdz(!p.czy_popup_otwarty());
        bws_sprawdz_msg(gui_liczba_alokacji_sterty()==alloc,"K3 picker stable lifecycle/input zero allocations");
        bws_log_u64("[K3 PICKER] lifecycle_allocations=",gui_liczba_alokacji_sterty()-alloc);
    }
    {
        ZarzadcaPopupow z;z.ustaw_geometrie_wlasciciela(Prostokat(0,0,640,480),Prostokat(0,28,640,452));
        auto* p=new SelektorKoloru();p->ustaw_zarzadce_popupow(&z);p->rozmiesc(Prostokat(10,40,230,36));p->otworz_popup();delete p;
        bws_sprawdz_msg(!z.czy_otwarty()&&z.fokus_popupu()==nullptr,"K3 picker destruction detaches popup");
    }
    {
        WidokMiniatur w;w.rozmiesc(Prostokat(0,0,420,240));static uint32_t image[64];for(auto& p:image)p=0x00ABCDEF;
        for(int i=0;i<1000;++i)w.dodaj_element(i,"Miniatura",image,8,8);
        ZdarzenieMyszki e{};e.x=10;e.y=10;w.obsluz_ruch_myszy(e);w.wyczysc_brudny_obszar();w.zeruj_pomiary();
        uint64_t alloc=gui_liczba_alokacji_sterty();for(int i=0;i<5000;++i)w.obsluz_ruch_myszy(e);
        bws_sprawdz_msg(gui_liczba_alokacji_sterty()==alloc&&w.pomiary_.dirty_calls==0&&w.pomiary_.layout_passes==0,"K3 thumbnail 5000 same item zero allocations/dirty/layout");
        bws_log_u64("[K3 THUMBNAIL] stable_mouse_events=",w.pomiary_.mouse_events);
        bws_log_u64("[K3 THUMBNAIL] stable_mouse_dirty_calls=",w.pomiary_.dirty_calls);
        auto& bar=w.pasek_pionowy();bar.zeruj_pomiary();
        uint64_t maxpaint=0,wheelpixels=0;
        struct Seen { uint64_t first=99999,last=0; } seen;
        w.ustaw_renderer([](WidokMiniatur*,KontekstRysowania&,const WidokMiniatur::Element& e,Prostokat,void* d){auto* s=static_cast<Seen*>(d);if(e.identyfikator<s->first)s->first=e.identyfikator;if(e.identyfikator>s->last)s->last=e.identyfikator;},&seen);
        alloc=gui_liczba_alokacji_sterty();
        for(int i=0;i<1000;++i) {
            w.wyczysc_brudny_obszar();ZdarzenieRolki wheel{};wheel.delta_y=(i&1)?1:-1;w.obsluz_rolke(wheel);
            wheelpixels=k3_dirty(w);seen={};k3_paint(w);
            uint64_t first=static_cast<uint64_t>(bar.wartosc()/100)*3;
            bws_sprawdz_msg(seen.first==first&&seen.last<first+12,"K3 current visible range before paint after scroll");
            if(w.liczba_ostatnio_narysowanych_elementow()>maxpaint)maxpaint=w.liczba_ostatnio_narysowanych_elementow();
            bws_sprawdz(bar.czy_widoczna()&&bar.prostokat_uchwytu().wysokosc>0);
        }
        bws_log_u64("[K3 THUMBNAIL] wheel_allocations=",gui_liczba_alokacji_sterty()-alloc);
        bws_sprawdz(gui_liczba_alokacji_sterty()==alloc);
        bar.ustaw_wartosc(0);auto thumb=bar.prostokat_uchwytu();ZdarzenieMyszki d{};d.przycisk=1;
        d.x=bar.obszar_ekranu().x+thumb.x+thumb.szerokosc/2;d.y=bar.obszar_ekranu().y+thumb.y+thumb.wysokosc/2;bar.obsluz_wcisniecie_myszy(d);
        uint64_t dragpixels=0;alloc=gui_liczba_alokacji_sterty();
        for(int i=0;i<1000;++i) {
            w.wyczysc_brudny_obszar();d.x=800;d.y=40+(i%150);bar.obsluz_ruch_myszy(d);dragpixels=k3_dirty(w);k3_paint(w);
            bws_sprawdz_msg(bar.czy_widoczna()&&bar.czy_przeciagany()&&bar.prostokat_uchwytu().wysokosc>0&&bar.wartosc()>=0&&bar.wartosc()<=bar.maksimum()-bar.rozmiar_strony(),"K3 scrollbar stays visible/captured/clamped through 1000 outside-thumb drag events");
        }
        bar.obsluz_zwolnienie_myszy(d);bws_sprawdz(!bar.czy_przeciagany()&&gui_liczba_alokacji_sterty()==alloc);
        bws_log_u64("[K3 THUMBNAIL] drag_allocations=",gui_liczba_alokacji_sterty()-alloc);
        bar.ustaw_wartosc(0);w.ustaw_wybrany_indeks(0);alloc=gui_liczba_alokacji_sterty();
        for(int i=0;i<1000;++i) {
            w.wyczysc_brudny_obszar();w.ustaw_wybrany_indeks((i&1)?0:1);
            bws_sprawdz_msg(k3_dirty(w)<=22400&&k3_dirty(w)<k3_area(w.prostokat_widoku()),"K3 selection must not invalidate full viewport");k3_paint(w);
        }
        bws_sprawdz(gui_liczba_alokacji_sterty()==alloc&&w.pomiary_.layout_passes==0&&bar.pomiary_.layout_passes==0);
        bws_log_u64("[K3 THUMBNAIL] model_items=",w.liczba_elementow());bws_log_u64("[K3 THUMBNAIL] max_painted_items=",maxpaint);
        bws_log_u64("[K3 THUMBNAIL] wheel_dirty_pixels=",wheelpixels);bws_log_u64("[K3 THUMBNAIL] drag_dirty_pixels=",dragpixels);
        bws_log_u64("[K3 THUMBNAIL] scrollbar_paint_calls=",bar.pomiary_.scrollbar_paints);
        bws_log_u64("[K3 THUMBNAIL] thumbnail_paint_calls=",w.pomiary_.thumbnail_paints);
    }
    bws_sprawdz_msg(gui_liczba_aktywnych_alokacji_sterty()==live,"K3 stabilization no leaked allocations");
    bws_log(bws_test_blad==fail?"[K3 STABILIZACJA] PASS\n":"[K3 STABILIZACJA] FAIL\n");
}
