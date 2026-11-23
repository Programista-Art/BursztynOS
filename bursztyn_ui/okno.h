#pragma once

#include "kontrolka.h"
#include "panel.h"
#include "zdarzenia.h"
#include "rysowanie.h"
#include "popup.h"
#include "menu_kontekstowe.h"
#include "polecenie_ui.h"
#include "bursztyn_gui.h"
#include "okno_geometria.h"

namespace BursztynUI {

class Okno : private OdbiorcaListyPolecen {
public:
    static inline bool g_tlum_operacje_systemowe = false;
    static constexpr int WYSOKOSC_BELKI_TYTULOWEJ = 28;

    Okno(const char* tytul_okna = "Okno BursztynUI", int szerokosc = 480, int wysokosc = 320)
        : szerokosc_(szerokosc), wysokosc_(wysokosc),
          logiczna_szerokosc_(szerokosc), logiczna_wysokosc_(wysokosc) {
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
        ustaw_liste_polecen(nullptr);
        if (bufor_regionu_) gui_free(bufor_regionu_);
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
        if (!g_tlum_operacje_systemowe) {
            gui_ustaw_tytul_okna(tytul_);
        }
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
        return (!czy_zminimalizowane_ && kontrolka_glowna_ && kontrolka_glowna_->czy_wymaga_animacji()) ||
               popupy_.czy_wymaga_animacji();
    }

    bool czy_wymaga_odrysowania() const {
        return czy_brudne_ || ma_oczekujaca_geometrie_ || !brudny_obszar_klienta_.czy_pusty() ||
               (kontrolka_glowna_ && kontrolka_glowna_->czy_brudna()) ||
               popupy_.czy_brudny();
    }

    void krok_animacji(uint64_t teraz_ns = 0) {
        if (!czy_zminimalizowane_ && kontrolka_glowna_ && kontrolka_glowna_->czy_wymaga_animacji()) {
            kontrolka_glowna_->krok_animacji(teraz_ns);
        }
        if (popupy_.czy_wymaga_animacji()) {
            popupy_.krok_animacji(teraz_ns);
        }
    }

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

    int logiczna_szerokosc() const { return logiczna_szerokosc_; }
    int logiczna_wysokosc() const { return logiczna_wysokosc_; }
    size_t pojemnosc_regionu() const { return pojemnosc_regionu_; }
    const uint32_t* bufor_regionu() const { return bufor_regionu_; }

    void wysrodkuj() {
        dopasuj_do_obszaru_roboczego(true);
        Prostokat work = pobierz_obszar_roboczy();
        int nx = work.x + (work.szerokosc - szerokosc_) / 2;
        int ny = work.y + (work.wysokosc - wysokosc_) / 2;
        ustaw_pozycje(nx, ny);
    }

    void ustaw_pozycje(int x, int y) {
        const int stary_x = x_, stary_y = y_;
        x_ = x;
        y_ = y;
        dopasuj_do_obszaru_roboczego(false);
        if ((x_ != stary_x || y_ != stary_y) && !g_tlum_operacje_systemowe) {
            gui_ustaw_geometrie_okna(x_, y_, szerokosc_, wysokosc_,
                                     czy_zmaksymalizowane_ ? 2 : 1);
        }
        popupy_.ustaw_geometrie_wlasciciela(Prostokat(x_, y_, szerokosc_, wysokosc_),
                                            pobierz_obszar_roboczy());
    }

    void ustaw_logiczny_rozmiar(int lw, int lh) {
        if (lw < 100) lw = 100;
        if (lh < 60) lh = 60;
        logiczna_szerokosc_ = lw;
        logiczna_wysokosc_ = lh;
        szerokosc_ = ui_skaluj(logiczna_szerokosc_, skala_dpi_.procent());
        wysokosc_ = ui_skaluj(logiczna_wysokosc_, skala_dpi_.procent());
        dopasuj_do_obszaru_roboczego(false);
        if (!g_tlum_operacje_systemowe) {
            gui_ustaw_geometrie_okna(x_, y_, szerokosc_, wysokosc_, czy_zmaksymalizowane_ ? 2 : 1);
        }
        brudny_obszar_klienta_ = Prostokat();
        przelicz_uklad();
        czy_brudne_ = true;
    }

    void ustaw_rozmiar(int w, int h, bool aktualizuj_logiczny = true) {
        if (w < 100) w = 100;
        if (h < 60) h = 60;
        if (aktualizuj_logiczny) {
            logiczna_szerokosc_ = ui_odskaluj(w, skala_dpi_.procent());
            logiczna_wysokosc_ = ui_odskaluj(h, skala_dpi_.procent());
        }
        const int stary_x = x_, stary_y = y_, stary_w = szerokosc_, stary_h = wysokosc_;
        szerokosc_ = w;
        wysokosc_ = h;
        dopasuj_do_obszaru_roboczego(false);
        if (szerokosc_ != stary_w || wysokosc_ != stary_h || x_ != stary_x || y_ != stary_y) {
            if (!g_tlum_operacje_systemowe) {
                gui_ustaw_geometrie_okna(x_, y_, szerokosc_, wysokosc_,
                                         czy_zmaksymalizowane_ ? 2 : 1);
            }
            brudny_obszar_klienta_ = Prostokat();
            przelicz_uklad();
            czy_brudne_ = true;
        }
    }

    void ustaw_geometrie(int x, int y, int w, int h, bool aktualizuj_logiczny = true) {
        if (w < 100) w = 100;
        if (h < 60) h = 60;
        if (aktualizuj_logiczny) {
            logiczna_szerokosc_ = ui_odskaluj(w, skala_dpi_.procent());
            logiczna_wysokosc_ = ui_odskaluj(h, skala_dpi_.procent());
        }
        const int stary_x = x_, stary_y = y_, stary_w = szerokosc_, stary_h = wysokosc_;
        x_ = x;
        y_ = y;
        szerokosc_ = w;
        wysokosc_ = h;
        dopasuj_do_obszaru_roboczego(false);
        if (szerokosc_ != stary_w || wysokosc_ != stary_h || x_ != stary_x || y_ != stary_y) {
            if (!g_tlum_operacje_systemowe) {
                gui_ustaw_geometrie_okna(x_, y_, szerokosc_, wysokosc_,
                                         czy_zmaksymalizowane_ ? 2 : 1);
            }
            brudny_obszar_klienta_ = Prostokat();
            przelicz_uklad();
            czy_brudne_ = true;
        }
    }

    // Wywolywane przed utworzeniem warstwy oraz po programowej zmianie
    // geometrii. BWS62 jest jedynym zrodlem rozmiaru obszaru roboczego.
    void dopasuj_do_obszaru_roboczego(bool dopasuj_rozmiar = true) {
        Prostokat work = pobierz_obszar_roboczy();
        if (dopasuj_rozmiar) {
            int target_w = ui_skaluj(logiczna_szerokosc_, skala_dpi_.procent());
            int target_h = ui_skaluj(logiczna_wysokosc_, skala_dpi_.procent());
            szerokosc_ = target_w > work.szerokosc ? work.szerokosc : target_w;
            wysokosc_ = target_h > work.wysokosc ? work.wysokosc : target_h;
            if (szerokosc_ < 1) szerokosc_ = 1;
            if (wysokosc_ < 1) wysokosc_ = 1;
            int max_x = work.prawy() - szerokosc_;
            int max_y = work.dolny() - wysokosc_;
            if (max_x < work.x) max_x = work.x;
            if (max_y < work.y) max_y = work.y;
            x_ = przytnij(x_, work.x, max_x);
            y_ = przytnij(y_, work.y, max_y);
            return;
        }
        // Okno mozna rozciagnac poza ekran (do 2x ekranu, limit kompozytora) i
        // przesunac czesciowo poza obszar roboczy; wymagamy tylko, by pasek
        // tytulu pozostal uchwytny (odzyskanie po zmianie rozdzielczosci).
        const int limit_w = work.szerokosc > 0 ? 2 * work.szerokosc : 1;
        const int limit_h = work.wysokosc > 0 ? 2 * work.wysokosc : 1;
        if (szerokosc_ > limit_w) szerokosc_ = limit_w;
        if (wysokosc_ > limit_h) wysokosc_ = limit_h;
        if (szerokosc_ < 1) szerokosc_ = 1;
        if (wysokosc_ < 1) wysokosc_ = 1;
        przytnij_pozycje_do_odzyskania(work);
    }

    // Pasek tytulu zawsze uchwytny: >= 96 px (skalowanych) w poziomie, gorna
    // krawedz co najwyzej polowe paska nad obszarem roboczym.
    void przytnij_pozycje_do_odzyskania(const Prostokat& work) {
        const int belka = WYSOKOSC_BELKI_TYTULOWEJ * skala_dpi_;
        const OknoGeometria::Prost p = OknoGeometria::przytnij_ruch(
            {x_, y_, szerokosc_, wysokosc_}, {work.x, work.y, work.szerokosc, work.wysokosc},
            ui_skaluj(96, skala_dpi_.procent()), belka, belka / 2);
        x_ = p.x;
        y_ = p.y;
    }

    // ---- zmiana rozmiaru krawedziami i rogami -------------------------
    bool czy_zmiana_rozmiaru() const { return czy_zmiana_rozmiaru_; }
    // Aplikacje o ukladzie niestandardowym moga zablokowac zmiane rozmiaru.
    void ustaw_zmiane_rozmiaru_dozwolona(bool dozwolona) { zmiana_rozmiaru_dozwolona_ = dozwolona; }
    // Minimalny rozmiar w pikselach logicznych (domyslnie 320x200, ale nie
    // wiecej niz rozmiar poczatkowy okna, zeby uklady stalych kontrolek byly bezpieczne).
    void ustaw_minimalny_rozmiar(int lw, int lh) { min_logiczna_szerokosc_ = lw; min_logiczna_wysokosc_ = lh; }

    void minimalny_rozmiar_pikseli(int& w, int& h) const {
        int lw = min_logiczna_szerokosc_ > 0 ? min_logiczna_szerokosc_
                 : (logiczna_szerokosc_ < 320 ? logiczna_szerokosc_ : 320);
        int lh = min_logiczna_wysokosc_ > 0 ? min_logiczna_wysokosc_
                 : (logiczna_wysokosc_ < 200 ? logiczna_wysokosc_ : 200);
        w = ui_skaluj(lw, skala_dpi_.procent());
        h = ui_skaluj(lh, skala_dpi_.procent());
    }

    uint8_t krawedz_w_punkcie_okna(int lokalny_x, int lokalny_y) const {
        if (!zmiana_rozmiaru_dozwolona_ || czy_zmaksymalizowane_ || czy_zminimalizowane_) return 0;
        return OknoGeometria::krawedz_w_punkcie(lokalny_x, lokalny_y, szerokosc_, wysokosc_,
            ui_skaluj(5, skala_dpi_.procent()), ui_skaluj(14, skala_dpi_.procent()));
    }

    // Zwraca true, gdy kursor jest w strefie chwytania (kursor zmiany rozmiaru ustawiony).
    bool aktualizuj_kursor_krawedzi(int lokalny_x, int lokalny_y) {
        if (popupy_.czy_otwarty()) return false;
        const uint8_t krawedz = krawedz_w_punkcie_okna(lokalny_x, lokalny_y);
        if (krawedz) {
            const uint32_t kursor = OknoGeometria::kursor_dla_krawedzi(krawedz);
            if (kursor != kursor_krawedzi_) { kursor_krawedzi_ = kursor; (void)gui_ustaw_kursor(kursor); }
            return true;
        }
        if (kursor_krawedzi_ != 0) { kursor_krawedzi_ = 0; (void)gui_ustaw_kursor(0); }
        return false;
    }

    bool rozpocznij_zmiane_rozmiaru(int mx, int my) {
        const uint8_t krawedz = krawedz_w_punkcie_okna(mx - x_, my - y_);
        if (!krawedz) return false;
        czy_zmiana_rozmiaru_ = true;
        krawedzie_zmiany_ = krawedz;
        geometria_startowa_ = {x_, y_, szerokosc_, wysokosc_};
        start_zmiany_x_ = mx;
        start_zmiany_y_ = my;
        ma_oczekujaca_geometrie_ = false;
        czy_przeciagane_ = false;
        kontrolka_przechwytujaca_ = nullptr;
        gui_ustaw_capture_myszy(true);
        return true;
    }

    void obsluz_ruch_zmiany_rozmiaru(int mx, int my) {
        OknoGeometria::Ograniczenia lim{};
        int mw = 0, mh = 0;
        minimalny_rozmiar_pikseli(mw, mh);
        const Prostokat work = pobierz_obszar_roboczy();
        lim.min_w = mw; lim.min_h = mh;
        lim.max_w = work.szerokosc > 0 ? 2 * work.szerokosc : mw;
        lim.max_h = work.wysokosc > 0 ? 2 * work.wysokosc : mh;
        const OknoGeometria::Prost nowa = OknoGeometria::zmien_rozmiar(
            geometria_startowa_, krawedzie_zmiany_,
            OknoGeometria::do_i32(int64_t(mx) - start_zmiany_x_),
            OknoGeometria::do_i32(int64_t(my) - start_zmiany_y_), lim);
        // Pozycja krawedzi zakotwiczonej moze wyjsc poza ekran, ale pasek tytulu zostaje uchwytny.
        const OknoGeometria::Prost p = OknoGeometria::przytnij_ruch(nowa,
            {work.x, work.y, work.szerokosc, work.wysokosc}, ui_skaluj(96, skala_dpi_.procent()),
            WYSOKOSC_BELKI_TYTULOWEJ * skala_dpi_, WYSOKOSC_BELKI_TYTULOWEJ * skala_dpi_ / 2);
        oczekujaca_geometria_ = p;
        ma_oczekujaca_geometrie_ = true;
        czy_brudne_ = true;   // wymusza przejscie rysuj(), ktore zastosuje geometrie raz na klatke
    }

    // Wspolne dla klatki: wiele zdarzen ruchu daje jedna realokacje warstwy.
    void zastosuj_oczekujaca_geometrie() {
        if (!ma_oczekujaca_geometrie_) return;
        ma_oczekujaca_geometrie_ = false;
        const OknoGeometria::Prost g = oczekujaca_geometria_;
        if (g.x == x_ && g.y == y_ && g.w == szerokosc_ && g.h == wysokosc_) return;
        popupy_.zamknij(PowodZamknieciaPopup::ZmianaGeometrii);
        x_ = g.x; y_ = g.y; szerokosc_ = g.w; wysokosc_ = g.h;
        logiczna_szerokosc_ = ui_odskaluj(szerokosc_, skala_dpi_.procent());
        logiczna_wysokosc_ = ui_odskaluj(wysokosc_, skala_dpi_.procent());
        if (!g_tlum_operacje_systemowe)
            gui_ustaw_geometrie_okna(x_, y_, szerokosc_, wysokosc_, 1);
        brudny_obszar_klienta_ = Prostokat();
        przelicz_uklad();
        czy_brudne_ = true;
    }

    void zakoncz_zmiane_rozmiaru() {
        if (!czy_zmiana_rozmiaru_) return;
        czy_zmiana_rozmiaru_ = false;
        krawedzie_zmiany_ = 0;
        zastosuj_oczekujaca_geometrie();
        popupy_.ustaw_geometrie_wlasciciela(Prostokat(x_, y_, szerokosc_, wysokosc_), pobierz_obszar_roboczy());
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

    virtual void zaktualizuj_motyw() {
        Motyw::pobierz_domyslny().zaktualizuj_z_systemu();
        if (kontrolka_glowna_) {
            kontrolka_glowna_->zaktualizuj_motyw();
        }
        popupy_.zaktualizuj_motyw();
        brudny_obszar_klienta_ = Prostokat();
        przelicz_uklad();
        czy_brudne_ = true;
    }

    template <typename T, typename... Args>
    T& dodaj(Args&&... args) {
        T* k = new T(args...);
        ustaw_kontrolke_glowna(k);
        return *k;
    }

    Kontrolka* fokus() const { return kontrolka_z_fokusem_; }
    bool ustaw_liste_polecen(ListaPolecen* lista) {
        if (lista_polecen_ == lista) return true;
        if (lista_polecen_) lista_polecen_->odlacz_odbiorce(this);
        lista_polecen_ = nullptr;
        if (lista && !lista->dolacz_odbiorce(this)) return false;
        lista_polecen_ = lista; return true;
    }
    void ustaw_liste_polecen(ListaPolecen& lista) { ustaw_liste_polecen(&lista); }
    ListaPolecen* lista_polecen() const { return lista_polecen_; }
    ZarzadcaPopupow& zarzadca_popupow() { return popupy_; }
    const ZarzadcaPopupow& zarzadca_popupow() const { return popupy_; }
    void ustaw_fokus(Kontrolka* k) {
        // Wejscie na pasek menu zapamietuje kontrolke, do ktorej fokus wroci po
        // zakonczeniu pracy z menu; zwykla kontrolka konczy ten tryb.
        if (k && k != kontrolka_z_fokusem_) {
            if (k->czy_pasek_menu()) {
                if (kontrolka_z_fokusem_ && !kontrolka_z_fokusem_->czy_pasek_menu())
                    fokus_przed_menu_ = kontrolka_z_fokusem_;
            } else {
                fokus_przed_menu_ = nullptr;
            }
            menu_bylo_otwarte_ = false;
        }
        if (kontrolka_z_fokusem_ != k) {
            if (kontrolka_z_fokusem_) {
                kontrolka_z_fokusem_->przy_utracie_fokusu();
            }
            kontrolka_z_fokusem_ = k;
            if (kontrolka_z_fokusem_) {
                kontrolka_z_fokusem_->przy_fokusie();
            }
            // Obie kontrolki uniewazniaja swoje lokalne prostokaty w
            // przy_fokusie/przy_utracie_fokusu. Zmiana fokusu nie jest
            // zmiana geometrii i nie moze wymuszac pelnego repaintu okna.
        }
    }

    MenuKontekstowe* znajdz_menu_kontekstowe(Kontrolka* od,
                                               Kontrolka** wlasciciel = nullptr) const {
        for (Kontrolka* k = od; k; k = k->rodzic()) {
            if (k->menu_kontekstowe()) {
                if (wlasciciel) *wlasciciel = k;
                return k->menu_kontekstowe();
            }
        }
        if (wlasciciel) *wlasciciel = nullptr;
        return nullptr;
    }

    bool otworz_menu_kontekstowe(Kontrolka* od, int x_ekranu, int y_ekranu,
                                 bool z_klawiatury = false) {
        Kontrolka* wlasciciel = nullptr;
        MenuKontekstowe* menu = znajdz_menu_kontekstowe(od, &wlasciciel);
        if (!menu || !wlasciciel || !wlasciciel->czy_efektywnie_widoczna()) return false;
        if (z_klawiatury)
            return menu->pokaz_pod_kontrolka(popupy_, *wlasciciel, od);
        return menu->pokaz_dla(*wlasciciel, x_ekranu, y_ekranu);
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

    SkalaUI skala_dpi() const { return skala_dpi_; }
    void ustaw_skale_dpi(SkalaUI skala, bool zglos_geometrie = false) {
        const uint32_t stary_procent = skala_dpi_.procent();
        const bool zmiana_skali = stary_procent != skala.procent();
        skala_dpi_ = skala;
        if (zmiana_skali) {
            if (!czy_zmaksymalizowane_) {
                szerokosc_ = ui_skaluj(logiczna_szerokosc_, skala_dpi_.procent());
                wysokosc_ = ui_skaluj(logiczna_wysokosc_, skala_dpi_.procent());
                dopasuj_do_obszaru_roboczego(false);
            } else {
                Prostokat work = pobierz_obszar_roboczy();
                x_ = work.x; y_ = work.y;
                szerokosc_ = work.szerokosc; wysokosc_ = work.wysokosc;
                zapisana_geometria_.szerokosc = ui_skaluj(logiczna_szerokosc_, skala_dpi_.procent());
                zapisana_geometria_.wysokosc = ui_skaluj(logiczna_wysokosc_, skala_dpi_.procent());
            }
        }
        popupy_.ustaw_skale_dpi(skala_dpi_);
        if (kontrolka_glowna_) {
            kontrolka_glowna_->ustaw_skale_dpi(skala_dpi_);
        }
        if (zglos_geometrie && zmiana_skali && !g_tlum_operacje_systemowe) {
            gui_ustaw_geometrie_okna(x_, y_, szerokosc_, wysokosc_,
                                     czy_zmaksymalizowane_ ? 2 : 1);
        }
        brudny_obszar_klienta_ = Prostokat();
        przelicz_uklad();
        czy_brudne_ = true;
    }

    void przelicz_uklad() {
        ++liczba_przejsc_ukladu_;
        // Allocate only on geometry changes, never in input or paint.
        size_t piksele = (szerokosc_ > 0 && wysokosc_ > 0 && szerokosc_ <= 8192 && wysokosc_ <= 8192)
            ? static_cast<size_t>(szerokosc_) * wysokosc_ : 0;
        size_t wymagane = piksele <= 2048ULL * 2048ULL ? piksele : (2048ULL * 2048ULL);
        if (wymagane > pojemnosc_regionu_) {
            uint32_t* nowy = static_cast<uint32_t*>(gui_malloc(wymagane * sizeof(uint32_t)));
            if (nowy) { if (bufor_regionu_) gui_free(bufor_regionu_); bufor_regionu_ = nowy; pojemnosc_regionu_ = wymagane; }
        }
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
    uint64_t liczba_zdarzen_ruchu_myszy() const { return liczba_zdarzen_ruchu_myszy_; }
    uint64_t liczba_przejsc_ukladu() const { return liczba_przejsc_ukladu_; }
    uint64_t liczba_odrysowan() const { return liczba_odrysowan_; }
    uint64_t liczba_pelnych_odrysowan() const { return liczba_pelnych_odrysowan_; }
    uint64_t liczba_brudnych_pikseli() const { return liczba_brudnych_pikseli_; }

    Prostokat obszar_klienta() const {
        int belka_h = WYSOKOSC_BELKI_TYTULOWEJ * skala_dpi_;
        int klient_h = wysokosc_ - belka_h;
        if (klient_h < 0) klient_h = 0;
        return Prostokat(0, belka_h, szerokosc_, klient_h);
    }

    Prostokat pobierz_brudny_obszar() const { return brudny_obszar_klienta_; }

    void uniewaznij() { czy_brudne_ = true; }

    void uniewaznij_obszar(Prostokat r) {
        brudny_obszar_klienta_ = brudny_obszar_klienta_.suma(r);
    }

    // Opcjonalny hak cyklu rysowania (domyslnie brak, koszt: jedno porownanie). Aplikacje z praca czasu
    // rzeczywistego w tym samym watku (np. Grajek: dopelnienie kolejki PCM BWS 110) dostaja punkt kontrolny tuz
    // przed kazda publikacja regionu (BWS 57) oraz znaczniki poczatku i konca cyklu. Hak nie moze rysowac ani
    // uniewazniac kontrolek; nie zmienia kolejnosci ani zawartosci rysowania.
    enum class FazaRysowania : uint8_t { Poczatek, PrzedPublikacja, PoPublikacji, Koniec };
    static inline void (*hak_rysowania)(FazaRysowania) = nullptr;

    void rysuj(KontekstRysowania& ctx, bool wymus = false) {
        if (czy_zminimalizowane_) return;
        zastosuj_oczekujaca_geometrie();
        bool pelne = czy_brudne_ || wymus;
        if (!pelne && !czy_wymaga_odrysowania()) return;
        if (hak_rysowania) hak_rysowania(FazaRysowania::Poczatek);
        ObszaryBrudne regiony;
        Prostokat klient = obszar_klienta();
        if (pelne) {
            ++liczba_pelnych_odrysowan_;
            if (!KontekstRysowania::g_tlum_rysowanie_globalne)
                gui_rysuj_standardowa_belke(x_, y_, szerokosc_, tytul_, czy_zmaksymalizowane_);
            regiony.dodaj(klient);
        } else {
            if (kontrolka_glowna_) {
                const auto& r = kontrolka_glowna_->brudne_regiony_;
                for (size_t i = 0; i < r.liczba; ++i) regiony.dodaj(r.prostokaty[i].przeciecie(klient));
            }
            regiony.dodaj(brudny_obszar_klienta_.przeciecie(klient));
        }
        ostatni_obszar_odrysowania_ = pelne ? Prostokat(0, 0, szerokosc_, wysokosc_) : Prostokat();
        ++liczba_odrysowan_;
        if (pelne) liczba_brudnych_pikseli_ += static_cast<uint64_t>(szerokosc_)*WYSOKOSC_BELKI_TYTULOWEJ*skala_dpi_;
        for (size_t i = 0; i < regiony.liczba; ++i) {
            Prostokat r = regiony.prostokaty[i];
            if (r.czy_pusty()) continue;
            ostatni_obszar_odrysowania_ = ostatni_obszar_odrysowania_.suma(r);
            size_t pixels = static_cast<size_t>(r.szerokosc)*r.wysokosc;
            liczba_brudnych_pikseli_ += pixels;
            Prostokat swiat(x_+r.x, y_+r.y, r.szerokosc, r.wysokosc);
            bool buforowany = bufor_regionu_ && pixels <= pojemnosc_regionu_;
            if (buforowany) ctx.ustaw_cel(bufor_regionu_, swiat);
            ctx.dodaj_przesuniecie(Punkt(x_, y_)); ctx.dodaj_przyciecie(r);
            Prostokat tlo_klienta = r.przeciecie(klient);
            if (!tlo_klienta.czy_pusty())
                ctx.rysuj_prostokat(tlo_klienta, motyw_kolor(MOTYW_KOLOR_OKNOTLO));
            if (kontrolka_glowna_) kontrolka_glowna_->rysuj(ctx, false);
            ctx.zdejmij_przyciecie(); ctx.zdejmij_przesuniecie(); ctx.ustaw_cel(nullptr, Prostokat());
            if (buforowany && !KontekstRysowania::g_tlum_rysowanie_globalne) {
                if (hak_rysowania) hak_rysowania(FazaRysowania::PrzedPublikacja);
                gui_blit(swiat.x, swiat.y, r.szerokosc, r.wysokosc, bufor_regionu_);
                if (hak_rysowania) hak_rysowania(FazaRysowania::PoPublikacji);
            }
        }

        if (popupy_.czy_otwarty() && (popupy_.czy_brudny() || pelne)) {
            popupy_.odrysuj_popupy(pelne);
        }

        if (kontrolka_glowna_) kontrolka_glowna_->wyczysc_brudny_obszar();
        brudny_obszar_klienta_ = Prostokat(); czy_brudne_ = false;
        if (hak_rysowania) hak_rysowania(FazaRysowania::Koniec);
    }

    void minimalizuj() {
        popupy_.zamknij(PowodZamknieciaPopup::WlascicielZminimalizowany);
        czy_przeciagane_ = false; czy_zmiana_rozmiaru_ = false; ma_oczekujaca_geometrie_ = false;
#if BURSZTYN_DIAG_GEOM_TRACE
        int vx = 0, vy = 0, vw = 0, vh = 0;
        gui_pobierz_widoczna_geometrie(&vx, &vy, &vw, &vh);
        gui_log_geom_trace("Okno::minimalizuj", vx, vy, vw, vh,
                           0, 0, 0, 0,
                           g_tlum_operacje_systemowe, !g_tlum_operacje_systemowe,
                           vx, vy, vw, vh);
#endif
        if (!g_tlum_operacje_systemowe) {
            gui_ustaw_capture_myszy(false);
            czy_zminimalizowane_ = gui_minimalizuj_okno();
        } else {
            czy_zminimalizowane_ = true;
        }
    }

    void przelacz_maksymalizacje() {
        popupy_.zamknij(PowodZamknieciaPopup::ZmianaGeometrii);
        czy_przeciagane_ = false; czy_zmiana_rozmiaru_ = false; ma_oczekujaca_geometrie_ = false;
        if (!g_tlum_operacje_systemowe) {
            gui_ustaw_capture_myszy(false);
        }

        const int old_x = x_;
        const int old_y = y_;
        const int old_w = szerokosc_;
        const int old_h = wysokosc_;

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

#if BURSZTYN_DIAG_GEOM_TRACE
        int vx = 0, vy = 0, vw = 0, vh = 0;
        gui_pobierz_widoczna_geometrie(&vx, &vy, &vw, &vh);
        gui_log_geom_trace(czy_zmaksymalizowane_ ? "Okno::przelacz_maksymalizacje(max)" : "Okno::przelacz_maksymalizacje(restore)",
                           old_x, old_y, old_w, old_h,
                           x_, y_, szerokosc_, wysokosc_,
                           g_tlum_operacje_systemowe, !g_tlum_operacje_systemowe,
                           vx, vy, vw, vh);
#else
        (void)old_x; (void)old_y; (void)old_w; (void)old_h;
#endif

        if (!g_tlum_operacje_systemowe) {
            gui_ustaw_geometrie_okna(x_, y_, szerokosc_, wysokosc_, czy_zmaksymalizowane_ ? 2 : 1);
        }

        przelicz_uklad();
        czy_brudne_ = true;
    }

    virtual void zamknij() {
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

    virtual void obsluz_zdarzenie(const bws_zdarzenie& surowe_zdarzenie) {
        if (przed_zdarzeniem_menu(surowe_zdarzenie)) return;
        obsluz_zdarzenie_okna(surowe_zdarzenie);
        po_zdarzeniu_menu();
    }

    Kontrolka* fokus_przed_menu() const { return fokus_przed_menu_; }

private:
    // Lewy Alt (fizyczny): PS/2 38 bez E0 albo USB 226; nigdy AltGr.
    static bool czy_lewy_alt(const bws_zdarzenie& e) {
        const uint32_t kod = e.kod;
        return !(e.przyciski & BWS_MOD_ALTGR) &&
               (((kod & BWS_KEY_SOURCE_USB) && (kod & 0xFFU) == 226U) ||
                (!(kod & BWS_KEY_SOURCE_USB) && !(kod & BWS_KEY_SOURCE_E0) && (kod & 0x7FU) == 0x38U));
    }

    bool przywroc_fokus_po_menu() {
        Kontrolka* cel = fokus_przed_menu_;
        menu_bylo_otwarte_ = false;
        if (cel && czy_w_drzewie(kontrolka_glowna_, cel) && cel->czy_efektywnie_widoczna() &&
            cel->czy_moze_miec_fokus()) {
            ustaw_fokus(cel);
            return true;
        }
        return false;
    }

    // Pasek menu z fokusem, bez otwartego menu (np. po lewym Alt): tekst i klawisze,
    // ktore nie steruja menu, wracaja do poprzedniej kontrolki (edytora), zamiast
    // ginac w pasku menu. Esc i ponowny lewy Alt konczą tryb menu. true = pochloniete.
    bool przed_zdarzeniem_menu(const bws_zdarzenie& e) {
        if (!kontrolka_z_fokusem_ || !kontrolka_z_fokusem_->czy_pasek_menu() || popupy_.czy_otwarty()) return false;
        if (e.typ == BWS_ZDARZENIE_TEXT_INPUT) { (void)przywroc_fokus_po_menu(); return false; }
        if (e.typ != BWS_ZDARZENIE_KLAWISZ_FIZYCZNY || e.x == 0) return false;
        if (czy_lewy_alt(e)) return przywroc_fokus_po_menu();
        const uint32_t polecenie = klawisz_polecenia(e);
        if (polecenie == 0) return false;                         // modyfikatory (Shift, Ctrl, AltGr, Caps)
        if (polecenie == 27U) return przywroc_fokus_po_menu();    // Esc
        if (polecenie == 0x4BU || polecenie == 0x4DU || polecenie == 0x50U || polecenie == 0x48U ||
            polecenie == 13U || polecenie == 9U) return false;    // nawigacja paska menu i Tab
        (void)przywroc_fokus_po_menu();
        return false;
    }

    // Menu zamkniete (polecenie wykonane, Esc, klik obok, zamkniety dialog
    // otwarty z menu): fokus wraca do kontrolki sprzed wejscia na pasek menu.
    void po_zdarzeniu_menu() {
        if (!kontrolka_z_fokusem_ || !kontrolka_z_fokusem_->czy_pasek_menu()) return;
        if (popupy_.czy_otwarty()) menu_bylo_otwarte_ = true;
        else if (menu_bylo_otwarte_) (void)przywroc_fokus_po_menu();
    }

public:
    void obsluz_zdarzenie_okna(const bws_zdarzenie& surowe_zdarzenie) {
        int belka_h = WYSOKOSC_BELKI_TYTULOWEJ * skala_dpi_;
        int mx = surowe_zdarzenie.x;
        int my = surowe_zdarzenie.y;
        int lokalny_x = mx - x_;
        int lokalny_y = my - y_;

        switch (surowe_zdarzenie.typ) {
            case BWS_ZDARZENIE_EKRAN: {
                GuiMonitorInfo current{};
                const uint32_t scale=gui_pobierz_monitor(0,&current)?current.scale_x1000/10:surowe_zdarzenie.kod;
                if (!ui_skala_poprawna(scale)) break;
                popupy_.zamknij(PowodZamknieciaPopup::ZmianaGeometrii);
                zwolnij_przechwycenie_myszy(); czy_przeciagane_ = false; czy_zmiana_rozmiaru_ = false; ma_oczekujaca_geometrie_ = false; kontrolka_najechana_ = nullptr;
                ui_skala_procent = scale;
                skala_dpi_ = ui_skala();
                if (czy_zmaksymalizowane_) {
                    Prostokat work = pobierz_obszar_roboczy();
                    x_ = work.x; y_ = work.y;
                    szerokosc_ = work.szerokosc; wysokosc_ = work.wysokosc;
                } else {
                    szerokosc_ = ui_skaluj(logiczna_szerokosc_, skala_dpi_.procent());
                    wysokosc_ = ui_skaluj(logiczna_wysokosc_, skala_dpi_.procent());
                    dopasuj_do_obszaru_roboczego(false);
                }
                popupy_.ustaw_skale_dpi(skala_dpi_);
                if (kontrolka_glowna_) {
                    kontrolka_glowna_->ustaw_skale_dpi(skala_dpi_);
                }
                if (!g_tlum_operacje_systemowe) {
                    gui_ustaw_geometrie_okna(x_, y_, szerokosc_, wysokosc_,
                                             czy_zmaksymalizowane_ ? 2 : 1);
                }
                brudny_obszar_klienta_ = Prostokat();
                przelicz_uklad();
                czy_brudne_ = true;
                break;
            }
            case BWS_ZDARZENIE_MOTYW_ZMIENIONY: {
                zaktualizuj_motyw();
                break;
            }
            case BWS_ZDARZENIE_MYSZ_RUCH: {
                ++liczba_zdarzen_ruchu_myszy_;
                if (popupy_.czy_otwarty()) {
                    ZdarzenieMyszki pe;
                    pe.typ = TypZdarzenia::RuchMyszy;
                    pe.x = mx; pe.y = my;
                    pe.modyfikatory = surowe_zdarzenie.przyciski;
                    pe.timestamp = surowe_zdarzenie.timestamp;
                    if (popupy_.obsluz_ruch(pe)) return;
                }
                if (czy_zmiana_rozmiaru_) {
                    obsluz_ruch_zmiany_rozmiaru(mx, my);
                    return;
                }
                if (czy_przeciagane_) {
                    Prostokat work = pobierz_obszar_roboczy();
                    const int stary_x = x_, stary_y = y_;
                    x_ = static_cast<int>(OknoGeometria::do_i32(int64_t(mx) - przesuniecie_ciagniecia_x_));
                    y_ = static_cast<int>(OknoGeometria::do_i32(int64_t(my) - przesuniecie_ciagniecia_y_));
                    przytnij_pozycje_do_odzyskania(work);
                    const int nx = x_, ny = y_;
                    x_ = stary_x; y_ = stary_y;

                    if (nx != x_ || ny != y_) {
                        x_ = nx;
                        y_ = ny;
                        popupy_.ustaw_geometrie_wlasciciela(
                            Prostokat(x_, y_, szerokosc_, wysokosc_), work);
                        bws_przesun_warstwe(x_, y_);
                        gui_odswiez();
                    }
                    return;
                }

                if (!kontrolka_przechwytujaca_ && aktualizuj_kursor_krawedzi(lokalny_x, lokalny_y)) break;
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
                    pe.x = mx; pe.y = my;
                    pe.przycisk = static_cast<int>(
                        surowe_zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_DOWN
                        ? PrzyciskMyszy::Prawy : PrzyciskMyszy::Lewy);
                    pe.modyfikatory = surowe_zdarzenie.przyciski;
                    pe.timestamp = surowe_zdarzenie.timestamp;
                    if (popupy_.obsluz_wcisniecie(pe)) return;
                }
                if (surowe_zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN && rozpocznij_zmiane_rozmiaru(mx, my)) return;
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
                        ZdarzenieMyszki me;
                        me.typ = TypZdarzenia::WcisniecieMyszy;
                        me.x = lokalny_x;
                        me.y = lokalny_y;
                        me.przycisk = static_cast<int>(
                            (surowe_zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_DOWN)
                            ? PrzyciskMyszy::Prawy : PrzyciskMyszy::Lewy);
                        me.modyfikatory = surowe_zdarzenie.przyciski;
                        me.timestamp = surowe_zdarzenie.timestamp;

                        if (me.czy_prawy_przycisk()) {
                            if (otworz_menu_kontekstowe(hit, mx, my, false)) {
                                me.obsluzone = true;
                                return;
                            }
                            // Kontrolowane babelkowanie prawego przycisku.
                            for (Kontrolka* k = hit; k && !me.obsluzone; k = k->rodzic())
                                k->obsluz_wcisniecie_myszy(me);
                            return;
                        }

                        ustaw_przechwycenie_myszy(hit);

                        uint64_t teraz = surowe_zdarzenie.timestamp;
                        int ddx = (mx > ost_click_x_) ? (mx - ost_click_x_) : (ost_click_x_ - mx);
                        int ddy = (my > ost_click_y_) ? (my - ost_click_y_) : (ost_click_y_ - my);

                        if (teraz > 0 && ost_click_czas_ > 0 && (teraz - ost_click_czas_ < 400000000ULL || (teraz - ost_click_czas_ < 400 && teraz < 1000000000ULL)) && ddx < 5 && ddy < 5) {
                            // Podwojne klikniecie jest dodatkowa semantyka
                            // drugiego DOWN, a nie jego zamiennikiem. Kontrolki
                            // aktywowane na DOWN/UP (np. PoleWyboru) musza byc
                            // uzbrojone takze przy drugim kliknieciu.
                            hit->obsluz_wcisniecie_myszy(me);
                            ZdarzenieMyszki podwojne = me;
                            podwojne.typ = TypZdarzenia::PodwojneKlikniecie;
                            podwojne.liczba_klikniec = 2;
                            hit->obsluz_podwojne_klikniecie(podwojne);
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
                if (popupy_.czy_otwarty() || popupy_.czy_przechwytuje_mysz()) {
                    ZdarzenieMyszki pe;
                    pe.typ = TypZdarzenia::ZwolnienieMyszy;
                    pe.x = mx; pe.y = my;
                    pe.przycisk = static_cast<int>(
                        surowe_zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_UP
                        ? PrzyciskMyszy::Prawy : PrzyciskMyszy::Lewy);
                    pe.modyfikatory = surowe_zdarzenie.przyciski;
                    pe.timestamp = surowe_zdarzenie.timestamp;
                    if (popupy_.obsluz_zwolnienie(pe)) return;
                }
                // Zwolnienie przycisku zawsze kończy systemowe i lokalne capture.
                gui_ustaw_capture_myszy(false);
                if (czy_zmiana_rozmiaru_) {
                    zakoncz_zmiane_rozmiaru();
                    break;
                }
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
                if (popupy_.czy_otwarty()) {
                    ZdarzenieRolki pre;
                    pre.typ = TypZdarzenia::RolkaMyszy;
                    pre.x = mx;
                    pre.y = my;
                    pre.delta_x = surowe_zdarzenie.dx;
                    pre.delta_y = surowe_zdarzenie.dy;
                    pre.modyfikatory = surowe_zdarzenie.kod;
                    if (popupy_.obsluz_rolke(pre)) break;
                }
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
                bool fokus_byl_na_sciezce = false;
                for (Kontrolka* k = cel; k && !re.obsluzone; k = k->rodzic()) {
                    if (k == kontrolka_z_fokusem_) fokus_byl_na_sciezce = true;
                    k->obsluz_rolke(re);
                }
                if (!re.obsluzone && kontrolka_z_fokusem_ &&
                    !fokus_byl_na_sciezce) {
                    kontrolka_z_fokusem_->obsluz_rolke(re);
                }
                if (kontrolka_z_fokusem_ && !kontrolka_z_fokusem_->czy_efektywnie_widoczna()) {
                    Kontrolka* k = kontrolka_z_fokusem_->rodzic();
                    while (k && (!k->czy_efektywnie_widoczna() || !k->czy_moze_miec_fokus())) k = k->rodzic();
                    ustaw_fokus(k);
                }
                break;
            }

            case BWS_ZDARZENIE_TEXT_INPUT: {
                // Sterownik publikuje TEXT_INPUT rowniez dla litery wcisnietej
                // z Ctrl. Skrot zostal juz obsluzony przez zdarzenie fizyczne;
                // nie wolno potem wstawic literalnego C/X/V/A/Z/Y. Ctrl+Alt
                // pozostaje dozwolone dla ukladow uzywajacych AltGr.
                if (bws_mod_ctrl_skrotu(surowe_zdarzenie.przyciski) &&
                    (surowe_zdarzenie.przyciski & BWS_MOD_ALT) == 0) break;
                if (popupy_.czy_otwarty()) {
                    ZdarzenieWprowadzaniaTekstu pe;
                    pe.punkt_kodowy = surowe_zdarzenie.kod;
                    pe.modyfikatory = surowe_zdarzenie.przyciski;
                    if (popupy_.obsluz_tekst(pe)) break;
                }
                if (kontrolka_z_fokusem_) {
                    ZdarzenieWprowadzaniaTekstu te;
                    te.punkt_kodowy = surowe_zdarzenie.kod;
                    te.modyfikatory = surowe_zdarzenie.przyciski;
                    for (Kontrolka* k = kontrolka_z_fokusem_;
                         k && !te.obsluzone; k = k->rodzic()) {
                        k->obsluz_wprowadzenie_tekstu(te);
                    }
                }
                break;
            }

            case BWS_ZDARZENIE_KLAWISZ:
            case BWS_ZDARZENIE_KLAWISZ_FIZYCZNY: {
                bool fizyczny = surowe_zdarzenie.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;
                if(echo_klawiatury(surowe_zdarzenie))break;
                const uint32_t polecenie=klawisz_polecenia(surowe_zdarzenie);
                if(!polecenie)break;
                bool wcisniety = !fizyczny || surowe_zdarzenie.x != 0;
                uint32_t kod_bazowy = surowe_zdarzenie.kod & 0xFFU;
                // Tylko LEWY Alt aktywuje pasek menu. Prawy Alt (E0 38 / USB
                // 230) to AltGr ukladu Polski Programisty: przeniesienie fokusu
                // do MenuGlowne gubilo nastepujacy TEXT_INPUT (ą, ć, ę, ...).
                bool klawisz_alt = !(surowe_zdarzenie.przyciski & BWS_MOD_ALTGR) &&
                                   (((surowe_zdarzenie.kod & BWS_KEY_SOURCE_USB) &&
                                     kod_bazowy == 226U) ||
                                    (!(surowe_zdarzenie.kod & BWS_KEY_SOURCE_USB) &&
                                     !(surowe_zdarzenie.kod & BWS_KEY_SOURCE_E0) &&
                                     ((surowe_zdarzenie.kod & 0x7FU) == 0x38U)));
                if (fizyczny && wcisniety && klawisz_alt && kontrolka_glowna_) {
                    Kontrolka* cel_alt = kontrolka_glowna_->kontrolka_aktywacji_alt();
                    if (cel_alt) { ustaw_fokus(cel_alt); break; }
                }
                bool tab = polecenie == 9;
                // Ctrl+Tab nalezy do kontenera kart, nie do globalnej
                // nawigacji fokusu.
                if (wcisniety && tab && !popupy_.czy_otwarty() &&
                    (surowe_zdarzenie.przyciski & BWS_MOD_CTRL) == 0) {
                    bool shift = (surowe_zdarzenie.przyciski & BWS_MOD_SHIFT) != 0;
                    nastepny_fokus(shift);
                    break;
                }

                ZdarzenieKlawiatury ke;
                ke.typ = TypZdarzenia::KlawiszWcisniety;
                ke.kod_klawisza = polecenie;
                ke.kod_skanu = fizyczny ? surowe_zdarzenie.kod|KLAWISZ_FIZYCZNY_GUI : 0;
                ke.modyfikatory = surowe_zdarzenie.przyciski;
                if (wcisniety && popupy_.czy_otwarty() && popupy_.obsluz_klawisz(ke)) break;
                if (kontrolka_z_fokusem_) {
                    if (wcisniety) {
                        for (Kontrolka* k = kontrolka_z_fokusem_;
                             k && !ke.obsluzone; k = k->rodzic()) {
                            k->obsluz_wcisniecie_klawisza(ke);
                        }
                    } else {
                        ke.typ = TypZdarzenia::KlawiszZwolniony;
                        for (Kontrolka* k = kontrolka_z_fokusem_;
                             k && !ke.obsluzone; k = k->rodzic()) {
                            k->obsluz_zwolnienie_klawisza(ke);
                        }
                    }
                }
                if (wcisniety && !ke.obsluzone && czy_klawisz_menu_kontekstowego(ke)) {
                    if (otworz_menu_kontekstowe(kontrolka_z_fokusem_, 0, 0, true))
                        ke.obsluzone = true;
                }
                // Skroty aplikacji sa rozpatrywane dopiero po kontrolce z
                // fokusem. Ctrl+C/V/X pozostaja dzieki temu w edytorze tekstu,
                // jesli edytor potrafi je wykonac.
                if (wcisniety && !ke.obsluzone && lista_polecen_ &&
                    lista_polecen_->wykonaj_skrot(surowe_zdarzenie.kod, ke.modyfikatory))
                    ke.obsluzone = true;
                break;
            }

            case BWS_ZDARZENIE_FOCUS: {
                czy_aktywne_ = true;
                if (czy_zminimalizowane_) {
                    czy_zminimalizowane_ = false;
                    przelicz_uklad();
                }
                // Powrot fokusu okna (np. po zamknieciu okna podrzednego albo
                // aktywacji z paska zadan) przywraca fokus kontrolki sprzed
                // BLUR; inaczej klawiatura nie trafia do edytora az do klikniecia.
                // Wskaznik jest uzywany tylko, gdy nadal jest w drzewie okna.
                if (!kontrolka_z_fokusem_ && fokus_przed_blur_ &&
                    czy_w_drzewie(kontrolka_glowna_, fokus_przed_blur_) &&
                    fokus_przed_blur_->czy_efektywnie_widoczna() &&
                    fokus_przed_blur_->czy_moze_miec_fokus()) {
                    ustaw_fokus(fokus_przed_blur_);
                }
                fokus_przed_blur_ = nullptr;
                czy_brudne_ = true;
                break;
            }

            case BWS_ZDARZENIE_BLUR: {
                popupy_.zamknij(PowodZamknieciaPopup::KlikPoza);
                czy_aktywne_ = false;
                zwolnij_przechwycenie_myszy();
                if (kontrolka_z_fokusem_)
                    fokus_przed_blur_ = (kontrolka_z_fokusem_->czy_pasek_menu() && fokus_przed_menu_)
                        ? fokus_przed_menu_ : kontrolka_z_fokusem_;
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

    void lista_polecen_zniszczona(ListaPolecen& lista) override {
        if (lista_polecen_ == &lista) lista_polecen_ = nullptr;
    }

    static bool czy_klawisz_menu_kontekstowego(const ZdarzenieKlawiatury& e) {
        bool usb = (e.kod_klawisza & BWS_KEY_SOURCE_USB) != 0;
        uint32_t baza = usb ? (e.kod_klawisza & 0xFFU) : (e.kod_klawisza & 0x7FU);
        bool menu = (usb && baza == 101U) ||
                    (!usb && (e.kod_klawisza & BWS_KEY_SOURCE_E0) != 0 && baza == 0x5DU);
        bool f10 = (usb && baza == 67U) || (!usb && baza == 0x44U);
        return menu || (f10 && e.czy_shift());
    }

    // Porownanie adresow bez dereferencji szukanego wskaznika.
    static bool czy_w_drzewie(Kontrolka* k, const Kontrolka* szukana) {
        if (!k || !szukana) return false;
        if (k == szukana) return true;
        for (size_t i = 0; i < k->liczba_dzieci(); ++i)
            if (czy_w_drzewie(k->dziecko(i), szukana)) return true;
        return false;
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

protected:
    char tytul_[64]{0};
    int x_{100};
    int y_{100};
    int szerokosc_{480};
    int wysokosc_{320};
    int logiczna_szerokosc_{480};
    int logiczna_wysokosc_{320};
    SkalaUI skala_dpi_{1};

    bool czy_aktywne_{true};
    bool czy_zmaksymalizowane_{false};
    bool czy_zminimalizowane_{false};
    bool czy_przeciagane_{false};
    int przesuniecie_ciagniecia_x_{0};
    int przesuniecie_ciagniecia_y_{0};
    bool czy_zmiana_rozmiaru_{false};
    bool zmiana_rozmiaru_dozwolona_{true};
    uint8_t krawedzie_zmiany_{0};
    uint32_t kursor_krawedzi_{0};
    OknoGeometria::Prost geometria_startowa_{0, 0, 0, 0};
    OknoGeometria::Prost oczekujaca_geometria_{0, 0, 0, 0};
    bool ma_oczekujaca_geometrie_{false};
    int start_zmiany_x_{0};
    int start_zmiany_y_{0};
    int min_logiczna_szerokosc_{0};
    int min_logiczna_wysokosc_{0};
    uint32_t* bufor_regionu_{nullptr};
    size_t pojemnosc_regionu_{0};
    bool czy_brudne_{true};
    Prostokat brudny_obszar_klienta_{0, 0, 0, 0};
    Prostokat ostatni_obszar_odrysowania_{0, 0, 0, 0};
    Prostokat zapisana_geometria_{100, 100, 480, 320};

    uint64_t ost_click_czas_{0};
    int ost_click_x_{0};
    int ost_click_y_{0};

    uint64_t liczba_zdarzen_ruchu_myszy_{0};
    uint64_t liczba_przejsc_ukladu_{0};
    uint64_t liczba_odrysowan_{0};
    uint64_t liczba_pelnych_odrysowan_{0};
    uint64_t liczba_brudnych_pikseli_{0};

    Kontrolka* kontrolka_glowna_{nullptr};
    Panel* domyslny_korzen_{nullptr};
    Kontrolka* kontrolka_z_fokusem_{nullptr};
    Kontrolka* fokus_przed_blur_{nullptr};   // tylko do przywrocenia przy FOCUS
    Kontrolka* fokus_przed_menu_{nullptr};   // kontrolka sprzed wejscia na pasek menu
    bool menu_bylo_otwarte_{false};
    Kontrolka* kontrolka_najechana_{nullptr};
    Kontrolka* kontrolka_przechwytujaca_{nullptr};
    ListaPolecen* lista_polecen_{nullptr};
    ZarzadcaPopupow popupy_;
};

} // namespace BursztynUI
