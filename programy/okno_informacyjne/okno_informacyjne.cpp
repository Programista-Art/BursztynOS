/*
 * Bursztyn OS - okno informacyjne (okno podrzedne BursztynUI, Ring 3).
 *
 * Uruchamiane WYLACZNIE przez jadro (BWS 101) w imieniu aplikacji-wlasciciela: osobny proces z wlasnym oknem
 * (prawdziwe okno natywne, modeless). Tresc i tytul odbiera przez BWS 102 (tylko proces zarejestrowany jako dziecko).
 * Ma wylacznie prawo GUI: bez plikow, sieci i uruchamiania programow.
 */
#include "loader.h"
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "../../bursztyn_ui/okno_podrzedne.h"

using namespace BursztynUI;

extern "C" void _start();
extern "C" {
    __attribute__((section(".naglowek"), used))
    NaglowekBur naglowek = {
        {'B', 'U', 'R', '\0'}, (uint64_t)&_start, 0x1000ULL, 0x40000ULL, 0x1001000ULL,
        0x41000ULL, 131072ULL, 0x1041000ULL
    };
}

namespace {
char g_bufor[4096 + 64 + 8];
}

extern "C" __attribute__((noreturn)) void _start() {
    const uint32_t n = gui_okno_podrzedne_tresc(g_bufor, sizeof(g_bufor));
    const char* tytul = "Informacje";
    const char* tresc = "Brak treści okna.";
    size_t dl = 17;
    if (n > 2) {                                    // "tytul\n\ntresc"
        uint32_t i = 0;
        while (i + 1 < n && !(g_bufor[i] == '\n' && g_bufor[i + 1] == '\n')) ++i;
        if (i + 1 < n && i > 0) {
            g_bufor[i] = '\0';
            tytul = g_bufor;
            tresc = g_bufor + i + 2;
            dl = n - (i + 2);
        }
    }
    AplikacjaGUI app;
    int lw = 520, lh = 360;
    OknoInformacyjne::oblicz_rozmiar(tresc, dl, &lw, &lh);
    OknoInformacyjne okno(tytul, lw, lh);
    // Kaskada: okna roznych rodzajow nie przykrywaja sie dokladnie (deterministyczne przesuniecie z tytulu).
    unsigned h = 0;
    for (const char* p = tytul; *p; ++p) h = h * 31U + static_cast<unsigned char>(*p);
    okno.wysrodkuj();
    okno.ustaw_pozycje(okno.x() + static_cast<int>(h % 5U) * 28, okno.y() + static_cast<int>(h % 5U) * 24);
    okno.ustaw_tresc(tresc, dl);
    app.uruchom(okno);
    bws_zakoncz_proces();
}
