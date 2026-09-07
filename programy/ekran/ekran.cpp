/* Ustawienia ekranu: pojedyncze okno, prawdziwe mozliwosci HAL przez BWS69/70. */
#include "../../loader.h"
#include "../../bursztyn_ui/bursztyn_ui.h"

using namespace BursztynUI;

extern "C" void _start();
static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 0x1000ULL;
static constexpr uint64_t BUR_TEKST_ROZMIAR = 0x30000ULL;
static constexpr uint64_t BUR_TEKST_WIRTUALNY = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE = 0x31000ULL;
static constexpr uint64_t BUR_DANE_ROZMIAR = 0x20000ULL;
static constexpr uint64_t BUR_DANE_WIRTUALNY = 0x1031000ULL;

extern "C" {
__attribute__((section(".naglowek"), used)) NaglowekBur naglowek = {
    {'B','U','R','\0'}, (uint64_t)&_start,
    BUR_TEKST_PRZESUNIECIE, BUR_TEKST_ROZMIAR, BUR_TEKST_WIRTUALNY,
    BUR_DANE_PRZESUNIECIE, BUR_DANE_ROZMIAR, BUR_DANE_WIRTUALNY
};
}
static_assert(sizeof(NaglowekBur) == 60U, "Naglowek .bur musi miec 60 bajtow");

#include "klient.h"
namespace {
uint64_t endpoint = 0;
EkranOdpowiedz stan{};
ListaRozwijana *tryby = nullptr, *skale = nullptr;
Etykieta *info = nullptr, *backend = nullptr, *potwierdzenie = nullptr;
Panel *panel_akcji = nullptr;
Panel *panel_potwierdzenia = nullptr;
Okno *okno_glowne = nullptr;
WidokPrzewijany *widok_glowny = nullptr;
Przycisk *zastosuj_btn = nullptr, *anuluj_btn = nullptr, *domyslne_btn = nullptr;
Przycisk *zachowaj_btn = nullptr, *przywroc_btn = nullptr;
int pending_tryb = -1, pending_skala = -1;

constexpr int OKNO_SZEROKOSC = EKRAN_OKNO_SZEROKOSC;
constexpr int TRESC_NORMALNA_H = EKRAN_TRESC_NORMALNA_H;
constexpr int TRESC_POTWIERDZENIE_H = EKRAN_TRESC_POTWIERDZENIE_H;
constexpr int PASEK_PRZYCISKOW_H = EKRAN_PASEK_PRZYCISKOW_H;
constexpr int OKNO_NORMALNE_H = EKRAN_OKNO_NORMALNE_H;
constexpr int OKNO_POTWIERDZENIE_H = EKRAN_OKNO_POTWIERDZENIE_H;

class PasekDolnyEkranu;
class PanelGlownyEkranu;
class OknoEkranu;
void synchronizuj(bool wybor);
void potwierdz(Przycisk*, void* dane);

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
        int h_pasek = ui_skaluj(PASEK_PRZYCISKOW_H, p);
        if (h_pasek > r.wysokosc) h_pasek = r.wysokosc;
        int h_widok = r.wysokosc - h_pasek;
        if (h_widok < 0) h_widok = 0;

        widok_->rozmiesc(Prostokat(0, 0, r.szerokosc, h_widok));
        pasek_->rozmiesc(Prostokat(0, h_widok, r.szerokosc, h_pasek));
    }

private:
    WidokPrzewijany* widok_{nullptr};
    Panel* pasek_{nullptr};
};

class OknoEkranu : public Okno {
public:
    OknoEkranu() : Okno("Ustawienia ekranu", OKNO_SZEROKOSC, OKNO_NORMALNE_H) {}

    ~OknoEkranu() override {
        if (stan.transakcja != 0) {
            potwierdz(nullptr, reinterpret_cast<void*>(1));
        }
    }

    void zamknij() override {
        if (stan.transakcja != 0) {
            potwierdz(nullptr, reinterpret_cast<void*>(1));
        }
        Okno::zamknij();
    }

    void dopasuj_rozmiar_i_polozenie(bool reset_position = false) {
        const uint32_t p = ui_skala_procent;
        const Prostokat work = pobierz_obszar_roboczy();
        int docelowa_szer = ui_skaluj(OKNO_SZEROKOSC, p);
        if (docelowa_szer > work.szerokosc) docelowa_szer = work.szerokosc;
        int docelowa_wys = ui_skaluj(OKNO_NORMALNE_H, p);
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

        if (widok_glowny) {
            widok_glowny->ustaw_rozmiar_zawartosci(
                Rozmiar(1, ui_skaluj(TRESC_NORMALNA_H, p)));
        }

        ustaw_geometrie(nx, ny, docelowa_szer, docelowa_wys);
    }

    void obsluz_zdarzenie(const bws_zdarzenie& e) override {
        if (stan.transakcja != 0) {
            bool wcisniety = (e.typ == BWS_ZDARZENIE_KLAWISZ) ||
                             (e.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY && e.x != 0);
            if (wcisniety) {
                uint32_t kod = e.kod;
                bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
                uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);
                bool enter = (kod == 13U || kod == 10U || (usb && baza == 40U) || (!usb && baza == 0x1CU));
                bool escape = (kod == 27U || (usb && baza == 41U) || (!usb && baza == 0x01U));
                if (enter) {
                    potwierdz(nullptr, nullptr);
                    return;
                }
                if (escape) {
                    potwierdz(nullptr, reinterpret_cast<void*>(1));
                    return;
                }
            }
        }
        if (e.typ == BWS_ZDARZENIE_EKRAN) {
            if (ui_skala_poprawna(e.kod)) {
                ui_skala_procent = e.kod;
            }
            dopasuj_rozmiar_i_polozenie(false);
        }
        Okno::obsluz_zdarzenie(e);
        if (e.typ == BWS_ZDARZENIE_EKRAN || (e.typ == BWS_ZDARZENIE_TIMER && stan.transakcja)) {
            EkranZadanie r{}; r.operacja = EKRAN_STAN;
            if (ekran_zapytaj(endpoint, r, &stan)) synchronizuj(!stan.transakcja);
        }
    }
};

void tekst(char* out, size_t cap, const char* prefix, uint32_t a, const char* middle = "", uint32_t b = 0, const char* suffix = "") {
    size_t p = 0; auto str = [&](const char* s) { while (*s && p + 1 < cap) out[p++] = *s++; };
    auto num = [&](uint32_t n) { char r[10]; unsigned i = 0; do { r[i++] = char('0' + n % 10); n /= 10; } while (n); while (i && p + 1 < cap) out[p++] = r[--i]; };
    str(prefix); num(a); str(middle); if (b) num(b); str(suffix); out[p] = 0;
}

void synchronizuj(bool wybor) {
    if (wybor) {
        int idx = 0;
        for (unsigned i = 0; i < stan.liczba_trybow; ++i)
            if (ekran_ten_sam(stan.tryby[i], stan.aktywny)) idx = i;
        pending_tryb = idx;
        pending_skala = (stan.skala - 100) / 25;
        tryby->ustaw_wybrany_indeks(pending_tryb);
        skale->ustaw_wybrany_indeks(pending_skala);
    }
    const bool pending = stan.transakcja != 0;
    tryby->ustaw_wlaczona(!pending && stan.mozliwosci.mozna_zmienic_runtime);
    skale->ustaw_wlaczona(!pending);
    zastosuj_btn->ustaw_wlaczona(!pending);
    domyslne_btn->ustaw_wlaczona(!pending);

    char b[160]{};
    char tryb[48]{};
    ekran_formatuj_tryb(tryb, sizeof(tryb), stan.aktywny.szerokosc, stan.aktywny.wysokosc, false);
    size_t p = 0;
    const char* prefix = "Bieżąca rozdzielczość: ";
    while (prefix[p] && p + 1 < sizeof(b)) { b[p] = prefix[p]; ++p; }
    for (size_t i = 0; tryb[i] && p + 1 < sizeof(b); ++i) b[p++] = tryb[i];
    b[p] = 0;
    info->ustaw_tekst(b);

    panel_akcji->ustaw_widoczna(!pending);
    panel_potwierdzenia->ustaw_widoczna(pending);
    if (pending) {
        tekst(b, sizeof(b), "Zachować nowe ustawienia? Pozostało ", stan.pozostalo_sekund, " s");
        potwierdzenie->ustaw_tekst(b);
    }
}

void wynik(uint32_t status) {
    if (status == EKRAN_OK || status == EKRAN_OCZEKUJE) return;
    info->ustaw_tekst(status == EKRAN_PAMIEC ? "Za mało pamięci na bufory ekranu." :
        status == EKRAN_ZAPIS ? "Nie udało się zapisać ustawień." :
        status == EKRAN_NIEOBSLUGIWANY ? "Ten tryb nie jest obsługiwany przez sterownik." :
        status == EKRAN_ZAJETY ? "Trwa potwierdzanie innej zmiany." : "Nie udało się zastosować ustawień.");
}

void zastosuj(Przycisk*, void*) {
    const int i = pending_tryb, s = pending_skala;
    if (i < 0 || i >= int(stan.liczba_trybow) || s < 0 || s > 4) return;
    EkranZadanie r{};
    r.operacja = EKRAN_ZASTOSUJ;
    r.szerokosc = stan.tryby[i].szerokosc;
    r.wysokosc = stan.tryby[i].wysokosc;
    r.bpp = 32;
    r.skala = 100 + s * 25;
    if (!ekran_zapytaj(endpoint, r, &stan)) {
        info->ustaw_tekst("Brak odpowiedzi usługi ekranu.");
        return;
    }
    synchronizuj(false);
    wynik(stan.status);
}

void potwierdz(Przycisk*, void* dane) {
    EkranZadanie r{};
    r.operacja = dane ? EKRAN_PRZYWROC : EKRAN_ZACHOWAJ;
    r.transakcja = stan.transakcja;
    if (ekran_zapytaj(endpoint, r, &stan)) {
        synchronizuj(true);
        wynik(stan.status);
    }
}

void anuluj(Przycisk*, void*) {
    if (stan.transakcja) {
        potwierdz(nullptr, reinterpret_cast<void*>(1));
        return;
    }
    int idx = 0;
    for (unsigned i = 0; i < stan.liczba_trybow; ++i)
        if (ekran_ten_sam(stan.tryby[i], stan.aktywny)) idx = i;
    int s = (stan.skala - 100) / 25;
    if (pending_tryb != idx || pending_skala != s) {
        synchronizuj(true);
        return;
    }
    okno_glowne->zamknij();
}

void domyslne(Przycisk*, void*) {
    // Firmware mode remains the safe default; scale is independently 100%.
    pending_tryb = 0;
    pending_skala = 0;
    tryby->ustaw_wybrany_indeks(pending_tryb);
    skale->ustaw_wybrany_indeks(pending_skala);
}

void zmien_tryb(ListaRozwijana*, int indeks, void*) { pending_tryb = indeks; }
void zmien_skale(ListaRozwijana*, int indeks, void*) { pending_skala = indeks; }
}

extern "C" __attribute__((noreturn)) void _start() {
    AplikacjaGUI app;
    OknoEkranu okno;
    okno_glowne = &okno;
    app.ustaw_glowne_okno(&okno);

    endpoint = ipc_create();
    EkranZadanie req{};
    req.operacja = EKRAN_STAN;
    if (!ekran_zapytaj(endpoint, req, &stan)) gui_zakoncz_aplikacje();

    auto* glowny_panel = new PanelGlownyEkranu();
    okno.ustaw_kontrolke_glowna(glowny_panel);

    WidokPrzewijany widok;
    widok_glowny = &widok;
    widok.ustaw_kolor_tla(0);
    widok.ustaw_grubosc_ramki(0);
    widok.ustaw_tryb_paska_poziomego(TrybPaskaPrzewijania::Nigdy);
    widok.ustaw_tryb_paska_pionowego(TrybPaskaPrzewijania::Automatycznie);
    widok.ustaw_rozmiar_zawartosci(Rozmiar(1, ui_skaluj(TRESC_NORMALNA_H, ui_skala_procent)));

    auto* root = &widok.zawartosc();
    root->ustaw_uklad_automatyczny(true);
    root->ustaw_kolor_tla(0);
    root->ustaw_grubosc_ramki(0);
    root->ustaw_wypelnienie(Marginesy(12, 10, 12, 10));
    root->ustaw_odstep(6);

    root->dodaj<Etykieta>("Monitor główny");
    backend = &root->dodaj<Etykieta>(stan.mozliwosci.backend == EKRAN_BOCHS ? "Sterownik: Bochs VBE" : stan.mozliwosci.backend == EKRAN_GOP ? "Sterownik: UEFI GOP" : "Sterownik: VESA");
    root->dodaj<Etykieta>("Rozdzielczość");
    tryby = &root->dodaj<ListaRozwijana>();
    tryby->ustaw_preferowany_rozmiar(Rozmiar(-1, 28));
    tryby->przy_zmianie_wyboru(zmien_tryb);
    for (unsigned i = 0; i < stan.liczba_trybow; ++i) {
        const auto& t = stan.tryby[i]; char b[80]{};
        if (ekran_formatuj_tryb(b, sizeof(b), t.szerokosc, t.wysokosc, true)) tryby->dodaj_element(b);
    }
    root->dodaj<Etykieta>("Skala interfejsu");
    skale = &root->dodaj<ListaRozwijana>();
    skale->ustaw_preferowany_rozmiar(Rozmiar(-1, 28));
    skale->przy_zmianie_wyboru(zmien_skale);
    const char* procenty[] = {"100%", "125%", "150%", "175%", "200%"};
    for (auto* s : procenty) skale->dodaj_element(s);
    info = &root->dodaj<Etykieta>("");
    auto& limit = root->dodaj<Etykieta>(stan.mozliwosci.mozna_zmienic_runtime ? "Zmiana rozdzielczości dostępna bez restartu." : "Bieżący sterownik nie obsługuje zmiany rozdzielczości podczas pracy systemu.");
    limit.ustaw_zawijanie(true);

    panel_akcji = new Panel(Orientacja::Pozioma);
    panel_akcji->ustaw_uklad_automatyczny(true);
    panel_akcji->ustaw_kolor_tla(0);
    panel_akcji->ustaw_grubosc_ramki(0);
    panel_akcji->ustaw_wypelnienie(Marginesy(12, 6, 12, 8));
    panel_akcji->ustaw_odstep(8);

    zastosuj_btn = &panel_akcji->dodaj<Przycisk>("Zastosuj");
    zastosuj_btn->przy_kliknieciu(zastosuj);
    anuluj_btn = &panel_akcji->dodaj<Przycisk>("Anuluj");
    anuluj_btn->przy_kliknieciu(anuluj);
    domyslne_btn = &panel_akcji->dodaj<Przycisk>("Przywróć domyślne");
    domyslne_btn->przy_kliknieciu(domyslne);

    panel_potwierdzenia = new Panel(Orientacja::Pozioma);
    panel_potwierdzenia->ustaw_uklad_automatyczny(true);
    panel_potwierdzenia->ustaw_kolor_tla(0);
    panel_potwierdzenia->ustaw_grubosc_ramki(0);
    panel_potwierdzenia->ustaw_wypelnienie(Marginesy(12, 6, 12, 8));
    panel_potwierdzenia->ustaw_odstep(8);

    potwierdzenie = &panel_potwierdzenia->dodaj<Etykieta>("");
    potwierdzenie->ustaw_wyrownanie_pionowe_tekstu(WyrownaniePionowe::Srodek);
    zachowaj_btn = &panel_potwierdzenia->dodaj<Przycisk>("Zachowaj");
    zachowaj_btn->przy_kliknieciu(potwierdz, nullptr);
    przywroc_btn = &panel_potwierdzenia->dodaj<Przycisk>("Przywróć");
    przywroc_btn->przy_kliknieciu(potwierdz, reinterpret_cast<void*>(1));

    auto* pasek_dolny = new PasekDolnyEkranu();
    pasek_dolny->ustaw_panele(panel_akcji, panel_potwierdzenia);

    glowny_panel->ustaw_elementy(&widok, pasek_dolny);

    synchronizuj(true);
    okno.dopasuj_rozmiar_i_polozenie(true);
    app.uruchom();
    gui_zakoncz_aplikacje();
}
