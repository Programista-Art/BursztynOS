
#include <stdint.h>
#include "bws_api.h"
#include "bursztyn_gui.h"

struct NaglowekBur {
    uint8_t magia[4];
    uint64_t punkt_wejscia;
    uint64_t tekst_przesuniecie;
    uint64_t tekst_rozmiar;
    uint64_t tekst_wirtualny;
    uint64_t dane_przesuniecie;
    uint64_t dane_rozmiar;
    uint64_t dane_wirtualny;
} __attribute__((packed));

extern "C" __attribute__((noreturn)) void _start();

extern "C" {
__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},
    (uint64_t)&_start,
    0x1000ULL, 0x4e000ULL, 0x1001000ULL,
    0x4f000ULL, 0x20000ULL, 0x104f000ULL
};
}

extern "C" __attribute__((noreturn)) void _start() {
      bws_wywolaj(BWS_URUCHOM_PROGRAM,(uint64_t)"/programy/fpu-a.cebula/fpu-a.bur",0,0,0);
      unsigned iteration=0;
      static const char* modes[]={"g","t","r","h","s","c"};
      for (;;) {
        const char* mode=iteration<6 ? modes[iteration] : "";
        if(bws_wywolaj(BWS_URUCHOM_PROGRAM,(uint64_t)"/programy/test.cebula/test.bur",(mode[0] ? (uint64_t)mode : 0),0,0)) ++iteration;
        auto until=bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS,0,0,0,0)+500000000ULL;
        while(bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS,0,0,0,0)<until) asm volatile("pause");
      }
    }
