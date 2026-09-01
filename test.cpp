/*
 * Aplikacja Bursztyn OS (Zgodna z C++17, bursztyn_gui.h i ABI Linkera)
 * Wygenerowana przez: Bursztyn Builder RAD v3.1 Ultimate
 *
 * Dostepne wywolania BWS 1..56 opisane sa w bws_api.js / przegladarce API.
 */
#include "bursztyn_gui.h"
#include "bursztyn_ui/bursztyn_ui.h"
#include <stddef.h>
#include <stdint.h>

using namespace BursztynUI;

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

static constexpr uint64_t BUR_TEKST_ROZMIAR     = 0x48000ULL; // 288 KiB
static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 0x1000ULL;
static constexpr uint64_t BUR_TEKST_WIRTUALNY     = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE   = 0x49000ULL;
static constexpr uint64_t BUR_DANE_WIRTUALNY       = 0x1049000ULL;
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

// Prezentacja korzysta z jednego ukladu lokalnego wzgledem powierzchni okna.
// Funkcje BWS rysuja we wspolrzednych ekranu, dlatego translacja o WIN_X/WIN_Y
// jest wykonywana dopiero w funkcjach rysujacych i hit-testach ponizej.
static constexpr int CHROME_H = 28;
static constexpr int CLIENT_PAD = 14;
static constexpr int GRID_GAP = 10;
static constexpr int BUTTON_H = 30;
static constexpr int COMBO_H = 24;
static constexpr int STATUS_H = 16;

struct TestUiRect {
    int x;
    int y;
    int w;
    int h;
};

struct TestUiLayout {
    TestUiRect client;
    TestUiRect toolbar[4];
    TestUiRect selector;
    TestUiRect status_bws;
    TestUiRect status_ui;
    TestUiRect output;
};

static bool wynik_bws_ok = false;
static bool wynik_bursztyn_ui_ok = false;

static bool combo_otwarty_TComboBox_3_3 = false;
static int  combo_wybor_TComboBox_3_3 = 0;
static const char* komunikat_output = "Interfejs testowy jest gotowy.";
static uint32_t bws_test_ok = 0;
static uint32_t bws_test_blad = 0;
static uint32_t bws_test_kursor[GUI_CURSOR_CUSTOM_MAX_W *
                                GUI_CURSOR_CUSTOM_MAX_H] = {};

static void bws_log(const char* s) {
    if (s) bws_wywolaj(1, reinterpret_cast<uint64_t>(s), 0, 0, 0);
}

static void bws_log_u64(const char* opis, uint64_t wartosc) {
    char bufor[96];
    size_t pos = 0;
    if (opis) {
        while (opis[pos] != '\0' && pos + 1 < sizeof(bufor)) {
            bufor[pos] = opis[pos];
            ++pos;
        }
    }
    char odwrotnie[24];
    size_t ile = 0;
    do {
        odwrotnie[ile++] = static_cast<char>('0' + (wartosc % 10));
        wartosc /= 10;
    } while (wartosc != 0 && ile < sizeof(odwrotnie));
    while (ile > 0 && pos + 1 < sizeof(bufor)) bufor[pos++] = odwrotnie[--ile];
    if (pos + 1 < sizeof(bufor)) bufor[pos++] = '\n';
    bufor[pos] = '\0';
    bws_log(bufor);
}

static void bws_sprawdz(bool warunek) {
    if (warunek) ++bws_test_ok;
    else {
        ++bws_test_blad;
        bws_log("[BWS TEST FAIL] Asercja nie powiodla sie!\n");
    }
}

static void bws_sprawdz_msg(bool warunek, const char* msg) {
    if (warunek) ++bws_test_ok;
    else {
        ++bws_test_blad;
        bws_log("[BWS TEST FAIL] ");
        if (msg) bws_log(msg);
        bws_log("\n");
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

static void wykonaj_testy_bursztyn_ui() {
    const uint32_t bledy_bui_przed = bws_test_blad;
    bws_log("\n=== ROZPOCZECIE TESTOW BURSZTYNUI ===\n");

    // -------------------------------------------------------------------------
    // 1. Bezpieczenstwo czcionki extronic16B_unicode.h & UTF-8
    // -------------------------------------------------------------------------
    bws_log("[BURSZTYNUI 1/4] Bezpieczenstwo extronic16B_unicode.h i UTF-8...\n");

    // Wysokosc kanoniczna
    bws_sprawdz(CzcionkaUI::pobierz_wysokosc(1) == 16);
    bws_sprawdz(CzcionkaUI::pobierz_wysokosc(2) == 32);

    // Polskie znaki (male i duze)
    const char* pl_tekst = "Zazolc gesla jazn ZAZOLC GESLA JAZN";
    int szer_pl = CzcionkaUI::zmierz_tekst(pl_tekst, 1);
    bws_sprawdz(szer_pl > 150);

    // Weryfikacja kodowania i szerokosci kazdego z 18 polskich znakow
    uint32_t polskie_litery[18] = {
        260, 261, // A, a
        262, 263, // C, c
        280, 281, // E, e
        321, 322, // L, l
        323, 324, // N, n
        211, 243, // O, o
        346, 347, // S, s
        377, 378, // Z, z
        379, 380  // Z, z
    };
    for (int i = 0; i < 18; ++i) {
        int w = CzcionkaUI::szerokosc_znaku(polskie_litery[i]);
        bws_sprawdz(w >= 4 && w <= 16);
    }

    // Bezpieczenstwo przed wyjsciem poza tablice dla nieobslugiwanych punktow kodowych
    uint32_t nieobslugiwane[5] = { 1170, 2000, 0x1F600, 0x10FFFF, 0xFFFFFFFF };
    int w_pytajnik = CzcionkaUI::szerokosc_znaku('?');
    for (int i = 0; i < 5; ++i) {
        int w = CzcionkaUI::szerokosc_znaku(nieobslugiwane[i]);
        bws_sprawdz(w == w_pytajnik); // Zastapienie bezpiecznym znakiem '?'
    }

    // Bezpieczenstwo uszkodzonego UTF-8 w dekoderze
    const char uszkodzony_utf8[] = { static_cast<char>(0xC0), static_cast<char>(0x20), '\0' };
    uint32_t cp_bad = 0;
    size_t zuzyte_bad = CzcionkaUI::dekoduj_utf8(uszkodzony_utf8, 0, &cp_bad);
    bws_sprawdz(zuzyte_bad == 1);
    bws_sprawdz(cp_bad == 0xC0);

    bws_log("[BURSZTYNUI 1/4] extronic16B_unicode.h & UTF-8: OK\n");

    // -------------------------------------------------------------------------
    // 2. Audyt przestrzeni wspolrzednych i hierarchii (Okno -> Kolumna -> Wiersz -> Przycisk)
    // -------------------------------------------------------------------------
    bws_log("[BURSZTYNUI 2/4] Audyt przestrzeni wspolrzednych i hit-testu...\n");

    Okno okno("Test BursztynUI", 540, 350);
    okno.ustaw_pozycje(130, 80);

    auto& glowna = okno.dodaj<Kolumna>();
    glowna.ustaw_wypelnienie(Marginesy(16, 12, 16, 12));
    glowna.ustaw_odstep(8);

    auto& lbl_tytul = glowna.dodaj<Etykieta>("BursztynUI Komponenty");
    (void)lbl_tytul;
    auto& lbl_status = glowna.dodaj<Etykieta>("Status poczatkowy");
    (void)lbl_status;
    auto& txt_input = glowna.dodaj<PoleTekstowe>("Wpisz tekst...");
    txt_input.ustaw_tekst("Bursztyn OS");
    txt_input.ustaw_preferowany_rozmiar(Rozmiar(508, 30));

    auto& panel_opcji = glowna.dodaj<Wiersz>();
    panel_opcji.ustaw_wypelnienie(Marginesy(10, 6, 10, 6));
    panel_opcji.ustaw_preferowany_rozmiar(Rozmiar(508, 28));
    auto& chk_haslo = panel_opcji.dodaj<PoleWyboru>("Haslo", false);
    auto& chk_trojstan = panel_opcji.dodaj<PoleWyboru>("Trojstan", false);
    chk_trojstan.ustaw_trojstanowe(true);

    auto& wiersz_przyciskow = glowna.dodaj<Wiersz>();
    wiersz_przyciskow.ustaw_wypelnienie(Marginesy(0, 0, 0, 0));
    wiersz_przyciskow.ustaw_odstep(12);
    wiersz_przyciskow.ustaw_preferowany_rozmiar(Rozmiar(508, 32));

    auto& btn1 = wiersz_przyciskow.dodaj<Przycisk>("Zatwierdz tekst");
    btn1.ustaw_preferowany_rozmiar(Rozmiar(230, 32));

    auto& btn2 = wiersz_przyciskow.dodaj<Przycisk>("Aktualizacja grupowa");
    btn2.ustaw_preferowany_rozmiar(Rozmiar(260, 32));

    auto& suwak = glowna.dodaj<Suwak>();
    suwak.ustaw_preferowany_rozmiar(Rozmiar(508, 32));
    suwak.ustaw_zakres(0, 100);
    suwak.ustaw_krok(1);
    suwak.ustaw_wartosc(50);

    okno.przelicz_uklad();

    // Weryfikacja obszarow
    Prostokat r_glowna = glowna.obszar_ekranu();
    bws_sprawdz_msg(r_glowna.x == 0 && r_glowna.y == 28 && r_glowna.szerokosc == 540, "r_glowna != (0, 28, 540)");

    Prostokat r_btn1 = btn1.obszar_ekranu();
    Prostokat r_btn2 = btn2.obszar_ekranu();
    bws_sprawdz_msg(r_btn1.szerokosc == 230 && r_btn1.wysokosc == 32, "r_btn1 size != (230, 32)");
    bws_sprawdz_msg(r_btn2.szerokosc == 260 && r_btn2.wysokosc == 32, "r_btn2 size != (260, 32)");
    bws_sprawdz_msg(r_btn1.x == 16, "r_btn1.x != 16");
    bws_sprawdz_msg(r_btn2.x == 16 + 230 + 12, "r_btn2.x != 258");

    // Test Hit-Testu we wszystkich punktach przycisku 1: srodek i wszystkie 4 krawedzie
    int c1_x = r_btn1.x + r_btn1.szerokosc / 2;
    int c1_y = r_btn1.y + r_btn1.wysokosc / 2;
    bws_sprawdz_msg(glowna.sprawdz_trafienie(Punkt(c1_x, c1_y)) == &btn1, "btn1 center hit-test failed");
    bws_sprawdz_msg(glowna.sprawdz_trafienie(Punkt(r_btn1.x, c1_y)) == &btn1, "btn1 left edge hit-test failed");
    bws_sprawdz_msg(glowna.sprawdz_trafienie(Punkt(r_btn1.prawy() - 1, c1_y)) == &btn1, "btn1 right edge hit-test failed");
    bws_sprawdz_msg(glowna.sprawdz_trafienie(Punkt(c1_x, r_btn1.y)) == &btn1, "btn1 top edge hit-test failed");
    bws_sprawdz_msg(glowna.sprawdz_trafienie(Punkt(c1_x, r_btn1.dolny() - 1)) == &btn1, "btn1 bottom edge hit-test failed");

    // Punkty 1px poza przyciskiem 1 (nie moga trafic w btn1)
    bws_sprawdz_msg(glowna.sprawdz_trafienie(Punkt(r_btn1.x - 1, c1_y)) != &btn1, "btn1 left outer hit should not be btn1");
    bws_sprawdz_msg(glowna.sprawdz_trafienie(Punkt(r_btn1.prawy(), c1_y)) != &btn1, "btn1 right outer hit should not be btn1");
    bws_sprawdz_msg(glowna.sprawdz_trafienie(Punkt(c1_x, r_btn1.y - 1)) != &btn1, "btn1 top outer hit should not be btn1");
    bws_sprawdz_msg(glowna.sprawdz_trafienie(Punkt(c1_x, r_btn1.dolny())) != &btn1, "btn1 bottom outer hit should not be btn1");

    // Test Hit-Testu we wszystkich punktach przycisku 2: srodek i wszystkie 4 krawedzie
    int c2_x = r_btn2.x + r_btn2.szerokosc / 2;
    int c2_y = r_btn2.y + r_btn2.wysokosc / 2;
    bws_sprawdz_msg(glowna.sprawdz_trafienie(Punkt(c2_x, c2_y)) == &btn2, "btn2 center hit-test failed");
    bws_sprawdz_msg(glowna.sprawdz_trafienie(Punkt(r_btn2.x, c2_y)) == &btn2, "btn2 left edge hit-test failed");
    bws_sprawdz_msg(glowna.sprawdz_trafienie(Punkt(r_btn2.prawy() - 1, c2_y)) == &btn2, "btn2 right edge hit-test failed");
    bws_sprawdz_msg(glowna.sprawdz_trafienie(Punkt(c2_x, r_btn2.y)) == &btn2, "btn2 top edge hit-test failed");
    bws_sprawdz_msg(glowna.sprawdz_trafienie(Punkt(c2_x, r_btn2.dolny() - 1)) == &btn2, "btn2 bottom edge hit-test failed");

    // Test 10 klikniec w Przycisk 1
    static int licznik_btn1 = 0;
    licznik_btn1 = 0;
    btn1.przy_kliknieciu([](Przycisk*, void*) {
        licznik_btn1++;
    });

    for (int k = 0; k < 10; ++k) {
        bws_zdarzenie down{};
        down.typ = BWS_ZDARZENIE_MYSZ_DOWN;
        down.x = okno.x() + c1_x;
        down.y = okno.y() + c1_y;
        down.przyciski = 1;
        okno.obsluz_zdarzenie(down);

        bws_zdarzenie up{};
        up.typ = BWS_ZDARZENIE_MYSZ_UP;
        up.x = okno.x() + c1_x;
        up.y = okno.y() + c1_y;
        up.przyciski = 0;
        okno.obsluz_zdarzenie(up);
    }
    bws_sprawdz_msg(licznik_btn1 == 10, "btn1 10 clicks callback failed");

    // Test 10 klikniec w Przycisk 2 (Batch)
    static int licznik_btn2 = 0;
    licznik_btn2 = 0;
    btn2.przy_kliknieciu([](Przycisk*, void*) {
        licznik_btn2++;
    });

    for (int k = 0; k < 10; ++k) {
        bws_zdarzenie down{};
        down.typ = BWS_ZDARZENIE_MYSZ_DOWN;
        down.x = okno.x() + c2_x;
        down.y = okno.y() + c2_y;
        down.przyciski = 1;
        okno.obsluz_zdarzenie(down);

        bws_zdarzenie up{};
        up.typ = BWS_ZDARZENIE_MYSZ_UP;
        up.x = okno.x() + c2_x;
        up.y = okno.y() + c2_y;
        up.przyciski = 0;
        okno.obsluz_zdarzenie(up);
    }
    bws_sprawdz_msg(licznik_btn2 == 10, "btn2 10 clicks callback failed");

    bws_log("[BURSZTYNUI 2/4] Audyt wspolrzednych i 10x klikniec: OK\n");

    // -------------------------------------------------------------------------
    // 3. Dirty Redraw i pomiar powierzchni odrysowywania
    // -------------------------------------------------------------------------
    bws_log("[BURSZTYNUI 3/4] Dirty Redraw i pomiary powierzchni...\n");

    KontekstRysowania ctx;
    // 1. Pelne odrysowanie
    okno.rysuj(ctx, true);
    int pelna_powierzchnia = okno.ostatnia_powierzchnia_odrysowania();
    bws_sprawdz_msg(pelna_powierzchnia == 540 * 350, "pelna_powierzchnia != 540*350");

    // 2. Odrysowanie po wpisaniu 1 znaku w PoleTekstowe
    txt_input.ustaw_tekst("Bursztyn OS: Zazolc gesla jazn!");
    okno.rysuj(ctx, false);
    int brudna_powierzchnia = okno.ostatnia_powierzchnia_odrysowania();
    bws_sprawdz_msg(brudna_powierzchnia == txt_input.obszar_ekranu().szerokosc * txt_input.obszar_ekranu().wysokosc, "brudna_powierzchnia mismatch");
    bws_sprawdz_msg(brudna_powierzchnia < pelna_powierzchnia / 5, "brudna_powierzchnia reduction < 80%");

    // 3. Test skracania tekstu (usuwanie starych pikseli)
    txt_input.ustaw_tekst("Bardzo dlugi tekst testowy do natychmiastowego skrocenia");
    okno.rysuj(ctx, false);
    txt_input.ustaw_tekst("Krotki");
    okno.rysuj(ctx, false);
    bws_sprawdz_msg(txt_input.tekst()[0] == 'K' && txt_input.tekst()[5] == 'i' && txt_input.tekst()[6] == '\0', "skrocenie tekstu mismatch");

    // 4. Regresja hover dirty-rect: kontrolka na ekranie przesunietego okna
    // musi zostac ponownie namalowana po kazdym wyczyszczeniu jej dirty rect.
    // Pozycja okna jest celowo rozna od (0, 0), aby wykryc pomieszanie
    // wspolrzednych klienta i ekranu.
    Okno okno_hover("Dirty hover", 260, 250);
    okno_hover.ustaw_pozycje(173, 91);
    auto& hover_kolumna = okno_hover.dodaj<Kolumna>();
    hover_kolumna.ustaw_wypelnienie(Marginesy(10));
    hover_kolumna.ustaw_odstep(10);
    auto& hover_checkbox = hover_kolumna.dodaj<PoleWyboru>("Hover 100x", true);
    auto& hover_pion = hover_kolumna.dodaj<Suwak>();
    hover_pion.ustaw_orientacje(Orientacja::Pionowa);
    hover_pion.ustaw_preferowany_rozmiar(Rozmiar(52, 150));
    hover_pion.ustaw_wartosc(60);

    struct LicznikiHover {
        int checkbox{0};
        int pion{0};
    } liczniki_hover;
    hover_checkbox.przy_wlasnym_rysowaniu([](Kontrolka*, KontekstRysowania&, void* dane) {
        ++static_cast<LicznikiHover*>(dane)->checkbox;
    }, &liczniki_hover);
    hover_pion.przy_wlasnym_rysowaniu([](Kontrolka*, KontekstRysowania&, void* dane) {
        ++static_cast<LicznikiHover*>(dane)->pion;
    }, &liczniki_hover);

    okno_hover.przelicz_uklad();
    KontekstRysowania ctx_hover;
    okno_hover.rysuj(ctx_hover, true);
    liczniki_hover.checkbox = 0;
    liczniki_hover.pion = 0;

    for (int i = 0; i < 100; ++i) {
        hover_checkbox.obsluz_wejscie_myszy();
        okno_hover.rysuj(ctx_hover, false);
        bws_sprawdz_msg(liczniki_hover.checkbox == i * 2 + 1,
                        "PoleWyboru zniknelo po hover-enter dirty redraw");
        hover_checkbox.obsluz_opuszczenie_myszy();
        okno_hover.rysuj(ctx_hover, false);
        bws_sprawdz_msg(liczniki_hover.checkbox == i * 2 + 2,
                        "PoleWyboru zniknelo po hover-leave dirty redraw");

        hover_pion.obsluz_wejscie_myszy();
        okno_hover.rysuj(ctx_hover, false);
        bws_sprawdz_msg(liczniki_hover.pion == i * 2 + 1,
                        "Pionowy Suwak zniknal po hover-enter dirty redraw");
        hover_pion.obsluz_opuszczenie_myszy();
        okno_hover.rysuj(ctx_hover, false);
        bws_sprawdz_msg(liczniki_hover.pion == i * 2 + 2,
                        "Pionowy Suwak zniknal po hover-leave dirty redraw");
    }
    bws_sprawdz_msg(liczniki_hover.checkbox == 200 && liczniki_hover.pion == 200,
                    "Hover 100x: niepelna liczba odrysowan kontrolek");

    bws_log("[BURSZTYNUI 3/4] Dirty Redraw + hover 100x: OK\n");

    // -------------------------------------------------------------------------
    // 4. Zaawansowane komponenty (PoleTekstowe, PoleWyboru, Fokus Tab, Maximize)
    // -------------------------------------------------------------------------
    bws_log("[BURSZTYNUI 4/4] Komponenty: PoleTekstowe, PoleWyboru, Fokus, Maximize...\n");

    // PoleTekstowe: edycja, zaznaczanie, Backspace, Delete, Undo/Redo
    txt_input.ustaw_tekst("Zazolc gesla jazn");
    bws_sprawdz(txt_input.pozycja_kursora() > 0);

    txt_input.zaznacz_wszystko();
    bws_sprawdz(txt_input.czy_ma_zaznaczenie());
    bws_sprawdz(txt_input.poczatek_zaznaczenia() == 0);
    bws_sprawdz(txt_input.koniec_zaznaczenia() == txt_input.pozycja_kursora());

    txt_input.zastap_zaznaczenie("Bursztyn");
    bws_sprawdz(txt_input.pozycja_kursora() == 8);

    // Undo / Redo
    bws_sprawdz(txt_input.czy_mozna_cofnac());
    txt_input.cofnij();
    bws_sprawdz(txt_input.pozycja_kursora() > 8); // Przywrocono "Zazolc gesla jazn"
    txt_input.ponow();
    bws_sprawdz(txt_input.pozycja_kursora() == 8); // Ponowiono "Bursztyn"

    // Tryb hasla
    txt_input.ustaw_tryb_hasla(true);
    bws_sprawdz(txt_input.czy_tryb_hasla());
    txt_input.ustaw_tryb_hasla(false);
    bws_sprawdz(!txt_input.czy_tryb_hasla());

    // PoleWyboru: zwykle i trojstanowe
    bws_sprawdz(!chk_haslo.czy_zaznaczone());
    chk_haslo.ustaw_zaznaczone(true);
    bws_sprawdz(chk_haslo.czy_zaznaczone());

    bws_sprawdz(chk_trojstan.stan_wyboru() == StanWyboru::Niezaznaczone);
    chk_trojstan.ustaw_stan_wyboru(StanWyboru::Zaznaczone);
    bws_sprawdz(chk_trojstan.stan_wyboru() == StanWyboru::Zaznaczone);
    chk_trojstan.ustaw_stan_wyboru(StanWyboru::Nieokreslone);
    bws_sprawdz(chk_trojstan.czy_nieokreslone());

    // Cykl fokusu (Tab / Shift+Tab)
    okno.ustaw_fokus(&txt_input);
    bws_sprawdz(okno.fokus() == &txt_input);
    okno.nastepny_fokus(false); // Tab do chk_haslo
    bws_sprawdz(okno.fokus() == &chk_haslo);
    okno.nastepny_fokus(false); // Tab do chk_trojstan
    bws_sprawdz(okno.fokus() == &chk_trojstan);
    okno.nastepny_fokus(false); // Tab do btn1
    bws_sprawdz(okno.fokus() == &btn1);
    okno.nastepny_fokus(true);  // Shift+Tab z powrotem do chk_trojstan
    bws_sprawdz(okno.fokus() == &chk_trojstan);

    // Maksymalizacja i przywrocenie rozmiaru okna
    int orig_w = okno.szerokosc();
    int orig_h = okno.wysokosc();
    int suwak_h_przed = suwak.obszar_ekranu().wysokosc;
    int suwak_w_przed = suwak.obszar_ekranu().szerokosc;
    GuiMonitorInfo ui_monitor{};
    bws_sprawdz_msg(gui_pobierz_monitor(0, &ui_monitor),
                    "BursztynUI: brak informacji BWS62 o obszarze roboczym");
    okno.przelacz_maksymalizacje();
    bws_sprawdz(okno.czy_zmaksymalizowane());
    bws_sprawdz_msg(okno.x() == ui_monitor.work_x && okno.y() == ui_monitor.work_y &&
                    okno.szerokosc() == ui_monitor.work_szer &&
                    okno.wysokosc() == ui_monitor.work_wys,
                    "BursztynUI: maksymalizacja nie uzyla work area BWS62");
    bws_sprawdz(suwak.obszar_ekranu().wysokosc == suwak_h_przed);
    bws_sprawdz(suwak.obszar_ekranu().szerokosc >= suwak_w_przed);
    okno.przelacz_maksymalizacje();
    bws_sprawdz(!okno.czy_zmaksymalizowane());
    bws_sprawdz(okno.szerokosc() == orig_w && okno.wysokosc() == orig_h);
    bws_sprawdz(suwak.obszar_ekranu().wysokosc == suwak_h_przed);

    Okno ograniczone("Work area clamp", ui_monitor.work_szer + 200,
                     ui_monitor.work_wys + 200);
    ograniczone.ustaw_geometrie(ui_monitor.work_x - 100,
                               ui_monitor.work_y + ui_monitor.work_wys - 10,
                               ui_monitor.work_szer + 200,
                               ui_monitor.work_wys + 200);
    ograniczone.dopasuj_do_obszaru_roboczego(true);
    bws_sprawdz_msg(ograniczone.x() == ui_monitor.work_x &&
                    ograniczone.y() == ui_monitor.work_y &&
                    ograniczone.szerokosc() == ui_monitor.work_szer &&
                    ograniczone.wysokosc() == ui_monitor.work_wys,
                    "BursztynUI: geometria poczatkowa wychodzi poza work area");

    ograniczone.ustaw_geometrie(ui_monitor.work_x + ui_monitor.work_szer - 20,
                               ui_monitor.work_y + ui_monitor.work_wys - 20,
                               300, 180);
    bws_sprawdz_msg(ograniczone.x() >= ui_monitor.work_x &&
                    ograniczone.y() >= ui_monitor.work_y &&
                    ograniczone.x() + ograniczone.szerokosc() <= ui_monitor.work_x + ui_monitor.work_szer &&
                    ograniczone.y() + ograniczone.wysokosc() <= ui_monitor.work_y + ui_monitor.work_wys,
                    "BursztynUI: ruch/resize przy dolnej krawedzi zaslania pasek zadan");

    bws_log("[BURSZTYNUI 4/4] Komponenty, Fokus i Geometria: OK\n");

    // -------------------------------------------------------------------------
    // 5. Suwak: model, wejście, capture, callbacki, dirty rect i alokacje
    // -------------------------------------------------------------------------
    bws_log("[BURSZTYNUI SUWAK] Model i interakcje...\n");

    struct PomiarSuwaka {
        int zmiany{0};
        int zatwierdzenia{0};
    } pomiar;
    suwak.przy_zmianie_wartosci([](Suwak*, int, void* dane) {
        static_cast<PomiarSuwaka*>(dane)->zmiany++;
    }, &pomiar);
    suwak.przy_zatwierdzeniu_wartosci([](Suwak*, int, void* dane) {
        static_cast<PomiarSuwaka*>(dane)->zatwierdzenia++;
    }, &pomiar);

    // Ten sam stan nie może wygenerować zdarzenia zmiany.
    suwak.ustaw_wartosc(50);
    suwak.ustaw_wartosc(50);
    bws_sprawdz_msg(pomiar.zmiany == 0, "Suwak: duplicate callback dla tej samej wartosci");

    // Klik toru przesuwa wartość i zatwierdza ją dopiero przy zwolnieniu.
    Prostokat r_suwak = suwak.obszar_ekranu();
    int y_suwak = r_suwak.y + r_suwak.wysokosc / 2;
    int x_cwierc = r_suwak.x + r_suwak.szerokosc / 4;
    bws_zdarzenie klik_down{};
    klik_down.typ = BWS_ZDARZENIE_MYSZ_DOWN;
    klik_down.x = okno.x() + x_cwierc;
    klik_down.y = okno.y() + y_suwak;
    klik_down.przyciski = 1;
    okno.obsluz_zdarzenie(klik_down);
    bws_sprawdz(suwak.wartosc() >= 20 && suwak.wartosc() <= 30);
    bws_sprawdz(okno.przechwycenie_myszy() == &suwak);
    bws_zdarzenie klik_up = klik_down;
    klik_up.typ = BWS_ZDARZENIE_MYSZ_UP;
    klik_up.przyciski = 0;
    okno.obsluz_zdarzenie(klik_up);
    bws_sprawdz(pomiar.zatwierdzenia == 1);
    bws_sprawdz(okno.przechwycenie_myszy() == nullptr);
    bws_sprawdz(!suwak.czy_wcisnieta());

    // Instrumentowana sekwencja: uchwyt -> poza kontrolkę -> MouseUp poza oknem.
    suwak.ustaw_wartosc(50);
    pomiar.zmiany = 0;
    pomiar.zatwierdzenia = 0;
    okno.rysuj(ctx, false);
    r_suwak = suwak.obszar_ekranu();
    int x_uchwyt = r_suwak.x + r_suwak.szerokosc / 2;
    y_suwak = r_suwak.y + r_suwak.wysokosc / 2;

    bws_zdarzenie drag_down{};
    drag_down.typ = BWS_ZDARZENIE_MYSZ_DOWN;
    drag_down.x = okno.x() + x_uchwyt;
    drag_down.y = okno.y() + y_suwak;
    drag_down.przyciski = 1;
    okno.obsluz_zdarzenie(drag_down);
    okno.rysuj(ctx, false); // oddziela zmianę stanu pressed od pomiaru ruchu

    uint64_t alokacje_przed = gui_liczba_alokacji_sterty();
    int liczba_odrysowan = 0;
    bws_zdarzenie drag_move{};
    drag_move.typ = BWS_ZDARZENIE_MYSZ_RUCH;
    drag_move.x = okno.x() + okno.szerokosc() + 120;
    drag_move.y = okno.y() + y_suwak;
    drag_move.przyciski = 1;
    okno.obsluz_zdarzenie(drag_move);
    okno.rysuj(ctx, false);
    ++liczba_odrysowan;
    int obszar_drag = okno.ostatnia_powierzchnia_odrysowania();

    bws_zdarzenie drag_up = drag_move;
    drag_up.typ = BWS_ZDARZENIE_MYSZ_UP;
    drag_up.przyciski = 0;
    okno.obsluz_zdarzenie(drag_up);
    uint64_t alokacje_po = gui_liczba_alokacji_sterty();

    bws_sprawdz_msg(suwak.wartosc() == 100, "Suwak: drag poza kontrolke nie osiagnal maksimum");
    bws_sprawdz_msg(pomiar.zmiany == 1, "Suwak: callback zmiany podczas drag != 1");
    bws_sprawdz_msg(pomiar.zatwierdzenia == 1, "Suwak: callback zatwierdzenia drag != 1");
    bws_sprawdz_msg(okno.przechwycenie_myszy() == nullptr, "Suwak: capture pozostal po MouseUp");
    bws_sprawdz_msg(!suwak.czy_wcisnieta(), "Suwak: pressed pozostal po MouseUp");
    bws_sprawdz_msg(obszar_drag > 0 && obszar_drag < pelna_powierzchnia,
                    "Suwak: drag nie byl odrysowaniem dirty-rect");
    bws_sprawdz_msg(alokacje_po == alokacje_przed,
                    "Suwak: alokacja sterty na goracej sciezce drag/render");
    bws_log_u64("[SUWAK POMIAR] callback_zmiana=", static_cast<uint64_t>(pomiar.zmiany));
    bws_log_u64("[SUWAK POMIAR] callback_zatwierdzenie=", static_cast<uint64_t>(pomiar.zatwierdzenia));
    bws_log_u64("[SUWAK POMIAR] redraw_count=", static_cast<uint64_t>(liczba_odrysowan));
    bws_log_u64("[SUWAK POMIAR] redraw_area_px=", static_cast<uint64_t>(obszar_drag));
    bws_log_u64("[SUWAK POMIAR] heap_allocations=", alokacje_po - alokacje_przed);

    // Powtarzane pełne interakcje nie zostawiają capture ani pressed.
    pomiar.zmiany = 0;
    pomiar.zatwierdzenia = 0;
    for (int i = 0; i < 6; ++i) {
        bws_zdarzenie down{};
        down.typ = BWS_ZDARZENIE_MYSZ_DOWN;
        down.x = okno.x() + (i % 2 == 0 ? r_suwak.x : r_suwak.prawy() - 1);
        down.y = okno.y() + y_suwak;
        down.przyciski = 1;
        okno.obsluz_zdarzenie(down);
        bws_zdarzenie up = down;
        up.typ = BWS_ZDARZENIE_MYSZ_UP;
        up.przyciski = 0;
        okno.obsluz_zdarzenie(up);
        bws_sprawdz(okno.przechwycenie_myszy() == nullptr);
        bws_sprawdz(!suwak.czy_wcisnieta());
    }
    bws_sprawdz(pomiar.zmiany == 6 && pomiar.zatwierdzenia == 6);

    // Klawiatura: key-up nie może powtórzyć zmiany wykonanej na key-down.
    okno.ustaw_fokus(&suwak);
    suwak.ustaw_wartosc(50);
    pomiar.zmiany = 0;
    pomiar.zatwierdzenia = 0;
    bws_zdarzenie key{};
    key.typ = BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;
    key.x = 1;
    key.kod = BWS_KEY_SOURCE_E0 | 0x4DU; // Right
    okno.obsluz_zdarzenie(key);
    key.x = 0;
    okno.obsluz_zdarzenie(key);
    bws_sprawdz(suwak.wartosc() == 51);
    bws_sprawdz(pomiar.zmiany == 1 && pomiar.zatwierdzenia == 1);

    key.x = 1;
    key.kod = BWS_KEY_SOURCE_E0 | 0x47U; // Home
    okno.obsluz_zdarzenie(key);
    bws_sprawdz(suwak.wartosc() == suwak.minimum());
    key.kod = BWS_KEY_SOURCE_E0 | 0x4FU; // End
    okno.obsluz_zdarzenie(key);
    bws_sprawdz(suwak.wartosc() == suwak.maksimum());
    key.kod = BWS_KEY_SOURCE_E0 | 0x51U; // PageDown
    okno.obsluz_zdarzenie(key);
    bws_sprawdz(suwak.wartosc() == 90);
    key.kod = BWS_KEY_SOURCE_USB | 75U; // USB PageUp
    okno.obsluz_zdarzenie(key);
    bws_sprawdz(suwak.wartosc() == 100);

    bws_zdarzenie rolka{};
    rolka.typ = BWS_ZDARZENIE_MYSZ_KOLO;
    // Fokus musi wystarczyć także wtedy, gdy wskaźnik jest poza drzewem okna.
    rolka.x = okno.x() + okno.szerokosc() + 40;
    rolka.y = okno.y() + okno.wysokosc() + 40;
    rolka.dy = -1;
    okno.obsluz_zdarzenie(rolka);
    bws_sprawdz(suwak.wartosc() == 99);

    // Zakres ujemny, snapping, bezpieczne końce int oraz min == max.
    Suwak model;
    model.ustaw_zakres(-50, 50);
    model.ustaw_krok(5);
    model.ustaw_wartosc(-47);
    bws_sprawdz(model.wartosc() == -45);
    model.ustaw_wartosc(-50);
    bws_sprawdz(model.wartosc() == -50);
    model.ustaw_wartosc(50);
    bws_sprawdz(model.wartosc() == 50);
    model.ustaw_zakres(-2147483647 - 1, 2147483647);
    model.ustaw_krok(1);
    model.ustaw_wartosc(0);
    bws_sprawdz(model.wartosc() == 0);
    model.rozmiesc(Prostokat(0, 0, 300, 32));
    ZdarzenieMyszki me{};
    me.typ = TypZdarzenia::WcisniecieMyszy;
    me.x = 299;
    me.y = 16;
    me.przycisk = 1;
    model.obsluz_wcisniecie_myszy(me);
    bws_sprawdz(model.wartosc() == 2147483647);
    me.typ = TypZdarzenia::ZwolnienieMyszy;
    model.obsluz_zwolnienie_myszy(me);
    model.ustaw_zakres(7, 7);
    model.ustaw_wartosc(-100);
    bws_sprawdz(model.minimum() == 7 && model.maksimum() == 7 && model.wartosc() == 7);

    // Pion: góra zwiększa, dół zmniejsza; wyłączony nie reaguje.
    Suwak pion;
    pion.ustaw_orientacje(Orientacja::Pionowa);
    pion.ustaw_zakres(-50, 50);
    pion.ustaw_krok(5);
    pion.ustaw_wartosc(0);
    ZdarzenieKlawiatury vk{};
    vk.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x48U;
    pion.obsluz_wcisniecie_klawisza(vk);
    bws_sprawdz(pion.wartosc() == 5);
    vk.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x50U;
    pion.obsluz_wcisniecie_klawisza(vk);
    bws_sprawdz(pion.wartosc() == 0);
    pion.ustaw_wlaczona(false);
    pion.obsluz_wcisniecie_klawisza(vk);
    bws_sprawdz(pion.wartosc() == 0);

    bws_log("[BURSZTYNUI SUWAK] Model, wejscie, capture i pomiary: OK\n");

    // -------------------------------------------------------------------------
    // 6. ModelPrzewijania i PasekPrzewijania: int64, geometria i capture
    // -------------------------------------------------------------------------
    bws_log("[BURSZTYNUI PASEK] Model int64, geometria i interakcje...\n");

    ModelPrzewijania model_przewijania;
    model_przewijania.ustaw_zakres(INT64_MIN, INT64_MAX);
    model_przewijania.ustaw_wartosc(0);
    model_przewijania.przewin_o(INT64_MAX);
    bws_sprawdz(model_przewijania.wartosc() == INT64_MAX);
    model_przewijania.przewin_o(INT64_MIN);
    bws_sprawdz(model_przewijania.wartosc() == -1);
    model_przewijania.przewin_o(INT64_MIN);
    bws_sprawdz(model_przewijania.wartosc() == INT64_MIN);
    model_przewijania.ustaw_zakres(7, 7);
    model_przewijania.ustaw_wartosc(INT64_MAX);
    bws_sprawdz(model_przewijania.minimum() == 7 &&
                model_przewijania.maksimum() == 7 &&
                model_przewijania.wartosc() == 7);

    PasekPrzewijania pasek_model;
    pasek_model.rozmiesc(Prostokat(0, 0, 300, 20));
    pasek_model.ustaw_zakres(-100, 100);
    pasek_model.ustaw_rozmiar_strony(25);
    pasek_model.ustaw_maly_krok(3);
    pasek_model.ustaw_duzy_krok(40);
    pasek_model.ustaw_wartosc(-10);
    bws_sprawdz(pasek_model.minimum() == -100 && pasek_model.maksimum() == 100);
    bws_sprawdz(pasek_model.wartosc() == -10);
    bws_sprawdz(pasek_model.rozmiar_strony() == 25);
    bws_sprawdz(pasek_model.maly_krok() == 3 && pasek_model.duzy_krok() == 40);
    Prostokat uchwyt_sredni = pasek_model.prostokat_uchwytu();
    bws_sprawdz(uchwyt_sredni.szerokosc >= 12 && uchwyt_sredni.szerokosc < 260);

    pasek_model.ustaw_rozmiar_strony(INT64_MAX);
    Prostokat uchwyt_duza_strona = pasek_model.prostokat_uchwytu();
    bws_sprawdz(uchwyt_duza_strona.szerokosc > uchwyt_sredni.szerokosc);
    pasek_model.ustaw_zakres(5, 5);
    bws_sprawdz(pasek_model.wartosc() == 5);
    Prostokat uchwyt_pelny = pasek_model.prostokat_uchwytu();
    bws_sprawdz(uchwyt_pelny.szerokosc >= uchwyt_duza_strona.szerokosc);

    pasek_model.ustaw_zakres(INT64_MIN, INT64_MAX);
    pasek_model.ustaw_rozmiar_strony(4096);
    pasek_model.ustaw_wartosc(0);
    Prostokat uchwyt_ogromny_zakres = pasek_model.prostokat_uchwytu();
    bws_sprawdz(uchwyt_ogromny_zakres.szerokosc >= 12);

    // Klawiatura pozioma oraz pionowa.
    pasek_model.ustaw_zakres(-100, 100);
    pasek_model.ustaw_wartosc(0);
    ZdarzenieKlawiatury pk{};
    pk.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x4DU; // Right
    pasek_model.obsluz_wcisniecie_klawisza(pk);
    bws_sprawdz(pasek_model.wartosc() == 3 && pk.obsluzone);
    pk = {};
    pk.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x51U; // PageDown
    pasek_model.obsluz_wcisniecie_klawisza(pk);
    bws_sprawdz(pasek_model.wartosc() == 43);
    pk = {};
    pk.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x47U; // Home
    pasek_model.obsluz_wcisniecie_klawisza(pk);
    bws_sprawdz(pasek_model.wartosc() == -100);
    pk = {};
    pk.kod_klawisza = BWS_KEY_SOURCE_USB | 77U; // End
    pasek_model.obsluz_wcisniecie_klawisza(pk);
    bws_sprawdz(pasek_model.wartosc() == 100);

    pasek_model.ustaw_orientacje(Orientacja::Pionowa);
    pasek_model.rozmiesc(Prostokat(0, 0, 20, 300));
    pasek_model.ustaw_wartosc(0);
    pk = {};
    pk.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x48U; // Up zmniejsza offset
    pasek_model.obsluz_wcisniecie_klawisza(pk);
    bws_sprawdz(pasek_model.wartosc() == -3);
    pasek_model.ustaw_wlaczona(false);
    pasek_model.obsluz_wcisniecie_klawisza(pk);
    bws_sprawdz(pasek_model.wartosc() == -3);

    // Prawdziwy routing Okno zapewnia capture i ruch poza kontrolka/oknem.
    Okno okno_paska("Test paska", 380, 110);
    okno_paska.ustaw_pozycje(220, 140);
    auto& panel_paska = okno_paska.dodaj<Kolumna>();
    panel_paska.ustaw_wypelnienie(Marginesy(12));
    auto& pasek = panel_paska.dodaj<PasekPrzewijania>();
    pasek.ustaw_preferowany_rozmiar(Rozmiar(356, 20));
    pasek.ustaw_zakres(-500, 500);
    pasek.ustaw_rozmiar_strony(100);
    pasek.ustaw_maly_krok(5);
    pasek.ustaw_duzy_krok(100);
    pasek.ustaw_wartosc(0);
    struct PomiarPaska { int zmiany{0}; int zatwierdzenia{0}; } pomiar_paska;
    pasek.przy_przewijaniu([](PasekPrzewijania*, int64_t, void* dane) {
        ++static_cast<PomiarPaska*>(dane)->zmiany;
    }, &pomiar_paska);
    pasek.przy_zatwierdzeniu([](PasekPrzewijania*, int64_t, void* dane) {
        ++static_cast<PomiarPaska*>(dane)->zatwierdzenia;
    }, &pomiar_paska);
    okno_paska.przelicz_uklad();
    KontekstRysowania ctx_paska;
    okno_paska.rysuj(ctx_paska, true);

    Prostokat rp = pasek.obszar_ekranu();
    Prostokat up_lokalny = pasek.prostokat_uchwytu();
    int px_uchwytu = rp.x + up_lokalny.x + up_lokalny.szerokosc / 2;
    int py_uchwytu = rp.y + up_lokalny.y + up_lokalny.wysokosc / 2;
    bws_zdarzenie pdown{};
    pdown.typ = BWS_ZDARZENIE_MYSZ_DOWN;
    pdown.x = okno_paska.x() + px_uchwytu;
    pdown.y = okno_paska.y() + py_uchwytu;
    pdown.przyciski = 1;
    okno_paska.obsluz_zdarzenie(pdown);
    bws_sprawdz(okno_paska.przechwycenie_myszy() == &pasek);
    okno_paska.rysuj(ctx_paska, false);

    uint64_t pasek_alokacje_przed = gui_liczba_alokacji_sterty();
    bws_zdarzenie pmove = pdown;
    pmove.typ = BWS_ZDARZENIE_MYSZ_RUCH;
    pmove.x = okno_paska.x() + okno_paska.szerokosc() + 200;
    okno_paska.obsluz_zdarzenie(pmove);
    okno_paska.rysuj(ctx_paska, false);
    int pasek_obszar_drag = okno_paska.ostatnia_powierzchnia_odrysowania();
    bws_zdarzenie pup = pmove;
    pup.typ = BWS_ZDARZENIE_MYSZ_UP;
    pup.przyciski = 0;
    okno_paska.obsluz_zdarzenie(pup);
    uint64_t pasek_alokacje_po = gui_liczba_alokacji_sterty();
    bws_sprawdz(pasek.wartosc() == 500);
    bws_sprawdz(okno_paska.przechwycenie_myszy() == nullptr);
    bws_sprawdz(pomiar_paska.zmiany == 1 && pomiar_paska.zatwierdzenia == 1);
    bws_sprawdz(pasek_obszar_drag > 0 &&
                pasek_obszar_drag < okno_paska.szerokosc() * okno_paska.wysokosc());
    bws_sprawdz(pasek_alokacje_po == pasek_alokacje_przed);

    // Klik toru wykonuje jeden duzy krok i zatwierdza na MouseUp.
    pasek.ustaw_wartosc(0);
    pomiar_paska.zmiany = 0;
    pomiar_paska.zatwierdzenia = 0;
    rp = pasek.obszar_ekranu();
    up_lokalny = pasek.prostokat_uchwytu();
    pdown = {};
    pdown.typ = BWS_ZDARZENIE_MYSZ_DOWN;
    pdown.x = okno_paska.x() + rp.x + up_lokalny.prawy() + 8;
    pdown.y = okno_paska.y() + rp.y + up_lokalny.y + up_lokalny.wysokosc / 2;
    pdown.przyciski = 1;
    okno_paska.obsluz_zdarzenie(pdown);
    bws_sprawdz(pasek.wartosc() == 100);
    pup = pdown;
    pup.typ = BWS_ZDARZENIE_MYSZ_UP;
    pup.przyciski = 0;
    okno_paska.obsluz_zdarzenie(pup);
    bws_sprawdz(pomiar_paska.zmiany == 1 && pomiar_paska.zatwierdzenia == 1);

    // Wyłączona kontrolka nie reaguje na mysz.
    pasek.ustaw_wlaczona(false);
    int64_t wartosc_wylaczonego_paska = pasek.wartosc();
    okno_paska.obsluz_zdarzenie(pdown);
    okno_paska.obsluz_zdarzenie(pup);
    bws_sprawdz(pasek.wartosc() == wartosc_wylaczonego_paska);

    bws_log_u64("[PASEK POMIAR] redraw_area_px=", static_cast<uint64_t>(pasek_obszar_drag));
    bws_log_u64("[PASEK POMIAR] heap_allocations_drag=", pasek_alokacje_po - pasek_alokacje_przed);
    bws_log("[BURSZTYNUI PASEK] Model, capture, dirty i alokacje: OK\n");

    // -------------------------------------------------------------------------
    // 7. PrzyciskOpcji/RamkaGrupy oraz ListaWyboru
    // -------------------------------------------------------------------------
    bws_log("[BURSZTYNUI WYBOR] Grupy opcji i lista wirtualizowana...\n");
    RamkaGrupy ramka("Tryb testu");
    PrzyciskOpcji& opcja_a = ramka.dodaj_opcje("Pierwsza", 10);
    PrzyciskOpcji& opcja_b = ramka.dodaj_opcje("Druga", 20);
    PrzyciskOpcji& opcja_c = ramka.dodaj_opcje("Trzecia", 30);
    bws_sprawdz(opcja_a.czy_zaznaczony());
    bws_sprawdz(!opcja_b.czy_zaznaczony() && !opcja_c.czy_zaznaczony());
    opcja_b.ustaw_zaznaczony(true);
    bws_sprawdz(!opcja_a.czy_zaznaczony() && opcja_b.czy_zaznaczony() &&
                !opcja_c.czy_zaznaczony());
    bws_sprawdz(opcja_b.wartosc() == 20);
    ZdarzenieKlawiatury ok_opcji{};
    ok_opcji.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x50U; // Down
    opcja_b.obsluz_wcisniecie_klawisza(ok_opcji);
    bws_sprawdz(opcja_c.czy_zaznaczony() && !opcja_b.czy_zaznaczony());
    opcja_a.ustaw_wlaczona(false);
    opcja_c.obsluz_wcisniecie_klawisza(ok_opcji); // pomija disabled i zawija do B
    bws_sprawdz(opcja_b.czy_zaznaczony());
    ramka.grupa_opcji().ustaw_dopuszczanie_braku(true);
    opcja_b.ustaw_zaznaczony(false);
    bws_sprawdz(ramka.grupa_opcji().zaznaczona_opcja() == nullptr);

    ListaWyboru* lista_ptr = new ListaWyboru();
    bws_sprawdz(lista_ptr != nullptr);
    ListaWyboru& lista = *lista_ptr;
    lista.rozmiesc(Prostokat(0, 0, 280, 180));
    bws_sprawdz(lista.liczba_elementow() == 0 && lista.wybrany_indeks() == -1);
    bws_sprawdz(lista.dodaj_element("Zażółć gęślą jaźń", 0xA5));
    bws_sprawdz(lista.liczba_elementow() == 1 && lista.znacznik_elementu(0) == 0xA5);
    bws_sprawdz(lista.ustaw_wybrany_indeks(0));
    bws_sprawdz(lista.wybrany_element() && lista.wybrany_element()[0] == 'Z');
    lista.usun_element(0);
    bws_sprawdz(lista.liczba_elementow() == 0 && lista.wybrany_indeks() == -1);

    char numer_elementu[32];
    for (int i = 0; i < 1200; ++i) {
        size_t p = 0;
        const char* pref = "Element ";
        while (pref[p]) { numer_elementu[p] = pref[p]; ++p; }
        int n = i;
        char odwrotnie[12];
        int cyfr = 0;
        do { odwrotnie[cyfr++] = static_cast<char>('0' + n % 10); n /= 10; } while (n);
        while (cyfr > 0 && p + 1 < sizeof(numer_elementu)) numer_elementu[p++] = odwrotnie[--cyfr];
        numer_elementu[p] = '\0';
        bws_sprawdz(lista.dodaj_element(numer_elementu, static_cast<uint64_t>(i)));
    }
    bws_sprawdz(lista.liczba_elementow() == 1200);
    lista.rozmiesc(Prostokat(0, 0, 280, 180));
    KontekstRysowania ctx_listy;
    uint64_t lista_alokacje_przed = gui_liczba_alokacji_sterty();
    lista.rysuj(ctx_listy, true);
    uint64_t lista_alokacje_po = gui_liczba_alokacji_sterty();
    bws_sprawdz(lista.liczba_ostatnio_narysowanych_wierszy() > 0);
    bws_sprawdz(lista.liczba_ostatnio_narysowanych_wierszy() < 20);
    bws_sprawdz(lista_alokacje_po == lista_alokacje_przed);

    // Rzeczywista sciezka BWS -> Okno -> hit-test -> ListaWyboru. Nie
    // wysylamy poprzedzajacego ruchu myszy: routing kola nie moze zalezec od
    // zapamietanego hover ani od istnienia zaznaczenia.
    Okno* okno_listy_ptr = new Okno("Test kola listy", 320, 230);
    bws_sprawdz(okno_listy_ptr != nullptr);
    Okno& okno_listy = *okno_listy_ptr;
    okno_listy.ustaw_pozycje(40, 40);
    okno_listy.ustaw_kontrolke_glowna(&lista);
    okno_listy.przelicz_uklad();
    lista.pasek_pionowy().ustaw_wartosc(0);
    Prostokat uchwyt_na_gorze = lista.pasek_pionowy().prostokat_uchwytu();
    Prostokat widok_listy = lista.prostokat_widoku();
    Prostokat lista_ekran = lista.obszar_ekranu();
    bws_zdarzenie kolo_listy{};
    kolo_listy.typ = BWS_ZDARZENIE_MYSZ_KOLO;
    kolo_listy.x = okno_listy.x() + lista_ekran.x + widok_listy.x + 8;
    kolo_listy.y = okno_listy.y() + lista_ekran.y + widok_listy.y + 8;
    kolo_listy.dy = -1; // USB HID: ujemne = kolo w dol
    lista.wyczysc_brudny_obszar();
    uint64_t lista_kolo_alok_przed = gui_liczba_alokacji_sterty();
    okno_listy.obsluz_zdarzenie(kolo_listy);
    uint64_t lista_kolo_alok_po = gui_liczba_alokacji_sterty();
    bws_sprawdz(lista.pierwszy_widoczny_indeks() == 3);
    bws_sprawdz(lista.wybrany_indeks() == -1 && lista.indeks_fokusu() == -1);
    bws_sprawdz(lista_kolo_alok_po == lista_kolo_alok_przed);
    Prostokat brudny_kola = lista.pobierz_brudny_obszar();
    uint64_t pole_brudne_kola = static_cast<uint64_t>(brudny_kola.szerokosc) *
                                static_cast<uint64_t>(brudny_kola.wysokosc);
    bws_sprawdz(pole_brudne_kola > 0 &&
                pole_brudne_kola < static_cast<uint64_t>(lista.szerokosc()) * lista.wysokosc());

    for (int i = 0; i < 500; ++i) okno_listy.obsluz_zdarzenie(kolo_listy);
    bws_sprawdz(lista.pierwszy_widoczny_indeks() == lista.pasek_pionowy().maksimum());
    int64_t lista_dol = lista.pasek_pionowy().wartosc();
    okno_listy.obsluz_zdarzenie(kolo_listy);
    bws_sprawdz(lista.pasek_pionowy().wartosc() == lista_dol);
    Prostokat uchwyt_na_dole = lista.pasek_pionowy().prostokat_uchwytu();
    bws_sprawdz(uchwyt_na_dole.y > uchwyt_na_gorze.y);
    kolo_listy.dy = 1;
    for (int i = 0; i < 500; ++i) okno_listy.obsluz_zdarzenie(kolo_listy);
    bws_sprawdz(lista.pierwszy_widoczny_indeks() == 0);
    bws_sprawdz(lista.wybrany_indeks() == -1 && lista.indeks_fokusu() == -1);

    lista.ustaw_wybrany_indeks(0);
    ZdarzenieKlawiatury lk{};
    lk.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x51U; // PageDown
    lista.obsluz_wcisniecie_klawisza(lk);
    bws_sprawdz(lista.wybrany_indeks() > 0);
    lk = {};
    lk.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x4FU; // End
    lista.obsluz_wcisniecie_klawisza(lk);
    bws_sprawdz(lista.wybrany_indeks() == 1199);
    bws_sprawdz(lista.pasek_pionowy().wartosc() > 0);
    ZdarzenieRolki lr{};
    lr.delta_y = 1;
    int64_t lista_scroll_przed = lista.pasek_pionowy().wartosc();
    lista.obsluz_rolke(lr);
    bws_sprawdz(lista.pasek_pionowy().wartosc() < lista_scroll_przed);
    lista.ustaw_wlaczona(false);
    int lista_wybor_przed = lista.wybrany_indeks();
    lk = {};
    lk.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x48U;
    lista.obsluz_wcisniecie_klawisza(lk);
    bws_sprawdz(lista.wybrany_indeks() == lista_wybor_przed);
    bws_log_u64("[LISTA POMIAR] visible_rows=", lista.liczba_ostatnio_narysowanych_wierszy());
    bws_log_u64("[LISTA POMIAR] heap_allocations_repaint=", lista_alokacje_po - lista_alokacje_przed);
    bws_log_u64("[LISTA POMIAR] wheel_dirty_area_px=", pole_brudne_kola);
    bws_log_u64("[LISTA POMIAR] heap_allocations_wheel=", lista_kolo_alok_po - lista_kolo_alok_przed);
    bws_log("[BURSZTYNUI WYBOR] Grupy, klawiatura i 1200 elementow: OK\n");

    // -------------------------------------------------------------------------
    // 8. PoleWielowierszowe: UTF-8, nowe linie, historia i widoczny rendering
    // -------------------------------------------------------------------------
    bws_log("[BURSZTYNUI MEMO] UTF-8, edycja wielowierszowa i przewijanie...\n");
    Okno okno_memo("Test memo", 430, 260);
    okno_memo.ustaw_pozycje(150, 110);
    auto& memo_root = okno_memo.dodaj<Kolumna>();
    memo_root.ustaw_wypelnienie(Marginesy(8));
    auto& memo = memo_root.dodaj<PoleWielowierszowe>("Tekst wielowierszowy...");
    memo.ustaw_preferowany_rozmiar(Rozmiar(414, 216));
    memo.ustaw_tekst("Zażółć\r\ngęślą\rjaźń\nkoniec");
    bws_sprawdz(memo.liczba_wierszy() == 4);
    const char* memo_t = memo.tekst();
    bool ma_cr = false;
    for (size_t i = 0; memo_t[i]; ++i) if (memo_t[i] == '\r') ma_cr = true;
    bws_sprawdz(!ma_cr);
    memo.ustaw_maksymalna_liczbe_znakow(2000);
    memo.ustaw_odstep_linii(2);
    okno_memo.przelicz_uklad();
    KontekstRysowania ctx_memo;
    okno_memo.rysuj(ctx_memo, true);
    bws_sprawdz(memo.liczba_ostatnio_narysowanych_linii() == 4);

    // Kursor nigdy nie moze znalezc sie wewnatrz sekwencji UTF-8.
    memo.ustaw_zaznaczenie(8, 8); // granica po "Zażółć"
    ZdarzenieKlawiatury mk{};
    mk.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x4BU;
    memo.obsluz_wcisniecie_klawisza(mk);
    size_t memo_kursor = memo.pozycja_kursora();
    bws_sprawdz((static_cast<uint8_t>(memo.tekst()[memo_kursor]) & 0xC0U) != 0x80U);

    // Enter wstawia LF i generuje zatwierdzenie, po czym Undo/Redo odtwarza stan.
    int zatwierdzenia_memo = 0;
    memo.przy_zatwierdzeniu([](PoleWielowierszowe*, const char*, void* dane) {
        ++*static_cast<int*>(dane);
    }, &zatwierdzenia_memo);
    size_t wiersze_przed_enter = memo.liczba_wierszy();
    mk = {};
    mk.kod_klawisza = 13;
    memo.obsluz_wcisniecie_klawisza(mk);
    bws_sprawdz(memo.liczba_wierszy() == wiersze_przed_enter + 1);
    bws_sprawdz(zatwierdzenia_memo == 1);
    bws_sprawdz(memo.cofnij());
    bws_sprawdz(memo.liczba_wierszy() == wiersze_przed_enter);
    bws_sprawdz(memo.ponow());
    bws_sprawdz(memo.liczba_wierszy() == wiersze_przed_enter + 1);

    // Zaznaczenie przecinajace linie, schowek text/plain, wytnij i wklej.
    memo.ustaw_tekst("Pierwszy\nDrugi\nTrzeci");
    memo.ustaw_zaznaczenie(4, 15);
    mk = {};
    mk.modyfikatory = BWS_MOD_CTRL;
    mk.kod_klawisza = 'c';
    memo.obsluz_wcisniecie_klawisza(mk);
    mk = {};
    mk.modyfikatory = BWS_MOD_CTRL;
    mk.kod_klawisza = 'x';
    memo.obsluz_wcisniecie_klawisza(mk);
    size_t dl_po_wycieciu = 0; while (memo.tekst()[dl_po_wycieciu]) ++dl_po_wycieciu;
    mk = {};
    mk.modyfikatory = BWS_MOD_CTRL;
    mk.kod_klawisza = 'v';
    memo.obsluz_wcisniecie_klawisza(mk);
    size_t dl_po_wklejeniu = 0; while (memo.tekst()[dl_po_wklejeniu]) ++dl_po_wklejeniu;
    bws_sprawdz(dl_po_wklejeniu > dl_po_wycieciu);

    memo.ustaw_tekst("Bardzo długi wiersz UTF-8: Zażółć gęślą jaźń, który wymusza zawijanie w małym widoku.\nDrugi wiersz.\nTrzeci wiersz.\nCzwarty wiersz.\nPiąty wiersz.\nSzósty wiersz.\nSiódmy wiersz.\nÓsmy wiersz.\nDziewiąty wiersz.\nDziesiąty wiersz.\nJedenasty wiersz.\nDwunasty wiersz.\nTrzynasty wiersz.\nCzternasty wiersz.");
    memo.ustaw_zawijanie_wierszy(true);
    size_t zawiniete = memo.liczba_linii_wizualnych();
    memo.ustaw_zawijanie_wierszy(false);
    bws_sprawdz(memo.liczba_linii_wizualnych() <= zawiniete);
    bws_sprawdz(memo.pasek_poziomy().czy_widoczna());
    memo.przejdz_do_wiersza(13);
    memo.przewin_do_kursora();
    bws_sprawdz(memo.pasek_pionowy().wartosc() > 0);

    // Dokladny viewport: polska linia bez zawijania oraz wiele wierszy
    // wymuszaja oba paski jednoczesnie.
    static const char* linia_dluga =
        "Zażółć gęślą jaźń — Pchnąć w tę łódź jeża lub ośm skrzyń fig. "
        "Długi wiersz 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ "
        "Zażółć gęślą jaźń — Pchnąć w tę łódź jeża lub ośm skrzyń fig. "
        "Długi wiersz 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ "
        "Zażółć gęślą jaźń — koniec bardzo długiego wiersza.";
    PoleWielowierszowe* memo_clip_ptr = new PoleWielowierszowe();
    bws_sprawdz(memo_clip_ptr != nullptr);
    PoleWielowierszowe& memo_clip = *memo_clip_ptr;
    memo_clip.ustaw_zawijanie_wierszy(false);
    memo_clip.ustaw_tekst(
        "Zażółć gęślą jaźń — Pchnąć w tę łódź jeża lub ośm skrzyń fig. Długi wiersz 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ Zażółć gęślą jaźń — Pchnąć w tę łódź jeża lub ośm skrzyń fig. Długi wiersz 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ Zażółć gęślą jaźń — koniec bardzo długiego wiersza.\n"
        "wiersz 2\nwiersz 3\nwiersz 4\nwiersz 5\nwiersz 6\nwiersz 7\nwiersz 8");
    memo_clip.rozmiesc(Prostokat(0, 0, 210, 110));
    bws_sprawdz(memo_clip.pasek_poziomy().czy_widoczna());
    bws_sprawdz(memo_clip.pasek_pionowy().czy_widoczna());
    Prostokat vp = memo_clip.prostokat_widoku_tekstu();
    Prostokat pp = memo_clip.prostokat_paska_pionowego();
    Prostokat ph = memo_clip.prostokat_paska_poziomego();
    Prostokat rog = memo_clip.prostokat_rogu_paskow();
    bws_sprawdz(vp == Prostokat(7, 6, 178, 80));
    bws_sprawdz(ph.prawy() == pp.x && pp.dolny() == ph.y);
    bws_sprawdz(!rog.czy_pusty() && rog.x == pp.x && rog.y == ph.y);
    bws_sprawdz(!vp.przecina(pp) && !vp.przecina(ph) && !vp.przecina(rog));
    bws_sprawdz(memo_clip.pasek_poziomy().maksimum() + vp.szerokosc ==
                CzcionkaUI::zmierz_tekst(linia_dluga));

    KontekstRysowania ctx_clip;
    memo_clip.pasek_poziomy().ustaw_wartosc(0);
    memo_clip.pasek_pionowy().ustaw_wartosc(0);
    ctx_clip.wyczysc_pomiar_tekstu();
    memo_clip.rysuj(ctx_clip, true);
    Prostokat tekst_clip = ctx_clip.obszar_narysowanego_tekstu();
    bws_sprawdz(!tekst_clip.czy_pusty());
    bws_sprawdz(tekst_clip.x >= vp.x && tekst_clip.y >= vp.y &&
                tekst_clip.prawy() <= vp.prawy() && tekst_clip.dolny() <= vp.dolny());

    int64_t polowa_x = memo_clip.pasek_poziomy().maksimum() / 2;
    memo_clip.wyczysc_brudny_obszar();
    uint64_t memo_scroll_alok_przed = gui_liczba_alokacji_sterty();
    memo_clip.pasek_poziomy().ustaw_wartosc(polowa_x);
    ctx_clip.wyczysc_pomiar_tekstu();
    memo_clip.rysuj(ctx_clip, false);
    uint64_t memo_scroll_alok_po = gui_liczba_alokacji_sterty();
    tekst_clip = ctx_clip.obszar_narysowanego_tekstu();
    bws_sprawdz(tekst_clip.x >= vp.x && tekst_clip.y >= vp.y &&
                tekst_clip.prawy() <= vp.prawy() && tekst_clip.dolny() <= vp.dolny());
    bws_sprawdz(memo_scroll_alok_po == memo_scroll_alok_przed);
    Prostokat memo_scroll_dirty = memo_clip.pobierz_brudny_obszar();
    uint64_t memo_scroll_dirty_px = static_cast<uint64_t>(memo_scroll_dirty.szerokosc) *
                                    static_cast<uint64_t>(memo_scroll_dirty.wysokosc);
    bws_sprawdz(memo_scroll_dirty_px > 0 && memo_scroll_dirty_px < 210ULL * 110ULL);

    memo_clip.pasek_poziomy().ustaw_wartosc(memo_clip.pasek_poziomy().maksimum());
    ctx_clip.wyczysc_pomiar_tekstu();
    memo_clip.rysuj(ctx_clip, false);
    tekst_clip = ctx_clip.obszar_narysowanego_tekstu();
    bws_sprawdz(tekst_clip.x >= vp.x && tekst_clip.prawy() <= vp.prawy());

    for (int i = 0; i < 4; ++i) {
        memo_clip.rozmiesc(Prostokat(0, 0, 3600, 110));
        bws_sprawdz(!memo_clip.pasek_poziomy().czy_widoczna());
        memo_clip.rozmiesc(Prostokat(0, 0, 210, 110));
        bws_sprawdz(memo_clip.pasek_poziomy().czy_widoczna() &&
                    memo_clip.pasek_pionowy().czy_widoczna());
        bws_sprawdz(memo_clip.prostokat_widoku_tekstu() == vp);
    }

    // Ramka istnieje przed hover/fokusem i po utracie fokusu. Fokus zmienia
    // kolor/emfaze, ale nie grubosc geometrii kontrolki.
    bws_sprawdz(txt_input.styl_stanu(StanWizualny::Normalny).ramka != 0);
    bws_sprawdz(memo_clip.styl_stanu(StanWizualny::Normalny).ramka != 0);
    bws_sprawdz(lista.styl_stanu(StanWizualny::Normalny).ramka != 0);
    bws_sprawdz(suwak.styl_stanu(StanWizualny::Normalny).ramka != 0);
    bws_sprawdz(pasek_model.styl_stanu(StanWizualny::Normalny).ramka != 0);
    bws_sprawdz(chk_haslo.styl_stanu(StanWizualny::Normalny).ramka != 0);
    bws_sprawdz(txt_input.styl_stanu(StanWizualny::Normalny).grubosc_ramki ==
                txt_input.styl_stanu(StanWizualny::Fokus).grubosc_ramki);
    bws_sprawdz(suwak.styl_stanu(StanWizualny::Normalny).grubosc_ramki ==
                suwak.styl_stanu(StanWizualny::Fokus).grubosc_ramki);
    txt_input.przy_fokusie();
    bws_sprawdz(txt_input.czy_ma_fokus());
    txt_input.przy_utracie_fokusu();
    bws_sprawdz(!txt_input.czy_ma_fokus() &&
                txt_input.styl_stanu(StanWizualny::Normalny).ramka != 0);

    okno_memo.rysuj(ctx_memo, false);
    uint64_t memo_alokacje_przed = gui_liczba_alokacji_sterty();
    ZdarzenieWprowadzaniaTekstu mt{};
    mt.punkt_kodowy = 0x0105; // ą
    memo.obsluz_wprowadzenie_tekstu(mt);
    okno_memo.rysuj(ctx_memo, false);
    uint64_t memo_alokacje_po = gui_liczba_alokacji_sterty();
    int memo_dirty = okno_memo.ostatnia_powierzchnia_odrysowania();
    bws_sprawdz(mt.obsluzone);
    bws_sprawdz(memo_alokacje_po == memo_alokacje_przed);
    bws_sprawdz(memo_dirty > 0 && memo_dirty < okno_memo.szerokosc() * okno_memo.wysokosc());
    bws_sprawdz(memo.liczba_ostatnio_narysowanych_linii() <=
                static_cast<size_t>(memo.wysokosc() / 16 + 2));
    memo.ustaw_tylko_do_odczytu(true);
    const char* przed_ro = memo.tekst();
    size_t dl_ro = 0; while (przed_ro[dl_ro]) ++dl_ro;
    memo.obsluz_wprowadzenie_tekstu(mt);
    size_t dl_ro_po = 0; while (memo.tekst()[dl_ro_po]) ++dl_ro_po;
    bws_sprawdz(dl_ro == dl_ro_po);
    bws_log_u64("[MEMO POMIAR] visible_lines=", memo.liczba_ostatnio_narysowanych_linii());
    bws_log_u64("[MEMO POMIAR] dirty_area_px=", static_cast<uint64_t>(memo_dirty));
    bws_log_u64("[MEMO POMIAR] heap_allocations_type_repaint=", memo_alokacje_po - memo_alokacje_przed);
    bws_log_u64("[MEMO POMIAR] horizontal_scroll_dirty_area_px=", memo_scroll_dirty_px);
    bws_log_u64("[MEMO POMIAR] horizontal_scroll_allocations=", memo_scroll_alok_po - memo_scroll_alok_przed);

    // ---------------------------------------------------------------------
    // 7-11. Jeden wspolny popup BWS56, ListaRozwijana oraz wspolny model menu
    // ---------------------------------------------------------------------
    bws_log("[BURSZTYNUI POPUP] BWS56, ComboBox i menu wspoldzielone...\n");
    // Wczesniejsze testy maksymalizacji celowo zmieniaja rzeczywista warstwe
    // procesu. BWS56 waliduje wzgledem tej warstwy, wiec przed kontraktami
    // popupu przywracamy te sama geometrie, ktora otrzyma logiczny Okno.
    bws_sprawdz(gui_ustaw_geometrie_okna(WIN_X, WIN_Y, WIN_W, WIN_H));
    Okno* okno_popup_ptr = new Okno("Test popupow", WIN_W, WIN_H);
    bws_sprawdz(okno_popup_ptr != nullptr);
    Okno& okno_popup = *okno_popup_ptr;
    okno_popup.ustaw_geometrie(WIN_X, WIN_Y, WIN_W, WIN_H);
    okno_popup.przelicz_uklad();
    ZarzadcaPopupow& popupy = okno_popup.zarzadca_popupow();

    // Kontrakty wspolrzednych: wynik jest lokalny dla klienta wlasciciela,
    // a prostokat rejestrowany w BWS56 jest ekranowy. BWS62 ogranicza obszar,
    // zas istniejacy prymityw BWS56 wymaga tez przeciecia z warstwa wlasciciela.
    Prostokat nad = popupy.umiesc_pod_kotwica(
        Prostokat(300, WIN_H - 20, 20, 10), Rozmiar(120, 90), true);
    bws_sprawdz(nad.y + nad.wysokosc <= WIN_H - 20);
    Prostokat lewo = popupy.umiesc_podmenu(
        Prostokat(WIN_W - 18, 90, 16, 24), Rozmiar(140, 80));
    bws_sprawdz(lewo.x + lewo.szerokosc <= WIN_W - 18);
    Prostokat rog_popupu = popupy.umiesc_przy_punkcie_ekranu(
        WIN_X + WIN_W - 2, WIN_Y + WIN_H - 2, Rozmiar(130, 95));
    bws_sprawdz(rog_popupu.x >= 0 && rog_popupu.y >= 0 &&
                rog_popupu.prawy() <= WIN_W && rog_popupu.dolny() <= WIN_H);

    PowierzchniaPopup* powierzchnia_testowa_ptr = new PowierzchniaPopup();
    Przycisk* fokus_wlasciciela_ptr = new Przycisk("Fokus wlasciciela");
    bws_sprawdz(powierzchnia_testowa_ptr != nullptr && fokus_wlasciciela_ptr != nullptr);
    PowierzchniaPopup& powierzchnia_testowa = *powierzchnia_testowa_ptr;
    Przycisk& fokus_wlasciciela = *fokus_wlasciciela_ptr;
    okno_popup.ustaw_fokus(&fokus_wlasciciela);
    Prostokat testowy_popup(40, 80, 180, 90);
    uint64_t popup_alok_przed = gui_liczba_alokacji_sterty();
    for (int i = 0; i < 100; ++i) {
        bws_sprawdz(popupy.otworz(powierzchnia_testowa, testowy_popup,
                                  &fokus_wlasciciela, &powierzchnia_testowa));
        bws_sprawdz(powierzchnia_testowa.czy_otwarta());
        popupy.zamknij(PowodZamknieciaPopup::Anulowanie);
        bws_sprawdz(!powierzchnia_testowa.czy_otwarta());
    }
    uint64_t popup_alok_po = gui_liczba_alokacji_sterty();
    bws_sprawdz(popup_alok_po == popup_alok_przed);
    bws_sprawdz(okno_popup.fokus() == &fokus_wlasciciela);
    bws_sprawdz(!popupy.czy_ma_wpis_paska_zadan());
    bws_log_u64("[POPUP DIAG] bledy_po_open100=", bws_test_blad - bledy_bui_przed);

    // Capture nie moze pozostac po anulowaniu w trakcie wcisniecia.
    bws_sprawdz(popupy.otworz(powierzchnia_testowa, testowy_popup,
                              &fokus_wlasciciela, &powierzchnia_testowa));
    ZdarzenieMyszki popup_down{};
    popup_down.typ = TypZdarzenia::WcisniecieMyszy;
    popup_down.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
    popup_down.x = testowy_popup.x + 8; popup_down.y = testowy_popup.y + 8;
    bws_sprawdz(popupy.obsluz_wcisniecie(popup_down));
    bws_sprawdz(popupy.czy_przechwytuje_mysz());
    popupy.zamknij(PowodZamknieciaPopup::Escape);
    bws_sprawdz(!popupy.czy_przechwytuje_mysz() &&
                !powierzchnia_testowa.czy_wcisnieta());
    bws_sprawdz(popupy.czy_brudny());
    Prostokat brudny_zamkniecia = popupy.brudny_obszar();
    bws_sprawdz(brudny_zamkniecia.szerokosc == testowy_popup.szerokosc &&
                brudny_zamkniecia.wysokosc == testowy_popup.wysokosc);

    // Klik poza popupem zamyka go, ale pozostaje nieobsluzony, aby Okno moglo
    // skierowac go dalej do zwyklego drzewa kontrolek.
    bws_sprawdz(popupy.otworz(powierzchnia_testowa, testowy_popup));
    ZdarzenieMyszki popup_poza{};
    popup_poza.typ = TypZdarzenia::WcisniecieMyszy;
    popup_poza.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
    popup_poza.x = 5; popup_poza.y = 35;
    bws_sprawdz(!popupy.obsluz_wcisniecie(popup_poza));
    bws_sprawdz(!popupy.czy_otwarty() && !popup_poza.obsluzone);

    // Escape dziala takze wtedy, gdy zwykle drzewo wlasciciela nie ma fokusu.
    okno_popup.ustaw_fokus(nullptr);
    bws_sprawdz(popupy.otworz(powierzchnia_testowa, testowy_popup));
    bws_zdarzenie popup_escape{};
    popup_escape.typ = BWS_ZDARZENIE_KLAWISZ;
    popup_escape.kod = 27;
    okno_popup.obsluz_zdarzenie(popup_escape);
    bws_sprawdz(!popupy.czy_otwarty());

    // Destrukcja wlasciciela odlacza zyjaca dluzej powierzchnie.
    PowierzchniaPopup* popup_po_wlascicielu_ptr = new PowierzchniaPopup();
    bws_sprawdz(popup_po_wlascicielu_ptr != nullptr);
    PowierzchniaPopup& popup_po_wlascicielu = *popup_po_wlascicielu_ptr;
    Okno* wlasciciel_tymczasowy = new Okno("Wlasciciel tymczasowy", WIN_W, WIN_H);
    bws_sprawdz(wlasciciel_tymczasowy != nullptr);
    if (wlasciciel_tymczasowy) {
        wlasciciel_tymczasowy->ustaw_geometrie(WIN_X, WIN_Y, WIN_W, WIN_H);
        wlasciciel_tymczasowy->przelicz_uklad();
        bws_sprawdz(wlasciciel_tymczasowy->zarzadca_popupow().otworz(
            popup_po_wlascicielu, Prostokat(30, 60, 120, 70)));
        delete wlasciciel_tymczasowy;
        bws_sprawdz(!popup_po_wlascicielu.czy_otwarta());
    }
    bws_log_u64("[POPUP DIAG] bledy_po_lifecycle=", bws_test_blad - bledy_bui_przed);

    // ListaRozwijana komponuje ListaWyboru: kolko przesuwa widok, nie wybor.
    ListaRozwijana* combo_pusta_ptr = new ListaRozwijana();
    bws_sprawdz(combo_pusta_ptr != nullptr);
    ListaRozwijana& combo_pusta = *combo_pusta_ptr;
    combo_pusta.ustaw_zarzadce_popupow(&popupy);
    combo_pusta.rozmiesc(Prostokat(20, 45, 220, 30));
    bws_sprawdz(combo_pusta.liczba_elementow() == 0 && combo_pusta.rozwin());
    bws_sprawdz(combo_pusta.czy_rozwinieta());
    popupy.zamknij(PowodZamknieciaPopup::Escape);

    ListaRozwijana* combo_ptr = new ListaRozwijana();
    bws_sprawdz(combo_ptr != nullptr);
    ListaRozwijana& combo = *combo_ptr;
    combo.ustaw_zarzadce_popupow(&popupy);
    combo.rozmiesc(Prostokat(260, 45, 240, 30));
    combo.ustaw_maks_widocznych_elementow(7);
    bool dodano_duza_liste = true;
    for (int i = 0; i < 1200; ++i)
        if (!combo.dodaj_element("Element listy UTF-8: Zażółć")) dodano_duza_liste = false;
    bws_sprawdz(dodano_duza_liste);
    bws_sprawdz(combo.liczba_elementow() == 1200);
    bws_sprawdz(combo.wstaw_element(1, "Wstawiony", 77));
    bws_sprawdz(combo.znacznik_elementu(1) == 77 && combo.usun_element(1));
    bws_sprawdz(combo.ustaw_wybrany_indeks(0));
    struct PomiarCombo { int zmiany; int zatwierdzenia; } pomiar_combo{};
    combo.przy_zmianie_wyboru([](ListaRozwijana*, int, void* dane) {
        ++static_cast<PomiarCombo*>(dane)->zmiany;
    }, &pomiar_combo);
    combo.przy_zatwierdzeniu([](ListaRozwijana*, int, const char*, void* dane) {
        ++static_cast<PomiarCombo*>(dane)->zatwierdzenia;
    }, &pomiar_combo);
    bws_sprawdz(combo.rozwin());
    bws_sprawdz(combo.lista_popupu().pierwszy_widoczny_indeks() == 0);
    int wybor_przed_kolem = combo.wybrany_indeks();
    Prostokat rp_combo = combo.prostokat_popupu();
    ZdarzenieRolki combo_kolo{};
    combo_kolo.x = rp_combo.x + 10; combo_kolo.y = rp_combo.y + 10;
    combo_kolo.delta_y = -1;
    bws_sprawdz(popupy.obsluz_rolke(combo_kolo));
    bws_sprawdz(combo.lista_popupu().pierwszy_widoczny_indeks() == 3);
    bws_sprawdz(combo.wybrany_indeks() == wybor_przed_kolem);

    ZdarzenieKlawiatury combo_dol{};
    combo_dol.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x50U;
    bws_sprawdz(popupy.obsluz_klawisz(combo_dol));
    bws_sprawdz(combo.wybrany_indeks() == 1 && combo.czy_rozwinieta());
    ZdarzenieKlawiatury combo_enter{}; combo_enter.kod_klawisza = 13;
    bws_sprawdz(popupy.obsluz_klawisz(combo_enter));
    bws_sprawdz(!combo.czy_rozwinieta() && pomiar_combo.zatwierdzenia == 1);

    // Escape przywraca wybor sprzed otwarcia, Home/End/Page poruszaja lista.
    bws_sprawdz(combo.ustaw_wybrany_indeks(10) && combo.rozwin());
    combo_dol = {}; combo_dol.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x51U;
    bws_sprawdz(popupy.obsluz_klawisz(combo_dol));
    bws_sprawdz(combo.wybrany_indeks() > 10);
    ZdarzenieKlawiatury combo_esc{}; combo_esc.kod_klawisza = 27;
    bws_sprawdz(popupy.obsluz_klawisz(combo_esc));
    bws_sprawdz(combo.wybrany_indeks() == 10 && !combo.czy_rozwinieta());
    combo.ustaw_edytowalna(true);
    combo.pole_edycji().ustaw_tekst("Wlasna wartosc Zażółć");
    bws_sprawdz(combo.czy_edytowalna() && combo.wybrany_tekst()[0] == 'W');
    bws_log_u64("[POPUP DIAG] bledy_po_combo=", bws_test_blad - bledy_bui_przed);

    // Jeden model ElementMenu/Menu obsluguje kontekst, check, radio i podmenu.
    Menu model_menu;
    ElementMenu& pole_modelu = model_menu.dodaj_element("Pokaz", 101);
    pole_modelu.ustaw_typ_zaznaczenia(TypZaznaczeniaMenu::Pole).ustaw_zaznaczony(true)
                .ustaw_skrot("Ctrl+P").ustaw_ikone(9).ustaw_znacznik(0x1234);
    model_menu.dodaj_separator();
    ElementMenu& radio_a = model_menu.dodaj_element("A", 102);
    ElementMenu& radio_b = model_menu.dodaj_element("B", 103);
    radio_a.ustaw_typ_zaznaczenia(TypZaznaczeniaMenu::Radio).ustaw_grupe_radialna(7);
    radio_b.ustaw_typ_zaznaczenia(TypZaznaczeniaMenu::Radio).ustaw_grupe_radialna(7);
    model_menu.zaznacz_radio(7, 103);
    bws_sprawdz(model_menu.znajdz(101) == &pole_modelu && pole_modelu.czy_zaznaczony());
    bws_sprawdz(!radio_a.czy_zaznaczony() && radio_b.czy_zaznaczony());
    bws_sprawdz(pole_modelu.ikona() == 9 && pole_modelu.znacznik() == 0x1234);

    MenuKontekstowe* menu_kontekstowe_ptr = new MenuKontekstowe();
    bws_sprawdz(menu_kontekstowe_ptr != nullptr);
    MenuKontekstowe& menu_kontekstowe = *menu_kontekstowe_ptr;
    int wywolania_menu = 0;
    menu_kontekstowe.menu().dodaj_element("Wylaczony", 201).ustaw_wlaczony(false);
    menu_kontekstowe.menu().dodaj_separator();
    menu_kontekstowe.menu().dodaj_element("Zaznaczony", 202,
        [](ElementMenu&, void* dane) { ++*static_cast<int*>(dane); }, &wywolania_menu)
        .ustaw_typ_zaznaczenia(TypZaznaczeniaMenu::Pole).ustaw_zaznaczony(true);
    bws_sprawdz(menu_kontekstowe.pokaz(okno_popup, WIN_X + 250, WIN_Y + 160));
    bws_sprawdz(popupy.liczba_powierzchni() == 1);
    ZdarzenieKlawiatury menu_enter{}; menu_enter.kod_klawisza = 13;
    bws_sprawdz(popupy.obsluz_klawisz(menu_enter));
    bws_sprawdz(wywolania_menu == 1 && !menu_kontekstowe.czy_otwarte());
    bws_log_u64("[POPUP DIAG] bledy_po_menu_akcji=", bws_test_blad - bledy_bui_przed);

    // Zagniezdzone podmenu korzysta z tego samego widoku i zamyka sie Left.
    menu_kontekstowe.menu().wyczysc();
    Menu& podmenu_test = menu_kontekstowe.menu().dodaj_podmenu("Wiecej", 210);
    podmenu_test.dodaj_element("Polecenie", 211,
        [](ElementMenu&, void* dane) { ++*static_cast<int*>(dane); }, &wywolania_menu);
    bool pok_ok = menu_kontekstowe.pokaz(okno_popup, WIN_X + WIN_W - 4, WIN_Y + WIN_H - 4);
    bws_log_u64("[PODMENU DIAG] pok_ok=", pok_ok ? 1 : 0);
    bws_log_u64("[PODMENU DIAG] count_przed=", popupy.liczba_powierzchni());
    bws_sprawdz(pok_ok);
    ZdarzenieKlawiatury menu_prawo{};
    menu_prawo.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x4DU;
    bool r_ok = popupy.obsluz_klawisz(menu_prawo);
    bws_log_u64("[PODMENU DIAG] r_ok=", r_ok ? 1 : 0);
    bws_log_u64("[PODMENU DIAG] count_po_r=", popupy.liczba_powierzchni());
    bws_sprawdz(r_ok);
    bws_sprawdz(popupy.liczba_powierzchni() == 2);
    ZdarzenieKlawiatury menu_lewo{};
    menu_lewo.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x4BU;
    bool l_ok = popupy.obsluz_klawisz(menu_lewo);
    bws_log_u64("[PODMENU DIAG] l_ok=", l_ok ? 1 : 0);
    bws_log_u64("[PODMENU DIAG] count_po_l=", popupy.liczba_powierzchni());
    bws_sprawdz(l_ok);
    bws_sprawdz(popupy.liczba_powierzchni() == 1);
    popupy.zamknij();
    bws_log_u64("[POPUP DIAG] bledy_po_podmenu=", bws_test_blad - bledy_bui_przed);

    // MenuGlowne: otwarcie mysza i przelaczenie top-level przez Right.
    MenuGlowne* menu_glowne_test_ptr = new MenuGlowne();
    bws_sprawdz(menu_glowne_test_ptr != nullptr);
    MenuGlowne& menu_glowne_test = *menu_glowne_test_ptr;
    menu_glowne_test.ustaw_zarzadce_popupow(&popupy);
    menu_glowne_test.rozmiesc(Prostokat(0, 28, 420, 28));
    int wywolania_glownego = 0;
    Menu& plik_test = menu_glowne_test.menu().dodaj_podmenu("Plik", 301);
    plik_test.dodaj_element("Otworz", 302,
        [](ElementMenu&, void* dane) { ++*static_cast<int*>(dane); }, &wywolania_glownego);
    Menu& edycja_test = menu_glowne_test.menu().dodaj_podmenu("Edycja", 303);
    edycja_test.dodaj_element("Kopiuj", 304,
        [](ElementMenu&, void* dane) { ++*static_cast<int*>(dane); }, &wywolania_glownego);
    ZdarzenieMyszki menu_down{};
    menu_down.typ = TypZdarzenia::WcisniecieMyszy;
    menu_down.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
    menu_down.x = 6; menu_down.y = 34;
    menu_glowne_test.obsluz_wcisniecie_myszy(menu_down);
    bws_sprawdz(menu_glowne_test.czy_otwarte());
    menu_prawo = {}; menu_prawo.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x4DU;
    bws_sprawdz(popupy.obsluz_klawisz(menu_prawo));
    menu_enter = {}; menu_enter.kod_klawisza = 13;
    bws_sprawdz(popupy.obsluz_klawisz(menu_enter));
    bws_sprawdz(wywolania_glownego == 1 && !menu_glowne_test.czy_otwarte());
    bws_log_u64("[POPUP DIAG] bledy_po_menu_glownym=", bws_test_blad - bledy_bui_przed);

    // Hover 100x: po pierwszym pelnym rysowaniu zmieniaja sie tylko dwa wiersze.
    menu_kontekstowe.menu().wyczysc();
    menu_kontekstowe.menu().dodaj_element("Pierwszy", 401);
    menu_kontekstowe.menu().dodaj_element("Drugi", 402);
    menu_kontekstowe.menu().dodaj_element("Trzeci", 403);
    bws_sprawdz(menu_kontekstowe.pokaz(okno_popup, WIN_X + 300, WIN_Y + 140));
    KontekstRysowania* ctx_popup_ptr = new KontekstRysowania();
    bws_sprawdz(ctx_popup_ptr != nullptr);
    KontekstRysowania& ctx_popup = *ctx_popup_ptr;
    okno_popup.rysuj(ctx_popup, true);
    Prostokat r_hover = popupy.prostokat_zarejestrowany_ekranu();
    uint64_t hover_alok_przed = gui_liczba_alokacji_sterty();
    for (int i = 0; i < 100; ++i) {
        ZdarzenieMyszki ruch_menu{};
        ruch_menu.x = r_hover.x - WIN_X + 10;
        ruch_menu.y = r_hover.y - WIN_Y + (i & 1 ? 36 : 12);
        popupy.obsluz_ruch(ruch_menu);
    }
    uint64_t hover_alok_po = gui_liczba_alokacji_sterty();
    Prostokat hover_dirty = popupy.brudny_obszar();
    uint64_t hover_dirty_px = static_cast<uint64_t>(hover_dirty.szerokosc) *
                              static_cast<uint64_t>(hover_dirty.wysokosc);
    bws_sprawdz(hover_alok_po == hover_alok_przed);
    bws_sprawdz(hover_dirty_px > 0 && hover_dirty_px <
                static_cast<uint64_t>(r_hover.szerokosc) * r_hover.wysokosc);
    popupy.zamknij();
    bws_log_u64("[POPUP DIAG] bledy_po_hover=", bws_test_blad - bledy_bui_przed);

    // Minimalizacja Okno zamyka lancuch popupow.
    bws_sprawdz(popupy.otworz(powierzchnia_testowa, testowy_popup));
    okno_popup.minimalizuj();
    bws_sprawdz(!popupy.czy_otwarty() && okno_popup.czy_zminimalizowane());
    bws_log_u64("[POPUP DIAG] bledy_po_minimalizacji=", bws_test_blad - bledy_bui_przed);

    bws_log_u64("[POPUP POMIAR] open_close_100_heap_allocations=",
                popup_alok_po - popup_alok_przed);
    bws_log_u64("[POPUP POMIAR] close_dirty_area_px=",
                static_cast<uint64_t>(brudny_zamkniecia.szerokosc) * brudny_zamkniecia.wysokosc);
    bws_log_u64("[MENU POMIAR] hover_100_heap_allocations=",
                hover_alok_po - hover_alok_przed);
    bws_log_u64("[MENU POMIAR] hover_dirty_area_px=", hover_dirty_px);
    bws_log("[BURSZTYNUI POPUP] Popup, ListaRozwijana, MenuGlowne i MenuKontekstowe: OK\n");
    delete ctx_popup_ptr;
    delete menu_glowne_test_ptr;
    delete menu_kontekstowe_ptr;
    delete combo_ptr;
    delete combo_pusta_ptr;
    delete popup_po_wlascicielu_ptr;
    delete fokus_wlasciciela_ptr;
    delete powierzchnia_testowa_ptr;
    // ---------------------------------------------------------------------
    // BURSZTYNUI STAGE 12..19: PasekPostepu, PoleNumeryczne, Karty,
    // WidokPrzewijany, Drzewo, PasekNarzedzi, PasekStanu, Podpowiedz
    // ---------------------------------------------------------------------
    bws_log("[BURSZTYNUI ROZSZERZONE] Testy 8 zaawansowanych kontrolek...\n");
    uint64_t bledy_adv_przed = bws_test_blad;

    // 1. PasekPostepu
    PasekPostepu* postep_ptr = new PasekPostepu(0, 100);
    bws_sprawdz(postep_ptr != nullptr);
    PasekPostepu& postep = *postep_ptr;
    postep.rozmiesc(Prostokat(0, 0, 200, 24));
    bws_sprawdz(postep.minimum() == 0 && postep.maksimum() == 100 && postep.wartosc() == 0);
    int cb_postep_count = 0;
    postep.przy_zmianie_wartosci([](PasekPostepu*, int64_t, void* d) { ++*static_cast<int*>(d); }, &cb_postep_count);
    postep.ustaw_wartosc(50);
    bws_sprawdz(postep.wartosc() == 50 && postep.procent() == 50 && cb_postep_count == 1);
    postep.zwieksz_o(25);
    bws_sprawdz(postep.wartosc() == 75 && postep.procent() == 75 && cb_postep_count == 2);
    // Odwrocony zakres i ujemne
    postep.ustaw_zakres(-50, 50);
    bws_sprawdz(postep.minimum() == -50 && postep.maksimum() == 50);
    postep.ustaw_wartosc(0);
    bws_sprawdz(postep.procent() == 50);
    // Indeterminate mode animation
    postep.ustaw_tryb_nieokreslony(true);
    bws_sprawdz(postep.czy_tryb_nieokreslony());
    bws_sprawdz(postep.czy_wymaga_animacji());
    postep.ustaw_widoczna(false);
    bws_sprawdz(!postep.czy_wymaga_animacji());
    postep.ustaw_widoczna(true);
    bws_sprawdz(postep.czy_wymaga_animacji());

    int cb_przed_anim = cb_postep_count;
    uint64_t anim_alok_przed = gui_liczba_alokacji_sterty();
    for (int i = 0; i < 100; ++i) {
        postep.krok_animacji(1000000000ULL + i * 35000000ULL);
    }
    uint64_t anim_alok_po = gui_liczba_alokacji_sterty();
    bws_sprawdz(cb_postep_count == cb_przed_anim); // brak wywolan callbacku liczbowego podczas animacji
    bws_sprawdz(anim_alok_po == anim_alok_przed); // 0 alokacji w animacji
    bws_log_u64("[POSTEP POMIAR] anim_100_allocations=", anim_alok_po - anim_alok_przed);
    bws_log_u64("[BURSZTYNUI DIAG] bledy_po_postep=", bws_test_blad - bledy_adv_przed);

    // 2. PoleNumeryczne - Badanie skrajnych przypadkow edycji i INT64
    PoleNumeryczne* num_ptr = new PoleNumeryczne(-100, 100, 0);
    bws_sprawdz(num_ptr != nullptr);
    PoleNumeryczne& num = *num_ptr;
    num.rozmiesc(Prostokat(0, 0, 140, 26));
    bws_sprawdz(num.wartosc() == 0 && num.minimum() == -100 && num.maksimum() == 100);
    int cb_num_count = 0;
    num.przy_zmianie_wartosci([](PoleNumeryczne*, int64_t, void* d) { ++*static_cast<int*>(d); }, &cb_num_count);
    uint64_t num_alok_przed = gui_liczba_alokacji_sterty();
    num.zwieksz();
    bws_sprawdz(num.wartosc() == 1 && cb_num_count == 1);
    num.zmniejsz();
    bws_sprawdz(num.wartosc() == 0 && cb_num_count == 2);
    // Duzy krok
    num.ustaw_duzy_krok(25);
    ZdarzenieKlawiatury pgup{}; pgup.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x49U; // PageUp
    num.dziecko(0)->obsluz_wcisniecie_klawisza(pgup);
    bws_sprawdz(num.wartosc() == 25);
    // Wrap mode
    num.ustaw_zawijanie_wartosci(true);
    num.ustaw_wartosc(100);
    num.zwieksz();
    bws_sprawdz(num.wartosc() == -100);

    // Testy parsowania ciagow wejsciowych w PoleNumeryczne
    PoleTekstowe* inner_edit = static_cast<PoleTekstowe*>(num.dziecko(0));
    bws_sprawdz(inner_edit != nullptr);

    // Tymczasowe niekompletne stany: "" i "-" i "+" nie moga uszkodzic modelu
    num.ustaw_wartosc(42);
    inner_edit->ustaw_tekst("");
    bws_sprawdz(num.wartosc() == 42);
    inner_edit->ustaw_tekst("-");
    bws_sprawdz(num.wartosc() == 42);
    inner_edit->ustaw_tekst("+");
    bws_sprawdz(num.wartosc() == 42);
    // Escape przywraca ostatnia zatwierdzona wartosc
    ZdarzenieKlawiatury esc_key{}; esc_key.kod_klawisza = 27U;
    inner_edit->obsluz_wcisniecie_klawisza(esc_key);
    bws_sprawdz(num.wartosc() == 42);

    // Wiodace zera
    inner_edit->ustaw_tekst("000012");
    num.zatwierdz();
    bws_sprawdz(num.wartosc() == 12);
    inner_edit->ustaw_tekst("-00012");
    num.zatwierdz();
    bws_sprawdz(num.wartosc() == -12);

    // INT64_MAX i INT64_MIN oraz overflow/underflow
    num.ustaw_zakres(-9223372036854775807LL - 1LL, 9223372036854775807LL);
    inner_edit->ustaw_tekst("9223372036854775807");
    num.zatwierdz();
    bws_sprawdz(num.wartosc() == 9223372036854775807LL);
    inner_edit->ustaw_tekst("-9223372036854775808");
    num.zatwierdz();
    bws_sprawdz(num.wartosc() == (-9223372036854775807LL - 1LL));

    // Przepelnienie: 9223372036854775808 -> clamp do MAX
    inner_edit->ustaw_tekst("9223372036854775808");
    num.zatwierdz();
    bws_sprawdz(num.wartosc() == 9223372036854775807LL);

    // Niedomiar: -9223372036854775809 -> clamp do MIN
    inner_edit->ustaw_tekst("-9223372036854775809");
    num.zatwierdz();
    bws_sprawdz(num.wartosc() == (-9223372036854775807LL - 1LL));

    // Niepoprawny tekst: "abc", "12abc"
    num.ustaw_wartosc(55);
    inner_edit->ustaw_tekst("abc");
    num.zatwierdz();
    bws_sprawdz(num.wartosc() == 55); // przywrocenie ostatniej zatwierdzonej
    inner_edit->ustaw_tekst("12abc");
    num.zatwierdz();
    bws_sprawdz(num.wartosc() == 55);

    uint64_t num_alok_po = gui_liczba_alokacji_sterty();
    bws_log_u64("[NUMERYCZNE POMIAR] spinner_allocations=", num_alok_po - num_alok_przed);
    bws_log_u64("[BURSZTYNUI DIAG] bledy_po_num=", bws_test_blad - bledy_adv_przed);

    // 3. WidokPrzewijany i Zagniezdzone Przewijanie (Nested Scrolling)
    WidokPrzewijany* scroll_outer_ptr = new WidokPrzewijany();
    WidokPrzewijany* scroll_inner_ptr = new WidokPrzewijany();
    bws_sprawdz(scroll_outer_ptr != nullptr && scroll_inner_ptr != nullptr);
    WidokPrzewijany& scroll_outer = *scroll_outer_ptr;
    WidokPrzewijany& scroll_inner = *scroll_inner_ptr;

    scroll_outer.rozmiesc(Prostokat(0, 0, 300, 300));
    scroll_outer.ustaw_rozmiar_zawartosci(Rozmiar(280, 600));

    scroll_inner.ustaw_preferowany_rozmiar(Rozmiar(200, 150));
    scroll_inner.rozmiesc(Prostokat(10, 10, 200, 150));
    scroll_inner.ustaw_rozmiar_zawartosci(Rozmiar(180, 400));
    scroll_outer.dodaj_do_zawartosci(&scroll_inner);

    Przycisk* btn_in_inner = new Przycisk("Wewnetrzny");
    btn_in_inner->rozmiesc(Prostokat(10, 50, 100, 30));
    scroll_inner.dodaj_do_zawartosci(btn_in_inner);

    bws_sprawdz(scroll_outer.offset_y() == 0 && scroll_inner.offset_y() == 0);

    // Kolo myszy nad scroll_inner
    uint64_t scroll_alok_przed = gui_liczba_alokacji_sterty();
    ZdarzenieRolki rolka_in{};
    rolka_in.x = WIN_X + 50;
    rolka_in.y = WIN_Y + 50;
    rolka_in.delta_y = -2;

    // 1) scroll_inner przewija sie w dol
    scroll_inner.obsluz_rolke(rolka_in);
    bws_log_u64("[SCROLL DIAG] s1_obs=", rolka_in.obsluzone ? 1 : 0);
    bws_log_u64("[SCROLL DIAG] s1_in_y=", scroll_inner.offset_y());
    bws_log_u64("[SCROLL DIAG] s1_out_y=", scroll_outer.offset_y());
    bws_sprawdz(rolka_in.obsluzone && scroll_inner.offset_y() == 48 && scroll_outer.offset_y() == 0);

    // 2) Przewijamy scroll_inner do samego dolu
    int64_t max_in_y = scroll_inner.pasek_pionowy().maksimum();
    scroll_inner.przewin_do(0, max_in_y);
    bws_log_u64("[SCROLL DIAG] s2_max_y=", max_in_y);
    bws_log_u64("[SCROLL DIAG] s2_in_y=", scroll_inner.offset_y());
    bws_sprawdz(scroll_inner.offset_y() == max_in_y);
    rolka_in.obsluzone = false;
    scroll_inner.obsluz_rolke(rolka_in);
    bws_log_u64("[SCROLL DIAG] s2_obs_in=", rolka_in.obsluzone ? 1 : 0);
    bws_sprawdz(!rolka_in.obsluzone);
    scroll_outer.obsluz_rolke(rolka_in);
    bws_log_u64("[SCROLL DIAG] s2_obs_out=", rolka_in.obsluzone ? 1 : 0);
    bws_log_u64("[SCROLL DIAG] s2_out_y=", scroll_outer.offset_y());
    bws_sprawdz(rolka_in.obsluzone && scroll_outer.offset_y() == 48);

    // 3) Ruch w przeciwnym kierunku (do gory) -> scroll_inner od razu konsumuje
    rolka_in.obsluzone = false;
    rolka_in.delta_y = 2;
    scroll_inner.obsluz_rolke(rolka_in);
    bws_log_u64("[SCROLL DIAG] s3_obs_in=", rolka_in.obsluzone ? 1 : 0);
    bws_log_u64("[SCROLL DIAG] s3_in_y=", scroll_inner.offset_y());
    bws_sprawdz(rolka_in.obsluzone && scroll_inner.offset_y() == max_in_y - 48);

    // Shift+kolo przewijanie poziome
    rolka_in.obsluzone = false;
    rolka_in.delta_y = -2;
    rolka_in.modyfikatory = 1; // Shift
    scroll_outer.ustaw_rozmiar_zawartosci(Rozmiar(600, 600));
    scroll_outer.obsluz_rolke(rolka_in);
    bws_log_u64("[SCROLL DIAG] s4_obs_out=", rolka_in.obsluzone ? 1 : 0);
    bws_log_u64("[SCROLL DIAG] s4_out_x=", scroll_outer.offset_x());
    bws_sprawdz(rolka_in.obsluzone && scroll_outer.offset_x() == 48);

    uint64_t scroll_alok_po = gui_liczba_alokacji_sterty();
    bws_log_u64("[WIDOK_PRZEWIJANY POMIAR] scroll_allocations=", scroll_alok_po - scroll_alok_przed);
    bws_log_u64("[BURSZTYNUI DIAG] bledy_po_scroll=", bws_test_blad - bledy_adv_przed);

    // 4. Karty - 0 alokacji sterty na przelaczanie kart
    Karty* karty_ptr = new Karty();
    bws_sprawdz(karty_ptr != nullptr);
    Karty& karty = *karty_ptr;
    karty.rozmiesc(Prostokat(0, 0, 400, 250));

    Panel* page0 = new Panel();
    Przycisk* btn_page0 = new Przycisk("P0");
    page0->dodaj_kontrolke(btn_page0);

    Panel* page1 = new Panel();
    Przycisk* btn_page1 = new Przycisk("P1");
    page1->dodaj_kontrolke(btn_page1);

    Panel* page2 = new Panel();
    Przycisk* btn_page2 = new Przycisk("P2");
    page2->dodaj_kontrolke(btn_page2);

    karty.dodaj_karte("Pierwsza", page0);
    karty.dodaj_karte("Druga", page1);
    karty.dodaj_karte("Trzecia", page2);

    bws_sprawdz(karty.liczba_kart() == 3 && karty.aktywna_karta() == 0);
    bws_sprawdz(page0->czy_widoczna() && !page1->czy_widoczna() && !page2->czy_widoczna());

    // Rozgrzewka: A -> B, B -> A
    karty.ustaw_aktywna_karte(1);
    bws_sprawdz(page1->czy_widoczna() && !page0->czy_widoczna());
    karty.ustaw_aktywna_karte(0);
    bws_sprawdz(page0->czy_widoczna() && !page1->czy_widoczna());

    // Pomiar 100 naprzemiennych przelaczen (musi byc dokladnie 0 alokacji sterty!)
    uint64_t tab_sw_przed = gui_liczba_alokacji_sterty();
    for (int i = 0; i < 100; ++i) {
        karty.ustaw_aktywna_karte((i % 2 == 0) ? 1 : 0);
    }
    uint64_t tab_sw_po = gui_liczba_alokacji_sterty();
    bws_sprawdz(tab_sw_po == tab_sw_przed);
    bws_sprawdz(btn_page0->rodzic() == page0 && btn_page1->rodzic() == page1); // instancje dzieci nienaruszone
    bws_log_u64("[KARTY POMIAR] tab_switch_100_allocations=", tab_sw_po - tab_sw_przed);
    bws_log_u64("[BURSZTYNUI DIAG] bledy_po_karty=", bws_test_blad - bledy_adv_przed);

    // 5. Drzewo - Limit 4096 wezlow, usuwanie, obsluga przepelnienia i wirtualizacja
    Drzewo* drzewo_ptr = new Drzewo();
    bws_sprawdz(drzewo_ptr != nullptr);
    Drzewo& drzewo = *drzewo_ptr;
    drzewo.rozmiesc(Prostokat(0, 0, 240, 200));
    UchwytWezlaDrzewa root1 = drzewo.dodaj_korzen("Katalog Glowny");
    UchwytWezlaDrzewa child1 = drzewo.dodaj_dziecko(root1, "Dokumenty");
    UchwytWezlaDrzewa child2 = drzewo.dodaj_dziecko(root1, "Zdjecia");
    (void)child2;
    UchwytWezlaDrzewa subchild = drzewo.dodaj_dziecko(child1, "plik.txt");
    bws_sprawdz(drzewo.liczba_wezlow() == 4);
    bws_sprawdz(drzewo.rodzic(child1) == root1 && drzewo.rodzic(subchild) == child1);
    bws_sprawdz(drzewo.liczba_dzieci(root1) == 2 && drzewo.liczba_dzieci(child1) == 1);

    // Usuwanie i bezpieczenstwo uchwytu pokoleniowego
    drzewo.usun_wezel(child1);
    bws_sprawdz(!child1.czy_poprawny() || drzewo.rodzic(child1).indeks == 0);
    // Stary uchwyt child1 odrzucony
    bws_sprawdz(!drzewo.czy_uchwyt_poprawny(child1));

    // Wypelnienie drzewa do limitu 4096 wezlow (64 katalogi, po 63 pliki kazdy = 4096 wezlow)
    drzewo.wyczysc();
    drzewo.rozpocznij_aktualizacje();
    for (size_t k = 0; k < 64; ++k) {
        char kbuf[32] = "Kat ";
        kbuf[4] = static_cast<char>('0' + (k / 10));
        kbuf[5] = static_cast<char>('0' + (k % 10));
        kbuf[6] = '\0';
        UchwytWezlaDrzewa r = drzewo.dodaj_korzen(kbuf);
        for (size_t d = 0; d < 63; ++d) {
            drzewo.dodaj_dziecko(r, "plik");
        }
    }
    drzewo.zakoncz_aktualizacje();
    bws_sprawdz(drzewo.liczba_wezlow() == Drzewo::MAKS_WEZLOW);
    // Kolejny wezel 4097 -> bezpieczne odrzucenie i zwrot niepoprawnego uchwytu
    UchwytWezlaDrzewa h_nadmiar = drzewo.dodaj_korzen("nadmiar");
    bws_sprawdz(!h_nadmiar.czy_poprawny());

    // Wirtualizacja renderingu: rysowanie przy 4096 wezlach
    KontekstRysowania ctx_tree;
    uint64_t tree_render_alok_przed = gui_liczba_alokacji_sterty();
    drzewo.rysuj(ctx_tree, true);
    uint64_t tree_render_alok_po = gui_liczba_alokacji_sterty();
    bws_sprawdz(tree_render_alok_po == tree_render_alok_przed);
    bws_log_u64("[DRZEWO POMIAR] render_4096_allocations=", tree_render_alok_po - tree_render_alok_przed);
    bws_log_u64("[BURSZTYNUI DIAG] bledy_po_drzewo=", bws_test_blad - bledy_adv_przed);

    // 6. PasekNarzedzi - Overflow i synchronizacja stanu
    PasekNarzedzi* toolbar_ptr = new PasekNarzedzi();
    bws_sprawdz(toolbar_ptr != nullptr);
    PasekNarzedzi& toolbar = *toolbar_ptr;
    toolbar.rozmiesc(Prostokat(0, 0, 300, 32));
    int tb_click = 0;
    ElementPaskaNarzedzi& btn_new = toolbar.dodaj_przycisk("Nowy", 1,
        [](ElementPaskaNarzedzi*, void* d) { ++*static_cast<int*>(d); }, &tb_click);
    toolbar.dodaj_separator();
    int tb_toggle_state = 0;
    ElementPaskaNarzedzi& btn_tog = toolbar.dodaj_przelacznik("Siatka", false,
        [](ElementPaskaNarzedzi*, bool s, void* d) { *static_cast<int*>(d) = s ? 1 : 0; }, &tb_toggle_state);
    bws_sprawdz(toolbar.liczba_elementow() == 3);
    btn_new.wykonaj();
    bws_sprawdz(tb_click == 1);
    btn_tog.wykonaj();
    bws_sprawdz(btn_tog.czy_przelaczony() && tb_toggle_state == 1);

    // Zmniejszenie szerokosci paska -> wymuszenie overflow
    toolbar.rozmiesc(Prostokat(0, 0, 80, 32));
    bws_sprawdz(toolbar.czy_jest_nadmiar());
    toolbar.ustaw_zarzadce_popupow(&popupy);
    toolbar.pokaz_menu_nadmiaru(); // rozgrzewka menu
    popupy.zamknij();

    // Otwarcie menu nadmiaru w stanie ustalonym (0 alokacji sterty)
    uint64_t tb_ov_alok_przed = gui_liczba_alokacji_sterty();
    for (int i = 0; i < 10; ++i) {
        toolbar.pokaz_menu_nadmiaru();
        popupy.zamknij();
    }
    uint64_t tb_ov_alok_po = gui_liczba_alokacji_sterty();
    bws_sprawdz(tb_ov_alok_po == tb_ov_alok_przed);
    bws_log_u64("[TOOLBAR POMIAR] overflow_allocations=", tb_ov_alok_po - tb_ov_alok_przed);
    bws_log_u64("[BURSZTYNUI DIAG] bledy_po_toolbar=", bws_test_blad - bledy_adv_przed);

    // 7. PasekStanu - Kombinacje rozmiarow i waskie okno
    PasekStanu* status_ptr = new PasekStanu();
    bws_sprawdz(status_ptr != nullptr);
    PasekStanu& statusbar = *status_ptr;
    statusbar.rozmiesc(Prostokat(0, 0, 400, 24));
    size_t s1 = statusbar.dodaj_sekcje("Gotowy", 100, RozmiarSekcjiStanu::Rozciagnij);
    size_t s2 = statusbar.dodaj_sekcje("UTF-8", 60, RozmiarSekcjiStanu::Stala);
    size_t s3 = statusbar.dodaj_sekcje("Linia 1", 80, RozmiarSekcjiStanu::DopasujDoTresci);
    size_t s4 = statusbar.dodaj_sekcje("Ekstra", 100, RozmiarSekcjiStanu::Rozciagnij);
    (void)s3;
    (void)s4;
    bws_sprawdz(statusbar.liczba_sekcji() == 4);
    bws_sprawdz(statusbar.prostokat_sekcji(s1).szerokosc > 0);
    bws_sprawdz(statusbar.prostokat_sekcji(s2).szerokosc == 60);

    // Waskie okno: okno wezsze niz suma sekcji stalych -> brak ujemnych szerokosci i nakladania sie
    statusbar.rozmiesc(Prostokat(0, 0, 100, 24));
    for (size_t i = 0; i < statusbar.liczba_sekcji(); ++i) {
        bws_sprawdz(statusbar.prostokat_sekcji(i).szerokosc >= 0);
        bws_sprawdz(statusbar.prostokat_sekcji(i).prawy() <= 100);
    }
    bws_log_u64("[BURSZTYNUI DIAG] bledy_po_statusbar=", bws_test_blad - bledy_adv_przed);

    // 8. Podpowiedz (Tooltips) - Czas i brak alokacji
    ZarzadcaPodpowiedzi* zarzadca_tip_ptr = new ZarzadcaPodpowiedzi();
    bws_sprawdz(zarzadca_tip_ptr != nullptr);
    ZarzadcaPodpowiedzi& zarzadca_tip = *zarzadca_tip_ptr;
    zarzadca_tip.ustaw_opoznienie_pokazania(500); // 500 ms
    Przycisk* btn_tip_target_ptr = new Przycisk("Przycisk z podpowiedzia");
    btn_tip_target_ptr->ustaw_podpowiedz("To jest pomocny opis kontrolki w UTF-8: Zażółć gęślą jaźń.");
    btn_tip_target_ptr->rozmiesc(Prostokat(20, 20, 160, 30));

    // Wejscie myszy: czas < 500 ms -> podpowiedz sie NIE otwiera
    zarzadca_tip.obsluz_ruch(popupy, btn_tip_target_ptr, WIN_X + 40, WIN_Y + 35, 1000);
    bws_sprawdz(!zarzadca_tip.czy_widoczna());
    // Mysz wychodzi przed uplywem opoznienia -> nadal zamknieta
    zarzadca_tip.obsluz_ruch(popupy, nullptr, WIN_X + 250, WIN_Y + 250, 1200);
    bws_sprawdz(!zarzadca_tip.czy_widoczna());

    // Ponowne wejscie i odczekanie > 500 ms -> podpowiedz sie otwiera
    zarzadca_tip.obsluz_ruch(popupy, btn_tip_target_ptr, WIN_X + 40, WIN_Y + 35, 2000);
    zarzadca_tip.obsluz_ruch(popupy, btn_tip_target_ptr, WIN_X + 41, WIN_Y + 35, 2550);
    bws_sprawdz(zarzadca_tip.czy_widoczna());

    // Stres test 100 cykli bez alokacji
    zarzadca_tip.ustaw_opoznienie_pokazania(0);
    uint64_t tip_alok_przed = gui_liczba_alokacji_sterty();
    for (int i = 0; i < 100; ++i) {
        zarzadca_tip.obsluz_ruch(popupy, btn_tip_target_ptr, WIN_X + 40, WIN_Y + 35, 3000 + i * 10);
        zarzadca_tip.obsluz_ruch(popupy, nullptr, WIN_X + 250, WIN_Y + 250, 3005 + i * 10);
    }
    uint64_t tip_alok_po = gui_liczba_alokacji_sterty();
    bws_sprawdz(tip_alok_po == tip_alok_przed);
    bws_log_u64("[PODPOWIEDZ POMIAR] tip_100_allocations=", tip_alok_po - tip_alok_przed);
    bws_log_u64("[BURSZTYNUI DIAG] bledy_po_podpowiedz=", bws_test_blad - bledy_adv_przed);

    delete zarzadca_tip_ptr;
    delete btn_tip_target_ptr;
    delete status_ptr;
    delete toolbar_ptr;
    delete drzewo_ptr;
    delete karty_ptr;
    delete scroll_outer_ptr;
    delete scroll_inner_ptr;
    delete btn_in_inner;
    delete page0;
    delete page1;
    delete page2;
    delete btn_page0;
    delete btn_page1;
    delete btn_page2;
    delete num_ptr;
    delete postep_ptr;
    bws_log("[BURSZTYNUI ROZSZERZONE] Wszystkie 8 kontrolek przetestowane: OK\n");

    delete okno_popup_ptr;
    delete memo_clip_ptr;
    delete okno_listy_ptr;
    delete lista_ptr;
    bws_log("[BURSZTYNUI MEMO] Edycja, historia, schowek i rendering: OK\n");
    if (bws_test_blad == bledy_bui_przed)
        bws_log("=== WSZYSTKIE TESTY BURSZTYNUI ZAKONCZONE SUKCESEM (PASS) ===\n\n");
    else
        bws_log("=== WYKRYTO BLEDY W TESTACH BURSZTYNUI (FAIL) ===\n\n");
}

static bool punkt_w_prostokacie(int px, int py, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    return px >= x && px < x + w && py >= y && py < y + h;
}

static bool punkt_w_obszarze(int lokalny_x, int lokalny_y,
                             const TestUiRect& obszar) {
    return punkt_w_prostokacie(lokalny_x, lokalny_y,
                               obszar.x, obszar.y, obszar.w, obszar.h);
}

static int minimum(int a, int b) {
    return a < b ? a : b;
}

static TestUiLayout oblicz_uklad_prezentacji() {
    TestUiLayout uklad{};
    uklad.client = {
        0,
        CHROME_H,
        WIN_W,
        WIN_H > CHROME_H ? WIN_H - CHROME_H : 0
    };

    const int content_x = uklad.client.x + CLIENT_PAD;
    const int content_w = uklad.client.w > 2 * CLIENT_PAD
        ? uklad.client.w - 2 * CLIENT_PAD
        : 0;
    const int content_bottom = uklad.client.y + uklad.client.h - CLIENT_PAD;

    const int preferowane_szerokosci[4] = {110, 100, 150, 120};
    const int preferowana_suma = 110 + 100 + 150 + 120;
    const int szerokosc_na_przyciski = content_w > 3 * GRID_GAP
        ? content_w - 3 * GRID_GAP
        : 0;

    int szerokosci[4] = {};
    if (szerokosc_na_przyciski >= preferowana_suma) {
        for (int i = 0; i < 4; ++i) szerokosci[i] = preferowane_szerokosci[i];
    } else if (szerokosc_na_przyciski > 0) {
        const int wspolna = szerokosc_na_przyciski / 4;
        int reszta = szerokosc_na_przyciski - wspolna * 4;
        for (int i = 0; i < 4; ++i) {
            szerokosci[i] = wspolna + (reszta > 0 ? 1 : 0);
            if (reszta > 0) --reszta;
        }
    }

    int x = content_x;
    const int toolbar_y = uklad.client.y + CLIENT_PAD;
    for (int i = 0; i < 4; ++i) {
        uklad.toolbar[i] = {x, toolbar_y, szerokosci[i], BUTTON_H};
        x += szerokosci[i] + GRID_GAP;
    }

    const int selector_y = toolbar_y + BUTTON_H + GRID_GAP;
    uklad.selector = {content_x, selector_y, minimum(content_w, 200), COMBO_H};

    const int status_bws_y = selector_y + COMBO_H + GRID_GAP;
    uklad.status_bws = {content_x, status_bws_y, content_w, STATUS_H};
    uklad.status_ui = {
        content_x,
        status_bws_y + STATUS_H + 4,
        content_w,
        STATUS_H
    };

    const int output_y = uklad.status_ui.y + STATUS_H + GRID_GAP;
    uklad.output = {
        content_x,
        output_y,
        content_w,
        content_bottom > output_y ? content_bottom - output_y : 0
    };
    return uklad;
}

static void rysuj_prostokat_lokalny(const TestUiRect& obszar,
                                    uint32_t kolor) {
    if (obszar.w <= 0 || obszar.h <= 0) return;
    gui_rysuj_prostokat(WIN_X + obszar.x, WIN_Y + obszar.y,
                        obszar.w, obszar.h, kolor);
}

static void rysuj_ramke_lokalna(const TestUiRect& obszar,
                                uint32_t kolor) {
    if (obszar.w <= 1 || obszar.h <= 1) return;
    gui_rysuj_prostokat(WIN_X + obszar.x, WIN_Y + obszar.y,
                        obszar.w, 1, kolor);
    gui_rysuj_prostokat(WIN_X + obszar.x,
                        WIN_Y + obszar.y + obszar.h - 1,
                        obszar.w, 1, kolor);
    gui_rysuj_prostokat(WIN_X + obszar.x, WIN_Y + obszar.y,
                        1, obszar.h, kolor);
    gui_rysuj_prostokat(WIN_X + obszar.x + obszar.w - 1,
                        WIN_Y + obszar.y, 1, obszar.h, kolor);
}

static void ogranicz_geometrie_okna() {
    if (WIN_W > work_w) WIN_W = work_w;
    if (WIN_H > work_h) WIN_H = work_h;
    if (WIN_W < 1) WIN_W = 1;
    if (WIN_H < 1) WIN_H = 1;
    int max_x = work_x + work_w - WIN_W;
    int max_y = work_y + work_h - WIN_H;
    if (max_x < work_x) max_x = work_x;
    if (max_y < work_y) max_y = work_y;
    if (WIN_X < work_x) WIN_X = work_x;
    if (WIN_Y < work_y) WIN_Y = work_y;
    if (WIN_X > max_x) WIN_X = max_x;
    if (WIN_Y > max_y) WIN_Y = max_y;
}

static void ogranicz_pozycje_okna(int* x, int* y) {
    if (!x || !y) return;
    int max_x = work_x + work_w - WIN_W;
    int max_y = work_y + work_h - WIN_H;
    if (max_x < work_x) max_x = work_x;
    if (max_y < work_y) max_y = work_y;
    *x = przytnij(*x, work_x, max_x);
    *y = przytnij(*y, work_y, max_y);
}

static bool przelacz_maksymalizacje_okna() {
    int nowy_x = 0;
    int nowy_y = 0;
    int nowy_w = 0;
    int nowy_h = 0;

    if (!zmaksymalizowane) {
        restore_x = WIN_X;
        restore_y = WIN_Y;
        restore_w = WIN_W;
        restore_h = WIN_H;
        nowy_x = work_x;
        nowy_y = work_y;
        nowy_w = work_w;
        nowy_h = work_h;
    } else {
        nowy_x = restore_x;
        nowy_y = restore_y;
        nowy_w = restore_w;
        nowy_h = restore_h;
    }

    if (!gui_ustaw_geometrie_okna(nowy_x, nowy_y, nowy_w, nowy_h)) {
        return false;
    }

    WIN_X = nowy_x;
    WIN_Y = nowy_y;
    WIN_W = nowy_w;
    WIN_H = nowy_h;
    zmaksymalizowane = !zmaksymalizowane;
    return true;
}

static void RysujInterfejs(bool wyczysc_warstwe) {
    if (wyczysc_warstwe) gui_odswiez_pulpit();
    if (aplikacja_zminimalizowana) return;

    const TestUiLayout uklad = oblicz_uklad_prezentacji();
    const char* const etykiety[4] = {
        "Otwórz", "Zamknij", "Maksymalizuj", "Minimalizuj"
    };

    // Klient zaczyna sie dokladnie pod standardowym chrome 28 px.
    rysuj_prostokat_lokalny(uklad.client, Kolory::BursztynowyCiemnyTlo);
    gui_rysuj_standardowa_belke(WIN_X, WIN_Y, WIN_W,
                                "Test BWS / BursztynUI",
                                zmaksymalizowane);

    for (int i = 0; i < 4; ++i) {
        const TestUiRect& p = uklad.toolbar[i];
        if (p.w <= 0) continue;
        RysujPrzycisk(WIN_X + p.x, WIN_Y + p.y, p.w, p.h,
                      Kolory::AkcentWcisniety, Kolory::TekstCiemny,
                      i == 2 && zmaksymalizowane ? "Przywroc" : etykiety[i]);
    }

    rysuj_prostokat_lokalny(uklad.selector, 0x00FFFFFFU);
    rysuj_ramke_lokalna(uklad.selector, Kolory::RamkaDomyslna);

    const char* const combo_items[] = {"Opcja 1", "Opcja 2", "Opcja 3"};
    const int combo_n = 3;
    const char* const combo_aktualny =
        combo_wybor_TComboBox_3_3 >= 0 && combo_wybor_TComboBox_3_3 < combo_n
            ? combo_items[combo_wybor_TComboBox_3_3]
            : "";
    gui_wypisz_tekst_kolor_skala(WIN_X + uklad.selector.x + 6,
                                  WIN_Y + uklad.selector.y + 4,
                                  Kolory::TekstCiemny, 1, combo_aktualny);
    if (uklad.selector.w >= 18) {
        gui_rysuj_prostokat(WIN_X + uklad.selector.x + uklad.selector.w - 18,
                            WIN_Y + uklad.selector.y, 18, uklad.selector.h,
                            Kolory::AkcentWcisniety);
        gui_wypisz_tekst_kolor_skala(
            WIN_X + uklad.selector.x + uklad.selector.w - 14,
            WIN_Y + uklad.selector.y + 4,
            Kolory::TekstCiemny, 1,
            combo_otwarty_TComboBox_3_3 ? "^" : "v");
    }

    gui_wypisz_tekst_kolor_skala(
        WIN_X + uklad.status_bws.x, WIN_Y + uklad.status_bws.y,
        wynik_bws_ok ? 0x0000CC66U : 0x00DD3333U, 1,
        wynik_bws_ok ? "BWS 61..83: PASS" : "BWS 61..83: FAIL");
    gui_wypisz_tekst_kolor_skala(
        WIN_X + uklad.status_ui.x, WIN_Y + uklad.status_ui.y,
        wynik_bursztyn_ui_ok ? 0x0000CC66U : 0x00DD3333U, 1,
        wynik_bursztyn_ui_ok ? "BursztynUI: PASS" : "BursztynUI: FAIL");

    rysuj_prostokat_lokalny(uklad.output, Kolory::PowierzchniaAlternatywna);
    rysuj_ramke_lokalna(uklad.output, Kolory::RamkaDomyslna);
    if (uklad.output.w > 20 && uklad.output.h >= 42) {
        gui_wypisz_tekst_kolor_skala(
            WIN_X + uklad.output.x + 10, WIN_Y + uklad.output.y + 8,
            Kolory::TekstBialy, 1, "Wynik testow uruchomionych przy starcie.");
        gui_wypisz_tekst_kolor_skala(
            WIN_X + uklad.output.x + 10, WIN_Y + uklad.output.y + 26,
            Kolory::TekstWyciszony, 1,
            komunikat_output);
    }

    // Lista jest popupem prezentacji i musi znalezc sie nad statusem/outputem.
    if (combo_otwarty_TComboBox_3_3 && uklad.selector.w > 0) {
        const TestUiRect lista = {
            uklad.selector.x,
            uklad.selector.y + uklad.selector.h,
            uklad.selector.w,
            combo_n * STATUS_H
        };
        rysuj_prostokat_lokalny(lista, 0x00FFFFFFU);
        rysuj_ramke_lokalna(lista, Kolory::RamkaDomyslna);
        for (int i = 0; i < combo_n; ++i) {
            const int iy = lista.y + i * STATUS_H;
            if (i == combo_wybor_TComboBox_3_3) {
                const TestUiRect zaznaczenie = {
                    lista.x + 1, iy, lista.w > 2 ? lista.w - 2 : 0, STATUS_H
                };
                rysuj_prostokat_lokalny(zaznaczenie, Kolory::ZaznaczenieTlo);
            }
            gui_wypisz_tekst_kolor_skala(
                WIN_X + lista.x + 6, WIN_Y + iy + 1,
                i == combo_wybor_TComboBox_3_3
                    ? Kolory::TekstBialy : Kolory::TekstCiemny,
                1, combo_items[i]);
        }
    }

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
    ogranicz_geometrie_okna();
    if (bws_utworz_warstwe(WIN_X, WIN_Y, WIN_W, WIN_H, 10) < 0) gui_zakoncz_aplikacje();
    gui_ustaw_przejecie_myszy(true);

    wykonaj_testy_bws_61_83();
    const uint32_t bledy_po_bws = bws_test_blad;
    wynik_bws_ok = bledy_po_bws == 0;

    wykonaj_testy_bursztyn_ui();
    wynik_bursztyn_ui_ok = bws_test_blad == bledy_po_bws;

    /*
     * Testy BursztynUI celowo wywoluja BWS61 przez Okno::przelacz_maksymalizacje().
     * Po ich zakonczeniu procesowa warstwa ma geometrie testowego Okna, a nie
     * ponizszej prezentacji. Przywracamy ja przed pierwszym rysowaniem, aby
     * wszystkie ekranowe wspolrzedne trafialy do wlasciwego bufora warstwy.
     */
    if (!gui_ustaw_geometrie_okna(WIN_X, WIN_Y, WIN_W, WIN_H)) {
        gui_zakoncz_aplikacje();
    }
    if (!gui_ustaw_tytul_okna("Test BWS / BursztynUI")) {
        gui_zakoncz_aplikacje();
    }

    bool wyjdz = false;
    RysujInterfejs(true);

    while (!wyjdz) {
        bws_zdarzenie zdarzenie{};
        if (!gui_czekaj_na_zdarzenie(&zdarzenie)) continue;

        if (zdarzenie.typ == BWS_ZDARZENIE_FOCUS && aplikacja_zminimalizowana) {
            aplikacja_zminimalizowana = false;
            RysujInterfejs(true);
            continue;
        }

        const int mx = zdarzenie.x;
        const int my = zdarzenie.y;
        const int lokalny_x = mx - WIN_X;
        const int lokalny_y = my - WIN_Y;
        const bool lewy = (zdarzenie.przyciski & 0x01U) != 0;
        const bool klik_lewy = zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN;
        const bool puszczenie_lewego = zdarzenie.typ == BWS_ZDARZENIE_MYSZ_UP;
        bool redraw = false;
        bool pelne_czyszczenie = false;

        if (aplikacja_zminimalizowana) continue;

        const TestUiLayout uklad = oblicz_uklad_prezentacji();
        bool combo_klik_obsluzony = false;
        if (klik_lewy && combo_otwarty_TComboBox_3_3) {
            const TestUiRect lista = {
                uklad.selector.x,
                uklad.selector.y + uklad.selector.h,
                uklad.selector.w,
                3 * STATUS_H
            };
            if (punkt_w_obszarze(lokalny_x, lokalny_y, lista)) {
                const int wybrany = (lokalny_y - lista.y) / STATUS_H;
                if (wybrany >= 0 && wybrany < 3) {
                    combo_wybor_TComboBox_3_3 = wybrany;
                }
                combo_klik_obsluzony = true;
            } else if (punkt_w_obszarze(lokalny_x, lokalny_y,
                                         uklad.selector)) {
                combo_klik_obsluzony = true;
            }
            combo_otwarty_TComboBox_3_3 = false;
            redraw = true;
            pelne_czyszczenie = true;
        }

        if (!combo_klik_obsluzony && klik_lewy &&
            punkt_w_prostokacie(lokalny_x, lokalny_y, 0, 0, WIN_W, WIN_H)) {
            const gui_akcja_belki akcja = gui_hit_test_belki(mx, my, WIN_X, WIN_Y, WIN_W);
            if (akcja == GUI_BELKA_MINIMALIZUJ) {
                aplikacja_zminimalizowana = gui_minimalizuj_okno();
                dragging = false;
                gui_ustaw_capture_myszy(false);
            } else if (akcja == GUI_BELKA_ZAMKNIJ) {
                wyjdz = true;
                dragging = false;
                gui_ustaw_capture_myszy(false);
            } else if (akcja == GUI_BELKA_MAKSYMALIZUJ) {
                if (!przelacz_maksymalizacje_okna()) gui_zakoncz_aplikacje();
                redraw = true;
                pelne_czyszczenie = true;
            } else if (akcja == GUI_BELKA_DRAG && !zmaksymalizowane) {
                dragging = true;
                gui_ustaw_capture_myszy(true);
                drag_off_x = mx - WIN_X; drag_off_y = my - WIN_Y;
            } else {
                if (punkt_w_obszarze(lokalny_x, lokalny_y, uklad.toolbar[3])) {
                    aplikacja_zminimalizowana = gui_minimalizuj_okno();
                    dragging = false;
                    gui_ustaw_capture_myszy(false);
                } else if (punkt_w_obszarze(lokalny_x, lokalny_y,
                                             uklad.toolbar[2])) {
                    if (!przelacz_maksymalizacje_okna()) gui_zakoncz_aplikacje();
                    redraw = true;
                    pelne_czyszczenie = true;
                } else if (punkt_w_obszarze(lokalny_x, lokalny_y,
                                             uklad.toolbar[1])) {
                    wyjdz = true;
                } else if (punkt_w_obszarze(lokalny_x, lokalny_y,
                                             uklad.selector)) {
                    combo_otwarty_TComboBox_3_3 = !combo_otwarty_TComboBox_3_3;
                    redraw = true;
                    pelne_czyszczenie = true;
                } else if (punkt_w_obszarze(lokalny_x, lokalny_y,
                                             uklad.toolbar[0])) {
                    komunikat_output = "Otworz: Bursztyn";
                    redraw = true;
                    pelne_czyszczenie = true;
                } else if (punkt_w_obszarze(lokalny_x, lokalny_y,
                                             uklad.output)) {
                    komunikat_output = "Kliknieto obszar wynikow.";
                    redraw = true;
                    pelne_czyszczenie = true;
                }
            }
        }

        if (dragging && lewy) {
            int nowy_x = mx - drag_off_x;
            int nowy_y = my - drag_off_y;
            ogranicz_pozycje_okna(&nowy_x, &nowy_y);
            if (nowy_x != WIN_X || nowy_y != WIN_Y) {
                WIN_X = nowy_x;
                WIN_Y = nowy_y;
                bws_przesun_warstwe(WIN_X, WIN_Y);
                gui_odswiez();
            }
        }
        if (puszczenie_lewego && dragging) {
            dragging = false;
            gui_ustaw_capture_myszy(false);
        }
        if (redraw && !wyjdz) RysujInterfejs(pelne_czyszczenie);
    }
    gui_zakoncz_aplikacje();
}
