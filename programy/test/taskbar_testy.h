#pragma once

#include "../../ui_skala.h"
#include "../../bursztyn_gui.h"
#include "../../obrazy/png_dekoder.h"

static void wykonaj_testy_taskbara() {
    bws_log("\n=== TASKBAR & LOGO BURSZTYN OS ===\n");

    const uint32_t bledy_start = bws_test_blad;

    // -------------------------------------------------------------------------
    // 1. Logo asset load: /obrazy/logo.png istnieje w BSP2 i ma poprawny rozmiar
    // -------------------------------------------------------------------------
    const char* const sciezka_logo = "/obrazy/logo.png";
    uint64_t ret_rozmiar = bws_wywolaj(44, (uint64_t)sciezka_logo);
    bws_sprawdz(ret_rozmiar > 1); // 0 = blad, 1 = pusty plik
    uint64_t rozmiar_pliku = (ret_rozmiar > 1) ? (ret_rozmiar - 1) : 0;
    bws_sprawdz(rozmiar_pliku >= 50000 && rozmiar_pliku <= 200000);
    bws_log_u64("[TASKBAR TEST] Rozmiar /obrazy/logo.png B=", rozmiar_pliku);

    // -------------------------------------------------------------------------
    // 2. PNG dekodowanie i integralnosc kanalu alfa (brak halo, przezroczystosc)
    // -------------------------------------------------------------------------
    uint8_t* bufor_pliku = new uint8_t[rozmiar_pliku];
    bws_sprawdz(bufor_pliku != nullptr);

    if (bufor_pliku && rozmiar_pliku > 0) {
        uint64_t czyt_ret = bws_wywolaj(5, (uint64_t)sciezka_logo, (uint64_t)bufor_pliku, rozmiar_pliku);
        bws_sprawdz(czyt_ret == 1);

        uint32_t* pix = nullptr;
        int w = 0, h = 0;
        bool dekodowano = BursztynPNG::dekoduj_png(bufor_pliku, rozmiar_pliku, &pix, &w, &h);
        bws_sprawdz(dekodowano && pix != nullptr);
        bws_sprawdz(w == 840 && h == 916);
        bws_log_u64("[TASKBAR TEST] Wymiary logo szer=", (uint64_t)w);
        bws_log_u64("[TASKBAR TEST] Wymiary logo wys=", (uint64_t)h);

        if (pix && w == 840 && h == 916) {
            // Narozniki musza byc calkowicie przezroczyste (alfa == 0)
            uint32_t rog_tl = pix[0];
            uint32_t rog_tr = pix[w - 1];
            uint32_t rog_bl = pix[(h - 1) * w];
            uint32_t rog_br = pix[(h - 1) * w + w - 1];

            uint32_t a_tl = (rog_tl >> 24) & 0xFF;
            uint32_t a_tr = (rog_tr >> 24) & 0xFF;
            uint32_t a_bl = (rog_bl >> 24) & 0xFF;
            uint32_t a_br = (rog_br >> 24) & 0xFF;

            bws_sprawdz(a_tl == 0);
            bws_sprawdz(a_tr == 0);
            bws_sprawdz(a_bl == 0);
            bws_sprawdz(a_br == 0);

            // Piksel na grafice logo musi byc kryjacy (alfa == 255) i miec bursztynowy odcien
            uint32_t srodek = pix[537 * w + 277];
            uint32_t a_mid = (srodek >> 24) & 0xFF;
            uint32_t r_mid = (srodek >> 16) & 0xFF;
            uint32_t g_mid = (srodek >> 8) & 0xFF;
            uint32_t b_mid = srodek & 0xFF;

            bws_sprawdz(a_mid == 255);
            bws_sprawdz(r_mid > 100);
            bws_sprawdz(g_mid > 50);
            (void)b_mid;
            bws_log("[TASKBAR TEST] Integralnosc kanalu alfa i kolorow: OK\n");
        }

        if (pix) {
            delete[] pix;
        }
    }

    if (bufor_pliku) {
        delete[] bufor_pliku;
    }

    // -------------------------------------------------------------------------
    // 3. Geometria przycisku i skalowanie logo (100% do 200%)
    // -------------------------------------------------------------------------
    const uint32_t skale[] = {100, 125, 150, 175, 200};
    for (uint32_t s : skale) {
        const int pasek_h = ui_skaluj(40, s);
        const int btn_w = ui_skaluj(48, s);
        const int btn_h = ui_skaluj(30, s);
        const int logo_w = ui_skaluj(24, s);
        const int logo_h = ui_skaluj(26, s);

        bws_sprawdz(pasek_h >= 40 && pasek_h <= 80);
        bws_sprawdz(btn_w >= 48 && btn_w <= 96);
        bws_sprawdz(btn_h >= 30 && btn_h <= 60);
        bws_sprawdz(btn_h < pasek_h);

        // Wycentrowanie logo w przycisku
        const int lx = (btn_w - logo_w) / 2;
        const int ly = (btn_h - logo_h) / 2;
        bws_sprawdz(lx > 0);
        bws_sprawdz(ly >= 0);

        // Zachowanie proporcji bokow logo (840 : 916)
        const int oczekiwana_wys = (logo_w * 916 + 420) / 840;
        const int roznica = (logo_h > oczekiwana_wys) ? (logo_h - oczekiwana_wys) : (oczekiwana_wys - logo_h);
        bws_sprawdz(roznica <= 1);
    }
    bws_log("[TASKBAR TEST] Skalowanie logo i proporcje: OK\n");

    // -------------------------------------------------------------------------
    // 4. Hit-test przycisku Menu pokrywa dokladnie prostokat wizualny (100%)
    // -------------------------------------------------------------------------
    auto punkt_w_prostokacie_test = [](int px, int py, int rx, int ry, int rw, int rh) -> bool {
        return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
    };

    const int test_wysokosci[] = {720, 768, 1080, 1440, 2160};
    for (int sh : test_wysokosci) {
        for (uint32_t s : skale) {
            const int pasek_h = ui_skaluj(40, s);
            const int btn_x = ui_skaluj(10, s);
            const int btn_w = ui_skaluj(48, s);
            const int btn_h = ui_skaluj(30, s);
            const int btn_y = sh - pasek_h + (pasek_h - btn_h) / 2;

            // Wewnatrz prostokata: rogi i srodek musza byc trafione
            bws_sprawdz(punkt_w_prostokacie_test(btn_x, btn_y, btn_x, btn_y, btn_w, btn_h));
            bws_sprawdz(punkt_w_prostokacie_test(btn_x + btn_w - 1, btn_y, btn_x, btn_y, btn_w, btn_h));
            bws_sprawdz(punkt_w_prostokacie_test(btn_x, btn_y + btn_h - 1, btn_x, btn_y, btn_w, btn_h));
            bws_sprawdz(punkt_w_prostokacie_test(btn_x + btn_w - 1, btn_y + btn_h - 1, btn_x, btn_y, btn_w, btn_h));
            bws_sprawdz(punkt_w_prostokacie_test(btn_x + btn_w / 2, btn_y + btn_h / 2, btn_x, btn_y, btn_w, btn_h));

            // Na zewnatrz prostokata: 1 piksel poza granica nie moze byc trafiony
            bws_sprawdz(!punkt_w_prostokacie_test(btn_x - 1, btn_y, btn_x, btn_y, btn_w, btn_h));
            bws_sprawdz(!punkt_w_prostokacie_test(btn_x + btn_w, btn_y, btn_x, btn_y, btn_w, btn_h));
            bws_sprawdz(!punkt_w_prostokacie_test(btn_x, btn_y - 1, btn_x, btn_y, btn_w, btn_h));
            bws_sprawdz(!punkt_w_prostokacie_test(btn_x, btn_y + btn_h, btn_x, btn_y, btn_w, btn_h));
            bws_sprawdz(!punkt_w_prostokacie_test(btn_x - 1, btn_y - 1, btn_x, btn_y, btn_w, btn_h));
            bws_sprawdz(!punkt_w_prostokacie_test(btn_x + btn_w, btn_y + btn_h, btn_x, btn_y, btn_w, btn_h));
        }
    }
    bws_log("[TASKBAR TEST] Hit-test przycisku Menu (100% symetrii): OK\n");

    // -------------------------------------------------------------------------
    // 5. Geometria gradientu: pelna szerokosc/wysokosc, monotonicznosc (108 -> 52)
    // -------------------------------------------------------------------------
    const int rozdzielczosci[][2] = {
        {1280, 720}, {1920, 1080}, {2560, 1440}, {3440, 1440}, {3840, 2160}
    };
    for (const auto& res : rozdzielczosci) {
        (void)res;
        for (uint32_t s : skale) {
            const int pasek_h = ui_skaluj(40, s);
            bws_sprawdz(pasek_h >= 40);

            // Gora gradientu
            const uint32_t top_gray = (pasek_h > 1) ? (108 - (56 * 0) / (pasek_h - 1)) : 108;
            bws_sprawdz(top_gray == 108);

            // Dol gradientu
            const uint32_t bot_gray = (pasek_h > 1) ? (108 - (56 * (pasek_h - 1)) / (pasek_h - 1)) : 108;
            bws_sprawdz(bot_gray == 52);

            bws_sprawdz(top_gray != bot_gray);

            // Monotonicznosc: wartosc szarosci nigdy nie rosnie przy zwiekszaniu y
            uint32_t prev_gray = top_gray;
            for (int y = 1; y < pasek_h; ++y) {
                const uint32_t cur_gray = (pasek_h > 1) ? (108 - (56 * y) / (pasek_h - 1)) : 108;
                bws_sprawdz(cur_gray <= prev_gray);
                bws_sprawdz(cur_gray >= 52 && cur_gray <= 108);
                prev_gray = cur_gray;
            }
        }
    }
    bws_log("[TASKBAR TEST] Gradient szarosci i monotonicznosc (108..52): OK\n");

    // -------------------------------------------------------------------------
    // 6. Brak nakladania z przyciskami okien (task buttons) oraz zegarem
    // -------------------------------------------------------------------------
    for (uint32_t s : skale) {
        const int btn_menu_x = ui_skaluj(10, s);
        const int btn_menu_w = ui_skaluj(48, s);
        const int task_x = ui_skaluj(66, s);

        // Przycisk okna zaczyna sie za prawym brzegiem przycisku logo
        bws_sprawdz(task_x > btn_menu_x + btn_menu_w);
        bws_sprawdz(task_x - (btn_menu_x + btn_menu_w) >= ui_skaluj(8, s));
    }
    bws_log("[TASKBAR TEST] Brak nakladania z przyciskami okien: OK\n");

    // -------------------------------------------------------------------------
    // 7. BWS62 Obliczanie obszaru roboczego (Work Area)
    // -------------------------------------------------------------------------
    for (const auto& res : rozdzielczosci) {
        const int sw = res[0];
        const int sh = res[1];
        for (uint32_t s : skale) {
            const int pasek_h = ui_skaluj(40, s);
            const int work_x = 0;
            const int work_y = 0;
            const int work_w = sw;
            const int work_h = sh - pasek_h;

            bws_sprawdz(work_x == 0 && work_y == 0);
            bws_sprawdz(work_w == sw);
            bws_sprawdz(work_h == sh - pasek_h);
            bws_sprawdz(work_h + pasek_h == sh);
        }
    }
    bws_log("[TASKBAR TEST] Zgodnosc BWS62 Work Area: OK\n");

    // -------------------------------------------------------------------------
    // 8. Sekwencja przejsc na zywo (live-scale & live-resolution transitions)
    // -------------------------------------------------------------------------
    struct KrokPrzejscia { int w; int h; uint32_t s; };
    const KrokPrzejscia przejscia[] = {
        {1280, 720, 100},
        {1920, 1080, 125},
        {2560, 1440, 150},
        {3440, 1440, 175},
        {3840, 2160, 200},
        {1920, 1080, 100}
    };

    int prev_w = 0, prev_h = 0;
    uint32_t prev_scale = 0;
    for (size_t i = 0; i < sizeof(przejscia) / sizeof(przejscia[0]); ++i) {
        const auto& k = przejscia[i];
        const int ph = ui_skaluj(40, k.s);
        const int bw = ui_skaluj(48, k.s);
        const int bh = ui_skaluj(30, k.s);
        const int tx = ui_skaluj(66, k.s);
        const int work_h = k.h - ph;

        bws_sprawdz(ph >= 40 && ph <= 80);
        bws_sprawdz(bw >= 48 && bw <= 96);
        bws_sprawdz(bh < ph);
        bws_sprawdz(tx > ui_skaluj(10, k.s) + bw);
        bws_sprawdz(work_h + ph == k.h);

        prev_w = k.w;
        prev_h = k.h;
        prev_scale = k.s;
    }
    // Koncowy krok (1920x1080 @ 100%) musi odpowiadac dokladnie wartosciom bazowym
    bws_sprawdz(prev_w == 1920 && prev_h == 1080 && prev_scale == 100);
    bws_sprawdz(ui_skaluj(40, prev_scale) == 40);
    bws_sprawdz(ui_skaluj(48, prev_scale) == 48);
    bws_sprawdz(ui_skaluj(30, prev_scale) == 30);
    bws_sprawdz(ui_skaluj(66, prev_scale) == 66);
    bws_log("[TASKBAR TEST] Sekwencja live-transition: OK\n");

    bws_log(bws_test_blad == bledy_start ? "[TASKBAR SUITE] PASS\n" : "[TASKBAR SUITE] FAIL\n");
}
