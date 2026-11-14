#pragma once
/*
 * Silnik obrazu BMP dla Przegladarki Obrazow (bez UI okna).
 *
 * Zawartosc przeniesiona 1:1 z dawnej monolitycznej aplikacji: strumieniowy parser
 * BMP (24/32 bit, BI_RGB), limity rozmiaru, cache wierszy (zakresy plikow), cache
 * viewportu, renderer dopasowany i 1:1 z dirty-regionami i postepowym dekodowaniem.
 *
 * Silnik nie zna okna ani kontrolek. Warstwa UI ustawia:
 *   g_obszar_obrazu  - prostokat widoku (lokalny, x=y=0, w/h = rozmiar kontrolki),
 *   g_ujscie_obrazu  - funkcje odbierajaca gotowe fragmenty (unieważnienie kontrolki),
 *   kolor_tla        - kolor tla widoku (z motywu; kolory obrazu nigdy sie nie zmieniaja).
 * Dekodowanie trafia do viewport_buf / fit_buf (aw x ah), skad kontrolka je rysuje.
 */
#include "bursztyn_gui.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>
namespace {

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

int view_x = 0;
int view_y = 0;

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

using UjscieObrazu = bool (*)(int x, int y, int w, int h, const uint32_t* piksele);
ProstokatObrazu g_obszar_obrazu{0, 0, 0, 0};
UjscieObrazu g_ujscie_obrazu = nullptr;
uint32_t kolor_tla = 0x00101010U;   // niezerowy: 0x00000000 jest kluczem przezroczystosci warstwy

void ustaw_kolor_tla(uint32_t rgb) {
    rgb &= 0x00FFFFFFU;
    kolor_tla = rgb ? rgb : TECHNICZNY_CZARNY;
}

ProstokatObrazu pobierz_prostokat_obrazu() {
    ProstokatObrazu r = g_obszar_obrazu;
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
    return g_ujscie_obrazu ? g_ujscie_obrazu(x, y, w, h, pixels) : false;
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
    wypelnij_bufor(fit_buf, aw, ah, kolor_tla);
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
        wypelnij_bufor(viewport_buf, aw, ah, kolor_tla);
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
