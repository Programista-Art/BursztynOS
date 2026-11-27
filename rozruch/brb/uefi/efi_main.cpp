/*
 * BRB - punkt wejscia aplikacji UEFI (PE32+, IMAGE_SUBSYSTEM_EFI_APPLICATION).
 *
 * efi_main jest wolane przez firmware w konwencji Microsoft x64 (UEFI 2.10, 2.3.4): RCX = ImageHandle,
 * RDX = SystemTable, stos wyrownany do 16 B przed CALL, 32 B shadow space zapewnione przez wywolujacego.
 * Atrybut EFIAPI (ms_abi) sprawia, ze GCC zachowuje rejestry nieulotne ms_abi (RBX, RBP, RDI, RSI, R12-R15)
 * przed wywolaniem kodu System V. Plik jest jedynym miejscem z instrukcjami in/out - nie jest kompilowany
 * w testach hosta.
 */
#include "program.h"

namespace {

uint8_t port_czytaj(void*, uint16_t port) {
    uint8_t wartosc;
    asm volatile("inb %1, %0" : "=a"(wartosc) : "Nd"(port));
    return wartosc;
}

void port_zapisz(void*, uint16_t port, uint8_t wartosc) {
    asm volatile("outb %0, %1" : : "a"(wartosc), "Nd"(port));
}

// Tlumacz adresow fizycznych Fazy 4A: w czasie uslug rozruchowych UEFI pamiec jest mapowana identycznosciowo
// (UEFI 2.10, 2.3.4: "paging enabled ... identity mapped"). Tlumacz NIE decyduje o wlasnosci - modul ladowania
// wola go wylacznie dla zakresow przydzielonych BRB przez AllocatePages. Odrzuca jedynie zakresy z przepelnieniem.
uint8_t* fizyczny_na_wskaznik(void*, uint64_t adres, uint64_t dlugosc) {
    if (adres == 0 || dlugosc > ~uint64_t(0) - adres) return nullptr;
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(adres));
}

}  // namespace

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE obraz, EFI_SYSTEM_TABLE* tablica) {
    brb::LogSzeregowy log;
    log.inicjalizuj(brb::PortyWeWy{port_czytaj, port_zapisz, nullptr});
    const brb::DostepFizyczny pamiec{fizyczny_na_wskaznik, nullptr};
    return brb::uruchom(obraz, tablica, log, brb::USTAWIENIA_DOMYSLNE, pamiec);
}
