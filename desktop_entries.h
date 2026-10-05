#pragma once
#include <stdint.h>
#include <stddef.h>

// Protected virtual desktop shortcuts. No PSF objects or copied program payloads.
namespace DesktopEntries {
enum class TypIkonyAplikacji : uint8_t {
    Notatnik,
    Kalkulator,
    Husarz,
    Test,
    Snake,
    Eksplorator,
    Obrazy,
    DemoUI,
    Kontrolki2,
    Kontrolki3,
    Kontrolki4,
    Aktualizacje,
    Motywy
};

struct Launcher {const char* name; const char* path; const char* title; TypIkonyAplikacji icon;};
inline constexpr const char* Directory="/uzytkownicy/Pulpit";
inline constexpr Launcher Launchers[]={
    {"Notatnik","/programy/notatnik.cebula/notatnik.bur","Notatnik",TypIkonyAplikacji::Notatnik},
    {"Kalkulator","/programy/kalkulator.cebula/kalkulator.bur","Kalkulator",TypIkonyAplikacji::Kalkulator},
    {"Husarz","/programy/przegladarka.cebula/przegladarka.bur","Husarz",TypIkonyAplikacji::Husarz},
    {"Testy systemu","/programy/test.cebula/test.bur","Testy Bursztyn OS",TypIkonyAplikacji::Test},
    {"Snake","/gry/snake.cebula/snake.bur","Snake",TypIkonyAplikacji::Snake},
    {"Eksplorator","/programy/eksplorator.cebula/eksplorator-plikow.bur","Eksplorator Plików",TypIkonyAplikacji::Eksplorator},
    {"Przeglądarka Obrazów","/programy/przegladarka-obrazow.cebula/przegladarka-obrazow.bur","Przeglądarka Obrazów",TypIkonyAplikacji::Obrazy},
    {"Demo BursztynUI","/programy/demo_ui.cebula/demo_ui.bur","BursztynUI -",TypIkonyAplikacji::DemoUI},
    {"Kontrolki 2","/programy/kontrolki2.cebula/kontrolki2.bur","BursztynUI Kontrolki 2",TypIkonyAplikacji::Kontrolki2},
    {"Kontrolki 3","/programy/kontrolki3.cebula/kontrolki3.bur","Kontrolki 3",TypIkonyAplikacji::Kontrolki3},
    {"Aktualizacje","/programy/aktualizacje.cebula/aktualizacje.bur","Aktualizacje Systemu",TypIkonyAplikacji::Aktualizacje},
    {"Motywy","/programy/motywy.cebula/motywy.bur","Motywy",TypIkonyAplikacji::Motywy},
    {"Kontrolki 4","/programy/kontrolki4.cebula/kontrolki4.bur","Kontrolki 4",TypIkonyAplikacji::Kontrolki4},
};
inline constexpr int Count=sizeof(Launchers)/sizeof(Launchers[0]);
inline bool is_desktop(const char* path) {
    if(!path)return false;
    size_t i=0;
    while(Directory[i] && path[i]==Directory[i])++i;
    return !Directory[i] && !path[i];
}
inline const Launcher* launcher(int id) {return id>=0 && id<Count?&Launchers[id]:nullptr;}
}
