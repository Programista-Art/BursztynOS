#pragma once
/*
 * Grajek - lekki, ograniczony pomiar czasu sekcji (diagnostyka ciaglosci dzwieku).
 *
 * Zasady:
 *  - domyslnie WYLACZONY; wlacza go klucz konfiguracji "diagnostyka=1". Wylaczony kosztuje jedno porownanie,
 *  - czas mierzony licznikiem TSC (rdtsc dziala w Ring 3; bez wywolan systemowych w goracej sciezce audio),
 *    przeliczanym na ns na podstawie zegara monotonicznego platformy raz na okno pomiarowe,
 *  - zero logowania na port szeregowy i zero alokacji: stale tablice suma/maks/liczba na sekcje,
 *  - okno pomiarowe jest zamykane przez kontroler (ok. 1 s); poprzednie okno jest dostepne do wyswietlenia
 *    i zapisu (ustawienia aplikacji, klucz "diagnostyka").
 *
 * Wyniki opisuja prace procesu Grajka w Ring 3 oraz czas spedzony w jego wywolaniach systemowych
 * (BWS 110, BWS 57 jako czesc "Okno: publikacja"). Nie mierza czasu, w ktorym proces nie byl wykonywany
 * (np. kompozycja w PID 0) - to pokazuje dopiero "przerwa producenta" silnika.
 */
#include <stddef.h>
#include <stdint.h>

namespace Grajek {

enum class Sekcja : uint8_t {
    Dekodowanie,         // dekoder WAV/MP3 (odczyt pliku wlacznie)
    Konwersja,           // resampler -> 48 kHz stereo S16
    Korektor,            // 10 pasm biquad
    Analiza,             // kopiowanie probek do analizatora (goraca sciezka)
    Bws110,              // QUERY/WRITE/FINISH strumienia PCM
    Widmo,               // FFT + szczyty (na zadanie interfejsu)
    OknoRysowanie,       // drzewo BursztynUI do bufora regionu (Ring 3)
    OknoPublikacja,      // BWS 57 (blit regionu) - od konca rysowania do konca cyklu
    Tabele,              // biblioteka + kolejka (czesc OknoRysowanie)
    Wizualizacja,        // wskazniki i widmo (czesc OknoRysowanie)
    Okladka,             // dekodowanie okladki (raz na utwor)
    Liczba
};

inline const char* nazwa_sekcji(Sekcja s) {
    switch (s) {
        case Sekcja::Dekodowanie: return "dekodowanie";
        case Sekcja::Konwersja: return "konwersja";
        case Sekcja::Korektor: return "korektor";
        case Sekcja::Analiza: return "analiza";
        case Sekcja::Bws110: return "BWS 110";
        case Sekcja::Widmo: return "widmo";
        case Sekcja::OknoRysowanie: return "rysowanie";
        case Sekcja::OknoPublikacja: return "publikacja";
        case Sekcja::Tabele: return "tabele";
        case Sekcja::Wizualizacja: return "wizualizacja";
        case Sekcja::Okladka: return "okładka";
        default: return "?";
    }
}

struct OknoPomiaru {
    static constexpr int N = int(Sekcja::Liczba);
    uint64_t suma[N]{};      // takty TSC
    uint64_t maks[N]{};
    uint32_t liczba[N]{};
    uint64_t takty = 0;      // dlugosc okna w taktach
    uint64_t ns = 0;         // dlugosc okna w ns (zegar platformy)
    uint32_t odrysowania = 0;
    // Stalopozycyjnie (Q20), bez dzielenia 128-bit (Ring 3 nie linkuje libgcc): t <= takty, wiec t * k <= ns << 20.
    uint64_t ns_z(uint64_t t) const {
        if (!takty || ns >= (1ULL << 43)) return 0;
        const uint64_t k = (ns << 20) / takty;
        return (t * k) >> 20;
    }
    uint64_t suma_ns(Sekcja s) const { return ns_z(suma[int(s)]); }
    uint64_t maks_ns(Sekcja s) const { return ns_z(maks[int(s)]); }
};

class Profiler {
public:
    static uint64_t takt() {
#if defined(__x86_64__) || defined(__i386__)
        uint32_t lo, hi;
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return (uint64_t(hi) << 32) | lo;
#else
        return 0;
#endif
    }
    bool wlaczony() const { return wlaczony_; }
    void ustaw_wlaczony(bool w, uint64_t teraz_ns) {
        if (w == wlaczony_) return;
        wlaczony_ = w;
        biezace_ = OknoPomiaru{};
        ostatnie_ = OknoPomiaru{};
        start_takt_ = takt();
        start_ns_ = teraz_ns;
    }
    void dodaj(Sekcja s, uint64_t takty) {
        const int i = int(s);
        biezace_.suma[i] += takty;
        if (takty > biezace_.maks[i]) biezace_.maks[i] = takty;
        ++biezace_.liczba[i];
    }
    void odrysowanie() { if (wlaczony_) ++biezace_.odrysowania; }
    // Zamyka okno, gdy minelo >= odstep_ns; zwraca true, gdy jest nowe "ostatnie" okno.
    bool zamknij_okno(uint64_t teraz_ns, uint64_t odstep_ns) {
        if (!wlaczony_ || teraz_ns < start_ns_ || teraz_ns - start_ns_ < odstep_ns) return false;
        const uint64_t t = takt();
        biezace_.takty = t - start_takt_;
        biezace_.ns = teraz_ns - start_ns_;
        ostatnie_ = biezace_;
        biezace_ = OknoPomiaru{};
        start_takt_ = t;
        start_ns_ = teraz_ns;
        return true;
    }
    const OknoPomiaru& ostatnie() const { return ostatnie_; }

private:
    bool wlaczony_ = false;
    OknoPomiaru biezace_{}, ostatnie_{};
    uint64_t start_takt_ = 0, start_ns_ = 0;
};

// Jeden profiler na proces (Ring 3: jeden watek, petla BursztynUI).
inline Profiler g_profiler;

class PomiarSekcji {
public:
    explicit PomiarSekcji(Sekcja s) : s_(s), t0_(g_profiler.wlaczony() ? Profiler::takt() : 0) {}
    ~PomiarSekcji() { if (t0_) g_profiler.dodaj(s_, Profiler::takt() - t0_); }
    PomiarSekcji(const PomiarSekcji&) = delete;
    PomiarSekcji& operator=(const PomiarSekcji&) = delete;
private:
    Sekcja s_;
    uint64_t t0_;
};

} // namespace Grajek
