#pragma once
#include <stdint.h>
// Screen-down coordinates throughout InputDevice. Clamp only the accumulated
// position, never a relative delta. Invalid geometry preserves the position.
inline int pointer_accumulate(int current, int64_t delta, int dimension) {
    if (dimension <= 0) return current;
    const int64_t position = int64_t(current) + delta;
    return position < 0 ? 0 : position >= dimension ? dimension - 1 : int(position);
}
// Linux/VirtIO's 32-bit value is a wire bit pattern, not an unsigned delta.
inline int32_t input_signed_value(uint32_t value) {
    return value <= INT32_MAX ? int32_t(value) : -1 - int32_t(UINT32_MAX - value);
}
// Absolute axis -> pixel with endpoint semantics: min -> 0, max -> dimension-1,
// rounded to the nearest pixel (truncation biased every position towards the
// origin; with QEMU's px*32767/size encoding by up to 2 px at the far edge).
// Values are clamped to [min,max]; arithmetic is 64-bit and cannot overflow:
// offset < 2^32 and dimension < 2^31. Returns -1 for an unusable range.
inline int pointer_map_absolute(int32_t raw, int32_t min, int32_t max, int dimension) {
    if (dimension <= 0 || max <= min) return -1;
    const int32_t v = raw < min ? min : raw > max ? max : raw;
    const int64_t offset = int64_t(v) - min, range = int64_t(max) - min;
    return int((offset * int64_t(dimension - 1) + range / 2) / range);
}
