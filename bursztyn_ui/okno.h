#pragma once

#include "kontrolka.h"
#include "panel.h"
#include "zdarzenia.h"
#include "rysowanie.h"
#include "../bursztyn_gui.h"

namespace BursztynUI {

class Okno {
public:
    static constexpr int WYSOKOSC_BELKI_TYTULOWEJ = 28;

    Okno(const char* tytul_okna = "Okno BursztynUI", int szerokosc = 480, int wysokosc = 320)
        : szerokosc_(szerokosc), wysokosc_(wysokosc) {
        ustaw_tytul(tytul_okna);
        domyslny_korzen_ = new Panel(Orientacja::Pionowa);
        kontrolka_glowna_ = domyslny_korzen_;
    }

    virtual ~Okno() {
        if (domyslny_korzen_) {
            delete domyslny_korzen_;
            domyslny_korzen_ = nullptr;
        }
    }

    const char* tytul() const { return tytul_; }
    void ustaw_tytul(const char* nowy_tytul) {
        if (!nowy_tytul) nowy_tytul = "";
        size_t i = 0;
        for (; i < sizeof(tytul_) - 1 && nowy_tytul[i] != '\0'; ++i) {
            tytul_[i] = nowy_tytul[i];
        }
        tytul_[i] = '\0';
        gui_ustaw_tytul_okna(tytul_);
        czy_brudne_ = true;
    }

    int x() const { return x_; }
    int y() const { return y_; }
    int szerokosc() const { return szerokosc_; }
    int wysokosc() const { return wysokosc_; }

    bool czy_zmaksymalizowane() const { return czy_zmaksymalizowane_; }
    bool czy_zminimalizowane() const { return czy_zminimalizowane_; }
    bool czy_przeciagane() const { return czy_przeciagane_; }

    void ustaw_pozycje(int x, int y) {
        x_ = x;
        y_ = y;
    }

    void ustaw_rozmiar(int w, int h) {
        if (w < 100) w = 100;
        if (h < 60) h = 60;
        if (szerokosc_ != w || wysokosc_ != h) {
            szerokosc_ = w;
            wysokosc_ = h;
            przelicz_uklad();
            czy_brudne_ = true;
        }
    }

    void ustaw_geometrie(int x, int y, int w, int h) {
        x_ = x;
        y_ = y;
        ustaw_rozmiar(w, h);
    }

    Kontrolka* kontrolka_glowna() const { return kontrolka_glowna_; }
    void ustaw_kontrolke_glowna(Kontrolka* korzen) {
        if (kontrolka_glowna_ != korzen) {
            kontrolka_glowna_ = korzen;
            if (kontrolka_glowna_) {
                kontrolka_glowna_->ustaw_skale_dpi(skala_dpi_);
            }
            przelicz_uklad();
            czy_brudne_ = true;
        }
    }

    template <typename T, typename... Args>
    T& dodaj(Args&&... args) {
        T* k = new T(args...);
        ustaw_kontrolke_glowna(k);
        return *k;
    }

    Kontrolka* fokus() const { return kontrolka_z_fokusem_; }
    void ustaw_fokus(Kontrolka* k) {
        if (kontrolka_z_fokusem_ != k) {
            if (kontrolka_z_fokusem_) {
                kontrolka_z_fokusem_->przy_utracie_fokusu();
            }
            kontrolka_z_fokusem_ = k;
            if (kontrolka_z_fokusem_) {
                kontrolka_z_fokusem_->przy_fokusie();
            }
        }
    }

    Kontrolka* przechwycenie_myszy() const { return kontrolka_przechwytujaca_; }
    void ustaw_przechwycenie_myszy(Kontrolka* k) { kontrolka_przechwytujaca_ = k; }
    void zwolnij_przechwycenie_myszy() { kontrolka_przechwytujaca_ = nullptr; }

    int skala_dpi() const { return skala_dpi_; }
    void ustaw_skale_dpi(int skala) {
        skala_dpi_ = (skala < 1) ? 1 : skala;
        if (kontrolka_glowna_) {
            kontrolka_glowna_->ustaw_skale_dpi(skala_dpi_);
        }
        przelicz_uklad();
        czy_brudne_ = true;
    }

    void przelicz_uklad() {
        if (!kontrolka_glowna_) return;
        int belka_h = WYSOKOSC_BELKI_TYTULOWEJ * skala_dpi_;
        int klient_h = wysokosc_ - belka_h;
        if (klient_h < 0) klient_h = 0;
        Prostokat obszar_klienta(0, belka_h, szerokosc_, klient_h);
        kontrolka_glowna_->zmierz(obszar_klienta.rozmiar());
        kontrolka_glowna_->rozmiesc(obszar_klienta);
        kontrolka_glowna_->uniewaznij();
    }

    void rysuj(KontekstRysowania& ctx, bool wymus = false) {
        if (czy_zminimalizowane_) return;
        bool nalezy_rysowac = czy_brudne_ || wymus;

        if (nalezy_rysowac) {
            // Narysuj standardową belkę tytułową na pozycji okna
            gui_rysuj_standardowa_belke(x_, y_, szerokosc_, tytul_, czy_zmaksymalizowane_);
            // Tło okna poniżej belki
            int belka_h = WYSOKOSC_BELKI_TYTULOWEJ * skala_dpi_;
            int klient_h = wysokosc_ - belka_h;
            if (klient_h > 0) {
                ctx.dodaj_przesuniecie(Punkt(x_, y_));
                ctx.rysuj_prostokat(Prostokat(0, belka_h, szerokosc_, klient_h), Kolory::BursztynowyCiemnyTlo);
                ctx.zdejmij_przesuniecie();
            }
            czy_brudne_ = false;
        }

        // Renderuj drzewo kontrolek
        if (kontrolka_glowna_) {
            ctx.dodaj_przesuniecie(Punkt(x_, y_));
            kontrolka_glowna_->rysuj(ctx, nalezy_rysowac);
            ctx.zdejmij_przesuniecie();
        }
    }

    void minimalizuj() {
        czy_przeciagane_ = false;
        gui_ustaw_capture_myszy(false);
        czy_zminimalizowane_ = gui_minimalizuj_okno();
    }

    void przelacz_maksymalizacje() {
        czy_przeciagane_ = false;
        gui_ustaw_capture_myszy(false);

        if (!czy_zmaksymalizowane_) {
            // Zapisz bieżącą geometrię okna
            zapisana_geometria_ = Prostokat(x_, y_, szerokosc_, wysokosc_);

            // Pobierz obszar roboczy bieżącego monitora
            int docelowy_x = 0;
            int docelowy_y = 0;
            int docelowy_w = 800;
            int docelowy_h = 600;

            GuiMonitorInfo mon{};
            if (gui_pobierz_monitor(0, &mon) && mon.work_szer > 0 && mon.work_wys > 0) {
                docelowy_x = mon.work_x;
                docelowy_y = mon.work_y;
                docelowy_w = mon.work_szer;
                docelowy_h = mon.work_wys;
            } else {
                int sw = 800, sh = 600;
                gui_pobierz_rozdzielczosc(&sw, &sh);
                docelowy_x = 0;
                docelowy_y = 0;
                docelowy_w = sw;
                docelowy_h = (sh > 32) ? (sh - 32) : sh;
            }

            x_ = docelowy_x;
            y_ = docelowy_y;
            szerokosc_ = docelowy_w;
            wysokosc_ = docelowy_h;
            czy_zmaksymalizowane_ = true;
        } else {
            // Przywróć dokładną zapisaną geometrię
            x_ = zapisana_geometria_.x;
            y_ = zapisana_geometria_.y;
            szerokosc_ = zapisana_geometria_.szerokosc;
            wysokosc_ = zapisana_geometria_.wysokosc;
            czy_zmaksymalizowane_ = false;
        }

        bws_utworz_warstwe(x_, y_, szerokosc_, wysokosc_, 10);
        gui_ustaw_geometrie_okna(x_, y_, szerokosc_, wysokosc_);
        bws_przesun_warstwe(x_, y_);

        przelicz_uklad();
        czy_brudne_ = true;
    }

    void zamknij() {
        czy_przeciagane_ = false;
        gui_ustaw_capture_myszy(false);
        if (kontrolka_z_fokusem_) {
            kontrolka_z_fokusem_->przy_utracie_fokusu();
            kontrolka_z_fokusem_ = nullptr;
        }
        kontrolka_najechana_ = nullptr;
        kontrolka_przechwytujaca_ = nullptr;
        gui_zakoncz_aplikacje();
    }

    void obsluz_zdarzenie(const bws_zdarzenie& surowe_zdarzenie) {
        int belka_h = WYSOKOSC_BELKI_TYTULOWEJ * skala_dpi_;
        int mx = surowe_zdarzenie.x;
        int my = surowe_zdarzenie.y;
        int lokalny_x = mx - x_;
        int lokalny_y = my - y_;

        switch (surowe_zdarzenie.typ) {
            case BWS_ZDARZENIE_MYSZ_RUCH: {
                if (czy_przeciagane_) {
                    int nx = mx - przesuniecie_ciagniecia_x_;
                    int ny = my - przesuniecie_ciagniecia_y_;

                    int max_w = 1280, max_h = 720;
                    GuiMonitorInfo mon{};
                    if (gui_pobierz_monitor(0, &mon) && mon.work_szer > 0 && mon.work_wys > 0) {
                        max_w = mon.work_x + mon.work_szer;
                        max_h = mon.work_y + mon.work_wys;
                    } else {
                        int sw = 1280, sh = 720;
                        gui_pobierz_rozdzielczosc(&sw, &sh);
                        max_w = sw;
                        max_h = (sh > 32) ? (sh - 32) : sh;
                    }

                    int max_pos_x = max_w - szerokosc_;
                    int max_pos_y = max_h - wysokosc_;
                    if (max_pos_x < 0) max_pos_x = 0;
                    if (max_pos_y < 0) max_pos_y = 0;

                    if (nx < 0) nx = 0;
                    if (ny < 0) ny = 0;
                    if (nx > max_pos_x) nx = max_pos_x;
                    if (ny > max_pos_y) ny = max_pos_y;

                    if (nx != x_ || ny != y_) {
                        x_ = nx;
                        y_ = ny;
                        bws_przesun_warstwe(x_, y_);
                        gui_odswiez();
                    }
                    return;
                }

                if (kontrolka_przechwytujaca_) {
                    ZdarzenieMyszki me;
                    me.typ = TypZdarzenia::RuchMyszy;
                    me.x = lokalny_x;
                    me.y = lokalny_y;
                    me.przycisk = surowe_zdarzenie.przyciski;
                    kontrolka_przechwytujaca_->przy_ruchu_myszy(me);
                } else if (kontrolka_glowna_) {
                    Kontrolka* hit = kontrolka_glowna_->sprawdz_trafienie(Punkt(lokalny_x, lokalny_y));
                    if (hit != kontrolka_najechana_) {
                        if (kontrolka_najechana_) kontrolka_najechana_->przy_opuszczeniu();
                        kontrolka_najechana_ = hit;
                        if (kontrolka_najechana_) kontrolka_najechana_->przy_najechaniu();
                    }
                    if (kontrolka_najechana_) {
                        ZdarzenieMyszki me;
                        me.typ = TypZdarzenia::RuchMyszy;
                        me.x = lokalny_x;
                        me.y = lokalny_y;
                        me.przycisk = surowe_zdarzenie.przyciski;
                        kontrolka_najechana_->przy_ruchu_myszy(me);
                    }
                }
                break;
            }

            case BWS_ZDARZENIE_MYSZ_DOWN:
            case BWS_ZDARZENIE_MYSZ_PRAWY_DOWN: {
                if (my >= y_ && my < y_ + belka_h && mx >= x_ && mx < x_ + szerokosc_) {
                    gui_akcja_belki akcja = gui_hit_test_belki(mx, my, x_, y_, szerokosc_);
                    if (akcja == GUI_BELKA_ZAMKNIJ) {
                        zamknij();
                        return;
                    } else if (akcja == GUI_BELKA_MINIMALIZUJ) {
                        minimalizuj();
                        return;
                    } else if (akcja == GUI_BELKA_MAKSYMALIZUJ) {
                        przelacz_maksymalizacje();
                        return;
                    } else if (akcja == GUI_BELKA_DRAG && !czy_zmaksymalizowane_) {
                        czy_przeciagane_ = true;
                        przesuniecie_ciagniecia_x_ = mx - x_;
                        przesuniecie_ciagniecia_y_ = my - y_;
                        gui_ustaw_capture_myszy(true);
                        return;
                    }
                }

                if (kontrolka_glowna_) {
                    Kontrolka* hit = kontrolka_glowna_->sprawdz_trafienie(Punkt(lokalny_x, lokalny_y));
                    if (hit && hit->czy_moze_miec_fokus()) {
                        ustaw_fokus(hit);
                    } else if (!hit) {
                        ustaw_fokus(nullptr);
                    }

                    if (hit) {
                        kontrolka_przechwytujaca_ = hit;
                        ZdarzenieMyszki me;
                        me.typ = TypZdarzenia::WcisniecieMyszy;
                        me.x = lokalny_x;
                        me.y = lokalny_y;
                        me.przycisk = (surowe_zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_DOWN) ? 2 : 1;
                        hit->przy_wcisnieciu_myszy(me);
                    }
                }
                break;
            }

            case BWS_ZDARZENIE_MYSZ_UP:
            case BWS_ZDARZENIE_MYSZ_PRAWY_UP: {
                if (czy_przeciagane_) {
                    czy_przeciagane_ = false;
                    gui_ustaw_capture_myszy(false);
                }

                Kontrolka* cel = kontrolka_przechwytujaca_;
                kontrolka_przechwytujaca_ = nullptr;

                if (!cel && kontrolka_glowna_) {
                    cel = kontrolka_glowna_->sprawdz_trafienie(Punkt(lokalny_x, lokalny_y));
                }

                if (cel) {
                    ZdarzenieMyszki me;
                    me.typ = TypZdarzenia::ZwolnienieMyszy;
                    me.x = lokalny_x;
                    me.y = lokalny_y;
                    me.przycisk = (surowe_zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_UP) ? 2 : 1;
                    cel->przy_zwolnieniu_myszy(me);
                }
                break;
            }

            case BWS_ZDARZENIE_TEXT_INPUT: {
                if (kontrolka_z_fokusem_) {
                    ZdarzenieWprowadzaniaTekstu te;
                    te.punkt_kodowy = surowe_zdarzenie.kod;
                    kontrolka_z_fokusem_->przy_wprowadzeniu_tekstu(te);
                }
                break;
            }

            case BWS_ZDARZENIE_KLAWISZ:
            case BWS_ZDARZENIE_KLAWISZ_FIZYCZNY: {
                // Tab (9 lub scancode 0x0F) - nawigacja fokus
                if (surowe_zdarzenie.kod == 9 || surowe_zdarzenie.kod == 0x0F) {
                    bool shift = (surowe_zdarzenie.przyciski & BWS_MOD_SHIFT) != 0;
                    nastepny_fokus(shift);
                    break;
                }

                if (kontrolka_z_fokusem_) {
                    ZdarzenieKlawiatury ke;
                    ke.typ = TypZdarzenia::KlawiszWcisniety;
                    ke.kod_klawisza = surowe_zdarzenie.kod;
                    ke.kod_scancode = surowe_zdarzenie.kod;
                    ke.modyfikatory = surowe_zdarzenie.przyciski;
                    kontrolka_z_fokusem_->przy_wcisnieciu_klawisza(ke);
                }
                break;
            }

            case BWS_ZDARZENIE_FOCUS: {
                czy_aktywne_ = true;
                if (czy_zminimalizowane_) {
                    czy_zminimalizowane_ = false;
                    przelicz_uklad();
                }
                czy_brudne_ = true;
                break;
            }

            case BWS_ZDARZENIE_BLUR: {
                czy_aktywne_ = false;
                czy_brudne_ = true;
                break;
            }

            case BWS_ZDARZENIE_ZAMKNIJ: {
                zamknij();
                break;
            }

            default:
                break;
        }
    }

    void nastepny_fokus(bool wstecz = false) {
        if (!kontrolka_glowna_) return;
        Kontrolka* lista[32];
        size_t liczba = 0;
        zbierz_kontrolki_z_fokusem(kontrolka_glowna_, lista, liczba);
        if (liczba == 0) return;

        int biezacy_indeks = -1;
        for (size_t i = 0; i < liczba; ++i) {
            if (lista[i] == kontrolka_z_fokusem_) {
                biezacy_indeks = static_cast<int>(i);
                break;
            }
        }

        int nastepny_indeks = 0;
        if (biezacy_indeks >= 0) {
            if (wstecz) {
                nastepny_indeks = (biezacy_indeks - 1 + static_cast<int>(liczba)) % static_cast<int>(liczba);
            } else {
                nastepny_indeks = (biezacy_indeks + 1) % static_cast<int>(liczba);
            }
        }
        ustaw_fokus(lista[nastepny_indeks]);
    }

private:
    static void zbierz_kontrolki_z_fokusem(Kontrolka* k, Kontrolka** out_lista, size_t& liczba) {
        if (!k || !k->czy_widoczna() || liczba >= 32) return;
        if (k->czy_moze_miec_fokus()) {
            out_lista[liczba++] = k;
        }
        for (size_t i = 0; i < k->liczba_dzieci(); ++i) {
            zbierz_kontrolki_z_fokusem(k->dziecko(i), out_lista, liczba);
        }
    }

    char tytul_[64]{0};
    int x_{100};
    int y_{100};
    int szerokosc_{480};
    int wysokosc_{320};
    int skala_dpi_{1};

    bool czy_aktywne_{true};
    bool czy_zmaksymalizowane_{false};
    bool czy_zminimalizowane_{false};
    bool czy_przeciagane_{false};
    int przesuniecie_ciagniecia_x_{0};
    int przesuniecie_ciagniecia_y_{0};
    bool czy_brudne_{true};
    Prostokat zapisana_geometria_{100, 100, 480, 320};

    Kontrolka* kontrolka_glowna_{nullptr};
    Panel* domyslny_korzen_{nullptr};
    Kontrolka* kontrolka_z_fokusem_{nullptr};
    Kontrolka* kontrolka_najechana_{nullptr};
    Kontrolka* kontrolka_przechwytujaca_{nullptr};
};

} // namespace BursztynUI
