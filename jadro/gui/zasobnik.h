#pragma once
/*
 * Bursztynowy Zasobnik - obszar stanu systemu przy zegarze paska zadan (zaufany Menedzer Okien).
 * Opis: docs/bursztynowy-zasobnik.md.
 *
 * Ten plik: czesc bez wywolan systemowych, wspolna dla WM i testow hosta.
 *  - Rejestr: elementy o stalej tozsamosci (Id), z ikona, stanem wizualnym, dostepnoscia i dymkiem. Rejestracja,
 *    aktualizacja, pokazanie/ukrycie i usuniecie; elementy nie sa wskaznikami - zniknięcie urzadzenia nie zostawia
 *    wiszacych referencji (panel i dymek odwoluja sie po Id).
 *  - Uklad: elementy od prawej, przy zegarze; przy braku miejsca zostaja te o najwyzszym priorytecie (zegar nigdy
 *    nie jest zaslaniany; ukryte moduly sa dostepne z panelu "Szybkie ustawienia").
 *  - Glify: ikony rysowane wektorowo (pokrycie 3x3) w kolorach motywu - bez plikow PNG, ostre w kazdej skali.
 *  - Powierzchnia: bufor pikseli obszaru zasobnika, przerysowywany tylko przy zmianie sygnatury (jak zegar paska).
 *  - Model: stan z BWS 107 -> stan elementow i polskie dymki (tylko prawdziwe dane, "Niedostępne" gdy brak).
 *  - polozenie_panelu: wspolne pozycjonowanie paneli nad paskiem (takze kalendarza paska).
 */
#include "bursztyn_ui/bursztyn_ui.h"
#include "zasobnik_api.h"

namespace Zasobnik {
using namespace BursztynUI;

enum class Id : uint8_t { Brak = 0, SzybkieUstawienia = 1, Powiadomienia = 2, Dzwiek = 3, Siec = 4, Nosniki = 5 };
enum class Ikona : uint8_t { Ustawienia, Dzwonek, Glosnik, Siec, Nosnik };

/* Stan wizualny elementu (bity). */
inline constexpr uint32_t STAN_NIEDOSTEPNY = 1U << 0;      // brak backendu/urzadzenia: ikona przygaszona i przekreslona
inline constexpr uint32_t STAN_WYCISZONY = 1U << 1;
inline constexpr uint32_t STAN_ODLACZONY = 1U << 2;        // urzadzenie jest, lacze nie
inline constexpr uint32_t STAN_NIEPRZECZYTANE = 1U << 3;
inline constexpr uint32_t STAN_OSTRZEZENIE = 1U << 4;      // np. interfejs bez konfiguracji IPv4
inline constexpr uint32_t STAN_POZIOM_NISKI = 1U << 5;     // glosnosc 1..33 %: jedna fala
inline constexpr uint32_t STAN_POZIOM_SREDNI = 1U << 6;    // 34..66 %: dwie fale

inline constexpr int MAKS_ELEMENTOW = 8;
inline constexpr size_t MAKS_DYMKA = 96;

struct Element {
    Id id = Id::Brak;
    Ikona ikona = Ikona::Ustawienia;
    uint8_t priorytet = 0;     // 0 = najwazniejszy (zostaje przy braku miejsca)
    uint8_t kolejnosc = 0;     // kolejnosc od lewej
    bool widoczny = true;
    bool wlaczony = true;      // false: ikona przygaszona, klik nadal otwiera panel z wyjasnieniem
    uint32_t stan = 0;
    char dymek[MAKS_DYMKA] = {};
};

inline void kopiuj(char* cel, size_t n, const char* z) {
    if (!cel || !n) return;
    size_t i = 0;
    for (; z && z[i] && i + 1 < n; ++i) cel[i] = z[i];
    if (z && z[i])                                       // uciete: nie zostawiaj polowy znaku UTF-8
        while (i > 0 && (uint8_t(z[i]) & 0xC0) == 0x80) --i;
    cel[i] = '\0';
}
inline bool rowne(const char* a, const char* b) {
    size_t i = 0;
    for (; a[i] && a[i] == b[i]; ++i) {}
    return a[i] == b[i];
}

class Rejestr {
public:
    /* Nowy element; ta sama tozsamosc dwa razy albo pelny rejestr = false (bez zmian). */
    bool zarejestruj(Id id, Ikona ikona, uint8_t priorytet, uint8_t kolejnosc) {
        if (id == Id::Brak || znajdz(id) || liczba_ >= MAKS_ELEMENTOW) return false;
        Element& e = el_[liczba_++];
        e = Element{};
        e.id = id; e.ikona = ikona; e.priorytet = priorytet; e.kolejnosc = kolejnosc;
        ++generacja_;
        return true;
    }
    bool usun(Id id) {
        for (int i = 0; i < liczba_; ++i)
            if (el_[i].id == id) {
                for (int k = i + 1; k < liczba_; ++k) el_[k - 1] = el_[k];
                el_[--liczba_] = Element{};
                ++generacja_;
                return true;
            }
        return false;
    }
    const Element* znajdz(Id id) const {
        for (int i = 0; i < liczba_; ++i) if (el_[i].id == id) return &el_[i];
        return nullptr;
    }
    /* Zwraca true, gdy zmienil sie wyglad (stan, dostepnosc, widocznosc) - dymek nie wymaga przerysowania. */
    bool ustaw_widocznosc(Id id, bool w) {
        Element* e = edytuj(id);
        if (!e || e->widoczny == w) return false;
        e->widoczny = w; ++generacja_;
        return true;
    }
    bool aktualizuj(Id id, bool wlaczony, uint32_t stan, const char* dymek) {
        Element* e = edytuj(id);
        if (!e) return false;
        if (dymek && !rowne(e->dymek, dymek)) kopiuj(e->dymek, sizeof(e->dymek), dymek);
        if (e->wlaczony == wlaczony && e->stan == stan) return false;
        e->wlaczony = wlaczony; e->stan = stan; ++generacja_;
        return true;
    }
    int liczba() const { return liczba_; }
    const Element& operator[](int i) const { return el_[i]; }
    uint32_t generacja() const { return generacja_; }

private:
    Element* edytuj(Id id) {
        for (int i = 0; i < liczba_; ++i) if (el_[i].id == id) return &el_[i];
        return nullptr;
    }
    Element el_[MAKS_ELEMENTOW];
    int liczba_ = 0;
    uint32_t generacja_ = 1;
};

/* ---- uklad ---- */
struct Metryki {
    int bok = 32;              // szerokosc slotu (cel trafienia)
    int odstep = 2;
    int margines = 4;          // od zegara i od lewej krawedzi obszaru
};
inline Metryki metryki(SkalaUI s) { Metryki m; m.bok = 32 * s; m.odstep = 2 * s; m.margines = 4 * s; return m; }

struct Uklad {
    Prostokat obszar{};                    // caly obszar zasobnika (tlo), konczy sie na zegarze
    int liczba = 0;
    Id sloty[MAKS_ELEMENTOW] = {};
    Prostokat prostokaty[MAKS_ELEMENTOW] = {};
    int ukryte = 0;                        // widoczne elementy bez miejsca
    Prostokat prostokat(Id id) const {
        for (int i = 0; i < liczba; ++i) if (sloty[i] == id) return prostokaty[i];
        return {};
    }
    Id trafienie(int x, int y) const {
        for (int i = 0; i < liczba; ++i) if (prostokaty[i].zawiera(x, y)) return sloty[i];
        return Id::Brak;
    }
};

/* zegar_x = lewa krawedz zegara; maks_szer = najwieksza szerokosc obszaru (reszta paska dla okien). */
inline void uloz(const Rejestr& r, int zegar_x, int pasek_y, int pasek_h, int maks_szer, const Metryki& m, Uklad& u) {
    u = Uklad{};
    int kand[MAKS_ELEMENTOW], n = 0;
    for (int i = 0; i < r.liczba(); ++i) if (r[i].widoczny) kand[n++] = i;
    // priorytet rosnaco (stabilnie) - pierwsze k zostaje
    for (int a = 1; a < n; ++a)
        for (int b = a; b > 0 && r[kand[b]].priorytet < r[kand[b - 1]].priorytet; --b) { int t = kand[b]; kand[b] = kand[b - 1]; kand[b - 1] = t; }
    int k = 0;
    if (maks_szer > 2 * m.margines && m.bok > 0) k = (maks_szer - 2 * m.margines + m.odstep) / (m.bok + m.odstep);
    if (k > n) k = n;
    if (k < 0) k = 0;
    u.ukryte = n - k;
    // wybrane k: kolejnosc wyswietlania od lewej
    int wyb[MAKS_ELEMENTOW];
    for (int i = 0; i < k; ++i) wyb[i] = kand[i];
    for (int a = 1; a < k; ++a)
        for (int b = a; b > 0 && r[wyb[b]].kolejnosc < r[wyb[b - 1]].kolejnosc; --b) { int t = wyb[b]; wyb[b] = wyb[b - 1]; wyb[b - 1] = t; }
    const int szer = k ? 2 * m.margines + k * m.bok + (k - 1) * m.odstep : 0;
    u.obszar = {zegar_x - szer, pasek_y, szer, pasek_h};
    for (int i = 0; i < k; ++i) {
        u.sloty[i] = r[wyb[i]].id;
        u.prostokaty[i] = {u.obszar.x + m.margines + i * (m.bok + m.odstep), pasek_y + 4 * (m.bok / 32 > 0 ? m.bok / 32 : 1),
                           m.bok, pasek_h - 8 * (m.bok / 32 > 0 ? m.bok / 32 : 1)};
    }
    u.liczba = k;
}

/* Wspolne polozenie paneli nad paskiem: prawa krawedz przy prawej krawedzi kotwicy, w granicach ekranu, nad paskiem
 * (kotwica odslonieta); na niskim ekranie panel jest krotszy. */
inline Prostokat polozenie_panelu(Prostokat kotwica, int ekran_w, int pasek_y, int szer, int wys, int margines, int odstep) {
    if (szer > ekran_w - 2 * margines) szer = ekran_w - 2 * margines;
    if (szer < 1) szer = 1;
    if (wys > pasek_y - margines - odstep) wys = pasek_y - margines - odstep;
    if (wys < 1) wys = 1;
    int x = kotwica.prawy() - szer;
    if (x > ekran_w - margines - szer) x = ekran_w - margines - szer;
    if (x < margines) x = margines;
    int y = pasek_y - odstep - wys;
    if (y < margines) y = margines;
    return {x, y, szer, wys};
}

/* ---- glify: pokrycie 3x3 w jednostkach 0..1 kwadratu ikony ---- */
namespace Glif {

/* Liczby calkowite (Ring 3 bez SSE): jednostka = 1/1024 boku ikony. */
inline constexpr int U = 1024;
constexpr int u(int tysieczne) { return tysieczne * U / 1000; }
struct P { int x, y; };
inline int64_t kw(int64_t v) { return v * v; }
/* odleglosc^2 punktu od odcinka */
inline int64_t odcinek2(P p, P a, P b) {
    const int64_t dx = b.x - a.x, dy = b.y - a.y, l = dx * dx + dy * dy;
    int64_t t = l > 0 ? ((int64_t(p.x - a.x) * dx + int64_t(p.y - a.y) * dy) * U) / l : 0;
    t = t < 0 ? 0 : t > U ? U : t;
    return kw(p.x - (a.x + t * dx / U)) + kw(p.y - (a.y + t * dy / U));
}
inline bool linia(P p, P a, P b, int r) { return odcinek2(p, a, b) <= kw(r); }
inline bool kolo(P p, P c, int r) { return kw(p.x - c.x) + kw(p.y - c.y) <= kw(r); }
inline bool prost(P p, int x0, int y0, int x1, int y1) { return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1; }
/* luk pierscienia w prawo od c: promien r, grubosc g, |dy| <= nach * dx / 100 */
inline bool luk(P p, P c, int r, int g, int nach) {
    const int64_t dx = p.x - c.x, dy = p.y - c.y;
    if (dx <= 0) return false;
    const int64_t d2 = dx * dx + dy * dy;
    if (d2 < kw(r - g / 2) || d2 > kw(r + g / 2)) return false;
    return (dy < 0 ? -dy : dy) * 100 <= nach * dx;
}
inline P pt(int x, int y) { return P{u(x), u(y)}; }

/* Ksztalt ikony w punkcie (wspolrzedne w tysiecznych boku przez pt()): 0 = tlo, 1 = rysunek, 2 = akcent. */
inline int ksztalt(Ikona ik, uint32_t stan, P p) {
    const bool przekreslony = stan & STAN_NIEDOSTEPNY;
    if (przekreslony && linia(p, pt(120, 880), pt(880, 120), u(45))) return 1;
    switch (ik) {
        case Ikona::Glosnik: {
            if (prost(p, u(100), u(380), u(300), u(620))) return 1;
            if (p.x >= u(300) && p.x <= u(500)) {                   // stozek
                const int h = u(120) + (p.x - u(300)) * u(200) / u(200);
                if (p.y >= u(500) - h && p.y <= u(500) + h) return 1;
            }
            if (przekreslony) return 0;
            if (stan & STAN_WYCISZONY)
                return (linia(p, pt(620, 380), pt(860, 620), u(50)) || linia(p, pt(620, 620), pt(860, 380), u(50))) ? 2 : 0;
            if (luk(p, pt(500, 500), u(160), u(70), 110)) return 1;
            if (!(stan & STAN_POZIOM_NISKI) && luk(p, pt(500, 500), u(280), u(70), 100)) return 1;
            if (!(stan & (STAN_POZIOM_NISKI | STAN_POZIOM_SREDNI)) && luk(p, pt(500, 500), u(400), u(70), 90)) return 1;
            return 0;
        }
        case Ikona::Siec: {
            const int g = u(45);
            if (prost(p, u(360), u(120), u(640), u(340)) && !prost(p, u(430), u(190), u(570), u(270))) return 1;
            if (prost(p, u(80), u(640), u(360), u(860)) && !prost(p, u(150), u(710), u(290), u(790))) return 1;
            if (prost(p, u(640), u(640), u(920), u(860)) && !prost(p, u(710), u(710), u(850), u(790))) return 1;
            if (linia(p, pt(500, 340), pt(500, 500), g) || linia(p, pt(220, 500), pt(780, 500), g) ||
                linia(p, pt(220, 500), pt(220, 640), g) || linia(p, pt(780, 500), pt(780, 640), g)) return 1;
            if (!przekreslony && (stan & (STAN_ODLACZONY | STAN_OSTRZEZENIE)) && kolo(p, pt(800, 220), u(150))) return 2;
            return 0;
        }
        case Ikona::Dzwonek: {
            if ((stan & STAN_NIEPRZECZYTANE) && kolo(p, pt(800, 200), u(160))) return 2;
            if (kolo(p, pt(500, 420), u(270)) && p.y <= u(420)) return 1;
            if (prost(p, u(230), u(420), u(770), u(700))) return 1;
            if (prost(p, u(140), u(680), u(860), u(770))) return 1;
            if (kolo(p, pt(500, 850), u(80))) return 1;
            if (prost(p, u(460), u(80), u(540), u(160))) return 1;
            return 0;
        }
        case Ikona::Ustawienia: {
            if (kolo(p, pt(360, 320), u(120)) || kolo(p, pt(640, 680), u(120))) return 2;
            if (linia(p, pt(120, 320), pt(880, 320), u(45)) || linia(p, pt(120, 680), pt(880, 680), u(45))) return 1;
            return 0;
        }
        case Ikona::Nosnik: {
            const bool korpus = prost(p, u(280), u(380), u(720), u(900)) && !prost(p, u(350), u(450), u(650), u(830));
            const bool zlacze = prost(p, u(360), u(100), u(640), u(380)) && !prost(p, u(420), u(160), u(580), u(380));
            return (korpus || zlacze || prost(p, u(450), u(200), u(490), u(260)) || prost(p, u(510), u(200), u(550), u(260))) ? 1 : 0;
        }
    }
    return 0;
}

inline uint32_t mieszaj(uint32_t tlo, uint32_t kolor, int a /*0..9*/) {
    auto k = [&](int sh) { return ((int((tlo >> sh) & 255) * (9 - a) + int((kolor >> sh) & 255) * a) / 9) & 255; };
    return 0xFF000000U | (uint32_t(k(16)) << 16) | (uint32_t(k(8)) << 8) | uint32_t(k(0));
}

/* Rysuje ikone w kwadracie (x, y, bok) bufora w x h (wiersze ciagle). Piksele poza buforem sa pomijane. */
inline void rysuj(uint32_t* px, int w, int h, int x, int y, int bok, Ikona ik, uint32_t stan, uint32_t kolor, uint32_t akcent) {
    if (!px || bok <= 0) return;
    for (int j = 0; j < bok; ++j) {
        const int yy = y + j;
        if (yy < 0 || yy >= h) continue;
        for (int i = 0; i < bok; ++i) {
            const int xx = x + i;
            if (xx < 0 || xx >= w) continue;
            int a1 = 0, a2 = 0;
            for (int sy = 0; sy < 3; ++sy)
                for (int sx = 0; sx < 3; ++sx) {
                    const P p{int((int64_t(i) * 6 + sx * 2 + 1) * U / (6 * int64_t(bok))),
                              int((int64_t(j) * 6 + sy * 2 + 1) * U / (6 * int64_t(bok)))};
                    const int k = ksztalt(ik, stan, p);
                    a1 += k == 1; a2 += k == 2;
                }
            uint32_t& d = px[size_t(yy) * size_t(w) + size_t(xx)];
            if (a1) d = mieszaj(d, kolor, a1);
            if (a2) d = mieszaj(d, akcent, a2);
        }
    }
}

}  // namespace Glif

/* ---- powierzchnia ---- */
class Powierzchnia {
public:
    ~Powierzchnia() { delete[] px_; }
    Powierzchnia() = default;
    Powierzchnia(const Powierzchnia&) = delete;
    Powierzchnia& operator=(const Powierzchnia&) = delete;

    void uniewaznij() { sygnatura_ = 0; }
    bool wymaga(uint64_t sygnatura) const { return sygnatura != sygnatura_; }

    /* Rysuje obszar ukladu, gdy zmienila sie sygnatura (stan elementow, najechanie, aktywny panel, motyw, uklad) albo
     * force. publish(Prostokat, const uint32_t*) jak w zegarze paska. Bufor rosnie tylko przy wiekszym obszarze. */
    template <class Publish>
    bool rysuj(const Rejestr& r, const Uklad& u, Id najechany, Id aktywny, SkalaUI s, bool force, Publish publish) {
        if (u.obszar.czy_pusty()) { sygnatura_ = 0; return false; }
        const uint64_t sig = sygnatura(r, u, najechany, aktywny, s);
        if (!force && sig == sygnatura_) return false;
        const size_t n = size_t(u.obszar.szerokosc) * size_t(u.obszar.wysokosc);
        if (u.obszar.szerokosc > 4096 || u.obszar.wysokosc > 1024) return false;
        if (n > pojemnosc_) {
            uint32_t* nowy = new uint32_t[n];
            if (!nowy) return false;
            delete[] px_; px_ = nowy; pojemnosc_ = n;
        }
        const Prostokat lok{0, 0, u.obszar.szerokosc, u.obszar.wysokosc};
        KontekstRysowania ctx(lok);
        ctx.ustaw_cel(px_, lok);
        ctx.rysuj_prostokat(lok, 0xFF000000U | motyw_kolor(MOTYW_KOLOR_PANEL));
        ctx.rysuj_linie_pozioma(0, 0, lok.szerokosc, 0xFF000000U | motyw_kolor(MOTYW_KOLOR_RAMKA));
        for (int i = 0; i < u.liczba; ++i) {
            const Element* e = r.znajdz(u.sloty[i]);
            if (!e) continue;
            Prostokat b = u.prostokaty[i];
            b.x -= u.obszar.x; b.y -= u.obszar.y;
            const bool akt = e->id == aktywny, naj = e->id == najechany;
            if (akt || naj)
                ctx.rysuj_zaokraglony_prostokat(b, 0xFF000000U | motyw_kolor(akt ? MOTYW_KOLOR_PRZYCISK_NAJECHANY : MOTYW_KOLOR_PODNIESIONY), 4 * s);
            if (akt) {
                const int wp = b.szerokosc - 12 * s, hp = 2 * s;
                ctx.rysuj_prostokat({b.x + (b.szerokosc - wp) / 2, b.y + b.wysokosc - hp, wp, hp}, 0xFF000000U | motyw_kolor(MOTYW_KOLOR_AKCENT));
            }
            const int bok = 20 * s;
            const uint32_t kolor = 0xFF000000U | motyw_kolor(e->wlaczony && !(e->stan & STAN_NIEDOSTEPNY) ? MOTYW_KOLOR_TEKST : MOTYW_KOLOR_TEKST_DRUGI);
            const uint32_t akc = 0xFF000000U | motyw_kolor((e->stan & STAN_WYCISZONY) ? MOTYW_KOLOR_TEKST_DRUGI : MOTYW_KOLOR_AKCENT);
            Glif::rysuj(px_, lok.szerokosc, lok.wysokosc, b.x + (b.szerokosc - bok) / 2, b.y + (b.wysokosc - bok) / 2, bok,
                        e->ikona, e->stan, kolor, akc);
        }
        if (!publish(u.obszar, static_cast<const uint32_t*>(px_))) { sygnatura_ = 0; return false; }
        sygnatura_ = sig;
        ++rysowan_;
        return true;
    }
    uint64_t rysowan() const { return rysowan_; }

    static uint64_t sygnatura(const Rejestr& r, const Uklad& u, Id najechany, Id aktywny, SkalaUI s) {
        uint64_t h = 1469598103934665603ULL;
        auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ULL; };
        mix(r.generacja()); mix(uint64_t(najechany)); mix(uint64_t(aktywny)); mix(uint32_t(s));
        mix(uint64_t(uint32_t(u.obszar.x)) << 32 | uint32_t(u.obszar.y)); mix(uint64_t(uint32_t(u.obszar.szerokosc)) << 32 | uint32_t(u.obszar.wysokosc));
        mix(motyw_kolor(MOTYW_KOLOR_PANEL)); mix(motyw_kolor(MOTYW_KOLOR_TEKST)); mix(motyw_kolor(MOTYW_KOLOR_AKCENT));
        for (int i = 0; i < u.liczba; ++i) mix(uint64_t(u.sloty[i]));
        return h | 1;   // nigdy 0 (0 = uniewaznione)
    }

private:
    uint32_t* px_ = nullptr;
    size_t pojemnosc_ = 0;
    uint64_t sygnatura_ = 0, rysowan_ = 0;
};

/* ---- tekst ---- */
class Tekst {
public:
    Tekst(char* b, size_t n) : b_(b), n_(n) { if (n_) b_[0] = '\0'; }
    Tekst& operator<<(const char* s) {
        for (size_t i = 0; s && s[i] && w_ + 1 < n_; ++i) b_[w_++] = s[i];
        if (n_) b_[w_] = '\0';
        return *this;
    }
    Tekst& liczba(uint64_t v) {
        char t[24]; int k = 0;
        do { t[k++] = char('0' + v % 10); v /= 10; } while (v && k < 23);
        while (k && w_ + 1 < n_) b_[w_++] = t[--k];
        if (n_) b_[w_] = '\0';
        return *this;
    }
    Tekst& ip(const uint8_t a[4]) {
        for (int i = 0; i < 4; ++i) { if (i) *this << "."; liczba(a[i]); }
        return *this;
    }
    /* 1 kB = 1000 B (jak Menedzer zadan); jedna cyfra po przecinku. */
    Tekst& bajty(uint64_t v) {
        static const char* const J[] = {" B", " kB", " MB", " GB", " TB"};
        int j = 0; uint64_t calk = v, dz = 0;
        while (calk >= 1000 && j < 4) { dz = (calk % 1000) / 100; calk /= 1000; ++j; }
        liczba(calk);
        if (j) { *this << ","; liczba(dz); }
        return *this << J[j];
    }
    const char* c_str() const { return b_; }
private:
    char* b_; size_t n_; size_t w_ = 0;
};

/* ---- model: BWS 107 -> elementy ---- */
inline bool stan_poprawny(const BwsZasobnikStanV1& s) {
    return s.wersja == uint32_t(BWS_ZASOBNIK_V1) && s.rozmiar == sizeof(BwsZasobnikStanV1) && s.liczba_nosnikow <= BWS_ZAS_MAKS_NOSNIKOW;
}
inline const char* nazwa_sterownika_sieci(uint32_t typ) {
    return typ == BWS_SIEC_E1000 ? "Intel E1000" : typ == BWS_SIEC_VIRTIO ? "VirtIO-Net" : "Niedostępne";
}

/* Rejestracja elementow (kolejnosc od lewej: nosniki, siec, dzwiek, powiadomienia, szybkie ustawienia). Bateria
 * i wskaznik wydajnosci nie sa rejestrowane: jadro nie ma backendu baterii (BWS_ZAS_BATERIA_OBSLUGA), a zbiorcze
 * CPU/RAM nie sa dostepne dla WM bez prawa Menedzera zadan (docs/bursztynowy-zasobnik.md). */
inline void zarejestruj_podstawowe(Rejestr& r) {
    r.zarejestruj(Id::SzybkieUstawienia, Ikona::Ustawienia, 0, 4);
    r.zarejestruj(Id::Powiadomienia, Ikona::Dzwonek, 1, 3);
    r.zarejestruj(Id::Dzwiek, Ikona::Glosnik, 2, 2);
    r.zarejestruj(Id::Siec, Ikona::Siec, 3, 1);
}

/* Aktualizacja elementow ze stanu jadra (ok = false: brak/niepoprawna odpowiedz - wszystko "niedostepne"). Zwraca
 * true, gdy zmienil sie wyglad (potrzebne przerysowanie zasobnika). */
inline bool aktualizuj(Rejestr& r, const BwsZasobnikStanV1& s, bool ok) {
    bool zmiana = false;
    char t[MAKS_DYMKA];
    // dzwiek
    {
        uint32_t st = 0; Tekst x(t, sizeof(t));
        const BwsZasDzwiekV1& d = s.dzwiek;
        if (!ok || !(d.flagi & BWS_DZW_URZADZENIE)) { st = STAN_NIEDOSTEPNY; x << "Dźwięk: brak urządzenia audio"; }
        else if (!(d.flagi & BWS_DZW_REGULACJA)) { st = STAN_NIEDOSTEPNY; x << "Dźwięk: regulacja głośności niedostępna"; }
        else if (d.flagi & BWS_DZW_WYCISZONY) { st = STAN_WYCISZONY; x << "Głośność: wyciszono"; }
        else {
            const uint32_t g = d.glosnosc > 100 ? 100 : d.glosnosc;
            st = g == 0 ? STAN_WYCISZONY : g <= 33 ? STAN_POZIOM_NISKI : g <= 66 ? STAN_POZIOM_SREDNI : 0;
            x << "Głośność: "; x.liczba(g); x << " %";
        }
        zmiana |= r.aktualizuj(Id::Dzwiek, ok && (d.flagi & BWS_DZW_URZADZENIE), st, t);
    }
    // siec
    {
        uint32_t st = 0; Tekst x(t, sizeof(t));
        const BwsZasSiecV1& n = s.siec;
        const BwsSiecInfoV1& i = n.interfejs;
        if (!ok || !(n.flagi & BWS_ZSIEC_INTERFEJS)) { st = STAN_NIEDOSTEPNY; x << "Sieć: brak karty sieciowej"; }
        else if ((i.flagi & BWS_SIEC_LACZE_ZNANE) && !(i.flagi & BWS_SIEC_LACZE)) { st = STAN_ODLACZONY; x << "Sieć: kabel odłączony"; }
        else if (!(n.flagi & BWS_ZSIEC_IPV4)) { st = STAN_OSTRZEZENIE; x << "Sieć: brak konfiguracji IPv4"; }
        else { x << "Sieć: Ethernet · "; x.ip(n.ip); }
        zmiana |= r.aktualizuj(Id::Siec, ok && (n.flagi & BWS_ZSIEC_INTERFEJS), st, t);
    }
    // powiadomienia
    {
        Tekst x(t, sizeof(t));
        const uint32_t nie = ok ? s.powiadomienia_nieprzeczytane : 0;
        if (!ok) x << "Powiadomienia: niedostępne";
        else if (nie) { x << "Powiadomienia: "; x.liczba(nie); x << (nie == 1 ? " nieprzeczytane" : " nieprzeczytanych"); }
        else if (s.powiadomienia_liczba) x << "Powiadomienia: brak nowych";
        else x << "Brak powiadomień";
        zmiana |= r.aktualizuj(Id::Powiadomienia, ok, nie ? STAN_NIEPRZECZYTANE : 0, t);
    }
    zmiana |= r.aktualizuj(Id::SzybkieUstawienia, true, 0, "Szybkie ustawienia");
    // nosniki wymienne: element tylko, gdy jadro raportuje wymienny nosnik
    const bool nosniki = ok && s.liczba_nosnikow > 0;
    if (nosniki && !r.znajdz(Id::Nosniki)) { r.zarejestruj(Id::Nosniki, Ikona::Nosnik, 4, 0); zmiana = true; }
    if (!nosniki && r.znajdz(Id::Nosniki)) { r.usun(Id::Nosniki); zmiana = true; }
    if (nosniki) {
        Tekst x(t, sizeof(t));
        x << "Nośniki wymienne: "; x.liczba(s.liczba_nosnikow);
        zmiana |= r.aktualizuj(Id::Nosniki, true, 0, t);
    }
    return zmiana;
}

}  // namespace Zasobnik
