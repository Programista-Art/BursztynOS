#pragma once
/*
 * Bursztyn OS - Intel ICH AC97: stale rejestrow.
 *
 * Zrodla (publiczna dokumentacja, bez kodu innych sterownikow):
 *   - Audio Codec '97 Component Specification Rev. 2.3 (Intel),
 *   - Intel I/O Controller Hub (82801AA/AB ICH ... 82801G ICH7) datasheets,
 *     rozdzial "AC'97 Audio Controller" (NAMBAR/NABMBAR, Bus Master, BDL).
 *
 * NAMBAR (BAR0, I/O, 256 portow): rejestry kodeka (Native Audio Mixer), dostep 16-bitowy.
 * NABMBAR (BAR1, I/O, 64 porty): Native Audio Bus Master - trzy kanaly DMA (PCM In, PCM Out, Mic) i rejestry
 * globalne. Sterownik uzywa wylacznie kanalu PCM Out.
 */
#include <stdint.h>

namespace Ac97 {

/* ---- PCI ---- */
constexpr uint16_t PCI_VENDOR_INTEL = 0x8086;
constexpr uint8_t PCI_KLASA_MULTIMEDIA = 0x04;
constexpr uint8_t PCI_PODKLASA_AUDIO = 0x01;          // "Multimedia audio controller" (AC97), nie HDA (0x03)
constexpr uint8_t PCI_CFG_ICH4 = 0x41;                // ICH4+: CFG, bit 0 IOSE (wlaczenie natywnych BAR I/O)
constexpr uint8_t PCI_ICH4_IOSE = 0x01;
constexpr uint16_t ROZMIAR_NAM = 256;
constexpr uint16_t ROZMIAR_NABM = 64;

/* ---- NAM: rejestry kodeka AC97 2.3 ---- */
constexpr uint16_t NAM_RESET = 0x00;                  // zapis = reset rejestrow kodeka, odczyt = mozliwosci
constexpr uint16_t NAM_MASTER = 0x02;                 // bit 15 mute, 13:8 tlumienie L, 5:0 tlumienie P
constexpr uint16_t NAM_PCM_OUT = 0x18;                // bit 15 mute, 12:8 L, 4:0 P (0x08 = 0 dB)
constexpr uint16_t NAM_POWERDOWN = 0x26;              // 3:0 statusy gotowosci ADC/DAC/ANL/REF
constexpr uint16_t NAM_EXT_ID = 0x28;                 // bit 0 VRA
constexpr uint16_t NAM_EXT_CTRL = 0x2A;               // bit 0 wlaczenie VRA
constexpr uint16_t NAM_PCM_FRONT_RATE = 0x2C;         // czestotliwosc DAC (przy VRA)
constexpr uint16_t NAM_VENDOR1 = 0x7C;
constexpr uint16_t NAM_VENDOR2 = 0x7E;

constexpr uint16_t NAM_MUTE = 0x8000;
constexpr uint16_t PCM_OUT_0DB = 0x0808;
constexpr uint16_t PD_ADC = 1u << 0, PD_DAC = 1u << 1, PD_ANL = 1u << 2, PD_REF = 1u << 3;
constexpr uint16_t PD_WYJSCIE_GOTOWE = PD_DAC | PD_ANL | PD_REF;
constexpr uint16_t EXT_VRA = 1u << 0;

/* ---- NABM: kanal PCM Out (offset 0x10) i rejestry globalne ---- */
constexpr uint16_t PO_BASE = 0x10;
constexpr uint16_t BOX_BDBAR = 0x00;                  // 32 bit, adres fizyczny BDL (8-byte aligned)
constexpr uint16_t BOX_CIV = 0x04;                    // 8 bit, Current Index Value (RO)
constexpr uint16_t BOX_LVI = 0x05;                    // 8 bit, Last Valid Index
constexpr uint16_t BOX_SR = 0x06;                     // 16 bit, Status
constexpr uint16_t BOX_PICB = 0x08;                   // 16 bit, pozostale probki biezacego bufora (RO)
constexpr uint16_t BOX_PIV = 0x0A;                    // 8 bit, Prefetched Index Value (RO)
constexpr uint16_t BOX_CR = 0x0B;                     // 8 bit, Control

constexpr uint16_t NABM_GLOB_CNT = 0x2C;              // 32 bit
constexpr uint16_t NABM_GLOB_STA = 0x30;              // 32 bit
constexpr uint16_t NABM_CAS = 0x34;                   // 8 bit, Codec Access Semaphore

/* SR: bity 2..4 sa RW1C (zapis jedynki czysci), DCH i CELV sa tylko do odczytu. */
constexpr uint16_t SR_DCH = 1u << 0;                  // DMA controller halted
constexpr uint16_t SR_CELV = 1u << 1;                 // CIV == LVI (po ostatnim buforze)
constexpr uint16_t SR_LVBCI = 1u << 2;                // ostatni poprawny bufor zakonczony
constexpr uint16_t SR_BCIS = 1u << 3;                 // zakonczenie bufora z IOC
constexpr uint16_t SR_FIFOE = 1u << 4;                // blad FIFO (niedobor danych)
constexpr uint16_t SR_W1C = SR_LVBCI | SR_BCIS | SR_FIFOE;

constexpr uint8_t CR_RPBM = 1u << 0;                  // run / pause bus master
constexpr uint8_t CR_RR = 1u << 1;                    // reset rejestrow kanalu (samoczyszczacy)
constexpr uint8_t CR_LVBIE = 1u << 2;
constexpr uint8_t CR_FEIE = 1u << 3;
constexpr uint8_t CR_IOCE = 1u << 4;

constexpr uint32_t GC_GIE = 1u << 0;
constexpr uint32_t GC_COLD = 1u << 1;                 // 0 = AC_RESET# aktywny, 1 = koniec zimnego resetu
constexpr uint32_t GC_WARM = 1u << 2;
constexpr uint32_t GC_KANALY_MASKA = 3u << 20;        // 0 = 2 kanaly
constexpr uint32_t GC_TRYB_PCM_MASKA = 3u << 22;      // ICH4+: 0 = 16 bit

constexpr uint32_t GS_PCR = 1u << 8;                  // primary codec ready
constexpr uint32_t GS_RCS = 1u << 15;                 // read completion status: timeout odczytu kodeka (RW1C)
constexpr uint32_t GS_GSCI = 1u << 0;                 // RW1C

/* ---- BDL ---- */
constexpr uint32_t WPISY_BDL = 32;                    // stala sprzetowa: indeksy 5-bitowe
constexpr uint16_t BDL_IOC = 1u << 15;                // przerwanie (status BCIS) po zakonczeniu bufora
constexpr uint16_t BDL_BUP = 1u << 14;                // po ostatnim buforze nadawaj zera zamiast powtarzac probke
constexpr uint32_t BDL_MAKS_PROBEK = 0xFFFE;          // dlugosc w 16-bitowych probkach (nie ramkach), parzysta

struct WpisBdl {
    uint32_t adres;      // fizyczny, wyrownany do 2 bajtow (uzywamy 4096)
    uint16_t probki;     // liczba probek 16-bit (stereo: 2 na ramke); 0 = brak danych
    uint16_t flagi;
} __attribute__((packed));
static_assert(sizeof(WpisBdl) == 8, "Wpis BDL AC97 ma 8 bajtow");

} // namespace Ac97
