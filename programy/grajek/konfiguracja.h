#pragma once
/*
 * Grajek - trwala konfiguracja (klucz "konfiguracja" w /uzytkownicy/<u>/Ustawienia/grajek/, prawo
 * ustawienia_aplikacji). Format tekstowy "klucz=wartosc" w wierszach, <= 4096 B. Nieznane klucze sa ignorowane,
 * wartosci spoza zakresu przycinane, uszkodzony plik daje ustawienia domyslne - nigdy blad krytyczny.
 */
#include "kolejka.h"
#include "korektor.h"

namespace Grajek {

struct Konfiguracja {
    uint32_t glosnosc = 80;
    bool wyciszony = false;
    bool losowo = false;
    Powtarzanie powtarzanie = Powtarzanie::Brak;
    bool w_tle = false;
    bool korektor = false;
    int8_t pasma[Tabele::LICZBA_PASM]{};
    // Domyslnie wylaczona: pomiar QEMU (TCG) wykazal, ze odrysowania wizualizacji zwiekszaja przerwy dzwieku.
    bool wizualizacja = false;
    bool podkatalogi = true;
    // Pomiar sekcji (pomiar.h) i podsumowanie w pasku stanu; domyslnie wylaczony (zero kosztu w sciezce audio).
    bool diagnostyka = false;

    void zapisz(char* out, size_t cap) const {
        Tekst t(out, cap);
        t.dodaj("# Grajek 1.0 - konfiguracja").nowa_linia();
        t.dodaj("glosnosc="); t.liczba(glosnosc); t.nowa_linia();
        t.dodaj("wyciszony="); t.liczba(wyciszony); t.nowa_linia();
        t.dodaj("losowo="); t.liczba(losowo); t.nowa_linia();
        t.dodaj("powtarzanie="); t.liczba(uint32_t(powtarzanie)); t.nowa_linia();
        t.dodaj("w_tle="); t.liczba(w_tle); t.nowa_linia();
        t.dodaj("korektor="); t.liczba(korektor); t.nowa_linia();
        t.dodaj("pasma=");
        for (int i = 0; i < Tabele::LICZBA_PASM; ++i) {
            if (i) t.dodaj(",");
            if (pasma[i] < 0) { t.dodaj("-"); t.liczba(uint64_t(-pasma[i])); } else t.liczba(uint64_t(pasma[i]));
        }
        t.nowa_linia();
        t.dodaj("wizualizacja="); t.liczba(wizualizacja); t.nowa_linia();
        t.dodaj("podkatalogi="); t.liczba(podkatalogi); t.nowa_linia();
        t.dodaj("diagnostyka="); t.liczba(diagnostyka); t.nowa_linia();
    }

    // Zwraca liczbe rozpoznanych kluczy.
    uint32_t wczytaj(const char* tekst, size_t dl) {
        uint32_t rozpoznane = 0;
        size_t pos = 0;
        if (dl > 4096) dl = 4096;
        while (pos < dl && tekst[pos]) {
            size_t end = pos;
            while (end < dl && tekst[end] && tekst[end] != '\n') ++end;
            const char* l = tekst + pos;
            const size_t n = end - pos;
            size_t eq = 0;
            while (eq < n && l[eq] != '=') ++eq;
            if (n && l[0] != '#' && eq < n && eq < 24) {
                char klucz[24]; for (size_t i = 0; i < eq; ++i) klucz[i] = l[i]; klucz[eq] = '\0';
                const char* v = l + eq + 1;
                const size_t vn = n - eq - 1;
                uint64_t x = 0;
                auto bool_ = [&](bool& cel) { if (parsuj_liczbe(v, vn, 1, &x)) { cel = x != 0; ++rozpoznane; } };
                if (rowne(klucz, "glosnosc")) { if (parsuj_liczbe(v, vn, 1000, &x)) { glosnosc = x > 100 ? 100 : uint32_t(x); ++rozpoznane; } }
                else if (rowne(klucz, "wyciszony")) bool_(wyciszony);
                else if (rowne(klucz, "losowo")) bool_(losowo);
                else if (rowne(klucz, "powtarzanie")) { if (parsuj_liczbe(v, vn, 2, &x)) { powtarzanie = Powtarzanie(x); ++rozpoznane; } }
                else if (rowne(klucz, "w_tle")) bool_(w_tle);
                else if (rowne(klucz, "korektor")) bool_(korektor);
                else if (rowne(klucz, "wizualizacja")) bool_(wizualizacja);
                else if (rowne(klucz, "podkatalogi")) bool_(podkatalogi);
                else if (rowne(klucz, "diagnostyka")) bool_(diagnostyka);
                else if (rowne(klucz, "pasma")) {
                    int8_t tmp[Tabele::LICZBA_PASM] = {};
                    size_t p = 0; int k = 0; bool ok = true;
                    while (k < Tabele::LICZBA_PASM && p < vn && ok) {
                        bool minus = false;
                        if (v[p] == '-') { minus = true; ++p; }
                        size_t q = p;
                        while (q < vn && v[q] != ',') ++q;
                        uint64_t a = 0;
                        if (!parsuj_liczbe(v + p, q - p, 100, &a)) { ok = false; break; }
                        int db = minus ? -int(a) : int(a);
                        if (db < Tabele::MIN_DB) db = Tabele::MIN_DB;
                        if (db > Tabele::MAKS_DB) db = Tabele::MAKS_DB;
                        tmp[k++] = int8_t(db);
                        p = q + 1;
                    }
                    if (ok && k == Tabele::LICZBA_PASM) { for (int i = 0; i < k; ++i) pasma[i] = tmp[i]; ++rozpoznane; }
                }
            }
            pos = end + 1;
        }
        return rozpoznane;
    }
};

} // namespace Grajek
