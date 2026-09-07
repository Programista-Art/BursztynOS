#pragma once
#include "../ekran/klient.h"
static void wykonaj_testy_ekranu(bool runtime){
    bws_log("\n=== USTAWIENIA EKRANU ===\n");
    const uint32_t przed=bws_test_blad;
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
    bws_sprawdz(ekran_zapytaj(ep,r,&start)&&start.liczba_trybow>0);
    bws_log_u64("[EKRAN TEST] backend=",start.mozliwosci.backend);
    bws_log_u64("[EKRAN TEST] trybow=",start.liczba_trybow);
    for(unsigned i=0;i<start.liczba_trybow;++i){
        bws_log("[EKRAN TRYB] ");
        bws_log_u64("w=",start.tryby[i].szerokosc);
        bws_log_u64(" h=",start.tryby[i].wysokosc);
        bws_log_u64(" bpp=",start.tryby[i].bpp);
        bws_log("\n");
    }
    r.operacja=EKRAN_TEST;bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.asercje>=15&&!out.bledy);
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
        }else{
            r={};r.operacja=EKRAN_ZASTOSUJ;r.szerokosc=start.aktywny.szerokosc==1920?1280:1920;r.wysokosc=1080;r.bpp=32;r.skala=100;
            bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.status==EKRAN_NIEOBSLUGIWANY&&start.liczba_trybow==1);
        }
        for(unsigned cycle=0;cycle<2;++cycle)for(uint32_t s=100;s<=200;s+=25){
            r={};r.operacja=EKRAN_ZASTOSUJ;r.szerokosc=start.aktywny.szerokosc;r.wysokosc=start.aktywny.wysokosc;r.bpp=32;r.skala=s;
            bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.status==EKRAN_OK&&out.skala==s&&ekran_ten_sam(out.aktywny,start.aktywny));
            GuiMonitorInfo mon{};bws_sprawdz(gui_pobierz_monitor(0,&mon)&&mon.scale_x1000==s*10&&mon.szer==int(start.aktywny.szerokosc)&&mon.wys==int(start.aktywny.wysokosc)&&mon.work_x==0&&mon.work_y==0&&mon.work_szer==mon.szer&&mon.work_wys==mon.wys-ui_skaluj(40,s));
        }
        r.skala=start.skala;bws_sprawdz(ekran_zapytaj(ep,r,&out)&&out.skala==start.skala);
        bws_sprawdz(out.bufory_bajty==start.bufory_bajty);
    }
    ipc_close(ep);
    bws_log(bws_test_blad==przed?"[EKRAN TEST] PASS\n":"[EKRAN TEST] FAIL\n");
}
