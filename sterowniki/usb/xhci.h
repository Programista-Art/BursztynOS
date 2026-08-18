#pragma once
#include <stddef.h>
#include <stdint.h>

constexpr uint32_t BURSZTYN_XHCI_MAX_SLOTS = 64U;
constexpr uint32_t BURSZTYN_XHCI_MAX_PORTS = 255U;

bool xhci_inicjalizuj_pierwszy();
size_t xhci_poll_events(size_t budget);
void usb_obsluz();
uint32_t xhci_read32(const volatile void* base, uint32_t offset);
void xhci_write32(volatile void* base, uint32_t offset, uint32_t value);
uint64_t xhci_read64(const volatile void* base, uint32_t offset);
void xhci_write64(volatile void* base, uint32_t offset, uint64_t value);
