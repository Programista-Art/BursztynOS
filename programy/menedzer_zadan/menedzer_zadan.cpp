/*
 * Menedzer zadan Bursztyn OS - natywna aplikacja Ring 3 (BursztynUI).
 * Jedyna instancja: loader odmawia drugiego procesu o tej samej sciezce,
 * a Menedzer Okien aktywuje wtedy istniejace okno (uruchom_lub_aktywuj).
 */
#define BURSZTYN_UI_CZCIONKA_SYSTEMOWA 1
#include "bursztyn_gui.h"
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "okno.h"

namespace {
uint64_t wywolanie_jadra(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    return bws_wywolaj(nr, a1, a2, a3, a4);
}
uint64_t zegar() { return gui_czas_monotoniczny_ns(); }
// Zadanie okresowe petli: proces spi do terminu albo zdarzenia (bez aktywnego czekania).
void probka(void* okno) { static_cast<MenedzerZadan::OknoMenedzera*>(okno)->odswiez(); }
}

extern "C" __attribute__((noreturn)) void _start() {
    BursztynUI::AplikacjaGUI app;
    auto* okno = new MenedzerZadan::OknoMenedzera;
    okno->wywolaj = wywolanie_jadra;
    okno->zegar_ns = zegar;
    okno->init();
    app.ustaw_zadanie_okresowe(250, probka, okno);   // 250 tickow * 4 ms = ~1 s
    (void)app.uruchom(*okno);
    delete okno;
    bws_zakoncz_proces();
}
