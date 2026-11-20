#pragma once
#include <stdint.h>
#include <stddef.h>

#define BWS_PCM 110
#define BWS_PCM_V1 1
#define BWS_PCM_MAX_FRAMES 256
enum BwsPcmOperation { PCM_OPEN = 1, PCM_WRITE, PCM_QUERY, PCM_GAIN, PCM_CLOSE, PCM_FINISH };
enum BwsPcmError {
    PCM_OK = 0, PCM_INVALID = -1, PCM_UNSUPPORTED = -2, PCM_LIMIT = -3,
    PCM_HANDLE = -4, PCM_FULL = -5, PCM_BUSY = -6, PCM_DEVICE = -7, PCM_MEMORY = -8
};
struct BwsPcmFormat {
    uint32_t version, size, rate, channels, encoding, reserved;
};
struct BwsPcmState {
    uint32_t version, size, queued, capacity, volume, muted, channels, rate;
};
#ifdef __cplusplus
static_assert(sizeof(BwsPcmFormat) == 24 && offsetof(BwsPcmFormat, rate) == 8);
static_assert(sizeof(BwsPcmState) == 32 && offsetof(BwsPcmState, queued) == 8);
#endif
