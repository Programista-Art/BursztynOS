/*
 * Bursztyn OS - Architektura i Ochrona Stosow Jadra Ring 0
 *
 * Parametry i uklad pamieci wirtualnej:
 *   BAZA_STOSOW_JADRA: 0xFFFF940000000000 (PML4[296], galaz supervisor-only wspoldzielona w calym jadrze)
 *
 * Uklad kazdego 128 KiB slotu procesu PID:
 *   [slot_base + 0x0000 .. + 0x0FFF] : unmapped guard page (4 KiB, P=0, brak ramki fizycznej)
 *   [slot_base + 0x1000 .. + 0x10FFF]: zmapowany stos jadra (64 KiB, 16 stron fizycznych, RW, supervisor)
 *   [slot_base + 0x11000.. + 0x1FFFF]: unmapped padding / gorny margines (60 KiB, P=0)
 *
 * Pomiędzy szczytem stosu procesu N a baza stosu procesu N+1 znajduje sie 64 KiB niezmapowanej pamieci:
 * 60 KiB gornego marginesu slotu N + 4 KiB dolnego guard page slotu N+1.
 *
 * Ochrona integralnosci:
 *   - Stack Canary (0xB0057E11CADA0001) zapisywany na dnie zmapowanego stosu (offset +0x1000).
 *   - Znak wodny (0xCC) wypelniany przy alokacji stosu.
 *   - Skanowanie znaku wodnego wyznacza rzeczywisty szczyt zuzycia stosu (high-water mark).
 *   - Ostrzezenie w test buildach przy przekroczeniu 75% stosu (48 KiB).
 *   - Sprzetowy Page Fault (#PF) w guard page powoduje natychmiastowe zatrzymanie z pelna diagnostyka.
 *   - Dedykowany stos awaryjny IST1 w TSS dla Double Fault (#DF) z wlasnym guard page zapobiega triple-faultom.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

constexpr uint64_t BAZA_STOSOW_JADRA = 0xFFFF940000000000ULL;
constexpr size_t ROZMIAR_STRONY_STOSU = 4096ULL;
#ifdef BURSZTYN_DIAG_STOS16K
// Diagnostyczna reprodukcja historycznego 16 KiB stosu (wylacznie do
// kontrolowanych testow guard page / BWS84; slot wirtualny 128 KiB/PID
// pozostaje bez zmian, mapowane sa tylko 4 strony).
constexpr size_t ROZMIAR_STOSU_JADRA_STRONY = 4ULL;
#else
constexpr size_t ROZMIAR_STOSU_JADRA_STRONY = 16ULL;
#endif
constexpr size_t ROZMIAR_STOSU_JADRA_BAJTY = ROZMIAR_STOSU_JADRA_STRONY * ROZMIAR_STRONY_STOSU; // 64 KiB
constexpr size_t ROZMIAR_GUARD_PAGE_BAJTY = 4096ULL;
constexpr size_t ROZMIAR_SLOTU_PROCESU_BAJTY = 128ULL * 1024ULL; // 128 KiB

constexpr uint64_t KERNEL_STACK_CANARY = 0xB0057E11CADA0001ULL;
constexpr uint8_t STACK_WATERMARK_BYTE = 0xCC;

struct StosMetryki {
    uint64_t stack_base;
    uint64_t stack_top;
    uint64_t current_rsp;
    uint64_t min_rsp;
    uint64_t max_used;
    uint64_t remaining_margin;
    char deepest_context[16];
    uint64_t last_reported_max_used;
};

/*
 * Klasyfikacja aktywnego stosu w diagnostyce wyjatkow.
 *
 * Identyfikatory tekstowe uzywane w logu szeregowym:
 *   PROCESSKERNELSTACK - zmapowany 64 KiB stos procesu (slot PID)
 *   BOOTSTACK          - 64 KiB stos startowy z boot.S (PID 0 przed Ring 3)
 *   IST1DOUBLEFAULT    - awaryjny stos #DF z TSS
 *   IST2NMI / IST3MC   - awaryjne stosy NMI / Machine Check
 *   UNKNOWNSTACK       - zaden znany zakres (RSP spoza zarzadzanych stosow)
 */
enum StosRodzaj : uint32_t {
    STOS_NIEZNANY = 0,
    STOS_PROCES_JADRA = 1,
    STOS_BOOT = 2,
    STOS_IST_DF = 3,
    STOS_IST_NMI = 4,
    STOS_IST_MC = 5,
};

const char* stos_nazwa_rodzaju(StosRodzaj rodzaj);

/*
 * Identyfikuje, do ktorego znanego stosu nalezy podany RSP.
 * Zwraca rodzaj oraz (gdy znany) PID wlasciciela i zakres [baza, szczyt).
 * Dla STOS_NIEZNANY out_pid/out_base/out_top pozostaja nietkniete.
 * Funkcja nie dereferencjuje podanego wskaznika.
 */
StosRodzaj stos_klasyfikuj_rsp(uint64_t rsp, int* out_pid,
                               uint64_t* out_base, uint64_t* out_top);

// Inicjalizuje struktury PML4 dla stosow jadra oraz chroni stos IST dla Double Fault.
bool stos_jadra_inicjalizuj();

// Przydziela zmapowany stos 64 KiB z unmapped guard page ponizej dla wskazanego PID.
bool stos_jadra_przydziel(int pid, uint64_t* out_baza, uint64_t* out_szczyt);

// Weryfikuje kanarka, odmapowuje strony stosu i zwraca ramki fizyczne do PMM.
void stos_jadra_zwolnij(int pid);

// Sprawdza, czy podany adres wirtualny (np. CR2) wpada w guard page stosu danego procesu.
bool stos_czy_adres_w_guard_page(uint64_t va, int pid, uint64_t* out_base, uint64_t* out_top);

// Weryfikuje integralnosc kanarka na dnie stosu (baza_stosu_jadra).
bool stos_weryfikuj_kanarka(int pid);

// Dynamiczne probkowanie rejestru RSP.
void stos_probkuj_rsp(int pid, uint64_t rsp);

// Dynamiczne probkowanie rejestru RSP wraz z identyfikatorem kontekstu (np. BWS84, IRQTIMER).
void stos_probkuj_rsp_kontekst(int pid, uint64_t rsp, const char* kontekst);

// Oblicza rzeczywiste zuzycie stosu (watermark scan) oraz sprawdza prog 75%.
StosMetryki stos_zmierz_zuzycie(int pid);

// Raportuje biezace metryki stosu do portu szeregowego (w trybie diagnostycznym tylko przy nowym szczycie).
void stos_raportuj_sciezke(const char* nazwa_sciezki, int pid);

// Wymusza wypisanie metryk stosu do portu szeregowego (dla testow diagnostycznych).
void stos_wymus_raport_metryk(const char* nazwa_sciezki, int pid);

// Pobiera biezacy rekord metryk dla procesu.
StosMetryki stos_pobierz_metryki(int pid);

// Kontrolowany test przepelnienia guard page dla regresji.
[[noreturn]] void stos_wykonaj_test_przepelnienia(int pid);
