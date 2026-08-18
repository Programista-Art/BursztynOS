#pragma once
#include <stdint.h>

struct alignas(16) XhciTrb {
    uint32_t dword0;
    uint32_t dword1;
    uint32_t dword2;
    uint32_t dword3;
};
static_assert(sizeof(XhciTrb) == 16, "xHCI TRB musi miec 16 bajtow");

namespace xhci_trb {
constexpr uint32_t CYCLE = 1U;
constexpr uint32_t TOGGLE_CYCLE = 1U << 1;
constexpr uint32_t TYPE_SHIFT = 10U;
constexpr uint32_t TYPE_MASK = 0x3FU << TYPE_SHIFT;
constexpr uint32_t TYPE_LINK = 6U;
constexpr uint32_t TYPE_ENABLE_SLOT = 9U;
constexpr uint32_t TYPE_DISABLE_SLOT = 10U;
constexpr uint32_t TYPE_ADDRESS_DEVICE = 11U;
constexpr uint32_t TYPE_CONFIGURE_ENDPOINT = 12U;
constexpr uint32_t TYPE_EVALUATE_CONTEXT = 13U;
constexpr uint32_t TYPE_SETUP_STAGE = 2U;
constexpr uint32_t TYPE_NORMAL = 1U;
constexpr uint32_t TYPE_DATA_STAGE = 3U;
constexpr uint32_t TYPE_STATUS_STAGE = 4U;
constexpr uint32_t TYPE_TRANSFER_EVENT = 32U;
constexpr uint32_t TYPE_COMMAND_COMPLETION = 33U;
constexpr uint32_t TYPE_PORT_STATUS_CHANGE = 34U;
inline constexpr uint32_t type(uint32_t value) {
    return (value & TYPE_MASK) >> TYPE_SHIFT;
}
inline constexpr uint32_t type_field(uint32_t value) {
    return (value & 0x3FU) << TYPE_SHIFT;
}
}
