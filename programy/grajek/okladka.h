#pragma once
/*
 * Grajek - okladka albumu (ID3 APIC/PIC) dekodowana RAZ przy ladowaniu utworu i przechowywana jako mala,
 * przeskalowana bitmapa (<= MAKS_BOK x MAKS_BOK). Nigdy nie jest dekodowana w kazdej klatce.
 *
 * Kolejnosc ochron: rozmiar danych <= MAKS_OKLADKA_BAJTY (2 MiB), sygnatura JPEG/PNG zgodna z danymi,
 * wymiary z naglowka (sprawdz_jpeg/sprawdz_png) <= MAKS_WYMIAR x MAKS_WYMIAR i <= MAKS_PIKSELI PRZED dekodowaniem,
 * dekoder obrazow Bursztyn OS (obrazy/dekoder_obrazu, stb_image) z alokatorem Grajka (limit pojedynczej alokacji),
 * bufor zrodlowy zwalniany natychmiast po skalowaniu.
 */
#include "metadane.h"
#include "zrodlo.h"
#include "id3.h"
#include "pamiec.h"
#include "obrazy/dekoder_obrazu.h"

namespace Grajek {

class Okladka {
public:
    static constexpr uint32_t MAKS_BOK = 220;
    static constexpr uint32_t MAKS_WYMIAR = 3000;
    static constexpr uint32_t MAKS_PIKSELI = 4U * 1024U * 1024U;

    explicit Okladka(uint32_t maks_bok = MAKS_BOK) : maks_bok_(maks_bok ? maks_bok : MAKS_BOK) {}
    ~Okladka() { wyczysc(); }
    Okladka(const Okladka&) = delete;
    Okladka& operator=(const Okladka&) = delete;
    void wyczysc() { delete[] px_; px_ = nullptr; w_ = h_ = 0; }
    bool jest() const { return px_ && w_ && h_; }
    uint32_t szerokosc() const { return w_; }
    uint32_t wysokosc() const { return h_; }
    const uint32_t* piksele() const { return px_; }
    const char* blad() const { return blad_; }

    // Wczytuje okladke opisana w metadanych. false = brak lub odrzucona (blad() opisuje powod; brak nie jest bledem).
    bool wczytaj(const DostepDoPlikow& dostep, const char* sciezka, const Metadane& m) {
        wyczysc();
        blad_[0] = '\0';
        if (m.okladka == TypOkladki::Brak) return false;
        Czytnik c;
        if (!c.otworz(dostep, sciezka, 64U * 1024U)) return odrzuc("Nie można odczytać okładki.");
        uint8_t* dane = nullptr;
        uint32_t n = 0;
        uint8_t* bufor = nullptr;
        if (!m.okladka_niesynchronizowana) {
            if (!m.okladka_rozmiar || m.okladka_rozmiar > MAKS_OKLADKA_BAJTY) return odrzuc("Okładka jest zbyt duża.");
            bufor = static_cast<uint8_t*>(alokuj(m.okladka_rozmiar));
            if (!bufor) return odrzuc("Brak pamięci dla okładki.");
            if (!c.czytaj_w(m.okladka_offset, bufor, m.okladka_rozmiar)) { zwolnij(bufor); return odrzuc("Nie można odczytać okładki."); }
            dane = bufor; n = m.okladka_rozmiar;
        } else {
            if (!m.okladka_rozmiar_ramki || m.okladka_rozmiar_ramki > MAKS_OKLADKA_BAJTY + 4096) return odrzuc("Okładka jest zbyt duża.");
            bufor = static_cast<uint8_t*>(alokuj(m.okladka_rozmiar_ramki));
            if (!bufor) return odrzuc("Brak pamięci dla okładki.");
            if (!c.czytaj_w(m.okladka_offset_ramki, bufor, m.okladka_rozmiar_ramki)) { zwolnij(bufor); return odrzuc("Nie można odczytać okładki."); }
            const size_t dl = Id3::usun_niesynchronizacje(bufor, m.okladka_rozmiar_ramki);
            if (m.okladka_offset >= dl) { zwolnij(bufor); return odrzuc("Uszkodzona okładka."); }
            dane = bufor + m.okladka_offset; n = uint32_t(dl - m.okladka_offset);
        }
        const bool ok = dekoduj(dane, n);
        zwolnij(bufor);
        return ok;
    }

    // Dekodowanie i skalowanie z pamieci (takze testy).
    bool dekoduj(const uint8_t* dane, uint32_t n) {
        wyczysc();
        if (!dane || n < 8) return odrzuc("Uszkodzona okładka.");
        const FormatObrazu f = rozpoznaj_obraz(dane, n);
        uint32_t w = 0, h = 0;
        BladObrazu b = BladObrazu::Format;
        if (f == FormatObrazu::JPEG) b = sprawdz_jpeg(dane, n, &w, &h);
        else if (f == FormatObrazu::PNG) b = sprawdz_png(dane, n, &w, &h);
        else return odrzuc("Nieobsługiwany format okładki (tylko JPEG i PNG).");
        if (b != BladObrazu::OK) return odrzuc("Uszkodzona okładka.");
        if (!w || !h || w > MAKS_WYMIAR || h > MAKS_WYMIAR || uint64_t(w) * h > MAKS_PIKSELI) return odrzuc("Okładka ma zbyt duże wymiary.");
        DekodowanyObraz o{};
        b = f == FormatObrazu::JPEG ? dekoduj_jpeg(dane, n, &o, &Okladka::al, &Okladka::zw)
                                    : dekoduj_png(dane, n, &o, &Okladka::al, &Okladka::zw);
        if (b != BladObrazu::OK || !o.piksele || o.szerokosc != w || o.wysokosc != h || o.stride < w * 4) {
            if (o.piksele) zwolnij(o.piksele);
            return odrzuc("Nie można zdekodować okładki.");
        }
        skaluj(reinterpret_cast<const uint32_t*>(o.piksele), w, h, o.stride / 4);
        zwolnij(o.piksele);
        return jest();
    }

private:
    static void* al(size_t n) { return alokuj(n); }
    static void zw(void* p) { zwolnij(p); }
    bool odrzuc(const char* t) { kopiuj_tekst(blad_, sizeof(blad_), t); return false; }
    // Usrednianie obszarowe (calkowite) do maks_bok_ z zachowaniem proporcji; alfa = 0xFF (0 jest przezroczyste w UI).
    void skaluj(const uint32_t* zr, uint32_t w, uint32_t h, uint32_t stride) {
        uint32_t nw = w, nh = h;
        const uint32_t MB = maks_bok_;
        if (w > MB || h > MB) {
            if (w >= h) { nw = MB; nh = uint32_t(uint64_t(h) * MB / w); }
            else { nh = MB; nw = uint32_t(uint64_t(w) * MB / h); }
            if (!nw) nw = 1;
            if (!nh) nh = 1;
        }
        px_ = new uint32_t[size_t(nw) * nh];
        if (!px_) return;
        w_ = nw; h_ = nh;
        for (uint32_t y = 0; y < nh; ++y) {
            const uint32_t y0 = uint32_t(uint64_t(y) * h / nh), y1x = uint32_t(uint64_t(y + 1) * h / nh);
            const uint32_t y1 = y1x > y0 ? y1x : y0 + 1;
            for (uint32_t x = 0; x < nw; ++x) {
                const uint32_t x0 = uint32_t(uint64_t(x) * w / nw), x1x = uint32_t(uint64_t(x + 1) * w / nw);
                const uint32_t x1 = x1x > x0 ? x1x : x0 + 1;
                uint64_t r = 0, g = 0, b = 0, k = 0;
                for (uint32_t yy = y0; yy < y1 && yy < h; ++yy)
                    for (uint32_t xx = x0; xx < x1 && xx < w; ++xx) {
                        const uint32_t p = zr[size_t(yy) * stride + xx];
                        r += (p >> 16) & 0xFF; g += (p >> 8) & 0xFF; b += p & 0xFF; ++k;
                    }
                if (!k) k = 1;
                px_[size_t(y) * nw + x] = 0xFF000000u | uint32_t((r / k) << 16) | uint32_t((g / k) << 8) | uint32_t(b / k);
            }
        }
    }
    uint32_t maks_bok_;
    uint32_t* px_ = nullptr;
    uint32_t w_ = 0, h_ = 0;
    char blad_[64]{};
};

/*
 * Domyslna okladka Grajka: grafika uzytkownika ikonki/plyta_winylowa.jpg, osadzona bez zmian w obrazie systemu jako
 * /ikonki/grajek_plyta_winylowa.jpg (live i instalacja; sciezki hosta nie sa dostepne w gosciu). Uzywana, gdy utwor
 * nie ma okladki w tagach (okladka z tagu ma pierwszenstwo). Plik jest czytany i dekodowany RAZ na proces (takze
 * porazka jest zapamietana); te same limity i ten sam dekoder co okladki z tagow (rozmiar <= 256 KiB, wymiary z
 * naglowka przed dekodowaniem). Wynik: bitmapa <= BOK_POSREDNI (zapas na 200 % skali okladki 176 DIP), oryginal
 * zwalniany od razu; do rozmiaru kontrolki skaluje ObrazOkladki (raz na zmiane rozmiaru).
 */
class GrafikaPlyty {
public:
    static constexpr const char* SCIEZKA = "/ikonki/grajek_plyta_winylowa.jpg";
    static constexpr uint32_t MAKS_BAJTY = 256U * 1024U;
    static constexpr uint32_t BOK_POSREDNI = 352;
    GrafikaPlyty() : obraz_(BOK_POSREDNI) {}
    bool wczytaj(const DostepDoPlikow& dostep, const char* sciezka = SCIEZKA) {
        if (proba_) return obraz_.jest();
        proba_ = true;
        uint64_t rozmiar = 0;
        if (!dostep.rozmiar || !dostep.rozmiar(dostep.kontekst, sciezka, &rozmiar) || !rozmiar) return odrzuc("Brak grafiki płyty.");
        if (rozmiar > MAKS_BAJTY) return odrzuc("Grafika płyty jest zbyt duża.");
        Czytnik c;
        if (!c.otworz(dostep, sciezka, 64U * 1024U)) return odrzuc("Nie można odczytać grafiki płyty.");
        auto* bufor = static_cast<uint8_t*>(alokuj(size_t(rozmiar)));
        if (!bufor) return odrzuc("Brak pamięci dla grafiki płyty.");
        const bool ok = c.czytaj_w(0, bufor, uint32_t(rozmiar)) && obraz_.dekoduj(bufor, uint32_t(rozmiar));
        zwolnij(bufor);
        ++dekodowania_;
        if (!ok) return odrzuc(obraz_.blad()[0] ? obraz_.blad() : "Nie można zdekodować grafiki płyty.");
        return true;
    }
    bool dekoduj(const uint8_t* dane, uint32_t n) {   // testy
        if (proba_) return obraz_.jest();
        proba_ = true; ++dekodowania_;
        return obraz_.dekoduj(dane, n) || odrzuc(obraz_.blad());
    }
    bool jest() const { return obraz_.jest(); }
    bool proba() const { return proba_; }
    uint32_t dekodowania() const { return dekodowania_; }
    const uint32_t* piksele() const { return obraz_.piksele(); }
    uint32_t szerokosc() const { return obraz_.szerokosc(); }
    uint32_t wysokosc() const { return obraz_.wysokosc(); }
    const char* blad() const { return blad_; }
private:
    bool odrzuc(const char* t) { kopiuj_tekst(blad_, sizeof(blad_), t); return false; }
    Okladka obraz_;
    bool proba_ = false;
    uint32_t dekodowania_ = 0;
    char blad_[64]{};
};

} // namespace Grajek
