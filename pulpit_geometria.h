#pragma once

#include <stdint.h>
#include "ui_skala.h"
#if defined(BSF_NATIVE)
#include "bursztyn_ui/czcionka.h"
#endif

// Wszystkie czesci ikony pulpitu pochodza z jednego logicznego prostokata.
// Etykiety korzystaja z wysokosci BSF; komorka zachowuje logiczna siatke ikon.
struct PulpitMetryki {
    int ikona_szer;
    int ikona_wys;
    int komorka_szer;
    int komorka_wys;
    int etykieta_szer;
    int etykieta_wys;
    int odstep_etykiety;
    int wysokosc_linii;
    int odstep_linii;
    int max_linii;
};

struct PulpitGeometriaKomorki {
    int komorka_x, komorka_y, komorka_szer, komorka_wys;
    int ikona_x, ikona_y, ikona_szer, ikona_wys;
    int etykieta_x, etykieta_y, etykieta_szer, etykieta_wys;
};

inline PulpitMetryki pulpit_metryki(uint32_t procent) {
    int size=16;
#if defined(BSF_NATIVE)
    size=BSF::ui_size();
#endif
    // Logical budget, independent of hhea ascent/descent and wrapped content.
    const int logicalLine=(size*5+3)/4;
    const int region=3*ui_skaluj(logicalLine,procent)+2*ui_skaluj(2,procent);
    const int height=ui_skaluj(48,procent)+ui_skaluj(6,procent)+region+2*ui_skaluj(3,procent);
    int line=ui_skaluj(16,procent);
#if defined(BSF_NATIVE)
    line=BursztynUI::CzcionkaUI::pobierz_wysokosc(SkalaUI::procent(procent));
#endif
    return {
        ui_skaluj(48, procent), ui_skaluj(48, procent),
        ui_skaluj(120, procent), height,
        ui_skaluj(112, procent), region,
        ui_skaluj(6, procent), line,
        ui_skaluj(2, procent), 3
    };
}

inline PulpitGeometriaKomorki pulpit_geometria_komorki(int x, int y, uint32_t procent) {
    const PulpitMetryki m = pulpit_metryki(procent);
    const int ix = x + (m.komorka_szer - m.ikona_szer) / 2;
    const int lx = x + (m.komorka_szer - m.etykieta_szer) / 2;
    return {x, y, m.komorka_szer, m.komorka_wys,
            ix, y, m.ikona_szer, m.ikona_wys,
            lx, y + m.ikona_wys + m.odstep_etykiety,
            m.etykieta_szer, m.etykieta_wys};
}

// Niezaznaczone etykiety sa biale z ograniczonym cieniem; zaznaczone bez cienia,
// poniewaz sa renderowane bezposrednio nad tapetami, a nie powierzchniami okien aplikacji.
// Kolor nie zalezy od aktywnego motywu (np. Bursztyn Jasny nie moze zamienic go na czarny).
inline constexpr uint32_t KANONICZNY_KOLOR_ETYKIETY_PULPITU = 0x00FFFFFF;
inline constexpr uint32_t KANONICZNY_KOLOR_CIENIA_PULPITU   = 0x80101010;

