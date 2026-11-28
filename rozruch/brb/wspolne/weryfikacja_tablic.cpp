#include "weryfikacja_tablic.h"

#include "arytmetyka.h"
#include "pamiec_podstawowa.h"

namespace brb {

namespace {

constexpr uint64_t P = 1ull << 0, W = 1ull << 1, U = 1ull << 2, PS = 1ull << 7, NX = 1ull << 63;
constexpr uint64_t ADRES = 0x000FFFFFFFFFF000ull;
constexpr uint64_t KONIEC_DOLNEJ = 0x0000800000000000ull;

bool w_zakresie(uint64_t a, uint64_t p, uint64_t n) { return a >= p && a - p < n; }
bool nachodza(uint64_t a, uint64_t an, uint64_t b, uint64_t bn) { return a < b + bn && b < a + an; }

struct Stan {
    const OczekiwaniaTablic* o;
    const DostepTablic* d;
    RaportTablic* r;
    uint64_t limit_fiz;
    BladTablic blad;
};

BladTablic ustaw(Stan& s, BladTablic b, uint64_t adres) {
    if (s.blad == BladTablic::Brak) { s.blad = b; s.r->adres_bledu = adres; }
    return b;
}

// Prawa strony okna wg segmentu ELF: segment, ktorego [strona_poczatek, nastepny_poczatek) zawiera strone.
bool prawa_okna(const OczekiwaniaTablic& o, uint64_t va, bool* w, bool* x) {
    const PlanLadowania& p = *o.plan;
    int32_t wybrany = -1;
    for (uint32_t i = 0; i < p.liczba_segmentow; ++i) {
        const uint64_t poczatek = p.segmenty[i].adres_wirtualny & ~(ELF_STRONA - 1);
        if (va >= poczatek && (wybrany < 0 || poczatek >= (p.segmenty[uint32_t(wybrany)].adres_wirtualny & ~(ELF_STRONA - 1))))
            wybrany = int32_t(i);
    }
    if (wybrany < 0) return false;
    *w = (p.segmenty[uint32_t(wybrany)].prawa & ELF_PF_W) != 0;
    *x = (p.segmenty[uint32_t(wybrany)].prawa & ELF_PF_X) != 0;
    return true;
}

bool klasa_praw(uint32_t klasa, bool* w, bool* x) {
    switch (klasa) {
        case ZASOB_KOD_PRZEJSCIA: *w = false; *x = true; return true;
        case ZASOB_STOS: case ZASOB_DANE: *w = true; *x = false; return true;
        case ZASOB_TYLKO_ODCZYT: *w = false; *x = false; return true;
        default: return false;
    }
}

void lisc(Stan& s, uint64_t va, uint64_t pa, bool w, bool x) {
    const OczekiwaniaTablic& o = *s.o;
    ++s.r->liscie;
    if (w && x) { ustaw(s, BladTablic::WPlusX, va); return; }
    if (w_zakresie(va, o.baza_wirtualna, o.rozmiar)) {
        if (pa != o.baza_fizyczna + (va - o.baza_wirtualna)) { ustaw(s, BladTablic::ZlyAdresOkna, va); return; }
        bool ew = false, ex = false;
        if (!prawa_okna(o, va, &ew, &ex) || ew != w || ex != x) { ustaw(s, BladTablic::ZleUprawnieniaOkna, va); return; }
        ++s.r->strony_okna;
        return;
    }
    for (uint32_t i = 0; i < o.liczba_zasobow; ++i) {
        const ZasobTozsamosciowy& z = o.zasoby[i];
        const uint64_t p0 = z.pa & ~(ELF_STRONA - 1);
        uint64_t k = 0;
        if (!wyrownaj_w_gore(z.pa + z.dlugosc, ELF_STRONA, &k)) continue;
        if (va >= p0 && va < k) {
            if (pa != va) { ustaw(s, BladTablic::ZlaTozsamosc, va); return; }
            bool ew = false, ex = false;
            if (!klasa_praw(z.klasa, &ew, &ex) || ew != w || ex != x) { ustaw(s, BladTablic::ZleUprawnieniaZasobu, va); return; }
            ++s.r->strony_zasobow;
            return;
        }
    }
    if (w_zakresie(va, o.pula_pa, o.pula_ramek * ELF_STRONA)) {
        if (pa != va) { ustaw(s, BladTablic::ZlaTozsamosc, va); return; }
        if (w || x) { ustaw(s, BladTablic::ZleUprawnieniaZasobu, va); return; }
        ++s.r->strony_puli;
        return;
    }
    ustaw(s, BladTablic::MapowanieNieoczekiwane, va);
}

void przejdz(Stan& s, uint64_t tab_pa, int poziom, uint64_t baza_va, bool w, bool nx) {
    if (s.blad != BladTablic::Brak) return;
    const OczekiwaniaTablic& o = *s.o;
    if (tab_pa % ELF_STRONA || !w_zakresie(tab_pa, o.pula_pa, o.pula_ramek * ELF_STRONA)) {
        ustaw(s, BladTablic::TablicaPozaPula, tab_pa);
        return;
    }
    const uint64_t* t = s.d->tablica(s.d->kontekst, tab_pa);
    if (!t) { ustaw(s, BladTablic::BrakDostepuDoTablicy, tab_pa); return; }
    ++s.r->tablice;
    const unsigned przes = 12u + 9u * unsigned(poziom - 1);
    for (unsigned i = 0; i < 512 && s.blad == BladTablic::Brak; ++i) {
        const uint64_t e = t[i];
        if (!(e & P)) continue;
        const uint64_t va = baza_va + (uint64_t(i) << przes);
        if (poziom == 4 && i >= 256) { ustaw(s, BladTablic::MapowanieNieoczekiwane, 0xFFFF000000000000ull | va); return; }
        if (e & U) { ustaw(s, BladTablic::BitUzytkownika, va); return; }
        const uint64_t adres = e & ADRES;
        if (adres >= s.limit_fiz) { ustaw(s, BladTablic::BitZarezerwowany, va); return; }
        const bool w2 = w && (e & W), nx2 = nx || (o.nx && (e & NX));
        if (poziom > 1 && (e & PS)) { ustaw(s, BladTablic::BitZarezerwowany, va); return; }   // duza strona / PS w PML4E
        if (poziom == 1) { lisc(s, va, adres, w2, !nx2); continue; }
        przejdz(s, adres, poziom - 1, va, w2, nx2);
    }
}

// Niezalezne tlumaczenie jednej VA (do kompletnosci, wejscia i straznikow).
bool tlumacz(const Stan& s, uint64_t va, uint64_t* pa, bool* w, bool* x) {
    uint64_t t = s.o->cr3 & ADRES;
    bool ww = true, nn = false;
    for (int poziom = 4; poziom >= 1; --poziom) {
        if (!w_zakresie(t, s.o->pula_pa, s.o->pula_ramek * ELF_STRONA)) return false;
        const uint64_t* tab = s.d->tablica(s.d->kontekst, t);
        if (!tab) return false;
        const uint64_t e = tab[(va >> (12 + 9 * (poziom - 1))) & 511];
        if (!(e & P)) return false;
        ww = ww && (e & W);
        nn = nn || (s.o->nx && (e & NX));
        if (poziom == 1) { *pa = (e & ADRES) | (va & 0xFFF); *w = ww; *x = !nn; return true; }
        t = e & ADRES;
    }
    return false;
}

}  // namespace

BladTablic weryfikuj_tablice_przejscia(const OczekiwaniaTablic& o, const DostepTablic& d, RaportTablic* r) {
    if (!r) return BladTablic::BrakDanych;
    brb_memset(r, 0, sizeof(*r));
    if (!o.plan || !d.tablica || (o.liczba_zasobow && !o.zasoby) || o.liczba_zasobow > WERYFIKACJA_MAKS_ZASOBOW ||
        o.rozmiar == 0 || o.rozmiar % ELF_STRONA || o.baza_fizyczna % ELF_STRONA || o.pula_ramek == 0 ||
        o.bity_fizyczne < 36 || o.bity_fizyczne > 52)
        return BladTablic::BrakDanych;
    if (!o.nx) return BladTablic::BrakNx;
    Stan s{&o, &d, r, 1ull << o.bity_fizyczne, BladTablic::Brak};
    // Zasoby i pula: rozlaczne z podkladem (alias) i z oknem VA (tozsamosc nadpisalaby okno).
    for (uint32_t i = 0; i <= o.liczba_zasobow; ++i) {
        const uint64_t a = i < o.liczba_zasobow ? o.zasoby[i].pa : o.pula_pa;
        const uint64_t n = i < o.liczba_zasobow ? o.zasoby[i].dlugosc : o.pula_ramek * ELF_STRONA;
        uint64_t k;
        if (n == 0 || !dodaj_u64(a, n, &k) || k > KONIEC_DOLNEJ) return ustaw(s, BladTablic::AliasPodkladu, a);
        if (nachodza(a, n, o.baza_fizyczna, o.rozmiar) || nachodza(a, n, o.baza_wirtualna, o.rozmiar))
            return ustaw(s, BladTablic::AliasPodkladu, a);
    }
    if (o.cr3 % ELF_STRONA || !w_zakresie(o.cr3, o.pula_pa, o.pula_ramek * ELF_STRONA)) return ustaw(s, BladTablic::Cr3Niepoprawny, o.cr3);
    przejdz(s, o.cr3, 4, 0, true, false);
    if (s.blad != BladTablic::Brak) return s.blad;
    // Kompletnosc okna (kazda strona dokladnie raz - VA unikalne, wiec licznik == liczba stron).
    if (r->strony_okna != o.rozmiar / ELF_STRONA) {
        for (uint64_t va = o.baza_wirtualna; va < o.baza_wirtualna + o.rozmiar; va += ELF_STRONA) {
            uint64_t pa; bool w, x;
            if (!tlumacz(s, va, &pa, &w, &x)) return ustaw(s, BladTablic::BrakStronyOkna, va);
        }
        return ustaw(s, BladTablic::BrakStronyOkna, o.baza_wirtualna);
    }
    uint64_t oczekiwane_zasoby = 0;
    for (uint32_t i = 0; i < o.liczba_zasobow; ++i) {
        const uint64_t p0 = o.zasoby[i].pa & ~(ELF_STRONA - 1);
        uint64_t k = 0;
        wyrownaj_w_gore(o.zasoby[i].pa + o.zasoby[i].dlugosc, ELF_STRONA, &k);
        for (uint64_t va = p0; va < k; va += ELF_STRONA) {
            uint64_t pa; bool w, x;
            if (!tlumacz(s, va, &pa, &w, &x)) return ustaw(s, BladTablic::BrakStronyZasobu, va);
            bool duplikat = false;   // strona wspolna z wczesniejszym zasobem (dozwolona przy tej samej klasie)
            for (uint32_t j = 0; j < i; ++j) {
                const uint64_t q0 = o.zasoby[j].pa & ~(ELF_STRONA - 1);
                uint64_t qk = 0;
                wyrownaj_w_gore(o.zasoby[j].pa + o.zasoby[j].dlugosc, ELF_STRONA, &qk);
                if (va >= q0 && va < qk) duplikat = true;
            }
            if (!duplikat) ++oczekiwane_zasoby;
        }
    }
    if (r->strony_zasobow != oczekiwane_zasoby) return ustaw(s, BladTablic::BrakStronyZasobu, 0);
    if (r->strony_puli != o.pula_ramek) return ustaw(s, BladTablic::BrakStronyZasobu, o.pula_pa);
    // Wejscie natywne: wlasciwy bajt podkladu, wykonywalne, nie zapisywalne.
    {
        uint64_t pa; bool w, x;
        if (!w_zakresie(o.wejscie64, o.baza_wirtualna, o.rozmiar) || !tlumacz(s, o.wejscie64, &pa, &w, &x) ||
            pa != o.baza_fizyczna + (o.wejscie64 - o.baza_wirtualna) || !x || w)
            return ustaw(s, BladTablic::WejscieNiepoprawne, o.wejscie64);
        r->wejscie_pa = pa;
    }
    // Straznicy: strona 0, strona tuz przed i tuz za oknem (o ile nie sa jawnym zasobem).
    {
        uint64_t pa; bool w, x;
        const uint64_t straznicy[3] = {0, o.baza_wirtualna - ELF_STRONA, o.baza_wirtualna + o.rozmiar};
        for (uint64_t va : straznicy) {
            bool zasob = false;
            for (uint32_t i = 0; i < o.liczba_zasobow; ++i) if (nachodza(va, ELF_STRONA, o.zasoby[i].pa, o.zasoby[i].dlugosc)) zasob = true;
            if (nachodza(va, ELF_STRONA, o.pula_pa, o.pula_ramek * ELF_STRONA)) zasob = true;
            if (!zasob && tlumacz(s, va, &pa, &w, &x)) return ustaw(s, BladTablic::StraznikZmapowany, va);
        }
    }
    // Pierwszy odczyt jadra (RSI): caly zakres kontraktu tozsamosciowy, obecny, R+NX, supervisor.
    if (o.kontrakt_pa) {
        uint64_t k;
        if (!dodaj_u64(o.kontrakt_pa, o.kontrakt_dlugosc, &k) || o.kontrakt_dlugosc == 0)
            return ustaw(s, BladTablic::KontraktNiedostepny, o.kontrakt_pa);
        for (uint64_t va = o.kontrakt_pa & ~(ELF_STRONA - 1); va < k; va += ELF_STRONA) {
            uint64_t pa; bool w, x;
            if (!tlumacz(s, va, &pa, &w, &x) || pa != va || w || x) return ustaw(s, BladTablic::KontraktNiedostepny, va);
        }
    }
    return BladTablic::Brak;
}

const char* opis_bledu_tablic(BladTablic b) {
    switch (b) {
        case BladTablic::Brak: return "brak bledu";
        case BladTablic::BrakDanych: return "brak danych weryfikacji tablic";
        case BladTablic::Cr3Niepoprawny: return "CR3 poza pula albo niewyrownany";
        case BladTablic::TablicaPozaPula: return "ramka tablicy poza posiadana pula";
        case BladTablic::BrakDostepuDoTablicy: return "brak dostepu do ramki tablicy";
        case BladTablic::BitZarezerwowany: return "ustawiony bit zarezerwowany albo nieoczekiwana duza strona";
        case BladTablic::BitUzytkownika: return "bit U w tablicach przejscia";
        case BladTablic::MapowanieNieoczekiwane: return "mapowanie nieoczekiwane";
        case BladTablic::ZlyAdresOkna: return "strona okna wskazuje zly adres fizyczny";
        case BladTablic::ZleUprawnieniaOkna: return "uprawnienia strony okna niezgodne z segmentem ELF";
        case BladTablic::ZlaTozsamosc: return "mapowanie tozsamosciowe wskazuje inny adres";
        case BladTablic::ZleUprawnieniaZasobu: return "uprawnienia zasobu niezgodne z klasa";
        case BladTablic::WPlusX: return "strona jednoczesnie zapisywalna i wykonywalna";
        case BladTablic::BrakStronyOkna: return "brak strony okna obrazu";
        case BladTablic::BrakStronyZasobu: return "brak strony zasobu albo puli";
        case BladTablic::WejscieNiepoprawne: return "wejscie natywne tlumaczy sie niepoprawnie";
        case BladTablic::StraznikZmapowany: return "strona straznika zmapowana";
        case BladTablic::AliasPodkladu: return "zasob nachodzi na podklad albo okno obrazu";
        case BladTablic::KontraktNiedostepny: return "kontrakt nie jest dostepny tozsamosciowo R+NX";
        case BladTablic::BrakNx: return "brak NX - polityka R2 wymaga W^X tablic przejscia";
    }
    return "nieznany blad tablic";
}

}  // namespace brb
