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
    Motywy,
    TestDzwieku,
    Diagnostyka,
    UstawieniaEkranu
};

struct Launcher {
    const char* name;
    const char* path;
    const char* title;
    TypIkonyAplikacji icon;
    const char* id;
    const char* icon_png;
};
inline constexpr const char* Directory="/uzytkownicy/Pulpit";
inline constexpr Launcher Launchers[]={
    {"Notatnik","/programy/notatnik.cebula/notatnik.bur","Notatnik",TypIkonyAplikacji::Notatnik,"notatnik","/ikonki/app_notatnik.png"},
    {"Kalkulator","/programy/kalkulator.cebula/kalkulator.bur","Kalkulator",TypIkonyAplikacji::Kalkulator,"kalkulator","/ikonki/app_kalkulator.png"},
    {"Husarz","/programy/przegladarka.cebula/przegladarka.bur","Husarz",TypIkonyAplikacji::Husarz,"husarz","/ikonki/app_husarz.png"},
    {"Testy systemu","/programy/test.cebula/test.bur","Testy Bursztyn OS",TypIkonyAplikacji::Test,"testy-systemu","/ikonki/app_testy.png"},
    {"Snake","/gry/snake.cebula/snake.bur","Snake",TypIkonyAplikacji::Snake,"snake","/ikonki/app_snake.png"},
    {"Eksplorator","/programy/eksplorator.cebula/eksplorator-plikow.bur","Eksplorator Plików",TypIkonyAplikacji::Eksplorator,"eksplorator","/ikonki/app_eksplorator.png"},
    {"Przeglądarka Obrazów","/programy/przegladarka-obrazow.cebula/przegladarka-obrazow.bur","Przeglądarka Obrazów",TypIkonyAplikacji::Obrazy,"obrazy","/ikonki/app_obrazy.png"},
    {"Demo BursztynUI","/programy/demo_ui.cebula/demo_ui.bur","BursztynUI -",TypIkonyAplikacji::DemoUI,"demo-ui","/ikonki/app_demo_ui.png"},
    {"Kontrolki 2","/programy/kontrolki2.cebula/kontrolki2.bur","BursztynUI Kontrolki 2",TypIkonyAplikacji::Kontrolki2,"kontrolki2","/ikonki/app_kontrolki2.png"},
    {"Kontrolki 3","/programy/kontrolki3.cebula/kontrolki3.bur","Kontrolki 3",TypIkonyAplikacji::Kontrolki3,"kontrolki3","/ikonki/app_kontrolki3.png"},
    {"Aktualizacje","/programy/aktualizacje.cebula/aktualizacje.bur","Aktualizacje Systemu",TypIkonyAplikacji::Aktualizacje,"aktualizacje","/ikonki/app_aktualizacje.png"},
    {"Ustawienia","/programy/ustawienia.cebula/ustawienia.bur","Ustawienia",TypIkonyAplikacji::Motywy,"ustawienia","/ikonki/app_ustawienia.png"},
    {"Kontrolki 4","/programy/kontrolki4.cebula/kontrolki4.bur","Kontrolki 4",TypIkonyAplikacji::Kontrolki4,"kontrolki4","/ikonki/app_kontrolki4.png"},
    {"Test dźwięku","/programy/test_dzwieku.cebula/test_dzwieku.bur","Test dźwięku",TypIkonyAplikacji::TestDzwieku,"test-dzwieku","/ikonki/app_test_dzwieku.png"},
    {"Diagnostyka systemu","/programy/diagnostyka.cebula/diagnostyka.bur","Diagnostyka systemu",TypIkonyAplikacji::Diagnostyka,"diagnostyka-systemu","/ikonki/app_diagnostyka.png"},
    // Na koncu: istniejace indeksy i zapisane pozycje skrotow (klucz = id) bez zmian;
    // nowy skrot bez zapisanej pozycji trafia do wolnej komorki siatki.
    {"Ustawienia ekranu","/programy/ekran.cebula/ekran.bur","Ustawienia ekranu",TypIkonyAplikacji::UstawieniaEkranu,"ustawienia-ekranu","/ikonki/app_ustawienia_ekranu.png"},
};
inline constexpr int Count=sizeof(Launchers)/sizeof(Launchers[0]);
// Logical UI budget, independent of grid capacity and filesystem limits.
inline constexpr int FileCapacity=128;
inline constexpr int ModelCapacity=FileCapacity+Count;
inline bool is_desktop(const char* path) {
    if(!path)return false;
    size_t i=0;
    while(Directory[i] && path[i]==Directory[i])++i;
    return !Directory[i] && !path[i];
}
inline const Launcher* launcher(int id) {return id>=0 && id<Count?&Launchers[id]:nullptr;}
inline const char* icon_png_for(TypIkonyAplikacji typ) {
    for(int i=0;i<Count;++i)if(Launchers[i].icon==typ)return Launchers[i].icon_png;
    return nullptr;
}
}
