#pragma once
/*
 * Kalendarz - model (bez GUI, bez alokacji, bez libc). Obliczenia dat sa wylacznie w BursztynUI::DataKalendarza
 * (gregorianski, rok 1900..2100); tu tylko opis wybranego dnia dla interfejsu.
 *
 * Rozszerzalnosc: OpisDnia jest jedynym miejscem, z ktorego okno bierze tresc panelu dnia. Przyszle wydarzenia
 * (przypomnienia, terminy) dopisza tu swoje pola i zrodlo, bez zmiany komponentu siatki kalendarza.
 */
#include "bursztyn_ui/kalendarz.h"

namespace KalendarzApp {

using BursztynUI::DataKalendarza;

/* Kolejny numer dnia (od 1900-01-01 = 0); -1 dla niepoprawnej daty. Bez zawijania w zakresie komponentu. */
inline int numer_dnia(const DataKalendarza& d) {
    if (!d.czy_poprawna()) return -1;
    int n = 0;
    for (int r = 1900; r < d.rok; ++r) n += DataKalendarza::czy_przestepny(r) ? 366 : 365;
    return n + d.dzien_roku() - 1;
}

struct Tekst {
    char* b; size_t cap; size_t n = 0;
    Tekst(char* bufor, size_t c) : b(bufor), cap(c) { if (cap) b[0] = '\0'; }
    void dodaj(const char* s) { while (s && *s && n + 1 < cap) b[n++] = *s++; if (cap) b[n] = '\0'; }
    void liczba(int v) {
        if (v < 0) { dodaj("-"); v = -v; }
        char t[12]; int k = 0;
        do { t[k++] = char('0' + v % 10); v /= 10; } while (v && k < 12);
        while (k && n + 1 < cap) b[n++] = t[--k];
        if (cap) b[n] = '\0';
    }
};

/* "za 5 dni", "3 dni temu", "jutro", "wczoraj", "dzisiaj" (polska odmiana: 1 dzien, wiele dni). */
inline void odleglosc(int dni, char* out, size_t cap) {
    Tekst t(out, cap);
    if (dni == 0) { t.dodaj("dzisiaj"); return; }
    if (dni == 1) { t.dodaj("jutro"); return; }
    if (dni == -1) { t.dodaj("wczoraj"); return; }
    const int a = dni < 0 ? -dni : dni;
    if (dni > 0) { t.dodaj("za "); t.liczba(a); t.dodaj(" dni"); }
    else { t.liczba(a); t.dodaj(" dni temu"); }
}

struct OpisDnia {
    char data[64]{};      // "czwartek, 19 listopada 2026"
    char szczegoly[128]{};  // "Dzień roku: 323 · Tydzień ISO: 47 · dzisiaj"
};

inline OpisDnia opisz(const DataKalendarza& wybrana, const DataKalendarza& dzisiaj, bool ma_dzisiaj) {
    OpisDnia o;
    if (!wybrana.czy_poprawna()) { Tekst t(o.data, sizeof(o.data)); t.dodaj("Niepoprawna data"); return o; }
    wybrana.formatuj(o.data, sizeof(o.data));
    Tekst t(o.szczegoly, sizeof(o.szczegoly));
    t.dodaj("Dzień roku: "); t.liczba(wybrana.dzien_roku());
    t.dodaj(" · Tydzień ISO: "); t.liczba(wybrana.tydzien_iso());
    if (ma_dzisiaj && dzisiaj.czy_poprawna()) {
        char r[32]; odleglosc(numer_dnia(wybrana) - numer_dnia(dzisiaj), r, sizeof(r));
        t.dodaj(" · "); t.dodaj(r);
    } else t.dodaj(" · data systemowa niedostępna");
    return o;
}

/* "Listopad 2026" */
inline void tytul_miesiaca(int rok, int miesiac, char* out, size_t cap) {
    Tekst t(out, cap);
    t.dodaj(DataKalendarza::nazwa_miesiaca(miesiac)); t.dodaj(" "); t.liczba(rok);
}

}  // namespace KalendarzApp
