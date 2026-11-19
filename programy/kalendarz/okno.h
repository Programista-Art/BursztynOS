#pragma once
/*
 * Kalendarz - okno BursztynUI.
 *
 * Gorny pasek: miesiac i rok (duza czcionka), poprzedni/nastepny rok i miesiac, "Dzisiaj" - jedyna nawigacja okna.
 * Srodek: istniejacy komponent BursztynUI::Kalendarz z ukrytym wewnetrznym paskiem nawigacji (siatka, klawiatura);
 * kompaktowy kalendarz paska zadan zachowuje pasek komponentu. Dol: opis wybranego dnia (model.h). Data
 * dzisiejsza z BWS 9 (bursztyn_ui/data_systemowa.h), sprawdzana co ~30 s zadaniem okresowym petli - bez aktywnego
 * czekania i bez przerysowan, gdy dzien sie nie zmienil.
 */
#include "model.h"
#include "bursztyn_ui/data_systemowa.h"

namespace KalendarzApp {

using namespace BursztynUI;

class OknoKalendarza : public Okno {
public:
    // Minimum: najdluzszy tytul ("Październik 2026", podwojna czcionka) + piec przyciskow naglowka bez nachodzenia
    // (natywnie przy 440 px tytul wchodzil pod przycisk "<").
    static constexpr int SZEROKOSC = 720, WYSOKOSC = 560, MIN_SZEROKOSC = 640, MIN_WYSOKOSC = 420;

    OknoKalendarza() : Okno("Kalendarz", SZEROKOSC, WYSOKOSC) { ustaw_minimalny_rozmiar(MIN_SZEROKOSC, MIN_WYSOKOSC); }
    ~OknoKalendarza() override {
        auto* p = kontrolka_glowna(); ustaw_kontrolke_glowna(nullptr); delete p;
    }

    /* Testy hosta podstawiaja symulowane jadro. */
    FunkcjaBws bws = bws_domyslne;

    void init() {
        auto& root = dodaj<PanelDokowany>();
        root.ustaw_wypelnienie(Marginesy(10)); root.ustaw_odstep(8);
        auto& gora = root.dodaj<Siatka>(Dokowanie::Gora);
        gora.ustaw_preferowany_rozmiar(Rozmiar(-1, 44));
        gora.dodaj_wiersz();
        gora.dodaj_kolumne();
        static constexpr int SZER_PRZYCISKOW[] = {44, 44, 110, 44, 44};   // <<, <, Dzisiaj, >, >> (Ring 3: bez initializer_list)
        for (int szer : SZER_PRZYCISKOW) gora.dodaj_kolumne(DefinicjaWymiaru::piksele(szer));
        tytul_ = new Etykieta("");
        tytul_->ustaw_skale_czcionki(2);
        tytul_->ustaw_wyrownanie_pionowe_tekstu(WyrownaniePionowe::Srodek);
        poprzedni_rok_ = new Przycisk("<<");
        poprzedni_ = new Przycisk("<");
        dzisiaj_ = new Przycisk("Dzisiaj");
        nastepny_ = new Przycisk(">");
        nastepny_rok_ = new Przycisk(">>");
        gora.dodaj_do_siatki(tytul_, 0, 0); gora.dodaj_do_siatki(poprzedni_rok_, 0, 1); gora.dodaj_do_siatki(poprzedni_, 0, 2);
        gora.dodaj_do_siatki(dzisiaj_, 0, 3); gora.dodaj_do_siatki(nastepny_, 0, 4); gora.dodaj_do_siatki(nastepny_rok_, 0, 5);
        poprzedni_rok_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<OknoKalendarza*>(p)->kalendarz_->poprzedni_rok(); }, this);
        nastepny_rok_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<OknoKalendarza*>(p)->kalendarz_->nastepny_rok(); }, this);
        poprzedni_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<OknoKalendarza*>(p)->kalendarz_->poprzedni_miesiac(); }, this);
        nastepny_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<OknoKalendarza*>(p)->kalendarz_->nastepny_miesiac(); }, this);
        dzisiaj_->przy_kliknieciu([](Przycisk*, void* p) {
            auto* o = static_cast<OknoKalendarza*>(p);
            o->sprawdz_date();
            o->kalendarz_->pokaz_dzisiaj();
            o->ustaw_fokus(o->kalendarz_);
        }, this);

        // Panel dnia (dwie jednowierszowe etykiety: bez zawijania - stala, tania szerokosc rysowania).
        szczegoly_ = &root.dodaj<Etykieta>(Dokowanie::Dol, "");
        szczegoly_->ustaw_autodopasowanie(false); szczegoly_->ustaw_preferowany_rozmiar(Rozmiar(-1, 26));
        data_ = &root.dodaj<Etykieta>(Dokowanie::Dol, "");
        data_->ustaw_autodopasowanie(false); data_->ustaw_preferowany_rozmiar(Rozmiar(-1, 28));

        kalendarz_ = &root.dodaj<Kalendarz>(Dokowanie::Wypelnij);
        kalendarz_->ustaw_pasek_nawigacji(false);   // jedna nawigacja: przyciski naglowka okna
        kalendarz_->przy_zmianie_daty([](Kalendarz*, DataKalendarza, void* p) { static_cast<OknoKalendarza*>(p)->aktualizuj_opis(); }, this);
        kalendarz_->przy_zmianie_widoku([](Kalendarz*, DataKalendarza, void* p) { static_cast<OknoKalendarza*>(p)->aktualizuj_tytul(); }, this);

        DataKalendarza dzis{};
        if (pobierz_date_systemowa(&dzis, bws)) {
            dzisiaj_data_ = dzis; ma_dzisiaj_ = true;
            kalendarz_->ustaw_dzisiaj(dzis);
            kalendarz_->ustaw_wybrana_data(dzis, false);
            kalendarz_->ustaw_widok(dzis.rok, dzis.miesiac);
        }
        dzisiaj_->ustaw_wlaczona(ma_dzisiaj_);
        aktualizuj_tytul();
        aktualizuj_opis();
        ustaw_fokus(kalendarz_);
    }

    /* Zadanie okresowe (~30 s) i przycisk Dzisiaj: zmiana daty systemowej przesuwa wyroznienie "dzisiaj". */
    void sprawdz_date() {
        ++sprawdzen_;
        DataKalendarza dzis{};
        const bool ok = pobierz_date_systemowa(&dzis, bws);
        if (ok == ma_dzisiaj_ && (!ok || dzis == dzisiaj_data_)) return;
        ma_dzisiaj_ = ok;
        dzisiaj_data_ = ok ? dzis : DataKalendarza{0, 0, 0};
        kalendarz_->ustaw_dzisiaj(dzisiaj_data_);
        dzisiaj_->ustaw_wlaczona(ok);
        aktualizuj_opis();
    }

    // ---- dla testow
    Kalendarz* kalendarz() { return kalendarz_; }
    Etykieta* tytul() { return tytul_; }
    Etykieta* etykieta_daty() { return data_; }
    Etykieta* etykieta_szczegolow() { return szczegoly_; }
    Przycisk* przycisk_dzisiaj() { return dzisiaj_; }
    Przycisk* przycisk_poprzedni() { return poprzedni_; }
    Przycisk* przycisk_nastepny() { return nastepny_; }
    Przycisk* przycisk_poprzedni_rok() { return poprzedni_rok_; }
    Przycisk* przycisk_nastepny_rok() { return nastepny_rok_; }
    bool ma_dzisiaj() const { return ma_dzisiaj_; }
    uint64_t sprawdzen() const { return sprawdzen_; }

private:
    void aktualizuj_tytul() {
        char t[48]; tytul_miesiaca(kalendarz_->widok_rok(), kalendarz_->widok_miesiac(), t, sizeof(t));
        tytul_->ustaw_tekst(t);
        aktualizuj_opis();   // "poza widokiem" zalezy od widocznego miesiaca
    }
    void aktualizuj_opis() {
        const OpisDnia o = opisz(kalendarz_->wybrana_data(), dzisiaj_data_, ma_dzisiaj_);
        char t[128]; Tekst x(t, sizeof(t));
        x.dodaj(ma_dzisiaj_ || kalendarz_->wybrana_data() != DataKalendarza{} ? "Wybrana data: " : "");
        x.dodaj(o.data);
        // Wybor jest zachowywany przy nawigacji; gdy nie lezy w widocznym miesiacu, opis mowi to wprost.
        if (kalendarz_->wybrana_data().czy_poprawna() && !kalendarz_->wybrana_w_widoku()) x.dodaj(" · poza widocznym miesiącem");
        data_->ustaw_tekst(t);
        szczegoly_->ustaw_tekst(o.szczegoly);
    }

    Kalendarz* kalendarz_{};
    Etykieta *tytul_{}, *data_{}, *szczegoly_{};
    Przycisk *poprzedni_rok_{}, *poprzedni_{}, *dzisiaj_{}, *nastepny_{}, *nastepny_rok_{};
    DataKalendarza dzisiaj_data_{0, 0, 0};
    bool ma_dzisiaj_ = false;
    uint64_t sprawdzen_ = 0;
};

}  // namespace KalendarzApp
