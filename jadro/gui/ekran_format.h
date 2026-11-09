#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ui_skala.h"

constexpr uint64_t EKRAN_IPC = 0x454B52414E000001ULL;
#include "app_identity.h"
constexpr const char* EKRAN_PROGRAM = AppIdentity::DisplaySettings;
constexpr const char* EKRAN_CFG = "/ustawienia/ekran.cfg";
constexpr size_t EKRAN_CFG_MAX = 192;
constexpr unsigned EKRAN_MAX_TRYBOW = 16;

// Jeden formatter dla tekstow trybu widocznych dla uzytkownika. Separator
// jest ASCII, aby nie zalezec od obslugi U+00D7 w konkretnej sciezce tekstu.
// Funkcja nie zwraca uciętego napisu: przy zbyt malym buforze zwraca false i
// pozostawia poprawny pusty lancuch.
inline const char* ekran_przyjazna_nazwa(uint32_t szerokosc, uint32_t wysokosc) {
    if (szerokosc == 1280 && wysokosc == 720) return "HD";
    if (szerokosc == 1920 && wysokosc == 1080) return "Full HD";
    if (szerokosc == 2560 && wysokosc == 1440) return "QHD";
    if (szerokosc == 3440 && wysokosc == 1440) return "UWQHD";
    if (szerokosc == 3840 && wysokosc == 2160) return "4K UHD";
    return nullptr;
}

inline bool ekran_formatuj_tryb(char* out, size_t cap, uint32_t szerokosc,
                                uint32_t wysokosc, bool dodaj_przyjazna_nazwe = true) {
    if (!out || cap == 0) return false;
    char tmp[48]{};
    size_t pos = 0;
    auto znak = [&](char c) {
        if (pos + 1 >= sizeof(tmp)) return false;
        tmp[pos++] = c;
        return true;
    };
    auto napis = [&](const char* s) {
        while (s && *s) if (!znak(*s++)) return false;
        return true;
    };
    auto liczba = [&](uint32_t n) {
        char odwrotnie[10];
        unsigned ile = 0;
        do { odwrotnie[ile++] = char('0' + n % 10); n /= 10; } while (n && ile < sizeof(odwrotnie));
        while (ile) if (!znak(odwrotnie[--ile])) return false;
        return true;
    };

    const char* nazwa = dodaj_przyjazna_nazwe ? ekran_przyjazna_nazwa(szerokosc, wysokosc) : nullptr;
    bool ok = liczba(szerokosc) && napis(" x ") && liczba(wysokosc);
    if (ok && nazwa) ok = napis(" (") && napis(nazwa) && znak(')');
    if (!ok || pos + 1 > cap) { out[0] = '\0'; return false; }
    for (size_t i = 0; i < pos; ++i) out[i] = tmp[i];
    out[pos] = '\0';
    return true;
}

struct TrybEkranu { uint32_t szerokosc, wysokosc, bpp, pitch, odswiezanie, flagi; };
struct MozliwosciEkranu {
    uint32_t mozna_wyliczyc_tryby, mozna_zmienic_runtime, wymagany_restart, backend;
};
enum : uint32_t { EKRAN_VESA=1, EKRAN_GOP=2, EKRAN_BOCHS=3, EKRAN_VIRTIO_GPU=4 };
enum : uint32_t { EKRAN_STAN=1, EKRAN_ZASTOSUJ, EKRAN_ZACHOWAJ, EKRAN_PRZYWROC, EKRAN_TEST };
enum : uint32_t { EKRAN_OK=0, EKRAN_NIEOBSLUGIWANY, EKRAN_PAMIEC, EKRAN_SPRZET,
    EKRAN_ZAJETY, EKRAN_ZAPIS, EKRAN_OCZEKUJE, EKRAN_UPRAWNIENIA };
struct EkranZadanie {
    uint32_t wersja, operacja;
    uint64_t odpowiedz;
    uint32_t szerokosc, wysokosc, bpp, skala;
    uint64_t transakcja;
    uint32_t rezerwa[2];
};
struct EkranOdpowiedz {
    uint32_t wersja, status;
    MozliwosciEkranu mozliwosci;
    TrybEkranu aktywny;
    uint32_t skala, liczba_trybow;
    uint64_t transakcja;
    uint32_t pozostalo_sekund, generacja;
    uint64_t bufory_bajty, alokacje, zmiany, przywrocenia;
    TrybEkranu tryby[EKRAN_MAX_TRYBOW];
    uint32_t asercje,bledy;
};
static_assert(sizeof(EkranOdpowiedz) <= 512, "Odpowiedz miesci sie w BWS IPC");
inline bool ekran_rozmiar(const TrybEkranu& t, size_t* bajty, size_t* strony=nullptr) {
    if (!bajty || t.bpp!=32 || !t.szerokosc || !t.wysokosc ||
        t.szerokosc>8192 || t.wysokosc>8192 || (t.pitch&3) ||
        uint64_t(t.szerokosc)*4>t.pitch) return false;
    const uint64_t n=uint64_t(t.pitch)*t.wysokosc;
    if (!n || n>64ULL*1024*1024 || n>SIZE_MAX-4095) return false;
    *bajty=size_t(n);
    if (strony) *strony=(size_t(n)+4095)/4096;
    return true;
}
inline bool ekran_ten_sam(const TrybEkranu&a,const TrybEkranu&b) {
    return a.szerokosc==b.szerokosc && a.wysokosc==b.wysokosc && a.bpp==b.bpp && a.pitch==b.pitch;
}
struct EkranKonfiguracja { uint32_t szerokosc, wysokosc, bpp, skala; };
bool ekran_parsuj(const char*, size_t, EkranKonfiguracja*);
size_t ekran_serializuj(const EkranKonfiguracja&, char*, size_t);

struct GeometriaEkranu { int32_t x,y,szer,wys; };
inline GeometriaEkranu ekran_dopasuj(GeometriaEkranu r,int32_t w,int32_t h,bool max){
    if (w < 1 || h < 1) return {0, 0, 0, 0};
    if (max) return {0, 0, w, h};
    if (r.szer > w) r.szer = w;
    if (r.wys > h) r.wys = h;
    if (r.szer < 1) r.szer = 1;
    if (r.wys < 1) r.wys = 1;
    if (r.x > w - r.szer) r.x = w - r.szer;
    if (r.y > h - r.wys) r.y = h - r.wys;
    if (r.x < 0) r.x = 0;
    if (r.y < 0) r.y = 0;
    return r;
}

constexpr int EKRAN_OKNO_SZEROKOSC = 640;
constexpr int EKRAN_TRESC_NORMALNA_H = 220;
constexpr int EKRAN_TRESC_POTWIERDZENIE_H = 220;
constexpr int EKRAN_PASEK_PRZYCISKOW_H = 42;
constexpr int EKRAN_BELKA_H = 28;
constexpr int EKRAN_OKNO_NORMALNE_H = EKRAN_BELKA_H + EKRAN_TRESC_NORMALNA_H + EKRAN_PASEK_PRZYCISKOW_H;
constexpr int EKRAN_OKNO_POTWIERDZENIE_H = EKRAN_OKNO_NORMALNE_H;

struct EkranGeometriaUkladu {
    GeometriaEkranu okno;
    int belka_wys;
    int klient_wys;
    int pasek_przyciskow_y;
    int pasek_przyciskow_wys;
    int widok_wys;
    int zawartosc_wys;
    int maks_przewiniecie;
    bool scrollbar_widoczny;
};

inline EkranGeometriaUkladu ekran_oblicz_uklad(int screen_w, int screen_h, uint32_t skala, bool pending) {
    (void)pending;
    const int taskbar_h = ui_skaluj(40, skala);
    const int work_w = screen_w;
    const int work_h = screen_h > taskbar_h ? screen_h - taskbar_h : screen_h;

    const int baza_w = EKRAN_OKNO_SZEROKOSC;
    const int baza_h = EKRAN_OKNO_NORMALNE_H;

    int w = ui_skaluj(baza_w, skala);
    if (w > work_w) w = work_w;
    int h = ui_skaluj(baza_h, skala);
    if (h > work_h) h = work_h;

    int x = (work_w - w) / 2;
    int y = (work_h - h) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    const int belka = ui_skaluj(EKRAN_BELKA_H, skala);
    const int klient_h = h > belka ? h - belka : 0;
    int pasek_h = ui_skaluj(EKRAN_PASEK_PRZYCISKOW_H, skala);
    if (pasek_h > klient_h) pasek_h = klient_h;
    const int widok_h = klient_h - pasek_h;
    const int pasek_y = widok_h;

    const int tresc_h = ui_skaluj(EKRAN_TRESC_NORMALNA_H, skala);
    const int max_scroll = tresc_h > widok_h ? tresc_h - widok_h : 0;
    const bool scrollbar = max_scroll > 0;

    return {{x, y, w, h}, belka, klient_h, pasek_y, pasek_h, widok_h, tresc_h, max_scroll, scrollbar};
}

