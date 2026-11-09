#pragma once
#include "pointing_diag.h"
#ifndef BURSZTYNDIAGINPUTCOEXISTENCE
#define BURSZTYNDIAGINPUTCOEXISTENCE 0
#endif
inline void input_coexistence_diag(const char* stage, const int64_t* values, size_t count) {
#if BURSZTYNDIAGINPUTCOEXISTENCE
    static unsigned lines = 0;
    if (lines < 256) {++lines;pointing_diag(stage, values, count);}
#else
    (void)stage; (void)values; (void)count;
#endif
}
