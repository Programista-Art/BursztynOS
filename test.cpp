/*
 * Aplikacja Bursztyn OS (Zgodna z C++17, bursztyn_gui.h i ABI Linkera)
 * Wygenerowana przez: Bursztyn Builder RAD v3.1 Ultimate
 *
 * Dostepne wywolania BWS 1..56 opisane sa w bws_api.js / przegladarce API.
 */
#include "bursztyn_gui.h"
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Format .bur (Naglowek wymagany przez Loadera)
// -----------------------------------------------------------------------------

struct NaglowekBur {
    uint8_t  magia[4];
    uint64_t punkt_wejscia;
    uint64_t tekst_przesuniecie;
    uint64_t tekst_rozmiar;
    uint64_t tekst_wirtualny;
    uint64_t dane_przesuniecie;
    uint64_t dane_rozmiar;
    uint64_t dane_wirtualny;
} __attribute__((packed));

extern "C" __attribute__((noreturn)) void _start();

#ifndef BUR_TEKST_ROZMIAR
#define BUR_TEKST_ROZMIAR 32768ULL
#endif

static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 4096ULL;
static constexpr uint64_t BUR_TEKST_WIRTUALNY      = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE   = BUR_TEKST_PRZESUNIECIE + BUR_TEKST_ROZMIAR;
static constexpr uint64_t BUR_DANE_WIRTUALNY       = BUR_TEKST_WIRTUALNY + BUR_TEKST_ROZMIAR;
static constexpr uint64_t BUR_DANE_ROZMIAR         = 131072ULL;

extern "C" {
    __attribute__((section(".naglowek"), used))
    struct NaglowekBur naglowek = {
        {'B', 'U', 'R', '\0'},
        (uint64_t)&_start,
        BUR_TEKST_PRZESUNIECIE, BUR_TEKST_ROZMIAR, BUR_TEKST_WIRTUALNY,
        BUR_DANE_PRZESUNIECIE, BUR_DANE_ROZMIAR, BUR_DANE_WIRTUALNY
    };
}

// -----------------------------------------------------------------------------
// Zmienne preprocesora dla pętli GUI
// -----------------------------------------------------------------------------
static int WIN_X = 60;
static int WIN_Y = 50;
static int WIN_W = 600;
static int WIN_H = 450;
static bool zmaksymalizowane = false;
static bool aplikacja_zminimalizowana = false;
static bool dragging = false;
static int drag_off_x = 0;
static int drag_off_y = 0;
static int screen_w = 1024, screen_h = 768;
static int work_x = 0, work_y = 0, work_w = 1024, work_h = 768;
static int restore_x = 0, restore_y = 0, restore_w = 0, restore_h = 0;
static const bool BELKA_PRZYCISK_MIN = true;
static const bool BELKA_PRZYCISK_MAKS = true;

static bool combo_otwarty_TComboBox_3_3 = false;
static int  combo_wybor_TComboBox_3_3 = 0;
static uint32_t bws_test_ok = 0;
static uint32_t bws_test_blad = 0;
static uint32_t bws_test_kursor[GUI_CURSOR_CUSTOM_MAX_W *
                                GUI_CURSOR_CUSTOM_MAX_H] = {};

static void bws_log(const char* s) {
    if (s) bws_wywolaj(1, reinterpret_cast<uint64_t>(s), 0, 0, 0);
}

static void bws_sprawdz(bool warunek) {
    if (warunek) ++bws_test_ok;
    else {
        ++bws_test_blad;
        bws_log("[BWS TEST FAIL] Asercja nie powiodla sie!\n");
    }
}

static void wykonaj_testy_bws_61_83() {
    bws_log("\n=== ROZPOCZECIE TESTOW BWS 61..83 ===\n");

    // 1. Monitory, obszar roboczy, czas monotoniczny
    GuiMonitorInfo monitor{};
    bws_sprawdz(gui_liczba_monitorow() >= 1U);
    bws_sprawdz(gui_pobierz_monitor(0, &monitor));
    bws_sprawdz(!gui_pobierz_monitor(1, &monitor));
    bws_sprawdz(bws_wywolaj(BWS_GUI_MONITOR_INFO, 0, 1, 0, 0) == 0); // Invalid user pointer 0x1
    bws_sprawdz(!gui_ustaw_obszar_roboczy(0, 0, 1, 1)); // Brak PRAWO_SYSTEM_CONFIG
    const uint64_t czas0 = gui_czas_monotoniczny_ns();
    const uint64_t czas1 = gui_czas_monotoniczny_ns();
    bws_sprawdz(czas0 <= czas1);
    bws_log("[TEST 1/8] Monitory, work-area, HPET monotoniczny: OK\n");

    // 2. Geometria okna i walidacja granic
    bws_sprawdz(gui_ustaw_geometrie_okna(WIN_X, WIN_Y, WIN_W, WIN_H));
    bws_sprawdz(!gui_ustaw_geometrie_okna(WIN_X, WIN_Y, 0, WIN_H));
    bws_sprawdz(!gui_ustaw_geometrie_okna(WIN_X, WIN_Y, WIN_W, 0));
    bws_sprawdz(!gui_ustaw_geometrie_okna(WIN_X, WIN_Y, -1, WIN_H));
    bws_sprawdz(!gui_ustaw_geometrie_okna(WIN_X, WIN_Y, 4096, 4096)); // Over screen limit
    bws_log("[TEST 2/8] Geometria okna BWS 61 rollback & boundaries: OK\n");

    // 3. Stres-test SHM: 30 powtórzeń create -> map -> write/read -> unmap -> close
    for (uint32_t iter = 0; iter < 30; ++iter) {
        uint64_t h_shm = shm_create(4096, BWS_SHM_READ | BWS_SHM_WRITE);
        bws_sprawdz(h_shm != 0);
        uint8_t* m = static_cast<uint8_t*>(shm_map(h_shm, BWS_SHM_READ | BWS_SHM_WRITE));
        bws_sprawdz(m != nullptr);
        if (m) {
            bws_sprawdz(m[0] == 0); // Zeroed page check
            m[0] = static_cast<uint8_t>(0x55U + iter);
            m[4095] = static_cast<uint8_t>(0xAAU - iter);
            bws_sprawdz(m[0] == static_cast<uint8_t>(0x55U + iter));
            bws_sprawdz(m[4095] == static_cast<uint8_t>(0xAAU - iter));
            bws_sprawdz(shm_unmap(m));
            bws_sprawdz(!shm_unmap(m)); // Duplicate unmap rejection
        }
        bws_sprawdz(shm_close(h_shm));
        bws_sprawdz(!shm_close(h_shm)); // Stale handle rejection
    }

    // Walidacja błędnych parametrów SHM
    bws_sprawdz(shm_create(0, BWS_SHM_READ) == 0);
    bws_sprawdz(shm_create(UINT64_MAX, BWS_SHM_READ) == 0);
    bws_sprawdz(shm_create(4096, 0) == 0); // Must have at least READ
    bws_sprawdz(shm_create(4096, 0x80000000U) == 0); // Invalid flags

    // Test wielokrotnych równoczesnych mapowań SHM w jednym procesie (do 8 slotów)
    uint64_t shm_multi = shm_create(4096, BWS_SHM_READ | BWS_SHM_WRITE);
    bws_sprawdz(shm_multi != 0);
    void* mapy[8] = {};
    for (uint32_t i = 0; i < 8; ++i) {
        mapy[i] = shm_map(shm_multi, BWS_SHM_READ | BWS_SHM_WRITE);
        bws_sprawdz(mapy[i] != nullptr);
    }
    // 9. mapowanie powinno zostać odrzucone (limit slotów procesu)
    bws_sprawdz(shm_map(shm_multi, BWS_SHM_READ | BWS_SHM_WRITE) == nullptr);
    for (uint32_t i = 0; i < 8; ++i) {
        if (mapy[i]) bws_sprawdz(shm_unmap(mapy[i]));
    }
    bws_sprawdz(shm_close(shm_multi));
    bws_log("[TEST 3/8] Stres-test SHM 30x cykl, 8 mapowan + stale handles: OK\n");

    // 4. Stres-test IPC: 30 powtórzeń create -> send 16 wiadomości -> receive 16 -> close
    for (uint32_t iter = 0; iter < 30; ++iter) {
        uint64_t ep = ipc_create();
        bws_sprawdz(ep != 0);

        // Wypełnienie kolejki 16 wiadomościami
        for (uint32_t m = 0; m < 16; ++m) {
            char buf[16] = {};
            buf[0] = static_cast<char>('A' + (m % 26));
            buf[1] = static_cast<char>('0' + (iter % 10));
            buf[2] = '\0';
            BwsIpcWyslij w{};
            w.wersja = BWS_NOWOCZESNE_ABI_WERSJA;
            w.dane = reinterpret_cast<uint64_t>(buf);
            w.rozmiar = 3;
            bws_sprawdz(ipc_send(ep, &w));
        }

        // 17. wiadomość powinna zostać odrzucona (kolejka pełna)
        char extra[] = "overflow";
        BwsIpcWyslij w_extra{};
        w_extra.wersja = BWS_NOWOCZESNE_ABI_WERSJA;
        w_extra.dane = reinterpret_cast<uint64_t>(extra);
        w_extra.rozmiar = sizeof(extra);
        bws_sprawdz(!ipc_send(ep, &w_extra));

        // Odbiór wszystkich 16 wiadomości i weryfikacja kolejności FIFO
        for (uint32_t m = 0; m < 16; ++m) {
            BwsIpcOdbierz o{};
            bws_sprawdz(ipc_receive(ep, &o));
            bws_sprawdz(o.rozmiar == 3 && o.dane[0] == static_cast<uint8_t>('A' + (m % 26)));
        }

        // Kolejka jest pusta: kolejna próba receive zwraca false (brak danych)
        BwsIpcOdbierz o_pusty{};
        bws_sprawdz(!ipc_receive(ep, &o_pusty));

        bws_sprawdz(ipc_close(ep));
        bws_sprawdz(!ipc_close(ep)); // Stale handle rejection
    }

    // Test SHM queued in IPC while sender closes its handle before receiver receives
    uint64_t ep_queue = ipc_create();
    uint64_t shm_queue = shm_create(4096, BWS_SHM_READ | BWS_SHM_WRITE);
    bws_sprawdz(ep_queue != 0 && shm_queue != 0);
    uint8_t* ptr_queue = static_cast<uint8_t*>(shm_map(shm_queue, BWS_SHM_READ | BWS_SHM_WRITE));
    bws_sprawdz(ptr_queue != nullptr);
    if (ptr_queue) {
        ptr_queue[0] = 0xC0U;
        ptr_queue[1] = 0xFFU;
        ptr_queue[2] = 0xEEU;
        bws_sprawdz(shm_unmap(ptr_queue));
    }
    BwsIpcWyslij w_q{};
    w_q.wersja = BWS_NOWOCZESNE_ABI_WERSJA;
    w_q.flagi = BWS_IPC_ZAWIERA_SHM;
    w_q.shm_handle = shm_queue;
    bws_sprawdz(ipc_send(ep_queue, &w_q));

    // Nadawca zamyka swój uchwyt SHM PRZED odbiorem wiadomości
    bws_sprawdz(shm_close(shm_queue));

    // Odbiorca odbiera wiadomość: SHM musi nadal istnieć w kernelu (refcount wiadomości)
    BwsIpcOdbierz o_q{};
    bws_sprawdz(ipc_receive(ep_queue, &o_q));
    bws_sprawdz((o_q.flagi & BWS_IPC_ZAWIERA_SHM) != 0 && o_q.shm_handle == shm_queue);

    // Odbiorca mapuje przekazany SHM i weryfikuje dane
    uint8_t* ptr_rcv = static_cast<uint8_t*>(shm_map(o_q.shm_handle, BWS_SHM_READ | BWS_SHM_WRITE));
    bws_sprawdz(ptr_rcv != nullptr);
    if (ptr_rcv) {
        bws_sprawdz(ptr_rcv[0] == 0xC0U && ptr_rcv[1] == 0xFFU && ptr_rcv[2] == 0xEEU);
        bws_sprawdz(shm_unmap(ptr_rcv));
    }
    // Odbiorca zamyka SHM i endpoint - obiekt zostaje ostatecznie zwolniony
    bws_sprawdz(shm_close(o_q.shm_handle));
    bws_sprawdz(!shm_close(o_q.shm_handle));
    bws_sprawdz(ipc_close(ep_queue));
    bws_log("[TEST 4/8] Stres-test IPC 30x cykl + SHM transfer z zamknieciem nadawcy: OK\n");

    // 5. File watch (odrzucenie root / bez uprawnień)
    bws_sprawdz(file_watch("/", BWS_FILE_WATCH_WSZYSTKO) == 0);
    bws_sprawdz(file_watch(nullptr, BWS_FILE_WATCH_WSZYSTKO) == 0);
    bws_sprawdz(file_unwatch(0) == false);
    bws_log("[TEST 5/8] File watch boundary & handle checks: OK\n");

    // 6. Schowek MIME: testy formatów i limitów
    const char mime1[] = "text/plain";
    const char dane1[] = "Bursztyn OS Clipboard Test";
    bws_sprawdz(clipboard_set(mime1, dane1, sizeof(dane1)));
    uint64_t r_size = 0, r_got = 0;
    bws_sprawdz(clipboard_size(mime1, &r_size) && r_size == sizeof(dane1));
    char buf_clip[64] = {};
    bws_sprawdz(clipboard_read(mime1, buf_clip, sizeof(buf_clip), &r_got) && r_got == sizeof(dane1));
    bws_sprawdz(buf_clip[0] == 'B' && buf_clip[sizeof(dane1) - 2] == 't');

    // Błędne zapytania schowka
    bws_sprawdz(!clipboard_size("invalid-mime-no-slash", &r_size));
    bws_sprawdz(!clipboard_set("inv", dane1, sizeof(dane1)));
    bws_sprawdz(!clipboard_set(nullptr, dane1, sizeof(dane1)));
    bws_log("[TEST 6/8] MIME clipboard multi-format & PZB enforcement: OK\n");

    // 7. Kursory systemowe i custom
    for (uint32_t k = 0; k <= GUI_CURSOR_CROSSHAIR; ++k) {
        bws_sprawdz(gui_ustaw_kursor(k));
    }
    bws_sprawdz(!gui_ustaw_kursor(100)); // Invalid cursor type

    bws_test_kursor[0] = 0xFFFFFFFFU;
    GuiCustomCursorInfo kursor{};
    kursor.wersja = BWS_NOWOCZESNE_ABI_WERSJA;
    kursor.szer = GUI_CURSOR_CUSTOM_MAX_W;
    kursor.wys = GUI_CURSOR_CUSTOM_MAX_H;
    kursor.format = GUI_CURSOR_BGRA8888;
    kursor.piksele = reinterpret_cast<uint64_t>(bws_test_kursor);
    bws_sprawdz(gui_ustaw_kursor_custom(&kursor));
    kursor.format = GUI_CURSOR_RGBA8888;
    bws_sprawdz(gui_ustaw_kursor_custom(&kursor));

    // Błędy custom kursora
    kursor.szer = GUI_CURSOR_CUSTOM_MAX_W + 1U; // 65x64 -> over max
    bws_sprawdz(!gui_ustaw_kursor_custom(&kursor));
    kursor.szer = GUI_CURSOR_CUSTOM_MAX_W;
    kursor.hotspot_x = GUI_CURSOR_CUSTOM_MAX_W; // Hotspot poza obszarem
    bws_sprawdz(!gui_ustaw_kursor_custom(&kursor));
    bws_sprawdz(gui_ustaw_kursor(GUI_CURSOR_ARROW));
    bws_log("[TEST 7/8] System & custom cursors bounds and formats: OK\n");

    // 8. Socket (odrzucenie braku PRAWO_SIEC w manifeście)
    bws_sprawdz(socket_create() == 0);
    bws_sprawdz(socket_close(1) == false);
    bws_log("[TEST 8/8] Socket permission & ownership: OK\n");

    if (bws_test_blad == 0) {
        bws_log("=== WSZYSTKIE TESTY BWS 61..83 ZAKONCZONE SUKCESEM (PASS) ===\n\n");
    } else {
        bws_log("=== WYKRYTO BLEDY W TESTACH BWS 61..83 (FAIL) ===\n\n");
    }
}

static bool punkt_w_prostokacie(int px, int py, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void ogranicz_pozycje_okna(int* x, int* y) {
    if (!x || !y) return;
    int max_x = work_x + work_w - WIN_W;
    int max_y = work_y + work_h - WIN_H;
    if (max_x < work_x) max_x = work_x;
    if (max_y < work_y) max_y = work_y;
    if (*x < work_x) *x = work_x;
    if (*y < work_y) *y = work_y;
    if (*x > max_x) *x = max_x;
    if (*y > max_y) *y = max_y;
}

static void RysujInterfejs(bool wyczysc_warstwe) {
    if (wyczysc_warstwe) gui_odswiez_pulpit();
    if (aplikacja_zminimalizowana) return;

    // Tlo Okna i Belka Systemowa
    gui_rysuj_okno(WIN_X, WIN_Y, WIN_W, WIN_H, "Aplikacja Bursztyn (VCL Edition)");
    gui_rysuj_standardowa_belke(WIN_X, WIN_Y, WIN_W, "Aplikacja Bursztyn (VCL Edition)", zmaksymalizowane);

    // TButton_2 (TButton)
    gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 50, 120, 30, 0xFF2D2D2D);
    gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 50, 120, 1, 0x00404040);
    gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 50 + 30 - 1, 120, 1, 0x00101010);
    gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 50, 1, 30, 0x00404040);
    gui_rysuj_prostokat(WIN_X + 30 + 120 - 1, WIN_Y + 50, 1, 30, 0x00101010);
    rysuj_tekst_wysrodkowany(WIN_X + 30, WIN_Y + 50, 120, 30, 1, 0xFFFFFFFF, "Otwórz");
    // TComboBox_3 (TComboBox)
    gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 100, 150, 24, 0xFFFFFFFF);
    gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 100, 150, 1, 0x00404040);
    { const char* combo_items_TComboBox_3_3[] = { "Opcja 1", "Opcja 2", "Opcja 3" };
      const int combo_n_TComboBox_3_3 = 3;
      const char* combo_aktualny_TComboBox_3_3 = (combo_wybor_TComboBox_3_3 >= 0 && combo_wybor_TComboBox_3_3 < combo_n_TComboBox_3_3) ? combo_items_TComboBox_3_3[combo_wybor_TComboBox_3_3] : "";
      gui_wypisz_tekst_kolor_skala(WIN_X + 30 + 4, WIN_Y + 100 + 4, 0xFF000000, 1, combo_aktualny_TComboBox_3_3);
      gui_rysuj_prostokat(WIN_X + 162, WIN_Y + 100, 18, 24, 0x00E0E0E0);
      gui_wypisz_tekst_kolor_skala(WIN_X + 166, WIN_Y + 104, 0x00303030, 1, combo_otwarty_TComboBox_3_3 ? "^" : "v");
      if (combo_otwarty_TComboBox_3_3) {
          const int lista_h_TComboBox_3_3 = combo_n_TComboBox_3_3 * 16;
          gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 100 + 24, 150, lista_h_TComboBox_3_3, 0x00FFFFFF);
          gui_rysuj_prostokat(WIN_X + 30, WIN_Y + 100 + 24, 150, 1, 0x00404040);
          for (int i_TComboBox_3_3 = 0; i_TComboBox_3_3 < combo_n_TComboBox_3_3; ++i_TComboBox_3_3) {
              int iy_TComboBox_3_3 = WIN_Y + 100 + 24 + i_TComboBox_3_3 * 16;
              if (i_TComboBox_3_3 == combo_wybor_TComboBox_3_3) gui_rysuj_prostokat(WIN_X + 30, iy_TComboBox_3_3, 150, 16, 0x000078D7);
              gui_wypisz_tekst_kolor_skala(WIN_X + 30 + 4, iy_TComboBox_3_3 + 2, i_TComboBox_3_3 == combo_wybor_TComboBox_3_3 ? 0x00FFFFFF : 0x00000000, 1, combo_items_TComboBox_3_3[i_TComboBox_3_3]);
          }
      }
    }
    // TButton_5 (TButton)
    gui_rysuj_prostokat(WIN_X + 160, WIN_Y + 50, 100, 30, 0xFFFFFFFF);
    gui_rysuj_prostokat(WIN_X + 160, WIN_Y + 50, 100, 1, 0x00404040);
    gui_rysuj_prostokat(WIN_X + 160, WIN_Y + 50 + 30 - 1, 100, 1, 0x00101010);
    gui_rysuj_prostokat(WIN_X + 160, WIN_Y + 50, 1, 30, 0x00404040);
    gui_rysuj_prostokat(WIN_X + 160 + 100 - 1, WIN_Y + 50, 1, 30, 0x00101010);
    rysuj_tekst_wysrodkowany(WIN_X + 160, WIN_Y + 50, 100, 30, 1, 0xFF030000, "zamknij");
    // TButton_6 (TButton)
    gui_rysuj_prostokat(WIN_X + 270, WIN_Y + 50, 150, 30, 0xFF2D2D2D);
    gui_rysuj_prostokat(WIN_X + 270, WIN_Y + 50, 150, 1, 0x00404040);
    gui_rysuj_prostokat(WIN_X + 270, WIN_Y + 50 + 30 - 1, 150, 1, 0x00101010);
    gui_rysuj_prostokat(WIN_X + 270, WIN_Y + 50, 1, 30, 0x00404040);
    gui_rysuj_prostokat(WIN_X + 270 + 150 - 1, WIN_Y + 50, 1, 30, 0x00101010);
    rysuj_tekst_wysrodkowany(WIN_X + 270, WIN_Y + 50, 150, 30, 1, 0xFFFFFFFF, "Maksymalizuj okno");
    // TButton_7 (TButton)
    gui_rysuj_prostokat(WIN_X + 430, WIN_Y + 50, 100, 30, 0xFF2D2D2D);
    gui_rysuj_prostokat(WIN_X + 430, WIN_Y + 50, 100, 1, 0x00404040);
    gui_rysuj_prostokat(WIN_X + 430, WIN_Y + 50 + 30 - 1, 100, 1, 0x00101010);
    gui_rysuj_prostokat(WIN_X + 430, WIN_Y + 50, 1, 30, 0x00404040);
    gui_rysuj_prostokat(WIN_X + 430 + 100 - 1, WIN_Y + 50, 1, 30, 0x00101010);
    rysuj_tekst_wysrodkowany(WIN_X + 430, WIN_Y + 50, 100, 30, 1, 0xFFFFFFFF, "Minimalizuj");
    // TMemo_8 (TMemo)
    gui_rysuj_prostokat(WIN_X + 20, WIN_Y + 230, 435, 185, 0xFF515151);
    if (234 < 403) gui_wypisz_tekst_kolor_skala(WIN_X + 20 + 4, WIN_Y + 234, 0xFF000000, 1, "");

    gui_wypisz_tekst_kolor_skala(WIN_X + 24, WIN_Y + 205,
        bws_test_blad == 0 ? 0x0000AA00U : 0x00AA0000U, 1,
        bws_test_blad == 0 ? "BWS 61-83: PASS" : "BWS 61-83: FAIL");

    gui_odswiez();
}

extern "C" __attribute__((noreturn)) void _start() {
    gui_pobierz_rozdzielczosc(&screen_w, &screen_h);
    if (screen_w <= 0 || screen_h <= 0) gui_zakoncz_aplikacje();
    GuiMonitorInfo monitor{};
    if (gui_pobierz_monitor(0, &monitor)) {
        work_x = monitor.work_x; work_y = monitor.work_y;
        work_w = monitor.work_szer; work_h = monitor.work_wys;
    } else {
        work_w = screen_w; work_h = screen_h;
    }
    ogranicz_pozycje_okna(&WIN_X, &WIN_Y);
    if (bws_utworz_warstwe(WIN_X, WIN_Y, WIN_W, WIN_H, 10) < 0) gui_zakoncz_aplikacje();
    gui_ustaw_przejecie_myszy(true);
    wykonaj_testy_bws_61_83();
    bool wyjdz = false;
    RysujInterfejs(true);

    while (!wyjdz) {
        bws_zdarzenie zdarzenie{};
        if (!gui_czekaj_na_zdarzenie(&zdarzenie)) continue;
        if (zdarzenie.typ == BWS_ZDARZENIE_FOCUS && aplikacja_zminimalizowana) aplikacja_zminimalizowana = false;
        const int mx = zdarzenie.x; const int my = zdarzenie.y;
        const bool lewy = (zdarzenie.przyciski & 0x01U) != 0;
        const bool klik_lewy = zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN;
        const bool puszczenie_lewego = zdarzenie.typ == BWS_ZDARZENIE_MYSZ_UP;
        bool redraw = false; bool pelne_czyszczenie = false;

        if (aplikacja_zminimalizowana) continue;

        bool okno_spec_obsluzone = false;
        bool modal_zablokowane = false;
        // Klikniecie w pozycje rozwinietej listy ComboBox (obsluga PRZED reszta trafien,
        // bo lista rysuje sie NAD innymi kontrolkami).
        bool combo_klik_obsluzony = false;
        if (klik_lewy) {
            if (combo_otwarty_TComboBox_3_3) {
                const int n_TComboBox_3_3 = 3;
                if (punkt_w_prostokacie(mx, my, WIN_X + 30, WIN_Y + 100 + 24, 150, n_TComboBox_3_3 * 16)) {
                    int wybrany_TComboBox_3_3 = (my - (WIN_Y + 100 + 24)) / 16;
                    if (wybrany_TComboBox_3_3 >= 0 && wybrany_TComboBox_3_3 < n_TComboBox_3_3) combo_wybor_TComboBox_3_3 = wybrany_TComboBox_3_3;
                    combo_otwarty_TComboBox_3_3 = false; redraw = true; pelne_czyszczenie = true; combo_klik_obsluzony = true;
                } else if (punkt_w_prostokacie(mx, my, WIN_X + 30, WIN_Y + 100, 150, 24)) {
                    combo_otwarty_TComboBox_3_3 = false; redraw = true; pelne_czyszczenie = true; combo_klik_obsluzony = true;
                } else {
                    combo_otwarty_TComboBox_3_3 = false; redraw = true; pelne_czyszczenie = true;
                }
            }
        }

        if (!okno_spec_obsluzone && !modal_zablokowane && !combo_klik_obsluzony && klik_lewy && punkt_w_prostokacie(mx, my, WIN_X, WIN_Y, WIN_W, WIN_H)) {
            const gui_akcja_belki akcja = gui_hit_test_belki(mx, my, WIN_X, WIN_Y, WIN_W);
            if (akcja == GUI_BELKA_MINIMALIZUJ) {
                aplikacja_zminimalizowana = gui_minimalizuj_okno();
                dragging = false; gui_ustaw_capture_myszy(false);
            } else if (akcja == GUI_BELKA_ZAMKNIJ) {
                wyjdz = true; dragging = false;
            } else if (akcja == GUI_BELKA_MAKSYMALIZUJ) {
                if (!zmaksymalizowane) {
                    restore_x = WIN_X; restore_y = WIN_Y; restore_w = WIN_W; restore_h = WIN_H;
                    WIN_X = work_x; WIN_Y = work_y;
                    WIN_W = work_w; WIN_H = work_h;
                    zmaksymalizowane = true;
                } else {
                    WIN_X = restore_x; WIN_Y = restore_y; WIN_W = restore_w; WIN_H = restore_h;
                    zmaksymalizowane = false;
                }
                if (!gui_ustaw_geometrie_okna(
                        WIN_X, WIN_Y, WIN_W, WIN_H)) gui_zakoncz_aplikacje();
                redraw = true; pelne_czyszczenie = true;
            } else if (akcja == GUI_BELKA_DRAG && !zmaksymalizowane) {
                dragging = true; gui_ustaw_capture_myszy(true);
                drag_off_x = mx - WIN_X; drag_off_y = my - WIN_Y;
            } else {
                if (punkt_w_prostokacie(mx, my, WIN_X + 20, WIN_Y + 230, 435, 185)) {
                    gui_wypisz_tekst(20, 20, "Siemka");
                    redraw = true; pelne_czyszczenie = true;
                }
                else if (punkt_w_prostokacie(mx, my, WIN_X + 430, WIN_Y + 50, 100, 30)) {
                    aplikacja_zminimalizowana = gui_minimalizuj_okno();
                    redraw = true; pelne_czyszczenie = true;
                }
                else if (punkt_w_prostokacie(mx, my, WIN_X + 270, WIN_Y + 50, 150, 30)) {
                    // Maksymalizacja jest logiką aplikacji: pełny ekran = nowa warstwa BWS 33
                    if (!zmaksymalizowane) {
                        restore_x = WIN_X; restore_y = WIN_Y; restore_w = WIN_W; restore_h = WIN_H;
                        WIN_X = work_x; WIN_Y = work_y;
                        WIN_W = work_w; WIN_H = work_h;
                        zmaksymalizowane = true;
                    } else {
                        WIN_X = restore_x; WIN_Y = restore_y; WIN_W = restore_w; WIN_H = restore_h;
                        zmaksymalizowane = false;
                    }
                    if (!gui_ustaw_geometrie_okna(
                            WIN_X, WIN_Y, WIN_W, WIN_H)) gui_zakoncz_aplikacje();
                    redraw = true; pelne_czyszczenie = true;
                    redraw = true; pelne_czyszczenie = true;
                }
                else if (punkt_w_prostokacie(mx, my, WIN_X + 160, WIN_Y + 50, 100, 30)) {
                    gui_zakoncz_aplikacje(); // BWS 32 - nie wraca
                    redraw = true; pelne_czyszczenie = true;
                }
                else if (punkt_w_prostokacie(mx, my, WIN_X + 30, WIN_Y + 100, 150, 24)) {
                    combo_otwarty_TComboBox_3_3 = !combo_otwarty_TComboBox_3_3;
                    redraw = true; pelne_czyszczenie = true;
                }
                else if (punkt_w_prostokacie(mx, my, WIN_X + 30, WIN_Y + 50, 120, 30)) {
                    gui_wypisz_tekst_kolor_skala(20, 20, 0xFFFFBF00, 1, "Bursztyn");
                    redraw = true; pelne_czyszczenie = true;
                }
            }
        }

        if (dragging && lewy) {
            int nowy_x = mx - drag_off_x; int nowy_y = my - drag_off_y;
            ogranicz_pozycje_okna(&nowy_x, &nowy_y);
            if (nowy_x != WIN_X || nowy_y != WIN_Y) { WIN_X = nowy_x; WIN_Y = nowy_y; bws_przesun_warstwe(WIN_X, WIN_Y); gui_odswiez(); }
        }
        if (puszczenie_lewego && dragging) { dragging = false; gui_ustaw_capture_myszy(false); }
        if (redraw && !wyjdz) RysujInterfejs(pelne_czyszczenie);
    }
    gui_zakoncz_aplikacje();
}
