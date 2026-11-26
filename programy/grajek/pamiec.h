#pragma once
/*
 * Grajek - alokator z twardym limitem pojedynczej alokacji (dekoder MP3, bufory okladki).
 * Ring 3: prywatna sterta procesu (gui_malloc/gui_free). Host: malloc/free (testy z ASan).
 */
#include <stddef.h>
#include <stdint.h>

#if defined(GRAJEK_RING3) && GRAJEK_RING3
#include "bursztyn_gui.h"
namespace Grajek {
inline void* alokuj_surowo(size_t n) { return gui_malloc(static_cast<unsigned long>(n)); }
inline void zwolnij_surowo(void* p) { if (p) gui_free(p); }
}
#else
#include <stdlib.h>
namespace Grajek {
inline void* alokuj_surowo(size_t n) { return malloc(n); }
inline void zwolnij_surowo(void* p) { free(p); }
}
#endif

namespace Grajek {
constexpr size_t MAKS_POJEDYNCZA_ALOKACJA = 8U * 1024U * 1024U;
inline void* alokuj(size_t n) { return (n == 0 || n > MAKS_POJEDYNCZA_ALOKACJA) ? nullptr : alokuj_surowo(n); }
inline void zwolnij(void* p) { zwolnij_surowo(p); }
}
