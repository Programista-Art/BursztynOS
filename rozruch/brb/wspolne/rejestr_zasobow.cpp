#include "rejestr_zasobow.h"

#include "arytmetyka.h"
#include "pamiec_podstawowa.h"

namespace brb {

namespace {
constexpr uint64_t STRONA = 0x1000, KONIEC_DOLNEJ = 0x0000800000000000ull;

bool koniec(uint64_t adres, uint64_t strony, uint64_t* k) {
    uint64_t b;
    return strony != 0 && adres % STRONA == 0 && mnoz_u64(strony, STRONA, &b) && dodaj_u64(adres, b, k) && *k <= KONIEC_DOLNEJ;
}
}  // namespace

void rejestr_inicjalizuj(RejestrZasobow* r) {
    if (r) brb_memset(r, 0, sizeof(*r));
}

BladRejestru rejestr_sprawdz(const RejestrZasobow& r, uint64_t adres, uint64_t strony, int32_t* kolizja) {
    if (kolizja) *kolizja = -1;
    uint64_t k;
    if (!koniec(adres, strony, &k)) return BladRejestru::ZakresNiepoprawny;
    for (uint32_t i = 0; i < r.liczba && i < REJESTR_MAKS; ++i) {
        const WpisZasobu& w = r.wpisy[i];
        if (!w.wlasny && !w.chroniony) continue;   // zwolnione nie blokuja
        uint64_t wk;
        if (!koniec(w.adres, w.strony, &wk)) return BladRejestru::ZakresNiepoprawny;
        if (adres < wk && w.adres < k) { if (kolizja) *kolizja = int32_t(i); return BladRejestru::Nakladanie; }
    }
    return BladRejestru::Brak;
}

BladRejestru rejestr_dodaj(RejestrZasobow* r, uint64_t adres, uint64_t strony, RodzajZasobu rodzaj, bool chroniony,
                           int32_t* kolizja) {
    if (!r) return BladRejestru::ZakresNiepoprawny;
    if (r->liczba >= REJESTR_MAKS) return BladRejestru::Pelny;
    const BladRejestru b = rejestr_sprawdz(*r, adres, strony, kolizja);
    if (b != BladRejestru::Brak) return b;
    r->wpisy[r->liczba++] = WpisZasobu{adres, strony, rodzaj, chroniony, !chroniony, false};
    return BladRejestru::Brak;
}

bool rejestr_posiada(const RejestrZasobow& r, uint64_t adres, uint64_t dlugosc, RodzajZasobu rodzaj) {
    uint64_t k;
    if (dlugosc == 0 || !dodaj_u64(adres, dlugosc, &k)) return false;
    for (uint32_t i = 0; i < r.liczba && i < REJESTR_MAKS; ++i) {
        const WpisZasobu& w = r.wpisy[i];
        uint64_t wk;
        if (!w.wlasny || w.rodzaj != rodzaj || !koniec(w.adres, w.strony, &wk)) continue;
        if (adres >= w.adres && k <= wk) return true;
    }
    return false;
}

uint32_t rejestr_zwolnij_wszystko(RejestrZasobow* r, bool (*zwolnij)(void*, uint64_t, uint64_t), void* ctx) {
    if (!r || !zwolnij) return 0;
    uint32_t bledy = 0;
    for (uint32_t n = r->liczba; n > 0; --n) {
        WpisZasobu& w = r->wpisy[n - 1];
        if (!w.wlasny || w.chroniony) continue;
        w.wlasny = false;   // przed wywolaniem: zadna sciezka nie zwolni go drugi raz
        if (zwolnij(ctx, w.adres, w.strony)) { ++r->zwolnione; continue; }
        w.porzucony = true;
        ++bledy;
        ++r->bledy_zwalniania;
    }
    return bledy;
}

uint32_t rejestr_wlasne(const RejestrZasobow& r) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < r.liczba && i < REJESTR_MAKS; ++i) if (r.wpisy[i].wlasny && !r.wpisy[i].chroniony) ++n;
    return n;
}

const char* nazwa_zasobu(RodzajZasobu r) {
    switch (r) {
        case RodzajZasobu::Nieznany: return "nieznany";
        case RodzajZasobu::ObrazBrb: return "obraz-BRB";
        case RodzajZasobu::BuforZrodla: return "bufor-zrodla";
        case RodzajZasobu::PodkladJadra: return "podklad-jadra";
        case RodzajZasobu::TabliceStron: return "tablice-stron";
        case RodzajZasobu::StosPrzejscia: return "stos-przejscia";
        case RodzajZasobu::KodPrzejscia: return "kod-przejscia";
        case RodzajZasobu::InformacjeRozruchu: return "informacje-rozruchu";
        case RodzajZasobu::TestowyStraznik: return "testowy-straznik";
    }
    return "?";
}

}  // namespace brb
