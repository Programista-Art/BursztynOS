#pragma once
#include <stdint.h>
#include <stddef.h>
// Read-only BWS 91: (ABI << 32) | command, output, capacity, index.
constexpr uint32_t DIAG_ABI = 1, DIAG_MAX_DEVICES = 384, DIAG_MAX_LOG = 1024;
enum DiagCategory : uint32_t { DIAG_SYSTEM, DIAG_CPU, DIAG_MEMORY, DIAG_ACPI,
 DIAG_APIC, DIAG_HPET, DIAG_PCI, DIAG_GRAPHICS, DIAG_STORAGE, DIAG_AHCI,
 DIAG_VIRTIO_BLOCK, DIAG_FILESYSTEM, DIAG_USB, DIAG_XHCI, DIAG_OHCI,
 DIAG_PS2, DIAG_KEYBOARD, DIAG_MOUSE, DIAG_TOUCHPAD, DIAG_HDA, DIAG_NETWORK,
 DIAG_VIRTIO_INPUT, DIAG_VIRTIO_NET, DIAG_HID, DIAG_CATEGORY_COUNT };
enum DiagStatus : uint32_t { DIAG_INACTIVE, DIAG_OK, DIAG_NOT_FOUND,
 DIAG_UNSUPPORTED, DIAG_WARNING, DIAG_INIT_FAILED, DIAG_TIMEOUT,
 DIAG_DRIVER_MISSING, DIAG_DETECTED_UNSUPPORTED, DIAG_CRITICAL };
enum DiagTransport : uint32_t { DIAG_TRANSPORT_NONE, DIAG_TRANSPORT_PCI,
 DIAG_TRANSPORT_USB, DIAG_TRANSPORT_PS2, DIAG_TRANSPORT_VIRTIO,
 DIAG_TRANSPORT_I2C, DIAG_TRANSPORT_MEMORY, DIAG_TRANSPORT_FIRMWARE };
enum DiagCode : uint32_t { DIAG_CODE_NONE, DIAG_CODE_XHCI_INIT,
 DIAG_CODE_HID_INIT, DIAG_CODE_HDA_INIT, DIAG_CODE_AHCI_INIT,
 DIAG_CODE_NO_KEYBOARD, DIAG_CODE_NO_POINTER, DIAG_CODE_FILESYSTEM,
 DIAG_CODE_STORE_FULL, DIAG_CODE_I2C_DRIVER };
enum DiagCommand : uint64_t { DIAG_GET_SUMMARY=1, DIAG_GET_DEVICE=2,
 DIAG_GET_DRIVER=3, DIAG_GET_LOG=4 };
enum : uint32_t { DIAG_FLAG_DETECTED=1, DIAG_FLAG_KEYBOARD=2,
 DIAG_FLAG_POINTER=4, DIAG_FLAG_SYSTEM_REQUIRED=8, DIAG_FLAG_TRUNCATED=16, DIAG_FLAG_REDACTED=32 };
// No pointers, DMA addresses or register access handles cross this ABI.
struct DiagDevice {
 uint32_t version, size, id, parent, category, status, transport, flags, code;
 uint16_t vendor, device;
 uint8_t bus, pci_device, function, pci_valid;
 uint32_t reserved; // Explicit zeroed alignment; no implicit ABI padding.
 uint64_t bar; // Numeric PCI metadata only; never mapped into user space.
 uint32_t controller, port, slot, usb_class, configuration, interfaces, hid_interfaces;
 uint32_t interface_number, alternate, subclass, protocol, endpoint, dci, packet_size, interval, role;
 uint32_t ports, max_slots, xhci_version, hardware_status;
 uint64_t value; // Category-specific scalar: RAM bytes, codec count, etc.
 char name[64], driver[32], message[160];
};
struct DiagLog {
 uint32_t version, size, sequence, category, severity, code, flags;
 char text[228];
};
struct DiagSummary {
 uint32_t version, size, generation, devices, drivers, logs, errors, warnings;
 uint32_t status, serious, keyboard, pointer, dropped_devices, dropped_logs;
 uint32_t categories[DIAG_CATEGORY_COUNT];
};
static_assert(sizeof(DiagLog)==256, "diagnostic log ABI");

static_assert(sizeof(DiagDevice)==400 && offsetof(DiagDevice,bar)==48, "diagnostic device ABI");
