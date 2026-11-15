#pragma once
/*
 * Kopiowanie zdjec z kontenera BZP3 do /dane/zdjecia (wspolne dla importu zywego systemu i etapu instalatora).
 * Czyste: operacje plikowe przychodza przez `Io`, wiec mechanizm jest testowany na hoscie (atrapa systemu plikow
 * z limitem pojemnosci, wymuszonymi bledami i przerwaniami).
 *
 * Gwarancje:
 *  - plik uzytkownika (istniejacy final) NIGDY nie jest nadpisywany ani zmieniany;
 *  - dane trafiaja najpierw do pliku tymczasowego "zdjecie.bzp3tmp", a pod docelowa nazwe dopiero po poprawnym
 *    zapisie calosci (zmiana nazwy nie nadpisuje istniejacej nazwy), wiec przerwanie/utrata zasilania w polowie
 *    pozostawia co najwyzej plik tymczasowy, a NIGDY niepelne zdjecie pod finalna nazwa;
 *  - przed rozpoczeciem usuwany jest stary plik tymczasowy; po bledzie zapisu plik tymczasowy jest usuwany;
 *  - CRC-32 wpisu jest sprawdzane przed utworzeniem pliku (integralnosc, nie uwierzytelnienie).
 */
#include "galeria_pak.h"

namespace GaleriaPak {

struct Io {
    uint32_t (*rozmiar)(const char* sciezka);                                   // 0 = brak/pusty
    bool (*utworz)(const char* sciezka);
    bool (*usun)(const char* sciezka);
    bool (*zapisz)(const char* sciezka, uint32_t offset, const uint8_t* dane, uint32_t n);
    bool (*zmien_nazwe)(const char* sciezka, const char* nowa_nazwa);          // tylko nazwa, bez nadpisywania
    bool (*flush)();
};

enum class Krok : uint8_t { Dalej, Koniec, KoniecBlad };

inline constexpr char KATALOG_ZDJEC[] = "/dane/zdjecia/";
inline constexpr char NAZWA_TMP[] = "zdjecie.bzp3tmp";

struct Kopiarka {
    const uint8_t* pak{nullptr};
    const Wpis* wpisy{nullptr};
    const uint32_t* kolejnosc{nullptr};     // opcjonalna kolejnosc wpisow (nullptr = tabela po kolei)
    uint32_t liczba{0};
    Io io{};
    uint32_t pozycja{0}, offset{0};
    bool otwarty{false}, zakonczona{false};
    uint32_t zainstalowane{0}, pominiete{0}, bledne{0};
    uint64_t bajty{0};
    char tmp[96]{};

    static bool zloz(char* out, size_t cap, const char* nazwa, uint32_t dl) {
        size_t p = 0;
        for (; KATALOG_ZDJEC[p]; ++p) { if (p + 1 >= cap) return false; out[p] = KATALOG_ZDJEC[p]; }
        for (uint32_t k = 0; k < dl; ++k) { if (p + 1 >= cap) return false; out[p++] = nazwa[k]; }
        out[p] = '\0';
        return true;
    }
    void start(const uint8_t* p, const Wpis* w, uint32_t n, const Io& i, const uint32_t* kol = nullptr) {
        *this = Kopiarka{};
        pak = p; wpisy = w; liczba = n; io = i; kolejnosc = kol;
        zloz(tmp, sizeof(tmp), NAZWA_TMP, sizeof(NAZWA_TMP) - 1);
        (void)io.usun(tmp);                   // pozostalosc po przerwanym kopiowaniu
    }
    const Wpis& biezacy() const { return wpisy[kolejnosc ? kolejnosc[pozycja] : pozycja]; }
    void nastepny() { ++pozycja; offset = 0; otwarty = false; }
    void porzuc_tmp() { (void)io.usun(tmp); otwarty = false; }

    // Jedna porcja pracy (<= porcja bajtow). Wynik: Dalej / Koniec / KoniecBlad (blad zapisu: etap przerwany).
    Krok krok(uint32_t porcja) {
        if (zakonczona) return Krok::Koniec;
        if (pozycja >= liczba) { (void)io.flush(); zakonczona = true; return Krok::Koniec; }
        const Wpis& w = biezacy();
        char final_sciezka[96];
        const uint8_t* dane = pak + w.przesuniecie;
        if (!zloz(final_sciezka, sizeof(final_sciezka), w.nazwa, w.dl)) { ++bledne; bajty += w.rozmiar; nastepny(); return Krok::Dalej; }
        if (!otwarty) {
            if (io.rozmiar(final_sciezka) > 0) { ++pominiete; bajty += w.rozmiar; nastepny(); return Krok::Dalej; }
            if (crc32(dane, w.rozmiar) != w.crc) { ++bledne; bajty += w.rozmiar; nastepny(); return Krok::Dalej; }
            (void)io.usun(tmp);
            if (!io.utworz(tmp)) { ++bledne; bajty += w.rozmiar; nastepny(); return Krok::Dalej; }
            otwarty = true; offset = 0;
        }
        const uint32_t n = (w.rozmiar - offset > porcja) ? porcja : (w.rozmiar - offset);
        if (!io.zapisz(tmp, offset, dane + offset, n)) {
            porzuc_tmp(); ++bledne; zakonczona = true; (void)io.flush();
            return Krok::KoniecBlad;            // brak miejsca / blad: reszte pomijamy, plik tymczasowy usuniety
        }
        offset += n; bajty += n;
        if (offset >= w.rozmiar) {
            if (io.rozmiar(tmp) != w.rozmiar) { porzuc_tmp(); ++bledne; nastepny(); return Krok::Dalej; }
            if (io.rozmiar(final_sciezka) > 0) { porzuc_tmp(); ++pominiete; nastepny(); return Krok::Dalej; }   // wyscig: plik uzytkownika
            if (!io.zmien_nazwe(tmp, final_sciezka + (sizeof(KATALOG_ZDJEC) - 1))) { porzuc_tmp(); ++bledne; nastepny(); return Krok::Dalej; }
            ++zainstalowane; (void)io.flush();
            nastepny();
        }
        return Krok::Dalej;
    }
};

} // namespace GaleriaPak
