#pragma once

#include "kontrolka.h"
#include "pasek_przewijania.h"
#include "czcionka.h"

namespace BursztynUI {

class WidokMiniatur : public Kontrolka {
public:
    static constexpr size_t MAKS_ELEMENTOW = 16384;
    struct Element {
        uint64_t identyfikator{0};
        char* etykieta{nullptr};
        const uint32_t* piksele{nullptr};
        int szerokosc_obrazu{0};
        int wysokosc_obrazu{0};
    };
    using FunkcjaWidoku = void (*)(WidokMiniatur*, int, uint64_t, void*);
    using FunkcjaRenderera = void (*)(WidokMiniatur*, KontekstRysowania&, const Element&,
                                      Prostokat, void*);

    WidokMiniatur() {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = false;
        czy_reaguje_wizualnie_na_wcisniecie_ = false;
        styl_.fokus = styl_.normalny;
        czy_reaguje_wizualnie_na_najechanie_ = false;
        pionowy_ = new PasekPrzewijania();
        pionowy_->ustaw_orientacje(Orientacja::Pionowa);
        pionowy_->pokaz_strzalki(true);
        pionowy_->przy_przewijaniu([](PasekPrzewijania*, int64_t, void* d) {
            WidokMiniatur* w = static_cast<WidokMiniatur*>(d);
            if (w) { w->przygotuj_widoczne(false); w->uniewaznij_obszar(w->prostokat_widoku()); }
        }, this);
        dodaj_kontrolke(pionowy_);
        preferowany_rozmiar_ = Rozmiar(400, 260);
    }
    ~WidokMiniatur() override {
        if (cache_pixels_) gui_free(cache_pixels_);
        cache_pixels_ = nullptr;
        wyczysc();
        delete[] elementy_; elementy_ = nullptr;
    }

    bool dodaj_element(uint64_t id, const char* etykieta, const uint32_t* piksele,
                       int szerokosc, int wysokosc) {
        if (liczba_ >= MAKS_ELEMENTOW || szerokosc < 0 || wysokosc < 0 ||
            (piksele && (szerokosc <= 0 || wysokosc <= 0))) return false;
        if (szerokosc > 8192 || wysokosc > 8192) return false;
        if (szerokosc > 0 && static_cast<uint64_t>(szerokosc) * static_cast<uint64_t>(wysokosc) > UINT64_C(67108864)) return false;
        if (!zapewnij_pojemnosc(liczba_ + 1)) return false;
        char* kopia = kopiuj(etykieta ? etykieta : "");
        if (!kopia) return false;
        Element& e = elementy_[liczba_++];
        e.identyfikator = id; e.etykieta = kopia; e.piksele = piksele;
        e.szerokosc_obrazu = szerokosc; e.wysokosc_obrazu = wysokosc;
        aktualizuj_przewijanie(); uniewaznij(); return true;
    }
    void wyczysc() {
        for (size_t i = 0; i < liczba_; ++i) { delete[] elementy_[i].etykieta; elementy_[i] = Element{}; }
        for (auto& c : cache_) c = WpisCache{};
        liczba_ = 0; wybrany_ = fokus_ = najechany_ = -1;
        aktualizuj_przewijanie(); uniewaznij();
    }
    size_t liczba_elementow() const { return liczba_; }
    const Element* element(size_t i) const { return i < liczba_ ? &elementy_[i] : nullptr; }
    int wybrany_indeks() const { return wybrany_; }
    int fokus_indeks() const { return fokus_; }
    int najechany_indeks() const { return najechany_; }
    size_t liczba_ostatnio_narysowanych_elementow() const { return ostatnio_narysowane_; }
    PasekPrzewijania& pasek_pionowy() { return *pionowy_; }
    Prostokat prostokat_widoku() const { return Prostokat(1, 1, viewport_w(), viewport_h()); }

    bool ustaw_wybrany_indeks(int i, bool powiadom = true) {
        if (i < -1 || i >= static_cast<int>(liczba_)) return false;
        if (wybrany_ == i) return true;
        int stary = wybrany_, stary_fokus = fokus_; wybrany_ = i;
        BUI_POMIAR(identity_changes, 1);
        if (i >= 0) { fokus_ = i; przewin_do(i); }
        uniewaznij_element(stary); uniewaznij_element(i);
        if (stary_fokus != stary && stary_fokus != i) uniewaznij_element(stary_fokus);
        if (powiadom && cb_wybor_) cb_wybor_(this, i, i >= 0 ? elementy_[i].identyfikator : 0, dane_wyboru_);
        return true;
    }
    bool ustaw_fokus_indeks(int i) {
        if (i < -1 || i >= static_cast<int>(liczba_)) return false;
        if (fokus_ == i) return true;
        int stary = fokus_; fokus_ = i; if (i >= 0) przewin_do(i);
        uniewaznij_element(stary); uniewaznij_element(i); return true;
    }
    void ustaw_rozmiar_elementu(int w, int h) {
        w = przytnij(w, 48, 512); h = przytnij(h, 56, 512);
        if (w == szerokosc_elementu_ && h == wysokosc_elementu_) return;
        szerokosc_elementu_ = w; wysokosc_elementu_ = h;
        aktualizuj_przewijanie(); uniewaznij();
    }
    void przy_zmianie_zaznaczenia(FunkcjaWidoku cb, void* d = nullptr) { cb_wybor_ = cb; dane_wyboru_ = d; }
    void przy_aktywacji(FunkcjaWidoku cb, void* d = nullptr) { cb_aktywacja_ = cb; dane_aktywacji_ = d; }
    void ustaw_renderer(FunkcjaRenderera cb, void* d = nullptr) { renderer_ = cb; dane_renderera_ = d; uniewaznij(); }

    Rozmiar oblicz_naturalny_rozmiar() const override { return Rozmiar(400, 260); }
    void rozmiesc(Prostokat r) override {
        Kontrolka::rozmiesc(r); aktualizuj_geometrie(); aktualizuj_przewijanie();
    }
    void ustaw_skale_dpi(int s) override {
        Kontrolka::ustaw_skale_dpi(s);
        aktualizuj_geometrie();
        aktualizuj_przewijanie();
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        ostatnio_narysowane_ = 0;
        rysuj_tlo_i_ramke(ctx, wyznacz_biezacy_styl_stanu());
        int vw = viewport_w(), vh = viewport_h(), cols = liczba_kolumn();
        if (vw <= 0 || vh <= 0 || cols <= 0 || liczba_ == 0) return;
        int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
        int cw = szerokosc_elementu_ * s, ch = wysokosc_elementu_ * s;
        if (cw <= 0 || ch <= 0) return;
        int sy = pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0;
        if (sy < 0) sy = 0;
        int pierwszy_wiersz = sy / ch;
        int ostatni_wiersz = (sy + vh - 1) / ch;
        size_t start = static_cast<size_t>(pierwszy_wiersz) * static_cast<size_t>(cols);
        size_t koniec = static_cast<size_t>(ostatni_wiersz + 1) * static_cast<size_t>(cols);
        if (start > liczba_) start = liczba_;
        if (koniec > liczba_) koniec = liczba_;
        ctx.dodaj_przyciecie(Prostokat(1, 1, vw, vh));
        for (size_t i = start; i < koniec; ++i) {

            int row = static_cast<int>(i / static_cast<size_t>(cols));
            int col = static_cast<int>(i % static_cast<size_t>(cols));
            Prostokat kom(1 + col * cw + 2 * s, 1 + row * ch - sy + 2 * s,
                          cw - 4 * s, ch - 4 * s);
            if (!ctx.przecina_klip(kom)) continue;
            ++ostatnio_narysowane_; BUI_POMIAR(thumbnail_paints, 1);
            if (static_cast<int>(i) == wybrany_) ctx.rysuj_zaokraglony_prostokat(kom, Kolory::ZaznaczenieTlo, 4 * s);
            else if (static_cast<int>(i) == najechany_) ctx.rysuj_zaokraglony_prostokat(kom, 0x004A2605, 4 * s);
            if (static_cast<int>(i) == fokus_ && czy_ma_fokus_) ctx.rysuj_zaokraglona_ramke(kom, Kolory::AkcentFokus, 4 * s, s);
            int tekst_h = CzcionkaUI::pobierz_wysokosc(s);
            Prostokat img(kom.x + 5 * s, kom.y + 5 * s, kom.szerokosc - 10 * s,
                          kom.wysokosc - tekst_h - 14 * s);
            if (renderer_) renderer_(this, ctx, elementy_[i], img, dane_renderera_);
            else rysuj_obraz(ctx, obraz_do_rysowania(elementy_[i]), img);
            ctx.rysuj_tekst_obciety(Punkt(kom.x + 4 * s, kom.dolny() - tekst_h - 3 * s),
                                    elementy_[i].etykieta,
                                    static_cast<int>(i) == wybrany_ ? Kolory::ZaznaczenieTekst : Kolory::TekstBialy,
                                    kom.szerokosc - 8 * s, s);
        }
        ctx.zdejmij_przyciecie();
    }

    void obsluz_uzyskanie_skupienia() override { czy_ma_fokus_ = true; uniewaznij_element(fokus_); }
    void obsluz_utrate_skupienia() override { czy_ma_fokus_ = false; uniewaznij_element(fokus_); }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override { BUI_POMIAR(mouse_events, 1);
        int n = indeks_w_punkcie(e.x, e.y);
        if (n != najechany_) { BUI_POMIAR(identity_changes, 1); int st = najechany_; najechany_ = n; uniewaznij_element(st); uniewaznij_element(n); }
    }
    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        if (najechany_ >= 0) { int st = najechany_; najechany_ = -1; uniewaznij_element(st); }
    }
    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        int i = indeks_w_punkcie(e.x, e.y); if (i >= 0) { ustaw_wybrany_indeks(i); e.obsluzone = true; }
    }
    void obsluz_podwojne_klikniecie(ZdarzenieMyszki& e) override {
        int i = indeks_w_punkcie(e.x, e.y); if (i < 0) i = wybrany_;
        if (i >= 0 && cb_aktywacja_) cb_aktywacja_(this, i, elementy_[i].identyfikator, dane_aktywacji_);
        e.obsluzone = i >= 0;
    }
    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!pionowy_ || !pionowy_->czy_widoczna() || e.delta_y == 0) return;
        int scale = skala_dpi_ < 1 ? 1 : skala_dpi_;
        int ch = wysokosc_elementu_ * scale;
        int64_t krok = -static_cast<int64_t>(e.delta_y) * ch;
        pionowy_->przewin_o(krok);
        e.obsluzone = true;
    }
    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_ || liczba_ == 0) return;
        uint32_t k = e.kod_klawisza; bool usb = (k & BWS_KEY_SOURCE_USB) != 0;
        bool e0 = (k & BWS_KEY_SOURCE_E0) != 0; uint32_t b = usb ? k & 0xFFU : k & 0x7FU;
        int delta = 0, cols = liczba_kolumn();
        if ((usb && b == 79U) || (e0 && b == 0x4DU)) delta = 1;
        if ((usb && b == 80U) || (e0 && b == 0x4BU)) delta = -1;
        if ((usb && b == 81U) || (e0 && b == 0x50U)) delta = cols;
        if ((usb && b == 82U) || (e0 && b == 0x48U)) delta = -cols;
        if (delta) {
            int n = fokus_ < 0 ? 0 : przytnij(fokus_ + delta, 0, static_cast<int>(liczba_) - 1);
            ustaw_fokus_indeks(n); e.obsluzone = true; return;
        }
        if ((usb && b == 74U) || (e0 && b == 0x47U)) { // Home
            ustaw_fokus_indeks(0); e.obsluzone = true; return;
        }
        if ((usb && b == 77U) || (e0 && b == 0x4FU)) { // End
            ustaw_fokus_indeks(static_cast<int>(liczba_ - 1)); e.obsluzone = true; return;
        }
        if ((usb && b == 78U) || (e0 && b == 0x51U)) { // Page Down
            int ch = wysokosc_elementu_ * (skala_dpi_ < 1 ? 1 : skala_dpi_);
            int rpp = ch > 0 ? (viewport_h() / ch) : 1;
            if (rpp < 1) rpp = 1;
            int cur = fokus_ < 0 ? 0 : fokus_;
            int n = przytnij(cur + rpp * cols, 0, static_cast<int>(liczba_) - 1);
            ustaw_fokus_indeks(n); e.obsluzone = true; return;
        }
        if ((usb && b == 75U) || (e0 && b == 0x49U)) { // Page Up
            int ch = wysokosc_elementu_ * (skala_dpi_ < 1 ? 1 : skala_dpi_);
            int rpp = ch > 0 ? (viewport_h() / ch) : 1;
            if (rpp < 1) rpp = 1;
            int cur = fokus_ < 0 ? 0 : fokus_;
            int n = przytnij(cur - rpp * cols, 0, static_cast<int>(liczba_) - 1);
            ustaw_fokus_indeks(n); e.obsluzone = true; return;
        }
        if (k == 13U || k == 32U || (usb && (b == 40U || b == 44U))) {
            if (fokus_ >= 0) ustaw_wybrany_indeks(fokus_);
            e.obsluzone = true;
            return;
        }
        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }

private:
    static char* kopiuj(const char* s) {
        size_t n = 0; while (s && s[n] && n < 255) ++n;
        char* p = new char[n + 1]; if (!p) return nullptr;
        for (size_t i = 0; i < n; ++i) p[i] = s[i];
        p[n] = '\0';
        return p;
    }
    bool zapewnij_pojemnosc(size_t n) {
        if (n <= pojemnosc_) return true;
        size_t nowa = pojemnosc_ ? pojemnosc_ * 2 : 32;
        if (nowa < n) nowa = n;
        if (nowa > MAKS_ELEMENTOW) nowa = MAKS_ELEMENTOW;
        Element* p = new Element[nowa]; if (!p) return false;
        for (size_t i = 0; i < liczba_; ++i) p[i] = elementy_[i];
        delete[] elementy_; elementy_ = p; pojemnosc_ = nowa; return true;
    }
    int viewport_w() const {
        int scale = skala_dpi_ < 1 ? 1 : skala_dpi_;
        int sw = (pionowy_ && pionowy_->czy_widoczna()) ? (15 * scale) : 0;
        int w = obszar_.szerokosc - 2 - sw;
        return w > 0 ? w : 0;
    }
    int viewport_h() const { int h = obszar_.wysokosc - 2; return h > 0 ? h : 0; }
    int liczba_kolumn() const {
        int scale = skala_dpi_ < 1 ? 1 : skala_dpi_;
        int cw = szerokosc_elementu_ * scale;
        return cw > 0 ? przytnij(viewport_w() / cw, 1, 1024) : 1;
    }
    void aktualizuj_geometrie() {
        if (!pionowy_) return;
        int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
        int sw = 15 * s;
        int h = obszar_.wysokosc - 2;
        int x = obszar_.szerokosc - sw - 1;
        if (h < 0) h = 0;
        if (x < 1) x = 1;
        pionowy_->rozmiesc(Prostokat(x, 1, sw, h));
    }
    void aktualizuj_przewijanie() {
        if (!pionowy_) return;
        int scale = skala_dpi_ < 1 ? 1 : skala_dpi_;
        int cw = szerokosc_elementu_ * scale;
        int ch = wysokosc_elementu_ * scale;
        if (cw <= 0) cw = 1;
        if (ch <= 0) ch = 1;

        int fullw = obszar_.szerokosc - 2;
        int vh = obszar_.wysokosc - 2;
        if (fullw <= 0 || vh <= 0) {
            pionowy_->ustaw_widoczna(false);
            pionowy_->ustaw_zakres(0, 0);
            pionowy_->ustaw_wartosc(0);
            return;
        }

        int sw = 15 * scale;
        int cols_bez = przytnij(fullw / cw, 1, 1024);
        uint64_t rows_bez = liczba_ > 0 ? (liczba_ + cols_bez - 1) / cols_bez : 0;
        uint64_t total_h_bez = rows_bez * static_cast<uint64_t>(ch);
        bool widoczny = total_h_bez > static_cast<uint64_t>(vh);

        int cols = cols_bez;
        uint64_t rows = rows_bez;
        uint64_t total_h = total_h_bez;

        if (widoczny) {
            int w_ze_sb = fullw - sw;
            if (w_ze_sb > 0) {
                cols = przytnij(w_ze_sb / cw, 1, 1024);
                rows = liczba_ > 0 ? (liczba_ + cols - 1) / cols : 0;
                total_h = rows * static_cast<uint64_t>(ch);
            }
        }

        pionowy_->ustaw_widoczna(widoczny);
        aktualizuj_geometrie();

        int total_h_int = total_h > INT32_MAX ? INT32_MAX : static_cast<int>(total_h);
        int max_scroll = (widoczny && total_h_int > vh) ? (total_h_int - vh) : 0;

        pionowy_->ustaw_zakres(0, max_scroll);
        pionowy_->ustaw_rozmiar_strony(vh > 0 ? vh : 1);
        pionowy_->ustaw_maly_krok(ch);
        pionowy_->ustaw_duzy_krok(vh > 0 ? vh : ch);

        if (!widoczny) {
            pionowy_->ustaw_wartosc(0);
        } else if (pionowy_->wartosc() > max_scroll) {
            pionowy_->ustaw_wartosc(max_scroll);
        }

        przygotuj_widoczne(true);
    }
    Prostokat rect_elementu(int i) const {
        if (i < 0 || i >= static_cast<int>(liczba_)) return Prostokat();
        int s = skala_dpi_ < 1 ? 1 : skala_dpi_, cols = liczba_kolumn();
        if (cols <= 0) return Prostokat();
        int cw = szerokosc_elementu_ * s, ch = wysokosc_elementu_ * s;
        int sy = pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0;
        return Prostokat(1 + (i % cols) * cw, 1 + (i / cols) * ch - sy, cw, ch);
    }
    void uniewaznij_element(int i) { Prostokat r = rect_elementu(i); if (!r.czy_pusty()) uniewaznij_obszar(r.przeciecie(prostokat_widoku())); }
    int indeks_w_punkcie(int x, int y) const {
        int lx = x - obszar_ekranu_.x - 1, ly = y - obszar_ekranu_.y - 1;
        if (lx < 0 || ly < 0 || lx >= viewport_w() || ly >= viewport_h()) return -1;
        int s = skala_dpi_ < 1 ? 1 : skala_dpi_, cols = liczba_kolumn();
        if (cols <= 0) return -1;
        int cw = szerokosc_elementu_ * s, ch = wysokosc_elementu_ * s;
        if (cw <= 0 || ch <= 0) return -1;
        int sy = pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0;
        int col = lx / cw, row = (ly + sy) / ch;
        if (col >= cols) return -1;
        uint64_t idx = static_cast<uint64_t>(row) * static_cast<uint64_t>(cols) + static_cast<uint64_t>(col);
        return idx < liczba_ ? static_cast<int>(idx) : -1;
    }
    void przewin_do(int i) {
        if (i < 0 || !pionowy_) return;
        int s = skala_dpi_ < 1 ? 1 : skala_dpi_, cols = liczba_kolumn();
        if (cols <= 0) return;
        int ch = wysokosc_elementu_ * s;
        if (ch <= 0) return;
        int y = (i / cols) * ch;
        int cur = static_cast<int>(pionowy_->wartosc()), vh = viewport_h();
        if (vh <= 0) return;
        if (y < cur) pionowy_->ustaw_wartosc(y); else if (y + ch > cur + vh) pionowy_->ustaw_wartosc(y + ch - vh);
    }
    static void rysuj_obraz(KontekstRysowania& ctx, const Element& e, Prostokat dst) {
        if (!e.piksele || e.szerokosc_obrazu <= 0 || e.wysokosc_obrazu <= 0 || dst.czy_pusty()) {
            ctx.rysuj_prostokat(dst, 0x00302010); ctx.rysuj_ramke(dst, 0x00806030); return;
        }
        int dw = dst.szerokosc, dh = dst.wysokosc;
        if (dw > e.szerokosc_obrazu) dw = e.szerokosc_obrazu;
        if (dh > e.wysokosc_obrazu) dh = e.wysokosc_obrazu;
        int fitw = dw, fith = static_cast<int>((static_cast<int64_t>(e.wysokosc_obrazu) * dw) / e.szerokosc_obrazu);
        if (fith > dh) { fith = dh; fitw = static_cast<int>((static_cast<int64_t>(e.szerokosc_obrazu) * dh) / e.wysokosc_obrazu); }
        if (fitw <= 0 || fith <= 0) return;
        int ox = dst.x + (dw - fitw) / 2, oy = dst.y + (dh - fith) / 2;
        Prostokat visible = Prostokat(ox, oy, fitw, fith).przeciecie(ctx.lokalny_klip());
        for (int y = visible.y - oy; y < visible.dolny() - oy && !visible.czy_pusty(); ++y)
            for (int x = visible.x - ox; x < visible.prawy() - ox; ++x) {
            int sx = static_cast<int>((static_cast<int64_t>(x) * e.szerokosc_obrazu) / fitw);
            int sy = static_cast<int>((static_cast<int64_t>(y) * e.wysokosc_obrazu) / fith);
            uint32_t c = e.piksele[static_cast<size_t>(sy) * static_cast<size_t>(e.szerokosc_obrazu) + static_cast<size_t>(sx)];
            if (c != 0) ctx.rysuj_prostokat(Prostokat(ox + x, oy + y, 1, 1), c);
        }
    }

    // 32 bounded thumbnails, at most 160x90 each. Original model images
    // remain borrowed; the system wallpaper cache at exactly 160x90 is used directly.
    struct WpisCache { const uint32_t* source{nullptr}; int sw{0},sh{0},w{0},h{0}; Element image{}; };
    static constexpr int CACHE_SLOTS = 32, CACHE_PIXELS = 160*90;
    WpisCache cache_[CACHE_SLOTS]{};
    uint32_t* cache_pixels_{nullptr}; int nastepny_cache_{0};
    void rozmiar_obrazu(const Element& e,int& w,int& h) const {
        int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
        w = przytnij((szerokosc_elementu_-14)*s,1,160);
        h = przytnij((wysokosc_elementu_-34)*s,1,90);
        int fh = static_cast<int>(static_cast<int64_t>(e.wysokosc_obrazu)*w/e.szerokosc_obrazu);
        if(fh>h) w=static_cast<int>(static_cast<int64_t>(e.szerokosc_obrazu)*h/e.wysokosc_obrazu);
        else h=fh;
        if(w<1)w=1;
        if(h<1)h=1;
    }
    int znajdz_cache(const Element& e,int w,int h) const {
        for(int i=0;i<CACHE_SLOTS;++i) if(cache_[i].source==e.piksele && cache_[i].sw==e.szerokosc_obrazu &&
            cache_[i].sh==e.wysokosc_obrazu && cache_[i].w==w && cache_[i].h==h)return i;
        return -1;
    }
    void przygotuj_widoczne(bool alokuj) {
        if(renderer_||!liczba_)return;
        int cols=liczba_kolumn(),ch=wysokosc_elementu_*(skala_dpi_<1?1:skala_dpi_);
        if (cols <= 0 || ch <= 0) return;
        int sy=pionowy_&&pionowy_->czy_widoczna()?static_cast<int>(pionowy_->wartosc()):0;
        size_t begin=static_cast<size_t>(sy/ch)*cols,end=static_cast<size_t>((sy+viewport_h()+ch-1)/ch)*cols;
        if(end>liczba_)end=liczba_;
        for(size_t i=begin;i<end;++i) {
            const Element& e=elementy_[i];if(!e.piksele)continue;
            int w,h;rozmiar_obrazu(e,w,h);
            if((w==e.szerokosc_obrazu&&h==e.wysokosc_obrazu)||znajdz_cache(e,w,h)>=0)continue;
            if(!cache_pixels_&&alokuj) cache_pixels_=static_cast<uint32_t*>(gui_malloc(CACHE_SLOTS*CACHE_PIXELS*sizeof(uint32_t)));
            if(!cache_pixels_)continue;
            int slot=nastepny_cache_;
            nastepny_cache_=(nastepny_cache_+1)%CACHE_SLOTS;
            auto& c=cache_[slot];c.source=e.piksele;c.sw=e.szerokosc_obrazu;c.sh=e.wysokosc_obrazu;c.w=w;c.h=h;
            uint32_t* out=cache_pixels_+slot*CACHE_PIXELS;
            for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                out[y*w+x]=e.piksele[static_cast<size_t>(y*e.wysokosc_obrazu/h)*e.szerokosc_obrazu+x*e.szerokosc_obrazu/w];
            c.image=e;c.image.piksele=out;c.image.szerokosc_obrazu=w;c.image.wysokosc_obrazu=h;
        }
    }
    const Element& obraz_do_rysowania(const Element& e) const {
        if(!e.piksele)return e;
        int w,h;rozmiar_obrazu(e,w,h);int i=znajdz_cache(e,w,h);
        return i>=0?cache_[i].image:e;
    }

    Element* elementy_{nullptr}; size_t liczba_{0}, pojemnosc_{0};
    int wybrany_{-1}, fokus_{-1}, najechany_{-1};
    int szerokosc_elementu_{112}, wysokosc_elementu_{100};
    PasekPrzewijania* pionowy_{nullptr}; size_t ostatnio_narysowane_{0};
    FunkcjaWidoku cb_wybor_{nullptr}; void* dane_wyboru_{nullptr};
    FunkcjaWidoku cb_aktywacja_{nullptr}; void* dane_aktywacji_{nullptr};
    FunkcjaRenderera renderer_{nullptr}; void* dane_renderera_{nullptr};
};

} // namespace BursztynUI
