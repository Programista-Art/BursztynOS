#pragma once
#include "../tapety/klient.h"
#include "../tapety/miniatury.h"

static void wykonaj_testy_tapety() {
    bws_log("\n=== TAPETY BTP ===\n");
    uint64_t ep = ipc_create();
    for (unsigned grupa = 1; grupa <= 8; ++grupa) {
        uint32_t przed = bws_test_blad;
        if (grupa == 1 || grupa == 2) {
            uint8_t raw[19] = {'B','T','P','1',1,0,0,0,1,0,0,0,32,0,0,0,0,255,0};
            uint8_t rle[20] = {'B','T','P','1',1,0,0,0,1,0,0,0,32,1,1,255,255,255,255,0};
            uint32_t dst[3] = {0x12345678, 0, 0x87654321};
            if (grupa == 1) {
                bws_sprawdz(btpwczytajirysuj(raw,18,dst+1,1,1,1,4) == BTPOK && dst[1] != 0);
                bws_sprawdz(btpwczytajirysuj(rle,19,dst+1,1,1,1,4) == BTPOK && (dst[1]&0xffffff) == 0xffffff);
                bws_sprawdz(dst[0] == 0x12345678 && dst[2] == 0x87654321);
            } else {
                auto zly = [&](uint8_t* p, size_t n) {
                    dst[1] = 0xabc123;
                    bws_sprawdz(btpwczytajirysuj(p,n,dst+1,1,1,1,4) != BTPOK);
                    bws_sprawdz(dst[0] == 0x12345678 && dst[1] == 0xabc123 && dst[2] == 0x87654321);
                };
                zly(raw,13); zly(raw,17); zly(raw,19); zly(rle,18); zly(rle,20);
                const unsigned offsets[] = {0,4,8,12,13};
                for (unsigned off : offsets) { uint8_t old=raw[off]; raw[off]=0; if(off==13)raw[off]=2; zly(raw,18); raw[off]=old; }
                rle[14]=0; zly(rle,19); rle[14]=2; zly(rle,19);
                for(unsigned i=4;i<12;++i) raw[i]=255;
                zly(raw,18);
            }
        } else if (grupa == 3 || grupa == 6) {
            TapetyZadanie req{}; req.operacja=TAPETY_DIAGNOSTYKA; req.grupa=grupa;
            TapetyOdpowiedz out{};
            bool ok=tapety_zapytaj(ep,req,&out);
            bws_sprawdz(ok && out.status==TAPETY_OK && out.bledy==0);
            bws_sprawdz(out.poprawne >= (grupa==3 ? 10U : 100U));
            if(grupa==6) {
                bws_sprawdz(out.heap_przed==out.heap_po);
                bws_log_u64("[TAPETY] heap_przed=",out.heap_przed);
                bws_log_u64("[TAPETY] heap_po=",out.heap_po);
                bws_log_u64("[TAPETY] aktywny_bufor_B=",out.bufor_bajty);
            }
        } else if (grupa == 4) {
            const uint32_t sizes[][4]={{1280,720,800,600},{600,800,1280,720},{17,31,45,21},{8192,1,1,8192}};
            for(const auto& s:sizes) for(unsigned m=0;m<3;++m) {
                GeometriaObrazu g{};
                bws_sprawdz(geometria_obrazu(s[0],s[1],s[2],s[3],static_cast<TrybSkalowaniaObrazu>(m),&g));
                bws_sprawdz(g.src_w && g.src_h && g.dst_w && g.dst_h && g.src_x+g.src_w<=s[0] && g.src_y+g.src_h<=s[1] && g.dst_x+g.dst_w<=s[2] && g.dst_y+g.dst_h<=s[3]);
            }
            GeometriaObrazu g{};
            bws_sprawdz(!geometria_obrazu(UINT32_MAX,1,1,1,TrybSkalowaniaObrazu::Dopasuj,&g));
            bws_sprawdz(geometria_obrazu(1280,720,800,600,TrybSkalowaniaObrazu::Dopasuj,&g) && g.dst_w==800 && g.dst_h==450 && g.dst_y==75);
        } else if (grupa == 5) {
            TapetyKonfiguracja c=tapety_domyslna(),out{}; char buf[TAPETY_CFG_MAX]{};
            size_t n=tapety_serializuj(c,buf,sizeof(buf));
            bws_sprawdz(n && tapety_parsuj(buf,n,&out) && tapety_rowne(c.sciezka,out.sciezka));
            bws_sprawdz(!tapety_parsuj(nullptr,0,&out) && tapety_rowne(out.sciezka,c.sciezka));
            const char* bad[]={"tapeta=/tapety/../a.btp\ntryb=dopasuj\n","tapeta=/dev/a.btp\ntryb=dopasuj\n","tapeta=/tapety/a.btp\ntryb=inne\n","smieci","tapeta=/tapety/a.btp\ntryb=dopasuj\nextra\n"};
            for(auto p:bad) { size_t k=0;while(p[k])++k;bws_sprawdz(!tapety_parsuj(p,k,&out)); }
            buf[2]=0;bws_sprawdz(!tapety_parsuj(buf,n,&out));
            for(size_t i=0;i<sizeof(buf);++i)buf[i]='x';
            bws_sprawdz(!tapety_parsuj(buf,sizeof(buf),&out));
            bws_sprawdz(!tapety_sciezka("/tapety/\xc0\xaf.btp"));
        } else if (grupa == 7) {
            auto c=tapety_domyslna();tapety_kopiuj(c.sciezka,TAPETY_SCIEZKI[2],sizeof(c.sciezka));
            for(unsigned mask=0;mask<8;++mask) {
                bool solid=false;
                auto z=tapety_fallback(c,[&](const TapetyKonfiguracja& p) {
                    return tapety_rowne(p.sciezka,c.sciezka) ? (mask&1)!=0 : (mask&2)!=0;
                },[&](){return (mask&4)!=0;},[&](){solid=true;});
                bws_sprawdz(z==((mask&3)?TAPETY_BTP:(mask&4)?TAPETY_BMP:TAPETY_SOLID));
                bws_sprawdz(solid==(mask==0));
            }
        } else {
            MiniaturyTapet mini; bws_sprawdz(mini.przygotuj());
            bws_sprawdz(MiniaturyTapet::bajty==576000 && !mini.obraz(10));
            WidokMiniatur w;w.rozmiesc(Prostokat(0,0,600,320));
            for(unsigned i=0;i<10;++i)w.dodaj_element(i,TAPETY_NAZWY[i],mini.obraz(i),160,90);
            bws_sprawdz(w.liczba_elementow()==10);w.ustaw_wybrany_indeks(0);
            ZdarzenieKlawiatury right{};right.kod_klawisza=BWS_KEY_SOURCE_E0|0x4d;
            w.obsluz_wcisniecie_klawisza(right);
            ZdarzenieKlawiatury enter{};enter.kod_klawisza=13;w.obsluz_wcisniecie_klawisza(enter);
            bws_sprawdz(w.wybrany_indeks()==1);
            ZdarzenieMyszki mv{};mv.x=12;mv.y=12;w.obsluz_ruch_myszy(mv);
            auto a=gui_liczba_alokacji_sterty();for(unsigned i=0;i<5000;++i)w.obsluz_ruch_myszy(mv);
            bws_sprawdz(a==gui_liczba_alokacji_sterty());
            bws_log_u64("[TAPETY] miniatury_B=",MiniaturyTapet::bajty);
        }
        bws_log_u64("[TAPETY BTP] grupa=",grupa);
        bws_log(bws_test_blad==przed?"[TAPETY BTP] PASS\n":"[TAPETY BTP] FAIL\n");
    }
    if(ep)ipc_close(ep);
}
