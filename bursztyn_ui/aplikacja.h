#pragma once

#include "okno.h"
#include "rysowanie.h"
#include "../bursztyn_gui.h"

namespace BursztynUI {

class AplikacjaGUI {
public:
    struct StatystykiPetli {
        uint64_t iteracje{0};
        uint64_t odczyty_hpet{0};
        uint64_t wywolania_animacji{0};
        uint64_t odrysowania{0};
        uint64_t odebrane_zdarzenia{0};
    };

    AplikacjaGUI() {
        GuiMonitorInfo mon{};
        if(gui_pobierz_monitor(0,&mon)&&ui_skala_poprawna(mon.scale_x1000/10))
            ui_skala_procent=mon.scale_x1000/10;
        skala_dpi_=ui_skala();
    }
    virtual ~AplikacjaGUI() {
        GuiMonitorInfo mon{};
        if(gui_pobierz_monitor(0,&mon)&&ui_skala_poprawna(mon.scale_x1000/10))
            ui_skala_procent=mon.scale_x1000/10;
        skala_dpi_=ui_skala();
    }

    int skala_dpi() const { return skala_dpi_; }

    void ustaw_glowne_okno(Okno* okno) {
        glowne_okno_ = okno;
        if (glowne_okno_) {
            glowne_okno_->ustaw_skale_dpi(skala_dpi_);
        }
    }

    Okno* glowne_okno() const { return glowne_okno_; }
    const StatystykiPetli& statystyki_petli() const { return statystyki_; }

    void zakoncz() {
        czy_dziala_ = false;
        gui_zakoncz_aplikacje();
    }

    int uruchom(Okno& okno) {
        ustaw_glowne_okno(&okno);
        return uruchom();
    }

    int uruchom() {
        if (!glowne_okno_) return -1;
        czy_dziala_ = true;

        // Geometria zadana przez aplikacje nie moze wyjsc poza obszar
        // roboczy zwrocony przez BWS62 (np. zaslonic paska zadan).
        glowne_okno_->dopasuj_do_obszaru_roboczego(true);

        int wx = glowne_okno_->x();
        int wy = glowne_okno_->y();
        int ww = glowne_okno_->szerokosc();
        int wh = glowne_okno_->wysokosc();

        bws_utworz_warstwe(wx, wy, ww, wh, 10);
        gui_ustaw_tytul_okna(glowne_okno_->tytul());
        gui_ustaw_geometrie_okna(wx, wy, ww, wh);
        glowne_okno_->przelicz_uklad();

        // Pierwszy pełny cykl rysowania
        KontekstRysowania ctx;
        glowne_okno_->rysuj(ctx, true);
        gui_odswiez();

        bws_zdarzenie ev{};
        while (czy_dziala_) {
            ++statystyki_.iteracje;
            bool ma_animacje = glowne_okno_ && glowne_okno_->czy_wymaga_animacji();
            bool odebrano = false;

            if (ma_animacje) {
                ++statystyki_.odczyty_hpet;
                uint64_t teraz = gui_czas_monotoniczny_ns();
                glowne_okno_->krok_animacji(teraz);
                ++statystyki_.wywolania_animacji;
                if (!glowne_okno_->czy_zminimalizowane() && glowne_okno_->czy_wymaga_odrysowania()) {
                    glowne_okno_->rysuj(ctx);
                    gui_odswiez();
                    ++statystyki_.odrysowania;
                }
                // Usypiamy proces w schedulerze na czas do klatki (8 tickow = ~32 ms przy 250 Hz)
                odebrano = gui_czekaj_na_zdarzenie_z_timeoutem(&ev, 8);
            } else {
                odebrano = gui_czekaj_na_zdarzenie(&ev);
            }

            if (odebrano) {
                ++statystyki_.odebrane_zdarzenia;
                glowne_okno_->obsluz_zdarzenie(ev);

                // Sprawdź czy są dodatkowe zdarzenia w kolejce
                while (gui_pobierz_zdarzenie(&ev)) {
                    ++statystyki_.odebrane_zdarzenia;
                    glowne_okno_->obsluz_zdarzenie(ev);
                }

                // Odśwież widok
                if (!glowne_okno_->czy_zminimalizowane() && glowne_okno_->czy_wymaga_odrysowania()) {
                    glowne_okno_->rysuj(ctx);
                    gui_odswiez();
                    ++statystyki_.odrysowania;
                }
            }
        }

        return 0;
    }

private:
    Okno* glowne_okno_{nullptr};
    bool czy_dziala_{false};
    SkalaUI skala_dpi_{1};
    StatystykiPetli statystyki_{};
};

} // namespace BursztynUI
