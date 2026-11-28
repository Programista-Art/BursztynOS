#include "umiejscowienie.h"

#include "arytmetyka.h"
#include "pamiec_podstawowa.h"

namespace brb {

BladUmiejscowienia wymagania_obrazu(const PlanLadowania& plan, const MetadaneWejscia& meta, WymaganiaObrazu* wy) {
    if (!wy) return BladUmiejscowienia::BrakDanych;
    brb_memset(wy, 0, sizeof(*wy));
    if (plan.liczba_segmentow == 0 || plan.liczba_segmentow > ELF_MAKS_SEGMENTOW) return BladUmiejscowienia::BrakDanych;
    if (plan.zakres_poczatek % ELF_STRONA != 0 || plan.zakres_koniec <= plan.zakres_poczatek ||
        plan.zakres_koniec % ELF_STRONA != 0)
        return BladUmiejscowienia::PlanNieciagly;
    if (plan.zakres_poczatek != meta.baza_wirtualna) return BladUmiejscowienia::BazaNiezgodna;
    uint64_t poprzedni_koniec = plan.zakres_poczatek;
    uint64_t wyrownanie = meta.wyrownanie_podkladu;
    if (!potega_dwojki(wyrownanie) || wyrownanie < ELF_STRONA || wyrownanie > UMIESZCZENIE_MAKS_WYROWNANIE)
        return BladUmiejscowienia::Wyrownanie;
    for (uint32_t i = 0; i < plan.liczba_segmentow; ++i) {
        const SegmentPlanu& s = plan.segmenty[i];
        uint64_t k;
        // Plan Fazy 3: posortowany po VA i tozsamosciowy; model ciagly wymaga tego jawnie.
        if (s.adres_fizyczny != s.adres_wirtualny || s.adres_wirtualny < poprzedni_koniec ||
            !dodaj_u64(s.adres_wirtualny, s.rozmiar_w_pamieci, &k) || k > plan.zakres_koniec)
            return BladUmiejscowienia::PlanNieciagly;
        poprzedni_koniec = k;
        // p_align > 4 KiB: kongruencja PA z VA modulo p_align wymaga wyrownania bazy do p_align
        // (baza_wirtualna jest wyrownana do strony; tu wymagamy, by byla wyrownana takze do p_align).
        if (s.wyrownanie > ELF_STRONA) {
            if (!potega_dwojki(s.wyrownanie) || s.wyrownanie > UMIESZCZENIE_MAKS_WYROWNANIE ||
                plan.zakres_poczatek % s.wyrownanie != 0)
                return BladUmiejscowienia::Wyrownanie;
            if (s.wyrownanie > wyrownanie) wyrownanie = s.wyrownanie;
        }
    }
    wy->baza_wirtualna = plan.zakres_poczatek;
    wy->rozmiar = plan.zakres_koniec - plan.zakres_poczatek;
    wy->strony = wy->rozmiar / ELF_STRONA;
    wy->wyrownanie = wyrownanie;
    if (!dodaj_u64(wy->strony, wyrownanie / ELF_STRONA - 1, &wy->strony_przydzialu)) return BladUmiejscowienia::Przepelnienie;
    return BladUmiejscowienia::Brak;
}

BladUmiejscowienia wybierz_baze(const WymaganiaObrazu& wym, uint64_t przydzial, uint64_t strony_przydzialu, uint64_t limit,
                                unsigned bity_fizyczne, WybraneUmiejscowienie* wy) {
    if (!wy) return BladUmiejscowienia::BrakDanych;
    brb_memset(wy, 0, sizeof(*wy));
    if (wym.strony == 0 || wym.rozmiar != wym.strony * ELF_STRONA || !potega_dwojki(wym.wyrownanie) ||
        wym.wyrownanie < ELF_STRONA)
        return BladUmiejscowienia::BrakDanych;
    if (strony_przydzialu == 0 || przydzial % ELF_STRONA != 0 || przydzial == 0) return BladUmiejscowienia::PrzydzialNiewyrownany;
    uint64_t bajty, koniec_przydzialu, baza, koniec_obrazu;
    if (!mnoz_u64(strony_przydzialu, ELF_STRONA, &bajty) || !dodaj_u64(przydzial, bajty, &koniec_przydzialu) ||
        !wyrownaj_w_gore(przydzial, wym.wyrownanie, &baza) || !dodaj_u64(baza, wym.rozmiar, &koniec_obrazu))
        return BladUmiejscowienia::Przepelnienie;
    if (koniec_obrazu > koniec_przydzialu) return BladUmiejscowienia::ZaMalyPrzydzial;
    if (koniec_obrazu > limit) return BladUmiejscowienia::PozaLimitem;
    if (bity_fizyczne < 36 || bity_fizyczne > 52 || koniec_obrazu > (1ull << bity_fizyczne))
        return BladUmiejscowienia::PozaSzerokoscia;
    wy->przydzial = przydzial;
    wy->strony_przydzialu = strony_przydzialu;
    wy->baza = baza;
    wy->dopelnienie_przed = baza - przydzial;
    wy->dopelnienie_po = koniec_przydzialu - koniec_obrazu;
    return BladUmiejscowienia::Brak;
}

const char* opis_bledu_umiejscowienia(BladUmiejscowienia b) {
    switch (b) {
        case BladUmiejscowienia::Brak: return "brak bledu";
        case BladUmiejscowienia::BrakDanych: return "brak danych umieszczenia";
        case BladUmiejscowienia::BazaNiezgodna: return "baza wirtualna planu niezgodna z metadanymi";
        case BladUmiejscowienia::PlanNieciagly: return "plan niezgodny z modelem ciaglego podkladu";
        case BladUmiejscowienia::Wyrownanie: return "niepoprawne wymaganie wyrownania";
        case BladUmiejscowienia::Przepelnienie: return "przepelnienie arytmetyki umieszczenia";
        case BladUmiejscowienia::PrzydzialNiewyrownany: return "przydzial firmware niewyrownany albo pusty";
        case BladUmiejscowienia::ZaMalyPrzydzial: return "obraz z wyrownaniem nie miesci sie w przydziale";
        case BladUmiejscowienia::PozaLimitem: return "obraz powyzej tymczasowego limitu 4 GiB";
        case BladUmiejscowienia::PozaSzerokoscia: return "obraz poza szerokoscia fizyczna procesora";
    }
    return "nieznany blad umieszczenia";
}

}  // namespace brb
