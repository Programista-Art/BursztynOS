#pragma once
#include "../ekran/klient.h"
#include "../../pulpit_geometria.h"
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
    }

    struct UkladPrzypadek{int w,h;uint32_t p;};
    const UkladPrzypadek uklady[]={
        {1280,720,100},{1280,720,150},{1280,720,200},
        {1280,800,100},{1280,800,150},{1280,800,200},
        {1920,1080,100},{1920,1080,150},{1920,1080,200},
        {2560,1440,150},{3840,2160,200}
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
    if(runtime){
        r={};r.operacja=EKRAN_ZASTOSUJ;r.szerokosc=UINT32_MAX;r.wysokosc=UINT32_MAX;r.bpp=32;r.skala=100;
        bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.status==EKRAN_NIEOBSLUGIWANY&&ekran_ten_sam(out.aktywny,start.aktywny));
        if(start.mozliwosci.mozna_zmienic_runtime){
            const uint32_t widths[]={1920,1280,2560};
            for(unsigned cycle=0;cycle<2;++cycle)for(auto w:widths){
                const TrybEkranu* m=nullptr;for(unsigned i=0;i<start.liczba_trybow;++i)if(start.tryby[i].szerokosc==w)m=&start.tryby[i];
                if(!m||ekran_ten_sam(*m,start.aktywny))continue;
                r={};r.operacja=EKRAN_ZASTOSUJ;r.szerokosc=m->szerokosc;r.wysokosc=m->wysokosc;r.bpp=32;r.skala=start.skala;
                bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.status==EKRAN_OCZEKUJE&&ekran_ten_sam(out.aktywny,*m));
                GuiMonitorInfo mon{};bws_sprawdz(gui_pobierz_monitor(0,&mon)&&mon.szer==int(m->szerokosc)&&mon.wys==int(m->wysokosc)&&mon.work_x==0&&mon.work_y==0&&mon.work_szer==int(m->szerokosc)&&mon.work_wys==int(m->wysokosc)-ui_skaluj(40,start.skala));
                bws_log_u64("[EKRAN TEST] guest modeset szerokosc=",out.aktywny.szerokosc);
                r={};r.operacja=EKRAN_PRZYWROC;r.transakcja=out.transakcja;
                bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.status==EKRAN_OK&&ekran_ten_sam(out.aktywny,start.aktywny));
                bws_sprawdz(out.bufory_bajty==start.bufory_bajty);
            }
            // Test jednoczesnej zmiany rozdzielczosci i skali oraz cofniecia obu
            {
                const TrybEkranu* m=nullptr;for(unsigned i=0;i<start.liczba_trybow;++i)if(start.tryby[i].szerokosc==1280)m=&start.tryby[i];
                if(m && !ekran_ten_sam(*m,start.aktywny)){
                    const uint32_t nowa_skala = start.skala == 100 ? 150 : 100;
                    r={};r.operacja=EKRAN_ZASTOSUJ;r.szerokosc=m->szerokosc;r.wysokosc=m->wysokosc;r.bpp=32;r.skala=nowa_skala;
                    bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.status==EKRAN_OCZEKUJE&&ekran_ten_sam(out.aktywny,*m)&&out.skala==nowa_skala);
                    r={};r.operacja=EKRAN_PRZYWROC;r.transakcja=out.transakcja;
                    bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.status==EKRAN_OK&&ekran_ten_sam(out.aktywny,start.aktywny)&&out.skala==start.skala);
                }
            }
        }else{
            r={};r.operacja=EKRAN_ZASTOSUJ;r.szerokosc=start.aktywny.szerokosc==1920?1280:1920;r.wysokosc=1080;r.bpp=32;r.skala=100;
            bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.status==EKRAN_NIEOBSLUGIWANY&&start.liczba_trybow==1);
        }
        for(unsigned cycle=0;cycle<2;++cycle)for(uint32_t s=100;s<=200;s+=25){
            r={};r.operacja=EKRAN_ZASTOSUJ;r.szerokosc=start.aktywny.szerokosc;r.wysokosc=start.aktywny.wysokosc;r.bpp=32;r.skala=s;
            bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.status==EKRAN_OK&&out.transakcja==0&&out.skala==s&&ekran_ten_sam(out.aktywny,start.aktywny));
            GuiMonitorInfo mon{};bws_sprawdz(gui_pobierz_monitor(0,&mon)&&mon.scale_x1000==s*10&&mon.szer==int(start.aktywny.szerokosc)&&mon.wys==int(start.aktywny.wysokosc)&&mon.work_x==0&&mon.work_y==0&&mon.work_szer==mon.szer&&mon.work_wys==mon.wys-ui_skaluj(40,s));
        }
        r.skala=start.skala;bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.skala==start.skala);
        bws_sprawdz(out.bufory_bajty==start.bufory_bajty);
    }
    ipc_close(ep);
    bws_log(bws_test_blad==przed?"[EKRAN TEST] PASS\n":"[EKRAN TEST] FAIL\n");
}
