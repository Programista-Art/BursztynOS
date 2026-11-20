#pragma once
/*
 * Bursztyn OS - kolejka powiadomien systemowych (czysta logika: jadro + testy hosta).
 *
 * Ograniczona kolejka BWS_POW_MAKS wpisow ze stalymi buforami tekstu (bez alokacji). Zrodlo SYSTEM ustawia
 * wylacznie kod jadra (dodaj_systemowe); BWS 109 ZGLOS trafia do dodaj_aplikacji, ktora zawsze zapisuje
 * zrodlo APLIKACJA, PID i nazwe programu podana przez jadro - tekst aplikacji nie moze zmienic nadawcy.
 *
 * Aplikacje: poziom tylko INFO/SUKCES/OSTRZEZENIE, limit LIMIT_W_OKNIE zgloszen na OKNO_MS na proces i najwyzej
 * MAKS_NA_APLIKACJE wpisow w kolejce. Pelna kolejka: wypada najstarszy przeczytany, potem najstarszy wpis aplikacji;
 * zgloszenie aplikacji nigdy nie wypiera nieprzeczytanego powiadomienia systemowego (wtedy LIMIT).
 *
 * Teksty: tylko poprawny UTF-8; znaki sterujace -> spacja; niepoprawna sekwencja konczy tekst; ucinanie na granicy
 * znaku; zawsze NUL. Wywolujacy w jadrze trzyma krotka sekcje bez przerwan wokol kazdej operacji.
 */
#include <stdint.h>
#include <stddef.h>
#include "zasobnik_api.h"

namespace Powiadomienia {

inline constexpr uint32_t MAKS_PID = 16U;            // == PZB_MAKS_PROCESOW (sprawdzane w jadrze)
inline constexpr uint64_t OKNO_MS = 30000ULL;
inline constexpr uint32_t LIMIT_W_OKNIE = 3U;
inline constexpr uint32_t MAKS_NA_APLIKACJE = 4U;

/* Dlugosc poprawnego znaku UTF-8 od s[i] (0 = niepoprawny / uciety). Odrzuca nadmiarowe kodowania i surogaty. */
inline size_t znak_utf8(const char* s, size_t i, size_t n, uint32_t* cp) {
    const uint8_t b0 = uint8_t(s[i]);
    size_t dl; uint32_t v;
    if (b0 < 0x80) { *cp = b0; return 1; }
    if ((b0 & 0xE0) == 0xC0) { dl = 2; v = b0 & 0x1F; }
    else if ((b0 & 0xF0) == 0xE0) { dl = 3; v = b0 & 0x0F; }
    else if ((b0 & 0xF8) == 0xF0) { dl = 4; v = b0 & 0x07; }
    else return 0;
    if (i + dl > n) return 0;
    for (size_t k = 1; k < dl; ++k) {
        const uint8_t b = uint8_t(s[i + k]);
        if ((b & 0xC0) != 0x80) return 0;
        v = (v << 6) | (b & 0x3F);
    }
    if ((dl == 2 && v < 0x80) || (dl == 3 && v < 0x800) || (dl == 4 && (v < 0x10000 || v > 0x10FFFF)) ||
        (v >= 0xD800 && v <= 0xDFFF)) return 0;
    *cp = v;
    return dl;
}

/* Kopia tekstu z bufora o pojemnosci maks_zrodla (NUL nie jest wymagany) do cel[pojemnosc]. Zwraca dlugosc. */
inline size_t oczysc(char* cel, size_t pojemnosc, const char* zrodlo, size_t maks_zrodla) {
    if (!cel || !pojemnosc) return 0;
    cel[0] = '\0';
    if (!zrodlo) return 0;
    size_t n = 0;
    while (n < maks_zrodla && zrodlo[n]) ++n;
    size_t i = 0, w = 0;
    while (i < n) {
        uint32_t cp = 0;
        const size_t dl = znak_utf8(zrodlo, i, n, &cp);
        if (!dl || w + dl + 1 > pojemnosc) break;
        if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F)) {
            if (w > 0 && cel[w - 1] != ' ') cel[w++] = ' ';       // sterujace (np. \n) -> jedna spacja
        } else {
            for (size_t k = 0; k < dl; ++k) cel[w++] = zrodlo[i + k];
        }
        i += dl;
    }
    while (w > 0 && cel[w - 1] == ' ') --w;
    size_t start = 0;
    while (start < w && cel[start] == ' ') ++start;
    if (start) { for (size_t k = start; k < w; ++k) cel[k - start] = cel[k]; w -= start; }
    cel[w] = '\0';
    return w;
}

/* Nazwa programu z proces_t.sciezka_pliku: ostatni czlon bez ".bur" (np. "kalendarz"). */
inline void nazwa_programu(char* cel, size_t pojemnosc, const char* sciezka, size_t maks) {
    if (!cel || !pojemnosc) return;
    cel[0] = '\0';
    if (!sciezka) return;
    size_t n = 0, poczatek = 0;
    while (n < maks && sciezka[n]) { if (sciezka[n] == '/') poczatek = n + 1; ++n; }
    size_t koniec = n;
    if (koniec >= poczatek + 4 && sciezka[koniec - 4] == '.' && sciezka[koniec - 3] == 'b' && sciezka[koniec - 2] == 'u' &&
        sciezka[koniec - 1] == 'r') koniec -= 4;
    size_t w = 0;
    for (size_t k = poczatek; k < koniec && w + 1 < pojemnosc; ++k) {
        const char c = sciezka[k];
        cel[w++] = (c >= 0x20 && c < 0x7F) ? c : '_';
    }
    cel[w] = '\0';
    if (!w) oczysc(cel, pojemnosc, "aplikacja", 10);
}

class Kolejka {
public:
    /* Powiadomienie jadra/sterownika (zaufane). Zawsze przyjmowane: w razie potrzeby wypada najstarszy wpis. */
    uint64_t dodaj_systemowe(uint32_t poziom, const char* tytul, const char* tresc, uint64_t teraz_ms) {
        if (poziom > BWS_POW_BLAD) poziom = BWS_POW_INFO;
        BwsPowiadomienieV1 w{};
        if (!oczysc(w.tytul, sizeof(w.tytul), tytul, BWS_POW_TYTUL * 4)) return 0;
        oczysc(w.tresc, sizeof(w.tresc), tresc, BWS_POW_TRESC * 4);
        oczysc(w.nadawca, sizeof(w.nadawca), "System", 7);
        w.zrodlo = BWS_POW_ZRODLO_SYSTEM; w.poziom = poziom; w.czas_ms = teraz_ms;
        int slot = wolny_slot(true);
        if (slot < 0) slot = najstarszy([](const BwsPowiadomienieV1&) { return true; });
        return wstaw(slot, w);
    }

    /* BWS 109 ZGLOS. Zwraca BWS_ZAS_OK / LIMIT / ZLE_ARGUMENTY; *id = nadane id. */
    uint64_t dodaj_aplikacji(int pid, const char* nazwa, uint32_t poziom, const char* tytul, size_t maks_tytulu,
                             const char* tresc, size_t maks_tresci, uint64_t teraz_ms, uint64_t* id = nullptr) {
        if (pid <= 0 || uint32_t(pid) >= MAKS_PID || poziom > BWS_POW_OSTRZEZENIE) return BWS_ZAS_ZLE_ARGUMENTY;
        BwsPowiadomienieV1 w{};
        if (!oczysc(w.tytul, sizeof(w.tytul), tytul, maks_tytulu)) return BWS_ZAS_ZLE_ARGUMENTY;
        oczysc(w.tresc, sizeof(w.tresc), tresc, maks_tresci);
        oczysc(w.nadawca, sizeof(w.nadawca), nazwa, BWS_POW_NADAWCA);
        if (!w.nadawca[0]) oczysc(w.nadawca, sizeof(w.nadawca), "aplikacja", 10);
        Limit& l = limity_[pid];
        if (teraz_ms < l.okno_od || teraz_ms - l.okno_od >= OKNO_MS) { l.okno_od = teraz_ms; l.w_oknie = 0; }
        if (l.w_oknie >= LIMIT_W_OKNIE) return BWS_ZAS_LIMIT;
        uint32_t juz = 0;
        for (uint32_t i = 0; i < BWS_POW_MAKS; ++i)
            juz += uzyte_[i] && wpisy_[i].zrodlo == BWS_POW_ZRODLO_APLIKACJA && wpisy_[i].pid == pid;
        if (juz >= MAKS_NA_APLIKACJE) return BWS_ZAS_LIMIT;
        int slot = wolny_slot(false);
        if (slot < 0) return BWS_ZAS_LIMIT;                       // same nieprzeczytane systemowe: nie wypieramy
        w.zrodlo = BWS_POW_ZRODLO_APLIKACJA; w.pid = pid; w.poziom = poziom; w.czas_ms = teraz_ms;
        ++l.w_oknie;
        const uint64_t nowe = wstaw(slot, w);
        if (id) *id = nowe;
        return BWS_ZAS_OK;
    }

    uint64_t oznacz_przeczytane(uint64_t id) {
        const int i = znajdz(id);
        if (i < 0) return BWS_ZAS_BRAK;
        if (!(wpisy_[i].flagi & BWS_POW_PRZECZYTANE)) { wpisy_[i].flagi |= BWS_POW_PRZECZYTANE; ++generacja_; }
        return BWS_ZAS_OK;
    }
    uint64_t usun(uint64_t id) {
        const int i = znajdz(id);
        if (i < 0) return BWS_ZAS_BRAK;
        uzyte_[i] = false; wpisy_[i] = BwsPowiadomienieV1{}; ++generacja_;
        return BWS_ZAS_OK;
    }
    void wyczysc() {
        bool zmiana = false;
        for (uint32_t i = 0; i < BWS_POW_MAKS; ++i) { zmiana |= uzyte_[i]; uzyte_[i] = false; wpisy_[i] = BwsPowiadomienieV1{}; }
        if (zmiana) ++generacja_;
    }
    void przeczytaj_wszystkie() {
        bool zmiana = false;
        for (uint32_t i = 0; i < BWS_POW_MAKS; ++i)
            if (uzyte_[i] && !(wpisy_[i].flagi & BWS_POW_PRZECZYTANE)) { wpisy_[i].flagi |= BWS_POW_PRZECZYTANE; zmiana = true; }
        if (zmiana) ++generacja_;
    }
    /* Zakonczenie procesu: limit czestosci nie przechodzi na nastepny proces z tym PID (wpisy zostaja). */
    void posprzataj_proces(int pid) { if (pid > 0 && uint32_t(pid) < MAKS_PID) limity_[pid] = Limit{}; }

    void migawka(BwsPowiadomieniaListaV1& o) const {
        o = BwsPowiadomieniaListaV1{};
        o.wersja = uint32_t(BWS_ZASOBNIK_V1); o.rozmiar = sizeof(o); o.generacja = generacja_;
        bool wziety[BWS_POW_MAKS] = {};
        for (uint32_t n = 0; n < BWS_POW_MAKS; ++n) {               // od najnowszego (najwieksze id)
            int best = -1;
            for (uint32_t i = 0; i < BWS_POW_MAKS; ++i)
                if (uzyte_[i] && !wziety[i] && (best < 0 || wpisy_[i].id > wpisy_[best].id)) best = int(i);
            if (best < 0) break;
            wziety[best] = true;
            o.wpisy[o.liczba++] = wpisy_[best];
            if (!(wpisy_[best].flagi & BWS_POW_PRZECZYTANE)) ++o.nieprzeczytane;
        }
    }
    uint32_t liczba() const { uint32_t n = 0; for (bool u : uzyte_) n += u; return n; }
    uint32_t nieprzeczytane() const {
        uint32_t n = 0;
        for (uint32_t i = 0; i < BWS_POW_MAKS; ++i) n += uzyte_[i] && !(wpisy_[i].flagi & BWS_POW_PRZECZYTANE);
        return n;
    }
    uint64_t generacja() const { return generacja_; }

private:
    struct Limit { uint64_t okno_od = 0; uint32_t w_oknie = 0; };

    int znajdz(uint64_t id) const {
        if (!id) return -1;
        for (uint32_t i = 0; i < BWS_POW_MAKS; ++i) if (uzyte_[i] && wpisy_[i].id == id) return int(i);
        return -1;
    }
    template <class Warunek> int najstarszy(Warunek w) const {
        int best = -1;
        for (uint32_t i = 0; i < BWS_POW_MAKS; ++i)
            if (uzyte_[i] && w(wpisy_[i]) && (best < 0 || wpisy_[i].id < wpisy_[best].id)) best = int(i);
        return best;
    }
    /* Wolny slot; przy pelnej kolejce najstarszy przeczytany, potem (gdy wolno) najstarszy wpis aplikacji. */
    int wolny_slot(bool system) const {
        for (uint32_t i = 0; i < BWS_POW_MAKS; ++i) if (!uzyte_[i]) return int(i);
        int s = najstarszy([](const BwsPowiadomienieV1& w) { return (w.flagi & BWS_POW_PRZECZYTANE) != 0; });
        if (s >= 0) return s;
        s = najstarszy([](const BwsPowiadomienieV1& w) { return w.zrodlo == BWS_POW_ZRODLO_APLIKACJA; });
        if (s >= 0) return s;
        (void)system;
        return -1;
    }
    uint64_t wstaw(int slot, BwsPowiadomienieV1& w) {
        if (slot < 0 || uint32_t(slot) >= BWS_POW_MAKS) return 0;
        w.id = ++ostatnie_id_;
        wpisy_[slot] = w; uzyte_[slot] = true; ++generacja_;
        return w.id;
    }

    BwsPowiadomienieV1 wpisy_[BWS_POW_MAKS]{};
    bool uzyte_[BWS_POW_MAKS]{};
    Limit limity_[MAKS_PID]{};
    uint64_t ostatnie_id_ = 0, generacja_ = 0;
};

}  // namespace Powiadomienia
