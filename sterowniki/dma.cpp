#include "sterowniki/dma.h"
#include "pamiec.h"

namespace {
constexpr size_t PAGE = 4096;
void zero(void* p, size_t n) {
    volatile uint8_t* b = static_cast<volatile uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) b[i] = 0;
}
}

bool dma_allocate(size_t size, size_t alignment, DmaBuffer* out) {
    if (!out) return false;
    *out = {};
    if (size == 0 || alignment == 0 || (alignment & (alignment - 1U)) != 0 ||
        alignment > PAGE || size > SIZE_MAX - (PAGE - 1U)) return false;
    const size_t pages = (size + PAGE - 1U) / PAGE;
    void* const physical = ZaalokujCiagleRamki(pages);
    if (!physical) return false;
    const uint64_t address = reinterpret_cast<uint64_t>(physical);
    if ((address & static_cast<uint64_t>(alignment - 1U)) != 0) {
        ZwolnijCiagleRamki(physical, pages);
        return false;
    }
    /* PMM wydaje obecnie RAM z identity-map; virtual i physical sa rowne. */
    zero(physical, pages * PAGE);
    out->virtual_address = physical;
    out->physical_address = address;
    out->size = size;
    out->page_count = pages;
    return true;
}

void dma_release(DmaBuffer* buffer) {
    if (!buffer || !buffer->virtual_address || buffer->page_count == 0) return;
    ZwolnijCiagleRamki(reinterpret_cast<void*>(buffer->physical_address),
                      buffer->page_count);
    *buffer = {};
}
