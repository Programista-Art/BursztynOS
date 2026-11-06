#pragma once

#include "bursztyn_gui.h"
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "klient.h"

#ifndef BURSZTYN_DIAG_BOOT
#define BURSZTYN_DIAG_BOOT 0
#endif

namespace BursztynUI {

inline void tekst_liczba(char* out, size_t cap, const char* prefix, uint32_t a, const char* suffix = "") {
    if (!out || cap == 0) return;
    size_t p = 0;
    while (*prefix && p + 1 < cap) out[p++] = *prefix++;
    char r[10]; unsigned i = 0;
    do { r[i++] = char('0' + a % 10); a /= 10; } while (a);
    while (i && p + 1 < cap) out[p++] = r[--i];
    while (*suffix && p + 1 < cap) out[p++] = *suffix++;
    out[p] = '\0';
}

class PasekDolnyEkranu : public Panel {
public:
    PasekDolnyEkranu() : Panel(Orientacja::Pozioma) {
        ustaw_uklad_automatyczny(false);
        ustaw_kolor_tla(0);
        ustaw_grubosc_ramki(0);
        ustaw_wypelnienie(Marginesy(0));
    }

    void ustaw_panele(Panel* akcje, Panel* potwierdzenie) {
        panel_akcji_ = akcje;
        panel_potwierdzenia_ = potwierdzenie;
        dodaj_kontrolke(akcje);
        dodaj_kontrolke(potwierdzenie);
    }

    void rozmiesc(Prostokat r) override {
        obszar_ = r;
        Punkt rodzic_poz = rodzic_ ? rodzic_->obszar_ekranu().pozycja() : Punkt(0, 0);
        obszar_ekranu_ = Prostokat(rodzic_poz + obszar_.pozycja(), obszar_.rozmiar());

        if (panel_akcji_) panel_akcji_->rozmiesc(Prostokat(0, 0, r.szerokosc, r.wysokosc));
        if (panel_potwierdzenia_) panel_potwierdzenia_->rozmiesc(Prostokat(0, 0, r.szerokosc, r.wysokosc));
    }

private:
    Panel* panel_akcji_{nullptr};
    Panel* panel_potwierdzenia_{nullptr};
};

class PanelGlownyEkranu : public Panel {
public:
    PanelGlownyEkranu() : Panel(Orientacja::Pionowa) {
        ustaw_uklad_automatyczny(false);
        ustaw_kolor_tla(0);
        ustaw_grubosc_ramki(0);
        ustaw_wypelnienie(Marginesy(0));
    }

    void ustaw_elementy(WidokPrzewijany* w, Panel* p) {
        widok_ = w;
        pasek_ = p;
        dodaj_kontrolke(widok_);
        dodaj_kontrolke(pasek_);
    }

    void rozmiesc(Prostokat r) override {
        obszar_ = r;
        Punkt rodzic_poz = rodzic_ ? rodzic_->obszar_ekranu().pozycja() : Punkt(0, 0);
        obszar_ekranu_ = Prostokat(rodzic_poz + obszar_.pozycja(), obszar_.rozmiar());

        if (!widok_ || !pasek_) return;

        const uint32_t p = ui_skala_procent;
        int h_pasek = ui_skaluj(EKRAN_PASEK_PRZYCISKOW_H, p);
        if (h_pasek > r.wysokosc) h_pasek = r.wysokosc;
        int bar_y = r.wysokosc - h_pasek;
        if (bar_y < 0) bar_y = 0;
        int widok_h = bar_y;

        widok_->rozmiesc(Prostokat(0, 0, r.szerokosc, widok_h));
        pasek_->rozmiesc(Prostokat(0, bar_y, r.szerokosc, h_pasek));
    }

private:
    WidokPrzewijany* widok_{nullptr};
    Panel* pasek_{nullptr};
};

class OknoEkranu : public Okno {
public:
    OknoEkranu() : Okno("Ustawienia ekranu", EKRAN_OKNO_SZEROKOSC, EKRAN_OKNO_NORMALNE_H) {}

    ~OknoEkranu() override {
        if (stan_.transakcja != 0) {
            potwierdz_akcje(reinterpret_cast<void*>(1));
        }
        popupy_.zamknij(PowodZamknieciaPopup::WlascicielZamkniety);
        kontrolka_z_fokusem_ = nullptr;
        kontrolka_przechwytujaca_ = nullptr;
        if (glowny_panel_) {
            delete glowny_panel_;
            glowny_panel_ = nullptr;
            kontrolka_glowna_ = nullptr;
        }
    }

    void zamknij() override {
        if (stan_.transakcja != 0) {
            potwierdz_akcje(reinterpret_cast<void*>(1));
        }
        Okno::zamknij();
    }

    void ustaw_endpoint(uint64_t ep) { endpoint_ = ep; }
    uint64_t endpoint() const { return endpoint_; }

    const EkranOdpowiedz& stan() const { return stan_; }
    void ustaw_stan(const EkranOdpowiedz& s) { stan_ = s; }

    int pending_tryb() const { return pending_tryb_; }
    void ustaw_pending_tryb(int idx) { pending_tryb_ = idx; }

    int pending_skala() const { return pending_skala_; }
    void ustaw_pending_skala(int idx) { pending_skala_ = idx; }

    PanelGlownyEkranu& panel_glowny() { return *glowny_panel_; }
    const PanelGlownyEkranu& panel_glowny() const { return *glowny_panel_; }

    PasekDolnyEkranu& pasek_dolny() { return *pasek_dolny_; }
    const PasekDolnyEkranu& pasek_dolny() const { return *pasek_dolny_; }

    WidokPrzewijany& widok_glowny() { return *widok_glowny_; }
    const WidokPrzewijany& widok_glowny() const { return *widok_glowny_; }

    Panel& panel_akcji() { return *panel_akcji_; }
    const Panel& panel_akcji() const { return *panel_akcji_; }

    Panel& panel_potwierdzenia() { return *panel_potwierdzenia_; }
    const Panel& panel_potwierdzenia() const { return *panel_potwierdzenia_; }

    Etykieta& etykieta_potwierdzenia() { return *potwierdzenie_; }
    Etykieta& etykieta_info() { return *info_; }
    ListaRozwijana& lista_trybow() { return *tryby_; }
    ListaRozwijana& lista_skal() { return *skale_; }

    Przycisk& przycisk_zastosuj() { return *zastosuj_btn_; }
    Przycisk& przycisk_anuluj() { return *anuluj_btn_; }
    Przycisk& przycisk_domyslne() { return *domyslne_btn_; }
    Przycisk& przycisk_zachowaj() { return *zachowaj_btn_; }
    Przycisk& przycisk_przywroc() { return *przywroc_btn_; }

    void inicjalizuj(const EkranOdpowiedz& st, uint64_t ep = 0) {
        if (glowny_panel_) {
            delete glowny_panel_;
            glowny_panel_ = nullptr;
            kontrolka_glowna_ = nullptr;
        }
        stan_ = st;
        endpoint_ = ep;

        glowny_panel_ = new PanelGlownyEkranu();
        ustaw_kontrolke_glowna(glowny_panel_);

        widok_glowny_ = new WidokPrzewijany();
        widok_glowny_->ustaw_kolor_tla(0);
        widok_glowny_->ustaw_grubosc_ramki(0);
        widok_glowny_->ustaw_tryb_paska_poziomego(TrybPaskaPrzewijania::Nigdy);
        widok_glowny_->ustaw_tryb_paska_pionowego(TrybPaskaPrzewijania::Automatycznie);
        widok_glowny_->ustaw_rozmiar_zawartosci(
            Rozmiar(1, ui_skaluj(EKRAN_TRESC_NORMALNA_H, ui_skala_procent)));

        auto* root = &widok_glowny_->zawartosc();
        root->ustaw_uklad_automatyczny(true);
        root->ustaw_kolor_tla(0);
        root->ustaw_grubosc_ramki(0);
        root->ustaw_wypelnienie(Marginesy(12, 10, 12, 10));
        root->ustaw_odstep(6);

        root->dodaj<Etykieta>("Monitor główny");
        backend_ = &root->dodaj<Etykieta>(
            stan_.mozliwosci.backend == EKRAN_BOCHS ? "Sterownik: Bochs VBE" :
            stan_.mozliwosci.backend == EKRAN_GOP ? "Sterownik: UEFI GOP" :
            stan_.mozliwosci.backend == EKRAN_VIRTIO_GPU ? "Sterownik: VirtIO-GPU (runtime)" : "Sterownik: VESA");
        root->dodaj<Etykieta>("Rozdzielczość");
        tryby_ = &root->dodaj<ListaRozwijana>();
        tryby_->ustaw_preferowany_rozmiar(Rozmiar(-1, 28));
        tryby_->przy_zmianie_wyboru([](ListaRozwijana*, int idx, void* dane) {
            auto* o = static_cast<OknoEkranu*>(dane);
            if (o) o->pending_tryb_ = idx;
        }, this);
        for (unsigned i = 0; i < stan_.liczba_trybow; ++i) {
            const auto& t = stan_.tryby[i]; char b[80]{};
            if (ekran_formatuj_tryb(b, sizeof(b), t.szerokosc, t.wysokosc, true)) {
                tryby_->dodaj_element(b);
            }
        }
        root->dodaj<Etykieta>("Skala interfejsu");
        skale_ = &root->dodaj<ListaRozwijana>();
        skale_->ustaw_preferowany_rozmiar(Rozmiar(-1, 28));
        skale_->przy_zmianie_wyboru([](ListaRozwijana*, int idx, void* dane) {
            auto* o = static_cast<OknoEkranu*>(dane);
            if (o) o->pending_skala_ = idx;
        }, this);
        const char* procenty[] = {"100%", "125%", "150%", "175%", "200%"};
        for (auto* s : procenty) skale_->dodaj_element(s);
        info_ = &root->dodaj<Etykieta>("");
        if (!stan_.mozliwosci.mozna_zmienic_runtime) {
            auto& limit = root->dodaj<Etykieta>(
                "Bieżący sterownik nie obsługuje zmiany rozdzielczości podczas pracy systemu.");
            limit.ustaw_zawijanie(true);
        }

        panel_akcji_ = new Panel(Orientacja::Pozioma);
        panel_akcji_->ustaw_uklad_automatyczny(true);
        panel_akcji_->ustaw_kolor_tla(0);
        panel_akcji_->ustaw_grubosc_ramki(0);
        panel_akcji_->ustaw_wypelnienie(Marginesy(12, 6, 12, 8));
        panel_akcji_->ustaw_odstep(8);

        zastosuj_btn_ = &panel_akcji_->dodaj<Przycisk>("Zastosuj");
        zastosuj_btn_->przy_kliknieciu([](Przycisk*, void* dane) {
            auto* o = static_cast<OknoEkranu*>(dane);
            if (o) o->zastosuj_akcje();
        }, this);
        anuluj_btn_ = &panel_akcji_->dodaj<Przycisk>("Anuluj");
        anuluj_btn_->przy_kliknieciu([](Przycisk*, void* dane) {
            auto* o = static_cast<OknoEkranu*>(dane);
            if (o) o->anuluj_akcje();
        }, this);
        domyslne_btn_ = &panel_akcji_->dodaj<Przycisk>("Przywróć");
        domyslne_btn_->przy_kliknieciu([](Przycisk*, void* dane) {
            auto* o = static_cast<OknoEkranu*>(dane);
            if (o) o->domyslne_akcje();
        }, this);

        panel_potwierdzenia_ = new Panel(Orientacja::Pozioma);
        panel_potwierdzenia_->ustaw_uklad_automatyczny(true);
        panel_potwierdzenia_->ustaw_kolor_tla(0);
        panel_potwierdzenia_->ustaw_grubosc_ramki(0);
        panel_potwierdzenia_->ustaw_wypelnienie(Marginesy(12, 6, 12, 8));
        panel_potwierdzenia_->ustaw_odstep(8);

        potwierdzenie_ = &panel_potwierdzenia_->dodaj<Etykieta>(
            "Zachować nowe ustawienia? Pozostało 10 s");
        potwierdzenie_->ustaw_wyrownanie_pionowe_tekstu(WyrownaniePionowe::Srodek);
        potwierdzenie_->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);

        zachowaj_btn_ = &panel_potwierdzenia_->dodaj<Przycisk>("Zachowaj");
        zachowaj_btn_->przy_kliknieciu([](Przycisk*, void* dane) {
            auto* o = static_cast<OknoEkranu*>(dane);
            if (o) o->potwierdz_akcje(nullptr);
        }, this);
        przywroc_btn_ = &panel_potwierdzenia_->dodaj<Przycisk>("Przywróć");
        przywroc_btn_->przy_kliknieciu([](Przycisk*, void* dane) {
            auto* o = static_cast<OknoEkranu*>(dane);
            if (o) o->potwierdz_akcje(reinterpret_cast<void*>(1));
        }, this);

        pasek_dolny_ = new PasekDolnyEkranu();
        pasek_dolny_->ustaw_panele(panel_akcji_, panel_potwierdzenia_);

        glowny_panel_->ustaw_elementy(widok_glowny_, pasek_dolny_);

        synchronizuj(true);
        dopasuj_rozmiar_i_polozenie(true);
    }

    void dopasuj_rozmiar_i_polozenie(bool reset_position = false) {
        const uint32_t p = ui_skala_procent;
        skala_dpi_ = SkalaUI::procent(p);
        // Skala musi dotrzec do dzieci ZANIM policzymy layout,
        // inaczej widok/pasek policzy sie starą skalą (stale geometry).
        popupy_.ustaw_skale_dpi(skala_dpi_);
        if (kontrolka_glowna_) kontrolka_glowna_->ustaw_skale_dpi(skala_dpi_);
        const Prostokat work = pobierz_obszar_roboczy();
        int docelowa_szer = ui_skaluj(logiczna_szerokosc_, p);
        if (docelowa_szer > work.szerokosc) docelowa_szer = work.szerokosc;
        int docelowa_wys = ui_skaluj(logiczna_wysokosc_, p);
        if (docelowa_wys > work.wysokosc) docelowa_wys = work.wysokosc;

        int nx = x();
        int ny = y();
        if (reset_position) {
            nx = work.x + (work.szerokosc - docelowa_szer) / 2;
            ny = work.y + (work.wysokosc - docelowa_wys) / 2;
        } else {
            if (nx + docelowa_szer > work.prawy()) nx = work.prawy() - docelowa_szer;
            if (nx < work.x) nx = work.x;
            if (ny + docelowa_wys > work.dolny()) ny = work.dolny() - docelowa_wys;
            if (ny < work.y) ny = work.y;
        }

        if (widok_glowny_) {
            widok_glowny_->ustaw_rozmiar_zawartosci(
                Rozmiar(1, ui_skaluj(EKRAN_TRESC_NORMALNA_H, p)));
        }

        ustaw_geometrie(nx, ny, docelowa_szer, docelowa_wys, false);
    }

    void synchronizuj(bool wybor = true) {
        if (wybor) {
            int idx = 0;
            for (unsigned i = 0; i < stan_.liczba_trybow; ++i) {
                if (ekran_ten_sam(stan_.tryby[i], stan_.aktywny)) idx = i;
            }
            pending_tryb_ = idx;
            pending_skala_ = (stan_.skala - 100) / 25;
            if (tryby_) tryby_->ustaw_wybrany_indeks(pending_tryb_);
            if (skale_) skale_->ustaw_wybrany_indeks(pending_skala_);
        }
        const bool pending = stan_.transakcja != 0;
        if (tryby_) tryby_->ustaw_wlaczona(!pending && stan_.mozliwosci.mozna_zmienic_runtime);
        if (skale_) skale_->ustaw_wlaczona(!pending);
        if (zastosuj_btn_) zastosuj_btn_->ustaw_wlaczona(!pending);
        if (domyslne_btn_) domyslne_btn_->ustaw_wlaczona(!pending);

        char b[160]{};
        char tryb[48]{};
        ekran_formatuj_tryb(tryb, sizeof(tryb), stan_.aktywny.szerokosc, stan_.aktywny.wysokosc, false);
        size_t p = 0;
        const char* prefix = "Bieżąca rozdzielczość: ";
        while (prefix[p] && p + 1 < sizeof(b)) { b[p] = prefix[p]; ++p; }
        for (size_t i = 0; tryb[i] && p + 1 < sizeof(b); ++i) b[p++] = tryb[i];
        b[p] = 0;
        if (info_) info_->ustaw_tekst(b);

        if (panel_akcji_) panel_akcji_->ustaw_widoczna(!pending);
        if (panel_potwierdzenia_) panel_potwierdzenia_->ustaw_widoczna(pending);
        if (pending && potwierdzenie_) {
            char txt_buf[96]{};
            tekst_liczba(txt_buf, sizeof(txt_buf), "Zachować nowe ustawienia? Pozostało ", stan_.pozostalo_sekund, " s");
            potwierdzenie_->ustaw_tekst(txt_buf);
        }
        if (pasek_dolny_) {
            pasek_dolny_->rozmiesc(pasek_dolny_->obszar());
        }
        uniewaznij();
    }

    void zastosuj_akcje() {
        const int i = pending_tryb_, s = pending_skala_;
        if (i < 0 || i >= int(stan_.liczba_trybow) || s < 0 || s > 4) return;
        EkranZadanie r{};
        r.operacja = EKRAN_ZASTOSUJ;
        r.szerokosc = stan_.tryby[i].szerokosc;
        r.wysokosc = stan_.tryby[i].wysokosc;
        r.bpp = 32;
        r.skala = 100 + s * 25;
        if (!endpoint_ || !ekran_zapytaj(endpoint_, r, &stan_)) {
            if (info_) info_->ustaw_tekst("Brak odpowiedzi usługi ekranu.");
            return;
        }
        synchronizuj(false);
        wynik_status(stan_.status);
    }

    void potwierdz_akcje(void* dane) {
        if (!endpoint_) return;
        EkranZadanie r{};
        r.operacja = dane ? EKRAN_PRZYWROC : EKRAN_ZACHOWAJ;
        r.transakcja = stan_.transakcja;
        if (ekran_zapytaj(endpoint_, r, &stan_)) {
            synchronizuj(true);
            wynik_status(stan_.status);
        }
    }

    void anuluj_akcje() {
        if (stan_.transakcja) {
            potwierdz_akcje(reinterpret_cast<void*>(1));
            return;
        }
        int idx = 0;
        for (unsigned i = 0; i < stan_.liczba_trybow; ++i) {
            if (ekran_ten_sam(stan_.tryby[i], stan_.aktywny)) idx = i;
        }
        int s = (stan_.skala - 100) / 25;
        if (pending_tryb_ != idx || pending_skala_ != s) {
            synchronizuj(true);
            return;
        }
        zamknij();
    }

    void domyslne_akcje() {
        int idx = 0;
        for (unsigned i = 0; i < stan_.liczba_trybow; ++i) {
            if (stan_.tryby[i].szerokosc == 1280 && stan_.tryby[i].wysokosc == 720) {
                idx = i;
                break;
            }
        }
        pending_tryb_ = idx;
        pending_skala_ = 0;
        if (tryby_) tryby_->ustaw_wybrany_indeks(pending_tryb_);
        if (skale_) skale_->ustaw_wybrany_indeks(pending_skala_);
    }

    void wynik_status(uint32_t status) {
        if (status == EKRAN_OK || status == EKRAN_OCZEKUJE) return;
        if (!info_) return;
        info_->ustaw_tekst(status == EKRAN_PAMIEC ? "Za mało pamięci na bufory ekranu." :
            status == EKRAN_ZAPIS ? "Nie udało się zapisać ustawień." :
            status == EKRAN_NIEOBSLUGIWANY ? "Ten tryb nie jest obsługiwany przez sterownik." :
            status == EKRAN_ZAJETY ? "Trwa potwierdzanie innej zmiany." : "Nie udało się zastosować ustawień.");
    }

    void log_diagnostyka(const char* phase) {
#if BURSZTYN_DIAG_BOOT
        char buf[340]{};
        size_t pos = 0;
        auto str = [&](const char* s) {
            while (s && *s && pos + 1 < sizeof(buf)) buf[pos++] = *s++;
        };
        auto num = [&](int val) {
            if (val < 0) {
                if (pos + 1 < sizeof(buf)) buf[pos++] = '-';
                val = -val;
            }
            char rev[16];
            size_t ile = 0;
            uint32_t uval = static_cast<uint32_t>(val);
            do {
                rev[ile++] = static_cast<char>('0' + (uval % 10));
                uval /= 10;
            } while (uval != 0 && ile < sizeof(rev));
            while (ile > 0 && pos + 1 < sizeof(buf)) {
                buf[pos++] = rev[--ile];
            }
        };
        str("[EKRAN GUI GEOM] phase="); str(phase);
        str(" win=("); num(x_); str(","); num(y_); str(","); num(szerokosc_); str(","); num(wysokosc_);
        Prostokat kl = obszar_klienta();
        str(") client=("); num(kl.szerokosc); str(","); num(kl.wysokosc);
        int vx = 0, vy = 0, vw = 0, vh = 0;
        if (gui_czy_izolacja_geometrii_wlaczona())
            gui_pobierz_izolowana_geometrie(&vx, &vy, &vw, &vh);
        else
            gui_pobierz_widoczna_geometrie(&vx, &vy, &vw, &vh);
        str(") layer=("); num(vx); str(","); num(vy); str(","); num(vw); str(","); num(vh);
        str(") surf=("); num(szerokosc_); str(","); num(wysokosc_);
        Prostokat ro = glowny_panel_ ? glowny_panel_->obszar() : Prostokat();
        str(") root=("); num(ro.szerokosc); str(","); num(ro.wysokosc);
        Prostokat sc = widok_glowny_ ? widok_glowny_->obszar() : Prostokat();
        str(") scroll=("); num(sc.x); str(","); num(sc.y); str(","); num(sc.szerokosc); str(","); num(sc.wysokosc);
        Prostokat ba = pasek_dolny_ ? pasek_dolny_->obszar() : Prostokat();
        str(") bar=("); num(ba.x); str(","); num(ba.y); str(","); num(ba.szerokosc); str(","); num(ba.wysokosc);
        int ch = widok_glowny_ ? widok_glowny_->rozmiar_zawartosci().wysokosc : 0;
        str(") content_h="); num(ch);
        int vh_sc = widok_glowny_ ? widok_glowny_->prostokat_widoku().wysokosc : 0;
        str(" view_h="); num(vh_sc);
        int smax = widok_glowny_ ? static_cast<int>(widok_glowny_->pasek_pionowy().maksimum()) : 0;
        str(" scroll_max="); num(smax);
        Prostokat wo = pobierz_obszar_roboczy();
        str(" work=("); num(wo.x); str(","); num(wo.y); str(","); num(wo.szerokosc); str(","); num(wo.wysokosc);
        str(") scale="); num(static_cast<int>(ui_skala_procent));
        str("\n");
        buf[pos] = '\0';
        bws_wywolaj(1, reinterpret_cast<uint64_t>(buf), 0, 0, 0);
#else
        (void)phase;
#endif
    }

    void obsluz_zdarzenie(const bws_zdarzenie& e) override {
        if (stan_.transakcja != 0) {
            bool wcisniety = (e.typ == BWS_ZDARZENIE_KLAWISZ) ||
                             (e.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY && e.x != 0);
            if (wcisniety) {
                uint32_t kod = e.kod;
                bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
                uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);
                bool enter = (kod == 13U || kod == 10U || (usb && baza == 40U) || (!usb && baza == 0x1CU));
                bool escape = (kod == 27U || (usb && baza == 41U) || (!usb && baza == 0x01U));
                if (enter) {
                    potwierdz_akcje(nullptr);
                    return;
                }
                if (escape) {
                    potwierdz_akcje(reinterpret_cast<void*>(1));
                    return;
                }
            }
        }
        if (e.typ == BWS_ZDARZENIE_EKRAN) {
            // Kolejnosc musi byc: tryb fizyczny -> BWS62 -> clamp okna ->
            // warstwa BWS/surface -> layout z NOWEGO client size -> repaint.
            // Nie wolno layoutowac dzieci starą geometrią ani ruszać warstwy po layoucie.
            log_diagnostyka("before");
            GuiMonitorInfo current{};
            const uint32_t scale=gui_pobierz_monitor(0,&current)?current.scale_x1000/10:e.kod;
            if (ui_skala_poprawna(scale)) {
                ui_skala_procent = scale;
                skala_dpi_ = SkalaUI::procent(scale);
            }
            // Skala do dzieci przed layoutem (nie zmienia jeszcze geometrii okna).
            popupy_.ustaw_skale_dpi(skala_dpi_);
            if (kontrolka_glowna_) kontrolka_glowna_->ustaw_skale_dpi(skala_dpi_);
            // UWAGA: celowo NIE przepisujemy x_/y_/szer_/wys_ z e.x/e.y/e.dx/e.dy.
            // Pola zdarzenia niosą starą geometrię warstwy / stary rozmiar ekranu
            // i ich użycie odtwarza regresję clipowania po downscale.
            log_diagnostyka("after-bws62");
            // 1. Clamp outer geometry do NOWEGO work area, z logicznego rozmiaru.
            //    Logiczny rozmiar preferowany zostaje nietknięty (rozrost powrotny).
            {
                const uint32_t p = ui_skala_procent;
                const Prostokat work = pobierz_obszar_roboczy();
                int docelowa_szer = ui_skaluj(logiczna_szerokosc_, p);
                if (docelowa_szer > work.szerokosc) docelowa_szer = work.szerokosc;
                int docelowa_wys = ui_skaluj(logiczna_wysokosc_, p);
                if (docelowa_wys > work.wysokosc) docelowa_wys = work.wysokosc;
                if (docelowa_szer < 1) docelowa_szer = 1;
                if (docelowa_wys < 1) docelowa_wys = 1;
                int nx = x_;
                int ny = y_;
                if (nx + docelowa_szer > work.prawy()) nx = work.prawy() - docelowa_szer;
                if (nx < work.x) nx = work.x;
                if (ny + docelowa_wys > work.dolny()) ny = work.dolny() - docelowa_wys;
                if (ny < work.y) ny = work.y;
                x_ = nx;
                y_ = ny;
                szerokosc_ = docelowa_szer;
                wysokosc_ = docelowa_wys;
                if (widok_glowny_) {
                    widok_glowny_->ustaw_rozmiar_zawartosci(
                        Rozmiar(1, ui_skaluj(EKRAN_TRESC_NORMALNA_H, p)));
                }
            }
            log_diagnostyka("after-window-clamp");
            // 2. Warstwa BWS / presentation surface PRZED layoutem dzieci.
            if (!g_tlum_operacje_systemowe) {
                gui_ustaw_geometrie_okna(x_, y_, szerokosc_, wysokosc_,
                                         czy_zmaksymalizowane_ ? 2 : 1);
            }
            log_diagnostyka("after-surface-resize");
            // 3. Layout z NOWEGO client size (pasek dolny dockuje bary=clienth-barh).
            brudny_obszar_klienta_ = Prostokat();
            przelicz_uklad();
            czy_brudne_ = true;
            log_diagnostyka("after-layout");

            if (endpoint_) {
                EkranZadanie r{}; r.operacja = EKRAN_STAN;
                if (ekran_zapytaj(endpoint_, r, &stan_)) synchronizuj(!stan_.transakcja);
            }
            return;
        }
        Okno::obsluz_zdarzenie(e);
        if (e.typ == BWS_ZDARZENIE_TIMER && stan_.transakcja && endpoint_) {
            EkranZadanie r{}; r.operacja = EKRAN_STAN;
            if (ekran_zapytaj(endpoint_, r, &stan_)) synchronizuj(!stan_.transakcja);
        }
    }

private:
    uint64_t endpoint_{0};
    EkranOdpowiedz stan_{};
    int pending_tryb_{-1};
    int pending_skala_{-1};

    PanelGlownyEkranu* glowny_panel_{nullptr};
    WidokPrzewijany* widok_glowny_{nullptr};
    PasekDolnyEkranu* pasek_dolny_{nullptr};
    Panel* panel_akcji_{nullptr};
    Panel* panel_potwierdzenia_{nullptr};

    ListaRozwijana* tryby_{nullptr};
    ListaRozwijana* skale_{nullptr};
    Etykieta* info_{nullptr};
    Etykieta* backend_{nullptr};
    Etykieta* potwierdzenie_{nullptr};

    Przycisk* zastosuj_btn_{nullptr};
    Przycisk* anuluj_btn_{nullptr};
    Przycisk* domyslne_btn_{nullptr};
    Przycisk* zachowaj_btn_{nullptr};
    Przycisk* przywroc_btn_{nullptr};
};

} // namespace BursztynUI
