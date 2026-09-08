#pragma once
#include "../ekran/klient.h"
#include "../ekran/ekran_okno.h"
#include "../../pulpit_geometria.h"
// Tymczasowa diagnostyka regresji live-resize: taguje ktora asercja padla.
// Nie zmienia semantyki PASS/FAIL, tylko dokleja [EKRAN LIVE FAIL] <tag>.
static void bws_sprawdz_tag(bool warunek, const char* tag) {
    if (!warunek) {
        bws_log("[EKRAN LIVE FAIL] ");
        bws_log(tag);
        bws_log("\n");
    }
    bws_sprawdz(warunek);
}
static void wykonaj_testy_ekranu(bool runtime){
    bws_log("\n=== USTAWIENIA EKRANU ===\n");
    const uint32_t przed=bws_test_blad;
    auto ten_tekst=[](const char*a,const char*b){size_t i=0;if(!a||!b)return false;while(a[i]&&a[i]==b[i])++i;return a[i]==b[i];};
    struct FormatPrzypadek{uint32_t w,h;bool nazwa;const char* wynik;};
    const FormatPrzypadek formaty[]={
        {800,600,false,"800 x 600"},{1280,720,false,"1280 x 720"},
        {1280,800,false,"1280 x 800"},{1600,900,false,"1600 x 900"},
        {1920,1080,true,"1920 x 1080 (Full HD)"},
        {2560,1440,true,"2560 x 1440 (QHD)"},
        {3440,1440,true,"3440 x 1440 (UWQHD)"},
        {3440,1440,false,"3440 x 1440"},
        {3840,2160,true,"3840 x 2160 (4K UHD)"},
        {1280,720,true,"1280 x 720 (HD)"}
    };
    for(const auto& f:formaty){char b[48]{};bws_sprawdz(ekran_formatuj_tryb(b,sizeof(b),f.w,f.h,f.nazwa)&&ten_tekst(b,f.wynik));}
    char maly[8]={'Z','Z','Z','Z','Z','Z','Z','Z'};
    bws_sprawdz(!ekran_formatuj_tryb(maly,4,1920,1080,true)&&maly[0]=='\0'&&maly[4]=='Z'&&maly[7]=='Z');
    bws_sprawdz(!ekran_formatuj_tryb(nullptr,0,1920,1080,true));

    for(uint32_t p=100;p<=200;p+=25){
        const PulpitMetryki m=pulpit_metryki(p);
        const PulpitGeometriaKomorki a=pulpit_geometria_komorki(0,0,p);
        const PulpitGeometriaKomorki b=pulpit_geometria_komorki(m.komorka_szer,0,p);
        bws_sprawdz(a.ikona_x>=a.komorka_x&&a.ikona_y>=a.komorka_y&&
                    a.ikona_x+a.ikona_szer<=a.komorka_x+a.komorka_szer&&
                    a.ikona_y+a.ikona_wys<=a.etykieta_y);
        bws_sprawdz(a.etykieta_x>=a.komorka_x&&
                    a.etykieta_x+a.etykieta_szer<=a.komorka_x+a.komorka_szer&&
                    a.etykieta_y+a.etykieta_wys<=a.komorka_y+a.komorka_wys);
        bws_sprawdz(a.komorka_x+a.komorka_szer<=b.komorka_x&&
                    m.etykieta_wys>=2*m.wysokosc_linii+m.odstep_linii);
        const int cols=(1280-ui_skaluj(40,p))/m.komorka_szer;
        const int rows=(10+(cols>0?cols:1)-1)/(cols>0?cols:1);
        bws_sprawdz(ui_skaluj(20,p)+rows*m.komorka_wys<=720-ui_skaluj(40,p));

        // Ultrawide (3440x1440) desktop icon grid test
        const int cols_uw=(3440-ui_skaluj(40,p))/m.komorka_szer;
        const int rows_uw=(10+(cols_uw>0?cols_uw:1)-1)/(cols_uw>0?cols_uw:1);
        bws_sprawdz(cols_uw>0 && rows_uw>0);
        bws_sprawdz(ui_skaluj(20,p)+rows_uw*m.komorka_wys<=1440-ui_skaluj(40,p));

        // Ultrawide taskbar & maximized window geometry test
        const int tb_h = ui_skaluj(40, p);
        const int tb_w = 3440;
        const int z_x = 3440 - 150;
        const int z_w = 150;
        const int m_x = 10;
        const int m_w = 60;
        bws_sprawdz(tb_w == 3440);
        bws_sprawdz(z_x >= 0 && z_x + z_w == 3440);
        bws_sprawdz(m_x >= 0 && m_x + m_w < z_x);
        const GeometriaEkranu g_max = ekran_dopasuj({10, 10, 5000, 5000}, 3440, 1440 - tb_h, true);
        bws_sprawdz(g_max.x == 0 && g_max.y == 0 && g_max.szer == 3440 && g_max.wys == 1440 - tb_h);
    }

    struct UkladPrzypadek{int w,h;uint32_t p;};
    const UkladPrzypadek uklady[]={
        {1280,720,100},{1280,720,125},{1280,720,150},{1280,720,175},{1280,720,200},
        {1280,800,100},{1280,800,125},{1280,800,150},{1280,800,175},{1280,800,200},
        {1920,1080,100},{1920,1080,125},{1920,1080,150},{1920,1080,175},{1920,1080,200},
        {2560,1440,100},{2560,1440,125},{2560,1440,150},{2560,1440,175},{2560,1440,200},
        {3840,2160,100},{3840,2160,125},{3840,2160,150},{3840,2160,175},{3840,2160,200},
        {3440,1440,100},{3440,1440,125},{3440,1440,150},{3440,1440,175},{3440,1440,200}
    };
    char najdluzszy[48]{};bws_sprawdz(ekran_formatuj_tryb(najdluzszy,sizeof(najdluzszy),3840,2160,true));
    for(const auto& u:uklady){
        const int taskbar_h=ui_skaluj(40,u.p);
        const int work_h=u.h-taskbar_h;
        for(int state=0;state<2;++state){
            const bool pending=(state==1);
            const EkranGeometriaUkladu g=ekran_oblicz_uklad(u.w,u.h,u.p,pending);

            // 1. Okno miesci sie w calosci w obszarze roboczym BWS62
            bws_sprawdz(g.okno.x>=0 && g.okno.y>=0);
            bws_sprawdz(g.okno.szer>0 && g.okno.wys>g.belka_wys);
            bws_sprawdz(g.okno.x+g.okno.szer<=u.w);
            bws_sprawdz(g.okno.y+g.okno.wys<=work_h);

            // 2. Okno nie nachodzi na zarezerwowany pasek zadan
            bws_sprawdz(g.okno.y+g.okno.wys<=u.h-taskbar_h);

            // 3. Pasek przyciskow akcji (Zastosuj, Anuluj, Domyslne) jest w widocznym obszarze klienta
            bws_sprawdz(g.pasek_przyciskow_wys>0);
            bws_sprawdz(g.pasek_przyciskow_y>=0);
            bws_sprawdz(g.pasek_przyciskow_y+g.pasek_przyciskow_wys<=g.klient_wys);

            // 4. Etykiety i kontrolki mieszcza sie w szerokosci okna
            bws_sprawdz(oblicz_szerokosc_tekstu(najdluzszy,int(u.p))+ui_skaluj(24,u.p)<g.okno.szer);

            // 5. Zawartosc formularza / potwierdzenia jest osiagalna (bezposrednio lub przez przewijanie)
            bws_sprawdz(g.widok_wys+g.maks_przewiniecie>=g.zawartosc_wys);

            // 6. Dolny pasek akcji pozostaje zadokowany na samym dole obszaru klienta
            bws_sprawdz(g.pasek_przyciskow_y+g.pasek_przyciskow_wys==g.klient_wys);

            // 7. Standardowe rozdzielczosci nie wymagaja scrollbara w zadnym stanie
            bws_sprawdz(!g.scrollbar_widoczny && g.maks_przewiniecie==0);

            // 8. Kompaktowosc: okno ma stala, kompaktowa wysokosc w obu stanach
            if(g.okno.wys<work_h){
                bws_sprawdz(g.okno.wys==ui_skaluj(EKRAN_OKNO_NORMALNE_H,u.p));
            }
        }
        // Inwariant geometrii: geometria okna i paska przyciskow jest identyczna w stanie normalnym i oczekiwania
        const EkranGeometriaUkladu g_norm=ekran_oblicz_uklad(u.w,u.h,u.p,false);
        const EkranGeometriaUkladu g_pend=ekran_oblicz_uklad(u.w,u.h,u.p,true);
        bws_sprawdz(g_norm.okno.szer==g_pend.okno.szer && g_norm.okno.wys==g_pend.okno.wys);
        bws_sprawdz(g_norm.okno.x==g_pend.okno.x && g_norm.okno.y==g_pend.okno.y);
        bws_sprawdz(g_norm.pasek_przyciskow_y==g_pend.pasek_przyciskow_y);
        bws_sprawdz(g_norm.pasek_przyciskow_wys==g_pend.pasek_przyciskow_wys);
        bws_sprawdz(g_norm.widok_wys==g_pend.widok_wys);
        bws_sprawdz(g_norm.zawartosc_wys==g_pend.zawartosc_wys);
    }
    // Test dekodowania klawiszy Enter (zatwierdz) i Escape (przywroc)
    {
        auto czy_zatwierdz=[](uint32_t kod){
            bool usb=(kod & BWS_KEY_SOURCE_USB)!=0;
            uint32_t baza=usb?(kod & 0xFFU):(kod & 0x7FU);
            return (kod==13U || kod==10U || (usb && baza==40U) || (!usb && baza==0x1CU));
        };
        auto czy_przywroc=[](uint32_t kod){
            bool usb=(kod & BWS_KEY_SOURCE_USB)!=0;
            uint32_t baza=usb?(kod & 0xFFU):(kod & 0x7FU);
            return (kod==27U || (usb && baza==41U) || (!usb && baza==0x01U));
        };
        bws_sprawdz(czy_zatwierdz(13) && czy_zatwierdz(10) && czy_zatwierdz(0x1C) && czy_zatwierdz(40 | BWS_KEY_SOURCE_USB));
        bws_sprawdz(!czy_zatwierdz(27) && !czy_zatwierdz(0x01));
        bws_sprawdz(czy_przywroc(27) && czy_przywroc(0x01) && czy_przywroc(41 | BWS_KEY_SOURCE_USB));
        bws_sprawdz(!czy_przywroc(13) && !czy_przywroc(0x1C));
    }
    {
        const uint32_t zachowana_skala=ui_skala_procent;
        const bool zachowane_tlumienie=BursztynUI::Okno::g_tlum_operacje_systemowe;
        ui_skala_procent=100;
        BursztynUI::Okno::g_tlum_operacje_systemowe=true;
        {
            BursztynUI::Okno probne("Skalowanie logiczne",700,520);
            const BursztynUI::Prostokat work=BursztynUI::Okno::pobierz_obszar_roboczy();
            for(uint32_t p=100;p<=200;p+=25){
                probne.ustaw_skale_dpi(SkalaUI::procent(p));
                const int ew=ui_skaluj(700,p)<work.szerokosc?ui_skaluj(700,p):work.szerokosc;
                const int eh=ui_skaluj(520,p)<work.wysokosc?ui_skaluj(520,p):work.wysokosc;
                bws_sprawdz(probne.szerokosc()==ew&&probne.wysokosc()==eh&&
                            probne.obszar_klienta().wysokosc==eh-ui_skaluj(28,p));
            }
        }
        ui_skala_procent=zachowana_skala;
        BursztynUI::Okno::g_tlum_operacje_systemowe=zachowane_tlumienie;
    }
    size_t bytes=0,pages=0;
    TrybEkranu t{3840,2160,32,15360,0,0};
    bws_sprawdz(ekran_rozmiar(t,&bytes,&pages)&&bytes==33177600&&pages==8100);
    bws_sprawdz(bytes*2==66355200);
    const TrybEkranu bad[]={{0,1,32,4,0,0},{UINT32_MAX,UINT32_MAX,32,UINT32_MAX,0,0},{1920,1080,24,7680,0,0},{1920,1080,32,7679,0,0},{8192,8192,32,32768,0,0}};
    for(auto b:bad)bws_sprawdz(!ekran_rozmiar(b,&bytes));
    for(uint32_t p=100;p<=200;p+=25){
        SkalaUI s=SkalaUI::procent(p);
        bws_sprawdz(ui_skala_poprawna(p)&&28*s==int(28*p/100));
        bws_sprawdz(BursztynUI::CzcionkaUI::pobierz_wysokosc(s)==int(16*p/100));
        bws_sprawdz(ui_skaluj(40,p)==int(40*p/100));
        bws_sprawdz(BursztynUI::CzcionkaUI::zmierz_tekst("żółć",s)>0);
    }
    bws_sprawdz(!ui_skala_poprawna(0)&&!ui_skala_poprawna(124)&&!ui_skala_poprawna(UINT32_MAX));
    auto g=ekran_dopasuj({100,110,400,300},1280,680,false);
    bws_sprawdz(g.x==100&&g.y==110&&g.szer==400&&g.wys==300);
    g=ekran_dopasuj({1900,1000,600,500},800,560,false);
    bws_sprawdz(g.x==200&&g.y==60&&g.szer==600&&g.wys==500);
    g=ekran_dopasuj({-20,2000,1920,1080},800,560,false);
    bws_sprawdz(g.x==0&&g.y==0&&g.szer==800&&g.wys==560);
    g=ekran_dopasuj({100,110,400,300},2560,1390,true);
    bws_sprawdz(g.x==0&&g.y==0&&g.szer==2560&&g.wys==1390);
    EkranKonfiguracja cfg{1920,1080,32,125},parsed{};char text[EKRAN_CFG_MAX]{};
    size_t n=ekran_serializuj(cfg,text,sizeof(text));bws_sprawdz(n&&ekran_parsuj(text,n,&parsed)&&parsed.skala==125);
    const char* invalid[]={"version=1\nszerokosc=4294967296\nwysokosc=1080\nbpp=32\nskala=100\n", "version=1\nszerokosc=1920\nwysokosc=1080\nbpp=32\nskala=100\nskala=200\n", "version=1\nszerokosc=1920\nwysokosc=1080\nbpp=32\nskala=101\n"};
    for(auto s:invalid){size_t l=0;while(s[l])++l;bws_sprawdz(!ekran_parsuj(s,l,&parsed));}
    bws_sprawdz(!ekran_parsuj(text,EKRAN_CFG_MAX+1,&parsed));
    uint64_t ep=ipc_create();EkranZadanie r{};EkranOdpowiedz out{},start{};r.operacja=EKRAN_STAN;
    bws_sprawdz(ekran_zapytaj(ep,r,&start)&&start.liczba_trybow>0&&start.transakcja==0&&start.aktywny.szerokosc>0&&start.aktywny.wysokosc>0);
    bws_log_u64("[EKRAN TEST] backend=",start.mozliwosci.backend);
    bws_log_u64("[EKRAN TEST] trybow=",start.liczba_trybow);
    for(unsigned i=0;i<start.liczba_trybow;++i){
        bws_log("[EKRAN TRYB] ");
        bws_log_u64("w=",start.tryby[i].szerokosc);
        bws_log_u64(" h=",start.tryby[i].wysokosc);
        bws_log_u64(" bpp=",start.tryby[i].bpp);
        bws_log("\n");
    }
    r.operacja=EKRAN_TEST;r.szerokosc=runtime?1U:0U;
    bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.asercje>=5&&!out.bledy);
    // Linia bazowa musi opisywac stan PO samotescie jadra: EKRAN_TEST celowo
    // przestawia tryby/skale (m.in. skala wraca do 100%), wiec snapshot sprzed
    // samotestu (np. rozruchowa skala 125% po recznym teście człowieka) bylby
    // nieaktualny dla asercji cofniecia ponizej (RT-COMBO-REVERT-SCALE).
    {
        EkranZadanie odswiez{};
        odswiez.operacja = EKRAN_STAN;
        EkranOdpowiedz biezacy{};
        bws_sprawdz_tag(ekran_zapytaj(ep, odswiez, &biezacy), "RT-BASELINE-IPC");
        start = biezacy;
    }
    if(runtime){
        r={};r.operacja=EKRAN_ZASTOSUJ;r.szerokosc=UINT32_MAX;r.wysokosc=UINT32_MAX;r.bpp=32;r.skala=100;
        bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.status==EKRAN_NIEOBSLUGIWANY&&ekran_ten_sam(out.aktywny,start.aktywny));
        if(start.mozliwosci.mozna_zmienic_runtime){
            const uint32_t widths[]={1920,1280,2560,3440};
            for(unsigned cycle=0;cycle<2;++cycle)for(auto w:widths){
                const TrybEkranu* m=nullptr;for(unsigned i=0;i<start.liczba_trybow;++i)if(start.tryby[i].szerokosc==w)m=&start.tryby[i];
                if(!m||ekran_ten_sam(*m,start.aktywny))continue;
                r={};r.operacja=EKRAN_ZASTOSUJ;r.szerokosc=m->szerokosc;r.wysokosc=m->wysokosc;r.bpp=32;r.skala=start.skala;
                bws_sprawdz_tag(ekran_zapytaj(ep,r,&out)&&out.status==EKRAN_OCZEKUJE&&ekran_ten_sam(out.aktywny,*m), "RT-APPLY");
                GuiMonitorInfo mon{};
                for (int try_cnt = 0; try_cnt < 20; ++try_cnt) {
                    if (gui_pobierz_monitor(0, &mon) &&
                        mon.szer == int(m->szerokosc) &&
                        mon.wys == int(m->wysokosc) &&
                        mon.work_szer == int(m->szerokosc) &&
                        mon.work_wys == int(m->wysokosc) - ui_skaluj(40, start.skala)) {
                        break;
                    }
                    bws_zdarzenie ev{};
                    gui_czekaj_na_zdarzenie_z_timeoutem(&ev, 2);
                }
                bws_sprawdz_tag(mon.szer==int(m->szerokosc)&&mon.wys==int(m->wysokosc)&&mon.work_x==0&&mon.work_y==0&&mon.work_szer==int(m->szerokosc)&&mon.work_wys==int(m->wysokosc)-ui_skaluj(40,start.skala), "RT-MON");
                bws_log_u64("[EKRAN TEST] guest modeset szerokosc=",out.aktywny.szerokosc);
                r={};r.operacja=EKRAN_PRZYWROC;r.transakcja=out.transakcja;
                bws_sprawdz_tag(ekran_zapytaj(ep,r,&out)&&out.status==EKRAN_OK&&ekran_ten_sam(out.aktywny,start.aktywny), "RT-REVERT");
                bws_sprawdz_tag(out.bufory_bajty==start.bufory_bajty, "RT-BUF");
            }
            // Test jednoczesnej zmiany rozdzielczosci i skali oraz cofniecia obu
            {
                const TrybEkranu* m=nullptr;for(unsigned i=0;i<start.liczba_trybow;++i)if(start.tryby[i].szerokosc==1280)m=&start.tryby[i];
                if(m && !ekran_ten_sam(*m,start.aktywny)){
                    const uint32_t nowa_skala = start.skala == 100 ? 150 : 100;
                    r={};r.operacja=EKRAN_ZASTOSUJ;r.szerokosc=m->szerokosc;r.wysokosc=m->wysokosc;r.bpp=32;r.skala=nowa_skala;
                    bws_sprawdz_tag(ekran_zapytaj(ep,r,&out), "RT-COMBO-APPLY-IPC");
                    bws_sprawdz_tag(out.status==EKRAN_OCZEKUJE, "RT-COMBO-APPLY-STATUS");
                    bws_sprawdz_tag(ekran_ten_sam(out.aktywny,*m), "RT-COMBO-APPLY-MODE");
                    bws_sprawdz_tag(out.skala==nowa_skala, "RT-COMBO-APPLY-SCALE");
                    r={};r.operacja=EKRAN_PRZYWROC;r.transakcja=out.transakcja;
                    bws_sprawdz_tag(ekran_zapytaj(ep,r,&out), "RT-COMBO-REVERT-IPC");
                    bws_sprawdz_tag(out.status==EKRAN_OK, "RT-COMBO-REVERT-STATUS");
                    bws_sprawdz_tag(ekran_ten_sam(out.aktywny,start.aktywny), "RT-COMBO-REVERT-MODE");
                    bws_sprawdz_tag(out.skala==start.skala, "RT-COMBO-REVERT-SCALE");
                }
            }
            // Live-window transition regressions on an ALREADY CREATED OknoEkranu:
            // Mandatory transitions:
            // 2560x1440 @ 125% -> 1920x1080 @ 125%
            // 2560x1440 @ 125% -> 1280x720 @ 125%
            // 3840x2160 @ 125% -> 1920x1080 @ 125%
            // 1920x1080 @ 150% -> 1280x720 @ 150%
            // 1920x1080 @ 200% -> 1280x720 @ 200%
            // 1280x720 @ 125% -> 2560x1440 @ 125%
            // Followed by 20x QHD <-> HD cycles with the same window alive.

            auto sprawdz_live_okno = [&](BursztynUI::OknoEkranu& okno, int exp_w, int exp_h, uint32_t cel_skala, bool pending) {
                GuiMonitorInfo mon{};
                for (int try_cnt = 0; try_cnt < 20; ++try_cnt) {
                    if (gui_pobierz_monitor(0, &mon) &&
                        mon.szer == exp_w &&
                        mon.wys == exp_h &&
                        mon.work_szer == exp_w &&
                        mon.work_wys == (exp_h > ui_skaluj(40, cel_skala) ? exp_h - ui_skaluj(40, cel_skala) : exp_h)) {
                        break;
                    }
                    bws_zdarzenie ev{};
                    gui_czekaj_na_zdarzenie_z_timeoutem(&ev, 2);
                }
                bws_sprawdz_tag(mon.szer == exp_w && mon.wys == exp_h, "LIVE-MON-SCREEN");
                const int taskbar_h = ui_skaluj(40, cel_skala);
                const int work_h = exp_h > taskbar_h ? exp_h - taskbar_h : exp_h;
                bws_sprawdz_tag(mon.work_szer == exp_w && mon.work_wys == work_h, "LIVE-MON-WORK");

                // 1. Outer window inside work area
                bws_sprawdz_tag(okno.x() >= mon.work_x && okno.y() >= mon.work_y, "LIVE-WIN-POS");
                bws_sprawdz_tag(okno.x() + okno.szerokosc() <= mon.work_x + mon.work_szer, "LIVE-WIN-RIGHT");
                bws_sprawdz_tag(okno.y() + okno.wysokosc() <= mon.work_y + mon.work_wys, "LIVE-WIN-BOTTOM");

                // 2. BWS layer geometry matches window outer geometry
                int lx = 0, ly = 0, lw = 0, lh = 0;
                gui_pobierz_izolowana_geometrie(&lx, &ly, &lw, &lh);
                bws_sprawdz_tag(lx == okno.x() && ly == okno.y() && lw == okno.szerokosc() && lh == okno.wysokosc(), "LIVE-LAYER");

                // 3. Client geometry correct
                const int belka = ui_skaluj(EKRAN_BELKA_H, cel_skala);
                const int exp_klient_h = okno.wysokosc() > belka ? okno.wysokosc() - belka : 0;
                bws_sprawdz_tag(okno.obszar_klienta().szerokosc == okno.szerokosc(), "LIVE-CLIENT-W");
                bws_sprawdz_tag(okno.obszar_klienta().wysokosc == exp_klient_h, "LIVE-CLIENT-H");

                // 4. Presentation surface matches window outer/client geometry
                bws_sprawdz_tag(okno.pojemnosc_regionu() >= static_cast<size_t>(okno.szerokosc()) * okno.wysokosc(), "LIVE-SURF");

                // 5. Root panel matches client geometry
                bws_sprawdz_tag(okno.panel_glowny().obszar().szerokosc == okno.szerokosc(), "LIVE-ROOT-W");
                bws_sprawdz_tag(okno.panel_glowny().obszar().wysokosc == exp_klient_h, "LIVE-ROOT-H");

                // 6. Scroll viewport geometry correct
                int exp_bar_h = ui_skaluj(EKRAN_PASEK_PRZYCISKOW_H, cel_skala);
                if (exp_bar_h > exp_klient_h) exp_bar_h = exp_klient_h;
                const int exp_widok_h = exp_klient_h - exp_bar_h;
                bws_sprawdz_tag(okno.widok_glowny().obszar().wysokosc == exp_widok_h, "LIVE-SCROLL-H");
                bws_sprawdz_tag(okno.widok_glowny().prostokat_widoku().wysokosc == exp_widok_h, "LIVE-VIEW-H");

                // 7. Bottom action bar visible and docked using new client size
                bws_sprawdz_tag(okno.pasek_dolny().czy_widoczna(), "LIVE-BAR-VIS");
                bws_sprawdz_tag(okno.pasek_dolny().obszar().y == exp_widok_h, "LIVE-BAR-Y");
                bws_sprawdz_tag(okno.pasek_dolny().obszar().y + okno.pasek_dolny().obszar().wysokosc == exp_klient_h, "LIVE-BAR-BOTTOM");
                bws_sprawdz_tag(okno.pasek_dolny().obszar().szerokosc == okno.szerokosc(), "LIVE-BAR-W");

                // 8. Buttons reachable
                if (pending) {
                    bws_sprawdz_tag(okno.panel_potwierdzenia().czy_widoczna(), "LIVE-PEND-VIS");
                    bws_sprawdz_tag(!okno.panel_akcji().czy_widoczna(), "LIVE-PEND-HIDE");
                    bws_sprawdz_tag(okno.przycisk_zachowaj().czy_widoczna(), "LIVE-PEND-KEEP-VIS");
                    bws_sprawdz_tag(okno.przycisk_przywroc().czy_widoczna(), "LIVE-PEND-BACK-VIS");
                    bws_sprawdz_tag(okno.przycisk_zachowaj().obszar().prawy() <= okno.szerokosc(), "LIVE-PEND-KEEP-X");
                    bws_sprawdz_tag(okno.przycisk_przywroc().obszar().prawy() <= okno.szerokosc(), "LIVE-PEND-BACK-X");
                } else {
                    bws_sprawdz_tag(okno.panel_akcji().czy_widoczna(), "LIVE-ACT-VIS");
                    bws_sprawdz_tag(!okno.panel_potwierdzenia().czy_widoczna(), "LIVE-ACT-HIDE");
                    bws_sprawdz_tag(okno.przycisk_zastosuj().czy_widoczna(), "LIVE-ACT-APPLY-VIS");
                    bws_sprawdz_tag(okno.przycisk_anuluj().czy_widoczna(), "LIVE-ACT-CANCEL-VIS");
                    bws_sprawdz_tag(okno.przycisk_domyslne().czy_widoczna(), "LIVE-ACT-DEF-VIS");
                    bws_sprawdz_tag(okno.przycisk_domyslne().obszar().prawy() <= okno.szerokosc(), "LIVE-ACT-DEF-X");
                }

                // 9. No rectangle outside parent
                bws_sprawdz_tag(okno.widok_glowny().obszar().prawy() <= okno.panel_glowny().obszar().szerokosc, "LIVE-PARENT-SCROLL");
                bws_sprawdz_tag(okno.pasek_dolny().obszar().prawy() <= okno.panel_glowny().obszar().szerokosc, "LIVE-PARENT-BAR");

                // 10. No stale pre-transition dimensions
                int exp_ideal_w = ui_skaluj(EKRAN_OKNO_SZEROKOSC, cel_skala);
                if (exp_ideal_w > mon.work_szer) exp_ideal_w = mon.work_szer;
                int exp_ideal_h = ui_skaluj(EKRAN_OKNO_NORMALNE_H, cel_skala);
                if (exp_ideal_h > mon.work_wys) exp_ideal_h = mon.work_wys;
                bws_sprawdz_tag(okno.szerokosc() == exp_ideal_w, "LIVE-IDEAL-W");
                bws_sprawdz_tag(okno.wysokosc() == exp_ideal_h, "LIVE-IDEAL-H");
            };

            const bool prev_izolacja = gui_czy_izolacja_geometrii_wlaczona();
            gui_ustaw_izolacje_geometrii(true);
            {
                BursztynUI::Okno::g_tlum_operacje_systemowe = false;
                BursztynUI::OknoEkranu live_okno;
                live_okno.inicjalizuj(start, ep);
                live_okno.dopasuj_rozmiar_i_polozenie(true);

            struct PrzejsciePrzypadek {
                uint32_t src_w, src_h, src_skala;
                uint32_t dst_w, dst_h, dst_skala;
            };
            const PrzejsciePrzypadek przejscia[] = {
                {2560, 1440, 125, 1920, 1080, 125},
                {2560, 1440, 125, 1280, 720, 125},
                {3840, 2160, 125, 1920, 1080, 125},
                {1920, 1080, 150, 1280, 720, 150},
                {1920, 1080, 200, 1280, 720, 200},
                {1280, 720, 125, 2560, 1440, 125}
            };

            const uint64_t buf_start = start.bufory_bajty;
            for (const auto& p : przejscia) {
                bool src_ok = false, dst_ok = false;
                for (unsigned i = 0; i < start.liczba_trybow; ++i) {
                    if (start.tryby[i].szerokosc == p.src_w && start.tryby[i].wysokosc == p.src_h) src_ok = true;
                    if (start.tryby[i].szerokosc == p.dst_w && start.tryby[i].wysokosc == p.dst_h) dst_ok = true;
                }
                if (!src_ok || !dst_ok) continue;

                // 1. Establish source mode & scale
                r = {}; r.operacja = EKRAN_ZASTOSUJ;
                r.szerokosc = p.src_w; r.wysokosc = p.src_h; r.bpp = 32; r.skala = p.src_skala;
                bws_sprawdz(ekran_zapytaj(ep, r, &out));
                if (out.transakcja != 0) {
                    r = {}; r.operacja = EKRAN_ZACHOWAJ; r.transakcja = out.transakcja;
                    bws_sprawdz(ekran_zapytaj(ep, r, &out) && out.status == EKRAN_OK);
                    // Commit musi zsynchronizowac stan okna jak potwierdz_akcje() w aplikacji.
                    live_okno.ustaw_stan(out);
                    live_okno.synchronizuj(true);
                }
                bws_zdarzenie e_src{}; e_src.typ = BWS_ZDARZENIE_EKRAN; e_src.kod = p.src_skala;
                e_src.x = live_okno.x(); e_src.y = live_okno.y();
                e_src.dx = live_okno.szerokosc(); e_src.dy = live_okno.wysokosc();
                live_okno.obsluz_zdarzenie(e_src);
                sprawdz_live_okno(live_okno, p.src_w, p.src_h, p.src_skala, false);

                // 2. Perform live runtime transition to destination mode & scale
                r = {}; r.operacja = EKRAN_ZASTOSUJ;
                r.szerokosc = p.dst_w; r.wysokosc = p.dst_h; r.bpp = 32; r.skala = p.dst_skala;
                bws_sprawdz(ekran_zapytaj(ep, r, &out) && (out.status == EKRAN_OCZEKUJE || out.status == EKRAN_OK));

                bws_zdarzenie e_dst{}; e_dst.typ = BWS_ZDARZENIE_EKRAN; e_dst.kod = p.dst_skala;
                e_dst.x = live_okno.x(); e_dst.y = live_okno.y();
                e_dst.dx = live_okno.szerokosc(); e_dst.dy = live_okno.wysokosc();
                live_okno.obsluz_zdarzenie(e_dst);

                // Assert pending confirmation state after transition
                sprawdz_live_okno(live_okno, p.dst_w, p.dst_h, p.dst_skala, out.transakcja != 0);

                // 3. Confirm target mode
                if (out.transakcja != 0) {
                    r = {}; r.operacja = EKRAN_ZACHOWAJ; r.transakcja = out.transakcja;
                    bws_sprawdz(ekran_zapytaj(ep, r, &out) && out.status == EKRAN_OK);
                    // Commit musi zsynchronizowac stan okna jak potwierdz_akcje() w aplikacji.
                    live_okno.ustaw_stan(out);
                    live_okno.synchronizuj(true);
                }

                // Assert confirmed normal state
                sprawdz_live_okno(live_okno, p.dst_w, p.dst_h, p.dst_skala, false);
            }

            // 20 repeated QHD <-> HD cycles with the SAME window alive
            const TrybEkranu* t_qhd = nullptr;
            const TrybEkranu* t_hd = nullptr;
            for (unsigned i = 0; i < start.liczba_trybow; ++i) {
                if (start.tryby[i].szerokosc == 2560 && start.tryby[i].wysokosc == 1440) t_qhd = &start.tryby[i];
                if (start.tryby[i].szerokosc == 1280 && start.tryby[i].wysokosc == 720) t_hd = &start.tryby[i];
            }
            if (t_qhd && t_hd) {
                // Warmup
                for (int w = 0; w < 2; ++w) {
                    r = {}; r.operacja = EKRAN_ZASTOSUJ;
                    r.szerokosc = t_qhd->szerokosc; r.wysokosc = t_qhd->wysokosc; r.bpp = 32; r.skala = 125;
                    ekran_zapytaj(ep, r, &out);
                    if (out.transakcja) { r = {}; r.operacja = EKRAN_ZACHOWAJ; r.transakcja = out.transakcja; ekran_zapytaj(ep, r, &out); }
                    bws_zdarzenie ew{}; ew.typ = BWS_ZDARZENIE_EKRAN; ew.kod = 125;
                    ew.x = live_okno.x(); ew.y = live_okno.y(); ew.dx = live_okno.szerokosc(); ew.dy = live_okno.wysokosc();
                    live_okno.obsluz_zdarzenie(ew);

                    r = {}; r.operacja = EKRAN_ZASTOSUJ;
                    r.szerokosc = t_hd->szerokosc; r.wysokosc = t_hd->wysokosc; r.bpp = 32; r.skala = 125;
                    ekran_zapytaj(ep, r, &out);
                    if (out.transakcja) { r = {}; r.operacja = EKRAN_ZACHOWAJ; r.transakcja = out.transakcja; ekran_zapytaj(ep, r, &out); }
                    ew = {}; ew.typ = BWS_ZDARZENIE_EKRAN; ew.kod = 125;
                    ew.x = live_okno.x(); ew.y = live_okno.y(); ew.dx = live_okno.szerokosc(); ew.dy = live_okno.wysokosc();
                    live_okno.obsluz_zdarzenie(ew);
                }

                const uint64_t base_heap = gui_liczba_aktywnych_alokacji_sterty();
                const uint64_t base_buf = out.bufory_bajty;
                const size_t base_cap = live_okno.pojemnosc_regionu();

                for (int iter = 0; iter < 20; ++iter) {
                    // QHD @ 125%
                    r = {}; r.operacja = EKRAN_ZASTOSUJ;
                    r.szerokosc = t_qhd->szerokosc; r.wysokosc = t_qhd->wysokosc; r.bpp = 32; r.skala = 125;
                    bws_sprawdz(ekran_zapytaj(ep, r, &out) && (out.status == EKRAN_OCZEKUJE || out.status == EKRAN_OK));
                    bws_zdarzenie eq{}; eq.typ = BWS_ZDARZENIE_EKRAN; eq.kod = 125;
                    eq.x = live_okno.x(); eq.y = live_okno.y(); eq.dx = live_okno.szerokosc(); eq.dy = live_okno.wysokosc();
                    live_okno.obsluz_zdarzenie(eq);
                    sprawdz_live_okno(live_okno, 2560, 1440, 125, out.transakcja != 0);
                    if (out.transakcja) {
                        r = {}; r.operacja = EKRAN_ZACHOWAJ; r.transakcja = out.transakcja;
                        bws_sprawdz(ekran_zapytaj(ep, r, &out) && out.status == EKRAN_OK);
                        live_okno.ustaw_stan(out);
                        live_okno.synchronizuj(true);
                    }

                    // HD @ 125%
                    r = {}; r.operacja = EKRAN_ZASTOSUJ;
                    r.szerokosc = t_hd->szerokosc; r.wysokosc = t_hd->wysokosc; r.bpp = 32; r.skala = 125;
                    bws_sprawdz(ekran_zapytaj(ep, r, &out) && (out.status == EKRAN_OCZEKUJE || out.status == EKRAN_OK));
                    bws_zdarzenie eh{}; eh.typ = BWS_ZDARZENIE_EKRAN; eh.kod = 125;
                    eh.x = live_okno.x(); eh.y = live_okno.y(); eh.dx = live_okno.szerokosc(); eh.dy = live_okno.wysokosc();
                    live_okno.obsluz_zdarzenie(eh);
                    sprawdz_live_okno(live_okno, 1280, 720, 125, out.transakcja != 0);
                    if (out.transakcja) {
                        r = {}; r.operacja = EKRAN_ZACHOWAJ; r.transakcja = out.transakcja;
                        bws_sprawdz(ekran_zapytaj(ep, r, &out) && out.status == EKRAN_OK);
                        live_okno.ustaw_stan(out);
                        live_okno.synchronizuj(true);
                    }
                }

                bws_sprawdz_tag(gui_liczba_aktywnych_alokacji_sterty() == base_heap, "LIVE20-HEAP");
                bws_sprawdz_tag(out.bufory_bajty == base_buf, "LIVE20-BUF");
                bws_sprawdz_tag(live_okno.pojemnosc_regionu() == base_cap, "LIVE20-CAP");
            }

            // Return to initial mode & scale
            r = {}; r.operacja = EKRAN_ZASTOSUJ;
            r.szerokosc = start.aktywny.szerokosc; r.wysokosc = start.aktywny.wysokosc;
            r.bpp = 32; r.skala = start.skala;
            bws_sprawdz(ekran_zapytaj(ep, r, &out));
            if (out.transakcja != 0) {
                r = {}; r.operacja = EKRAN_ZACHOWAJ; r.transakcja = out.transakcja;
                bws_sprawdz(ekran_zapytaj(ep, r, &out) && out.status == EKRAN_OK);
            }
            bws_zdarzenie e_back{}; e_back.typ = BWS_ZDARZENIE_EKRAN; e_back.kod = start.skala;
            e_back.x = live_okno.x(); e_back.y = live_okno.y();
            e_back.dx = live_okno.szerokosc(); e_back.dy = live_okno.wysokosc();
            live_okno.obsluz_zdarzenie(e_back);
            live_okno.synchronizuj(true);
            bws_sprawdz(out.bufory_bajty == buf_start);
            }
            gui_ustaw_izolacje_geometrii(prev_izolacja);
        }else{
            r={};r.operacja=EKRAN_ZASTOSUJ;r.szerokosc=start.aktywny.szerokosc==1920?1280:1920;r.wysokosc=1080;r.bpp=32;r.skala=100;
            bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.status==EKRAN_NIEOBSLUGIWANY&&start.liczba_trybow==1);
        }
        for(unsigned cycle=0;cycle<2;++cycle)for(uint32_t s=100;s<=200;s+=25){
            r={};r.operacja=EKRAN_ZASTOSUJ;r.szerokosc=start.aktywny.szerokosc;r.wysokosc=start.aktywny.wysokosc;r.bpp=32;r.skala=s;
            bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.status==EKRAN_OK&&out.transakcja==0&&out.skala==s&&ekran_ten_sam(out.aktywny,start.aktywny));
            GuiMonitorInfo mon{};
            for (int try_cnt = 0; try_cnt < 20; ++try_cnt) {
                if (gui_pobierz_monitor(0, &mon) &&
                    mon.scale_x1000 == s * 10 &&
                    mon.szer == int(start.aktywny.szerokosc) &&
                    mon.wys == int(start.aktywny.wysokosc) &&
                    mon.work_szer == mon.szer &&
                    mon.work_wys == mon.wys - ui_skaluj(40, s)) {
                    break;
                }
                bws_zdarzenie ev{};
                gui_czekaj_na_zdarzenie_z_timeoutem(&ev, 2);
            }
            bws_sprawdz(mon.scale_x1000==s*10&&mon.szer==int(start.aktywny.szerokosc)&&mon.wys==int(start.aktywny.wysokosc)&&mon.work_x==0&&mon.work_y==0&&mon.work_szer==mon.szer&&mon.work_wys==mon.wys-ui_skaluj(40,s));
        }
        r.skala=start.skala;bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.skala==start.skala);
        bws_sprawdz(out.bufory_bajty==start.bufory_bajty);
    }
    ipc_close(ep);
    bws_log(bws_test_blad==przed?"[EKRAN TEST] PASS\n":"[EKRAN TEST] FAIL\n");
}
