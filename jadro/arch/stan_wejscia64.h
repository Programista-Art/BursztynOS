#pragma once
/*
 * Bursztyn OS - kontrakt stanu procesora na natywnym wejsciu 64-bit (Faza R1B).
 *
 * wejscie_jadra64 (jadro/arch/wejscie64.S) zapisuje surowy stan (rejestry kontrolne, EFER, CPUID, RSP i RFLAGS
 * z chwili wejscia), a waliduj_stan() decyduje - czysta funkcja, testowana na hoscie na stanach symulowanych.
 * Zrodla (Intel SDM t. 3A: 2.5 rejestry kontrolne, 4.1 tryby stronicowania, 4.1.4 CPUID; 2.2.1 EFER;
 * AMD APM t. 2: 3.1.7 EFER, 5.3 tryby stronicowania; t. 3: CPUID 80000001h, 80000008h):
 *   - IA-32e 64-bit: CR0.PE=1, CR0.PG=1, CR4.PAE=1, EFER.LME=1, EFER.LMA=1; CPL 0 (CS.RPL = 0),
 *   - stronicowanie 4-poziomowe: CR4.LA57 = 0 (5 poziomow nieobslugiwane - odmowa),
 *   - CR0.WP=1 (jadro respektuje strony tylko do odczytu takze w Ring 0),
 *   - CR4.PCIDE=0 (CR3 bez PCID: bity 11..0 = 0),
 *   - CR3: wyrownany do 4 KiB, ponizej 2^MAXPHYADDR (CPUID 80000008h EAX[7:0]; brak liscia = 36 bitow),
 *   - EFER.NXE=1, gdy procesor ma NX (tablice przejscia uzywaja bitu 63); bez NX bit 63 bylby zarezerwowany,
 *   - RFLAGS.IF=0 (przerwania wylaczone - IDT jadra jeszcze nie istnieje), RFLAGS.DF=0,
 *   - RSP z chwili skoku ≡ 0 (mod 16) - wejscie przez `jmp`, nie `call` (protokol-rozruchowy.md 2.2),
 *   - RDI = MetadaneJadra::MAGIC_WEJSCIA_NATYWNEGO, RSI = PA kontraktu (wyrownany do 8, w limicie tozsamosci).
 * Konwencja wywolan jadra: System V AMD64 (nie Microsoft x64 - ta dotyczy wylacznie wywolan uslug UEFI w BRB),
 * jadro kompilowane z -mno-red-zone (Makefile CPU_FLAGS), bez SSE/MMX (-mgeneral-regs-only).
 */

#include <stdint.h>

namespace StanWejscia64 {

inline constexpr uint64_t CR0_PE = 1ULL << 0;
inline constexpr uint64_t CR0_WP = 1ULL << 16;
inline constexpr uint64_t CR0_PG = 1ULL << 31;
inline constexpr uint64_t CR4_PAE = 1ULL << 5;
inline constexpr uint64_t CR4_PCIDE = 1ULL << 17;
inline constexpr uint64_t CR4_LA57 = 1ULL << 12;
inline constexpr uint64_t EFER_LME = 1ULL << 8;
inline constexpr uint64_t EFER_LMA = 1ULL << 10;
inline constexpr uint64_t EFER_NXE = 1ULL << 11;
inline constexpr uint64_t RFLAGS_IF = 1ULL << 9;
inline constexpr uint64_t RFLAGS_DF = 1ULL << 10;
inline constexpr uint32_t CPUID_LM = 1U << 29;     // 80000001h EDX
inline constexpr uint32_t CPUID_NX = 1U << 20;     // 80000001h EDX
inline constexpr uint32_t CPUID_PAE = 1U << 6;     // 1 EDX
inline constexpr uint64_t LIMIT_KONTRAKTU = 0x100000000ULL;

struct Stan {
    uint64_t magic;             // RDI
    uint64_t kontrakt;          // RSI (PA)
    uint64_t rsp_wejscia;       // RSP w chwili skoku
    uint64_t rflags_wejscia;    // RFLAGS przed cli/cld
    uint64_t cr0, cr3, cr4, efer;
    uint16_t cs;
    uint32_t cpuid_max_rozszerzony;   // 80000000h EAX
    uint32_t cpuid_1_edx;
    uint32_t cpuid_80000001_edx;
    uint32_t cpuid_80000008_eax;      // 0 = brak liscia
};

enum class Blad : uint32_t {
    OK = 0,
    ZLY_MAGIC,
    KONTRAKT_NIEPOPRAWNY,
    STOS_NIEWYROWNANY,
    PRZERWANIA_WLACZONE,
    KIERUNEK_USTAWIONY,
    CPL_NIE_ZERO,
    BRAK_LONG_MODE_CPUID,
    BRAK_PAE,
    TRYB_NIE_IA32E,
    STRONICOWANIE_5P,
    PCID_WLACZONE,
    WP_WYLACZONE,
    CR3_NIEPOPRAWNY,
    NXE_NIEZGODNE,
    SZEROKOSC_FIZYCZNA,
};

inline const char* opis(Blad b) {
    switch (b) {
    case Blad::OK: return "OK";
    case Blad::ZLY_MAGIC: return "zly magic w RDI";
    case Blad::KONTRAKT_NIEPOPRAWNY: return "niepoprawny wskaznik kontraktu w RSI";
    case Blad::STOS_NIEWYROWNANY: return "RSP przy wejsciu niewyrownany do 16";
    case Blad::PRZERWANIA_WLACZONE: return "RFLAGS.IF = 1 przy wejsciu";
    case Blad::KIERUNEK_USTAWIONY: return "RFLAGS.DF = 1 przy wejsciu";
    case Blad::CPL_NIE_ZERO: return "CPL != 0";
    case Blad::BRAK_LONG_MODE_CPUID: return "CPUID nie zglasza Long Mode";
    case Blad::BRAK_PAE: return "CPUID nie zglasza PAE";
    case Blad::TRYB_NIE_IA32E: return "procesor nie jest w trybie IA-32e (CR0.PE/PG, CR4.PAE, EFER.LME/LMA)";
    case Blad::STRONICOWANIE_5P: return "stronicowanie 5-poziomowe (CR4.LA57) nieobslugiwane";
    case Blad::PCID_WLACZONE: return "CR4.PCIDE = 1 nieobslugiwane";
    case Blad::WP_WYLACZONE: return "CR0.WP = 0";
    case Blad::CR3_NIEPOPRAWNY: return "CR3 niewyrownany albo poza szerokoscia fizyczna";
    case Blad::NXE_NIEZGODNE: return "EFER.NXE niezgodne z obsluga NX";
    case Blad::SZEROKOSC_FIZYCZNA: return "nieobslugiwana szerokosc adresu fizycznego";
    }
    return "nieznany blad stanu wejscia";
}

/* MAXPHYADDR z CPUID 80000008h EAX[7:0]; bez liscia: 36 bitow (Intel SDM 4.1.4). */
inline unsigned bity_fizyczne(const Stan& s) {
    if (s.cpuid_max_rozszerzony < 0x80000008U || s.cpuid_80000008_eax == 0) return 36;
    return s.cpuid_80000008_eax & 0xFFU;
}

inline bool nx_dostepne(const Stan& s) {
    return s.cpuid_max_rozszerzony >= 0x80000001U && (s.cpuid_80000001_edx & CPUID_NX) != 0;
}

inline Blad waliduj_stan(const Stan& s, uint64_t magic_oczekiwany) {
    if (s.magic != magic_oczekiwany) return Blad::ZLY_MAGIC;
    if ((s.cs & 3U) != 0) return Blad::CPL_NIE_ZERO;
    if ((s.rflags_wejscia & RFLAGS_IF) != 0) return Blad::PRZERWANIA_WLACZONE;
    if ((s.rflags_wejscia & RFLAGS_DF) != 0) return Blad::KIERUNEK_USTAWIONY;
    if ((s.rsp_wejscia & 0xFULL) != 0) return Blad::STOS_NIEWYROWNANY;
    if (s.cpuid_max_rozszerzony < 0x80000001U || (s.cpuid_80000001_edx & CPUID_LM) == 0) return Blad::BRAK_LONG_MODE_CPUID;
    if ((s.cpuid_1_edx & CPUID_PAE) == 0) return Blad::BRAK_PAE;
    if ((s.cr0 & (CR0_PE | CR0_PG)) != (CR0_PE | CR0_PG) || (s.cr4 & CR4_PAE) == 0 ||
        (s.efer & (EFER_LME | EFER_LMA)) != (EFER_LME | EFER_LMA)) {
        return Blad::TRYB_NIE_IA32E;
    }
    if (s.cr4 & CR4_LA57) return Blad::STRONICOWANIE_5P;
    if (s.cr4 & CR4_PCIDE) return Blad::PCID_WLACZONE;
    if ((s.cr0 & CR0_WP) == 0) return Blad::WP_WYLACZONE;
    const unsigned bity = bity_fizyczne(s);
    if (bity < 36 || bity > 52) return Blad::SZEROKOSC_FIZYCZNA;
    const uint64_t limit = 1ULL << bity;
    if ((s.cr3 & 0xFFFULL) != 0 || s.cr3 == 0 || s.cr3 >= limit) return Blad::CR3_NIEPOPRAWNY;
    if (nx_dostepne(s) != ((s.efer & EFER_NXE) != 0)) return Blad::NXE_NIEZGODNE;
    if (s.kontrakt == 0 || (s.kontrakt & 7ULL) != 0 || s.kontrakt >= limit || s.kontrakt >= LIMIT_KONTRAKTU) {
        return Blad::KONTRAKT_NIEPOPRAWNY;
    }
    return Blad::OK;
}

} // namespace StanWejscia64
