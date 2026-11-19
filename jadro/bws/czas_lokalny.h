#pragma once
/*
 * Bursztyn OS - data i czas lokalny dla Ring 3 (addytywny tryb BWS 9).
 *
 * BWS 9 (BWS_CZAS_RTC) historycznie zapisuje do bufora Ring 3 tylko tekst "HH:MM:SS" (arg2 == 0 - bez zmian).
 * Z arg2 == BWS_CZAS_LOKALNY_V1 zapisuje strukture BwsCzasLokalny z data. Zrodlo jest to samo: sterownik RTC
 * (pobierz_czas_rtc_bezpiecznie), ktory sam przelicza zegar CMOS na czas lokalny (domyslnie CMOS = UTC ->
 * CET/CEST, zob. sterowniki/czas/zegar-rtc.h). Aplikacje nie licza strefy ani czasu letniego - pokazuja to, co
 * podaje jadro. Wywolanie nie wymaga zadnego prawa (jak dotychczasowy BWS 9) i zwraca tylko liczby.
 *
 * Wynik: 1 = struktura zapisana i data pochodzi z prawdziwej probki RTC; 0 = zly bufor, nieznana wersja (arg2) albo
 * brak jakiejkolwiek poprawnej probki RTC (wtedy aplikacja pokazuje "data niedostepna" zamiast 2000-01-01).
 * Przy wyniku 0 bufor nie jest zmieniany: zakres i mapowanie calego bufora sa sprawdzane przed jedna kopia
 * struktury zbudowanej w pamieci jadra (zerowana - bez danych ze stosu jadra).
 */
#include <stdint.h>

inline constexpr uint64_t BWS_CZAS_LOKALNY_V1 = 0x314C435AULL;   // "ZCL1"
inline constexpr uint32_t BWS_CZAS_WERSJA = 1U;

/* Flagi */
inline constexpr uint32_t BWS_CZAS_PROBKA_RTC = 1U << 0;    // data/godzina z prawdziwej probki CMOS
inline constexpr uint32_t BWS_CZAS_STREFA_CET = 1U << 1;    // sterownik przelicza CMOS (UTC) na CET/CEST
inline constexpr uint32_t BWS_CZAS_CMOS_LOKALNY = 1U << 2;  // CMOS traktowany jako czas lokalny (bez przeliczania)

struct BwsCzasLokalny {
    uint32_t wersja;          // BWS_CZAS_WERSJA
    uint32_t rozmiar;         // sizeof(BwsCzasLokalny)
    uint16_t rok;             // pelny rok
    uint8_t miesiac;          // 1..12
    uint8_t dzien;            // 1..31 (poprawny dla miesiaca)
    uint8_t godzina, minuta, sekunda;
    uint8_t dzien_tygodnia;   // 0 = poniedzialek ... 6 = niedziela
    uint32_t flagi;
    uint32_t zarezerwowane;
};
static_assert(sizeof(BwsCzasLokalny) == 24, "ABI BWS 9 (czas lokalny V1)");

/* BWS 9: arg2 == 0 = tekst "HH:MM:SS" (zgodnosc), BWS_CZAS_LOKALNY_V1 = struktura; inne wartosci = 0 bez zapisu. */
inline constexpr bool czas_lokalny_tryb_tekstowy(uint64_t arg2) { return arg2 == 0; }

namespace CzasLokalny {

inline constexpr bool przestepny(int r) { return (r % 4 == 0 && r % 100 != 0) || r % 400 == 0; }
inline constexpr int dni_w_miesiacu(int r, int m) {
    return m < 1 || m > 12 ? 0 : m == 2 ? (przestepny(r) ? 29 : 28) : (m == 4 || m == 6 || m == 9 || m == 11) ? 30 : 31;
}
/* Gregorianski dzien tygodnia (Sakamoto), 0 = poniedzialek; -1 dla niepoprawnej daty. */
inline constexpr int dzien_tygodnia(int r, int m, int d) {
    if (r < 1 || m < 1 || m > 12 || d < 1 || d > dni_w_miesiacu(r, m)) return -1;
    constexpr int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    const int y = r - (m < 3);
    return ((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7 + 6) % 7;
}
inline constexpr bool poprawny(const BwsCzasLokalny& c) {
    return c.wersja == BWS_CZAS_WERSJA && c.rozmiar == sizeof(BwsCzasLokalny) && (c.flagi & BWS_CZAS_PROBKA_RTC) &&
           c.rok >= 1 && c.miesiac >= 1 && c.miesiac <= 12 && c.dzien >= 1 && c.dzien <= dni_w_miesiacu(c.rok, c.miesiac) &&
           c.godzina <= 23 && c.minuta <= 59 && c.sekunda <= 59 &&
           c.dzien_tygodnia == dzien_tygodnia(c.rok, c.miesiac, c.dzien);
}
/* Wypelnia strukture z pol RTC (jadro i testy); false = pola nie tworza poprawnej daty. */
inline bool wypelnij(BwsCzasLokalny& o, uint32_t rok, uint8_t mies, uint8_t dzien, uint8_t g, uint8_t min, uint8_t s,
                     uint32_t flagi_strefy) {
    o = BwsCzasLokalny{};
    o.wersja = BWS_CZAS_WERSJA; o.rozmiar = sizeof(BwsCzasLokalny);
    if (rok < 1 || rok > 9999 || dzien_tygodnia(int(rok), mies, dzien) < 0 || g > 23 || min > 59 || s > 59) return false;
    o.rok = uint16_t(rok); o.miesiac = mies; o.dzien = dzien; o.godzina = g; o.minuta = min; o.sekunda = s;
    o.dzien_tygodnia = uint8_t(dzien_tygodnia(int(rok), mies, dzien));
    o.flagi = BWS_CZAS_PROBKA_RTC | flagi_strefy;
    return true;
}

}  // namespace CzasLokalny
