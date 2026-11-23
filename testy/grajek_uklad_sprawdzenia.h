// Grajek - wspolne sprawdzenia ukladu okna (dolaczane po okno.h z "#define private public").
// Uzywane przez test_grajek_ui_host.cpp (czcionka bitmapowa hosta) i test_grajek_ui_ttf_host.cpp (prawdziwe TTF
// przez BSF - te same metryki co natywny BursztynUI, w ktorych wystepowaly defekty: przyciety "j" w "Grajek",
// stykajace sie etykiety Hz, ucieta linia opisu korektora, obciete naglowki biblioteki, "Wyczyść", nachodzenie
// "Wizualizacja" na "Diagnostyka", przycieta podpowiedz, skok sterowania po wlaczeniu diagnostyki).
// Wymaga od dolaczajacego: makra CHECK(x).
#pragma once

namespace GrajekUklad {
using namespace Grajek;

inline bool rozlaczne(Prostokat a, Prostokat b) { return a.prawy() <= b.x || b.prawy() <= a.x || a.dolny() <= b.y || b.dolny() <= a.y; }
inline bool wewnatrz(Prostokat r, int w, int h) { return r.x >= 0 && r.y >= 0 && r.prawy() <= w && r.dolny() <= h && r.szerokosc > 0 && r.wysokosc > 0; }
inline bool zawiera(Prostokat a, Prostokat b) { return b.x >= a.x && b.y >= a.y && b.prawy() <= a.prawy() && b.dolny() <= a.dolny(); }

// Tekst przycisku miesci sie w jego obszarze (z ramka/wypelnieniem po 4 px z kazdej strony).
inline bool tekst_miesci(Kontrolka* k) {
    const SkalaUI s = k->skala_dpi();
    return CzcionkaUI::zmierz_tekst(k->tekst(), s) + 8 * s <= k->obszar().szerokosc;
}
// Wszystkie kolumny tabeli mieszcza sie bez przewijania poziomego, naglowki nie sa wezsze niz tekst.
inline void sprawdz_tabele(TabelaGrajka* t, DostawcaKolumnGrajka* d) {
    const SkalaUI s = t->skala_dpi();
    int suma = 0;
    for (size_t k = 0; k < d->liczba_kolumn(); ++k) {
        const int w = t->szerokosc_kolumny_px(k);
        suma += w;
        CHECK(w >= CzcionkaUI::zmierz_tekst(d->naglowek_kolumny(k), s) + 12 * s);
    }
    CHECK(suma + 12 * s <= t->obszar().szerokosc);        // + pionowy pasek przewijania
}

// Uklad przy danym rozmiarze i skali: karty Kolejka, Korektor i Diagnostyka.
inline void sprawdz_uklad(OknoGrajka* o, int W, int H, int skala) {
    o->ustaw_skale_dpi(SkalaUI::procent(uint32_t(skala)));
    o->ustaw_geometrie(0, 0, W, H); o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    const SkalaUI s = o->nazwa_->skala_dpi();
    // naglowek: wysokosc z czcionki w skali 2 (z wydluzeniami dolnymi), tekst w calosci
    CHECK(o->nazwa_->obszar().wysokosc >= CzcionkaUI::pobierz_wysokosc(SkalaUI::procent(s.procent() * 2)));
    o->nazwa_->dopasuj();
    CHECK(!o->nazwa_->obciety() && o->nazwa_->skala_widoczna() == 2);
    // panele rozlaczne, srodek ma sensowna szerokosc
    CHECK(rozlaczne(o->bib_->obszar_ekranu(), o->wiz_->obszar_ekranu()) && rozlaczne(o->wiz_->obszar_ekranu(), o->karty_->obszar_ekranu()));
    CHECK(o->wiz_->obszar().szerokosc >= 200 * s);
    CHECK(rozlaczne(o->okladka_->obszar_ekranu(), o->tytul_->obszar_ekranu()) && wewnatrz(o->komunikat_->obszar_ekranu(), W, H));
    o->tytul_->dopasuj();
    CHECK(CzcionkaUI::zmierz_tekst(o->tytul_->tekst_widoczny(), o->tytul_->skala_tekstu()) <= o->tytul_->obszar().szerokosc);
    // biblioteka: naglowki w calosci, bez przewijania poziomego; przyciski bez obcinania
    sprawdz_tabele(o->bib_, &o->dane_bib_);
    CHECK(tekst_miesci(o->odswiez_) && tekst_miesci(o->dodaj_kol_) && rozlaczne(o->odswiez_->obszar_ekranu(), o->dodaj_kol_->obszar_ekranu()));
    // sterowanie: rozlaczne, w oknie, teksty w calosci (takze najszersze warianty zmiennych tekstow)
    Kontrolka* ster[] = {o->poprz_, o->graj_, o->pauza_, o->stop_, o->nast_, o->wycisz_, o->glosnosc_, o->glosnosc_proc_,
                         o->losowo_, o->powtarzanie_, o->w_tle_, o->wizualizacja_, o->diagnostyka_, o->zakoncz_};
    for (size_t i = 0; i < sizeof(ster) / sizeof(ster[0]); ++i) {
        CHECK(wewnatrz(ster[i]->obszar_ekranu(), W, H));
        for (size_t k = 0; k < i; ++k) CHECK(rozlaczne(ster[i]->obszar_ekranu(), ster[k]->obszar_ekranu()));
    }
    Przycisk* przyciski[] = {o->poprz_, o->graj_, o->pauza_, o->stop_, o->nast_, o->wycisz_, o->powtarzanie_, o->zakoncz_};
    for (Przycisk* b : przyciski) CHECK(tekst_miesci(b));
    PoleWyboru* pola[] = {o->losowo_, o->w_tle_, o->wizualizacja_, o->diagnostyka_};
    for (PoleWyboru* p : pola) CHECK(CzcionkaUI::zmierz_tekst(p->tekst(), s) + 16 * s <= p->obszar().szerokosc);   // pole + tekst
    {   // warianty zmiennych tekstow mieszcza sie w kolumnach Auto
        char b[64]; kopiuj_tekst(b, sizeof(b), o->powtarzanie_->tekst());
        static const char* const W3[] = {"Powtarzanie: wył.", "Powtarzanie: utwór", "Powtarzanie: wszystkie"};
        for (const char* t : W3) { o->powtarzanie_->ustaw_tekst(t); CHECK(tekst_miesci(o->powtarzanie_)); }
        o->powtarzanie_->ustaw_tekst(b);
        kopiuj_tekst(b, sizeof(b), o->wycisz_->tekst());
        o->wycisz_->ustaw_tekst("Przywróć"); CHECK(tekst_miesci(o->wycisz_)); o->wycisz_->ustaw_tekst(b);
        CHECK(CzcionkaUI::zmierz_tekst("wycisz.", s) <= o->glosnosc_proc_->obszar().szerokosc);
    }
    // pasek stanu: jeden wiersz o wysokosci czcionki; komunikat i podsumowanie rozlaczne
    CHECK(o->komunikat_->obszar().wysokosc == CzcionkaUI::pobierz_wysokosc(s) + 8 * s);
    o->komunikat_->przelicz(s);
    if (o->komunikat_->skrot()[0])
        CHECK(8 * s + 8 * s + 8 * s + CzcionkaUI::zmierz_tekst(o->komunikat_->tekst_widoczny(), s) <= o->komunikat_->obszar_skrotu().x);

    // karta Kolejka: przyciski 3 x 2 bez obcinania (w tym "Wyczyść"), tabela bez przewijania poziomego
    o->karty_->ustaw_aktywna_karte(0); o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    CHECK(o->karty_->wysokosc_paska() >= CzcionkaUI::pobierz_wysokosc(s) + 8 * s);
    CHECK(o->czysc_ && tekst_miesci(o->czysc_));
    for (size_t i = 0; i < o->przyciski_kol_->liczba_dzieci(); ++i) CHECK(tekst_miesci(o->przyciski_kol_->dziecko(i)));
    sprawdz_tabele(o->kol_, &o->dane_kol_);

    // karta Korektor: etykiety Hz i dB rozlaczne (w razie potrzeby w dwoch rzedach), opis w calosci pod nimi
    o->karty_->ustaw_aktywna_karte(1); o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    EtykietyPasm* rzedy[] = {o->db_pasm_, o->hz_pasm_};
    for (EtykietyPasm* e : rzedy) {
        CHECK(wewnatrz(e->obszar_ekranu(), W, H));
        for (int b = 0; b < EtykietyPasm::N; ++b) {
            const Prostokat r = e->obszar_etykiety(b);
            CHECK(zawiera(Prostokat(0, 0, e->obszar().szerokosc, e->obszar().wysokosc), r));
            for (int k = 0; k < b; ++k) {
                const Prostokat q = e->obszar_etykiety(k);
                CHECK(rozlaczne(Prostokat(q.x - s, q.y, q.szerokosc + 2 * s, q.wysokosc), r));   // >= 1 px odstepu
            }
        }
    }
    for (int b = 0; b < Tabele::LICZBA_PASM; ++b) {
        const Prostokat p = o->pasma_[b]->obszar_ekranu();
        CHECK(wewnatrz(p, W, H) && p.wysokosc >= 60 * s);
        CHECK(rozlaczne(p, o->db_pasm_->obszar_ekranu()) && rozlaczne(p, o->hz_pasm_->obszar_ekranu()));
        if (b) CHECK(rozlaczne(o->pasma_[b - 1]->obszar_ekranu(), p));
        // etykiety wysrodkowane nad/pod swoim suwakiem (+-2 px)
        const Prostokat h = o->hz_pasm_->obszar_etykiety(b);
        const int srodek_h = o->hz_pasm_->obszar_ekranu().x + h.x + h.szerokosc / 2, srodek_p = p.x + p.szerokosc / 2;
        CHECK(srodek_h - srodek_p <= 2 * s + 1 && srodek_p - srodek_h <= 2 * s + 1);
    }
    CHECK(o->opis_korektora_->obszar_ekranu().y >= o->hz_pasm_->obszar_ekranu().dolny());
    CHECK(wewnatrz(o->opis_korektora_->obszar_ekranu(), W, H));
    CHECK(o->opis_korektora_->zmierz(Rozmiar(o->opis_korektora_->obszar().szerokosc, -1)).wysokosc <= o->opis_korektora_->obszar().wysokosc);
    CHECK(rozlaczne(o->korektor_->obszar_ekranu(), o->preset_->obszar_ekranu()));
    CHECK(CzcionkaUI::zmierz_tekst(o->korektor_->tekst(), s) + 16 * s <= o->korektor_->obszar().szerokosc);

    // karta Diagnostyka: kazda nazwa i wartosc w calosci w swojej kolumnie, bez przewijania poziomego
    o->karty_->ustaw_aktywna_karte(OknoGrajka::KARTA_DIAGNOSTYKI); o->przelicz_uklad();
    o->aktualizuj_diagnostyke(true);
    { KontekstRysowania c; o->rysuj(c, true); }
    sprawdz_tabele(o->diag_tab_, &o->dane_diag_);
    for (size_t i = 0; i < DaneDiagnostyki::N; ++i) {
        CHECK(o->dane_diag_.nazwa(i)[0] && o->dane_diag_.wartosc(i)[0]);
        CHECK(CzcionkaUI::zmierz_tekst(o->dane_diag_.nazwa(i), s) + 12 * s <= o->diag_tab_->szerokosc_kolumny_px(0));
        CHECK(CzcionkaUI::zmierz_tekst(o->dane_diag_.wartosc(i), s) + 12 * s <= o->diag_tab_->szerokosc_kolumny_px(1));
    }
    CHECK(CzcionkaUI::zmierz_tekst("999,9 ms/s", s) + 12 * s <= o->diag_tab_->szerokosc_kolumny_px(1));
    CHECK(o->opis_diagnostyki_->zmierz(Rozmiar(o->opis_diagnostyki_->obszar().szerokosc, -1)).wysokosc <= o->opis_diagnostyki_->obszar().wysokosc);
    o->karty_->ustaw_aktywna_karte(0); o->przelicz_uklad();
}

// Wlaczenie/wylaczenie diagnostyki: zadna kontrolka sterowania nie zmienia polozenia, brak przeliczenia ukladu
// i pelnego odrysowania (wczesniej drugi wiersz paska podnosil sterowanie o ~27 px - przyciski "uciekaly" spod
// kursora natywnie).
template <class Petla>
inline void sprawdz_stabilnosc_diagnostyki(OknoGrajka* o, Petla petla) {
    Kontrolka* ster[] = {o->poprz_, o->graj_, o->pauza_, o->stop_, o->nast_, o->wycisz_, o->glosnosc_, o->losowo_,
                         o->powtarzanie_, o->w_tle_, o->wizualizacja_, o->diagnostyka_, o->zakoncz_, o->komunikat_, o->karty_};
    petla(10);                                         // zalegle odrysowania wczesniejszych krokow
    Prostokat przed[sizeof(ster) / sizeof(ster[0])];
    for (size_t i = 0; i < sizeof(ster) / sizeof(ster[0]); ++i) przed[i] = ster[i]->obszar_ekranu();
    const uint64_t uklad0 = o->liczba_przejsc_ukladu(), pelne0 = o->liczba_pelnych_odrysowan();
    for (int r = 0; r < 3; ++r) {
        o->o_.ustaw_diagnostyke(true);  petla(300);
        CHECK(o->komunikat_->skrot()[0] && std::strstr(o->komunikat_->skrot(), "Niedobór"));
        for (size_t i = 0; i < sizeof(ster) / sizeof(ster[0]); ++i) {
            const Prostokat p = ster[i]->obszar_ekranu();
            CHECK(p.x == przed[i].x && p.y == przed[i].y && p.szerokosc == przed[i].szerokosc && p.wysokosc == przed[i].wysokosc);
        }
        o->o_.ustaw_diagnostyke(false); petla(5);
        CHECK(!o->komunikat_->skrot()[0]);
    }
    CHECK(o->liczba_przejsc_ukladu() == uklad0 && o->liczba_pelnych_odrysowan() == pelne0);
}

// Podpowiedz BursztynUI: szerokosc i wysokosc z metryk czcionki, kazda linia miesci sie w ramce.
inline void sprawdz_podpowiedz(SkalaUI s) {
    Podpowiedz p;
    p.ustaw_skale_dpi(s);
    static const char* const T[] = {"Wskaźniki poziomu i widmo dźwięku",
                                    "Żółta łódź – zażółć gęślą jaźń; bardzo długi opis podpowiedzi z polskimi znakami, który musi się zawinąć",
                                    "Krótka", "Bezspacjowyciągznakówżółtyłabędźktóryniemiesciesiewjednejlinii"};
    for (const char* t : T) {
        p.ustaw_tekst(t);
        const Rozmiar r = p.obliczony_rozmiar();
        const size_t n = p.liczba_linii();
        CHECK(n >= 1);
        CHECK(r.wysokosc >= int(n) * CzcionkaUI::pobierz_wysokosc(s) + 8 * s);
        size_t w_liniach = 0;
        for (size_t i = 0; i < n; ++i) {
            char l[Podpowiedz::MAKS_TEKSTU]; p.linia(i, l, sizeof(l));
            const int w = CzcionkaUI::zmierz_tekst(l, s);
            CHECK(w + 12 * s <= r.szerokosc);                                    // ramka nie obcina linii
            if (std::strchr(l, ' ')) CHECK(w <= p.maksymalna_szerokosc() * s - 12 * s);
            for (const char* c = l; *c; ++c) if (*c != ' ') ++w_liniach;
        }
        size_t bez_spacji = 0; for (const char* c = t; *c; ++c) if (*c != ' ') ++bez_spacji;
        CHECK(w_liniach == bez_spacji);                                          // nic nie zginelo przy zawijaniu
    }
}

} // namespace GrajekUklad
