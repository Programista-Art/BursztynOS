#pragma once

#include <stdint.h>
#include "ui_skala.h"

// Wszystkie czesci ikony pulpitu pochodza z jednego logicznego prostokata.
// Komorka rezerwuje dwie linie fontu 16 px oraz odstep od obrazu.
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
};

struct PulpitGeometriaKomorki {
    int komorka_x, komorka_y, komorka_szer, komorka_wys;
    int ikona_x, ikona_y, ikona_szer, ikona_wys;
    int etykieta_x, etykieta_y, etykieta_szer, etykieta_wys;
};

inline PulpitMetryki pulpit_metryki(uint32_t procent) {
    return {
        ui_skaluj(48, procent), ui_skaluj(48, procent),
        ui_skaluj(120, procent), ui_skaluj(100, procent),
        ui_skaluj(112, procent), ui_skaluj(34, procent),
        ui_skaluj(6, procent), ui_skaluj(16, procent),
        ui_skaluj(2, procent)
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

// Etykiety ikon na pulpicie sa celowym wyjatkiem: zawsze biale z cieniem,
// poniewaz sa renderowane bezposrednio nad tapetami, a nie powierzchniami okien aplikacji.
// Kolor nie zalezy od aktywnego motywu (np. Bursztyn Jasny nie moze zamienic go na czarny).
inline constexpr uint32_t KANONICZNY_KOLOR_ETYKIETY_PULPITU = 0x00FFFFFF;
inline constexpr uint32_t KANONICZNY_KOLOR_CIENIA_PULPITU   = 0x00101010;

