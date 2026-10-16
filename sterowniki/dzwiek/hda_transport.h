#pragma once
// Kernel-only HDA bus. No user pointers, verbs or DMA buffers cross BWS.
#include "hda_diag_format.h"
#include "../dma.h"

enum HdaInicjalizacja : uint32_t {
    HDA_INIT_PCI = 0, HDA_INIT_MAPOWANIE, HDA_INIT_STOP,
    HDA_INIT_CZEKA_STOP, HDA_INIT_RESET_ZERO, HDA_INIT_CZEKA_RESET_ZERO,
    HDA_INIT_TRZYMA_RESET, HDA_INIT_RESET_JEDEN, HDA_INIT_CZEKA_RESET_JEDEN,
    HDA_INIT_KODEKI, HDA_INIT_RING_BUFOR, HDA_INIT_RING_STOP,
    HDA_INIT_RING_CZEKA_STOP, HDA_INIT_RING_BAZY, HDA_INIT_CORB_RESET,
    HDA_INIT_CORB_CZEKA_RESET, HDA_INIT_CORB_ZERUJ_RESET,
    HDA_INIT_CORB_CZEKA_ZERO, HDA_INIT_RIRB_RESET, HDA_INIT_RIRB_CZEKA_ZERO,
    HDA_INIT_RING_START, HDA_INIT_RING_CZEKA_START, HDA_INIT_GOTOWY, HDA_INIT_BLAD, HDA_INIT_RING_WYCISZENIE
};
enum HdaEtapBledu : uint32_t {
    HDA_BRAK_BLEDU = 0, HDA_BLAD_BAR, HDA_BLAD_MAPOWANIA, HDA_BLAD_PCI_MEMORY,
    HDA_BLAD_PCI_MASTER, HDA_BLAD_GCAP, HDA_BLAD_WERSJI, HDA_BLAD_ZEGARA,
    HDA_BLAD_STOP_DMA, HDA_BLAD_RESET_ZERO, HDA_BLAD_RESET_JEDEN,
    HDA_BLAD_CORB_ROZMIAR, HDA_BLAD_RIRB_ROZMIAR, HDA_BLAD_CORB_DMA,
    HDA_BLAD_RIRB_DMA, HDA_BLAD_CORB_RESET, HDA_BLAD_RIRB_RESET,
    HDA_BLAD_RING_START, HDA_BLAD_RESET_W_TRAKCIE
};
enum HdaFazaTransakcji : uint32_t {
    HDA_TX_BEZCZYNNY = 0, HDA_TX_PRZYGOTUJ, HDA_TX_CZEKA_INTERFEJS,
    HDA_TX_USUN_STARA, HDA_TX_WYSLIJ, HDA_TX_CZEKA_KOMENDA,
    HDA_TX_CZEKA_ODPOWIEDZ, HDA_TX_ODCZYTAJ, HDA_TX_GOTOWA,
    HDA_TX_TIMEOUT, HDA_TX_BLAD
};
constexpr uint32_t HDA_WLASCICIEL_DIAG = 1, HDA_WLASCICIEL_BOOT = 2, HDA_WLASCICIEL_TON = 3;
// Legacy code used 2,000,000 / 5,000,000 CPU iterations, not a time unit.
// Explicit HPET policy: 2 ms per hardware wait; 5 ms reset/ring handshake.
constexpr uint64_t HDA_CZAS_VERBA_NS = 2000000ULL;
constexpr uint64_t HDA_CZAS_RESETU_NS = 5000000ULL;
constexpr uint64_t HDA_CZAS_STABILIZACJI_NS = 542000ULL; // >=521 us, old 13000 WALCLK ticks

struct HdaPlatforma {
    void* kontekst;
    uint32_t (*pci_odczyt)(void*, uint8_t, uint8_t, uint8_t, uint8_t);
    void (*pci_zapis)(void*, uint8_t, uint8_t, uint8_t, uint8_t, uint32_t);
    bool (*mapuj)(void*, uint32_t, uint64_t, uint32_t, uint64_t*);
    uint32_t (*odczyt)(void*, uint32_t, uint32_t, uint8_t);
    void (*zapis)(void*, uint32_t, uint32_t, uint8_t, uint32_t);
    bool (*dma)(void*, bool, DmaBuffer*);
    void (*zwolnij_dma)(void*, DmaBuffer*);
    uint64_t (*czas_ns)(void*);
    bool (*zegar_gotowy)(void*);
};

// Platform injection is internal; host tests run the SAME driver state machine.
bool hda_transport_wykryj_z(const HdaPlatforma&);
bool hda_transport_wykryj(); // boot only: PCI enumeration is not a BWS operation
uint32_t hda_transport_liczba();
bool hda_transport_rejestr_obciety();
bool hda_transport_stan(uint32_t, HdaDiagKontroler*);
bool hda_transport_init_krok(uint32_t);
bool hda_transport_odnow(uint32_t); // bounded ring recovery, never a codec write
uint64_t hda_transport_mmio(uint32_t); // kernel only
HdaTransportStatus hda_transport_zglos(uint32_t, uint32_t wlasciciel, uint32_t komenda);
HdaTransportStatus hda_transport_krok(uint32_t, uint32_t wlasciciel);
HdaTransportStatus hda_transport_odbierz(uint32_t, uint32_t wlasciciel, uint32_t*);
void hda_transport_anuluj(uint32_t, uint32_t wlasciciel);
void hda_transport_zapisz_maski(uint32_t, uint16_t discovered, uint16_t responsive);
void hda_transport_test_dostepny(uint32_t, bool);
void hda_transport_generacja(uint32_t, uint32_t);
uint32_t hda_transport_odczyt_rejestru(uint32_t, uint32_t, uint8_t);

#ifdef BURSZTYN_HDA_HOST_TEST
void hda_transport_host_reset();
// Exercise the bounded IC implementation without selecting it at real boot.
bool hda_transport_host_immediate(uint32_t);
#endif
