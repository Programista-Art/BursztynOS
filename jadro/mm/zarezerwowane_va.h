#pragma once
/*
 * Stale okna wirtualne jadra w DOLNEJ polowie przestrzeni adresowej (te same adresy sa widoczne w KAZDEJ
 * przestrzeni procesu, bo tabele dolnej polowy sa klonowane przy tworzeniu procesu). Obrazy, sterty i
 * obszary SHM procesow (ASLR BUR v2) NIE moga na nie zachodzic: loader mapujacy strone uzytkownika na
 * adresie zajetym przez mapowanie jadra zastapilby jego PTE, a jadro pisalo/czytalo wtedy strony procesu
 * (SMAP #PF w Ring 0 albo cicha korupcja). Przyczyna przerywanego #PF w tapety_obsluz_obraz (BWS 69).
 *
 * UWAGA: to lista STATYCZNA (dokumentacja + szybki test). Autorytatywna jest weryfikacja faktycznego stanu tabel
 * (vmm_zakres.h / VmmZakresWolnyWBazie), ktora lapie takze mapowania dynamiczne (np. BAR-y mapowane VA==PA).
 *
 * Czyste stale/funkcje bez zaleznosci od jadra: testy hosta (test_bur_v2_aslr_host.cpp, test_wkb_*).
 */
#include <stdint.h>

namespace ZarezerwowaneVA {

struct Zakres { uint64_t start; uint64_t rozmiar; const char* nazwa; };

inline constexpr uint64_t MIB = 1024ULL * 1024ULL;

inline constexpr uint64_t SUROWY_BMP_START   = 0x120000000ULL;   // grafika.cpp: scratch tapet (VADDR_RAW_BMP)
inline constexpr uint64_t SUROWY_BMP_ROZMIAR = 32ULL * MIB;      // MAX_RAW_BMP
inline constexpr uint64_t FRAMEBUFFER_START   = 0x130000000ULL;  // grafika.cpp: VADDR_FRAMEBUFFER (LFB)
inline constexpr uint64_t FRAMEBUFFER_ROZMIAR = 64ULL * MIB;     // gorne ograniczenie rozmiaru LFB
inline constexpr uint64_t OKNO_ACPI_START   = 0x140000000ULL;   // acpi.cpp: OKNO_ACPI (tablice ACPI > 4 GiB)
inline constexpr uint64_t OKNO_ACPI_ROZMIAR = 1ULL * MIB;        // acpi.cpp: ROZMIAR_OKNA
inline constexpr uint64_t HPET_VA_START     = 0x141000000ULL;    // hpet.cpp: HPET_VA (rejestry HPET > 4 GiB)
inline constexpr uint64_t HPET_VA_ROZMIAR   = 4096ULL;
inline constexpr uint64_t OHCI_MMIO_START   = 0x144000000ULL;    // ohci.cpp: MMIO
inline constexpr uint64_t OHCI_MMIO_ROZMIAR = 4096ULL;
inline constexpr uint64_t XHCI_MMIO_START   = 0x2000000000ULL;   // xhci.cpp: MMIO_VIRTUAL_BASE
inline constexpr uint64_t XHCI_MMIO_ROZMIAR = 8ULL * 8ULL * 16ULL * MIB;   // 8 kontrolerow x 8 BAR x MAX_MMIO_SIZE
inline constexpr uint64_t VMM_SAMOTEST_NISKIE_START   = 0x146000000ULL;   // vmm.cpp: samotest synchronizacji (tylko diag)
inline constexpr uint64_t VMM_SAMOTEST_NISKIE_ROZMIAR = 4ULL * MIB;      // dwa regiony 2 MiB (osobne PT)
inline constexpr uint64_t RAM_DYSK_PSF_START   = 0x200000000ULL; // kernel.cpp: ADRES_RAM_DYSKU_PSF
inline constexpr uint64_t RAM_DYSK_PSF_ROZMIAR = 128ULL * MIB;
inline constexpr uint64_t STERTA_JADRA_START   = 0x500000000ULL; // kernel.cpp: ADRES_STERTY_JADRA
inline constexpr uint64_t STERTA_JADRA_ROZMIAR = 16ULL * MIB;

inline constexpr Zakres ZAKRESY[] = {
    {SUROWY_BMP_START, SUROWY_BMP_ROZMIAR, "surowy-bmp"},
    {FRAMEBUFFER_START, FRAMEBUFFER_ROZMIAR, "framebuffer"},
    {OKNO_ACPI_START, OKNO_ACPI_ROZMIAR, "okno-acpi"},
    {HPET_VA_START, HPET_VA_ROZMIAR, "hpet"},
    {OHCI_MMIO_START, OHCI_MMIO_ROZMIAR, "ohci-mmio"},
    {XHCI_MMIO_START, XHCI_MMIO_ROZMIAR, "xhci-mmio"},
    {VMM_SAMOTEST_NISKIE_START, VMM_SAMOTEST_NISKIE_ROZMIAR, "vmm-samotest"},
    {RAM_DYSK_PSF_START, RAM_DYSK_PSF_ROZMIAR, "ram-dysk-psf"},
    {STERTA_JADRA_START, STERTA_JADRA_ROZMIAR, "sterta-jadra"},
};

// Czy polotwarty przedzial [start, koniec) przecina ktorekolwiek okno jadra. Pusty/odwrocony przedzial = true
// (fail-closed), przepelnienia nie sa mozliwe (porownania bez dodawania do wartosci uzytkownika).
inline bool koliduje_z_jadrem(uint64_t start, uint64_t koniec) {
    if (koniec <= start) return true;
    for (const Zakres& z : ZAKRESY) {
        const uint64_t z_koniec = z.start + z.rozmiar;
        if (start < z_koniec && koniec > z.start) return true;
    }
    return false;
}

// Gorna polowa (PML4[256..511], wspolna dla wszystkich procesow, nigdy uzytkownika): bazy znanych regionow. Kazdy region
// zajmuje wlasny slot PML4 - slot tworzony po starcie procesow jest propagowany (vmm_synchronizacja.h), ale nowe stale
// adresy nadal musza byc rezerwowane TUTAJ i miec osobny slot.
inline constexpr uint64_t GRAFIKA_BUFORY_BAZA   = 0xFFFF900000000000ULL;   // bufory_grafiki.h
inline constexpr uint64_t HDA_BAZA              = 0xFFFF920000000000ULL;   // hda_transport.cpp
inline constexpr uint64_t STOSY_JADRA_BAZA      = 0xFFFF940000000000ULL;   // stos_jadra.h
inline constexpr uint64_t VMM_SAMOTEST_WYSOKIE  = 0xFFFFA00000000000ULL;   // vmm.cpp: samotest (nowy slot PML4)
constexpr unsigned slot_pml4(uint64_t va) { return static_cast<unsigned>((va >> 39) & 511ULL); }
static_assert(slot_pml4(GRAFIKA_BUFORY_BAZA) != slot_pml4(HDA_BAZA) && slot_pml4(HDA_BAZA) != slot_pml4(STOSY_JADRA_BAZA) &&
              slot_pml4(GRAFIKA_BUFORY_BAZA) != slot_pml4(STOSY_JADRA_BAZA) &&
              slot_pml4(VMM_SAMOTEST_WYSOKIE) != slot_pml4(GRAFIKA_BUFORY_BAZA) &&
              slot_pml4(VMM_SAMOTEST_WYSOKIE) != slot_pml4(HDA_BAZA) && slot_pml4(VMM_SAMOTEST_WYSOKIE) != slot_pml4(STOSY_JADRA_BAZA),
              "regiony gornej polowy musza miec rozne sloty PML4");
static_assert(slot_pml4(GRAFIKA_BUFORY_BAZA) >= 256 && slot_pml4(VMM_SAMOTEST_WYSOKIE) >= 256, "gorna polowa");

} // namespace ZarezerwowaneVA
