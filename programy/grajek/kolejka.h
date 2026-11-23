#pragma once
/*
 * Grajek - kolejka odtwarzania (niezalezna od biblioteki), tryb losowy, powtarzanie i listy M3U8.
 *
 * Kolejka przechowuje indeksy utworow biblioteki. Po odswiezeniu biblioteki indeksy sa przemapowywane po sciezce
 * (utwory, ktore zniknely, sa usuwane z kolejki). Tryb losowy uzywa permutacji pozycji kolejki (Fisher-Yates,
 * xorshift64*), w ktorej biezacy utwor jest pierwszy - kazdy utwor gra dokladnie raz na obieg.
 *
 * Format listy (M3U8, UTF-8):
 *   #EXTM3U
 *   #EXTINF:<sekundy>,<wykonawca> - <tytul>
 *   /dane/muzyka/<sciezka>
 * Wczytywane sa wylacznie bezwzgledne sciezki pod /dane/muzyka/ bez segmentow "." i "..", bez znakow sterujacych
 * i bez "\\" (ochrona przed przejsciem poza katalog muzyki); wpisy spoza biblioteki sa pomijane i liczone.
 * Listy sa zapisywane w katalogu ustawien Grajka - nigdy obok plikow muzycznych.
 */
#include "biblioteka.h"

namespace Grajek {

enum class Powtarzanie : uint8_t { Brak = 0, Jeden = 1, Wszystkie = 2 };

inline const char* nazwa_powtarzania(Powtarzanie p) {
    switch (p) {
        case Powtarzanie::Brak: return "Powtarzanie: wył.";
        case Powtarzanie::Jeden: return "Powtarzanie: utwór";
        case Powtarzanie::Wszystkie: return "Powtarzanie: wszystkie";
    }
    return "?";
}

constexpr uint32_t MAKS_KOLEJKA = MAKS_UTWOROW;
constexpr uint32_t MAKS_M3U8 = 4096;          // limit pliku ustawien aplikacji (ustawienia_zapisz)

class Kolejka {
public:
    uint32_t liczba() const { return n_; }
    int biezaca() const { return biezaca_; }                       // pozycja w kolejce albo -1
    int utwor(uint32_t poz) const { return poz < n_ ? int(e_[poz]) : -1; }
    int biezacy_utwor() const { return biezaca_ >= 0 ? int(e_[biezaca_]) : -1; }

    bool losowo() const { return losowo_; }
    Powtarzanie powtarzanie() const { return powt_; }
    void ustaw_powtarzanie(Powtarzanie p) { powt_ = p; }
    void ustaw_losowo(bool w, uint64_t ziarno) {
        losowo_ = w;
        if (ziarno) los_ = ziarno;
        if (losowo_) przetasuj();
    }

    void wyczysc() { n_ = 0; biezaca_ = -1; poz_los_ = 0; }

    bool dodaj(uint16_t indeks) {
        if (n_ >= MAKS_KOLEJKA) return false;
        e_[n_++] = indeks;
        if (losowo_) { perm_[n_ - 1] = uint16_t(n_ - 1); dotasuj_ostatni(); }
        return true;
    }
    bool usun(uint32_t poz) {
        if (poz >= n_) return false;
        for (uint32_t i = poz; i + 1 < n_; ++i) e_[i] = e_[i + 1];
        --n_;
        if (biezaca_ == int(poz)) biezaca_ = -1;       // usuniety biezacy: odtwarzanie trwa, ale nie ma "nastepnego po nim"
        else if (biezaca_ > int(poz)) --biezaca_;
        if (losowo_) przetasuj();
        return true;
    }
    // Zamienia pozycje poz z poz+kierunek (kierunek -1 albo +1).
    bool przesun(uint32_t poz, int kierunek) {
        if (poz >= n_ || (kierunek != -1 && kierunek != 1)) return false;
        if ((kierunek < 0 && poz == 0) || (kierunek > 0 && poz + 1 >= n_)) return false;
        const uint32_t cel = uint32_t(int(poz) + kierunek);
        const uint16_t t = e_[poz]; e_[poz] = e_[cel]; e_[cel] = t;
        if (biezaca_ == int(poz)) biezaca_ = int(cel);
        else if (biezaca_ == int(cel)) biezaca_ = int(poz);
        if (losowo_) przetasuj();
        return true;
    }
    // Zastepuje kolejke lista indeksow (np. widok biblioteki) i ustawia biezacy.
    void ustaw(const uint16_t* indeksy, uint32_t ile, int biezacy_poz) {
        n_ = ile > MAKS_KOLEJKA ? MAKS_KOLEJKA : ile;
        for (uint32_t i = 0; i < n_; ++i) e_[i] = indeksy[i];
        biezaca_ = biezacy_poz >= 0 && uint32_t(biezacy_poz) < n_ ? biezacy_poz : -1;
        if (losowo_) przetasuj();
    }
    bool ustaw_biezaca(int poz) {
        if (poz < 0 || uint32_t(poz) >= n_) return false;
        biezaca_ = poz;
        if (losowo_) przetasuj();
        return true;
    }
    int pozycja_utworu(int indeks) const {
        for (uint32_t i = 0; i < n_; ++i) if (int(e_[i]) == indeks) return int(i);
        return -1;
    }

    /*
     * Nastepna pozycja po zakonczeniu utworu (automatycznie=true) albo po "Nastepny" (false).
     * Powtarzanie jednego utworu dotyczy tylko przejscia automatycznego. -1 = koniec listy.
     */
    int nastepna(bool automatycznie) const {
        if (!n_) return -1;
        if (automatycznie && powt_ == Powtarzanie::Jeden && biezaca_ >= 0) return biezaca_;
        if (losowo_) {
            if (biezaca_ < 0) return perm_[0];
            const uint32_t k = poz_w_permutacji() + 1;
            if (k < n_) return perm_[k];
            return powt_ == Powtarzanie::Wszystkie ? int(perm_[0]) : -1;
        }
        if (biezaca_ < 0) return 0;
        if (uint32_t(biezaca_) + 1 < n_) return biezaca_ + 1;
        return powt_ == Powtarzanie::Wszystkie ? 0 : -1;
    }
    int poprzednia() const {
        if (!n_) return -1;
        if (losowo_) {
            const uint32_t k = poz_w_permutacji();
            if (k > 0) return perm_[k - 1];
            return powt_ == Powtarzanie::Wszystkie ? int(perm_[n_ - 1]) : int(perm_[0]);
        }
        if (biezaca_ <= 0) return powt_ == Powtarzanie::Wszystkie ? int(n_ - 1) : 0;
        return biezaca_ - 1;
    }
    // Przejscie do pozycji wybranej przez nastepna()/poprzednia() (zachowuje permutacje).
    void przejdz(int poz) {
        if (poz < 0 || uint32_t(poz) >= n_) return;
        biezaca_ = poz;
    }

    // Po odswiezeniu biblioteki: indeksy wg sciezek ze starej biblioteki -> nowa. Zwraca liczbe usunietych.
    uint32_t przemapuj(const char (*stare_sciezki)[MAKS_SCIEZKA], const Biblioteka& b) {
        uint32_t w = 0, usuniete = 0;
        int nowa_biezaca = -1;
        for (uint32_t i = 0; i < n_; ++i) {
            const int idx = b.znajdz(stare_sciezki[i]);
            if (idx < 0) { ++usuniete; continue; }
            if (int(i) == biezaca_) nowa_biezaca = int(w);
            e_[w++] = uint16_t(idx);
        }
        n_ = w;
        biezaca_ = nowa_biezaca;
        if (losowo_) przetasuj();
        return usuniete;
    }

    // ------------------------------------------------------------ M3U8
    // Zapisuje kolejke; zwraca liczbe zapisanych wpisow (moze byc mniejsza, gdy brak miejsca - patrz MAKS_M3U8).
    uint32_t zapisz_m3u8(const Biblioteka& b, char* out, size_t cap) const {
        Tekst t(out, cap);
        t.dodaj("#EXTM3U").nowa_linia();
        uint32_t ile = 0;
        for (uint32_t i = 0; i < n_; ++i) {
            const Utwor* u = b.utwor(e_[i]);
            if (!u) continue;
            char wpis[MAKS_SCIEZKA + 2 * MAKS_POLE_TEKSTU + 32];
            Tekst w(wpis, sizeof(wpis));
            w.dodaj("#EXTINF:"); w.liczba(u->czas_ms / 1000); w.dodaj(",");
            if (u->meta.wykonawca[0]) { w.dodaj(u->meta.wykonawca); w.dodaj(" - "); }
            w.dodaj(u->tytul()); w.nowa_linia(); w.dodaj(u->sciezka); w.nowa_linia();
            if (w.obciety() || t.dlugosc() + w.dlugosc() + 1 >= cap) break;
            t.dolacz(w);
            ++ile;
        }
        return ile;
    }

    static bool sciezka_dozwolona(const char* s, size_t n) {
        static constexpr char P[] = "/dane/muzyka/";
        const size_t pn = sizeof(P) - 1;
        if (n <= pn || n >= MAKS_SCIEZKA) return false;
        for (size_t i = 0; i < pn; ++i) if (s[i] != P[i]) return false;
        size_t seg = pn;
        for (size_t i = pn; i <= n; ++i) {
            if (i == n || s[i] == '/') {
                const size_t dl = i - seg;
                if (dl == 0) return false;                                       // "//"
                if (dl == 1 && s[seg] == '.') return false;
                if (dl == 2 && s[seg] == '.' && s[seg + 1] == '.') return false;
                seg = i + 1;
                continue;
            }
            const uint8_t c = uint8_t(s[i]);
            if (c < 0x20 || c == 0x7F || c == '\\') return false;
        }
        return sekwencje_poprawne(s, n);
    }

    struct WynikWczytania { uint32_t dodane = 0, pominiete = 0, odrzucone = 0; };

    // Wczytuje liste (zastepuje kolejke). Wpisy spoza biblioteki -> pominiete, niebezpieczne sciezki -> odrzucone.
    WynikWczytania wczytaj_m3u8(const Biblioteka& b, const char* tekst, size_t dl) {
        WynikWczytania w{};
        uint16_t nowe[MAKS_KOLEJKA];
        uint32_t ile = 0;
        size_t pos = 0;
        if (dl > MAKS_M3U8) dl = MAKS_M3U8;
        // BOM UTF-8
        if (dl >= 3 && uint8_t(tekst[0]) == 0xEF && uint8_t(tekst[1]) == 0xBB && uint8_t(tekst[2]) == 0xBF) pos = 3;
        while (pos < dl && tekst[pos]) {
            size_t end = pos;
            while (end < dl && tekst[end] && tekst[end] != '\n') ++end;
            size_t n = end - pos;
            if (n && tekst[pos + n - 1] == '\r') --n;
            const char* l = tekst + pos;
            if (n && l[0] != '#') {
                char sc[MAKS_SCIEZKA];
                if (!sciezka_dozwolona(l, n)) ++w.odrzucone;
                else {
                    for (size_t i = 0; i < n; ++i) sc[i] = l[i];
                    sc[n] = '\0';
                    const int idx = b.znajdz(sc);
                    if (idx < 0) ++w.pominiete;
                    else if (ile < MAKS_KOLEJKA) { nowe[ile++] = uint16_t(idx); ++w.dodane; }
                }
            }
            pos = end + 1;
        }
        ustaw(nowe, ile, -1);
        return w;
    }

private:
    static bool sekwencje_poprawne(const char* s, size_t n) {
        const auto* p = reinterpret_cast<const uint8_t*>(s);
        for (size_t i = 0; i < n;) { const size_t k = sekwencja_utf8(p + i, n - i); if (!k) return false; i += k; }
        return true;
    }
    uint64_t losuj() {
        los_ ^= los_ >> 12; los_ ^= los_ << 25; los_ ^= los_ >> 27;
        return los_ * 0x2545F4914F6CDD1DULL;
    }
    void przetasuj() {
        for (uint32_t i = 0; i < n_; ++i) perm_[i] = uint16_t(i);
        for (uint32_t i = n_; i > 1; --i) {
            const uint32_t j = uint32_t(losuj() % i);
            const uint16_t t = perm_[i - 1]; perm_[i - 1] = perm_[j]; perm_[j] = t;
        }
        // biezacy na poczatek obiegu
        if (biezaca_ >= 0) {
            for (uint32_t i = 0; i < n_; ++i) if (perm_[i] == uint16_t(biezaca_)) { perm_[i] = perm_[0]; perm_[0] = uint16_t(biezaca_); break; }
        }
    }
    void dotasuj_ostatni() {
        // nowy element w losowe miejsce po biezacym (nie zaburza juz zagranych)
        if (n_ < 2) return;
        const uint32_t start = poz_w_permutacji() + 1;
        if (start >= n_) return;
        const uint32_t j = start + uint32_t(losuj() % (n_ - start));
        const uint16_t t = perm_[n_ - 1]; perm_[n_ - 1] = perm_[j]; perm_[j] = t;
    }
    uint32_t poz_w_permutacji() const {
        if (biezaca_ < 0) return 0;
        for (uint32_t i = 0; i < n_; ++i) if (perm_[i] == uint16_t(biezaca_)) return i;
        return 0;
    }

    uint16_t e_[MAKS_KOLEJKA]{};
    uint16_t perm_[MAKS_KOLEJKA]{};
    uint32_t n_ = 0;
    int biezaca_ = -1;
    uint32_t poz_los_ = 0;
    bool losowo_ = false;
    Powtarzanie powt_ = Powtarzanie::Brak;
    uint64_t los_ = 0x9E3779B97F4A7C15ULL;
};

} // namespace Grajek
