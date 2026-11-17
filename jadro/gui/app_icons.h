#pragma once
#include <stddef.h>
#include "app_identity.h"
#include "desktop_entries.h"

// Rejestr ikon aplikacji: program (sciezka wykonywalna = stabilna tozsamosc
// loadera) -> ikona PNG. Zrodlem prawdy sa skroty pulpitu (DesktopEntries);
// tu dochodza tylko aplikacje dostepne wylacznie z menu Start. Pasek zadan
// i menu Start korzystaja z tych samych plikow ikon - bez osobnego zestawu.
// Tytuly okien (zmienne: nazwa dokumentu, tlumaczenia) nie sa tu uzywane.
namespace AppIcons {

struct Wpis {
    const char* executable;
    const char* icon_png;
};

inline constexpr Wpis Dodatkowe[] = {
    {"/shell.bur", "/ikonki/app_shell.png"},
    {"/programy/tapety.cebula/tapety.bur", "/ikonki/app_tapety.png"},
    {AppIdentity::DisplaySettings, "/ikonki/app_ustawienia_ekranu.png"},
};
inline constexpr int LiczbaDodatkowych = sizeof(Dodatkowe) / sizeof(Dodatkowe[0]);
inline constexpr int Count = DesktopEntries::Count + LiczbaDodatkowych;

inline const char* executable(int i) {
    if (i >= 0 && i < DesktopEntries::Count) return DesktopEntries::Launchers[i].path;
    if (i >= DesktopEntries::Count && i < Count) return Dodatkowe[i - DesktopEntries::Count].executable;
    return nullptr;
}
inline const char* icon_png(int i) {
    if (i >= 0 && i < DesktopEntries::Count) return DesktopEntries::Launchers[i].icon_png;
    if (i >= DesktopEntries::Count && i < Count) return Dodatkowe[i - DesktopEntries::Count].icon_png;
    return nullptr;
}

inline bool rowne(const char* a, const char* b) {
    if (!a || !b) return false;
    size_t i = 0;
    while (a[i] && a[i] == b[i]) ++i;
    return a[i] == b[i];
}

// Indeks wpisu dla dokladnej sciezki programu albo -1 (ikona ogolna).
inline int indeks_programu(const char* path) {
    for (int i = 0; i < Count; ++i)
        if (rowne(executable(i), path)) return i;
    return -1;
}

inline const char* icon_for_executable(const char* path) {
    return icon_png(indeks_programu(path));
}

}  // namespace AppIcons
