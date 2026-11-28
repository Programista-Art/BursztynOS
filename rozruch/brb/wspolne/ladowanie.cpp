#include "ladowanie.h"

#include "arytmetyka.h"
#include "pamiec_podstawowa.h"

namespace brb {

namespace {

bool nakladaja_sie(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) { return a0 < b1 && b0 < a1; }

// [poczatek, koniec) zakresu stron; false przy przepelnieniu.
bool koniec_zakresu(const ZakresStron& z, uint64_t* koniec) {
    uint64_t bajty;
    return mnoz_u64(z.strony, ELF_STRONA, &bajty) && dodaj_u64(z.poczatek, bajty, koniec);
}

BladLadowania sprawdz_zakresy(const ZakresyPrzydzialu& p) {
    if (p.liczba == 0) return BladLadowania::PustyPlan;
    if (p.liczba > LADOWANIE_MAKS_ZAKRESOW) return BladLadowania::ZaDuzoZakresow;
    for (uint32_t i = 0; i < p.liczba; ++i) {
        const ZakresStron& z = p.zakresy[i];
        uint64_t k;
        if (z.strony == 0 || z.poczatek % ELF_STRONA != 0 || !koniec_zakresu(z, &k)) return BladLadowania::ZleZakresy;
        for (uint32_t j = 0; j < i; ++j) {
            uint64_t kj = 0;
            if (!koniec_zakresu(p.zakresy[j], &kj)) return BladLadowania::ZleZakresy;
            if (nakladaja_sie(z.poczatek, k, p.zakresy[j].poczatek, kj)) return BladLadowania::ZakresyNakladajaSie;
        }
    }
    return BladLadowania::Brak;
}

// Czy [a, a + n) lezy w calosci w jednym posiadanym zakresie?
bool w_posiadanym(const ZakresyPrzydzialu& p, uint64_t a, uint64_t n) {
    uint64_t k;
    if (!dodaj_u64(a, n, &k)) return false;
    for (uint32_t i = 0; i < p.liczba; ++i) {
        uint64_t zk;
        if (!koniec_zakresu(p.zakresy[i], &zk)) return false;
        if (a >= p.zakresy[i].poczatek && k <= zk) return true;
    }
    return false;
}

bool same_zera(const uint8_t* p, uint64_t n, uint64_t* pierwszy) {
    for (uint64_t i = 0; i < n; ++i) {
        if (p[i] != 0) { *pierwszy = i; return false; }
    }
    return true;
}

// Indeksy segmentow posortowane wg adresu fizycznego (n <= 16).
void sortuj_fizycznie(const PlanLadowania& plan, uint32_t* idx) {
    for (uint32_t i = 0; i < plan.liczba_segmentow; ++i) {
        uint32_t j = i;
        while (j > 0 && plan.segmenty[idx[j - 1]].adres_fizyczny > plan.segmenty[i].adres_fizyczny) { idx[j] = idx[j - 1]; --j; }
        idx[j] = i;
    }
}

// Porownanie planow pole po polu. NIE memcmp: struktury maja bajty dopelnienia (np. po bool w NaglowekMb2),
// ktorych zawartosc po przypisaniu struktury jest nieokreslona - memcmp dawalby falszywe niezgodnosci.
bool segmenty_rowne(const SegmentPlanu& a, const SegmentPlanu& b) {
    return a.indeks_ph == b.indeks_ph && a.prawa == b.prawa && a.przesuniecie_pliku == b.przesuniecie_pliku &&
           a.adres_fizyczny == b.adres_fizyczny && a.adres_wirtualny == b.adres_wirtualny &&
           a.bajty_z_pliku == b.bajty_z_pliku && a.rozmiar_w_pamieci == b.rozmiar_w_pamieci &&
           a.bajty_zerowane == b.bajty_zerowane && a.wyrownanie == b.wyrownanie &&
           a.strona_poczatek == b.strona_poczatek && a.strona_koniec == b.strona_koniec && a.liczba_stron == b.liczba_stron;
}

bool plany_rowne(const PlanLadowania& a, const PlanLadowania& b) {
    if (a.liczba_segmentow != b.liczba_segmentow || a.liczba_segmentow > ELF_MAKS_SEGMENTOW) return false;
    for (uint32_t i = 0; i < a.liczba_segmentow; ++i)
        if (!segmenty_rowne(a.segmenty[i], b.segmenty[i])) return false;
    return a.rozmiar_pliku == b.rozmiar_pliku && a.wejscie_wirtualne == b.wejscie_wirtualne &&
           a.wejscie_fizyczne == b.wejscie_fizyczne && a.segment_wejscia == b.segment_wejscia &&
           a.zakres_poczatek == b.zakres_poczatek && a.zakres_koniec == b.zakres_koniec &&
           a.pamiec_stron == b.pamiec_stron && a.bajty_z_pliku == b.bajty_z_pliku &&
           a.bajty_zerowane == b.bajty_zerowane && a.mb2.obecny == b.mb2.obecny &&
           a.mb2.przesuniecie_pliku == b.mb2.przesuniecie_pliku && a.mb2.dlugosc == b.mb2.dlugosc &&
           a.mb2.architektura == b.mb2.architektura && a.mb2.liczba_tagow == b.mb2.liczba_tagow &&
           a.mb2.segment == b.mb2.segment;
}

}  // namespace

const char* opis_bledu_ladowania(BladLadowania b) {
    switch (b) {
        case BladLadowania::Brak: return "brak bledu";
        case BladLadowania::BrakDanych: return "brak danych wejsciowych ladowania";
        case BladLadowania::PustyPlan: return "pusty plan ladowania";
        case BladLadowania::NiespojnyPlan: return "plan niezgodny z ponownie zwalidowanym plikiem";
        case BladLadowania::ZleZakresy: return "niepoprawny zakres stron";
        case BladLadowania::ZakresyNakladajaSie: return "zakresy stron nakladaja sie";
        case BladLadowania::ZaDuzoZakresow: return "za duzo zakresow stron";
        case BladLadowania::ZrodloNiezgodne: return "bufor zrodla niezgodny z planem";
        case BladLadowania::ZrodloPozaBuforem: return "dane segmentu poza buforem zrodla";
        case BladLadowania::CelPozaPrzydzialem: return "cel segmentu poza posiadana pamiecia";
        case BladLadowania::ZrodloNakladaSieNaCel: return "bufor zrodla nachodzi na pamiec docelowa";
        case BladLadowania::PrzepelnienieArytmetyki: return "przepelnienie arytmetyki ladowania";
        case BladLadowania::BrakDostepuDoPamieci: return "brak dostepu do posiadanej pamieci";
        case BladLadowania::WeryfikacjaDanych: return "dane w pamieci niezgodne z plikiem";
        case BladLadowania::WeryfikacjaBss: return "obszar BSS nie jest wyzerowany";
        case BladLadowania::WeryfikacjaWypelnienia: return "wypelnienie stron nie jest wyzerowane";
        case BladLadowania::WejscieNiewykonywalne: return "punkt wejscia poza segmentem wykonywalnym";
        case BladLadowania::PrzesuniecieNiepoprawne: return "przesuniecie planu na baze fizyczna niepoprawne";
    }
    return "nieznany blad ladowania";
}

BladLadowania zbuduj_zakresy_przydzialu(const PlanLadowania& plan, ZakresyPrzydzialu* wy) {
    if (!wy) return BladLadowania::BrakDanych;
    brb_memset(wy, 0, sizeof(*wy));
    if (plan.liczba_segmentow == 0) return BladLadowania::PustyPlan;
    if (plan.liczba_segmentow > ELF_MAKS_SEGMENTOW) return BladLadowania::ZaDuzoZakresow;
    uint32_t idx[ELF_MAKS_SEGMENTOW];
    sortuj_fizycznie(plan, idx);
    for (uint32_t n = 0; n < plan.liczba_segmentow; ++n) {
        const SegmentPlanu& s = plan.segmenty[idx[n]];
        if (s.strona_poczatek % ELF_STRONA || s.strona_koniec % ELF_STRONA || s.strona_koniec <= s.strona_poczatek)
            return BladLadowania::ZleZakresy;
        if (wy->liczba > 0) {
            ZakresStron& o = wy->zakresy[wy->liczba - 1];
            uint64_t ok;
            if (!koniec_zakresu(o, &ok)) return BladLadowania::PrzepelnienieArytmetyki;
            if (s.strona_poczatek < ok) {   // wspolna strona (dozwolona przez Faze 3 tylko przy tych samych prawach)
                if (s.strona_koniec > ok) o.strony += (s.strona_koniec - ok) / ELF_STRONA;
                continue;
            }
        }
        wy->zakresy[wy->liczba++] = ZakresStron{s.strona_poczatek, (s.strona_koniec - s.strona_poczatek) / ELF_STRONA};
    }
    for (uint32_t i = 0; i < wy->liczba; ++i) {
        if (!dodaj_u64(wy->strony_lacznie, wy->zakresy[i].strony, &wy->strony_lacznie)) return BladLadowania::PrzepelnienieArytmetyki;
    }
    return sprawdz_zakresy(*wy);
}

BladLadowania przesun_plan(const PlanLadowania& w, uint64_t baza, PlanLadowania* wy) {
    if (!wy) return BladLadowania::BrakDanych;
    brb_memset(wy, 0, sizeof(*wy));
    if (w.liczba_segmentow == 0) return BladLadowania::PustyPlan;
    if (w.liczba_segmentow > ELF_MAKS_SEGMENTOW) return BladLadowania::ZaDuzoZakresow;
    if (baza == 0 || baza % ELF_STRONA != 0 || w.zakres_poczatek % ELF_STRONA != 0 || w.zakres_koniec <= w.zakres_poczatek)
        return BladLadowania::PrzesuniecieNiepoprawne;
    const uint64_t va0 = w.zakres_poczatek;   // najnizsza strona (polityka: p_paddr == p_vaddr)
    uint64_t rozmiar = w.zakres_koniec - va0, koniec_bazy;
    if (!dodaj_u64(baza, rozmiar, &koniec_bazy)) return BladLadowania::PrzesuniecieNiepoprawne;
    *wy = w;
    auto przesun = [&](uint64_t va, uint64_t dlugosc, uint64_t* pa) -> bool {
        uint64_t k;
        if (va < va0 || !dodaj_u64(va, dlugosc, &k) || k > w.zakres_koniec) return false;
        return dodaj_u64(baza, va - va0, pa);
    };
    for (uint32_t i = 0; i < w.liczba_segmentow; ++i) {
        const SegmentPlanu& s = w.segmenty[i];
        SegmentPlanu& d = wy->segmenty[i];
        // Model ciagly: wzor musi byc tozsamosciowy (Faza 3), a strony segmentu - w zakresie planu.
        if (s.adres_fizyczny != s.adres_wirtualny || s.strona_koniec < s.strona_poczatek ||
            !przesun(s.adres_wirtualny, s.rozmiar_w_pamieci, &d.adres_fizyczny) ||
            !przesun(s.strona_poczatek, s.strona_koniec - s.strona_poczatek, &d.strona_poczatek) ||
            !dodaj_u64(d.strona_poczatek, s.strona_koniec - s.strona_poczatek, &d.strona_koniec))
            return BladLadowania::PrzesuniecieNiepoprawne;
    }
    if (w.segment_wejscia >= w.liczba_segmentow) return BladLadowania::WejscieNiewykonywalne;
    if (!przesun(w.wejscie_wirtualne, 1, &wy->wejscie_fizyczne)) return BladLadowania::PrzesuniecieNiepoprawne;
    wy->zakres_poczatek = baza;
    wy->zakres_koniec = koniec_bazy;
    return BladLadowania::Brak;
}

BladLadowania sprawdz_przed_kopiowaniem(const PlanLadowania& plan, const PolitykaElf& pol, const ZrodloJadra& z,
                                        const ZakresyPrzydzialu& p, const DostepFizyczny& pamiec,
                                        const uint64_t* baza_przesuniecia) {
    if (!z.dane || !pamiec.wskaznik) return BladLadowania::BrakDanych;
    if (plan.liczba_segmentow == 0) return BladLadowania::PustyPlan;
    if (plan.liczba_segmentow > ELF_MAKS_SEGMENTOW) return BladLadowania::NiespojnyPlan;
    const BladLadowania bz = sprawdz_zakresy(p);
    if (bz != BladLadowania::Brak) return bz;
    if (z.rozmiar != plan.rozmiar_pliku || z.rozmiar > z.rozmiar_bufora) return BladLadowania::ZrodloNiezgodne;
    // Ponowne wyliczenie planu z TYCH SAMYCH bajtow (jedyny parser): wykrywa zmiane planu albo bufora.
    PlanLadowania ponownie;
    if (zbuduj_plan_ladowania(z.dane, z.rozmiar, pol, &ponownie).blad != BladElf::Brak) return BladLadowania::NiespojnyPlan;
    if (baza_przesuniecia) {   // R2: to samo przesuniecie co plan docelowy (z tych samych bajtow)
        PlanLadowania przesuniety;
        if (przesun_plan(ponownie, *baza_przesuniecia, &przesuniety) != BladLadowania::Brak) return BladLadowania::NiespojnyPlan;
        ponownie = przesuniety;
    }
    if (!plany_rowne(ponownie, plan)) return BladLadowania::NiespojnyPlan;
    if (plan.segment_wejscia >= plan.liczba_segmentow || plan.liczba_segmentow > ELF_MAKS_SEGMENTOW ||
        !(plan.segmenty[plan.segment_wejscia].prawa & ELF_PF_X))
        return BladLadowania::WejscieNiewykonywalne;
    uint64_t z_koniec;
    if (!dodaj_u64(z.adres_fizyczny, z.rozmiar_bufora, &z_koniec)) return BladLadowania::PrzepelnienieArytmetyki;
    for (uint32_t i = 0; i < p.liczba; ++i) {
        uint64_t k = 0;
        if (!koniec_zakresu(p.zakresy[i], &k)) return BladLadowania::ZleZakresy;
        if (nakladaja_sie(z.adres_fizyczny, z_koniec, p.zakresy[i].poczatek, k)) return BladLadowania::ZrodloNakladaSieNaCel;
        if (!pamiec.wskaznik(pamiec.kontekst, p.zakresy[i].poczatek, k - p.zakresy[i].poczatek))
            return BladLadowania::BrakDostepuDoPamieci;
    }
    for (uint32_t i = 0; i < plan.liczba_segmentow; ++i) {
        const SegmentPlanu& s = plan.segmenty[i];
        if (!zakres_w_buforze(s.przesuniecie_pliku, s.bajty_z_pliku, z.rozmiar)) return BladLadowania::ZrodloPozaBuforem;
        if (!w_posiadanym(p, s.adres_fizyczny, s.rozmiar_w_pamieci)) return BladLadowania::CelPozaPrzydzialem;
    }
    return BladLadowania::Brak;
}

BladLadowania zaladuj_segmenty(const PlanLadowania& plan, const PolitykaElf& pol, const ZrodloJadra& z,
                               const ZakresyPrzydzialu& p, const DostepFizyczny& pamiec, RaportLadowania* r,
                               const uint64_t* baza_przesuniecia) {
    if (r) { brb_memset(r, 0, sizeof(*r)); r->segment_bledu = -1; }
    const BladLadowania b = sprawdz_przed_kopiowaniem(plan, pol, z, p, pamiec, baza_przesuniecia);
    if (b != BladLadowania::Brak) return b;
    // 1. Zerowanie calych posiadanych stron (wypelnienie, BSS, wspolne strony) - przed jakimkolwiek kopiowaniem.
    for (uint32_t i = 0; i < p.liczba; ++i) {
        const uint64_t dl = p.zakresy[i].strony * ELF_STRONA;
        uint8_t* cel = pamiec.wskaznik(pamiec.kontekst, p.zakresy[i].poczatek, dl);
        if (!cel) return BladLadowania::BrakDostepuDoPamieci;
        brb_memset(cel, 0, dl);
    }
    // 2. Kopiowanie dokladnie p_filesz bajtow i jawne zerowanie [p_filesz, p_memsz).
    for (uint32_t i = 0; i < plan.liczba_segmentow; ++i) {
        const SegmentPlanu& s = plan.segmenty[i];
        uint8_t* cel = pamiec.wskaznik(pamiec.kontekst, s.adres_fizyczny, s.rozmiar_w_pamieci);
        if (!cel) { if (r) r->segment_bledu = int32_t(i); return BladLadowania::BrakDostepuDoPamieci; }
        brb_memcpy(cel, z.dane + s.przesuniecie_pliku, s.bajty_z_pliku);
        brb_memset(cel + s.bajty_z_pliku, 0, s.bajty_zerowane);
        if (r) {
            r->segmenty[i] = RaportSegmentu{s.adres_fizyczny, s.bajty_z_pliku, s.rozmiar_w_pamieci, s.bajty_zerowane, false, false};
            r->bajty_skopiowane += s.bajty_z_pliku;
            r->bajty_bss += s.bajty_zerowane;
        }
    }
    if (r) r->liczba = plan.liczba_segmentow;
    return BladLadowania::Brak;
}

BladLadowania weryfikuj_zaladowany_obraz(const PlanLadowania& plan, const ZrodloJadra& z, const ZakresyPrzydzialu& p,
                                         const DostepFizyczny& pamiec, RaportLadowania* r) {
    if (!r || !z.dane || !pamiec.wskaznik) return BladLadowania::BrakDanych;
    r->segment_bledu = -1;
    r->bajty_wypelnienia = 0;
    if (plan.liczba_segmentow == 0) return BladLadowania::PustyPlan;
    const BladLadowania bz = sprawdz_zakresy(p);
    if (bz != BladLadowania::Brak) return bz;
    if (z.rozmiar != plan.rozmiar_pliku) return BladLadowania::ZrodloNiezgodne;
    // Indeks sprawdzany PRZED uzyciem (plan moze pochodzic z uszkodzonej pamieci).
    if (plan.liczba_segmentow > ELF_MAKS_SEGMENTOW || plan.segment_wejscia >= plan.liczba_segmentow)
        return BladLadowania::WejscieNiewykonywalne;
    const SegmentPlanu& w = plan.segmenty[plan.segment_wejscia];
    if (!(w.prawa & ELF_PF_X) || plan.wejscie_wirtualne < w.adres_wirtualny ||
        plan.wejscie_wirtualne - w.adres_wirtualny >= w.bajty_z_pliku)
        return BladLadowania::WejscieNiewykonywalne;
    uint32_t idx[ELF_MAKS_SEGMENTOW];
    sortuj_fizycznie(plan, idx);
    r->liczba = plan.liczba_segmentow;
    // Segmenty: dane == zrodlo, BSS == 0; kazdy bajt w posiadanej pamieci.
    for (uint32_t i = 0; i < plan.liczba_segmentow; ++i) {
        const SegmentPlanu& s = plan.segmenty[i];
        RaportSegmentu& rs = r->segmenty[i];
        rs = RaportSegmentu{s.adres_fizyczny, s.bajty_z_pliku, s.rozmiar_w_pamieci, s.bajty_zerowane, false, false};
        if (!w_posiadanym(p, s.adres_fizyczny, s.rozmiar_w_pamieci)) { r->segment_bledu = int32_t(i); return BladLadowania::CelPozaPrzydzialem; }
        if (!zakres_w_buforze(s.przesuniecie_pliku, s.bajty_z_pliku, z.rozmiar)) { r->segment_bledu = int32_t(i); return BladLadowania::ZrodloPozaBuforem; }
        const uint8_t* m = pamiec.wskaznik(pamiec.kontekst, s.adres_fizyczny, s.rozmiar_w_pamieci);
        if (!m) { r->segment_bledu = int32_t(i); return BladLadowania::BrakDostepuDoPamieci; }
        const uint8_t* src = z.dane + s.przesuniecie_pliku;
        for (uint64_t k = 0; k < s.bajty_z_pliku; ++k) {
            if (m[k] != src[k]) { r->segment_bledu = int32_t(i); r->adres_bledu = s.adres_fizyczny + k; return BladLadowania::WeryfikacjaDanych; }
        }
        rs.dane_ok = true;
        uint64_t pierwszy;
        if (!same_zera(m + s.bajty_z_pliku, s.bajty_zerowane, &pierwszy)) {
            r->segment_bledu = int32_t(i);
            r->adres_bledu = s.adres_fizyczny + s.bajty_z_pliku + pierwszy;
            return BladLadowania::WeryfikacjaBss;
        }
        rs.bss_ok = true;
    }
    // Wypelnienie: kazdy posiadany bajt poza segmentami musi byc zerem.
    for (uint32_t i = 0; i < p.liczba; ++i) {
        uint64_t kur = p.zakresy[i].poczatek, kon = 0;
        if (!koniec_zakresu(p.zakresy[i], &kon)) return BladLadowania::ZleZakresy;
        const uint8_t* baza = pamiec.wskaznik(pamiec.kontekst, kur, kon - kur);
        if (!baza) return BladLadowania::BrakDostepuDoPamieci;
        for (uint32_t n = 0; n <= plan.liczba_segmentow; ++n) {
            uint64_t koniec_luki = kon, nastepny = kon;
            if (n < plan.liczba_segmentow) {
                const SegmentPlanu& s = plan.segmenty[idx[n]];
                if (s.adres_fizyczny + s.rozmiar_w_pamieci <= kur || s.adres_fizyczny >= kon) continue;
                koniec_luki = s.adres_fizyczny;
                nastepny = s.adres_fizyczny + s.rozmiar_w_pamieci;
            }
            if (koniec_luki > kur) {
                uint64_t pierwszy;
                if (!same_zera(baza + (kur - p.zakresy[i].poczatek), koniec_luki - kur, &pierwszy)) {
                    r->adres_bledu = kur + pierwszy;
                    return BladLadowania::WeryfikacjaWypelnienia;
                }
                r->bajty_wypelnienia += koniec_luki - kur;
            }
            if (nastepny > kur) kur = nastepny;
        }
    }
    return BladLadowania::Brak;
}

}  // namespace brb
