#pragma once
#include <stddef.h>
namespace AppIdentity {
// Executable paths are the loader's stable singleton keys, never translated titles.
inline constexpr char Settings[]="/programy/ustawienia.cebula/ustawienia.bur";
inline constexpr char DisplaySettings[]="/programy/ekran.cebula/ekran.bur";
template<size_t N> inline bool equal(const char (&stored)[N],const char* expected) {
    if(!expected)return false;
    for(size_t i=0;i<N;++i){if(stored[i]!=expected[i])return false;if(!stored[i])return true;}
    return false;
}
}
