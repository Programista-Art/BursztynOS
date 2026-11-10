#pragma once
#include <stddef.h>
#include <stdint.h>
/* Success requires a complete fresh hardware-backed seed. No clock/PID fallback. */
extern "C" bool entropia_pobierz_bajty(uint8_t*, size_t);
