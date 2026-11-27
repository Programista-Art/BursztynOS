#include "mapa_pamieci.h"

#include "arytmetyka.h"
#include "pamiec_podstawowa.h"

namespace brb {

namespace {
constexpr uint64_t MD_TYPE = 0, MD_PHYS = 8, MD_PAGES = 24, MD_ATTR = 32;
}

KlasaPamieci klasyfikuj_typ(uint32_t typ) {
    switch (typ) {
        case 7: return KlasaPamieci::Wolna;
        case 3: case 4: return KlasaPamieci::UslugiRozruchowe;
        case 1: case 2: return KlasaPamieci::Ladowarka;
        case 5: case 6: case 13: return KlasaPamieci::UslugiCzasuWykonania;
        case 9: return KlasaPamieci::AcpiOdzyskiwalna;
        case 10: return KlasaPamieci::AcpiNvs;
        case 11: case 12: return KlasaPamieci::Mmio;
        case 0: return KlasaPamieci::Zarezerwowana;
        case 8: return KlasaPamieci::Uszkodzona;
        case 14: return KlasaPamieci::Trwala;
        case 15: return KlasaPamieci::Nieprzyjeta;
        default: return KlasaPamieci::Nieznana;
    }
}

const char* nazwa_klasy(KlasaPamieci k) {
    switch (k) {
        case KlasaPamieci::Wolna: return "wolna";
        case KlasaPamieci::UslugiRozruchowe: return "uslugi-rozruchowe";
        case KlasaPamieci::Ladowarka: return "ladowarka";
        case KlasaPamieci::UslugiCzasuWykonania: return "runtime";
        case KlasaPamieci::AcpiOdzyskiwalna: return "acpi-odzyskiwalna";
        case KlasaPamieci::AcpiNvs: return "acpi-nvs";
        case KlasaPamieci::Mmio: return "mmio";
        case KlasaPamieci::Zarezerwowana: return "zarezerwowana";
        case KlasaPamieci::Uszkodzona: return "uszkodzona";
        case KlasaPamieci::Trwala: return "trwala";
        case KlasaPamieci::Nieprzyjeta: return "nieprzyjeta";
        case KlasaPamieci::Nieznana: return "nieznana";
    }
    return "nieznana";
}

const char* opis_bledu_mapy(BladMapy b) {
    switch (b) {
        case BladMapy::Brak: return "brak bledu";
        case BladMapy::BrakDanych: return "brak danych mapy";
        case BladMapy::ZlyRozmiarDeskryptora: return "niepoprawny rozmiar deskryptora";
        case BladMapy::ZlaWersja: return "nieobslugiwana wersja deskryptora";
        case BladMapy::RozmiarNiewielokrotny: return "rozmiar mapy nie jest wielokrotnoscia deskryptora";
        case BladMapy::PustaMapa: return "pusta mapa pamieci";
        case BladMapy::ZaDuzoWpisow: return "za duzo deskryptorow";
        case BladMapy::NiewyrownanyAdres: return "adres deskryptora niewyrownany do 4 KiB";
        case BladMapy::ZeroStron: return "deskryptor bez stron";
        case BladMapy::PrzepelnienieZakresu: return "przepelnienie zakresu deskryptora";
        case BladMapy::NakladanieWpisow: return "deskryptory nakladaja sie";
    }
    return "nieznany blad mapy";
}

WpisMapy wpis(const WidokMapy& w, uint32_t i) {
    const uint8_t* d = w.dane + uint64_t(i) * w.rozmiar_deskryptora;   // i < liczba: w granicach (walidacja)
    WpisMapy e;
    e.typ = le32(d + MD_TYPE);
    e.klasa = klasyfikuj_typ(e.typ);
    e.poczatek = le64(d + MD_PHYS);
    e.strony = le64(d + MD_PAGES);
    e.atrybuty = le64(d + MD_ATTR);
    e.koniec = e.poczatek + e.strony * MAPA_STRONA;   // bez przepelnienia: sprawdzone w waliduj_mape
    return e;
}

BladMapy waliduj_mape(const uint8_t* dane, uint64_t rozmiar_mapy, uint64_t krok, uint32_t wersja, WidokMapy* wy,
                      StatystykiMapy* st) {
    if (wy) *wy = WidokMapy{nullptr, 0, 0};
    if (st) brb_memset(st, 0, sizeof(*st));
    if (!wy || (!dane && rozmiar_mapy)) return BladMapy::BrakDanych;
    if (krok < MAPA_MIN_ROZMIAR_DESKRYPTORA || krok > MAPA_MAKS_ROZMIAR_DESKRYPTORA || krok % 8 != 0)
        return BladMapy::ZlyRozmiarDeskryptora;
    if (wersja != MAPA_WERSJA_DESKRYPTORA) return BladMapy::ZlaWersja;
    if (rozmiar_mapy % krok != 0) return BladMapy::RozmiarNiewielokrotny;
    const uint64_t liczba = rozmiar_mapy / krok;
    if (liczba == 0) return BladMapy::PustaMapa;
    if (liczba > MAPA_MAKS_WPISOW) return BladMapy::ZaDuzoWpisow;
    for (uint64_t i = 0; i < liczba; ++i) {
        const uint8_t* d = dane + i * krok;
        const uint64_t poczatek = le64(d + MD_PHYS), strony = le64(d + MD_PAGES);
        if (poczatek % MAPA_STRONA != 0) return BladMapy::NiewyrownanyAdres;
        if (strony == 0) return BladMapy::ZeroStron;
        uint64_t bajty, koniec;
        if (!mnoz_u64(strony, MAPA_STRONA, &bajty) || !dodaj_u64(poczatek, bajty, &koniec))
            return BladMapy::PrzepelnienieZakresu;
    }
    // Nakladanie: O(n^2) przy n <= 4096 (typowo ~100); mapa nie musi byc posortowana.
    for (uint64_t i = 0; i < liczba; ++i) {
        const uint8_t* a = dane + i * krok;
        const uint64_t a0 = le64(a + MD_PHYS), a1 = a0 + le64(a + MD_PAGES) * MAPA_STRONA;
        for (uint64_t j = i + 1; j < liczba; ++j) {
            const uint8_t* b = dane + j * krok;
            const uint64_t b0 = le64(b + MD_PHYS), b1 = b0 + le64(b + MD_PAGES) * MAPA_STRONA;
            if (a0 < b1 && b0 < a1) return BladMapy::NakladanieWpisow;
        }
    }
    *wy = WidokMapy{dane, krok, uint32_t(liczba)};
    if (st) {
        for (uint32_t i = 0; i < wy->liczba; ++i) {
            const WpisMapy e = wpis(*wy, i);
            uint64_t& licznik = st->strony[uint8_t(e.klasa)];
            licznik = licznik > ~0ull - e.strony ? ~0ull : licznik + e.strony;
            if (e.koniec > st->najwyzszy_adres) st->najwyzszy_adres = e.koniec;
            if (e.koniec > 0x100000000ull) ++st->liczba_powyzej_4gib;
        }
    }
    return BladMapy::Brak;
}

uint64_t wpisy_w_zakresie(const WidokMapy& w, uint64_t poczatek, uint64_t koniec,
                          void (*zwrot)(void*, const WpisMapy&), void* ctx) {
    if (koniec <= poczatek) return 0;
    uint64_t pokryte = 0;
    for (uint32_t i = 0; i < w.liczba; ++i) {
        const WpisMapy e = wpis(w, i);
        if (e.koniec <= poczatek || e.poczatek >= koniec) continue;
        const uint64_t p = e.poczatek > poczatek ? e.poczatek : poczatek;
        const uint64_t k = e.koniec < koniec ? e.koniec : koniec;
        pokryte += k - p;   // wpisy rozlaczne (walidacja), wiec suma <= dlugosc zakresu
        if (zwrot) zwrot(ctx, e);
    }
    return (koniec - poczatek) - pokryte;
}

}  // namespace brb
