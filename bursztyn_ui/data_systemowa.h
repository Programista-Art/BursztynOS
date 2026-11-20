#pragma once
/*
 * BursztynUI - data systemowa dla kalendarzy (pasek zadan, aplikacja Kalendarz).
 *
 * Jedyne zrodlo: BWS 9 w trybie BWS_CZAS_LOKALNY_V1 (jadro/bws/czas_lokalny.h) - czas lokalny wyliczony przez
 * sterownik RTC. Bez wlasnej strefy czasowej, bez czasu letniego i bez dat zastepczych: gdy jadro nie ma
 * poprawnej probki RTC, funkcja zwraca false i interfejs pokazuje "data niedostepna".
 */
#include "kalendarz.h"
#include "czas_lokalny.h"
#include "bursztyn_gui.h"

namespace BursztynUI {

/* Wywolanie systemowe jako parametr (testy hosta podstawiaja symulowane jadro). */
using FunkcjaBws = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

inline uint64_t bws_domyslne(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    return bws_wywolaj(n, a1, a2, a3, a4);
}

inline bool pobierz_czas_lokalny(BwsCzasLokalny* out, FunkcjaBws bws = bws_domyslne) {
    if (!out || !bws) return false;
    BwsCzasLokalny c{};
    if (bws(9, reinterpret_cast<uint64_t>(&c), BWS_CZAS_LOKALNY_V1, 0, 0) != 1 || !CzasLokalny::poprawny(c)) return false;
    *out = c;
    return true;
}

/* Dzisiejsza data jako DataKalendarza; false = brak daty (zakres komponentu 1900..2100 takze sprawdzany). */
inline bool pobierz_date_systemowa(DataKalendarza* out, FunkcjaBws bws = bws_domyslne, BwsCzasLokalny* pelny = nullptr) {
    BwsCzasLokalny c{};
    if (!out || !pobierz_czas_lokalny(&c, bws)) return false;
    const DataKalendarza d{c.rok, c.miesiac, c.dzien};
    if (!d.czy_poprawna()) return false;
    *out = d;
    if (pelny) *pelny = c;
    return true;
}

}  // namespace BursztynUI
