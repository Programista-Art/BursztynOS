#pragma once

#include "okno.h"
#include "rysowanie.h"
#include "../bursztyn_gui.h"

namespace BursztynUI {

class AplikacjaGUI {
public:
    AplikacjaGUI() = default;
    virtual ~AplikacjaGUI() = default;

    int skala_dpi() const { return skala_dpi_; }

    void ustaw_glowne_okno(Okno* okno) {
        glowne_okno_ = okno;
        if (glowne_okno_) {
            glowne_okno_->ustaw_skale_dpi(skala_dpi_);
        }
    }

    Okno* glowne_okno() const { return glowne_okno_; }

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
            bool ma_animacje = glowne_okno_ && glowne_okno_->czy_wymaga_animacji();
            bool odebrano = false;

            if (ma_animacje) {
                odebrano = gui_pobierz_zdarzenie(&ev);
                if (!odebrano) {
                    uint64_t teraz = gui_czas_monotoniczny_ns();
                    glowne_okno_->krok_animacji(teraz);
                    if (!glowne_okno_->czy_zminimalizowane()) {
                        glowne_okno_->rysuj(ctx);
                        gui_odswiez();
                    }
                    for (int p = 0; p < 2000; ++p) {
                        asm volatile("pause" ::: "memory");
                    }
                }
            } else {
                odebrano = gui_czekaj_na_zdarzenie(&ev);
            }

            if (odebrano) {
                glowne_okno_->obsluz_zdarzenie(ev);

                // Sprawdź czy są dodatkowe zdarzenia w kolejce
                while (gui_pobierz_zdarzenie(&ev)) {
                    glowne_okno_->obsluz_zdarzenie(ev);
                }

                // Odśwież widok
                if (!glowne_okno_->czy_zminimalizowane()) {
                    glowne_okno_->rysuj(ctx);
                    gui_odswiez();
                }
            }
        }

        return 0;
    }

private:
    Okno* glowne_okno_{nullptr};
    bool czy_dziala_{false};
    int skala_dpi_{1};
};

} // namespace BursztynUI
