#include "loader.h"

// Eksportowane funkcje z Menedżerów Pamięci (PMM/VMM) oraz Systemu Plików (BSP)
extern "C" void* ZaalokujRamke(); // Pobiera wolną ramkę fizyczną 4KB z PMM
extern "C" void ZmapujStrone(void* wirtualny, void* fizyczny, uint64_t flagi); // Mapuje stronę w VMM
extern "C" void* PobierzAktualnePML4(); // Zwraca wskaźnik na aktywną tablicę stron j