#pragma once

#include "pole_tekstowe.h"
#include "pasek_przewijania.h"
#include "selektor_koloru.h"

namespace BursztynUI {

enum class RodzajWlasciwosci : uint8_t { Tekst, Liczba, Logiczna, Wybor, Kolor };

class InspektorWlasciwosci : public Kontrolka {
public:
    static constexpr size_t MAKS_WLASCIWOSCI = 2048;
    static constexpr size_t MAKS_WYBOROW = 32;
    struct Wlasciwosc {
        uint32_t identyfikator{0};
        RodzajWlasciwosci rodzaj{RodzajWlasciwosci::Tekst};
        char nazwa[64]{};
        char kategoria[48]{};
        char tekst[128]{};
        int64_t liczba{0};
        bool logiczna{false};
        uint32_t kolor{0};
        const char* const* wybory{nullptr};
        size_t liczba_wyborow{0};
        int wybrany_wybor{-1};
        bool tylko_do_odczytu{false};
    };
    using FunkcjaZmiany = void (*)(InspektorWlasciwosci*, uint32_t, const Wlasciwosc&, void*);

private:
    class Edytor : public PoleTekstowe {
    public:
        explicit Edytor(InspektorWlasciwosci* w) : PoleTekstowe("Wartość"), wlasciciel_(w) {}
        void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
            uint32_t k = e.kod_klawisza; bool usb = (k & BWS_KEY_SOURCE_USB) != 0;
            uint32_t b = usb ? k & 0xFFU : k & 0x7FU;
            if (wlasciciel_ && (k == 13U || k == 10U || (usb && b == 40U))) {
                wlasciciel_->zatwierdz_edycje(); e.obsluzone = true; return;
            }
            if (wlasciciel_ && (k == 27U || (usb && b == 41U) || (!usb && b == 1U))) {
                wlasciciel_->anuluj_edycje(); e.obsluzone = true; return;
            }
            PoleTekstowe::obsluz_wcisniecie_klawisza(e);
        }
    private: InspektorWlasciwosci* wlasciciel_{nullptr};
    };

public:
    InspektorWlasciwosci() {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = false;
        czy_reaguje_wizualnie_na_wcisniecie_ = false;
        styl_.fokus = styl_.normalny;
        czy_reaguje_wizualnie_na_najechanie_ = false;
        pionowy_ = new PasekPrzewijania();
        pionowy_->ustaw_orientacje(Orientacja::Pionowa);
        pionowy_->przy_przewijaniu([](PasekPrzewijania*, int64_t, void* d) {
            InspektorWlasciwosci* w = static_cast<InspektorWlasciwosci*>(d);
            if (w) { w->anuluj_edycje(); w->uniewaznij_obszar(w->prostokat_danych()); }
        }, this);
        edytor_ = new Edytor(this); edytor_->ustaw_widoczna(false);
        dodaj_kontrolke(pionowy_); dodaj_kontrolke(edytor_);
        preferowany_rozmiar_ = Rozmiar(360, 280);
    }
    ~InspektorWlasciwosci() override { delete[] wlasciwosci_; wlasciwosci_ = nullptr; }

    bool dodaj_tekst(uint32_t id, const char* nazwa, const char* wartosc,
                     const char* kategoria = nullptr, bool tylko = false) {
        Wlasciwosc* w = dodaj_pusta(id, RodzajWlasciwosci::Tekst, nazwa, kategoria, tylko);
        if (!w) return false;
        kopiuj(w->tekst, sizeof(w->tekst), wartosc);
        return true;
    }
    bool dodaj_liczbe(uint32_t id, const char* nazwa, int64_t wartosc,
                      const char* kategoria = nullptr, bool tylko = false) {
        Wlasciwosc* w = dodaj_pusta(id, RodzajWlasciwosci::Liczba, nazwa, kategoria, tylko);
        if (!w) return false;
        w->liczba = wartosc;
        return true;
    }
    bool dodaj_logiczna(uint32_t id, const char* nazwa, bool wartosc,
                        const char* kategoria = nullptr, bool tylko = false) {
        Wlasciwosc* w = dodaj_pusta(id, RodzajWlasciwosci::Logiczna, nazwa, kategoria, tylko);
        if (!w) return false;
        w->logiczna = wartosc;
        return true;
    }
    bool dodaj_wybor(uint32_t id, const char* nazwa, const char* const* wybory,
                     size_t ile, int wybrany, const char* kategoria = nullptr, bool tylko = false) {
        if (ile > MAKS_WYBOROW || (ile && !wybory) || wybrany < -1 || wybrany >= static_cast<int>(ile)) return false;
        Wlasciwosc* w = dodaj_pusta(id, RodzajWlasciwosci::Wybor, nazwa, kategoria, tylko);
        if (!w) return false;
        w->wybory = wybory;
        w->liczba_wyborow = ile;
        w->wybrany_wybor = wybrany;
        return true;
    }
    bool dodaj_kolor(uint32_t id, const char* nazwa, uint32_t kolor,
                     const char* kategoria = nullptr, bool tylko = false) {
        Wlasciwosc* w = dodaj_pusta(id, RodzajWlasciwosci::Kolor, nazwa, kategoria, tylko);
        if (!w) return false;
        w->kolor = kolor;
        return true;
    }
    size_t liczba_wlasciwosci() const { return liczba_; }
    Wlasciwosc* wlasciwosc(size_t i) { return i < liczba_ ? &wlasciwosci_[i] : nullptr; }
    const Wlasciwosc* wlasciwosc(size_t i) const { return i < liczba_ ? &wlasciwosci_[i] : nullptr; }
    Wlasciwosc* znajdz(uint32_t id) { for (size_t i = 0; i < liczba_; ++i) if (wlasciwosci_[i].identyfikator == id) return &wlasciwosci_[i]; return nullptr; }
    int wybrany_indeks() const { return wybrany_; }
    int fokus_indeks() const { return fokus_; }
    bool czy_edytuje() const { return edytowany_ >= 0; }
    PoleTekstowe* aktywny_edytor() { return czy_edytuje() ? edytor_ : nullptr; }
    const PoleTekstowe* aktywny_edytor() const { return czy_edytuje() ? edytor_ : nullptr; }
    size_t liczba_ostatnio_narysowanych() const { return ostatnio_narysowane_; }
    PasekPrzewijania& pasek_pionowy() { return *pionowy_; }
    void przy_zmianie(FunkcjaZmiany cb, void* d = nullptr) { cb_zmiana_ = cb; dane_zmiany_ = d; }

    bool ustaw_wybrany_indeks(int i) {
        if (i < -1 || i >= static_cast<int>(liczba_)) return false;
        if (wybrany_ == i) return true;
        int st = wybrany_;
        wybrany_ = i;
        if (i >= 0) fokus_ = i;
        uniewaznij_wiersz(st); uniewaznij_wiersz(i); return true;
    }
    bool rozpocznij_edycje(int i) {
        if (i < 0 || i >= static_cast<int>(liczba_) || wlasciwosci_[i].tylko_do_odczytu) return false;
        if (edytowany_ >= 0) zatwierdz_edycje();
        Wlasciwosc& w = wlasciwosci_[i];
        if (w.rodzaj == RodzajWlasciwosci::Logiczna) { w.logiczna = !w.logiczna; powiadom(i); return true; }
        if (w.rodzaj == RodzajWlasciwosci::Wybor) {
            if (w.liczba_wyborow) { w.wybrany_wybor = (w.wybrany_wybor + 1) % static_cast<int>(w.liczba_wyborow); powiadom(i); }
            return true;
        }
        edytowany_ = i; char b[128]{};
        if (w.rodzaj == RodzajWlasciwosci::Tekst) kopiuj(b, sizeof(b), w.tekst);
        else if (w.rodzaj == RodzajWlasciwosci::Liczba) formatuj_i64(b, sizeof(b), w.liczba);
        else formatuj_kolor(b, sizeof(b), w.kolor);
        edytor_->ustaw_tekst(b); edytor_->ustaw_widoczna(true); aktualizuj_edytor(); return true;
    }
    bool zatwierdz_edycje() {
        if (edytowany_ < 0 || edytowany_ >= static_cast<int>(liczba_)) return false;
        int i = edytowany_; Wlasciwosc& w = wlasciwosci_[i]; bool ok = false, zmiana = false;
        if (w.rodzaj == RodzajWlasciwosci::Tekst) { zmiana = !rowne(w.tekst, edytor_->tekst()); kopiuj(w.tekst, sizeof(w.tekst), edytor_->tekst()); ok = true; }
        else if (w.rodzaj == RodzajWlasciwosci::Liczba) { int64_t v = 0; ok = parsuj_i64(edytor_->tekst(), &v); if (ok) { zmiana = v != w.liczba; w.liczba = v; } }
        else if (w.rodzaj == RodzajWlasciwosci::Kolor) { uint32_t v = 0; ok = parsuj_kolor(edytor_->tekst(), &v); if (ok) { zmiana = v != w.kolor; w.kolor = v; } }
        if (!ok) return false;
        edytowany_ = -1; edytor_->ustaw_widoczna(false); uniewaznij_wiersz(i);
        if (zmiana) powiadom(i);
        return true;
    }
    void anuluj_edycje() { if (edytowany_ < 0) return; int i = edytowany_; edytowany_ = -1; edytor_->ustaw_widoczna(false); uniewaznij_wiersz(i); }

    Rozmiar oblicz_naturalny_rozmiar() const override { return Rozmiar(360, 280); }
    void rozmiesc(Prostokat r) override { Kontrolka::rozmiesc(r); aktualizuj_geometrie(); aktualizuj_przewijanie(); aktualizuj_edytor(); }
    void rysuj_siebie(KontekstRysowania& ctx) override {
        ostatnio_narysowane_ = 0; rysuj_tlo_i_ramke(ctx, wyznacz_biezacy_styl_stanu());
        SkalaUI s = skala_dpi_; int  hh = wysokosc_naglowka_ * s, rh = wysokosc_wiersza_ * s;
        int vw = viewport_w(); if (vw <= 0) return; int split = (vw * 46) / 100;
        ctx.rysuj_prostokat(Prostokat(1, 1, vw, hh), 0x00391B02);
        ctx.rysuj_tekst_obciety(Punkt(7 * s, 1 + (hh - 16 * s) / 2), "Właściwość", 0x00FFD700, split - 10 * s, s);
        ctx.rysuj_tekst_obciety(Punkt(split + 7 * s, 1 + (hh - 16 * s) / 2), "Wartość", 0x00FFD700, vw - split - 10 * s, s);
        ctx.rysuj_prostokat(Prostokat(split, 1, s, obszar_.wysokosc - 2), Kolory::RamkaDomyslna);
        int sy = pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0;
        int first = sy / rh, count = (obszar_.wysokosc - hh + rh - 1) / rh + 1;
        ctx.dodaj_przyciecie(prostokat_danych());
        for (int n = 0; n < count; ++n) {
            int i = first + n; if (i >= static_cast<int>(liczba_)) break;
            int y = 1 + hh + i * rh - sy; Prostokat rr(1, y, vw, rh);
            if (!ctx.przecina_klip(rr)) continue;
            ++ostatnio_narysowane_;
            if (i == wybrany_) ctx.rysuj_prostokat(rr, Kolory::ZaznaczenieTlo);
            else if (i == najechany_) ctx.rysuj_prostokat(rr, 0x004A2605);
            if (i == fokus_ && czy_ma_fokus_) ctx.rysuj_ramke(rr, Kolory::AkcentFokus, s);
            const Wlasciwosc& w = wlasciwosci_[i];
            int ty = y + (rh - 16 * s) / 2;
            int nazwa_x = 6 * s;
            if (w.kategoria[0]) {
                int kat_w = 74 * s;
                ctx.rysuj_tekst_obciety(Punkt(nazwa_x, ty), w.kategoria,
                                        Kolory::TekstWyciszony, kat_w, s);
                nazwa_x += kat_w + 5 * s;
            }
            ctx.rysuj_tekst_obciety(Punkt(nazwa_x, ty), w.nazwa,
                                    w.tylko_do_odczytu ? Kolory::TekstWyciszony : Kolory::TekstBialy,
                                    split - nazwa_x - 6 * s, s);
            if (i != edytowany_) rysuj_wartosc(ctx, w, Prostokat(split + 1, y, vw - split - 1, rh), s);
            ctx.rysuj_linie_pozioma(1, y + rh - 1, vw, 0x00452A15);
        }
        ctx.zdejmij_przyciecie();
    }
    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override { BUI_POMIAR(mouse_events, 1); int n = indeks_w_punkcie(e.x, e.y); if (n != najechany_) { int st = najechany_; najechany_ = n; uniewaznij_wiersz(st); uniewaznij_wiersz(n); } }
    void obsluz_opuszczenie_myszy() override { Kontrolka::obsluz_opuszczenie_myszy(); if (najechany_ >= 0) { int st = najechany_; najechany_ = -1; uniewaznij_wiersz(st); } }
    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override { if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return; int i = indeks_w_punkcie(e.x, e.y); if (i >= 0) { ustaw_wybrany_indeks(i); if (e.x - obszar_ekranu_.x >= (viewport_w() * 46) / 100) rozpocznij_edycje(i); e.obsluzone = true; } }
    void obsluz_rolke(ZdarzenieRolki& e) override { if (e.delta_y && pionowy_->czy_widoczna()) { pionowy_->przewin_o(e.delta_y > 0 ? -3 * wysokosc_wiersza_ : 3 * wysokosc_wiersza_); e.obsluzone = true; } }
    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_ || liczba_ == 0) return;
        uint32_t k = e.kod_klawisza;
        bool usb = (k & BWS_KEY_SOURCE_USB) != 0;
        bool e0 = (k & BWS_KEY_SOURCE_E0) != 0;
        uint32_t b = usb ? k & 0xFFU : k & 0x7FU;
        if (k == 13U || (usb && b == 40U)) { rozpocznij_edycje(fokus_ < 0 ? 0 : fokus_); e.obsluzone = true; return; }
        if (k == 27U || (usb && b == 41U)) { anuluj_edycje(); e.obsluzone = true; return; }
        int d = 0; if ((usb && b == 82U) || (e0 && b == 0x48U)) d = -1; if ((usb && b == 81U) || (e0 && b == 0x50U)) d = 1;
        if (d) { int n = fokus_ < 0 ? 0 : przytnij(fokus_ + d, 0, static_cast<int>(liczba_) - 1); ustaw_wybrany_indeks(n); przewin_do(n); e.obsluzone = true; return; }
        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }

private:
    Wlasciwosc* dodaj_pusta(uint32_t id, RodzajWlasciwosci r, const char* n, const char* k, bool tylko) {
        if (liczba_ >= MAKS_WLASCIWOSCI || znajdz(id) || !zapewnij(liczba_ + 1)) return nullptr;
        Wlasciwosc& w = wlasciwosci_[liczba_++]; w = Wlasciwosc{}; w.identyfikator = id; w.rodzaj = r; w.tylko_do_odczytu = tylko; kopiuj(w.nazwa, sizeof(w.nazwa), n); kopiuj(w.kategoria, sizeof(w.kategoria), k); aktualizuj_przewijanie(); uniewaznij(); return &w;
    }
    bool zapewnij(size_t n) { if (n <= pojemnosc_) return true; size_t p = pojemnosc_ ? pojemnosc_ * 2 : 32; if (p < n) p = n; if (p > MAKS_WLASCIWOSCI) p = MAKS_WLASCIWOSCI; Wlasciwosc* nw = new Wlasciwosc[p]; if (!nw) return false; for (size_t i = 0; i < liczba_; ++i) nw[i] = wlasciwosci_[i]; delete[] wlasciwosci_; wlasciwosci_ = nw; pojemnosc_ = p; return true; }
    static void kopiuj(char* d, size_t c, const char* s) { if (!d || !c) return; size_t i = 0; if (s) while (s[i] && i + 1 < c) { d[i] = s[i]; ++i; } d[i] = '\0'; }
    static bool rowne(const char* a, const char* b) { size_t i = 0; while (a && b && a[i] && b[i] && a[i] == b[i]) ++i; return a && b && a[i] == b[i]; }
    static void formatuj_i64(char* b, size_t c, int64_t v) { if (!b || c < 2) return; bool neg = v < 0; uint64_t u = neg ? static_cast<uint64_t>(-(v + 1)) + 1 : static_cast<uint64_t>(v); char t[24]; size_t n = 0; do { t[n++] = static_cast<char>('0' + u % 10); u /= 10; } while (u && n < sizeof(t)); size_t p = 0; if (neg && p + 1 < c) b[p++] = '-'; while (n && p + 1 < c) b[p++] = t[--n]; b[p] = '\0'; }
    static bool parsuj_i64(const char* s, int64_t* out) { if (!s || !out || !s[0]) return false; size_t i = 0; bool neg = s[i] == '-'; if (neg && !s[++i]) return false; uint64_t lim = neg ? UINT64_C(9223372036854775808) : UINT64_C(9223372036854775807), v = 0; for (; s[i]; ++i) { if (s[i] < '0' || s[i] > '9') return false; uint32_t d = static_cast<uint32_t>(s[i] - '0'); if (v > (lim - d) / 10U) return false; v = v * 10U + d; } *out = neg ? (v == UINT64_C(9223372036854775808) ? INT64_MIN : -static_cast<int64_t>(v)) : static_cast<int64_t>(v); return true; }
    static void formatuj_kolor(char* b, size_t c, uint32_t v) { if (!b || c < 8) return; static const char h[] = "0123456789ABCDEF"; b[0] = '#'; v &= 0xFFFFFFU; for (int i = 0; i < 6; ++i) b[i + 1] = h[(v >> ((5 - i) * 4)) & 15U]; b[7] = '\0'; }
    static bool parsuj_kolor(const char* t, uint32_t* out) { if (!t || !out) return false; size_t i = t[0] == '#' ? 1 : 0, start = i; uint32_t v = 0; for (; t[i] && i - start < 6; ++i) { int d = t[i] >= '0' && t[i] <= '9' ? t[i] - '0' : (t[i] >= 'a' && t[i] <= 'f' ? t[i] - 'a' + 10 : (t[i] >= 'A' && t[i] <= 'F' ? t[i] - 'A' + 10 : -1)); if (d < 0) return false; v = (v << 4U) | static_cast<uint32_t>(d); } if (i - start != 6 || t[i]) return false; *out = v == 0 ? SelektorKoloru::WIDOCZNA_CZERN : v; return true; }
    int viewport_w() const { int w = obszar_.szerokosc - 2 - (pionowy_ && pionowy_->czy_widoczna() ? pionowy_->szerokosc() : 0); return w > 0 ? w : 0; }
    Prostokat prostokat_danych() const { SkalaUI s = skala_dpi_; return Prostokat(1, 1 + wysokosc_naglowka_ * s, viewport_w(), obszar_.wysokosc - 2 - wysokosc_naglowka_ * s); }
    void aktualizuj_geometrie() { SkalaUI s = skala_dpi_; int  sw = 15 * s; pionowy_->rozmiesc(Prostokat(obszar_.szerokosc - sw - 1, 1, sw, obszar_.wysokosc - 2)); }
    void aktualizuj_przewijanie() { if (!pionowy_) return; SkalaUI s = skala_dpi_; uint64_t h64 = liczba_ * static_cast<uint64_t>(wysokosc_wiersza_ * s); int h = h64 > INT32_MAX ? INT32_MAX : static_cast<int>(h64); int vh = obszar_.wysokosc - 2 - wysokosc_naglowka_ * s; pionowy_->ustaw_widoczna(h > vh); aktualizuj_geometrie(); pionowy_->ustaw_zakres(0, h); pionowy_->ustaw_rozmiar_strony(vh > 0 ? vh : 1); }
    int indeks_w_punkcie(int x, int y) const { SkalaUI s = skala_dpi_; int  ly = y - obszar_ekranu_.y - 1 - wysokosc_naglowka_ * s; if (ly < 0 || x < obszar_ekranu_.x || x >= obszar_ekranu_.x + viewport_w()) return -1; int sy = pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0; int i = (ly + sy) / (wysokosc_wiersza_ * s); return i >= 0 && i < static_cast<int>(liczba_) ? i : -1; }
    void uniewaznij_wiersz(int i) { if (i < 0 || i >= static_cast<int>(liczba_)) return; SkalaUI s = skala_dpi_; int  sy = pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0; Prostokat r(1, 1 + wysokosc_naglowka_ * s + i * wysokosc_wiersza_ * s - sy, viewport_w(), wysokosc_wiersza_ * s); r = r.przeciecie(prostokat_danych()); if (!r.czy_pusty()) uniewaznij_obszar(r); }
    void przewin_do(int i) { SkalaUI s = skala_dpi_; int  rh = wysokosc_wiersza_ * s, y = i * rh, cur = static_cast<int>(pionowy_->wartosc()), vh = prostokat_danych().wysokosc; if (y < cur) pionowy_->ustaw_wartosc(y); else if (y + rh > cur + vh) pionowy_->ustaw_wartosc(y + rh - vh); }
    void aktualizuj_edytor() { if (edytowany_ < 0 || !edytor_) return; SkalaUI s = skala_dpi_; int  split = (viewport_w() * 46) / 100, sy = pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0; int y = 1 + wysokosc_naglowka_ * s + edytowany_ * wysokosc_wiersza_ * s - sy; edytor_->rozmiesc(Prostokat(split + 2 * s, y + s, viewport_w() - split - 3 * s, wysokosc_wiersza_ * s - 2 * s)); edytor_->ustaw_widoczna(y >= prostokat_danych().y && y + wysokosc_wiersza_ * s <= prostokat_danych().dolny()); }
    void powiadom(int i) { uniewaznij_wiersz(i); if (cb_zmiana_) cb_zmiana_(this, wlasciwosci_[i].identyfikator, wlasciwosci_[i], dane_zmiany_); }
    static void rysuj_wartosc(KontekstRysowania& ctx, const Wlasciwosc& w, Prostokat r, SkalaUI s) { char b[128]{}; const char* t = b; if (w.rodzaj == RodzajWlasciwosci::Tekst) t = w.tekst; else if (w.rodzaj == RodzajWlasciwosci::Liczba) formatuj_i64(b, sizeof(b), w.liczba); else if (w.rodzaj == RodzajWlasciwosci::Logiczna) t = w.logiczna ? "Włączony" : "Wyłączony"; else if (w.rodzaj == RodzajWlasciwosci::Wybor) t = w.wybrany_wybor >= 0 && static_cast<size_t>(w.wybrany_wybor) < w.liczba_wyborow ? w.wybory[w.wybrany_wybor] : "Brak"; else { formatuj_kolor(b, sizeof(b), w.kolor); uint32_t c = w.kolor == 0 ? 0x01000000U : w.kolor; ctx.rysuj_prostokat(Prostokat(r.x + 5 * s, r.y + 5 * s, 22 * s, r.wysokosc - 10 * s), c); ctx.rysuj_ramke(Prostokat(r.x + 5 * s, r.y + 5 * s, 22 * s, r.wysokosc - 10 * s), 0x00A08060, s); r.x += 31 * s; r.szerokosc -= 31 * s; } ctx.rysuj_tekst_obciety(Punkt(r.x + 6 * s, r.y + (r.wysokosc - 16 * s) / 2), t, w.tylko_do_odczytu ? Kolory::TekstWyciszony : Kolory::TekstBialy, r.szerokosc - 12 * s, s); }

    Wlasciwosc* wlasciwosci_{nullptr}; size_t liczba_{0}, pojemnosc_{0};
    int wybrany_{-1}, fokus_{-1}, najechany_{-1}, edytowany_{-1};
    int wysokosc_naglowka_{25}, wysokosc_wiersza_{27}; size_t ostatnio_narysowane_{0};
    PasekPrzewijania* pionowy_{nullptr}; Edytor* edytor_{nullptr};
    FunkcjaZmiany cb_zmiana_{nullptr}; void* dane_zmiany_{nullptr};
};

} // namespace BursztynUI
