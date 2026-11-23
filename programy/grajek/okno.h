#pragma once
/*
 * Grajek - glowne okno BursztynUI ("Grajek", "Odtwarzacz muzyki Bursztyn OS").
 *
 *   lewy panel    : biblioteka /dane/muzyka (szukaj, Odswiez biblioteke, Dodaj do kolejki, tabela utworow)
 *   srodek        : biezacy utwor (okladka, tytul, wykonawca, album, format, stan), postep z przewijaniem, wizualizacja
 *   prawy panel   : karty Kolejka (kolejnosc, usuwanie, zapis/odczyt M3U8) i Korektor (10 pasm, presety)
 *   dol           : sterowanie (poprzedni, odtworz, pauza, stop, nastepny, wyciszenie, glosnosc, losowo,
 *                   powtarzanie, odtwarzanie w tle, wizualizacja, zakoncz) i pasek komunikatow.
 *
 * Interfejs nie wykonuje pracy audio: tick() wola kontroler (produkcja PCM) przy kazdym ticku petli, a elementy
 * okna sa aktualizowane z ograniczona czestotliwoscia (postep 1 Hz, wizualizacja 4 Hz) i tylko gdy zmienil sie
 * stan (liczniki wersji kontrolera). Teksty etykiet sa ustawiane tylko, gdy sie roznia (bez zbednego ukladania).
 *
 * Koszt odrysowan (Ring 3 nie ma watkow: kazde rysowanie wstrzymuje produkcje PCM, a kolejka BWS 110 to 85 ms):
 *  - stan ustalony uniewaznia wylacznie male obszary: etykieta czasu i suwak (1 Hz), zmienione pola wizualizacji
 *    (4 Hz, tylko gdy wartosci sie zmienily), wiersz wskaznika w tabelach (zmiana stanu), pasek stanu (komunikat),
 *  - pasek stanu ma stala wysokosc (jeden wiersz, takze z diagnostyka - podsumowanie po prawej, szczegoly na karcie
 *    "Diagnostyka"): komunikat ani wlaczenie diagnostyki nie przeliczaja ukladu i nie przesuwaja sterowania,
 *  - dlugie teksty sa dopasowywane (wielokropek) tylko przy zmianie tekstu lub szerokosci,
 *  - hak Okno::hak_rysowania dopelnia kolejke PCM tuz przed kazda publikacja regionu (BWS 57) i mierzy czas
 *    rysowania/publikacji (przy wlaczonej diagnostyce).
 * Uklad jest proporcjonalny (PanelElastyczny, ObrazOkladki, kolumny tabel wg szerokosci) - bez stalych pikseli
 * tam, gdzie tekst lub rozmiar okna moze sie zmienic. Wysokosci i minima wynikaja z metryk biezacej czcionki (natywnie
 * TTF przez BSF, inne niz czcionka bitmapowa hosta): naglowek, opis korektora, kolumny tabel, siatki przyciskow
 * (SiatkaPrzyciskow), wiersze sterowania (kolumny Auto), minima paneli (minimum_lewego / minimum_prawego).
 */
#include "widoki.h"

namespace Grajek {

class OknoGrajka : public Okno {
public:
    static constexpr int SZEROKOSC = 1180, WYSOKOSC = 660, MIN_SZEROKOSC = 980, MIN_WYSOKOSC = 580;
    static constexpr int BOK_OKLADKI = 176;

    OknoGrajka() : Okno("Grajek", SZEROKOSC, WYSOKOSC), dane_bib_(&o_), dane_kol_(&o_), dane_diag_(&o_) { ustaw_minimalny_rozmiar(MIN_SZEROKOSC, MIN_WYSOKOSC); }
    ~OknoGrajka() override {
        if (aktywne_ == this) { aktywne_ = nullptr; Okno::hak_rysowania = nullptr; g_pompa_pcm = PompaPcm{}; }
        auto* p = kontrolka_glowna(); ustaw_kontrolke_glowna(nullptr); delete p;
        delete[] domyslna_;
    }

    Odtwarzacz& odtwarzacz() { return o_; }

    void init(const Platforma& platforma) {
        o_.konfiguruj(platforma);
        g_pompa_pcm.f = [](void* p) { static_cast<OknoGrajka*>(p)->o_.silnik().dopompuj(); };
        g_pompa_pcm.kontekst = this;
        aktywne_ = this;
        Okno::hak_rysowania = &OknoGrajka::hak_rysowania_grajka;
        czas_ns_ = platforma.czas_ns;
        buduj();
        dopasuj_warianty();
        o_.start();
        synchronizuj_ustawienia();
        aktualizuj(true);
    }

    // Zadanie okresowe petli (co tick): praca audio + ograniczone aktualizacje interfejsu.
    void tick() {
        o_.okno_zminimalizowane(czy_zminimalizowane());
        o_.tick();
        aktualizuj(false);
    }
    // Czy petla moze spac dluzej (brak odtwarzania i skanu) - grajek.cpp dobiera odstep zadania okresowego.
    bool bezczynny() const {
        const Stan s = o_.silnik().stan();
        return (s == Stan::Zatrzymany || s == Stan::Blad || s == Stan::Pauza) && !o_.biblioteka().skanuje() &&
               o_.silnik().w_kolejce() == 0;
    }

    void zamknij() override {
        if (o_.przy_zamknieciu()) {           // odtwarzanie w tle: okno znika z ekranu, dzwiek gra dalej
            aktualizuj(true);
            minimalizuj();
            return;
        }
        Okno::zamknij();                      // konczy proces (strumien PCM juz zamkniety)
    }
    void zakoncz_calkowicie() { o_.zakoncz(); Okno::zamknij(); }

    void obsluz_zdarzenie(const bws_zdarzenie& e) override {
        if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN || e.typ == BWS_ZDARZENIE_MYSZ_PRAWY_DOWN) podpowiedzi_.ukryj(&zarzadca_popupow());
        Okno::obsluz_zdarzenie(e);
        if (e.typ == BWS_ZDARZENIE_MYSZ_RUCH && kontrolka_glowna()) {
            mysz_x_ = e.x; mysz_y_ = e.y;
            najechana_ = kontrolka_glowna()->sprawdz_trafienie(Punkt(e.x - x(), e.y - y()));
            podpowiedzi_.obsluz_ruch(zarzadca_popupow(), najechana_, e.x, e.y, teraz_ms());
        }
        if (e.typ == BWS_ZDARZENIE_BLUR) { podpowiedzi_.ukryj(&zarzadca_popupow()); najechana_ = nullptr; }
    }
    void zaktualizuj_motyw() override {
        Okno::zaktualizuj_motyw();
        delete[] domyslna_; domyslna_ = nullptr;
        okladka_wersja_ = 0xFFFFFFFF;           // odswiez grafike domyslna w nowych kolorach
        dopasuj_warianty();                     // motyw/czcionka mogly zmienic szerokosci tekstow
        aktualizuj(true);
    }

private:
    // ------------------------------------------------------------ budowa interfejsu
    // Kolejnosc dokowania ma znaczenie: Gora/Dol/Lewo/Prawo zajmuja miejsce, zanim Wypelnij dostanie reszte.
    void buduj() {
        auto& root = dodaj<PanelDokowany>();
        root.ustaw_wypelnienie(Marginesy(10)); root.ustaw_odstep(8);

        // naglowek
        // naglowek: wysokosc z metryk czcionki (skala 2 + zapas na wydluzenia dolne, np. "j" w TTF), nie stale 40 px
        auto& gora = root.dodaj<PanelDokowany>(Dokowanie::Gora);
        gora.ustaw_odstep(12);
        nazwa_ = &gora.dodaj<EtykietaJednowierszowa>(Dokowanie::Lewo, "Grajek", 2, MOTYW_KOLOR_AKCENT);
        nazwa_->ustaw_szerokosc_wg_tekstu(true);
        auto& podtytul = gora.dodaj<EtykietaJednowierszowa>(Dokowanie::Wypelnij, "Odtwarzacz muzyki Bursztyn OS", 1, MOTYW_KOLOR_TEKST_DRUGI);
        (void)podtytul;

        // dol: pasek stanu (staly) i sterowanie
        komunikat_ = &root.dodaj<PasekStanuGrajka>(Dokowanie::Dol);
        komunikat_->przy_kliknieciu_skrotu([](void* p) { auto* o = static_cast<OknoGrajka*>(p); o->karty_->ustaw_aktywna_karte(KARTA_DIAGNOSTYKI); o->aktualizuj_diagnostyke(true); }, this);
        buduj_sterowanie(root);

        // lewo: biblioteka (ok. 31 % szerokosci)
        auto& lewy = root.dodaj<PanelElastyczny>(Dokowanie::Lewo, 31, 300, 480);
        lewy.ustaw_odstep(6);
        lewy.ustaw_minimum_zawartosci(&OknoGrajka::minimum_lewego, this);
        auto& tyt_bib = lewy.dodaj<EtykietaJednowierszowa>(Dokowanie::Gora, "Biblioteka muzyki · /dane/muzyka");
        tyt_bib.ustaw_preferowany_rozmiar(Rozmiar(-1, 22));
        szukaj_ = &lewy.dodaj<PoleWyszukiwania>(Dokowanie::Gora);
        szukaj_->ustaw_placeholder("Szukaj tytułu lub wykonawcy…");
        szukaj_->przy_zmianie([](PoleWyszukiwania*, const char* t, void* p) { static_cast<OknoGrajka*>(p)->o_.filtruj(t); }, this);
        auto& przyciski_bib = lewy.dodaj<SiatkaPrzyciskow>(Dokowanie::Gora);
        przyciski_bib_ = &przyciski_bib;
        przyciski_bib.ustaw_odstep(6); przyciski_bib.ustaw_wypelnienie(Marginesy(0));
        przyciski_bib.dodaj_wiersz(DefinicjaWymiaru::automatyczny()); przyciski_bib.dodaj_kolumne(); przyciski_bib.dodaj_kolumne();
        odswiez_ = new Przycisk("Odśwież");                   // tytul panelu mowi "Biblioteka"; pelny opis w podpowiedzi
        odswiez_->ustaw_podpowiedz("Odśwież bibliotekę: przeszukaj /dane/muzyka");
        odswiez_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<OknoGrajka*>(p)->o_.odswiez_biblioteke(); }, this);
        dodaj_kol_ = new Przycisk("Dodaj do kolejki");
        dodaj_kol_->ustaw_podpowiedz("Dopisz zaznaczony utwór na koniec kolejki");
        dodaj_kol_->przy_kliknieciu([](Przycisk*, void* p) { auto* o = static_cast<OknoGrajka*>(p); o->o_.dodaj_do_kolejki(o->o_.biblioteka().widoczny(uint32_t(o->bib_->wybrany_wiersz()))); }, this);
        odswiez_->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij); dodaj_kol_->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        przyciski_bib.dodaj_przycisk(odswiez_, 0, 0); przyciski_bib.dodaj_przycisk(dodaj_kol_, 0, 1);
        podkatalogi_ = &lewy.dodaj<PoleWyboru>(Dokowanie::Dol, "Uwzględniaj podkatalogi", true);
        podkatalogi_->ustaw_podpowiedz("Skanuj także podfoldery (do 3 poziomów)");
        podkatalogi_->przy_przelaczeniu([](PoleWyboru*, bool z, void* p) { auto* o = static_cast<OknoGrajka*>(p); if (!o->sync_) o->o_.ustaw_podkatalogi(z); }, this);
        stan_bib_ = &lewy.dodaj<EtykietaJednowierszowa>(Dokowanie::Dol, "", 1, MOTYW_KOLOR_TEKST_DRUGI);
        bib_ = &lewy.dodaj<TabelaGrajka>(Dokowanie::Wypelnij);
        bib_->ustaw_dostawce_grajka(&dane_bib_);
        bib_->ustaw_sortowanie_naglowkiem(false);
        bib_->ustaw_szerokosc_wg_naglowkow(true);
        bib_->ustaw_przewijanie_poziome(true);
        bib_->przy_aktywacji([](TabelaGrajka*, size_t w, void* p) { static_cast<OknoGrajka*>(p)->o_.odtworz_z_widoku(uint32_t(w)); }, this);

        // prawo: kolejka, korektor i diagnostyka (ok. 40 % pozostalej szerokosci; minimum z zawartosci: 10 kolumn
        // korektora, przyciski kolejki, kolumny tabel - w biezacej czcionce)
        auto& prawy = root.dodaj<PanelElastyczny>(Dokowanie::Prawo, 42, 330, 420);
        prawy.ustaw_minimum_zawartosci(&OknoGrajka::minimum_prawego, this);
        auto& karty = prawy.dodaj<Karty>(Dokowanie::Wypelnij);
        karty_ = &karty;
        karty.ustaw_zawijanie_naglowkow(true);     // wysokosc naglowkow z metryk czcionki (150 %: bez przyciecia)
        karty.dodaj_karte("Kolejka", buduj_kolejke());
        karty.dodaj_karte("Korektor", buduj_korektor());
        karty.dodaj_karte("Diagnostyka", buduj_diagnostyke());
        karty.ustaw_aktywna_karte(0);

        // srodek: biezacy utwor
        auto& srodek = root.dodaj<PanelDokowany>(Dokowanie::Wypelnij);
        srodek.ustaw_odstep(8);
        auto& gora_utw = srodek.dodaj<PanelDokowany>(Dokowanie::Gora);
        gora_utw.ustaw_preferowany_rozmiar(Rozmiar(-1, BOK_OKLADKI + 8)); gora_utw.ustaw_odstep(12);
        okladka_ = &gora_utw.dodaj<ObrazOkladki>(Dokowanie::Lewo, BOK_OKLADKI);
        okladka_->ustaw_tryb(TrybObrazu::Dopasuj);
        auto& info = gora_utw.dodaj<PanelDokowany>(Dokowanie::Wypelnij);
        info.ustaw_odstep(2);
        tytul_ = &info.dodaj<EtykietaJednowierszowa>(Dokowanie::Gora, "Nic nie gra", 2);
        tytul_->ustaw_preferowany_rozmiar(Rozmiar(-1, 40));
        wykonawca_ = &info.dodaj<EtykietaJednowierszowa>(Dokowanie::Gora, "");
        album_ = &info.dodaj<EtykietaJednowierszowa>(Dokowanie::Gora, "", 1, MOTYW_KOLOR_TEKST_DRUGI);
        rok_ = &info.dodaj<EtykietaJednowierszowa>(Dokowanie::Gora, "", 1, MOTYW_KOLOR_TEKST_DRUGI);
        format_ = &info.dodaj<EtykietaJednowierszowa>(Dokowanie::Gora, "", 1, MOTYW_KOLOR_TEKST_DRUGI);
        stan_ = &info.dodaj<EtykietaJednowierszowa>(Dokowanie::Gora, "", 1, MOTYW_KOLOR_AKCENT);
        EtykietaJednowierszowa* wiersze[] = {wykonawca_, album_, rok_, format_, stan_};   // Ring 3: bez <initializer_list>
        for (EtykietaJednowierszowa* e : wiersze) e->ustaw_preferowany_rozmiar(Rozmiar(-1, 22));

        auto& postep = srodek.dodaj<Siatka>(Dokowanie::Gora);
        postep.ustaw_preferowany_rozmiar(Rozmiar(-1, 30)); postep.ustaw_odstep(8);
        postep.dodaj_wiersz();
        postep.dodaj_kolumne(DefinicjaWymiaru::piksele(56)); postep.dodaj_kolumne(); postep.dodaj_kolumne(DefinicjaWymiaru::piksele(56));
        czas_ = new Etykieta("0:00"); czas_->ustaw_wyrownanie_pionowe_tekstu(WyrownaniePionowe::Srodek);
        calosc_ = new Etykieta("0:00"); calosc_->ustaw_wyrownanie_pionowe_tekstu(WyrownaniePionowe::Srodek);
        calosc_->ustaw_wyrownanie_tekstu(WyrownaniePoziome::Koniec);
        pasek_ = new SuwakPostepu();
        pasek_->ustaw_zakres(0, 1000); pasek_->ustaw_wartosc(0);
        pasek_->ustaw_podpowiedz("Przeciągnij, aby przewinąć utwór");
        pasek_->przy_zatwierdzeniu_wartosci([](Suwak*, int v, void* p) { static_cast<OknoGrajka*>(p)->przewin_do(v); }, this);
        postep.dodaj_do_siatki(czas_, 0, 0); postep.dodaj_do_siatki(pasek_, 0, 1); postep.dodaj_do_siatki(calosc_, 0, 2);

        wiz_ = &srodek.dodaj<WidokWizualizacji>(Dokowanie::Wypelnij, &o_.silnik().analizator());
    }

    // Dwa niezalezne wiersze (osobne siatki, wysokosc z zawartosci):
    //  1: transport (5 x gwiazdka, przyciski rozciagniete), odstep, Wycisz (Auto), glosnosc (2*), wartosc (Auto),
    //  2: przelaczniki i Zakoncz w kolumnach Auto (naturalna szerokosc tekstu) + odstep gwiazdkowy.
    // Kolumny Auto nie zachodza na siebie przy zadnej szerokosci >= minimum okna (wczesniej wspolna siatka 2* dawala
    // przy 980 px ok. 104 px na "Wizualizacja", ktora w TTF potrzebuje wiecej i nachodzila na "Diagnostyka").
    void buduj_sterowanie(PanelDokowany& root) {
        auto& ster = root.dodaj<PanelDokowany>(Dokowanie::Dol);
        ster.ustaw_odstep(6);
        auto& w2 = ster.dodaj<Siatka>(Dokowanie::Dol);
        auto& w1 = ster.dodaj<Siatka>(Dokowanie::Dol);
        wiersz_ster_[0] = &w1; wiersz_ster_[1] = &w2;
        Siatka* wiersze[] = {&w1, &w2};
        for (Siatka* w : wiersze) { w->ustaw_wypelnienie(Marginesy(0)); w->ustaw_odstep(8, 0); w->dodaj_wiersz(DefinicjaWymiaru::automatyczny()); }
        for (int i = 0; i < 5; ++i) w1.dodaj_kolumne(DefinicjaWymiaru::gwiazdka(1));
        w1.dodaj_kolumne(DefinicjaWymiaru::piksele(4));
        w1.dodaj_kolumne(DefinicjaWymiaru::automatyczny());
        w1.dodaj_kolumne(DefinicjaWymiaru::gwiazdka(2));
        w1.dodaj_kolumne(DefinicjaWymiaru::automatyczny());
        poprz_ = new Przycisk("Poprzedni");
        graj_ = new Przycisk("Odtwórz");
        pauza_ = new Przycisk("Pauza");
        stop_ = new Przycisk("Stop");
        nast_ = new Przycisk("Następny");
        poprz_->ustaw_podpowiedz("Poprzedni utwór (lub początek)");
        graj_->ustaw_podpowiedz("Odtwórz albo wznów");
        pauza_->ustaw_podpowiedz("Wstrzymaj odtwarzanie");
        stop_->ustaw_podpowiedz("Zatrzymaj i wróć na początek");
        nast_->ustaw_podpowiedz("Następny utwór z kolejki");
        poprz_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<OknoGrajka*>(p)->o_.poprzedni(); }, this);
        graj_->przy_kliknieciu([](Przycisk*, void* p) { auto* o = static_cast<OknoGrajka*>(p); o->odtworz_zaznaczony_lub_kolejke(); }, this);
        pauza_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<OknoGrajka*>(p)->o_.pauza(); }, this);
        stop_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<OknoGrajka*>(p)->o_.stop(); }, this);
        nast_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<OknoGrajka*>(p)->o_.nastepny(); }, this);
        w1.dodaj_do_siatki(poprz_, 0, 0); w1.dodaj_do_siatki(graj_, 0, 1); w1.dodaj_do_siatki(pauza_, 0, 2);
        w1.dodaj_do_siatki(stop_, 0, 3); w1.dodaj_do_siatki(nast_, 0, 4);
        wycisz_ = new Przycisk("Wycisz");
        wycisz_->ustaw_podpowiedz("Wycisz lub przywróć dźwięk");
        wycisz_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<OknoGrajka*>(p)->o_.przelacz_wyciszenie(); }, this);
        glosnosc_ = new Suwak();
        glosnosc_->ustaw_zakres(0, 100);
        glosnosc_->ustaw_podpowiedz("Głośność strumienia Grajka");
        glosnosc_->przy_zmianie_wartosci([](Suwak*, int v, void* p) { auto* o = static_cast<OknoGrajka*>(p); if (!o->sync_) o->o_.ustaw_glosnosc(uint32_t(v)); }, this);
        glosnosc_proc_ = new EtykietaJednowierszowa("80%");
        w1.dodaj_do_siatki(wycisz_, 0, 6); w1.dodaj_do_siatki(glosnosc_, 0, 7); w1.dodaj_do_siatki(glosnosc_proc_, 0, 8);

        for (int i = 0; i < 3; ++i) w2.dodaj_kolumne(DefinicjaWymiaru::automatyczny());
        w2.dodaj_kolumne(DefinicjaWymiaru::gwiazdka(1));
        for (int i = 0; i < 3; ++i) w2.dodaj_kolumne(DefinicjaWymiaru::automatyczny());
        losowo_ = new PoleWyboru("Losowo", false);
        losowo_->ustaw_podpowiedz("Odtwarzaj kolejkę w losowej kolejności");
        losowo_->przy_przelaczeniu([](PoleWyboru*, bool z, void* p) { auto* o = static_cast<OknoGrajka*>(p); if (!o->sync_) o->o_.ustaw_losowo(z); }, this);
        powtarzanie_ = new Przycisk("Powtarzanie: wył.");
        powtarzanie_->ustaw_podpowiedz("Przełącz: brak / utwór / wszystkie");
        powtarzanie_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<OknoGrajka*>(p)->o_.nastepne_powtarzanie(); }, this);
        w_tle_ = new PoleWyboru("Odtwarzanie w tle", false);
        w_tle_->ustaw_podpowiedz("Graj dalej po zminimalizowaniu i zamknięciu");
        w_tle_->przy_przelaczeniu([](PoleWyboru*, bool z, void* p) { auto* o = static_cast<OknoGrajka*>(p); if (!o->sync_) o->o_.ustaw_w_tle(z); }, this);
        wizualizacja_ = new PoleWyboru("Wizualizacja", false);
        wizualizacja_->ustaw_podpowiedz("Wskaźniki poziomu i widmo dźwięku");
        wizualizacja_->przy_przelaczeniu([](PoleWyboru*, bool z, void* p) { auto* o = static_cast<OknoGrajka*>(p); if (!o->sync_) o->o_.ustaw_wizualizacje(z); }, this);
        diagnostyka_ = new PoleWyboru("Diagnostyka", false);
        diagnostyka_->ustaw_podpowiedz("Pomiar czasu sekcji (karta Diagnostyka)");
        diagnostyka_->przy_przelaczeniu([](PoleWyboru*, bool z, void* p) { auto* o = static_cast<OknoGrajka*>(p); if (!o->sync_) o->o_.ustaw_diagnostyke(z); }, this);
        zakoncz_ = new Przycisk("Zakończ");
        zakoncz_->ustaw_podpowiedz("Zatrzymaj muzykę i zamknij Grajka");
        zakoncz_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<OknoGrajka*>(p)->zakoncz_calkowicie(); }, this);
        w2.dodaj_do_siatki(losowo_, 0, 0); w2.dodaj_do_siatki(powtarzanie_, 0, 1); w2.dodaj_do_siatki(w_tle_, 0, 2);
        w2.dodaj_do_siatki(wizualizacja_, 0, 4); w2.dodaj_do_siatki(diagnostyka_, 0, 5); w2.dodaj_do_siatki(zakoncz_, 0, 6);
        Przycisk* wypelniaja[] = {poprz_, graj_, pauza_, stop_, nast_};
        for (Przycisk* b : wypelniaja) b->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        // teksty zmienne: kolumna Auto mierzy najszerszy wariant (inaczej "Powtarzanie: wszystkie" / "Przywróć" /
        // "wycisz." bylyby przyciete po zmianie tekstu - zmiana tekstu nie przelicza ukladu)
        static const char* const WARIANTY_POWT[] = {"Powtarzanie: wył.", "Powtarzanie: utwór", "Powtarzanie: wszystkie"};
        static const char* const WARIANTY_WYC[] = {"Wycisz", "Przywróć"};
        static const char* const WARIANTY_PROC[] = {"100%", "wycisz."};
        warianty_[0] = {powtarzanie_, nullptr, WARIANTY_POWT, 3};
        warianty_[1] = {wycisz_, nullptr, WARIANTY_WYC, 2};
        warianty_[2] = {nullptr, glosnosc_proc_, WARIANTY_PROC, 2};
    }

    // Minimalna szerokosc kontrolek o zmiennym tekscie = najszerszy wariant w biezacej czcionce (DIP). Liczone przy
    // budowie i zmianie motywu/czcionki (bez RTTI: Ring 3 kompiluje z -fno-rtti).
    struct Warianty { Kontrolka* k = nullptr; EtykietaJednowierszowa* e = nullptr; const char* const* teksty = nullptr; int n = 0; };
    Warianty warianty_[3];
    void dopasuj_warianty() {
        for (Warianty& w : warianty_) {
            Kontrolka* k = w.e ? static_cast<Kontrolka*>(w.e) : w.k;
            if (!k) continue;
            const SkalaUI s = k->skala_dpi();
            const int p = int(s.procent() ? s.procent() : 100);
            int maks = 0;
            if (w.e) {
                for (int i = 0; i < w.n; ++i) { const int d = dip_tekstu(w.teksty[i], s, 6); if (d > maks) maks = d; }
                w.e->ustaw_preferowany_rozmiar(Rozmiar(maks, -1));
            } else {
                char biezacy[MAKS_DLUGOSC_TEKSTU_GRAJKA]; kopiuj_tekst(biezacy, sizeof(biezacy), k->tekst());
                k->ustaw_minimalny_rozmiar(Rozmiar(0, 0));
                for (int i = 0; i < w.n; ++i) { k->ustaw_tekst(w.teksty[i]); const int d = (k->zmierz(Rozmiar(-1, -1)).szerokosc * 100 + p - 1) / p + 4; if (d > maks) maks = d; }
                k->ustaw_tekst(biezacy);
                k->ustaw_minimalny_rozmiar(Rozmiar(maks, 0));
            }
        }
    }

    Kontrolka* buduj_kolejke() {
        auto* p = new PanelDokowany();
        p->ustaw_wypelnienie(Marginesy(6)); p->ustaw_odstep(6);
        auto& dol = p->dodaj<SiatkaPrzyciskow>(Dokowanie::Dol);
        przyciski_kol_ = &dol;
        dol.ustaw_odstep(6, 6); dol.ustaw_wypelnienie(Marginesy(0));
        for (int w = 0; w < 3; ++w) dol.dodaj_wiersz(DefinicjaWymiaru::automatyczny());
        dol.dodaj_kolumne(); dol.dodaj_kolumne();
        auto* gora = new Przycisk("W górę"); auto* dol_ = new Przycisk("W dół"); auto* usun = new Przycisk("Usuń"); auto* czysc = new Przycisk("Wyczyść");
        auto* zapisz = new Przycisk("Zapisz listę"); auto* wczytaj = new Przycisk("Wczytaj listę");
        gora->ustaw_podpowiedz("Przesuń utwór wyżej"); dol_->ustaw_podpowiedz("Przesuń utwór niżej");
        usun->ustaw_podpowiedz("Usuń zaznaczony z kolejki"); czysc->ustaw_podpowiedz("Opróżnij kolejkę");
        zapisz->ustaw_podpowiedz("Zapisz kolejkę jako M3U8 w ustawieniach");
        wczytaj->ustaw_podpowiedz("Wczytaj zapisaną listę M3U8");
        gora->przy_kliknieciu([](Przycisk*, void* q) { static_cast<OknoGrajka*>(q)->przesun_zaznaczony(-1); }, this);
        dol_->przy_kliknieciu([](Przycisk*, void* q) { static_cast<OknoGrajka*>(q)->przesun_zaznaczony(1); }, this);
        usun->przy_kliknieciu([](Przycisk*, void* q) { auto* o = static_cast<OknoGrajka*>(q); o->o_.usun_z_kolejki(uint32_t(o->kol_->wybrany_wiersz())); }, this);
        czysc->przy_kliknieciu([](Przycisk*, void* q) { static_cast<OknoGrajka*>(q)->o_.wyczysc_kolejke(); }, this);
        zapisz->przy_kliknieciu([](Przycisk*, void* q) { static_cast<OknoGrajka*>(q)->o_.zapisz_liste(); }, this);
        wczytaj->przy_kliknieciu([](Przycisk*, void* q) { static_cast<OknoGrajka*>(q)->o_.wczytaj_liste(); }, this);
        Przycisk* wszystkie[] = {gora, dol_, usun, czysc, zapisz, wczytaj};
        for (Przycisk* b : wszystkie) b->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        // 3 wiersze x 2 kolumny (wagi wg tekstow): miesci sie w waskim panelu bez obcinania "Wyczyść"
        dol.dodaj_przycisk(gora, 0, 0); dol.dodaj_przycisk(dol_, 0, 1);
        dol.dodaj_przycisk(usun, 1, 0); dol.dodaj_przycisk(czysc, 1, 1);
        dol.dodaj_przycisk(zapisz, 2, 0); dol.dodaj_przycisk(wczytaj, 2, 1);
        czysc_ = czysc;
        kol_ = &p->dodaj<TabelaGrajka>(Dokowanie::Wypelnij);
        kol_->ustaw_dostawce_grajka(&dane_kol_);
        kol_->ustaw_sortowanie_naglowkiem(false);
        kol_->ustaw_szerokosc_wg_naglowkow(true);
        kol_->przy_aktywacji([](TabelaGrajka*, size_t w, void* q) { static_cast<OknoGrajka*>(q)->o_.odtworz_z_kolejki(uint32_t(w)); }, this);
        return p;
    }

    // Korektor: wiersz [Wlaczony][preset], etykiety dB, suwaki, etykiety Hz, opis. Opis (Dol) dokowany PRZED siatka
    // (Wypelnij) - inaczej siatka zajmuje cale miejsce, a opis nachodzi na suwaki i etykiety Hz.
    Kontrolka* buduj_korektor() {
        auto* p = new PanelDokowany();
        p->ustaw_wypelnienie(Marginesy(6)); p->ustaw_odstep(6);
        auto& gora = p->dodaj<Siatka>(Dokowanie::Gora);
        gora.ustaw_wypelnienie(Marginesy(0)); gora.ustaw_odstep(8, 0);
        gora.dodaj_wiersz(DefinicjaWymiaru::automatyczny());
        gora.dodaj_kolumne(DefinicjaWymiaru::automatyczny()); gora.dodaj_kolumne(DefinicjaWymiaru::gwiazdka(1));
        korektor_ = new PoleWyboru("Włączony", false);
        korektor_->ustaw_podpowiedz("Wyłączony korektor nie zmienia dźwięku");
        korektor_->przy_przelaczeniu([](PoleWyboru*, bool z, void* q) { auto* o = static_cast<OknoGrajka*>(q); if (!o->sync_) o->o_.ustaw_korektor(z); }, this);
        korektor_->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Srodek);
        preset_ = new ListaRozwijana();
        for (int i = 0; i < int(PresetKorektora::Liczba); ++i) preset_->dodaj_element(nazwa_presetu(PresetKorektora(i)), uint64_t(i));
        preset_->ustaw_wybrany_indeks(0);
        preset_->ustaw_podpowiedz("Ustawienie wstępne korektora");
        preset_->przy_zmianie_wyboru([](ListaRozwijana*, int i, void* q) { auto* o = static_cast<OknoGrajka*>(q); if (!o->sync_ && i >= 0) o->o_.ustaw_preset(PresetKorektora(i)); }, this);
        gora.dodaj_do_siatki(korektor_, 0, 0); gora.dodaj_do_siatki(preset_, 0, 1);
        // opis: wysokosc z zawijania przy biezacej szerokosci i czcionce (Etykieta::zmierz liczy wiersze) - stale
        // 52 px miescily 3 wiersze czcionki bitmapowej, ale nie TTF (natywnie ginal trzeci wiersz)
        auto& opis = p->dodaj<Etykieta>(Dokowanie::Dol, "Pasma w Hz, ±12 dB. Przy podbiciu sygnał jest wstępnie ściszany.");
        opis.ustaw_zawijanie(true); opis.ustaw_wypelnienie(Marginesy(2, 2, 2, 2));
        opis_korektora_ = &opis;
        opis.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI));
        // etykiety dB (gora) i Hz (dol) poza siatka: wyrownane do srodkow kolumn suwakow, w razie potrzeby w dwoch
        // rzedach (EtykietyPasm) - przy 1080 px w TTF "125", "250", "500" stykaly sie i byly obcinane
        db_pasm_ = &p->dodaj<EtykietyPasm>(Dokowanie::Gora, WYPELNIENIE_SIATKI_KOR, ODSTEP_SIATKI_KOR, MOTYW_KOLOR_TEKST);
        hz_pasm_ = &p->dodaj<EtykietyPasm>(Dokowanie::Dol, WYPELNIENIE_SIATKI_KOR, ODSTEP_SIATKI_KOR, MOTYW_KOLOR_TEKST_DRUGI);
        auto& siatka = p->dodaj<Siatka>(Dokowanie::Wypelnij);
        siatka.ustaw_odstep(ODSTEP_SIATKI_KOR, 0); siatka.ustaw_wypelnienie(Marginesy(WYPELNIENIE_SIATKI_KOR, 0, WYPELNIENIE_SIATKI_KOR, 0));
        siatka.dodaj_wiersz();
        static const char* const HZ[Tabele::LICZBA_PASM] = {"31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};
        for (int b = 0; b < Tabele::LICZBA_PASM; ++b) {
            siatka.dodaj_kolumne();
            db_pasm_->ustaw(b, "0");
            hz_pasm_->ustaw(b, HZ[b]);
            pasma_[b] = new Suwak();
            pasma_[b]->ustaw_orientacje(Orientacja::Pionowa);
            pasma_[b]->ustaw_zakres(Tabele::MIN_DB, Tabele::MAKS_DB);
            pasma_[b]->ustaw_wartosc(0);
            pasma_[b]->ustaw_wyrownanie_poziome(WyrownaniePoziome::Srodek);
            pasma_[b]->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            pasma_[b]->ustaw_podpowiedz("Wzmocnienie pasma: -12..+12 dB");
            pasma_[b]->przy_zmianie_wartosci([](Suwak* s, int v, void* q) {
                auto* o = static_cast<OknoGrajka*>(q);
                if (o->sync_) return;
                for (int k = 0; k < Tabele::LICZBA_PASM; ++k) if (o->pasma_[k] == s) o->o_.ustaw_pasmo(k, v);
            }, this);
            siatka.dodaj_do_siatki(pasma_[b], 0, b);
        }
        siatka_korektora_ = &siatka;
        return p;
    }

    // Diagnostyka: wszystkie liczniki w tabeli (bez obcinania) + objasnienie zrodla czasow.
    Kontrolka* buduj_diagnostyke() {
        auto* p = new PanelDokowany();
        p->ustaw_wypelnienie(Marginesy(6)); p->ustaw_odstep(6);
        auto& opis = p->dodaj<Etykieta>(Dokowanie::Dol, "Czasy: ostatnia sekunda, zegarowe (z wywłaszczeniem). Bez okresów pominiętych przez HDA.");
        opis.ustaw_zawijanie(true); opis.ustaw_wypelnienie(Marginesy(2, 2, 2, 2));
        opis.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI));
        opis_diagnostyki_ = &opis;
        diag_tab_ = &p->dodaj<TabelaGrajka>(Dokowanie::Wypelnij);
        diag_tab_->ustaw_dostawce_grajka(&dane_diag_);
        diag_tab_->ustaw_sortowanie_naglowkiem(false);
        diag_tab_->ustaw_szerokosc_wg_naglowkow(true);
        dane_diag_.odswiez();
        return p;
    }

    // ------------------------------------------------------------ minima paneli z zawartosci (piksele)
    static constexpr int WYPELNIENIE_SIATKI_KOR = 4, ODSTEP_SIATKI_KOR = 2;
    static constexpr int KARTA_DIAGNOSTYKI = 2;
    static int minimum_lewego(const void* p, SkalaUI s) {
        auto* o = static_cast<const OknoGrajka*>(p);
        int m = o->bib_ ? o->bib_->minimum_px() + o->bib_->margines().poziomo() * s : 0;   // tabela + jej margines
        const int b = o->przyciski_bib_ ? o->przyciski_bib_->minimum_px() : 0;
        return m > b ? m : b;
    }
    // Korektor: 10 kolumn suwakow; etykiety w dwoch rzedach potrzebuja 2 kolumn na najszerszy tekst ("125", "+12").
    static int minimum_korektora(SkalaUI s) {
        static const char* const T[] = {"125", "250", "500", "16k", "+12", "-12"};
        int tekst = 0;
        for (const char* t : T) { const int w = CzcionkaUI::zmierz_tekst(t, s); if (w > tekst) tekst = w; }
        int kol = 20 * s;                                         // suwak pionowy
        const int z_etykiet = (tekst + 4 * s - ODSTEP_SIATKI_KOR * s + 1) / 2;   // dwa rzedy: tekst <= 2 kolumny
        if (z_etykiet > kol) kol = z_etykiet;
        return Tabele::LICZBA_PASM * kol + (Tabele::LICZBA_PASM - 1) * ODSTEP_SIATKI_KOR * s + 2 * WYPELNIENIE_SIATKI_KOR * s;
    }
    static int minimum_prawego(const void* p, SkalaUI s) {
        auto* o = static_cast<const OknoGrajka*>(p);
        int m = minimum_korektora(s);
        const int z[] = {o->przyciski_kol_ ? o->przyciski_kol_->minimum_px() : 0, o->kol_ ? o->kol_->minimum_px() : 0,
                         o->diag_tab_ ? o->diag_tab_->minimum_px() : 0};
        for (int v : z) if (v > m) m = v;
        return m + 2 * 6 * s + 2 * 4 * s;            // wypelnienie karty (6) i ramka kart
    }

    // ------------------------------------------------------------ akcje
    void odtworz_zaznaczony_lub_kolejke() {
        // "Odtworz" w stanie zatrzymania bez kolejki: zaznaczony wiersz biblioteki; inaczej wznowienie/kolejka.
        const Stan s = o_.silnik().stan();
        if (!o_.silnik().zaladowany() && !o_.kolejka().liczba() && o_.biblioteka().widoczne() && (s == Stan::Zatrzymany || s == Stan::Blad)) {
            o_.odtworz_z_widoku(uint32_t(bib_->wybrany_wiersz()));
            return;
        }
        o_.odtworz();
    }
    void przesun_zaznaczony(int kierunek) {
        const size_t w = kol_->wybrany_wiersz();
        if (o_.przesun_w_kolejce(uint32_t(w), kierunek)) kol_->ustaw_wybrany_wiersz(size_t(int(w) + kierunek));
    }
    void przewin_do(int promile) {
        const uint64_t c = o_.silnik().czas_ms();
        if (!c) return;
        o_.przewin(c * uint64_t(promile < 0 ? 0 : promile) / 1000ULL);
        ostatni_postep_ = 0;
    }

    // ------------------------------------------------------------ hak rysowania (punkt kontrolny PCM + pomiar)
    static inline OknoGrajka* aktywne_ = nullptr;
    static void hak_rysowania_grajka(FazaRysowania f) {
        OknoGrajka* o = aktywne_;
        if (!o) return;
        const bool pomiar = g_profiler.wlaczony();
        const uint64_t t = pomiar ? Profiler::takt() : 0;
        switch (f) {
            case FazaRysowania::Poczatek:
                o->cykl_rys_ = 0; o->cykl_publ_ = 0; o->znacznik_ = t;
                g_profiler.odrysowanie();
                break;
            case FazaRysowania::PrzedPublikacja:
                if (pomiar) o->cykl_rys_ += t - o->znacznik_;
                o->o_.silnik().dopompuj();                 // kolejka pelna tuz przed BWS 57 i kompozycja
                o->znacznik_ = pomiar ? Profiler::takt() : 0;
                break;
            case FazaRysowania::PoPublikacji:
                if (pomiar) o->cykl_publ_ += t - o->znacznik_;
                o->znacznik_ = t;
                break;
            case FazaRysowania::Koniec:
                if (pomiar) {
                    o->cykl_rys_ += t - o->znacznik_;      // popupy i reszta cyklu po ostatniej publikacji
                    g_profiler.dodaj(Sekcja::OknoRysowanie, o->cykl_rys_);
                    if (o->cykl_publ_) g_profiler.dodaj(Sekcja::OknoPublikacja, o->cykl_publ_);
                }
                break;
        }
    }
    uint64_t znacznik_ = 0, cykl_rys_ = 0, cykl_publ_ = 0;

    // ------------------------------------------------------------ aktualizacja widoku
    uint64_t teraz_ms() const { return czas_ns_ ? czas_ns_() / 1000000ULL : 0; }
    static void ustaw(Kontrolka* e, const char* t) { if (e && !rowne(e->tekst(), t)) e->ustaw_tekst(t); }

    void aktualizuj(bool wszystko) {
        const uint64_t ms = teraz_ms();
        if (wszystko || o_.wersja_biblioteki() != w_bib_) {
            w_bib_ = o_.wersja_biblioteki();
            const size_t n = o_.biblioteka().widoczne();
            if (n && bib_->wybrany_wiersz() >= n) bib_->ustaw_wybrany_wiersz(n - 1);
            bib_->uniewaznij();
            char t[96]; Tekst s(t, sizeof(t));
            if (o_.biblioteka().skanuje()) { s.dodaj("Skanowanie… "); s.liczba(o_.biblioteka().postep()); s.dodaj(" / "); s.liczba(o_.biblioteka().kandydaci()); }
            else { s.liczba(n); s.dodaj(" z "); s.liczba(o_.biblioteka().liczba()); s.dodaj(" utworów"); }
            ustaw(stan_bib_, t);
        } else if (o_.biblioteka().skanuje() && ms - ostatni_skan_ >= 250) {
            ostatni_skan_ = ms;
            char t[96]; Tekst s(t, sizeof(t));
            s.dodaj("Skanowanie… "); s.liczba(o_.biblioteka().postep()); s.dodaj(" / "); s.liczba(o_.biblioteka().kandydaci());
            ustaw(stan_bib_, t);
        }
        if (wszystko || o_.wersja_kolejki() != w_kol_) {
            w_kol_ = o_.wersja_kolejki();
            const size_t n = o_.kolejka().liczba();
            if (n && kol_->wybrany_wiersz() >= n) kol_->ustaw_wybrany_wiersz(n - 1);
            kol_->uniewaznij();
            uniewaznij_wskazniki();           // wskaznik biezacego utworu: tylko wiersze, nie cala biblioteka
        }
        if (wszystko || o_.wersja_utworu() != w_utw_) { w_utw_ = o_.wersja_utworu(); aktualizuj_utwor(); }
        if (wszystko || o_.wersja_komunikatu() != w_kom_) { w_kom_ = o_.wersja_komunikatu(); aktualizuj_komunikat(); }
        if (wszystko || o_.wersja_ustawien() != w_ust_) { w_ust_ = o_.wersja_ustawien(); synchronizuj_ustawienia(); }
        if (wszystko || ms - ostatni_postep_ >= 1000) { ostatni_postep_ = ms; aktualizuj_postep(); }   // czas w sekundach
        const bool wiz = o_.konfiguracja().wizualizacja && !czy_zminimalizowane();
        wiz_->ustaw_aktywna(wiz);
        if (wiz && ms - ostatnia_wiz_ >= 250) {          // 4 Hz: pod emulacja kazda klatka kosztuje (pomiar QEMU)
            ostatnia_wiz_ = ms;
            { PomiarSekcji p(Sekcja::Widmo); o_.silnik().analizator().odswiez(20); }
            klatki_wiz_ += uint32_t(wiz_->odswiez());     // tylko zmienione pola (wskazniki / widmo)
        }
        if (wszystko || o_.wersja_diagnostyki() != w_diag_) {
            w_diag_ = o_.wersja_diagnostyki();
            const bool d = o_.konfiguracja().diagnostyka;
            // krotkie podsumowanie w stalym wierszu paska (bez zmiany wysokosci i bez przeliczania ukladu)
            komunikat_->ustaw_skrot(d ? (o_.tekst_diagnostyki()[0] ? o_.tekst_diagnostyki() : "Diagnostyka: pomiar…") : "");
            aktualizuj_diagnostyke(false);
        } else if (karty_ && karty_->aktywna_karta() != ostatnia_karta_) {
            ostatnia_karta_ = karty_->aktywna_karta();
            aktualizuj_diagnostyke(true);               // karta wlasnie pokazana: od razu aktualne wartosci
        } else if (ms - ostatnia_diag_ >= 1000) {
            aktualizuj_diagnostyke(false);              // liczniki ciaglosci (bez pomiaru sekcji) - tez co 1 s
        }
        if (nazwa_ && nazwa_->skala_dpi().procent() != ostatnia_skala_) {   // zmiana skali UI: warianty tekstow
            ostatnia_skala_ = nazwa_->skala_dpi().procent();
            if (!wszystko) { dopasuj_warianty(); przelicz_uklad(); uniewaznij(); }
        }
        if (!wszystko && najechana_ && kontrolka_glowna())
            podpowiedzi_.obsluz_ruch(zarzadca_popupow(), najechana_, mysz_x_, mysz_y_, ms);
    }

    // Karta "Diagnostyka": tylko gdy widoczna, najwyzej co ~1 s (albo na zadanie), uniewaznia wylacznie zmienione
    // wiersze tabeli. Ukryta karta nie kosztuje odrysowan.
    void aktualizuj_diagnostyke(bool wymus) {
        const uint64_t ms = teraz_ms();
        if (!wymus && ms - ostatnia_diag_ < 1000 && ostatnia_diag_) return;
        ostatnia_diag_ = ms ? ms : 1;
        if (!karty_ || karty_->aktywna_karta() != KARTA_DIAGNOSTYKI || czy_zminimalizowane()) return;
        const uint32_t zm = dane_diag_.odswiez();
        // nazwy sa stale (wypelnione przy budowie), wiec tylko kolumna wartosci zmienionych wierszy
        for (size_t i = 0; i < DaneDiagnostyki::N; ++i)
            if (zm & (1u << i)) { diag_tab_->uniewaznij_wiersz(i, 1); ++wiersze_diag_; }   // bez __builtin_popcount (Ring 3: brak libgcc)
    }

    void aktualizuj_utwor() {
        const Utwor* u = o_.utwor_biezacy();
        const Silnik& s = o_.silnik();
        char t[160];
        if (!u) {
            ustaw(tytul_, "Nic nie gra"); ustaw(wykonawca_, "Dwuklik albo Enter w bibliotece."); ustaw(album_, ""); ustaw(rok_, ""); ustaw(format_, "");
        } else {
            ustaw(tytul_, u->tytul());
            { Tekst x(t, sizeof(t)); x.dodaj(u->meta.wykonawca[0] ? u->meta.wykonawca : "Nieznany wykonawca"); ustaw(wykonawca_, t); }
            { Tekst x(t, sizeof(t)); if (u->meta.album[0]) { x.dodaj("Album: "); x.dodaj(u->meta.album); } ustaw(album_, t); }
            { Tekst x(t, sizeof(t));
              if (u->meta.rok[0]) { x.dodaj("Rok: "); x.dodaj(u->meta.rok); }
              if (u->meta.numer[0]) { if (u->meta.rok[0]) x.dodaj(" · "); x.dodaj("Utwór nr "); x.dodaj(u->meta.numer); }
              ustaw(rok_, t); }
            opis_formatu(*u, t, sizeof(t)); ustaw(format_, t);
        }
        { Tekst x(t, sizeof(t)); x.dodaj(nazwa_stanu(s.stan()));
          if (s.stan() == Stan::Odtwarzanie && o_.konfiguracja().w_tle) x.dodaj(" · w tle dozwolone");
          ustaw(stan_, t); }
        // okladka: dekodowana raz przez kontroler; tutaj tylko przekazanie bitmapy do kontrolki Obraz
        const uint32_t wersja = o_.okladka().jest() ? uint32_t(o_.biezacy_utwor()) ^ 0x8000u : 0xFFFFFFFEu;
        if (wersja != okladka_wersja_) {
            okladka_wersja_ = wersja;
            if (o_.okladka().jest()) okladka_->ustaw_okladke(o_.okladka().piksele(), int(o_.okladka().szerokosc()), int(o_.okladka().wysokosc()), true);
            else {
                if (!domyslna_) domyslna_ = grafika_domyslna(BOK_OKLADKI, motyw_kolor(MOTYW_KOLOR_AKCENT), motyw_kolor(MOTYW_KOLOR_PANEL));
                if (domyslna_) okladka_->ustaw_okladke(domyslna_, BOK_OKLADKI, BOK_OKLADKI, false);
            }
        }
        const bool gra = s.stan() == Stan::Odtwarzanie;
        pauza_->ustaw_wlaczona(gra);
        stop_->ustaw_wlaczona(s.zaladowany() && (gra || s.stan() == Stan::Pauza || s.pozycja_ms() > 0));
        graj_->ustaw_tekst(s.stan() == Stan::Pauza ? "Wznów" : "Odtwórz");
        graj_->ustaw_wlaczona(!gra);
        pasek_->ustaw_wlaczona(s.zaladowany() && s.czas_ms() > 0);
        uniewaznij_wskazniki();
        aktualizuj_postep();
    }

    // Wskaznik biezacego utworu: tylko poprzedni i nowy wiersz w obu tabelach (zamiast calych tabel).
    void uniewaznij_wskazniki() {
        int wb = -1;
        for (uint32_t w = 0; w < o_.biblioteka().widoczne() && o_.biezacy_utwor() >= 0; ++w)
            if (o_.biblioteka().widoczny(w) == o_.biezacy_utwor()) { wb = int(w); break; }
        const int wk = o_.kolejka().biezaca();
        if (wskaznik_bib_ >= 0) bib_->uniewaznij_wiersz(size_t(wskaznik_bib_));
        if (wb >= 0) bib_->uniewaznij_wiersz(size_t(wb));
        if (wskaznik_kol_ >= 0) kol_->uniewaznij_wiersz(size_t(wskaznik_kol_));
        if (wk >= 0) kol_->uniewaznij_wiersz(size_t(wk));
        wskaznik_bib_ = wb; wskaznik_kol_ = wk;
    }

    void aktualizuj_postep() {
        const Silnik& s = o_.silnik();
        char t[24];
        const uint64_t poz = s.zaladowany() ? s.pozycja_ms() : 0, c = s.czas_ms();
        { Tekst x(t, sizeof(t)); x.czas(poz); ustaw(czas_, t); }
        { Tekst x(t, sizeof(t)); if (c) x.czas(c); else x.dodaj("–:––"); ustaw(calosc_, t); }
        if (!pasek_->przeciagany()) {
            const int v = c ? int((poz > c ? c : poz) * 1000ULL / c) : 0;
            if (v != pasek_->wartosc()) { sync_ = true; pasek_->ustaw_wartosc(v); sync_ = false; }
        }
    }

    // Pasek stanu ma stala wysokosc: zmiana komunikatu uniewaznia tylko pasek (bez przeliczania ukladu).
    void aktualizuj_komunikat() {
        PasekStanuGrajka::Rodzaj r = PasekStanuGrajka::Rodzaj::Informacja;
        switch (o_.rodzaj_komunikatu()) {
            case RodzajKomunikatu::Sukces: r = PasekStanuGrajka::Rodzaj::Sukces; break;
            case RodzajKomunikatu::Ostrzezenie: r = PasekStanuGrajka::Rodzaj::Ostrzezenie; break;
            case RodzajKomunikatu::Blad: r = PasekStanuGrajka::Rodzaj::Blad; break;
            default: break;
        }
        komunikat_->ustaw_komunikat(o_.komunikat(), r);
    }

    void synchronizuj_ustawienia() {
        const Konfiguracja& k = o_.konfiguracja();
        sync_ = true;
        if (glosnosc_->wartosc() != int(k.glosnosc)) glosnosc_->ustaw_wartosc(int(k.glosnosc));
        char t[16]; { Tekst x(t, sizeof(t)); x.liczba(k.glosnosc); x.dodaj("%"); }
        ustaw(glosnosc_proc_, k.wyciszony ? "wycisz." : t);
        wycisz_->ustaw_tekst(k.wyciszony ? "Przywróć" : "Wycisz");
        if (losowo_->czy_zaznaczone() != k.losowo) losowo_->ustaw_zaznaczone(k.losowo);
        if (w_tle_->czy_zaznaczone() != k.w_tle) w_tle_->ustaw_zaznaczone(k.w_tle);
        if (wizualizacja_->czy_zaznaczone() != k.wizualizacja) wizualizacja_->ustaw_zaznaczone(k.wizualizacja);
        if (diagnostyka_->czy_zaznaczone() != k.diagnostyka) diagnostyka_->ustaw_zaznaczone(k.diagnostyka);
        if (podkatalogi_->czy_zaznaczone() != k.podkatalogi) podkatalogi_->ustaw_zaznaczone(k.podkatalogi);
        powtarzanie_->ustaw_tekst(nazwa_powtarzania(k.powtarzanie));
        if (korektor_->czy_zaznaczone() != k.korektor) korektor_->ustaw_zaznaczone(k.korektor);
        const Korektor& kor = o_.silnik().korektor();
        for (int b = 0; b < Tabele::LICZBA_PASM; ++b) {
            if (pasma_[b]->wartosc() != kor.wzmocnienie(b)) pasma_[b]->ustaw_wartosc(kor.wzmocnienie(b));
            char d[8]; Tekst x(d, sizeof(d));
            const int v = kor.wzmocnienie(b);
            if (v > 0) x.dodaj("+");
            if (v < 0) { x.dodaj("-"); x.liczba(uint64_t(-v)); } else x.liczba(uint64_t(v));
            db_pasm_->ustaw(b, d);
            pasma_[b]->ustaw_wlaczona(k.korektor);
        }
        db_pasm_->ustaw_wlaczona(k.korektor);
        const int p = int(kor.rozpoznaj_preset());
        if (preset_->wybrany_indeks() != p) preset_->ustaw_wybrany_indeks(p);
        preset_->ustaw_wlaczona(k.korektor);
        sync_ = false;
    }

    Odtwarzacz o_;
    DaneBiblioteki dane_bib_;
    DaneKolejki dane_kol_;
    DaneDiagnostyki dane_diag_;
    uint64_t (*czas_ns_)() = nullptr;
    ZarzadcaPodpowiedzi podpowiedzi_;
    Kontrolka* najechana_ = nullptr;
    int mysz_x_ = 0, mysz_y_ = 0;
    bool sync_ = false;
    uint32_t w_bib_ = 0xFFFFFFFF, w_kol_ = 0xFFFFFFFF, w_utw_ = 0xFFFFFFFF, w_kom_ = 0xFFFFFFFF, w_ust_ = 0xFFFFFFFF, w_diag_ = 0xFFFFFFFF;
    int wskaznik_bib_ = -1, wskaznik_kol_ = -1;
    uint32_t klatki_wiz_ = 0;            // liczba uniewaznionych pol wizualizacji (testy czestotliwosci)
    uint32_t okladka_wersja_ = 0xFFFFFFFF;
    uint64_t ostatni_postep_ = 0, ostatnia_wiz_ = 0, ostatni_skan_ = 0, ostatnia_diag_ = 0;
    uint32_t ostatnia_skala_ = 0, wiersze_diag_ = 0;
    int ostatnia_karta_ = -1;
    uint32_t* domyslna_ = nullptr;

    PasekStanuGrajka* komunikat_ = nullptr;
    EtykietaJednowierszowa* nazwa_ = nullptr;
    SiatkaPrzyciskow *przyciski_bib_ = nullptr, *przyciski_kol_ = nullptr;
    Przycisk* czysc_ = nullptr;
    Siatka* siatka_korektora_ = nullptr;
    Siatka* wiersz_ster_[2]{};
    TabelaGrajka* diag_tab_ = nullptr;
    Etykieta* opis_diagnostyki_ = nullptr;
    Karty* karty_ = nullptr;
    PoleWyszukiwania* szukaj_ = nullptr;
    Przycisk *odswiez_ = nullptr, *dodaj_kol_ = nullptr;
    PoleWyboru* podkatalogi_ = nullptr;
    EtykietaJednowierszowa* stan_bib_ = nullptr;
    TabelaGrajka *bib_ = nullptr, *kol_ = nullptr;
    ObrazOkladki* okladka_ = nullptr;
    EtykietaJednowierszowa *tytul_ = nullptr, *wykonawca_ = nullptr, *album_ = nullptr, *rok_ = nullptr, *format_ = nullptr, *stan_ = nullptr;
    Etykieta *czas_ = nullptr, *calosc_ = nullptr;
    EtykietaJednowierszowa* glosnosc_proc_ = nullptr;
    SuwakPostepu* pasek_ = nullptr;
    WidokWizualizacji* wiz_ = nullptr;
    Przycisk *poprz_ = nullptr, *graj_ = nullptr, *pauza_ = nullptr, *stop_ = nullptr, *nast_ = nullptr, *wycisz_ = nullptr;
    Przycisk *powtarzanie_ = nullptr, *zakoncz_ = nullptr;
    Suwak* glosnosc_ = nullptr;
    PoleWyboru *losowo_ = nullptr, *w_tle_ = nullptr, *wizualizacja_ = nullptr, *diagnostyka_ = nullptr, *korektor_ = nullptr;
    ListaRozwijana* preset_ = nullptr;
    Suwak* pasma_[Tabele::LICZBA_PASM]{};
    EtykietyPasm *db_pasm_ = nullptr, *hz_pasm_ = nullptr;
    Etykieta* opis_korektora_ = nullptr;
};

} // namespace Grajek
