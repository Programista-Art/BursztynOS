#pragma once
#include <stdint.h>

#ifndef BURSZTYNDIAGVIRTIOINPUTINIT
#define BURSZTYNDIAGVIRTIOINPUTINIT 0
#endif

// Bootstrap runs on the BSP. These breadcrumbs are observations, never locks
// or driver state. The before/after words are separate from IRQ/exception data
// so a nested entry cannot erase whether the MMIO instruction returned.
#if BURSZTYNDIAGVIRTIOINPUTINIT
struct InputActivationTrace {
    volatile uint32_t armed, phase, irq_depth, entries, exits;
    volatile uint64_t bdf, mmio, rflags, vector, rip, error, cr2;
    volatile uint64_t store_rip, next_rip;
};
extern InputActivationTrace input_activation_trace;
void input_activation_log(const char* operation, uint64_t value);
void input_activation_entry(uint64_t vector, uint64_t rip, uint64_t error, uint64_t cr2);
void input_activation_exit(uint64_t vector);
uint32_t heap_diagnostic_lock_state();
uint32_t pmm_diagnostic_lock_state();
uint32_t pci_diagnostic_lock_state();
uint32_t apic_diagnostic_pending(uint8_t vector);
bool pci_diagnostic_try_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t* value);
#endif
