/*
 * Bursztyn OS - Przeglądarka Obrazów
 * Ring 3, paczka .cebula, otwieranie .bmp (24/32, BI_RGB)
 * Zgodna z bursztyn_gui.h, BWS, PZB, loaderem
 */
#include "../../bursztyn_gui.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>

/* =========================================================================
 * 1. NAGŁÓWEK .BUR (60 bajtów, zgodny z RING3_STD_LINKER)
 * ========================================================================= */
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

static_assert(sizeof(NaglowekBur) == 60U, "Naglowek .bur musi miec 60 bajtow");
static_assert(offsetof(NaglowekBur, punkt_wejscia) == 0x04U, "layout");
static_assert(offsetof(NaglowekBur, dane_wirtualny) == 0x34U, "layout");

extern "C" __attribute__((noreturn)) void _start();

extern "C" {
__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B','U','R','\0'},
    reinterpret_cast<uint64_t>(&_start),
    UINT64_C(0x1000), UINT64_C(0x8000), UINT64_C(0x601000),
    UINT64_C(0x9000), UINT64_C(0x20000), UINT64_C(0x609000)
};
}

/* =========================================================================
 * 2. STAŁE I LIMITY
 * ========================================================================= */
namespace {
constexpr int TASKBAR_H = 40;
constexpr int WIN_X0 = 80;
constexpr int WIN_Y0 = 60;
constexpr int WIN_W0 = 740;
constexpr int WIN_H0 = 500;
constexpr int WIN_MIN_W = 360;
constexpr int WIN_MIN_H = 260;

constexpr uint32_t MAX_W = 8192;
constexpr uint32_t MAX_H = 8192;
constexpr uint64_t MAX_PIXELS = 8192ULL * 8192ULL;
constexpr uint64_t MAX_FILE_SAFE = 1024ULL * 1024ULL * 1024ULL; // do 1 GiB

// Wymiary kafelka renderera
constexpr int TILE_W = 512;
constexpr int TILE_H = 256;
constexpr size_t TILE_PIXELS = static_cast<size_t>(TILE_W) * static_cast<size_t>(TILE_H);
constexpr size_t MAX_SCANLINE_BYTES = MAX_W * 4U;

// kolory
constexpr uint32_t KOLOR_TLO = 0x00101010U;
constexpr uint32_t KOLOR_STATUS_BG = 0x00201500U;
constexpr uint32_t KOLOR_STATUS_TXT = 0x00FFFFFFU;
constexpr uint32_t KOLOR_ERROR = 0x00FF5555U;

// techniczny czarny - widoczna czerń zamiast 0x00000000 (klucz przezroczystości)
constexpr uint32_t TECHNICZNY_CZARNY = 0x01000000U;

enum class TrybSkalowania : uint8_t {
    DOPASUJ = 0,
    JEDEN_DO_JEDNEGO = 1
};

/* =========================================================================
 * 3. POMOCNICZE
 * ========================================================================= */
inline uint16_t czyt_le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t czyt_le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t czyt_le32s(const uint8_t* p) {
    return static_cast<int32_t>(czyt_le32(p));
}

size_t dlugosc_txt(const char* s, size_t cap) {
    if (!s) return 0;
    size_t i = 0;
    while (i < cap && s[i]) ++i;
    return i;
}

bool kopiuj_txt(char* dst, size_t cap, const char* src) {
    if (!dst || !src || cap == 0) return false;
    size_t i = 0;
    while (i + 1 < cap && src[i]) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
    size_t ls = 0;
    while (src[ls]) ++ls;
    return ls < cap;
}

void dopisz_txt(char* dst, size_t cap, const char* src) {
    if (!dst || !src) return;
    size_t d = dlugosc_txt(dst, cap);
    size_t i = 0;
    while (d + 1 < cap && src[i]) {
        dst[d++] = src[i++];
    }
    dst[d] = '\0';
}

void dopisz_u32(char* dst, size_t cap, uint32_t v) {
    char tmp[12] = {0};
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        char rev[12];
        int r = 0;
        while (v) {
            rev[r++] = '0' + (v % 10);
            v /= 10;
        }
        while (r--) tmp[n++] = rev[r];
    }
    tmp[n] = '\0';
    dopisz_txt(dst, cap, tmp);
}

bool dodaj_u64(uint64_t a, uint64_t b, uint64_t* out) {
    if (!out) return false;
    if (a > UINT64_MAX - b) return false;
    *out = a + b;
    return true;
}

bool mnoz_u64(uint64_t a, uint64_t b, uint64_t* out) {
    if (!out) return false;
    if (a != 0 && b > UINT64_MAX / a) return false;
    *out = a * b;
    return true;
}

/* =========================================================================
 * 4. STAN GLOBALNY
 * ========================================================================= */
int win_x = WIN_X0;
int win_y = WIN_Y0;
int win_w = WIN_W0;
int win_h = WIN_H0;
int win_restore_x = WIN_X0;
int win_restore_y = WIN_Y0;
int win_restore_w = WIN_W0;
int win_restore_h = WIN_H0;
int screen_w = 1024;
int screen_h = 768;

int view_x = 0;
int view_y = 0;
bool zminimalizowane = false;
bool zmaksymalizowane = false;

bool dragging_window = false;
int drag_off_x = 0;
int drag_off_y = 0;

bool panning_image = false;
int pan_start_mx = 0;
int pan_start_my = 0;
int pan_start_vx = 0;
int pan_start_vy = 0;

char aktualna_sciezka[512] = {0};
char status_txt[256] = {0};
char blad_txt[128] = {0};

uint64_t plik_rozmiar = 0;
uint32_t img_w = 0;
uint32_t img_h = 0;
uint32_t img_bpp = 0;
uint64_t img_offset = 0;
uint64_t img_stride = 0;
bool img_top_down = false;
bool img_valid = false;

TrybSkalowania tryb = TrybSkalowania::DOPASUJ;

// Bufor kafelka i bufor pojedynczego wiersza BMP (stały rozmiar pamięci ~544 KiB, alokowany raz na starcie)
uint32_t* kafel_buf = nullptr;
uint8_t* wiersz_buf = nullptr;
int32_t buforowany_wiersz_y = -1;

void ustaw_blad(const char* t) {
    if (!t) {
        blad_txt[0] = '\0';
        return;
    }
    kopiuj_txt(blad_txt, sizeof(blad_txt), t);
}

void ogranicz_geometrie() {
    if (screen_w < 1) screen_w = 1;
    if (screen_h < 1) screen_h = 1;
    int max_w = screen_w;
    int max_h = screen_h - TASKBAR_H;
    if (max_h < 1) max_h = screen_h;
    if (win_w < WIN_MIN_W) win_w = WIN_MIN_W;
    if (win_h < WIN_MIN_H) win_h = WIN_MIN_H;
    if (win_w > max_w) win_w = max_w;
    if (win_h > max_h) win_h = max_h;
    if (win_x < 0) win_x = 0;
    if (win_y < 0) win_y = 0;
    if (win_x + win_w > max_w) win_x = max_w - win_w;
    if (win_y + win_h > max_h) win_y = max_h - win_h;
}

bool utworz_warstwe() {
    ogranicz_geometrie();
    int id = bws_utworz_warstwe(win_x, win_y, win_w, win_h, 10);
    return id >= 0;
}

void zwolnij_bmp() {
    img_w = 0;
    img_h = 0;
    img_bpp = 0;
    img_offset = 0;
    img_stride = 0;
    img_top_down = false;
    img_valid = false;
    buforowany_wiersz_y = -1;
    plik_rozmiar = 0;
}

/* =========================================================================
 * 5. STRUMIENIOWY PARSER BMP (tylko 256 bajtów nagłówka)
 * ========================================================================= */
const char* parsuj_naglowek_bmp(const uint8_t* dane, uint32_t hdr_len, uint64_t fsize) {
    if (!dane || hdr_len < 54) {
        return "Uszkodzony plik BMP";
    }

    if (dane[0] != 'B' || dane[1] != 'M') {
        return "Nieobsługiwany format BMP";
    }

    uint32_t offset = czyt_le32(dane + 10);
    uint32_t dib_size = czyt_le32(dane + 14);

    if (dib_size < 40) {
        return "Nieobsługiwany format BMP";
    }

    if (offset < 54 || static_cast<uint64_t>(offset) >= fsize) {
        return "Uszkodzony plik BMP";
    }

    int32_t w = czyt_le32s(dane + 18);
    int32_t h_raw = czyt_le32s(dane + 22);
    uint16_t planes = czyt_le16(dane + 26);
    uint16_t bpp = czyt_le16(dane + 28);
    uint32_t comp = czyt_le32(dane + 30);

    if (w <= 0 || h_raw == 0 || h_raw == INT32_MIN) {
        return "Uszkodzony plik BMP";
    }

    if (planes != 1 || (bpp != 24 && bpp != 32) || comp != 0) {
        return "Nieobsługiwany format BMP";
    }

    uint32_t abs_h = (h_raw < 0) ? static_cast<uint32_t>(-h_raw) : static_cast<uint32_t>(h_raw);

    if (static_cast<uint32_t>(w) > MAX_W || abs_h > MAX_H) {
        return "Obraz ma zbyt dużą rozdzielczość";
    }

    uint64_t pixels = 0;
    if (!mnoz_u64(static_cast<uint64_t>(static_cast<uint32_t>(w)), static_cast<uint64_t>(abs_h), &pixels) ||
        pixels > MAX_PIXELS) {
        return "Obraz ma zbyt dużą rozdzielczość";
    }

    uint64_t bits_per_row = 0;
    if (!mnoz_u64(static_cast<uint64_t>(static_cast<uint32_t>(w)), static_cast<uint64_t>(bpp), &bits_per_row)) {
        return "Uszkodzony plik BMP";
    }

    uint64_t stride = ((bits_per_row + 31ULL) / 32ULL) * 4ULL;
    uint64_t data_size = 0;
    if (!mnoz_u64(stride, static_cast<uint64_t>(abs_h), &data_size)) {
        return "Uszkodzony plik BMP";
    }

    uint64_t koniec = 0;
    if (!dodaj_u64(static_cast<uint64_t>(offset), data_size, &koniec) || koniec > fsize) {
        return "Uszkodzony plik BMP";
    }

    img_w = static_cast<uint32_t>(w);
    img_h = abs_h;
    img_bpp = bpp;
    img_offset = static_cast<uint64_t>(offset);
    img_stride = stride;
    img_top_down = (h_raw < 0);
    img_valid = true;
    buforowany_wiersz_y = -1;

    return nullptr;
}

void aktualizuj_pasek_statusu() {
    if (!img_valid) return;
    char s[256] = {0};
    const char* nazwa = aktualna_sciezka;
    const char* q = aktualna_sciezka;
    while (*q) {
        if (*q == '/') nazwa = q + 1;
        ++q;
    }
    kopiuj_txt(s, sizeof(s), nazwa);
    dopisz_txt(s, sizeof(s), " | ");
    dopisz_u32(s, sizeof(s), img_w);
    dopisz_txt(s, sizeof(s), "x");
    dopisz_u32(s, sizeof(s), img_h);
    dopisz_txt(s, sizeof(s), " | ");
    dopisz_u32(s, sizeof(s), img_bpp);
    dopisz_txt(s, sizeof(s), "-bit | ");

    if (tryb == TrybSkalowania::JEDEN_DO_JEDNEGO) {
        dopisz_txt(s, sizeof(s), "1:1 (X:");
        dopisz_u32(s, sizeof(s), static_cast<uint32_t>(view_x));
        dopisz_txt(s, sizeof(s), ", Y:");
        dopisz_u32(s, sizeof(s), static_cast<uint32_t>(view_y));
        dopisz_txt(s, sizeof(s), ")");
    } else {
        dopisz_txt(s, sizeof(s), "Dopasuj");
    }

    uint32_t mb = static_cast<uint32_t>(plik_rozmiar / (1024ULL * 1024ULL));
    if (mb > 0) {
        dopisz_txt(s, sizeof(s), " | ");
        dopisz_u32(s, sizeof(s), mb);
        dopisz_txt(s, sizeof(s), " MB");
    }

    kopiuj_txt(status_txt, sizeof(status_txt), s);
}

/* =========================================================================
 * 6. OTWIERANIE PLIKU
 * ========================================================================= */
bool wczytaj_obraz(const char* sciezka) {
    zwolnij_bmp();
    blad_txt[0] = '\0';
    status_txt[0] = '\0';
    view_x = 0;
    view_y = 0;

    if (!sciezka || sciezka[0] != '/') {
        ustaw_blad("Nie można odczytać pliku");
        return false;
    }

    kopiuj_txt(aktualna_sciezka, sizeof(aktualna_sciezka), sciezka);

    uint32_t rsz = 0;
    if (!pobierz_rozmiar_pliku(sciezka, &rsz)) {
        ustaw_blad("Nie można odczytać pliku");
        return false;
    }

    if (rsz == 0) {
        ustaw_blad("Uszkodzony plik BMP");
        return false;
    }

    plik_rozmiar = static_cast<uint64_t>(rsz);

    if (plik_rozmiar > MAX_FILE_SAFE) {
        ustaw_blad("Plik przekracza bezpieczny limit");
        return false;
    }

    uint8_t hdr[256] = {};
    uint32_t to_read = (rsz < 256U) ? rsz : 256U;
    uint32_t got = 0;

    if (!czytaj_fragment_pliku(sciezka, 0, reinterpret_cast<char*>(hdr), to_read, &got) || got < to_read) {
        ustaw_blad("Nie można odczytać pliku");
        return false;
    }

    const char* blad = parsuj_naglowek_bmp(hdr, to_read, plik_rozmiar);
    if (blad) {
        ustaw_blad(blad);
        return false;
    }

    tryb = TrybSkalowania::DOPASUJ;
    aktualizuj_pasek_statusu();

    char tyt[96] = {};
    kopiuj_txt(tyt, sizeof(tyt), "Przeglądarka Obrazów");
    dopisz_txt(tyt, sizeof(tyt), " - ");
    const char* n = sciezka;
    const char* q = sciezka;
    while (*q) {
        if (*q == '/') n = q + 1;
        ++q;
    }
    dopisz_txt(tyt, sizeof(tyt), n);
    (void)gui_ustaw_tytul_okna(tyt);

    return true;
}

/* =========================================================================
 * 7. STRUMIENIOWY RENDERER KAFELKOWY
 * ========================================================================= */
const uint8_t* wczytaj_wiersz_bmp(uint32_t y) {
    if (!img_valid || y >= img_h) {
        return wiersz_buf;
    }

    if (buforowany_wiersz_y == static_cast<int32_t>(y)) {
        return wiersz_buf;
    }

    uint32_t file_row = img_top_down ? y : (img_h - 1U - y);
    uint64_t file_offset = img_offset + static_cast<uint64_t>(file_row) * img_stride;
    uint32_t to_read = static_cast<uint32_t>(img_stride);
    if (to_read > MAX_SCANLINE_BYTES) to_read = static_cast<uint32_t>(MAX_SCANLINE_BYTES);

    uint32_t got = 0;
    if (!czytaj_fragment_pliku(aktualna_sciezka, file_offset, reinterpret_cast<char*>(wiersz_buf), to_read, &got) ||
        got < to_read) {
        for (size_t i = 0; i < to_read; ++i) wiersz_buf[i] = 0;
    }

    buforowany_wiersz_y = static_cast<int32_t>(y);
    return wiersz_buf;
}

inline uint32_t pobierz_piksel_z_wiersza(const uint8_t* row, uint32_t x) {
    if (!row || x >= img_w) return TECHNICZNY_CZARNY;

    uint32_t bytes_per_pixel = img_bpp / 8U;
    uint64_t offset = static_cast<uint64_t>(x) * static_cast<uint64_t>(bytes_per_pixel);
    if (offset + 3ULL > img_stride) return TECHNICZNY_CZARNY;

    uint8_t b = row[offset];
    uint8_t g = row[offset + 1U];
    uint8_t r = row[offset + 2U];

    uint32_t color = (static_cast<uint32_t>(r) << 16) |
                     (static_cast<uint32_t>(g) << 8) |
                     static_cast<uint32_t>(b);

    if (color == 0x00000000U) {
        color = TECHNICZNY_CZARNY;
    }

    return color;
}

void rysuj_obraz() {
    if (zminimalizowane) return;

    const int BELKA_H = 28;
    const int STATUS_H = 22;
    const int MARGIN = 2;

    int ax = win_x + MARGIN;
    int ay = win_y + BELKA_H + MARGIN;
    int aw = win_w - 2 * MARGIN;
    int ah = win_h - BELKA_H - STATUS_H - 2 * MARGIN;

    if (aw <= 0 || ah <= 0) return;

    gui_rysuj_prostokat(ax, ay, aw, ah, KOLOR_TLO);

    if (!img_valid) {
        const char* msg = blad_txt[0] ? blad_txt : "Brak obrazu - otwórz plik BMP";
        int tw = oblicz_szerokosc_tekstu(msg, 1);
        int tx = ax + (aw - tw) / 2;
        if (tx < ax) tx = ax;
        int ty = ay + ah / 2 - 8;
        gui_wypisz_tekst_kolor(tx, ty, KOLOR_ERROR, msg);
        return;
    }

    if (tryb == TrybSkalowania::DOPASUJ) {
        uint64_t scaleW = (static_cast<uint64_t>(aw) * 65536ULL) / static_cast<uint64_t>(img_w);
        uint64_t scaleH = (static_cast<uint64_t>(ah) * 65536ULL) / static_cast<uint64_t>(img_h);
        uint64_t scale = (scaleW < scaleH) ? scaleW : scaleH;
        if (scale == 0) scale = 1;

        int dstW = static_cast<int>((static_cast<uint64_t>(img_w) * scale) / 65536ULL);
        int dstH = static_cast<int>((static_cast<uint64_t>(img_h) * scale) / 65536ULL);
        if (dstW <= 0) dstW = 1;
        if (dstH <= 0) dstH = 1;
        if (dstW > aw) dstW = aw;
        if (dstH > ah) dstH = ah;

        int offX = ax + (aw - dstW) / 2;
        int offY = ay + (ah - dstH) / 2;

        for (int ty = 0; ty < dstH; ty += TILE_H) {
            int cur_th = (ty + TILE_H <= dstH) ? TILE_H : (dstH - ty);
            for (int tx = 0; tx < dstW; tx += TILE_W) {
                int cur_tw = (tx + TILE_W <= dstW) ? TILE_W : (dstW - tx);

                for (int dy = 0; dy < cur_th; ++dy) {
                    uint32_t srcY = static_cast<uint32_t>((static_cast<uint64_t>(ty + dy) * static_cast<uint64_t>(img_h)) / static_cast<uint64_t>(dstH));
                    if (srcY >= img_h) srcY = img_h - 1U;
                    const uint8_t* row = wczytaj_wiersz_bmp(srcY);

                    for (int dx = 0; dx < cur_tw; ++dx) {
                        uint32_t srcX = static_cast<uint32_t>((static_cast<uint64_t>(tx + dx) * static_cast<uint64_t>(img_w)) / static_cast<uint64_t>(dstW));
                        if (srcX >= img_w) srcX = img_w - 1U;
                        kafel_buf[dy * cur_tw + dx] = pobierz_piksel_z_wiersza(row, srcX);
                    }
                }

                (void)gui_blit(offX + tx, offY + ty, cur_tw, cur_th, kafel_buf);
            }
        }
    } else {
        // Tryb 1:1
        int visW = aw;
        int visH = ah;
        int maxX = (static_cast<int>(img_w) > visW) ? (static_cast<int>(img_w) - visW) : 0;
        int maxY = (static_cast<int>(img_h) > visH) ? (static_cast<int>(img_h) - visH) : 0;

        if (view_x < 0) view_x = 0;
        if (view_y < 0) view_y = 0;
        if (view_x > maxX) view_x = maxX;
        if (view_y > maxY) view_y = maxY;

        int dstW = (static_cast<int>(img_w) - view_x < visW) ? (static_cast<int>(img_w) - view_x) : visW;
        int dstH = (static_cast<int>(img_h) - view_y < visH) ? (static_cast<int>(img_h) - view_y) : visH;
        if (dstW <= 0 || dstH <= 0) return;

        int offX = ax;
        int offY = ay;
        if (static_cast<int>(img_w) < visW) offX = ax + (visW - dstW) / 2;
        if (static_cast<int>(img_h) < visH) offY = ay + (visH - dstH) / 2;

        for (int ty = 0; ty < dstH; ty += TILE_H) {
            int cur_th = (ty + TILE_H <= dstH) ? TILE_H : (dstH - ty);
            for (int tx = 0; tx < dstW; tx += TILE_W) {
                int cur_tw = (tx + TILE_W <= dstW) ? TILE_W : (dstW - tx);

                for (int dy = 0; dy < cur_th; ++dy) {
                    uint32_t srcY = static_cast<uint32_t>(view_y + ty + dy);
                    if (srcY >= img_h) srcY = img_h - 1U;
                    const uint8_t* row = wczytaj_wiersz_bmp(srcY);

                    for (int dx = 0; dx < cur_tw; ++dx) {
                        uint32_t srcX = static_cast<uint32_t>(view_x + tx + dx);
                        if (srcX >= img_w) srcX = img_w - 1U;
                        kafel_buf[dy * cur_tw + dx] = pobierz_piksel_z_wiersza(row, srcX);
                    }
                }

                (void)gui_blit(offX + tx, offY + ty, cur_tw, cur_th, kafel_buf);
            }
        }
    }
}

void rysuj_interfejs(bool pelne) {
    if (zminimalizowane) return;
    if (pelne) gui_odswiez_pulpit();

    char tytul[96] = {};
    kopiuj_txt(tytul, sizeof(tytul), "Przeglądarka Obrazów");
    if (aktualna_sciezka[0]) {
        dopisz_txt(tytul, sizeof(tytul), " - ");
        const char* n = aktualna_sciezka;
        const char* p = n;
        while (*p) {
            if (*p == '/') n = p + 1;
            ++p;
        }
        dopisz_txt(tytul, sizeof(tytul), n);
    }

    gui_rysuj_prostokat(win_x + 1, win_y + 1, win_w - 2, win_h - 2, KOLOR_TLO);
    gui_rysuj_standardowa_belke(win_x, win_y, win_w, tytul, zmaksymalizowane);
    rysuj_obraz();

    const int STATUS_H = 22;
    int sx = win_x + 1;
    int sy = win_y + win_h - STATUS_H - 1;
    int sw = win_w - 2;

    if (sw > 0) {
        gui_rysuj_prostokat(sx, sy, sw, STATUS_H, KOLOR_STATUS_BG);
        if (status_txt[0]) {
            gui_wypisz_tekst_kolor(sx + 6, sy + 6, KOLOR_STATUS_TXT, status_txt);
        } else if (blad_txt[0]) {
            gui_wypisz_tekst_kolor(sx + 6, sy + 6, KOLOR_ERROR, blad_txt);
        } else {
            gui_wypisz_tekst_kolor(sx + 6, sy + 6, KOLOR_STATUS_TXT, "1: Tryb 1:1 | D: Dopasuj | Strzałki / Mysz: Przesuwanie");
        }
    }

    gui_odswiez();
}

void przesun_widok(int dx, int dy) {
    if (tryb != TrybSkalowania::JEDEN_DO_JEDNEGO || !img_valid) return;
    const int BELKA_H = 28;
    const int STATUS_H = 22;
    const int MARGIN = 2;
    int aw = win_w - 2 * MARGIN;
    int ah = win_h - BELKA_H - STATUS_H - 2 * MARGIN;

    int maxX = (static_cast<int>(img_w) > aw) ? (static_cast<int>(img_w) - aw) : 0;
    int maxY = (static_cast<int>(img_h) > ah) ? (static_cast<int>(img_h) - ah) : 0;

    view_x += dx;
    view_y += dy;

    if (view_x < 0) view_x = 0;
    if (view_y < 0) view_y = 0;
    if (view_x > maxX) view_x = maxX;
    if (view_y > maxY) view_y = maxY;

    aktualizuj_pasek_statusu();
}

} // namespace

/* =========================================================================
 * 8. PĘTLA GŁÓWNA
 * ========================================================================= */
extern "C" __attribute__((noreturn)) void _start() {
    gui_pobierz_rozdzielczosc(&screen_w, &screen_h);
    if (screen_w <= 0 || screen_h <= 0) gui_zakoncz_aplikacje();

    kafel_buf = static_cast<uint32_t*>(gui_malloc(sizeof(uint32_t) * TILE_PIXELS));
    wiersz_buf = static_cast<uint8_t*>(gui_malloc(MAX_SCANLINE_BYTES));
    if (!kafel_buf || !wiersz_buf) {
        if (kafel_buf) gui_free(kafel_buf);
        if (wiersz_buf) gui_free(wiersz_buf);
        gui_zakoncz_aplikacje();
    }

    ogranicz_geometrie();
    if (!utworz_warstwe()) {
        gui_free(kafel_buf);
        gui_free(wiersz_buf);
        gui_zakoncz_aplikacje();
    }
    (void)gui_ustaw_tytul_okna("Przeglądarka Obrazów");
    gui_ustaw_przejecie_myszy(true);

    char arg[512] = {};
    if (pobierz_argument_startowy(arg, sizeof(arg))) {
        wczytaj_obraz(arg);
    }

    bool wyjdz = false;
    bool redraw = true;
    bool pelne = true;

    // Bufor sekwencji ANSI dla klawiszy funkcyjnych
    char ansi_seq[8] = {};
    int ansi_idx = 0;

    while (!wyjdz) {
        bws_zdarzenie ev{};
        if (!gui_czekaj_na_zdarzenie(&ev)) continue;

        if (ev.typ == BWS_ZDARZENIE_OTWORZ_PLIK) {
            char sciezka[512] = {};
            if (pobierz_argument_startowy(sciezka, sizeof(sciezka))) {
                wczytaj_obraz(sciezka);
                redraw = true;
                pelne = false;
            }
        }

        if (ev.typ == BWS_ZDARZENIE_FOCUS && zminimalizowane) {
            zminimalizowane = false;
        }

        int mx = -1, my = -1;
        uint8_t mb = 0;
        if (ev.typ == BWS_ZDARZENIE_MYSZ_RUCH ||
            ev.typ == BWS_ZDARZENIE_MYSZ_DOWN ||
            ev.typ == BWS_ZDARZENIE_MYSZ_UP) {
            mx = ev.x;
            my = ev.y;
            mb = static_cast<uint8_t>(ev.przyciski);
        }

        bool lewy = (mb & 0x01U) != 0;
        bool klik = (ev.typ == BWS_ZDARZENIE_MYSZ_DOWN);
        bool puszczenie = (ev.typ == BWS_ZDARZENIE_MYSZ_UP);

        const int BELKA_H = 28;
        const int STATUS_H = 22;
        const int MARGIN = 2;
        int ax = win_x + MARGIN;
        int ay = win_y + BELKA_H + MARGIN;
        int aw = win_w - 2 * MARGIN;
        int ah = win_h - BELKA_H - STATUS_H - 2 * MARGIN;

        if (klik && !zminimalizowane) {
            bool wOknie = (mx >= win_x && mx < win_x + win_w && my >= win_y && my < win_y + win_h);
            if (wOknie) {
                gui_akcja_belki akcja = gui_hit_test_belki(mx, my, win_x, win_y, win_w);
                if (akcja == GUI_BELKA_ZAMKNIJ) {
                    wyjdz = true;
                    dragging_window = false;
                } else if (akcja == GUI_BELKA_MINIMALIZUJ) {
                    zminimalizowane = gui_minimalizuj_okno();
                    dragging_window = false;
                    panning_image = false;
                    gui_ustaw_capture_myszy(false);
                } else if (akcja == GUI_BELKA_MAKSYMALIZUJ) {
                    if (!zmaksymalizowane) {
                        win_restore_x = win_x;
                        win_restore_y = win_y;
                        win_restore_w = win_w;
                        win_restore_h = win_h;
                        int nW = screen_w;
                        int nH = screen_h - TASKBAR_H;
                        if (nW < WIN_MIN_W) nW = WIN_MIN_W;
                        if (nH < WIN_MIN_H) nH = WIN_MIN_H;
                        win_x = 0;
                        win_y = 0;
                        win_w = nW;
                        win_h = nH;
                        if (bws_utworz_warstwe(win_x, win_y, win_w, win_h, 10) < 0) {
                            win_x = win_restore_x;
                            win_y = win_restore_y;
                            win_w = win_restore_w;
                            win_h = win_restore_h;
                        } else {
                            zmaksymalizowane = true;
                            redraw = true;
                            pelne = false;
                        }
                    } else {
                        win_x = win_restore_x;
                        win_y = win_restore_y;
                        win_w = win_restore_w;
                        win_h = win_restore_h;
                        if (bws_utworz_warstwe(win_x, win_y, win_w, win_h, 10) >= 0) {
                            zmaksymalizowane = false;
                            redraw = true;
                            pelne = false;
                        }
                    }
                } else if (akcja == GUI_BELKA_DRAG && !zmaksymalizowane) {
                    dragging_window = true;
                    gui_ustaw_capture_myszy(true);
                    drag_off_x = mx - win_x;
                    drag_off_y = my - win_y;
                } else if (mx >= ax && mx < ax + aw && my >= ay && my < ay + ah) {
                    if (tryb == TrybSkalowania::JEDEN_DO_JEDNEGO && img_valid) {
                        panning_image = true;
                        pan_start_mx = mx;
                        pan_start_my = my;
                        pan_start_vx = view_x;
                        pan_start_vy = view_y;
                        gui_ustaw_capture_myszy(true);
                    }
                }
            }
        }

        if (dragging_window && lewy) {
            int nx = mx - drag_off_x;
            int ny = my - drag_off_y;
            int maxX = screen_w - win_w;
            int maxY = screen_h - TASKBAR_H - win_h;
            if (maxX < 0) maxX = 0;
            if (maxY < 0) maxY = 0;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            if (nx > maxX) nx = maxX;
            if (ny > maxY) ny = maxY;
            if (nx != win_x || ny != win_y) {
                win_x = nx;
                win_y = ny;
                bws_przesun_warstwe(win_x, win_y);
                gui_odswiez();
            }
        }

        if (panning_image && lewy && tryb == TrybSkalowania::JEDEN_DO_JEDNEGO) {
            int dx = mx - pan_start_mx;
            int dy = my - pan_start_my;
            int target_vx = pan_start_vx - dx;
            int target_vy = pan_start_vy - dy;

            int maxX = (static_cast<int>(img_w) > aw) ? (static_cast<int>(img_w) - aw) : 0;
            int maxY = (static_cast<int>(img_h) > ah) ? (static_cast<int>(img_h) - ah) : 0;

            if (target_vx < 0) target_vx = 0;
            if (target_vy < 0) target_vy = 0;
            if (target_vx > maxX) target_vx = maxX;
            if (target_vy > maxY) target_vy = maxY;

            if (target_vx != view_x || target_vy != view_y) {
                view_x = target_vx;
                view_y = target_vy;
                aktualizuj_pasek_statusu();
                redraw = true;
                pelne = false;
            }
        }

        if (puszczenie) {
            if (dragging_window || panning_image) {
                dragging_window = false;
                panning_image = false;
                gui_ustaw_capture_myszy(false);
            }
        }

        // Obsługa klawiatury
        if (ev.typ == BWS_ZDARZENIE_KLAWISZ && !zminimalizowane) {
            char c = static_cast<char>(ev.kod);

            if (ansi_idx > 0) {
                if (ansi_idx < static_cast<int>(sizeof(ansi_seq)) - 1) {
                    ansi_seq[ansi_idx++] = c;
                    ansi_seq[ansi_idx] = '\0';
                }

                if (ansi_idx == 3 && ansi_seq[1] == '[') {
                    if (ansi_seq[2] == 'A') { // Up
                        przesun_widok(0, -64);
                        redraw = true; pelne = false;
                        ansi_idx = 0;
                    } else if (ansi_seq[2] == 'B') { // Down
                        przesun_widok(0, 64);
                        redraw = true; pelne = false;
                        ansi_idx = 0;
                    } else if (ansi_seq[2] == 'C') { // Right
                        przesun_widok(64, 0);
                        redraw = true; pelne = false;
                        ansi_idx = 0;
                    } else if (ansi_seq[2] == 'D') { // Left
                        przesun_widok(-64, 0);
                        redraw = true; pelne = false;
                        ansi_idx = 0;
                    } else if (ansi_seq[2] == 'H') { // Home
                        view_x = 0; view_y = 0;
                        aktualizuj_pasek_statusu();
                        redraw = true; pelne = false;
                        ansi_idx = 0;
                    } else if (ansi_seq[2] == 'F') { // End
                        int maxX = (static_cast<int>(img_w) > aw) ? (static_cast<int>(img_w) - aw) : 0;
                        int maxY = (static_cast<int>(img_h) > ah) ? (static_cast<int>(img_h) - ah) : 0;
                        view_x = maxX; view_y = maxY;
                        aktualizuj_pasek_statusu();
                        redraw = true; pelne = false;
                        ansi_idx = 0;
                    }
                } else if (ansi_idx == 4 && ansi_seq[1] == '[' && ansi_seq[3] == '~') {
                    if (ansi_seq[2] == '5') { // PageUp
                        przesun_widok(0, -ah / 2);
                        redraw = true; pelne = false;
                        ansi_idx = 0;
                    } else if (ansi_seq[2] == '6') { // PageDown
                        przesun_widok(0, ah / 2);
                        redraw = true; pelne = false;
                        ansi_idx = 0;
                    } else {
                        ansi_idx = 0;
                    }
                } else if (ansi_idx >= 4) {
                    ansi_idx = 0;
                }
            } else if (c == '\x1B') {
                ansi_seq[0] = '\x1B';
                ansi_idx = 1;
            } else if (c == '1') {
                tryb = TrybSkalowania::JEDEN_DO_JEDNEGO;
                aktualizuj_pasek_statusu();
                redraw = true;
                pelne = false;
            } else if (c == 'd' || c == 'D') {
                tryb = TrybSkalowania::DOPASUJ;
                aktualizuj_pasek_statusu();
                redraw = true;
                pelne = false;
            } else if (c == 'w' || c == 'W') {
                przesun_widok(0, -64);
                redraw = true;
                pelne = false;
            } else if (c == 's' || c == 'S') {
                przesun_widok(0, 64);
                redraw = true;
                pelne = false;
            } else if (c == 'a' || c == 'A') {
                przesun_widok(-64, 0);
                redraw = true;
                pelne = false;
            } else if (c == 'd' || c == 'D') {
                przesun_widok(64, 0);
                redraw = true;
                pelne = false;
            }
        }

        if (redraw && !wyjdz) {
            rysuj_interfejs(pelne);
            redraw = false;
            pelne = false;
        }
    }

    zwolnij_bmp();
    if (kafel_buf) gui_free(kafel_buf);
    if (wiersz_buf) gui_free(wiersz_buf);
    gui_zakoncz_aplikacje();
}
