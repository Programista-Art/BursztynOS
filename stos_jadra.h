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
constexpr size_t ROZMIAR_STOSU_JADRA_STRONY = 16ULL;
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
};

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

// Oblicza rzeczywiste zuzycie stosu (watermark scan) oraz sprawdza prog 75%.
StosMetryki stos_zmierz_zuzycie(int pid);

// Raportuje biezace metryki stosu do portu szeregowego dla sciezki wywolania.
void stos_raportuj_sciezke(const char* nazwa_sciezki, int pid);

// Pobiera biezacy rekord metryk dla procesu.
StosMetryki stos_pobierz_metryki(int pid);

// Kontrolowany test przepelnienia guard page dla regresji.
[[noreturn]] void stos_wykonaj_test_przepelnienia(int pid);
