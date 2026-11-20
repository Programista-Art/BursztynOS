#pragma once
#include <stdint.h>

// Bounded global accounting at BWS WRITE and QUERY entry/return. No user pointers.
namespace PcmWriteDiag {
inline uint64_t calls, small, large, accepted;
inline uint64_t errors[9]{};
inline uint64_t query_calls, query_busy, query_errors;
inline void request(uint64_t frames) {
    __atomic_add_fetch(&calls, 1, __ATOMIC_RELAXED);
    if (frames && frames <= 256) __atomic_add_fetch(&small, 1, __ATOMIC_RELAXED);
    else if (frames <= 512 && frames) __atomic_add_fetch(&large, 1, __ATOMIC_RELAXED);
}
inline int64_t result(int64_t value) {
    if (value > 0) __atomic_add_fetch(&accepted, value, __ATOMIC_RELAXED);
    else if (value < 0 && value >= -8)
        __atomic_add_fetch(&errors[-value], 1, __ATOMIC_RELAXED);
    return value;
}
inline int64_t query_result(int64_t value) {
    __atomic_add_fetch(&query_calls, 1, __ATOMIC_RELAXED);
    if (value == -6) __atomic_add_fetch(&query_busy, 1, __ATOMIC_RELAXED);
    else if (value < 0) __atomic_add_fetch(&query_errors, 1, __ATOMIC_RELAXED);
    return value;
}
inline uint64_t read(const uint64_t& counter) {
    return __atomic_load_n(&counter, __ATOMIC_RELAXED);
}
}
