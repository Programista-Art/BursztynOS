/*
 * Kalendarz Bursztyn OS - natywna aplikacja Ring 3 (BursztynUI). Uprawnienia: tylko "okna" (GUI); data z BWS 9,
 * ktory nie wymaga zadnego prawa. Jedna instancja: menu Start uzywa uruchom_lub_aktywuj (aktywuje istniejace okno).
 */
#define BURSZTYN_UI_CZCIONKA_SYSTEMOWA 1
#include "bursztyn_gui.h"
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "okno.h"

namespace {
// Zadanie okresowe petli: proces spi do terminu albo zdarzenia (bez aktywnego czekania).
void sprawdz_date(void* okno) { static_cast<KalendarzApp::OknoKalendarza*>(okno)->sprawdz_date(); }
}

extern "C" __attribute__((noreturn)) void _start() {
    BursztynUI::AplikacjaGUI app;
    auto* okno = new KalendarzApp::OknoKalendarza;
    okno->init();
    app.ustaw_zadanie_okresowe(7500, sprawdz_date, okno);   // 7500 tickow * 4 ms = ~30 s
    (void)app.uruchom(*okno);
    delete okno;
    bws_zakoncz_proces();
}
