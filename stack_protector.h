#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * ============================================================================
 *  BURSZTYN OS — OCHRONA STOSU KOMPILATORA (STACK SMASHING PROTECTOR / SSP)
 * ============================================================================
 *
 *  Wsparcie wykonawcze (runtime) dla opcji kompilatora:
 *    -fstack-protector-strong -mstack-protector-guard=global
 *
 *  Architektura:
 *    1. Ring 0 (jadro):
 *       - Globalny straznik `__stack_chk_guard` inicjalizowany w `boot.S`
 *         (`stack_protector_early_init`) przed wywolaniem `kernel_main()`,
 *         zanim powstanie jakakolwiek ramka stosu C/C++.
 *       - Zrodlo entropii: sprzetowe instrukcje RDRAND / RDSEED (jesli
 *         dostepne w CPUID) zmieszane z licznikiem TSC, adresem stosu oraz
 *         nieliniowym mikserem SplitMix64.
 *       - Bajt 0 straznika jest zawsze zerowy (0x00 — tzw. terminator canary
 *         zatrzymujacy przepelnienia funkcji operujacych na C-stringach),
 *         natomiast pozostale 7 bajtow ma gwarancje wartosci niezerowych.
 *       - Wykrycie naruszenia w Ring 0 (`__stack_chk_fail`) natychmiast
 *         wylacza przerwania (`cli`), wypisuje ograniczony komunikat
 *         diagnostyczny `[STACKPROTECTORVIOLATION]` bez ujawniania tajnej
 *         wartosci straznika i zatrzymuje procesor (`cli; hlt`).
 *
 *    2. Ring 3 (aplikacje .bur):
 *       - Kazda aplikacja Ring 3 posiada wlasna zmienna `__stack_chk_guard`
 *         w prywatnej sekcji `.data` zainicjalizowana w pliku obrazu `.bur`
 *         wartoscia znacznikowa `BUR_SSP_GUARD_PLACEHOLDER`.
 *       - Podczas tworzenia procesu w `loader.cpp` (przed wejsciem do `_start`)
 *         jadro nadpisuje znacznik w prywatnych stronach `.data` procesu
 *         unikalnym, losowym straznikiem wygenerowanym specjalnie dla tego
 *         procesu (`stack_protector_zainicjuj_segment_danych_procesu`).
 *       - Wykrycie naruszenia w Ring 3 wywoluje `BWS_ZAKONCZ_PROCES` z kodem
 *         `BWS_EXIT_STACK_PROTECTOR_VIOLATION` (`0x53535056`), co powoduje
 *         kontrolowane zakonczenie wylacznie uszkodzonego procesu bez paniki
 *         calego jadra.
 * ============================================================================
 */

/* Znacznik w sekcji .data pliku .bur zastepowany unikalnym kanarkiem procesu */
#define BUR_SSP_GUARD_PLACEHOLDER           0x4750535352554200ULL

/* Specjalny kod zakonczenia BWS_ZAKONCZ_PROCES przy naruszeniu SSP w Ring 3 */
#define BWS_EXIT_STACK_PROTECTOR_VIOLATION  0x53535056ULL

#ifdef __cplusplus
extern "C" {
#endif

extern uintptr_t __stack_chk_guard;

/*
 * Wczesna inicjalizacja straznika stosu jadra wywolywana z `boot.S`
 * przed wejsciem do `kernel_main()`.
 */
void stack_protector_early_init(void);

/*
 * Zwraca true, jezeli podczas inicjalizacji straznika jadra procesor
 * dostarczyl sprzetowa entropie z instrukcji RDSEED lub RDRAND.
 */
bool stack_protector_czy_entropia_sprzetowa(void);

/*
 * Generuje nowy 64-bitowy straznik stosu dla procesu Ring 3
 * (bajt 0 == 0x00, bajty 1..7 != 0x00, rozny od straznika jadra).
 */
uintptr_t stack_protector_generuj_dla_procesu(
    uint64_t pid,
    uintptr_t punkt_wejscia
);

/*
 * Podmienia znacznik `BUR_SSP_GUARD_PLACEHOLDER` w prywatnym segmencie `.data`
 * nowo tworzonego procesu Ring 3 na unikalny straznik dla tego procesu.
 * Zwraca liczbe zaktualizowanych 64-bitowych slow.
 */
uint32_t stack_protector_zainicjuj_segment_danych_procesu(
    uint64_t pid,
    uintptr_t punkt_wejscia,
    uint8_t* dane_wirtualny,
    uint64_t rozmiar_danych
);

/*
 * Procedura awaryjna wywolywana przez kod wygenerowany przez GCC przy
 * niezgodnosci straznika stosu. Nigdy nie wraca.
 */
[[noreturn]] void __stack_chk_fail(void);
[[noreturn]] void __stack_chk_fail_local(void);

#ifdef __cplusplus
}
#endif
