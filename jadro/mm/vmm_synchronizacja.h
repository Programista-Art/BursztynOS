#pragma once
/*
 * Synchronizacja mapowan JADRA z prywatnymi klonami PML4 procesow (bez migracji do higher-half).
 *
 * Fakty architektury (vmm.cpp): kazdy proces ma klon dolnej polowy bazowego PML4 (poziomy 4..2 skopiowane,
 * liscie 4 KiB skopiowane jako wpisy), a wpisy PML4[256..511] sa kopiowane PO WARTOSCI - czyli tabele nizszych
 * poziomow gornej polowy sa wspolne, ale SAM wpis PML4 utworzony po sklonowaniu procesu nie trafia do istniejacych
 * klonow. Mapowanie jadra wykonane po utworzeniu procesu jest wiec:
 *   - w dolnej polowie: widoczne tylko w przestrzeni, ktora akurat byla w CR3 (reszta ma stare kopie PT/PD);
 *   - w gornej polowie: wspolne, ale tylko jesli slot PML4 juz istnial (inaczej prywatny dla biezacego procesu).
 * Ten naglowek zawiera czysta logike (testowalna na hoscie z wstrzykiwaniem bledow); operacje na prawdziwych tablicach
 * dostarcza `Ops` (vmm.cpp). Wszystko dzieje sie pod BlokadaVMM (IRQ wylaczone) na jedynym uruchomionym CPU (BSP).
 *
 * Kontrakt Ops (cel 0 = przestrzen bazowa jadra, kolejne = zywe klony procesow):
 *   size_t liczba_celow();
 *   bool   konflikt_uzytkownika(size_t cel, uint64_t va);          // obecna strona UZYTKOWNIKA pod va
 *   bool   przygotuj_tabele(size_t cel, uint64_t va);              // PDP/PD/PT (rozbicie huge); semantycznie neutralne
 *   uint64_t odczytaj_lisc(size_t cel, uint64_t va);               // surowy wpis liscia 4K (0 = brak)
 *   bool   zapisz_lisc(size_t cel, uint64_t va, uint64_t pa, uint64_t flagi);
 *   void   ustaw_lisc_surowy(size_t cel, uint64_t va, uint64_t wpis);   // rollback / odmapowanie
 *   bool   lisc_jest_4k(size_t cel, uint64_t va);                  // pod va stoi lisc 4K (nie huge) albo nic
 *   uint64_t slot_celu(size_t cel, unsigned idx); void ustaw_slot_celu(size_t cel, unsigned idx, uint64_t wpis);
 *   uint64_t alokuj_slot_pdp();                                    // nowa pusta tablica PDP -> gotowy wpis PML4 (0 = brak pamieci)
 */
#include <stddef.h>
#include <stdint.h>

namespace VmmSynchronizacja {

enum class Wynik : uint8_t { OK, Odmowa, Konflikt, BladAlokacji, BladZapisu };

template <class Ops>
inline Wynik mapuj_globalnie(Ops& o, uint64_t va, uint64_t pa, uint64_t flagi) {
    const size_t n = o.liczba_celow();
    if (n == 0) return Wynik::Odmowa;
    // 1. Kolizja z mapowaniem uzytkownika w DOWOLNEJ przestrzeni - przed jakakolwiek zmiana.
    for (size_t i = 0; i < n; ++i)
        if (o.konflikt_uzytkownika(i, va)) return Wynik::Konflikt;
    // 2. Tabele posrednie we wszystkich celach (jedyne miejsce, gdzie moze zabraknac pamieci). Zmiany sa neutralne
    //    semantycznie (puste PT / rozbita huge-page), wiec porazka nie wymaga cofania.
    for (size_t i = 0; i < n; ++i)
        if (!o.przygotuj_tabele(i, va)) return Wynik::BladAlokacji;
    // 3. Zapis lisci; przy (teoretycznej) porazce cofamy juz zapisane do poprzednich wartosci.
    uint64_t stare[1 + 256];
    if (n > sizeof(stare) / sizeof(stare[0])) return Wynik::Odmowa;
    for (size_t i = 0; i < n; ++i) {
        stare[i] = o.odczytaj_lisc(i, va);
        if (!o.zapisz_lisc(i, va, pa, flagi)) {
            for (size_t j = 0; j < i; ++j) o.ustaw_lisc_surowy(j, va, stare[j]);
            return Wynik::BladZapisu;
        }
    }
    return Wynik::OK;
}

// Zwraca w `*stary_bazowy` wpis liscia z przestrzeni bazowej (do zwolnienia ramki przez wlasciciela).
template <class Ops>
inline Wynik odmapuj_globalnie(Ops& o, uint64_t va, uint64_t* stary_bazowy) {
    const size_t n = o.liczba_celow();
    if (n == 0) return Wynik::Odmowa;
    for (size_t i = 0; i < n; ++i) {
        if (o.konflikt_uzytkownika(i, va)) return Wynik::Konflikt;
        if (!o.lisc_jest_4k(i, va)) return Wynik::Odmowa;                 // huge-page nie jest odmapowywana liscmi
    }
    if (stary_bazowy) *stary_bazowy = o.odczytaj_lisc(0, va);
    for (size_t i = 0; i < n; ++i) o.ustaw_lisc_surowy(i, va, 0);
    return Wynik::OK;
}

// Slot PML4 gornej polowy: tworzony RAZ w bazie i propagowany do wszystkich zywych klonow (takze naprawia klon,
// ktory ma niezgodny/pusty wpis). Zwraca false przy braku pamieci (nic nie zmienione w zadnym celu).
template <class Ops>
inline bool zapewnij_slot_jadra(Ops& o, unsigned idx) {
    const size_t n = o.liczba_celow();
    if (n == 0 || idx < 256 || idx > 511) return false;
    uint64_t e = o.slot_celu(0, idx);
    if (!(e & 1ULL)) {
        e = o.alokuj_slot_pdp();
        if (!e) return false;
        o.ustaw_slot_celu(0, idx, e);
    }
    for (size_t i = 1; i < n; ++i)
        if (o.slot_celu(i, idx) != e) o.ustaw_slot_celu(i, idx, e);
    return true;
}

// Rejestr zywych klonow procesow (pod blokada VMM). Deterministyczny, bez alokacji.
template <size_t N>
struct Rejestr {
    uint64_t* wpisy[N] = {};
    size_t liczba = 0;
    bool dodaj(uint64_t* p) {
        if (!p) return false;
        for (size_t i = 0; i < N; ++i) if (wpisy[i] == p) return true;
        for (size_t i = 0; i < N; ++i) if (!wpisy[i]) { wpisy[i] = p; ++liczba; return true; }
        return false;
    }
    bool usun(uint64_t* p) {
        for (size_t i = 0; i < N; ++i) if (wpisy[i] == p) { wpisy[i] = nullptr; --liczba; return true; }
        return false;
    }
    // kompaktowa lista celow: [0]=baza, potem zywe klony; zwraca liczbe celow
    size_t cele(uint64_t* baza, uint64_t** out, size_t pojemnosc) const {
        size_t n = 0;
        if (!baza || pojemnosc == 0) return 0;
        out[n++] = baza;
        for (size_t i = 0; i < N && n < pojemnosc; ++i) if (wpisy[i]) out[n++] = wpisy[i];
        return n;
    }
};

} // namespace VmmSynchronizacja
