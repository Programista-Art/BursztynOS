#ifndef BURSZTYN_APIC_H
#define BURSZTYN_APIC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns the Local APIC ID of the calling CPU.
 */
uint8_t apic_pobierz_lapic_id(void);

#ifdef __cplusplus
}
#endif

#endif // BURSZTYN_APIC_H
