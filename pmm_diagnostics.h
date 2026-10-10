#pragma once
#include <stdint.h>
#include "virtio_input_activation_diag.h"

#if BURSZTYNDIAGVIRTIOINPUTINIT || defined(BURSZTYN_PMM_HOST_TEST)
enum class PmmPageOwner : uint8_t {
    UNKNOWN, FREE, ALLOCATED_PMM, RESERVED_MODULE, KERNEL, FRAMEBUFFER,
    RESERVED_FIRMWARE, MMIO
};
// Nonblocking bootstrap snapshot. UNKNOWN if an allocator transaction is live.
PmmPageOwner pmm_diagnostic_page_owner(uint64_t physical);
uint32_t pmm_diagnostic_lock_state();
void pmm_diagnostic_dump_modules();
#endif
