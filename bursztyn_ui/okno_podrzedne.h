#pragma once
/*
 * BursztynUI: OknoPodrzedne - modeless, natywne okna informacyjne podlegajace wlascicielowi.
 *
 * Wlasciciel (aplikacja) wola OknoPodrzedne::otworz(rodzaj, tytul, tresc): jadro (BWS 101) uruchamia wbudowany program
 * okna informacyjnego jako OSOBNY proces/okno, rejestruje wlasnosc (zamkniecie wlasciciela zamyka dzieci, dziecko jest
 * nad wlascicielem, minimalizuje/przywraca sie z nim) i zapobiega duplikatom (ponowne otwarcie aktywuje istniejace).
 * Tresc (format: tresc_uklad.h) jest ukladana po stronie dziecka z uzyciem AKTYWNEJ czcionki BSF: zawijanie, dwie
 * kolumny wlasciwosci, przewijanie. Dolny pasek akcji (OK / Anuluj) jest osobny od przewijanej tresci.
 * Wyniki: OK = Wynik::OK, Anuluj/Esc/X = Wynik::Anuluj (rozne kody, gotowe dla przyszlych okien edycyjnych).
 */
#include "bursztyn_ui.h"
#include "tresc_uklad.h"

namespace BursztynUI {

namespace OknoPodrzedne {

// Numery zgodne z jadrem (jadro/bws/okna_podrzedne.h).
enum class Rodzaj : uint32_t {
    OProgramieObrazy = 1, InfoObrazu = 2, SterowanieObrazy = 3, SterowanieSnake = 4, ZasadySnake = 5, OGrzeSnake = 6
};
enum class Otwarcie : uint8_t { Odmowa = 0, Utworzono = 1, Aktywowano = 2 };
enum class Wynik : uint8_t { Brak = 0, OK = 1, Anuluj = 2 };

inline Otwarcie otworz(Rodzaj rodzaj, const char* tytul, const char* tresc) {
    if (!tytul || !tresc) return Otwarcie::Odmowa;
    uint32_t dl = 0;
    while (tresc[dl] && dl < 4096U) ++dl;
    return static_cast<Otwarcie>(gui_okno_podrzedne_otworz(static_cast<uint32_t>(rodzaj), tytul, tresc, dl));
}

// Miernik tekstu z aktywna czcionka BSF i skala UI (do ukladu i do obliczania rozmiaru okna).
struct MiernikUI {
    SkalaUI skala;
    int operator()(const char* t, size_t n) const {
        char b[320];
        if (n >= sizeof(b)) n = sizeof(b) - 1;       // fragmenty dluzsze niz bufor: mierzymy prefiks (zawijanie i tak je lamie)
        for (size_t i = 0; i < n; ++i) b[i] = t[i];
        b[n] = '\0';
        return CzcionkaUI::zmierz_tekst(b, skala);
    }
};

} // namespace OknoPodrzedne

class OknoInformacyjne : public Okno {
public:
    static constexpr int MARGINES = 8;          // logiczne px
    static constexpr int PASEK_AKCJI_WYS = 36;
    static constexpr int PASEK_PRZEWIJANIA_SZER = 16;
    static constexpr int ODSTEP_UKLADU = 6;

    // Okresla preferowany rozmiar logiczny okna dla tresci (wysokosc ograniczona do ~80% obszaru roboczego; powyzej
    // tresc jest przewijana, nigdy ucinana ani pomniejszana).
    static void oblicz_rozmiar(const char* tresc, size_t dl, int* lw, int* lh) {
        const SkalaUI s = ui_skala();
        const Prostokat work = pobierz_obszar_roboczy();
        int szer = 520;
        const int maks_szer = work.szerokosc * 100 / s.procent() * 9 / 10;
        if (szer > maks_szer) szer = maks_szer > 360 ? maks_szer : 360;
        const int pole_szer = (szer - 2 * MARGINES - PASEK_PRZEWIJANIA_SZER - 4) * s.procent() / 100;
        OknoPodrzedne::MiernikUI m{s};
        TrescUklad::Uklad* u = new TrescUklad::Uklad();
        int tresc_h = 0;
        if (u) {
            const int wl = CzcionkaUI::pobierz_wysokosc(s);
            TrescUklad::zbuduj(*u, tresc, dl, pole_szer - 2 * MARGINES * s.procent() / 100, wl, wl / 2, m);
            tresc_h = u->wysokosc * 100 / s.procent() + 2 * MARGINES;
            delete u;
        }
        int wys = WYSOKOSC_BELKI_TYTULOWEJ + 3 * MARGINES + 2 * ODSTEP_UKLADU + 1 + PASEK_AKCJI_WYS + tresc_h;
        const int maks_wys = work.wysokosc * 100 / s.procent() * 8 / 10;
        if (wys > maks_wys) wys = maks_wys;
        if (wys < 240) wys = 240;
        *lw = szer;
        *lh = wys;
    }

    OknoInformacyjne(const char* tytul, int lw, int lh) : Okno(tytul, lw, lh) {
        ustaw_minimalny_rozmiar(360, 240);
    }
    ~OknoInformacyjne() override { delete uklad_; }

    OknoPodrzedne::Wynik wynik() const { return wynik_; }
    void przerysuj_tresc() { if (powierzchnia_) powierzchnia_->uniewaznij(); }

    // Pierwsze wywolanie buduje interfejs; kolejne (odswiezenie tresci istniejacego okna) tylko podmieniaja tekst.
    void ustaw_tresc(const char* tresc, size_t dl) {
        if (dl > sizeof(tresc_) - 1) dl = sizeof(tresc_) - 1;
        for (size_t i = 0; i < dl; ++i) tresc_[i] = tresc[i];
        tresc_[dl] = '\0';
        dl_ = dl;
        szer_ukladu_ = -1;
        if (!powierzchnia_) zbuduj_interfejs();
        else {
            if (pasek_) pasek_->ustaw_wartosc(0);
            powierzchnia_->uniewaznij();
            uniewaznij();
        }
    }

    // Kody akcji: OK i Anuluj to rozne wyniki (okno zamyka sie w obu przypadkach).
    void akceptuj() { wynik_ = OknoPodrzedne::Wynik::OK; zamknij(); }
    void anuluj() { wynik_ = OknoPodrzedne::Wynik::Anuluj; zamknij(); }

    // Odswiezenie: jadro wysyla OTWORZ_PLIK, gdy wlasciciel ponownie otworzyl to samo okno z nowa trescia.
    void odswiez_z_jadra() {
        static char bufor[4096 + 64 + 8];
        const uint32_t n = gui_okno_podrzedne_tresc(bufor, sizeof(bufor));
        uint32_t i = 0;
        while (n > 2 && i + 1 < n && !(bufor[i] == '\n' && bufor[i + 1] == '\n')) ++i;
        if (n > 2 && i + 1 < n && i > 0) ustaw_tresc(bufor + i + 2, n - (i + 2));
    }

    void obsluz_zdarzenie(const bws_zdarzenie& ev) override {
        if (ev.typ == BWS_ZDARZENIE_OTWORZ_PLIK) { odswiez_z_jadra(); return; }
        // Sterownik klawiatury raportuje strzalki rowniez jako sekwencje VT (ESC '[' ...) w zdarzeniach znakowych:
        // gdy dziala klawiatura fizyczna, zdarzenia znakowe to tylko echo i NIE moga byc traktowane jako Esc/Enter.
        if (echo_klawiatury(ev)) { Okno::obsluz_zdarzenie(ev); return; }
        if (ev.typ == BWS_ZDARZENIE_KLAWISZ || (ev.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY && ev.x != 0)) {
            const uint32_t k = klawisz_polecenia(ev);
            if (k == 27) { anuluj(); return; }
            if (k == 13) {                                      // Enter: OK (chyba ze fokus jest na przycisku Anuluj)
                if (fokus() == static_cast<Kontrolka*>(anuluj_)) anuluj(); else akceptuj();
                return;
            }
            const int strona = widok_wysokosc_ > 0 ? widok_wysokosc_ : 100;
            switch (k) {
                case 0x48: przewin_o(-wys_linii_); return;
                case 0x50: przewin_o(wys_linii_); return;
                case 0x49: przewin_o(-strona + wys_linii_); return;
                case 0x51: przewin_o(strona - wys_linii_); return;
                case 0x47: przewin_do(0); return;
                case 0x4f: przewin_do(1 << 30); return;
                default: break;
            }
        }
        Okno::obsluz_zdarzenie(ev);
    }

private:
    void zbuduj_interfejs() {
        Panel* korzen = new Panel(Orientacja::Pionowa);
        korzen->ustaw_wypelnienie(Marginesy(MARGINES));
        korzen->ustaw_odstep(ODSTEP_UKLADU);
        ustaw_kontrolke_glowna(korzen);

        Panel& wiersz = korzen->dodaj<Panel>(Orientacja::Pozioma);
        wiersz.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        wiersz.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        wiersz.ustaw_odstep(2);
        wiersz.ustaw_wypelnienie(Marginesy(0));
        wiersz.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));

        powierzchnia_ = new PowierzchniaRysunkowa();
        powierzchnia_->ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
        powierzchnia_->ustaw_mozliwosc_fokusu(false);
        powierzchnia_->przy_rysowaniu(&rysuj_cb, this);
        powierzchnia_->przy_rolce([](PowierzchniaRysunkowa*, const ZdarzenieRolki& e, void* d) {
            OknoInformacyjne* o = static_cast<OknoInformacyjne*>(d);
            o->przewin_o(-e.delta_y * o->wys_linii_ * 3);
        }, this);
        wiersz.dodaj_kontrolke(powierzchnia_);

        pasek_ = new PasekPrzewijania();
        pasek_->ustaw_orientacje(Orientacja::Pionowa);
        pasek_->ustaw_preferowany_rozmiar(Rozmiar(PASEK_PRZEWIJANIA_SZER, -1));
        pasek_->ustaw_wyrownanie_poziome(WyrownaniePoziome::Poczatek);
        pasek_->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        pasek_->ustaw_mozliwosc_fokusu(false);
        pasek_->model().przy_zmianie([](ModelPrzewijania*, int64_t, void* d) {
            OknoInformacyjne* o = static_cast<OknoInformacyjne*>(d);
            if (o->powierzchnia_) o->powierzchnia_->uniewaznij();
        }, this);
        wiersz.dodaj_kontrolke(pasek_);

        Panel& separator = korzen->dodaj<Panel>(Orientacja::Pozioma);   // oddziela tresc od paska akcji (nie przecina tekstu)
        separator.ustaw_preferowany_rozmiar(Rozmiar(-1, 1));
        separator.ustaw_wypelnienie(Marginesy(0));
        separator.ustaw_kolor_tla(motyw_kolor(MOTYW_KOLOR_RAMKA));

        Panel& akcje = korzen->dodaj<Panel>(Orientacja::Pozioma);        // dedykowany dolny pasek akcji
        akcje.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        akcje.ustaw_preferowany_rozmiar(Rozmiar(-1, PASEK_AKCJI_WYS));
        akcje.ustaw_wypelnienie(Marginesy(0, 2, 0, 2));
        akcje.ustaw_odstep(8);
        Etykieta& rozpychacz = akcje.dodaj<Etykieta>("");
        rozpychacz.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        ok_ = &akcje.dodaj<Przycisk>("OK");
        ok_->ustaw_preferowany_rozmiar(Rozmiar(96, 30));
        ok_->przy_kliknieciu([](Przycisk*, void* d) { static_cast<OknoInformacyjne*>(d)->akceptuj(); }, this);
        anuluj_ = &akcje.dodaj<Przycisk>("Anuluj");
        anuluj_->ustaw_preferowany_rozmiar(Rozmiar(96, 30));
        anuluj_->przy_kliknieciu([](Przycisk*, void* d) { static_cast<OknoInformacyjne*>(d)->anuluj(); }, this);
        ustaw_fokus(ok_);
    }

    static void rysuj_cb(PowierzchniaRysunkowa* n, KontekstRysowania& ctx, void* d) {
        static_cast<OknoInformacyjne*>(d)->rysuj_tresc(*n, ctx);
    }

    void rysuj_tresc(PowierzchniaRysunkowa& n, KontekstRysowania& ctx) {
        const SkalaUI s = n.skala_dpi();
        const Prostokat a = n.obszar();
        const int pad = MARGINES * s.procent() / 100;
        const int szer = a.szerokosc - 2 * pad;
        const int wid_h = a.wysokosc - 2 * pad;
        if (szer < 8 || wid_h < 4) return;
        wys_linii_ = CzcionkaUI::pobierz_wysokosc(s);
        widok_wysokosc_ = wid_h;
        if (!uklad_) uklad_ = new TrescUklad::Uklad();
        if (!uklad_) return;                                      // brak pamieci: pusty widok, bez awarii
        if (szer != szer_ukladu_) {
            OknoPodrzedne::MiernikUI m{s};
            TrescUklad::zbuduj(*uklad_, tresc_, dl_, szer, wys_linii_, wys_linii_ / 2, m);
            szer_ukladu_ = szer;
        }
        const int maks = TrescUklad::maks_przewiniecie(uklad_->wysokosc, wid_h);
        if (pasek_) {
            if (pasek_->minimum() != 0 || pasek_->maksimum() != maks) pasek_->ustaw_zakres(0, maks);
            if (pasek_->rozmiar_strony() != wid_h) pasek_->ustaw_rozmiar_strony(wid_h);
            pasek_->ustaw_maly_krok(wys_linii_);
            pasek_->ustaw_duzy_krok(wid_h > wys_linii_ ? wid_h - wys_linii_ : wid_h);
            pasek_->ustaw_widoczna(maks > 0);
        }
        int przesuniecie = pasek_ ? static_cast<int>(pasek_->wartosc()) : 0;
        if (przesuniecie > maks) przesuniecie = maks;
        if (przesuniecie < 0) przesuniecie = 0;
        const uint32_t kolor_tekstu = motyw_kolor(MOTYW_KOLOR_TEKST);
        const uint32_t kolor_klucza = motyw_kolor(MOTYW_KOLOR_TEKST_DRUGORZEDNY);
        const uint32_t kolor_naglowka = motyw_kolor(MOTYW_KOLOR_AKCENT);
        for (size_t i = 0; i < uklad_->liczba; ++i) {
            const TrescUklad::Linia& l = uklad_->linie[i];
            const int y = pad + l.y - przesuniecie;
            if (y + wys_linii_ <= 0 || y >= a.wysokosc) continue;
            char b[320];
            size_t dl = l.dlugosc < sizeof(b) - 1 ? l.dlugosc : sizeof(b) - 1;
            for (size_t k = 0; k < dl; ++k) b[k] = tresc_[l.poczatek + k];
            b[dl] = '\0';
            const uint32_t kolor = l.typ == TrescUklad::Typ::Naglowek ? kolor_naglowka
                                 : l.typ == TrescUklad::Typ::Klucz ? kolor_klucza : kolor_tekstu;
            ctx.rysuj_tekst(Punkt(pad + l.x, y), b, kolor, s);
        }
    }

    void przewin_o(int dy) {
        if (!pasek_) return;
        int64_t v = pasek_->wartosc() + dy;
        if (v < pasek_->minimum()) v = pasek_->minimum();
        if (v > pasek_->maksimum()) v = pasek_->maksimum();
        pasek_->ustaw_wartosc(v);
    }
    void przewin_do(int v) {
        if (!pasek_) return;
        if (v > pasek_->maksimum()) v = static_cast<int>(pasek_->maksimum());
        pasek_->ustaw_wartosc(v);
    }

    char tresc_[4097] = {};
    size_t dl_ = 0;
    TrescUklad::Uklad* uklad_{nullptr};
    int szer_ukladu_{-1};
    int wys_linii_{16};
    int widok_wysokosc_{0};
    PowierzchniaRysunkowa* powierzchnia_{nullptr};
    PasekPrzewijania* pasek_{nullptr};
    Przycisk* ok_{nullptr};
    Przycisk* anuluj_{nullptr};
    OknoPodrzedne::Wynik wynik_{OknoPodrzedne::Wynik::Brak};
};

} // namespace BursztynUI
