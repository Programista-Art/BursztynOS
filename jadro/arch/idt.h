#ifndef BURSZTYN_IDT_H
#define BURSZTYN_IDT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IDT_PIERWSZY_WEKTOR_DYNAMICZNY 48
#define IDT_OSTATNI_WEKTOR_DYNAMICZNY  252
/* Wektor 253: programowe oddanie CPU z punktu zwrotu syscallu (int $253, DPL 0) - poza pulą dynamiczną. */
#define IDT_WEKTOR_ODDANIA_CPU         253

/*
 * Wektor 253: czy obsluga ma przelaczyc kontekst (PrzelaczKontekstProgramowy)? Tylko gdy przerwano Ring 0 procesu
 * uzytkownika (1..maks-1), ktory jawnie na to zezwolil (scheduler_czeka_w_sieci). W kazdym innym przypadku
 * obsluga wraca do przerwanego kodu bez zmian. Z Ring 3 wektor i tak nie jest osiagalny (DPL 0 -> #GP).
 */
static inline bool idt_oddanie_cpu_przelacz(bool przerwano_ring0, int pid, int maks_procesow, bool zezwolenie) {
    return przerwano_ring0 && pid > 0 && pid < maks_procesow && zezwolenie;
}

typedef void (*IrqHandlerFn)(void* context);

void InicjalizujIDT(void);

/**
 * Dynamic IRQ vector allocator for PCI MSI / MSI-X and other devices.
 * Allocates from the pool [48..252]. Returns allocated vector number, or -1 on exhaustion.
 */
int idt_alokuj_wektor_irq(void);

/**
 * Releases a dynamically allocated IRQ vector.
 */
bool idt_zwolnij_wektor_irq(uint8_t vector);

/**
 * Registers an interrupt handler and context for a dynamic IRQ vector.
 */
bool idt_zarejestruj_irq(uint8_t vector, IrqHandlerFn handler, void* context);

/**
 * Unregisters an interrupt handler for a dynamic IRQ vector.
 */
bool idt_wyrejestruj_irq(uint8_t vector);

/**
 * Returns the number of currently allocated dynamic IRQ vectors (for leak/delta checking).
 */
uint32_t idt_zaalokowane_wektory_liczba(void);

#ifdef __cplusplus
}
#endif

#endif // BURSZTYN_IDT_H
