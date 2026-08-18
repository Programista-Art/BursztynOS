#pragma once
#include <stddef.h>
#include <stdint.h>

struct DmaBuffer {
    void* virtual_address;
    uint64_t physical_address;
    size_t size;
    size_t page_count;
};

bool dma_allocate(size_t size, size_t alignment, DmaBuffer* out);
void dma_release(DmaBuffer* buffer);
