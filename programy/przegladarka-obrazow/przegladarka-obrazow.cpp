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
    UINT64_C(0x1000), UINT64_C(0x8000), UINT64_C(0x1001000),
    UINT64_C(0x9000), UINT64_C(0x20000), UINT64_C(0x1009000)
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
constexpr int BELKA_H = 28;
constexpr int STATUS_H = 22;
constexpr int IMAGE_MARGIN = 2;

constexpr uint32_t MAX_W = 8192;
constexpr uint32_t MAX_H = 8192;
constexpr uint64_t MAX_PIXELS = 8192ULL * 8192ULL;
constexpr uint64_t MAX_FILE_SAFE = 1024ULL * 1024ULL * 1024ULL; // do 1 GiB

// Kafelek jest uzywany tylko jako awaryjny bufor dla ekranow poza limitem BWS57.
constexpr int TILE_W = 512;
constexpr int TILE_H = 256;
constexpr size_t TILE_PIXELS = static_cast<size_t>(TILE_W) * static_cast<size_t>(TILE_H);
constexpr size_t MAX_SCANLINE_BYTES = MAX_W * 4U;
constexpr uint32_t ROW_CACHE_ROWS = 32U;
constexpr uint32_t MAX_ROW_CACHE_SLOTS = 48U;
constexpr uint64_t ROW_CACHE_BYTES =
    static_cast<uint64_t>(MAX_SCANLINE_BYTES) * ROW_CACHE_ROWS; // 1 MiB na slot
constexpr uint64_t BWS57_MAX_PIXELS = 4ULL * 1024ULL * 1024ULL;
constexpr int BWS57_MAX_DIM = 2048;
constexpr uint64_t FRAME_NS = 16666667ULL;
constexpr size_t EVENT_BATCH = 64U;
constexpr uint32_t FIT_ROW_CACHE_ROWS = 1U;
constexpr uint32_t ONE_TO_ONE_STEP_ROWS = 16U;
constexpr uint32_t ONE_TO_ONE_BLIT_ROWS = 96U;
constexpr int MAX_DIRTY_RECTS = 3;
constexpr int BUTTON_H = 18;
constexpr int BUTTON_GAP = 4;
constexpr int BUTTON_PAD_X = 10;

// kolory
constexpr uint32_t KOLOR_TLO = 0x00101010U;
constexpr uint32_t KOLOR_STATUS_BG = 0x00201500U;
constexpr uint32_t KOLOR_STATUS_TXT = 0x00FFFFFFU;
constexpr uint32_t KOLOR_ERROR = 0x00FF5555U;
constexpr uint32_t KOLOR_PRZYCISK = 0x00352A16U;
constexpr uint32_t KOLOR_PRZYCISK_HOVER = 0x00483A20U;
constexpr uint32_t KOLOR_PRZYCISK_AKTYWNY = 0x00A86D12U;
constexpr uint32_t KOLOR_PRZYCISK_RAMKA = 0x00D8A43BU;

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

void dopisz_u64(char* dst, size_t cap, uint64_t v) {
    char tmp[24] = {};
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        char rev[24] = {};
        int r = 0;
        while (v && r < static_cast<int>(sizeof(rev))) {
            rev[r++] = static_cast<char>('0' + (v % 10ULL));
            v /= 10ULL;
        }
        while (r > 0) tmp[n++] = rev[--r];
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

enum class Kontrolka : uint8_t { BRAK, JEDEN_DO_JEDNEGO, DOPASUJ };
Kontrolka kontrolka_hover = Kontrolka::BRAK;
Kontrolka kontrolka_wcisnieta = Kontrolka::BRAK;

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

struct ProfilRedraw {
    uint64_t range_calls;
    uint64_t range_bytes;
    uint64_t range_got;
    uint64_t blit_calls;
    uint64_t allocations;
    uint64_t hpet_ns;
};

struct CacheViewportu {
    bool valid;
    TrybSkalowania mode;
    int area_w;
    int area_h;
    int image_x;
    int image_y;
    int image_w;
    int image_h;
    int view_x;
    int view_y;
};

struct CacheDopasowania {
    bool valid;
    uint64_t generation;
    int area_w;
    int area_h;
};

struct ProstokatObrazu {
    int x;
    int y;
    int w;
    int h;
};

struct Render1do1 {
    bool active;
    uint64_t generation;
    CacheViewportu target;
    ProstokatObrazu dirty[MAX_DIRTY_RECTS];
    int dirty_count;
    int dirty_index;
    int next_row;
    int presented_row;
};

struct GeometriaKontrolek {
    ProstokatObrazu bar;
    ProstokatObrazu jeden_do_jednego;
    ProstokatObrazu dopasuj;
};

struct SlotCacheWierszy {
    uint8_t* data;
    uint32_t first;
    uint32_t count;
    uint64_t stamp;
    bool valid;
};

// Wszystkie bufory sa alokowane raz przed petla zdarzen (zero alokacji na sciezce krytycznej).
uint32_t* kafel_buf = nullptr;
uint32_t* viewport_buf = nullptr;
uint32_t* fit_buf = nullptr;
size_t viewport_capacity = 0;
SlotCacheWierszy row_cache_slots[MAX_ROW_CACHE_SLOTS] = {};
uint32_t row_cache_slot_count = 0;
uint64_t row_cache_clock = 0;
uint64_t image_generation = 0;
CacheViewportu viewport_cache{};
CacheDopasowania fit_cache{};
Render1do1 render_1do1{};
ProfilRedraw profil{};
bool profil_aktywny = false;
bool profil_gotowy = false;
uint64_t profil_start_ns = 0;
bws_zdarzenie event_buf[EVENT_BATCH] = {};

ProstokatObrazu pobierz_prostokat_obrazu() {
    ProstokatObrazu r{
        win_x + IMAGE_MARGIN,
        win_y + BELKA_H + IMAGE_MARGIN,
        win_w - 2 * IMAGE_MARGIN,
        win_h - BELKA_H - STATUS_H - 2 * IMAGE_MARGIN
    };
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
    return r;
}

void anuluj_render_1do1() {
    render_1do1 = Render1do1{};
}

void uniewaznij_cache_rozmiaru() {
    anuluj_render_1do1();
    profil_aktywny = false;
    profil_gotowy = false;
    profil_start_ns = 0;
    viewport_cache.valid = false;
    fit_cache.valid = false;
}

void* profilowany_malloc(unsigned long bytes) {
    if (profil_aktywny) ++profil.allocations;
    return gui_malloc(bytes);
}

void uniewaznij_cache_wierszy() {
    row_cache_clock = 0;
    for (uint32_t i = 0; i < row_cache_slot_count; ++i) {
        row_cache_slots[i].valid = false;
        row_cache_slots[i].first = 0;
        row_cache_slots[i].count = 0;
        row_cache_slots[i].stamp = 0;
    }
}

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
    uniewaznij_cache_wierszy();
    uniewaznij_cache_rozmiaru();
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
    uniewaznij_cache_wierszy();
    uniewaznij_cache_rozmiaru();

    return nullptr;
}

void aktualizuj_pasek_statusu() {
    if (!img_valid) return;
    char s[256] = {0};
    if (profil_gotowy) {
        kopiuj_txt(s, sizeof(s), "R:");
        dopisz_u64(s, sizeof(s), profil.range_calls);
        dopisz_txt(s, sizeof(s), "/");
        dopisz_u64(s, sizeof(s), profil.range_bytes);
        dopisz_txt(s, sizeof(s), "B B:");
        dopisz_u64(s, sizeof(s), profil.blit_calls);
        dopisz_txt(s, sizeof(s), " A:");
        dopisz_u64(s, sizeof(s), profil.allocations);
        dopisz_txt(s, sizeof(s), " T:");
        dopisz_u64(s, sizeof(s), profil.hpet_ns / 1000ULL);
        dopisz_txt(s, sizeof(s), "us | ");
    }
    const char* nazwa = aktualna_sciezka;
    const char* q = aktualna_sciezka;
    while (*q) {
        if (*q == '/') nazwa = q + 1;
        ++q;
    }
    dopisz_txt(s, sizeof(s), nazwa);
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

bool aktywuj_tryb(TrybSkalowania nowy, bool nowy_obraz = false);

/* =========================================================================
 * 6. OTWIERANIE PLIKU
 * ========================================================================= */
bool wczytaj_obraz(const char* sciezka) {
    zwolnij_bmp();
    blad_txt[0] = '\0';
    status_txt[0] = '\0';
    view_x = 0;
    view_y = 0;
    profil_gotowy = false;

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

    ++image_generation;
    if (image_generation == 0) ++image_generation;
    /* Otwieranie, D i przycisk przechodza przez aktywuj_tryb(). Dla nowego
       obrazu ta funkcja synchronicznie buduje kompletny cache FIT. */
    (void)aktywuj_tryb(TrybSkalowania::DOPASUJ, true);

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
 * 7. CACHE WIERSZY I BEZALOKACYJNY RENDERER VIEWPORTU
 * ========================================================================= */
void rozpocznij_profil() {
    profil = ProfilRedraw{};
    profil_gotowy = false;
    profil_start_ns = gui_czas_monotoniczny_ns();
    profil_aktywny = true;
}

void zakoncz_profil() {
    const uint64_t koniec = gui_czas_monotoniczny_ns();
    if (profil_start_ns != 0 && koniec >= profil_start_ns) {
        profil.hpet_ns = koniec - profil_start_ns;
    }
    profil_aktywny = false;
    profil_gotowy = true;
}

bool profilowany_odczyt(uint64_t offset, uint8_t* dst, uint32_t bytes,
                       uint32_t* got) {
    if (!dst || bytes == 0 || !got) return false;
    if (profil_aktywny) {
        ++profil.range_calls;
        profil.range_bytes += bytes;
    }
    *got = 0;
    const bool ok = czytaj_fragment_pliku(
        aktualna_sciezka, offset, reinterpret_cast<char*>(dst), bytes, got);
    if (profil_aktywny) profil.range_got += *got;
    return ok;
}

bool profilowany_blit(int x, int y, int w, int h, const uint32_t* pixels) {
    if (profil_aktywny) ++profil.blit_calls;
    return gui_blit(x, y, w, h, pixels);
}

const uint8_t* wskaz_wiersz_cache(const uint8_t* data, uint32_t first,
                                  uint32_t count, uint32_t y) {
    if (!data || count == 0 || y < first || y - first >= count) return nullptr;
    const uint32_t logical = y - first;
    const uint32_t slot = img_top_down ? logical : (count - 1U - logical);
    uint64_t byte_offset = 0;
    if (!mnoz_u64(static_cast<uint64_t>(slot), img_stride, &byte_offset) ||
        byte_offset >= ROW_CACHE_BYTES) return nullptr;
    return data + static_cast<size_t>(byte_offset);
}

bool zaladuj_cache_wierszy(uint8_t* data, uint32_t first, uint32_t count) {
    if (!data || count == 0 || first >= img_h || count > img_h - first) return false;
    uint64_t bytes64 = 0;
    if (!mnoz_u64(img_stride, static_cast<uint64_t>(count), &bytes64) ||
        bytes64 == 0 || bytes64 > ROW_CACHE_BYTES || bytes64 > UINT32_MAX)
        return false;

    const uint32_t first_file_row = img_top_down ? first : (img_h - first - count);
    uint64_t row_offset = 0;
    uint64_t file_offset = 0;
    uint64_t file_end = 0;
    if (!mnoz_u64(static_cast<uint64_t>(first_file_row), img_stride, &row_offset) ||
        !dodaj_u64(img_offset, row_offset, &file_offset) ||
        !dodaj_u64(file_offset, bytes64, &file_end) || file_end > plik_rozmiar)
        return false;

    const uint32_t bytes = static_cast<uint32_t>(bytes64);
    uint32_t got = 0;
    if (!profilowany_odczyt(file_offset, data, bytes, &got) || got < bytes) {
        for (uint32_t i = 0; i < bytes; ++i) data[i] = 0;
        return false;
    }
    return true;
}

SlotCacheWierszy* wybierz_slot_cache() {
    SlotCacheWierszy* victim = nullptr;
    for (uint32_t i = 0; i < row_cache_slot_count; ++i) {
        SlotCacheWierszy& slot = row_cache_slots[i];
        if (!victim || (!slot.valid && victim->valid) ||
            (slot.valid && victim->valid && slot.stamp < victim->stamp)) {
            victim = &slot;
        }
    }
    return victim;
}

bool slot_zawiera_zakres(const SlotCacheWierszy& slot,
                         uint32_t first, uint32_t count) {
    if (!slot.valid || count == 0 || first < slot.first) return false;
    const uint64_t slot_end =
        static_cast<uint64_t>(slot.first) + slot.count;
    const uint64_t wanted_end = static_cast<uint64_t>(first) + count;
    return wanted_end <= slot_end;
}

bool zaladuj_slot_cache(uint32_t first, uint32_t count) {
    if (count == 0 || first >= img_h || count > img_h - first) return false;
    for (uint32_t i = 0; i < row_cache_slot_count; ++i) {
        if (slot_zawiera_zakres(row_cache_slots[i], first, count)) {
            row_cache_slots[i].stamp = ++row_cache_clock;
            return true;
        }
    }

    SlotCacheWierszy* victim = wybierz_slot_cache();
    if (!victim || !victim->data ||
        !zaladuj_cache_wierszy(victim->data, first, count)) {
        if (victim) victim->valid = false;
        return false;
    }
    victim->first = first;
    victim->count = count;
    victim->stamp = ++row_cache_clock;
    victim->valid = true;
    return true;
}

const uint8_t* wczytaj_wiersz_bmp(uint32_t y,
                                  uint32_t preferowane_wiersze = ROW_CACHE_ROWS) {
    if (!img_valid || y >= img_h || img_stride == 0 ||
        img_stride > MAX_SCANLINE_BYTES || row_cache_slot_count == 0) {
        return nullptr;
    }

    for (uint32_t i = 0; i < row_cache_slot_count; ++i) {
        SlotCacheWierszy& slot = row_cache_slots[i];
        if (slot.valid && y >= slot.first && y - slot.first < slot.count) {
            slot.stamp = ++row_cache_clock;
            return wskaz_wiersz_cache(slot.data, slot.first, slot.count, y);
        }
    }

    if (preferowane_wiersze == 0 || preferowane_wiersze > ROW_CACHE_ROWS)
        preferowane_wiersze = ROW_CACHE_ROWS;
    const uint32_t first =
        (y / preferowane_wiersze) * preferowane_wiersze;
    uint32_t count = img_h - first;
    if (count > preferowane_wiersze) count = preferowane_wiersze;
    if (!zaladuj_slot_cache(first, count)) return nullptr;
    for (uint32_t i = 0; i < row_cache_slot_count; ++i) {
        SlotCacheWierszy& slot = row_cache_slots[i];
        if (slot_zawiera_zakres(slot, first, count))
            return wskaz_wiersz_cache(slot.data, slot.first, slot.count, y);
    }
    return nullptr;
}

bool prefetch_jeden_blok_1do1() {
    if (!img_valid || row_cache_slot_count < 2 || img_h == 0 ||
        render_1do1.active) return false;

    const int area_h = pobierz_prostokat_obrazu().h;
    if (area_h <= 0 || view_y < 0) return false;
    uint32_t visible_h = static_cast<uint32_t>(area_h);
    const uint32_t start_y = static_cast<uint32_t>(view_y);
    if (start_y >= img_h) return false;
    if (visible_h > img_h - start_y) visible_h = img_h - start_y;
    if (visible_h == 0) return false;

    const uint32_t total_blocks =
        (img_h + ROW_CACHE_ROWS - 1U) / ROW_CACHE_ROWS;
    const uint32_t first_visible = start_y / ROW_CACHE_ROWS;
    const uint64_t last_y64 =
        static_cast<uint64_t>(start_y) + visible_h - 1U;
    if (last_y64 >= img_h) return false;
    const uint32_t last_visible =
        static_cast<uint32_t>(last_y64) / ROW_CACHE_ROWS;
    const uint32_t visible_blocks = last_visible - first_visible + 1U;
    /* Tylko po jednym bloku marginesu nad i pod widokiem.  Wypelnianie calej
       puli w petli jałowej niepotrzebnie monopolizuje wolny backend PSF
       (szczegolnie widoczne pod TCG), a dwa bloki wystarczaja dla kroku 64 px. */
    uint32_t wanted_blocks = visible_blocks;
    if (wanted_blocks <= UINT32_MAX - 2U) wanted_blocks += 2U;
    if (wanted_blocks > total_blocks) wanted_blocks = total_blocks;

    uint32_t before = (wanted_blocks - visible_blocks) / 2U;
    if (before > first_visible) before = first_visible;
    uint32_t wanted_first = first_visible - before;
    if (wanted_blocks > total_blocks - wanted_first)
        wanted_first = total_blocks - wanted_blocks;
    const uint32_t wanted_end = wanted_first + wanted_blocks;

    for (uint32_t block = wanted_first; block < wanted_end; ++block) {
        const uint32_t first = block * ROW_CACHE_ROWS;
        uint32_t count = img_h - first;
        if (count > ROW_CACHE_ROWS) count = ROW_CACHE_ROWS;
        bool present = false;
        for (uint32_t i = 0; i < row_cache_slot_count; ++i) {
            if (slot_zawiera_zakres(row_cache_slots[i], first, count)) {
                present = true;
                break;
            }
        }
        if (!present) return zaladuj_slot_cache(first, count);
    }
    return false;
}

bool poprawny_prostokat(int x, int y, int w, int h, int area_w, int area_h) {
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || area_w <= 0 || area_h <= 0)
        return false;
    if (x > area_w || y > area_h || w > area_w - x || h > area_h - y)
        return false;
    return true;
}

void wypelnij_bufor(uint32_t* bufor, int w, int h, uint32_t kolor) {
    if (!bufor || w <= 0 || h <= 0) return;
    uint64_t pixels64 = 0;
    if (!mnoz_u64(static_cast<uint64_t>(w), static_cast<uint64_t>(h), &pixels64) ||
        pixels64 > viewport_capacity) return;
    for (size_t i = 0; i < static_cast<size_t>(pixels64); ++i) {
        bufor[i] = kolor;
    }
}

bool dekoduj_prostokat_1do1(int area_w, int area_h,
                            int dst_x, int dst_y, int w, int h,
                            int src_x, int src_y,
                            uint32_t preferowane_wiersze = ROW_CACHE_ROWS) {
    if (!viewport_buf || !img_valid ||
        !poprawny_prostokat(dst_x, dst_y, w, h, area_w, area_h) ||
        src_x < 0 || src_y < 0) return false;
    const uint64_t src_right = static_cast<uint64_t>(src_x) + static_cast<uint64_t>(w);
    const uint64_t src_bottom = static_cast<uint64_t>(src_y) + static_cast<uint64_t>(h);
    if (src_right > img_w || src_bottom > img_h) return false;

    const uint32_t bpp = img_bpp;
    const size_t aw = static_cast<size_t>(area_w);

    for (int dy = 0; dy < h; ++dy) {
        const uint32_t sy = static_cast<uint32_t>(src_y + dy);
        const uint8_t* row = wczytaj_wiersz_bmp(sy, preferowane_wiersze);
        if (!row) return false;

        const size_t dst_row_start = static_cast<size_t>(dst_y + dy) * aw + static_cast<size_t>(dst_x);
        if (dst_row_start > viewport_capacity ||
            static_cast<size_t>(w) > viewport_capacity - dst_row_start) return false;

        uint32_t* dst_ptr = viewport_buf + dst_row_start;

        if (bpp == 32) {
            const size_t byte_off = static_cast<size_t>(src_x) * 4U;
            if (byte_off + static_cast<size_t>(w) * 4U > img_stride) return false;
            const uint8_t* src_bytes = row + byte_off;
            for (int dx = 0; dx < w; ++dx) {
                uint32_t b = src_bytes[0];
                uint32_t g = src_bytes[1];
                uint32_t r = src_bytes[2];
                uint32_t col = (r << 16) | (g << 8) | b;
                if (col == 0) col = TECHNICZNY_CZARNY;
                dst_ptr[dx] = col;
                src_bytes += 4;
            }
        } else if (bpp == 24) {
            const size_t byte_off = static_cast<size_t>(src_x) * 3U;
            if (byte_off + static_cast<size_t>(w) * 3U > img_stride) return false;
            const uint8_t* src_bytes = row + byte_off;
            for (int dx = 0; dx < w; ++dx) {
                uint32_t b = src_bytes[0];
                uint32_t g = src_bytes[1];
                uint32_t r = src_bytes[2];
                uint32_t col = (r << 16) | (g << 8) | b;
                if (col == 0) col = TECHNICZNY_CZARNY;
                dst_ptr[dx] = col;
                src_bytes += 3;
            }
        } else {
            return false;
        }
    }
    return true;
}

void kopiuj_nakladajace_piksele(uint32_t* dst, const uint32_t* src, int count) {
    if (!dst || !src || count <= 0 || dst == src) return;
    if (dst < src) {
        for (int i = 0; i < count; ++i) dst[i] = src[i];
    } else {
        for (int i = count; i > 0; --i) dst[i - 1] = src[i - 1];
    }
}

bool przesun_zawartosc_viewportu(int area_w, int area_h,
                                 int x, int y, int w, int h,
                                 int delta_x, int delta_y) {
    if (!viewport_buf || !poprawny_prostokat(x, y, w, h, area_w, area_h))
        return false;
    const int abs_x = delta_x < 0 ? -delta_x : delta_x;
    const int abs_y = delta_y < 0 ? -delta_y : delta_y;
    if (abs_x >= w || abs_y >= h) return false;

    const int src_x = x + (delta_x > 0 ? delta_x : 0);
    const int dst_x = x + (delta_x < 0 ? -delta_x : 0);
    const int src_y = y + (delta_y > 0 ? delta_y : 0);
    const int dst_y = y + (delta_y < 0 ? -delta_y : 0);
    const int copy_w = w - abs_x;
    const int copy_h = h - abs_y;

    for (int n = 0; n < copy_h; ++n) {
        const int row = dst_y < src_y ? n : (copy_h - 1 - n);
        const size_t src_index = static_cast<size_t>(src_y + row) *
                                 static_cast<size_t>(area_w) +
                                 static_cast<size_t>(src_x);
        const size_t dst_index = static_cast<size_t>(dst_y + row) *
                                 static_cast<size_t>(area_w) +
                                 static_cast<size_t>(dst_x);
        if (src_index > viewport_capacity || dst_index > viewport_capacity ||
            static_cast<size_t>(copy_w) > viewport_capacity - src_index ||
            static_cast<size_t>(copy_w) > viewport_capacity - dst_index) return false;
        kopiuj_nakladajace_piksele(viewport_buf + dst_index,
                                  viewport_buf + src_index, copy_w);
    }
    return true;
}

bool blit_viewport(const ProstokatObrazu& obszar, const uint32_t* bufor) {
    const int x = obszar.x;
    const int y = obszar.y;
    const int w = obszar.w;
    const int h = obszar.h;
    if (!bufor || w <= 0 || h <= 0) return false;
    uint64_t pixels = 0;
    if (!mnoz_u64(static_cast<uint64_t>(w), static_cast<uint64_t>(h), &pixels) ||
        pixels > viewport_capacity) return false;

    if (w <= BWS57_MAX_DIM && h <= BWS57_MAX_DIM &&
        pixels <= BWS57_MAX_PIXELS) {
        return profilowany_blit(x, y, w, h, bufor);
    }

    bool ok = true;
    for (int ty = 0; ty < h; ty += TILE_H) {
        const int tile_h = ty + TILE_H <= h ? TILE_H : h - ty;
        for (int tx = 0; tx < w; tx += TILE_W) {
            const int tile_w = tx + TILE_W <= w ? TILE_W : w - tx;
            for (int dy = 0; dy < tile_h; ++dy) {
                const size_t src = static_cast<size_t>(ty + dy) *
                                   static_cast<size_t>(w) + static_cast<size_t>(tx);
                const size_t dst = static_cast<size_t>(dy) * static_cast<size_t>(tile_w);
                if (src > viewport_capacity ||
                    static_cast<size_t>(tile_w) > viewport_capacity - src) return false;
                for (int dx = 0; dx < tile_w; ++dx) {
                    kafel_buf[dst + static_cast<size_t>(dx)] =
                        bufor[src + static_cast<size_t>(dx)];
                }
            }
            if (!poprawny_prostokat(tx, ty, tile_w, tile_h, w, h)) return false;
            if (!profilowany_blit(x + tx, y + ty, tile_w, tile_h, kafel_buf)) ok = false;
        }
    }
    return ok;
}

bool blit_fragment_viewportu(const ProstokatObrazu& obszar,
                             int local_x, int local_y, int w, int h,
                             const uint32_t* bufor) {
    if (!bufor || !kafel_buf ||
        !poprawny_prostokat(local_x, local_y, w, h, obszar.w, obszar.h))
        return false;

    /* Pelnej szerokosci pas jest juz ciasno upakowany w viewport_buf.  Dzieki
       temu sasiednie ukonczone kroki moga trafic do BWS57 jednym blitem. */
    uint64_t pixels = 0;
    if (local_x == 0 && w == obszar.w &&
        w <= BWS57_MAX_DIM && h <= BWS57_MAX_DIM &&
        mnoz_u64(static_cast<uint64_t>(w), static_cast<uint64_t>(h), &pixels) &&
        pixels <= BWS57_MAX_PIXELS) {
        const size_t src = static_cast<size_t>(local_y) *
                           static_cast<size_t>(obszar.w);
        if (src > viewport_capacity ||
            static_cast<size_t>(pixels) > viewport_capacity - src)
            return false;
        return profilowany_blit(obszar.x, obszar.y + local_y,
                                w, h, bufor + src);
    }

    /* Pozostale fragmenty maja stride calego viewportu, wiec sa pakowane do
       stalego kafla; ta sciezka obsluguje m.in. waskie pasy po przesunieciu. */
    bool ok = true;
    for (int ty = 0; ty < h; ty += TILE_H) {
        const int tile_h = ty + TILE_H <= h ? TILE_H : h - ty;
        for (int tx = 0; tx < w; tx += TILE_W) {
            const int tile_w = tx + TILE_W <= w ? TILE_W : w - tx;
            if (!poprawny_prostokat(local_x + tx, local_y + ty,
                                    tile_w, tile_h, obszar.w, obszar.h))
                return false;
            for (int dy = 0; dy < tile_h; ++dy) {
                const size_t src = static_cast<size_t>(local_y + ty + dy) *
                    static_cast<size_t>(obszar.w) +
                    static_cast<size_t>(local_x + tx);
                const size_t dst = static_cast<size_t>(dy) *
                    static_cast<size_t>(tile_w);
                if (src > viewport_capacity ||
                    static_cast<size_t>(tile_w) > viewport_capacity - src)
                    return false;
                for (int dx = 0; dx < tile_w; ++dx)
                    kafel_buf[dst + static_cast<size_t>(dx)] =
                        bufor[src + static_cast<size_t>(dx)];
            }
            if (!profilowany_blit(obszar.x + local_x + tx,
                                  obszar.y + local_y + ty,
                                  tile_w, tile_h, kafel_buf)) ok = false;
        }
    }
    return ok;
}

bool renderuj_dopasowany(int aw, int ah) {
    if (fit_cache.valid && fit_cache.generation == image_generation &&
        fit_cache.area_w == aw && fit_cache.area_h == ah) {
        return true;
    }

    if (!fit_buf) return false;
    /* Bufor moze byc widoczny dopiero po ukonczeniu wszystkich wierszy. */
    fit_cache.valid = false;
    wypelnij_bufor(fit_buf, aw, ah, KOLOR_TLO);
    uint64_t scale_w = (static_cast<uint64_t>(aw) * 65536ULL) / img_w;
    uint64_t scale_h = (static_cast<uint64_t>(ah) * 65536ULL) / img_h;
    uint64_t scale = scale_w < scale_h ? scale_w : scale_h;
    if (scale == 0) scale = 1;
    int dst_w = static_cast<int>((static_cast<uint64_t>(img_w) * scale) / 65536ULL);
    int dst_h = static_cast<int>((static_cast<uint64_t>(img_h) * scale) / 65536ULL);
    if (dst_w < 1) dst_w = 1;
    if (dst_h < 1) dst_h = 1;
    if (dst_w > aw) dst_w = aw;
    if (dst_h > ah) dst_h = ah;
    const int off_x = (aw - dst_w) / 2;
    const int off_y = (ah - dst_h) / 2;

    const uint32_t bytes_per_pixel = img_bpp / 8U;
    const size_t aw_sz = static_cast<size_t>(aw);

    for (int dy = 0; dy < dst_h; ++dy) {
        int64_t sy_fp = static_cast<int64_t>(
            ((static_cast<uint64_t>(2 * dy + 1) * img_h) << 15) /
            static_cast<uint64_t>(dst_h)) - 32768;
        if (sy_fp < 0) sy_fp = 0;
        const int64_t max_sy_fp = static_cast<int64_t>(img_h - 1U) << 16;
        if (sy_fp > max_sy_fp) sy_fp = max_sy_fp;
        const uint32_t sy0 = static_cast<uint32_t>(sy_fp >> 16);
        const uint32_t sy1 = sy0 + 1U < img_h ? sy0 + 1U : sy0;
        const uint32_t fy = static_cast<uint32_t>(sy_fp) & 0xFFFFU;
        const uint8_t* row0 = wczytaj_wiersz_bmp(sy0, FIT_ROW_CACHE_ROWS);
        const uint8_t* row1 = wczytaj_wiersz_bmp(sy1, FIT_ROW_CACHE_ROWS);
        if (!row0 || !row1) return false;
        const size_t dst_row = static_cast<size_t>(off_y + dy) * aw_sz + static_cast<size_t>(off_x);
        if (dst_row > viewport_capacity ||
            static_cast<size_t>(dst_w) > viewport_capacity - dst_row) return false;

        uint32_t* dst_ptr = fit_buf + dst_row;

        for (int dx = 0; dx < dst_w; ++dx) {
            int64_t sx_fp = static_cast<int64_t>(
                ((static_cast<uint64_t>(2 * dx + 1) * img_w) << 15) /
                static_cast<uint64_t>(dst_w)) - 32768;
            if (sx_fp < 0) sx_fp = 0;
            const int64_t max_sx_fp = static_cast<int64_t>(img_w - 1U) << 16;
            if (sx_fp > max_sx_fp) sx_fp = max_sx_fp;
            const uint32_t sx0 = static_cast<uint32_t>(sx_fp >> 16);
            const uint32_t sx1 = sx0 + 1U < img_w ? sx0 + 1U : sx0;
            const uint32_t fx = static_cast<uint32_t>(sx_fp) & 0xFFFFU;
            const size_t o0 = static_cast<size_t>(sx0) * bytes_per_pixel;
            const size_t o1 = static_cast<size_t>(sx1) * bytes_per_pixel;
            if (o0 + 3U > img_stride || o1 + 3U > img_stride) return false;

            uint32_t kanal[3] = {};
            for (uint32_t c = 0; c < 3; ++c) {
                const uint32_t a = row0[o0 + c];
                const uint32_t b = row0[o1 + c];
                const uint32_t d = row1[o0 + c];
                const uint32_t e = row1[o1 + c];
                const uint32_t gora = (a * (65536U - fx) + b * fx + 32768U) >> 16;
                const uint32_t dol = (d * (65536U - fx) + e * fx + 32768U) >> 16;
                kanal[c] = (gora * (65536U - fy) + dol * fy + 32768U) >> 16;
            }
            uint32_t col = (kanal[2] << 16) | (kanal[1] << 8) | kanal[0];
            if (col == 0) col = TECHNICZNY_CZARNY;
            dst_ptr[dx] = col;
        }
    }

    fit_cache = CacheDopasowania{true, image_generation, aw, ah};
    return true;
}

bool dodaj_dirty_1do1(int x, int y, int w, int h, int aw, int ah) {
    if (!poprawny_prostokat(x, y, w, h, aw, ah) ||
        render_1do1.dirty_count >= MAX_DIRTY_RECTS) return false;
    render_1do1.dirty[render_1do1.dirty_count++] =
        ProstokatObrazu{x, y, w, h};
    return true;
}

bool cache_viewportu_pasuje(const CacheViewportu& c,
                            const CacheViewportu& target) {
    return c.valid && c.mode == target.mode &&
        c.area_w == target.area_w && c.area_h == target.area_h &&
        c.image_x == target.image_x && c.image_y == target.image_y &&
        c.image_w == target.image_w && c.image_h == target.image_h &&
        c.view_x == target.view_x && c.view_y == target.view_y;
}

bool przygotuj_render_1do1(int aw, int ah) {
    const int max_x = static_cast<int>(img_w) > aw ? static_cast<int>(img_w) - aw : 0;
    const int max_y = static_cast<int>(img_h) > ah ? static_cast<int>(img_h) - ah : 0;
    if (view_x < 0) view_x = 0;
    if (view_y < 0) view_y = 0;
    if (view_x > max_x) view_x = max_x;
    if (view_y > max_y) view_y = max_y;

    const int dst_w = static_cast<int>(img_w) - view_x < aw
        ? static_cast<int>(img_w) - view_x : aw;
    const int dst_h = static_cast<int>(img_h) - view_y < ah
        ? static_cast<int>(img_h) - view_y : ah;
    if (dst_w <= 0 || dst_h <= 0) return false;
    const int off_x = static_cast<int>(img_w) < aw ? (aw - dst_w) / 2 : 0;
    const int off_y = static_cast<int>(img_h) < ah ? (ah - dst_h) / 2 : 0;

    const CacheViewportu target{true, TrybSkalowania::JEDEN_DO_JEDNEGO,
        aw, ah, off_x, off_y, dst_w, dst_h, view_x, view_y};
    if (cache_viewportu_pasuje(viewport_cache, target)) return true;
    if (render_1do1.active && render_1do1.generation == image_generation &&
        cache_viewportu_pasuje(render_1do1.target, target)) return true;

    if (!profil_aktywny) rozpocznij_profil();
    anuluj_render_1do1();
    render_1do1.generation = image_generation;
    render_1do1.target = target;

    bool reused = false;
    int delta_x = 0;
    int delta_y = 0;
    if (viewport_cache.valid && viewport_cache.mode == TrybSkalowania::JEDEN_DO_JEDNEGO &&
        viewport_cache.area_w == aw && viewport_cache.area_h == ah &&
        viewport_cache.image_x == off_x && viewport_cache.image_y == off_y &&
        viewport_cache.image_w == dst_w && viewport_cache.image_h == dst_h) {
        delta_x = view_x - viewport_cache.view_x;
        delta_y = view_y - viewport_cache.view_y;
        if (delta_x == 0 && delta_y == 0) {
            return true;
        }
        reused = przesun_zawartosc_viewportu(
            aw, ah, off_x, off_y, dst_w, dst_h, delta_x, delta_y);
    }

    if (!reused) {
        wypelnij_bufor(viewport_buf, aw, ah, KOLOR_TLO);
        if (!dodaj_dirty_1do1(off_x, off_y, dst_w, dst_h, aw, ah))
            return false;
    } else {
        if (delta_y > 0 && !dodaj_dirty_1do1(
                off_x, off_y + dst_h - delta_y, dst_w, delta_y, aw, ah))
            return false;
        if (delta_y < 0 && !dodaj_dirty_1do1(
                off_x, off_y, dst_w, -delta_y, aw, ah)) return false;

        const int overlap_y = delta_y < 0 ? -delta_y : 0;
        const int overlap_h = dst_h - (delta_y < 0 ? -delta_y : delta_y);
        if (overlap_h > 0 && delta_x > 0 && !dodaj_dirty_1do1(
                off_x + dst_w - delta_x, off_y + overlap_y,
                delta_x, overlap_h, aw, ah))
            return false;
        if (overlap_h > 0 && delta_x < 0 && !dodaj_dirty_1do1(
                off_x, off_y + overlap_y, -delta_x, overlap_h, aw, ah))
            return false;
    }

    viewport_cache.valid = false;
    render_1do1.active = render_1do1.dirty_count > 0;
    if (!render_1do1.active) viewport_cache = target;
    return true;
}

bool renderuj_1do1(int aw, int ah) {
    if (!przygotuj_render_1do1(aw, ah)) {
        if (profil_aktywny) zakoncz_profil();
        return false;
    }
    if (!render_1do1.active && profil_aktywny) zakoncz_profil();
    return true;
}

bool krok_renderowania_1do1() {
    if (!render_1do1.active || tryb != TrybSkalowania::JEDEN_DO_JEDNEGO ||
        !img_valid || render_1do1.generation != image_generation) {
        anuluj_render_1do1();
        return false;
    }

    const ProstokatObrazu obszar = pobierz_prostokat_obrazu();
    const CacheViewportu& target = render_1do1.target;
    if (obszar.w != target.area_w || obszar.h != target.area_h ||
        view_x != target.view_x || view_y != target.view_y ||
        render_1do1.dirty_index >= render_1do1.dirty_count) {
        anuluj_render_1do1();
        return false;
    }

    const ProstokatObrazu& dirty =
        render_1do1.dirty[render_1do1.dirty_index];
    int rows = dirty.h - render_1do1.next_row;
    if (rows > static_cast<int>(ONE_TO_ONE_STEP_ROWS))
        rows = static_cast<int>(ONE_TO_ONE_STEP_ROWS);
    if (rows <= 0) return false;

    const int dst_y = dirty.y + render_1do1.next_row;
    const int src_x = target.view_x + dirty.x - target.image_x;
    const int src_y = target.view_y + dst_y - target.image_y;
    if (!dekoduj_prostokat_1do1(
            target.area_w, target.area_h, dirty.x, dst_y, dirty.w, rows,
            src_x, src_y, ONE_TO_ONE_STEP_ROWS)) {
        viewport_cache.valid = false;
        anuluj_render_1do1();
        if (profil_aktywny) zakoncz_profil();
        return false;
    }

    render_1do1.next_row += rows;
    bool presented = false;
    const int completed_rows =
        render_1do1.next_row - render_1do1.presented_row;
    const bool dirty_complete = render_1do1.next_row >= dirty.h;
    if (completed_rows >= static_cast<int>(ONE_TO_ONE_BLIT_ROWS) ||
        dirty_complete) {
        const int presented_y = dirty.y + render_1do1.presented_row;
        if (!blit_fragment_viewportu(
                obszar, dirty.x, presented_y, dirty.w,
                completed_rows, viewport_buf)) {
            viewport_cache.valid = false;
            anuluj_render_1do1();
            if (profil_aktywny) zakoncz_profil();
            return false;
        }
        render_1do1.presented_row = render_1do1.next_row;
        presented = true;
    }

    if (render_1do1.next_row >= dirty.h) {
        ++render_1do1.dirty_index;
        render_1do1.next_row = 0;
        render_1do1.presented_row = 0;
    }
    if (render_1do1.dirty_index >= render_1do1.dirty_count) {
        viewport_cache = render_1do1.target;
        viewport_cache.valid = true;
        anuluj_render_1do1();
        if (profil_aktywny) zakoncz_profil();
        aktualizuj_pasek_statusu();
    }
    return presented;
}

/* Jedyna sciezka zmiany trybu. OPEN_FILE, przyciski i klawiatura wywoluja
   te sama funkcje i ten sam renderer cache, zanim obraz trafi do warstwy. */
bool aktywuj_tryb(TrybSkalowania nowy, bool nowy_obraz) {
    const bool zmiana = tryb != nowy;
    tryb = nowy;
    if (nowy_obraz) {
        uniewaznij_cache_wierszy();
        uniewaznij_cache_rozmiaru();
        fit_cache = CacheDopasowania{};
        view_x = 0;
        view_y = 0;
    }

    if (img_valid) {
        const ProstokatObrazu obszar = pobierz_prostokat_obrazu();
        if (obszar.w > 0 && obszar.h > 0) {
            if (nowy == TrybSkalowania::DOPASUJ) {
                anuluj_render_1do1();
                if (profil_aktywny) zakoncz_profil();
                (void)renderuj_dopasowany(obszar.w, obszar.h);
            } else {
                (void)renderuj_1do1(obszar.w, obszar.h);
            }
        }
        aktualizuj_pasek_statusu();
    }
    return zmiana || nowy_obraz;
}

void rysuj_obraz() {
    if (zminimalizowane) return;

    const ProstokatObrazu obszar = pobierz_prostokat_obrazu();
    const int ax = obszar.x;
    const int ay = obszar.y;
    const int aw = obszar.w;
    const int ah = obszar.h;

    if (aw <= 0 || ah <= 0) return;

    if (!img_valid) {
        gui_rysuj_prostokat(ax, ay, aw, ah, KOLOR_TLO);
        const char* msg = blad_txt[0] ? blad_txt : "Brak obrazu - otwórz plik BMP";
        int tw = oblicz_szerokosc_tekstu(msg, 1);
        int tx = ax + (aw - tw) / 2;
        if (tx < ax) tx = ax;
        int ty = ay + ah / 2 - 8;
        gui_wypisz_tekst_kolor(tx, ty, KOLOR_ERROR, msg);
        return;
    }

    uint64_t pixels = 0;
    if (!viewport_buf ||
        !mnoz_u64(static_cast<uint64_t>(aw), static_cast<uint64_t>(ah), &pixels) ||
        pixels > viewport_capacity) {
        gui_rysuj_prostokat(ax, ay, aw, ah, KOLOR_TLO);
        return;
    }

    const bool dopasuj = tryb == TrybSkalowania::DOPASUJ;
    if (dopasuj) rozpocznij_profil();
    const bool ok = dopasuj ? renderuj_dopasowany(aw, ah) : renderuj_1do1(aw, ah);
    if (ok) {
        (void)blit_viewport(obszar, dopasuj ? fit_buf : viewport_buf);
    }
    if (dopasuj) zakoncz_profil();
    aktualizuj_pasek_statusu();
}

GeometriaKontrolek pobierz_geometrie_kontrolek() {
    GeometriaKontrolek g{};
    g.bar = ProstokatObrazu{
        win_x + 1, win_y + win_h - STATUS_H - 1, win_w - 2, STATUS_H};
    if (g.bar.w < 0) g.bar.w = 0;

    const int w_1do1 = oblicz_szerokosc_tekstu("1:1", 1) + 2 * BUTTON_PAD_X;
    const int w_fit = oblicz_szerokosc_tekstu("Dopasuj", 1) + 2 * BUTTON_PAD_X;
    const int group_w = w_1do1 + BUTTON_GAP + w_fit;
    int start = g.bar.x + (g.bar.w - group_w) / 2;
    if (start < g.bar.x) start = g.bar.x;
    const int button_y = g.bar.y + (g.bar.h - BUTTON_H) / 2;
    g.jeden_do_jednego = ProstokatObrazu{start, button_y, w_1do1, BUTTON_H};
    g.dopasuj = ProstokatObrazu{
        start + w_1do1 + BUTTON_GAP, button_y, w_fit, BUTTON_H};
    return g;
}

bool punkt_w_prostokacie(int x, int y, const ProstokatObrazu& r) {
    return r.w > 0 && r.h > 0 &&
        x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

Kontrolka hit_test_kontrolek(int mx, int my) {
    const GeometriaKontrolek g = pobierz_geometrie_kontrolek();
    if (punkt_w_prostokacie(mx, my, g.jeden_do_jednego))
        return Kontrolka::JEDEN_DO_JEDNEGO;
    if (punkt_w_prostokacie(mx, my, g.dopasuj)) return Kontrolka::DOPASUJ;
    return Kontrolka::BRAK;
}

void rysuj_przycisk_trybu(const ProstokatObrazu& r, const char* tekst,
                          Kontrolka kontrolka, bool aktywny) {
    uint32_t kolor = aktywny ? KOLOR_PRZYCISK_AKTYWNY : KOLOR_PRZYCISK;
    if (kontrolka_hover == kontrolka && !aktywny) kolor = KOLOR_PRZYCISK_HOVER;
    if (kontrolka_wcisnieta == kontrolka) kolor = KOLOR_PRZYCISK_AKTYWNY;
    gui_rysuj_prostokat(r.x, r.y, r.w, r.h, KOLOR_PRZYCISK_RAMKA);
    gui_rysuj_prostokat(r.x + 1, r.y + 1, r.w - 2, r.h - 2, kolor);
    rysuj_tekst_wysrodkowany(
        r.x, r.y, r.w, r.h, 1, KOLOR_STATUS_TXT, tekst);
}

void rysuj_pasek_kontrolek() {
    const GeometriaKontrolek g = pobierz_geometrie_kontrolek();
    if (g.bar.w <= 0 || g.bar.h <= 0) return;

    gui_rysuj_prostokat(g.bar.x, g.bar.y, g.bar.w, g.bar.h, KOLOR_STATUS_BG);

    /* Tekst statusu jest przycinany przed centralnymi przyciskami. */
    const int max_text_w = g.jeden_do_jednego.x - (g.bar.x + 6) - 6;
    if (max_text_w > 0) {
        const char* zrodlo = status_txt[0] ? status_txt
            : (blad_txt[0] ? blad_txt : "1 / D: tryb | WASD: przesuwanie");
        char widoczny[256] = {};
        size_t n = 0;
        while (zrodlo[n] && n + 1 < sizeof(widoczny)) {
            widoczny[n] = zrodlo[n];
            widoczny[n + 1] = '\0';
            if (oblicz_szerokosc_tekstu(widoczny, 1) > max_text_w) {
                widoczny[n] = '\0';
                break;
            }
            ++n;
        }
        gui_wypisz_tekst_kolor(g.bar.x + 6, g.bar.y + 3,
            blad_txt[0] && !status_txt[0] ? KOLOR_ERROR : KOLOR_STATUS_TXT,
            widoczny);
    }
    rysuj_przycisk_trybu(g.jeden_do_jednego, "1:1",
        Kontrolka::JEDEN_DO_JEDNEGO,
        tryb == TrybSkalowania::JEDEN_DO_JEDNEGO);
    rysuj_przycisk_trybu(g.dopasuj, "Dopasuj",
        Kontrolka::DOPASUJ, tryb == TrybSkalowania::DOPASUJ);

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

    rysuj_pasek_kontrolek();

    gui_odswiez();
}

bool przesun_widok(int dx, int dy) {
    if (tryb != TrybSkalowania::JEDEN_DO_JEDNEGO || !img_valid) return false;
    const ProstokatObrazu obszar = pobierz_prostokat_obrazu();
    const int aw = obszar.w;
    const int ah = obszar.h;

    int maxX = (static_cast<int>(img_w) > aw) ? (static_cast<int>(img_w) - aw) : 0;
    int maxY = (static_cast<int>(img_h) > ah) ? (static_cast<int>(img_h) - ah) : 0;

    const int old_x = view_x;
    const int old_y = view_y;
    int64_t next_x = static_cast<int64_t>(view_x) + dx;
    int64_t next_y = static_cast<int64_t>(view_y) + dy;
    if (next_x < 0) next_x = 0;
    if (next_y < 0) next_y = 0;
    if (next_x > maxX) next_x = maxX;
    if (next_y > maxY) next_y = maxY;
    view_x = static_cast<int>(next_x);
    view_y = static_cast<int>(next_y);

    return (view_x != old_x || view_y != old_y);
}

} // namespace

/* =========================================================================
 * 8. PĘTLA GŁÓWNA
 * ========================================================================= */
extern "C" __attribute__((noreturn)) void _start() {
    gui_pobierz_rozdzielczosc(&screen_w, &screen_h);
    if (screen_w <= 0 || screen_h <= 0) gui_zakoncz_aplikacje();

    uint64_t screen_pixels = 0;
    uint64_t viewport_bytes = 0;
    if (!mnoz_u64(static_cast<uint64_t>(screen_w),
                  static_cast<uint64_t>(screen_h), &screen_pixels) ||
        !mnoz_u64(screen_pixels, sizeof(uint32_t), &viewport_bytes) ||
        screen_pixels == 0 || screen_pixels > static_cast<uint64_t>(SIZE_MAX) ||
        viewport_bytes > static_cast<uint64_t>(ULONG_MAX)) {
        gui_zakoncz_aplikacje();
    }
    viewport_capacity = static_cast<size_t>(screen_pixels);

    uint64_t slots64 = (static_cast<uint64_t>(screen_h) + ROW_CACHE_ROWS - 1U) /
                       ROW_CACHE_ROWS + 8ULL;
    if (slots64 > MAX_ROW_CACHE_SLOTS) slots64 = MAX_ROW_CACHE_SLOTS;
    if (slots64 < 16) slots64 = 16;
    row_cache_slot_count = static_cast<uint32_t>(slots64);

    kafel_buf = static_cast<uint32_t*>(
        profilowany_malloc(sizeof(uint32_t) * TILE_PIXELS));
    viewport_buf = static_cast<uint32_t*>(
        profilowany_malloc(static_cast<unsigned long>(viewport_bytes)));
    fit_buf = static_cast<uint32_t*>(
        profilowany_malloc(static_cast<unsigned long>(viewport_bytes)));
    bool sloty_ok = (row_cache_slot_count != 0);
    for (uint32_t i = 0; i < row_cache_slot_count; ++i) {
        row_cache_slots[i].data = static_cast<uint8_t*>(
            profilowany_malloc(static_cast<unsigned long>(ROW_CACHE_BYTES)));
        if (!row_cache_slots[i].data) {
            sloty_ok = false;
            break;
        }
    }
    if (!kafel_buf || !viewport_buf || !fit_buf || !sloty_ok) {
        if (kafel_buf) gui_free(kafel_buf);
        if (viewport_buf) gui_free(viewport_buf);
        if (fit_buf) gui_free(fit_buf);
        for (uint32_t i = 0; i < row_cache_slot_count; ++i) {
            if (row_cache_slots[i].data) gui_free(row_cache_slots[i].data);
        }
        gui_zakoncz_aplikacje();
    }
    uniewaznij_cache_wierszy();

    ogranicz_geometrie();
    if (!utworz_warstwe()) {
        gui_free(kafel_buf);
        gui_free(viewport_buf);
        gui_free(fit_buf);
        for (uint32_t i = 0; i < row_cache_slot_count; ++i)
            gui_free(row_cache_slots[i].data);
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
    bool redraw_controls = false;
    bool pelne = true;
    uint64_t ostatnia_klatka_ns = 0;

    // Bufor sekwencji ANSI dla klawiszy funkcyjnych
    char ansi_seq[8] = {};
    int ansi_idx = 0;

    while (!wyjdz) {
        if (redraw) {
            const uint64_t teraz = gui_czas_monotoniczny_ns();
            const bool zegar_brak = teraz == 0 || ostatnia_klatka_ns == 0;
            const bool zegar_cofniety =
                !zegar_brak && teraz < ostatnia_klatka_ns;
            const bool termin = zegar_brak || zegar_cofniety ||
                teraz - ostatnia_klatka_ns >= FRAME_NS;
            if (termin) {
                rysuj_interfejs(pelne);
                redraw = false;
                redraw_controls = false;
                pelne = false;
                const uint64_t po_klatce = gui_czas_monotoniczny_ns();
                ostatnia_klatka_ns = po_klatce != 0 ? po_klatce : teraz;
            }
        } else if (redraw_controls && !zminimalizowane) {
            rysuj_pasek_kontrolek();
            gui_odswiez();
            redraw_controls = false;
        }

        bool mamy_zdarzenie = false;
        if (redraw && ostatnia_klatka_ns != 0) {
            /* Do terminu klatki tylko odpytujemy kolejke. Kolejne ruchy sa
               sumowane w view_x/view_y i zakonczone jednym renderem. */
            mamy_zdarzenie = gui_pobierz_zdarzenie(&event_buf[0]);
            if (!mamy_zdarzenie) {
                asm volatile("pause" ::: "memory");
                continue;
            }
        } else {
            mamy_zdarzenie = gui_pobierz_zdarzenie(&event_buf[0]);
            if (!mamy_zdarzenie && render_1do1.active) {
                const bool zmieniono = krok_renderowania_1do1();
                if (zmieniono && !zminimalizowane) {
                    /* Fragment obrazu jest zawsze w image_rect. Pasek jest
                       mimo to odnawiany, aby stan/metryki pozostaly widoczne. */
                    rysuj_pasek_kontrolek();
                    gui_odswiez();
                }
                continue;
            }
            if (!mamy_zdarzenie && prefetch_jeden_blok_1do1()) continue;
            if (!mamy_zdarzenie)
                mamy_zdarzenie = gui_czekaj_na_zdarzenie(&event_buf[0]);
        }
        if (!mamy_zdarzenie) continue;

        size_t event_count = 1;
        while (event_count < EVENT_BATCH &&
               gui_pobierz_zdarzenie(&event_buf[event_count])) {
            ++event_count;
        }

        for (size_t event_index = 0; event_index < event_count && !wyjdz;
             ++event_index) {
            const bws_zdarzenie& ev = event_buf[event_index];

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
                redraw = true;
                pelne = false;
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

            const ProstokatObrazu obszar_obrazu = pobierz_prostokat_obrazu();
            const int ax = obszar_obrazu.x;
            const int ay = obszar_obrazu.y;
            const int aw = obszar_obrazu.w;
            const int ah = obszar_obrazu.h;

            if (!zminimalizowane && ev.typ == BWS_ZDARZENIE_MYSZ_RUCH &&
                !dragging_window && !panning_image) {
                const Kontrolka nowy_hover = hit_test_kontrolek(mx, my);
                if (nowy_hover != kontrolka_hover) {
                    kontrolka_hover = nowy_hover;
                    redraw_controls = true;
                }
            }

            if (klik && !zminimalizowane) {
                bool wOknie = (mx >= win_x && mx < win_x + win_w && my >= win_y && my < win_y + win_h);
                if (wOknie) {
                    const Kontrolka kontrolka = hit_test_kontrolek(mx, my);
                    gui_akcja_belki akcja = gui_hit_test_belki(mx, my, win_x, win_y, win_w);
                    if (kontrolka != Kontrolka::BRAK) {
                        kontrolka_wcisnieta = kontrolka;
                        const TrybSkalowania nowy =
                            kontrolka == Kontrolka::JEDEN_DO_JEDNEGO
                                ? TrybSkalowania::JEDEN_DO_JEDNEGO
                                : TrybSkalowania::DOPASUJ;
                        if (aktywuj_tryb(nowy)) {
                            redraw = true;
                            pelne = false;
                        } else {
                            redraw_controls = true;
                        }
                    } else if (akcja == GUI_BELKA_ZAMKNIJ) {
                        wyjdz = true;
                        dragging_window = false;
                    } else if (akcja == GUI_BELKA_MINIMALIZUJ) {
                        zminimalizowane = gui_minimalizuj_okno();
                        anuluj_render_1do1();
                        viewport_cache.valid = false;
                        profil_aktywny = false;
                        profil_gotowy = false;
                        profil_start_ns = 0;
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
                                uniewaznij_cache_rozmiaru();
                                kontrolka_hover = Kontrolka::BRAK;
                                kontrolka_wcisnieta = Kontrolka::BRAK;
                                redraw = true;
                                redraw_controls = true;
                                pelne = true;
                            }
                        } else {
                            win_x = win_restore_x;
                            win_y = win_restore_y;
                            win_w = win_restore_w;
                            win_h = win_restore_h;
                            if (bws_utworz_warstwe(win_x, win_y, win_w, win_h, 10) >= 0) {
                                zmaksymalizowane = false;
                                uniewaznij_cache_rozmiaru();
                                kontrolka_hover = Kontrolka::BRAK;
                                kontrolka_wcisnieta = Kontrolka::BRAK;
                                redraw = true;
                                redraw_controls = true;
                                pelne = true;
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
                    redraw = true;
                    pelne = false;
                }
            }

            if (puszczenie) {
                if (kontrolka_wcisnieta != Kontrolka::BRAK) {
                    kontrolka_wcisnieta = Kontrolka::BRAK;
                    redraw_controls = true;
                }
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
                            if (przesun_widok(0, -64)) { redraw = true; pelne = false; }
                            ansi_idx = 0;
                        } else if (ansi_seq[2] == 'B') { // Down
                            if (przesun_widok(0, 64)) { redraw = true; pelne = false; }
                            ansi_idx = 0;
                        } else if (ansi_seq[2] == 'C') { // Right
                            if (przesun_widok(64, 0)) { redraw = true; pelne = false; }
                            ansi_idx = 0;
                        } else if (ansi_seq[2] == 'D') { // Left
                            if (przesun_widok(-64, 0)) { redraw = true; pelne = false; }
                            ansi_idx = 0;
                        } else if (ansi_seq[2] == 'H') { // Home
                            if (tryb == TrybSkalowania::JEDEN_DO_JEDNEGO &&
                                (view_x != 0 || view_y != 0)) {
                                view_x = 0; view_y = 0;
                                redraw = true; pelne = false;
                            }
                            ansi_idx = 0;
                        } else if (ansi_seq[2] == 'F') { // End
                            int maxX = (static_cast<int>(img_w) > aw) ? (static_cast<int>(img_w) - aw) : 0;
                            int maxY = (static_cast<int>(img_h) > ah) ? (static_cast<int>(img_h) - ah) : 0;
                            if (tryb == TrybSkalowania::JEDEN_DO_JEDNEGO &&
                                (view_x != maxX || view_y != maxY)) {
                                view_x = maxX; view_y = maxY;
                                redraw = true; pelne = false;
                            }
                            ansi_idx = 0;
                        }
                    } else if (ansi_idx == 4 && ansi_seq[1] == '[' && ansi_seq[3] == '~') {
                        if (ansi_seq[2] == '5') { // PageUp
                            if (przesun_widok(0, -ah / 2)) { redraw = true; pelne = false; }
                            ansi_idx = 0;
                        } else if (ansi_seq[2] == '6') { // PageDown
                            if (przesun_widok(0, ah / 2)) { redraw = true; pelne = false; }
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
                    if (aktywuj_tryb(TrybSkalowania::JEDEN_DO_JEDNEGO)) {
                        redraw = true;
                        pelne = false;
                    }
                } else if (c == 'D') {
                    if (aktywuj_tryb(TrybSkalowania::DOPASUJ)) {
                        redraw = true;
                        pelne = false;
                    }
                } else if (c == 'w' || c == 'W') {
                    if (przesun_widok(0, -64)) { redraw = true; pelne = false; }
                } else if (c == 's' || c == 'S') {
                    if (przesun_widok(0, 64)) { redraw = true; pelne = false; }
                } else if (c == 'a' || c == 'A') {
                    if (przesun_widok(-64, 0)) { redraw = true; pelne = false; }
                } else if (c == 'd') {
                    if (przesun_widok(64, 0)) { redraw = true; pelne = false; }
                }
            }
        }

    }

    zwolnij_bmp();
    if (kafel_buf) gui_free(kafel_buf);
    if (viewport_buf) gui_free(viewport_buf);
    if (fit_buf) gui_free(fit_buf);
    for (uint32_t i = 0; i < row_cache_slot_count; ++i) {
        if (row_cache_slots[i].data) gui_free(row_cache_slots[i].data);
    }
    gui_zakoncz_aplikacje();
}
