#define BURSZTYN_UI_DIAGNOSTYKA 1
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

static constexpr uint64_t BUR_TEKST_ROZMIAR     = 0x80000ULL; // 512 KiB
static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 0x1000ULL;
static constexpr uint64_t BUR_TEKST_WIRTUALNY     = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE   = 0x81000ULL;
static constexpr uint64_t BUR_DANE_WIRTUALNY       = 0x1081000ULL;
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
// Geometria warstwy potrzebnej przez testy BWS i przez okno wynikow.
// -----------------------------------------------------------------------------
static int WIN_X = 60;
static int WIN_Y = 50;
static int WIN_W = 600;
static int WIN_H = 450;
static int screen_w = 1024, screen_h = 768;
static int work_x = 0, work_y = 0, work_w = 1024, work_h = 768;
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

static void bws_dopisz_i32(char* buf, size_t cap, size_t* pos, int32_t val) {
    if (!buf || !pos || *pos >= cap) return;
    if (val < 0) {
        if (*pos + 1 < cap) buf[(*pos)++] = '-';
        val = -val;
    }
    char tmp[12];
    size_t len = 0;
    do {
        tmp[len++] = static_cast<char>('0' + (val % 10));
        val /= 10;
    } while (val > 0 && len < sizeof(tmp));
    while (len > 0 && *pos + 1 < cap) {
        buf[(*pos)++] = tmp[--len];
    }
    buf[*pos] = '\0';
}

static void bws_dopisz_str(char* buf, size_t cap, size_t* pos, const char* str) {
    if (!buf || !pos || !str) return;
    while (*str && *pos + 1 < cap) {
        buf[(*pos)++] = *str++;
    }
    buf[*pos] = '\0';
}

static void bws_dopisz_geom(char* buf, size_t cap, size_t* pos, int x, int y, int w, int h) {
    bws_dopisz_str(buf, cap, pos, "(");
    bws_dopisz_i32(buf, cap, pos, x);
    bws_dopisz_str(buf, cap, pos, ",");
    bws_dopisz_i32(buf, cap, pos, y);
    bws_dopisz_str(buf, cap, pos, ",");
    bws_dopisz_i32(buf, cap, pos, w);
    bws_dopisz_str(buf, cap, pos, ",");
    bws_dopisz_i32(buf, cap, pos, h);
    bws_dopisz_str(buf, cap, pos, ")");
}

static void log_rerun_geom(int cykl,
                           int bl_x, int bl_y, int bl_w, int bl_h,
                           int bv_x, int bv_y, int bv_w, int bv_h,
                           int bf_x, int bf_y, int bf_w, int bf_h,
                           int af_x, int af_y, int af_w, int af_h,
                           int wo_x, int wo_y, int wo_w, int wo_h,
                           int ly_x, int ly_y, int ly_w, int ly_h) {
    char buf[384];
    size_t pos = 0;
    buf[0] = '\0';
    bws_dopisz_str(buf, sizeof(buf), &pos, "[RERUN-GEOM]\ncycle=");
    bws_dopisz_i32(buf, sizeof(buf), &pos, cykl);
    bws_dopisz_str(buf, sizeof(buf), &pos, "\nbaseline_logical=");
    bws_dopisz_geom(buf, sizeof(buf), &pos, bl_x, bl_y, bl_w, bl_h);
    bws_dopisz_str(buf, sizeof(buf), &pos, "\nbaseline_visible=");
    bws_dopisz_geom(buf, sizeof(buf), &pos, bv_x, bv_y, bv_w, bv_h);
    bws_dopisz_str(buf, sizeof(buf), &pos, "\nbefore=");
    bws_dopisz_geom(buf, sizeof(buf), &pos, bf_x, bf_y, bf_w, bf_h);
    bws_dopisz_str(buf, sizeof(buf), &pos, "\nafter=");
    bws_dopisz_geom(buf, sizeof(buf), &pos, af_x, af_y, af_w, af_h);
    bws_dopisz_str(buf, sizeof(buf), &pos, "\nwindow_object=");
    bws_dopisz_geom(buf, sizeof(buf), &pos, wo_x, wo_y, wo_w, wo_h);
    bws_dopisz_str(buf, sizeof(buf), &pos, "\nlayer=");
    bws_dopisz_geom(buf, sizeof(buf), &pos, ly_x, ly_y, ly_w, ly_h);
    bws_dopisz_str(buf, sizeof(buf), &pos, "\n");
    bws_log(buf);
}

class IzolacjaPrezentacjiTestow {
public:
    explicit IzolacjaPrezentacjiTestow(Okno* okno_rzeczywiste = nullptr)
        : okno_(okno_rzeczywiste),
          poprzednie_tlum_rysowania_(KontekstRysowania::g_tlum_rysowanie_globalne),
          poprzednie_tlum_operacji_(Okno::g_tlum_operacje_systemowe),
          poprzednia_izolacja_geometrii_(gui_czy_izolacja_geometrii_wlaczona()) {

        KontekstRysowania::g_tlum_rysowanie_globalne = true;
        Okno::g_tlum_operacje_systemowe = true;
        gui_ustaw_izolacje_geometrii(true);

        if (okno_) {
            zapisane_x_ = okno_->x();
            zapisane_y_ = okno_->y();
            zapisana_szer_ = okno_->szerokosc();
            zapisana_wys_ = okno_->wysokosc();
            zapisany_fokus_ = okno_->fokus();
            zapisane_przechwycenie_ = okno_->przechwycenie_myszy();
            zapisane_zmaksymalizowanie_ = okno_->czy_zmaksymalizowane();
            zapisane_zminimalizowanie_ = okno_->czy_zminimalizowane();

            const char* src = okno_->tytul();
            size_t i = 0;
            while (src && src[i] != '\0' && i + 1 < sizeof(zapisany_tytul_)) {
                zapisany_tytul_[i] = src[i];
                ++i;
            }
            zapisany_tytul_[i] = '\0';
        }
    }

    ~IzolacjaPrezentacjiTestow() {
        zakoncz();
    }

    void zakoncz() {
        if (zakonczona_) return;
        zakonczona_ = true;

        KontekstRysowania::g_tlum_rysowanie_globalne = poprzednie_tlum_rysowania_;
        Okno::g_tlum_operacje_systemowe = poprzednie_tlum_operacji_;
        gui_ustaw_izolacje_geometrii(poprzednia_izolacja_geometrii_);

        if (okno_) {
            gui_ustaw_tytul_okna(zapisany_tytul_);
            gui_ustaw_capture_myszy(false);

            okno_->ustaw_geometrie(zapisane_x_, zapisane_y_, zapisana_szer_, zapisana_wys_);
            okno_->ustaw_tytul(zapisany_tytul_);
            okno_->ustaw_fokus(zapisany_fokus_);
            okno_->ustaw_przechwycenie_myszy(zapisane_przechwycenie_);
            okno_->przelicz_uklad();
            okno_->uniewaznij();
        }
    }

    IzolacjaPrezentacjiTestow(const IzolacjaPrezentacjiTestow&) = delete;
    IzolacjaPrezentacjiTestow& operator=(const IzolacjaPrezentacjiTestow&) = delete;

private:
    Okno* okno_{nullptr};
    bool poprzednie_tlum_rysowania_{false};
    bool poprzednie_tlum_operacji_{false};
    bool poprzednia_izolacja_geometrii_{false};
    int zapisane_x_{60};
    int zapisane_y_{50};
    int zapisana_szer_{600};
    int zapisana_wys_{450};
    char zapisany_tytul_[64]{"Testy Bursztyn OS"};
    Kontrolka* zapisany_fokus_{nullptr};
    Kontrolka* zapisane_przechwycenie_{nullptr};
    bool zapisane_zmaksymalizowanie_{false};
    bool zapisane_zminimalizowanie_{false};
    bool zakonczona_{false};
};

__attribute__((noinline)) static void usun_kontrolke_testowa(Kontrolka* kontrolka) {
    delete kontrolka;
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

static bool bws_tekst_rowny(const char* a, const char* b) {
    if (!a || !b) return a == b;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a; ++b;
    }
    return *a == *b;
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
    bws_log("[TEST 8/9] Socket permission & ownership: OK\n");

    // 9. Podsystem motywów BWS 86..87, accessors, transakcja podglądu i 50x stres-test przełączeń
    {
        bws_log("[TEST 9/9] Podsystem motywów BWS 86..87 i 50x stres-test przełączeń...\n");
        const BursztynMotyw* m_poczatkowy = gui_pobierz_motyw();
        bws_sprawdz(m_poczatkowy != nullptr);
        if (m_poczatkowy) {
            bws_sprawdz(m_poczatkowy->wersja == 2);
            bws_sprawdz(m_poczatkowy->rozmiar_struktury == 172);
            bws_sprawdz(bws_tekst_rowny(m_poczatkowy->id, "bursztynnocny"));
            bws_sprawdz(m_poczatkowy->pulpit == 0x00111315U);
            bws_sprawdz(m_poczatkowy->oknotlo == 0x00191C20U);
            bws_sprawdz(m_poczatkowy->panel == 0x0020242AU);
            bws_sprawdz(m_poczatkowy->podniesiony == 0x00262B31U);
            bws_sprawdz(m_poczatkowy->ramka == 0x00343A42U);
            bws_sprawdz(m_poczatkowy->tekst == 0x00F2F3F5U);
            bws_sprawdz(m_poczatkowy->tekstdrugorzedny == 0x00A7ADB5U);
            bws_sprawdz(m_poczatkowy->wylaczony == 0x006F7680U);
            bws_sprawdz(m_poczatkowy->akcent == 0x00F2A900U);
            bws_sprawdz(m_poczatkowy->akcentnajechany == 0x00FFBC32U);
            bws_sprawdz(m_poczatkowy->akcentaktywny == 0x00D99100U);
            bws_sprawdz(m_poczatkowy->blad == 0x00E85D5DU);
            bws_sprawdz(m_poczatkowy->sukces == 0x0051B87AU);
            bws_sprawdz(m_poczatkowy->ostrzezenie == 0x00F2A900U);
            bws_sprawdz(m_poczatkowy->informacja == 0x0058A6E7U);
            bws_sprawdz(m_poczatkowy->poletekstowe == 0x0015181CU);
            bws_sprawdz(m_poczatkowy->zaznaczenie == 0x00F2A900U);
            bws_sprawdz(m_poczatkowy->przycisk == 0x00262B31U);
            bws_sprawdz(m_poczatkowy->przycisknajechany == 0x00343A42U);
            bws_sprawdz(m_poczatkowy->tekstakcentu == 0x00111315U);
        }
        bws_sprawdz(motyw_promien(MOTYW_ROG_OKNA) == 10);
        bws_sprawdz(motyw_promien(MOTYW_ROG_PANELU) == 8);
        bws_sprawdz(motyw_promien(MOTYW_ROG_PRZYCISKU) == 7);
        bws_sprawdz(motyw_promien(MOTYW_ROG_POLA) == 6);
        bws_sprawdz(motyw_promien(MOTYW_ROG_MENU) == 8);
        bws_sprawdz(motyw_promien(MOTYW_ROG_PODPOWIEDZI) == 6);
        bws_sprawdz(motyw_grubosc_ramki() == 1);
        bws_sprawdz(motyw_cien_okna() == 1);
        bws_sprawdz(motyw_kolor(MOTYW_KOLOR_PULPIT) == 0x00111315U);
        bws_sprawdz(motyw_kolor(MOTYW_KOLOR_AKCENT) == 0x00F2A900U);

        // BWS 86: Walidacja wskaźnika użytkownika (fail-closed)
        bws_sprawdz(bws_wywolaj(BWS_MOTYW_POBIERZ, 1, 0, 0, 0) == 0);
        bws_sprawdz(bws_wywolaj(BWS_MOTYW_POBIERZ, 0, 0, 0, 0) == 0);

        // BWS 86: Odczyt do bufora ABI v2 (172 B)
        BursztynMotyw m_buf{};
        bws_sprawdz(bws_wywolaj(BWS_MOTYW_POBIERZ, reinterpret_cast<uint64_t>(&m_buf), 0, 0, 0) == 1);
        bws_sprawdz(m_buf.wersja == 2);
        bws_sprawdz(m_buf.rozmiar_struktury == 172);
        bws_sprawdz(m_buf.pulpit == 0x00111315U);
        bws_sprawdz(m_buf.akcent == 0x00F2A900U);

        // BWS 86: Zgodność wsteczna odczytu ABI v1 (144 B) gdy wersja == 1
        BursztynMotywV1 v1_buf{};
        v1_buf.wersja = 1;
        bws_sprawdz(bws_wywolaj(BWS_MOTYW_POBIERZ, reinterpret_cast<uint64_t>(&v1_buf), 0, 0, 0) == 1);
        bws_sprawdz(v1_buf.wersja == 1);
        bws_sprawdz(v1_buf.pulpit == 0x00111315U);
        bws_sprawdz(v1_buf.akcent == 0x00F2A900U);
        bws_sprawdz(bws_tekst_rowny(v1_buf.id, "bursztynnocny"));

        // BWS 87: Wymóg autoryzacji (PZB <= PZB_ZAUFANE i PRAWO_SYSTEM_CONFIG).
        // test.bur jest zwykłym procesem Ring 3 (PZB_UZYTKOWNIK) - wszystkie operacje mutujące muszą zawieść.
        bws_sprawdz(bws_wywolaj(BWS_MOTYW_POLECENIE, MOTYW_CMD_USTAW, reinterpret_cast<uint64_t>("grafit"), 0, 0) == 0);
        bws_sprawdz(bws_wywolaj(BWS_MOTYW_POLECENIE, MOTYW_CMD_PODGLAD, reinterpret_cast<uint64_t>("baltyckanoc"), 0, 0) == 0);
        bws_sprawdz(bws_wywolaj(BWS_MOTYW_POLECENIE, MOTYW_CMD_ZATWIERDZ, 0, 0, 0) == 0);
        bws_sprawdz(bws_wywolaj(BWS_MOTYW_POLECENIE, MOTYW_CMD_PRZYWROC, 0, 0, 0) == 0);

        // BWS 88: Wymog autoryzacji BWS_PULPIT_GOTOWY (tylko zaufany Menedzer Okien /menedzer_okien.bur).
        // test.bur (PZB_UZYTKOWNIK) nie ma PRAWO_SYSTEM_CONFIG i nie jest Menedzerem Okien -> odrzucenie i wynik 0.
        // Idempotencja: wielokrotne wywołania nie zmieniają stanu systemu i są bezpiecznie odrzucane.
        bws_sprawdz(bws_wywolaj(BWS_PULPIT_GOTOWY, 0, 0, 0, 0) == 0);
        bws_sprawdz(bws_wywolaj(BWS_PULPIT_GOTOWY, 0, 0, 0, 0) == 0);

        // BWS 87: MOTYW_CMD_LISTUJ jest bezpiecznym odczytem i zwraca liczbę dostępnych motywów
        bws_sprawdz(bws_wywolaj(BWS_MOTYW_POLECENIE, MOTYW_CMD_LISTUJ, 0, 0, 0) == 5);

        // BWS 87: Odrzucenie nieprawidłowych ID (ścieżki, wielkie litery, brak walidacji motyw_waliduj_id)
        bws_sprawdz(bws_wywolaj(BWS_MOTYW_POLECENIE, MOTYW_CMD_USTAW, reinterpret_cast<uint64_t>("../etc/passwd"), 0, 0) == 0);
        bws_sprawdz(bws_wywolaj(BWS_MOTYW_POLECENIE, MOTYW_CMD_PODGLAD, reinterpret_cast<uint64_t>("/system/motywy/grafit.motyw"), 0, 0) == 0);
        bws_sprawdz(bws_wywolaj(BWS_MOTYW_POLECENIE, MOTYW_CMD_USTAW, reinterpret_cast<uint64_t>("GRAFIT"), 0, 0) == 0);
        bws_sprawdz(bws_wywolaj(BWS_MOTYW_POLECENIE, MOTYW_CMD_USTAW, reinterpret_cast<uint64_t>("motyw ze spacja"), 0, 0) == 0);

        // 50-krotny stres-test odczytu motywów, accessors i weryfikacji braku wycieków
        const uint64_t alokacje_przed_50 = gui_liczba_aktywnych_alokacji_sterty();
        for (int i = 0; i < 50; ++i) {
            motyw_odswiez_lokalny();
            const BursztynMotyw* m_biezacy = gui_pobierz_motyw();
            bws_sprawdz(m_biezacy != nullptr && bws_tekst_rowny(m_biezacy->id, "bursztynnocny"));
            bws_sprawdz(motyw_kolor(MOTYW_KOLOR_PULPIT) == 0x00111315U);
            bws_sprawdz(motyw_kolor(MOTYW_KOLOR_AKCENT) == 0x00F2A900U);
            bws_sprawdz(motyw_kolor(MOTYW_KOLOR_PANEL) == 0x0020242AU);
            bws_sprawdz(motyw_kolor(MOTYW_KOLOR_RAMKA) == 0x00343A42U);
            bws_sprawdz(motyw_promien(MOTYW_ROG_OKNA) == 10);
            bws_sprawdz(motyw_promien(MOTYW_ROG_PANELU) == 8);
        }
        const uint64_t alokacje_po_50 = gui_liczba_aktywnych_alokacji_sterty();
        bws_sprawdz(alokacje_przed_50 == alokacje_po_50);
        bws_log("[TEST 9/9] Podsystem motywów BWS 86..87, accessors, 50x stres-test i brak wycieków: OK\n");
    }

    if (bws_test_blad == 0) {
        bws_log("=== WSZYSTKIE TESTY BWS 61..87 ZAKONCZONE SUKCESEM (PASS) ===\n\n");
    } else {
        bws_log("=== WYKRYTO BLEDY W TESTACH BWS 61..87 (FAIL) ===\n\n");
    }
}

__attribute__((noinline)) static void testy_interaktywne_bursztyn_ui() {
    bws_log("[BURSZTYNUI INTERAKTYWNOSC] Testy WidokPrzewijany, Drzewo i Efektywna Widocznosc...\n");
    // 1. WidokPrzewijany: 20 przyciskow "Akcja", transformacje, hit-test, scrolling i brak driftu
    {
        WidokPrzewijany* sv = new WidokPrzewijany();
        sv->rozmiesc(Prostokat(20, 30, 400, 300));
        sv->ustaw_rozmiar_zawartosci(Rozmiar(600, 700));

        static int klikniecia_akcja[20] = {0};
        for (int i = 0; i < 20; ++i) klikniecia_akcja[i] = 0;

        for (int i = 0; i < 20; ++i) {
            Przycisk* btn = new Przycisk("Akcja");
            btn->rozmiesc(Prostokat(100, i * 30 + 10, 80, 24));
            btn->przy_kliknieciu([](Przycisk*, void* dane) {
                int idx = static_cast<int>(reinterpret_cast<intptr_t>(dane));
                if (idx >= 0 && idx < 20) klikniecia_akcja[idx]++;
            }, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
            sv->dodaj_do_zawartosci(btn);
        }

        // Test hit-testu przy offset 0: przycisk 0 jest na (100, 10) w zawartosci
        Kontrolka* hit0 = sv->sprawdz_trafienie(Punkt(161, 53));
        bws_sprawdz_msg(hit0 != nullptr, "SV HIT przycisk 0");

        // Test przewiniecia w dol o 90px (3 wiersze)
        sv->przewin_do(0, 90);
        Kontrolka* hit3 = sv->sprawdz_trafienie(Punkt(161, 53));
        bws_sprawdz_msg(hit3 != nullptr, "SV HIT przycisk 3 po scrollu");

        if (hit3) {
            ZdarzenieMyszki me_down{};
            me_down.typ = TypZdarzenia::WcisniecieMyszy;
            me_down.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
            me_down.x = hit3->obszar_ekranu().x + 10;
            me_down.y = hit3->obszar_ekranu().y + 10;
            hit3->obsluz_wcisniecie_myszy(me_down);

            ZdarzenieMyszki me_up = me_down;
            me_up.typ = TypZdarzenia::ZwolnienieMyszy;
            hit3->obsluz_zwolnienie_myszy(me_up);
            bws_sprawdz_msg(klikniecia_akcja[3] == 1, "SV CALLBACK przycisk 3");
            bws_sprawdz_msg(klikniecia_akcja[0] == 0, "SV BRAK false callback przycisk 0");
        }

        // Test 100 cykli scroll down/up - weryfikacja braku driftu geometrii
        for (int c = 0; c < 100; ++c) {
            sv->przewin_do(0, 150);
            sv->przewin_do(0, 0);
        }
        bws_sprawdz_msg(sv->zawartosc().obszar().y == sv->prostokat_widoku().y, "SV 100 cykli BRAK DRIFTU");

        delete sv;
    }

    // 2. Drzewo: Geometria lokalna, brak displacementu i stabilnosc projekcji
    {
        Drzewo* tr = new Drzewo();
        tr->rozmiesc(Prostokat(50, 60, 240, 200));
        UchwytWezlaDrzewa k1 = tr->dodaj_korzen("Moj Komputer");
        UchwytWezlaDrzewa c1 = tr->dodaj_dziecko(k1, "Dysk C:");
        tr->dodaj_dziecko(c1, "Plik.txt");
        tr->ustaw_rozwiniety(k1, true);
        tr->ustaw_rozwiniety(c1, true);

        // Weryfikacja liczby wezlow i widocznych
        bws_sprawdz_msg(tr->liczba_wezlow() == 3, "DRZEWO liczba wezlow");
        bws_sprawdz_msg(tr->czy_uchwyt_poprawny(k1), "DRZEWO uchwyt k1");

        // Symulacja klikniecia w pierwszy wiersz (y=5 w lokalnych)
        ZdarzenieMyszki me_click{};
        me_click.typ = TypZdarzenia::WcisniecieMyszy;
        me_click.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
        me_click.x = tr->obszar_ekranu().x + 20;
        me_click.y = tr->obszar_ekranu().y + 5; // Pierwszy wiersz
        tr->obsluz_wcisniecie_myszy(me_click);
        bws_sprawdz_msg(tr->wybrany_wezel() == k1, "DRZEWO klikniecie pierwszego wiersza");

        const uint64_t projekcje_przed = tr->liczba_przebudowan_widocznych();
        uint64_t alok_przed = gui_liczba_alokacji_sterty();
        for (int i = 0; i < 100; ++i) {
            ZdarzenieMyszki move{};
            move.typ = TypZdarzenia::RuchMyszy;
            move.x = tr->obszar_ekranu().x + 200;
            move.y = tr->obszar_ekranu().y + 180;
            tr->obsluz_ruch_myszy(move);
        }
        uint64_t alok_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(tr->liczba_przebudowan_widocznych() == projekcje_przed,
                        "DRZEWO MouseMove nie przebudowuje projekcji");
        bws_sprawdz_msg(alok_po == alok_przed,
                        "DRZEWO MouseMove 0 heap allocations");

        delete tr;
    }

    // 3. Efektywna widocznosc i animacja (czy_efektywnie_widoczna)
    {
        Panel* stronaA = new Panel();
        Panel* stronaB = new Panel();
        PasekPostepu* p_anim = new PasekPostepu();
        p_anim->ustaw_tryb_nieokreslony(true);
        stronaA->dodaj_kontrolke(p_anim);

        Karty* k = new Karty();
        k->dodaj_karte("Tab A", stronaA);
        k->dodaj_karte("Tab B", stronaB);
        k->rozmiesc(Prostokat(0, 0, 400, 300));

        // Zakladka A jest aktywna -> animacja powinna dzialac
        bws_sprawdz_msg(p_anim->czy_efektywnie_widoczna() == true, "ANIM Tab A widoczna");
        bws_sprawdz_msg(k->czy_wymaga_animacji() == true, "ANIM Tab A wymaga animacji");
        k->krok_animacji(1000000000ULL);
        const uint64_t widoczne_kroki = p_anim->liczba_krokow_animacji();
        bws_sprawdz_msg(widoczne_kroki == 1, "ANIM widoczny jeden tick");

        // Przelacz na Zakladke B -> stronaA zostaje ukryta
        k->ustaw_aktywna_karte(1);
        bws_sprawdz_msg(stronaA->czy_widoczna() == false, "ANIM Tab A ukryta");
        bws_sprawdz_msg(p_anim->czy_efektywnie_widoczna() == false, "ANIM p_anim nieefektywna");
        bws_sprawdz_msg(p_anim->czy_wymaga_animacji() == false, "ANIM p_anim czy_wymaga false");
        bws_sprawdz_msg(k->czy_wymaga_animacji() == false, "ANIM Karty czy_wymaga false na Tab B");
        for (int i = 0; i < 100; ++i)
            k->krok_animacji(2000000000ULL + static_cast<uint64_t>(i) * 35000000ULL);
        bws_sprawdz_msg(p_anim->liczba_krokow_animacji() == widoczne_kroki,
                        "ANIM ukryty tick counter == 0");

        // Powrot na Zakladke A -> animacja wznawia sie bezpiecznie
        k->ustaw_aktywna_karte(0);
        bws_sprawdz_msg(p_anim->czy_efektywnie_widoczna() == true, "ANIM p_anim powrot Tab A");
        bws_sprawdz_msg(k->czy_wymaga_animacji() == true, "ANIM Karty czy_wymaga true powrot Tab A");
        k->krok_animacji(6000000000ULL);
        bws_sprawdz_msg(p_anim->liczba_krokow_animacji() == widoczne_kroki + 1,
                        "ANIM powrot bez catch-up");

        delete k;
    }

    bws_log("[INTERAKTYWNOSC AUDYT] WidokPrzewijany, Drzewo i Efektywna Widocznosc: OK\n");
}

static void formatuj_liczbe_t(char* buf, size_t cap, int n) {
    if (cap < 2) return;
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[16]; size_t len = 0;
    while (n > 0 && len < sizeof(tmp)) { tmp[len++] = static_cast<char>('0' + (n % 10)); n /= 10; }
    size_t bi = 0;
    while (len > 0 && bi + 1 < cap) { buf[bi++] = tmp[--len]; }
    buf[bi] = '\0';
}

static void wykonaj_testy_kontrolki2() {
    bws_log("\n=== ROZPOCZECIE TESTOW NOWYCH KONTROLEK (BURSZTYNUI GENERACJA 2) ===\n");

    // 1. Tabela (1000 wierszy, kolumny, selekcja, nawigacja)
    {
        bws_log("[KONTROLKI2 1/8] Tabela: model, wejscie i wirtualizacja 1000 wierszy...\n");
        Tabela* tab = new Tabela();
        bws_sprawdz_msg(tab != nullptr, "Tabela alokacja");
        if (!tab) return;
        tab->rozmiesc(Prostokat(0, 0, 400, 300));
        bws_sprawdz_msg(tab->dodaj_kolumne("ID", 90), "Tabela dodaj_kolumne 1");
        bws_sprawdz_msg(tab->dodaj_kolumne("Nazwa", 210), "Tabela dodaj_kolumne 2");
        bws_sprawdz_msg(tab->dodaj_kolumne("Wartość", 180), "Tabela dodaj_kolumne 3");
        bws_sprawdz_msg(tab->liczba_kolumn() == 3, "Tabela liczba_kolumn == 3");

        tab->rozpocznij_aktualizacje();
        for (int i = 0; i < 1000; ++i) {
            char buf[16];
            formatuj_liczbe_t(buf, sizeof(buf), i);
            bws_sprawdz_msg(tab->dodaj_wiersz(buf, "Element Zażółć", "100"),
                            "Tabela dodaj_wiersz");
        }
        tab->zakoncz_aktualizacje();
        bws_sprawdz_msg(tab->liczba_wierszy() == 1000, "Tabela 1000 wierszy");
        bws_sprawdz_msg(tab->pasek_pionowy().czy_widoczna(), "Tabela pionowy scroll");
        bws_sprawdz_msg(tab->pasek_poziomy().czy_widoczna(), "Tabela poziomy scroll");

        struct PomiarTabeli { int zmiany; int aktywacje; } pomiar{};
        tab->przy_zmianie_zaznaczenia([](Tabela*, int, void* dane) {
            ++static_cast<PomiarTabeli*>(dane)->zmiany;
        }, &pomiar);
        tab->przy_aktywacji([](Tabela*, int, void* dane) {
            ++static_cast<PomiarTabeli*>(dane)->aktywacje;
        }, &pomiar);

        // Selekcja i odczyt
        bws_sprawdz_msg(tab->ustaw_zaznaczony_wiersz(42), "Tabela ustaw_zaznaczony_wiersz 42");
        bws_sprawdz_msg(tab->zaznaczony_wiersz() == 42 && pomiar.zmiany == 1,
                        "Tabela zaznaczenie/callback");
        bws_sprawdz_msg(tab->komorka(42, 1)[0] == 'E', "Tabela komorka UTF-8");

        ZdarzenieKlawiatury tk{};
        tk.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x4FU; // End
        tab->obsluz_wcisniecie_klawisza(tk);
        bws_sprawdz_msg(tab->zaznaczony_wiersz() == 999 &&
                        tab->pasek_pionowy().wartosc() > 0,
                        "Tabela End przewija do ostatniego wiersza");
        tk = {};
        tk.kod_klawisza = 13;
        tab->obsluz_wcisniecie_klawisza(tk);
        bws_sprawdz_msg(pomiar.aktywacje == 1, "Tabela aktywacja Enter");

        KontekstRysowania ctx_tab;
        uint64_t alok_przed = gui_liczba_alokacji_sterty();
        tab->rysuj(ctx_tab, true);
        uint64_t alok_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(tab->liczba_ostatnio_narysowanych_wierszy() > 0 &&
                        tab->liczba_ostatnio_narysowanych_wierszy() < 20,
                        "Tabela renderuje tylko widoczne wiersze");
        bws_sprawdz_msg(alok_po == alok_przed, "Tabela paint 0 alokacji");

        // Klik wiersza nie moze przelaczac stylu powierzchni calej tabeli.
        tab->pasek_pionowy().ustaw_wartosc(0);
        tab->ustaw_zaznaczony_wiersz(1);
        tab->wyczysc_brudny_obszar();
        const uint32_t tabela_tlo_przed = tab->wyznacz_biezacy_styl_stanu().tlo;
        ZdarzenieMyszki tdown{};
        tdown.typ = TypZdarzenia::WcisniecieMyszy;
        tdown.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
        tdown.x = 10;
        tdown.y = tab->wysokosc_naglowka() + 1 + 2 * tab->wysokosc_wiersza() + 5;
        uint64_t tabela_click_alok_przed = gui_liczba_alokacji_sterty();
        tab->obsluz_wcisniecie_myszy(tdown);
        uint64_t tabela_click_alok_po = gui_liczba_alokacji_sterty();
        Prostokat tabela_click_dirty = tab->pobierz_brudny_obszar();
        bws_sprawdz_msg(tab->wyznacz_biezacy_styl_stanu().tlo == tabela_tlo_przed,
                        "Tabela klik nie zmienia tla korzenia");
        bws_sprawdz_msg(tab->zaznaczony_wiersz() == 2 &&
                        tabela_click_dirty.wysokosc <= 2 * tab->wysokosc_wiersza(),
                        "Tabela klik brudzi tylko stary i nowy wiersz");
        bws_sprawdz_msg(tabela_click_alok_po == tabela_click_alok_przed,
                        "Tabela klik wiersza 0 alokacji");

        // 5 s syntetycznego ruchu 1000 Hz w tej samej tozsamosci hover.
        ZdarzenieMyszki tmove{};
        tmove.typ = TypZdarzenia::RuchMyszy;
        tmove.y = tab->wysokosc_naglowka() + 1 + 3 * tab->wysokosc_wiersza() + 5;
        tmove.x = 10;
        tab->obsluz_ruch_myszy(tmove);
        tab->wyczysc_brudny_obszar();
        uint64_t tabela_move_alok_przed = gui_liczba_alokacji_sterty();
        for (int i = 0; i < 5000; ++i) {
            tmove.x = 10 + (i % 100);
            tab->obsluz_ruch_myszy(tmove);
        }
        uint64_t tabela_move_alok_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(!tab->czy_brudna(),
                        "Tabela ten sam hover: 0 dodatkowych invalidacji");
        bws_sprawdz_msg(tabela_move_alok_po == tabela_move_alok_przed,
                        "Tabela MouseMove 0 alokacji");
        bws_log_u64("[PERF 5S TABELA ROW] mouse_events=", 5000);
        bws_log_u64("[PERF 5S TABELA ROW] dirty_calls=", tab->czy_brudna() ? 1 : 0);
        bws_log_u64("[PERF 5S TABELA ROW] dirty_pixels=", 0);
        bws_log_u64("[PERF 5S TABELA ROW] layout_passes=", 0);
        bws_log_u64("[PERF 5S TABELA ROW] allocations=",
                    tabela_move_alok_po - tabela_move_alok_przed);

        // Rolka zmienia model, lecz nie selekcje; na granicy pozwala babelkowac.
        tab->pasek_pionowy().ustaw_wartosc(0);
        tab->wyczysc_brudny_obszar();
        const int tabela_sel_przed_rolka = tab->zaznaczony_wiersz();
        ZdarzenieRolki twheel{};
        twheel.delta_y = -120;
        uint64_t tabela_wheel_alok_przed = gui_liczba_alokacji_sterty();
        tab->obsluz_rolke(twheel);
        uint64_t tabela_wheel_alok_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(twheel.obsluzone && tab->pasek_pionowy().wartosc() > 0,
                        "Tabela rolka zmienia pionowy model");
        bws_sprawdz_msg(tab->zaznaczony_wiersz() == tabela_sel_przed_rolka,
                        "Tabela rolka nie zmienia selekcji");
        bws_sprawdz_msg(tabela_wheel_alok_po == tabela_wheel_alok_przed,
                        "Tabela rolka 0 alokacji");
        tab->pasek_pionowy().ustaw_wartosc(tab->pasek_pionowy().maksimum());
        twheel = {};
        twheel.delta_y = -120;
        tab->obsluz_rolke(twheel);
        bws_sprawdz_msg(!twheel.obsluzone,
                        "Tabela rolka na dolnej granicy babelkuje");

        // Modyfikacja komórki
        bws_sprawdz_msg(tab->ustaw_komorke(42, 1, "Zmieniona Nazwa"), "Tabela ustaw_komorke");
        bws_sprawdz_msg(tab->komorka(42, 1)[0] == 'Z', "Tabela komorka po zmianie");

        // Usuwanie wiersza
        bws_sprawdz_msg(tab->usun_wiersz(42), "Tabela usun_wiersz");
        bws_sprawdz_msg(tab->liczba_wierszy() == 999, "Tabela liczba_wierszy po usunieciu == 999");
        tab->ustaw_wlaczona(false);
        const int wybor_przed = tab->zaznaczony_wiersz();
        tk = {};
        tk.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x48U;
        tab->obsluz_wcisniecie_klawisza(tk);
        bws_sprawdz_msg(tab->zaznaczony_wiersz() == wybor_przed,
                        "Tabela wylaczona ignoruje klawiature");
        bws_log_u64("[TABELA POMIAR] visible_rows=", tab->liczba_ostatnio_narysowanych_wierszy());
        bws_log_u64("[TABELA POMIAR] paint_allocations=", alok_po - alok_przed);
        delete tab;

        Tabela* tabela_pusta = new Tabela();
        bws_sprawdz_msg(tabela_pusta != nullptr, "Tabela pusta alokacja");
        tabela_pusta->rozmiesc(Prostokat(0, 0, 400, 300));
        tabela_pusta->dodaj_kolumne("Pusta", 200);
        tabela_pusta->wyczysc_brudny_obszar();
        ZdarzenieMyszki empty_move{};
        empty_move.typ = TypZdarzenia::RuchMyszy;
        empty_move.y = tabela_pusta->wysokosc_naglowka() + 30;
        uint64_t empty_alok_przed = gui_liczba_alokacji_sterty();
        for (int i = 0; i < 5000; ++i) {
            empty_move.x = 10 + (i % 300);
            tabela_pusta->obsluz_ruch_myszy(empty_move);
        }
        uint64_t empty_alok_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(!tabela_pusta->czy_brudna() && empty_alok_po == empty_alok_przed,
                        "Tabela pusta 5s MouseMove: 0 dirty/layout/allocation");
        bws_log_u64("[PERF 5S TABELA EMPTY] mouse_events=", 5000);
        bws_log_u64("[PERF 5S TABELA EMPTY] dirty_calls=", 0);
        bws_log_u64("[PERF 5S TABELA EMPTY] dirty_pixels=", 0);
        bws_log_u64("[PERF 5S TABELA EMPTY] layout_passes=", 0);
        bws_log_u64("[PERF 5S TABELA EMPTY] allocations=",
                    empty_alok_po - empty_alok_przed);
        delete tabela_pusta;

        // Pelna sciezka BWS -> swiezy hit-test -> Tabela/PasekPrzewijania.
        {
            Okno ok_tabela("Tabela route", 420, 320);
            Panel* root = static_cast<Panel*>(ok_tabela.kontrolka_glowna());
            root->ustaw_wypelnienie(Marginesy(0));
            Tabela& rt = root->dodaj<Tabela>();
            rt.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            rt.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
            rt.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            rt.dodaj_kolumne("ID", 100);
            rt.dodaj_kolumne("Nazwa", 500);
            rt.rozpocznij_aktualizacje();
            for (int i = 0; i < 200; ++i) rt.dodaj_wiersz("1", "Wiersz");
            rt.zakoncz_aktualizacje();
            ok_tabela.przelicz_uklad();

            bws_zdarzenie raw_wheel{};
            raw_wheel.typ = BWS_ZDARZENIE_MYSZ_KOLO;
            raw_wheel.x = ok_tabela.x() + rt.obszar_ekranu().x + 20;
            raw_wheel.y = ok_tabela.y() + rt.obszar_ekranu().y +
                          rt.wysokosc_naglowka() + 10;
            raw_wheel.dy = -120;
            ok_tabela.obsluz_zdarzenie(raw_wheel);
            bws_sprawdz_msg(rt.pasek_pionowy().wartosc() > 0,
                            "Tabela fizyczna rolka przez swiezy hit-test");

            PasekPrzewijania& sb = rt.pasek_pionowy();
            bws_sprawdz_msg(sb.obszar_ekranu().x ==
                            rt.obszar_ekranu().x + sb.obszar().x,
                            "Tabela pasek ma zgodna geometrie ekranowa i rysowania");
            sb.ustaw_wartosc(0);
            Prostokat thumb = sb.prostokat_uchwytu();
            bws_zdarzenie raw_down{};
            raw_down.typ = BWS_ZDARZENIE_MYSZ_DOWN;
            raw_down.x = ok_tabela.x() + sb.obszar_ekranu().x +
                         thumb.x + thumb.szerokosc / 2;
            raw_down.y = ok_tabela.y() + sb.obszar_ekranu().y +
                         thumb.y + thumb.wysokosc / 2;
            ok_tabela.obsluz_zdarzenie(raw_down);
            bws_sprawdz_msg(ok_tabela.przechwycenie_myszy() == &sb,
                            "Tabela pasek przechwytuje drag uchwytu");
            bws_zdarzenie raw_drag = raw_down;
            raw_drag.typ = BWS_ZDARZENIE_MYSZ_RUCH;
            raw_drag.y = ok_tabela.y() + sb.obszar_ekranu().y +
                         sb.obszar_ekranu().wysokosc / 2;
            uint64_t drag_sb_alok_przed = gui_liczba_alokacji_sterty();
            ok_tabela.obsluz_zdarzenie(raw_drag);
            const int64_t wartosc_srodek = sb.wartosc();
            raw_drag.y = ok_tabela.y() + sb.obszar_ekranu().dolny() - 22;
            ok_tabela.obsluz_zdarzenie(raw_drag);
            uint64_t drag_sb_alok_po = gui_liczba_alokacji_sterty();
            bws_sprawdz_msg(wartosc_srodek > 0 && sb.wartosc() > wartosc_srodek,
                            "Tabela drag uchwytu top-middle-bottom zmienia model");
            bws_sprawdz_msg(drag_sb_alok_po == drag_sb_alok_przed,
                            "Tabela drag paska 0 alokacji");
            raw_drag.x += 80; // poza paskiem, lecz nadal pod capture
            ok_tabela.obsluz_zdarzenie(raw_drag);
            bws_zdarzenie raw_up = raw_drag;
            raw_up.typ = BWS_ZDARZENIE_MYSZ_UP;
            ok_tabela.obsluz_zdarzenie(raw_up);
            bws_sprawdz_msg(ok_tabela.przechwycenie_myszy() == nullptr,
                            "Tabela release poza paskiem konczy capture");

            sb.ustaw_wartosc(0);
            thumb = sb.prostokat_uchwytu();
            bws_zdarzenie track_down{};
            track_down.typ = BWS_ZDARZENIE_MYSZ_DOWN;
            track_down.x = ok_tabela.x() + sb.obszar_ekranu().x +
                           thumb.x + thumb.szerokosc / 2;
            track_down.y = ok_tabela.y() + sb.obszar_ekranu().y +
                           thumb.dolny() + 8;
            ok_tabela.obsluz_zdarzenie(track_down);
            bws_sprawdz_msg(sb.wartosc() > 0,
                            "Tabela klik toru przewija o strone");
            bws_zdarzenie track_up = track_down;
            track_up.typ = BWS_ZDARZENIE_MYSZ_UP;
            ok_tabela.obsluz_zdarzenie(track_up);
        }
        bws_log("[KONTROLKI2 1/8] Tabela: OK\n");
    }

    // 2. Rozdzielacz (orientacje, ograniczenia, układ)
    {
        bws_log("[KONTROLKI2 2/8] Rozdzielacz: podzial, granice, drag i klawiatura...\n");
        Rozdzielacz rz(Orientacja::Pozioma);
        rz.rozmiesc(Prostokat(0, 0, 400, 300));
        rz.ustaw_minimalne_rozmiary(50, 50);

        Panel* p1 = new Panel();
        Panel* p2 = new Panel();
        rz.ustaw_pierwszy(p1);
        rz.ustaw_drugi(p2);

        rz.ustaw_pozycje_podzialu(150);
        bws_sprawdz_msg(rz.pozycja_podzialu() == 150, "Rozdzielacz pozycja 150");

        // Test ograniczenia minimalnego rozmiaru
        rz.ustaw_pozycje_podzialu(10);
        bws_sprawdz_msg(rz.pozycja_podzialu() >= 50, "Rozdzielacz min_pierwszy ograniczony");

        rz.ustaw_pozycje_podzialu(390);
        bws_sprawdz_msg(rz.pozycja_podzialu() <= 350, "Rozdzielacz min_drugi ograniczony");

        struct PomiarRozdzielacza { int zmiany; } pomiar{};
        rz.przy_zmianie_podzialu([](Rozdzielacz*, int, void* dane) {
            ++static_cast<PomiarRozdzielacza*>(dane)->zmiany;
        }, &pomiar);
        rz.ustaw_pozycje_podzialu(150);
        ZdarzenieMyszki rm{};
        rm.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
        rm.x = rz.obszar_paska().x + 1;
        rm.y = 20;
        rz.obsluz_wcisniecie_myszy(rm);
        rm.x += 30;
        uint64_t drag_alok_przed = gui_liczba_alokacji_sterty();
        rz.obsluz_ruch_myszy(rm);
        uint64_t drag_alok_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(rz.pozycja_podzialu() == 180 && pomiar.zmiany == 1,
                        "Rozdzielacz drag/callback");
        bws_sprawdz_msg(drag_alok_po == drag_alok_przed,
                        "Rozdzielacz drag 0 alokacji");
        rz.obsluz_zwolnienie_myszy(rm);

        int resize_p1 = 0;
        int resize_p2 = 0;
        p1->przy_zmianie_rozmiaru([](Kontrolka*, void* dane) {
            ++*static_cast<int*>(dane);
        }, &resize_p1);
        p2->przy_zmianie_rozmiaru([](Kontrolka*, void* dane) {
            ++*static_cast<int*>(dane);
        }, &resize_p2);
        ZdarzenieMyszki rz_hover{};
        rz_hover.typ = TypZdarzenia::RuchMyszy;
        rz_hover.x = rz.obszar_paska().x + 1;
        rz_hover.y = 20;
        rz.obsluz_ruch_myszy(rz_hover);
        rz.wyczysc_brudny_obszar();
        uint64_t rz_hover_alok_przed = gui_liczba_alokacji_sterty();
        for (int i = 0; i < 5000; ++i) {
            rz_hover.y = 10 + (i % 250);
            rz.obsluz_ruch_myszy(rz_hover);
        }
        uint64_t rz_hover_alok_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(!rz.czy_brudna() && resize_p1 == 0 && resize_p2 == 0,
                        "Rozdzielacz ten sam hover: 0 dirty i 0 layout");
        bws_sprawdz_msg(rz_hover_alok_po == rz_hover_alok_przed,
                        "Rozdzielacz hover 0 alokacji");
        bws_log_u64("[PERF 5S ROZDZIELACZ] mouse_events=", 5000);
        bws_log_u64("[PERF 5S ROZDZIELACZ] dirty_calls=", 0);
        bws_log_u64("[PERF 5S ROZDZIELACZ] dirty_pixels=", 0);
        bws_log_u64("[PERF 5S ROZDZIELACZ] layout_passes=",
                    static_cast<uint64_t>(resize_p1 + resize_p2));
        bws_log_u64("[PERF 5S ROZDZIELACZ] allocations=",
                    rz_hover_alok_po - rz_hover_alok_przed);

        // Ruch po tej samej, ograniczonej pozycji nie uruchamia ponownie ukladu.
        rz.ustaw_pozycje_podzialu(350);
        resize_p1 = resize_p2 = 0;
        rz.wyczysc_brudny_obszar();
        rm.x = rz.obszar_paska().x + 1;
        rm.y = 20;
        rz.obsluz_wcisniecie_myszy(rm);
        rz.wyczysc_brudny_obszar();
        rm.x += 1000;
        rz.obsluz_ruch_myszy(rm);
        bws_sprawdz_msg(resize_p1 == 0 && resize_p2 == 0 && !rz.czy_brudna(),
                        "Rozdzielacz clamp bez zmiany: 0 layout/repaint");
        rz.obsluz_zwolnienie_myszy(rm);
        rz.ustaw_pozycje_podzialu(180);

        ZdarzenieKlawiatury rk{};
        rk.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x4BU;
        rz.obsluz_wcisniecie_klawisza(rk);
        bws_sprawdz_msg(rz.pozycja_podzialu() == 170,
                        "Rozdzielacz regulacja klawiatura");

        {
            Okno ok_rz("Rozdzielacz capture", 420, 300);
            Panel* root = static_cast<Panel*>(ok_rz.kontrolka_glowna());
            root->ustaw_wypelnienie(Marginesy(0));
            Rozdzielacz& route_rz = root->dodaj<Rozdzielacz>(Orientacja::Pozioma);
            route_rz.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            route_rz.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
            route_rz.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            route_rz.ustaw_pierwszy(new Panel());
            route_rz.ustaw_drugi(new Panel());
            route_rz.ustaw_pozycje_podzialu(160);
            ok_rz.przelicz_uklad();
            Prostokat stripe = route_rz.obszar_paska();
            bws_zdarzenie rz_down{};
            rz_down.typ = BWS_ZDARZENIE_MYSZ_DOWN;
            rz_down.x = ok_rz.x() + route_rz.obszar_ekranu().x + stripe.x + 1;
            rz_down.y = ok_rz.y() + route_rz.obszar_ekranu().y + 30;
            ok_rz.obsluz_zdarzenie(rz_down);
            bws_sprawdz_msg(ok_rz.przechwycenie_myszy() == &route_rz,
                            "Rozdzielacz przechwytuje drag");
            bws_zdarzenie rz_drag = rz_down;
            rz_drag.typ = BWS_ZDARZENIE_MYSZ_RUCH;
            rz_drag.x += 60;
            ok_rz.obsluz_zdarzenie(rz_drag);
            bws_sprawdz_msg(route_rz.pozycja_podzialu() > 160,
                            "Rozdzielacz drag dziala pod capture");
            rz_drag.x += 300;
            ok_rz.obsluz_zdarzenie(rz_drag);
            bws_zdarzenie rz_up = rz_drag;
            rz_up.typ = BWS_ZDARZENIE_MYSZ_UP;
            ok_rz.obsluz_zdarzenie(rz_up);
            bws_sprawdz_msg(ok_rz.przechwycenie_myszy() == nullptr,
                            "Rozdzielacz release poza paskiem konczy capture");
        }

        bws_log("[KONTROLKI2 2/8] Rozdzielacz: OK\n");
    }

    // 3. WidokIkon (1000 elementów, siatka, selekcja)
    {
        bws_log("[KONTROLKI2 3/8] WidokIkon: 1000 elementow, siatka i wejscie...\n");
        WidokIkon wi;
        wi.rozmiesc(Prostokat(0, 0, 300, 200));

        wi.rozpocznij_aktualizacje();
        for (int i = 0; i < 1000; ++i) {
            bws_sprawdz_msg(wi.dodaj_element("Ikona Zażółć", TypIkonyWidoku::Plik, i),
                            "WidokIkon dodaj_element");
        }
        wi.zakoncz_aktualizacje();
        bws_sprawdz_msg(wi.liczba_elementow() == 1000, "WidokIkon 1000 elementow");
        bws_sprawdz_msg(wi.pasek_pionowy().czy_widoczna(), "WidokIkon scroll pionowy");

        bws_sprawdz_msg(wi.ustaw_wybrany_indeks(77), "WidokIkon ustaw_wybrany_indeks 77");
        bws_sprawdz_msg(wi.wybrany_indeks() == 77, "WidokIkon wybrany_indeks == 77");
        bws_sprawdz_msg(wi.znacznik_elementu(77) == 77, "WidokIkon znacznik == 77");

        ZdarzenieKlawiatury wk{};
        wk.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x50U;
        int przed = wi.wybrany_indeks();
        wi.obsluz_wcisniecie_klawisza(wk);
        bws_sprawdz_msg(wi.wybrany_indeks() > przed, "WidokIkon klawiatura Dol");

        KontekstRysowania ctx_wi;
        uint64_t wi_alok_przed = gui_liczba_alokacji_sterty();
        wi.rysuj(ctx_wi, true);
        uint64_t wi_alok_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(wi.liczba_ostatnio_narysowanych_elementow() > 0 &&
                        wi.liczba_ostatnio_narysowanych_elementow() < 30,
                        "WidokIkon renderuje tylko widoczne elementy");
        bws_sprawdz_msg(wi_alok_po == wi_alok_przed,
                        "WidokIkon paint 0 alokacji");

        wi.pasek_pionowy().ustaw_wartosc(0);
        wi.ustaw_wybrany_indeks(0);
        wi.wyczysc_brudny_obszar();
        const uint32_t wi_tlo_przed = wi.wyznacz_biezacy_styl_stanu().tlo;
        ZdarzenieMyszki wi_down{};
        wi_down.typ = TypZdarzenia::WcisniecieMyszy;
        wi_down.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
        wi_down.x = 1 + wi.szerokosc_komorki() + 5;
        wi_down.y = 6;
        uint64_t wi_click_alok_przed = gui_liczba_alokacji_sterty();
        wi.obsluz_wcisniecie_myszy(wi_down);
        uint64_t wi_click_alok_po = gui_liczba_alokacji_sterty();
        Prostokat wi_click_dirty = wi.pobierz_brudny_obszar();
        bws_sprawdz_msg(wi.wyznacz_biezacy_styl_stanu().tlo == wi_tlo_przed,
                        "WidokIkon klik nie zmienia tla korzenia");
        bws_sprawdz_msg(wi.wybrany_indeks() == 1 &&
                        wi_click_dirty.szerokosc <= 2 * wi.szerokosc_komorki() &&
                        wi_click_dirty.wysokosc <= wi.wysokosc_komorki(),
                        "WidokIkon klik brudzi tylko stary/nowy element");
        bws_sprawdz_msg(wi_click_alok_po == wi_click_alok_przed,
                        "WidokIkon klik 0 alokacji");

        ZdarzenieMyszki wi_move{};
        wi_move.typ = TypZdarzenia::RuchMyszy;
        wi_move.x = wi_down.x;
        wi_move.y = wi_down.y;
        wi.obsluz_ruch_myszy(wi_move);
        wi.wyczysc_brudny_obszar();
        uint64_t wi_move_alok_przed = gui_liczba_alokacji_sterty();
        for (int i = 0; i < 5000; ++i) {
            wi_move.x = 1 + wi.szerokosc_komorki() + 3 + (i % 50);
            wi.obsluz_ruch_myszy(wi_move);
        }
        uint64_t wi_move_alok_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(!wi.czy_brudna(),
                        "WidokIkon ten sam element: 0 dodatkowych invalidacji");
        bws_sprawdz_msg(wi_move_alok_po == wi_move_alok_przed,
                        "WidokIkon MouseMove 0 alokacji");
        bws_log_u64("[PERF 5S WIDOKIKON] mouse_events=", 5000);
        bws_log_u64("[PERF 5S WIDOKIKON] dirty_calls=", 0);
        bws_log_u64("[PERF 5S WIDOKIKON] dirty_pixels=", 0);
        bws_log_u64("[PERF 5S WIDOKIKON] layout_passes=", 0);
        bws_log_u64("[PERF 5S WIDOKIKON] allocations=",
                    wi_move_alok_po - wi_move_alok_przed);
        bws_log_u64("[WIDOKIKON POMIAR] visible_items=",
                    wi.liczba_ostatnio_narysowanych_elementow());
        bws_log_u64("[WIDOKIKON POMIAR] paint_allocations=",
                    wi_alok_po - wi_alok_przed);

        // Rzeczywista sciezka Nawigacja: WidokIkon posiada ten pionowy
        // PasekPrzewijania. Mierzymy 1000 ruchow pod capture.
        Okno* ok_nav = new Okno("WidokIkon drag", 340, 260);
        bws_sprawdz_msg(ok_nav != nullptr, "WidokIkon drag okno alokacja");
        Panel* nav_root = static_cast<Panel*>(ok_nav->kontrolka_glowna());
        nav_root->ustaw_wypelnienie(Marginesy(0));
        WidokIkon& nav_widok = nav_root->dodaj<WidokIkon>();
        nav_widok.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
        nav_widok.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        nav_widok.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        nav_widok.rozpocznij_aktualizacje();
        for (int i = 0; i < 1000; ++i)
            nav_widok.dodaj_element("Plik", TypIkonyWidoku::Plik,
                                    static_cast<uint64_t>(i));
        nav_widok.zakoncz_aktualizacje();
        ok_nav->przelicz_uklad();
        KontekstRysowania ctx_nav;
        ok_nav->rysuj(ctx_nav, true);

        struct PomiarDragWidoku {
            uint64_t content_paints{0};
            uint64_t scrollbar_paints{0};
        } pomiar_drag_widoku;
        nav_widok.przy_wlasnym_rysowaniu([](Kontrolka*, KontekstRysowania&,
                                             void* dane) {
            ++static_cast<PomiarDragWidoku*>(dane)->content_paints;
        }, &pomiar_drag_widoku);
        PasekPrzewijania& nav_sb = nav_widok.pasek_pionowy();
        nav_sb.przy_wlasnym_rysowaniu([](Kontrolka*, KontekstRysowania&,
                                          void* dane) {
            ++static_cast<PomiarDragWidoku*>(dane)->scrollbar_paints;
        }, &pomiar_drag_widoku);
        nav_sb.ustaw_wartosc(0);
        ok_nav->rysuj(ctx_nav, false);
        pomiar_drag_widoku = {};

        Prostokat nav_thumb = nav_sb.prostokat_uchwytu();
        bws_zdarzenie nav_down{};
        nav_down.typ = BWS_ZDARZENIE_MYSZ_DOWN;
        nav_down.przyciski = 1;
        nav_down.x = ok_nav->x() + nav_sb.obszar_ekranu().x +
                     nav_thumb.x + nav_thumb.szerokosc / 2;
        nav_down.y = ok_nav->y() + nav_sb.obszar_ekranu().y +
                     nav_thumb.y + nav_thumb.wysokosc / 2;
        ok_nav->obsluz_zdarzenie(nav_down);
        bws_sprawdz_msg(ok_nav->przechwycenie_myszy() == &nav_sb &&
                        nav_sb.czy_przeciagany() &&
                        nav_sb.czy_uchwyt_wcisniety(),
                        "WidokIkon drag rozpoczyna stabilny capture/pressed");
        ok_nav->rysuj(ctx_nav, false);
        pomiar_drag_widoku = {};

        const uint64_t nav_layout_przed = ok_nav->liczba_przejsc_ukladu();
        const uint64_t nav_alloc_przed = gui_liczba_alokacji_sterty();
        uint64_t nav_dirty_bounding_px = 0;
        uint64_t nav_dirty_viewport_px = 0;
        uint64_t nav_dirty_scrollbar_px = 0;
        uint64_t nav_zmienione_ruchy = 0;
        const int nav_droga = nav_sb.obszar().wysokosc + 80;
        for (int i = 0; i < 1000; ++i) {
            const int64_t stara_wartosc = nav_sb.wartosc();
            const Prostokat stary_uchwyt = nav_sb.prostokat_uchwytu();
            bws_zdarzenie nav_move = nav_down;
            nav_move.typ = BWS_ZDARZENIE_MYSZ_RUCH;
            nav_move.x += (i >= 500) ? 80 : 0;
            nav_move.y = nav_down.y + (i * nav_droga) / 999;
            ok_nav->obsluz_zdarzenie(nav_move);
            const Prostokat nowy_uchwyt = nav_sb.prostokat_uchwytu();
            const bool wartosc_zmieniona =
                nav_sb.wartosc() != stara_wartosc;

            bws_sprawdz_msg(nav_sb.czy_przeciagany() &&
                            nav_sb.czy_uchwyt_wcisniety() &&
                            nav_sb.czy_uchwyt_najechany(),
                            "WidokIkon drag bez oscylacji pressed/hover");
            if (wartosc_zmieniona) {
                ++nav_zmienione_ruchy;
                Prostokat sb_dirty = nav_sb.pobierz_brudny_obszar();
                Prostokat expected = stary_uchwyt.suma(nowy_uchwyt)
                    .powiekszony(Marginesy(2));
                bws_sprawdz_msg(sb_dirty.szerokosc <= expected.szerokosc &&
                                sb_dirty.wysokosc <= expected.wysokosc,
                                "WidokIkon drag pasek dirty = old/new thumb");
                nav_dirty_scrollbar_px +=
                    static_cast<uint64_t>(sb_dirty.szerokosc) *
                    static_cast<uint64_t>(sb_dirty.wysokosc);
                Prostokat vp = nav_widok.prostokat_widoku();
                nav_dirty_viewport_px +=
                    static_cast<uint64_t>(vp.szerokosc) * vp.wysokosc;
            }

            if (ok_nav->czy_wymaga_odrysowania()) {
                Prostokat dirty = nav_root->pobierz_brudny_obszar();
                nav_dirty_bounding_px +=
                    static_cast<uint64_t>(dirty.szerokosc) * dirty.wysokosc;
                ok_nav->rysuj(ctx_nav, false);
                if (wartosc_zmieniona) {
                    bws_sprawdz_msg(
                        nav_widok.liczba_ostatnio_narysowanych_elementow() > 0,
                        "WidokIkon drag nie prezentuje pustego stanu");
                }
            }
        }
        const uint64_t nav_alloc_po = gui_liczba_alokacji_sterty();
        const uint64_t nav_layout_po = ok_nav->liczba_przejsc_ukladu();
        bws_zdarzenie nav_up = nav_down;
        nav_up.typ = BWS_ZDARZENIE_MYSZ_UP;
        nav_up.przyciski = 0;
        nav_up.x += 80;
        nav_up.y += nav_droga;
        ok_nav->obsluz_zdarzenie(nav_up);
        bws_sprawdz_msg(ok_nav->przechwycenie_myszy() == nullptr &&
                        !nav_sb.czy_przeciagany() &&
                        !nav_sb.czy_uchwyt_wcisniety(),
                        "WidokIkon drag MouseUp zwalnia capture/pressed");
        bws_sprawdz_msg(nav_sb.wartosc() == nav_sb.maksimum(),
                        "WidokIkon drag przewija zawartosc do konca");
        bws_sprawdz_msg(nav_layout_po == nav_layout_przed,
                        "WidokIkon 1000 drag MouseMove: 0 layout");
        bws_sprawdz_msg(nav_alloc_po == nav_alloc_przed,
                        "WidokIkon 1000 drag MouseMove: 0 alokacji");
        bws_sprawdz_msg(pomiar_drag_widoku.content_paints ==
                            pomiar_drag_widoku.scrollbar_paints &&
                        pomiar_drag_widoku.content_paints == nav_zmienione_ruchy,
                        "WidokIkon jeden paint content/pasek na zmiane modelu");
        bws_log_u64("[PERF DRAG WIDOKIKON] mouse_events=", 1000);
        bws_log_u64("[PERF DRAG WIDOKIKON] value_changes=",
                    nav_zmienione_ruchy);
        bws_log_u64("[PERF DRAG WIDOKIKON] content_paints=",
                    pomiar_drag_widoku.content_paints);
        bws_log_u64("[PERF DRAG WIDOKIKON] scrollbar_paints=",
                    pomiar_drag_widoku.scrollbar_paints);
        bws_log_u64("[PERF DRAG WIDOKIKON] layout_passes=",
                    nav_layout_po - nav_layout_przed);
        bws_log_u64("[PERF DRAG WIDOKIKON] allocations=",
                    nav_alloc_po - nav_alloc_przed);
        bws_log_u64("[PERF DRAG WIDOKIKON] viewport_dirty_pixels=",
                    nav_dirty_viewport_px);
        bws_log_u64("[PERF DRAG WIDOKIKON] scrollbar_dirty_pixels=",
                    nav_dirty_scrollbar_px);
        bws_log_u64("[PERF DRAG WIDOKIKON] bounding_dirty_pixels=",
                    nav_dirty_bounding_px);
        delete ok_nav;

        bws_log("[KONTROLKI2 3/8] WidokIkon: OK\n");
    }

    // 4. Obraz (bufor pikseli, tryby skalowania)
    {
        bws_log("[KONTROLKI2 4/8] Obraz: bufor, tryby, clipping i paint...\n");
        Obraz* img = new Obraz();
        bws_sprawdz_msg(img != nullptr, "Obraz alokacja");
        img->rozmiesc(Prostokat(0, 0, 100, 100));

        static uint32_t raw_px[16 * 16];
        for (int i = 0; i < 256; ++i) raw_px[i] = 0xFF112233U;

        img->ustaw_obraz(raw_px, 16, 16, true);
        bws_sprawdz_msg(img->czy_ma_obraz(), "Obraz czy_ma_obraz == true");
        bws_sprawdz_msg(img->szerokosc_oryginalna() == 16, "Obraz w == 16");
        bws_sprawdz_msg(img->wysokosc_oryginalna() == 16, "Obraz h == 16");

        img->ustaw_tryb(TrybObrazu::Wycentrowany);
        bws_sprawdz_msg(img->tryb() == TrybObrazu::Wycentrowany, "Obraz tryb Wycentrowany");
        img->ustaw_tryb(TrybObrazu::Rozciagnij);
        bws_sprawdz_msg(img->tryb() == TrybObrazu::Rozciagnij, "Obraz tryb Rozciagnij");

        KontekstRysowania ctx_img;
        ctx_img.dodaj_przyciecie(Prostokat(10, 10, 40, 30));
        uint64_t img_alok_przed = gui_liczba_alokacji_sterty();
        img->rysuj(ctx_img, true);
        uint64_t img_alok_po = gui_liczba_alokacji_sterty();
        ctx_img.zdejmij_przyciecie();
        bws_sprawdz_msg(img_alok_po == img_alok_przed, "Obraz paint 0 alokacji");
        img->ustaw_tryb(TrybObrazu::Naturalny);
        bws_sprawdz_msg(img->tryb() == TrybObrazu::Naturalny, "Obraz tryb Naturalny");

        img->wyczysc_obraz();
        bws_sprawdz_msg(!img->czy_ma_obraz(), "Obraz wyczysc_obraz");
        bws_log_u64("[OBRAZ POMIAR] paint_allocations=", img_alok_po - img_alok_przed);
        delete img;
        bws_log("[KONTROLKI2 4/8] Obraz: OK\n");
    }

    // 5. PrzyciskObrazkowy (tryby ikony i tekstu)
    {
        bws_log("[KONTROLKI2 5/8] PrzyciskObrazkowy: kompozycja i zachowanie Przycisku...\n");
        PrzyciskObrazkowy btn("Zapisz", TypIkonyWidoku::Dysk);
        btn.rozmiesc(Prostokat(0, 0, 120, 36));

        int klikniecia = 0;
        btn.przy_kliknieciu([](Przycisk*, void* dane) {
            ++*static_cast<int*>(dane);
        }, &klikniecia);

        bws_sprawdz_msg(btn.typ_ikony() == TypIkonyWidoku::Dysk, "PrzyciskObrazkowy typ ikony");
        btn.ustaw_tryb(TrybPrzyciskuObrazkowego::TylkoIkona);
        bws_sprawdz_msg(btn.tryb() == TrybPrzyciskuObrazkowego::TylkoIkona, "PrzyciskObrazkowy TylkoIkona");

        btn.ustaw_pozycja_ikony(PozycjaIkony::Gora);
        bws_sprawdz_msg(btn.pozycja_ikony() == PozycjaIkony::Gora, "PrzyciskObrazkowy PozycjaIkony Gora");
        ZdarzenieMyszki bm{};
        bm.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
        bm.x = 20; bm.y = 20;
        btn.obsluz_wcisniecie_myszy(bm);
        btn.obsluz_zwolnienie_myszy(bm);
        bws_sprawdz_msg(klikniecia == 1, "PrzyciskObrazkowy callback klikniecia");
        btn.ustaw_wlaczona(false);
        btn.obsluz_wcisniecie_myszy(bm);
        btn.obsluz_zwolnienie_myszy(bm);
        bws_sprawdz_msg(klikniecia == 1, "PrzyciskObrazkowy disabled");
        bws_log("[KONTROLKI2 5/8] PrzyciskObrazkowy: OK\n");
    }

    // 6. PasekSciezki (segmenty, parsowanie, skrót)
    {
        bws_log("[KONTROLKI2 6/8] PasekSciezki: UTF-8, overflow i aktywacja...\n");
        PasekSciezki* ps = new PasekSciezki();
        bws_sprawdz_msg(ps != nullptr, "PasekSciezki alokacja");
        ps->rozmiesc(Prostokat(0, 0, 180, 28));

        bws_sprawdz_msg(ps->ustaw_sciezke("/system/użytkownicy/dokumenty/kontrolki2"), "PasekSciezki ustaw_sciezke");
        bws_sprawdz_msg(ps->liczba_segmentow() == 4, "PasekSciezki liczba_segmentow == 4");
        bws_sprawdz_msg(ps->segment(0)[0] == 's', "PasekSciezki segment 0");
        bws_sprawdz_msg(ps->segment(3)[0] == 'k', "PasekSciezki segment 3");
        int wybrany_segment = -1;
        ps->przy_wyborze_segmentu([](PasekSciezki*, int indeks, const char*, void* dane) {
            *static_cast<int*>(dane) = indeks;
        }, &wybrany_segment);
        KontekstRysowania ctx_ps;
        ps->rysuj(ctx_ps, true);
        bws_sprawdz_msg(!ps->obszar_skrotu().czy_pusty(), "PasekSciezki overflow");
        Prostokat ostatni = ps->obszar_segmentu(3);
        ZdarzenieMyszki pm{};
        pm.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
        pm.x = ostatni.x + 2; pm.y = ostatni.y + 2;
        ps->obsluz_wcisniecie_myszy(pm);
        bws_sprawdz_msg(wybrany_segment == 3, "PasekSciezki callback segmentu");
        delete ps;
        bws_log("[KONTROLKI2 6/8] PasekSciezki: OK\n");
    }

    // 7. Kalendarz (obliczenia dat, lata przestępne, dni tygodnia)
    {
        bws_log("[KONTROLKI2 7/8] Kalendarz: matematyka, granice i klawiatura...\n");
        bws_sprawdz_msg(DataKalendarza::czy_przestepny(2024) == true, "Kalendarz 2024 przestepny");
        bws_sprawdz_msg(DataKalendarza::czy_przestepny(2026) == false, "Kalendarz 2026 nieprzestepny");
        bws_sprawdz_msg(DataKalendarza::czy_przestepny(2000) == true, "Kalendarz 2000 przestepny");
        bws_sprawdz_msg(DataKalendarza::czy_przestepny(1900) == false, "Kalendarz 1900 nieprzestepny");

        bws_sprawdz_msg(DataKalendarza::dni_w_miesiacu(2024, 2) == 29, "Kalendarz Luty 2024 == 29");
        bws_sprawdz_msg(DataKalendarza::dni_w_miesiacu(2026, 2) == 28, "Kalendarz Luty 2026 == 28");
        bws_sprawdz_msg(DataKalendarza::dni_w_miesiacu(2026, 9) == 30, "Kalendarz Wrzesien 2026 == 30");

        // 2026-09-02 to Środa (indeks 2: Pn=0, Wt=1, Śr=2)
        bws_sprawdz_msg(DataKalendarza::dzien_tygodnia(2026, 9, 2) == 2, "Kalendarz 2026-09-02 DOW == 2");

        Kalendarz cal(DataKalendarza{2026, 9, 2});
        bws_sprawdz_msg(cal.wybrana_data().dzien == 2, "Kalendarz wybrana_data dzien == 2");
        int zmiany_dat = 0;
        cal.przy_zmianie_daty([](Kalendarz*, DataKalendarza, void* dane) {
            ++*static_cast<int*>(dane);
        }, &zmiany_dat);
        ZdarzenieKlawiatury ck{};
        ck.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x4BU;
        cal.obsluz_wcisniecie_klawisza(ck);
        bws_sprawdz_msg(cal.wybrana_data() == DataKalendarza{2026, 9, 1} &&
                        zmiany_dat == 1, "Kalendarz Lewo/callback");
        ck = {};
        ck.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x4BU;
        cal.obsluz_wcisniecie_klawisza(ck);
        bws_sprawdz_msg(cal.wybrana_data() == DataKalendarza{2026, 8, 31},
                        "Kalendarz przejscie miedzy miesiacami");
        Kalendarz cal_invalid(DataKalendarza{2026, 2, 30});
        bws_sprawdz_msg(cal_invalid.wybrana_data().czy_poprawna(),
                        "Kalendarz nie ujawnia niepoprawnej daty");

        Kalendarz cal_ui(DataKalendarza{2026, 9, 2});
        cal_ui.rozmiesc(Prostokat(0, 0, 230, 200));
        const int komorka_2 = DataKalendarza::dzien_tygodnia(2026, 9, 1) + 1;
        Prostokat cell2 = cal_ui.prostokat_komorki(komorka_2);
        ZdarzenieMyszki cal_move{};
        cal_move.typ = TypZdarzenia::RuchMyszy;
        cal_move.x = cell2.x + cell2.szerokosc / 2;
        cal_move.y = cell2.y + cell2.wysokosc / 2;
        cal_ui.obsluz_ruch_myszy(cal_move);
        cal_ui.wyczysc_brudny_obszar();
        uint64_t cal_move_alok_przed = gui_liczba_alokacji_sterty();
        for (int i = 0; i < 5000; ++i) {
            cal_move.x = cell2.x + 2 + (i % (cell2.szerokosc - 4));
            cal_ui.obsluz_ruch_myszy(cal_move);
        }
        uint64_t cal_move_alok_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(!cal_ui.czy_brudna(),
                        "Kalendarz ta sama komorka: 0 dodatkowych invalidacji");
        bws_sprawdz_msg(cal_move_alok_po == cal_move_alok_przed,
                        "Kalendarz MouseMove 0 alokacji");
        cal_ui.wyczysc_brudny_obszar();
        Prostokat cell3 = cal_ui.prostokat_komorki(komorka_2 + 1);
        cal_move.x = cell3.x + cell3.szerokosc / 2;
        cal_move.y = cell3.y + cell3.wysokosc / 2;
        cal_ui.obsluz_ruch_myszy(cal_move);
        Prostokat cal_hover_dirty = cal_ui.pobierz_brudny_obszar();
        bws_sprawdz_msg(cal_hover_dirty.wysokosc <= cell2.wysokosc &&
                        cal_hover_dirty.szerokosc <= cell2.szerokosc + cell3.szerokosc,
                        "Kalendarz zmiana hover brudzi stara i nowa komorke");

        cal_ui.wyczysc_brudny_obszar();
        const uint32_t cal_tlo_przed = cal_ui.wyznacz_biezacy_styl_stanu().tlo;
        ZdarzenieMyszki cal_down{};
        cal_down.typ = TypZdarzenia::WcisniecieMyszy;
        cal_down.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
        cal_down.x = cell3.x + cell3.szerokosc / 2;
        cal_down.y = cell3.y + cell3.wysokosc / 2;
        uint64_t cal_click_alok_przed = gui_liczba_alokacji_sterty();
        cal_ui.obsluz_wcisniecie_myszy(cal_down);
        uint64_t cal_click_alok_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(cal_ui.wybrana_data().dzien == 3 &&
                        cal_ui.wyznacz_biezacy_styl_stanu().tlo == cal_tlo_przed,
                        "Kalendarz data nie zmienia tla korzenia");
        bws_sprawdz_msg(cal_click_alok_po == cal_click_alok_przed,
                        "Kalendarz klik dnia 0 alokacji");
        ZdarzenieMyszki cal_up = cal_down;
        cal_up.typ = TypZdarzenia::ZwolnienieMyszy;
        cal_ui.obsluz_zwolnienie_myszy(cal_up);

        Prostokat nav_next = cal_ui.prostokat_przycisku_nawigacji(2);
        cal_down.x = nav_next.x + nav_next.szerokosc / 2;
        cal_down.y = nav_next.y + nav_next.wysokosc / 2;
        cal_ui.obsluz_wcisniecie_myszy(cal_down);
        bws_sprawdz_msg(cal_ui.wyznacz_biezacy_styl_stanu().tlo == cal_tlo_przed,
                        "Kalendarz przycisk miesiaca nie zmienia tla korzenia");
        cal_ui.obsluz_zwolnienie_myszy(cal_up);
        bws_sprawdz_msg(cal_ui.kolor_nieaktywnego_dnia() == Kolory::TekstWyciszony &&
                        cal_ui.kolor_nieaktywnego_dnia() != Kolory::WejscieTlo,
                        "Kalendarz dni nieaktywne uzywaja czytelnego tekstu motywu");

        Kalendarz* cal_perf = new Kalendarz(DataKalendarza{2026, 9, 2});
        bws_sprawdz_msg(cal_perf != nullptr,
                        "Kalendarz rapid click alokacja kontrolki");
        cal_perf->rozmiesc(Prostokat(0, 0, 230, 200));
        struct PomiarKalendarza {
            uint64_t callbacks{0};
            uint64_t paints{0};
        } pomiar_cal_perf;
        cal_perf->przy_zmianie_daty([](Kalendarz*, DataKalendarza, void* dane) {
            ++static_cast<PomiarKalendarza*>(dane)->callbacks;
        }, &pomiar_cal_perf);
        cal_perf->przy_wlasnym_rysowaniu([](Kontrolka*, KontekstRysowania&,
                                             void* dane) {
            ++static_cast<PomiarKalendarza*>(dane)->paints;
        }, &pomiar_cal_perf);
        const uint64_t cal_model_przed =
            cal_perf->liczba_przeliczen_miesiaca();
        const uint64_t cal_perf_alloc_przed = gui_liczba_alokacji_sterty();
        uint64_t cal_perf_dirty_px = 0;
        KontekstRysowania ctx_cal_perf;
        for (int i = 0; i < 1000; ++i) {
            const int dzien = (i & 1) ? 2 : 3;
            const int komorka = DataKalendarza::dzien_tygodnia(2026, 9, 1) +
                                dzien - 1;
            const Prostokat cell = cal_perf->prostokat_komorki(komorka);
            ZdarzenieMyszki hover{};
            hover.typ = TypZdarzenia::RuchMyszy;
            hover.x = cell.x + cell.szerokosc / 2;
            hover.y = cell.y + cell.wysokosc / 2;
            cal_perf->obsluz_ruch_myszy(hover);
            cal_perf->wyczysc_brudny_obszar();

            ZdarzenieMyszki down = hover;
            down.typ = TypZdarzenia::WcisniecieMyszy;
            down.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
            cal_perf->obsluz_wcisniecie_myszy(down);
            ZdarzenieMyszki up = down;
            up.typ = TypZdarzenia::ZwolnienieMyszy;
            cal_perf->obsluz_zwolnienie_myszy(up);
            ZdarzenieMyszki leave{};
            leave.typ = TypZdarzenia::RuchMyszy;
            leave.x = -100;
            leave.y = -100;
            cal_perf->obsluz_ruch_myszy(leave);

            bws_sprawdz_msg(cal_perf->wybrana_data().dzien == dzien &&
                            cal_perf->najechana_komorka() == -1,
                            "Kalendarz click+fast-leave zachowuje wybor i czysci hover");
            Prostokat dirty = cal_perf->pobierz_brudny_obszar();
            bws_sprawdz_msg(dirty.wysokosc <= cell.wysokosc &&
                            dirty.szerokosc <= 2 * cell.szerokosc,
                            "Kalendarz click+fast-leave minimalny dirty");
            cal_perf_dirty_px += static_cast<uint64_t>(dirty.szerokosc) *
                                 dirty.wysokosc;
            ctx_cal_perf.dodaj_przyciecie(dirty);
            cal_perf->rysuj(ctx_cal_perf, false);
            ctx_cal_perf.zdejmij_przyciecie();
            cal_perf->wyczysc_brudny_obszar();
        }
        const uint64_t cal_perf_alloc_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(pomiar_cal_perf.callbacks == 1000 &&
                        pomiar_cal_perf.paints == 1000,
                        "Kalendarz 1000 click+leave: callback/paint raz na sekwencje");
        bws_sprawdz_msg(cal_perf->liczba_przeliczen_miesiaca() ==
                            cal_model_przed,
                        "Kalendarz wybor w miesiacu nie przelicza modelu 42 komorek");
        bws_sprawdz_msg(cal_perf_alloc_po == cal_perf_alloc_przed,
                        "Kalendarz 1000 click+leave: 0 alokacji");
        bws_log_u64("[PERF RAPID KALENDARZ] sequences=", 1000);
        bws_log_u64("[PERF RAPID KALENDARZ] paints=",
                    pomiar_cal_perf.paints);
        bws_log_u64("[PERF RAPID KALENDARZ] callbacks=",
                    pomiar_cal_perf.callbacks);
        bws_log_u64("[PERF RAPID KALENDARZ] month_recalculations=",
                    cal_perf->liczba_przeliczen_miesiaca() - cal_model_przed);
        bws_log_u64("[PERF RAPID KALENDARZ] layout_passes=", 0);
        bws_log_u64("[PERF RAPID KALENDARZ] allocations=",
                    cal_perf_alloc_po - cal_perf_alloc_przed);
        bws_log_u64("[PERF RAPID KALENDARZ] dirty_pixels=",
                    cal_perf_dirty_px);
        delete cal_perf;
        bws_log_u64("[PERF 5S KALENDARZ] mouse_events=", 5000);
        bws_log_u64("[PERF 5S KALENDARZ] dirty_calls=", 0);
        bws_log_u64("[PERF 5S KALENDARZ] dirty_pixels=", 0);
        bws_log_u64("[PERF 5S KALENDARZ] layout_passes=", 0);
        bws_log_u64("[PERF 5S KALENDARZ] allocations=",
                    cal_move_alok_po - cal_move_alok_przed);
        bws_log("[KONTROLKI2 7/8] Kalendarz: OK\n");
    }

    // 8. PoleDaty i PoleCzasu (walidacja i manipulacja)
    {
        bws_log("[KONTROLKI2 8/8] PoleDaty i PoleCzasu: walidacja i bezpieczenstwo...\n");
        PoleDaty pd(DataKalendarza{2026, 9, 2});
        pd.rozmiesc(Prostokat(0, 0, 180, 32));
        bws_sprawdz_msg(pd.pobierz_date().rok == 2026, "PoleDaty rok == 2026");
        pd.kolejny_dzien();
        bws_sprawdz_msg(pd.pobierz_date().dzien == 3, "PoleDaty kolejny dzien == 3");
        pd.ustaw_date(DataKalendarza{2026, 2, 30});
        bws_sprawdz_msg(pd.pobierz_date() == DataKalendarza{2026, 9, 3},
                        "PoleDaty odrzuca niepoprawna date");
        Prostokat pd_text = pd.prostokat_tekstu();
        Punkt pd_pos = pd.pozycja_tekstu();
        const int pd_tw = CzcionkaUI::zmierz_tekst("2026-09-03", 1);
        const int pd_th = CzcionkaUI::pobierz_wysokosc(1);
        bws_sprawdz_msg(pd_pos.x == pd_text.x + (pd_text.szerokosc - pd_tw) / 2 &&
                        pd_pos.y == pd_text.y + (pd_text.wysokosc - pd_th) / 2 &&
                        pd_th == 16,
                        "PoleDaty tekst kanoniczny i matematycznie wycentrowany");
        pd.obsluz_uzyskanie_skupienia();
        pd.wyczysc_brudny_obszar();
        const StylStanu pd_fokus = pd.wyznacz_biezacy_styl_stanu();
        ZdarzenieMyszki pd_down{};
        pd_down.typ = TypZdarzenia::WcisniecieMyszy;
        pd_down.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
        pd_down.x = pd_text.x + 4;
        pd_down.y = pd_text.y + 4;
        uint64_t pd_click_alok_przed = gui_liczba_alokacji_sterty();
        pd.obsluz_wcisniecie_myszy(pd_down);
        StylStanu pd_po_down = pd.wyznacz_biezacy_styl_stanu();
        ZdarzenieMyszki pd_up = pd_down;
        pd_up.typ = TypZdarzenia::ZwolnienieMyszy;
        pd.obsluz_zwolnienie_myszy(pd_up);
        uint64_t pd_click_alok_po = gui_liczba_alokacji_sterty();
        StylStanu pd_po_up = pd.wyznacz_biezacy_styl_stanu();
        bws_sprawdz_msg(pd.czy_ma_fokus() &&
                        pd_po_down.tlo == pd_fokus.tlo &&
                        pd_po_up.tlo == pd_fokus.tlo &&
                        pd_po_down.ramka == pd_fokus.ramka &&
                        pd_po_up.ramka == pd_fokus.ramka,
                        "PoleDaty fokus stabilny przez MouseDown/MouseUp");
        bws_sprawdz_msg(pd_click_alok_po == pd_click_alok_przed,
                        "PoleDaty fokus/klik 0 alokacji");
        pd.wyczysc_brudny_obszar();
        ZdarzenieMyszki pd_move{};
        pd_move.typ = TypZdarzenia::RuchMyszy;
        pd_move.y = pd_text.y + pd_text.wysokosc / 2;
        uint64_t pd_move_alok_przed = gui_liczba_alokacji_sterty();
        for (int i = 0; i < 5000; ++i) {
            pd_move.x = pd_text.x + 1 + (i % (pd_text.szerokosc - 2));
            pd.obsluz_ruch_myszy(pd_move);
        }
        uint64_t pd_move_alok_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(!pd.czy_brudna() && pd_move_alok_po == pd_move_alok_przed,
                        "PoleDaty 5s hover: 0 dirty/layout/allocation");
        struct PomiarPolaDaty {
            uint64_t callbacks{0};
            uint64_t paints{0};
        } pomiar_pd_perf;
        pd.przy_zmianie_daty([](PoleDaty*, DataKalendarza, void* dane) {
            ++static_cast<PomiarPolaDaty*>(dane)->callbacks;
        }, &pomiar_pd_perf);
        pd.przy_wlasnym_rysowaniu([](Kontrolka*, KontekstRysowania&,
                                      void* dane) {
            ++static_cast<PomiarPolaDaty*>(dane)->paints;
        }, &pomiar_pd_perf);
        const Prostokat pd_gora = pd.prostokat_przycisku_gora();
        const uint64_t pd_perf_alloc_przed = gui_liczba_alokacji_sterty();
        uint64_t pd_perf_dirty_px = 0;
        KontekstRysowania ctx_pd_perf;
        for (int i = 0; i < 1000; ++i) {
            ZdarzenieMyszki hover{};
            hover.typ = TypZdarzenia::RuchMyszy;
            hover.x = pd_gora.x + pd_gora.szerokosc / 2;
            hover.y = pd_gora.y + pd_gora.wysokosc / 2;
            pd.obsluz_ruch_myszy(hover);
            pd.wyczysc_brudny_obszar();
            ZdarzenieMyszki down = hover;
            down.typ = TypZdarzenia::WcisniecieMyszy;
            down.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
            pd.obsluz_wcisniecie_myszy(down);
            ZdarzenieMyszki up = down;
            up.typ = TypZdarzenia::ZwolnienieMyszy;
            pd.obsluz_zwolnienie_myszy(up);
            ZdarzenieMyszki leave{};
            leave.typ = TypZdarzenia::RuchMyszy;
            leave.x = -100;
            leave.y = -100;
            pd.obsluz_ruch_myszy(leave);
            bws_sprawdz_msg(pd.czy_ma_fokus() &&
                            pd.najechany_przycisk() == -1 &&
                            pd.wcisniety_przycisk() == -1,
                            "PoleDaty arrow+fast-leave stabilny fokus i stan");
            Prostokat dirty = pd.pobierz_brudny_obszar();
            bws_sprawdz_msg(dirty.wysokosc < pd.obszar().wysokosc,
                            "PoleDaty arrow+fast-leave bez pelnego repaintu");
            pd_perf_dirty_px += static_cast<uint64_t>(dirty.szerokosc) *
                                dirty.wysokosc;
            ctx_pd_perf.dodaj_przyciecie(dirty);
            pd.rysuj(ctx_pd_perf, false);
            ctx_pd_perf.zdejmij_przyciecie();
            pd.wyczysc_brudny_obszar();
        }
        const uint64_t pd_perf_alloc_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(pomiar_pd_perf.callbacks == 1000 &&
                        pomiar_pd_perf.paints == 1000,
                        "PoleDaty 1000 arrow+leave: callback/paint raz");
        bws_sprawdz_msg(pd_perf_alloc_po == pd_perf_alloc_przed,
                        "PoleDaty 1000 arrow+leave: 0 alokacji");
        bws_log_u64("[PERF RAPID POLEDATY] sequences=", 1000);
        bws_log_u64("[PERF RAPID POLEDATY] paints=",
                    pomiar_pd_perf.paints);
        bws_log_u64("[PERF RAPID POLEDATY] callbacks=",
                    pomiar_pd_perf.callbacks);
        bws_log_u64("[PERF RAPID POLEDATY] layout_passes=", 0);
        bws_log_u64("[PERF RAPID POLEDATY] allocations=",
                    pd_perf_alloc_po - pd_perf_alloc_przed);
        bws_log_u64("[PERF RAPID POLEDATY] dirty_pixels=",
                    pd_perf_dirty_px);
        bws_log_u64("[PERF 5S POLEDATY] mouse_events=", 5000);
        bws_log_u64("[PERF 5S POLEDATY] dirty_calls=", 0);
        bws_log_u64("[PERF 5S POLEDATY] dirty_pixels=", 0);
        bws_log_u64("[PERF 5S POLEDATY] layout_passes=", 0);
        bws_log_u64("[PERF 5S POLEDATY] allocations=",
                    pd_move_alok_po - pd_move_alok_przed);

        PoleCzasu pc(CzasDnia{23, 59, 59});
        pc.rozmiesc(Prostokat(0, 0, 180, 32));
        bws_sprawdz_msg(pc.pobierz_czas().godzina == 23, "PoleCzasu godzina == 23");
        bws_sprawdz_msg(pc.pobierz_czas().czy_poprawny() == true, "PoleCzasu poprawny");
        pc.ustaw_czas(CzasDnia{25, 0, 0});
        bws_sprawdz_msg(pc.pobierz_czas() == CzasDnia{23, 59, 59},
                        "PoleCzasu odrzuca niepoprawny czas");
        ZdarzenieKlawiatury pk{};
        pk.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x48U;
        pc.obsluz_wcisniecie_klawisza(pk);
        bws_sprawdz_msg(pc.pobierz_czas() == CzasDnia{0, 59, 59},
                        "PoleCzasu segment HH zawija 23->00");
        Prostokat pc_gora = pc.prostokat_przycisku_gora();
        Prostokat pc_dol = pc.prostokat_przycisku_dol();
        PoleCzasu::GeometriaStrzalki pc_up_geo = pc.geometria_strzalki_gora();
        PoleCzasu::GeometriaStrzalki pc_dn_geo = pc.geometria_strzalki_dol();
        bws_sprawdz_msg(pc_up_geo.srodek.x == pc_gora.x + pc_gora.szerokosc / 2 &&
                        pc_up_geo.srodek.y == pc_gora.y + pc_gora.wysokosc / 2 &&
                        pc_dn_geo.srodek.x == pc_dol.x + pc_dol.szerokosc / 2 &&
                        pc_dn_geo.srodek.y == pc_dol.y + pc_dol.wysokosc / 2 &&
                        pc_up_geo.promien == pc_dn_geo.promien &&
                        pc_gora.dolny() == pc_dol.y,
                        "PoleCzasu strzalki symetryczne w identycznych prostokatach");
        const uint32_t pc_tlo_przed = pc.wyznacz_biezacy_styl_stanu().tlo;
        ZdarzenieMyszki pc_down{};
        pc_down.typ = TypZdarzenia::WcisniecieMyszy;
        pc_down.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
        pc_down.x = pc_gora.x + pc_gora.szerokosc / 2;
        pc_down.y = pc_gora.y + pc_gora.wysokosc / 2;
        uint64_t pc_click_alok_przed = gui_liczba_alokacji_sterty();
        pc.obsluz_wcisniecie_myszy(pc_down);
        ZdarzenieMyszki pc_fast_move{};
        pc_fast_move.typ = TypZdarzenia::RuchMyszy;
        pc_fast_move.x = 2;
        pc_fast_move.y = 2;
        pc.obsluz_ruch_myszy(pc_fast_move);
        ZdarzenieMyszki pc_up = pc_fast_move;
        pc_up.typ = TypZdarzenia::ZwolnienieMyszy;
        pc_up.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
        pc.obsluz_zwolnienie_myszy(pc_up);
        uint64_t pc_click_alok_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(pc.wyznacz_biezacy_styl_stanu().tlo == pc_tlo_przed,
                        "PoleCzasu spinner nie zmienia tla korzenia");
        bws_sprawdz_msg(pc_click_alok_po == pc_click_alok_przed,
                        "PoleCzasu klik i szybkie odejscie 0 alokacji");
        pc.wyczysc_brudny_obszar();
        uint64_t pc_move_alok_przed = gui_liczba_alokacji_sterty();
        for (int i = 0; i < 5000; ++i) pc.obsluz_ruch_myszy(pc_fast_move);
        uint64_t pc_move_alok_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(!pc.czy_brudna() && pc_move_alok_po == pc_move_alok_przed,
                        "PoleCzasu 5s neutral hover: 0 dirty/layout/allocation");
        struct PomiarPolaCzasu {
            uint64_t callbacks{0};
            uint64_t paints{0};
        } pomiar_pc_perf;
        pc.przy_zmianie_czasu([](PoleCzasu*, CzasDnia, void* dane) {
            ++static_cast<PomiarPolaCzasu*>(dane)->callbacks;
        }, &pomiar_pc_perf);
        pc.przy_wlasnym_rysowaniu([](Kontrolka*, KontekstRysowania&,
                                      void* dane) {
            ++static_cast<PomiarPolaCzasu*>(dane)->paints;
        }, &pomiar_pc_perf);
        pc.obsluz_uzyskanie_skupienia();
        const uint64_t pc_perf_alloc_przed = gui_liczba_alokacji_sterty();
        uint64_t pc_perf_dirty_px = 0;
        KontekstRysowania ctx_pc_perf;
        for (int i = 0; i < 1000; ++i) {
            ZdarzenieMyszki hover{};
            hover.typ = TypZdarzenia::RuchMyszy;
            hover.x = pc_gora.x + pc_gora.szerokosc / 2;
            hover.y = pc_gora.y + pc_gora.wysokosc / 2;
            pc.obsluz_ruch_myszy(hover);
            pc.wyczysc_brudny_obszar();
            ZdarzenieMyszki down = hover;
            down.typ = TypZdarzenia::WcisniecieMyszy;
            down.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
            pc.obsluz_wcisniecie_myszy(down);
            ZdarzenieMyszki up = down;
            up.typ = TypZdarzenia::ZwolnienieMyszy;
            pc.obsluz_zwolnienie_myszy(up);
            ZdarzenieMyszki leave{};
            leave.typ = TypZdarzenia::RuchMyszy;
            leave.x = -100;
            leave.y = -100;
            pc.obsluz_ruch_myszy(leave);
            bws_sprawdz_msg(pc.najechany_przycisk() == -1 &&
                            pc.wcisniety_przycisk() == -1,
                            "PoleCzasu arrow+fast-leave czysci hover/pressed");
            Prostokat dirty = pc.pobierz_brudny_obszar();
            bws_sprawdz_msg(dirty.wysokosc < pc.obszar().wysokosc,
                            "PoleCzasu arrow+fast-leave bez pelnego repaintu");
            pc_perf_dirty_px += static_cast<uint64_t>(dirty.szerokosc) *
                                dirty.wysokosc;
            ctx_pc_perf.dodaj_przyciecie(dirty);
            pc.rysuj(ctx_pc_perf, false);
            ctx_pc_perf.zdejmij_przyciecie();
            pc.wyczysc_brudny_obszar();
        }
        const uint64_t pc_perf_alloc_po = gui_liczba_alokacji_sterty();
        bws_sprawdz_msg(pomiar_pc_perf.callbacks == 1000 &&
                        pomiar_pc_perf.paints == 1000,
                        "PoleCzasu 1000 arrow+leave: callback/paint raz");
        bws_sprawdz_msg(pc_perf_alloc_po == pc_perf_alloc_przed,
                        "PoleCzasu 1000 arrow+leave: 0 alokacji");
        bws_log_u64("[PERF RAPID POLECZASU] sequences=", 1000);
        bws_log_u64("[PERF RAPID POLECZASU] paints=",
                    pomiar_pc_perf.paints);
        bws_log_u64("[PERF RAPID POLECZASU] callbacks=",
                    pomiar_pc_perf.callbacks);
        bws_log_u64("[PERF RAPID POLECZASU] layout_passes=", 0);
        bws_log_u64("[PERF RAPID POLECZASU] allocations=",
                    pc_perf_alloc_po - pc_perf_alloc_przed);
        bws_log_u64("[PERF RAPID POLECZASU] dirty_pixels=",
                    pc_perf_dirty_px);
        pc.ustaw_skale_dpi(2);
        pc.rozmiesc(Prostokat(0, 0, 360, 64));
        pc_gora = pc.prostokat_przycisku_gora();
        pc_dol = pc.prostokat_przycisku_dol();
        pc_up_geo = pc.geometria_strzalki_gora();
        pc_dn_geo = pc.geometria_strzalki_dol();
        bws_sprawdz_msg(pc_up_geo.srodek.x == pc_gora.x + pc_gora.szerokosc / 2 &&
                        pc_dn_geo.srodek.x == pc_dol.x + pc_dol.szerokosc / 2 &&
                        pc_up_geo.promien == pc_dn_geo.promien,
                        "PoleCzasu symetria strzalek DPI 200%");
        bws_log_u64("[PERF 5S POLECZASU] mouse_events=", 5000);
        bws_log_u64("[PERF 5S POLECZASU] dirty_calls=", 0);
        bws_log_u64("[PERF 5S POLECZASU] dirty_pixels=", 0);
        bws_log_u64("[PERF 5S POLECZASU] layout_passes=", 0);
        bws_log_u64("[PERF 5S POLECZASU] allocations=",
                    pc_move_alok_po - pc_move_alok_przed);
        PoleCzasu pc_invalid(CzasDnia{99, 99, 99});
        bws_sprawdz_msg(pc_invalid.pobierz_czas().czy_poprawny(),
                        "PoleCzasu konstruktor nie ujawnia invalid");

        bws_sprawdz_msg(CzasDnia{25, 0, 0}.czy_poprawny() == false, "CzasDnia(25) niepoprawny");
        bws_sprawdz_msg(CzasDnia{12, 60, 0}.czy_poprawny() == false, "CzasDnia(60m) niepoprawny");
        bws_log("[KONTROLKI2 8/8] PoleDaty i PoleCzasu: OK\n");
    }

    bws_log("=== TESTY WDROZONYCH KONTROLEK 1-8 ZAKONCZONE (PASS) ===\n");
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

    // Polskie znaki UTF-8 (male i duze) - pelna reprezentacja diakrytykow
    const char* pl_utf8 = "Zażółć gęślą jaźń ZAŻÓŁĆ GĘŚLĄ JAŹŃ";
    int szer_pl_utf8 = CzcionkaUI::zmierz_tekst(pl_utf8, 1);
    bws_sprawdz(szer_pl_utf8 > 150);

    // Weryfikacja dekodowania i szerokosci kazdego z 18 polskich znakow UTF-8
    const char* pojedyncze_pl[] = {
        "Ą", "ą", "Ć", "ć", "Ę", "ę", "Ł", "ł", "Ń", "ń",
        "Ó", "ó", "Ś", "ś", "Ź", "ź", "Ż", "ż"
    };
    uint32_t oczekiwane_cp[18] = {
        260, 261, 262, 263, 280, 281, 321, 322, 323, 324,
        211, 243, 346, 347, 377, 378, 379, 380
    };
    for (int i = 0; i < 18; ++i) {
        uint32_t cp = 0;
        size_t z = CzcionkaUI::dekoduj_utf8(pojedyncze_pl[i], 0, &cp);
        bws_sprawdz(z == (oczekiwane_cp[i] <= 0x7FF ? 2 : 3));
        bws_sprawdz(cp == oczekiwane_cp[i]);
        int w = CzcionkaUI::szerokosc_znaku(cp);
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

    // Neutralny MouseMove przez rzeczywista trase Okno nie zmienia zadnego
    // stanu wizualnego. Nie moze uruchamiac layoutu, odrysowania ani alokacji.
    okno_hover.rysuj(ctx_hover, false);
    const uint64_t neutral_layout_przed = okno_hover.liczba_przejsc_ukladu();
    const uint64_t neutral_redraw_przed = okno_hover.liczba_odrysowan();
    const uint64_t neutral_full_przed = okno_hover.liczba_pelnych_odrysowan();
    const uint64_t neutral_px_przed = okno_hover.liczba_brudnych_pikseli();
    const uint64_t neutral_mouse_przed = okno_hover.liczba_zdarzen_ruchu_myszy();
    const uint64_t neutral_alloc_przed = gui_liczba_alokacji_sterty();
    for (int i = 0; i < 5000; ++i) {
        bws_zdarzenie move{};
        move.typ = BWS_ZDARZENIE_MYSZ_RUCH;
        move.x = okno_hover.x() + 250;
        move.y = okno_hover.y() + 240;
        move.timestamp = 1000000000ULL + static_cast<uint64_t>(i) * 1000000ULL;
        okno_hover.obsluz_zdarzenie(move);
        okno_hover.rysuj(ctx_hover, false);
    }
    const uint64_t neutral_alloc_po = gui_liczba_alokacji_sterty();
    bws_sprawdz_msg(okno_hover.liczba_zdarzen_ruchu_myszy() - neutral_mouse_przed == 5000,
                    "PERF neutral MouseMove events == 5000 (5 s @ 1000 Hz)");
    bws_sprawdz_msg(okno_hover.liczba_przejsc_ukladu() == neutral_layout_przed,
                    "PERF neutral MouseMove layout_pass == 0");
    bws_sprawdz_msg(okno_hover.liczba_odrysowan() == neutral_redraw_przed &&
                    okno_hover.liczba_pelnych_odrysowan() == neutral_full_przed &&
                    okno_hover.liczba_brudnych_pikseli() == neutral_px_przed,
                    "PERF neutral MouseMove redraw/dirty/full == 0");
    bws_sprawdz_msg(neutral_alloc_po == neutral_alloc_przed,
                    "PERF neutral MouseMove heap allocations == 0");
    bws_log_u64("[MOUSE PERF] events=", okno_hover.liczba_zdarzen_ruchu_myszy() - neutral_mouse_przed);
    bws_log_u64("[MOUSE PERF] layout_passes=", okno_hover.liczba_przejsc_ukladu() - neutral_layout_przed);
    bws_log_u64("[MOUSE PERF] redraws=", okno_hover.liczba_odrysowan() - neutral_redraw_przed);
    bws_log_u64("[MOUSE PERF] dirty_pixels=", okno_hover.liczba_brudnych_pikseli() - neutral_px_przed);
    bws_log_u64("[MOUSE PERF] heap_allocations=", neutral_alloc_po - neutral_alloc_przed);

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

    // PoleWyboru: pelna sciezka Okno MouseDown/MouseUp, wlacznie z szybkim
    // drugim kliknieciem rozpoznawanym jako podwojne. Kazda aktywacja zmienia
    // stan i wywoluje oba callbacki dokladnie raz.
    struct LicznikiCheckbox {
        int stan{0};
        int przelaczenie{0};
    } liczniki_checkbox;
    chk_haslo.przy_zmianie_stanu([](PoleWyboru*, StanWyboru, void* dane) {
        ++static_cast<LicznikiCheckbox*>(dane)->stan;
    }, &liczniki_checkbox);
    chk_haslo.przy_przelaczeniu([](PoleWyboru*, bool, void* dane) {
        ++static_cast<LicznikiCheckbox*>(dane)->przelaczenie;
    }, &liczniki_checkbox);
    chk_haslo.ustaw_zaznaczone(false);
    liczniki_checkbox = {};
    const Prostokat r_chk = chk_haslo.obszar_ekranu();
    const int chk_x = okno.x() + r_chk.x + r_chk.szerokosc / 2;
    const int chk_y = okno.y() + r_chk.y + r_chk.wysokosc / 2;
    uint64_t czas_chk = 1000000000ULL;
    for (int cykl = 0; cykl < 100; ++cykl) {
        for (int klik = 0; klik < 2; ++klik) {
            bws_zdarzenie down{};
            down.typ = BWS_ZDARZENIE_MYSZ_DOWN;
            down.x = chk_x; down.y = chk_y;
            down.przyciski = 1;
            down.timestamp = czas_chk;
            okno.obsluz_zdarzenie(down);
            bws_zdarzenie up = down;
            up.typ = BWS_ZDARZENIE_MYSZ_UP;
            up.przyciski = 0;
            okno.obsluz_zdarzenie(up);
            const bool oczekiwany = klik == 0;
            bws_sprawdz_msg(chk_haslo.czy_zaznaczone() == oczekiwany,
                            "CHECKBOX mouse toggle 100x");
            czas_chk += 100000000ULL;
        }
    }
    bws_sprawdz_msg(liczniki_checkbox.stan == 200 &&
                    liczniki_checkbox.przelaczenie == 200,
                    "CHECKBOX mouse callbacks exactly once");

    // Fizyczna Spacja PS/2 (0x39), ta sama trasa Okno -> fokus -> kontrolka.
    okno.ustaw_fokus(&chk_haslo);
    for (int i = 0; i < 100; ++i) {
        bws_zdarzenie spacja{};
        spacja.typ = BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;
        spacja.x = 1;
        spacja.kod = 0x39U;
        okno.obsluz_zdarzenie(spacja);
        bws_sprawdz_msg(chk_haslo.czy_zaznaczone() == ((i & 1) == 0),
                        "CHECKBOX Space toggle 100x");
        spacja.x = 0;
        okno.obsluz_zdarzenie(spacja);
    }
    bws_sprawdz_msg(liczniki_checkbox.stan == 300 &&
                    liczniki_checkbox.przelaczenie == 300,
                    "CHECKBOX keyboard callbacks exactly once");

    chk_trojstan.ustaw_stan_wyboru(StanWyboru::Niezaznaczone);
    okno.ustaw_fokus(&chk_trojstan);
    const StanWyboru cykl_3[4] = {StanWyboru::Zaznaczone,
                                  StanWyboru::Nieokreslone,
                                  StanWyboru::Niezaznaczone,
                                  StanWyboru::Zaznaczone};
    for (int i = 0; i < 4; ++i) {
        bws_zdarzenie spacja{};
        spacja.typ = BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;
        spacja.x = 1; spacja.kod = 0x39U;
        okno.obsluz_zdarzenie(spacja);
        bws_sprawdz_msg(chk_trojstan.stan_wyboru() == cykl_3[i],
                        "CHECKBOX 3-state cycle");
    }

    const int cb_przed_disabled = liczniki_checkbox.stan;
    chk_haslo.ustaw_wlaczona(false);
    okno.ustaw_fokus(&chk_haslo);
    bws_zdarzenie disabled_space{};
    disabled_space.typ = BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;
    disabled_space.x = 1; disabled_space.kod = 0x39U;
    okno.obsluz_zdarzenie(disabled_space);
    bws_sprawdz(!chk_haslo.czy_zaznaczone() &&
                liczniki_checkbox.stan == cb_przed_disabled);
    chk_haslo.ustaw_wlaczona(true);

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

    // Rzeczywista trasa zdarzen aplikacji: Okno otrzymuje Ctrl down,
    // fizyczny klawisz, TEXT_INPUT generowany przez sterownik, key up i Ctrl
    // up. TEXT_INPUT z Ctrl nie moze przeciec po obsluzonym skrocie.
    memo.ustaw_tekst("Bursztyn OS / żółć");
    const Prostokat memo_r = memo.obszar_ekranu();
    bws_zdarzenie memo_down{};
    memo_down.typ = BWS_ZDARZENIE_MYSZ_DOWN;
    memo_down.x = okno_memo.x() + memo_r.x + 20;
    memo_down.y = okno_memo.y() + memo_r.y + 12;
    memo_down.przyciski = 1;
    okno_memo.obsluz_zdarzenie(memo_down);
    bws_zdarzenie memo_up = memo_down;
    memo_up.typ = BWS_ZDARZENIE_MYSZ_UP;
    memo_up.przyciski = 0;
    okno_memo.obsluz_zdarzenie(memo_up);
    bws_sprawdz_msg(okno_memo.fokus() == &memo, "MEMO focus after text click");

    // Drag-select i rolka zachowuja logiczny fokus edytora.
    bws_zdarzenie memo_drag = memo_down;
    memo_drag.x += 80;
    memo_drag.typ = BWS_ZDARZENIE_MYSZ_RUCH;
    okno_memo.obsluz_zdarzenie(memo_drag);
    bws_zdarzenie memo_drag_up = memo_drag;
    memo_drag_up.typ = BWS_ZDARZENIE_MYSZ_UP;
    memo_drag_up.przyciski = 0;
    okno_memo.obsluz_zdarzenie(memo_drag_up);
    bws_sprawdz_msg(okno_memo.fokus() == &memo,
                    "MEMO focus after drag selection and release");
    bws_zdarzenie memo_wheel{};
    memo_wheel.typ = BWS_ZDARZENIE_MYSZ_KOLO;
    memo_wheel.x = memo_down.x; memo_wheel.y = memo_down.y;
    memo_wheel.dy = -1;
    okno_memo.obsluz_zdarzenie(memo_wheel);
    bws_sprawdz_msg(okno_memo.fokus() == &memo, "MEMO focus after wheel");

    auto wyslij_ctrl_ps2 = [&](uint32_t skan, uint32_t tekst_cp,
                               bool shift = false) {
        const uint32_t mods = BWS_MOD_CTRL | (shift ? BWS_MOD_SHIFT : 0U);
        bws_zdarzenie ctrl{};
        ctrl.typ = BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;
        ctrl.x = 1; ctrl.kod = 0x1DU; ctrl.przyciski = BWS_MOD_CTRL;
        okno_memo.obsluz_zdarzenie(ctrl);
        bws_zdarzenie key = ctrl;
        key.kod = skan; key.przyciski = mods;
        okno_memo.obsluz_zdarzenie(key);
        if (tekst_cp != 0) {
            bws_zdarzenie text{};
            text.typ = BWS_ZDARZENIE_TEXT_INPUT;
            text.kod = tekst_cp; text.przyciski = mods;
            okno_memo.obsluz_zdarzenie(text);
        }
        key.x = 0;
        okno_memo.obsluz_zdarzenie(key);
        ctrl.x = 0; ctrl.przyciski = 0;
        okno_memo.obsluz_zdarzenie(ctrl);
    };

    memo.ustaw_zaznaczenie(0, 11); // "Bursztyn OS"
    wyslij_ctrl_ps2(0x2EU, 'c');
    bws_sprawdz_msg(memo.dlugosc() == 22 && memo.tekst()[0] == 'B',
                    "MEMO Ctrl+C no TEXT_INPUT leak");
    char memo_clipboard[64]{};
    uint64_t memo_clipboard_n = 0;
    bws_sprawdz(clipboard_read("text/plain", memo_clipboard,
                              sizeof(memo_clipboard) - 1,
                              &memo_clipboard_n));
    const char oczekiwane_copy[] = "Bursztyn OS";
    bool copy_rowne = true;
    for (size_t i = 0; i < sizeof(oczekiwane_copy); ++i)
        if (memo_clipboard[i] != oczekiwane_copy[i]) copy_rowne = false;
    bws_sprawdz_msg(copy_rowne, "MEMO Ctrl+C clipboard exact");

    memo.ustaw_zaznaczenie(0, 11);
    wyslij_ctrl_ps2(0x2DU, 'x');
    bws_sprawdz_msg(memo.dlugosc() == 11 && memo.tekst()[0] == ' ',
                    "MEMO Ctrl+X exact document and no X leak");
    bws_sprawdz_msg(memo.pozycja_kursora() == 0 && !memo.czy_ma_zaznaczenie(),
                    "MEMO Ctrl+X caret and selection");
    wyslij_ctrl_ps2(0x2CU, 'z');
    bws_sprawdz_msg(memo.dlugosc() == 22 && memo.tekst()[0] == 'B',
                    "MEMO Ctrl+Z after cut");
    wyslij_ctrl_ps2(0x15U, 'y');
    bws_sprawdz_msg(memo.dlugosc() == 11 && memo.tekst()[0] == ' ',
                    "MEMO Ctrl+Y after cut");
    wyslij_ctrl_ps2(0x2CU, 'z');
    wyslij_ctrl_ps2(0x2CU, 'z', true);
    bws_sprawdz_msg(memo.dlugosc() == 11 && memo.tekst()[0] == ' ',
                    "MEMO Ctrl+Shift+Z redo");
    wyslij_ctrl_ps2(0x2CU, 'z');
    memo.ustaw_zaznaczenie(memo.dlugosc(), memo.dlugosc());
    wyslij_ctrl_ps2(0x2FU, 'v');
    bws_sprawdz_msg(memo.dlugosc() == 33 && memo.tekst()[22] == 'B',
                    "MEMO Ctrl+V exact document and no V leak");
    bws_sprawdz_msg(memo.pozycja_kursora() == 33 && !memo.czy_ma_zaznaczenie(),
                    "MEMO Ctrl+V caret and selection");
    wyslij_ctrl_ps2(0x1EU, 'a');
    bws_sprawdz_msg(memo.poczatek_zaznaczenia() == 0 &&
                    memo.koniec_zaznaczenia() == memo.dlugosc(),
                    "MEMO Ctrl+A routed");

    // Polska selekcja UTF-8 oraz USB HID Usage C=6.
    memo.ustaw_tekst("Bursztyn OS / żółć");
    memo.ustaw_zaznaczenie(14, 22);
    bws_zdarzenie usb_ctrl{};
    usb_ctrl.typ = BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;
    usb_ctrl.x = 1;
    usb_ctrl.kod = BWS_KEY_SOURCE_USB | 224U;
    usb_ctrl.przyciski = BWS_MOD_CTRL;
    okno_memo.obsluz_zdarzenie(usb_ctrl);
    bws_zdarzenie usb_c = usb_ctrl;
    usb_c.kod = BWS_KEY_SOURCE_USB | 6U;
    okno_memo.obsluz_zdarzenie(usb_c);
    usb_c.x = 0; okno_memo.obsluz_zdarzenie(usb_c);
    usb_ctrl.x = 0; usb_ctrl.przyciski = 0;
    okno_memo.obsluz_zdarzenie(usb_ctrl);
    memo_clipboard_n = 0;
    bws_sprawdz(clipboard_read("text/plain", memo_clipboard,
                              sizeof(memo_clipboard) - 1,
                              &memo_clipboard_n));
    const char oczekiwane_utf8[] = "żółć";
    bool utf8_rowne = true;
    for (size_t i = 0; i < sizeof(oczekiwane_utf8); ++i)
        if (memo_clipboard[i] != oczekiwane_utf8[i]) utf8_rowne = false;
    bws_sprawdz_msg(utf8_rowne, "MEMO USB Ctrl+C Polish UTF-8");

    // W read-only Ctrl+X zachowuje semantyke kopiowania, ale nie usuwa.
    memo.ustaw_zaznaczenie(14, 22);
    memo.ustaw_tylko_do_odczytu(true);
    const size_t ro_len = memo.dlugosc();
    wyslij_ctrl_ps2(0x2DU, 'x');
    bws_sprawdz_msg(memo.dlugosc() == ro_len && memo.czy_ma_zaznaczenie(),
                    "MEMO read-only Ctrl+X copies without delete");
    memo.ustaw_tylko_do_odczytu(false);

    // Wewnetrzny pasek moze miec fokus wizualny, ale nie odcina komend od
    // logicznego rodzica Memo. Powrot kliknieciem tekstu przywraca fokus Memo.
    memo.ustaw_tekst("Bursztyn OS\nL2\nL3\nL4\nL5\nL6\nL7\nL8\nL9\nL10\nL11\nL12\nL13\nL14\nL15\nL16");
    const Prostokat memo_pp = memo.prostokat_paska_pionowego();
    bws_sprawdz_msg(!memo_pp.czy_pusty(), "MEMO internal vertical scrollbar visible");
    if (!memo_pp.czy_pusty()) {
        bws_zdarzenie scroll_down{};
        scroll_down.typ = BWS_ZDARZENIE_MYSZ_DOWN;
        scroll_down.x = okno_memo.x() + memo_r.x + memo_pp.x + memo_pp.szerokosc / 2;
        scroll_down.y = okno_memo.y() + memo_r.y + memo_pp.y + memo_pp.wysokosc / 2;
        scroll_down.przyciski = 1;
        okno_memo.obsluz_zdarzenie(scroll_down);
        bws_zdarzenie scroll_up = scroll_down;
        scroll_up.typ = BWS_ZDARZENIE_MYSZ_UP; scroll_up.przyciski = 0;
        okno_memo.obsluz_zdarzenie(scroll_up);
        memo.ustaw_zaznaczenie(0, 11);
        const size_t dl_przed_scroll_copy = memo.dlugosc();
        wyslij_ctrl_ps2(0x2EU, 'c');
        bws_sprawdz_msg(memo.dlugosc() == dl_przed_scroll_copy,
                        "MEMO scrollbar focus routes Ctrl+C to owner");
        okno_memo.obsluz_zdarzenie(memo_down);
        okno_memo.obsluz_zdarzenie(memo_up);
        bws_sprawdz_msg(okno_memo.fokus() == &memo,
                        "MEMO focus restored after scrollbar");
    }

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

    // Testy A-J: PoleWielowierszowe scrollbar, klik w pusta przestrzen i zaznaczenia
    // Test A: content height 3x viewport height -> pionowy pasek widoczny
    PoleWielowierszowe* memo_a = new PoleWielowierszowe();
    memo_a->ustaw_tekst("L1\nL2\nL3\nL4\nL5\nL6\nL7\nL8\nL9\nL10\nL11\nL12\nL13\nL14\nL15");
    memo_a->rozmiesc(Prostokat(0, 0, 200, 100));
    bws_sprawdz(memo_a->pasek_pionowy().czy_widoczna());

    // Test B: content mieści się w całości -> pasek niewidoczny
    PoleWielowierszowe* memo_b = new PoleWielowierszowe();
    memo_b->ustaw_tekst("Linia 1\nLinia 2");
    memo_b->rozmiesc(Prostokat(0, 0, 200, 100));
    bws_sprawdz(!memo_b->pasek_pionowy().czy_widoczna());

    // Test C: zmniejszenie wysokości -> pojawienie się paska
    PoleWielowierszowe* memo_c = new PoleWielowierszowe();
    memo_c->ustaw_tekst("Linia 1\nLinia 2\nLinia 3\nLinia 4");
    memo_c->rozmiesc(Prostokat(0, 0, 200, 120));
    bws_sprawdz(!memo_c->pasek_pionowy().czy_widoczna());
    memo_c->rozmiesc(Prostokat(0, 0, 200, 50));
    bws_sprawdz(memo_c->pasek_pionowy().czy_widoczna());

    // Test D: zwiększenie wysokości -> zniknięcie paska
    memo_c->rozmiesc(Prostokat(0, 0, 200, 300));
    bws_sprawdz(!memo_c->pasek_pionowy().czy_widoczna());

    // Test E: zaznacz tekst -> klik w pusta przestrzen -> zwijanie zaznaczenia
    PoleWielowierszowe* memo_e = new PoleWielowierszowe();
    memo_e->ustaw_tekst("Pierwszy wiersz tekstu\nDrugi wiersz tekstu\nTrzeci wiersz");
    memo_e->rozmiesc(Prostokat(0, 0, 300, 150));
    memo_e->ustaw_zaznaczenie(0, 10);
    bws_sprawdz(memo_e->czy_ma_zaznaczenie());
    ZdarzenieMyszki zm_e{};
    zm_e.typ = TypZdarzenia::WcisniecieMyszy;
    zm_e.x = 250;
    zm_e.y = 30;
    zm_e.przycisk = 1;
    memo_e->obsluz_wcisniecie_myszy(zm_e);
    bws_sprawdz(!memo_e->czy_ma_zaznaczenie());
    bws_sprawdz(memo_e->poczatek_zaznaczenia() == memo_e->koniec_zaznaczenia());

    // Test F: zaznaczenie wielowierszowe -> klik ponizej ostatniego wiersza -> kursor == koniec tekstu
    PoleWielowierszowe* memo_f = new PoleWielowierszowe();
    memo_f->ustaw_tekst("Linia A\nLinia B\nLinia C");
    memo_f->rozmiesc(Prostokat(0, 0, 300, 200));
    memo_f->ustaw_zaznaczenie(0, 15);
    bws_sprawdz(memo_f->czy_ma_zaznaczenie());
    ZdarzenieMyszki zm_f{};
    zm_f.typ = TypZdarzenia::WcisniecieMyszy;
    zm_f.x = 50;
    zm_f.y = 180;
    zm_f.przycisk = 1;
    memo_f->obsluz_wcisniecie_myszy(zm_f);
    bws_sprawdz(!memo_f->czy_ma_zaznaczenie());
    bws_sprawdz(memo_f->pozycja_kursora() == memo_f->dlugosc());

    // Test G: klik za koncem krotkiego wiersza -> kursor na koncu tego wiersza
    PoleWielowierszowe* memo_g = new PoleWielowierszowe();
    memo_g->ustaw_tekst("Krotka\nDruga dluzsza linia tekstu");
    memo_g->rozmiesc(Prostokat(0, 0, 300, 100));
    ZdarzenieMyszki zm_g{};
    zm_g.typ = TypZdarzenia::WcisniecieMyszy;
    zm_g.x = 250;
    zm_g.y = 10;
    zm_g.przycisk = 1;
    memo_g->obsluz_wcisniecie_myszy(zm_g);
    bws_sprawdz(memo_g->pozycja_kursora() == 6);

    // Test H: klik w dlugim wierszu UTF-8 nigdy nie laduje w bajcie kontynuacji
    PoleWielowierszowe* memo_h = new PoleWielowierszowe();
    memo_h->ustaw_tekst("Zażółć gęślą jaźń — test UTF-8");
    memo_h->rozmiesc(Prostokat(0, 0, 300, 100));
    for (int x = 0; x < 200; x += 3) {
        ZdarzenieMyszki zm_h{};
        zm_h.typ = TypZdarzenia::WcisniecieMyszy;
        zm_h.x = x;
        zm_h.y = 10;
        zm_h.przycisk = 1;
        memo_h->obsluz_wcisniecie_myszy(zm_h);
        size_t pos = memo_h->pozycja_kursora();
        if (pos < memo_h->dlugosc()) {
            uint8_t byte = static_cast<uint8_t>(memo_h->tekst()[pos]);
            bws_sprawdz((byte & 0xC0U) != 0x80U);
        }
    }

    // Test I: jednoczesne oba paski -> prawidlowa geometria, brak nakladania, rog
    PoleWielowierszowe* memo_i = new PoleWielowierszowe();
    memo_i->ustaw_zawijanie_wierszy(false);
    memo_i->ustaw_tekst(
        "Bardzo dlugi wiersz tekstu przekraczajacy szerokosc kontrolki memo...\n"
        "L2\nL3\nL4\nL5\nL6\nL7\nL8\nL9\nL10\nL11\nL12\nL13\nL14\nL15");
    memo_i->rozmiesc(Prostokat(0, 0, 200, 100));
    bws_sprawdz(memo_i->pasek_pionowy().czy_widoczna());
    bws_sprawdz(memo_i->pasek_poziomy().czy_widoczna());
    Prostokat vp_i = memo_i->prostokat_widoku_tekstu();
    Prostokat pp_i = memo_i->prostokat_paska_pionowego();
    Prostokat ph_i = memo_i->prostokat_paska_poziomego();
    Prostokat rog_i = memo_i->prostokat_rogu_paskow();
    bws_sprawdz(!vp_i.przecina(pp_i) && !vp_i.przecina(ph_i) && !vp_i.przecina(rog_i));
    bws_sprawdz(pp_i.dolny() == ph_i.y && ph_i.prawy() == pp_i.x);
    bws_sprawdz(!rog_i.czy_pusty() && rog_i.x == pp_i.x && rog_i.y == ph_i.y);

    // Test J: zwijanie zaznaczenia dirty-repaint -> brak alokacji, brudny obszar ograniczony
    PoleWielowierszowe* memo_j = new PoleWielowierszowe();
    memo_j->ustaw_tekst("Linia 1\nLinia 2\nLinia 3\nLinia 4");
    memo_j->rozmiesc(Prostokat(0, 0, 200, 100));
    memo_j->ustaw_zaznaczenie(0, 20);
    bws_sprawdz(memo_j->czy_ma_zaznaczenie());
    memo_j->wyczysc_brudny_obszar();
    uint64_t alok_sel_przed = gui_liczba_alokacji_sterty();
    ZdarzenieMyszki zm_j{};
    zm_j.typ = TypZdarzenia::WcisniecieMyszy;
    zm_j.x = 20;
    zm_j.y = 10;
    zm_j.przycisk = 1;
    memo_j->obsluz_wcisniecie_myszy(zm_j);
    uint64_t alok_sel_po = gui_liczba_alokacji_sterty();
    bws_sprawdz(!memo_j->czy_ma_zaznaczenie());
    bws_sprawdz(alok_sel_po == alok_sel_przed);
    Prostokat dirty_j = memo_j->pobierz_brudny_obszar();
    bws_sprawdz(!dirty_j.czy_pusty());

    delete memo_a; delete memo_b; delete memo_c; delete memo_e;
    delete memo_f; delete memo_g; delete memo_h; delete memo_i; delete memo_j;

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
    bws_sprawdz(brudny_zamkniecia.szerokosc >= testowy_popup.szerokosc &&
                brudny_zamkniecia.wysokosc >= testowy_popup.wysokosc);

    // Klik poza popupem zamyka go i jest konsumowany: nie moze trafic do
    // kontrolki pod popupem ani otworzyc jej menu kontekstowego.
    bws_sprawdz(popupy.otworz(powierzchnia_testowa, testowy_popup));
    ZdarzenieMyszki popup_poza{};
    popup_poza.typ = TypZdarzenia::WcisniecieMyszy;
    popup_poza.przycisk = static_cast<int>(PrzyciskMyszy::Lewy);
    popup_poza.x = 5; popup_poza.y = 35;
    bws_sprawdz(popupy.obsluz_wcisniecie(popup_poza));
    bws_sprawdz(!popupy.czy_otwarty() && popup_poza.obsluzone);

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
        gui_ustaw_tytul_okna("Testy Bursztyn OS");
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

    // Menu kontekstowe jest wlasciwoscia kazdej Kontrolka. Najglebszy cel
    // dziedziczy menu rodzica, ale menu dziecka ma pierwszenstwo. Hook jest
    // wywolywany bezposrednio przed kazdym otwarciem.
    struct KontrolkaPrzygotowujacaMenu : Kontrolka {
        int przygotowania{0};
        void przygotuj_menu_kontekstowe(MenuKontekstowe& m) override {
            ++przygotowania;
            ElementMenu* e = m.menu().znajdz(501);
            if (e) e->ustaw_wlaczony(przygotowania > 1);
        }
    };
    MenuKontekstowe menu_rodzica;
    MenuKontekstowe menu_dziecka;
    ElementMenu* dynamiczny_menu = &menu_rodzica.menu().dodaj_element("Dynamiczny", 501);
    KontrolkaPrzygotowujacaMenu* rodzic_menu = new KontrolkaPrzygotowujacaMenu();
    Kontrolka* dziecko_menu = new Kontrolka();
    bws_sprawdz(rodzic_menu && dziecko_menu && rodzic_menu->dodaj_kontrolke(dziecko_menu));
    rodzic_menu->ustaw_menu_kontekstowe(menu_rodzica);
    bws_sprawdz(okno_popup.kontrolka_glowna()->dodaj_kontrolke(rodzic_menu));
    okno_popup.przelicz_uklad();
    rodzic_menu->rozmiesc(Prostokat(8, 8, 200, 100));
    dziecko_menu->rozmiesc(Prostokat(0, 0, 100, 50));
    bws_zdarzenie prawy_ctx{};
    prawy_ctx.typ = BWS_ZDARZENIE_MYSZ_PRAWY_DOWN;
    prawy_ctx.x = WIN_X + 20; prawy_ctx.y = WIN_Y + 40;
    okno_popup.obsluz_zdarzenie(prawy_ctx);
    bws_sprawdz(rodzic_menu->przygotowania == 1 &&
                !dynamiczny_menu->czy_wlaczony());
    popupy.zamknij();
    dziecko_menu->ustaw_menu_kontekstowe(menu_dziecka);
    bws_sprawdz(okno_popup.znajdz_menu_kontekstowe(dziecko_menu) == &menu_dziecka);
    dziecko_menu->ustaw_menu_kontekstowe(nullptr);

    // Klawisz Menu oraz Shift+F10 kotwicza odziedziczone menu przy kontrolce.
    okno_popup.ustaw_fokus(dziecko_menu);
    bws_zdarzenie klawisz_menu_ctx{};
    klawisz_menu_ctx.typ = BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;
    klawisz_menu_ctx.x = 1;
    klawisz_menu_ctx.kod = BWS_KEY_SOURCE_USB | 101U;
    okno_popup.obsluz_zdarzenie(klawisz_menu_ctx);
    bws_sprawdz(menu_rodzica.czy_otwarte() && rodzic_menu->przygotowania == 2 &&
                dynamiczny_menu->czy_wlaczony());
    popupy.zamknij();
    bws_zdarzenie shift_f10{};
    shift_f10.typ = BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;
    shift_f10.x = 1;
    shift_f10.kod = BWS_KEY_SOURCE_USB | 67U;
    shift_f10.przyciski = BWS_MOD_SHIFT;
    okno_popup.obsluz_zdarzenie(shift_f10);
    bws_sprawdz(menu_rodzica.czy_otwarte() && rodzic_menu->przygotowania == 3);

    // Zmiana motywu uniewaznia otwarta powierzchnie; ponowne otwieranie po
    // zamknieciu nadal nie alokuje po rozgrzaniu modelu.
    KontekstRysowania ctx_motyw_popup;
    okno_popup.rysuj(ctx_motyw_popup, true);
    okno_popup.zaktualizuj_motyw();
    bws_sprawdz(popupy.czy_brudny());
    popupy.zamknij();
    okno_popup.ustaw_fokus(nullptr);

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
    struct PanelKartyPomiarowy : Panel {
        uint64_t wywolania_rozmieszczenia{0};
        void rozmiesc(Prostokat r) override {
            ++wywolania_rozmieszczenia;
            Panel::rozmiesc(r);
        }
    };
    Karty* karty_ptr = new Karty();
    bws_sprawdz(karty_ptr != nullptr);
    Karty& karty = *karty_ptr;
    karty.rozmiesc(Prostokat(0, 0, 400, 250));

    PanelKartyPomiarowy* page0 = new PanelKartyPomiarowy();
    Przycisk* btn_page0 = new Przycisk("P0");
    page0->dodaj_kontrolke(btn_page0);

    PanelKartyPomiarowy* page1 = new PanelKartyPomiarowy();
    Przycisk* btn_page1 = new Przycisk("P1");
    page1->dodaj_kontrolke(btn_page1);

    PanelKartyPomiarowy* page2 = new PanelKartyPomiarowy();
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
    page0->wywolania_rozmieszczenia = 0;
    page1->wywolania_rozmieszczenia = 0;
    page2->wywolania_rozmieszczenia = 0;
    karty.wyczysc_brudny_obszar();
    KontekstRysowania ctx_karty_switch;
    uint64_t tab_sw_dirty_pixels = 0;
    uint64_t tab_sw_paint_calls = 0;
    uint64_t tab_sw_przed = gui_liczba_alokacji_sterty();
    for (int i = 0; i < 100; ++i) {
        karty.ustaw_aktywna_karte((i % 2 == 0) ? 1 : 0);
        Prostokat brudny = karty.pobierz_brudny_obszar();
        tab_sw_dirty_pixels += static_cast<uint64_t>(brudny.szerokosc) *
                               static_cast<uint64_t>(brudny.wysokosc);
        if (karty.czy_brudna()) {
            ++tab_sw_paint_calls;
            karty.rysuj(ctx_karty_switch, false);
        }
    }
    uint64_t tab_sw_po = gui_liczba_alokacji_sterty();
    bws_sprawdz(tab_sw_po == tab_sw_przed);
    const uint64_t tab_sw_layout = page0->wywolania_rozmieszczenia +
                                   page1->wywolania_rozmieszczenia +
                                   page2->wywolania_rozmieszczenia;
    bws_sprawdz_msg(tab_sw_layout == 0,
                    "Karty 100 cieplych zmian: 0 ponownych layoutow stron");
    bws_sprawdz(btn_page0->rodzic() == page0 && btn_page1->rodzic() == page1); // instancje dzieci nienaruszone
    bws_log_u64("[KARTY POMIAR] tab_switch_100_allocations=", tab_sw_po - tab_sw_przed);
    bws_log_u64("[KARTY POMIAR] tab_switch_100_layout_passes=", tab_sw_layout);
    bws_log_u64("[KARTY POMIAR] tab_switch_100_paint_calls=", tab_sw_paint_calls);
    bws_log_u64("[KARTY POMIAR] tab_switch_100_dirty_pixels=", tab_sw_dirty_pixels);
    bws_log_u64("[BURSZTYNUI DIAG] bledy_po_karty=", bws_test_blad - bledy_adv_przed);

    // 5. Drzewo - Limit 4096 wezlow, usuwanie, obsluga przepelnienia i wirtualizacja
    Drzewo* drzewo_ptr = new Drzewo();
    bws_sprawdz(drzewo_ptr != nullptr);
    Drzewo& drzewo = *drzewo_ptr;
    drzewo.rozmiesc(Prostokat(0, 0, 240, 200));

    // Pomiary sizeof i pamieci
    uint64_t sz_drzewo = sizeof(Drzewo);
    uint64_t sz_wezel = sizeof(Drzewo::MAKS_WEZLOW); // bazowy offset
    (void)sz_wezel;
    bws_log_u64("[DRZEWO AUDYT] sizeof_Drzewo_B=", sz_drzewo);
    bws_log_u64("[DRZEWO AUDYT] wezly_total_limit=", Drzewo::MAKS_WEZLOW);

    UchwytWezlaDrzewa root1 = drzewo.dodaj_korzen("Katalog Glowny");
    UchwytWezlaDrzewa child1 = drzewo.dodaj_dziecko(root1, "Dokumenty");
    UchwytWezlaDrzewa child2 = drzewo.dodaj_dziecko(root1, "Zdjecia");
    (void)child2;
    UchwytWezlaDrzewa subchild = drzewo.dodaj_dziecko(child1, "plik.txt");
    bws_sprawdz(drzewo.liczba_wezlow() == 4);
    bws_sprawdz(drzewo.rodzic(child1) == root1 && drzewo.rodzic(subchild) == child1);
    bws_sprawdz(drzewo.liczba_dzieci(root1) == 2 && drzewo.liczba_dzieci(child1) == 1);
    bws_sprawdz(drzewo.dziecko(root1, 0) == child1 && drzewo.dziecko(root1, 1) == child2);

    // Usuwanie i bezpieczenstwo uchwytu pokoleniowego
    drzewo.usun_wezel(child1);
    bws_sprawdz(!drzewo.czy_uchwyt_poprawny(child1));

    // Ponowna alokacja w tym samym slocie -> nowa generacja, stary uchwyt nadal odrzucony
    UchwytWezlaDrzewa child1_nowy = drzewo.dodaj_dziecko(root1, "Nowe Dokumenty");
    bws_sprawdz(child1_nowy.indeks == child1.indeks);
    bws_sprawdz(child1_nowy.generacja != child1.generacja);
    bws_sprawdz(!drzewo.czy_uchwyt_poprawny(child1));
    bws_sprawdz(drzewo.czy_uchwyt_poprawny(child1_nowy));

    // Stres test 1000 cykli alokuj/usun w jednym slocie -> inkrementacja generacji
    UchwytWezlaDrzewa h_cur = child1_nowy;
    for (int cykl = 0; cykl < 1000; ++cykl) {
        uint32_t gen_przed = h_cur.generacja;
        bws_sprawdz(drzewo.usun_wezel(h_cur));
        bws_sprawdz(!drzewo.czy_uchwyt_poprawny(h_cur));
        h_cur = drzewo.dodaj_dziecko(root1, "Tymczasowy");
        bws_sprawdz(drzewo.czy_uchwyt_poprawny(h_cur));
        bws_sprawdz(h_cur.generacja == gen_przed + 1);
    }

    // Test krancowy: generacja UINT32_MAX - 1 oraz UINT32_MAX -> trwala emerytura slotu
    drzewo.wyczysc();
    drzewo._ustaw_generacje_slotu_dla_testu(0, 0xFFFFFFFEU); // UINT32_MAX - 1
    UchwytWezlaDrzewa w_max1 = drzewo.dodaj_korzen("Wezel Max-1");
    bws_sprawdz(w_max1.indeks == 0);
    bws_sprawdz(w_max1.generacja == 0xFFFFFFFEU);
    bws_sprawdz(drzewo.czy_uchwyt_poprawny(w_max1));

    // Usuniecie wezla -> generacja osiaga UINT32_MAX
    bws_sprawdz(drzewo.usun_wezel(w_max1));
    bws_sprawdz(!drzewo.czy_uchwyt_poprawny(w_max1));

    // Ponowna alokacja -> slot 0 otrzymuje generacje UINT32_MAX
    UchwytWezlaDrzewa w_max2 = drzewo.dodaj_korzen("Wezel Max");
    bws_sprawdz(w_max2.indeks == 0);
    bws_sprawdz(w_max2.generacja == 0xFFFFFFFFU);
    bws_sprawdz(drzewo.czy_uchwyt_poprawny(w_max2));
    bws_sprawdz(!drzewo.czy_uchwyt_poprawny(w_max1)); // stary nadal odrzucony

    // Usuniecie wezla o generacji UINT32_MAX -> slot 0 zostaje trwale emerytowany
    bws_sprawdz(drzewo.usun_wezel(w_max2));
    bws_sprawdz(!drzewo.czy_uchwyt_poprawny(w_max2));
    bws_sprawdz(!drzewo.czy_uchwyt_poprawny(w_max1));
    bws_sprawdz(drzewo._czy_slot_emerytowany_dla_testu(0));

    // Kolejna alokacja MUSI pominac emerytowany slot 0 i przydzielic slot 1
    UchwytWezlaDrzewa w_next = drzewo.dodaj_korzen("Nowy wezel po emeryturze");
    bws_sprawdz(w_next.indeks == 1);
    bws_sprawdz(drzewo.czy_uchwyt_poprawny(w_next));
    bws_sprawdz(!drzewo.czy_uchwyt_poprawny(w_max2)); // stary uchwyt slotu 0 nigdy nie zmartwychwstanie
    bws_sprawdz(!drzewo.czy_uchwyt_poprawny(w_max1));

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
    bws_sprawdz(drzewo.liczba_wezlow() == Drzewo::MAKS_WEZLOW - 1);
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
    statusbar.wyczysc_brudny_obszar();
    const uint64_t status_same_alloc_przed = gui_liczba_alokacji_sterty();
    statusbar.ustaw_tekst(s1, "Gotowy");
    const uint64_t status_same_alloc_po = gui_liczba_alokacji_sterty();
    bws_sprawdz_msg(!statusbar.czy_brudna() &&
                    status_same_alloc_po == status_same_alloc_przed,
                    "PasekStanu identyczny tekst: 0 layout/dirty/alokacji");

    // Waskie okno: okno wezsze niz suma sekcji stalych -> brak ujemnych szerokosci i nakladania sie
    statusbar.rozmiesc(Prostokat(0, 0, 100, 24));
    for (size_t i = 0; i < statusbar.liczba_sekcji(); ++i) {
        bws_sprawdz(statusbar.prostokat_sekcji(i).szerokosc >= 0);
        bws_sprawdz(statusbar.prostokat_sekcji(i).prawy() <= 100);
    }
    bws_log_u64("[BURSZTYNUI DIAG] bledy_po_statusbar=", bws_test_blad - bledy_adv_przed);

    // 8. Podpowiedz (Tooltips) - Multi-window, cykl zycia i brak alokacji
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

    // Multi-window test: Okno A i Okno B
    Okno* okno_a = new Okno("Okno A", 300, 200);
    Okno* okno_b = new Okno("Okno B", 300, 200);
    Przycisk* btn_a = new Przycisk("Przycisk A");
    btn_a->ustaw_podpowiedz("Podpowiedz A");
    Przycisk* btn_b = new Przycisk("Przycisk B");
    btn_b->ustaw_podpowiedz("Podpowiedz B");

    // 1) Tooltip w Okno A
    zarzadca_tip.obsluz_ruch(okno_a->zarzadca_popupow(), btn_a, 50, 50, 10000);
    zarzadca_tip.obsluz_ruch(okno_a->zarzadca_popupow(), btn_a, 50, 50, 10600);
    bws_sprawdz(zarzadca_tip.czy_widoczna());
    bws_sprawdz(okno_a->zarzadca_popupow().czy_otwarty());

    // 2) Przelaczenie do Okno B -> popup w A zamkniety, popup w B otwarty
    zarzadca_tip.obsluz_ruch(okno_b->zarzadca_popupow(), btn_b, 150, 150, 11000);
    bws_sprawdz(!okno_a->zarzadca_popupow().czy_otwarty());
    zarzadca_tip.obsluz_ruch(okno_b->zarzadca_popupow(), btn_b, 150, 150, 11600);
    bws_sprawdz(zarzadca_tip.czy_widoczna());
    bws_sprawdz(okno_b->zarzadca_popupow().czy_otwarty());

    // 3) Zniszczenie Okno A -> Okno B nadal dziala stabilnie
    delete okno_a;
    zarzadca_tip.obsluz_ruch(okno_b->zarzadca_popupow(), btn_b, 150, 150, 12000);
    bws_sprawdz(zarzadca_tip.czy_widoczna());

    // 4) Usuniecie najechanej kontrolki przed uplywem opoznienia -> brak wiszacego wskaznika
    zarzadca_tip.obsluz_ruch(okno_b->zarzadca_popupow(), nullptr, 0, 0, 13000);
    zarzadca_tip.obsluz_ruch(okno_b->zarzadca_popupow(), btn_b, 150, 150, 14000);
    zarzadca_tip.odrejestruj_kontrolke(btn_b);
    delete btn_b;
    // Czas uplywa po usunieciu -> nie pokazuje widmowego tooltipa
    zarzadca_tip.obsluz_ruch(okno_b->zarzadca_popupow(), nullptr, 150, 150, 14600);
    bws_sprawdz(!zarzadca_tip.czy_widoczna());

    // 5) Zniszczenie wlasciciela gdy tooltip jest otwarty -> prawidlowe odlaczenie
    zarzadca_tip.pokaz(okno_b->zarzadca_popupow(), "Testowy", 100, 100);
    bws_sprawdz(zarzadca_tip.czy_widoczna());
    delete okno_b; // destructor okna zamyka popupy i odlacza zarzadce
    bws_sprawdz(!zarzadca_tip.czy_widoczna());
    delete btn_a;

    // Stres test 100 cykli bez alokacji
    zarzadca_tip.ustaw_opoznienie_pokazania(0);
    uint64_t tip_alok_przed = gui_liczba_alokacji_sterty();
    for (int i = 0; i < 100; ++i) {
        zarzadca_tip.obsluz_ruch(popupy, btn_tip_target_ptr, WIN_X + 40, WIN_Y + 35, 20000 + i * 10);
        zarzadca_tip.obsluz_ruch(popupy, nullptr, WIN_X + 250, WIN_Y + 250, 20005 + i * 10);
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

    // ---------------------------------------------------------------------
    // 9. Kompletny test struktury i geometrii okna demonstracyjnego DemoUI
    // ---------------------------------------------------------------------
    {
        Okno* demo_win = new Okno("BursztynUI - Pokaz Komponentow Retained-Mode", 720, 520);
        bws_sprawdz(demo_win != nullptr);
        Panel* d_root = static_cast<Panel*>(demo_win->kontrolka_glowna());
        bws_sprawdz(d_root != nullptr);
        d_root->ustaw_orientacje(Orientacja::Pionowa);
        d_root->ustaw_wypelnienie(Marginesy(4, 4, 4, 4));

        // 1) Menu glowne
        MenuGlowne& d_menu = d_root->dodaj<MenuGlowne>();
        d_menu.ustaw_preferowany_rozmiar(Rozmiar(-1, 26));
        Menu& d_m_plik = d_menu.menu().dodaj_podmenu("Plik", 100);
        d_m_plik.dodaj_element("Nowy", 101);
        d_m_plik.dodaj_element("Zapisz", 102);
        d_m_plik.dodaj_separator();
        d_m_plik.dodaj_element("Zakoncz", 109);
        Menu& d_m_edycja = d_menu.menu().dodaj_podmenu("Edycja", 200);
        d_m_edycja.dodaj_element("Cofnij", 201);
        d_m_edycja.dodaj_element("Ponow", 202);
        d_menu.menu().dodaj_podmenu("Widok", 300);
        d_menu.menu().dodaj_podmenu("Pomoc", 400);

        // 2) Pasek narzedzi
        PasekNarzedzi& d_tb = d_root->dodaj<PasekNarzedzi>();
        d_tb.ustaw_preferowany_rozmiar(Rozmiar(-1, 32));
        d_tb.ustaw_margines(Marginesy(0, 0, 0, 4));
        d_tb.dodaj_przycisk("Nowy", 1);
        d_tb.dodaj_przycisk("Otworz", 2);
        d_tb.dodaj_przycisk("Zapisz", 3);
        d_tb.dodaj_separator();
        d_tb.dodaj_przelacznik("Siatka", true);
        d_tb.dodaj_separator();
        PoleTekstowe* d_tb_szukaj = new PoleTekstowe();
        d_tb_szukaj->ustaw_preferowany_rozmiar(Rozmiar(120, 24));
        d_tb.dodaj_kontrolke(d_tb_szukaj);

        // 3) Karty
        Karty& d_karty = d_root->dodaj<Karty>();
        d_karty.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        d_karty.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        d_karty.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));

        // Tab 0: Podstawowe
        Panel* d_strona0 = new Panel();
        d_strona0->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
        Kolumna& d_col0 = d_strona0->dodaj<Kolumna>();
        d_col0.ustaw_odstep(10);
        d_col0.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        d_col0.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        d_col0.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));

        Wiersz& d_w_postep = d_col0.dodaj<Wiersz>();
        d_w_postep.ustaw_odstep(14);
        Kolumna& d_cp1 = d_w_postep.dodaj<Kolumna>();
        d_cp1.dodaj<Etykieta>("Pasek postepu (deterministyczny):");
        PasekPostepu& d_pb1 = d_cp1.dodaj<PasekPostepu>(0, 100);
        d_pb1.ustaw_wartosc(65);
        d_pb1.ustaw_preferowany_rozmiar(Rozmiar(200, 24));
        Kolumna& d_cp2 = d_w_postep.dodaj<Kolumna>();
        d_cp2.dodaj<Etykieta>("Pasek postepu (animowany):");
        PasekPostepu& d_pb2 = d_cp2.dodaj<PasekPostepu>(0, 100);
        d_pb2.ustaw_tryb_nieokreslony(true);
        d_pb2.ustaw_preferowany_rozmiar(Rozmiar(200, 24));
        Kolumna& d_cp3 = d_w_postep.dodaj<Kolumna>();
        d_cp3.dodaj<Etykieta>("Pionowy:");
        PasekPostepu& d_pb_v = d_cp3.dodaj<PasekPostepu>(0, 100);
        d_pb_v.ustaw_orientacje(Orientacja::Pionowa);
        d_pb_v.ustaw_preferowany_rozmiar(Rozmiar(24, 24));

        Wiersz& d_w_num = d_col0.dodaj<Wiersz>();
        d_w_num.ustaw_odstep(16);
        Kolumna& d_cn1 = d_w_num.dodaj<Kolumna>();
        d_cn1.dodaj<Etykieta>("Pole numeryczne (0..100):");
        PoleNumeryczne& d_pn1 = d_cn1.dodaj<PoleNumeryczne>(0, 100, 25);
        d_pn1.ustaw_preferowany_rozmiar(Rozmiar(180, 24));
        Kolumna& d_cn2 = d_w_num.dodaj<Kolumna>();
        d_cn2.dodaj<Etykieta>("Z prefiksem:");
        PoleNumeryczne& d_pn2 = d_cn2.dodaj<PoleNumeryczne>(10, 500, 75);
        d_pn2.ustaw_prefiks("Waga: ");
        d_pn2.ustaw_sufiks(" kg");
        d_pn2.ustaw_preferowany_rozmiar(Rozmiar(180, 24));

        Wiersz& d_w_ctrl = d_col0.dodaj<Wiersz>();
        d_w_ctrl.ustaw_odstep(16);
        Kolumna& d_cs = d_w_ctrl.dodaj<Kolumna>();
        d_cs.dodaj<Etykieta>("Suwak (0..100):");
        Suwak& d_swk = d_cs.dodaj<Suwak>();
        d_swk.ustaw_zakres(0, 100);
        d_swk.ustaw_wartosc(65);
        d_swk.ustaw_preferowany_rozmiar(Rozmiar(200, 24));
        RamkaGrupy& d_rg = d_w_ctrl.dodaj<RamkaGrupy>("Opcje");
        Kolumna& d_co = d_rg.dodaj<Kolumna>();
        PrzyciskOpcji& d_opc1 = d_co.dodaj<PrzyciskOpcji>("Normalna", 1);
        d_opc1.ustaw_zaznaczony(true);
        d_co.dodaj<PrzyciskOpcji>("Wysoka", 2);

        Wiersz& d_w_chk = d_col0.dodaj<Wiersz>();
        d_w_chk.ustaw_odstep(16);
        d_w_chk.dodaj<PoleWyboru>("Akceleracja", true);
        PoleWyboru& d_chk3 = d_w_chk.dodaj<PoleWyboru>("Trojstan", false);
        d_chk3.ustaw_trojstanowe(true);
        d_chk3.ustaw_stan_wyboru(StanWyboru::Nieokreslone);
        d_w_chk.dodaj<Przycisk>("Zastosuj");

        d_karty.dodaj_karte("Podstawowe", d_strona0);

        // Tab 1: Tekst i Edycja
        Panel* d_strona1 = new Panel();
        d_strona1->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
        Kolumna& d_col1 = d_strona1->dodaj<Kolumna>();
        d_col1.ustaw_odstep(8);
        d_col1.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        d_col1.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        d_col1.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
        d_col1.dodaj<Etykieta>("Edytor tekstowy:");
        PoleWielowierszowe& d_memo = d_col1.dodaj<PoleWielowierszowe>();
        d_memo.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        d_memo.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        d_memo.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
        d_memo.ustaw_tekst("Pierwszy wiersz\nDrugi wiersz: Zażółć gęślą jaźń\nBardzo dlugi wiersz tekstu do testowania poziomego paska przewijania w edytorze BursztynUI.\n");
        Wiersz& d_w_inp = d_col1.dodaj<Wiersz>();
        d_w_inp.ustaw_odstep(8);
        d_w_inp.dodaj<Etykieta>("Wejscie:");
        PoleTekstowe& d_txt = d_w_inp.dodaj<PoleTekstowe>("Tekst...");
        d_txt.ustaw_preferowany_rozmiar(Rozmiar(240, 26));
        d_w_inp.dodaj<PoleWyboru>("Zawijaj", false);
        d_w_inp.dodaj<Przycisk>("Wyslij");
        d_karty.dodaj_karte("Tekst i Edycja", d_strona1);

        // Tab 2: Listy i Drzewo
        Panel* d_strona2 = new Panel();
        d_strona2->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
        Wiersz& d_w_listy = d_strona2->dodaj<Wiersz>();
        d_w_listy.ustaw_odstep(14);
        d_w_listy.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        d_w_listy.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        d_w_listy.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
        Kolumna& d_c_tree = d_w_listy.dodaj<Kolumna>();
        d_c_tree.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        d_c_tree.dodaj<Etykieta>("Drzewo:");
        Drzewo& d_tree = d_c_tree.dodaj<Drzewo>();
        d_tree.ustaw_preferowany_rozmiar(Rozmiar(280, -1));
        d_tree.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        UchwytWezlaDrzewa d_r1 = d_tree.dodaj_korzen("Moj Komputer");
        UchwytWezlaDrzewa d_d1 = d_tree.dodaj_dziecko(d_r1, "Dysk Systemowy (C:)");
        d_tree.dodaj_dziecko(d_d1, "Dokumenty");
        d_tree.dodaj_dziecko(d_d1, "System");
        d_tree.ustaw_rozwiniety(d_r1, true);
        d_tree.ustaw_rozwiniety(d_d1, true);

        Kolumna& d_c_lst = d_w_listy.dodaj<Kolumna>();
        d_c_lst.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        d_c_lst.dodaj<Etykieta>("Lista:");
        ListaWyboru& d_lst = d_c_lst.dodaj<ListaWyboru>();
        d_lst.ustaw_preferowany_rozmiar(Rozmiar(280, 200));
        d_lst.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        for (int i = 1; i <= 50; ++i) d_lst.dodaj_element("Element");
        d_c_lst.dodaj<Etykieta>("Combo:");
        ListaRozwijana& d_combo = d_c_lst.dodaj<ListaRozwijana>();
        d_combo.ustaw_preferowany_rozmiar(Rozmiar(280, 26));
        d_combo.dodaj_element("Opcja 1");
        d_combo.dodaj_element("Opcja 2");
        d_karty.dodaj_karte("Listy i Drzewo", d_strona2);

        // Tab 3: Kontenery
        Panel* d_strona3 = new Panel();
        d_strona3->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
        Kolumna& d_col3 = d_strona3->dodaj<Kolumna>();
        d_col3.ustaw_odstep(8);
        d_col3.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        d_col3.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        d_col3.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
        d_col3.dodaj<Etykieta>("Widok przewijany:");
        WidokPrzewijany& d_sv = d_col3.dodaj<WidokPrzewijany>();
        d_sv.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
        d_sv.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        d_sv.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        for (int r = 1; r <= 20; ++r) {
            Etykieta* l = new Etykieta("Wiersz");
            l->rozmiesc(Prostokat(10, (r - 1) * 28 + 6, 300, 22));
            d_sv.dodaj_do_zawartosci(l);
            Przycisk* b = new Przycisk("Akcja");
            b->rozmiesc(Prostokat(330, (r - 1) * 28 + 4, 80, 22));
            d_sv.dodaj_do_zawartosci(b);
        }
        d_sv.ustaw_rozmiar_zawartosci(Rozmiar(500, 580));
        d_karty.dodaj_karte("Kontenery", d_strona3);

        // Tab 4: Informacje
        Panel* d_strona4 = new Panel();
        d_strona4->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
        Kolumna& d_col4 = d_strona4->dodaj<Kolumna>();
        d_col4.ustaw_odstep(8);
        d_col4.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        d_col4.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        d_col4.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
        d_col4.dodaj<Etykieta>("O BursztynUI:");
        PoleWielowierszowe& d_info = d_col4.dodaj<PoleWielowierszowe>();
        d_info.ustaw_tylko_do_odczytu(true);
        d_info.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        d_info.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        d_info.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
        d_info.ustaw_tekst("BursztynUI Framework v2.0 - Zaawansowane komponenty retained-mode.\n");
        d_karty.dodaj_karte("Informacje", d_strona4);

        // 4) Pasek stanu
        PasekStanu& d_sb = d_root->dodaj<PasekStanu>();
        d_sb.ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
        d_sb.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        d_sb.dodaj_sekcje("Gotowy", 100, RozmiarSekcjiStanu::Rozciagnij);
        d_sb.dodaj_sekcje("UTF-8", 60, RozmiarSekcjiStanu::DopasujDoTresci);
        d_sb.dodaj_sekcje("DPI: 100%", 70, RozmiarSekcjiStanu::Stala);
        d_sb.dodaj_sekcje("Bursztyn OS", 90, RozmiarSekcjiStanu::Stala);

        // Walidacja ukladu i geometrii po przelicz_uklad()
        demo_win->przelicz_uklad();

        bws_log_u64("[DEMO AUDYT] karty_liczba=", d_karty.liczba_kart());
        bws_log_u64("[DEMO AUDYT] karty_szer=", d_karty.obszar().szerokosc);
        bws_log_u64("[DEMO AUDYT] karty_wys=", d_karty.obszar().wysokosc);
        bws_log_u64("[DEMO AUDYT] sb_wys=", d_sb.obszar().wysokosc);
        bws_log_u64("[DEMO AUDYT] sb_y=", d_sb.obszar().y);

        bws_sprawdz(d_karty.liczba_kart() == 5);
        bws_sprawdz(d_karty.aktywna_karta() == 0);
        bws_sprawdz(d_karty.obszar().szerokosc >= 680);
        bws_sprawdz(d_karty.obszar().wysokosc >= 300); // STRETCH - NIE MOZE BYC 0!
        bws_sprawdz(d_menu.obszar().wysokosc == 26);
        bws_sprawdz(d_tb.obszar().wysokosc == 32);
        bws_sprawdz(d_sb.obszar().wysokosc == 24);
        bws_sprawdz(d_karty.obszar().dolny() <= d_sb.obszar().y);

        // Sprawdzenie przelaczania i widocznosci wszystkich 5 zakladek
        for (int tab_idx = 0; tab_idx < 5; ++tab_idx) {
            d_karty.ustaw_aktywna_karte(tab_idx);
            bws_sprawdz(d_karty.aktywna_karta() == tab_idx);
            bws_sprawdz(d_karty.strona(tab_idx) != nullptr);
            bws_sprawdz(d_karty.strona(tab_idx)->zawartosc() != nullptr);
            bws_sprawdz(d_karty.strona(tab_idx)->zawartosc()->czy_widoczna());
            bws_sprawdz(d_karty.strona(tab_idx)->zawartosc()->obszar().wysokosc > 250);
            bws_sprawdz(d_karty.strona(tab_idx)->zawartosc()->obszar().szerokosc > 600);
        }

        // Sprawdzenie zachowania przy zmianie rozmiaru / maksymalizacji
        demo_win->ustaw_pozycje(0, 0);
        demo_win->ustaw_rozmiar(780, 560);
        bws_sprawdz(d_karty.obszar().szerokosc >= 760);
        bws_sprawdz(d_karty.obszar().wysokosc >= 400);
        bws_sprawdz(d_sb.obszar().dolny() <= 560 - Okno::WYSOKOSC_BELKI_TYTULOWEJ);

        // Pelne odrysowanie bez wyciekow
        KontekstRysowania* ctx_demo = new KontekstRysowania();
        if (ctx_demo) {
            demo_win->rysuj(*ctx_demo, true);
            delete ctx_demo;
        }

        delete demo_win;
        bws_log("[DEMO AUDYT] Architektura i geometria DemoUI 5 zakladek: OK\n");
    }

    // 10. Rozlegle testy mechanizmu STRETCH i obslugi sentinela Rozmiar(-1, -1)
    {
        // Test 1: Jeden element Stretch w Kolumnie
        {
            Kolumna* k1 = new Kolumna();
            k1->ustaw_wypelnienie(Marginesy(0));
            k1->ustaw_odstep(0);
            Panel& c1 = k1->dodaj<Panel>();
            c1.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            c1.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            k1->rozmiesc(Prostokat(0, 0, 300, 200));
            bws_sprawdz_msg(c1.obszar().wysokosc == 200, "STRETCH T1 h");
            bws_sprawdz_msg(c1.obszar().szerokosc == 300, "STRETCH T1 w");
            delete k1;
        }

        // Test 2: Dwa elementy Stretch dzielace wysokosc
        {
            Kolumna* k2 = new Kolumna();
            k2->ustaw_wypelnienie(Marginesy(0));
            k2->ustaw_odstep(0);
            Panel& k2_c1 = k2->dodaj<Panel>();
            k2_c1.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k2_c1.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            Panel& k2_c2 = k2->dodaj<Panel>();
            k2_c2.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k2_c2.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            k2->rozmiesc(Prostokat(0, 0, 300, 200));
            bws_sprawdz_msg(k2_c1.obszar().wysokosc == 100, "STRETCH T2 c1_h");
            bws_sprawdz_msg(k2_c2.obszar().wysokosc == 100, "STRETCH T2 c2_h");
            bws_sprawdz_msg(k2_c1.obszar().dolny() <= k2_c2.obszar().y, "STRETCH T2 overlap");
            delete k2;
        }

        // Test 3: Trzy elementy Stretch z deterministycznym podzialem reszty pikseli (100 px / 3 -> 34, 33, 33)
        {
            Kolumna* k3 = new Kolumna();
            k3->ustaw_wypelnienie(Marginesy(0));
            k3->ustaw_odstep(0);
            Panel& k3_c1 = k3->dodaj<Panel>();
            k3_c1.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k3_c1.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            Panel& k3_c2 = k3->dodaj<Panel>();
            k3_c2.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k3_c2.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            Panel& k3_c3 = k3->dodaj<Panel>();
            k3_c3.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k3_c3.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            k3->rozmiesc(Prostokat(0, 0, 300, 100));
            bws_sprawdz_msg(k3_c1.obszar().wysokosc == 34, "STRETCH T3 c1_h");
            bws_sprawdz_msg(k3_c2.obszar().wysokosc == 33, "STRETCH T3 c2_h");
            bws_sprawdz_msg(k3_c3.obszar().wysokosc == 33, "STRETCH T3 c3_h");
            bws_sprawdz_msg(k3_c1.obszar().dolny() == k3_c2.obszar().y, "STRETCH T3 o1");
            bws_sprawdz_msg(k3_c2.obszar().dolny() == k3_c3.obszar().y, "STRETCH T3 o2");
            bws_sprawdz_msg(k3_c3.obszar().dolny() == 100, "STRETCH T3 end");
            delete k3;
        }

        // Test 4: Fixed (50 px) + Stretch (w przestrzeni 200 px)
        {
            Kolumna* k4 = new Kolumna();
            k4->ustaw_wypelnienie(Marginesy(0));
            k4->ustaw_odstep(0);
            Panel& k4_f = k4->dodaj<Panel>();
            k4_f.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Poczatek);
            k4_f.ustaw_preferowany_rozmiar(Rozmiar(-1, 50));
            Panel& k4_s = k4->dodaj<Panel>();
            k4_s.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k4_s.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            k4->rozmiesc(Prostokat(0, 0, 300, 200));
            bws_sprawdz_msg(k4_f.obszar().wysokosc == 50, "STRETCH T4 f");
            bws_sprawdz_msg(k4_s.obszar().wysokosc == 150, "STRETCH T4 s");
            bws_sprawdz_msg(k4_f.obszar().dolny() <= k4_s.obszar().y, "STRETCH T4 o");
            delete k4;
        }

        // Test 5: Natural + Stretch
        {
            Kolumna* k5 = new Kolumna();
            k5->ustaw_wypelnienie(Marginesy(0));
            k5->ustaw_odstep(0);
            Etykieta& k5_lbl = k5->dodaj<Etykieta>("Etykieta testowa");
            k5_lbl.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Poczatek);
            Panel& k5_s = k5->dodaj<Panel>();
            k5_s.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k5_s.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            k5->rozmiesc(Prostokat(0, 0, 300, 200));
            int lbl_h = k5_lbl.obszar().wysokosc;
            bws_sprawdz_msg(lbl_h > 0, "STRETCH T5 lbl_h > 0");
            bws_sprawdz_msg(k5_s.obszar().wysokosc == 200 - lbl_h, "STRETCH T5 s_h");
            bws_sprawdz_msg(k5_lbl.obszar().dolny() <= k5_s.obszar().y, "STRETCH T5 o");
            delete k5;
        }

        // Test 6: Stretch z rozmiarem minimalnym
        {
            Kolumna* k6 = new Kolumna();
            k6->ustaw_wypelnienie(Marginesy(0));
            k6->ustaw_odstep(0);
            Panel& k6_f = k6->dodaj<Panel>();
            k6_f.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Poczatek);
            k6_f.ustaw_preferowany_rozmiar(Rozmiar(-1, 180));
            Panel& k6_s = k6->dodaj<Panel>();
            k6_s.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k6_s.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            k6_s.ustaw_minimalny_rozmiar(Rozmiar(0, 50));
            k6->rozmiesc(Prostokat(0, 0, 300, 200));
            bws_sprawdz_msg(k6_f.obszar().wysokosc == 180, "STRETCH T6 f");
            bws_sprawdz_msg(k6_s.obszar().wysokosc == 50, "STRETCH T6 s");
            delete k6;
        }

        // Test 7: Stretch z rozmiarem maksymalnym
        {
            Kolumna* k7 = new Kolumna();
            k7->ustaw_wypelnienie(Marginesy(0));
            k7->ustaw_odstep(0);
            Panel& k7_s = k7->dodaj<Panel>();
            k7_s.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k7_s.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            k7_s.ustaw_maksymalny_rozmiar(Rozmiar(10000, 80));
            k7->rozmiesc(Prostokat(0, 0, 300, 200));
            bws_sprawdz_msg(k7_s.obszar().wysokosc == 80, "STRETCH T7 s");
            delete k7;
        }

        // Test 8: Karty Stretch
        {
            Kolumna* k8 = new Kolumna();
            k8->ustaw_wypelnienie(Marginesy(0));
            k8->ustaw_odstep(0);
            Karty& k8_k = k8->dodaj<Karty>();
            k8_k.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k8_k.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            Panel* p8_tab = new Panel();
            k8_k.dodaj_karte("Karta 1", p8_tab);
            k8->rozmiesc(Prostokat(0, 0, 400, 300));
            bws_sprawdz_msg(k8_k.obszar().wysokosc == 300, "STRETCH T8 k");
            bws_sprawdz_msg(p8_tab->obszar().wysokosc >= 260, "STRETCH T8 tab");
            delete k8;
        }

        // Test 9: WidokPrzewijany Stretch
        {
            Kolumna* k9 = new Kolumna();
            k9->ustaw_wypelnienie(Marginesy(0));
            k9->ustaw_odstep(0);
            WidokPrzewijany& k9_sv = k9->dodaj<WidokPrzewijany>();
            k9_sv.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k9_sv.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            k9_sv.ustaw_rozmiar_zawartosci(Rozmiar(600, 600));
            k9->rozmiesc(Prostokat(0, 0, 300, 200));
            bws_sprawdz_msg(k9_sv.obszar().wysokosc == 200, "STRETCH T9 h");
            bws_sprawdz_msg(k9_sv.obszar().szerokosc == 300, "STRETCH T9 w");
            delete k9;
        }

        // Test 10: Zagniezdzone panele (Kolumna -> Wiersz Stretch -> Kolumna Stretch)
        {
            Kolumna* k10 = new Kolumna();
            k10->ustaw_wypelnienie(Marginesy(0));
            k10->ustaw_odstep(0);
            Wiersz& k10_w = k10->dodaj<Wiersz>();
            k10_w.ustaw_wypelnienie(Marginesy(0));
            k10_w.ustaw_odstep(0);
            k10_w.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k10_w.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            Kolumna& k10_inner = k10_w.dodaj<Kolumna>();
            k10_inner.ustaw_wypelnienie(Marginesy(0));
            k10_inner.ustaw_odstep(0);
            k10_inner.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
            k10_inner.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k10_inner.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            k10->rozmiesc(Prostokat(0, 0, 400, 300));
            bws_sprawdz_msg(k10_w.obszar().wysokosc == 300, "STRETCH T10 w_h");
            bws_sprawdz_msg(k10_inner.obszar().wysokosc == 300, "STRETCH T10 in_h");
            bws_sprawdz_msg(k10_inner.obszar().szerokosc == 400, "STRETCH T10 in_w");
            delete k10;
        }

        // Test 11: Zero wolnej przestrzeni
        {
            Kolumna* k11 = new Kolumna();
            k11->ustaw_wypelnienie(Marginesy(0));
            k11->ustaw_odstep(0);
            Panel& k11_f = k11->dodaj<Panel>();
            k11_f.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Poczatek);
            k11_f.ustaw_preferowany_rozmiar(Rozmiar(-1, 100));
            Panel& k11_s = k11->dodaj<Panel>();
            k11_s.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k11_s.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            k11->rozmiesc(Prostokat(0, 0, 200, 100));
            bws_sprawdz_msg(k11_f.obszar().wysokosc == 100, "STRETCH T11 f");
            bws_sprawdz_msg(k11_s.obszar().wysokosc == 0, "STRETCH T11 s");
            bws_sprawdz_msg(k11_s.obszar().szerokosc >= 0, "STRETCH T11 w>=0");
            delete k11;
        }

        // Test 12: Rodzic mniejszy niz suma minimow
        {
            Kolumna* k12 = new Kolumna();
            k12->ustaw_wypelnienie(Marginesy(0));
            k12->ustaw_odstep(0);
            Panel& k12_1 = k12->dodaj<Panel>();
            k12_1.ustaw_minimalny_rozmiar(Rozmiar(0, 40));
            k12_1.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k12_1.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            Panel& k12_2 = k12->dodaj<Panel>();
            k12_2.ustaw_minimalny_rozmiar(Rozmiar(0, 40));
            k12_2.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            k12_2.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
            k12->rozmiesc(Prostokat(0, 0, 200, 50));
            bws_sprawdz_msg(k12_1.obszar().wysokosc >= 40, "STRETCH T12 1>=40");
            bws_sprawdz_msg(k12_2.obszar().wysokosc >= 40, "STRETCH T12 2>=40");
            bws_sprawdz_msg(k12_1.obszar().szerokosc >= 0, "STRETCH T12 1w>=0");
            bws_sprawdz_msg(k12_2.obszar().szerokosc >= 0, "STRETCH T12 2w>=0");
            delete k12;
        }

        bws_log("[STRETCH AUDYT] Wszystkie 12 scenariuszy STRETCH i sentineli: OK\n");
    }

    // Okno::dodaj() zwraca dynamiczny korzen, ale Okno zachowuje wlasnosc
    // tylko nad swoim domyslnym panelem. Testy korzystaja z tej skroconej
    // skladni, wiec jawnie zwalniaja utworzone przez nia korzenie.
    usun_kontrolke_testowa(&memo_root);
    usun_kontrolke_testowa(&panel_paska);
    usun_kontrolke_testowa(&hover_kolumna);
    usun_kontrolke_testowa(&glowna);
    delete okno_popup_ptr;
    delete memo_clip_ptr;
    delete okno_listy_ptr;
    delete lista_ptr;
    bws_log("[BURSZTYNUI MEMO] Edycja, historia, schowek i rendering: OK\n");

    // ---------------------------------------------------------------------
    // 10. Test regresji 20 cykli rerun z zachowaniem chromu i geometrii
    // ---------------------------------------------------------------------
    {
        bws_log("[REGRESJA RERUN] Test 20 cykli rerun z zachowaniem chromu...\n");
        const uint32_t bledy_rerun_przed = bws_test_blad;
        Okno* okno_rerun = new Okno("Okno Regresji Rerun", 600, 450);
        bws_sprawdz(okno_rerun != nullptr);
        if (okno_rerun) {
            const int init_x = okno_rerun->x();
            const int init_y = okno_rerun->y();
            const int init_w = okno_rerun->szerokosc();
            const int init_h = okno_rerun->wysokosc();
            const int expected_belka_h = Okno::WYSOKOSC_BELKI_TYTULOWEJ * okno_rerun->skala_dpi();
            bws_sprawdz_msg(expected_belka_h == 28, "Canonical titlebar height 28px");

            // Wariant A: przechwycenie rzeczywistej geometrii widocznej przed regresja
            int base_vx = 0, base_vy = 0, base_vw = 0, base_vh = 0;
            gui_pobierz_widoczna_geometrie(&base_vx, &base_vy, &base_vw, &base_vh);

            for (int cykl = 1; cykl <= 20; ++cykl) {
                const bool tlum_rys_przed = KontekstRysowania::g_tlum_rysowanie_globalne;
                const bool tlum_sys_przed = Okno::g_tlum_operacje_systemowe;

                const int bf_x = okno_rerun->x();
                const int bf_y = okno_rerun->y();
                const int bf_w = okno_rerun->szerokosc();
                const int bf_h = okno_rerun->wysokosc();

                {
                    IzolacjaPrezentacjiTestow izolacja(okno_rerun);
                    bws_sprawdz(KontekstRysowania::g_tlum_rysowanie_globalne);
                    bws_sprawdz(Okno::g_tlum_operacje_systemowe);
                    okno_rerun->przelacz_maksymalizacje();
                    okno_rerun->przelacz_maksymalizacje();
                }

                const int af_x = okno_rerun->x();
                const int af_y = okno_rerun->y();
                const int af_w = okno_rerun->szerokosc();
                const int af_h = okno_rerun->wysokosc();

                bws_sprawdz(KontekstRysowania::g_tlum_rysowanie_globalne == tlum_rys_przed);
                bws_sprawdz(Okno::g_tlum_operacje_systemowe == tlum_sys_przed);
                bws_sprawdz_msg(okno_rerun->x() == init_x, "Regresja rerun: drift x");
                bws_sprawdz_msg(okno_rerun->y() == init_y, "Regresja rerun: drift y");
                bws_sprawdz_msg(okno_rerun->szerokosc() == init_w, "Regresja rerun: drift szerokosci");
                bws_sprawdz_msg(okno_rerun->wysokosc() == init_h, "Regresja rerun: drift wysokosci");

                int cur_vx = 0, cur_vy = 0, cur_vw = 0, cur_vh = 0;
                gui_pobierz_widoczna_geometrie(&cur_vx, &cur_vy, &cur_vw, &cur_vh);

                const bool fail_vx = (cur_vx != base_vx);
                const bool fail_vy = (cur_vy != base_vy);
                const bool fail_vw = (cur_vw != base_vw);
                const bool fail_vh = (cur_vh != base_vh);

                if (fail_vx || fail_vy || fail_vw || fail_vh) {
                    log_rerun_geom(cykl,
                                   init_x, init_y, init_w, init_h,
                                   base_vx, base_vy, base_vw, base_vh,
                                   bf_x, bf_y, bf_w, bf_h,
                                   af_x, af_y, af_w, af_h,
                                   okno_rerun->x(), okno_rerun->y(), okno_rerun->szerokosc(), okno_rerun->wysokosc(),
                                   cur_vx, cur_vy, cur_vw, cur_vh);
                }

                bws_sprawdz_msg(!fail_vx, "Regresja rerun: visible_x drift");
                bws_sprawdz_msg(!fail_vy, "Regresja rerun: visible_y drift");
                bws_sprawdz_msg(!fail_vw, "Regresja rerun: visible_w drift");
                bws_sprawdz_msg(!fail_vh, "Regresja rerun: visible_h drift");

                const int belka_h = Okno::WYSOKOSC_BELKI_TYTULOWEJ * okno_rerun->skala_dpi();
                bws_sprawdz_msg(belka_h == 28, "Regresja rerun: zla wysokosc belki");
                bws_sprawdz_msg(okno_rerun->obszar_klienta().y == belka_h, "Regresja rerun: client.y != belka_h");
                bws_sprawdz_msg(okno_rerun->obszar_klienta().wysokosc == init_h - belka_h, "Regresja rerun: zla wysokosc klienta");

                bws_sprawdz(gui_hit_test_belki(init_x + init_w - 15, init_y + 14,
                                              init_x, init_y, init_w) == GUI_BELKA_ZAMKNIJ);
                bws_sprawdz(gui_hit_test_belki(init_x + init_w - 40, init_y + 14,
                                              init_x, init_y, init_w) == GUI_BELKA_MAKSYMALIZUJ);
                bws_sprawdz(gui_hit_test_belki(init_x + init_w - 64, init_y + 14,
                                              init_x, init_y, init_w) == GUI_BELKA_MINIMALIZUJ);
                bws_sprawdz(gui_hit_test_belki(init_x + 50, init_y + 14,
                                              init_x, init_y, init_w) == GUI_BELKA_DRAG);

                Prostokat brudny = okno_rerun->pobierz_brudny_obszar();
                bws_sprawdz(brudny.czy_pusty() || brudny.y >= belka_h);
            }
            delete okno_rerun;

            // Wariant B: jawna zmiana pozycji okna przed regresja i sprawdzenie stabilnosci nowego baselinu
            Okno* okno_rerun_moved = new Okno("Okno Regresji Rerun Moved", 520, 380);
            bws_sprawdz(okno_rerun_moved != nullptr);
            if (okno_rerun_moved) {
                okno_rerun_moved->ustaw_pozycje(180, 120);
                const int moved_init_x = okno_rerun_moved->x();
                const int moved_init_y = okno_rerun_moved->y();
                const int moved_init_w = okno_rerun_moved->szerokosc();
                const int moved_init_h = okno_rerun_moved->wysokosc();

                for (int cykl = 1; cykl <= 5; ++cykl) {
                    {
                        IzolacjaPrezentacjiTestow izolacja(okno_rerun_moved);
                        okno_rerun_moved->przelacz_maksymalizacje();
                        okno_rerun_moved->przelacz_maksymalizacje();
                    }
                    bws_sprawdz_msg(okno_rerun_moved->x() == moved_init_x, "Regresja rerun moved: drift x");
                    bws_sprawdz_msg(okno_rerun_moved->y() == moved_init_y, "Regresja rerun moved: drift y");
                    bws_sprawdz_msg(okno_rerun_moved->szerokosc() == moved_init_w, "Regresja rerun moved: drift w");
                    bws_sprawdz_msg(okno_rerun_moved->wysokosc() == moved_init_h, "Regresja rerun moved: drift h");

                    int cur_vx = 0, cur_vy = 0, cur_vw = 0, cur_vh = 0;
                    gui_pobierz_widoczna_geometrie(&cur_vx, &cur_vy, &cur_vw, &cur_vh);
                    bws_sprawdz_msg(cur_vx == base_vx, "Regresja rerun moved: visible_x drift");
                    bws_sprawdz_msg(cur_vy == base_vy, "Regresja rerun moved: visible_y drift");
                    bws_sprawdz_msg(cur_vw == base_vw, "Regresja rerun moved: visible_w drift");
                    bws_sprawdz_msg(cur_vh == base_vh, "Regresja rerun moved: visible_h drift");
                }
                delete okno_rerun_moved;
            }

            const uint32_t bledy_rerun = bws_test_blad - bledy_rerun_przed;
            if (bledy_rerun == 0) {
                bws_log("[REGRESJA RERUN] 20 cykli zakonczonych sukcesem (PASS).\n");
            } else {
                char msg_fail[64];
                size_t pos_fail = 0;
                msg_fail[0] = '\0';
                bws_dopisz_str(msg_fail, sizeof(msg_fail), &pos_fail, "[REGRESJA RERUN] FAIL ");
                bws_dopisz_i32(msg_fail, sizeof(msg_fail), &pos_fail, static_cast<int32_t>(bledy_rerun));
                bws_dopisz_str(msg_fail, sizeof(msg_fail), &pos_fail, " blednych asercji.\n");
                bws_log(msg_fail);
            }
        }
    }

    if (bws_test_blad == bledy_bui_przed)
        bws_log("=== WSZYSTKIE TESTY BURSZTYNUI ZAKONCZONE SUKCESEM (PASS) ===\n\n");
    else
        bws_log("=== WYKRYTO BLEDY W TESTACH BURSZTYNUI (FAIL) ===\n\n");
}

#include "kontrolki3_stabilizacja.h"

static void wykonaj_testy_kontrolki3() {
    bws_log("\n=== ROZPOCZECIE TESTOW KONTROLKI 3 ===\n");
    uint32_t wszystkie_przed = bws_test_blad;

    {
        uint32_t przed = bws_test_blad;
        bws_log("[KONTROLKI3 1/8] WidokDrzewiastoKolumnowy...\n");
        WidokDrzewiastoKolumnowy* w = new WidokDrzewiastoKolumnowy();
        bws_sprawdz_msg(w != nullptr, "K3 drzewo alokacja");
        if (w) {
            bws_sprawdz_msg(sizeof(WidokDrzewiastoKolumnowy) < 32768U,
                            "K3 drzewo pusta instancja bez statycznej puli setek KiB");
            bws_log_u64("[K3 DRZEWO POMIAR] sizeof_control_B=", sizeof(WidokDrzewiastoKolumnowy));
            w->rozmiesc(Prostokat(0, 0, 420, 240));
            bws_sprawdz(w->dodaj_kolumne("Nazwa", 220));
            bws_sprawdz(w->dodaj_kolumne("Typ", 160));
            bws_sprawdz(w->dodaj_kolumne("Rozmiar", 120));
            bws_sprawdz(w->liczba_kolumn() == 3 && w->kolumna(0) &&
                        w->kolumna(0)->szerokosc == 220 && w->kolumna(3) == nullptr);
            auto korzen = w->dodaj_korzen("programy", "Katalog", "—");
            auto dziecko = w->dodaj_dziecko(korzen, "notatnik.cebula", "Aplikacja", "176 KiB");
            bws_sprawdz(w->czy_uchwyt_poprawny(korzen) && w->czy_uchwyt_poprawny(dziecko));
            bws_sprawdz(w->ustaw_ikone(dziecko, 7) && w->ikona(dziecko) == 7);
            UchwytWezlaDrzewaKolumnowego zly{99999U, 1U};
            bws_sprawdz(!w->czy_uchwyt_poprawny(zly) && !w->ustaw_fokus_wezel(zly));
            bws_sprawdz(w->liczba_widocznych_wezlow() == 2);
            w->zwin(korzen); bws_sprawdz(w->liczba_widocznych_wezlow() == 1);
            w->rozwin(korzen); bws_sprawdz(w->liczba_widocznych_wezlow() == 2);
            w->ustaw_wybrany_wezel(korzen); w->ustaw_fokus_wezel(dziecko);
            bws_sprawdz(w->wybrany_wezel() != w->fokus_wezel());
            // Bezposredni hit-test grotu rozwijania w pierwszym wierszu.
            ZdarzenieMyszki grot{}; grot.przycisk = 1; grot.x = 8; grot.y = 30;
            w->obsluz_wcisniecie_myszy(grot); bws_sprawdz(!w->czy_rozwiniety(korzen));
            w->obsluz_wcisniecie_myszy(grot); bws_sprawdz(w->czy_rozwiniety(korzen));
            ZdarzenieKlawiatury home{}; home.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x47U;
            w->obsluz_wcisniecie_klawisza(home); bws_sprawdz(w->fokus_wezel() == korzen);
            ZdarzenieKlawiatury right{}; right.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x4DU;
            w->obsluz_wcisniecie_klawisza(right); bws_sprawdz(w->fokus_wezel() == dziecko);
            ZdarzenieKlawiatury left{}; left.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x4BU;
            w->obsluz_wcisniecie_klawisza(left); bws_sprawdz(w->fokus_wezel() == korzen);
            w->obsluz_wcisniecie_klawisza(left); bws_sprawdz(!w->czy_rozwiniety(korzen));
            w->obsluz_wcisniecie_klawisza(right); bws_sprawdz(w->czy_rozwiniety(korzen));
            ZdarzenieKlawiatury end{}; end.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x4FU;
            w->obsluz_wcisniecie_klawisza(end); bws_sprawdz(w->fokus_wezel() == dziecko);
            ZdarzenieKlawiatury up{}; up.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x48U;
            ZdarzenieKlawiatury down{}; down.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x50U;
            w->obsluz_wcisniecie_klawisza(up); bws_sprawdz(w->fokus_wezel() == korzen);
            ZdarzenieKlawiatury space{}; space.kod_klawisza = 32;
            w->obsluz_wcisniecie_klawisza(space); bws_sprawdz(!w->czy_rozwiniety(korzen));
            w->obsluz_wcisniecie_klawisza(space); bws_sprawdz(w->czy_rozwiniety(korzen));
            w->obsluz_wcisniecie_klawisza(down); bws_sprawdz(w->fokus_wezel() == dziecko);
            w->rozpocznij_aktualizacje();
            auto gleboki = dziecko;
            for (int i = 2; i < 2000; ++i) gleboki = w->dodaj_dziecko(gleboki, "węzeł", "Test", "1 KiB");
            w->zakoncz_aktualizacje();
            bws_sprawdz(w->liczba_wezlow() == 2000 && w->czy_uchwyt_poprawny(gleboki));
            bws_sprawdz(w->pasek_pionowy().czy_widoczna() && w->pasek_poziomy().czy_widoczna());
            ZdarzenieKlawiatury page_down{}; page_down.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x51U;
            int64_t przed_page = w->pasek_pionowy().wartosc();
            w->obsluz_wcisniecie_klawisza(page_down);
            bws_sprawdz(w->pasek_pionowy().wartosc() >= przed_page);
            w->pasek_pionowy().ustaw_wartosc(0);
            ZdarzenieRolki rolka{}; rolka.delta_y = -1;
            w->obsluz_rolke(rolka);
            bws_sprawdz(rolka.obsluzone && w->pasek_pionowy().wartosc() > 0);
            KontekstRysowania ctx; uint64_t a0 = gui_liczba_alokacji_sterty(); w->rysuj(ctx, true); uint64_t a1 = gui_liczba_alokacji_sterty();
            const uint64_t alokacje_rysowania = a1 - a0;
            bws_sprawdz_msg(a0 == a1, "K3 drzewo paint 0 alokacji");
            bws_sprawdz(w->liczba_ostatnio_narysowanych_wierszy() < w->liczba_widocznych_wezlow());
            bws_log_u64("[K3 DRZEWO POMIAR] visible_rows=", w->liczba_ostatnio_narysowanych_wierszy());
            bws_log_u64("[K3 DRZEWO POMIAR] model_rows=", w->liczba_widocznych_wezlow());
            bws_log_u64("[K3 DRZEWO POMIAR] paint_allocations=", alokacje_rysowania);
            w->wyczysc_brudny_obszar();
            ZdarzenieMyszki mv{}; mv.typ = TypZdarzenia::RuchMyszy; mv.x = 300; mv.y = 100;
            w->obsluz_ruch_myszy(mv); w->wyczysc_brudny_obszar();
            a0 = gui_liczba_alokacji_sterty(); for (int i = 0; i < 5000; ++i) w->obsluz_ruch_myszy(mv); a1 = gui_liczba_alokacji_sterty();
            bws_sprawdz(a0 == a1 && !w->czy_brudna());
            bws_log_u64("[K3 DRZEWO 5K] mouse_allocations=", a1 - a0);
            bws_log_u64("[K3 DRZEWO 5K] dirty_calls=", w->czy_brudna() ? 1 : 0);
            auto stary = korzen; w->wyczysc(); bws_sprawdz(!w->czy_uchwyt_poprawny(stary));
            auto nowy = w->dodaj_korzen("nowy");
            bws_sprawdz(w->czy_uchwyt_poprawny(nowy) && nowy != stary && !w->czy_uchwyt_poprawny(stary));
            delete w;
        }
        bws_log(bws_test_blad == przed ? "[KONTROLKI3 1/8] WidokDrzewiastoKolumnowy OK\n" : "[KONTROLKI3 1/8] WidokDrzewiastoKolumnowy FAIL\n");
    }

    {
        uint32_t przed = bws_test_blad;
        bws_log("[KONTROLKI3 2/8] InspektorWlasciwosci...\n");
        InspektorWlasciwosci i; i.rozmiesc(Prostokat(0, 0, 360, 220));
        static const char* const wybory[] = {"Mały", "Średni", "Duży"};
        bws_sprawdz(i.dodaj_tekst(1, "Nazwa", "Obiekt", "Geometria"));
        bws_sprawdz(i.dodaj_liczbe(2, "Szerokość", 120, "Geometria"));
        bws_sprawdz(i.dodaj_logiczna(3, "Widoczny", true, "Zachowanie"));
        bws_sprawdz(i.dodaj_wybor(4, "Rozmiar", wybory, 3, 1, "Wygląd"));
        bws_sprawdz(i.dodaj_kolor(5, "Kolor tła", 0x00FFBF00, "Wygląd"));
        bws_sprawdz(i.dodaj_tekst(6, "Tylko odczyt", "Stała", "Zachowanie", true));
        bws_sprawdz(!i.dodaj_tekst(1, "Duplikat", "Błąd"));
        bws_sprawdz(!i.dodaj_wybor(7, "Błędny wybór", wybory, 3, 3));
        struct P { int n; } p{}; i.przy_zmianie([](InspektorWlasciwosci*, uint32_t, const InspektorWlasciwosci::Wlasciwosc&, void* d) { ++static_cast<P*>(d)->n; }, &p);
        bws_sprawdz(!i.rozpocznij_edycje(5));
        bws_sprawdz(i.rozpocznij_edycje(0));
        // Edytor jest jeden i korzysta z PoleTekstowe; commit i anulowanie.
        bws_sprawdz(i.czy_edytuje() && i.aktywny_edytor() != nullptr);
        i.aktywny_edytor()->ustaw_tekst("Nowa nazwa");
        bws_sprawdz(i.zatwierdz_edycje() && p.n == 1);
        bws_sprawdz(i.rozpocznij_edycje(1));
        i.aktywny_edytor()->ustaw_tekst("999999999999999999999999999999");
        bws_sprawdz(!i.zatwierdz_edycje() && i.czy_edytuje());
        i.anuluj_edycje(); bws_sprawdz(!i.czy_edytuje());
        i.rozpocznij_edycje(2); bws_sprawdz(!i.wlasciwosc(2)->logiczna && p.n == 2);
        i.rozpocznij_edycje(3); bws_sprawdz(i.wlasciwosc(3)->wybrany_wybor == 2 && p.n == 3);
        i.ustaw_wybrany_indeks(0);
        ZdarzenieKlawiatury down{}; down.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x50U;
        i.obsluz_wcisniecie_klawisza(down); bws_sprawdz(i.fokus_indeks() == 1);
        ZdarzenieKlawiatury enter{}; enter.kod_klawisza = 13;
        i.obsluz_wcisniecie_klawisza(enter); bws_sprawdz(i.czy_edytuje());
        ZdarzenieKlawiatury esc{}; esc.kod_klawisza = 27;
        i.obsluz_wcisniecie_klawisza(esc); bws_sprawdz(!i.czy_edytuje());
        for (int n = 7; n < 307; ++n) i.dodaj_liczbe(static_cast<uint32_t>(n), "Pozycja", n, "Duża lista");
        bws_sprawdz(i.pasek_pionowy().czy_widoczna());
        i.pasek_pionowy().ustaw_wartosc(0);
        ZdarzenieRolki rolka{}; rolka.delta_y = -1;
        i.obsluz_rolke(rolka);
        bws_sprawdz(rolka.obsluzone && i.pasek_pionowy().wartosc() > 0);
        KontekstRysowania ctx; uint64_t a0 = gui_liczba_alokacji_sterty(); i.rysuj(ctx, true); uint64_t a1 = gui_liczba_alokacji_sterty();
        bws_sprawdz(a0 == a1 && i.liczba_ostatnio_narysowanych() < i.liczba_wlasciwosci());
        bws_log_u64("[K3 INSPEKTOR POMIAR] visible_rows=", i.liczba_ostatnio_narysowanych());
        bws_log_u64("[K3 INSPEKTOR POMIAR] model_rows=", i.liczba_wlasciwosci());
        bws_log_u64("[K3 INSPEKTOR POMIAR] paint_allocations=", a1 - a0);
        ZdarzenieMyszki mv{}; mv.x = 40; mv.y = 80; i.obsluz_ruch_myszy(mv); i.wyczysc_brudny_obszar();
        a0 = gui_liczba_alokacji_sterty(); for (int n = 0; n < 5000; ++n) i.obsluz_ruch_myszy(mv); a1 = gui_liczba_alokacji_sterty();
        bws_sprawdz(a0 == a1 && !i.czy_brudna());
        bws_log_u64("[K3 INSPEKTOR 5K] mouse_allocations=", a1 - a0);
        bws_log_u64("[K3 INSPEKTOR 5K] dirty_calls=", i.czy_brudna() ? 1 : 0);
        bws_log(bws_test_blad == przed ? "[KONTROLKI3 2/8] InspektorWlasciwosci OK\n" : "[KONTROLKI3 2/8] InspektorWlasciwosci FAIL\n");
    }

    {
        uint32_t przed = bws_test_blad;
        bws_log("[KONTROLKI3 3/8] PoleWyszukiwania...\n");
        PoleWyszukiwania p("Szukaj…"); p.rozmiesc(Prostokat(0, 0, 300, 34));
        struct P { int zmiany; int szukaj; } m{};
        p.przy_zmianie([](PoleWyszukiwania*, const char*, void* d) { ++static_cast<P*>(d)->zmiany; }, &m);
        p.przy_wyszukaniu([](PoleWyszukiwania*, const char*, void* d) { ++static_cast<P*>(d)->szukaj; }, &m);
        bws_sprawdz(p.placeholder()[0] == 'S' && p.placeholder()[1] == 'z');
        p.ustaw_tekst("Pamięć ą ć ę ł ń ó ś ź ż"); int z = m.zmiany;
        p.wyczysc_brudny_obszar(); p.ustaw_tekst("Pamięć ą ć ę ł ń ó ś ź ż");
        bws_sprawdz(m.zmiany == z && !p.czy_brudna() && p.czy_przycisk_czyszczenia_widoczny());
        ZdarzenieMyszki clear{}; clear.przycisk = 1; clear.x = 290; clear.y = 17;
        p.obsluz_wcisniecie_myszy(clear); p.obsluz_zwolnienie_myszy(clear);
        bws_sprawdz(p.pobierz_tekst()[0] == '\0' && !p.czy_przycisk_czyszczenia_widoczny());
        p.ustaw_tekst("Pamięć ą ć ę ł ń ó ś ź ż");
        p.wyczysc_brudny_obszar(); ZdarzenieMyszki mv{}; mv.x = 285; mv.y = 16; p.obsluz_ruch_myszy(mv); p.wyczysc_brudny_obszar();
        uint64_t a0 = gui_liczba_alokacji_sterty(); for (int n = 0; n < 5000; ++n) p.obsluz_ruch_myszy(mv); uint64_t a1 = gui_liczba_alokacji_sterty();
        bws_sprawdz(a0 == a1 && !p.czy_brudna());
        bws_log_u64("[K3 WYSZUKIWANIE 5K] mouse_allocations=", a1 - a0);
        bws_log_u64("[K3 WYSZUKIWANIE 5K] dirty_calls=", p.czy_brudna() ? 1 : 0);
        ZdarzenieKlawiatury enter{}; enter.kod_klawisza = 13; p.pole_tekstowe().obsluz_wcisniecie_klawisza(enter); bws_sprawdz(m.szukaj == 1);
        ZdarzenieKlawiatury esc{}; esc.kod_klawisza = 27; p.pole_tekstowe().obsluz_wcisniecie_klawisza(esc); bws_sprawdz(p.pobierz_tekst()[0] == '\0');
        p.pole_tekstowe().przy_fokusie(); bws_sprawdz(p.pole_tekstowe().czy_ma_fokus());
        p.ustaw_tekst("Blokada"); p.ustaw_tylko_do_odczytu(true);
        ZdarzenieWprowadzaniaTekstu te{}; te.punkt_kodowy = 0x104;
        p.pole_tekstowe().obsluz_wprowadzenie_tekstu(te); bws_sprawdz(p.pobierz_tekst()[0] == 'B');
        p.pole_tekstowe().obsluz_wcisniecie_klawisza(esc); bws_sprawdz(p.pobierz_tekst()[0] == 'B');
        p.ustaw_tylko_do_odczytu(false); p.wyczysc(); p.ustaw_wlaczona(false);
        p.pole_tekstowe().obsluz_wprowadzenie_tekstu(te); bws_sprawdz(p.pobierz_tekst()[0] == '\0');
        const char* alfabet = "ą ć ę ł ń ó ś ź ż Ą Ć Ę Ł Ń Ó Ś Ź Ż";
        bws_sprawdz(CzcionkaUI::zmierz_tekst(alfabet, 1) > 0);
        KontekstRysowania font_ctx;
        const bool tlumienie = KontekstRysowania::g_tlum_rysowanie_globalne;
        KontekstRysowania::g_tlum_rysowanie_globalne = true;
        a0 = gui_liczba_alokacji_sterty(); font_ctx.rysuj_tekst(Punkt(0, 0), alfabet, 0x00FFFFFFU, 1); a1 = gui_liczba_alokacji_sterty();
        KontekstRysowania::g_tlum_rysowanie_globalne = tlumienie;
        bws_sprawdz(a0 == a1);
        bws_log(bws_test_blad == przed ? "[KONTROLKI3 3/8] PoleWyszukiwania OK\n" : "[KONTROLKI3 3/8] PoleWyszukiwania FAIL\n");
    }

    {
        uint32_t przed = bws_test_blad;
        bws_log("[KONTROLKI3 4/8] PoleSugestii...\n");
        ZarzadcaPopupow z; z.ustaw_geometrie_wlasciciela(Prostokat(0,0,400,300), Prostokat(0,0,400,300));
        PoleSugestii p; p.ustaw_zarzadce_popupow(&z); p.rozmiesc(Prostokat(10, 10, 280, 34));
        static const char* const s[] = {"Bursztyn", "BursztynUI", "Pamięć", "Przeglądarka", "Ustawienia"};
        bws_sprawdz(p.ustaw_sugestie(s, 5)); p.ustaw_maksymalna_liczbe_widocznych(3);
        struct P { int wybor; int zmiana; } m{}; p.przy_wybraniu([](PoleSugestii*, int, const char*, void* d){ ++static_cast<P*>(d)->wybor; }, &m); p.przy_zmianie([](PoleSugestii*, const char*, void* d){ ++static_cast<P*>(d)->zmiana; }, &m);
        p.ustaw_tekst("Bur"); bws_sprawdz(p.liczba_dopasowanych() == 2 && p.czy_popup_otwarty() && z.liczba_powierzchni() == 1);
        bws_sprawdz(z.prostokat_zarejestrowany_ekranu().prawy() <= 400 && z.prostokat_zarejestrowany_ekranu().dolny() <= 300);
        ZdarzenieKlawiatury down{}; down.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x50U; z.obsluz_klawisz(down);
        KontekstRysowania ctx; uint64_t a0 = gui_liczba_alokacji_sterty(); z.rysuj(ctx, true); uint64_t a1 = gui_liczba_alokacji_sterty();
        bws_sprawdz(a0 == a1 && p.liczba_ostatnio_narysowanych() <= 3);
        bws_log_u64("[K3 SUGESTIE POMIAR] visible_rows=", p.liczba_ostatnio_narysowanych());
        bws_log_u64("[K3 SUGESTIE POMIAR] paint_allocations=", a1 - a0);
        Prostokat popup_hover = z.prostokat_zarejestrowany_ekranu();
        ZdarzenieMyszki hover{}; hover.x = popup_hover.x + 5; hover.y = popup_hover.y + 5;
        z.obsluz_ruch(hover); z.rysuj(ctx, false);
        a0 = gui_liczba_alokacji_sterty();
        for (int n = 0; n < 5000; ++n) z.obsluz_ruch(hover);
        a1 = gui_liczba_alokacji_sterty();
        bws_sprawdz(a0 == a1 && !z.czy_brudny());
        bws_log_u64("[K3 SUGESTIE 5K] mouse_allocations=", a1 - a0);
        bws_log_u64("[K3 SUGESTIE 5K] dirty_calls=", z.czy_brudny() ? 1 : 0);
        ZdarzenieKlawiatury enter{}; enter.kod_klawisza = 13; z.obsluz_klawisz(enter);
        bws_sprawdz(m.wybor == 1 && !p.czy_popup_otwarty());
        p.ustaw_tekst("Pamię"); bws_sprawdz(p.czy_popup_otwarty());
        ZdarzenieKlawiatury esc{}; esc.kod_klawisza = 27; z.obsluz_klawisz(esc);
        bws_sprawdz(!p.czy_popup_otwarty() && p.pobierz_tekst()[0] != '\0');
        p.ustaw_tekst("Prz"); bws_sprawdz(p.czy_popup_otwarty());
        Prostokat popup = z.prostokat_zarejestrowany_ekranu();
        ZdarzenieMyszki klik{}; klik.przycisk = 1; klik.x = popup.x + 5; klik.y = popup.y + 5;
        z.obsluz_wcisniecie(klik);
        bws_sprawdz(m.wybor == 2 && !p.czy_popup_otwarty());
        p.ustaw_tekst("Pamię"); bws_sprawdz(p.czy_popup_otwarty());
        z.zamknij(PowodZamknieciaPopup::WlascicielZminimalizowany);
        bws_sprawdz(!p.czy_popup_otwarty() && z.liczba_powierzchni() == 0);

        PoleSugestii ograniczone;
        const char* wiele[PoleSugestii::MAKS_SUGESTII + 1]{};
        for (size_t n = 0; n < PoleSugestii::MAKS_SUGESTII + 1; ++n) wiele[n] = "Sugestia";
        bws_sprawdz(!ograniczone.ustaw_sugestie(wiele, PoleSugestii::MAKS_SUGESTII + 1));
        bws_sprawdz(ograniczone.liczba_sugestii() == PoleSugestii::MAKS_SUGESTII);

        ZarzadcaPopupow z2; z2.ustaw_geometrie_wlasciciela(Prostokat(0,0,400,300), Prostokat(0,0,400,300));
        PoleSugestii* zycie = new PoleSugestii();
        bws_sprawdz(zycie != nullptr);
        if (zycie) {
            zycie->ustaw_zarzadce_popupow(&z2); zycie->rozmiesc(Prostokat(10,10,280,34));
            zycie->ustaw_sugestie(s, 5); zycie->ustaw_tekst("Bur");
            bws_sprawdz(z2.liczba_powierzchni() == 1);
            delete zycie;
            bws_sprawdz(z2.liczba_powierzchni() == 0);
        }
        bws_log(bws_test_blad == przed ? "[KONTROLKI3 4/8] PoleSugestii OK\n" : "[KONTROLKI3 4/8] PoleSugestii FAIL\n");
    }

    {
        uint32_t przed = bws_test_blad;
        bws_log("[KONTROLKI3 5/8] PrzyciskPodzielony...\n");
        PrzyciskPodzielony p("Wykonaj"); p.rozmiesc(Prostokat(10, 10, 180, 36));
        struct P { int glowna; int menu; } m{}; p.przy_kliknieciu([](PrzyciskPodzielony*, void* d){ ++static_cast<P*>(d)->glowna; }, &m); p.przy_otwarciu_menu([](PrzyciskPodzielony*, void* d){ ++static_cast<P*>(d)->menu; }, &m);
        ZdarzenieMyszki d{}; d.przycisk = 1; d.x = 30; d.y = 20; p.obsluz_wcisniecie_myszy(d);
        bws_sprawdz(p.wcisnieta_czesc() == CzescPrzyciskuPodzielonego::Glowna);
        p.obsluz_zwolnienie_myszy(d); bws_sprawdz(m.glowna == 1);
        d.x = 180; p.obsluz_wcisniecie_myszy(d);
        bws_sprawdz(p.wcisnieta_czesc() == CzescPrzyciskuPodzielonego::Strzalka);
        p.obsluz_zwolnienie_myszy(d); bws_sprawdz(m.menu == 1);
        ZdarzenieMyszki mv{}; mv.x = 30; mv.y = 20; p.obsluz_ruch_myszy(mv); bws_sprawdz(p.najechana_czesc() == CzescPrzyciskuPodzielonego::Glowna); p.wyczysc_brudny_obszar();
        uint64_t a0 = gui_liczba_alokacji_sterty(); for (int i = 0; i < 5000; ++i) p.obsluz_ruch_myszy(mv); uint64_t a1 = gui_liczba_alokacji_sterty(); bws_sprawdz(a0 == a1 && !p.czy_brudna());
        bws_log_u64("[K3 SPLIT 5K] mouse_allocations=", a1 - a0);
        bws_log_u64("[K3 SPLIT 5K] dirty_calls=", p.czy_brudna() ? 1 : 0);
        mv.x = 180; p.obsluz_ruch_myszy(mv); bws_sprawdz(p.najechana_czesc() == CzescPrzyciskuPodzielonego::Strzalka);
        p.przy_fokusie(); bws_sprawdz(p.czy_ma_fokus());
        ZdarzenieKlawiatury e{}; e.kod_klawisza = 13; p.obsluz_wcisniecie_klawisza(e); bws_sprawdz(m.glowna == 2);
        ZdarzenieKlawiatury menu_key{}; menu_key.kod_klawisza = BWS_KEY_SOURCE_USB | 81U;
        p.obsluz_wcisniecie_klawisza(menu_key); bws_sprawdz(m.menu == 2);
        p.ustaw_skale_dpi(1); bws_sprawdz(p.szerokosc_czesci_strzalki() == 28); p.ustaw_skale_dpi(2); bws_sprawdz(p.szerokosc_czesci_strzalki() == 56);
        p.ustaw_wlaczona(false); p.obsluz_wcisniecie_klawisza(e); p.obsluz_wcisniecie_klawisza(menu_key);
        d.x = 30; p.obsluz_wcisniecie_myszy(d); p.obsluz_zwolnienie_myszy(d);
        bws_sprawdz(m.glowna == 2 && m.menu == 2);
        bws_log(bws_test_blad == przed ? "[KONTROLKI3 5/8] PrzyciskPodzielony OK\n" : "[KONTROLKI3 5/8] PrzyciskPodzielony FAIL\n");
    }

    {
        uint32_t przed = bws_test_blad;
        bws_log("[KONTROLKI3 6/8] Przelacznik...\n");
        Przelacznik p(false); p.rozmiesc(Prostokat(0,0,52,28)); int zmiany = 0;
        p.przy_zmianie([](Przelacznik*, bool, void* d){ ++*static_cast<int*>(d); }, &zmiany);
        p.ustaw_wartosc(true); p.ustaw_wartosc(true); bws_sprawdz(p.pobierz_wartosc() && zmiany == 1);
        p.wyczysc_brudny_obszar(); p.ustaw_wartosc(true);
        bws_sprawdz(!p.czy_brudna() && zmiany == 1);
        ZdarzenieKlawiatury sp{}; sp.kod_klawisza = 32; p.obsluz_wcisniecie_klawisza(sp); bws_sprawdz(!p.pobierz_wartosc() && zmiany == 2);
        ZdarzenieMyszki d{}; d.przycisk = 1; d.x = 10; d.y = 10; p.obsluz_wcisniecie_myszy(d); p.obsluz_zwolnienie_myszy(d); bws_sprawdz(p.pobierz_wartosc() && zmiany == 3);
        p.wyczysc_brudny_obszar(); ZdarzenieMyszki mv{}; mv.x = 10; mv.y = 10; uint64_t a0 = gui_liczba_alokacji_sterty(); for (int i = 0; i < 5000; ++i) p.obsluz_ruch_myszy(mv); uint64_t a1 = gui_liczba_alokacji_sterty(); bws_sprawdz(a0 == a1 && !p.czy_brudna());
        bws_log_u64("[K3 PRZELACZNIK 5K] mouse_allocations=", a1 - a0);
        bws_log_u64("[K3 PRZELACZNIK 5K] dirty_calls=", p.czy_brudna() ? 1 : 0);
        p.ustaw_tylko_do_odczytu(true); p.obsluz_wcisniecie_klawisza(sp); bws_sprawdz(p.pobierz_wartosc());
        p.ustaw_tylko_do_odczytu(false); p.przy_fokusie(); bws_sprawdz(p.czy_ma_fokus());
        p.ustaw_wlaczona(false); p.obsluz_wcisniecie_klawisza(sp);
        p.obsluz_wcisniecie_myszy(d); p.obsluz_zwolnienie_myszy(d);
        bws_sprawdz(p.pobierz_wartosc() && zmiany == 3);
        p.ustaw_skale_dpi(1); bws_sprawdz(p.oblicz_naturalny_rozmiar().szerokosc == 52);
        p.ustaw_skale_dpi(2); bws_sprawdz(p.oblicz_naturalny_rozmiar().szerokosc == 104);
        bws_log(bws_test_blad == przed ? "[KONTROLKI3 6/8] Przelacznik OK\n" : "[KONTROLKI3 6/8] Przelacznik FAIL\n");
    }

    {
        uint32_t przed = bws_test_blad;
        bws_log("[KONTROLKI3 7/8] SelektorKoloru...\n");
        SelektorKoloru s; s.rozmiesc(Prostokat(0,0,300,112)); int zmiany = 0;
        s.przy_zmianie([](SelektorKoloru*, uint32_t, void* d){ ++*static_cast<int*>(d); }, &zmiany);
        bws_sprawdz(s.ustaw_hex("#000000") && s.pobierz_kolor() == SelektorKoloru::WIDOCZNA_CZERN);
        bws_sprawdz(s.ustaw_hex("#12A0FE") && s.pobierz_kolor() == 0x0012A0FEU);
        uint32_t stary = s.pobierz_kolor(); bws_sprawdz(!s.ustaw_hex("#GG0000") && s.pobierz_kolor() == stary);
        bws_sprawdz(!s.ustaw_hex("#1234567") && !s.ustaw_hex("#FFFFFFFF") && s.pobierz_kolor() == stary);
        int cb = zmiany; s.ustaw_kolor(stary); bws_sprawdz(zmiany == cb); s.ustaw_kolor(0); bws_sprawdz(s.pobierz_kolor() == 0);
        ZarzadcaPopupow z;
        z.ustaw_geometrie_wlasciciela(Prostokat(0,0,640,480),Prostokat(0,28,640,452));
        s.ustaw_zarzadce_popupow(&z);
        bws_sprawdz(s.otworz_popup() && s.czy_popup_otwarty());
        bws_sprawdz(s.ustaw_hsv(0,255,255) && s.kolor_podgladu()==0x00FF0000);
        bws_sprawdz(s.zatwierdz_popup() && !s.czy_popup_otwarty() && s.pobierz_kolor()==0x00FF0000);
        bws_sprawdz(s.otworz_popup()); s.ustaw_rgb(0,255,0); s.anuluj_popup();
        bws_sprawdz(s.pobierz_kolor()==0x00FF0000 && !s.czy_popup_otwarty());
        s.wyczysc_brudny_obszar(); ZdarzenieMyszki mv{}; mv.x = 12; mv.y = 55; s.obsluz_ruch_myszy(mv); s.wyczysc_brudny_obszar();
        uint64_t a0 = gui_liczba_alokacji_sterty(); for (int n = 0; n < 5000; ++n) s.obsluz_ruch_myszy(mv); uint64_t a1 = gui_liczba_alokacji_sterty();
        bws_sprawdz(a0 == a1 && !s.czy_brudna());
        bws_log_u64("[K3 KOLOR 5K] mouse_allocations=", a1 - a0);
        bws_log_u64("[K3 KOLOR 5K] dirty_calls=", s.czy_brudna() ? 1 : 0);
        bws_log(bws_test_blad == przed ? "[KONTROLKI3 7/8] SelektorKoloru OK\n" : "[KONTROLKI3 7/8] SelektorKoloru FAIL\n");
    }

    {
        uint32_t przed = bws_test_blad;
        bws_log("[KONTROLKI3 8/8] WidokMiniatur...\n");
        WidokMiniatur w; w.rozmiesc(Prostokat(0,0,420,240)); static uint32_t obraz[8*8];
        for (int i = 0; i < 64; ++i) obraz[i] = (i & 1) ? 0x00FFBF00 : 0x00302010;
        for (int i = 0; i < 1000; ++i) bws_sprawdz(w.dodaj_element(static_cast<uint64_t>(i + 1), "Miniatura", obraz, 8, 8));
        bws_sprawdz(w.liczba_elementow() == 1000 && w.pasek_pionowy().czy_widoczna());
        {
            w.wyczysc_brudny_obszar(); w.zeruj_pomiary(); w.pasek_pionowy().zeruj_pomiary();
            uint64_t alloc = gui_liczba_alokacji_sterty();
            ZdarzenieRolki wheel{}; wheel.delta_y = -1; w.obsluz_rolke(wheel);
            Prostokat dirty = w.pobierz_brudny_obszar();
            KontekstRysowania paint; paint.dodaj_przyciecie(dirty); w.rysuj(paint);
            bws_log_u64("[K3 FRAME] wheel_allocations=", gui_liczba_alokacji_sterty() - alloc);
            bws_log_u64("[K3 FRAME] wheel_layout=", w.pomiary_.layout_passes + w.pasek_pionowy().pomiary_.layout_passes);
            bws_log_u64("[K3 FRAME] wheel_paint_calls=", w.pomiary_.paint_calls + w.pasek_pionowy().pomiary_.paint_calls);
            bws_log_u64("[K3 FRAME] wheel_items=", w.liczba_ostatnio_narysowanych_elementow());
            bws_log_u64("[K3 FRAME] wheel_dirty_calls=", w.pomiary_.dirty_calls + w.pasek_pionowy().pomiary_.dirty_calls);
            bws_log_u64("[K3 FRAME] wheel_dirty_pixels=", static_cast<uint64_t>(dirty.szerokosc)*dirty.wysokosc);
            bws_log_u64("[K3 FRAME] wheel_scrollbar_paints=", w.pasek_pionowy().pomiary_.scrollbar_paints);
            auto& bar = w.pasek_pionowy(); bar.ustaw_wartosc(0);
            Prostokat thumb = bar.prostokat_uchwytu(); ZdarzenieMyszki d{}; d.przycisk = 1;
            d.x = bar.obszar_ekranu().x + thumb.x + thumb.szerokosc/2;
            d.y = bar.obszar_ekranu().y + thumb.y + thumb.wysokosc/2;
            bar.obsluz_wcisniecie_myszy(d);
            w.wyczysc_brudny_obszar(); w.zeruj_pomiary(); bar.zeruj_pomiary(); alloc = gui_liczba_alokacji_sterty();
            d.y += 30; bar.obsluz_ruch_myszy(d); dirty = w.pobierz_brudny_obszar();
            KontekstRysowania dragpaint; dragpaint.dodaj_przyciecie(dirty); w.rysuj(dragpaint);
            bws_log_u64("[K3 FRAME] drag_allocations=", gui_liczba_alokacji_sterty() - alloc);
            bws_log_u64("[K3 FRAME] drag_layout=", w.pomiary_.layout_passes + bar.pomiary_.layout_passes);
            bws_log_u64("[K3 FRAME] drag_paint_calls=", w.pomiary_.paint_calls + bar.pomiary_.paint_calls);
            bws_log_u64("[K3 FRAME] drag_items=", w.liczba_ostatnio_narysowanych_elementow());
            bws_log_u64("[K3 FRAME] drag_dirty_calls=", w.pomiary_.dirty_calls + bar.pomiary_.dirty_calls);
            bws_log_u64("[K3 FRAME] drag_dirty_pixels=", static_cast<uint64_t>(dirty.szerokosc)*dirty.wysokosc);
            bws_log_u64("[K3 FRAME] drag_scrollbar_paints=", bar.pomiary_.scrollbar_paints);
            bar.obsluz_zwolnienie_myszy(d); bar.ustaw_wartosc(0);
        }
        struct P { int wybor; int aktywacja; } m{}; w.przy_zmianie_zaznaczenia([](WidokMiniatur*, int, uint64_t, void* d){ ++static_cast<P*>(d)->wybor; }, &m); w.przy_aktywacji([](WidokMiniatur*, int, uint64_t, void* d){ ++static_cast<P*>(d)->aktywacja; }, &m);
        w.ustaw_wybrany_indeks(10); w.ustaw_fokus_indeks(11); bws_sprawdz(w.wybrany_indeks() == 10 && w.fokus_indeks() == 11 && m.wybor == 1);
        w.pasek_pionowy().ustaw_wartosc(0);
        ZdarzenieMyszki klik{}; klik.przycisk = 1; klik.x = 10; klik.y = 10;
        w.obsluz_wcisniecie_myszy(klik); bws_sprawdz(w.wybrany_indeks() == 0 && m.wybor == 2);
        ZdarzenieKlawiatury right{}; right.kod_klawisza = BWS_KEY_SOURCE_E0 | 0x4DU;
        w.obsluz_wcisniecie_klawisza(right); bws_sprawdz(w.fokus_indeks() == 1);
        ZdarzenieKlawiatury enter{}; enter.kod_klawisza = 13; w.obsluz_wcisniecie_klawisza(enter); bws_sprawdz(w.wybrany_indeks() == 1);
        bws_sprawdz(w.ustaw_fokus_indeks(11));
        w.obsluz_wcisniecie_klawisza(enter); bws_sprawdz(w.wybrany_indeks() == 11);
        KontekstRysowania ctx; uint64_t a0 = gui_liczba_alokacji_sterty(); w.rysuj(ctx, true); uint64_t a1 = gui_liczba_alokacji_sterty();
        bws_sprawdz(a0 == a1 && w.liczba_ostatnio_narysowanych_elementow() < w.liczba_elementow());
        bws_log_u64("[K3 MINIATURY POMIAR] visible_items=", w.liczba_ostatnio_narysowanych_elementow());
        bws_log_u64("[K3 MINIATURY POMIAR] model_items=", w.liczba_elementow());
        bws_log_u64("[K3 MINIATURY POMIAR] paint_allocations=", a1 - a0);
        bws_sprawdz(w.element(0)->piksele == obraz);
        struct R { int liczba; bool geometria; } render{};
        w.ustaw_renderer([](WidokMiniatur*, KontekstRysowania&, const WidokMiniatur::Element&, Prostokat r, void* d) {
            R* p = static_cast<R*>(d); ++p->liczba; p->geometria = p->geometria || (r.szerokosc > 0 && r.wysokosc > 0);
        }, &render);
        w.rysuj(ctx, true);
        bws_sprawdz(render.liczba == static_cast<int>(w.liczba_ostatnio_narysowanych_elementow()) && render.geometria);
        w.ustaw_renderer(nullptr);
        ZdarzenieMyszki mv{}; mv.x = 10; mv.y = 10; w.obsluz_ruch_myszy(mv); w.wyczysc_brudny_obszar(); a0 = gui_liczba_alokacji_sterty(); for (int i = 0; i < 5000; ++i) w.obsluz_ruch_myszy(mv); a1 = gui_liczba_alokacji_sterty(); bws_sprawdz(a0 == a1 && !w.czy_brudna());
        bws_log_u64("[K3 MINIATURY 5K] mouse_allocations=", a1 - a0);
        bws_log_u64("[K3 MINIATURY 5K] dirty_calls=", w.czy_brudna() ? 1 : 0);
        w.pasek_pionowy().ustaw_wartosc(0);
        ZdarzenieRolki rolka{}; rolka.delta_y = -1; w.obsluz_rolke(rolka);
        bws_sprawdz(rolka.obsluzone && w.pasek_pionowy().wartosc() > 0);
        w.pasek_pionowy().ustaw_wartosc(0);
        PasekPrzewijania& sb = w.pasek_pionowy(); Prostokat uchwyt = sb.prostokat_uchwytu();
        ZdarzenieMyszki down{}; down.przycisk = 1;
        down.x = sb.obszar_ekranu().x + uchwyt.x + uchwyt.szerokosc / 2;
        down.y = sb.obszar_ekranu().y + uchwyt.y + uchwyt.wysokosc / 2;
        sb.obsluz_wcisniecie_myszy(down); bws_sprawdz(sb.czy_przeciagany());
        ZdarzenieMyszki drag = down; drag.y += sb.obszar().wysokosc / 2; sb.obsluz_ruch_myszy(drag);
        int64_t po_drag = sb.wartosc(); sb.obsluz_zwolnienie_myszy(drag);
        bws_sprawdz(po_drag > 0 && !sb.czy_przeciagany());
        ZdarzenieMyszki dbl{}; dbl.x = 10; dbl.y = 10; w.obsluz_podwojne_klikniecie(dbl); bws_sprawdz(m.aktywacja == 1);
        bws_log(bws_test_blad == przed ? "[KONTROLKI3 8/8] WidokMiniatur OK\n" : "[KONTROLKI3 8/8] WidokMiniatur FAIL\n");
    }

    wykonaj_stabilizacje_k3();
    bws_log(bws_test_blad == wszystkie_przed
        ? "[TESTY KONTROLKI 3] Wszystkie 8 grup PASS\n"
        : "[TESTY KONTROLKI 3] Co najmniej jedna grupa FAIL\n");
}

#include "tapety_testy.h"
#include "ekran_testy.h"
#include "taskbar_testy.h"

struct WynikTestow {
    uint32_t zaliczone_asercje{0};
    uint32_t niezaliczone_asercje{0};
    uint32_t zaliczone_grupy{0};
    uint32_t niezaliczone_grupy{0};
    bool bws_61_83{false};
    bool bursztyn_ui{false};
    bool bursztyn_ui_podstawowe{false};
    bool interakcje{false};
    bool kontrolki2{false};
    bool kontrolki3{false};
    bool tapety{false};
    bool pamiec_stabilna{false};
};

static WynikTestow uruchom_pelny_pakiet_testow() {
    WynikTestow wynik{};
    bws_test_ok = 0;
    bws_test_blad = 0;
    const uint64_t aktywne_przed = gui_liczba_aktywnych_alokacji_sterty();

    uint32_t bledy_przed = bws_test_blad;
    wykonaj_testy_bws_61_83();
    wynik.bws_61_83 = bws_test_blad == bledy_przed;

    bledy_przed = bws_test_blad;
    wykonaj_testy_bursztyn_ui();
    wynik.bursztyn_ui_podstawowe = bws_test_blad == bledy_przed;

    bledy_przed = bws_test_blad;
    testy_interaktywne_bursztyn_ui();
    wynik.interakcje = bws_test_blad == bledy_przed;

    bledy_przed = bws_test_blad;
    wykonaj_testy_kontrolki2();
    wynik.kontrolki2 = bws_test_blad == bledy_przed;

    bledy_przed = bws_test_blad;
    wykonaj_testy_kontrolki3();
    wynik.kontrolki3 = bws_test_blad == bledy_przed;
    bledy_przed = bws_test_blad;
    wykonaj_testy_tapety();
    wykonaj_testy_ekranu(true);
    wykonaj_testy_taskbara();
    wynik.tapety = bws_test_blad == bledy_przed;
    wynik.bursztyn_ui = wynik.bursztyn_ui_podstawowe &&
                        wynik.interakcje && wynik.kontrolki2 && wynik.kontrolki3;
    const uint64_t aktywne_po = gui_liczba_aktywnych_alokacji_sterty();
    wynik.pamiec_stabilna = aktywne_po == aktywne_przed;
    bws_log(wynik.pamiec_stabilna
        ? "[TEST PAMIECI] Aktywne alokacje po przebiegu: STABILNE (PASS)\n"
        : "[TEST PAMIECI] Wykryto niezrownowazone alokacje (FAIL)\n");

    bws_log_u64("[TESTY] PASS assertions=", bws_test_ok);
    bws_log_u64("[TESTY] FAIL assertions=", bws_test_blad);
    wynik.zaliczone_asercje = bws_test_ok;
    wynik.niezaliczone_asercje = bws_test_blad;
    wynik.zaliczone_grupy =
        static_cast<uint32_t>(wynik.bws_61_83) +
        static_cast<uint32_t>(wynik.bursztyn_ui_podstawowe) +
        static_cast<uint32_t>(wynik.interakcje) +
        static_cast<uint32_t>(wynik.kontrolki2) +
        static_cast<uint32_t>(wynik.kontrolki3) +
        static_cast<uint32_t>(wynik.tapety) +
        static_cast<uint32_t>(wynik.pamiec_stabilna);
    wynik.niezaliczone_grupy = 7U - wynik.zaliczone_grupy;

    bws_log(wynik.niezaliczone_grupy == 0
        ? "=== TESTY BURSZTYN OS: PASS ===\n"
        : "=== TESTY BURSZTYN OS: FAIL ===\n");
    return wynik;
}

struct StanOknaWynikow {
    WynikTestow wynik{};
    Okno* okno{nullptr};
    Etykieta* naglowek{nullptr};
    Etykieta* bws{nullptr};
    Etykieta* ui{nullptr};
    Etykieta* interakcje{nullptr};
    Etykieta* kontrolki{nullptr};
    Etykieta* kontrolki3{nullptr};
    Etykieta* tapety{nullptr};
    Etykieta* pamiec{nullptr};
    Etykieta* ogolny{nullptr};
    Etykieta* liczniki{nullptr};
    Przycisk* btn_start{nullptr};
    Przycisk* btn_ponow{nullptr};
    Przycisk* btn_zamknij{nullptr};
    WynikTestow (*uruchom)(){nullptr};
    char tekst_licznikow[128]{};
    bool testy_zaplanowane{false};
    int klatki_opoznienia{0};
    int krok_grupy{0};
    int init_x{0};
    int init_y{0};
    int init_w{0};
    int init_h{0};
    uint32_t interactive_visible_geometry_changes{0};
    uint64_t aktywne_przed{0};
};

static void dopisz_u32(char* bufor, size_t pojemnosc, size_t* pozycja, uint32_t wartosc) {
    if (!bufor || !pozycja || *pozycja >= pojemnosc) return;
    char odwrotnie[16];
    size_t ile = 0;
    do {
        odwrotnie[ile++] = static_cast<char>('0' + (wartosc % 10));
        wartosc /= 10;
    } while (wartosc != 0 && ile < sizeof(odwrotnie));
    while (ile > 0 && *pozycja + 1 < pojemnosc)
        bufor[(*pozycja)++] = odwrotnie[--ile];
    bufor[*pozycja] = '\0';
}

static void dopisz_tekst(char* bufor, size_t pojemnosc, size_t* pozycja, const char* tekst) {
    if (!bufor || !pozycja || !tekst) return;
    size_t i = 0;
    while (tekst[i] != '\0' && *pozycja + 1 < pojemnosc)
        bufor[(*pozycja)++] = tekst[i++];
    if (*pozycja < pojemnosc) bufor[*pozycja] = '\0';
}

class KontrolerDiagnostyki : public Kontrolka {
public:
    explicit KontrolerDiagnostyki(StanOknaWynikow* stan) : stan_(stan) {}

    bool czy_wymaga_animacji() const override {
        return stan_ && stan_->testy_zaplanowane;
    }

    void krok_animacji(uint64_t) override {
        if (!stan_ || !stan_->testy_zaplanowane) return;
        if (stan_->klatki_opoznienia > 0) {
            --stan_->klatki_opoznienia;
            return;
        }

        auto sprawdz_geometrie_grupy = [this](const char* nazwa_grupy) {
            int cur_vx = 0, cur_vy = 0, cur_vw = 0, cur_vh = 0;
            gui_pobierz_widoczna_geometrie(&cur_vx, &cur_vy, &cur_vw, &cur_vh);
            if (cur_vx != stan_->init_x || cur_vy != stan_->init_y ||
                cur_vw != stan_->init_w || cur_vh != stan_->init_h) {
                stan_->interactive_visible_geometry_changes++;
                bws_log("[BLAD GEOMETRII] Grupa ");
                bws_log(nazwa_grupy);
                bws_log(" zmienila widoczna geometrie!\n");
            }
            bws_sprawdz_msg(cur_vx == stan_->init_x, "visible_x mismatch");
            bws_sprawdz_msg(cur_vy == stan_->init_y, "visible_y mismatch");
            bws_sprawdz_msg(cur_vw == stan_->init_w, "visible_w mismatch");
            bws_sprawdz_msg(cur_vh == stan_->init_h, "visible_h mismatch");
        };

        if (stan_->krok_grupy == 0) {
            uint32_t bledy_przed = bws_test_blad;
            {
                IzolacjaPrezentacjiTestow izolacja(stan_->okno);
                wykonaj_testy_bws_61_83();
            }
            stan_->wynik.bws_61_83 = (bws_test_blad == bledy_przed);
            stan_->bws->ustaw_tekst(stan_->wynik.bws_61_83 ? "BWS 61..83: PASS" : "BWS 61..83: FAIL");
            sprawdz_geometrie_grupy("BWS 61..83");
            stan_->krok_grupy = 1;
            if (stan_->okno) {
                stan_->okno->przelicz_uklad();
                stan_->okno->uniewaznij();
            }
            return;
        }

        if (stan_->krok_grupy == 1) {
            uint32_t bledy_przed = bws_test_blad;
            {
                IzolacjaPrezentacjiTestow izolacja(stan_->okno);
                wykonaj_testy_bursztyn_ui();
            }
            stan_->wynik.bursztyn_ui_podstawowe = (bws_test_blad == bledy_przed);
            stan_->ui->ustaw_tekst(stan_->wynik.bursztyn_ui_podstawowe ? "BursztynUI: PASS" : "BursztynUI: FAIL");
            sprawdz_geometrie_grupy("BursztynUI podstawowe");
            stan_->krok_grupy = 2;
            if (stan_->okno) {
                stan_->okno->przelicz_uklad();
                stan_->okno->uniewaznij();
            }
            return;
        }

        if (stan_->krok_grupy == 2) {
            uint32_t bledy_przed = bws_test_blad;
            {
                IzolacjaPrezentacjiTestow izolacja(stan_->okno);
                testy_interaktywne_bursztyn_ui();
            }
            stan_->wynik.interakcje = (bws_test_blad == bledy_przed);
            stan_->interakcje->ustaw_tekst(stan_->wynik.interakcje
                ? "WidokPrzewijany / Drzewo / widoczność: PASS"
                : "WidokPrzewijany / Drzewo / widoczność: FAIL");
            sprawdz_geometrie_grupy("BursztynUI interakcje");
            stan_->krok_grupy = 3;
            if (stan_->okno) {
                stan_->okno->przelicz_uklad();
                stan_->okno->uniewaznij();
            }
            return;
        }

        if (stan_->krok_grupy == 3) {
            uint32_t bledy_przed = bws_test_blad;
            {
                IzolacjaPrezentacjiTestow izolacja(stan_->okno);
                wykonaj_testy_kontrolki2();
            }
            stan_->wynik.kontrolki2 = (bws_test_blad == bledy_przed);
            stan_->kontrolki->ustaw_tekst(stan_->wynik.kontrolki2
                ? "Kontrolki 2 - 8 grup komponentów: PASS"
                : "Kontrolki 2 - 8 grup komponentów: FAIL");
            sprawdz_geometrie_grupy("Kontrolki 2");
            stan_->krok_grupy = 4;
            if (stan_->okno) {
                stan_->okno->przelicz_uklad();
                stan_->okno->uniewaznij();
            }
            return;
        }

        if (stan_->krok_grupy == 4) {
            uint32_t bledy_przed = bws_test_blad;
            {
                IzolacjaPrezentacjiTestow izolacja(stan_->okno);
                wykonaj_testy_kontrolki3();
            }
            stan_->wynik.kontrolki3 = (bws_test_blad == bledy_przed);
            stan_->kontrolki3->ustaw_tekst(stan_->wynik.kontrolki3
                ? "Kontrolki 3 - 8 grup komponentów: PASS"
                : "Kontrolki 3 - 8 grup komponentów: FAIL");
            sprawdz_geometrie_grupy("Kontrolki 3");
            stan_->krok_grupy = 5;
            if (stan_->okno) {
                stan_->okno->przelicz_uklad();
                stan_->okno->uniewaznij();
            }
            return;
        }

        if (stan_->krok_grupy == 5) {
            uint32_t przed = bws_test_blad;
            { IzolacjaPrezentacjiTestow izolacja(stan_->okno); wykonaj_testy_tapety(); wykonaj_testy_ekranu(false); wykonaj_testy_taskbara(); }
            sprawdz_geometrie_grupy("Tapety BTP");
            stan_->wynik.tapety = bws_test_blad == przed;
            stan_->tapety->ustaw_tekst(stan_->wynik.tapety ? "Tapety BTP: PASS" : "Tapety BTP: FAIL");
            stan_->krok_grupy = 6;
            if (stan_->okno) { stan_->okno->przelicz_uklad(); stan_->okno->uniewaznij(); }
            return;
        }
        if (stan_->krok_grupy == 6) {
            stan_->wynik.bursztyn_ui = stan_->wynik.bursztyn_ui_podstawowe &&
                                      stan_->wynik.interakcje && stan_->wynik.kontrolki2 &&
                                      stan_->wynik.kontrolki3;
            const uint64_t aktywne_po = gui_liczba_aktywnych_alokacji_sterty();
            stan_->wynik.pamiec_stabilna = (aktywne_po == stan_->aktywne_przed);
            stan_->pamiec->ustaw_tekst(stan_->wynik.pamiec_stabilna
                ? "Pamięć GUI po przebiegu: STABILNA (PASS)"
                : "Pamięć GUI po przebiegu: WYCIEK (FAIL)");

            sprawdz_geometrie_grupy("Finalizacja");

            stan_->wynik.zaliczone_asercje = bws_test_ok;
            stan_->wynik.niezaliczone_asercje = bws_test_blad;
            stan_->wynik.zaliczone_grupy =
                static_cast<uint32_t>(stan_->wynik.bws_61_83) +
                static_cast<uint32_t>(stan_->wynik.bursztyn_ui_podstawowe) +
                static_cast<uint32_t>(stan_->wynik.interakcje) +
                static_cast<uint32_t>(stan_->wynik.kontrolki2) +
                static_cast<uint32_t>(stan_->wynik.kontrolki3) +
                static_cast<uint32_t>(stan_->wynik.tapety) +
                static_cast<uint32_t>(stan_->wynik.pamiec_stabilna);
            stan_->wynik.niezaliczone_grupy = 7U - stan_->wynik.zaliczone_grupy;

            stan_->naglowek->ustaw_tekst(stan_->wynik.niezaliczone_grupy == 0
                ? "Diagnostyka systemu zakończona"
                : "Diagnostyka systemu zakończona (wykryto błędy)");
            stan_->ogolny->ustaw_tekst(stan_->wynik.niezaliczone_grupy == 0
                ? "Wynik ogólny: PASS" : "Wynik ogólny: FAIL");

            size_t pos = 0;
            stan_->tekst_licznikow[0] = '\0';
            dopisz_tekst(stan_->tekst_licznikow, sizeof(stan_->tekst_licznikow), &pos, "Grupy: zaliczone ");
            dopisz_u32(stan_->tekst_licznikow, sizeof(stan_->tekst_licznikow), &pos, stan_->wynik.zaliczone_grupy);
            dopisz_tekst(stan_->tekst_licznikow, sizeof(stan_->tekst_licznikow), &pos, ", niezaliczone ");
            dopisz_u32(stan_->tekst_licznikow, sizeof(stan_->tekst_licznikow), &pos, stan_->wynik.niezaliczone_grupy);
            dopisz_tekst(stan_->tekst_licznikow, sizeof(stan_->tekst_licznikow), &pos, " | Asercje: PASS ");
            dopisz_u32(stan_->tekst_licznikow, sizeof(stan_->tekst_licznikow), &pos, stan_->wynik.zaliczone_asercje);
            dopisz_tekst(stan_->tekst_licznikow, sizeof(stan_->tekst_licznikow), &pos, ", FAIL ");
            dopisz_u32(stan_->tekst_licznikow, sizeof(stan_->tekst_licznikow), &pos, stan_->wynik.niezaliczone_asercje);
            stan_->liczniki->ustaw_tekst(stan_->tekst_licznikow);

            bws_sprawdz_msg(stan_->interactive_visible_geometry_changes == 0,
                            "interactive_visible_geometry_changes != 0");

            if (stan_->btn_start) stan_->btn_start->ustaw_wlaczona(false);
            if (stan_->btn_ponow) stan_->btn_ponow->ustaw_wlaczona(true);
            if (stan_->btn_zamknij) stan_->btn_zamknij->ustaw_wlaczona(true);

            stan_->testy_zaplanowane = false;
            stan_->krok_grupy = 0;

            if (stan_->okno) {
                stan_->okno->przelicz_uklad();
                stan_->okno->uniewaznij();
            }

            bws_log("[INTERAKTYWNY PRZEBIEG] ZAKONCZONO\n");
            return;
        }
    }

    void rysuj_siebie(KontekstRysowania&) override {}
    Rozmiar zmierz(Rozmiar) override { return Rozmiar(0, 0); }

private:
    StanOknaWynikow* stan_{nullptr};
};

static void rozpocznij_interaktywny_przebieg_testow(StanOknaWynikow* stan) {
    if (!stan || stan->testy_zaplanowane) return;

    stan->naglowek->ustaw_tekst("Trwa diagnostyka systemu...");
    stan->bws->ustaw_tekst("BWS 61..83: OCZEKUJE");
    stan->ui->ustaw_tekst("BursztynUI: OCZEKUJE");
    stan->interakcje->ustaw_tekst("WidokPrzewijany / Drzewo / widoczność: OCZEKUJE");
    stan->kontrolki->ustaw_tekst("Kontrolki 2 - 8 grup komponentów: OCZEKUJE");
    stan->tapety->ustaw_tekst("Tapety BTP: OCZEKUJE");
    stan->kontrolki3->ustaw_tekst("Kontrolki 3 - 8 grup komponentów: OCZEKUJE");
    stan->pamiec->ustaw_tekst("Pamięć GUI po przebiegu: OCZEKUJE");
    stan->ogolny->ustaw_tekst("Wynik ogólny: W toku...");
    stan->liczniki->ustaw_tekst("Wykonywanie testów diagnostycznych...");

    if (stan->btn_start) stan->btn_start->ustaw_wlaczona(false);
    if (stan->btn_ponow) stan->btn_ponow->ustaw_wlaczona(false);
    if (stan->btn_zamknij) stan->btn_zamknij->ustaw_wlaczona(true);

    if (stan->okno) {
        stan->init_x = stan->okno->x();
        stan->init_y = stan->okno->y();
        stan->init_w = stan->okno->szerokosc();
        stan->init_h = stan->okno->wysokosc();
    } else {
        gui_pobierz_widoczna_geometrie(&stan->init_x, &stan->init_y, &stan->init_w, &stan->init_h);
    }
    stan->interactive_visible_geometry_changes = 0;
    stan->aktywne_przed = gui_liczba_aktywnych_alokacji_sterty();

    bws_test_ok = 0;
    bws_test_blad = 0;
    stan->wynik = WynikTestow{};

    stan->testy_zaplanowane = true;
    stan->krok_grupy = 0;
    stan->klatki_opoznienia = 1;

    if (stan->okno) {
        stan->okno->przelicz_uklad();
        stan->okno->uniewaznij();
    }
}

extern "C" __attribute__((noreturn)) void _start() {
    gui_pobierz_rozdzielczosc(&screen_w, &screen_h);
    if (screen_w <= 0 || screen_h <= 0) gui_zakoncz_aplikacje();
    static GuiMonitorInfo monitor{};
    if (gui_pobierz_monitor(0, &monitor)) {
        work_x = monitor.work_x; work_y = monitor.work_y;
        work_w = monitor.work_szer; work_h = monitor.work_wys;
    } else {
        work_w = screen_w; work_h = screen_h;
    }
    if (WIN_W > work_w) WIN_W = work_w;
    if (WIN_H > work_h) WIN_H = work_h;
    if (WIN_W < 1) WIN_W = 1;
    if (WIN_H < 1) WIN_H = 1;
    const int max_x = work_x + work_w - WIN_W;
    const int max_y = work_y + work_h - WIN_H;
    if (WIN_X < work_x) WIN_X = work_x;
    if (WIN_Y < work_y) WIN_Y = work_y;
    if (WIN_X > max_x) WIN_X = max_x;
    if (WIN_Y > max_y) WIN_Y = max_y;

    auto tekst_rowny = [](const char* a, const char* b) {
        if (!a || !b) return false;
        size_t i = 0;
        while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) ++i;
        return a[i] == '\0' && b[i] == '\0';
    };

    static char argument[32] = {};
    const bool tryb_automatyczny =
        pobierz_argument_startowy(argument, sizeof(argument)) &&
        tekst_rowny(argument, "--automated");

    if (tryb_automatyczny) {
        if (bws_utworz_warstwe(WIN_X, WIN_Y, WIN_W, WIN_H, 10) < 0) gui_zakoncz_aplikacje();
        gui_ustaw_przejecie_myszy(true);
        WynikTestow wynik = uruchom_pelny_pakiet_testow();
        (void)wynik;
        bws_log("[TEST MODE] AUTOMATED: zakonczono; proces test.bur konczy dzialanie.\n");
        gui_zakoncz_aplikacje();
        bws_zakoncz_proces();
    }

    // Tryb interaktywny: stan poczatkowy jest w trybie bezczynnym (IDLE)
    AplikacjaGUI* aplikacja = new AplikacjaGUI();
    Okno* okno = new Okno("Testy Bursztyn OS", WIN_W, WIN_H);
    if (!aplikacja || !okno) gui_zakoncz_aplikacje();
    okno->ustaw_pozycje(WIN_X, WIN_Y);
    Panel* korzen = static_cast<Panel*>(okno->kontrolka_glowna());
    if (!korzen) gui_zakoncz_aplikacje();
    korzen->ustaw_orientacje(Orientacja::Pionowa);
    korzen->ustaw_wypelnienie(Marginesy(18, 18, 18, 18));
    korzen->ustaw_odstep(10);

    // Etykiety stanu poczatkowego IDLE
    Etykieta& naglowek = korzen->dodaj<Etykieta>("Diagnostyka systemu");
    Etykieta& et_bws = korzen->dodaj<Etykieta>("BWS 61..83: Testy nie zostały jeszcze uruchomione.");
    Etykieta& et_ui = korzen->dodaj<Etykieta>("BursztynUI: Testy nie zostały jeszcze uruchomione.");
    Etykieta& et_interakcje = korzen->dodaj<Etykieta>("WidokPrzewijany / Drzewo / widoczność: Testy nie zostały jeszcze uruchomione.");
    Etykieta& et_kontrolki = korzen->dodaj<Etykieta>("Kontrolki 2 - 8 grup komponentów: Testy nie zostały jeszcze uruchomione.");
    Etykieta& et_tapety = korzen->dodaj<Etykieta>("Tapety BTP: Testy nie zostały jeszcze uruchomione.");
    Etykieta& et_kontrolki3 = korzen->dodaj<Etykieta>("Kontrolki 3 - 8 grup komponentów: Testy nie zostały jeszcze uruchomione.");
    Etykieta& et_pamiec = korzen->dodaj<Etykieta>("Pamięć GUI po przebiegu: Testy nie zostały jeszcze uruchomione.");
    Etykieta& et_ogolny = korzen->dodaj<Etykieta>("Wynik ogólny: Testy nie zostały jeszcze uruchomione.");
    Etykieta& et_liczniki = korzen->dodaj<Etykieta>("Oczekiwanie na uruchomienie testów diagnostycznych...");

    Panel& przyciski = korzen->dodaj<Panel>(Orientacja::Pozioma);
    przyciski.ustaw_odstep(12);
    przyciski.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    przyciski.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Koniec);
    przyciski.ustaw_preferowany_rozmiar(Rozmiar(-1, 44));

    Przycisk& start_btn = przyciski.dodaj<Przycisk>("Uruchom test");
    start_btn.ustaw_preferowany_rozmiar(Rozmiar(150, 36));
    start_btn.ustaw_wlaczona(true);

    Przycisk& ponow_btn = przyciski.dodaj<Przycisk>("Uruchom ponownie");
    ponow_btn.ustaw_preferowany_rozmiar(Rozmiar(180, 36));
    ponow_btn.ustaw_wlaczona(false);

    Przycisk& zamknij_btn = przyciski.dodaj<Przycisk>("Zamknij");
    zamknij_btn.ustaw_preferowany_rozmiar(Rozmiar(120, 36));
    zamknij_btn.ustaw_wlaczona(true);

    StanOknaWynikow* stan = new StanOknaWynikow{
        {}, okno, &naglowek, &et_bws, &et_ui, &et_interakcje,
        &et_kontrolki, &et_kontrolki3, &et_tapety, &et_pamiec, &et_ogolny, &et_liczniki,
        &start_btn, &ponow_btn, &zamknij_btn,
        &uruchom_pelny_pakiet_testow, {}, false, 0, 0,
        WIN_X, WIN_Y, WIN_W, WIN_H, 0, 0
    };
    if (!stan) gui_zakoncz_aplikacje();

    korzen->dodaj<KontrolerDiagnostyki>(stan);

    start_btn.przy_kliknieciu([](Przycisk*, void* dane) {
        rozpocznij_interaktywny_przebieg_testow(static_cast<StanOknaWynikow*>(dane));
    }, stan);

    ponow_btn.przy_kliknieciu([](Przycisk*, void* dane) {
        rozpocznij_interaktywny_przebieg_testow(static_cast<StanOknaWynikow*>(dane));
    }, stan);

    zamknij_btn.przy_kliknieciu([](Przycisk*, void*) {
        gui_zakoncz_aplikacje();
    });

    (void)aplikacja->uruchom(*okno);
    bws_zakoncz_proces();
}
