#include "jadro/gui/display_mode_policy.h"
#include "jadro/gui/graphics_perf.h"
#include "jadro/gui/graphics_copy.h"
#include "jadro/gui/skalowanie.h"
#include "kolor_alfa.h"
#include "bws_gui_batch.h"
#include "diagnostyka_systemu.h"
#include "boot_phase_timing.h"
#include "xhci_pointing_diag.h"
#include "pointer_coordinates.h"
#include "diagnostyka_startu.h"
#include "diagnostyka_klatek_startu.h"
#include "ekran_powitalny_api.h"
#include "move_trace.h"
#include "wm_input.h"
#include "gui_runtime_diag.h"
#include "kursor_prezentacja.h"
#include "kursor_diag.h"
#include "grafika.h"
#include "bufory_grafiki.h"
#include "sterowniki/grafika/bochs_vbe.h"
#include "virtio_gpu.h"
#include "virtio_input.h"
#include "mysz_input.h"
#include "pamiec.h"
#include "zegar-rtc.h"
#include "psf.h"
#include "scheduler.h"
#include "ekran.h"
#include "zegar_ui.h"
#include "sterowniki/czas/hpet.h"
#include "skladacz_obrazu.h"
#include "bws_zdarzenia.h"
#include "bws_nowoczesne.h"
#include "heap.h"
#include "scheduler.h"
#include "motyw.h"

#include <stdint.h>
#include <stddef.h>

// Czcionka Unicode i sterownik GOP.
#include "czcionki/extronic16Bunicode.h"
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
extern "C" uint32_t klawiatura_modyfikatory();

// Placement new bez zaleznosci od biblioteki standardowej.
#ifdef BURSZTYN_GRAPHICS_HOST_TEST
#include <new>
#else
inline void* operator new(unsigned long, void* p) { return p; }
inline void* operator new[](unsigned long, void* p) { return p; }

extern "C" {
    void* __dso_handle = nullptr;
    int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
}
#endif

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

constexpr uint64_t VADDR_RAW_BMP      = 0x120000000ULL;
constexpr uint64_t VADDR_FRAMEBUFFER  = 0x130000000ULL;

constexpr uint64_t MAX_RAW_BMP = 32ULL * 1024ULL * 1024ULL;

/*
 * LFB jest MMIO, nie zwykla pamiecia RAM. Na obecnym sprzecie nie
 * programujemy jeszcze PAT dla write-combining, wiec wybieramy jeden,
 * konserwatywny typ UC- (PCD=1, PWT=0) dla KAZDEGO aliasu LFB.
 */
constexpr uint32_t FLAGI_CACHE_FRAMEBUFFER =
    VMM_FLAGA_OBECNA | VMM_FLAGA_ZAPIS | VMM_FLAGA_PCD;
constexpr uint64_t LIMIT_IDENTITY_MAP = 4ULL * 1024ULL * 1024ULL * 1024ULL;

constexpr int MAX_SKALA_TEKSTU = 4;
constexpr size_t MAX_TEKST_WEWNETRZNY = 4096;
constexpr size_t MAX_TEKST_GUI_BWS = 1024;

bool surowy_bufor_bmp_zmapowany = false;
bool bufor_tapety_zmapowany = false;
size_t tapeta_zmapowane_bajty = 0;
size_t scratch_zmapowane_bajty = 0;

// Mapowane PRZED utworzeniem procesow: VMM klonuje hierarchie jadra.
// Licznik zachowuje poprawne czesciowe mapowanie po braku ramek; ponowna
// proba nie nadpisuje PTE ani nie gubi juz przydzielonych ramek.
bool mapuj_bufor_tapety(uint64_t adres, size_t potrzebne, size_t* zmapowane) {
    if (!zmapowane || potrzebne > MAX_RAW_BMP || potrzebne > SIZE_MAX - 4095U) return false;
    const size_t koniec = (potrzebne + 4095U) & ~size_t(4095U);
    while (*zmapowane < koniec) {
        void* ramka = ZaalokujRamke();
        if (!ramka) return false;
        void* va = reinterpret_cast<void*>(adres + *zmapowane);
        if (!VmmMapujNowaStroneJadra(va, ramka)) {
            ZwolnijRamke(ramka);
            return false;
        }
        *zmapowane += 4096U;
    }
    return true;
}

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
    uint64_t cr3 = 0;

    asm volatile(
        "mov %%cr3, %0"
        : "=r"(cr3)
        :
        : "memory"
    );

    cr3 &= 0x000FFFFFFFFFF000ULL;
    if (cr3 == 0) return;

    asm volatile(
        "mov %0, %%cr3"
        :
        : "r"(cr3)
        : "memory");
}

bool zmapuj_framebuffer(uint64_t adres_fizyczny,
                        uint64_t rozmiar,
                        uint64_t* adres_wirtualny_wynik) {
#ifdef BURSZTYN_GRAPHICS_HOST_TEST
    if(!adres_fizyczny||!rozmiar||!adres_wirtualny_wynik)return false;
    *adres_wirtualny_wynik=adres_fizyczny;return true;
#else
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
        const uint64_t fizyczny = baza_fizyczna + i;

        ZmapujStrone(
            reinterpret_cast<void*>(VADDR_FRAMEBUFFER + i),
            reinterpret_cast<void*>(fizyczny),
            FLAGI_CACHE_FRAMEBUFFER);

        /*
         * VBE LFB zwykle lezy ponizej 4 GiB i byl jednoczesnie widoczny
         * przez bootstrapowy identity-map jako WB. Alias WB + alias PCD
         * tej samej pamieci jest niedozwolona kombinacja typow cache x86.
         * Rozbicie odpowiedniej huge-page i ponowne zmapowanie identity
         * aliasu usuwa roznice bez kasowania potrzebnego mapowania MMIO.
         */
        if (fizyczny < LIMIT_IDENTITY_MAP) {
            ZmapujStrone(
                reinterpret_cast<void*>(fizyczny),
                reinterpret_cast<void*>(fizyczny),
                FLAGI_CACHE_FRAMEBUFFER);
        }
    }

    przeladuj_cr3();

    *adres_wirtualny_wynik =
        VADDR_FRAMEBUFFER + przesuniecie;

    return true;
#endif
}

int ogranicz_skale(int skala) {
    if(skala>=100&&skala<=400)return skala;
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

/* RAM→RAM: kompilator generuje rep movs / SIMD zamiast petli C na piksel. */
static inline void kopiuj_wiersz32(uint32_t* dst, const uint32_t* src, uint32_t count) {
    if (count)
        graphics_copy_pixels(dst,src,count);
}

/*
 * RAM→LFB/UC: wylacznie volatile store'y. Firmware LFB/UC nie toleruje
 * zwyklego memcpy (cacheable stores moga nie dojsc do scanoutu); rozwijamy
 * po 4 dwordy, zeby obciac narzut petli bez utraty widocznosci zapisow.
 */
static inline void kopiuj_wiersz_lfb(volatile uint32_t* dst, const uint32_t* src, uint32_t count) {
    uint32_t i = 0;
    for (; i + 3 < count; i += 4) {
        dst[i] = src[i];
        dst[i + 1] = src[i + 1];
        dst[i + 2] = src[i + 2];
        dst[i + 3] = src[i + 3];
    }
    for (; i < count; ++i) dst[i] = src[i];
}

/*
 * VirtIO backing is ordinary cacheable RAM, not a firmware LFB or UC MMIO.
 * Keep this path separate from kopiuj_wiersz_lfb(): volatile scalar stores
 * make every dirty-region publication unnecessarily expensive under TCG.
 */
static inline void kopiuj_wiersz_ram32(uint32_t* dst, const uint32_t* src,
                                       uint32_t count) {
    if (count)
        graphics_copy_pixels(dst,src,count);
}

static inline void wypelnij_wiersz32(uint32_t* dst, uint32_t kolor, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) dst[i] = kolor;
}

// =========================================================================
// HAL EKRANU
// =========================================================================

struct CursorPlaneCaps {
    bool hardware = false;
    uint32_t max_width = 0;
    uint32_t max_height = 0;
};

class SterownikEkranu {
protected:
    uint32_t szerokosc = 0;
    uint32_t wysokosc = 0;
    uint32_t pitch = 0;
    uint8_t bpp = 0;
    volatile uint8_t* framebuffer_fizyczny = nullptr;

    void KopiujCalyBufor32(uint8_t* bufor) {
        if (!PrezentacjaStartu::dozwolona() || !bufor || !framebuffer_fizyczny || bpp != 32)
            return;

        for (uint32_t rzad = 0; rzad < wysokosc; ++rzad) {
            const uint64_t offset =
                static_cast<uint64_t>(rzad) * static_cast<uint64_t>(pitch);
            volatile uint32_t* dst =
                reinterpret_cast<volatile uint32_t*>(
                    framebuffer_fizyczny + offset);
            const uint32_t* src =
                reinterpret_cast<const uint32_t*>(
                    bufor + offset);
            kopiuj_wiersz_lfb(dst, src, szerokosc);
#ifdef BURSZTYN_GRAPHICS_HOST_TEST
            if(KopiaWidoczna()) {
                extern void grafika_host_wiersz_opublikowany(uint32_t);
                grafika_host_wiersz_opublikowany(rzad);
            }
#endif
        }
        if(BURSZTYNDIAGSPLASHRUNTIMEFRAMES&&KopiaWidoczna())KlatkiStartuDiag::event("KopiujCalyBufor32",Backend(),czas_monotoniczny_us(),
            const_cast<const uint8_t*>(framebuffer_fizyczny),bufor,0,{0,0,int(szerokosc),int(wysokosc)},
            reinterpret_cast<const uint32_t*>(const_cast<const uint8_t*>(framebuffer_fizyczny)),
            szerokosc,wysokosc,pitch/4,true);
    }

    void KopiujFragment32(uint8_t* bufor,
                          int x,
                          int y,
                          int szer,
                          int wys) {
        if (!PrezentacjaStartu::dozwolona() || !bufor || !framebuffer_fizyczny ||
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
            end_x = static_cast<int64_t>(szerokosc);

        if (end_y > static_cast<int64_t>(wysokosc))
            end_y = static_cast<int64_t>(wysokosc);

        if (start_x >= end_x || start_y >= end_y)
            return;

        const uint32_t pikseli_w_wierszu =
            static_cast<uint32_t>(end_x - start_x);

        for (int64_t rzad = start_y; rzad < end_y; ++rzad) {
            const uint64_t offset =
                static_cast<uint64_t>(rzad) *
                    static_cast<uint64_t>(pitch) +
                static_cast<uint64_t>(start_x) * 4ULL;

            volatile uint32_t* dst =
                reinterpret_cast<volatile uint32_t*>(
                    framebuffer_fizyczny + offset);

            const uint32_t* src =
                reinterpret_cast<const uint32_t*>(
                    bufor + offset);

            kopiuj_wiersz_lfb(dst, src, pikseli_w_wierszu);
        }
        if(BURSZTYNDIAGSPLASHRUNTIMEFRAMES&&KopiaWidoczna())KlatkiStartuDiag::event("KopiujFragment32",Backend(),czas_monotoniczny_us(),
            const_cast<const uint8_t*>(framebuffer_fizyczny),bufor,0,{int(start_x),int(start_y),int(end_x-start_x),int(end_y-start_y)},
            reinterpret_cast<const uint32_t*>(const_cast<const uint8_t*>(framebuffer_fizyczny)),
            szerokosc,wysokosc,pitch/4,true);
    }

    void KopiujCalyBufor32Ram(uint8_t* bufor) {
        if (!PrezentacjaStartu::dozwolona() || !bufor || !framebuffer_fizyczny ||
            bpp != 32) {
            return;
        }

        for (uint32_t rzad = 0; rzad < wysokosc; ++rzad) {
            const uint64_t offset =
                static_cast<uint64_t>(rzad) * static_cast<uint64_t>(pitch);
            uint32_t* dst = reinterpret_cast<uint32_t*>(
                const_cast<uint8_t*>(framebuffer_fizyczny + offset));
            const uint32_t* src = reinterpret_cast<const uint32_t*>(
                bufor + offset);
            kopiuj_wiersz_ram32(dst, src, szerokosc);
        }
    }

    void KopiujFragment32Ram(uint8_t* bufor,
                             int x,
                             int y,
                             int szer,
                             int wys) {
        if (!PrezentacjaStartu::dozwolona() || !bufor || !framebuffer_fizyczny ||
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
            end_x = static_cast<int64_t>(szerokosc);
        if (end_y > static_cast<int64_t>(wysokosc))
            end_y = static_cast<int64_t>(wysokosc);
        if (start_x >= end_x || start_y >= end_y) return;

        const uint32_t pikseli_w_wierszu =
            static_cast<uint32_t>(end_x - start_x);
        for (int64_t rzad = start_y; rzad < end_y; ++rzad) {
            const uint64_t offset =
                static_cast<uint64_t>(rzad) * static_cast<uint64_t>(pitch) +
                static_cast<uint64_t>(start_x) * 4ULL;
            uint32_t* dst = reinterpret_cast<uint32_t*>(
                const_cast<uint8_t*>(framebuffer_fizyczny + offset));
            const uint32_t* src = reinterpret_cast<const uint32_t*>(
                bufor + offset);
            kopiuj_wiersz_ram32(dst, src, pikseli_w_wierszu);
        }
    }

public:
    virtual const char* Backend() const { return "native"; }
    virtual uint32_t ZasobScanoutu() const { return 0; }
    virtual bool KopiaWidoczna() const { return true; }
    virtual bool PublikacjaUdana() const { return PrezentacjaStartu::dozwolona(); }
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
    virtual void RozpocznijBatch() {}
    virtual void ZakonczBatch() {}
    virtual CursorPlaneCaps PobierzCapsKursora() const {
        return {};
    }
    virtual bool UstawKsztaltKursora(const uint32_t*, uint32_t, uint32_t,
                                     uint32_t, uint32_t) {
        return false;
    }
    virtual bool PrzesunKursor(int, int) { return false; }
    virtual bool UkryjKursor() { return false; }

    uint32_t PobierzSzerokosc() const { return szerokosc; }
    uint32_t PobierzWysokosc() const { return wysokosc; }
    uint32_t PobierzPitch() const { return pitch; }
    uint8_t PobierzBpp() const { return bpp; }

    volatile uint8_t* PobierzBuforFizyczny() const { return framebuffer_fizyczny; }
};

class SterownikVESA final : public SterownikEkranu {
public:
    const char* Backend() const override { return "VESA"; }
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
    const char* Backend() const override { return "GOP"; }
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

class SterownikVirtioGPU final : public SterownikEkranu {
private:
    VirtioGpuDriver* drv = nullptr;
    bool opublikowano=false;
    bool batch_active=false;
    uint32_t batch_count=0;
    VirtioGpuRect batch_rects[SKLADACZ_MAKS_DIRTY_RECT]{};

public:
    const char* Backend() const override { return "VirtIO"; }
    uint32_t ZasobScanoutu() const override { return drv?drv->resource_id:0; }
    bool KopiaWidoczna() const override { return false; }
    bool PublikacjaUdana() const override { return opublikowano; }
    bool InicjalizujGPU(VirtioGpuDriver* gdrv) {
        if (!gdrv || !gdrv->fb_vaddr || gdrv->fb_width == 0 || gdrv->fb_height == 0) {
            return false;
        }
        drv = gdrv;
        if (!SterownikEkranu::Inicjalizuj(
                reinterpret_cast<uint64_t>(gdrv->fb_vaddr),
                gdrv->fb_width,
                gdrv->fb_height,
                gdrv->fb_stride,
                32)) {
            return false;
        }
        return true;
    }

    bool Inicjalizuj(uint64_t adres_lfb,
                     uint32_t w,
                     uint32_t h,
                     uint32_t p,
                     uint8_t b) override {
        if (drv && drv->fb_vaddr) {
            adres_lfb = reinterpret_cast<uint64_t>(drv->fb_vaddr);
            w = drv->fb_width;
            h = drv->fb_height;
            p = drv->fb_stride;
            b = 32;
        }
        return SterownikEkranu::Inicjalizuj(adres_lfb, w, h, p, b);
    }

    VirtioGpuDriver* PobierzSterownikGPU() const { return drv; }
    CursorPlaneCaps PobierzCapsKursora() const override {
        /*
         * The VirtIO cursor plane is not part of the scanout resource used by
         * QEMU screendump/diagnostic paths and can report successful commands
         * while the visible desktop still has no cursor. Keep the software
         * overlay canonical until the plane has a visibility proof.
         */
        return {};
    }
    bool UstawKsztaltKursora(const uint32_t* pixels, uint32_t w,
                             uint32_t h, uint32_t hx, uint32_t hy) override {
        return virtio_gpu_cursor_set_shape(drv, pixels, w, h, hx, hy);
    }
    bool PrzesunKursor(int x, int y) override {
        return virtio_gpu_cursor_move(drv, x, y);
    }
    bool UkryjKursor() override {
        return virtio_gpu_cursor_hide(drv);
    }

    void RozpocznijBatch() override {
        batch_count=0;
        batch_active=drv && drv->initialized && !virtio_gpu_is_quiescing();
    }

    void ZakonczBatch() override {
        if(!batch_active)return;
        batch_active=false;
        if(!batch_count)return;
        const auto start=gui_perf_now();
        opublikowano=virtio_gpu_transfer_rects_and_flush(drv,batch_rects,batch_count);
        const auto elapsed=gui_perf_now()-start;
        gui_perf_add(0,GP_GPU_NS,elapsed);
        gui_perf_max(0,GP_GPU_MAX_NS,elapsed);
        batch_count=0;
    }

    void KopiujNaEkran(uint8_t* bufor) override {
        opublikowano=false;
        if (!PrezentacjaStartu::dozwolona() || virtio_gpu_is_quiescing() || !drv || !drv->initialized || drv->has_error) return;
        if (static_cast<const void*>(bufor) != static_cast<const void*>(const_cast<const uint8_t*>(framebuffer_fizyczny))) {
            GraphicsPerf::Timer copy_timer(GraphicsPerf::CopyNs);
            GraphicsPerf::add(GraphicsPerf::CopyBytes,uint64_t(szerokosc)*wysokosc*4);
            KopiujCalyBufor32Ram(bufor);
        }
        MoveTrace::flush_request();
        opublikowano=virtio_gpu_transfer_and_flush(drv, 0, 0, szerokosc, wysokosc);
    }

    void KopiujFragmentNaEkran(uint8_t* bufor,
                               int x,
                               int y,
                               int szer,
                               int wys) override {
        opublikowano=false;
        if (!PrezentacjaStartu::dozwolona() || virtio_gpu_is_quiescing() || !drv || !drv->initialized || drv->has_error) return;
        if (szer <= 0 || wys <= 0) return;

        int x0 = x < 0 ? 0 : x;
        int y0 = y < 0 ? 0 : y;
        int x1 = x + szer;
        int y1 = y + wys;
        if (x1 > static_cast<int>(szerokosc)) x1 = static_cast<int>(szerokosc);
        if (y1 > static_cast<int>(wysokosc)) y1 = static_cast<int>(wysokosc);
        if (x0 >= x1 || y0 >= y1) return;

        int rx = x0;
        int ry = y0;
        int rw = x1 - x0;
        int rh = y1 - y0;

        const auto copy_start=gui_perf_now();
        if (static_cast<const void*>(bufor) != static_cast<const void*>(const_cast<const uint8_t*>(framebuffer_fizyczny))) {
            GraphicsPerf::Timer copy_timer(GraphicsPerf::CopyNs);
            GraphicsPerf::add(GraphicsPerf::CopyBytes,uint64_t(rw)*rh*4);
            KopiujFragment32Ram(bufor, rx, ry, rw, rh);
        }
        gui_perf_add(0,GP_COPY_NS,gui_perf_now()-copy_start);
        if(batch_active) {
            if(batch_count<SKLADACZ_MAKS_DIRTY_RECT) {
                batch_rects[batch_count++]={static_cast<uint32_t>(rx),static_cast<uint32_t>(ry),
                    static_cast<uint32_t>(rw),static_cast<uint32_t>(rh)};
                opublikowano=true;
                return;
            }
            ZakonczBatch();
        }
        const auto gpu_start=gui_perf_now();
        MoveTrace::flush_request();
        opublikowano=virtio_gpu_transfer_and_flush(drv, static_cast<uint32_t>(rx), static_cast<uint32_t>(ry),
                                      static_cast<uint32_t>(rw), static_cast<uint32_t>(rh));
        const auto gpu_elapsed=gui_perf_now()-gpu_start;
        gui_perf_add(0,GP_GPU_NS,gpu_elapsed);gui_perf_max(0,GP_GPU_MAX_NS,gpu_elapsed);
    }
};

alignas(SterownikVirtioGPU) static uint8_t st_virtio_gpu_pamiec[sizeof(SterownikVirtioGPU)];

static SterownikEkranu* aktywny_ekran = nullptr;
#ifdef BURSZTYN_GRAPHICS_HOST_TEST
void grafika_host_bezposrednia_kopia(uint8_t* p) {
    if(aktywny_ekran){aktywny_ekran->KopiujNaEkran(p);aktywny_ekran->KopiujFragmentNaEkran(p,0,0,1,1);}
}
#endif
static uint8_t* backbuffer = nullptr;
static bool backbuffer_pozyczony=false;
static size_t backbuffer_bajty=0;
static uint64_t ekran_lfb_va=0, ekran_vram=0;
static uint64_t boot_lfb_va = 0;
static uint32_t boot_szer = 0, boot_wys = 0, boot_pitch = 0, boot_bpp = 0;
static MozliwosciEkranu boot_ekran_caps{};
static SterownikEkranu* boot_sterownik = nullptr;
static MozliwosciEkranu ekran_caps{};
static TrybEkranu ekran_tryby[EKRAN_MAX_TRYBOW]{};
static uint32_t ekran_liczba_trybow=0;
struct AktywnyStanEkranu {
    uint32_t szerokosc, wysokosc, pitch, bpp;
    uint64_t framebuffer;
    uint64_t generacja;
};
// Publikowany wylacznie razem z wymiana buforow pod blokada ekranu.
// Sterownik sprzetowy, compositor i wejscie nie utrzymuja osobnych granic.
static AktywnyStanEkranu aktywny_stan{};
static DisplayGeometry committed_geometry{};
static uint32_t* bufor_tapety = nullptr;
static bool tapeta_zaladowana = false;

static void grafika_rozpocznij_batch_gpu() {
    if(aktywny_ekran && ekran_caps.backend==EKRAN_VIRTIO_GPU)
        aktywny_ekran->RozpocznijBatch();
}

static void grafika_zakoncz_batch_gpu() {
    if(aktywny_ekran && ekran_caps.backend==EKRAN_VIRTIO_GPU)
        aktywny_ekran->ZakonczBatch();
}

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
static bool grafika_wyciszona=false;
void grafika_wycisz() {grafika_wyciszona=true;tryb_skladania_obrazu=false;diag_start_reboot("[STARTUP] graphics quiesce");}
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
        false,
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
static int motion_hover_pid_gui = -1;
static int mysz_y = 300;

/* Pozycja faktycznie narysowanego overlayu w fizycznym framebufferze. */
static bool kursor_overlay_widoczny = false;
static int kursor_overlay_x = 0;
static int kursor_overlay_y = 0;
static int kursor_overlay_w = 16;
static int kursor_overlay_h = 16;

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

struct StanKursoraProcesu {
    uint32_t typ;
    uint32_t szer;
    uint32_t wys;
    uint32_t hotspot_x;
    uint32_t hotspot_y;
    uint32_t* piksele;
};

static StanKursoraProcesu kursory_procesow[SKLADACZ_MAKS_WARSTW] = {};

static const StanKursoraProcesu* aktywny_stan_kursora() {
    if (aktywny_pid_gui > 0 && aktywny_pid_gui < SKLADACZ_MAKS_WARSTW)
        return &kursory_procesow[aktywny_pid_gui];
    return nullptr;
}

static void parametry_kursora(const StanKursoraProcesu* s,
                              int* w, int* h, int* hx, int* hy) {
    if (s && s->typ == GUI_CURSOR_CUSTOM && s->szer && s->wys) {
        *w = static_cast<int>(s->szer); *h = static_cast<int>(s->wys);
        *hx = static_cast<int>(s->hotspot_x); *hy = static_cast<int>(s->hotspot_y);
        return;
    }
    *w = 16; *h = 16;
    const uint32_t typ = s ? s->typ : GUI_CURSOR_ARROW;
    *hx = typ == GUI_CURSOR_ARROW ? 0 : 8;
    *hy = typ == GUI_CURSOR_HAND ? 1 : (typ == GUI_CURSOR_ARROW ? 0 : 8);
}

static bool piksel_kursora(const StanKursoraProcesu* s, int x, int y,
                           uint32_t* kolor) {
    if (!kolor || x < 0 || y < 0) return false;
    const uint32_t typ = s ? s->typ : GUI_CURSOR_ARROW;
    if (typ == GUI_CURSOR_CUSTOM && s && x < static_cast<int>(s->szer) &&
        y < static_cast<int>(s->wys)) {
        if (!s->piksele) return false;
        const uint32_t p = s->piksele[static_cast<uint32_t>(y) * s->szer +
                                      static_cast<uint32_t>(x)];
        if ((p >> 24U) == 0) return false;
        *kolor = p & 0x00FFFFFFU;
        return true;
    }
    if (x >= 16 || y >= 16) return false;
    if (typ == GUI_CURSOR_ARROW) {
        const uint8_t p = kursor_bitmapa[y][x];
        if (!p) return false;
        *kolor = p == 1 ? 0x00000000U : 0x00FFFFFFU;
        return true;
    }
    bool on = false;
    switch (typ) {
        case GUI_CURSOR_HAND:
            on = ((x == 7 || x == 8) && y >= 1 && y <= 12) ||
                 (y >= 6 && y <= 12 && x >= 4 && x <= 11) ||
                 (y == 13 && x >= 5 && x <= 10);
            break;
        case GUI_CURSOR_TEXT:
            on = (x == 7 || x == 8) || ((y == 1 || y == 14) && x >= 4 && x <= 11);
            break;
        case GUI_CURSOR_RESIZE_NS:
            on = (x == 7 || x == 8) || (y <= 4 && x >= 7-y && x <= 8+y) ||
                 (y >= 11 && x >= y-8 && x <= 23-y);
            break;
        case GUI_CURSOR_RESIZE_EW:
            on = (y == 7 || y == 8) || (x <= 4 && y >= 7-x && y <= 8+x) ||
                 (x >= 11 && y >= x-8 && y <= 23-x);
            break;
        case GUI_CURSOR_RESIZE_NWSE:
            on = (x == y || x + 1 == y || y + 1 == x) && x >= 2 && x <= 13;
            on = on || (x <= 4 && y <= 4 && x + y >= 3) ||
                 (x >= 11 && y >= 11 && x + y <= 27);
            break;
        case GUI_CURSOR_RESIZE_NESW:
            on = (x + y >= 14 && x + y <= 16 && x >= 2 && x <= 13) ||
                 (x >= 11 && y <= 4 && x-y <= 13) ||
                 (x <= 4 && y >= 11 && y-x <= 13);
            break;
        case GUI_CURSOR_WAIT: {
            const int dx = x - 7, dy = y - 7;
            const int r2 = dx * dx + dy * dy;
            on = r2 >= 30 && r2 <= 50;
            break;
        }
        case GUI_CURSOR_CROSSHAIR:
            on = x == 7 || x == 8 || y == 7 || y == 8;
            break;
        default: return false;
    }
    if (!on) return false;
    *kolor = ((x + y) & 1) ? 0x00FFFFFFU : 0x00000000U;
    return true;
}

// =========================================================================
// SERIAL
// =========================================================================

static inline void serial_outb(uint16_t port, uint8_t val) {
    asm volatile(
        "outb %0, %1"
        :
        : "a"(val), "Nd"(port));
}

extern "C" void SerialLog(const char* str) {
    if (!str) return;
#ifdef BURSZTYN_GRAPHICS_HOST_TEST
    extern void grafika_host_serial(const char*);
    grafika_host_serial(str);
#else
    for (size_t i = 0; str[i] != '\0'; ++i) {
        const uint8_t c = static_cast<uint8_t>(str[i]);
        serial_outb(0x3F8, c);
        serial_outb(0xE9, c);
    }
#endif
}

#ifndef BURSZTYN_DEBUG_GUI_INPUT
#define BURSZTYN_DEBUG_GUI_INPUT 0
#endif

#ifndef BURSZTYN_DEBUG_GFX
#define BURSZTYN_DEBUG_GFX 0
#endif

void SerialU64(uint64_t v) {
    char cyfry[24] = {};
    int n = 0;
    do { cyfry[n++] = static_cast<char>('0' + v % 10U); v /= 10U; }
    while (v != 0 && n < static_cast<int>(sizeof(cyfry)));
    while (n > 0) { char s[2] = {cyfry[--n], '\0'}; SerialLog(s); }
}

void SerialHex64(uint64_t v) {
    static constexpr char HEX[] = "0123456789ABCDEF";
    char out[19] = {'0','x'};
    for (int i = 0; i < 16; ++i)
        out[2 + i] = HEX[(v >> ((15 - i) * 4)) & 0xFU];
    out[18] = '\0';
    SerialLog(out);
}

void LogGfxU64(const char* key, uint64_t value, bool hex = false) {
    SerialLog(key);
    if (hex) SerialHex64(value); else SerialU64(value);
    SerialLog("\n");
}

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

    if (diag_log_impl) diag_capture_serial(tekst);
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

    DopiszDoBufora(tekst, 0x00FFBF00);
    DopiszDoBufora("\n", 0x00FFBF00);

    // Bootstrap diagnostics stay headless. The desktop owns presentation.
}

// =========================================================================
// PREZENTACJA EKRANU
// =========================================================================

// During the splash the scene and overlay remain independent surfaces. After
// it ends, compose directly into ordinary VirtIO backing RAM. TRANSFER copies
// to the host image only after the complete damaged scene/cursor transaction.
static bool przygotuj_wlasnosc_sceny(){
    if(!aktywny_ekran||!backbuffer)return true;
    if(powitanie_aktywne()&&backbuffer_pozyczony){
        auto* owned=static_cast<uint8_t*>(bufor_grafiki_alokuj(backbuffer_bajty));
        if(!owned)return false;
        graphics_copy_pixels(reinterpret_cast<uint32_t*>(owned),reinterpret_cast<const uint32_t*>(backbuffer),backbuffer_bajty/4);
        backbuffer=owned;backbuffer_pozyczony=false;
    }else if(!powitanie_aktywne()&&!backbuffer_pozyczony&&!display_mode_policy.transaction_pending&&ekran_caps.backend==EKRAN_VIRTIO_GPU){
        auto* drv=virtio_gpu_get_driver(0);
        if(drv&&drv->fb_vaddr&&drv->fb_stride==aktywny_ekran->PobierzPitch()&&
           drv->fb_width==aktywny_ekran->PobierzSzerokosc()&&drv->fb_height==aktywny_ekran->PobierzWysokosc()){
            const size_t bytes=size_t(drv->fb_stride)*drv->fb_height;
            if(bytes>backbuffer_bajty)return false;
            graphics_copy_pixels(static_cast<uint32_t*>(drv->fb_vaddr),reinterpret_cast<const uint32_t*>(backbuffer),bytes/4);
            bufor_grafiki_zwolnij(backbuffer);backbuffer=static_cast<uint8_t*>(drv->fb_vaddr);backbuffer_bajty=bytes;backbuffer_pozyczony=true;
        }
    }
    return true;
}

void PrzeniesNaEkran() {
    if(!przygotuj_wlasnosc_sceny())return;
    if (grafika_wyciszona || !backbuffer || !aktywny_ekran)
        return;

    auto* source=powitanie_zrodlo(backbuffer,
        grafika_pobierz_szerokosc(),grafika_pobierz_wysokosc(),aktywny_ekran->PobierzPitch(),
        0,0,grafika_pobierz_szerokosc(),grafika_pobierz_wysokosc(),ekran_skala());
    if(powitanie_aktywne()&&!powitanie_pierwsza_klatka_gotowa())return;
    if(!PrezentacjaStartu::dozwolona())return;
    if(BURSZTYNDIAGSPLASHRUNTIMEFRAMES)KlatkiStartuDiag::info().backbuffer=source;
    if(BURSZTYNDIAGSPLASHRUNTIMEFRAMES)KlatkiStartuDiag::event("PrzeniesNaEkran",aktywny_ekran->Backend(),czas_monotoniczny_us(),
        const_cast<const uint8_t*>(aktywny_ekran->PobierzBuforFizyczny()),source,aktywny_ekran->ZasobScanoutu(),
        {0,0,grafika_pobierz_szerokosc(),grafika_pobierz_wysokosc()},
        reinterpret_cast<const uint32_t*>(source),grafika_pobierz_szerokosc(),grafika_pobierz_wysokosc(),
        aktywny_ekran->PobierzPitch()/4,false);
    if(powitanie_pierwsza_publikacja())SplashFirstDiag::event("first_graphics_commit",czas_monotoniczny_us());
    aktywny_ekran->KopiujNaEkran(source);
    if(aktywny_ekran->PublikacjaUdana()){BootTiming::framebuffer_published();PrezentacjaStartu::opublikowana();powitanie_publikacja();}
}

void PrzeniesFragmentNaEkran(int x,
                             int y,
                             int szer,
                             int wys) {
    if (grafika_wyciszona || !backbuffer || !aktywny_ekran)
        return;

    const auto start=gui_perf_now();
    MoveTrace::present();
    gui_perf_add(0,GP_PRESENT);gui_perf_add(0,GP_COPY);
    auto* source=powitanie_zrodlo(backbuffer,grafika_pobierz_szerokosc(),grafika_pobierz_wysokosc(),
            aktywny_ekran->PobierzPitch(),x,y,szer,wys,ekran_skala());
    if(powitanie_aktywne()&&!powitanie_pierwsza_klatka_gotowa())return;
    if(!PrezentacjaStartu::dozwolona())return;
    if(BURSZTYNDIAGSPLASHRUNTIMEFRAMES)KlatkiStartuDiag::info().backbuffer=source;
    if(BURSZTYNDIAGSPLASHRUNTIMEFRAMES)KlatkiStartuDiag::event("PrzeniesFragmentNaEkran",aktywny_ekran->Backend(),czas_monotoniczny_us(),
        const_cast<const uint8_t*>(aktywny_ekran->PobierzBuforFizyczny()),source,aktywny_ekran->ZasobScanoutu(),{x,y,szer,wys},
        reinterpret_cast<const uint32_t*>(source),grafika_pobierz_szerokosc(),grafika_pobierz_wysokosc(),
        aktywny_ekran->PobierzPitch()/4,false);
    // First publication is always the whole image, even for a cursor dirty rect.
    if(powitanie_aktywne()||PrezentacjaStartu::wymaga_pelnej()) {
        SplashFirstDiag::event("first_graphics_commit",czas_monotoniczny_us());
        aktywny_ekran->KopiujNaEkran(source);
        if(aktywny_ekran->PublikacjaUdana()){BootTiming::framebuffer_published();PrezentacjaStartu::opublikowana();powitanie_publikacja();}
    } else aktywny_ekran->KopiujFragmentNaEkran(source,x,y,szer,wys);
    const auto elapsed=gui_perf_now()-start;
    gui_perf_add(0,GP_PRESENT_NS,elapsed);gui_perf_max(0,GP_PRESENT_MAX_NS,elapsed);
}

int grafika_pobierz_szerokosc() {
    if (!aktywny_ekran) return 0;

    if (aktywny_stan.szerokosc) return aktywny_stan.szerokosc > uint32_t(INT32_MAX) ?
        INT32_MAX : static_cast<int>(aktywny_stan.szerokosc);

    const uint32_t szer =
        aktywny_ekran->PobierzSzerokosc();

    if (szer > static_cast<uint32_t>(INT32_MAX))
        return INT32_MAX;

    return static_cast<int>(szer);
}

int grafika_pobierz_wysokosc() {
    if (!aktywny_ekran) return 0;

    if (aktywny_stan.wysokosc) return aktywny_stan.wysokosc > uint32_t(INT32_MAX) ?
        INT32_MAX : static_cast<int>(aktywny_stan.wysokosc);

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
    (void)przygotuj_wlasnosc_sceny();
    if (!backbuffer || !aktywny_ekran)
        return;

    tryb_skladania_obrazu = true;
    grafika_rozpocznij_batch_gpu();
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

            kopiuj_wiersz32(dst, src, static_cast<uint32_t>(szer));
        }
    } else {
        const BursztynMotyw* m = motyw_pobierz_aktywny();
        const uint32_t kolor_pulpitu = m ? m->pulpit : g_domyslny_motyw_fallback.pulpit;
        for (int y = 0; y < wys; ++y) {
            uint32_t* dst =
                reinterpret_cast<uint32_t*>(
                    backbuffer +
                    static_cast<uint64_t>(y) * pitch);

            wypelnij_wiersz32(dst, kolor_pulpitu, static_cast<uint32_t>(szer));
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
    const BursztynMotyw* m = motyw_pobierz_aktywny();
    const uint32_t kolor_pulpitu = m ? m->pulpit : g_domyslny_motyw_fallback.pulpit;
    const uint32_t row_w = static_cast<uint32_t>(x1 - x0);
    for (int py = y0; py < y1; ++py) {
        uint32_t* dst = reinterpret_cast<uint32_t*>(backbuffer +
            static_cast<uint64_t>(py) * pitch) + x0;
        if (tapeta_zaladowana && bufor_tapety) {
            const uint32_t* src = bufor_tapety +
                static_cast<uint64_t>(py) * ekran_szer + x0;
            kopiuj_wiersz32(dst, src, row_w);
        } else {
            wypelnij_wiersz32(dst, kolor_pulpitu, row_w);
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

    const GuiDirtyRect dirty{0,0,grafika_pobierz_szerokosc(),grafika_pobierz_wysokosc()};
    int cx=0,cy=0;grafika_pobierz_pozycje_kursora(&cx,&cy);
    grafika_prezentuj_scene_z_kursorem(&dirty,1,cx,cy);
    grafika_zakoncz_batch_gpu();
    tryb_skladania_obrazu = false;
}

static uint32_t kursor_tlo_kopia[64 * 64];
static uint32_t kursor_plane_piksele[64 * 64];
static bool kursor_plane_ksztalt_brudny = true;
static bool kursor_plane_aktywny = false;
/* Jedna awaria komendy kursora nie może pozostawić niewidocznego wskaźnika. */
static bool kursor_plane_zablokowany = false;
#ifndef BURSZTYN_DIAG_GEOM_TRACE
#define BURSZTYN_DIAG_GEOM_TRACE 0
#endif
#ifndef BURSZTYN_DIAG_BOOT
#define BURSZTYN_DIAG_BOOT 0
#endif

#if BURSZTYN_DIAG_GEOM_TRACE
static uint32_t g_cursor_diag_logged = 0;
static constexpr uint32_t CURSOR_DIAG_MAX = 10;

static void log_cursor_diag(int old_x, int old_y, int new_x, int new_y,
                            int old_rx, int old_ry, int old_rw, int old_rh, bool had_old,
                            int new_rx, int new_ry, int new_rw, int new_rh, bool has_new) {
    if (g_cursor_diag_logged >= CURSOR_DIAG_MAX) return;
    ++g_cursor_diag_logged;

    char buf[256]{};
    char* p = buf;
    char* end = buf + sizeof(buf);
    auto app = [&](const char* s) { while (*s && p + 1 < end) *p++ = *s++; };
    auto app_i = [&](int val) {
        if (val < 0) { if (p + 1 < end) *p++ = '-'; val = -val; }
        char b[16]; int idx = 0;
        do { b[idx++] = '0' + (val % 10); val /= 10; } while (val > 0);
        while (idx > 0 && p + 1 < end) *p++ = b[--idx];
    };
    app("[CURSOR DIAG #"); app_i(g_cursor_diag_logged);
    app("] old=("); app_i(old_x); app(","); app_i(old_y);
    app(") new=("); app_i(new_x); app(","); app_i(new_y);
    app(") backend=");
    app(ekran_caps.backend == EKRAN_VIRTIO_GPU ? "virtio-gpu" :
        (ekran_caps.backend == EKRAN_BOCHS ? "bochs" :
        (ekran_caps.backend == EKRAN_GOP ? "uefi-gop" : "vesa")));
    app(" called=PrzeniesFragmentNaEkran");
    if (had_old) {
        app(" old_dirty=["); app_i(old_rx); app(","); app_i(old_ry);
        app(" "); app_i(old_rw); app("x"); app_i(old_rh); app("]");
    }
    if (has_new) {
        app(" new_dirty=["); app_i(new_rx); app(","); app_i(new_ry);
        app(" "); app_i(new_rw); app("x"); app_i(new_rh); app("]");
    }
    app("\n");
    *p = '\0';
    klog_serial(buf);
}
#endif

void grafika_naloz_kursor_regionu(int x, int y, int szer, int wys) {
    (void)x; (void)y; (void)szer; (void)wys;
    grafika_prezentuj_kursor();
}

void grafika_zakoncz_skladanie_regionu(int x,int y,int szer,int wys) {
    if (!backbuffer || !aktywny_ekran) { tryb_skladania_obrazu=false; return; }
    const GuiDirtyRect dirty{x,y,szer,wys};
    int cx=0,cy=0;grafika_pobierz_pozycje_kursora(&cx,&cy);
    grafika_prezentuj_scene_z_kursorem(&dirty,1,cx,cy);
    grafika_zakoncz_batch_gpu();
    tryb_skladania_obrazu=false;
}

void grafika_prezentuj_region(int x,int y,int szer,int wys){PrzeniesFragmentNaEkran(x,y,szer,wys);}

static bool grafika_cursor_plane_przygotuj(const StanKursoraProcesu* stan,
                                           int x, int y) {
    if (!aktywny_ekran) return false;
    if (kursor_plane_zablokowany) return false;
    const CursorPlaneCaps caps=aktywny_ekran->PobierzCapsKursora();
    if (!caps.hardware) {
        if (kursor_plane_aktywny) {
            aktywny_ekran->UkryjKursor();
            kursor_plane_ksztalt_brudny=true;
        }
        kursor_plane_aktywny=false;
        return false;
    }

    int kw=16,kh=16,hx=0,hy=0;
    parametry_kursora(stan,&kw,&kh,&hx,&hy);
    if (kw<=0||kh<=0||kw>int(caps.max_width)||kh>int(caps.max_height)) {
        if (kursor_plane_aktywny) {
            aktywny_ekran->UkryjKursor();
            kursor_plane_ksztalt_brudny=true;
        }
        kursor_plane_aktywny=false;
        return false;
    }
    // After a hide, the GPU plane has no resource; force UPDATE_CURSOR again.
    if (!kursor_plane_aktywny) kursor_plane_ksztalt_brudny=true;
    if (kursor_plane_ksztalt_brudny) {
        for (uint32_t i=0;i<64U*64U;++i) kursor_plane_piksele[i]=0;
        for (int py=0;py<kh;++py) {
            for (int px=0;px<kw;++px) {
                uint32_t kolor=0;
                if (piksel_kursora(stan,px,py,&kolor)) {
                    kursor_plane_piksele[py*64+px]=
                        (kolor&0x00FFFFFFU)|0xFF000000U;
                }
            }
        }
        if (!aktywny_ekran->UstawKsztaltKursora(
                kursor_plane_piksele,static_cast<uint32_t>(kw),
                static_cast<uint32_t>(kh),static_cast<uint32_t>(hx),
                static_cast<uint32_t>(hy))) {
            if (kursor_plane_aktywny) aktywny_ekran->UkryjKursor();
            kursor_plane_aktywny=false;
            kursor_plane_ksztalt_brudny=true;
            kursor_plane_zablokowany=true;
            return false;
        }
        kursor_plane_ksztalt_brudny=false;
    }
    if (!aktywny_ekran->PrzesunKursor(x,y)) {
        if (kursor_plane_aktywny) aktywny_ekran->UkryjKursor();
        kursor_plane_aktywny=false;
        kursor_plane_ksztalt_brudny=true;
        kursor_plane_zablokowany=true;
        return false;
    }
    kursor_plane_aktywny=true;
    kursor_overlay_widoczny=false;
    return true;
}

bool grafika_czy_kursor_sprzetowy() {
    return aktywny_ekran &&
           !kursor_plane_zablokowany &&
           kursor_plane_aktywny;
}

void grafika_prezentuj_scene_z_kursorem(const GuiDirtyRect* dirty, uint32_t count,
                                      int cursor_x, int cursor_y) {
    if (!aktywny_ekran || !backbuffer) return;
    const int sw=grafika_pobierz_szerokosc(),sh=grafika_pobierz_wysokosc();
    if(sw<=0||sh<=0||count>SKLADACZ_MAKS_DIRTY_RECT)return;
    const bool owns_batch=!tryb_skladania_obrazu;
    if(owns_batch)grafika_rozpocznij_batch_gpu();
    // Pre-handshake splash owns the whole scanout (no pointer overlay).
    // After the desktop handshake, continue into normal cursor presentation so
    // a delayed fade cannot leave the user without a visible, clickable cursor.
    if(powitanie_aktywne() && !powitanie_pulpit_potwierdzony()) {
        if(kursor_plane_aktywny) {
            aktywny_ekran->UkryjKursor();
            kursor_plane_aktywny=false;
            kursor_plane_ksztalt_brudny=true;
        }
        PrzeniesNaEkran();kursor_overlay_widoczny=false;
        if(owns_batch)grafika_zakoncz_batch_gpu();
        return;
    }
    if(powitanie_aktywne() && powitanie_pulpit_potwierdzony()) {
        // Finish the farewell scanout, then fall through so the pointer glyph
        // is restored immediately after (or on top of) the last splash frame.
        PrzeniesNaEkran();
        if(powitanie_aktywne()) {
            // Still fading: keep software overlay available; HW plane may be empty
            // after the pre-handshake hide until shape is re-uploaded below.
            kursor_plane_ksztalt_brudny=true;
        }
    }
    if(count==1&&dirty[0].x==0&&dirty[0].y==0&&dirty[0].width==sw&&dirty[0].height==sh)
        PrezentacjaStartu::przygotowana();
    // Snapshot shape too: input can change focus while presentation runs.
    StanKursoraProcesu shape{};
    const auto* active=aktywny_stan_kursora();
    if(active)shape=*active;
    const auto* stan=active?&shape:nullptr;
    if (grafika_cursor_plane_przygotuj(stan,cursor_x,cursor_y)) {
        /*
         * Hardware cursorq is independent from the scanout resource. Scene
         * regions can therefore be transferred while the pointer moves
         * without restoring or repainting a software overlay.
         */
        for (uint32_t ri=0;ri<count;++ri) {
            const auto& r=dirty[ri];
            PrzeniesFragmentNaEkran(r.x,r.y,r.width,r.height);
        }
        if(owns_batch)grafika_zakoncz_batch_gpu();
        return;
    }
    int kw=16,kh=16,hx=0,hy=0;
    parametry_kursora(stan,&kw,&kh,&hx,&hy);
    if(kw>64)kw=64;
    if(kh>64)kh=64;
    const GuiDirtyRect old=kursor_overlay_widoczny?
        GuiDirtyRect{kursor_overlay_x,kursor_overlay_y,kursor_overlay_w,kursor_overlay_h}:GuiDirtyRect{};
    gui_perf_add(0,GP_CURSOR);
    const auto next=kursor_prezentuj_transakcje(
        reinterpret_cast<uint32_t*>(backbuffer),aktywny_ekran->PobierzPitch()/4,
        sw,sh,old,cursor_x-hx,cursor_y-hy,kw,kh,kursor_tlo_kopia,dirty,count,
        [&](int x,int y,uint32_t& c){return piksel_kursora(stan,x,y,&c);},
        [&](GuiDirtyRect r){kursor_diag_present(r);PrzeniesFragmentNaEkran(r.x,r.y,r.width,r.height);},
        [&](int stage){
            kursor_diag_stage(stage);
            // Flush while the cursor is still in driver backing. The shared
            // transaction restores only guest RAM afterwards, never host ink.
            if(stage==3&&backbuffer_pozyczony)grafika_zakoncz_batch_gpu();
        });
    kursor_overlay_x=next.x;kursor_overlay_y=next.y;
    kursor_overlay_w=next.width;kursor_overlay_h=next.height;
    kursor_overlay_widoczny=next.width>0;
#if BURSZTYN_DIAG_GEOM_TRACE
    log_cursor_diag(old.x,old.y,cursor_x,cursor_y,
        old.x,old.y,old.width,old.height,old.width>0,
        next.x,next.y,next.width,next.height,next.width>0);
#endif
    // Logical position is input-owned. Never commit an older consumed sample.
    kursor_diag_finish();
    if(owns_batch)grafika_zakoncz_batch_gpu();
}
#if BURSZTYNDIAGABSMAP
void grafika_abs_map_geometria() {
    char buf[384]{};unsigned n=0;
    auto text=[&](const char* p){while(*p&&n+1<sizeof(buf))buf[n++]=*p++;};
    auto num=[&](uint64_t v){char t[24];unsigned i=0;do{t[i++]=char('0'+v%10);v/=10;}while(v);while(i&&n+1<sizeof(buf))buf[n++]=t[--i];};
    auto size=[&](const char* key,unsigned w,unsigned h){text(key);num(w);text("x");num(h);text(" ");};
    text("[ABS GEOM] ");size("fb=",aktywny_ekran->PobierzSzerokosc(),aktywny_ekran->PobierzWysokosc());
    // Compositor clips against these physical graphics dimensions; no host viewport.
    size("comp=",grafika_pobierz_szerokosc(),grafika_pobierz_wysokosc());
    if(auto* desktop=pobierz_warstwe(1))size("desktop=",desktop->szerokosc,desktop->wysokosc);
    else text("desktop=unavailable ");
    text("wm=see-ABS-WM ");text("gen=");num(grafika_generacja_ekranu());
    text(" scale=");num(ekran_skala());text("\n");klog_serial(buf);
}
#endif
#if BURSZTYNDIAGGUIRUNTIME
void grafika_runtime_diag_snapshot() {
    static uint64_t previous=0;const auto now=gui_perf_now();
    if(previous&&now-previous<1000000000ULL)return;
    previous=now;
    int x=0,y=0;grafika_pobierz_pozycje_kursora(&x,&y);
    int kw=0,kh=0,hx=0,hy=0;parametry_kursora(aktywny_stan_kursora(),&kw,&kh,&hx,&hy);
    char buf[512]{};unsigned n=0;
    auto text=[&](const char* p){while(*p&&n+1<sizeof(buf))buf[n++]=*p++;};
    auto num=[&](int64_t v){if(v<0){text("-");v=-v;}char tmp[24];unsigned i=0;do{tmp[i++]=char('0'+v%10);v/=10;}while(v);while(i&&n+1<sizeof(buf))buf[n++]=tmp[--i];};
    auto field=[&](const char* key,int64_t value){text(key);num(value);text(" ");};
    text("[CURSOR GEOM] ");field("screen_w=",grafika_pobierz_szerokosc());field("screen_h=",grafika_pobierz_wysokosc());
    field("driver_w=",aktywny_ekran->PobierzSzerokosc());field("driver_h=",aktywny_ekran->PobierzWysokosc());
    if(ekran_caps.backend==EKRAN_VIRTIO_GPU){auto* drv=virtio_gpu_get_driver(0);if(drv){field("gpu_w=",drv->fb_width);field("gpu_h=",drv->fb_height);}}
    auto* desktop=pobierz_warstwe(1);if(desktop){field("desktop_w=",desktop->szerokosc);field("desktop_h=",desktop->wysokosc);}
    field("logical_x=",x);field("logical_y=",y);field("min_x=",0);field("min_y=",0);
    field("max_x=",grafika_pobierz_szerokosc()-1);field("max_y=",grafika_pobierz_wysokosc()-1);
    field("hotspot_x=",hx);field("hotspot_y=",hy);field("bitmap_w=",kw);field("bitmap_h=",kh);
    field("scale=",ekran_skala());field("generation=",grafika_generacja_ekranu());text("\n");klog_serial(buf);
}
#endif

void grafika_prezentuj_kursor_w(int x,int y) {
    // Cursor-only presents stay off during the pre-handshake splash so they
    // cannot republish a welcome frame. After handshake, allow the overlay.
    if(powitanie_aktywne() && !powitanie_pulpit_potwierdzony())return;
    grafika_prezentuj_scene_z_kursorem(nullptr,0,x,y);
}

void grafika_prezentuj_kursor(){int x=0,y=0;grafika_pobierz_pozycje_kursora(&x,&y);grafika_prezentuj_kursor_w(x,y);}

void grafika_zakoncz_scene(){
    /*
     * The compositor owns the batch between grafika_rozpocznij_skladanie()
     * and this boundary. Leaving it open silently discarded all queued
     * VirtIO transfer rectangles at the next frame's batch reset.
     */
    if (tryb_skladania_obrazu) grafika_zakoncz_batch_gpu();
    tryb_skladania_obrazu=false;
}

void grafika_pobierz_pozycje_kursora(int* x,int* y) {
#ifndef BURSZTYN_GRAPHICS_HOST_TEST
    uint64_t flags;
    asm volatile("pushfq; popq %0; cli":"=r"(flags)::"memory","cc");
#endif
    if(x) *x=mysz_x;
    if(y) *y=mysz_y;
#ifndef BURSZTYN_GRAPHICS_HOST_TEST
    if(flags&(1ULL<<9))asm volatile("sti":::"memory");
#endif
}

void grafika_pobierz_overlay_kursora(int* x,int* y,int* w,int* h,bool* widoczny) {
    if(x) *x=kursor_overlay_x;
    if(y) *y=kursor_overlay_y;
    if(w) *w=kursor_overlay_w;
    if(h) *h=kursor_overlay_h;
    if(widoczny) *widoczny=kursor_overlay_widoczny;
}

void grafika_pobierz_region_kursora(int nowy_x, int nowy_y,
                                    int* x, int* y, int* w, int* h) {
    if (!x || !y || !w || !h) return;
    const int sw = grafika_pobierz_szerokosc();
    const int sh = grafika_pobierz_wysokosc();
    if (sw <= 0 || sh <= 0) {
        *x = *y = *w = *h = 0;
        return;
    }

    const auto* state = aktywny_stan_kursora();
    int kw = 16, kh = 16, hx = 0, hy = 0;
    parametry_kursora(state, &kw, &kh, &hx, &hy);
    const GuiDirtyRect old = kursor_overlay_widoczny
        ? GuiDirtyRect{kursor_overlay_x, kursor_overlay_y,
                       kursor_overlay_w, kursor_overlay_h}
        : GuiDirtyRect{};
    const GuiDirtyRect next{
        nowy_x - hx, nowy_y - hy, kw, kh};
    GuiDirtyRect region = kursor_union(old, next);
    region = kursor_clip(region, sw, sh);
    *x = region.x;
    *y = region.y;
    *w = region.width;
    *h = region.height;
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
    wynik->max_x = grafika_pobierz_szerokosc();
    wynik->max_y = grafika_pobierz_wysokosc();

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
    const uint32_t procent=ui_procent_legacy(skala);

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
                zmaluj ? ((kolor_tekstu & 0x00FFFFFFU) == 0 ? 0x01000000U : kolor_tekstu) : kolor_tla;

            for (int sy = ui_skaluj(y,procent); sy < ui_skaluj(y+1,procent); ++sy) {
                for (int sx = ui_skaluj(x,procent); sx < ui_skaluj(x+1,procent); ++sx) {
                    const int64_t dx =
                        static_cast<int64_t>(px) +
                        static_cast<int64_t>(sx);

                    const int64_t dy =
                        static_cast<int64_t>(py) +
                        static_cast<int64_t>(sy);

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

static size_t dekoduj_utf8_renderera(const char* tekst, size_t i,
                                     size_t limit, uint32_t* znak) {
    if (!tekst || !znak || i >= limit || tekst[i] == '\0') return 0;
    const uint8_t b0 = static_cast<uint8_t>(tekst[i]);
    *znak = b0;
    if (b0 < 0xC2U || b0 > 0xF4U || i + 1 >= limit) return 1;
    const uint8_t b1 = static_cast<uint8_t>(tekst[i + 1]);
    if (b1 == 0 || (b1 & 0xC0U) != 0x80U) return 1;
    if (b0 <= 0xDFU) {
        *znak = (static_cast<uint32_t>(b0 & 0x1FU) << 6) |
                static_cast<uint32_t>(b1 & 0x3FU);
        return 2;
    }
    if (i + 2 >= limit) return 1;
    const uint8_t b2 = static_cast<uint8_t>(tekst[i + 2]);
    if (b2 == 0 || (b2 & 0xC0U) != 0x80U) return 1;
    if (b0 <= 0xEFU) {
        if ((b0 == 0xE0U && b1 < 0xA0U) ||
            (b0 == 0xEDU && b1 > 0x9FU)) return 1;
        *znak = (static_cast<uint32_t>(b0 & 0x0FU) << 12) |
                (static_cast<uint32_t>(b1 & 0x3FU) << 6) |
                static_cast<uint32_t>(b2 & 0x3FU);
        return 3;
    }
    if (i + 3 >= limit) return 1;
    const uint8_t b3 = static_cast<uint8_t>(tekst[i + 3]);
    if (b3 == 0 || (b3 & 0xC0U) != 0x80U ||
        (b0 == 0xF0U && b1 < 0x90U) ||
        (b0 == 0xF4U && b1 > 0x8FU)) return 1;
    *znak = (static_cast<uint32_t>(b0 & 0x07U) << 18) |
            (static_cast<uint32_t>(b1 & 0x3FU) << 12) |
            (static_cast<uint32_t>(b2 & 0x3FU) << 6) |
            static_cast<uint32_t>(b3 & 0x3FU);
    return 4;
}

int WypiszTekst(const char* tekst,
                int px,
                int py,
                uint32_t kolor_tekstu,
                int skala) {
    if (!tekst) return 0;

    skala = ogranicz_skale(skala);

    int64_t pozycja_x = px;
    size_t i = 0;

    const uint32_t max_znaki =
        sizeof(nowa_czcionka_16x16) /
        sizeof(nowa_czcionka_16x16[0]);

    while (i < MAX_TEKST_WEWNETRZNY &&
           tekst[i] != '\0') {

        uint32_t unicode = 0;
        const size_t zuzyte = dekoduj_utf8_renderera(
            tekst, i, MAX_TEKST_WEWNETRZNY, &unicode);

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
                ui_skaluj(szerokosc_znaku+1,ui_procent_legacy(skala)));

        i += zuzyte;
    }

    return static_cast<int>(pozycja_x - px);
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

    const int cur_w = grafika_pobierz_szerokosc();
    const int cur_h = grafika_pobierz_wysokosc();
    uint32_t procent = ekran_skala();
    if (!ui_skala_poprawna(procent)) procent = 100;
    const int zegar_w = ui_skaluj(150, procent);
    const int taskbar_h = ui_skaluj(40, procent);
    if (cur_w < zegar_w || cur_h < taskbar_h) {
        return;
    }

    czas_rtc czas;
    pobierz_czas_rtc(&czas);

    char bufor_czasu[16] = {};
    formatuj_czas_do_stringa(
        &czas,
        bufor_czasu);

    int tekst_szer = 0;
    for (size_t i = 0; bufor_czasu[i] != '\0'; ++i) {
        const uint32_t znak = static_cast<uint8_t>(bufor_czasu[i]);
        int szer_znaku = 8;
        if (znak < sizeof(nowa_czcionka_szerokosci) /
                       sizeof(nowa_czcionka_szerokosci[0]))
            szer_znaku = nowa_czcionka_szerokosci[znak];
        if (szer_znaku < 1) szer_znaku = 1;
        if (szer_znaku > 16) szer_znaku = 16;
        tekst_szer += ui_skaluj(szer_znaku + 1,
                                static_cast<uint32_t>(ui_skaluj(200, procent)));
    }
    const ZegarUiGeometria g =
        zegar_ui_geometria(cur_w, cur_h, procent, tekst_szer);

    const BursztynMotyw* m = motyw_pobierz_aktywny();
    const uint32_t tlo_kolor = m ? m->panel : 0x0020242AU;
    const uint32_t ramka_kolor = m ? m->ramka : 0x00343A42U;
    const uint32_t tekst_kolor = m ? m->akcent : 0x00F2A900U;

    RysujProstokat(g.x, g.y, g.szer, g.wys, tlo_kolor);
    RysujProstokat(g.x, g.y, g.szer, 1, ramka_kolor);

    WypiszTekst(
        bufor_czasu,
        g.tekst_x,
        g.tekst_y,
        tekst_kolor,
        g.skala_fontu);
}

static bool g_pasek_zadan_gotowy = false;

void grafika_ustaw_pasek_zadan_gotowy(bool gotowy) {
    g_pasek_zadan_gotowy = gotowy;
}

bool grafika_czy_pasek_zadan_gotowy(void) {
    return g_pasek_zadan_gotowy;
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
            const int pasek_w = static_cast<int>(aktywny_ekran->PobierzSzerokosc());

            for (int y = 0; y < 40; ++y) {
                uint32_t gray = 108 - (56 * y) / 39;
                uint32_t kolor = (gray << 16) | (gray << 8) | gray;
                RysujProstokat(0, pasek_y + y, pasek_w, 1, kolor);
            }

            WypiszTekst(
                "Bursztyn OS - Terminal",
                20,
                static_cast<int>(
                    aktywny_ekran->PobierzWysokosc()) - 28,
                0x00FFBF00,
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

// Retain pointer movement and lifecycle delivery; consume interaction while
// the system welcome surface covers the desktop. No app receives splash focus.
static bool grafika_dostarcz_zdarzenie(int pid,const bws_zdarzenie* e) {
    if(powitanie_aktywne()) {
        switch(e->typ) {
        case BWS_ZDARZENIE_MYSZ_RUCH: case BWS_ZDARZENIE_MYSZ_DOWN:
        case BWS_ZDARZENIE_MYSZ_UP: case BWS_ZDARZENIE_MYSZ_PRAWY_DOWN:
        case BWS_ZDARZENIE_MYSZ_PRAWY_UP: case BWS_ZDARZENIE_MYSZ_KOLO:
        case BWS_ZDARZENIE_KLAWISZ: case BWS_ZDARZENIE_KLAWISZ_FIZYCZNY:
        case BWS_ZDARZENIE_TEXT_INPUT: case BWS_ZDARZENIE_WSKAZNIK_PRECYZYJNY: return true;
        default: break;
        }
    }
    return scheduler_dodaj_zdarzenie(pid,e);
}

// =========================================================================
// KLAWIATURA
// =========================================================================

// System window switching is handled before application delivery, so a busy
// Ring 3 client cannot capture Alt+Tab as ordinary control-focus traversal.
static bool system_tab_down = false;
static void przelacz_okno_klawiatura(bool odwrotnie) {
    GuiOknoInfo windows[SKLADACZ_MAKS_WARSTW]{};
    const unsigned count=skladacz_obrazu_snapshot_okien(windows,SKLADACZ_MAKS_WARSTW,aktywny_pid_gui);
    if(!count)return;
    unsigned active=odwrotnie?0:count-1;
    for(unsigned i=0;i<count;++i)if(windows[i].aktywne){active=i;break;}
    for(unsigned step=1;step<=count;++step){
        const unsigned i=odwrotnie?(active+count-step)%count:(active+step)%count;
        if(windows[i].aktywne || !windows[i].tytul[0])continue;
        if(bws_gui_aktywuj_okno(windows[i].window_id))break;
    }
}

extern "C" bool zaktualizuj_klawiature_gui(char znak) {
    if(system_tab_down && znak=='\t')return true;
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
    (void)grafika_dostarcz_zdarzenie(aktywny_pid_gui, &e);
    /* Aktywny model GUI jest wylacznym odbiorca. Nawet przepelnienie jego
       kolejki nie moze spowodowac double-delivery do legacy byte-streamu. */
    return true;
}

extern "C" bool zaktualizuj_klawisz_fizyczny_gui(
    uint32_t kod,
    bool wcisniety,
    uint32_t modyfikatory
) {
    const bool tab=(kod&BWS_KEY_SOURCE_USB)?(kod&255U)==43U:
        !(kod&BWS_KEY_SOURCE_E0) && (kod&127U)==15U;
    if(tab && !wcisniety && system_tab_down){system_tab_down=false;return true;}
    if(tab && wcisniety && (modyfikatory&BWS_MOD_ALT) &&
       !(modyfikatory&(BWS_MOD_CTRL|BWS_MOD_SUPER)) && !powitanie_aktywne()){
        system_tab_down=true;
        przelacz_okno_klawiatura((modyfikatory&BWS_MOD_SHIFT)!=0);
        return true;
    }
    if (aktywny_pid_gui <= 0) return false;
    bws_zdarzenie e{};
    e.typ = BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;
    e.x = wcisniety ? 1 : 0;
    e.przyciski = modyfikatory;
    e.kod = kod;
    e.timestamp = znacznik_zdarzenia();
    (void)grafika_dostarcz_zdarzenie(aktywny_pid_gui, &e);
    return true;
}

extern "C" bool zaktualizuj_tekst_gui(
    uint32_t codepoint,
    uint32_t modyfikatory
) {
    if(system_tab_down && codepoint=='\t')return true;
    if (aktywny_pid_gui <= 0 || codepoint == 0 || codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) return false;
    bws_zdarzenie e{};
    e.typ = BWS_ZDARZENIE_TEXT_INPUT;
    e.przyciski = modyfikatory;
    e.kod = codepoint;
    e.timestamp = znacznik_zdarzenia();
    (void)grafika_dostarcz_zdarzenie(aktywny_pid_gui, &e);
    return true;
}

// =========================================================================
// KURSOR
// =========================================================================

void UkryjKursor() {
    if (kursor_plane_aktywny && aktywny_ekran) {
        aktywny_ekran->UkryjKursor();
        kursor_plane_aktywny=false;
        // VirtIO UPDATE_CURSOR with resource_id=0 clears the plane. The next
        // show must re-upload pixels; MOVE_CURSOR alone leaves an invisible tip.
        kursor_plane_ksztalt_brudny=true;
        return;
    }
    if (!kursor_overlay_widoczny || !backbuffer || !aktywny_ekran) return;
    PrzeniesFragmentNaEkran(kursor_overlay_x,kursor_overlay_y,
                            kursor_overlay_w,kursor_overlay_h);
    kursor_overlay_widoczny=false;
}

void PokazKursor() {
    grafika_prezentuj_kursor();
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

    // Losing a GUI owner never activates the obsolete kernel terminal.
}

static int znajdz_warstwe_pod_punktem(int x, int y) {
    return wm_warstwa_pod_punktem(x,y,
        skladacz_obrazu_overlay_pod_punktem(x,y),
        [](int pid) { return skladacz_obrazu_czy_widoczna(pid) ? pobierz_warstwe(pid) : nullptr; },
        [](int pid,int lx,int ly) { return skladacz_obrazu_dekoracja_w_punkcie(pid,lx,ly); },
        [](int pid,int lx,int ly) { return skladacz_obrazu_ksztalt_w_punkcie(pid,lx,ly); });
}

static void ustaw_focus_gui(int pid) {
    if (pid == aktywny_pid_gui) return;
    const int stary = aktywny_pid_gui;
    if (stary > 0)
        (void)skladacz_obrazu_ustaw_popup_aplikacji(
            stary, false, 0, 0, 0, 0);
    aktywny_pid_gui = pid;
    kursor_plane_ksztalt_brudny = true;
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

static uint64_t mouse_last_gen = 0;
static int mouse_last_max_x = -1;
static int mouse_last_max_y = -1;
static bool mouse_first_usb_logged = false;
static bool mouse_first_ps2_logged = false;
static bool mouse_first_mysz_logged = false;

static void log_granice_kursora(const char* zrodlo, uint32_t sw, uint32_t sh, uint64_t gen) {
    char s[140]{};
    char* p = s;
    char* e = s + sizeof(s);
    auto app = [&](const char* str) { while (*str && p + 1 < e) *p++ = *str++; };
    auto app_d = [&](uint64_t val) {
        char buf[24]; int i = 0;
        do { buf[i++] = static_cast<char>('0' + (val % 10)); val /= 10; } while (val > 0);
        while (i > 0 && p + 1 < e) *p++ = buf[--i];
    };
    app("[MOUSE BOUNDS] source=");
    app(zrodlo ? zrodlo : "cursor");
    app(" screen=");
    app_d(sw);
    app("x");
    app_d(sh);
    app(" max=");
    app_d(sw > 0 ? sw - 1 : 0);
    app("x");
    app_d(sh > 0 ? sh - 1 : 0);
    app(" gen=");
    app_d(gen);
    *p = '\0';
    klog_serial(s);
}

extern "C" void input_device_pobierz_ostatnie_abs(int32_t* rx, int32_t* ry);

static void zaktualizuj_mysze_wspolne(int64_t dx,
                                         int64_t dy,
                                         uint8_t przyciski,
                                         const char* zrodlo) {
    if (!backbuffer || !aktywny_ekran)
        return;

    GraphicsPerf::input();
    skladacz_obrazu_zarejestruj_raport_myszy(dx != 0 || dy != 0);

    NaprawWlascicielaMyszyJesliProcesZniknal();

    const int stary_mysz_x = mysz_x;
    const int stary_mysz_y = mysz_y;

    const int ekran_szer = grafika_pobierz_szerokosc();
    const int ekran_wys = grafika_pobierz_wysokosc();
    if (ekran_szer <= 0 || ekran_wys <= 0) return;
    const int nowy_x = pointer_accumulate(mysz_x, int64_t(dx), ekran_szer);
    // Legacy graphics entry uses upward Y. InputDevice's screen-down adapter
    // changes representation here; PS/2 protocol inversion stays in its parser.
    const int nowy_y = pointer_accumulate(mysz_y, -int64_t(dy), ekran_wys);

    pointing_count(pointing_counters.gui_consumed);
    mysz_x = static_cast<int>(nowy_x);
    mysz_y = static_cast<int>(nowy_y);
    pointing_count(pointing_counters.cursor_commits);
    kursor_diag_logical();

    const uint64_t gen = grafika_generacja_ekranu();
    const int max_x = ekran_szer > 0 ? ekran_szer - 1 : 0;
    const int max_y = ekran_wys > 0 ? ekran_wys - 1 : 0;

    const bool is_usb = zrodlo && (zrodlo[0] == 'u' || zrodlo[0] == 'U');
    const bool is_ps2 = zrodlo && (zrodlo[0] == 'p' || zrodlo[0] == 'P');
    bool first_for_source = false;
    if (is_usb && !mouse_first_usb_logged) {
        mouse_first_usb_logged = true;
        first_for_source = true;
    } else if (is_ps2 && !mouse_first_ps2_logged) {
        mouse_first_ps2_logged = true;
        first_for_source = true;
    } else if (!is_usb && !is_ps2 && !mouse_first_mysz_logged) {
        mouse_first_mysz_logged = true;
        first_for_source = true;
    }

    const bool bounds_changed = (max_x != mouse_last_max_x || max_y != mouse_last_max_y);
    const bool gen_changed = (gen != mouse_last_gen);

    if (first_for_source || bounds_changed || gen_changed) {
        mouse_last_gen = gen;
        mouse_last_max_x = max_x;
        mouse_last_max_y = max_y;

        char s[200]{};
        char* p = s;
        char* e = s + sizeof(s);
        auto app = [&](const char* str) {
            while (*str && p + 1 < e) *p++ = *str++;
        };
        auto app_d = [&](int64_t val) {
            if (val < 0) {
                if (p + 1 < e) *p++ = '-';
                val = -val;
            }
            char buf[24];
            int i = 0;
            do {
                buf[i++] = static_cast<char>('0' + (val % 10));
                val /= 10;
            } while (val > 0);
            while (i > 0 && p + 1 < e) *p++ = buf[--i];
        };
        app("[MOUSE BOUNDS] source=");
        app(zrodlo ? zrodlo : "mysz");
        app(" screen=");
        app_d(ekran_szer);
        app("x");
        app_d(ekran_wys);
        app(" max=");
        app_d(max_x);
        app("x");
        app_d(max_y);
        app(" cur=");
        app_d(stary_mysz_x);
        app(",");
        app_d(stary_mysz_y);
        app(" dx=");
        app_d(dx);
        app(" dy=");
        app_d(dy);
        app(" res=");
        app_d(mysz_x);
        app(",");
        app_d(mysz_y);
        app(" gen=");
        app_d(static_cast<int64_t>(gen));
        *p = '\0';
        klog_serial(s);
    }

    if (pid_przejmujacy_mysz != -1) {
        lewy_wcisniety =
            (przyciski & MYSZ_PRZYCISK_LEWY) != 0;

        static uint8_t poprzednie_przyciski_gui = 0;
        const bool klik = (przyciski & MYSZ_PRZYCISK_LEWY) != 0 &&
                          (poprzednie_przyciski_gui & MYSZ_PRZYCISK_LEWY) == 0;
        const bool pusc = (przyciski & MYSZ_PRZYCISK_LEWY) == 0 &&
                          (poprzednie_przyciski_gui & MYSZ_PRZYCISK_LEWY) != 0;
        const bool prawy_klik = (przyciski & MYSZ_PRZYCISK_PRAWY) != 0 &&
            (poprzednie_przyciski_gui & MYSZ_PRZYCISK_PRAWY) == 0;
        const bool prawy_pusc = (przyciski & MYSZ_PRZYCISK_PRAWY) == 0 &&
            (poprzednie_przyciski_gui & MYSZ_PRZYCISK_PRAWY) != 0;
        if (klik || prawy_klik) {
            /* Nowa sekwencja przycisku nie moze odziedziczyc capture po
               poprzednim, niedokonczonym drag. Focus i adresat DOWN musza
               wskazywac ten sam hit-testowany proces. */
            capture_pid_gui = -1;
            const int trafiony = znajdz_warstwe_pod_punktem(mysz_x, mysz_y);
            skladacz_obrazu_podnies_warstwe(trafiony);
            ustaw_focus_gui(trafiony);
        }
        const bool motion_only=(dx!=0 || dy!=0) && !klik && !pusc && !prawy_klik && !prawy_pusc;
        const int cel=wm_cel_ruchu(aktywny_pid_gui,capture_pid_gui,
            motion_only?znajdz_warstwe_pod_punktem(mysz_x,mysz_y):-1,motion_only);
        // One boundary event clears hover in the previous owner. Further
        // unrelated motion wakes only the currently hit window.
        if(motion_only) {
            const int leave=wm_opuszczony_cel_ruchu(motion_hover_pid_gui,cel,capture_pid_gui);
            if(leave>0 && pobierz_warstwe(leave)) {
                bws_zdarzenie out{};out.typ=BWS_ZDARZENIE_MYSZ_RUCH;
                out.x=mysz_x;out.y=mysz_y;out.dx=dx;out.dy=-dy;
                out.przyciski=przyciski;out.kod=klawiatura_modyfikatory();out.timestamp=znacznik_zdarzenia();
                grafika_dostarcz_zdarzenie(leave,&out);
            }
        }
        if (klik) {
#if BURSZTYN_DEBUG_GUI_INPUT
            static uint32_t s_click_trace_cnt = 0;
            if (s_click_trace_cnt < 30) {
                ++s_click_trace_cnt;
                int32_t raw_x = -1, raw_y = -1;
                input_device_pobierz_ostatnie_abs(&raw_x, &raw_y);
                const int sw = grafika_pobierz_szerokosc();
                const int sh = grafika_pobierz_wysokosc();
                char cbuf[256]{};
                char* cp = cbuf;
                char* cep = cbuf + sizeof(cbuf);
                auto capp = [&](const char* str) { while (*str && cp + 1 < cep) *cp++ = *str++; };
                auto capp_i = [&](int64_t v) {
                    if (v < 0) { if (cp + 1 < cep) *cp++ = '-'; v = -v; }
                    char b[24]; int idx = 0;
                    do { b[idx++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v);
                    while (idx > 0 && cp + 1 < cep) *cp++ = b[--idx];
                };
                capp("[CLICK TRACE]\n  rawabs=");
                capp_i(raw_x); capp(","); capp_i(raw_y);
                capp("\n  logical="); capp_i(mysz_x); capp(","); capp_i(mysz_y);
                capp("\n  cursor="); capp_i(mysz_x); capp(","); capp_i(mysz_y);
                capp("\n  wmevent="); capp_i(mysz_x); capp(","); capp_i(mysz_y);
                capp("\n  framebuffer="); capp_i(sw); capp("x"); capp_i(sh);
                capp("\n  compositor="); capp_i(sw); capp("x"); capp_i(sh);
                capp("\n  desktop="); capp_i(sw); capp("x"); capp_i(sh);
                capp("\n  target PID="); capp_i(cel);
                warstwa_obrazu* w = (cel > 0 && cel < SKLADACZ_MAKS_WARSTW) ? pobierz_warstwe(cel) : nullptr;
                if (w) {
                    capp(" bounds=("); capp_i(w->x); capp(","); capp_i(w->y);
                    capp(")-("); capp_i(w->x + w->szerokosc); capp(","); capp_i(w->y + w->wysokosc);
                    capp(")");
                }
                capp(" coords=("); capp_i(mysz_x); capp(","); capp_i(mysz_y); capp(")\n");
                *cp = '\0';
                klog_serial(cbuf);
            }
#endif
        }
        if (cel > 0) {
            if (klik || pusc || dx != 0 || dy != 0) {
                bws_zdarzenie e{};
                e.typ = klik ? BWS_ZDARZENIE_MYSZ_DOWN :
                        (pusc ? BWS_ZDARZENIE_MYSZ_UP : BWS_ZDARZENIE_MYSZ_RUCH);
                e.x = mysz_x; e.y = mysz_y; e.dx = dx; e.dy = -dy;
                e.przyciski = przyciski;
                e.kod = klawiatura_modyfikatory();
                e.timestamp = znacznik_zdarzenia();
                grafika_dostarcz_zdarzenie(cel, &e);
                if (dx != 0 || dy != 0) {
                    e.typ = BWS_ZDARZENIE_WSKAZNIK_PRECYZYJNY;
                    grafika_dostarcz_zdarzenie(cel, &e);
                }
            }
            if (prawy_klik || prawy_pusc) {
                bws_zdarzenie e{};
                e.typ = prawy_klik ? BWS_ZDARZENIE_MYSZ_PRAWY_DOWN
                                   : BWS_ZDARZENIE_MYSZ_PRAWY_UP;
                e.x = mysz_x; e.y = mysz_y; e.dx = dx; e.dy = -dy;
                e.przyciski = przyciski;
                e.kod = klawiatura_modyfikatory();
                e.timestamp = znacznik_zdarzenia();
                grafika_dostarcz_zdarzenie(cel, &e);
            }
        }
        if (pusc) capture_pid_gui = -1;
        poprzednie_przyciski_gui = przyciski;

        kursor_diag_wm();
        skladacz_obrazu_oznacz_ruch_kursora(
            stary_mysz_x, stary_mysz_y, mysz_x, mysz_y);
        WybudzProcesyOczekujaceNaMysz();
        return;
    }

    const bool nowy_lewy =
        (przyciski & MYSZ_PRZYCISK_LEWY) != 0;

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

extern "C" void zaktualizuj_mysze_zrodlo(int dx,int dy,uint8_t buttons,const char* source) {
    zaktualizuj_mysze_wspolne(dx,dy,buttons,source);
}
extern "C" void zaktualizuj_mysze_wzgledne_zrodlo(int32_t dx,int32_t dy,uint8_t buttons,const char* source) {
    zaktualizuj_mysze_wspolne(dx,-int64_t(dy),buttons,source);
}

extern "C" void zaktualizuj_mysze(int dx,
                                  int dy,
                                  uint8_t przyciski) {
    zaktualizuj_mysze_zrodlo(dx, dy, przyciski, "mysz");
}

extern "C" int grafika_pobierz_mysz_x() {
    return mysz_x;
}

extern "C" int grafika_pobierz_mysz_y() {
    return mysz_y;
}

extern "C" void zaktualizuj_mysze_pozycja_zrodlo(int x,
                                                 int y,
                                                 uint8_t przyciski,
                                                 const char* zrodlo) {
    if (!backbuffer || !aktywny_ekran) return;
    const int dx = x - mysz_x;
    const int dy = mysz_y - y;
    zaktualizuj_mysze_zrodlo(dx, dy, przyciski, zrodlo);
}

extern "C" void zaktualizuj_kolo_myszy(
    int pion,
    int poziom,
    uint8_t przyciski
) {
    if ((pion == 0 && poziom == 0) || aktywny_pid_gui <= 0) return;
    const int cel = capture_pid_gui > 0 ? capture_pid_gui : aktywny_pid_gui;
    if (cel <= 0) return;
    bws_zdarzenie e{};
    e.typ = BWS_ZDARZENIE_MYSZ_KOLO;
    e.x = mysz_x; e.y = mysz_y;
    e.dx = poziom; e.dy = pion;
    e.przyciski = przyciski;
    e.kod = klawiatura_modyfikatory();
    e.timestamp = znacznik_zdarzenia();
    (void)grafika_dostarcz_zdarzenie(cel, &e);
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
    uint32_t rtc_scale = ekran_skala();
    if (!ui_skala_poprawna(rtc_scale)) rtc_scale = 100;
    const ZegarUiGeometria rtc = zegar_ui_geometria(
        grafika_pobierz_szerokosc(), grafika_pobierz_wysokosc(),
        rtc_scale, 0);
    skladacz_obrazu_oznacz_dirty_rect(rtc.x, rtc.y, rtc.szer, rtc.wys);
    bws_zdarzenie e{};
    e.typ = BWS_ZDARZENIE_TIMER;
    e.timestamp = znacznik_zdarzenia();
    /* Timer GUI ma czestotliwosc zegara pulpitu (1 Hz), ale trafia do
       kazdej istniejacej aplikacji z warstwa. Aplikacje same decyduja,
       czy potrzebuja wykonac okresowa prace; nie jest to tick compositora. */
    for (int pid = 1; pid < SKLADACZ_MAKS_WARSTW; ++pid) {
        warstwa_obrazu* w = pobierz_warstwe(pid);
        if (w) scheduler_dodaj_zdarzenie(pid, &e);
    }
}

// =========================================================================
// BUFOR I OBSLUGA TAPETY
// =========================================================================

bool grafika_przygotuj_bufory_tapety() {
    if (!aktywny_ekran || !backbuffer) return false;
    const uint64_t w = aktywny_ekran->PobierzSzerokosc();
    const uint64_t h = aktywny_ekran->PobierzWysokosc();
    if (!w || !h || w > 8192 || h > 8192 || w * h > MAX_RAW_BMP / 4) return false;
    if(!bufor_tapety || tapeta_zmapowane_bajty < size_t(w*h*4)) {
        if (bufor_tapety) {
            bufor_grafiki_zwolnij(bufor_tapety);
        }
        bufor_tapety=static_cast<uint32_t*>(bufor_grafiki_alokuj(size_t(w*h*4)));
        if(!bufor_tapety)return false;
        tapeta_zmapowane_bajty=size_t(w*h*4);
    }
    bufor_tapety_zmapowany = true;
    if (!mapuj_bufor_tapety(VADDR_RAW_BMP, MAX_RAW_BMP, &scratch_zmapowane_bajty)) return false;
    surowy_bufor_bmp_zmapowany = true;
    return true;
}

uint8_t* grafika_scratch_tapety(size_t* pojemnosc) {
    if (pojemnosc) *pojemnosc = scratch_zmapowane_bajty;
    return surowy_bufor_bmp_zmapowany ? reinterpret_cast<uint8_t*>(VADDR_RAW_BMP) : nullptr;
}

uint32_t* grafika_bufor_tapety(size_t* pojemnosc) {
    if (pojemnosc) *pojemnosc = tapeta_zmapowane_bajty;
    return bufor_tapety_zmapowany ? bufor_tapety : nullptr;
}

void grafika_publikuj_tapete() {
    if (bufor_tapety_zmapowany && bufor_tapety) {
        tapeta_zaladowana = true;
        skladacz_obrazu_oznacz_dirty();
    }
}
bool grafika_czy_tapeta() { return tapeta_zaladowana && bufor_tapety; }
void grafika_tapeta_solid() {
    tapeta_zaladowana = false;
    skladacz_obrazu_oznacz_dirty();
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

static void grafika_reset_startu() {
    PrezentacjaStartu::reset();KlatkiStartuDiag::reset();
    powitanie_wycisz();grafika_wyciszona=false;skladacz_obrazu_wznow();
}
void InicjalizujGrafike(uint64_t adres_mb2) {
    KlatkiStartuDiag::Sciezka diagnostic_path("graphics/init");
    grafika_reset_startu();
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
#if BURSZTYN_DEBUG_GFX
    uint8_t TYP_PIKSELA = 0xFFU;
    uint8_t RED_POS = 0, RED_MASK = 0;
    uint8_t GREEN_POS = 0, GREEN_MASK = 0;
    uint8_t BLUE_POS = 0, BLUE_MASK = 0;
#endif

    bool mamy_efi = false;

    while (aktualny + 8ULL <= koniec) {
        TagFramebufferMB2* tag =
            reinterpret_cast<TagFramebufferMB2*>(aktualny);

        if (tag->typ == 0)
            break;

        if (tag->rozmiar < 8 || tag->rozmiar > koniec-aktualny)
            break;

        if (tag->typ == 11 ||
            tag->typ == 12) {
            mamy_efi = true;
        }

        if (tag->typ == 8 && tag->rozmiar >= sizeof(TagFramebufferMB2)+6 &&
            tag->typ_bufora == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
            const uint8_t* kanaly=reinterpret_cast<const uint8_t*>(tag)+sizeof(TagFramebufferMB2);
            if(kanaly[0]!=16||kanaly[1]!=8||kanaly[2]!=8||kanaly[3]!=8||kanaly[4]!=0||kanaly[5]!=8) { aktualny += (uint64_t(tag->rozmiar)+7)&~7ULL; continue; }
            LFB = tag->adres_fizyczny;
            PITCH = tag->pitch;
            SZER = tag->szerokosc;
            WYS = tag->wysokosc;
            BPP = tag->bpp;
#if BURSZTYN_DEBUG_GFX
            TYP_PIKSELA = tag->typ_bufora;
            if (tag->typ_bufora == MULTIBOOT_FRAMEBUFFER_TYPE_RGB &&
                tag->rozmiar >= sizeof(TagFramebufferMB2) + 6U) {
                const uint8_t* rgb =
                    reinterpret_cast<const uint8_t*>(tag) +
                    sizeof(TagFramebufferMB2);
                RED_POS = rgb[0]; RED_MASK = rgb[1];
                GREEN_POS = rgb[2]; GREEN_MASK = rgb[3];
                BLUE_POS = rgb[4]; BLUE_MASK = rgb[5];
            }
#endif
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

    if(!bufory_grafiki_inicjalizuj()) { SerialLog("[EKRAN] BLAD puli PMM/VMM.\n"); while(true) asm volatile("cli; hlt"); }
    TrybEkranu boot_tryb{SZER,WYS,BPP,PITCH,0,0};
    ekran_vram=lfb_waga;
    const bool bochs=bochs_wykryj(LFB,boot_tryb,&ekran_vram);
    ekran_caps={bochs?1U:0U,bochs?1U:0U,0U,bochs?EKRAN_BOCHS:(mamy_efi?EKRAN_GOP:EKRAN_VESA)};
    uint64_t framebuffer_wirtualny = 0;

    if (!zmapuj_framebuffer(
            LFB,
            ekran_vram,
            &framebuffer_wirtualny)) {

        SerialLog(
            "[GRAFIKA] BLAD KRYTYCZNY: Nie mozna zmapowac framebufferu.\n");

        while (true)
            asm volatile("cli; hlt");
    }

    backbuffer=static_cast<uint8_t*>(bufor_grafiki_alokuj(lfb_waga));
    if(!backbuffer) { SerialLog("[EKRAN] BLAD backbuffer PMM/VMM.\n"); while(true) asm volatile("cli; hlt"); }
    backbuffer_bajty=lfb_waga;backbuffer_pozyczony=false;
    ekran_lfb_va=framebuffer_wirtualny;

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

#if BURSZTYN_DEBUG_GFX
    uint64_t mapowane_z_offsetem = lfb_waga + (LFB & (ROZMIAR_STRONY - 1ULL));
    uint64_t mapowane = 0;
    (void)wyrownaj_do_strony(mapowane_z_offsetem, &mapowane);
    SerialLog("[GFX-DEBUG]\nbackend=");
    SerialLog(mamy_efi ? "GOP\n" : "VBE\n");
    LogGfxU64("framebuffer_phys=", LFB, true);
    LogGfxU64("width=", SZER);
    LogGfxU64("height=", WYS);
    LogGfxU64("pitch=", PITCH);
    LogGfxU64("bpp=", BPP);
    LogGfxU64("bytes_per_pixel=", BPP / 8U);
    SerialLog("pixel_format=");
    if (TYP_PIKSELA == MULTIBOOT_FRAMEBUFFER_TYPE_RGB &&
        RED_POS == 16U && GREEN_POS == 8U && BLUE_POS == 0U &&
        RED_MASK == 8U && GREEN_MASK == 8U && BLUE_MASK == 8U) {
        SerialLog("XRGB8888\n");
    } else if (TYP_PIKSELA == MULTIBOOT_FRAMEBUFFER_TYPE_RGB &&
               RED_POS == 0U && GREEN_POS == 8U && BLUE_POS == 16U &&
               RED_MASK == 8U && GREEN_MASK == 8U && BLUE_MASK == 8U) {
        SerialLog("XBGR8888\n");
    } else if (TYP_PIKSELA == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
        SerialLog("RGB-custom\n");
    } else {
        SerialLog("non-RGB\n");
    }
    LogGfxU64("mapped_size=", mapowane);
    SerialLog("cache_flags=PRESENT|RW|PCD(UC-); identity_alias=");
    SerialLog(LFB < LIMIT_IDENTITY_MAP ? "same-flags\n" : "none\n");
#endif

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

    aktywny_stan={SZER,WYS,PITCH,BPP,framebuffer_wirtualny,1};
    boot_lfb_va = framebuffer_wirtualny;
    boot_szer = SZER;
    boot_wys = WYS;
    boot_pitch = PITCH;
    boot_bpp = BPP;
    boot_sterownik = aktywny_ekran;
    boot_ekran_caps = ekran_caps;
    log_granice_kursora("cursor", SZER, WYS, 1);

#if BURSZTYN_DIAG_BOOT
    SerialLog("[BOOT-TELEMETRY] boot_framebuffer_phys=");
    SerialHex64(LFB);
    SerialLog("\n[BOOT-TELEMETRY] boot_framebuffer_virt=");
    SerialHex64(framebuffer_wirtualny);
    SerialLog("\n[BOOT-TELEMETRY] boot_geometry=");
    SerialU64(SZER);
    SerialLog("x");
    SerialU64(WYS);
    SerialLog(" pitch=");
    SerialU64(PITCH);
    SerialLog(" bpp=");
    SerialU64(BPP);
    SerialLog("\n[BOOT-TELEMETRY] active_backend_before_virtio=");
    SerialLog(mamy_efi ? "UEFI GOP" : (bochs ? "Bochs VBE" : "VESA VBE"));
    SerialLog("\n[BOOT-TELEMETRY] compositor_backbuffer=");
    SerialHex64(reinterpret_cast<uint64_t>(backbuffer));
    SerialLog(" ");
    SerialU64(SZER);
    SerialLog("x");
    SerialU64(WYS);
    SerialLog("\n");
#endif

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

    if(bochs) {
        static constexpr uint32_t wymiary[][2]={
            {800,600},{1024,768},{1280,720},{1280,800},{1360,768},
            {1600,900},{1920,1080},{2560,1440},{3440,1440},{3840,2160}
        };
        bool boot_dodany = false;
        for(const auto& wh:wymiary) {
            TrybEkranu t{wh[0],wh[1],32,wh[0]*4,0,0};
            if(ekran_ten_sam(t,boot_tryb)) {
                if(ekran_liczba_trybow<EKRAN_MAX_TRYBOW) {
                    ekran_tryby[ekran_liczba_trybow++]=boot_tryb;
                    boot_dodany = true;
                }
                continue;
            }
            if(!bochs_sprawdz_tryb(t))continue;
            size_t n=0; if(!ekran_rozmiar(t,&n))continue;
            void* probe[3]={}; bool ok=true;
            for(auto& p:probe) { p=bufor_grafiki_alokuj(n); if(!p)ok=false; }
            for(auto p:probe)bufor_grafiki_zwolnij(p);
            if(ok&&ekran_liczba_trybow<EKRAN_MAX_TRYBOW)ekran_tryby[ekran_liczba_trybow++]=t;
        }
        if(!boot_dodany && ekran_liczba_trybow<EKRAN_MAX_TRYBOW) {
            ekran_tryby[ekran_liczba_trybow++]=boot_tryb;
        }
    } else {
        ekran_tryby[ekran_liczba_trybow++]=boot_tryb;
    }
    klog_serial(bochs?"[EKRAN] Backend Bochs VBE (PCI/BAR/DISPI potwierdzone).":
        (mamy_efi?"[EKRAN] Backend UEFI GOP (framebuffer).":"[EKRAN] Backend VESA (framebuffer)."));
    // Initialize off-screen; retain firmware pixels until the startup policy
    // can publish a complete welcome (or the compositor's complete desktop).
    grafika_odtworz_tlo_skladania();
    SplashFirstDiag::event("graphics_ready",czas_monotoniczny_us());
    if(BURSZTYNDIAGSPLASHRUNTIMEFRAMES)KlatkiStartuDiag::event("InicjalizujGrafike",mamy_efi?"GOP/init":"VESA/init",czas_monotoniczny_us(),
        reinterpret_cast<const void*>(framebuffer_wirtualny),backbuffer,0,{0,0,int(SZER),int(WYS)},
        reinterpret_cast<const uint32_t*>(framebuffer_wirtualny),SZER,WYS,PITCH/4,false);
    wypisz_log("[SYSTEM] Bursztyn OS HAL gotowy!");
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

    char text_bezp[MAX_TEKST_GUI_BWS] = {};

    if (!skopiuj_string_z_uzytkownika(
            text_bezp,
            text,
            sizeof(text_bezp))) {
        return;
    }

    const int szer_px = WypiszTekst(
        text_bezp,
        x,
        y,
        0x00D1D5DB,
        1);
    zapamietaj_dirty_rysowania(x, y, szer_px, 16);
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
        if (skladacz_obrazu_publikuj_klatke(aktualny_pid) &&
            skladacz_obrazu_czy_widoczna(aktualny_pid))
            bws_gui_powiadom_lifecycle(BWS_ZDARZENIE_OKNO_POKAZANE, aktualny_pid);
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

    if ((skala < 1 || skala > MAX_SKALA_TEKSTU) && !(skala>=100&&skala<=400)) {
        return;
    }

    char text_bezp[MAX_TEKST_GUI_BWS] = {};

    if (!skopiuj_string_z_uzytkownika(
            text_bezp,
            text,
            sizeof(text_bezp))) {
        return;
    }

    const int szer_px = WypiszTekst(
        text_bezp,
        x,
        y,
        kolor,
        skala);
    zapamietaj_dirty_rysowania(x, y, szer_px, ui_skaluj(16,ui_procent_legacy(skala)));
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

static bool gui_blit_region(
    int x,
    int y,
    int szer,
    int wys,
    const uint32_t* piksele, bool zastap_przezroczyste, bool alfa=false,bool batch=false) {

    if (!piksele) return false;
    if (!backbuffer || !aktywny_ekran ||
        !BwsProcesMaWarstwe() ||
        !BwsPoprawnyWymiar(szer, wys)) {
        return false;
    }
    if (szer > 2048 || wys > 2048) return false;
    uint64_t cnt = 0;
    if (__builtin_mul_overflow((uint64_t)szer, (uint64_t)wys, &cnt)) return false;
    // limit 4M pikseli (~16 MiB)
    if (cnt > 4ULL * 1024ULL * 1024ULL) return false;

    warstwa_obrazu* warstwa = pobierz_warstwe(aktualny_pid);
    if (!warstwa || !warstwa->bufor_pikseli) return false;

    int x1 = x + szer;
    int y1 = y + wys;
    int lx0 = x < warstwa->x ? warstwa->x : x;
    int ly0 = y < warstwa->y ? warstwa->y : y;
    int lx1 = x1 > warstwa->x + (int)warstwa->szerokosc ? warstwa->x + (int)warstwa->szerokosc : x1;
    int ly1 = y1 > warstwa->y + (int)warstwa->wysokosc ? warstwa->y + (int)warstwa->wysokosc : y1;
    if (lx0 >= lx1 || ly0 >= ly1) return false;

    /*
     * SMAP: bufor pikseli nalezy do Ring 3. Cala petla kopiujaca musi
     * pozostac w oknie AC, inaczej pierwszy odczyt wywola #PF.
     */
    OknoUzytkownika okno(piksele, (size_t)cnt * sizeof(uint32_t));

    for (int dy = ly0; dy < ly1; ++dy) {
        int srcY = dy - y;
        uint64_t srcRow = (uint64_t)srcY * (uint64_t)szer;
        uint64_t dstRow = (uint64_t)(dy - warstwa->y) * (uint64_t)warstwa->szerokosc;
        for (int dx = lx0; dx < lx1; ++dx) {
            int srcX = dx - x;
            uint64_t srcIdx = srcRow + (uint64_t)srcX;
            uint32_t pix = piksele[srcIdx];
            if (!zastap_przezroczyste && pix == SKLADACZ_PIKSEL_PRZEZROCZYSTY) continue;
            uint64_t dstIdx = dstRow + (uint64_t)(dx - warstwa->x);
            if(alfa && warstwa->z_order==0){pix=kolor_nad(warstwa->bufor_pikseli[dstIdx],pix);}
            else if(alfa){unsigned a=pix>>24;if(!a)continue;uint32_t old=warstwa->bufor_pikseli[dstIdx];uint32_t color=0;for(unsigned shift=0;shift<24;shift+=8)color|=((((pix>>shift)&255)*a+((old>>shift)&255)*(255-a)+127)/255)<<shift;pix=0xFF000000U|color;}
            warstwa->bufor_pikseli[dstIdx] = pix;
        }
    }
    if(!batch)zapamietaj_dirty_rysowania(lx0, ly0, lx1 - lx0, ly1 - ly0);
    return true;
}

extern "C" bool bws_gui_blit(int x,int y,int w,int h,const uint32_t* pixels) {
    return gui_blit_region(x,y,w,h,pixels,false);
}
extern "C" bool bws_gui_alpha_blit(int x,int y,int w,int h,const uint32_t* pixels) {
    return gui_blit_region(x,y,w,h,pixels,false,true);
}

/*
 * BWS98 - skalowany blit do warstwy procesu.
 *
 * Kontrakt:
 *   - zrodlo jest ciaglym buforem src_w x src_h w Ring 3 (stride = src_w*4),
 *   - wynik trafia do prostokata dst_x/dst_y/dst_w/dst_h we wspolrzednych
 *     ekranu, przycietego do warstwy wywolujacego,
 *   - filtr i tryb dopasowania pochodza z deskryptora,
 *   - pusty piksel (dokladne 0) nie jest zapisywany, chyba ze flaga
 *     BWS_BLIT_ZAMIEN_REGION kaze nadpisac caly prostokat.
 *
 * Bufor Ring 3 jest czytany wylacznie wewnatrz OknoUzytkownika, wiec SMAP
 * pozostaje aktywny dla calego pozostalego kodu jadra.
 */
/*
 * Zlozenie skalowanego piksela z pokryciem czesciowym w powierzchnie warstwy.
 *
 * Warstwa jest dla kompozytora powierzchnia RGB: gorny bajt nie jest kanalem
 * alfa. Piksel o coverage < 255 musi wiec zostac zmieszany z tym, co juz jest
 * w warstwie - inaczej kompozytor obcina gorny bajt i krawedzie skalowanego
 * tekstu dostaja jasna obwodke.
 *
 * Wynik pozostaje w konwencji znacznikow: 0 = puste, 0x01000000 = widoczna
 * czern, 0x00RRGGBB = kolor.
 */
static inline void compose_scaled_into(uint32_t& dst, uint32_t src, uint32_t coverage) {
    const uint32_t zrodlo_rgb = src & 0x00FFFFFFU;
    uint32_t zlozony;
    if (dst == 0U) {
        zlozony = zrodlo_rgb;
    } else {
        zlozony = 0U;
        for (unsigned shift = 0; shift < 24; shift += 8) {
            const uint32_t s = (zrodlo_rgb >> shift) & 0xFFU;
            const uint32_t d = (dst >> shift) & 0xFFU;
            zlozony |= ((s * coverage + d * (255U - coverage) + 127U) / 255U) << shift;
        }
    }
    dst = zlozony ? zlozony : 0x01000000U;
}

static bool gui_blit_skalowany_region(const BwsBlitSkalowany& r, bool batch) {    if (!r.piksele || r.src_w == 0U || r.src_h == 0U || r.src_w > 8192U || r.src_h > 8192U)
        return false;
    if (!skalowanie_filtr_poprawny(r.filtr)) return false;
    if (!backbuffer || !aktywny_ekran || !BwsProcesMaWarstwe()) return false;

    uint64_t zrodlo_piksele = 0;
    if (__builtin_mul_overflow((uint64_t)r.src_w, (uint64_t)r.src_h, &zrodlo_piksele))
        return false;
    /* Ten sam limit co BWS57: 4 mln pikseli zrodla na jedno wywolanie. */
    if (zrodlo_piksele > 4ULL * 1024ULL * 1024ULL) return false;

    warstwa_obrazu* warstwa = pobierz_warstwe(aktualny_pid);
    if (!warstwa || !warstwa->bufor_pikseli) return false;

    int dst_w = r.dst_w;
    int dst_h = r.dst_h;
    int dst_x = r.dst_x;
    int dst_y = r.dst_y;
    if (dst_w <= 0 || dst_h <= 0 || dst_w > 8192 || dst_h > 8192) return false;

    const bool zachowaj = (r.flagi & BWS_BLIT_ZACHOWAJ_ASPECT) != 0U;
    if (zachowaj) {
        const SkalowanieProstokat p = skalowanie_dopasuj(
            r.src_w, r.src_h,
            static_cast<uint32_t>(dst_w), static_cast<uint32_t>(dst_h),
            r.tryb);
        if (p.szerokosc <= 0 || p.wysokosc <= 0) return false;
        dst_x += p.x;
        dst_y += p.y;
        dst_w = p.szerokosc;
        dst_h = p.wysokosc;
    }

    /* Przyciecie do warstwy wywolujacego (moze byc poza ekranem). */
    const int lx0 = dst_x < warstwa->x ? warstwa->x : dst_x;
    const int ly0 = dst_y < warstwa->y ? warstwa->y : dst_y;
    const int warstwa_x1 = warstwa->x + (int)warstwa->szerokosc;
    const int warstwa_y1 = warstwa->y + (int)warstwa->wysokosc;
    const int dst_x1 = dst_x + dst_w;
    const int dst_y1 = dst_y + dst_h;
    const int lx1 = dst_x1 > warstwa_x1 ? warstwa_x1 : dst_x1;
    const int ly1 = dst_y1 > warstwa_y1 ? warstwa_y1 : dst_y1;
    if (lx0 >= lx1 || ly0 >= ly1) return false;

    const uint32_t filtr = r.filtr;
    const bool alfa = (r.flagi & BWS_BLIT_ALFA_SOURCE_OVER) != 0U;
    const bool nadpisz_puste = (r.flagi & BWS_BLIT_ZAMIEN_REGION) != 0U;

    SkalowanieZrodlo z{};
    z.piksele = reinterpret_cast<const uint32_t*>(r.piksele);
    z.szerokosc = r.src_w;
    z.wysokosc = r.src_h;

    OknoUzytkownika okno(reinterpret_cast<const void*>(r.piksele),
                         (size_t)zrodlo_piksele * sizeof(uint32_t));

    const uint32_t dst_w_u = static_cast<uint32_t>(dst_w);
    const uint32_t dst_h_u = static_cast<uint32_t>(dst_h);
    const uint32_t liczba = static_cast<uint32_t>(lx1 - lx0);
    const uint32_t pierwszy_x = static_cast<uint32_t>(lx0 - dst_x);

    for (int y = ly0; y < ly1; ++y) {
        const uint32_t wiersz_dst = static_cast<uint32_t>(y - dst_y);
        uint32_t* wiersz = warstwa->bufor_pikseli +
                           (size_t)(y - warstwa->y) * (size_t)warstwa->szerokosc +
                           (size_t)(lx0 - warstwa->x);

        /*
         * Szybka sciezka 1:1 - brak matematyki probkowania. Kopiujemy blokowo
         * dokladnie sprawdzony fragment (najczestszy przypadek: aplikacja
         * rysuje w skali DPI i wysyla nietkniety obraz).
         */
        if (dst_w_u == z.szerokosc && dst_h_u == z.wysokosc && filtr == SKALOWANIE_NEAREST) {
            const uint32_t* zrodlo = z.piksele + (size_t)wiersz_dst * z.szerokosc + pierwszy_x;
            if (nadpisz_puste && !alfa) {
                skalowanie_kopia_1do1(wiersz, zrodlo, liczba);
                continue;
            }
            for (uint32_t i = 0; i < liczba; ++i) {
                const uint32_t pix = zrodlo[i];
                if (pix == 0U) {
                    if (nadpisz_puste) wiersz[i] = 0U;
                    continue;
                }
                const uint32_t coverage = (pix >> 24) & 0xFFU;
                if (alfa || (coverage != 0U && coverage != 255U)) {
                    compose_scaled_into(wiersz[i], pix, coverage);
                } else {
                    wiersz[i] = pix;
                }
            }
            continue;
        }

        /*
         * Szybka sciezka: pelne nadpisanie bez filtrowania przezroczystosci.
         * Zadnego bufora posredniego - piszemy wprost do wiersza warstwy.
         */
        if (nadpisz_puste && !alfa) {
            skalowanie_wiersz_wycinek(wiersz, z, wiersz_dst, dst_w_u, dst_h_u,
                                      pierwszy_x, liczba, filtr);
            continue;
        }

        for (uint32_t i = 0; i < liczba; ++i) {
            const uint32_t x = pierwszy_x + i;
            const uint32_t pix = skalowanie_piksel(z, x, wiersz_dst, dst_w_u, dst_h_u, filtr);
            if (pix == 0U) {
                if (nadpisz_puste) wiersz[i] = 0U;
                continue;
            }
            const uint32_t coverage = (pix >> 24) & 0xFFU;
            if (alfa || (coverage != 0U && coverage != 255U)) {
                compose_scaled_into(wiersz[i], pix, coverage);
            } else {
                wiersz[i] = pix;
            }
        }
    }

    if (!batch) zapamietaj_dirty_rysowania(lx0, ly0, lx1 - lx0, ly1 - ly0);
    return true;
}

extern "C" bool bws_gui_blit_skalowany(const BwsBlitSkalowany* opis) {
    if (!opis) return false;
    return gui_blit_skalowany_region(*opis, false);
}

/*
 * BWS57 mode 5 - batch skalowanych regionow. Wywolujacy (syscalls.cpp)
 * sprawdzil deskryptory i zakresy Ring 3; tutaj walidujemy geometrie
 * wzgledem warstwy i skladamy wszystkie regiony pod jedna blokada ekranu.
 */
extern "C" bool bws_gui_zastap_regiony_skalowane(
    const BwsGuiRegionSkalowany* regions, uint32_t count, uint32_t generation,
    uint32_t filtr, uint32_t tryb
) {
    auto* layer = pobierz_warstwe(aktualny_pid);
    if (!regions || !count || count > BWS_GUI_BATCH_SKALOWANY_MAX || !layer ||
        !layer->bufor_pikseli || !backbuffer || !aktywny_ekran ||
        (generation && generation != grafika_generacja_ekranu()))
        return false;
    if (!skalowanie_filtr_poprawny(filtr) || tryb > SKALOWANIE_WYPELNIJ)
        return false;

    /* Najpierw cala walidacja, potem jakikolwiek zapis. */
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& r = regions[i];
        if (!r.pixels || r.src_w == 0 || r.src_h == 0 ||
            r.src_w > 8192 || r.src_h > 8192)
            return false;
        if (r.width < 1 || r.height < 1 || r.width > 8192 || r.height > 8192)
            return false;
        if (r.x < layer->x || r.y < layer->y ||
            int64_t(r.x) + r.width > int64_t(layer->x) + layer->szerokosc ||
            int64_t(r.y) + r.height > int64_t(layer->y) + layer->wysokosc)
            return false;
        uint64_t piksele = 0;
        if (__builtin_mul_overflow((uint64_t)r.src_w, (uint64_t)r.src_h, &piksele))
            return false;
        total += piksele;
        if (total > BWS_GUI_BATCH_PIXELS_MAX) return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        const auto& r = regions[i];
        BwsBlitSkalowany opis{};
        opis.wersja = BWS_NOWOCZESNE_ABI_WERSJA;
        opis.flagi = r.flagi | BWS_BLIT_ZAMIEN_REGION;
        opis.filtr = filtr;
        opis.tryb = tryb;
        opis.dst_x = r.x;
        opis.dst_y = r.y;
        opis.dst_w = r.width;
        opis.dst_h = r.height;
        opis.src_w = r.src_w;
        opis.src_h = r.src_h;
        opis.piksele = r.pixels;
        if (!gui_blit_skalowany_region(opis, true)) return false;
    }

    if (skladacz_obrazu_publikuj_klatke(aktualny_pid) &&
        skladacz_obrazu_czy_widoczna(aktualny_pid))
        bws_gui_powiadom_lifecycle(BWS_ZDARZENIE_OKNO_POKAZANE, aktualny_pid);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& r = regions[i];
        skladacz_obrazu_oznacz_dirty_rect_dokladny(r.x, r.y, r.width, r.height);
    }
    return true;
}

extern "C" bool bws_gui_zastap_region(int x,int y,int w,int h,const uint32_t* pixels) {
    return gui_blit_region(x,y,w,h,pixels,true);
}

// The syscall validates all source mappings and holds BlokadaEkranu. Validate
// the entire destination set before writing anything: a rejected/stale batch
// must never leave half a selection update in a published desktop surface.
extern "C" bool bws_gui_zastap_regiony(const BwsGuiRegion* regions,uint32_t count,uint32_t generation) {
    auto* layer=pobierz_warstwe(aktualny_pid);
    if(!regions || !count || count>BWS_GUI_BATCH_MAX || !layer || !layer->bufor_pikseli ||
       !backbuffer || !aktywny_ekran || (generation && generation!=grafika_generacja_ekranu()))return false;

    /*
     * SMAP: deskryptory regionow sa kopiowane do BSS przez syscall, wiec
     * regions[] to pamiec jadra. Bufor pikseli kazdego regionu pozostaje
     * w Ring 3 - jego odczyt zabezpiecza gui_blit_region() wlasnym oknem.
     */
    uint64_t total=0;
    for(uint32_t i=0;i<count;++i) {
        const auto& r=regions[i];
        if(!r.pixels || r.width<1 || r.height<1 || r.width>2048 || r.height>2048 ||
           r.x<layer->x || r.y<layer->y ||
           int64_t(r.x)+r.width>int64_t(layer->x)+layer->szerokosc ||
           int64_t(r.y)+r.height>int64_t(layer->y)+layer->wysokosc)return false;
        total+=uint64_t(r.width)*r.height;
        if(total>BWS_GUI_BATCH_PIXELS_MAX)return false;
    }
    for(uint32_t i=0;i<count;++i) {
        const auto& r=regions[i];
        gui_blit_region(r.x,r.y,r.width,r.height,reinterpret_cast<const uint32_t*>(r.pixels),true,false,true);
    }
    if(skladacz_obrazu_publikuj_klatke(aktualny_pid) && skladacz_obrazu_czy_widoczna(aktualny_pid))
        bws_gui_powiadom_lifecycle(BWS_ZDARZENIE_OKNO_POKAZANE,aktualny_pid);
    for(uint32_t i=0;i<count;++i) {
        const auto& r=regions[i];
        skladacz_obrazu_oznacz_dirty_rect_dokladny(r.x,r.y,r.width,r.height);
    }
    return true;
}

extern "C" bool bws_gui_ustaw_tytul_okna(const char* tytul) {
    if (!backbuffer || !aktywny_ekran || !BwsProcesMaWarstwe() || !tytul)
        return false;

    char tytul_bezp[64] = {};

    if (!skopiuj_string_z_uzytkownika(
            tytul_bezp,
            tytul,
            sizeof(tytul_bezp))) {
        return false;
    }

    if (!skladacz_obrazu_ustaw_tytul(aktualny_pid, tytul_bezp))
        return false;

    bws_gui_powiadom_lifecycle(BWS_ZDARZENIE_OKNO_TYTUL, aktualny_pid);
    return true;
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

    // Releasing input ownership must not resurrect the kernel terminal.
    pid_przejmujacy_mysz = -1;
    flaga_zamkniecia_powloki = false;
    okna[0].widoczne = false;
}

extern "C" void bws_gui_ustaw_capture(bool stan) {
    if (stan) {
        if (aktualny_pid == aktywny_pid_gui && BwsProcesMaWarstwe())
            capture_pid_gui = aktualny_pid;
    } else {
        /* MOUSE_UP nie moze zostawic ostatniej probki w kolejce geometrii. */
        skladacz_obrazu_zastosuj_pending_geometrii(aktualny_pid);
        if (capture_pid_gui == aktualny_pid) capture_pid_gui = -1;
    }
}

extern "C" void bws_gui_usun_stan_procesu(int pid) {
    if(pid>0&&pid<SKLADACZ_MAKS_WARSTW){
        ma_oczekujacy_dirty[pid]=false;
        uint32_t* stare = kursory_procesow[pid].piksele;
        kursory_procesow[pid]={};
        if (stare) kfree(stare);
    }
    if (capture_pid_gui == pid) capture_pid_gui = -1;
    if (motion_hover_pid_gui == pid) motion_hover_pid_gui = -1;
    if (aktywny_pid_gui == pid) {
        aktywny_pid_gui = -1;
        int best=-1,best_z=-2147483647;
        for(int i=1;i<SKLADACZ_MAKS_WARSTW;++i){warstwa_obrazu*w=pobierz_warstwe(i);if(w&&(w->z_order>best_z||(w->z_order==best_z&&i>best))){best=i;best_z=w->z_order;}}
        if(best>0)ustaw_focus_gui(best);
    }
}

extern "C" bool bws_gui_ustaw_kursor_ksztalt(int pid, uint32_t typ) {
    if (pid <= 0 || pid >= SKLADACZ_MAKS_WARSTW ||
        typ > GUI_CURSOR_CROSSHAIR) return false;
    StanKursoraProcesu nowy{};
    nowy.typ = typ;
    uint32_t* stare = kursory_procesow[pid].piksele;
    kursory_procesow[pid] = nowy;
    if (stare) kfree(stare);
    if (aktywny_pid_gui == pid)
        kursor_plane_ksztalt_brudny = true;
    if (aktywny_pid_gui == pid)
        skladacz_obrazu_oznacz_ruch_kursora(mysz_x, mysz_y, mysz_x, mysz_y);
    return true;
}

extern "C" bool bws_gui_ustaw_kursor_custom(
    int pid,
    const GuiCustomCursorInfo* info,
    const uint32_t* piksele
) {
    if (pid <= 0 || pid >= SKLADACZ_MAKS_WARSTW || !info || !piksele ||
        info->szer == 0 || info->wys == 0 ||
        info->szer > GUI_CURSOR_CUSTOM_MAX_W ||
        info->wys > GUI_CURSOR_CUSTOM_MAX_H ||
        info->hotspot_x >= info->szer || info->hotspot_y >= info->wys ||
        (info->format != GUI_CURSOR_BGRA8888 &&
         info->format != GUI_CURSOR_RGBA8888)) return false;
    const uint32_t count = info->szer * info->wys;
    uint32_t* kopia = static_cast<uint32_t*>(
        kmalloc(static_cast<uint64_t>(count) * sizeof(uint32_t)));
    if (!kopia) return false;
    StanKursoraProcesu nowy{};
    nowy.typ = GUI_CURSOR_CUSTOM;
    nowy.szer = info->szer; nowy.wys = info->wys;
    nowy.hotspot_x = info->hotspot_x; nowy.hotspot_y = info->hotspot_y;
    nowy.piksele = kopia;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t p = piksele[i];
        if (info->format == GUI_CURSOR_BGRA8888) kopia[i] = p;
        else {
            const uint32_t a = p & 0xFF000000U;
            const uint32_t r = p & 0x000000FFU;
            const uint32_t g = p & 0x0000FF00U;
            const uint32_t b = p & 0x00FF0000U;
            kopia[i] = a | (r << 16U) | g | (b >> 16U);
        }
    }
    uint32_t* stare = kursory_procesow[pid].piksele;
    kursory_procesow[pid] = nowy;
    if (stare) kfree(stare);
    if (aktywny_pid_gui == pid)
        kursor_plane_ksztalt_brudny = true;
    if (aktywny_pid_gui == pid)
        skladacz_obrazu_oznacz_ruch_kursora(mysz_x, mysz_y, mysz_x, mysz_y);
    return true;
}

extern "C" int bws_gui_aktywny_pid() { return aktywny_pid_gui; }

extern "C" void bws_gui_aktywuj_warstwe(int pid) {
    if(pid<=0||!pobierz_warstwe(pid))return;
    ma_oczekujacy_dirty[pid] = false;
    ustaw_focus_gui(pid);
    skladacz_obrazu_podnies_warstwe(pid);
}

extern "C" bool bws_gui_minimalizuj_warstwe(int pid) {
    if (pid <= 0 || !skladacz_obrazu_minimalizuj(pid)) return false;
    if (capture_pid_gui == pid) capture_pid_gui = -1;
    if (motion_hover_pid_gui == pid) motion_hover_pid_gui = -1;
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

    okna[0].widoczne = false;
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

const MozliwosciEkranu& grafika_mozliwosci_ekranu(){return ekran_caps;}
TrybEkranu grafika_tryb_ekranu(){return {uint32_t(grafika_pobierz_szerokosc()),uint32_t(grafika_pobierz_wysokosc()),32,aktywny_ekran?aktywny_ekran->PobierzPitch():0,0,0};}
uint32_t grafika_tryby_ekranu(TrybEkranu* out,uint32_t cap){
    uint32_t n=ekran_liczba_trybow<cap?ekran_liczba_trybow:cap;
    if(out)for(uint32_t i=0;i<n;++i)out[i]=ekran_tryby[i];
    return n;
}
bool grafika_tryb_obslugiwany(const TrybEkranu&t){
    size_t n=0;if(!ekran_rozmiar(t,&n)||t.flagi||t.odswiezanie)return false;
    if(ekran_ten_sam(t,grafika_tryb_ekranu()))return true;
    if(ekran_caps.backend==EKRAN_VIRTIO_GPU)return t.szerokosc<=4096&&t.wysokosc<=4096&&t.bpp==32;
    if(n>ekran_vram)return false;
    return ekran_caps.mozna_zmienic_runtime&&bochs_sprawdz_tryb(t);
}
void grafika_pobierz_stan(StanBuforowEkranu* s){
    if(s)*s={grafika_tryb_ekranu(),backbuffer,bufor_tapety,backbuffer_bajty,tapeta_zmapowane_bajty,tapeta_zaladowana,backbuffer_pozyczony};
}
bool grafika_przygotuj_ekran(const TrybEkranu&t,StanBuforowEkranu* s){
    if(!s||!grafika_tryb_obslugiwany(t))return false;
    *s={};s->tryb=t;
    if(!ekran_rozmiar(t,&s->bajty))return false;
    s->tapeta_bajty=size_t(t.szerokosc)*t.wysokosc*4;
    s->backbuffer_pozyczony=ekran_caps.backend==EKRAN_VIRTIO_GPU&&!powitanie_aktywne();
    if(!s->backbuffer_pozyczony)s->backbuffer=static_cast<uint8_t*>(bufor_grafiki_alokuj(s->bajty));
    s->tapeta=static_cast<uint32_t*>(bufor_grafiki_alokuj(s->tapeta_bajty));
    if((!s->backbuffer&&!s->backbuffer_pozyczony)||!s->tapeta){grafika_zwolnij_stan(s);return false;}
    if(s->backbuffer)for(size_t i=0;i<s->bajty;++i)s->backbuffer[i]=0;
    s->ma_tapete=tapeta_zaladowana;
    return true;
}
bool grafika_zmien_sprzet(const TrybEkranu&t){
    if(PrezentacjaStartu::stan().inicjalizacja&&!ekran_ten_sam(t,grafika_tryb_ekranu()))
        PrezentacjaStartu::uniewaznij();
    if(ekran_caps.backend==EKRAN_VIRTIO_GPU){
        VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
        if(!drv)return false;
        if(drv->fb_width==t.szerokosc&&drv->fb_height==t.wysokosc)return true;
        return virtio_gpu_setup_framebuffer(drv,t.szerokosc,t.wysokosc,true);
    }
    if(ekran_caps.mozna_zmienic_runtime)return bochs_ustaw_tryb(t);
    return ekran_ten_sam(t,grafika_tryb_ekranu());
}
void grafika_pobierz_boot_rozmiar(uint32_t* out_w, uint32_t* out_h) {
    if (out_w) *out_w = boot_szer;
    if (out_h) *out_h = boot_wys;
}

void grafika_ustaw_tryby_virtio_gpu() {
    static constexpr uint32_t vg_wymiary[][2] = {
        {800, 600},
        {1024, 768},
        {1280, 720},
        {1280, 800},
        {1366, 768},
        {1600, 900},
        {1920, 1080},
        {2560, 1440},
        {3440, 1440},
        {3840, 2160}
    };
    ekran_liczba_trybow = 0;
    bool boot_znaleziony = false;
    for (const auto& wh : vg_wymiary) {
        TrybEkranu t{wh[0], wh[1], 32, wh[0] * 4, 0, 0};
        size_t n = 0;
        if (!ekran_rozmiar(t, &n)) continue;
        void* probe[3] = {};
        bool ok = true;
        for (auto& p : probe) {
            p = bufor_grafiki_alokuj(n);
            if (!p) ok = false;
        }
        for (auto p : probe) bufor_grafiki_zwolnij(p);
        if (!ok) continue;

        if (boot_szer == wh[0] && boot_wys == wh[1]) {
            boot_znaleziony = true;
        }
        if (ekran_liczba_trybow < EKRAN_MAX_TRYBOW) {
            ekran_tryby[ekran_liczba_trybow++] = t;
        }
    }
    if (!boot_znaleziony && boot_szer > 0 && boot_wys > 0 && ekran_liczba_trybow < EKRAN_MAX_TRYBOW) {
        TrybEkranu t{boot_szer, boot_wys, 32, boot_szer * 4, 0, 0};
        ekran_tryby[ekran_liczba_trybow++] = t;
    }
}

bool grafika_aktywuj_virtio_gpu(VirtioGpuDriver* drv) {
    KlatkiStartuDiag::Sciezka diagnostic_path("graphics/backend-selection");
    if (!drv || !drv->initialized || !drv->fb_vaddr) return false;

    SterownikVirtioGPU* st = new (st_virtio_gpu_pamiec) SterownikVirtioGPU();
    if (!st->InicjalizujGPU(drv)) {
        return false;
    }

    aktywny_ekran = st;
    ekran_caps.backend = EKRAN_VIRTIO_GPU;
    ekran_caps.mozna_zmienic_runtime = 1;
    ekran_caps.mozna_wyliczyc_tryby = 1;

    grafika_ustaw_tryby_virtio_gpu();

    aktywny_stan.szerokosc = drv->fb_width;
    aktywny_stan.wysokosc = drv->fb_height;
    aktywny_stan.pitch = drv->fb_stride;
    aktywny_stan.bpp = 32;
    aktywny_stan.framebuffer = reinterpret_cast<uint64_t>(drv->fb_vaddr);
    if (++aktywny_stan.generacja == 0) ++aktywny_stan.generacja;

    const size_t wymagane_bajty = size_t(drv->fb_width) * drv->fb_height * 4;
    if (backbuffer_bajty < wymagane_bajty) {
        uint8_t* nowy_bb = static_cast<uint8_t*>(bufor_grafiki_alokuj(wymagane_bajty));
        if (nowy_bb) {
            if (backbuffer && !backbuffer_pozyczony) {
                bufor_grafiki_zwolnij(backbuffer);
            }
            backbuffer = nowy_bb;backbuffer_pozyczony=false;
            backbuffer_bajty = wymagane_bajty;
            // The new scanout is not a source of valid desktop pixels.
        }
    }
    if (bufor_tapety && tapeta_zmapowane_bajty < wymagane_bajty) {
        uint32_t* nowa_tapeta = static_cast<uint32_t*>(bufor_grafiki_alokuj(wymagane_bajty));
        if (nowa_tapeta) {
            bufor_grafiki_zwolnij(bufor_tapety);
            bufor_tapety = nowa_tapeta;
            tapeta_zmapowane_bajty = wymagane_bajty;
        }
    }

    if (mysz_x >= int(drv->fb_width)) mysz_x = drv->fb_width - 1;
    if (mysz_y >= int(drv->fb_height)) mysz_y = drv->fb_height - 1;

    ekran_lfb_va = reinterpret_cast<uint64_t>(drv->fb_vaddr);
    kursor_overlay_widoczny = false;
    kursor_overlay_x = 0;
    kursor_overlay_y = 0;
    kursor_plane_ksztalt_brudny = true;
    kursor_plane_aktywny = false;
    kursor_plane_zablokowany = false;

    // A new graphics generation cannot inherit a publishable splash surface.
    powitanie_reset_ekranu();
    grafika_odtworz_tlo_skladania();
    PrezentacjaStartu::wybierz_backend();
    BootTiming::mark("graphics backend selected");
    PrzeniesNaEkran();
    PokazKursor();

    SerialLog("[HAL] Aktywacja sterownika VirtIO-GPU 2D jako backend graficzny.\n");
#if BURSZTYN_DIAG_BOOT
    SerialLog("[BOOT-TELEMETRY] virtio_gpu_framebuffer=");
    SerialHex64(reinterpret_cast<uint64_t>(drv->fb_vaddr));
    SerialLog(" ");
    SerialU64(drv->fb_width);
    SerialLog("x");
    SerialU64(drv->fb_height);
    SerialLog(" stride=");
    SerialU64(drv->fb_stride);
    SerialLog("\n[BOOT-TELEMETRY] compositor_backbuffer_after_virtio=");
    SerialHex64(reinterpret_cast<uint64_t>(backbuffer));
    SerialLog(" ");
    SerialU64(drv->fb_width);
    SerialLog("x");
    SerialU64(drv->fb_height);
    SerialLog(" framebuffer_generation=");
    SerialU64(aktywny_stan.generacja);
    SerialLog("\n");
#endif
    return true;
}

void grafika_zakoncz_wybor_backendu() {
    if(PrezentacjaStartu::stan().backend)return;
    PrezentacjaStartu::wybierz_backend();
    BootTiming::mark("graphics backend selected");
    // Native fallback retains firmware until this completed policy frame.
    // With an inactive overlay the compositor authorizes its first full scene.
    if(powitanie_aktywne())PrzeniesNaEkran();
}

bool grafika_przywroc_boot_backend() {
    if (!boot_sterownik || boot_lfb_va == 0) return false;
    if(backbuffer_pozyczony || backbuffer_bajty<size_t(boot_pitch)*boot_wys){
        const auto bytes=size_t(boot_pitch)*boot_wys;
        auto* owned=static_cast<uint8_t*>(bufor_grafiki_alokuj(bytes));
        if(!owned)return false;
        for(size_t i=0;i<bytes/4;++i)reinterpret_cast<uint32_t*>(owned)[i]=0;
        // Preserve the completed scene when leaving driver-owned backing.
        // The firmware mode can have a different pitch and dimensions.
        const auto old_pitch=aktywny_ekran?aktywny_ekran->PobierzPitch():0;
        const auto old_w=aktywny_ekran?aktywny_ekran->PobierzSzerokosc():0;
        const auto old_h=aktywny_ekran?aktywny_ekran->PobierzWysokosc():0;
        const auto copy_w=old_w<boot_szer?old_w:boot_szer;
        const auto copy_h=old_h<boot_wys?old_h:boot_wys;
        if(backbuffer&&old_pitch&&size_t(old_pitch)*copy_h<=backbuffer_bajty)
            for(uint32_t y=0;y<copy_h;++y)
                graphics_copy_pixels(reinterpret_cast<uint32_t*>(owned+size_t(y)*boot_pitch),
                    reinterpret_cast<const uint32_t*>(backbuffer+size_t(y)*old_pitch),copy_w);
        if(backbuffer&&!backbuffer_pozyczony)bufor_grafiki_zwolnij(backbuffer);
        backbuffer=owned;backbuffer_bajty=bytes;backbuffer_pozyczony=false;
    }

    if (aktywny_ekran && kursor_plane_aktywny) {
        aktywny_ekran->UkryjKursor();
        kursor_plane_ksztalt_brudny = true;
    }
    aktywny_ekran = boot_sterownik;
    ekran_caps = boot_ekran_caps;
    ekran_lfb_va = boot_lfb_va;

    if (!aktywny_ekran->Inicjalizuj(boot_lfb_va, boot_szer, boot_wys, boot_pitch, boot_bpp)) {
        return false;
    }

    aktywny_stan.szerokosc = boot_szer;
    aktywny_stan.wysokosc = boot_wys;
    aktywny_stan.pitch = boot_pitch;
    aktywny_stan.bpp = boot_bpp;
    aktywny_stan.framebuffer = boot_lfb_va;
    if (++aktywny_stan.generacja == 0) ++aktywny_stan.generacja;

    kursor_overlay_widoczny = false;
    kursor_overlay_x = 0;
    kursor_overlay_y = 0;
    kursor_plane_ksztalt_brudny = true;
    kursor_plane_aktywny = false;
    kursor_plane_zablokowany = false;

    PrzeniesNaEkran();
    PokazKursor();

    SerialLog("[HAL] Przywrocono boot backend graficzny.\n");
    return true;
}

void grafika_wymien_stan(const StanBuforowEkranu&s){
    if (aktywny_ekran && kursor_plane_aktywny) {
        aktywny_ekran->UkryjKursor();
        kursor_plane_aktywny=false;
        kursor_plane_ksztalt_brudny=true;
    }
    if (ekran_caps.backend == EKRAN_VIRTIO_GPU) {
        VirtioGpuDriver* drv = virtio_gpu_get_driver(0);
        if (drv && drv->fb_vaddr) {
            ekran_lfb_va = reinterpret_cast<uint64_t>(drv->fb_vaddr);
        }
    }
    aktywny_ekran->Inicjalizuj(ekran_lfb_va,s.tryb.szerokosc,s.tryb.wysokosc,s.tryb.pitch,s.tryb.bpp);
    backbuffer=s.backbuffer;backbuffer_pozyczony=s.backbuffer_pozyczony;
    if(backbuffer_pozyczony){auto* drv=virtio_gpu_get_driver(0);backbuffer=drv?static_cast<uint8_t*>(drv->fb_vaddr):nullptr;}
    bufor_tapety=s.tapeta;backbuffer_bajty=s.bajty;
    tapeta_zmapowane_bajty=s.tapeta_bajty;bufor_tapety_zmapowany=bufor_tapety!=nullptr;tapeta_zaladowana=s.ma_tapete;
    aktywny_stan.szerokosc=s.tryb.szerokosc;aktywny_stan.wysokosc=s.tryb.wysokosc;
    aktywny_stan.pitch=s.tryb.pitch;aktywny_stan.bpp=s.tryb.bpp;
    aktywny_stan.framebuffer=ekran_lfb_va;if(++aktywny_stan.generacja==0)++aktywny_stan.generacja;
    if(mysz_x<0)mysz_x=0;
    if(mysz_y<0)mysz_y=0;
    if(mysz_x>=int(s.tryb.szerokosc))mysz_x=s.tryb.szerokosc-1;
    if(mysz_y>=int(s.tryb.wysokosc))mysz_y=s.tryb.wysokosc-1;
    kursor_overlay_widoczny=false;
    kursor_overlay_x=0;
    kursor_overlay_y=0;
    kursor_plane_ksztalt_brudny=true;
    kursor_plane_aktywny=false;
    kursor_plane_zablokowany=false;
    for(unsigned i=0;i<SKLADACZ_MAKS_WARSTW;++i)ma_oczekujacy_dirty[i]=false;
    log_granice_kursora("cursor", s.tryb.szerokosc, s.tryb.wysokosc, aktywny_stan.generacja);
}
void grafika_pobierz_granice_kursora(int* max_x,int* max_y){
    if(max_x)*max_x=grafika_pobierz_szerokosc()-1;
    if(max_y)*max_y=grafika_pobierz_wysokosc()-1;
}
void grafika_zatwierdz_geometrie(uint32_t scale,uint32_t generation) {
    const auto g=display_geometry(grafika_pobierz_szerokosc(),grafika_pobierz_wysokosc(),scale,generation);
    mysz_x=display_clamp(mysz_x,g.cursor_max_x);
    mysz_y=display_clamp(mysz_y,g.cursor_max_y);
    // Geometry-bound capture must not survive the transaction. Focus/owner
    // remain intact; clients cancel their own gesture on the display event.
    capture_pid_gui=-1;okno_przeciagane=-1;motion_hover_pid_gui=-1;
    for(size_t i=0;i<virtio_input_get_count();++i)
        virtio_input_commit_geometry(virtio_input_get_driver(i),g.width,g.height,g.generation);
    committed_geometry=g;
}
DisplayGeometry grafika_geometria_ekranu() {
    if(committed_geometry.generation && committed_geometry.width==grafika_pobierz_szerokosc() &&
       committed_geometry.height==grafika_pobierz_wysokosc())return committed_geometry;
    // Boot/host fixtures before the display service has committed its state.
    return display_geometry(grafika_pobierz_szerokosc(),grafika_pobierz_wysokosc(),100,uint32_t(aktywny_stan.generacja));
}
uint64_t grafika_generacja_ekranu(){return grafika_geometria_ekranu().generation;}
void grafika_zwolnij_stan(StanBuforowEkranu*s){
    if(!s)return;
    if(!s->backbuffer_pozyczony)bufor_grafiki_zwolnij(s->backbuffer);
    bufor_grafiki_zwolnij(s->tapeta);*s={};
}

bool grafika_test_kursor_suite(VirtioGpuDriver* gpu_drv) {
    wypisz_log("[CURSOR TEST] === ROZPOCZECIE PAKIETU TESTOW KURSORA ===");

    const int sw = grafika_pobierz_szerokosc();
    const int sh = grafika_pobierz_wysokosc();
    if (sw <= 0 || sh <= 0 || !aktywny_ekran || !backbuffer) {
        wypisz_log("[CURSOR TEST] BLAD: Niepoprawne srodowisko graficzne.");
        return false;
    }

    const uint32_t pitch = aktywny_ekran->PobierzPitch();

    // 1. Test: cursor render at center
    const int cx = sw / 2;
    const int cy = sh / 2;

    for (int dy = -16; dy < 32; ++dy) {
        for (int dx = -16; dx < 32; ++dx) {
            int px = cx + dx, py = cy + dy;
            if (px >= 0 && px < sw && py >= 0 && py < sh) {
                reinterpret_cast<uint32_t*>(backbuffer + static_cast<uint64_t>(py) * pitch + static_cast<uint64_t>(px) * 4ULL)[0] = 0x00112233;
            }
        }
    }
    PrzeniesFragmentNaEkran(cx - 16, cy - 16, 48, 48);

    grafika_prezentuj_kursor_w(cx, cy);

    bool test1_pass = kursor_overlay_widoczny && (kursor_overlay_x == cx) && (kursor_overlay_y == cy);
    volatile uint32_t* fb32 = reinterpret_cast<volatile uint32_t*>(aktywny_stan.framebuffer);
    if (fb32) {
        uint32_t p0 = fb32[static_cast<uint64_t>(cy) * (pitch / 4) + static_cast<uint64_t>(cx)];
        uint32_t p1 = fb32[static_cast<uint64_t>(cy + 1) * (pitch / 4) + static_cast<uint64_t>(cx + 1)];
        if ((p0 & 0x00FFFFFFU) != 0x00000000U || (p1 & 0x00FFFFFFU) != 0x00FFFFFFU) {
            test1_pass = false;
        }
    }
    uint32_t bb_clean = *reinterpret_cast<const uint32_t*>(backbuffer + static_cast<uint64_t>(cy) * pitch + static_cast<uint64_t>(cx) * 4ULL);
    if (bb_clean != 0x00112233) test1_pass = false;

    if (test1_pass) {
        wypisz_log("[CURSOR TEST] cursor render at center: PASS");
    } else {
        wypisz_log("[CURSOR TEST] cursor render at center: FAIL");
        return false;
    }

    // 2. Test: cursor movement
    const int new_x = cx + 40;
    const int new_y = cy + 30;
    grafika_prezentuj_kursor_w(new_x, new_y);

    bool test2_pass = kursor_overlay_widoczny && (kursor_overlay_x == new_x) && (kursor_overlay_y == new_y);
    if (fb32) {
        uint32_t np0 = fb32[static_cast<uint64_t>(new_y) * (pitch / 4) + static_cast<uint64_t>(new_x)];
        uint32_t np1 = fb32[static_cast<uint64_t>(new_y + 1) * (pitch / 4) + static_cast<uint64_t>(new_x + 1)];
        if ((np0 & 0x00FFFFFFU) != 0x00000000U || (np1 & 0x00FFFFFFU) != 0x00FFFFFFU) {
            test2_pass = false;
        }
    }
    if (test2_pass) {
        wypisz_log("[CURSOR TEST] cursor movement: PASS");
    } else {
        wypisz_log("[CURSOR TEST] cursor movement: FAIL");
        return false;
    }

    // 3. Test: old cursor region restored
    bool test3_pass = true;
    if (fb32) {
        uint32_t old_restored_0 = fb32[static_cast<uint64_t>(cy) * (pitch / 4) + static_cast<uint64_t>(cx)];
        uint32_t old_restored_1 = fb32[static_cast<uint64_t>(cy + 1) * (pitch / 4) + static_cast<uint64_t>(cx + 1)];
        if ((old_restored_0 & 0x00FFFFFFU) != 0x00112233 || (old_restored_1 & 0x00FFFFFFU) != 0x00112233) {
            test3_pass = false;
        }
    }
    if (test3_pass) {
        wypisz_log("[CURSOR TEST] old cursor region restored: PASS");
    } else {
        wypisz_log("[CURSOR TEST] old cursor region restored: FAIL");
        return false;
    }

    // 4. Test: edge clipping
    bool test4_pass = true;
    grafika_prezentuj_kursor_w(-5, -5);
    if (!kursor_overlay_widoczny || kursor_overlay_x != 0 || kursor_overlay_y != 0 ||
        kursor_overlay_w != 11 || kursor_overlay_h != 11) {
        test4_pass = false;
    }
    grafika_prezentuj_kursor_w(sw - 5, sh - 5);
    if (!kursor_overlay_widoczny || kursor_overlay_x != sw - 5 || kursor_overlay_y != sh - 5 ||
        kursor_overlay_w != 5 || kursor_overlay_h != 5) {
        test4_pass = false;
    }
    grafika_prezentuj_kursor_w(-100, -100);
    if (kursor_overlay_widoczny) {
        test4_pass = false;
    }
    if (test4_pass) {
        wypisz_log("[CURSOR TEST] edge clipping: PASS");
    } else {
        wypisz_log("[CURSOR TEST] edge clipping: FAIL");
        return false;
    }

    // 5. Test: rapid movement (1000 iterations)
    bool test5_pass = true;
    for (int i = 0; i < 1000; ++i) {
        int rx = 100 + (i * 7) % 300;
        int ry = 100 + (i * 11) % 200;
        grafika_prezentuj_kursor_w(rx, ry);
        if (gpu_drv && gpu_drv->has_error) {
            test5_pass = false;
            break;
        }
    }
    if (test5_pass) {
        wypisz_log("[CURSOR TEST] rapid movement (1000 iterations): PASS");
    } else {
        wypisz_log("[CURSOR TEST] rapid movement (1000 iterations): FAIL");
        return false;
    }

    // 6. Test: VirtIO-GPU dirty-rect flush
    bool test6_pass = true;
    if (gpu_drv) {
        uint64_t before_m = gpu_drv->flush_count;
        grafika_prezentuj_kursor_w(200, 200);
        grafika_prezentuj_kursor_w(250, 250);
        uint64_t after_m = gpu_drv->flush_count;
        if (after_m != before_m + 2) {
            test6_pass = false;
        }
    }
    if (test6_pass) {
        wypisz_log("[CURSOR TEST] VirtIO-GPU dirty-rect flush: PASS");
    } else {
        wypisz_log("[CURSOR TEST] VirtIO-GPU dirty-rect flush: FAIL");
        return false;
    }

    // 7. Test: backend switch GOP/Bochs VirtIO-GPU
    bool test7_pass = true;
    if (gpu_drv) {
        bool sw_boot = grafika_przywroc_boot_backend();
        if (!sw_boot || ekran_caps.backend == EKRAN_VIRTIO_GPU) {
            test7_pass = false;
        }
        bool sw_gpu = grafika_aktywuj_virtio_gpu(gpu_drv);
        if (!sw_gpu || ekran_caps.backend != EKRAN_VIRTIO_GPU) {
            test7_pass = false;
        }
        if (aktywny_stan.framebuffer != reinterpret_cast<uint64_t>(gpu_drv->fb_vaddr)) {
            test7_pass = false;
        }
    }
    if (test7_pass) {
        wypisz_log("[CURSOR TEST] backend switch GOP/Bochs VirtIO-GPU: PASS");
    } else {
        wypisz_log("[CURSOR TEST] backend switch GOP/Bochs VirtIO-GPU: FAIL");
        return false;
    }

    // 8. Test: cursor still visible after switch
    bool test8_pass = true;
    grafika_prezentuj_kursor_w(500, 300);
    if (!kursor_overlay_widoczny || kursor_overlay_x != 500 || kursor_overlay_y != 300) {
        test8_pass = false;
    }
    if (fb32) {
        uint32_t sp0 = fb32[300ULL * (pitch / 4) + 500ULL];
        uint32_t sp1 = fb32[301ULL * (pitch / 4) + 501ULL];
        if ((sp0 & 0x00FFFFFFU) != 0x00000000U || (sp1 & 0x00FFFFFFU) != 0x00FFFFFFU) {
            test8_pass = false;
        }
    }
    if (test8_pass) {
        wypisz_log("[CURSOR TEST] cursor still visible after switch: PASS");
    } else {
        wypisz_log("[CURSOR TEST] cursor still visible after switch: FAIL");
        return false;
    }

    wypisz_log("[CURSOR TEST] === WSZYSTKIE TESTY KURSORA: PASS ===");

    mysz_x = 500;
    mysz_y = 300;
    grafika_prezentuj_kursor_w(500, 300);
    return true;
}
