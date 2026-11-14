/*
 * Bursztyn OS - Przeglądarka Obrazów 2.0 (BursztynUI)
 *
 * Formaty: BMP, PNG, JPEG (.jpg/.jpeg), BTP (BTP1 i BTP2). Domyślna galeria: /dane/zdjecia.
 * Prawdziwe, ciągłe powiększenie 10%-800% (suwak, przyciski, Ctrl + kółko myszy).
 *
 * Podział odpowiedzialności:
 *   galeria.h        - format wg rozszerzenia, kolejność naturalna, model galerii (bez I/O)
 *   powiekszenie.h   - geometria widoku i renderer powiększenia (bez alokacji, bez float)
 *   silnik_obrazu.h  - strumieniowy silnik BMP (Fit/1:1) dla bardzo dużych BMP, bez zmian
 *   obrazy/dekoder_obrazu.* + tapetybtp.* - współdzielone dekodery PNG/JPEG/BTP1/BTP2 (Ring 3)
 *   (tu)             - UI, ładowanie plików, galeria, powiększenie
 *
 * Obrazy to niezaufane dane: sygnatura, wymiary, limity pamięci i bufory są walidowane;
 * dekodowanie odbywa się wyłącznie w Ring 3 (limity dekodera podniesione tylko w tym procesie).
 */
#include "loader.h"
#include "bursztyn_ui/bursztyn_ui.h"
#include "bursztyn_gui.h"
#include "obrazy/dekoder_obrazu.h"
#include "obrazy/podglad_jpeg.h"
#include "narzedzia/tapety-btp/tapetybtp.h"
#include "silnik_obrazu.h"
#include "galeria.h"
#include "powiekszenie.h"

using namespace BursztynUI;

#ifndef PRZEGLADARKA_HOST_TEST
extern "C" __attribute__((noreturn)) void _start();

extern "C" {
__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},
    reinterpret_cast<uint64_t>(&_start),
    0x1000ULL, 0x50000ULL, 0x1001000ULL,
    0x51000ULL, 0x20000ULL, 0x1051000ULL
};
}
#endif

namespace {

constexpr int OKNO_SZER = 860;
constexpr int OKNO_WYS = 580;
constexpr int MIN_SZER = 520;
constexpr int MIN_WYS = 320;
constexpr int MARGINES_OBRAZU = 2;
constexpr uint32_t OKRES_KROKU_TIKI = 4;            // ~16 ms przy 250 Hz
constexpr int KROK_PRZESUNIECIA = 64;
constexpr const char* KATALOG_GALERII = "/dane/zdjecia";

// Dwupoziomowy model duzych obrazow (patrz docs/image-viewer-large-images.md):
//  Poziom 1 - pelna rozdzielczosc: powierzchnia BGRA do 17 Mpx (64,9 MiB) i plik do 32 MiB. Wymaga
//             zaufanej alokacji jadra (bws_alokuj_sterte: pojedynczy blok do 72 MiB tylko dla tego
//             programu o zweryfikowanym SHA-256) oraz szacunku szczytu pamieci <= BUDZET_SZCZYTU.
//  Poziom 2 - podglad JPEG (1/2, 1/4, 1/8) dekodowany strumieniowo przez obrazy/podglad_jpeg.*,
//             bez pelnej powierzchni; nigdy nie jest przedstawiany jako widok 100%.
// Budzet pamieci procesu: sterta 256 MiB (limit jadra); aplikacja uzywa najwyzej BUDZET_SZCZYTU.
constexpr uint64_t LIMIT_PIKSELI_PELNY = OBRAZ_TWARDY_MAX_PIKSELE;           // 17 Mpx
constexpr size_t LIMIT_PLIKU_PELNY = OBRAZ_TWARDY_MAX_PLIK;                  // 32 MiB
constexpr uint64_t BUDZET_SZCZYTU = 200ULL * 1024ULL * 1024ULL;
constexpr uint64_t BUDZET_PODGLADU = 48ULL * 1024ULL * 1024ULL;
constexpr uint32_t PODGLAD_MAX_PIKSELI = 4200000U;
constexpr uint32_t LIMIT_BOKU = 8192U;
constexpr uint64_t LIMIT_PIKSELI_BMP = 4190000ULL;   // powierzchnia BMP; wieksze BMP: silnik strumieniowy
constexpr size_t PORCJA_ODCZYTU = 512U * 1024U;
constexpr uint64_t PORCJA_PODGLADU_NS = 12000000ULL; // ~12 ms pracy na tyk (okres 16 ms)

// Zliczanie pamieci obrazu (do pomiarow i kontroli budzetu). Naglowek 16 B przechowuje rozmiar.
uint64_t g_pam_aktualna = 0, g_pam_szczyt = 0, g_pam_liczba = 0, g_pam_nieudane = 0;
uint64_t g_limit_alokacji_test = 0;                 // tylko do testow (host): odrzucaj alokacje wieksze niz N bajtow

using Galeria::FormatPliku;
using Galeria::ModelGalerii;

void* alokuj_obraz(size_t n) {
    if ((g_limit_alokacji_test && n > g_limit_alokacji_test) || n > (static_cast<size_t>(1) << 31)) { ++g_pam_nieudane; return nullptr; }
    uint8_t* b = static_cast<uint8_t*>(gui_malloc(static_cast<unsigned long>(n + 16)));
    if (!b) { ++g_pam_nieudane; return nullptr; }
    *reinterpret_cast<uint64_t*>(b) = n;
    g_pam_aktualna += n; ++g_pam_liczba;
    if (g_pam_aktualna > g_pam_szczyt) g_pam_szczyt = g_pam_aktualna;
    return b + 16;
}
void zwolnij_obraz(void* p) {
    if (!p) return;
    uint8_t* b = static_cast<uint8_t*>(p) - 16;
    g_pam_aktualna -= *reinterpret_cast<uint64_t*>(b);
    gui_free(b);
}
uint64_t czas_ns() { return gui_czas_monotoniczny_ns(); }

struct KontekstOdczytu { const char* sciezka; };
uint32_t czytaj_dla_podgladu(void* ctx, uint64_t off, uint8_t* dst, uint32_t n) {
    const KontekstOdczytu* k = static_cast<const KontekstOdczytu*>(ctx);
    if (off > 0xFFFFFFFFULL) return 0;
    uint32_t got = 0;
    if (!czytaj_fragment_pliku(k->sciezka, static_cast<uint32_t>(off), reinterpret_cast<char*>(dst), n, &got)) return 0;
    return got;
}

// Szacunek szczytu pamieci pelnego dekodowania (plik + bufory dekodera + powierzchnia BGRA).
uint64_t szacunek_szczytu(FormatObrazu f, uint64_t px, uint64_t plik) {
    const uint64_t mnoznik = f == FormatObrazu::PNG ? 7ULL : 9ULL;   // PNG: raw+RGBA; JPEG: wspolczynniki+plaszczyzny+RGBA
    return plik + px * mnoznik;
}

struct Powierzchnia {
    uint32_t* piksele{nullptr};
    int szer{0}, wys{0};
    bool alfa{false};
    bool wazna() const { return piksele && szer > 0 && wys > 0; }
    void zwolnij() { if (piksele) zwolnij_obraz(piksele); piksele = nullptr; szer = wys = 0; alfa = false; }
};

enum class Zrodlo : uint8_t { Brak, Powierzchnia, StrumienBmp };

class WidokObrazu;

struct AplikacjaObrazow {
    AplikacjaGUI* app{nullptr};
    Okno* okno{nullptr};
    PowierzchniaRysunkowa* widok{nullptr};
    PasekStanu* pasek{nullptr};
    Suwak* suwak{nullptr};
    Etykieta* lbl_zoom{nullptr};
    Przycisk* btn_poprz{nullptr};
    Przycisk* btn_nast{nullptr};
    Przycisk* btn_strz_l{nullptr};
    Przycisk* btn_strz_p{nullptr};
    Przycisk* btn_fit{nullptr};
    Przycisk* btn_100{nullptr};
    Przycisk* btn_plus{nullptr};
    Przycisk* btn_minus{nullptr};
    Menu* menu_ustawien{nullptr};

    ModelGalerii gal{};
    char sciezka[512] = {};
    bool zewnetrzny{false};              // pokazywany plik spoza galerii (nie zmienia modelu galerii)
    Zrodlo zrodlo{Zrodlo::Brak};
    FormatPliku format{FormatPliku::Nieznany};
    Powierzchnia pow{};
    Powiekszenie::Widok wd{};
    bool tryb_dopasuj{true};
    bool wygladzanie{true};
    char komunikat[160] = {};            // blad/stan wyswietlany w widoku
    int oczekujacy{-1};                  // indeks galerii do wczytania w nastepnym kroku timera
    char oczekujacy_plik[512] = {};      // plik zewnetrzny do wczytania w nastepnym kroku timera
    bool oczekuje_plik{false};
    bool timer_wlaczony{false};
    bool przesuwanie{false};
    int przesuw_mx{0}, przesuw_my{0}, przesuw_vx{0}, przesuw_vy{0};
    int zoom_pokazany{-1};
    int64_t przesuw_ox{0}, przesuw_oy{0};
    bool bufory_ok{false};
    bool aktualizuje_suwak{false};
    uint32_t klucz_renderu{0};           // wersja stanu (zoom/offset/rozmiar/motyw/obraz) buforu widoku
    uint32_t klucz_ostatni{0xFFFFFFFFu};
    uint32_t wersja_obrazu{0};
    uint32_t wersja_motywu{0};
    uint64_t nastepny_ns{0};

    // Duze obrazy / pomiary
    uint32_t zrodlo_w{0}, zrodlo_h{0};   // wymiary PLIKU (dla podgladu inne niz powierzchnia)
    uint32_t skala_podgladu{0};          // 0 = pelna rozdzielczosc; 2/4/8 = podglad 1/N
    PodgladJpeg* pdek{nullptr};          // trwajacy podglad (zrodlo == Brak, dopoki nie gotowy)
    KontekstOdczytu pctx{};
    uint64_t t_start_ns{0}, t_koniec_ns{0}, render_ns{0};
    uint64_t pom_szczyt{0};              // szczyt pamieci obrazu dla ostatniego otwarcia
    uint64_t pom_powierzchnia{0};
    uint64_t pom_baza{0};

    // ---- bufory ---------------------------------------------------------------
    bool przygotuj_bufory() {
        int sw = 0, sh = 0;
        gui_pobierz_rozdzielczosc(&sw, &sh);
        uint64_t piksele = 0, bajty = 0;
        if (sw <= 0 || sh <= 0 || !mnoz_u64(static_cast<uint64_t>(sw), static_cast<uint64_t>(sh), &piksele) ||
            !mnoz_u64(piksele, sizeof(uint32_t), &bajty) || piksele > static_cast<uint64_t>(SIZE_MAX) ||
            bajty > static_cast<uint64_t>(ULONG_MAX)) return false;
        viewport_capacity = static_cast<size_t>(piksele);
        kafel_buf = static_cast<uint32_t*>(profilowany_malloc(sizeof(uint32_t) * TILE_PIXELS));
        viewport_buf = static_cast<uint32_t*>(profilowany_malloc(static_cast<unsigned long>(bajty)));
        bufory_ok = kafel_buf && viewport_buf;
        if (!bufory_ok) { zwolnij_bufory(); return false; }
        return true;
    }

    // Cache wierszy (48 MiB) i bufor dopasowania tylko dla strumieniowego BMP - alokowane leniwie.
    bool przygotuj_silnik_bmp() {
        if (fit_buf && row_cache_slot_count) return true;
        int sw = 0, sh = 0;
        gui_pobierz_rozdzielczosc(&sw, &sh);
        uint64_t sloty = (static_cast<uint64_t>(sh) + ROW_CACHE_ROWS - 1U) / ROW_CACHE_ROWS + 8ULL;
        if (sloty > MAX_ROW_CACHE_SLOTS) sloty = MAX_ROW_CACHE_SLOTS;
        if (sloty < 16) sloty = 16;
        row_cache_slot_count = static_cast<uint32_t>(sloty);
        fit_buf = static_cast<uint32_t*>(profilowany_malloc(static_cast<unsigned long>(viewport_capacity * sizeof(uint32_t))));
        bool ok = fit_buf != nullptr;
        for (uint32_t i = 0; ok && i < row_cache_slot_count; ++i) {
            row_cache_slots[i].data = static_cast<uint8_t*>(profilowany_malloc(static_cast<unsigned long>(ROW_CACHE_BYTES)));
            if (!row_cache_slots[i].data) ok = false;
        }
        if (!ok) { zwolnij_silnik_bmp(); return false; }
        uniewaznij_cache_wierszy();
        return true;
    }

    void zwolnij_silnik_bmp() {
        if (fit_buf) { gui_free(fit_buf); fit_buf = nullptr; }
        for (uint32_t i = 0; i < row_cache_slot_count; ++i)
            if (row_cache_slots[i].data) { gui_free(row_cache_slots[i].data); row_cache_slots[i].data = nullptr; }
        row_cache_slot_count = 0;
    }

    void zwolnij_bufory() {
        if (kafel_buf) { gui_free(kafel_buf); kafel_buf = nullptr; }
        if (viewport_buf) { gui_free(viewport_buf); viewport_buf = nullptr; }
        zwolnij_silnik_bmp();
        bufory_ok = false;
    }

    // ---- ladowanie ------------------------------------------------------------
    void ustaw_komunikat(const char* t) { kopiuj_txt(komunikat, sizeof(komunikat), t ? t : ""); }

    void zwolnij_obraz_biezacy() {
        if (pdek) { podglad_jpeg_zamknij(pdek, false); pdek = nullptr; }
        skala_podgladu = 0; zrodlo_w = zrodlo_h = 0;
        pow.zwolnij();
        if (zrodlo == Zrodlo::StrumienBmp) { zwolnij_bmp(); zwolnij_silnik_bmp(); }
        zrodlo = Zrodlo::Brak;
        format = FormatPliku::Nieznany;
        przesuwanie = false;
        ++wersja_obrazu;
    }

    bool wczytaj_plik_do_pamieci(const char* sc, uint8_t** dane, size_t* rozmiar) {
        uint32_t rsz = 0;
        if (!sc || sc[0] != '/' || !pobierz_rozmiar_pliku(sc, &rsz)) { ustaw_komunikat("Nie można odczytać pliku"); return false; }
        if (rsz == 0) { ustaw_komunikat("Pusty plik"); return false; }
        if (rsz > LIMIT_PLIKU_PELNY) { ustaw_komunikat("Plik zbyt duży (limit 32 MiB)"); return false; }
        uint8_t* b = static_cast<uint8_t*>(alokuj_obraz(rsz));
        if (!b) { ustaw_komunikat("Brak pamięci na plik"); return false; }
        uint32_t odczyt = 0;
        while (odczyt < rsz) {
            uint32_t chce = rsz - odczyt;
            if (chce > PORCJA_ODCZYTU) chce = static_cast<uint32_t>(PORCJA_ODCZYTU);
            uint32_t got = 0;
            if (!czytaj_fragment_pliku(sc, odczyt, reinterpret_cast<char*>(b + odczyt), chce, &got) || got == 0) {
                zwolnij_obraz(b); ustaw_komunikat("Błąd odczytu pliku"); return false;
            }
            odczyt += got;
        }
        *dane = b; *rozmiar = rsz;
        return true;
    }

    static const char* komunikat_bledu(BladObrazu b) {
        switch (b) {
            case BladObrazu::ZaDuzy: return "Obraz zbyt duży do wczytania";
            case BladObrazu::Pamiec: return "Brak pamięci na obraz";
            case BladObrazu::Format: case BladObrazu::Nieobslugiwany: return "Nieobsługiwany format obrazu";
            case BladObrazu::Argument: return "Błąd wewnętrzny dekodera";
            default: return "Uszkodzony plik obrazu";
        }
    }

    // Wymiary z samego naglowka (bez dekodowania): PNG IHDR, JPEG SOF, BTP1/BTP2 (btpnaglowek).
    static bool wymiary_z_naglowka(FormatObrazu f, const uint8_t* b, size_t n, uint32_t* w, uint32_t* h) {
        if (!b) return false;
        if (f == FormatObrazu::PNG) {
            if (n < 24 || b[12] != 'I' || b[13] != 'H' || b[14] != 'D' || b[15] != 'R') return false;
            *w = (uint32_t(b[16]) << 24) | (uint32_t(b[17]) << 16) | (uint32_t(b[18]) << 8) | b[19];
            *h = (uint32_t(b[20]) << 24) | (uint32_t(b[21]) << 16) | (uint32_t(b[22]) << 8) | b[23];
            return true;
        }
        if (f == FormatObrazu::JPEG) {
            size_t i = 2;
            while (i + 9 < n) {
                if (b[i] != 0xFF) { ++i; continue; }
                const uint8_t m = b[i + 1];
                if (m == 0xFF) { ++i; continue; }
                if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
                const size_t dl = (size_t(b[i + 2]) << 8) | b[i + 3];
                if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
                    *h = (uint32_t(b[i + 5]) << 8) | b[i + 6];
                    *w = (uint32_t(b[i + 7]) << 8) | b[i + 8];
                    return true;
                }
                if (dl < 2) return false;
                i += 2 + dl;
            }
            return false;
        }
        if (f == FormatObrazu::BTP) {
            BtpInfo info{};
            if (btpnaglowek(b, n, &info) != BTPOK) return false;
            *w = info.szerokosc; *h = info.wysokosc;
            return true;
        }
        return false;
    }

    // BMP w calosci do powierzchni BGRA. Naglowek waliduje silnik (parsuj_naglowek_bmp) - jedyny parser BMP.
    bool bmp_do_powierzchni(const char* sc, const uint8_t* hdr, uint32_t hdr_len, uint64_t rozmiar_pliku) {
        const char* blad = parsuj_naglowek_bmp(hdr, hdr_len, rozmiar_pliku);
        if (blad) { ustaw_komunikat(blad); return false; }
        const uint32_t w = img_w, h = img_h, bpp = img_bpp;
        const uint64_t off = img_offset, stride = img_stride;
        const bool gora_dol = img_top_down;
        uint64_t px = 0;
        if (!mnoz_u64(w, h, &px) || px > LIMIT_PIKSELI_BMP) return false;      // wywolujacy wybiera silnik strumieniowy
        uint32_t* out = static_cast<uint32_t*>(alokuj_obraz(static_cast<size_t>(px * 4U)));
        uint8_t* wiersz = static_cast<uint8_t*>(alokuj_obraz(static_cast<size_t>(stride)));
        if (!out || !wiersz) { if (out) zwolnij_obraz(out); if (wiersz) zwolnij_obraz(wiersz); ustaw_komunikat("Brak pamięci na obraz"); return false; }
        bool ok = true;
        for (uint32_t y = 0; ok && y < h; ++y) {
            const uint32_t fy = gora_dol ? y : h - 1 - y;
            uint64_t o = 0, o2 = 0;
            uint32_t got = 0;
            if (!mnoz_u64(fy, stride, &o) || !dodaj_u64(off, o, &o2) ||
                !czytaj_fragment_pliku(sc, o2, reinterpret_cast<char*>(wiersz), static_cast<uint32_t>(stride), &got) ||
                got < stride) { ok = false; break; }
            uint32_t* dst = out + static_cast<size_t>(y) * w;
            if (bpp == 32) for (uint32_t x = 0; x < w; ++x)
                dst[x] = 0xFF000000U | (static_cast<uint32_t>(wiersz[4 * x + 2]) << 16) |
                         (static_cast<uint32_t>(wiersz[4 * x + 1]) << 8) | wiersz[4 * x];
            else for (uint32_t x = 0; x < w; ++x)
                dst[x] = 0xFF000000U | (static_cast<uint32_t>(wiersz[3 * x + 2]) << 16) |
                         (static_cast<uint32_t>(wiersz[3 * x + 1]) << 8) | wiersz[3 * x];
        }
        zwolnij_obraz(wiersz);
        if (!ok) { zwolnij_obraz(out); ustaw_komunikat("Błąd odczytu pliku BMP"); return false; }
        pow.piksele = out; pow.szer = static_cast<int>(w); pow.wys = static_cast<int>(h); pow.alfa = false;
        return true;
    }

    // PNG / JPEG / BTP: wybor poziomu (pelna rozdzielczosc albo podglad) wg wymiarow, pliku i budzetu.
    // Zwraca true, gdy powierzchnia jest gotowa ALBO uruchomiono podglad (zrodlo zostaje Brak do konca).
    bool otworz_rastrowy(const char* sc, FormatObrazu f, uint32_t rsz, const uint8_t* hdr, uint32_t got) {
        uint32_t nw = 0, nh = 0;
        if (!wymiary_z_naglowka(f, hdr, got, &nw, &nh) && f != FormatObrazu::BTP) {
            // JPEG ze znacznikiem SOF poza pierwszymi 256 B: dedykowany parser naglowka (strumieniowo).
            if (f == FormatObrazu::JPEG) {
                KontekstOdczytu k{sc};
                PodgladZrodlo z{czytaj_dla_podgladu, &k, rsz};
                bool prog = false;
                (void)podglad_jpeg_naglowek(z, &nw, &nh, &prog, alokuj_obraz, zwolnij_obraz);
            }
        }
        zrodlo_w = nw; zrodlo_h = nh;
        if (nw && nh && (nw > LIMIT_BOKU || nh > LIMIT_BOKU)) {
            char t[112] = {};
            kopiuj_txt(t, sizeof(t), "Obraz zbyt duży: ");
            dopisz_u32(t, sizeof(t), nw); dopisz_txt(t, sizeof(t), " x "); dopisz_u32(t, sizeof(t), nh);
            dopisz_txt(t, sizeof(t), " (limit 8192 px na bok)");
            ustaw_komunikat(t);
            return false;
        }
        const uint64_t px = static_cast<uint64_t>(nw) * nh;
        const bool pelny = (nw == 0 || nh == 0) ||
            (px <= LIMIT_PIKSELI_PELNY && rsz <= LIMIT_PLIKU_PELNY && szacunek_szczytu(f, px, rsz) <= BUDZET_SZCZYTU);
        if (pelny) {
            const BladObrazu b = wczytaj_i_dekoduj_pelny(sc);
            if (b == BladObrazu::OK) return true;
            // Brak pamieci przy pelnym dekodowaniu JPEG: bezpieczny powrot do podgladu.
            if (!(b == BladObrazu::Pamiec && f == FormatObrazu::JPEG)) return false;
            ustaw_komunikat("");
        }
        if (f == FormatObrazu::JPEG) return rozpocznij_podglad(sc, rsz);
        char t[128] = {};
        kopiuj_txt(t, sizeof(t), "Obraz PNG zbyt duży: ");
        dopisz_u32(t, sizeof(t), nw); dopisz_txt(t, sizeof(t), " x "); dopisz_u32(t, sizeof(t), nh);
        dopisz_txt(t, sizeof(t), " (podgląd tylko dla JPEG)");
        ustaw_komunikat(t);
        return false;
    }

    // Pelne dekodowanie (Poziom 1). Bledy ustawiaja komunikat; BladObrazu::Pamiec jest zwracany osobno.
    BladObrazu wczytaj_i_dekoduj_pelny(const char* sc) {
        uint8_t* dane = nullptr; size_t n = 0;
        const uint64_t nieudane0 = g_pam_nieudane;
        if (!wczytaj_plik_do_pamieci(sc, &dane, &n)) return g_pam_nieudane > nieudane0 ? BladObrazu::Pamiec : BladObrazu::Uszkodzony;
        DekodowanyObraz d{};
        const BladObrazu b = dekoduj_obraz_z_alfa_limity(dane, n, &d, alokuj_obraz, zwolnij_obraz,
                                                         LIMIT_PIKSELI_PELNY, LIMIT_PLIKU_PELNY);
        zwolnij_obraz(dane);
        if (b != BladObrazu::OK) {
            // Dekoder zglasza nieudana alokacje jako "uszkodzony": rozpoznaj po liczniku odmow alokatora.
            const BladObrazu realny = g_pam_nieudane > nieudane0 ? BladObrazu::Pamiec : b;
            ustaw_komunikat(komunikat_bledu(realny));
            return realny;
        }
        if (d.format_pikseli != OBRAZ_BGRA8888 || !d.piksele || !d.szerokosc || !d.wysokosc ||
            d.szerokosc > LIMIT_BOKU || d.wysokosc > LIMIT_BOKU ||
            static_cast<uint64_t>(d.szerokosc) * d.wysokosc * 4ULL > d.bajty) {
            if (d.piksele) zwolnij_obraz(d.piksele);
            ustaw_komunikat("Uszkodzony plik obrazu");
            return BladObrazu::Uszkodzony;
        }
        pow.piksele = reinterpret_cast<uint32_t*>(d.piksele);
        pow.szer = static_cast<int>(d.szerokosc); pow.wys = static_cast<int>(d.wysokosc);
        zrodlo_w = d.szerokosc; zrodlo_h = d.wysokosc;
        const size_t cnt = static_cast<size_t>(d.szerokosc) * d.wysokosc;
        pow.alfa = false;
        for (size_t i = 0; i < cnt; ++i) if ((pow.piksele[i] >> 24) != 255U) { pow.alfa = true; break; }
        zrodlo = Zrodlo::Powierzchnia;
        skala_podgladu = 0;
        return BladObrazu::OK;
    }

    // Podglad (Poziom 2): naglowek jest czytany od razu, reszta w kolejnych tykach timera.
    bool rozpocznij_podglad(const char* sc, uint32_t rsz) {
        kopiuj_txt(pctx_sciezka, sizeof(pctx_sciezka), sc);
        pctx.sciezka = pctx_sciezka;
        PodgladZrodlo z{czytaj_dla_podgladu, &pctx, rsz};
        const PodgladOpcje o{BUDZET_PODGLADU, PODGLAD_MAX_PIKSELI, LIMIT_BOKU, 64ULL * 1024ULL * 1024ULL};
        BladObrazu b = BladObrazu::OK;
        pdek = podglad_jpeg_otworz(z, o, alokuj_obraz, zwolnij_obraz, &b);
        if (!pdek) { ustaw_komunikat(b == BladObrazu::ZaDuzy ? "Obraz zbyt duży nawet dla podglądu" : komunikat_bledu(b)); return false; }
        ustaw_komunikat("Wczytywanie podglądu... 0%");
        return true;
    }
    char pctx_sciezka[512] = {};
    uint32_t licznik_tykow_podgladu{0};

    // Jedna porcja pracy podgladu (~6 ms). Zwraca true, gdy stan obrazu sie zmienil.
    void krok_podgladu() {
        if (!pdek) return;
        const uint64_t t0 = czas_ns();
        PodgladStan st;
        do {
            st = podglad_jpeg_krok(pdek, 2);
        } while (st == PodgladStan::Dalej && t0 != 0 && czas_ns() - t0 < PORCJA_PODGLADU_NS);
        if (st == PodgladStan::Dalej) {
            char t[64] = {};
            kopiuj_txt(t, sizeof(t), "Wczytywanie podglądu... ");
            dopisz_u32(t, sizeof(t), podglad_jpeg_postep_promile(pdek) / 10U); dopisz_txt(t, sizeof(t), "%");
            ustaw_komunikat(t);
            return;
        }
        if (st == PodgladStan::Blad) {
            const BladObrazu b = podglad_jpeg_blad(pdek);
            podglad_jpeg_zamknij(pdek, false); pdek = nullptr;
            ustaw_komunikat(b == BladObrazu::Pamiec ? "Brak pamięci na podgląd" : komunikat_bledu(b));
            return;
        }
        const PodgladWynik w = podglad_jpeg_wynik(pdek);
        pow.piksele = w.bgra; pow.szer = static_cast<int>(w.szer); pow.wys = static_cast<int>(w.wys); pow.alfa = false;
        zrodlo_w = w.szer_zrodla; zrodlo_h = w.wys_zrodla; skala_podgladu = w.skala;
        podglad_jpeg_zamknij(pdek, true); pdek = nullptr;
        zrodlo = Zrodlo::Powierzchnia;
        ustaw_komunikat("");
        tryb_dopasuj = true;
        wd = Powiekszenie::Widok{};
        wd.szer_obr = pow.szer; wd.wys_obr = pow.wys; wd.zoom = 100;
        zakoncz_pomiar();
        ++wersja_obrazu;
    }

    void zakoncz_pomiar() {
        t_koniec_ns = czas_ns();
        pom_szczyt = g_pam_szczyt;   // szczyt jednoczesnych alokacji obrazu od poczatku otwarcia
        pom_powierzchnia = zrodlo == Zrodlo::Powierzchnia ? static_cast<uint64_t>(pow.szer) * pow.wys * 4ULL : 0;
    }

    // Otwiera plik (galeria albo zewnetrzny). Zwraca true, gdy obraz jest gotowy do wyswietlenia.
    bool otworz_plik(const char* sc) {
        zwolnij_obraz_biezacy();
        t_start_ns = czas_ns(); t_koniec_ns = 0; pom_baza = g_pam_aktualna; g_pam_szczyt = g_pam_aktualna;
        ustaw_komunikat("");
        kopiuj_txt(sciezka, sizeof(sciezka), sc ? sc : "");
        kopiuj_txt(aktualna_sciezka, sizeof(aktualna_sciezka), sciezka);
        bool ok = false;
        uint32_t rsz = 0;
        uint8_t hdr[256] = {};
        uint32_t got = 0;
        if (!sc || sc[0] != '/' || !pobierz_rozmiar_pliku(sc, &rsz)) {
            ustaw_komunikat("Nie można odczytać pliku");
        } else if (rsz == 0) {
            ustaw_komunikat("Pusty plik");
        } else if (!czytaj_fragment_pliku(sc, 0, reinterpret_cast<char*>(hdr), rsz < 256U ? rsz : 256U, &got) || got == 0) {
            ustaw_komunikat("Nie można odczytać pliku");
        } else {
            const FormatObrazu f = rozpoznaj_obraz(hdr, got);
            if (f == FormatObrazu::BMP) {
                format = FormatPliku::BMP;
                uint64_t px = 0;
                const char* blad = parsuj_naglowek_bmp(hdr, got, rsz);
                if (blad) {
                    ustaw_komunikat(blad);
                } else if (mnoz_u64(img_w, img_h, &px) && px <= LIMIT_PIKSELI_BMP) {
                    ok = bmp_do_powierzchni(sc, hdr, got, rsz);
                    zwolnij_bmp();
                    if (ok) zrodlo = Zrodlo::Powierzchnia;
                } else if (przygotuj_silnik_bmp()) {
                    // Bardzo duzy BMP: strumieniowy silnik (Dopasuj / 1:1), bez powierzchni w pamieci.
                    ok = wczytaj_obraz(sc);
                    if (ok) zrodlo = Zrodlo::StrumienBmp; else if (blad_txt[0]) ustaw_komunikat(blad_txt);
                } else {
                    ustaw_komunikat("Brak pamięci na silnik BMP");
                }
            } else if (f == FormatObrazu::PNG || f == FormatObrazu::JPEG || f == FormatObrazu::BTP) {
                format = f == FormatObrazu::PNG ? FormatPliku::PNG : (f == FormatObrazu::JPEG ? FormatPliku::JPEG : FormatPliku::BTP);
                ok = otworz_rastrowy(sc, f, rsz, hdr, got);
            } else {
                ustaw_komunikat(f == FormatObrazu::GIF ? "Format GIF nie jest obsługiwany" : "Nieobsługiwany lub uszkodzony plik");
            }
        }
        if (ok) {
            tryb_dopasuj = true;
            if (zrodlo == Zrodlo::Powierzchnia) {
                wd = Powiekszenie::Widok{};
                wd.szer_obr = pow.szer; wd.wys_obr = pow.wys; wd.zoom = 100;
            } else {
                view_x = view_y = 0;
            }
        } else {
            zrodlo = Zrodlo::Brak;
        }
        if (zrodlo == Zrodlo::Powierzchnia) zakoncz_pomiar();
        ++wersja_obrazu;
        return ok;
    }

    // ---- galeria ----------------------------------------------------------------
    void zaladuj_galerie() {
        gal = ModelGalerii{};
        char* bufor = static_cast<char*>(gui_malloc(64U * 1024U));
        if (!bufor) { ustaw_komunikat("Brak pamięci"); return; }
        bufor[0] = '\0';
        const bool ok = wylistuj_katalog_uzytkownika(KATALOG_GALERII, bufor, 64U * 1024U);
        size_t dl = 0;
        while (dl < 64U * 1024U && bufor[dl]) ++dl;
        if (!ok) {
            gal.zbuduj(KATALOG_GALERII, "", 0);
            ustaw_komunikat("Brak katalogu /dane/zdjecia");
        } else if (dl >= 64U * 1024U) {
            gal.zbuduj(KATALOG_GALERII, bufor, 64U * 1024U - 1U);
        } else {
            gal.zbuduj(KATALOG_GALERII, bufor, dl);
        }
        gui_free(bufor);
        if (gal.liczba == 0 && ok) ustaw_komunikat("Brak zdjęć");
        gal.zawijaj = zawijaj_galerie;
    }
    bool zawijaj_galerie{true};

    bool otworz_z_galerii(int indeks) {
        char sc[512];
        if (!gal.pelna_sciezka(indeks, sc, sizeof(sc))) return false;
        gal.biezacy = indeks;
        zewnetrzny = false;
        return otworz_plik(sc);
    }

    // Poprzednie/Nastepne: ladowanie w nastepnym kroku timera (najpierw widac "Wczytywanie...").
    void zaplanuj_indeks(int indeks) {
        if (indeks < 0) return;
        oczekujacy = indeks;
        oczekuje_plik = false;
        ustaw_komunikat("Wczytywanie...");
        odswiez_wszystko();
        synchronizuj_timer();
    }
    void nastepny() { zaplanuj_indeks(gal.nastepny()); }
    void poprzedni() { zaplanuj_indeks(gal.poprzedni()); }
    void pierwszy() { if (gal.liczba > 0 && gal.biezacy != 0) zaplanuj_indeks(0); }
    void ostatni() { if (gal.liczba > 0 && gal.biezacy != gal.liczba - 1) zaplanuj_indeks(gal.liczba - 1); }

    void zaplanuj_plik(const char* sc) {
        kopiuj_txt(oczekujacy_plik, sizeof(oczekujacy_plik), sc);
        oczekuje_plik = true;
        oczekujacy = -1;
        ustaw_komunikat("Wczytywanie...");
        odswiez_wszystko();
        synchronizuj_timer();
    }

    // Plik wybrany recznie: jesli lezy w katalogu galerii - wybierz jego indeks; inaczej "zewnetrzny"
    // (model galerii, biezacy indeks i domyslny katalog pozostaja nietkniete).
    void otworz_reczny(const char* sc) {
        char kat[128];
        size_t n = 0, ost = 0;
        while (sc[n]) { if (sc[n] == '/') ost = n; ++n; }
        size_t k = 0;
        for (; k < ost && k + 1 < sizeof(kat); ++k) kat[k] = sc[k];
        kat[k] = '\0';
        const char* nazwa = sc + ost + 1;
        int idx = -1;
        size_t a = 0;
        while (kat[a] && KATALOG_GALERII[a] && kat[a] == KATALOG_GALERII[a]) ++a;
        if (!kat[a] && !KATALOG_GALERII[a]) idx = gal.znajdz(nazwa);
        if (idx >= 0) { zaplanuj_indeks(idx); return; }
        zaplanuj_plik(sc);
    }

    void wczytaj_oczekujace() {
        const int idx = oczekujacy;
        const bool plik = oczekuje_plik;
        oczekujacy = -1; oczekuje_plik = false;
        if (plik) {
            zewnetrzny = true;
            otworz_plik(oczekujacy_plik);
        } else if (idx >= 0) {
            otworz_z_galerii(idx);
        }
        ustaw_tytul();
        odswiez_wszystko();
    }

    // ---- zegar: wczytywanie i postepowe dekodowanie 1:1 (strumieniowy BMP) -------
    static void tykniecie(void* ctx) { static_cast<AplikacjaObrazow*>(ctx)->tyk(); }

    void synchronizuj_timer() {
        const bool potrzebny = oczekujacy >= 0 || oczekuje_plik || pdek != nullptr ||
                               (zrodlo == Zrodlo::StrumienBmp && render_1do1.active && tryb == TrybSkalowania::JEDEN_DO_JEDNEGO);
        if (potrzebny == timer_wlaczony || !app) return;
        timer_wlaczony = potrzebny;
        app->ustaw_zadanie_okresowe(potrzebny ? OKRES_KROKU_TIKI : 0, &AplikacjaObrazow::tykniecie, this);
    }

    void tyk() {
        if (oczekujacy >= 0 || oczekuje_plik) { wczytaj_oczekujace(); synchronizuj_timer(); return; }
        if (pdek) {
            krok_podgladu();
            // Pelne odswiezenie interfejsu nie na kazdym tyku: praca dekodera ma pierwszenstwo.
            if (!pdek) { ustaw_tytul(); odswiez_wszystko(); }
            else if ((++licznik_tykow_podgladu & 7U) == 0U) odswiez_wszystko();
            return;
        }
        if (zrodlo == Zrodlo::StrumienBmp && render_1do1.active) (void)krok_renderowania_1do1();
        if (!render_1do1.active) { odswiez_status(); if (widok) widok->uniewaznij(); }
        synchronizuj_timer();
    }

    // ---- stan -> UI -----------------------------------------------------------
    int zoom_procent() const {
        if (zrodlo == Zrodlo::Powierzchnia) return wd.zoom;
        if (zrodlo == Zrodlo::StrumienBmp) return tryb == TrybSkalowania::JEDEN_DO_JEDNEGO ? 100 : 0;
        return 100;
    }

    // Tekst powiekszenia wzgledem PLIKU zrodlowego. Podglad 1/N pokazuje "~X%" (X = zoom / N),
    // zeby nie przedstawiac pomniejszonego podgladu jako widoku 100%.
    void zoom_tekst(char* b, size_t cap) const {
        b[0] = '\0';
        if (zrodlo == Zrodlo::StrumienBmp && tryb == TrybSkalowania::DOPASUJ) { kopiuj_txt(b, cap, "Dopasuj"); return; }
        const uint32_t z = static_cast<uint32_t>(zoom_procent());
        if (skala_podgladu > 0) {
            dopisz_txt(b, cap, "~"); dopisz_u32(b, cap, z / skala_podgladu); dopisz_txt(b, cap, "%");
        } else { dopisz_u32(b, cap, z); dopisz_txt(b, cap, "%"); }
    }

    static const char* nazwa_pliku(const char* sc) {
        const char* n = sc;
        for (const char* q = sc; *q; ++q) if (*q == '/') n = q + 1;
        return n;
    }

    void odswiez_status() {
        if (!pasek) return;
        char b[128] = {};
        if (zrodlo == Zrodlo::Brak) {
            pasek->ustaw_tekst(0, komunikat[0] ? komunikat : "Brak obrazu");
            pasek->ustaw_tekst(1, gal.liczba ? "" : "");
            if (gal.liczba > 0 && !zewnetrzny) {
                b[0] = '\0'; dopisz_txt(b, sizeof(b), "Zdjęcie "); dopisz_u32(b, sizeof(b), static_cast<uint32_t>(gal.biezacy + 1));
                dopisz_txt(b, sizeof(b), " z "); dopisz_u32(b, sizeof(b), static_cast<uint32_t>(gal.liczba));
                pasek->ustaw_tekst(1, b);
            }
            pasek->ustaw_tekst(2, ""); pasek->ustaw_tekst(3, ""); pasek->ustaw_tekst(4, "");
        } else {
            if (skala_podgladu > 0) {
                char n0[160] = {};
                kopiuj_txt(n0, sizeof(n0), nazwa_pliku(sciezka));
                dopisz_txt(n0, sizeof(n0), "  [podgląd 1/"); dopisz_u32(n0, sizeof(n0), skala_podgladu); dopisz_txt(n0, sizeof(n0), "]");
                pasek->ustaw_tekst(0, n0);
            } else pasek->ustaw_tekst(0, nazwa_pliku(sciezka));
            b[0] = '\0';
            if (zewnetrzny) dopisz_txt(b, sizeof(b), "Plik spoza galerii");
            else { dopisz_txt(b, sizeof(b), "Zdjęcie "); dopisz_u32(b, sizeof(b), static_cast<uint32_t>(gal.biezacy + 1));
                   dopisz_txt(b, sizeof(b), " z "); dopisz_u32(b, sizeof(b), static_cast<uint32_t>(gal.liczba)); }
            pasek->ustaw_tekst(1, b);
            b[0] = '\0';
            const uint32_t w = skala_podgladu > 0 ? zrodlo_w : (zrodlo == Zrodlo::Powierzchnia ? static_cast<uint32_t>(pow.szer) : img_w);
            const uint32_t h = skala_podgladu > 0 ? zrodlo_h : (zrodlo == Zrodlo::Powierzchnia ? static_cast<uint32_t>(pow.wys) : img_h);
            dopisz_u32(b, sizeof(b), w); dopisz_txt(b, sizeof(b), " x "); dopisz_u32(b, sizeof(b), h);
            pasek->ustaw_tekst(2, b);
            pasek->ustaw_tekst(3, Galeria::nazwa_formatu(format));
            b[0] = '\0';
            zoom_tekst(b, sizeof(b));
            pasek->ustaw_tekst(4, b);
        }
    }

    void ustaw_tytul() {
        if (!okno) return;
        char t[96] = {};
        kopiuj_txt(t, sizeof(t), "Przeglądarka Obrazów");
        if (zrodlo != Zrodlo::Brak) { dopisz_txt(t, sizeof(t), " - "); dopisz_txt(t, sizeof(t), nazwa_pliku(sciezka)); }
        okno->ustaw_tytul(t);
        (void)gui_ustaw_tytul_okna(t);
    }

    void odswiez_sterowanie() {
        const bool zr = zrodlo == Zrodlo::Powierzchnia;
        const bool nav = gal.mozna_nawigowac();
        const bool nast = nav && (gal.zawijaj || gal.biezacy + 1 < gal.liczba);
        const bool poprz = nav && (gal.zawijaj || gal.biezacy > 0);
        auto wl = [](Przycisk* p, bool v) { if (p) p->ustaw_wlaczona(v); };
        wl(btn_nast, nast); wl(btn_strz_p, nast); wl(btn_poprz, poprz); wl(btn_strz_l, poprz);
        wl(btn_plus, zr || zrodlo == Zrodlo::StrumienBmp); wl(btn_minus, zr || zrodlo == Zrodlo::StrumienBmp);
        wl(btn_fit, zrodlo != Zrodlo::Brak); wl(btn_100, zrodlo != Zrodlo::Brak && skala_podgladu == 0);
        if (suwak) {
            suwak->ustaw_wlaczona(zr);
            aktualizuje_suwak = true;
            suwak->ustaw_wartosc(Powiekszenie::zakres_zoom(zoom_procent()));
            aktualizuje_suwak = false;
        }
        if (lbl_zoom) {
            char b[16] = {};
            zoom_tekst(b, sizeof(b));
            lbl_zoom->ustaw_tekst(b);
        }
    }

    void odswiez_wszystko() {
        // W trybie Dopasuj procent wynika z rozmiaru widoku: policz go PRZED odswiezeniem etykiet,
        // zeby pasek narzedzi (rysowany przed widokiem) nie pokazywal starej wartosci.
        if (zrodlo == Zrodlo::Powierzchnia && tryb_dopasuj && widok && widok->szerokosc() > 0) {
            const int aw = widok->szerokosc() - 2 * MARGINES_OBRAZU, ah = widok->wysokosc() - 2 * MARGINES_OBRAZU;
            if (aw > 0 && ah > 0) {
                wd.szer_obr = pow.szer; wd.wys_obr = pow.wys; wd.vw = aw; wd.vh = ah;
                wd.zoom = Powiekszenie::zoom_dopasuj(wd.szer_obr, wd.wys_obr, aw, ah);
                zoom_pokazany = wd.zoom;
            }
        }
        odswiez_status();
        odswiez_sterowanie();
        ++klucz_renderu;
        if (widok) widok->uniewaznij();
        synchronizuj_timer();
    }

    // ---- polecenia powiekszenia ------------------------------------------------
    int srodek_x() const { return widok ? widok->szerokosc() / 2 : 0; }
    int srodek_y() const { return widok ? widok->wysokosc() / 2 : 0; }

    void ustaw_zoom(int proc, int ax, int ay) {
        if (zrodlo == Zrodlo::StrumienBmp) { if (proc >= 100) jeden_do_jednego(); else dopasuj(); return; }
        if (zrodlo != Zrodlo::Powierzchnia) return;
        tryb_dopasuj = false;
        Powiekszenie::zmien_zoom(wd, Powiekszenie::zakres_zoom(proc), ax, ay);
        odswiez_wszystko();
    }
    void zoom_suwakiem(int proc) {
        if (aktualizuje_suwak) return;
        ustaw_zoom(proc, srodek_x(), srodek_y());       // suwak: zachowuje srodek widoku
    }
    void zoom_krok(int kierunek, int ax, int ay) {
        if (zrodlo == Zrodlo::StrumienBmp) { if (kierunek > 0) jeden_do_jednego(); else dopasuj(); return; }
        ustaw_zoom(Powiekszenie::zoom_krok(wd.zoom, kierunek), ax, ay);
    }
    void powieksz() { zoom_krok(+1, srodek_x(), srodek_y()); }
    void pomniejsz() { zoom_krok(-1, srodek_x(), srodek_y()); }
    void dopasuj() {
        if (zrodlo == Zrodlo::Brak) return;
        if (zrodlo == Zrodlo::StrumienBmp) { if (aktywuj_tryb(TrybSkalowania::DOPASUJ)) odswiez_wszystko(); return; }
        tryb_dopasuj = true;
        odswiez_wszystko();
    }
    void jeden_do_jednego() {
        if (zrodlo == Zrodlo::Brak) return;
        if (skala_podgladu > 0) {
            // Podglad ma 1/N rozdzielczosci pliku: prawdziwy widok 100% nie jest dostepny.
            pokaz_pomoc("Widok 100% niedostępny",
                        "Ten obraz jest za duży na pełne dekodowanie, więc wyświetlany jest podgląd o zmniejszonej rozdzielczości.\n"
                        "Widok 100% (1 piksel obrazu = 1 piksel ekranu) nie jest dla niego dostępny. Dopasowanie i powiększenie podglądu działają.");
            return;
        }
        if (zrodlo == Zrodlo::StrumienBmp) { if (aktywuj_tryb(TrybSkalowania::JEDEN_DO_JEDNEGO)) odswiez_wszystko(); return; }
        ustaw_zoom(100, srodek_x(), srodek_y());
    }
    void przesun(int dx, int dy) {
        if (zrodlo == Zrodlo::StrumienBmp) { if (przesun_widok(dx, dy)) odswiez_wszystko(); return; }
        if (zrodlo != Zrodlo::Powierzchnia || tryb_dopasuj) return;
        Powiekszenie::przesun(wd, dx, dy);
        odswiez_wszystko();
    }
    void zamknij_obraz() {
        zwolnij_obraz_biezacy();
        ustaw_komunikat("");
        sciezka[0] = '\0';
        zewnetrzny = false;
        ustaw_tytul();
        odswiez_wszystko();
    }

    // Zoom w trybie Dopasuj wynika z rozmiaru widoku (znanego dopiero przy rysowaniu).
    void odswiez_sterowanie_zoom_jesli_zmieniony() {
        if (wd.zoom == zoom_pokazany) return;
        zoom_pokazany = wd.zoom;
        odswiez_status();
        odswiez_sterowanie();
    }

    void ustaw_wygladzanie(bool v) { wygladzanie = v; ++klucz_renderu; odswiez_wszystko(); }
    void ustaw_zawijanie(bool v) { zawijaj_galerie = v; gal.zawijaj = v; odswiez_wszystko(); }

    void pokaz_dialog_otwierania() {
        if (!okno) return;
        DialogPliku::otworz(okno->zarzadca_popupow(), KATALOG_GALERII,
            "Obrazy (*.png;*.jpg;*.jpeg;*.btp;*.bmp)|*.png;*.jpg;*.jpeg;*.btp;*.bmp;;Wszystkie pliki|*.*",
            [](DialogPliku*, WynikDialoguPliku wynik, const char* sc, void*) {
                if (wynik == WynikDialoguPliku::Wybrano && sc && sc[0]) g_obrazy_otworz(sc);
            }, nullptr, okno->kontrolka_glowna(), "Otwórz obraz");
    }
    static void g_obrazy_otworz(const char* sc);

    static void dopisz_mib(char* b, size_t cap, uint64_t bajty) {
        dopisz_u32(b, cap, static_cast<uint32_t>(bajty / (1024ULL * 1024ULL))); dopisz_txt(b, cap, ",");
        dopisz_u32(b, cap, static_cast<uint32_t>((bajty % (1024ULL * 1024ULL)) * 10ULL / (1024ULL * 1024ULL))); dopisz_txt(b, cap, " MiB");
    }

    // Pomiary ostatniego otwarcia: tryb, wymiary, pamiec i czasy (pomoc w ocenie budzetu pamieci).
    void pokaz_informacje() {
        char t[640] = {};
        if (zrodlo != Zrodlo::Powierzchnia && zrodlo != Zrodlo::StrumienBmp) { pokaz_pomoc("Informacje o obrazie", "Brak otwartego obrazu."); return; }
        dopisz_txt(t, sizeof(t), "Plik: "); dopisz_txt(t, sizeof(t), nazwa_pliku(sciezka)); dopisz_txt(t, sizeof(t), "\n");
        dopisz_txt(t, sizeof(t), "Wymiary pliku: "); dopisz_u32(t, sizeof(t), skala_podgladu ? zrodlo_w : static_cast<uint32_t>(pow.szer));
        dopisz_txt(t, sizeof(t), " x "); dopisz_u32(t, sizeof(t), skala_podgladu ? zrodlo_h : static_cast<uint32_t>(pow.wys)); dopisz_txt(t, sizeof(t), "\n");
        if (skala_podgladu) {
            dopisz_txt(t, sizeof(t), "Tryb: PODGLĄD 1/"); dopisz_u32(t, sizeof(t), skala_podgladu);
            dopisz_txt(t, sizeof(t), " ("); dopisz_u32(t, sizeof(t), static_cast<uint32_t>(pow.szer)); dopisz_txt(t, sizeof(t), " x ");
            dopisz_u32(t, sizeof(t), static_cast<uint32_t>(pow.wys)); dopisz_txt(t, sizeof(t), "), bez widoku 100%\n");
        } else dopisz_txt(t, sizeof(t), "Tryb: pełna rozdzielczość\n");
        dopisz_txt(t, sizeof(t), "Powierzchnia: "); dopisz_mib(t, sizeof(t), pom_powierzchnia);
        dopisz_txt(t, sizeof(t), "\nSzczyt pamięci obrazu: "); dopisz_mib(t, sizeof(t), pom_szczyt);
        dopisz_txt(t, sizeof(t), "\nAktualnie zajęte: "); dopisz_mib(t, sizeof(t), g_pam_aktualna);
        dopisz_txt(t, sizeof(t), "\nCzas wczytania: ");
        dopisz_u32(t, sizeof(t), t_koniec_ns > t_start_ns ? static_cast<uint32_t>((t_koniec_ns - t_start_ns) / 1000000ULL) : 0U);
        dopisz_txt(t, sizeof(t), " ms\nOstatnie rysowanie: ");
        dopisz_u32(t, sizeof(t), static_cast<uint32_t>(render_ns / 1000ULL)); dopisz_txt(t, sizeof(t), " us\nAlokacji: ");
        dopisz_u32(t, sizeof(t), static_cast<uint32_t>(g_pam_liczba)); dopisz_txt(t, sizeof(t), ", nieudanych: ");
        dopisz_u32(t, sizeof(t), static_cast<uint32_t>(g_pam_nieudane));
        pokaz_pomoc("Informacje o obrazie", t);
    }

    void pokaz_pomoc(const char* tytul, const char* tekst) {
        if (okno) Komunikat::informacja(okno->zarzadca_popupow(), tytul, tekst, nullptr, nullptr, okno->kontrolka_glowna());
    }
};

AplikacjaObrazow g_obrazy;

void AplikacjaObrazow::g_obrazy_otworz(const char* sc) { g_obrazy.otworz_reczny(sc); }

// Ujscie silnika strumieniowego BMP: gotowy fragment bufora => unieważnij kontrolke.
bool ujscie_do_widoku(int, int, int, int, const uint32_t*) {
    if (g_obrazy.widok) g_obrazy.widok->uniewaznij();
    return true;
}

void ustaw_obszar_widoku(int szer, int wys) {
    ProstokatObrazu r{0, 0, szer - 2 * MARGINES_OBRAZU, wys - 2 * MARGINES_OBRAZU};
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
    g_obszar_obrazu = r;
}

// Rysowanie widoku: powierzchnia -> renderer powiekszenia -> bufor; strumien BMP -> silnik.
void rysuj_widok(PowierzchniaRysunkowa* pow_ui, KontekstRysowania& ctx, void*) {
    AplikacjaObrazow& s = g_obrazy;
    ustaw_kolor_tla(motyw_kolor(MOTYW_KOLOR_OKNOTLO));
    ustaw_obszar_widoku(pow_ui->szerokosc(), pow_ui->wysokosc());
    const ProstokatObrazu a = pobierz_prostokat_obrazu();
    if (a.w <= 0 || a.h <= 0) return;
    const Prostokat ekran = pow_ui->prostokat_do_ekranu(Prostokat(MARGINES_OBRAZU, MARGINES_OBRAZU, a.w, a.h));
    const Prostokat lokalny(MARGINES_OBRAZU, MARGINES_OBRAZU, a.w, a.h);

    auto komunikat = [&](const char* tekst, uint32_t kolor) {
        ctx.rysuj_prostokat(lokalny, kolor_tla);
        ctx.rysuj_tekst_wysrodkowany(lokalny, tekst, kolor, 1);
    };

    if (s.zrodlo == Zrodlo::Brak) {
        if (s.komunikat[0]) komunikat(s.komunikat, motyw_kolor(s.komunikat[0] == 'W' && s.komunikat[1] == 'c' ?
                                      MOTYW_KOLOR_TEKST_DRUGI : MOTYW_KOLOR_BLAD));
        else komunikat("Brak obrazu - Plik > Otwórz...", motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI));
        return;
    }
    uint64_t piksele = 0;
    if (!s.bufory_ok || !mnoz_u64(static_cast<uint64_t>(a.w), static_cast<uint64_t>(a.h), &piksele) ||
        piksele > viewport_capacity) {
        komunikat("Okno zbyt duże dla bufora obrazu", motyw_kolor(MOTYW_KOLOR_BLAD));
        return;
    }
    if (s.zrodlo == Zrodlo::Powierzchnia) {
        Powiekszenie::Widok& wd = s.wd;
        wd.vw = a.w; wd.vh = a.h;
        wd.szer_obr = s.pow.szer; wd.wys_obr = s.pow.wys;
        if (s.tryb_dopasuj) {
            wd.zoom = Powiekszenie::zoom_dopasuj(wd.szer_obr, wd.wys_obr, a.w, a.h);
            Powiekszenie::wysrodkuj(wd);
        } else {
            Powiekszenie::ogranicz_offset(wd);
        }
        // Przerenderuj tylko, gdy zmienil sie stan widoku (zoom/offset/rozmiar/obraz/tlo/filtr).
        const uint32_t klucz = static_cast<uint32_t>(wd.zoom) * 2654435761U ^ static_cast<uint32_t>(wd.ox) * 40503U ^
                               static_cast<uint32_t>(wd.oy) * 9973U ^ static_cast<uint32_t>(a.w) * 131U ^ static_cast<uint32_t>(a.h) * 17U ^
                               s.wersja_obrazu * 7919U ^ (s.wygladzanie ? 0x5A5A5A5AU : 0U) ^ kolor_tla;
        if (klucz != s.klucz_ostatni) {
            const uint64_t t_r0 = czas_ns();
            if (!Powiekszenie::renderuj(s.pow.piksele, s.pow.szer, s.pow.wys, s.pow.alfa, viewport_buf, viewport_capacity,
                                        wd, kolor_tla, s.wygladzanie)) {
                komunikat("Błąd renderowania obrazu", motyw_kolor(MOTYW_KOLOR_BLAD));
                return;
            }
            s.klucz_ostatni = klucz;
            s.render_ns = czas_ns() - t_r0;
        }
        for (int y = 0; y < a.h; ++y)
            ctx.rysuj_wiersz_ekranu(ekran.x, ekran.y + y, a.w, viewport_buf + static_cast<size_t>(y) * static_cast<size_t>(a.w));
        s.odswiez_sterowanie_zoom_jesli_zmieniony();
        return;
    }
    // Strumieniowy BMP (powyzej limitu powierzchni): dotychczasowy silnik.
    const bool dopasuj = tryb == TrybSkalowania::DOPASUJ;
    const bool ok = dopasuj ? renderuj_dopasowany(a.w, a.h) : renderuj_1do1(a.w, a.h);
    if (!ok) { komunikat("Nie można zdekodować obrazu", motyw_kolor(MOTYW_KOLOR_BLAD)); return; }
    const uint32_t* bufor = dopasuj ? fit_buf : viewport_buf;
    for (int y = 0; y < a.h; ++y)
        ctx.rysuj_wiersz_ekranu(ekran.x, ekran.y + y, a.w, bufor + static_cast<size_t>(y) * static_cast<size_t>(a.w));
    s.synchronizuj_timer();
}

void mysz_wcisnieta(PowierzchniaRysunkowa*, const ZdarzenieMyszki& e, void*) {
    AplikacjaObrazow& s = g_obrazy;
    if (e.przycisk_myszy() != PrzyciskMyszy::Lewy || s.zrodlo == Zrodlo::Brak) return;
    if (s.zrodlo == Zrodlo::Powierzchnia && s.tryb_dopasuj) return;
    if (s.zrodlo == Zrodlo::StrumienBmp && tryb != TrybSkalowania::JEDEN_DO_JEDNEGO) return;
    s.przesuwanie = true;
    s.przesuw_mx = e.x; s.przesuw_my = e.y;
    s.przesuw_ox = s.wd.ox; s.przesuw_oy = s.wd.oy;
    s.przesuw_vx = view_x; s.przesuw_vy = view_y;
}

void mysz_ruch(PowierzchniaRysunkowa*, const ZdarzenieMyszki& e, void*) {
    AplikacjaObrazow& s = g_obrazy;
    if (!s.przesuwanie) return;
    if ((e.modyfikatory & 1U) == 0U) { s.przesuwanie = false; return; }
    if (s.zrodlo == Zrodlo::StrumienBmp) {
        s.przesun(s.przesuw_vx - (e.x - s.przesuw_mx) - view_x, s.przesuw_vy - (e.y - s.przesuw_my) - view_y);
    } else {
        const int64_t nx = s.przesuw_ox - (e.x - s.przesuw_mx), ny = s.przesuw_oy - (e.y - s.przesuw_my);
        s.przesun(static_cast<int>(nx - s.wd.ox), static_cast<int>(ny - s.wd.oy));
    }
}

void mysz_zwolniona(PowierzchniaRysunkowa*, const ZdarzenieMyszki&, void*) { g_obrazy.przesuwanie = false; }

// Ctrl + kolko: powiekszenie z zachowaniem punktu pod kursorem; samo kolko: przewijanie pionowe.
void mysz_rolka(PowierzchniaRysunkowa* pow_ui, const ZdarzenieRolki& e, void*) {
    AplikacjaObrazow& s = g_obrazy;
    if (s.zrodlo == Zrodlo::Brak) return;
    const int dy = e.delta_y;
    if (dy == 0) return;
    if (e.czy_ctrl()) {
        const Prostokat lok = pow_ui->prostokat_do_ekranu(Prostokat(MARGINES_OBRAZU, MARGINES_OBRAZU, 1, 1));
        s.zoom_krok(dy > 0 ? +1 : -1, e.x - lok.x, e.y - lok.y);
    } else {
        s.przesun(0, dy > 0 ? -KROK_PRZESUNIECIA : KROK_PRZESUNIECIA);
    }
}

const char* TEKST_STEROWANIE =
    "Strzałki w lewo / w prawo, PgUp / PgDn - poprzednie / następne zdjęcie.\n"
    "Home / End - pierwsze / ostatnie zdjęcie.\n"
    "Ctrl + kółko myszy - powiększenie (punkt pod kursorem zostaje na miejscu).\n"
    "Kółko myszy, W A S D, przeciąganie myszą - przesuwanie obrazu.\n"
    "+ / - powiększ / pomniejsz, 0 lub 1 - 100%, F lub D - dopasuj do okna.";
const char* TEKST_O_PROGRAMIE =
    "Przeglądarka Obrazów dla Bursztyn OS\n"
    "Formaty: BMP, PNG, JPEG, BTP (BTP1 i BTP2).\n"
    "Domyślna galeria: /dane/zdjecia.";

class OknoObrazow : public Okno {
public:
    OknoObrazow(const char* tytul, int w, int h) : Okno(tytul, w, h) { ustaw_minimalny_rozmiar(MIN_SZER, MIN_WYS); }

    void obsluz_zdarzenie(const bws_zdarzenie& ev) override {
        AplikacjaObrazow& s = g_obrazy;
        if (ev.typ == BWS_ZDARZENIE_OTWORZ_PLIK) {
            char sc[512] = {};
            if (pobierz_argument_startowy(sc, sizeof(sc))) s.otworz_reczny(sc);
            return;
        }
        if (ev.typ == BWS_ZDARZENIE_MOTYW_ZMIENIONY) {
            Okno::obsluz_zdarzenie(ev);
            uniewaznij_cache_rozmiaru();                 // tlo zalezy od motywu; piksele obrazu bez zmian
            fit_cache = CacheDopasowania{};
            s.klucz_ostatni = 0xFFFFFFFFu;
            s.odswiez_wszystko();
            return;
        }
        const bool modalne = zarzadca_popupow().czy_otwarty();
        if (!modalne && ev.typ == BWS_ZDARZENIE_KLAWISZ && !czy_zminimalizowane()) {
            switch (ev.kod) {
                case '1': case '0': s.jeden_do_jednego(); return;
                case 'D': case 'f': case 'F': s.dopasuj(); return;
                case '+': case '=': s.powieksz(); return;
                case '-': case '_': s.pomniejsz(); return;
                case 'w': case 'W': s.przesun(0, -KROK_PRZESUNIECIA); return;
                case 's': case 'S': s.przesun(0, KROK_PRZESUNIECIA); return;
                case 'a': case 'A': s.przesun(-KROK_PRZESUNIECIA, 0); return;
                case 'd': s.przesun(KROK_PRZESUNIECIA, 0); return;
                default: break;
            }
        } else if (!modalne && ev.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY && ev.x != 0 && !czy_zminimalizowane() &&
                   !(ev.przyciski & (BWS_MOD_CTRL | BWS_MOD_ALT))) {
            switch (klawisz_polecenia(ev)) {          // strzalki, Home/End, PgUp/PgDn
                case 0x4b: case 0x49: s.poprzedni(); return;      // Left, PgUp
                case 0x4d: case 0x51: s.nastepny(); return;       // Right, PgDn
                case 0x47: s.pierwszy(); return;                  // Home
                case 0x4f: s.ostatni(); return;                   // End
                default: break;
            }
        }
        Okno::obsluz_zdarzenie(ev);
    }
};

} // namespace

#ifndef PRZEGLADARKA_HOST_TEST
extern "C" {
void* __dso_handle = nullptr;
int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
}
#endif

static Przycisk& dodaj_przycisk(Panel& rodzic, const char* tekst, int szer, void (*cb)(Przycisk*, void*)) {
    Przycisk& p = rodzic.dodaj<Przycisk>(tekst);
    p.ustaw_preferowany_rozmiar(Rozmiar(szer, 28));
    p.ustaw_mozliwosc_fokusu(false);                // klawisze zdjec zostaja przy oknie
    p.przy_kliknieciu(cb);
    return p;
}

static void obrazy_zbuduj_ui(AplikacjaGUI& app, OknoObrazow& okno) {
    g_obrazy.app = &app;
    g_obrazy.okno = &okno;
    app.ustaw_glowne_okno(&okno);
    g_ujscie_obrazu = &ujscie_do_widoku;

    Panel* korzen = new Panel(Orientacja::Pionowa);
    korzen->ustaw_wypelnienie(Marginesy(2, 2, 2, 0));
    korzen->ustaw_odstep(2);
    okno.ustaw_kontrolke_glowna(korzen);

    MenuGlowne& menu = korzen->dodaj<MenuGlowne>();
    Menu& m_plik = menu.menu().dodaj_podmenu("Plik", 100);
    m_plik.dodaj_element("Otwórz...", 101, [](ElementMenu&, void*) { g_obrazy.pokaz_dialog_otwierania(); });
    m_plik.dodaj_element("Zamknij obraz", 102, [](ElementMenu&, void*) { g_obrazy.zamknij_obraz(); });
    m_plik.dodaj_separator();
    m_plik.dodaj_element("Zakończ", 103, [](ElementMenu&, void*) { if (g_obrazy.app) g_obrazy.app->zakoncz(); });
    Menu& m_widok = menu.menu().dodaj_podmenu("Widok", 200);
    m_widok.dodaj_element("Poprzednie zdjęcie", 201, [](ElementMenu&, void*) { g_obrazy.poprzedni(); });
    m_widok.dodaj_element("Następne zdjęcie", 202, [](ElementMenu&, void*) { g_obrazy.nastepny(); });
    m_widok.dodaj_separator();
    m_widok.dodaj_element("Dopasuj do okna", 203, [](ElementMenu&, void*) { g_obrazy.dopasuj(); });
    m_widok.dodaj_element("Rozmiar rzeczywisty 100%", 204, [](ElementMenu&, void*) { g_obrazy.jeden_do_jednego(); });
    m_widok.dodaj_element("Powiększ", 205, [](ElementMenu&, void*) { g_obrazy.powieksz(); });
    m_widok.dodaj_element("Pomniejsz", 206, [](ElementMenu&, void*) { g_obrazy.pomniejsz(); });
    Menu& m_ust = menu.menu().dodaj_podmenu("Ustawienia", 300);
    g_obrazy.menu_ustawien = &m_ust;
    ElementMenu& e_wyg = m_ust.dodaj_element("Wygładzanie obrazu", 301, [](ElementMenu& el, void*) {
        g_obrazy.ustaw_wygladzanie(!g_obrazy.wygladzanie); el.ustaw_zaznaczony(g_obrazy.wygladzanie); });
    e_wyg.ustaw_typ_zaznaczenia(TypZaznaczeniaMenu::Pole).ustaw_zaznaczony(true);
    ElementMenu& e_zaw = m_ust.dodaj_element("Zawijaj galerię (po ostatnim - pierwsze)", 302, [](ElementMenu& el, void*) {
        g_obrazy.ustaw_zawijanie(!g_obrazy.zawijaj_galerie); el.ustaw_zaznaczony(g_obrazy.zawijaj_galerie); });
    e_zaw.ustaw_typ_zaznaczenia(TypZaznaczeniaMenu::Pole).ustaw_zaznaczony(true);
    Menu& m_pomoc = menu.menu().dodaj_podmenu("Pomoc", 400);
    m_pomoc.dodaj_element("Sterowanie", 401, [](ElementMenu&, void*) { g_obrazy.pokaz_pomoc("Sterowanie", TEKST_STEROWANIE); });
    m_pomoc.dodaj_element("Informacje o obrazie", 403, [](ElementMenu&, void*) { g_obrazy.pokaz_informacje(); });
    m_pomoc.dodaj_element("O programie", 402, [](ElementMenu&, void*) { g_obrazy.pokaz_pomoc("O programie", TEKST_O_PROGRAMIE); });

    Panel& pasek = korzen->dodaj<Panel>(Orientacja::Pozioma);
    pasek.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    pasek.ustaw_preferowany_rozmiar(Rozmiar(-1, 34));
    pasek.ustaw_wypelnienie(Marginesy(4, 2, 4, 2));
    pasek.ustaw_odstep(4);
    dodaj_przycisk(pasek, "Otwórz", 64, [](Przycisk*, void*) { g_obrazy.pokaz_dialog_otwierania(); });
    g_obrazy.btn_poprz = &dodaj_przycisk(pasek, "Poprzednie", 84, [](Przycisk*, void*) { g_obrazy.poprzedni(); });
    g_obrazy.btn_nast = &dodaj_przycisk(pasek, "Następne", 76, [](Przycisk*, void*) { g_obrazy.nastepny(); });
    g_obrazy.btn_fit = &dodaj_przycisk(pasek, "Dopasuj", 70, [](Przycisk*, void*) { g_obrazy.dopasuj(); });
    g_obrazy.btn_100 = &dodaj_przycisk(pasek, "100%", 52, [](Przycisk*, void*) { g_obrazy.jeden_do_jednego(); });
    g_obrazy.btn_minus = &dodaj_przycisk(pasek, "-", 30, [](Przycisk*, void*) { g_obrazy.pomniejsz(); });
    Suwak& suwak = pasek.dodaj<Suwak>();
    g_obrazy.suwak = &suwak;
    suwak.ustaw_zakres(Powiekszenie::ZOOM_MIN, Powiekszenie::ZOOM_MAX);
    suwak.ustaw_krok(1);
    suwak.ustaw_wartosc(100);
    suwak.ustaw_pokazywanie_wartosci(false);
    suwak.ustaw_preferowany_rozmiar(Rozmiar(150, 28));
    suwak.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    suwak.ustaw_mozliwosc_fokusu(false);
    suwak.przy_zmianie_wartosci([](Suwak*, int v, void*) { g_obrazy.zoom_suwakiem(v); });
    g_obrazy.btn_plus = &dodaj_przycisk(pasek, "+", 30, [](Przycisk*, void*) { g_obrazy.powieksz(); });
    Etykieta& lz = pasek.dodaj<Etykieta>("100%");
    g_obrazy.lbl_zoom = &lz;
    lz.ustaw_preferowany_rozmiar(Rozmiar(58, 28));
    lz.ustaw_wyrownanie_pionowe_tekstu(WyrownaniePionowe::Srodek);
    lz.ustaw_wyrownanie_poziome(WyrownaniePoziome::Poczatek);

    Panel& srodek = korzen->dodaj<Panel>(Orientacja::Pozioma);
    srodek.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    srodek.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    srodek.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    srodek.ustaw_wypelnienie(Marginesy(0, 0, 0, 0));
    srodek.ustaw_odstep(2);
    Przycisk& sl = srodek.dodaj<Przycisk>("<");
    g_obrazy.btn_strz_l = &sl;
    sl.ustaw_preferowany_rozmiar(Rozmiar(40, -1));
    sl.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    sl.ustaw_mozliwosc_fokusu(false);
    sl.przy_kliknieciu([](Przycisk*, void*) { g_obrazy.poprzedni(); });
    PowierzchniaRysunkowa* widok = new PowierzchniaRysunkowa();
    g_obrazy.widok = widok;
    widok->ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    widok->ustaw_mozliwosc_fokusu(false);
    widok->przy_rysowaniu(&rysuj_widok);
    widok->przy_wcisnieciu(&mysz_wcisnieta);
    widok->przy_ruchu(&mysz_ruch);
    widok->przy_zwolnieniu(&mysz_zwolniona);
    widok->przy_rolce(&mysz_rolka);
    srodek.dodaj_kontrolke(widok);
    Przycisk& sp = srodek.dodaj<Przycisk>(">");
    g_obrazy.btn_strz_p = &sp;
    sp.ustaw_preferowany_rozmiar(Rozmiar(40, -1));
    sp.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    sp.ustaw_mozliwosc_fokusu(false);
    sp.przy_kliknieciu([](Przycisk*, void*) { g_obrazy.nastepny(); });

    PasekStanu& sb = korzen->dodaj<PasekStanu>();
    g_obrazy.pasek = &sb;
    sb.ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
    sb.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    sb.dodaj_sekcje("Brak obrazu", 140, RozmiarSekcjiStanu::Rozciagnij);
    sb.dodaj_sekcje("", 110, RozmiarSekcjiStanu::Stala);
    sb.dodaj_sekcje("", 128, RozmiarSekcjiStanu::Stala);
    sb.dodaj_sekcje("", 46, RozmiarSekcjiStanu::Stala);
    sb.dodaj_sekcje("", 60, RozmiarSekcjiStanu::Stala);
    g_obrazy.odswiez_status();
    g_obrazy.odswiez_sterowanie();
}

#ifndef PRZEGLADARKA_HOST_TEST
extern "C" __attribute__((noreturn)) void _start() {
    AplikacjaGUI app;
    if (!g_obrazy.przygotuj_bufory()) bws_zakoncz_proces();
    OknoObrazow okno("Przeglądarka Obrazów", OKNO_SZER, OKNO_WYS);
    obrazy_zbuduj_ui(app, okno);

    char arg[512] = {};
    if (pobierz_argument_startowy(arg, sizeof(arg)) && arg[0]) {
        g_obrazy.zaladuj_galerie();
        g_obrazy.otworz_reczny(arg);
    } else {
        g_obrazy.zaladuj_galerie();
        if (g_obrazy.gal.liczba > 0) g_obrazy.zaplanuj_indeks(0);   // pierwsze zdjecie galerii
        else g_obrazy.odswiez_wszystko();
    }

    app.uruchom(okno);
    g_obrazy.zwolnij_obraz_biezacy();
    g_obrazy.zwolnij_bufory();
    bws_zakoncz_proces();
}
#endif
