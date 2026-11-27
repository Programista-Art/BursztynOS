#include "mapa_uefi.h"

#include "../wspolne/arytmetyka.h"
#include "../wspolne/pamiec_podstawowa.h"

namespace brb {

namespace {
WynikMapy wynik(EFI_STATUS s, BladPobraniaMapy b, BladMapy w = BladMapy::Brak) { return WynikMapy{s, b, w}; }
}

const char* opis_bledu_pobrania_mapy(BladPobraniaMapy b) {
    switch (b) {
        case BladPobraniaMapy::Brak: return "brak bledu";
        case BladPobraniaMapy::BrakFunkcji: return "brak uslug pamieci firmware";
        case BladPobraniaMapy::NieoczekiwanyStatus: return "nieoczekiwany status zapytania o rozmiar mapy";
        case BladPobraniaMapy::ZlyRozmiarZgloszony: return "niewiarygodny rozmiar mapy albo deskryptora";
        case BladPobraniaMapy::Alokacja: return "brak pamieci na bufor mapy";
        case BladPobraniaMapy::ZaDuzoProb: return "mapa pamieci zmienia sie przy kazdej probie";
        case BladPobraniaMapy::BladFirmware: return "GetMemoryMap zwrocil blad";
        case BladPobraniaMapy::Niespojnosc: return "GetMemoryMap zglosil wiecej danych niz bufor";
        case BladPobraniaMapy::Walidacja: return "migawka mapy pamieci niepoprawna";
    }
    return "nieznany blad mapy";
}

EFI_STATUS zwolnij_migawke_mapy(EFI_BOOT_SERVICES* bs, MigawkaMapy* m) {
    if (!m || m->bufor == 0) return EFI_SUCCESS;
    const EFI_STATUS s = bs->FreePages(m->bufor, m->strony);
    // Wlasnosc jest porzucana niezaleznie od wyniku: ponowne FreePages tego samego zakresu bylby podwojnym
    // zwolnieniem. Blad jest zwracany wywolujacemu (raport "sprzatanie nieudane").
    m->bufor = 0;
    m->strony = 0;
    m->widok = WidokMapy{nullptr, 0, 0};
    return s;
}

WynikMapy pobierz_migawke_mapy(EFI_BOOT_SERVICES* bs, MigawkaMapy* m) {
    if (!m) return wynik(EFI_INVALID_PARAMETER, BladPobraniaMapy::BrakFunkcji);
    brb_memset(m, 0, sizeof(*m));
    if (!bs || !bs->GetMemoryMap || !bs->AllocatePages || !bs->FreePages)
        return wynik(EFI_UNSUPPORTED, BladPobraniaMapy::BrakFunkcji);

    UINTN rozmiar = 0, klucz = 0, krok = 0;
    UINT32 wersja = 0;
    EFI_STATUS s = bs->GetMemoryMap(&rozmiar, nullptr, &klucz, &krok, &wersja);
    if (s != EFI_BUFFER_TOO_SMALL) return wynik(EFI_ERROR(s) ? s : EFI_DEVICE_ERROR, BladPobraniaMapy::NieoczekiwanyStatus);

    for (UINT32 proba = 1; proba <= MAPA_MAKS_PROB; ++proba) {
        if (rozmiar == 0 || krok < MAPA_MIN_ROZMIAR_DESKRYPTORA || krok > MAPA_MAKS_ROZMIAR_DESKRYPTORA)
            return wynik(EFI_LOAD_ERROR, BladPobraniaMapy::ZlyRozmiarZgloszony);
        uint64_t zapas, pojemnosc, zaokraglona;
        if (!mnoz_u64(krok, MAPA_ZAPAS_DESKRYPTOROW, &zapas) || !dodaj_u64(rozmiar, zapas, &pojemnosc) ||
            pojemnosc > MAPA_MAKS_BUFOR || !wyrownaj_w_gore(pojemnosc, MAPA_STRONA, &zaokraglona))
            return wynik(EFI_BAD_BUFFER_SIZE, BladPobraniaMapy::ZlyRozmiarZgloszony);
        EFI_PHYSICAL_ADDRESS bufor = 0;
        const UINTN strony = UINTN(zaokraglona / MAPA_STRONA);
        s = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, strony, &bufor);
        if (EFI_ERROR(s) || bufor == 0) return wynik(EFI_ERROR(s) ? s : EFI_OUT_OF_RESOURCES, BladPobraniaMapy::Alokacja);
        UINTN n = UINTN(pojemnosc);
        s = bs->GetMemoryMap(&n, reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(static_cast<uintptr_t>(bufor)), &klucz, &krok,
                             &wersja);
        if (s == EFI_BUFFER_TOO_SMALL) {
            // Mapa urosla (m.in. o wlasna alokacje bufora): zwolnij i ponow z nowym rozmiarem.
            bs->FreePages(bufor, strony);
            rozmiar = n;
            continue;
        }
        m->bufor = bufor;   // od tej chwili bufor nalezy do migawki (zwalniany przez zwolnij_migawke_mapy)
        m->strony = strony;
        m->proby = proba;
        if (EFI_ERROR(s)) {
            zwolnij_migawke_mapy(bs, m);
            return wynik(s, BladPobraniaMapy::BladFirmware);
        }
        if (n > pojemnosc) {
            zwolnij_migawke_mapy(bs, m);
            return wynik(EFI_LOAD_ERROR, BladPobraniaMapy::Niespojnosc);
        }
        m->rozmiar = n;
        m->klucz = klucz;
        m->rozmiar_deskryptora = krok;
        m->wersja = wersja;
        const BladMapy w = waliduj_mape(reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(bufor)), n, krok, wersja,
                                        &m->widok, &m->statystyki);
        if (w != BladMapy::Brak) {
            zwolnij_migawke_mapy(bs, m);
            return wynik(EFI_LOAD_ERROR, BladPobraniaMapy::Walidacja, w);
        }
        return wynik(EFI_SUCCESS, BladPobraniaMapy::Brak);
    }
    return wynik(EFI_LOAD_ERROR, BladPobraniaMapy::ZaDuzoProb);
}

}  // namespace brb
