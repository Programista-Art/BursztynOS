/*
 * Mechanizm: Poziomy Zaufania Bursztyna (PZB) i Flagi Uprawnień (BZL)
 * Opis: Abstrakcja logicznych uprawnień nadzorująca działania programów w Ring 3.
 */

#pragma once
#include <stdint.h>

// Poziomy zaufania PZB (0-5)
#define PZB_JADRO         0
#define PZB_STEROWNIKI    1
#define PZB_USLUGI        2
#define PZB_ZAUFANE       3
#define PZB_UZYTKOWNIK    4
#define PZB_PIASKOWNICA   5

// Flagi uprawnień systemowych
#define PRAWO_PLIKI_CZYTAJ      (1 << 0)
#define PRAWO_PLIKI_ZAPISZ      (1 << 1)
#define PRAWO_SIEC              (1 << 2)
#define PRAWO_GUI               (1 << 3)
#define PRAWO_URUCHOM_PROGRAM   (1 << 4)
#define PRAWO_SYSTEM_CONFIG     (1 << 5)
#define PRAWO_STEROWNIK         (1 << 6)
#define PRAWO_DEBUG             (1 << 7)

// Struktura opisująca aktualny proces i jego autoryzację
typedef struct proces {
    uint64_t pid;
    uint8_t  poziom_zaufania;     
    uint64_t uprawnienia;        
    void* przestrzen_adresowa;
} proces_t;

// Globalna zmienna jądra reprezentująca działający program
extern proces_t aktywny_proces;