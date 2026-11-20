#pragma once
/*
 * Bursztynowy Zasobnik - panele i kontroler (Menedzer Okien). Opis: docs/bursztynowy-zasobnik.md.
 *
 * Panele sa PowierzchniaPopup zaufanego WM obslugiwanymi przez ZarzadcaPopupow + PlikiUI::ObslugaPopup - ten sam
 * mechanizm co menu Start, menu przepelnienia i kalendarz paska (bez nowego procesu, bez dymka BWS 56). Naraz
 * otwarty jest co najwyzej jeden panel zasobnika. Dane pochodza wylacznie z BWS 107/109; polecenia ida przez
 * BWS 108 (glosnosc, wyciszenie, powiadomienia) i BWS 87 (motyw - istniejace API Ustawien). Kontrolka pokazuje
 * wartosc z jadra po poleceniu, nigdy wartosc "zyczeniowa": odmowa albo zajety sprzet = komunikat i powrot.
 */
#include "plik_popup_ui.h"
#include "zasobnik.h"
#include "bursztyn_ui/data_systemowa.h"

namespace Zasobnik {
using namespace BursztynUI;

/* Akcje WM wykonywane z paneli (uruchamianie aplikacji przez zwykle uruchom_lub_aktywuj). */
enum class Akcja : uint8_t { Ustawienia, UstawieniaEkranu };

struct Dane {
    BwsZasobnikStanV1 stan{};
    bool ok = false;
    BwsPowiadomieniaListaV1 lista{};
    bool lista_ok = false;
    uint64_t rx_tempo = 0, tx_tempo = 0;   // B/s z dwoch migawek (tylko gdy czas_ns i ten sam interfejs)
    bool tempo_ok = false;
    uint64_t teraz_ms = 0;
    const char* komunikat = "";            // wynik ostatniego polecenia ("" = brak)
    bool motyw_jasny = false;
    bool motyw_znany = false;
};

class Kontroler;

/* ---- baza panelu: tytul + wiersze kontrolek (do 3 w wierszu, szerokosci w procentach) ---- */
class Panel : public PowierzchniaPopup {
public:
    PlikiUI::ObslugaPopup events;

    Panel(Kontroler* k, const char* tytul) : kontroler_(k) {
        tytul_ = new Etykieta(tytul);
        dodaj_kontrolke(tytul_);
        tytul_->ustaw_autodopasowanie(false);
    }
    virtual Id id() const { return Id::Brak; }
    virtual int szerokosc_dip() const { return 300; }
    virtual void zastosuj(const Dane&) {}
    virtual Kontrolka* fokus() { return this; }

    bool otworz(Prostokat kotwica, int ekran_w, int ekran_h, int pasek_y, const Dane& d) {
        events.reset_input();
        events.geometria({0, 0, ekran_w, ekran_h}, {0, 0, ekran_w, pasek_y});
        zaktualizuj_motyw();
        zastosuj(d);
        kotwica_ = kotwica; ekran_w_ = ekran_w; pasek_y_ = pasek_y;
        const Prostokat r = prostokat();
        if (!events.popups.otworz(*this, r, nullptr, fokus())) return false;
        events.popups.odrysuj_popupy(true);
        return true;
    }
    void zamknij() { if (czy_otwarta()) events.popups.zamknij(); }
    bool obsluz(const bws_zdarzenie& e) { return events.zdarzenie(e); }
    void odswiez(const Dane& d) {
        zastosuj(d);
        if (!czy_otwarta() || !events.popups.czy_otwarty()) return;
        const Prostokat r = prostokat();                 // zmiana liczby widocznych wierszy (np. powiadomien): nowa wysokosc
        if (r.wysokosc != obszar().wysokosc || r.y != obszar().y) {
            events.popups.przemiesc_powierzchnie(*this, r);
            uniewaznij();
            events.popups.odrysuj_popupy(true);
        } else events.popups.odrysuj_popupy(false);
    }
    Prostokat prostokat() const {
        const SkalaUI s = ui_skala();
        return polozenie_panelu(kotwica_, ekran_w_, pasek_y_, szerokosc_dip() * s, wysokosc(s), 4 * s, 6 * s);
    }

    void rozmiesc(Prostokat r) override {
        PowierzchniaPopup::rozmiesc(r);
        const SkalaUI s = skala_dpi_;
        const int pad = 12 * s, gap = 6 * s;
        int y = pad;
        const int h_t = CzcionkaUI::pobierz_wysokosc(s) + 6 * s;
        tytul_->rozmiesc({pad, y, r.szerokosc - 2 * pad, h_t});
        y += h_t + gap;
        const int inner = r.szerokosc - 2 * pad;
        for (int i = 0; i < liczba_wierszy_; ++i) {
            const Wiersz& w = wiersze_[i];
            if (!widoczny(w)) continue;
            const int h = wys_wiersza(w, s);
            const int dostepne = inner - (w.n - 1) * gap;
            int x = pad;
            for (int k = 0; k < w.n; ++k) {
                const int szer = k == w.n - 1 ? pad + inner - x : dostepne * w.udzial[k] / 100;
                if (w.k[k]) w.k[k]->rozmiesc({x, y, szer, h});
                x += szer + gap;
            }
            y += h + gap;
        }
    }
    void rysuj_siebie(KontekstRysowania& ctx) override { rysuj_zaokraglone_tlo(ctx); }
    void zaktualizuj_motyw() override {
        PowierzchniaPopup::zaktualizuj_motyw();
        if (tytul_) tytul_->ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));
        po_zmianie_motywu();
    }
    virtual void po_zmianie_motywu() {}

    int wysokosc(SkalaUI s) const {
        const int pad = 12 * s, gap = 6 * s;
        int h = pad + CzcionkaUI::pobierz_wysokosc(s) + 6 * s + gap;
        for (int i = 0; i < liczba_wierszy_; ++i) if (widoczny(wiersze_[i])) h += wys_wiersza(wiersze_[i], s) + gap;
        return h - gap + pad;
    }
    Etykieta* tytul() { return tytul_; }

protected:
    enum class Rodzaj : uint8_t { Tekst, Przycisk };
    struct Wiersz { Kontrolka* k[3] = {}; int udzial[3] = {}; int n = 0; Rodzaj rodzaj = Rodzaj::Tekst; };
    /* Wiersz bez widocznej kontrolki nie zajmuje miejsca (np. puste miejsca na powiadomienia). */
    static bool widoczny(const Wiersz& w) {
        for (int k = 0; k < w.n; ++k) if (w.k[k] && w.k[k]->czy_widoczna()) return true;
        return false;
    }
    void pokaz(Kontrolka* k, bool v) { if (k && k->czy_widoczna() != v) { k->ustaw_widoczna(v); uniewaznij(); } }
    static int wys_wiersza(const Wiersz& w, SkalaUI s) {
        return CzcionkaUI::pobierz_wysokosc(s) + (w.rodzaj == Rodzaj::Przycisk ? 12 : 4) * s;
    }
    void wiersz(Rodzaj r, Kontrolka* a, int ua, Kontrolka* b = nullptr, int ub = 0, Kontrolka* c = nullptr, int uc = 0) {
        if (liczba_wierszy_ >= MAKS_WIERSZY) return;
        Wiersz& w = wiersze_[liczba_wierszy_++];
        Kontrolka* ks[3] = {a, b, c};
        const int us[3] = {ua, ub, uc};
        for (int i = 0; i < 3; ++i)
            if (ks[i]) { if (!ks[i]->rodzic()) dodaj_kontrolke(ks[i]); w.k[w.n] = ks[i]; w.udzial[w.n] = us[i]; ++w.n; }
        w.rodzaj = r;
    }
    Etykieta* etykieta(const char* t = "") {
        auto* e = new Etykieta(t);
        e->ustaw_autodopasowanie(false);
        return e;
    }
    Etykieta* klucz(const char* t) {
        auto* e = etykieta(t);
        e->ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI));
        return e;
    }

    static constexpr int MAKS_WIERSZY = 16;
    Prostokat kotwica_{};
    int ekran_w_ = 0, pasek_y_ = 0;
    Kontroler* kontroler_;
    Etykieta* tytul_{};
    Wiersz wiersze_[MAKS_WIERSZY];
    int liczba_wierszy_ = 0;
};

/* ---- kontroler ---- */
class Kontroler {
public:
    FunkcjaBws bws = bws_domyslne;
    void (*uruchom)(Akcja, void*) = nullptr;       // WM: uruchom_lub_aktywuj
    void (*przed_otwarciem)(void*) = nullptr;      // WM: zamyka menu Start, kalendarz, menu przepelnienia
    void* dane_wm = nullptr;

    Rejestr rejestr;
    Uklad uklad;
    Powierzchnia powierzchnia;
    Dane dane;

    Kontroler() { zarejestruj_podstawowe(rejestr); (void)aktualizuj(rejestr, dane.stan, false); }
    ~Kontroler();

    /* Uklad przy zegarze. Zwraca true, gdy zmienil sie obszar (pasek i przyciski okien do przeliczenia). */
    bool konfiguruj(int ekran_w, int ekran_h, int zegar_x, int pasek_y, int pasek_h, int maks_szer, SkalaUI s) {
        ekran_w_ = ekran_w; ekran_h_ = ekran_h;
        zegar_x_ = zegar_x; pasek_y_ = pasek_y; pasek_h_ = pasek_h; maks_szer_ = maks_szer; skala_ = s;
        return przeloz();
    }
    int poczatek() const { return uklad.liczba ? uklad.obszar.x : zegar_x_; }

    /* Odczyt stanu jadra: co 2 s, co 1 s przy otwartym panelu, natychmiast z wymus. true = przerysowac zasobnik. */
    bool odswiez(uint64_t teraz_ns, bool wymus = false) {
        const uint64_t okres = otwarty() ? 1000000000ULL : 2000000000ULL;
        if (!wymus && ostatni_odczyt_ns_ && teraz_ns >= ostatni_odczyt_ns_ && teraz_ns - ostatni_odczyt_ns_ < okres) return false;
        ostatni_odczyt_ns_ = teraz_ns ? teraz_ns : 1;
        ++odczytow_;
        const BwsZasobnikStanV1 poprzedni = dane.stan;
        const bool poprzedni_ok = dane.ok;
        BwsZasobnikStanV1 s{};
        const uint64_t w = bws(BWS_ZASOBNIK_STAN, reinterpret_cast<uint64_t>(&s), sizeof(s), BWS_ZASOBNIK_V1, 0);
        dane.ok = w == BWS_ZAS_OK && stan_poprawny(s);
        dane.stan = dane.ok ? s : BwsZasobnikStanV1{};
        dane.teraz_ms = teraz_ns / 1000000ULL;
        policz_tempo(poprzedni, poprzedni_ok);
        if (dane.ok && (!dane.lista_ok || dane.lista.generacja != dane.stan.powiadomienia_generacja)) pobierz_liste();
        odswiez_motyw_id();
        bool zmiana = aktualizuj(rejestr, dane.stan, dane.ok);
        if (zmiana) zmiana |= przeloz();
        if (Panel* p = panel_aktywny()) p->odswiez(dane);
        return zmiana;
    }

    Id trafienie(int x, int y) const { return uklad.trafienie(x, y); }
    bool otwarty() const { return panel_aktywny() != nullptr; }
    Id aktywny() const { const Panel* p = panel_aktywny(); return p ? p->id() : Id::Brak; }
    Id najechany() const { return najechany_; }

    /* Najechanie (x < 0 = kursor poza zasobnikiem). true = zmiana wygladu. */
    bool najechanie(int x, int y) {
        const Id id = x < 0 ? Id::Brak : trafienie(x, y);
        if (id == najechany_) return false;
        najechany_ = id;
        return true;
    }

    /* Klik w ikone: otwiera jej panel albo zamyka otwarty. false = klik poza zasobnikiem. */
    bool klik(int x, int y) {
        const Id id = trafienie(x, y);
        if (id == Id::Brak) return false;
        if (aktywny() == id) { zamknij(); return true; }
        otworz(id);
        return true;
    }

    bool otworz(Id id);
    void zamknij();

    /* Zdarzenie przy otwartym panelu. Klik poza panelem zamyka go; klik w ikone tego samego panelu jest pochloniety
     * (drugi klik = zamkniecie), klik gdzie indziej przechodzi dalej (np. inna ikona zasobnika otwiera swoj panel,
     * przycisk Start otwiera menu). TIMER nigdy nie jest pochlaniany (zegar paska). */
    bool obsluz(const bws_zdarzenie& e) {
        Panel* p = panel_aktywny();
        if (!p) return false;
        if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN && !p->obszar().zawiera(e.x, e.y)) {
            const bool ta_sama = trafienie(e.x, e.y) == p->id();
            zamknij();
            return ta_sama;
        }
        const bool wynik = p->obsluz(e);
        if (e.typ == BWS_ZDARZENIE_TIMER) return false;
        if (!p->czy_otwarta()) { po_zamknieciu(p); return true; }   // Escape
        return wynik;
    }
    bool wymaga_odswiezenia_ekranu() const { const Panel* p = panel_aktywny(); return p && p->events.wymaga_odswiezenia(); }

    template <class Publish>
    bool rysuj(bool force, Publish publish) {
        return powierzchnia.rysuj(rejestr, uklad, najechany_, aktywny(), skala_, force, publish);
    }
    void odswiez_motyw();

    // ---- polecenia z paneli ----
    void ustaw_glosnosc(int procent) {
        if (procent < 0) procent = 0;
        if (procent > 100) procent = 100;
        wynik_polecenia(bws(BWS_ZASOBNIK_POLECENIE, BWS_ZAS_POL_GLOSNOSC, uint64_t(procent), 0, 0));
    }
    void przelacz_wyciszenie() {
        const bool teraz = dane.stan.dzwiek.flagi & BWS_DZW_WYCISZONY;
        wynik_polecenia(bws(BWS_ZASOBNIK_POLECENIE, BWS_ZAS_POL_WYCISZENIE, teraz ? 0 : 1, 0, 0));
    }
    void powiadomienie(uint64_t pol, uint64_t id) { wynik_polecenia(bws(BWS_ZASOBNIK_POLECENIE, pol, id, 0, 0)); }
    /* Motyw: istniejace polecenie Ustawien (BWS 87 MOTYW_CMD_USTAW), trwale jak w Ustawieniach. */
    void ustaw_motyw_jasny(bool jasny) {
        const char* id = jasny ? "bursztynjasny" : "bursztynnocny";
        const bool ok = bws(BWS_MOTYW_POLECENIE, MOTYW_CMD_USTAW, reinterpret_cast<uint64_t>(id), 0, 0) == 1;
        dane.komunikat = ok ? "" : "Nie udało się zmienić motywu.";
        odswiez_motyw_id();
        if (Panel* p = panel_aktywny()) p->odswiez(dane);
    }
    void akcja(Akcja a) { zamknij(); if (uruchom) uruchom(a, dane_wm); }

    uint64_t odczytow() const { return odczytow_; }
    Panel* panel(Id id) const;

private:
    Panel* panel_aktywny() const;
    void po_zamknieciu(Panel* p);
    bool przeloz() {
        const Prostokat stary = uklad.obszar;
        const int stara_liczba = uklad.liczba;
        uloz(rejestr, zegar_x_, pasek_y_, pasek_h_, maks_szer_, metryki(skala_), uklad);
        if (najechany_ != Id::Brak && uklad.prostokat(najechany_).czy_pusty()) najechany_ = Id::Brak;
        if (Panel* p = panel_aktywny(); p && uklad.prostokat(p->id()).czy_pusty() && p->id() != Id::SzybkieUstawienia) {
            // element panelu zniknal (np. nosnik odlaczony) albo brak miejsca: panel zamkniety, bez wiszacej kotwicy
            if (rejestr.znajdz(p->id()) == nullptr) zamknij();
        }
        return stary.x != uklad.obszar.x || stary.szerokosc != uklad.obszar.szerokosc || stary.y != uklad.obszar.y ||
               stary.wysokosc != uklad.obszar.wysokosc || stara_liczba != uklad.liczba;
    }
    void wynik_polecenia(uint64_t w) {
        dane.komunikat = w == BWS_ZAS_OK ? "" :
                         w == BWS_ZAS_ZAJETE ? "Urządzenie zajęte – bez zmian." :
                         w == BWS_ZAS_NIEOBSLUGIWANE ? "Funkcja nieobsługiwana." :
                         w == BWS_ZAS_BRAK_UPRAWNIEN ? "Brak uprawnień." : "Polecenie nie powiodło się.";
        (void)odswiez(ostatni_odczyt_ns_, true);    // zawsze wartosc z jadra (synchronizacja suwakow i ikony)
        przerysuj_ = true;
    }
    void pobierz_liste() {
        BwsPowiadomieniaListaV1 l{};
        const uint64_t w = bws(BWS_POWIADOMIENIA, BWS_POW_POL_POBIERZ, reinterpret_cast<uint64_t>(&l), sizeof(l), 0);
        dane.lista_ok = w == BWS_ZAS_OK && l.wersja == uint32_t(BWS_ZASOBNIK_V1) && l.rozmiar == sizeof(l) && l.liczba <= BWS_POW_MAKS;
        dane.lista = dane.lista_ok ? l : BwsPowiadomieniaListaV1{};
        for (uint32_t i = 0; i < dane.lista.liczba; ++i) {               // obrona: zawsze NUL w polach tekstowych
            BwsPowiadomienieV1& p = dane.lista.wpisy[i];
            p.nadawca[sizeof(p.nadawca) - 1] = p.tytul[sizeof(p.tytul) - 1] = p.tresc[sizeof(p.tresc) - 1] = '\0';
        }
    }
    void policz_tempo(const BwsZasobnikStanV1& a, bool a_ok) {
        const BwsZasSiecV1 &p = a.siec, &n = dane.stan.siec;
        dane.tempo_ok = false;
        if (!a_ok || !dane.ok || !(p.flagi & n.flagi & BWS_ZSIEC_INTERFEJS) || p.interfejs.id != n.interfejs.id ||
            !p.czas_ns || n.czas_ns <= p.czas_ns || n.interfejs.rx_bajty < p.interfejs.rx_bajty || n.interfejs.tx_bajty < p.interfejs.tx_bajty)
            return;
        const uint64_t dt = n.czas_ns - p.czas_ns;
        dane.rx_tempo = (n.interfejs.rx_bajty - p.interfejs.rx_bajty) * 1000000000ULL / dt;
        dane.tx_tempo = (n.interfejs.tx_bajty - p.interfejs.tx_bajty) * 1000000000ULL / dt;
        dane.tempo_ok = true;
    }
    void odswiez_motyw_id() {
        const BursztynMotyw* m = gui_pobierz_motyw();
        dane.motyw_znany = m != nullptr;
        dane.motyw_jasny = m && (rowne(m->id, "bursztynjasny") || rowne(m->id, "bursztyn_jasny"));
    }

    int ekran_w_ = 0, ekran_h_ = 0, zegar_x_ = 0, pasek_y_ = 0, pasek_h_ = 0, maks_szer_ = 0;
    SkalaUI skala_{1};
    Id najechany_ = Id::Brak;
    uint64_t ostatni_odczyt_ns_ = 0, odczytow_ = 0;
    bool przerysuj_ = false;
    mutable Panel* panele_[6] = {};
    Panel* aktywny_ = nullptr;
};

/* ---- Głośność ---- */
class PanelGlosnosci : public Panel {
public:
    explicit PanelGlosnosci(Kontroler* k) : Panel(k, "Głośność") {
        urzadzenie_ = etykieta();
        suwak_ = new Suwak();
        suwak_->ustaw_zakres(0, 100);
        suwak_->ustaw_krok(1);
        wartosc_ = etykieta();
        wartosc_->ustaw_wyrownanie_tekstu(WyrownaniePoziome::Koniec);
        wycisz_ = new Przycisk("Wycisz");
        opis_ = klucz("");
        komunikat_ = etykieta();
        wiersz(Rodzaj::Tekst, urzadzenie_, 100);
        wiersz(Rodzaj::Przycisk, suwak_, 78, wartosc_, 22);
        wiersz(Rodzaj::Przycisk, wycisz_, 100);
        wiersz(Rodzaj::Tekst, opis_, 100);
        wiersz(Rodzaj::Tekst, komunikat_, 100);
        suwak_->przy_zmianie_wartosci([](Suwak*, int v, void* p) {
            auto* s = static_cast<PanelGlosnosci*>(p);
            if (!s->blokuj_) s->kontroler_->ustaw_glosnosc(v);    // tylko zmiana uzytkownika, nie synchronizacja z jadra
        }, this);
        wycisz_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<PanelGlosnosci*>(p)->kontroler_->przelacz_wyciszenie(); }, this);
    }
    Id id() const override { return Id::Dzwiek; }
    int szerokosc_dip() const override { return 340; }
    Kontrolka* fokus() override { return suwak_->czy_wlaczona() ? static_cast<Kontrolka*>(suwak_) : this; }

    void zastosuj(const Dane& d) override {
        const BwsZasDzwiekV1& z = d.stan.dzwiek;
        const bool urz = d.ok && (z.flagi & BWS_DZW_URZADZENIE);
        const bool reg = urz && (z.flagi & BWS_DZW_REGULACJA);
        const bool mute = urz && (z.flagi & BWS_DZW_WYCISZANIE);
        const bool wyc = z.flagi & BWS_DZW_WYCISZONY;
        char t[96]; Tekst x(t, sizeof(t));
        if (urz) x << (z.urzadzenie[0] ? z.urzadzenie : "Urządzenie audio");
        else x << "Brak urządzenia audio";
        urzadzenie_->ustaw_tekst(t);
        suwak_->ustaw_wlaczona(reg);
        const int g = reg ? int(z.glosnosc > 100 ? 100 : z.glosnosc) : 0;
        if (g != suwak_->wartosc()) { blokuj_ = true; suwak_->ustaw_wartosc(g); blokuj_ = false; }
        Tekst v(t, sizeof(t));
        if (reg) { v.liczba(uint64_t(g)); v << " %"; } else v << "—";
        wartosc_->ustaw_tekst(t);
        wycisz_->ustaw_wlaczona(mute);
        wycisz_->ustaw_tekst(wyc ? "Włącz dźwięk" : "Wycisz");
        opis_->ustaw_tekst(!urz ? "Regulacja niedostępna" :
                           z.backend == BWS_DZW_BACKEND_WZMACNIACZ ? "Sprzętowo: wzmacniacz kodeka" :
                           z.backend == BWS_DZW_BACKEND_PCM ? "Programowo: od następnego dźwięku" :
                                                              "Regulacja niedostępna");
        komunikat_->ustaw_tekst(d.komunikat);
        komunikat_->ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_OSTRZEZENIE));
        pokaz(komunikat_, d.komunikat && d.komunikat[0]);
    }
    Suwak* suwak() { return suwak_; }
    Przycisk* wycisz() { return wycisz_; }
    Etykieta* urzadzenie() { return urzadzenie_; }
    Etykieta* komunikat() { return komunikat_; }

private:
    Etykieta *urzadzenie_{}, *wartosc_{}, *opis_{}, *komunikat_{};
    Suwak* suwak_{};
    Przycisk* wycisz_{};
    bool blokuj_ = false;
};

/* ---- Sieć ---- */
class PanelSieci : public Panel {
public:
    static constexpr int POLA = 9;
    explicit PanelSieci(Kontroler* k) : Panel(k, "Sieć") {
        static const char* const KLUCZE[POLA] = {"Interfejs", "Sterownik", "Łącze", "Adres IPv4", "Maska podsieci",
                                                 "Brama domyślna", "Serwer DNS", "Odebrano", "Wysłano"};
        for (int i = 0; i < POLA; ++i) {
            klucze_[i] = klucz(KLUCZE[i]);
            wartosci_[i] = etykieta();
            wiersz(Rodzaj::Tekst, klucze_[i], 46, wartosci_[i], 54);
        }
    }
    Id id() const override { return Id::Siec; }
    int szerokosc_dip() const override { return 360; }

    void zastosuj(const Dane& d) override {
        const BwsZasSiecV1& n = d.stan.siec;
        const BwsSiecInfoV1& i = n.interfejs;
        const bool ifc = d.ok && (n.flagi & BWS_ZSIEC_INTERFEJS);
        const bool ip = d.ok && (n.flagi & BWS_ZSIEC_IPV4);
        char t[64];
        auto ustaw = [&](int k, const char* v) { wartosci_[k]->ustaw_tekst(v && v[0] ? v : "Niedostępne"); };
        if (ifc) {
            char nazwa[20]; kopiuj(nazwa, sizeof(nazwa), i.nazwa);
            Tekst x(t, sizeof(t)); x << (nazwa[0] ? nazwa : "Ethernet");
            if (n.liczba_interfejsow > 1) { x << " (1 z "; x.liczba(n.liczba_interfejsow); x << ")"; }
            ustaw(0, t);
        } else ustaw(0, d.ok ? "Brak karty sieciowej" : nullptr);
        ustaw(1, ifc ? nazwa_sterownika_sieci(i.typ) : nullptr);
        ustaw(2, !ifc ? nullptr : !(i.flagi & BWS_SIEC_LACZE_ZNANE) ? "Nieznany" :
                 (i.flagi & BWS_SIEC_LACZE) ? "Połączono" : "Kabel odłączony");
        { Tekst x(t, sizeof(t)); if (ip) { x.ip(n.ip); x << " (DHCP)"; } ustaw(3, t); }
        { Tekst x(t, sizeof(t)); if (ip) x.ip(n.maska); ustaw(4, t); }
        { Tekst x(t, sizeof(t)); if (ip && (n.flagi & BWS_ZSIEC_BRAMA)) x.ip(n.brama); ustaw(5, t); }
        { Tekst x(t, sizeof(t)); if (ip && (n.flagi & BWS_ZSIEC_DNS)) x.ip(n.dns); ustaw(6, t); }
        for (int k = 0; k < 2; ++k) {
            Tekst x(t, sizeof(t));
            if (ifc) {
                x.bajty(k == 0 ? i.rx_bajty : i.tx_bajty);
                if (d.tempo_ok) { x << " · "; x.bajty(k == 0 ? d.rx_tempo : d.tx_tempo); x << "/s"; }
            }
            ustaw(7 + k, t);
        }
    }
    Etykieta* wartosc(int k) { return k >= 0 && k < POLA ? wartosci_[k] : nullptr; }

private:
    Etykieta* klucze_[POLA]{};
    Etykieta* wartosci_[POLA]{};
};

/* ---- Powiadomienia ---- */
class PanelPowiadomien : public Panel {
public:
    static constexpr int WIDOCZNE = 4;
    explicit PanelPowiadomien(Kontroler* k) : Panel(k, "Powiadomienia") {
        brak_ = klucz("Brak powiadomień");
        wiersz(Rodzaj::Tekst, brak_, 100);
        for (int i = 0; i < WIDOCZNE; ++i) {
            naglowki_[i] = klucz("");
            usun_[i] = new Przycisk("×");
            tytuly_[i] = etykieta();
            tresci_[i] = etykieta();
            wiersz(Rodzaj::Przycisk, naglowki_[i], 88, usun_[i], 12);
            wiersz(Rodzaj::Tekst, tytuly_[i], 100);
            wiersz(Rodzaj::Tekst, tresci_[i], 100);
            usun_[i]->ustaw_znacznik(uint64_t(i));
            usun_[i]->przy_kliknieciu([](Przycisk* b, void* p) {
                auto* s = static_cast<PanelPowiadomien*>(p);
                const uint64_t id = s->id_[b->znacznik() < WIDOCZNE ? b->znacznik() : 0];
                if (id) s->kontroler_->powiadomienie(BWS_ZAS_POL_USUN, id);
            }, this);
        }
        starsze_ = klucz("");
        przeczytane_ = new Przycisk("Oznacz jako przeczytane");
        wyczysc_ = new Przycisk("Wyczyść");
        wiersz(Rodzaj::Tekst, starsze_, 100);
        wiersz(Rodzaj::Przycisk, przeczytane_, 64, wyczysc_, 36);
        przeczytane_->przy_kliknieciu([](Przycisk*, void* p) {
            static_cast<PanelPowiadomien*>(p)->kontroler_->powiadomienie(BWS_ZAS_POL_PRZECZYTANE_WSZYSTKIE, 0); }, this);
        wyczysc_->przy_kliknieciu([](Przycisk*, void* p) {
            static_cast<PanelPowiadomien*>(p)->kontroler_->powiadomienie(BWS_ZAS_POL_WYCZYSC, 0); }, this);
    }
    Id id() const override { return Id::Powiadomienia; }
    int szerokosc_dip() const override { return 400; }

    void zastosuj(const Dane& d) override {
        const BwsPowiadomieniaListaV1& l = d.lista;
        const uint32_t n = d.lista_ok ? l.liczba : 0;
        brak_->ustaw_tekst(!d.lista_ok ? "Powiadomienia niedostępne" : "Brak powiadomień");
        pokaz(brak_, !d.lista_ok || !n);
        char t[96];
        for (int i = 0; i < WIDOCZNE; ++i) {
            const bool jest = uint32_t(i) < n;
            const BwsPowiadomienieV1* p = jest ? &l.wpisy[i] : nullptr;
            id_[i] = jest ? p->id : 0;
            usun_[i]->ustaw_wlaczona(jest);
            pokaz(usun_[i], jest); pokaz(naglowki_[i], jest); pokaz(tytuly_[i], jest); pokaz(tresci_[i], jest && p->tresc[0]);
            if (!jest) { naglowki_[i]->ustaw_tekst(""); tytuly_[i]->ustaw_tekst(""); tresci_[i]->ustaw_tekst(""); continue; }
            Tekst x(t, sizeof(t));
            // Nadawca z pola zrodla ustawionego przez jadro - tekst aplikacji nie moze udawac systemu.
            if (p->zrodlo == BWS_POW_ZRODLO_SYSTEM) x << "System";
            else {                                       // nazwa programu skrocona (naglowek miesci sie w kolumnie)
                char n[13]; kopiuj(n, sizeof(n), p->nadawca);
                size_t dl = 0;
                while (dl < sizeof(p->nadawca) && p->nadawca[dl]) ++dl;
                x << "Aplikacja: " << n << (dl >= sizeof(n) ? "…" : "");
            }
            x << " · ";
            czas(x, d.teraz_ms, p->czas_ms);
            naglowki_[i]->ustaw_tekst(t);
            tytuly_[i]->ustaw_tekst(p->tytul);
            const bool nowe = !(p->flagi & BWS_POW_PRZECZYTANE);
            tytuly_[i]->ustaw_kolor_tekstu(motyw_kolor(p->poziom == BWS_POW_BLAD ? MOTYW_KOLOR_BLAD :
                                                       p->poziom == BWS_POW_OSTRZEZENIE ? MOTYW_KOLOR_OSTRZEZENIE :
                                                       nowe ? MOTYW_KOLOR_AKCENT : MOTYW_KOLOR_TEKST));
            tresci_[i]->ustaw_tekst(p->tresc);
        }
        Tekst s(t, sizeof(t));
        if (n > uint32_t(WIDOCZNE)) { s << "Starsze: "; s.liczba(n - WIDOCZNE); }
        starsze_->ustaw_tekst(t);
        pokaz(starsze_, n > uint32_t(WIDOCZNE));
        przeczytane_->ustaw_wlaczona(d.lista_ok && l.nieprzeczytane > 0);
        wyczysc_->ustaw_wlaczona(n > 0);
    }
    /* Ids widocznych wpisow (zamkniecie panelu = obejrzane = przeczytane). */
    uint64_t widoczne_id(int i) const { return i >= 0 && i < WIDOCZNE ? id_[i] : 0; }
    Przycisk* przycisk_usun(int i) { return usun_[i]; }
    Przycisk* przycisk_wyczysc() { return wyczysc_; }
    Przycisk* przycisk_przeczytane() { return przeczytane_; }
    Etykieta* brak() { return brak_; }
    Etykieta* naglowek(int i) { return naglowki_[i]; }
    Etykieta* tytul_wpisu(int i) { return tytuly_[i]; }

    static void czas(Tekst& x, uint64_t teraz_ms, uint64_t czas_ms) {
        if (!teraz_ms || czas_ms > teraz_ms) { x << "teraz"; return; }
        const uint64_t s = (teraz_ms - czas_ms) / 1000;
        if (s < 60) x << "przed chwilą";
        else if (s < 3600) { x.liczba(s / 60); x << " min temu"; }
        else { x.liczba(s / 3600); x << " godz. temu"; }
    }

private:
    Etykieta* brak_{};
    Etykieta *naglowki_[WIDOCZNE]{}, *tytuly_[WIDOCZNE]{}, *tresci_[WIDOCZNE]{};
    Przycisk* usun_[WIDOCZNE]{};
    uint64_t id_[WIDOCZNE]{};
    Etykieta* starsze_{};
    Przycisk *przeczytane_{}, *wyczysc_{};
};

/* ---- Szybkie ustawienia ---- */
class PanelSzybkichUstawien : public Panel {
public:
    explicit PanelSzybkichUstawien(Kontroler* k) : Panel(k, "Szybkie ustawienia") {
        glosnosc_ = klucz("Głośność");
        suwak_ = new Suwak();
        suwak_->ustaw_zakres(0, 100);
        suwak_->ustaw_krok(1);
        wycisz_ = new Przycisk("Wycisz");
        motyw_ = etykieta("Motyw jasny");
        przelacznik_ = new Przelacznik(false);
        jasnosc_ = klucz("Jasność ekranu: niedostępna");
        siec_ = new Przycisk("Sieć");
        powiadomienia_ = new Przycisk("Powiadomienia");
        ekran_ = new Przycisk("Ekran");
        ustawienia_ = new Przycisk("Ustawienia");
        wiersz(Rodzaj::Przycisk, glosnosc_, 26, suwak_, 46, wycisz_, 28);
        wiersz(Rodzaj::Przycisk, motyw_, 70, przelacznik_, 30);
        wiersz(Rodzaj::Tekst, jasnosc_, 100);
        wiersz(Rodzaj::Przycisk, siec_, 50, powiadomienia_, 50);
        wiersz(Rodzaj::Przycisk, ekran_, 50, ustawienia_, 50);
        komunikat_ = etykieta();
        wiersz(Rodzaj::Tekst, komunikat_, 100);
        suwak_->przy_zmianie_wartosci([](Suwak*, int v, void* p) {
            auto* s = static_cast<PanelSzybkichUstawien*>(p);
            if (!s->blokuj_) s->kontroler_->ustaw_glosnosc(v);
        }, this);
        wycisz_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<PanelSzybkichUstawien*>(p)->kontroler_->przelacz_wyciszenie(); }, this);
        przelacznik_->przy_zmianie([](Przelacznik*, bool v, void* p) {
            auto* s = static_cast<PanelSzybkichUstawien*>(p);
            if (!s->blokuj_) s->kontroler_->ustaw_motyw_jasny(v);
        }, this);
        siec_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<PanelSzybkichUstawien*>(p)->kontroler_->otworz(Id::Siec); }, this);
        powiadomienia_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<PanelSzybkichUstawien*>(p)->kontroler_->otworz(Id::Powiadomienia); }, this);
        ekran_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<PanelSzybkichUstawien*>(p)->kontroler_->akcja(Akcja::UstawieniaEkranu); }, this);
        ustawienia_->przy_kliknieciu([](Przycisk*, void* p) { static_cast<PanelSzybkichUstawien*>(p)->kontroler_->akcja(Akcja::Ustawienia); }, this);
    }
    Id id() const override { return Id::SzybkieUstawienia; }
    int szerokosc_dip() const override { return 340; }

    void zastosuj(const Dane& d) override {
        const BwsZasDzwiekV1& z = d.stan.dzwiek;
        const bool urz = d.ok && (z.flagi & BWS_DZW_URZADZENIE);
        const bool reg = urz && (z.flagi & BWS_DZW_REGULACJA);
        suwak_->ustaw_wlaczona(reg);
        const int g = reg ? int(z.glosnosc > 100 ? 100 : z.glosnosc) : 0;
        if (g != suwak_->wartosc()) { blokuj_ = true; suwak_->ustaw_wartosc(g); blokuj_ = false; }
        wycisz_->ustaw_wlaczona(urz && (z.flagi & BWS_DZW_WYCISZANIE));
        wycisz_->ustaw_tekst((z.flagi & BWS_DZW_WYCISZONY) ? "Włącz" : "Wycisz");
        przelacznik_->ustaw_wlaczona(d.motyw_znany);
        blokuj_ = true; przelacznik_->ustaw_wartosc(d.motyw_jasny, false); blokuj_ = false;
        komunikat_->ustaw_tekst(d.komunikat);
        komunikat_->ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_OSTRZEZENIE));
        pokaz(komunikat_, d.komunikat && d.komunikat[0]);
    }
    Suwak* suwak() { return suwak_; }
    Przelacznik* przelacznik() { return przelacznik_; }
    Przycisk* przycisk_siec() { return siec_; }
    Przycisk* przycisk_powiadomienia() { return powiadomienia_; }
    Przycisk* przycisk_ustawienia() { return ustawienia_; }
    Przycisk* przycisk_ekran() { return ekran_; }
    Etykieta* jasnosc() { return jasnosc_; }

private:
    Etykieta *glosnosc_{}, *motyw_{}, *jasnosc_{}, *komunikat_{};
    Suwak* suwak_{};
    Przycisk *wycisz_{}, *siec_{}, *powiadomienia_{}, *ekran_{}, *ustawienia_{};
    Przelacznik* przelacznik_{};
    bool blokuj_ = false;
};

/* ---- Nośniki wymienne (tylko informacja: jadro nie ma sekwencji bezpiecznego odlaczania) ---- */
class PanelNosnikow : public Panel {
public:
    explicit PanelNosnikow(Kontroler* k) : Panel(k, "Nośniki wymienne") {
        for (int i = 0; i < int(BWS_ZAS_MAKS_NOSNIKOW); ++i) { opisy_[i] = etykieta(); wiersz(Rodzaj::Tekst, opisy_[i], 100); }
        uwaga_ = klucz("Bezpieczne usuwanie: niedostępne");
        wiersz(Rodzaj::Tekst, uwaga_, 100);
    }
    Id id() const override { return Id::Nosniki; }
    int szerokosc_dip() const override { return 360; }
    void zastosuj(const Dane& d) override {
        char t[96];
        for (uint32_t i = 0; i < BWS_ZAS_MAKS_NOSNIKOW; ++i) {
            Tekst x(t, sizeof(t));
            if (d.ok && i < d.stan.liczba_nosnikow) {
                const BwsZasNosnikV1& n = d.stan.nosniki[i];
                char m[48]; kopiuj(m, sizeof(m), n.model);
                x << (m[0] ? m : "Nośnik wymienny") << " · ";
                x.bajty(n.sektory * uint64_t(n.rozmiar_sektora ? n.rozmiar_sektora : 512));
                if (n.flagi & BWS_NOS_TYLKO_ODCZYT) x << " · tylko odczyt";
            }
            opisy_[i]->ustaw_tekst(t);
            pokaz(opisy_[i], d.ok && i < d.stan.liczba_nosnikow);
        }
    }
private:
    Etykieta* opisy_[BWS_ZAS_MAKS_NOSNIKOW]{};
    Etykieta* uwaga_{};
};

/* ---- implementacja kontrolera ---- */
inline Kontroler::~Kontroler() {
    for (Panel*& p : panele_) { if (p) { p->zamknij(); delete p; } p = nullptr; }
}

inline Panel* Kontroler::panel(Id id) const {
    const int i = int(id);
    if (i <= 0 || i >= 6) return nullptr;
    if (!panele_[i]) {
        Kontroler* k = const_cast<Kontroler*>(this);
        switch (id) {
            case Id::SzybkieUstawienia: panele_[i] = new PanelSzybkichUstawien(k); break;
            case Id::Powiadomienia: panele_[i] = new PanelPowiadomien(k); break;
            case Id::Dzwiek: panele_[i] = new PanelGlosnosci(k); break;
            case Id::Siec: panele_[i] = new PanelSieci(k); break;
            case Id::Nosniki: panele_[i] = new PanelNosnikow(k); break;
            default: break;
        }
    }
    return panele_[i];
}

inline Panel* Kontroler::panel_aktywny() const { return aktywny_ && aktywny_->czy_otwarta() ? aktywny_ : nullptr; }

/* Otwiera panel elementu (zamyka poprzedni). Kotwica: ikona elementu albo - gdy element jest ukryty z braku miejsca
 * (wejscie z Szybkich ustawien) - prawa krawedz zasobnika. */
inline bool Kontroler::otworz(Id id) {
    Panel* p = panel(id);
    if (!p || !rejestr.znajdz(id)) return false;
    if (aktywny_ && aktywny_ != p) { Panel* stary = aktywny_; aktywny_ = nullptr; stary->zamknij(); po_zamknieciu(stary); }
    if (przed_otwarciem) przed_otwarciem(dane_wm);
    (void)odswiez(ostatni_odczyt_ns_, true);
    Prostokat kotwica = uklad.prostokat(id);
    if (kotwica.czy_pusty()) kotwica = {uklad.obszar.x, pasek_y_, uklad.obszar.szerokosc ? uklad.obszar.szerokosc : 1, pasek_h_};
    if (!p->otworz(kotwica, ekran_w_, ekran_h_, pasek_y_, dane)) return false;
    aktywny_ = p;
    return true;
}

inline void Kontroler::zamknij() {
    Panel* p = aktywny_;
    if (!p) return;
    if (p->czy_otwarta()) p->zamknij();
    po_zamknieciu(p);
}

inline void Kontroler::po_zamknieciu(Panel* p) {
    if (aktywny_ == p) aktywny_ = nullptr;
    dane.komunikat = "";
    if (p && p->id() == Id::Powiadomienia) {             // obejrzane wpisy staja sie przeczytane
        auto* pp = static_cast<PanelPowiadomien*>(p);
        bool zmiana = false;
        for (int i = 0; i < PanelPowiadomien::WIDOCZNE; ++i) {
            const uint64_t id = pp->widoczne_id(i);
            if (!id) continue;
            for (uint32_t k = 0; k < dane.lista.liczba; ++k)
                if (dane.lista.wpisy[k].id == id && !(dane.lista.wpisy[k].flagi & BWS_POW_PRZECZYTANE)) {
                    (void)bws(BWS_ZASOBNIK_POLECENIE, BWS_ZAS_POL_PRZECZYTANE, id, 0, 0);
                    zmiana = true;
                }
        }
        if (zmiana) (void)odswiez(ostatni_odczyt_ns_, true);
    }
}

inline void Kontroler::odswiez_motyw() {
    powierzchnia.uniewaznij();
    for (Panel* p : panele_) if (p) { p->events.popups.zaktualizuj_motyw(); if (p->czy_otwarta()) { p->rozmiesc(p->obszar()); p->uniewaznij(); p->events.popups.odrysuj_popupy(true); } }
    odswiez_motyw_id();
    if (Panel* p = panel_aktywny()) p->odswiez(dane);
}

}  // namespace Zasobnik
