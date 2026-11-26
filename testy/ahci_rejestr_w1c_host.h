#pragma once
// TYLKO test hosta (BURSZTYN_AHCI_HOST_TEST): rejestr AHCI typu W1C (PxIS, PxSERR) - zapis jedynki kasuje bit,
// odczyt zwraca stan. Zwykla pamiec nie oddaje tej semantyki (zapis UINT32_MAX ustawilby wszystkie bity bledu).
// "Urzadzenie" testu ustawia bity przez ustaw_sprzetowo(). Uklad: jeden uint32_t, jak w sprzecie.
#ifndef BURSZTYN_AHCI_HOST_TEST
#error "ahci_rejestr_w1c_host.h jest wylacznie dla testow hosta"
#endif
#include <stdint.h>

struct AhciRejestrW1CHost {
    uint32_t v;
    void operator=(uint32_t w) volatile { v = v & ~w; }
    operator uint32_t() const volatile { return v; }
    void ustaw_sprzetowo(uint32_t bity) volatile { v = v | bity; }
};
static_assert(sizeof(AhciRejestrW1CHost) == sizeof(uint32_t), "uklad rejestru AHCI");
