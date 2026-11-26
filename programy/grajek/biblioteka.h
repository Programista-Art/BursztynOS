#pragma once
/*
 * Grajek - biblioteka muzyki: skanowanie katalogu /dane/muzyka (PSF, BWS 6), walidacja plikow, metadane, sortowanie
 * i filtrowanie.
 *
 *  - Skan jest PRZYROSTOWY (krok() przetwarza ograniczona liczbe plikow), wiec nie blokuje interfejsu ani produkcji
 *    PCM i nie jest powtarzany w kazdej klatce - tylko przy starcie i po "Odswiez biblioteke".
 *  - Rozszerzenia .wav/.mp3 bez rozrozniania wielkosci liter wybieraja kandydatow, ale o przyjeciu decyduje
 *    zawartosc (RIFF/WAVE z obslugiwanym formatem albo dwie spojne ramki MPEG Layer III).
 *  - Podkatalogi (opcjonalnie): glebokosc <= MAKS_GLEBOKOSC, lacznie <= MAKS_KATALOGOW katalogow, nazwy "." i ".."
 *    oraz zbyt dlugie sciezki sa pomijane. PSF nie ma dowiazan, a limity i tak wykluczaja nieskonczona rekurencje.
 *  - Pliki muzyczne sa tylko czytane: biblioteka nigdy ich nie modyfikuje, nie przenosi ani nie usuwa.
 *  - Kolekcja ma staly limit (MAKS_UTWOROW), przydzielana dynamicznie raz na skan.
 *  - Pliki otwarte przez system (argument startowy, np. dwuklik w Eksploratorze; takze spoza /dane/muzyka):
 *    analizowane ta sama droga co w skanie (analizuj_plik), dopisywane ZA utworami skanu (dodaj_zewnetrzny) -
 *    maja zwykle indeksy (kolejka, odtwarzanie, okladka), ale nie sa widoczne w widoku biblioteki i przetrwaja
 *    kolejne publikacje skanu. Plik znaleziony tez przez skan nie jest dublowany. Nic nie jest kopiowane.
 */
#include "dekoder.h"

namespace Grajek {

constexpr const char* KATALOG_MUZYKI = "/dane/muzyka";
constexpr uint32_t MAKS_UTWOROW = 512;
constexpr uint32_t MAKS_KANDYDATOW = 1024;
constexpr uint32_t MAKS_KATALOGOW = 32;
constexpr uint32_t MAKS_GLEBOKOSC = 3;
constexpr uint32_t MAKS_LISTING = 64U * 1024U;     // limit BWS 6 (MAX_LISTA_KATALOGU_BWS)
constexpr uint32_t MAKS_ZEWNETRZNYCH = 32;          // pliki otwarte spoza skanu w jednej sesji
constexpr uint32_t POJEMNOSC_UTWOROW = MAKS_UTWOROW + MAKS_ZEWNETRZNYCH;

// Listing katalogu (BWS 6): "[KAT]  nazwa\n" / "[PLIK] nazwa\n" w buforze; true = katalog istnieje.
using FunkcjaListowania = bool (*)(void* kontekst, const char* sciezka, char* bufor, uint32_t pojemnosc);

struct Utwor {
    char sciezka[MAKS_SCIEZKA]{};
    char nazwa_pliku[64]{};
    Metadane meta{};
    FormatAudio format = FormatAudio::Nieznany;
    uint32_t czas_ms = 0;
    uint32_t czestotliwosc = 0;
    uint16_t kanaly = 0;
    uint16_t bity = 0;              // WAV: bity probki; MP3: 0
    uint32_t przeplywnosc = 0;      // kbit/s
    bool vbr = false;

    // Tytul do wyswietlenia: tag albo nazwa pliku bez rozszerzenia.
    const char* tytul() const { return meta.tytul[0] ? meta.tytul : nazwa_bez_rozszerzenia_; }
    char nazwa_bez_rozszerzenia_[64]{};
};

enum class StanSkanu : uint8_t { Bezczynny, Listowanie, Analiza, Gotowy };

struct WynikSkanu {
    bool katalog_istnieje = false;
    uint32_t znalezione = 0;        // pliki .wav/.mp3
    uint32_t przyjete = 0;
    uint32_t odrzucone = 0;         // niepoprawna zawartosc / nieobslugiwany format
    uint32_t pominiete_limit = 0;   // ponad MAKS_UTWOROW / MAKS_KANDYDATOW / MAKS_KATALOGOW / dlugie sciezki
    char ostatni_blad[96]{};
};

// Opis formatu do tabeli/panelu: "MP3 · 192 kbit/s · 44,1 kHz · stereo".
inline void opis_formatu(const Utwor& u, char* out, size_t cap) {
    Tekst t(out, cap);
    t.dodaj(nazwa_formatu(u.format));
    if (u.format == FormatAudio::WAV && u.bity) { t.dodaj(" · "); t.liczba(u.bity); t.dodaj(" bit"); }
    if (u.przeplywnosc) { t.dodaj(" · "); t.liczba(u.przeplywnosc); t.dodaj(u.vbr ? " kbit/s (VBR)" : " kbit/s"); }
    if (u.czestotliwosc) {
        t.dodaj(" · "); t.liczba(u.czestotliwosc / 1000);
        if (u.czestotliwosc % 1000) { t.dodaj(","); t.liczba((u.czestotliwosc % 1000) / 100); }
        t.dodaj(" kHz");
    }
    if (u.kanaly) t.dodaj(u.kanaly == 1 ? " · mono" : " · stereo");
}

class Biblioteka {
public:
    Biblioteka() = default;
    ~Biblioteka() { delete[] utwory_; delete[] nowe_; delete[] kandydaci_; delete[] bufor_listingu_; delete[] bufor_id3_; delete[] widoczne_; delete[] porzadek_; delete[] zewn_; }
    Biblioteka(const Biblioteka&) = delete;
    Biblioteka& operator=(const Biblioteka&) = delete;

    void konfiguruj(const DostepDoPlikow& d, FunkcjaListowania l, void* kontekst_listowania) {
        dostep_ = d; listuj_ = l; kontekst_listowania_ = kontekst_listowania;
    }
    void ustaw_podkatalogi(bool w) { podkatalogi_ = w; }
    bool podkatalogi() const { return podkatalogi_; }

    // Rozpoczyna nowy skan (poprzednia lista pozostaje widoczna az do zakonczenia - zamiana atomowa).
    bool rozpocznij(const char* katalog = KATALOG_MUZYKI) {
        if (!listuj_) return false;
        if (!kandydaci_) kandydaci_ = new Kandydat[MAKS_KANDYDATOW];
        if (!bufor_listingu_) bufor_listingu_ = new char[MAKS_LISTING];
        if (!bufor_id3_) bufor_id3_ = new uint8_t[Id3::MAKS_OKNO_TAGU];
        if (!kandydaci_ || !bufor_listingu_ || !bufor_id3_) return false;
        delete[] nowe_; nowe_ = new Utwor[POJEMNOSC_UTWOROW];   // miejsce tez na pliki zewnetrzne
        if (!nowe_) return false;
        nowe_n_ = 0; kand_n_ = 0; kand_poz_ = 0;
        wynik_nowy_ = WynikSkanu{};
        kat_n_ = 0; kat_poz_ = 0;
        dodaj_katalog(katalog, 0);
        stan_ = StanSkanu::Listowanie;
        return true;
    }
    StanSkanu stan() const { return stan_; }
    bool skanuje() const { return stan_ == StanSkanu::Listowanie || stan_ == StanSkanu::Analiza; }
    // Postep analizy: (przeanalizowane, kandydaci).
    uint32_t postep() const { return kand_poz_; }
    uint32_t kandydaci() const { return kand_n_; }

    /*
     * Jeden krok skanu: listuje jeden katalog albo analizuje do `pliki` kandydatow. Zwraca true, gdy skan
     * zakonczyl sie W TYM wywolaniu (nowa lista opublikowana).
     */
    bool krok(uint32_t pliki = 1) {
        if (stan_ == StanSkanu::Listowanie) {
            if (kat_poz_ < kat_n_) { listuj_katalog(kat_poz_++); return false; }
            stan_ = StanSkanu::Analiza;
            return false;
        }
        if (stan_ != StanSkanu::Analiza) return false;
        for (uint32_t i = 0; i < pliki && kand_poz_ < kand_n_; ++i) analizuj(kandydaci_[kand_poz_++]);
        if (kand_poz_ < kand_n_) return false;
        opublikuj();
        return true;
    }
    // Skan synchroniczny (testy, narzedzia).
    void skanuj_calosc(const char* katalog = KATALOG_MUZYKI) {
        if (!rozpocznij(katalog)) return;
        while (!krok(16)) {}
    }

    // ------------------------------------------------------------ wyniki
    // Indeksy utworow sa stabilne do nastepnej publikacji skanu; kolejnosc wyswietlania daje posortowany(k).
    // liczba(): wszystkie indeksy (skan + pliki zewnetrzne); liczba_skanu(): tylko utwory z /dane/muzyka.
    uint32_t liczba() const { return n_; }
    uint32_t liczba_skanu() const { return n_skan_; }
    bool zewnetrzny(uint32_t i) const { return i >= n_skan_ && i < n_; }
    int posortowany(uint32_t k) const { return k < n_skan_ ? int(porzadek_ ? porzadek_[k] : k) : -1; }
    const Utwor* utwor(uint32_t i) const { return i < n_ ? &utwory_[i] : nullptr; }
    const WynikSkanu& wynik() const { return wynik_; }
    uint64_t generacja() const { return generacja_; }   // rosnie przy kazdej publikacji
    int znajdz(const char* sciezka) const {
        for (uint32_t i = 0; i < n_; ++i) if (rowne(utwory_[i].sciezka, sciezka)) return int(i);
        return -1;
    }

    /*
     * Analiza JEDNEGO pliku (np. argument startowy): ta sama walidacja co w skanie - o formacie decyduje zawartosc
     * (RIFF/WAVE albo spojne ramki MPEG Layer III), nie rozszerzenie; odczyt ograniczony (naglowek + okno tagu).
     * true = `u` wypelniony; false = *powod (polski opis).
     */
    bool analizuj_plik(const char* sciezka, Utwor& u, const char** powod) {
        const char* p = nullptr;
        uint64_t r = 0;
        if (!sciezka || sciezka[0] != '/' || dlugosc(sciezka, MAKS_SCIEZKA) >= MAKS_SCIEZKA) p = "nieprawidłowa ścieżka pliku.";
        else if (!dostep_.rozmiar || !dostep_.rozmiar(dostep_.kontekst, sciezka, &r))
            p = "plik nie istnieje, jest katalogiem albo brak uprawnień do odczytu.";
        else if (r == 0) p = "plik jest pusty.";
        else if (!bufor_id3_ && !(bufor_id3_ = new uint8_t[Id3::MAKS_OKNO_TAGU])) p = "brak pamięci do analizy pliku.";
        else {
            const char* nazwa = sciezka;
            for (const char* c = sciezka; *c; ++c) if (*c == '/') nazwa = c + 1;
            if (!*nazwa) p = "nieprawidłowa ścieżka pliku.";
            else p = analizuj_do(sciezka, nazwa, u);
        }
        if (powod) *powod = p;
        return p == nullptr;
    }
    // Dopisuje plik zewnetrzny (wynik analizuj_plik); zwraca indeks (istniejacy, gdy sciezka juz jest) albo -1.
    int dodaj_zewnetrzny(const Utwor& u) {
        const int jest = znajdz(u.sciezka);
        if (jest >= 0) return jest;
        if (!zewn_) zewn_ = new Utwor[MAKS_ZEWNETRZNYCH];
        if (!utwory_) { utwory_ = new Utwor[POJEMNOSC_UTWOROW]; n_ = n_skan_ = 0; }
        if (!zewn_ || !utwory_ || zewn_n_ >= MAKS_ZEWNETRZNYCH || n_ >= POJEMNOSC_UTWOROW) return -1;
        zewn_[zewn_n_++] = u;
        utwory_[n_] = u;
        return int(n_++);
    }

    // Filtr widoku (tytul, wykonawca, album, nazwa pliku). Zwraca liczbe widocznych.
    uint32_t filtruj(const char* wzorzec) {
        kopiuj_tekst(filtr_, sizeof(filtr_), wzorzec ? wzorzec : "");
        przelicz_widoczne();
        return widoczne_n_;
    }
    const char* filtr() const { return filtr_; }
    uint32_t widoczne() const { return widoczne_n_; }
    // indeks w bibliotece dla wiersza widoku albo -1
    int widoczny(uint32_t wiersz) const { return wiersz < widoczne_n_ ? int(widoczne_[wiersz]) : -1; }
    int wiersz_utworu(int indeks) const {
        for (uint32_t w = 0; w < widoczne_n_; ++w) if (int(widoczne_[w]) == indeks) return int(w);
        return -1;
    }

    static bool pasuje(const Utwor& u, const char* w) {
        return zawiera(u.tytul(), w) || zawiera(u.meta.wykonawca, w) || zawiera(u.meta.album, w) || zawiera(u.nazwa_pliku, w);
    }
    // Porzadek: tytul (naturalnie, po polsku), potem wykonawca, potem sciezka (stabilnie i deterministycznie).
    static int porownaj(const Utwor& a, const Utwor& b) {
        int r = porownaj_naturalnie(a.tytul(), b.tytul());
        if (r) return r;
        r = porownaj_naturalnie(a.meta.wykonawca, b.meta.wykonawca);
        if (r) return r;
        return porownaj_naturalnie(a.sciezka, b.sciezka);
    }

private:
    struct Kandydat { char sciezka[MAKS_SCIEZKA]; char nazwa[64]; FormatAudio wg_rozszerzenia; };
    struct Katalog { char sciezka[MAKS_SCIEZKA]; uint8_t glebokosc; };

    void dodaj_katalog(const char* sciezka, uint8_t g) {
        if (kat_n_ >= MAKS_KATALOGOW) { ++wynik_nowy_.pominiete_limit; return; }
        kopiuj_tekst(katalogi_[kat_n_].sciezka, MAKS_SCIEZKA, sciezka);
        katalogi_[kat_n_].glebokosc = g;
        ++kat_n_;
    }
    static bool nazwa_poprawna(const char* n, size_t dl) {
        if (dl == 0 || dl >= 64) return false;
        if ((dl == 1 && n[0] == '.') || (dl == 2 && n[0] == '.' && n[1] == '.')) return false;
        for (size_t i = 0; i < dl; ++i) {
            const uint8_t c = uint8_t(n[i]);
            if (c < 0x20 || c == 0x7F || c == '/' || c == '\\') return false;
        }
        return true;
    }
    static bool zloz_sciezke(char* out, size_t cap, const char* kat, const char* nazwa, size_t dl) {
        Tekst t(out, cap);
        t.dodaj(kat);
        if (!(t.dlugosc() > 0 && out[t.dlugosc() - 1] == '/')) t.dodaj("/");
        for (size_t i = 0; i < dl; ++i) if (!nazwa[i]) return false;
        char tmp[64]; size_t k = 0;
        for (; k < dl && k + 1 < sizeof(tmp); ++k) tmp[k] = nazwa[k];
        tmp[k] = '\0';
        t.dodaj(tmp);
        return !t.obciety();
    }
    void listuj_katalog(uint32_t idx) {
        const Katalog& kat = katalogi_[idx];
        zeruj(bufor_listingu_, MAKS_LISTING);
        const bool ok = listuj_(kontekst_listowania_, kat.sciezka, bufor_listingu_, MAKS_LISTING - 1);
        if (idx == 0) wynik_nowy_.katalog_istnieje = ok;
        if (!ok) return;
        bufor_listingu_[MAKS_LISTING - 1] = '\0';
        size_t pos = 0;
        const size_t n = dlugosc(bufor_listingu_, MAKS_LISTING);
        while (pos < n) {
            size_t end = pos;
            while (end < n && bufor_listingu_[end] != '\n') ++end;
            const char* w = bufor_listingu_ + pos;
            const size_t dl = end - pos;
            // "[PLIK] nazwa" albo "[KAT]  nazwa" - prefiks 7 znakow
            if (dl > 7 && w[0] == '[') {
                const bool plik = w[1] == 'P';
                const bool katalog = w[1] == 'K';
                const char* nazwa = w + 7;
                size_t nl = dl - 7;
                while (nl && (nazwa[nl - 1] == '\r')) --nl;
                if (nazwa_poprawna(nazwa, nl)) {
                    char sc[MAKS_SCIEZKA];
                    if (!zloz_sciezke(sc, sizeof(sc), kat.sciezka, nazwa, nl)) ++wynik_nowy_.pominiete_limit;
                    else if (plik) {
                        char nm[64]; size_t k = 0;
                        for (; k < nl && k + 1 < sizeof(nm); ++k) nm[k] = nazwa[k];
                        nm[k] = '\0';
                        const FormatAudio f = format_z_rozszerzenia(nm);
                        if (f != FormatAudio::Nieznany) {
                            ++wynik_nowy_.znalezione;
                            if (kand_n_ >= MAKS_KANDYDATOW) ++wynik_nowy_.pominiete_limit;
                            else {
                                Kandydat& c = kandydaci_[kand_n_++];
                                kopiuj_tekst(c.sciezka, sizeof(c.sciezka), sc);
                                kopiuj_tekst(c.nazwa, sizeof(c.nazwa), nm);
                                c.wg_rozszerzenia = f;
                            }
                        }
                    } else if (katalog && podkatalogi_ && kat.glebokosc + 1u <= MAKS_GLEBOKOSC) {
                        dodaj_katalog(sc, uint8_t(kat.glebokosc + 1));
                    }
                }
            }
            pos = end + 1;
        }
    }
    void analizuj(const Kandydat& k) {
        if (nowe_n_ >= MAKS_UTWOROW) { ++wynik_nowy_.pominiete_limit; return; }
        const char* b = analizuj_do(k.sciezka, k.nazwa, nowe_[nowe_n_]);
        if (b) { odrzuc(b); return; }
        ++nowe_n_;
        ++wynik_nowy_.przyjete;
    }
    // Wspolna analiza (skan i analizuj_plik): nullptr = przyjety (u wypelniony), inaczej powod odrzucenia.
    const char* analizuj_do(const char* sciezka, const char* nazwa, Utwor& u) {
        Czytnik c;
        if (!c.otworz(dostep_, sciezka, 16U * 1024U)) return "Nie można odczytać pliku.";
        uint8_t h[16] = {};
        if (!c.czytaj_w(0, h, c.rozmiar() < 16 ? uint32_t(c.rozmiar()) : 16)) return "Nie można odczytać pliku.";
        const FormatAudio f = format_z_naglowka(h, c.rozmiar() < 16 ? size_t(c.rozmiar()) : 16);
        u = Utwor{};
        kopiuj_tekst(u.sciezka, sizeof(u.sciezka), sciezka);
        kopiuj_tekst(u.nazwa_pliku, sizeof(u.nazwa_pliku), nazwa);
        {
            kopiuj_tekst(u.nazwa_bez_rozszerzenia_, sizeof(u.nazwa_bez_rozszerzenia_), nazwa);
            size_t n = dlugosc(u.nazwa_bez_rozszerzenia_);
            while (n > 0 && u.nazwa_bez_rozszerzenia_[n - 1] != '.') --n;
            if (n > 1) u.nazwa_bez_rozszerzenia_[n - 1] = '\0';
        }
        if (f == FormatAudio::WAV) {
            Wav::Info wi{};
            const Wav::Blad b = Wav::parsuj(c, wi, &u.meta);
            if (b != Wav::Blad::OK) return Wav::opis(b);
            u.format = FormatAudio::WAV;
            u.czestotliwosc = wi.czestotliwosc; u.kanaly = wi.kanaly; u.bity = wi.bity;
            u.czas_ms = uint32_t(wi.ramki * 1000ULL / wi.czestotliwosc > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : wi.ramki * 1000ULL / wi.czestotliwosc);
            u.przeplywnosc = uint32_t(uint64_t(wi.czestotliwosc) * wi.blok * 8ULL / 1000ULL);
        } else if (f == FormatAudio::MP3) {
            Mp3::Info mi{};
            const Mp3::Blad b = Mp3::analizuj(c, mi, &u.meta, bufor_id3_, Id3::MAKS_OKNO_TAGU);
            if (b != Mp3::Blad::OK) return Mp3::opis(b);
            u.format = FormatAudio::MP3;
            u.czestotliwosc = mi.czestotliwosc; u.kanaly = mi.kanaly;
            u.przeplywnosc = mi.przeplywnosc; u.vbr = mi.vbr;
            u.czas_ms = uint32_t(mi.czas_ms > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : mi.czas_ms);
        } else {
            return "Zawartość pliku nie odpowiada formatowi WAV ani MP3.";
        }
        return nullptr;
    }
    void odrzuc(const char* powod) {
        ++wynik_nowy_.odrzucone;
        kopiuj_tekst(wynik_nowy_.ostatni_blad, sizeof(wynik_nowy_.ostatni_blad), powod);
    }
    void opublikuj() {
        // Porzadek wyswietlania jako tablica indeksow (rekordy Utwor nie sa przesuwane): sortowanie przez wstawianie,
        // n <= MAKS_UTWOROW, porownania ograniczone dlugoscia pol.
        uint16_t* p = new uint16_t[MAKS_UTWOROW];
        if (p) {
            for (uint32_t i = 0; i < nowe_n_; ++i) {
                uint32_t j = i;
                while (j > 0 && porownaj(nowe_[p[j - 1]], nowe_[i]) > 0) { p[j] = p[j - 1]; --j; }
                p[j] = uint16_t(i);
            }
        }
        delete[] utwory_;
        delete[] porzadek_;
        utwory_ = nowe_; nowe_ = nullptr;
        porzadek_ = p;
        n_ = n_skan_ = nowe_n_;
        // pliki zewnetrzne za utworami skanu (bez duplikatu, gdy skan znalazl ten sam plik)
        for (uint32_t i = 0; i < zewn_n_ && n_ < POJEMNOSC_UTWOROW; ++i)
            if (znajdz(zewn_[i].sciezka) < 0) utwory_[n_++] = zewn_[i];
        wynik_ = wynik_nowy_;
        ++generacja_;
        stan_ = StanSkanu::Gotowy;
        przelicz_widoczne();
    }
    void przelicz_widoczne() {
        if (!widoczne_) widoczne_ = new uint16_t[MAKS_UTWOROW];
        widoczne_n_ = 0;
        if (!widoczne_) return;
        for (uint32_t k = 0; k < n_skan_; ++k) {                 // widok biblioteki: tylko utwory skanu
            const uint32_t i = porzadek_ ? porzadek_[k] : k;
            if (pasuje(utwory_[i], filtr_)) widoczne_[widoczne_n_++] = uint16_t(i);
        }
    }

    DostepDoPlikow dostep_{};
    FunkcjaListowania listuj_ = nullptr;
    void* kontekst_listowania_ = nullptr;
    bool podkatalogi_ = true;
    StanSkanu stan_ = StanSkanu::Bezczynny;
    Utwor* utwory_ = nullptr;
    uint32_t n_ = 0, n_skan_ = 0;
    Utwor* zewn_ = nullptr;            // kopie plikow zewnetrznych (dopisywane przy kazdej publikacji)
    uint32_t zewn_n_ = 0;
    Utwor* nowe_ = nullptr;
    uint32_t nowe_n_ = 0;
    Kandydat* kandydaci_ = nullptr;
    uint32_t kand_n_ = 0, kand_poz_ = 0;
    Katalog katalogi_[MAKS_KATALOGOW]{};
    uint32_t kat_n_ = 0, kat_poz_ = 0;
    char* bufor_listingu_ = nullptr;
    uint8_t* bufor_id3_ = nullptr;
    WynikSkanu wynik_{}, wynik_nowy_{};
    uint64_t generacja_ = 0;
    char filtr_[128]{};
    uint16_t* widoczne_ = nullptr;
    uint16_t* porzadek_ = nullptr;     // indeksy utwory_ w kolejnosci sortowania
    uint32_t widoczne_n_ = 0;
};

} // namespace Grajek
