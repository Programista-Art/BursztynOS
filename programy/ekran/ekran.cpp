/* Ustawienia ekranu: pojedyncze okno, prawdziwe mozliwosci HAL przez BWS69/70. */
#include "../../loader.h"
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "klient.h"
#include "ekran_okno.h"

using namespace BursztynUI;

extern "C" void _start();
static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 0x1000ULL;
static constexpr uint64_t BUR_TEKST_ROZMIAR = 0x38000ULL;
static constexpr uint64_t BUR_TEKST_WIRTUALNY = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE = 0x39000ULL;
static constexpr uint64_t BUR_DANE_ROZMIAR = 0x20000ULL;
static constexpr uint64_t BUR_DANE_WIRTUALNY = 0x1039000ULL;

extern "C" {
__attribute__((section(".naglowek"), used)) NaglowekBur naglowek = {
    {'B','U','R','\0'}, (uint64_t)&_start,
    BUR_TEKST_PRZESUNIECIE, BUR_TEKST_ROZMIAR, BUR_TEKST_WIRTUALNY,
    BUR_DANE_PRZESUNIECIE, BUR_DANE_ROZMIAR, BUR_DANE_WIRTUALNY
};
}
static_assert(sizeof(NaglowekBur) == 60U, "Naglowek .bur musi miec 60 bajtow");

extern "C" __attribute__((noreturn)) void _start() {
    AplikacjaGUI app;
    uint64_t endpoint = ipc_create();
    EkranZadanie req{};
    req.operacja = EKRAN_STAN;
    EkranOdpowiedz stan{};
    if (!ekran_zapytaj(endpoint, req, &stan)) gui_zakoncz_aplikacje();

    OknoEkranu okno;
    okno.inicjalizuj(stan, endpoint);
    app.ustaw_glowne_okno(&okno);

    app.uruchom();
    gui_zakoncz_aplikacje();
}
