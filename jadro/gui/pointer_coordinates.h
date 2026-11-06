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
