#pragma once
/*
 * Grajek - zrodlo bajtow pliku z buforem okienkowym.
 *
 * PSF udostepnia w Ring 3 odczyt "sciezka + przesuniecie" (BWS czytaj_fragment_pliku, maks. 1 MiB na wywolanie),
 * bez uchwytow plikow. Czytnik trzyma sciezke, rozmiar i jedno okno bufora (domyslnie 64 KiB), wiec dekodery
 * czytaja plik strumieniowo, a liczba wywolan systemowych jest ograniczona. Caly plik NIGDY nie jest wczytywany
 * do pamieci. Wszystkie przesuniecia sa sprawdzane wzgledem rozmiaru (arytmetyka 64-bitowa bez przepelnien).
 *
 * Funkcja odczytu jest wstrzykiwana (Platforma), dzieki czemu te same dekodery dzialaja w testach hosta.
 */
#include "tekst.h"

namespace Grajek {

// Odczyt fragmentu: true + *odczytano (0 = koniec pliku) albo false (blad I/O / brak pliku).
using FunkcjaOdczytu = bool (*)(void* kontekst, const char* sciezka, uint64_t offset, void* bufor, uint32_t n,
                               uint32_t* odczytano);
// Rozmiar pliku: true + *rozmiar albo false (brak pliku / katalog).
using FunkcjaRozmiaru = bool (*)(void* kontekst, const char* sciezka, uint64_t* rozmiar);

struct DostepDoPlikow {
    FunkcjaOdczytu odczyt = nullptr;
    FunkcjaRozmiaru rozmiar = nullptr;
    void* kontekst = nullptr;
};

constexpr size_t MAKS_SCIEZKA = 192;          // pelna sciezka w PSF (katalogi <= 3 poziomy, nazwy <= 55 B)
constexpr uint32_t ROZMIAR_OKNA_CZYTNIKA = 64U * 1024U;

class Czytnik {
public:
    Czytnik() = default;
    ~Czytnik() { zwolnij(); }
    Czytnik(const Czytnik&) = delete;
    Czytnik& operator=(const Czytnik&) = delete;

    bool otworz(const DostepDoPlikow& dostep, const char* sciezka, uint32_t okno = ROZMIAR_OKNA_CZYTNIKA) {
        zamknij();
        if (!dostep.odczyt || !dostep.rozmiar || !sciezka || !sciezka[0]) return false;
        if (dlugosc(sciezka, MAKS_SCIEZKA) >= MAKS_SCIEZKA) return false;
        kopiuj_tekst(sciezka_, sizeof(sciezka_), sciezka);
        dostep_ = dostep;
        if (!dostep_.rozmiar(dostep_.kontekst, sciezka_, &rozmiar_)) return false;
        if (okno < 512) okno = 512;
        if (okno > 1024U * 1024U) okno = 1024U * 1024U;
        if (!bufor_ || pojemnosc_ != okno) {
            zwolnij();
            bufor_ = new uint8_t[okno];
            if (!bufor_) return false;
            pojemnosc_ = okno;
        }
        otwarty_ = true;
        blad_ = false;
        return true;
    }
    void zamknij() { otwarty_ = false; pozycja_ = 0; rozmiar_ = 0; okno_start_ = 0; okno_dl_ = 0; blad_ = false; sciezka_[0] = '\0'; }

    bool otwarty() const { return otwarty_; }
    bool blad() const { return blad_; }
    uint64_t rozmiar() const { return rozmiar_; }
    uint64_t pozycja() const { return pozycja_; }
    const char* sciezka() const { return sciezka_; }
    bool koniec() const { return pozycja_ >= rozmiar_; }

    bool ustaw_pozycje(uint64_t p) {
        if (!otwarty_ || p > rozmiar_) return false;
        pozycja_ = p;
        return true;
    }
    bool przesun(uint64_t o) {
        if (!otwarty_ || o > rozmiar_ - pozycja_) { pozycja_ = rozmiar_; return false; }
        pozycja_ += o;
        return true;
    }

    // Czyta do n bajtow od biezacej pozycji; zwraca liczbe przeczytanych (mniej tylko na koncu pliku lub po bledzie).
    uint32_t czytaj(void* cel, uint32_t n) {
        if (!otwarty_ || !cel || !n) return 0;
        auto* d = static_cast<uint8_t*>(cel);
        uint32_t zrobione = 0;
        while (zrobione < n && pozycja_ < rozmiar_) {
            if (!w_oknie(pozycja_) && !wczytaj_okno(pozycja_)) break;
            const uint64_t w = pozycja_ - okno_start_;
            uint64_t dost = okno_dl_ - w;
            if (dost > n - zrobione) dost = n - zrobione;
            kopiuj_bajty(d + zrobione, bufor_ + w, size_t(dost));
            zrobione += uint32_t(dost);
            pozycja_ += dost;
        }
        return zrobione;
    }
    // Odczyt dokladnie n bajtow spod przesuniecia (false, gdy plik jest krotszy albo wystapil blad).
    bool czytaj_w(uint64_t offset, void* cel, uint32_t n) {
        if (!otwarty_ || offset > rozmiar_ || n > rozmiar_ - offset) return false;
        const uint64_t stara = pozycja_;
        pozycja_ = offset;
        const uint32_t got = czytaj(cel, n);
        pozycja_ = stara;
        return got == n;
    }

private:
    bool w_oknie(uint64_t p) const { return okno_dl_ && p >= okno_start_ && p - okno_start_ < okno_dl_; }
    bool wczytaj_okno(uint64_t p) {
        uint64_t n = rozmiar_ - p;
        if (n > pojemnosc_) n = pojemnosc_;
        uint32_t got = 0;
        if (!dostep_.odczyt(dostep_.kontekst, sciezka_, p, bufor_, uint32_t(n), &got) || got == 0 || got > n) {
            blad_ = true;
            okno_dl_ = 0;
            return false;
        }
        okno_start_ = p;
        okno_dl_ = got;
        return true;
    }
    void zwolnij() { delete[] bufor_; bufor_ = nullptr; pojemnosc_ = 0; okno_dl_ = 0; }

    DostepDoPlikow dostep_{};
    char sciezka_[MAKS_SCIEZKA]{};
    uint8_t* bufor_ = nullptr;
    uint32_t pojemnosc_ = 0;
    uint64_t rozmiar_ = 0, pozycja_ = 0, okno_start_ = 0, okno_dl_ = 0;
    bool otwarty_ = false, blad_ = false;
};

inline uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
inline uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); }
inline uint32_t be24(const uint8_t* p) { return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[2]); }

} // namespace Grajek
