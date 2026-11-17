#pragma once
/*
 * Okna podrzedne (informacyjne, modeless) - rejestr wlasnosci i polityka (czysta logika, testy hosta).
 *
 * Architektura: system ma jedna warstwe okna na proces, wiec okno podrzedne jest PRAWDZIWYM, osobnym procesem Ring 3
 * (wbudowany program okna informacyjnego), uruchamianym wylacznie przez BWS 101 w imieniu wlasciciela. Jadro prowadzi
 * wlasnosc: (proces-dziecko -> wlasciciel, rodzaj, tytul, tresc <= 4 KiB). Dziecko odbiera tresc przez BWS 102 (tylko
 * ten, kto jest w rejestrze). Zakonczenie wlasciciela konczy jego dzieci; zakonczenie dziecka czyni wpis wolnym i
 * oddaje fokus wlascicielowi. Rejestr jest deterministyczny i ograniczony (bez alokacji).
 */
#include <stddef.h>
#include <stdint.h>

namespace OknaPodrzedne {

inline constexpr uint32_t RODZAJ_O_PROGRAMIE_OBRAZY = 1;
inline constexpr uint32_t RODZAJ_INFO_OBRAZU        = 2;
inline constexpr uint32_t RODZAJ_STEROWANIE_OBRAZY  = 3;
inline constexpr uint32_t RODZAJ_STEROWANIE_SNAKE   = 4;
inline constexpr uint32_t RODZAJ_ZASADY_SNAKE       = 5;
inline constexpr uint32_t RODZAJ_O_GRZE_SNAKE       = 6;
inline constexpr uint32_t RODZAJ_O_NOTATNIKU        = 7;
inline constexpr uint32_t RODZAJ_STEROWANIE_NOTATNIK = 8;
inline constexpr uint32_t RODZAJ_MAKS               = 8;

inline constexpr size_t MAKS_TRESC = 4096;      // bajty UTF-8 (bez NUL)
inline constexpr size_t MAKS_TYTUL = 64;
inline constexpr size_t MAKS_OKIEN = 8;         // jednoczesnie w systemie
inline constexpr size_t MAKS_NA_WLASCICIELA = 4;

enum class Wynik : uint8_t { OK, ZlyRodzaj, ZlaTresc, LimitWlasciciela, BrakMiejsca, Duplikat, WlascicielToDziecko };

struct Wpis {
    bool aktywny = false;
    bool uruchomiony = false;      // pid_dziecka ustawiony
    int pid_dziecka = 0;
    int wlasciciel = 0;
    uint32_t rodzaj = 0;
    uint16_t dl_tresci = 0;
    uint64_t termin_ns = 0;        // dla osieroconego dziecka: do kiedy ma sie zamknac samo (potem wymuszone zakonczenie)
    char tytul[MAKS_TYTUL] = {};
    char tresc[MAKS_TRESC + 1] = {};
};

// Tresc: UTF-8, bez NUL i bez znakow sterujacych poza '\n' i '\t'; tytul niepusty, bez znakow sterujacych.
inline bool tresc_poprawna(const char* t, size_t dl) {
    if (!t || dl == 0 || dl > MAKS_TRESC) return false;
    for (size_t i = 0; i < dl; ++i) {
        const unsigned char c = static_cast<unsigned char>(t[i]);
        if (c == 0) return false;
        if (c < 0x20 && c != '\n' && c != '\t') return false;
        if (c == 0x7F) return false;
    }
    return true;
}
inline bool tytul_poprawny(const char* t, size_t dl) {
    if (!t || dl == 0 || dl >= MAKS_TYTUL) return false;
    for (size_t i = 0; i < dl; ++i) {
        const unsigned char c = static_cast<unsigned char>(t[i]);
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

class Rejestr {
public:
    Wpis w[MAKS_OKIEN];

    int indeks_dziecka(int pid) const {
        for (size_t i = 0; i < MAKS_OKIEN; ++i) if (w[i].aktywny && w[i].uruchomiony && w[i].pid_dziecka == pid) return int(i);
        return -1;
    }
    int indeks(int wlasciciel, uint32_t rodzaj) const {
        for (size_t i = 0; i < MAKS_OKIEN; ++i) if (w[i].aktywny && w[i].wlasciciel == wlasciciel && w[i].rodzaj == rodzaj) return int(i);
        return -1;
    }
    size_t liczba_wlasciciela(int wlasciciel) const {
        size_t n = 0;
        for (size_t i = 0; i < MAKS_OKIEN; ++i) if (w[i].aktywny && w[i].wlasciciel == wlasciciel) ++n;
        return n;
    }
    bool jest_dzieckiem(int pid) const { return indeks_dziecka(pid) >= 0; }

    // Rezerwuje wpis (jeszcze bez pid). `*idx` = indeks wpisu przy Wynik::OK.
    Wynik zarezerwuj(int wlasciciel, uint32_t rodzaj, const char* tytul, size_t dl_tytulu,
                     const char* tresc, size_t dl_tresci, int* idx) {
        if (rodzaj == 0 || rodzaj > RODZAJ_MAKS) return Wynik::ZlyRodzaj;
        if (!tytul_poprawny(tytul, dl_tytulu) || !tresc_poprawna(tresc, dl_tresci)) return Wynik::ZlaTresc;
        if (jest_dzieckiem(wlasciciel)) return Wynik::WlascicielToDziecko;   // dziecko nie otwiera kolejnych okien
        if (indeks(wlasciciel, rodzaj) >= 0) return Wynik::Duplikat;
        if (liczba_wlasciciela(wlasciciel) >= MAKS_NA_WLASCICIELA) return Wynik::LimitWlasciciela;
        for (size_t i = 0; i < MAKS_OKIEN; ++i) {
            if (w[i].aktywny) continue;
            Wpis& e = w[i];
            e = Wpis{};
            e.aktywny = true; e.wlasciciel = wlasciciel; e.rodzaj = rodzaj; e.dl_tresci = uint16_t(dl_tresci);
            for (size_t k = 0; k < dl_tytulu; ++k) e.tytul[k] = tytul[k];
            for (size_t k = 0; k < dl_tresci; ++k) e.tresc[k] = tresc[k];
            if (idx) *idx = int(i);
            return Wynik::OK;
        }
        return Wynik::BrakMiejsca;
    }
    // Odswiezenie tresci istniejacego okna (np. nowe dane obrazu); tytul bez zmian.
    Wynik zaktualizuj_tresc(int idx, const char* tresc, size_t dl_tresci) {
        if (idx < 0 || size_t(idx) >= MAKS_OKIEN || !w[idx].aktywny) return Wynik::ZlyRodzaj;
        if (!tresc_poprawna(tresc, dl_tresci)) return Wynik::ZlaTresc;
        for (size_t k = 0; k < dl_tresci; ++k) w[idx].tresc[k] = tresc[k];
        w[idx].tresc[dl_tresci] = '\0';
        w[idx].dl_tresci = uint16_t(dl_tresci);
        return Wynik::OK;
    }
    // false (bez zmian), gdy indeks jest nieprawidlowy albo ten pid jest juz zarejestrowany jako inne dziecko.
    bool ustaw_pid(int idx, int pid) {
        if (idx < 0 || size_t(idx) >= MAKS_OKIEN || !w[idx].aktywny || pid <= 0) return false;
        const int zajety = indeks_dziecka(pid);
        if (zajety >= 0 && zajety != idx) return false;
        w[idx].pid_dziecka = pid; w[idx].uruchomiony = true;
        return true;
    }
    void anuluj(int idx) { if (idx >= 0 && size_t(idx) < MAKS_OKIEN) w[idx] = Wpis{}; }
    // Zwraca wlasciciela usuwanego dziecka (0 = nie bylo dzieckiem).
    int usun_dziecko(int pid) {
        const int i = indeks_dziecka(pid);
        if (i < 0) return 0;
        const int wl = w[i].wlasciciel;
        w[i] = Wpis{};
        return wl;
    }
    // Dzieci wlasciciela (pid-y uruchomionych); zwraca liczbe wpisanych do out.
    size_t dzieci(int wlasciciel, int* out, size_t cap) const {
        size_t n = 0;
        for (size_t i = 0; i < MAKS_OKIEN && n < cap; ++i)
            if (w[i].aktywny && w[i].uruchomiony && w[i].wlasciciel == wlasciciel) out[n++] = w[i].pid_dziecka;
        return n;
    }
    // Zakonczenie wlasciciela: jego okna stają się OSIEROCONE (wlasciciel = -1: zaden pid nie dziedziczy wpisu), a pid-y
    // uruchomionych dzieci trafiaja do `out` (wywolujacy prosi je o zamkniecie). Niezakonczone rezerwacje sa usuwane.
    size_t osieroc(int wlasciciel, int* out, size_t cap, uint64_t termin_ns = 0) {
        size_t n = 0;
        for (size_t i = 0; i < MAKS_OKIEN; ++i) {
            if (!w[i].aktywny || w[i].wlasciciel != wlasciciel) continue;
            if (!w[i].uruchomiony) { w[i] = Wpis{}; continue; }
            if (out && n < cap) out[n++] = w[i].pid_dziecka;
            w[i].wlasciciel = -1;
            w[i].termin_ns = termin_ns;
        }
        return n;
    }
    // Osierocone dzieci, ktore nie zamknely sie do `teraz` (jednorazowo: termin zerowany); zwraca liczbe pid-ow.
    size_t przeterminowane(uint64_t teraz, int* out, size_t cap) {
        size_t n = 0;
        for (size_t i = 0; i < MAKS_OKIEN && n < cap; ++i)
            if (w[i].aktywny && w[i].uruchomiony && w[i].wlasciciel == -1 && w[i].termin_ns != 0 && teraz >= w[i].termin_ns) {
                out[n++] = w[i].pid_dziecka;
                w[i].termin_ns = 0;
            }
        return n;
    }
    // Ponowna proba eskalacji (np. proces bywa chwilowo nieosiagalny): nowy termin dla dziecka `pid`.
    void ponow_termin(int pid, uint64_t termin_ns) {
        for (size_t i = 0; i < MAKS_OKIEN; ++i)
            if (w[i].aktywny && w[i].uruchomiony && w[i].pid_dziecka == pid && w[i].wlasciciel == -1) w[i].termin_ns = termin_ns;
    }
    // Kopiuje "tytul\n\ntresc" dziecka do bufora (NUL-terminated). Zwraca liczbe bajtow bez NUL albo 0.
    size_t kopiuj_dla_dziecka(int pid, char* out, size_t cap) const {
        const int i = indeks_dziecka(pid);
        if (i < 0 || !out || cap == 0) return 0;
        size_t n = 0;
        auto put = [&](char c) { if (n + 1 < cap) out[n] = c; ++n; };
        for (size_t k = 0; w[i].tytul[k]; ++k) put(w[i].tytul[k]);
        put('\n'); put('\n');
        for (size_t k = 0; k < w[i].dl_tresci; ++k) put(w[i].tresc[k]);
        if (n + 1 > cap) { out[0] = '\0'; return 0; }     // bufor zbyt maly: nic nie wydajemy
        out[n] = '\0';
        return n;
    }
};

} // namespace OknaPodrzedne
