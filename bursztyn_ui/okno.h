#pragma once

#include "kontrolka.h"
#include "panel.h"
#include "zdarzenia.h"
#include "rysowanie.h"
#include "popup.h"
#include "../bursztyn_gui.h"

namespace BursztynUI {

class Okno {
public:
    static constexpr int WYSOKOSC_BELKI_TYTULOWEJ = 28;

    Okno(const char* tytul_okna = "Okno BursztynUI", int szerokosc = 480, int wysokosc = 320)
        : szerokosc_(szerokosc), wysokosc_(wysokosc) {
        ustaw_tytul(tytul_okna);
        dopasuj_do_obszaru_roboczego(true);
        zapisana_geometria_ = Prostokat(x_, y_, szerokosc_, wysokosc_);
        domyslny_korzen_ = new Panel(Orientacja::Pionowa);
        kontrolka_glowna_ = domyslny_korzen_;
        kontrolka_glowna_->ustaw_zarzadce_popupow(&popupy_);
        popupy_.ustaw_geometrie_wlasciciela(Prostokat(x_, y_, szerokosc_, wysokosc_),
                                            pobierz_obszar_roboczy());
    }

    virtual ~Okno() {
        popupy_.zamknij(PowodZamknieciaPopup::WlascicielZamkniety);
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

    bool czy_wymaga_animacji() const {
        return !czy_zminimalizowane_ && kontrolka_glowna_ && kontrolka_glowna_->czy_wymaga_animacji();
    }

    void krok_animacji(uint64_t teraz_ns = 0) {
        if (czy_wymaga_animacji()) {
            kontrolka_glowna_->krok_animacji(teraz_ns);
        }
    }

    void ustaw_pozycje(int x, int y) {
        x_ = x;
        y_ = y;
        dopasuj_do_obszaru_roboczego(true);
    }

    void ustaw_rozmiar(int w, int h) {
        if (w < 100) w = 100;
        if (h < 60) h = 60;
        bool zmiana = szerokosc_ != w || wysokosc_ != h;
        szerokosc_ = w;
        wysokosc_ = h;
        dopasuj_do_obszaru_roboczego(true);
        if (zmiana || szerokosc_ != w || wysokosc_ != h) {
            przelicz_uklad();
            czy_brudne_ = true;
        }
    }

    void ustaw_geometrie(int x, int y, int w, int h) {
        x_ = x;
        y_ = y;
        ustaw_rozmiar(w, h);
        dopasuj_do_obszaru_roboczego(true);
    }

    // Wywolywane przed utworzeniem warstwy oraz po programowej zmianie
    // geometrii. BWS62 jest jedynym zrodlem rozmiaru obszaru roboczego.
    void dopasuj_do_obszaru_roboczego(bool dopasuj_rozmiar = true) {
        Prostokat work = pobierz_obszar_roboczy();
        if (dopasuj_rozmiar || szerokosc_ > work.szerokosc) {
            if (szerokosc_ > work.szerokosc) szerokosc_ = work.szerokosc;
        }
        if (dopasuj_rozmiar || wysokosc_ > work.wysokosc) {
            if (wysokosc_ > work.wysokosc) wysokosc_ = work.wysokosc;
        }
        if (szerokosc_ < 1) szerokosc_ = 1;
        if (wysokosc_ < 1) wysokosc_ = 1;

        int max_x = work.prawy() - szerokosc_;
        int max_y = work.dolny() - wysokosc_;
        if (max_x < work.x) max_x = work.x;
        if (max_y < work.y) max_y = work.y;
        x_ = przytnij(x_, work.x, max_x);
        y_ = przytnij(y_, work.y, max_y);
    }

    Kontrolka* kontrolka_glowna() const { return kontrolka_glowna_; }
    void ustaw_kontrolke_glowna(Kontrolka* korzen) {
        if (kontrolka_glowna_ != korzen) {
            kontrolka_glowna_ = korzen;
            if (kontrolka_glowna_) {
                kontrolka_glowna_->ustaw_skale_dpi(skala_dpi_);
                kontrolka_glowna_->ustaw_zarzadce_popupow(&popupy_);
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
    ZarzadcaPopupow& zarzadca_popupow() { return popupy_; }
    const ZarzadcaPopupow& zarzadca_popupow() const { return popupy_; }
    void ustaw_fokus(Kontrolka* k) {
        if (kontrolka_z_fokusem_ != k) {
            if (kontrolka_z_fokusem_) {
                kontrolka_z_fokusem_->przy_utracie_fokusu();
            }
            kontrolka_z_fokusem_ = k;
            if (kontrolka_z_fokusem_) {
                kontrolka_z_fokusem_->przy_fokusie();
            }
            czy_brudne_ = true;
        }
    }

    Kontrolka* przechwycenie_myszy() const { return kontrolka_przechwytujaca_; }
    void ustaw_przechwycenie_myszy(Kontrolka* k) {
        kontrolka_przechwytujaca_ = k;
        gui_ustaw_capture_myszy(k != nullptr);
    }
    void zwolnij_przechwycenie_myszy() {
        kontrolka_przechwytujaca_ = nullptr;
        gui_ustaw_capture_myszy(false);
    }

    int skala_dpi() const { return skala_dpi_; }
    void ustaw_skale_dpi(int skala) {
        skala_dpi_ = (skala < 1) ? 1 : skala;
        popupy_.ustaw_skale_dpi(skala_dpi_);
        if (kontrolka_glowna_) {
            kontrolka_glowna_->ustaw_skale_dpi(skala_dpi_);
        }
        przelicz_uklad();
        czy_brudne_ = true;
    }

    void przelicz_uklad() {
        popupy_.ustaw_geometrie_wlasciciela(Prostokat(x_, y_, szerokosc_, wysokosc_),
                                            pobierz_obszar_roboczy());
        if (!kontrolka_glowna_) return;
        int belka_h = WYSOKOSC_BELKI_TYTULOWEJ * skala_dpi_;
        int klient_h = wysokosc_ - belka_h;
        if (klient_h < 0) klient_h = 0;
        Prostokat obszar_klienta(0, belka_h, szerokosc_, klient_h);
        kontrolka_glowna_->zmierz(obszar_klienta.rozmiar());
        kontrolka_glowna_->rozmiesc(obszar_klienta);
        kontrolka_glowna_->uniewaznij();
        czy_brudne_ = true;
    }

    Prostokat ostatni_obszar_odrysowania() const { return ostatni_obszar_odrysowania_; }
    int ostatnia_powierzchnia_odrysowania() const { return ostatni_obszar_odrysowania_.szerokosc * ostatni_obszar_odrysowania_.wysokosc; }

    void uniewaznij_obszar(Prostokat r) {
        brudny_obszar_klienta_ = brudny_obszar_klienta_.suma(r);
    }

    void rysuj(KontekstRysowania& ctx, bool wymus = false) {
        if (czy_zminimalizowane_) return;

        int belka_h = WYSOKOSC_BELKI_TYTULOWEJ * skala_dpi_;
        int klient_h = wysokosc_ - belka_h;
        if (klient_h < 0) klient_h = 0;
        Prostokat obszar_klienta(0, belka_h, szerokosc_, klient_h);

        bool pelne_odrysowanie = czy_brudne_ || wymus;

        if (pelne_odrysowanie) {
            ostatni_obszar_odrysowania_ = Prostokat(0, 0, szerokosc_, wysokosc_);

            // Narysuj standardową belkę tytułową na pozycji okna
            gui_rysuj_standardowa_belke(x_, y_, szerokosc_, tytul_, czy_zmaksymalizowane_);

            // Tło okna poniżej belki
            if (klient_h > 0) {
                ctx.dodaj_przesuniecie(Punkt(x_, y_));
                ctx.dodaj_przyciecie(obszar_klienta);
                ctx.rysuj_prostokat(obszar_klienta, Kolory::BursztynowyCiemnyTlo);

                // Renderuj drzewo kontrolek na czystym tle
                if (kontrolka_glowna_) {
                    kontrolka_glowna_->rysuj(ctx, true);
                    kontrolka_glowna_->wyczysc_brudny_obszar();
                }
                popupy_.rysuj(ctx, true);

                ctx.zdejmij_przyciecie();
                ctx.zdejmij_przesuniecie();
            }

            brudny_obszar_klienta_ = Prostokat(0, 0, 0, 0);
            czy_brudne_ = false;
        } else if ((kontrolka_glowna_ && kontrolka_glowna_->czy_brudna()) ||
                   !brudny_obszar_klienta_.czy_pusty() || popupy_.czy_brudny()) {
            // Częściowe odrysowanie wyłącznie w obrębie brudnego prostokąta
            Prostokat brudny = brudny_obszar_klienta_;
            if (kontrolka_glowna_)
                brudny = brudny.suma(kontrolka_glowna_->pobierz_brudny_obszar());
            brudny = brudny.suma(popupy_.brudny_obszar());
            if (brudny.czy_pusty()) {
                brudny = obszar_klienta;
            }
            brudny = brudny.przeciecie(obszar_klienta);

            if (!brudny.czy_pusty()) {
                ostatni_obszar_odrysowania_ = brudny;

                ctx.dodaj_przesuniecie(Punkt(x_, y_));
                ctx.dodaj_przyciecie(brudny);

                // Przywróć tło okna tylko w brudnym prostokącie
                ctx.rysuj_prostokat(brudny, Kolory::BursztynowyCiemnyTlo);

                // Narysuj kontrolki przecinające brudny prostokąt
                if (kontrolka_glowna_) {
                    kontrolka_glowna_->rysuj(ctx, false);
                    kontrolka_glowna_->wyczysc_brudny_obszar();
                }
                popupy_.rysuj(ctx, false);

                ctx.zdejmij_przyciecie();
                ctx.zdejmij_przesuniecie();
            }

            brudny_obszar_klienta_ = Prostokat(0, 0, 0, 0);
        }
    }

    void minimalizuj() {
        popupy_.zamknij(PowodZamknieciaPopup::WlascicielZminimalizowany);
        czy_przeciagane_ = false;
        gui_ustaw_capture_myszy(false);
        czy_zminimalizowane_ = gui_minimalizuj_okno();
    }

    void przelacz_maksymalizacje() {
        popupy_.zamknij(PowodZamknieciaPopup::ZmianaGeometrii);
        czy_przeciagane_ = false;
        gui_ustaw_capture_myszy(false);

        if (!czy_zmaksymalizowane_) {
            zapisana_geometria_ = Prostokat(x_, y_, szerokosc_, wysokosc_);

            Prostokat work = pobierz_obszar_roboczy();
            x_ = work.x;
            y_ = work.y;
            szerokosc_ = work.szerokosc;
            wysokosc_ = work.wysokosc;
            czy_zmaksymalizowane_ = true;
        } else {
            x_ = zapisana_geometria_.x;
            y_ = zapisana_geometria_.y;
            szerokosc_ = zapisana_geometria_.szerokosc;
            wysokosc_ = zapisana_geometria_.wysokosc;
            czy_zmaksymalizowane_ = false;
            dopasuj_do_obszaru_roboczego(true);
        }

        gui_ustaw_geometrie_okna(x_, y_, szerokosc_, wysokosc_);

        przelicz_uklad();
        czy_brudne_ = true;
    }

    void zamknij() {
        popupy_.zamknij(PowodZamknieciaPopup::WlascicielZamkniety);
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
                if (popupy_.czy_otwarty()) {
                    ZdarzenieMyszki pe;
                    pe.typ = TypZdarzenia::RuchMyszy;
                    pe.x = lokalny_x; pe.y = lokalny_y;
                    pe.modyfikatory = surowe_zdarzenie.przyciski;
                    pe.timestamp = surowe_zdarzenie.timestamp;
                    if (popupy_.obsluz_ruch(pe)) return;
                }
                if (czy_przeciagane_) {
                    int nx = mx - przesuniecie_ciagniecia_x_;
                    int ny = my - przesuniecie_ciagniecia_y_;

                    Prostokat work = pobierz_obszar_roboczy();
                    int max_pos_x = work.prawy() - szerokosc_;
                    int max_pos_y = work.dolny() - wysokosc_;
                    if (max_pos_x < work.x) max_pos_x = work.x;
                    if (max_pos_y < work.y) max_pos_y = work.y;
                    nx = przytnij(nx, work.x, max_pos_x);
                    ny = przytnij(ny, work.y, max_pos_y);

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
                    me.przycisk = static_cast<int>(PrzyciskMyszy::Brak);
                    me.modyfikatory = surowe_zdarzenie.przyciski;
                    me.timestamp = surowe_zdarzenie.timestamp;
                    kontrolka_przechwytujaca_->obsluz_ruch_myszy(me);
                } else if (kontrolka_glowna_) {
                    Kontrolka* hit = kontrolka_glowna_->sprawdz_trafienie(Punkt(lokalny_x, lokalny_y));
                    if (hit != kontrolka_najechana_) {
                        if (kontrolka_najechana_) kontrolka_najechana_->obsluz_opuszczenie_myszy();
                        kontrolka_najechana_ = hit;
                        if (kontrolka_najechana_) kontrolka_najechana_->obsluz_wejscie_myszy();
                    }
                    if (kontrolka_najechana_) {
                        ZdarzenieMyszki me;
                        me.typ = TypZdarzenia::RuchMyszy;
                        me.x = lokalny_x;
                        me.y = lokalny_y;
                        me.przycisk = static_cast<int>(PrzyciskMyszy::Brak);
                        me.modyfikatory = surowe_zdarzenie.przyciski;
                        me.timestamp = surowe_zdarzenie.timestamp;
                        kontrolka_najechana_->obsluz_ruch_myszy(me);
                    }
                }
                break;
            }

            case BWS_ZDARZENIE_MYSZ_DOWN:
            case BWS_ZDARZENIE_MYSZ_PRAWY_DOWN: {
                if (popupy_.czy_otwarty()) {
                    ZdarzenieMyszki pe;
                    pe.typ = TypZdarzenia::WcisniecieMyszy;
                    pe.x = lokalny_x; pe.y = lokalny_y;
                    pe.przycisk = static_cast<int>(
                        surowe_zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_DOWN
                        ? PrzyciskMyszy::Prawy : PrzyciskMyszy::Lewy);
                    pe.modyfikatory = surowe_zdarzenie.przyciski;
                    pe.timestamp = surowe_zdarzenie.timestamp;
                    if (popupy_.obsluz_wcisniecie(pe)) return;
                }
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
                    }

                    if (hit) {
                        ustaw_przechwycenie_myszy(hit);
                        ZdarzenieMyszki me;
                        me.typ = TypZdarzenia::WcisniecieMyszy;
                        me.x = lokalny_x;
                        me.y = lokalny_y;
                        me.przycisk = static_cast<int>(
                            (surowe_zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_DOWN)
                            ? PrzyciskMyszy::Prawy : PrzyciskMyszy::Lewy);
                        me.modyfikatory = surowe_zdarzenie.przyciski;
                        me.timestamp = surowe_zdarzenie.timestamp;

                        uint64_t teraz = surowe_zdarzenie.timestamp;
                        int ddx = (mx > ost_click_x_) ? (mx - ost_click_x_) : (ost_click_x_ - mx);
                        int ddy = (my > ost_click_y_) ? (my - ost_click_y_) : (ost_click_y_ - my);

                        if (teraz > 0 && ost_click_czas_ > 0 && (teraz - ost_click_czas_ < 400000000ULL || (teraz - ost_click_czas_ < 400 && teraz < 1000000000ULL)) && ddx < 5 && ddy < 5) {
                            me.typ = TypZdarzenia::PodwojneKlikniecie;
                            me.liczba_klikniec = 2;
                            hit->obsluz_podwojne_klikniecie(me);
                            ost_click_czas_ = 0;
                        } else {
                            hit->obsluz_wcisniecie_myszy(me);
                            ost_click_czas_ = teraz;
                            ost_click_x_ = mx;
                            ost_click_y_ = my;
                        }
                    }
                }
                break;
            }

            case BWS_ZDARZENIE_MYSZ_UP:
            case BWS_ZDARZENIE_MYSZ_PRAWY_UP: {
                if (popupy_.czy_otwarty()) {
                    ZdarzenieMyszki pe;
                    pe.typ = TypZdarzenia::ZwolnienieMyszy;
                    pe.x = lokalny_x; pe.y = lokalny_y;
                    pe.przycisk = static_cast<int>(
                        surowe_zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_UP
                        ? PrzyciskMyszy::Prawy : PrzyciskMyszy::Lewy);
                    pe.modyfikatory = surowe_zdarzenie.przyciski;
                    pe.timestamp = surowe_zdarzenie.timestamp;
                    if (popupy_.obsluz_zwolnienie(pe)) return;
                }
                // Zwolnienie przycisku zawsze kończy systemowe i lokalne capture.
                gui_ustaw_capture_myszy(false);
                if (czy_przeciagane_) {
                    czy_przeciagane_ = false;
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
                    me.przycisk = static_cast<int>(
                        (surowe_zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_UP)
                        ? PrzyciskMyszy::Prawy : PrzyciskMyszy::Lewy);
                    me.modyfikatory = surowe_zdarzenie.przyciski;
                    me.timestamp = surowe_zdarzenie.timestamp;
                    cel->obsluz_zwolnienie_myszy(me);
                }
                break;
            }

            case BWS_ZDARZENIE_MYSZ_KOLO: {
                // Zdarzenie kola niesie aktualne wspolrzedne BWS. Nie wolno
                // kierowac go przez zapamietany hover: kolo moze przyjsc bez
                // poprzedzajacego ruchu albo po zmianie ukladu pod kursorem.
                Kontrolka* cel = kontrolka_glowna_
                    ? kontrolka_glowna_->sprawdz_trafienie(Punkt(lokalny_x, lokalny_y))
                    : nullptr;
                ZdarzenieRolki re;
                re.typ = TypZdarzenia::RolkaMyszy;
                re.x = lokalny_x;
                re.y = lokalny_y;
                re.delta_x = surowe_zdarzenie.dx;
                re.delta_y = surowe_zdarzenie.dy;
                re.modyfikatory = surowe_zdarzenie.kod;
                if (popupy_.obsluz_rolke(re)) break;
                bool fokus_byl_na_sciezce = false;
                for (Kontrolka* k = cel; k && !re.obsluzone; k = k->rodzic()) {
                    if (k == kontrolka_z_fokusem_) fokus_byl_na_sciezce = true;
                    k->obsluz_rolke(re);
                }
                if (!re.obsluzone && kontrolka_z_fokusem_ &&
                    !fokus_byl_na_sciezce) {
                    kontrolka_z_fokusem_->obsluz_rolke(re);
                }
                break;
            }

            case BWS_ZDARZENIE_TEXT_INPUT: {
                if (popupy_.czy_otwarty()) {
                    ZdarzenieWprowadzaniaTekstu pe;
                    pe.punkt_kodowy = surowe_zdarzenie.kod;
                    if (popupy_.obsluz_tekst(pe)) break;
                }
                if (kontrolka_z_fokusem_) {
                    ZdarzenieWprowadzaniaTekstu te;
                    te.punkt_kodowy = surowe_zdarzenie.kod;
                    kontrolka_z_fokusem_->obsluz_wprowadzenie_tekstu(te);
                }
                break;
            }

            case BWS_ZDARZENIE_KLAWISZ:
            case BWS_ZDARZENIE_KLAWISZ_FIZYCZNY: {
                bool fizyczny = surowe_zdarzenie.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;
                bool wcisniety = !fizyczny || surowe_zdarzenie.x != 0;
                uint32_t kod_bazowy = surowe_zdarzenie.kod & 0xFFU;
                bool klawisz_alt = ((surowe_zdarzenie.kod & BWS_KEY_SOURCE_USB) &&
                                    (kod_bazowy == 226U || kod_bazowy == 230U)) ||
                                   (!(surowe_zdarzenie.kod & BWS_KEY_SOURCE_USB) &&
                                    ((surowe_zdarzenie.kod & 0x7FU) == 0x38U));
                if (wcisniety && klawisz_alt && kontrolka_glowna_) {
                    Kontrolka* cel_alt = kontrolka_glowna_->kontrolka_aktywacji_alt();
                    if (cel_alt) { ustaw_fokus(cel_alt); break; }
                }
                // Tab (9 lub scancode 0x0F) - nawigacja fokus
                if (wcisniety && (surowe_zdarzenie.kod == 9 || surowe_zdarzenie.kod == 0x0F)) {
                    bool shift = (surowe_zdarzenie.przyciski & BWS_MOD_SHIFT) != 0;
                    nastepny_fokus(shift);
                    break;
                }

                ZdarzenieKlawiatury ke;
                ke.typ = TypZdarzenia::KlawiszWcisniety;
                ke.kod_klawisza = surowe_zdarzenie.kod;
                ke.kod_skanu = surowe_zdarzenie.kod;
                ke.modyfikatory = surowe_zdarzenie.przyciski;
                if (wcisniety && popupy_.czy_otwarty() && popupy_.obsluz_klawisz(ke)) break;
                if (kontrolka_z_fokusem_) {
                    if (wcisniety) {
                        kontrolka_z_fokusem_->obsluz_wcisniecie_klawisza(ke);
                    } else {
                        ke.typ = TypZdarzenia::KlawiszZwolniony;
                        kontrolka_z_fokusem_->obsluz_zwolnienie_klawisza(ke);
                    }
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
                popupy_.zamknij(PowodZamknieciaPopup::KlikPoza);
                czy_aktywne_ = false;
                zwolnij_przechwycenie_myszy();
                ustaw_fokus(nullptr);
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
    static Prostokat pobierz_obszar_roboczy() {
        GuiMonitorInfo mon{};
        if (gui_pobierz_monitor(0, &mon) && mon.work_szer > 0 && mon.work_wys > 0) {
            return Prostokat(mon.work_x, mon.work_y, mon.work_szer, mon.work_wys);
        }

        int sw = 800;
        int sh = 600;
        gui_pobierz_rozdzielczosc(&sw, &sh);
        if (sw < 1) sw = 1;
        if (sh < 1) sh = 1;
        return Prostokat(0, 0, sw, sh);
    }

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
    Prostokat brudny_obszar_klienta_{0, 0, 0, 0};
    Prostokat ostatni_obszar_odrysowania_{0, 0, 0, 0};
    Prostokat zapisana_geometria_{100, 100, 480, 320};

    uint64_t ost_click_czas_{0};
    int ost_click_x_{0};
    int ost_click_y_{0};

    Kontrolka* kontrolka_glowna_{nullptr};
    Panel* domyslny_korzen_{nullptr};
    Kontrolka* kontrolka_z_fokusem_{nullptr};
    Kontrolka* kontrolka_najechana_{nullptr};
    Kontrolka* kontrolka_przechwytujaca_{nullptr};
    ZarzadcaPopupow popupy_;
};

} // namespace BursztynUI
