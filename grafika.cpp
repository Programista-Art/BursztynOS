#include "grafika.h"
#include "pamiec.h"
#include "zegar-rtc.h"
#include "ahci.h"
#include "scheduler.h"
#include "sterowniki/czas/hpet.h"
#include "skladacz_obrazu.h"
#include "bws_zdarzenia.h"
#include "scheduler.h"

#include <stdint.h>
#include <stddef.h>

// Czcionka Unicode i sterownik GOP.
#include "czcionki/extronic16B_unicode.h"
#include "sterowniki/grafika/uefi_gop.h"

// Bezpieczna komunikacja Ring 0 <-> Ring 3.
extern bool skopiuj_string_z_uzytkownika(
    char* bufor_jadra,
    const char* ptr_uzytkownika,
    size_t max_rozmiar);

extern bool skopiuj_do_przestrzeni_uzytkownika(
    void* ptr_uzytkownika,
    const void* bufor_jadra,
    size_t rozmiar);

extern bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(
    void* ptr,
    size_t rozmiar);

// Placement new bez zaleznosci od biblioteki standardowej.
inline void* operator new(unsigned long, void* p) { return p; }
inline void* operator new[](unsigned long, void* p) { return p; }

extern "C" {
    void* __dso_handle = nullptr;
    int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
}

extern "C" bool bws_uruchom_program_z_pliku(
    const char* sciezka_pliku,
    uint8_t bzl_poziom,
    uint64_t flagi_praw,
    bool z_syscalla);

extern "C" void* PobierzAktualnePML4();
extern "C" void WybudzProcesyOczekujaceNaMysz();

bool flaga_zamkniecia_powloki = false;

namespace {

constexpr uint64_t ROZMIAR_STRONY = 4096ULL;

constexpr uint64_t VADDR_BACKBUFFER   = 0x100000000ULL;
constexpr uint64_t VADDR_TAPETA       = 0x110000000ULL;
constexpr uint64_t VADDR_RAW_BMP      = 0x120000000ULL;
constexpr uint64_t VADDR_FRAMEBUFFER  = 0x130000000ULL;

constexpr uint64_t MAX_RAW_BMP = 32ULL * 1024ULL * 1024ULL;

constexpr int MAX_SKALA_TEKSTU = 4;
constexpr size_t MAX_TEKST_WEWNETRZNY = 4096;

bool surowy_bufor_bmp_zmapowany = false;
bool bufor_tapety_zmapowany = false;

bool dodaj_u64(uint64_t a, uint64_t b, uint64_t* wynik) {
    if (!wynik) return false;
    if (a > UINT64_MAX - b) return false;
    *wynik = a + b;
    return true;
}

bool wyrownaj_do_strony(uint64_t wartosc, uint64_t* wynik) {
    if (!wynik) return false;
    if (wartosc > UINT64_MAX - (ROZMIAR_STRONY - 1ULL)) return false;
    *wynik = (wartosc + ROZMIAR_STRONY - 1ULL) &
             ~(ROZMIAR_STRONY - 1ULL);
    return true;
}

void przeladuj_cr3() {
    void* pml4 = PobierzAktualnePML4();
    if (!pml4) return;

    asm volatile(
        "mov %0, %%cr3"
        :
        : "r"(pml4)
        : "memory");
}

bool zmapuj_nowe_strony(uint64_t adres_wirtualny,
                        uint64_t rozmiar,
                        uint64_t flagi) {
    if (rozmiar == 0) return true;

    uint64_t rozmiar_mapowania = 0;
    if (!wyrownaj_do_strony(rozmiar, &rozmiar_mapowania))
        return false;

    for (uint64_t i = 0; i < rozmiar_mapowania; i += ROZMIAR_STRONY) {
        auto ramka = ZaalokujRamke();
        if (!ramka) return false;

        ZmapujStrone(
            reinterpret_cast<void*>(adres_wirtualny + i),
            ramka,
            flagi);
    }

    przeladuj_cr3();
    return true;
}

bool zmapuj_framebuffer(uint64_t adres_fizyczny,
                        uint64_t rozmiar,
                        uint64_t* adres_wirtualny_wynik) {
    if (!adres_wirtualny_wynik || rozmiar == 0)
        return false;

    const uint64_t baza_fizyczna =
        adres_fizyczny & ~(ROZMIAR_STRONY - 1ULL);

    const uint64_t przesuniecie =
        adres_fizyczny & (ROZMIAR_STRONY - 1ULL);

    uint64_t potrzebne = 0;
    if (!dodaj_u64(rozmiar, przesuniecie, &potrzebne))
        return false;

    uint64_t rozmiar_mapowania = 0;
    if (!wyrownaj_do_strony(potrzebne, &rozmiar_mapowania))
        return false;

    for (uint64_t i = 0; i < rozmiar_mapowania; i += ROZMIAR_STRONY) {
        ZmapujStrone(
            reinterpret_cast<void*>(VADDR_FRAMEBUFFER + i),
            reinterpret_cast<void*>(baza_fizyczna + i),
            0b11 | 0x10);
    }

    przeladuj_cr3();

    *adres_wirtualny_wynik =
        VADDR_FRAMEBUFFER + przesuniecie;

    return true;
}

uint16_t odczytaj_le16(const uint8_t* p) {
    if (!p) return 0;
    return static_cast<uint16_t>(
        static_cast<uint16_t>(p[0]) |
        (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t odczytaj_le32(const uint8_t* p) {
    if (!p) return 0;
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

int32_t odczytaj_le_i32(const uint8_t* p) {
    return static_cast<int32_t>(odczytaj_le32(p));
}

int ogranicz_skale(int skala) {
    if (skala < 1) return 1;
    if (skala > MAX_SKALA_TEKSTU) return MAX_SKALA_TEKSTU;
    return skala;
}

bool pid_ma_warstwe(int pid) {
    return pobierz_warstwe(pid) != nullptr;
}

int znajdz_pierwsza_warstwe_gui() {
    for (int pid = 0; pid < 16; ++pid) {
        if (pid_ma_warstwe(pid))
            return pid;
    }
    return -1;
}

} // namespace

// =========================================================================
// HAL EKRANU
// =========================================================================

class SterownikEkranu {
protected:
    uint32_t szerokosc = 0;
    uint32_t wysokosc = 0;
    uint32_t pitch = 0;
    uint8_t bpp = 0;
    volatile uint8_t* framebuffer_fizyczny = nullptr;

    void KopiujCalyBufor32(uint8_t* bufor) {
        if (!bufor || !framebuffer_fizyczny || bpp != 32)
            return;

        const uint64_t bajtow =
            static_cast<uint64_t>(pitch) *
            static_cast<uint64_t>(wysokosc);

        volatile uint32_t* dst =
            reinterpret_cast<volatile uint32_t*>(
                framebuffer_fizyczny);

        const uint32_t* src =
            reinterpret_cast<const uint32_t*>(bufor);

        const uint64_t slow = bajtow / sizeof(uint32_t);

        for (uint64_t i = 0; i < slow; ++i)
            dst[i] = src[i];
    }

    void KopiujFragment32(uint8_t* bufor,
                          int x,
                          int y,
                          int szer,
                          int wys) {
        if (!bufor || !framebuffer_fizyczny ||
            bpp != 32 || szer <= 0 || wys <= 0) {
            return;
        }

        int64_t start_x = static_cast<int64_t>(x);
        int64_t start_y = static_cast<int64_t>(y);
        int64_t end_x = start_x + static_cast<int64_t>(szer);
        int64_t end_y = start_y + static_cast<int64_t>(wys);

        if (start_x < 0) start_x = 0;
        if (start_y < 0) start_y = 0;

        if (end_x > static_cast<int64_t>(szerokosc))
            end_x = szerokosc;

        if (end_y > static_cast<int64_t>(wysokosc))
            end_y = wysokosc;

        if (start_x >= end_x || start_y >= end_y)
            return;

        const uint64_t bajtow_w_wierszu =
            static_cast<uint64_t>(end_x - start_x) * 4ULL;

        for (int64_t rzad = start_y; rzad < end_y; ++rzad) {
            const uint64_t offset =
                static_cast<uint64_t>(rzad) *
                    static_cast<uint64_t>(pitch) +
                static_cast<uint64_t>(start_x) * 4ULL;

            volatile uint64_t* dst64=reinterpret_cast<volatile uint64_t*>(framebuffer_fizyczny+offset);
            const uint64_t* src64=reinterpret_cast<const uint64_t*>(bufor+offset);
            const uint64_t slow64=bajtow_w_wierszu/8ULL;
            for(uint64_t i=0;i<slow64;++i)dst64[i]=src64[i];
            if(bajtow_w_wierszu&4ULL){
                volatile uint32_t* d32=reinterpret_cast<volatile uint32_t*>(framebuffer_fizyczny+offset+slow64*8ULL);
                const uint32_t* s32=reinterpret_cast<const uint32_t*>(bufor+offset+slow64*8ULL);*d32=*s32;
            }
        }
    }

public:
    virtual ~SterownikEkranu() {}

    virtual bool Inicjalizuj(uint64_t adres_lfb,
                             uint32_t w,
                             uint32_t h,
                             uint32_t p,
                             uint8_t b) {
        if (adres_lfb == 0 || w == 0 || h == 0 ||
            p == 0 || b != 32) {
            return false;
        }

        if (static_cast<uint64_t>(p) <
            static_cast<uint64_t>(w) * 4ULL) {
            return false;
        }

        if ((p & 3U) != 0)
            return false;

        szerokosc = w;
        wysokosc = h;
        pitch = p;
        bpp = b;

        framebuffer_fizyczny =
            reinterpret_cast<volatile uint8_t*>(adres_lfb);

        return true;
    }

    virtual void KopiujNaEkran(uint8_t* bufor) = 0;

    virtual void KopiujFragmentNaEkran(uint8_t* bufor,
                                       int x,
                                       int y,
                                       int szer,
                                       int wys) = 0;

    uint32_t PobierzSzerokosc() const { return szerokosc; }
    uint32_t PobierzWysokosc() const { return wysokosc; }
    uint32_t PobierzPitch() const { return pitch; }
    uint8_t PobierzBpp() const { return bpp; }

    void ZapiszPikselFramebuffer(int x, int y, uint32_t kolor) {
        if (!framebuffer_fizyczny || bpp != 32 || x < 0 || y < 0 ||
            x >= static_cast<int>(szerokosc) || y >= static_cast<int>(wysokosc))
            return;
        volatile uint32_t* dst = reinterpret_cast<volatile uint32_t*>(
            framebuffer_fizyczny + static_cast<uint64_t>(y) * pitch +
            static_cast<uint64_t>(x) * 4ULL);
        *dst = kolor;
    }
};

class SterownikVESA final : public SterownikEkranu {
public:
    void KopiujNaEkran(uint8_t* bufor) override {
        KopiujCalyBufor32(bufor);
    }

    void KopiujFragmentNaEkran(uint8_t* bufor,
                               int x,
                               int y,
                               int szer,
                               int wys) override {
        KopiujFragment32(bufor, x, y, szer, wys);
    }
};

class SterownikGOP final : public SterownikEkranu {
public:
    bool Inicjalizuj(uint64_t adres_lfb,
                     uint32_t w,
                     uint32_t h,
                     uint32_t p,
                     uint8_t b) override {
        if (!SterownikEkranu::Inicjalizuj(
                adres_lfb, w, h, p, b)) {
            return false;
        }

        InicjalizujGOP(adres_lfb, w, h, p, b);
        return true;
    }

    void KopiujNaEkran(uint8_t* bufor) override {
        KopiujCalyBufor32(bufor);
    }

    void KopiujFragmentNaEkran(uint8_t* bufor,
                               int x,
                               int y,
                               int szer,
                               int wys) override {
        KopiujFragment32(bufor, x, y, szer, wys);
    }
};

static SterownikEkranu* aktywny_ekran = nullptr;
static uint8_t* backbuffer = nullptr;
static uint32_t* bufor_tapety = nullptr;
static bool tapeta_zaladowana = false;

// -1 = stary tryb terminalowy jadra.
// >=0 = PID procesu, ktory ostatnio aktywowal GUI Ring 3.
static int pid_przejmujacy_mysz = -1;
static int aktywny_pid_gui = -1;
static int capture_pid_gui = -1;
static GuiDirtyRect oczekujacy_dirty[SKLADACZ_MAKS_WARSTW] = {};
static bool ma_oczekujacy_dirty[SKLADACZ_MAKS_WARSTW] = {};

static void zapamietaj_dirty_rysowania(int x,int y,int w,int h) {
    if(aktualny_pid<=0||aktualny_pid>=SKLADACZ_MAKS_WARSTW||w<=0||h<=0)return;
    GuiDirtyRect r{x,y,w,h};
    if(!ma_oczekujacy_dirty[aktualny_pid]){oczekujacy_dirty[aktualny_pid]=r;ma_oczekujacy_dirty[aktualny_pid]=true;return;}
    GuiDirtyRect&a=oczekujacy_dirty[aktualny_pid];int x0=a.x<r.x?a.x:r.x,y0=a.y<r.y?a.y:r.y;
    int x1=a.x+a.width>r.x+r.width?a.x+a.width:r.x+r.width;
    int y1=a.y+a.height>r.y+r.height?a.y+a.height:r.y+r.height;a={x0,y0,x1-x0,y1-y0};
}

static uint64_t znacznik_zdarzenia() {
    return hpet_dostepny() ? czas_monotoniczny_ns() : scheduler_pobierz_tick();
}

static bool tryb_skladania_obrazu = false;
static bool clip_terminala_aktywny = false;
static int clip_terminala_x0 = 0;
static int clip_terminala_y0 = 0;
static int clip_terminala_x1 = 0;
static int clip_terminala_y1 = 0;

#ifndef BURSZTYN_DEBUG_GUI_BOUNDS
#define BURSZTYN_DEBUG_GUI_BOUNDS 0
#endif

// =========================================================================
// DEKLARACJE LOKALNE
// =========================================================================

void UkryjKursor();
void PokazKursor();
void OdswiezEkran();
void PrzeniesNaEkran();
void PrzeniesFragmentNaEkran(int x, int y, int szer, int wys);
void PostawPiksel(int x, int y, uint32_t kolor);
void rysuj_zegar_rtc();

// =========================================================================
// STARY TERMINAL JADRA
// =========================================================================

struct Okno {
    int32_t x;
    int32_t y;
    int32_t szer;
    int32_t wys;

    int32_t stary_x;
    int32_t stary_y;
    int32_t stary_szer;
    int32_t stary_wys;

    const char* krotka_nazwa;
    const char* tytul;
    uint32_t kolor_tla;

    bool widoczne;
    bool zmaksymalizowane;
};

static Okno okna[1] = {
    {
        20, 20, 660, 360,
        0, 0, 0, 0,
        "Terminal",
        "Powłoka Bursztynowa",
        0x001A0B00,
        true,
        false
    }
};

static int z_order[1] = {0};

static int okno_przeciagane = -1;
static int chwyt_x = 0;
static int chwyt_y = 0;
static bool lewy_wcisniety = false;

struct ZnakTerminala {
    char znak;
    uint32_t kolor;
};

#define MAX_ROWS 80
#define MAX_COLS 140

static ZnakTerminala term_buf[MAX_ROWS][MAX_COLS];

static int term_r = 0;
static int term_c = 0;
static int term_max_r = 25;
static int term_max_c = 80;
static bool terminal_przewinieto = false;

// =========================================================================
// MYSZ I KURSOR
// =========================================================================

static int mysz_x = 500;
static int mysz_y = 300;

static uint32_t bufor_kursora[16][16];
static bool kursor_widoczny = false;

static const uint8_t kursor_bitmapa[16][16] = {
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,1,1,1,1,0,0,0,0},
    {1,2,2,1,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,1,0,1,2,2,2,1,0,0,0,0,0,0,0},
    {1,1,0,0,0,1,2,2,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0}
};

// =========================================================================
// SERIAL
// =========================================================================

static inline void serial_outb(uint16_t port, uint8_t val) {
    asm volatile(
        "outb %0, %1"
        :
        : "a"(val), "Nd"(port));
}

void SerialLog(const char* str) {
    if (!str) return;

    for (size_t i = 0; str[i] != '\0'; ++i)
        serial_outb(0x3F8, static_cast<uint8_t>(str[i]));
}

#ifndef BURSZTYN_DEBUG_GUI_INPUT
#define BURSZTYN_DEBUG_GUI_INPUT 0
#endif

#if BURSZTYN_DEBUG_GUI_INPUT
static void SerialLiczba(int v) {
    char b[16]; int n=0; unsigned x=v<0?static_cast<unsigned>(-v):static_cast<unsigned>(v);
    if(v<0)SerialLog("-"); do{b[n++]=static_cast<char>('0'+x%10U);x/=10U;}while(x&&n<15);
    while(n) { char s[2]={b[--n],'\0'}; SerialLog(s); }
}
#endif

// =========================================================================
// TERMINAL
// =========================================================================

static void AktualizujRozmiarTerminala() {
    if (okna[0].szer < 20 || okna[0].wys < 40) {
        term_max_c = 1;
        term_max_r = 1;
        return;
    }

    term_max_c = (okna[0].szer - 12) / 9;
    term_max_r = (okna[0].wys - 36) / 16;

    if (term_max_c < 1) term_max_c = 1;
    if (term_max_r < 1) term_max_r = 1;

    if (term_max_c > MAX_COLS) term_max_c = MAX_COLS;
    if (term_max_r > MAX_ROWS) term_max_r = MAX_ROWS;

    if (term_r >= term_max_r)
        term_r = term_max_r - 1;

    if (term_c >= term_max_c)
        term_c = term_max_c - 1;

    if (term_r < 0) term_r = 0;
    if (term_c < 0) term_c = 0;
}

static void PrzewinTerminalJesliTrzeba() {
    while (term_r >= term_max_r) {
        terminal_przewinieto = true;
        for (int r = 1; r < term_max_r; ++r) {
            for (int c = 0; c < term_max_c; ++c)
                term_buf[r - 1][c] = term_buf[r][c];
        }

        for (int c = 0; c < term_max_c; ++c) {
            term_buf[term_max_r - 1][c].znak = 0;
            term_buf[term_max_r - 1][c].kolor = 0;
        }

        term_r = term_max_r - 1;
    }
}

void DopiszDoBufora(const char* tekst, uint32_t kolor) {
    if (!tekst) return;

    AktualizujRozmiarTerminala();

    for (size_t i = 0; tekst[i] != '\0'; ++i) {
        const char znak = tekst[i];

        if (znak == '\n') {
            ++term_r;
            term_c = 0;
            PrzewinTerminalJesliTrzeba();
            continue;
        }

        if (znak == '\r') {
            term_c = 0;
            continue;
        }

        if (znak == '\b') {
            if (term_c > 0) {
                --term_c;

                term_buf[term_r][term_c].znak = 0;
                term_buf[term_r][term_c].kolor = 0;

                if (term_c > 0) {
                    const uint8_t poprzedni =
                        static_cast<uint8_t>(
                            term_buf[term_r][term_c - 1].znak);

                    if ((poprzedni & 0xE0U) == 0xC0U) {
                        --term_c;
                        term_buf[term_r][term_c].znak = 0;
                        term_buf[term_r][term_c].kolor = 0;
                    }
                }
            }
            continue;
        }

        if (term_r < 0 || term_r >= term_max_r)
            PrzewinTerminalJesliTrzeba();

        if (term_c < 0) term_c = 0;

        if (term_c >= term_max_c) {
            ++term_r;
            term_c = 0;
            PrzewinTerminalJesliTrzeba();
        }

        term_buf[term_r][term_c].znak = znak;
        term_buf[term_r][term_c].kolor = kolor;
        ++term_c;

        if (term_c >= term_max_c) {
            ++term_r;
            term_c = 0;
            PrzewinTerminalJesliTrzeba();
        }
    }
}

static void OznaczDirtyTerminalaPoDopisaniu(int stary_r) {
    if (pid_przejmujacy_mysz != -1 || !okna[0].widoczne)
        return;

    if (terminal_przewinieto) {
        skladacz_obrazu_oznacz_dirty_rect(
            okna[0].x, okna[0].y, okna[0].szer, okna[0].wys);
        return;
    }

    int pierwszy_r = stary_r < term_r ? stary_r : term_r;
    int ostatni_r = stary_r > term_r ? stary_r : term_r;
    if (pierwszy_r < 0) pierwszy_r = 0;
    if (ostatni_r >= term_max_r) ostatni_r = term_max_r - 1;

    skladacz_obrazu_oznacz_dirty_rect(
        okna[0].x + 6,
        okna[0].y + 32 + pierwszy_r * 16,
        okna[0].szer - 12,
        (ostatni_r - pierwszy_r + 1) * 16);
}

void wypisz_log(const char* tekst) {
    if (!tekst) return;

    SerialLog(tekst);
    SerialLog("\n");
}

void klog_serial(const char* tekst) {
    wypisz_log(tekst);
}

void boot_log_gui(const char* tekst) {
    if (!tekst || pid_przejmujacy_mysz != -1)
        return;

    if (!backbuffer || !aktywny_ekran)
        return;

    UkryjKursor();

    DopiszDoBufora(tekst, 0x00FFBF00);
    DopiszDoBufora("\n", 0x00FFBF00);

    OdswiezEkran();
    PokazKursor();
    PrzeniesNaEkran();
}

// =========================================================================
// PREZENTACJA EKRANU
// =========================================================================

void PrzeniesNaEkran() {
    if (!backbuffer || !aktywny_ekran)
        return;

    aktywny_ekran->KopiujNaEkran(backbuffer);
}

void PrzeniesFragmentNaEkran(int x,
                             int y,
                             int szer,
                             int wys) {
    if (!backbuffer || !aktywny_ekran)
        return;

    aktywny_ekran->KopiujFragmentNaEkran(
        backbuffer, x, y, szer, wys);
}

int grafika_pobierz_szerokosc() {
    if (!aktywny_ekran) return 0;

    const uint32_t szer =
        aktywny_ekran->PobierzSzerokosc();

    if (szer > static_cast<uint32_t>(INT32_MAX))
        return INT32_MAX;

    return static_cast<int>(szer);
}

int grafika_pobierz_wysokosc() {
    if (!aktywny_ekran) return 0;

    const uint32_t wys =
        aktywny_ekran->PobierzWysokosc();

    if (wys > static_cast<uint32_t>(INT32_MAX))
        return INT32_MAX;

    return static_cast<int>(wys);
}

uint32_t* grafika_pobierz_wiersz_backbuffer(int y) {
    if(!backbuffer||!aktywny_ekran||y<0||y>=grafika_pobierz_wysokosc())return nullptr;
    return reinterpret_cast<uint32_t*>(backbuffer+
        static_cast<uint64_t>(y)*aktywny_ekran->PobierzPitch());
}

// =========================================================================
// SUROWE OPERACJE COMPOSITORA
// =========================================================================

static inline void ZapiszPikselBackbuffer(int x,
                                         int y,
                                         uint32_t kolor) {
    if (!backbuffer || !aktywny_ekran)
        return;

    if (x < 0 || y < 0)
        return;

    if (x >= static_cast<int>(aktywny_ekran->PobierzSzerokosc()) ||
        y >= static_cast<int>(aktywny_ekran->PobierzWysokosc())) {
        return;
    }

    const uint64_t offset =
        static_cast<uint64_t>(y) *
            static_cast<uint64_t>(aktywny_ekran->PobierzPitch()) +
        static_cast<uint64_t>(x) * 4ULL;

    *reinterpret_cast<uint32_t*>(backbuffer + offset) = kolor;
}

void grafika_rozpocznij_skladanie() {
    if (!backbuffer || !aktywny_ekran)
        return;

    UkryjKursor();
    tryb_skladania_obrazu = true;
}

void grafika_odtworz_tlo_skladania() {
    if (!backbuffer || !aktywny_ekran)
        return;

    const int szer = grafika_pobierz_szerokosc();
    const int wys = grafika_pobierz_wysokosc();

    if (szer <= 0 || wys <= 0)
        return;

    const uint32_t pitch =
        aktywny_ekran->PobierzPitch();

    if (tapeta_zaladowana && bufor_tapety) {
        for (int y = 0; y < wys; ++y) {
            uint32_t* dst =
                reinterpret_cast<uint32_t*>(
                    backbuffer +
                    static_cast<uint64_t>(y) * pitch);

            const uint32_t* src =
                bufor_tapety +
                static_cast<uint64_t>(y) *
                    static_cast<uint64_t>(szer);

            for (int x = 0; x < szer; ++x)
                dst[x] = src[x];
        }
    } else {
        for (int y = 0; y < wys; ++y) {
            uint32_t* dst =
                reinterpret_cast<uint32_t*>(
                    backbuffer +
                    static_cast<uint64_t>(y) * pitch);

            for (int x = 0; x < szer; ++x)
                dst[x] = 0x001A0B00;
        }
    }
}

void grafika_odtworz_tlo_regionu(int x, int y, int szer, int wys) {
    if (!backbuffer || !aktywny_ekran || szer <= 0 || wys <= 0) return;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + szer, y1 = y + wys;
    const int ekran_szer = grafika_pobierz_szerokosc();
    const int ekran_wys = grafika_pobierz_wysokosc();
    if (x1 > ekran_szer) x1 = ekran_szer;
    if (y1 > ekran_wys) y1 = ekran_wys;
    if (x0 >= x1 || y0 >= y1) return;
    const uint32_t pitch = aktywny_ekran->PobierzPitch();
    for (int py = y0; py < y1; ++py) {
        uint32_t* dst = reinterpret_cast<uint32_t*>(backbuffer +
            static_cast<uint64_t>(py) * pitch) + x0;
        if (tapeta_zaladowana && bufor_tapety) {
            const uint32_t* src = bufor_tapety +
                static_cast<uint64_t>(py) * ekran_szer + x0;
            for (int px = x0; px < x1; ++px) *dst++ = *src++;
        } else {
            for (int px = x0; px < x1; ++px) *dst++ = 0x001A0B00;
        }
    }
}

void grafika_zapisz_surowy_piksel(int x,
                                   int y,
                                   uint32_t kolor) {
    /*
     * Ta funkcja jest wywolywana dla kazdego piksela warstwy przez
     * compositor. Nie przelaczamy trybu dla kazdego piksela.
     */
    ZapiszPikselBackbuffer(x, y, kolor);
}

void grafika_zakoncz_skladanie() {
    if (!backbuffer || !aktywny_ekran) {
        tryb_skladania_obrazu = false;
        return;
    }

    PokazKursor();
    PrzeniesNaEkran();
    tryb_skladania_obrazu = false;
}

void grafika_naloz_kursor_regionu(int x, int y, int szer, int wys) {
    if (!backbuffer || !aktywny_ekran || szer <= 0 || wys <= 0) return;
    const int x1=x+szer, y1=y+wys;
    for (int cy=0;cy<16;++cy) for(int cx=0;cx<16;++cx) {
        const int px=mysz_x+cx, py=mysz_y+cy;
        if(px<x||py<y||px>=x1||py>=y1) continue;
        const uint8_t typ=kursor_bitmapa[cy][cx];
        if(typ==1) ZapiszPikselBackbuffer(px,py,0x00000000);
        else if(typ==2) ZapiszPikselBackbuffer(px,py,0x00FFFFFF);
    }
}

void grafika_zakoncz_skladanie_regionu(int x,int y,int szer,int wys) {
    if (!backbuffer || !aktywny_ekran) { tryb_skladania_obrazu=false; return; }
    grafika_naloz_kursor_regionu(x,y,szer,wys);
    PrzeniesFragmentNaEkran(x,y,szer,wys);
    tryb_skladania_obrazu=false;
    kursor_widoczny=false; /* region compositor nie korzysta z save/restore */
}

void grafika_prezentuj_region(int x,int y,int szer,int wys){PrzeniesFragmentNaEkran(x,y,szer,wys);}

void grafika_prezentuj_kursor(){
    if(!aktywny_ekran)return;
    const int sw=grafika_pobierz_szerokosc(),sh=grafika_pobierz_wysokosc();
    for(int cy=0;cy<16;++cy)for(int cx=0;cx<16;++cx){int px=mysz_x+cx,py=mysz_y+cy;if(px<0||py<0||px>=sw||py>=sh)continue;uint8_t typ=kursor_bitmapa[cy][cx];if(!typ)continue;aktywny_ekran->ZapiszPikselFramebuffer(px,py,typ==1?0x00000000:0x00FFFFFF);}
}

void grafika_zakoncz_scene(){tryb_skladania_obrazu=false;kursor_widoczny=false;}

void grafika_pobierz_pozycje_kursora(int* x,int* y) {
    if(x) *x=mysz_x;
    if(y) *y=mysz_y;
}

// =========================================================================
// PODSTAWOWE RYSOWANIE
// =========================================================================

void PostawPiksel(int x, int y, uint32_t kolor) {
    /* Tryb compositora nalezy do PID 0. Po wywlaszczeniu PID 0 proces
       Ring 3 nadal musi pisac do swojej warstwy, nie do backbufferu. */
    if (!tryb_skladania_obrazu || aktualny_pid != 0) {
        warstwa_obrazu* warstwa =
            pobierz_warstwe(aktualny_pid);

        if (warstwa) {
            const int64_t lokalny_x =
                static_cast<int64_t>(x) -
                static_cast<int64_t>(warstwa->x);

            const int64_t lokalny_y =
                static_cast<int64_t>(y) -
                static_cast<int64_t>(warstwa->y);

            if (lokalny_x >= 0 &&
                lokalny_y >= 0 &&
                lokalny_x < warstwa->szerokosc &&
                lokalny_y < warstwa->wysokosc &&
                warstwa->bufor_pikseli) {

                /*
                 * 0x00000000 jest maska przezroczystosci.
                 * Dla czerni ustawiamy nieuzywany w XRGB bajt 24,
                 * dzieki czemu wizualnie nadal jest czarna.
                 */
                if (kolor == 0x00000000)
                    kolor = 0x01000000;

                const uint64_t indeks =
                    static_cast<uint64_t>(lokalny_y) *
                        static_cast<uint64_t>(warstwa->szerokosc) +
                    static_cast<uint64_t>(lokalny_x);

                warstwa->bufor_pikseli[indeks] = kolor;
            }
#if BURSZTYN_DEBUG_GUI_BOUNDS
            else {
                SerialLog("[GUI] OOB write do warstwy\n");
            }
#endif

            return;
        }
    }

    if (clip_terminala_aktywny &&
        (x < clip_terminala_x0 || x >= clip_terminala_x1 ||
         y < clip_terminala_y0 || y >= clip_terminala_y1)) {
        return;
    }

    ZapiszPikselBackbuffer(x, y, kolor);
}

uint32_t PobierzPiksel(int x, int y) {
    if (!backbuffer || !aktywny_ekran)
        return 0;

    if (x < 0 || y < 0)
        return 0;

    if (x >= static_cast<int>(aktywny_ekran->PobierzSzerokosc()) ||
        y >= static_cast<int>(aktywny_ekran->PobierzWysokosc())) {
        return 0;
    }

    const uint64_t offset =
        static_cast<uint64_t>(y) *
            static_cast<uint64_t>(aktywny_ekran->PobierzPitch()) +
        static_cast<uint64_t>(x) * 4ULL;

    return *reinterpret_cast<uint32_t*>(backbuffer + offset);
}

struct ObszarRysowania {
    int64_t min_x;
    int64_t min_y;
    int64_t max_x;
    int64_t max_y;
};

static bool PobierzObszarRysowania(ObszarRysowania* wynik) {
    if (!wynik || !aktywny_ekran)
        return false;

    if (!tryb_skladania_obrazu) {
        warstwa_obrazu* warstwa =
            pobierz_warstwe(aktualny_pid);

        if (warstwa &&
            warstwa->szerokosc > 0 &&
            warstwa->wysokosc > 0) {

            wynik->min_x = warstwa->x;
            wynik->min_y = warstwa->y;
            wynik->max_x =
                static_cast<int64_t>(warstwa->x) +
                static_cast<int64_t>(warstwa->szerokosc);
            wynik->max_y =
                static_cast<int64_t>(warstwa->y) +
                static_cast<int64_t>(warstwa->wysokosc);

            return true;
        }
    }

    wynik->min_x = 0;
    wynik->min_y = 0;
    wynik->max_x = aktywny_ekran->PobierzSzerokosc();
    wynik->max_y = aktywny_ekran->PobierzWysokosc();

    return true;
}

void RysujProstokat(int px,
                    int py,
                    int szer,
                    int wys,
                    uint32_t kolor) {
    if (!aktywny_ekran || szer <= 0 || wys <= 0)
        return;

    ObszarRysowania obszar;
    if (!PobierzObszarRysowania(&obszar))
        return;

    int64_t start_x = px;
    int64_t start_y = py;

    int64_t end_x =
        static_cast<int64_t>(px) +
        static_cast<int64_t>(szer);

    int64_t end_y =
        static_cast<int64_t>(py) +
        static_cast<int64_t>(wys);

    if (start_x < obszar.min_x) start_x = obszar.min_x;
    if (start_y < obszar.min_y) start_y = obszar.min_y;
    if (end_x > obszar.max_x) end_x = obszar.max_x;
    if (end_y > obszar.max_y) end_y = obszar.max_y;

    if (start_x < INT32_MIN) start_x = INT32_MIN;
    if (start_y < INT32_MIN) start_y = INT32_MIN;
    if (end_x > static_cast<int64_t>(INT32_MAX) + 1LL)
        end_x = static_cast<int64_t>(INT32_MAX) + 1LL;
    if (end_y > static_cast<int64_t>(INT32_MAX) + 1LL)
        end_y = static_cast<int64_t>(INT32_MAX) + 1LL;

    if (start_x >= end_x || start_y >= end_y)
        return;

    for (int64_t y = start_y; y < end_y; ++y) {
        for (int64_t x = start_x; x < end_x; ++x) {
            PostawPiksel(
                static_cast<int>(x),
                static_cast<int>(y),
                kolor);
        }
    }
}

void RysujZnak(uint32_t unicode,
               int px,
               int py,
               uint32_t kolor_tekstu,
               uint32_t kolor_tla,
               bool przezroczyste_tlo,
               int skala) {
    skala = ogranicz_skale(skala);

    const uint16_t* glyph = nullptr;

    const uint32_t max_znaki =
        sizeof(nowa_czcionka_16x16) /
        sizeof(nowa_czcionka_16x16[0]);

    int szerokosc = 8;

    if (unicode < max_znaki) {
        glyph = nowa_czcionka_16x16[unicode];
        szerokosc = nowa_czcionka_szerokosci[unicode];
    } else {
        glyph = nowa_czcionka_16x16[0];
    }

    if (!glyph)
        return;

    if (szerokosc < 1) szerokosc = 1;
    if (szerokosc > 16) szerokosc = 16;

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < szerokosc; ++x) {
            const bool zmaluj =
                (glyph[y] & (1U << (15 - x))) != 0;

            if (!zmaluj && przezroczyste_tlo)
                continue;

            const uint32_t kolor =
                zmaluj ? kolor_tekstu : kolor_tla;

            for (int sy = 0; sy < skala; ++sy) {
                for (int sx = 0; sx < skala; ++sx) {
                    const int64_t dx =
                        static_cast<int64_t>(px) +
                        static_cast<int64_t>(x * skala + sx);

                    const int64_t dy =
                        static_cast<int64_t>(py) +
                        static_cast<int64_t>(y * skala + sy);

                    if (dx < INT32_MIN || dx > INT32_MAX ||
                        dy < INT32_MIN || dy > INT32_MAX) {
                        continue;
                    }

                    PostawPiksel(
                        static_cast<int>(dx),
                        static_cast<int>(dy),
                        kolor);
                }
            }
        }
    }
}

void WypiszTekst(const char* tekst,
                 int px,
                 int py,
                 uint32_t kolor_tekstu,
                 int skala) {
    if (!tekst) return;

    skala = ogranicz_skale(skala);

    int64_t pozycja_x = px;
    size_t i = 0;

    const uint32_t max_znaki =
        sizeof(nowa_czcionka_16x16) /
        sizeof(nowa_czcionka_16x16[0]);

    while (i < MAX_TEKST_WEWNETRZNY &&
           tekst[i] != '\0') {

        uint32_t unicode =
            static_cast<uint8_t>(tekst[i]);

        const uint8_t b0 =
            static_cast<uint8_t>(tekst[i]);

        if ((b0 & 0xE0U) == 0xC0U &&
            i + 1 < MAX_TEKST_WEWNETRZNY &&
            tekst[i + 1] != '\0') {

            const uint8_t b1 =
                static_cast<uint8_t>(tekst[i + 1]);

            if ((b1 & 0xC0U) == 0x80U) {
                unicode =
                    (static_cast<uint32_t>(b0 & 0x1FU) << 6) |
                    static_cast<uint32_t>(b1 & 0x3FU);
                ++i;
            }
        }

        if (pozycja_x >= INT32_MIN &&
            pozycja_x <= INT32_MAX) {
            RysujZnak(
                unicode,
                static_cast<int>(pozycja_x),
                py,
                kolor_tekstu,
                0,
                true,
                skala);
        }

        int szerokosc_znaku = 8;

        if (unicode < max_znaki)
            szerokosc_znaku =
                nowa_czcionka_szerokosci[unicode];

        if (szerokosc_znaku < 1) szerokosc_znaku = 1;
        if (szerokosc_znaku > 16) szerokosc_znaku = 16;

        pozycja_x +=
            static_cast<int64_t>(
                (szerokosc_znaku + 1) * skala);

        ++i;
    }
}

// =========================================================================
// STARE OKNO TERMINALA
// =========================================================================

void RysujOkno(int id) {
    if (id < 0 || id >= 1)
        return;

    if (!okna[id].widoczne)
        return;

    const int px = okna[id].x;
    const int py = okna[id].y;
    const int szer = okna[id].szer;
    const int wys = okna[id].wys;

    if (szer < 10 || wys < 40)
        return;

    const bool aktywne =
        (z_order[0] == id);

    const uint32_t kolor_paska =
        aktywne ? 0x00FFBF00 : 0x008A5A00;

    const uint32_t kolor_tekstu_paska =
        aktywne ? 0x001A0B00 : 0x00D1D5DB;

    RysujProstokat(px, py, szer, wys, 0x008A5A00);
    RysujProstokat(px + 2, py + 2, szer - 4, 24, kolor_paska);

    WypiszTekst(
        okna[id].tytul,
        px + 8,
        py + 4,
        kolor_tekstu_paska,
        1);

    const int min_btn_x =
        px + szer - 74;

    RysujProstokat(
        min_btn_x,
        py + 4,
        20,
        20,
        0x00E58A00);

    WypiszTekst(
        "-",
        min_btn_x + 4,
        py + 6,
        0x001A0B00,
        1);

    RysujProstokat(
        px + szer - 50,
        py + 4,
        20,
        20,
        0x00E58A00);

    WypiszTekst(
        okna[id].zmaksymalizowane ? "v" : "^",
        px + szer - 46,
        py + 6,
        0x001A0B00,
        1);

    RysujProstokat(
        px + szer - 26,
        py + 4,
        20,
        20,
        0x00AA0000);

    WypiszTekst(
        "X",
        px + szer - 22,
        py + 6,
        0x00FFFFFF,
        1);

    RysujProstokat(
        px + 2,
        py + 28,
        szer - 4,
        wys - 30,
        okna[id].kolor_tla);
}

void RysujTekstZBufora(ZnakTerminala buf[][MAX_COLS],
                       int max_r,
                       int max_c,
                       int r_cursor,
                       int c_cursor,
                       int px,
                       int py,
                       int szer,
                       int wys,
                       bool rysuj_kursor,
                       bool aktywne_okno) {
    if (!buf || max_r <= 0 || max_c <= 0 ||
        szer <= 0 || wys <= 0) {
        return;
    }

    if (max_r > MAX_ROWS) max_r = MAX_ROWS;
    if (max_c > MAX_COLS) max_c = MAX_COLS;

    const int skala = 1;
    const int wysokosc_linii = 16;

    const int start_x = px + 6;
    const int start_y = py + 32;

    const uint32_t max_znaki =
        sizeof(nowa_czcionka_16x16) /
        sizeof(nowa_czcionka_16x16[0]);

    for (int r = 0; r < max_r; ++r) {
        int cx = start_x;
        const int cy =
            start_y + r * wysokosc_linii;

        if (cy + wysokosc_linii >= py + wys)
            break;

        for (int c = 0; c < max_c; ++c) {
            const char z = buf[r][c].znak;
            const uint32_t kolor = buf[r][c].kolor;

            if (z != 0) {
                uint32_t unicode =
                    static_cast<uint8_t>(z);

                const uint8_t b0 =
                    static_cast<uint8_t>(z);

                if ((b0 & 0xE0U) == 0xC0U &&
                    c + 1 < max_c &&
                    buf[r][c + 1].znak != 0) {

                    const uint8_t b1 =
                        static_cast<uint8_t>(
                            buf[r][c + 1].znak);

                    if ((b1 & 0xC0U) == 0x80U) {
                        unicode =
                            (static_cast<uint32_t>(
                                b0 & 0x1FU) << 6) |
                            static_cast<uint32_t>(
                                b1 & 0x3FU);

                        ++c;
                    }
                }

                if (cx + 9 >= px + szer - 6)
                    break;

                RysujZnak(
                    unicode,
                    cx,
                    cy,
                    kolor,
                    0,
                    true,
                    skala);

                int szerokosc_znaku = 8;

                if (unicode < max_znaki)
                    szerokosc_znaku =
                        nowa_czcionka_szerokosci[unicode];

                if (szerokosc_znaku < 1) szerokosc_znaku = 1;
                if (szerokosc_znaku > 16) szerokosc_znaku = 16;

                cx += szerokosc_znaku + 1;
            } else {
                cx += 9;
            }
        }
    }

    if (!rysuj_kursor || !aktywne_okno)
        return;

    if (r_cursor < 0 || r_cursor >= max_r ||
        c_cursor < 0 || c_cursor > max_c) {
        return;
    }

    int cx = start_x;

    for (int c = 0; c < c_cursor; ++c) {
        const char z =
            buf[r_cursor][c].znak;

        if (z != 0) {
            uint32_t unicode =
                static_cast<uint8_t>(z);

            const uint8_t b0 =
                static_cast<uint8_t>(z);

            if ((b0 & 0xE0U) == 0xC0U &&
                c + 1 < max_c &&
                buf[r_cursor][c + 1].znak != 0) {

                const uint8_t b1 =
                    static_cast<uint8_t>(
                        buf[r_cursor][c + 1].znak);

                if ((b1 & 0xC0U) == 0x80U) {
                    unicode =
                        (static_cast<uint32_t>(
                            b0 & 0x1FU) << 6) |
                        static_cast<uint32_t>(
                            b1 & 0x3FU);
                    ++c;
                }
            }

            int szerokosc_znaku = 8;

            if (unicode < max_znaki)
                szerokosc_znaku =
                    nowa_czcionka_szerokosci[unicode];

            if (szerokosc_znaku < 1) szerokosc_znaku = 1;
            if (szerokosc_znaku > 16) szerokosc_znaku = 16;

            cx += szerokosc_znaku + 1;
        } else {
            cx += 9;
        }
    }

    const int cy =
        start_y + r_cursor * wysokosc_linii;

    if (cy + 14 < py + wys &&
        cx + 9 < px + szer) {
        RysujProstokat(
            cx,
            cy + 14,
            9,
            2,
            0x00FFBF00);
    }
}

void rysuj_zegar_rtc() {
    if (!aktywny_ekran)
        return;

    if (aktywny_ekran->PobierzSzerokosc() < 150 ||
        aktywny_ekran->PobierzWysokosc() < 40) {
        return;
    }

    czas_rtc czas;
    pobierz_czas_rtc(&czas);

    char bufor_czasu[16] = {};
    formatuj_czas_do_stringa(
        &czas,
        bufor_czasu);

    const int zegar_x =
        static_cast<int>(
            aktywny_ekran->PobierzSzerokosc()) - 150;

    const int pasek_y =
        static_cast<int>(
            aktywny_ekran->PobierzWysokosc()) - 40;

    const int zegar_y =
        static_cast<int>(
            aktywny_ekran->PobierzWysokosc()) - 32;

    RysujProstokat(
        zegar_x,
        pasek_y,
        150,
        40,
        0x001A0B00);

    RysujProstokat(
        zegar_x,
        pasek_y,
        150,
        2,
        0x00E58A00);

    WypiszTekst(
        bufor_czasu,
        zegar_x + 10,
        zegar_y,
        0x00FFBF00,
        2);
}

void OdswiezEkran() {
    if (!backbuffer || !aktywny_ekran)
        return;

    /*
     * Shell jest oknem kernela nalozonym na wielozadaniowy pulpit. Jezeli
     * istnieja warstwy GUI, pelna klatke musi zaczac compositor; inaczej
     * stara sciezka wyczyscilaby ikonki i pasek zadan.
     */
    if (pid_przejmujacy_mysz == -1 &&
        znajdz_pierwsza_warstwe_gui() != -1) {

        skladacz_obrazu_oznacz_dirty();
        return;
    }

    /*
     * OdswiezEkran jest stara sciezka jadra.
     * Musi omijac przekierowanie PostawPiksel do warstwy aktualnego PID.
     */
    const bool poprzedni_tryb =
        tryb_skladania_obrazu;

    tryb_skladania_obrazu = true;

    grafika_odtworz_tlo_skladania();

    for (int k = 0; k < 1; ++k) {
        const int i = z_order[k];

        if (i < 0 || i >= 1)
            continue;

        if (!okna[i].widoczne)
            continue;

        RysujOkno(i);

        if (i == 0) {
            RysujTekstZBufora(
                term_buf,
                term_max_r,
                term_max_c,
                term_r,
                term_c,
                okna[0].x,
                okna[0].y,
                okna[0].szer,
                okna[0].wys,
                false,
                false);
        }
    }

    if (aktywny_ekran->PobierzWysokosc() >= 40) {
        if (pid_przejmujacy_mysz == -1) {
            const int pasek_y =
                static_cast<int>(
                    aktywny_ekran->PobierzWysokosc()) - 40;

            RysujProstokat(
                0,
                pasek_y,
                static_cast<int>(
                    aktywny_ekran->PobierzSzerokosc()),
                40,
                0x001A0B00);

            RysujProstokat(
                0,
                pasek_y,
                static_cast<int>(
                    aktywny_ekran->PobierzSzerokosc()),
                2,
                0x00E58A00);

            WypiszTekst(
                "Bursztyn OS - Terminal",
                20,
                static_cast<int>(
                    aktywny_ekran->PobierzWysokosc()) - 28,
                0x008A5A00,
                1);
        }

        rysuj_zegar_rtc();
    }

    tryb_skladania_obrazu =
        poprzedni_tryb;
}

extern "C" void grafika_naloz_okno_terminala_region(int x,
                                                       int y,
                                                       int szer,
                                                       int wys) {
    if (!backbuffer ||
        !aktywny_ekran ||
        pid_przejmujacy_mysz != -1 ||
        !okna[0].widoczne ||
        szer <= 0 || wys <= 0) {

        return;
    }

    const int64_t x1_64 = static_cast<int64_t>(x) + szer;
    const int64_t y1_64 = static_cast<int64_t>(y) + wys;
    clip_terminala_x0 = x < 0 ? 0 : x;
    clip_terminala_y0 = y < 0 ? 0 : y;
    clip_terminala_x1 = x1_64 > INT32_MAX ? INT32_MAX : static_cast<int>(x1_64);
    clip_terminala_y1 = y1_64 > INT32_MAX ? INT32_MAX : static_cast<int>(y1_64);
    clip_terminala_aktywny = true;

    RysujOkno(0);

    RysujTekstZBufora(
        term_buf,
        term_max_r,
        term_max_c,
        term_r,
        term_c,
        okna[0].x,
        okna[0].y,
        okna[0].szer,
        okna[0].wys,
        false,
        false
    );

    clip_terminala_aktywny = false;
}

// =========================================================================
// KLAWIATURA
// =========================================================================

extern "C" bool zaktualizuj_klawiature_gui(char znak) {
    if (aktywny_pid_gui <= 0)
        return false;
    bws_zdarzenie e{};
    e.typ = BWS_ZDARZENIE_KLAWISZ;
    e.kod = static_cast<uint8_t>(znak);
    e.timestamp = znacznik_zdarzenia();
#if BURSZTYN_DEBUG_GUI_INPUT
    SerialLog("[KEY] char="); SerialLiczba(static_cast<uint8_t>(znak));
    SerialLog(" focus_pid="); SerialLiczba(aktywny_pid_gui); SerialLog("\n");
#endif
    (void)scheduler_dodaj_zdarzenie(aktywny_pid_gui, &e);
    /* Aktywny model GUI jest wylacznym odbiorca. Nawet przepelnienie jego
       kolejki nie moze spowodowac double-delivery do legacy byte-streamu. */
    return true;
}

// =========================================================================
// KURSOR
// =========================================================================

void UkryjKursor() {
    if (!kursor_widoczny ||
        !backbuffer ||
        !aktywny_ekran) {
        return;
    }

    const bool poprzedni_tryb =
        tryb_skladania_obrazu;

    tryb_skladania_obrazu = true;

    const int szer =
        static_cast<int>(
            aktywny_ekran->PobierzSzerokosc());

    const int wys =
        static_cast<int>(
            aktywny_ekran->PobierzWysokosc());

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int px = mysz_x + x;
            const int py = mysz_y + y;

            if (px < 0 || py < 0 ||
                px >= szer || py >= wys) {
                continue;
            }

            ZapiszPikselBackbuffer(
                px,
                py,
                bufor_kursora[y][x]);
        }
    }

    kursor_widoczny = false;
    tryb_skladania_obrazu =
        poprzedni_tryb;
}

void PokazKursor() {
    if (kursor_widoczny ||
        !backbuffer ||
        !aktywny_ekran) {
        return;
    }

    const bool poprzedni_tryb =
        tryb_skladania_obrazu;

    tryb_skladania_obrazu = true;

    const int szer =
        static_cast<int>(
            aktywny_ekran->PobierzSzerokosc());

    const int wys =
        static_cast<int>(
            aktywny_ekran->PobierzWysokosc());

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int px = mysz_x + x;
            const int py = mysz_y + y;

            if (px < 0 || py < 0 ||
                px >= szer || py >= wys) {
                continue;
            }

            bufor_kursora[y][x] =
                PobierzPiksel(px, py);

            const uint8_t typ =
                kursor_bitmapa[y][x];

            if (typ == 1) {
                ZapiszPikselBackbuffer(
                    px, py, 0x00000000);
            } else if (typ == 2) {
                ZapiszPikselBackbuffer(
                    px, py, 0x00FFFFFF);
            }
        }
    }

    kursor_widoczny = true;
    tryb_skladania_obrazu =
        poprzedni_tryb;
}

// =========================================================================
// OBSLUGA MYSZY
// =========================================================================

static void OgraniczOkno(Okno& o) {
    if (o.zmaksymalizowane || !aktywny_ekran)
        return;

    const int32_t ekran_szer =
        static_cast<int32_t>(
            aktywny_ekran->PobierzSzerokosc());

    const int32_t ekran_wys =
        static_cast<int32_t>(
            aktywny_ekran->PobierzWysokosc());

    int64_t min_x =
        -static_cast<int64_t>(o.szer) + 80;

    int64_t max_x =
        static_cast<int64_t>(ekran_szer) - 80;

    const int64_t min_y = 0;

    int64_t max_y =
        static_cast<int64_t>(ekran_wys) - 40;

    if (max_x < min_x)
        max_x = min_x;

    if (max_y < min_y)
        max_y = min_y;

    if (o.x < min_x)
        o.x = static_cast<int32_t>(min_x);

    if (o.x > max_x)
        o.x = static_cast<int32_t>(max_x);

    if (o.y < min_y)
        o.y = static_cast<int32_t>(min_y);

    if (o.y > max_y)
        o.y = static_cast<int32_t>(max_y);
}

static void NaprawWlascicielaMyszyJesliProcesZniknal() {
    if (pid_przejmujacy_mysz < 0)
        return;

    if (pid_ma_warstwe(pid_przejmujacy_mysz))
        return;

    pid_przejmujacy_mysz =
        znajdz_pierwsza_warstwe_gui();

    if (pid_przejmujacy_mysz == -1)
        okna[0].widoczne = true;
}

static int znajdz_warstwe_pod_punktem(int x, int y) {
    const int overlay = skladacz_obrazu_overlay_pod_punktem(x, y);
    if (overlay >= 0) return overlay;
    int wynik = -1;
    int najlepsze_z = -2147483647 - 1;
    for (int pid = 1; pid < SKLADACZ_MAKS_WARSTW; ++pid) {
        warstwa_obrazu* w = pobierz_warstwe(pid);
        if (!w || x < w->x || y < w->y ||
            static_cast<int64_t>(x) >= static_cast<int64_t>(w->x) + w->szerokosc ||
            static_cast<int64_t>(y) >= static_cast<int64_t>(w->y) + w->wysokosc)
            continue;
        if (wynik < 0 || w->z_order > najlepsze_z ||
            (w->z_order == najlepsze_z && pid > wynik)) {
            wynik = pid;
            najlepsze_z = w->z_order;
        }
    }
    return wynik;
}

static void ustaw_focus_gui(int pid) {
    if (pid == aktywny_pid_gui) return;
    const int stary = aktywny_pid_gui;
    aktywny_pid_gui = pid;
#if BURSZTYN_DEBUG_GUI_INPUT
    SerialLog("[FOCUS] old="); SerialLiczba(stary);
    SerialLog(" new="); SerialLiczba(pid); SerialLog("\n");
#endif
    bws_zdarzenie e{};
    e.timestamp = znacznik_zdarzenia();
    if (stary > 0) {
        e.typ = BWS_ZDARZENIE_BLUR;
        scheduler_dodaj_zdarzenie(stary, &e);
    }
    if (pid > 0) {
        e.typ = BWS_ZDARZENIE_FOCUS;
        scheduler_dodaj_zdarzenie(pid, &e);
    }
    if (pid > 0) bws_gui_powiadom_lifecycle(BWS_ZDARZENIE_OKNO_FOCUS, pid);
}

extern "C" void bws_gui_powiadom_lifecycle(uint32_t typ, int pid) {
    int manager = -1;
    int najnizsze_z = 2147483647;
    for (int i = 1; i < SKLADACZ_MAKS_WARSTW; ++i) {
        warstwa_obrazu* w = pobierz_warstwe(i);
        if (w && w->z_order < najnizsze_z) {
            manager = i;
            najnizsze_z = w->z_order;
        }
    }
    if (manager <= 0 ||
        (manager == pid && typ == BWS_ZDARZENIE_OKNO_FOCUS))
        return;
    bws_zdarzenie e{};
    e.typ = typ;
    e.kod = static_cast<uint32_t>(pid);
    e.timestamp = znacznik_zdarzenia();
    scheduler_dodaj_zdarzenie(manager, &e);
}

extern "C" void zaktualizuj_mysze(int dx,
                                  int dy,
                                  uint8_t przyciski) {
    if (!backbuffer || !aktywny_ekran)
        return;

    NaprawWlascicielaMyszyJesliProcesZniknal();

    const int stary_mysz_x = mysz_x;
    const int stary_mysz_y = mysz_y;

    int64_t nowy_x =
        static_cast<int64_t>(mysz_x) +
        static_cast<int64_t>(dx);

    int64_t nowy_y =
        static_cast<int64_t>(mysz_y) -
        static_cast<int64_t>(dy);

    const int ekran_szer =
        static_cast<int>(
            aktywny_ekran->PobierzSzerokosc());

    const int ekran_wys =
        static_cast<int>(
            aktywny_ekran->PobierzWysokosc());

    if (nowy_x < 0) nowy_x = 0;
    if (nowy_y < 0) nowy_y = 0;

    if (nowy_x >= ekran_szer)
        nowy_x = ekran_szer - 1;

    if (nowy_y >= ekran_wys)
        nowy_y = ekran_wys - 1;

    mysz_x = static_cast<int>(nowy_x);
    mysz_y = static_cast<int>(nowy_y);

    if (pid_przejmujacy_mysz != -1) {
        lewy_wcisniety =
            (przyciski & 0x01U) != 0;

        static uint8_t poprzednie_przyciski_gui = 0;
        const bool klik = (przyciski & 1U) != 0 &&
                          (poprzednie_przyciski_gui & 1U) == 0;
        const bool pusc = (przyciski & 1U) == 0 &&
                          (poprzednie_przyciski_gui & 1U) != 0;
        if (klik) {
            /* Nowa sekwencja przycisku nie moze odziedziczyc capture po
               poprzednim, niedokonczonym drag. Focus i adresat DOWN musza
               wskazywac ten sam hit-testowany proces. */
            capture_pid_gui = -1;
            const int trafiony = znajdz_warstwe_pod_punktem(mysz_x, mysz_y);
            skladacz_obrazu_podnies_warstwe(trafiony);
            ustaw_focus_gui(trafiony);
        }
        const int cel = capture_pid_gui > 0 ? capture_pid_gui : aktywny_pid_gui;
        if (cel > 0) {
            bws_zdarzenie e{};
            e.typ = klik ? BWS_ZDARZENIE_MYSZ_DOWN :
                    (pusc ? BWS_ZDARZENIE_MYSZ_UP : BWS_ZDARZENIE_MYSZ_RUCH);
            e.x = mysz_x; e.y = mysz_y; e.dx = dx; e.dy = -dy;
            e.przyciski = przyciski;
            e.timestamp = znacznik_zdarzenia();
            scheduler_dodaj_zdarzenie(cel, &e);
        }
        if (pusc) capture_pid_gui = -1;
        poprzednie_przyciski_gui = przyciski;

        skladacz_obrazu_oznacz_ruch_kursora(
            stary_mysz_x, stary_mysz_y, mysz_x, mysz_y);
        WybudzProcesyOczekujaceNaMysz();
        return;
    }

    const bool nowy_lewy =
        (przyciski & 0x01U) != 0;

    const bool klik_lewy =
        nowy_lewy && !lewy_wcisniety;

    const bool puszcz_lewy =
        !nowy_lewy && lewy_wcisniety;

    bool wymaga_odrysowania = false;
    const int stary_okno_x = okna[0].x;
    const int stary_okno_y = okna[0].y;
    const int stary_okno_szer = okna[0].szer;
    const int stary_okno_wys = okna[0].wys;

    if (klik_lewy && okno_przeciagane == -1) {
        for (int k = 0; k >= 0; --k) {
            const int i = z_order[k];

            if (i < 0 || i >= 1)
                continue;

            if (!okna[i].widoczne)
                continue;

            const int px = okna[i].x;
            const int py = okna[i].y;
            const int sz = okna[i].szer;
            const int wy = okna[i].wys;

            const bool w_oknie =
                mysz_x >= px &&
                mysz_x < px + sz &&
                mysz_y >= py &&
                mysz_y < py + wy;

            if (!w_oknie)
                continue;

            wymaga_odrysowania = true;

            if (mysz_y < py + 26) {
                const int zamknij_x =
                    px + sz - 26;

                if (mysz_x >= zamknij_x &&
                    mysz_x < zamknij_x + 20) {
                    okna[i].widoczne = false;

                    if (i == 0)
                        flaga_zamkniecia_powloki = true;

                    scheduler_wybudz_klawiature();

                    break;
                }

                const int min_btn_x =
                    px + sz - 74;

                if (mysz_x >= min_btn_x &&
                    mysz_x < min_btn_x + 20) {
                    okna[i].widoczne = false;

                    if (i == 0)
                        flaga_zamkniecia_powloki = true;

                    scheduler_wybudz_klawiature();

                    break;
                }

                const int max_btn_x =
                    px + sz - 50;

                if (mysz_x >= max_btn_x &&
                    mysz_x < max_btn_x + 20) {

                    if (!okna[i].zmaksymalizowane) {
                        okna[i].stary_x = okna[i].x;
                        okna[i].stary_y = okna[i].y;
                        okna[i].stary_szer = okna[i].szer;
                        okna[i].stary_wys = okna[i].wys;

                        okna[i].x = 0;
                        okna[i].y = 0;
                        okna[i].szer = ekran_szer;
                        okna[i].wys =
                            ekran_wys > 40 ?
                            ekran_wys - 40 :
                            ekran_wys;

                        okna[i].zmaksymalizowane = true;
                    } else {
                        okna[i].x = okna[i].stary_x;
                        okna[i].y = okna[i].stary_y;
                        okna[i].szer = okna[i].stary_szer;
                        okna[i].wys = okna[i].stary_wys;
                        okna[i].zmaksymalizowane = false;
                    }

                    if (i == 0)
                        AktualizujRozmiarTerminala();

                    break;
                }

                const int drag_max =
                    px + sz - 79;

                if (!okna[i].zmaksymalizowane &&
                    mysz_x < drag_max) {
                    okno_przeciagane = i;
                    chwyt_x = mysz_x - okna[i].x;
                    chwyt_y = mysz_y - okna[i].y;
                }
            }

            break;
        }
    } else if (puszcz_lewy &&
               okno_przeciagane != -1) {
        okno_przeciagane = -1;
    }

    if (okno_przeciagane != -1 &&
        nowy_lewy &&
        (dx != 0 || dy != 0)) {

        const int i = okno_przeciagane;

        okna[i].x =
            mysz_x - chwyt_x;

        okna[i].y =
            mysz_y - chwyt_y;

        OgraniczOkno(okna[i]);
        wymaga_odrysowania = true;
    }

    lewy_wcisniety = nowy_lewy;

    WybudzProcesyOczekujaceNaMysz();

    if (wymaga_odrysowania) {
        /* Odbuduj zarowno stary, jak i nowy obszar okna. Prezentacja
           nalezy wylacznie do PID 0; IRQ myszy nie dotyka framebufferu. */
        skladacz_obrazu_oznacz_dirty_rect(
            stary_okno_x, stary_okno_y, stary_okno_szer, stary_okno_wys);
        if (okna[0].widoczne) {
            skladacz_obrazu_oznacz_dirty_rect(
                okna[0].x, okna[0].y, okna[0].szer, okna[0].wys);
        }
    }

    skladacz_obrazu_oznacz_ruch_kursora(
        stary_mysz_x, stary_mysz_y, mysz_x, mysz_y);
}

extern "C" void obsluga_przerwania_zegara() {
    /* Tylko bounded enqueue; RTC i renderowanie pozostaja poza IRQ. */
    scheduler_zarejestruj_irq_timera();
    static uint64_t nastepny_zegar_ns = 0;
    static uint32_t fallback_ticki = 0;
    bool termin = false;
    if (hpet_dostepny()) {
        const uint64_t teraz = czas_monotoniczny_ns();
        if (nastepny_zegar_ns == 0) nastepny_zegar_ns = teraz + 1000000000ULL;
        if (teraz >= nastepny_zegar_ns) {
            termin = true;
            nastepny_zegar_ns = teraz + 1000000000ULL;
        }
    } else if (++fallback_ticki >= 100U) {
        fallback_ticki = 0; termin = true;
    }
    if (!termin) return;
    skladacz_obrazu_oznacz_dirty_rect(
        grafika_pobierz_szerokosc()-150,
        grafika_pobierz_wysokosc()-40,150,40);
    int desktop_pid = -1;
    int najnizsze_z = 2147483647;
    for (int pid = 1; pid < SKLADACZ_MAKS_WARSTW; ++pid) {
        warstwa_obrazu* w = pobierz_warstwe(pid);
        if (w && w->z_order < najnizsze_z) {
            najnizsze_z = w->z_order;
            desktop_pid = pid;
        }
    }
    if (desktop_pid > 0) {
        bws_zdarzenie e{};
        e.typ = BWS_ZDARZENIE_TIMER;
        e.timestamp = znacznik_zdarzenia();
        scheduler_dodaj_zdarzenie(desktop_pid, &e);
    }
}

// =========================================================================
// TAPETA BMP
// =========================================================================

extern "C" void wczytaj_tapete_z_dysku() {
    if (!aktywny_ekran || !backbuffer)
        return;

    wypisz_log(
        "[GRAFIKA] Proba wczytania tapeta.bmp z dysku AHCI (LBA 10)...");

    if (!surowy_bufor_bmp_zmapowany) {
        if (!zmapuj_nowe_strony(
                VADDR_RAW_BMP,
                MAX_RAW_BMP,
                0b11)) {
            wypisz_log(
                "[GRAFIKA] BLAD: Nie mozna zmapowac bufora BMP.");
            return;
        }

        surowy_bufor_bmp_zmapowany = true;
    }

    uint8_t* raw_bmp =
        reinterpret_cast<uint8_t*>(VADDR_RAW_BMP);

    if (!czytaj_z_glownego_dysku_ahci(
            10, 1, raw_bmp)) {
        wypisz_log(
            "[GRAFIKA] BLAD: Nie mozna odczytac naglowka BMP.");
        return;
    }

    if (raw_bmp[0] != 'B' ||
        raw_bmp[1] != 'M') {
        wypisz_log(
            "[GRAFIKA] BLAD: Plik tapety nie jest BMP.");
        return;
    }

    const uint32_t rozmiar_pliku =
        odczytaj_le32(raw_bmp + 2);

    if (rozmiar_pliku < 54 ||
        rozmiar_pliku > MAX_RAW_BMP) {
        wypisz_log(
            "[GRAFIKA] BLAD: Niepoprawny rozmiar BMP.");
        return;
    }

    const uint32_t sektory_do_odczytu =
        (rozmiar_pliku + 511U) / 512U;

    uint32_t przeczytane = 1;
    uint64_t lba = 11;

    while (przeczytane < sektory_do_odczytu) {
        uint32_t paczka =
            sektory_do_odczytu - przeczytane;

        if (paczka > 32)
            paczka = 32;

        if (!czytaj_z_glownego_dysku_ahci(
                lba,
                paczka,
                raw_bmp +
                    static_cast<uint64_t>(przeczytane) * 512ULL)) {
            wypisz_log(
                "[GRAFIKA] BLAD: Nie mozna odczytac calego BMP.");
            return;
        }

        lba += paczka;
        przeczytane += paczka;
    }

    const uint32_t dib_size =
        odczytaj_le32(raw_bmp + 14);

    if (dib_size < 40) {
        wypisz_log(
            "[GRAFIKA] BLAD: Nieobslugiwany naglowek BMP.");
        return;
    }

    const uint32_t piksele_offset =
        odczytaj_le32(raw_bmp + 10);

    const int32_t bmp_szerokosc =
        odczytaj_le_i32(raw_bmp + 18);

    const int32_t bmp_wysokosc_raw =
        odczytaj_le_i32(raw_bmp + 22);

    const uint16_t planes =
        odczytaj_le16(raw_bmp + 26);

    const uint16_t bmp_bpp =
        odczytaj_le16(raw_bmp + 28);

    const uint32_t compression =
        odczytaj_le32(raw_bmp + 30);

    if (planes != 1 ||
        compression != 0 ||
        bmp_szerokosc <= 0 ||
        bmp_wysokosc_raw == 0 ||
        bmp_wysokosc_raw == INT32_MIN ||
        (bmp_bpp != 24 && bmp_bpp != 32)) {
        wypisz_log(
            "[GRAFIKA] BLAD: Nieobslugiwany format BMP.");
        return;
    }

    const bool rysuj_od_gory =
        bmp_wysokosc_raw < 0;

    const int32_t bmp_wysokosc =
        rysuj_od_gory ?
        -bmp_wysokosc_raw :
        bmp_wysokosc_raw;

    const uint64_t bajtow_na_piksel =
        static_cast<uint64_t>(bmp_bpp / 8);

    const uint64_t bity_wiersza =
        static_cast<uint64_t>(
            static_cast<uint32_t>(bmp_szerokosc)) *
        static_cast<uint64_t>(bmp_bpp);

    const uint64_t rzad_bajtow =
        ((bity_wiersza + 31ULL) / 32ULL) * 4ULL;

    uint64_t dane_pikseli = 0;

    if (!dodaj_u64(
            static_cast<uint64_t>(piksele_offset),
            rzad_bajtow *
                static_cast<uint64_t>(
                    static_cast<uint32_t>(bmp_wysokosc)),
            &dane_pikseli) ||
        dane_pikseli > rozmiar_pliku) {
        wypisz_log(
            "[GRAFIKA] BLAD: BMP ma uszkodzone dane pikseli.");
        return;
    }

    const uint64_t ekran_szer =
        aktywny_ekran->PobierzSzerokosc();

    const uint64_t ekran_wys =
        aktywny_ekran->PobierzWysokosc();

    uint64_t ilosc_pikseli_ekranu =
        ekran_szer * ekran_wys;

    if (ilosc_pikseli_ekranu >
        UINT64_MAX / sizeof(uint32_t)) {
        return;
    }

    const uint64_t waga_tapety_bajty =
        ilosc_pikseli_ekranu *
        sizeof(uint32_t);

    if (!bufor_tapety_zmapowany) {
        if (!zmapuj_nowe_strony(
                VADDR_TAPETA,
                waga_tapety_bajty,
                0b11)) {
            wypisz_log(
                "[GRAFIKA] BLAD: Nie mozna przydzielic bufora tapety.");
            return;
        }

        bufor_tapety_zmapowany = true;
    }

    bufor_tapety =
        reinterpret_cast<uint32_t*>(VADDR_TAPETA);

    for (uint64_t i = 0;
         i < ilosc_pikseli_ekranu;
         ++i) {
        bufor_tapety[i] = 0x000A0500;
    }

    const int copy_w =
        bmp_szerokosc <
                static_cast<int32_t>(ekran_szer) ?
        bmp_szerokosc :
        static_cast<int>(ekran_szer);

    const int copy_h =
        bmp_wysokosc <
                static_cast<int32_t>(ekran_wys) ?
        bmp_wysokosc :
        static_cast<int>(ekran_wys);

    const int src_start_x =
        (bmp_szerokosc - copy_w) / 2;

    const int src_start_y =
        (bmp_wysokosc - copy_h) / 2;

    const int dst_start_x =
        (static_cast<int>(ekran_szer) - copy_w) / 2;

    const int dst_start_y =
        (static_cast<int>(ekran_wys) - copy_h) / 2;

    for (int dy = 0; dy < copy_h; ++dy) {
        const int logiczny_y =
            src_start_y + dy;

        const int plik_y =
            rysuj_od_gory ?
            logiczny_y :
            (bmp_wysokosc - 1 - logiczny_y);

        const uint64_t baza_wiersza =
            static_cast<uint64_t>(piksele_offset) +
            static_cast<uint64_t>(plik_y) *
                rzad_bajtow;

        for (int dx = 0; dx < copy_w; ++dx) {
            const int src_x =
                src_start_x + dx;

            const uint64_t offset_piksela =
                baza_wiersza +
                static_cast<uint64_t>(src_x) *
                    bajtow_na_piksel;

            if (offset_piksela +
                    bajtow_na_piksel >
                rozmiar_pliku) {
                continue;
            }

            const uint8_t* piksel =
                raw_bmp + offset_piksela;

            const uint32_t kolor =
                (static_cast<uint32_t>(piksel[2]) << 16) |
                (static_cast<uint32_t>(piksel[1]) << 8) |
                static_cast<uint32_t>(piksel[0]);

            const int dst_x =
                dst_start_x + dx;

            const int dst_y =
                dst_start_y + dy;

            bufor_tapety[
                static_cast<uint64_t>(dst_y) *
                    ekran_szer +
                static_cast<uint64_t>(dst_x)] =
                kolor;
        }
    }

    tapeta_zaladowana = true;

    wypisz_log(
        "[GRAFIKA] Pulpit i tapeta gotowe!");

    if (pid_przejmujacy_mysz == -1 && aktualny_pid == 0) {
        UkryjKursor();
        OdswiezEkran();
        PokazKursor();
        PrzeniesNaEkran();
    } else {
        skladacz_obrazu_oznacz_dirty();
    }
}

// =========================================================================
// WYJSCIE TEKSTOWE JADRA
// =========================================================================

extern "C" void wypisz_na_ekranie(const char* tekst) {
    if (!tekst || !backbuffer || !aktywny_ekran)
        return;

    const int stary_r = term_r;
    terminal_przewinieto = false;
    DopiszDoBufora(
        tekst,
        0x00E58A00);

    /*
     * Shell jest legacy terminalem kernela, ale od startu compositora nie
     * wolno mu prezentowac backbufferu samodzielnie. Stara sekwencja
     * UkryjKursor/OdswiezEkran/PokazKursor/PrzeniesNaEkran kopiowala scene
     * w trakcie oczekujacej kompozycji i zapisywala kursor myszy w scene
     * backbufferze. Kolejny cursor restore odtwarzal potem jego stare kopie.
     */
    OznaczDirtyTerminalaPoDopisaniu(stary_r);
}

// =========================================================================
// INICJALIZACJA GRAFIKI
// =========================================================================

alignas(SterownikVESA) static
uint8_t st_vesa_pamiec[sizeof(SterownikVESA)];

alignas(SterownikGOP) static
uint8_t st_gop_pamiec[sizeof(SterownikGOP)];

void InicjalizujGrafike(uint64_t adres_mb2) {
    if (adres_mb2 == 0)
        return;

    const uint32_t rozmiar =
        *reinterpret_cast<uint32_t*>(adres_mb2);

    if (rozmiar < 16)
        return;

    const uint64_t koniec =
        adres_mb2 + static_cast<uint64_t>(rozmiar);

    if (koniec < adres_mb2)
        return;

    uint64_t aktualny =
        adres_mb2 + 8ULL;

    uint64_t LFB = 0;
    uint32_t SZER = 0;
    uint32_t WYS = 0;
    uint32_t PITCH = 0;
    uint8_t BPP = 0;

    bool mamy_efi = false;

    while (aktualny + 8ULL <= koniec) {
        TagFramebufferMB2* tag =
            reinterpret_cast<TagFramebufferMB2*>(aktualny);

        if (tag->typ == 0)
            break;

        if (tag->rozmiar < 8)
            break;

        if (tag->typ == 11 ||
            tag->typ == 12) {
            mamy_efi = true;
        }

        if (tag->typ == 8) {
            LFB = tag->adres_fizyczny;
            PITCH = tag->pitch;
            SZER = tag->szerokosc;
            WYS = tag->wysokosc;
            BPP = tag->bpp;
        }

        const uint64_t krok =
            (static_cast<uint64_t>(tag->rozmiar) + 7ULL) &
            ~7ULL;

        if (krok == 0 ||
            aktualny > UINT64_MAX - krok) {
            break;
        }

        const uint64_t nastepny =
            aktualny + krok;

        if (nastepny <= aktualny ||
            nastepny > koniec) {
            break;
        }

        aktualny = nastepny;
    }

    if (LFB == 0 ||
        SZER == 0 ||
        WYS == 0 ||
        PITCH == 0) {
        SerialLog(
            "[GRAFIKA] BLAD KRYTYCZNY: Brak LFB w Multiboot2!\n");

        while (true)
            asm volatile("cli; hlt");
    }

    /*
     * Obecny renderer Bursztyna operuje na uint32_t/XRGB.
     * Nie probujemy udawac obslugi 24 BPP, bo prowadziloby to
     * do nadpisywania pamieci przy kopiowaniu tapety/backbufferu.
     */
    if (BPP != 32 ||
        SZER > static_cast<uint32_t>(INT32_MAX) ||
        WYS > static_cast<uint32_t>(INT32_MAX) ||
        SZER < 64 ||
        WYS < 64 ||
        static_cast<uint64_t>(PITCH) <
            static_cast<uint64_t>(SZER) * 4ULL ||
        (PITCH & 3U) != 0) {

        SerialLog(
            "[GRAFIKA] BLAD KRYTYCZNY: Wymagany poprawny framebuffer 32 BPP.\n");

        while (true)
            asm volatile("cli; hlt");
    }

    const uint64_t lfb_waga =
        static_cast<uint64_t>(PITCH) *
        static_cast<uint64_t>(WYS);

    uint64_t framebuffer_wirtualny = 0;

    if (!zmapuj_framebuffer(
            LFB,
            lfb_waga,
            &framebuffer_wirtualny)) {

        SerialLog(
            "[GRAFIKA] BLAD KRYTYCZNY: Nie mozna zmapowac framebufferu.\n");

        while (true)
            asm volatile("cli; hlt");
    }

    if (!zmapuj_nowe_strony(
            VADDR_BACKBUFFER,
            lfb_waga,
            0b11)) {

        SerialLog(
            "[GRAFIKA] BLAD KRYTYCZNY: Brak pamieci na backbuffer.\n");

        while (true)
            asm volatile("cli; hlt");
    }

    backbuffer =
        reinterpret_cast<uint8_t*>(
            VADDR_BACKBUFFER);

    if (mamy_efi) {
        aktywny_ekran =
            new (st_gop_pamiec)
                SterownikGOP();

        SerialLog(
            "[HAL] Aktywacja sterownika UEFI GOP.\n");
    } else {
        aktywny_ekran =
            new (st_vesa_pamiec)
                SterownikVESA();

        SerialLog(
            "[HAL] Aktywacja sterownika VESA VBE.\n");
    }

    if (!aktywny_ekran ||
        !aktywny_ekran->Inicjalizuj(
            framebuffer_wirtualny,
            SZER,
            WYS,
            PITCH,
            BPP)) {

        SerialLog(
            "[GRAFIKA] BLAD KRYTYCZNY: Sterownik ekranu odrzucil tryb graficzny.\n");

        while (true)
            asm volatile("cli; hlt");
    }

    for (uint64_t i = 0; i < lfb_waga; ++i)
        backbuffer[i] = 0;

    if (okna[0].x < 0 ||
        okna[0].x >= static_cast<int32_t>(SZER)) {
        okna[0].x = 20;
    }

    if (okna[0].y < 0 ||
        okna[0].y >= static_cast<int32_t>(WYS)) {
        okna[0].y = 20;
    }

    const int32_t maks_szer =
        static_cast<int32_t>(SZER) -
        okna[0].x - 20;

    const int32_t maks_wys =
        static_cast<int32_t>(WYS) -
        okna[0].y - 40;

    if (maks_szer >= 100 &&
        okna[0].szer > maks_szer) {
        okna[0].szer = maks_szer;
    }

    if (maks_wys >= 80 &&
        okna[0].wys > maks_wys) {
        okna[0].wys = maks_wys;
    }

    AktualizujRozmiarTerminala();

    if (mysz_x >= static_cast<int>(SZER))
        mysz_x = static_cast<int>(SZER) / 2;

    if (mysz_y >= static_cast<int>(WYS))
        mysz_y = static_cast<int>(WYS) / 2;

    wypisz_log(
        "[SYSTEM] Bursztyn OS HAL gotowy!");
}

// =========================================================================
// BWS GUI - RING 3
// =========================================================================

static bool BwsProcesMaWarstwe() {
    return pobierz_warstwe(aktualny_pid) != nullptr;
}

static bool BwsPoprawnyWymiar(int w, int h) {
    if (!aktywny_ekran || w <= 0 || h <= 0)
        return false;

    const uint64_t max_w =
        static_cast<uint64_t>(
            aktywny_ekran->PobierzSzerokosc()) * 2ULL;

    const uint64_t max_h =
        static_cast<uint64_t>(
            aktywny_ekran->PobierzWysokosc()) * 2ULL;

    if (static_cast<uint64_t>(
            static_cast<uint32_t>(w)) > max_w ||
        static_cast<uint64_t>(
            static_cast<uint32_t>(h)) > max_h) {
        return false;
    }

    return true;
}

extern "C" void bws_gui_rysuj_okno(int x,
                                   int y,
                                   int szer,
                                   int wys,
                                   const char* tytul) {
    if (!backbuffer || !aktywny_ekran ||
        !BwsProcesMaWarstwe() ||
        !BwsPoprawnyWymiar(szer, wys) ||
        szer < 10 ||
        wys < 30) {
        return;
    }

    char tytul_bezp[64] = {};

    if (!skopiuj_string_z_uzytkownika(
            tytul_bezp,
            tytul,
            sizeof(tytul_bezp))) {
        return;
    }

    if (skladacz_obrazu_ustaw_tytul(aktualny_pid, tytul_bezp))
        bws_gui_powiadom_lifecycle(BWS_ZDARZENIE_OKNO_TYTUL, aktualny_pid);

    RysujProstokat(
        x,
        y,
        szer,
        wys,
        0x008A5A00);

    RysujProstokat(
        x + 2,
        y + 2,
        szer - 4,
        24,
        0x00FFBF00);

    WypiszTekst(
        tytul_bezp,
        x + 8,
        y + 4,
        0x001A0B00,
        1);

    RysujProstokat(
        x + 2,
        y + 28,
        szer - 4,
        wys - 30,
        0x00280F00);
    zapamietaj_dirty_rysowania(x,y,szer,wys);
}

extern "C" void bws_gui_wypisz_tekst(int x,
                                     int y,
                                     const char* text) {
    if (!backbuffer || !aktywny_ekran ||
        !BwsProcesMaWarstwe()) {
        return;
    }

    char text_bezp[256] = {};

    if (!skopiuj_string_z_uzytkownika(
            text_bezp,
            text,
            sizeof(text_bezp))) {
        return;
    }

    WypiszTekst(
        text_bezp,
        x,
        y,
        0x00D1D5DB,
        1);
    int dl=0;while(text_bezp[dl]&&dl<255)++dl;
    zapamietaj_dirty_rysowania(x,y,dl*9,18);
}

extern "C" void bws_gui_wyczyscz_obszar(int x,
                                        int y,
                                        int szer,
                                        int wys) {
    if (!backbuffer || !aktywny_ekran ||
        !BwsProcesMaWarstwe() ||
        !BwsPoprawnyWymiar(szer, wys)) {
        return;
    }

    RysujProstokat(
        x,
        y,
        szer,
        wys,
        0x00280F00);
    zapamietaj_dirty_rysowania(x,y,szer,wys);
}

extern "C" void bws_gui_odswiez() {
    if (!backbuffer || !aktywny_ekran)
        return;

    if (!BwsProcesMaWarstwe())
        return;

    if(ma_oczekujacy_dirty[aktualny_pid]){
        const GuiDirtyRect r=oczekujacy_dirty[aktualny_pid];ma_oczekujacy_dirty[aktualny_pid]=false;
        skladacz_obrazu_oznacz_dirty_rect(r.x,r.y,r.width,r.height);
    }
}

extern "C" void bws_gui_pobierz_mysz(int* x,
                                     int* y,
                                     uint8_t* przyciski) {
    if (!backbuffer ||
        !x || !y || !przyciski) {
        return;
    }

    /*
     * Najpierw walidujemy wszystkie trzy cele, aby nie wykonac
     * polowicznego zapisu.
     */
    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            x, sizeof(int)) ||
        !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            y, sizeof(int)) ||
        !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            przyciski, sizeof(uint8_t))) {
        return;
    }

    const int lokalny_x = mysz_x;
    const int lokalny_y = mysz_y;

    const uint8_t lokalne_przyciski =
        lewy_wcisniety ? 1U : 0U;

    if (!skopiuj_do_przestrzeni_uzytkownika(
            x,
            &lokalny_x,
            sizeof(lokalny_x))) {
        return;
    }

    if (!skopiuj_do_przestrzeni_uzytkownika(
            y,
            &lokalny_y,
            sizeof(lokalny_y))) {
        return;
    }

    skopiuj_do_przestrzeni_uzytkownika(
        przyciski,
        &lokalne_przyciski,
        sizeof(lokalne_przyciski));
}

extern "C" void bws_gui_odswiez_pulpit() {
    if (!backbuffer || !aktywny_ekran)
        return;

    warstwa_obrazu* warstwa =
        pobierz_warstwe(aktualny_pid);

    if (!warstwa)
        return;

    wyczysc_warstwe(aktualny_pid);
    ma_oczekujacy_dirty[aktualny_pid]=false;
}

extern "C" void bws_gui_wypisz_tekst_kolor(
    int x,
    int y,
    uint64_t kolor_skala,
    const char* text) {

    if (!backbuffer || !aktywny_ekran ||
        !BwsProcesMaWarstwe()) {
        return;
    }

    const uint32_t kolor =
        static_cast<uint32_t>(
            kolor_skala & 0xFFFFFFFFULL);

    int skala =
        static_cast<int>(
            (kolor_skala >> 32) &
            0xFFFFFFFFULL);

    if (skala == 0)
        skala = 1;

    if (skala < 1 ||
        skala > MAX_SKALA_TEKSTU) {
        return;
    }

    char text_bezp[256] = {};

    if (!skopiuj_string_z_uzytkownika(
            text_bezp,
            text,
            sizeof(text_bezp))) {
        return;
    }

    WypiszTekst(
        text_bezp,
        x,
        y,
        kolor,
        skala);
    int dl=0;while(text_bezp[dl]&&dl<255)++dl;
    zapamietaj_dirty_rysowania(x,y,dl*9*skala,18*skala);
}

extern "C" void bws_gui_rysuj_prostokat(
    int x,
    int y,
    int w,
    int h,
    uint32_t kolor) {

    if (!backbuffer || !aktywny_ekran ||
        !BwsProcesMaWarstwe() ||
        !BwsPoprawnyWymiar(w, h)) {
        return;
    }

    RysujProstokat(
        x,
        y,
        w,
        h,
        kolor);
    zapamietaj_dirty_rysowania(x,y,w,h);
}

extern "C" void bws_gui_ustaw_przejecie_myszy(bool stan) {
    if (stan) {
        if (!BwsProcesMaWarstwe())
            return;

        if (okna[0].widoczne) {
            skladacz_obrazu_oznacz_dirty_rect(
                okna[0].x, okna[0].y, okna[0].szer, okna[0].wys);
        }

        pid_przejmujacy_mysz = aktualny_pid;
        /* Nowo uruchomione okno staje sie aktywne. Jest to rowniez jego
           pierwsze zdarzenie, potrzebne aplikacjom renderujacym po wejściu
           do petli eventowej (np. Notatnik). */
        ustaw_focus_gui(aktualny_pid);
        skladacz_obrazu_podnies_warstwe(aktualny_pid);

        okna[0].widoczne = false;
        return;
    }

    /*
     * Wylaczenie trybu GUI jest globalnym przejsciem do starej powloki.
     * Zachowujemy to ABI dla menedzera okien / shell.bur.
     */
    pid_przejmujacy_mysz = -1;
    flaga_zamkniecia_powloki = false;
    okna[0].widoczne = true;
    skladacz_obrazu_oznacz_dirty_rect(
        okna[0].x, okna[0].y, okna[0].szer, okna[0].wys);
}

extern "C" void bws_gui_ustaw_capture(bool stan) {
    if (stan) {
        if (aktualny_pid == aktywny_pid_gui && BwsProcesMaWarstwe())
            capture_pid_gui = aktualny_pid;
    } else if (capture_pid_gui == aktualny_pid) {
        capture_pid_gui = -1;
    }
}

extern "C" void bws_gui_usun_stan_procesu(int pid) {
    if(pid>0&&pid<SKLADACZ_MAKS_WARSTW)ma_oczekujacy_dirty[pid]=false;
    if (capture_pid_gui == pid) capture_pid_gui = -1;
    if (aktywny_pid_gui == pid) {
        aktywny_pid_gui = -1;
        int best=-1,best_z=-2147483647;
        for(int i=1;i<SKLADACZ_MAKS_WARSTW;++i){warstwa_obrazu*w=pobierz_warstwe(i);if(w&&(w->z_order>best_z||(w->z_order==best_z&&i>best))){best=i;best_z=w->z_order;}}
        if(best>0)ustaw_focus_gui(best);
    }
}

extern "C" int bws_gui_aktywny_pid() { return aktywny_pid_gui; }

extern "C" void bws_gui_aktywuj_warstwe(int pid) {
    if(pid<=0||!pobierz_warstwe(pid))return;
    ustaw_focus_gui(pid);
    skladacz_obrazu_podnies_warstwe(pid);
}

extern "C" bool bws_gui_minimalizuj_warstwe(int pid) {
    if (pid <= 0 || !skladacz_obrazu_minimalizuj(pid)) return false;
    if (capture_pid_gui == pid) capture_pid_gui = -1;
    if (aktywny_pid_gui == pid) {
        ustaw_focus_gui(-1);
        int best = -1, best_z = -2147483647;
        for (int i = 1; i < SKLADACZ_MAKS_WARSTW; ++i) {
            warstwa_obrazu* w = pobierz_warstwe(i);
            if (w && (w->z_order > best_z ||
                      (w->z_order == best_z && i > best))) {
                best = i; best_z = w->z_order;
            }
        }
        if (best > 0) ustaw_focus_gui(best);
    }
    bws_gui_powiadom_lifecycle(BWS_ZDARZENIE_OKNO_ZMINIMALIZOWANE, pid);
    return true;
}

extern "C" bool bws_gui_aktywuj_okno(uint64_t window_id) {
    if (!skladacz_obrazu_przywroc(window_id)) return false;
    const int pid = static_cast<int>(static_cast<uint32_t>(window_id));
    skladacz_obrazu_podnies_warstwe(pid);
    ustaw_focus_gui(pid);
    bws_gui_powiadom_lifecycle(BWS_ZDARZENIE_OKNO_PRZYWROCONE, pid);
    return true;
}

extern "C" void bws_gui_zwolnij_mysz_procesu(int pid) {
    if (pid < 0 ||
        (pid_przejmujacy_mysz != pid &&
         pid_przejmujacy_mysz != -1)) {

        return;
    }

    /*
     * owner == -1 obejmuje konczaca sie powloke terminalowa, ktora nie ma
     * warstwy. Po jej wyjsciu fokus powinien wrocic do pozostalego pulpitu.
     */

    pid_przejmujacy_mysz =
        znajdz_pierwsza_warstwe_gui();

    okna[0].widoczne =
        pid_przejmujacy_mysz == -1;
}

extern "C" void bws_gui_pobierz_rozdzielczosc(
    int* szer,
    int* wys) {

    if (!szer || !wys)
        return;

    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            szer, sizeof(int)) ||
        !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            wys, sizeof(int))) {
        return;
    }

    const int lokalny_szer =
        aktywny_ekran ?
        grafika_pobierz_szerokosc() :
        0;

    const int lokalny_wys =
        aktywny_ekran ?
        grafika_pobierz_wysokosc() :
        0;

    if (!skopiuj_do_przestrzeni_uzytkownika(
            szer,
            &lokalny_szer,
            sizeof(lokalny_szer))) {
        return;
    }

    skopiuj_do_przestrzeni_uzytkownika(
        wys,
        &lokalny_wys,
        sizeof(lokalny_wys));
}

extern "C" bool gui_czy_zamknieto_powloke() {
    if (!flaga_zamkniecia_powloki)
        return false;

    flaga_zamkniecia_powloki = false;
    return true;
}

extern "C" int bws_gui_pobierz_szerokosc_znaku(
    uint32_t unicode) {

    const uint32_t max_znaki =
        sizeof(nowa_czcionka_16x16) /
        sizeof(nowa_czcionka_16x16[0]);

    if (unicode < max_znaki) {
        int szer =
            nowa_czcionka_szerokosci[unicode];

        if (szer < 1) szer = 1;
        if (szer > 16) szer = 16;

        return szer;
    }

    return 8;
}
