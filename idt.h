#ifndef BURSZTYN_IDT_H
#define BURSZTYN_IDT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IDT_PIERWSZY_WEKTOR_DYNAMICZNY 48
#define IDT_OSTATNI_WEKTOR_DYNAMICZNY  254

typedef void (*IrqHandlerFn)(void* context);

void InicjalizujIDT(void);

/**
 * Dynamic IRQ vector allocator for PCI MSI / MSI-X and other devices.
 * Allocates from the pool [48..254]. Returns allocated vector number, or -1 on exhaustion.
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
