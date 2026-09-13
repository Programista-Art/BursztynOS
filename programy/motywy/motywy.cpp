/*
 * Bursztyn OS - Aplikacja graficzna "Motywy Systemowe" (.cebula)
 *
 * Natywny interfejs wyboru i podgladu motywow systemowych (Ring 3).
 * Wykorzystuje BWS_MOTYW_POBIERZ (86) oraz BWS_MOTYW_POLECENIE (87).
 * Caly interfejs w jezyku polskim z pelnym zakresem znakow Unicode UTF-8.
 */

#include "../../motyw.h"
#include "../../bursztyn_gui.h"
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
    {'B','U','R','\0'}, reinterpret_cast<uint64_t>(&_start),
    BUR_TEKST_PRZESUNIECIE, BUR_TEKST_ROZMIAR, BUR_TEKST_WIRTUALNY,
    BUR_DANE_PRZESUNIECIE, BUR_DANE_ROZMIAR, BUR_DANE_WIRTUALNY
};
}
static_assert(sizeof(NaglowekBur) == 60U, "Naglowek .bur musi miec 60 bajtow");
static_assert((BUR_TEKST_PRZESUNIECIE & 0xFFFULL) == 0, "BUR_TEKST_PRZESUNIECIE musi byc wyrownane do 4 KiB");
static_assert((BUR_TEKST_ROZMIAR & 0xFFFULL) == 0, "BUR_TEKST_ROZMIAR musi byc wyrownane do 4 KiB");
static_assert((BUR_TEKST_WIRTUALNY & 0xFFFULL) == 0, "BUR_TEKST_WIRTUALNY musi byc wyrownane do 4 KiB");
static_assert((BUR_DANE_PRZESUNIECIE & 0xFFFULL) == 0, "BUR_DANE_PRZESUNIECIE musi byc wyrownane do 4 KiB");
static_assert((BUR_DANE_ROZMIAR & 0xFFFULL) == 0, "BUR_DANE_ROZMIAR musi byc wyrownane do 4 KiB");
static_assert((BUR_DANE_WIRTUALNY & 0xFFFULL) == 0, "BUR_DANE_WIRTUALNY musi byc wyrownane do 4 KiB");
static_assert(BUR_TEKST_PRZESUNIECIE + BUR_TEKST_ROZMIAR == BUR_DANE_PRZESUNIECIE, "Segmenty w pliku musza byc ciagle");
static_assert(BUR_TEKST_WIRTUALNY + BUR_TEKST_ROZMIAR == BUR_DANE_WIRTUALNY, "Segmenty w VA musza byc ciagle");

namespace {

struct InformacjeOMotywie {
    const char* id;
    const char* nazwa;
    const char* etykieta;
    const char* opis;
};

static const InformacjeOMotywie DOSTEPNE_MOTYWY[] = {
    {
        "bursztynnocny",
        "Bursztyn Nocny",
        "Bursztyn Nocny (Domyślny)",
        "Ciemny, głęboki motyw bursztynowy o wysokim kontraście i ciepłych barwach."
    },
    {
        "baltyckanoc",
        "Bałtycka Noc",
        "Bałtycka Noc",
        "Głęboki granat z chłodnymi, oceanicznymi i błękitnymi akcentami."
    },
    {
        "grafit",
        "Grafit",
        "Grafit",
        "Elegancka neutralna szarość, profesjonalny i spokojny styl roboczy."
    },
    {
        "bursztynklasyczny",
        "Bursztyn Klasyczny",
        "Bursztyn Klasyczny",
        "Ciepły, tradycyjny bursztynowo-brązowy styl wizualny Bursztyn OS."
    },
    {
        "bursztynjasny",
        "Bursztyn Jasny",
        "Bursztyn Jasny",
        "Jasny motyw dzienny o czystym, nowoczesnym i czytelnym wyglądzie."
    }
};
static constexpr int LICZBA_MOTYWOW = static_cast<int>(sizeof(DOSTEPNE_MOTYWY) / sizeof(DOSTEPNE_MOTYWY[0]));

static void formatuj_pozostaly_czas(uint32_t sek, char* buf, size_t cap) {
    if (!buf || cap < 40) return;
    const char* pref = "Tryb podglądu aktywny! Pozostało: ";
    size_t p = 0;
    while (pref[p] && p + 1 < cap) { buf[p] = pref[p]; ++p; }
    if (sek >= 10) {
        if (p + 1 < cap) buf[p++] = static_cast<char>('0' + (sek / 10));
    }
    if (p + 1 < cap) buf[p++] = static_cast<char>('0' + (sek % 10));
    const char* suf = " s";
    for (int i = 0; suf[i] && p + 1 < cap; ++i) buf[p++] = suf[i];
    buf[p] = '\0';
}

class OknoMotywow;

class PodgladMotywuKontrolka : public Kontrolka {
public:
    PodgladMotywuKontrolka() {
        ustaw_preferowany_rozmiar(Rozmiar(320, 240));
        ustaw_minimalny_rozmiar(Rozmiar(280, 200));
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Rozciagnij;
    }

    void ustaw_okno(OknoMotywow* okno) { okno_ = okno; }
    void ustaw_animacje_podgladu(bool wlaczona) { animacja_wlaczona_ = wlaczona; }

    bool czy_wymaga_animacji() const override {
        return animacja_wlaczona_;
    }

    void krok_animacji(uint64_t teraz_ns = 0) override;

    void rysuj_siebie(KontekstRysowania& ctx) override {
        uint32_t tlo_okna = motyw_kolor(MOTYW_KOLOR_OKNOTLO);
        uint32_t belka = motyw_kolor(MOTYW_KOLOR_PODNIESIONY);
        uint32_t belka_tekst = motyw_kolor(MOTYW_KOLOR_TEKST);
        uint32_t tekst = motyw_kolor(MOTYW_KOLOR_TEKST);
        uint32_t tekst_ciemny = motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI);
        uint32_t akcent = motyw_kolor(MOTYW_KOLOR_AKCENT);
        uint32_t ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        uint32_t przycisk_tlo = motyw_kolor(MOTYW_KOLOR_PRZYCISK);
        uint32_t przycisk_tekst = motyw_kolor(MOTYW_KOLOR_TEKST);
        uint32_t wejscie_tlo = motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE);
        uint32_t wejscie_ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        uint32_t sukces = motyw_kolor(MOTYW_KOLOR_SUKCES);
        uint32_t ostrzezenie = motyw_kolor(MOTYW_KOLOR_OSTRZEZENIE);
        uint32_t blad = motyw_kolor(MOTYW_KOLOR_BLAD);
        uint32_t tekst_akcentu = motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU);

        int r_okno = motyw_promien(MOTYW_ROG_OKNO);
        int r_przycisk = motyw_promien(MOTYW_ROG_PRZYCISK);
        int r_wejscie = motyw_promien(MOTYW_ROG_POLE);
        int r_odznaka = motyw_promien(MOTYW_ROG_PRZYCISK);

        // Karta tła dla podglądu z delikatnym obramowaniem
        ctx.rysuj_zaokraglony_prostokat(Prostokat(0, 0, obszar_.szerokosc, obszar_.wysokosc), 0x20000000 | (ramka & 0x00FFFFFF), 6);
        ctx.rysuj_zaokraglona_ramke(Prostokat(0, 0, obszar_.szerokosc, obszar_.wysokosc), ramka, 1, 6);

        // Symulowane mini okno na środku karty
        int win_x = 12;
        int win_y = 12;
        int win_w = obszar_.szerokosc - 24;
        int win_h = obszar_.wysokosc - 24;
        if (win_w < 100 || win_h < 80) return;

        int belka_h = 28;

        // Belka tytułowa okna z zaokrąglonymi górnymi rogami
        ctx.rysuj_zaokraglony_prostokat(Prostokat(win_x, win_y, win_w, belka_h + r_okno), belka, r_okno);
        // Ciało okna z zaokrąglonymi dolnymi rogami
        ctx.rysuj_zaokraglony_prostokat(Prostokat(win_x, win_y + belka_h, win_w, win_h - belka_h), tlo_okna, r_okno);
        // Linia podziału belki
        ctx.rysuj_prostokat(Prostokat(win_x, win_y + belka_h - 1, win_w, 1), ramka);
        // Obramowanie okna
        ctx.rysuj_zaokraglona_ramke(Prostokat(win_x, win_y, win_w, win_h), ramka, 1, r_okno);

        // Kropki kontrolne okna
        ctx.rysuj_zaokraglony_prostokat(Prostokat(win_x + 8, win_y + 9, 10, 10), blad, 5);
        ctx.rysuj_zaokraglony_prostokat(Prostokat(win_x + 22, win_y + 9, 10, 10), ostrzezenie, 5);
        ctx.rysuj_zaokraglony_prostokat(Prostokat(win_x + 36, win_y + 9, 10, 10), sukces, 5);

        // Tytuł okna w belce
        ctx.rysuj_tekst(Punkt(win_x + 52, win_y + 6), "Okno Podglądu", belka_tekst);

        // Zawartość klienta okna
        int cx = win_x + 14;
        int cy = win_y + belka_h + 12;

        ctx.rysuj_tekst(Punkt(cx, cy), "Bursztyn OS - Systemowy Wygląd", tekst);
        cy += 20;
        ctx.rysuj_tekst(Punkt(cx, cy), "Natywny silnik motywów deklaratywnych", tekst_ciemny);
        cy += 24;

        // Przycisk standardowy
        int btn_w = 74;
        int btn_h = 24;
        ctx.rysuj_zaokraglony_prostokat(Prostokat(cx, cy, btn_w, btn_h), przycisk_tlo, r_przycisk);
        ctx.rysuj_zaokraglona_ramke(Prostokat(cx, cy, btn_w, btn_h), ramka, 1, r_przycisk);
        ctx.rysuj_tekst(Punkt(cx + 10, cy + 4), "Zwykły", przycisk_tekst);

        // Przycisk akcentowy
        int btn2_x = cx + btn_w + 10;
        ctx.rysuj_zaokraglony_prostokat(Prostokat(btn2_x, cy, btn_w, btn_h), akcent, r_przycisk);
        ctx.rysuj_tekst(Punkt(btn2_x + 10, cy + 4), "Akcent", tekst_akcentu);

        cy += btn_h + 12;

        // Pole tekstowe
        int wej_w = win_w - 28;
        int wej_h = 24;
        if (cy + wej_h < win_y + win_h - 10) {
            ctx.rysuj_zaokraglony_prostokat(Prostokat(cx, cy, wej_w, wej_h), wejscie_tlo, r_wejscie);
            ctx.rysuj_zaokraglona_ramke(Prostokat(cx, cy, wej_w, wej_h), wejscie_ramka, 1, r_wejscie);
            ctx.rysuj_tekst(Punkt(cx + 8, cy + 4), "Pole edycji tekstu...", tekst_ciemny);
        }

        cy += wej_h + 10;

        // Odznaki statusu
        if (cy + 18 < win_y + win_h - 4) {
            ctx.rysuj_zaokraglony_prostokat(Prostokat(cx, cy, 64, 18), sukces, r_odznaka);
            ctx.rysuj_tekst(Punkt(cx + 8, cy + 1), "Sukces", tekst_akcentu);

            ctx.rysuj_zaokraglony_prostokat(Prostokat(cx + 74, cy, 64, 18), ostrzezenie, r_odznaka);
            ctx.rysuj_tekst(Punkt(cx + 82, cy + 1), "Uwaga", tekst_akcentu);
        }
    }

private:
    OknoMotywow* okno_{nullptr};
    bool animacja_wlaczona_{false};
};

class OknoMotywow : public Okno {
public:
    OknoMotywow() : Okno("Motywy systemowe", 640, 480) {}

    void inicjalizuj() {
        Panel& korzen = dodaj<Panel>(Orientacja::Pionowa);
        korzen.ustaw_wypelnienie(Marginesy(12, 10, 12, 8));
        korzen.ustaw_odstep(10);

        // 1. Nagłówek okna
        Etykieta& naglowek = korzen.dodaj<Etykieta>("Personalizacja Wyglądu Pulpitu");
        naglowek.ustaw_skale_czcionki(1);
        naglowek.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));

        Etykieta& podtytul = korzen.dodaj<Etykieta>("Wybierz motyw wizualny dla całego systemu operacyjnego:");
        podtytul.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI));

        // 2. Obszar główny: lewa kolumna (lista + opis + przyciski), prawa kolumna (podgląd)
        Panel& glowny = korzen.dodaj<Panel>(Orientacja::Pozioma);
        glowny.ustaw_preferowany_rozmiar(Rozmiar(-1, 310));
        glowny.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        glowny.ustaw_odstep(14);

        // Lewa kolumna: szerokość 280 px
        Panel& lewa = glowny.dodaj<Panel>(Orientacja::Pionowa);
        lewa.ustaw_preferowany_rozmiar(Rozmiar(280, -1));
        lewa.ustaw_minimalny_rozmiar(Rozmiar(260, 0));
        lewa.ustaw_odstep(8);

        Etykieta& naglowek_sekcji = lewa.dodaj<Etykieta>("Wygląd systemu");
        naglowek_sekcji.ustaw_preferowany_rozmiar(Rozmiar(280, 24));
        naglowek_sekcji.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));

        ListaWyboru& lista = lewa.dodaj<ListaWyboru>();
        lista.ustaw_preferowany_rozmiar(Rozmiar(280, 130));
        for (int i = 0; i < LICZBA_MOTYWOW; ++i) {
            lista.dodaj_element(DOSTEPNE_MOTYWY[i].etykieta, static_cast<uint64_t>(i));
        }

        // Zaznacz biezacy motyw
        const BursztynMotyw* biezacy = gui_pobierz_motyw();
        int domyslny_indeks = 0;
        if (biezacy) {
            for (int i = 0; i < LICZBA_MOTYWOW; ++i) {
                bool pasuje = true;
                for (size_t j = 0; DOSTEPNE_MOTYWY[i].nazwa[j] != '\0'; ++j) {
                    if (biezacy->nazwa[j] != DOSTEPNE_MOTYWY[i].nazwa[j]) {
                        pasuje = false;
                        break;
                    }
                }
                if (pasuje) {
                    domyslny_indeks = i;
                    break;
                }
            }
        }
        lista.ustaw_wybrany_indeks(domyslny_indeks);
        wybrany_indeks_ = domyslny_indeks;
        lista_ = &lista;

        lista.przy_zmianie_wyboru([](ListaWyboru* l, int idx, void* dane) {
            (void)l;
            auto* okno = static_cast<OknoMotywow*>(dane);
            if (okno && idx >= 0 && idx < LICZBA_MOTYWOW) {
                okno->zmien_wybor(idx);
            }
        }, this);

        // Opis wybranego motywu
        Etykieta& opis = lewa.dodaj<Etykieta>(DOSTEPNE_MOTYWY[domyslny_indeks].opis);
        opis.ustaw_preferowany_rozmiar(Rozmiar(280, 54));
        opis.ustaw_zawijanie(true);
        opis.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST));
        opis_ = &opis;

        // Przyciski akcji motywu
        Panel& rzad_przyciskow = lewa.dodaj<Panel>(Orientacja::Pozioma);
        rzad_przyciskow.ustaw_preferowany_rozmiar(Rozmiar(280, 36));
        rzad_przyciskow.ustaw_odstep(8);

        Przycisk& btn_podglad = rzad_przyciskow.dodaj<Przycisk>("Podgląd (15 s)");
        btn_podglad.ustaw_preferowany_rozmiar(Rozmiar(134, 34));
        btn_podglad.przy_kliknieciu([](Przycisk* k, void* dane) {
            (void)k;
            auto* okno = static_cast<OknoMotywow*>(dane);
            if (okno) okno->uruchom_podglad();
        }, this);
        btn_podglad_ = &btn_podglad;

        Przycisk& btn_ustaw = rzad_przyciskow.dodaj<Przycisk>("Zastosuj");
        btn_ustaw.ustaw_preferowany_rozmiar(Rozmiar(134, 34));
        btn_ustaw.przy_kliknieciu([](Przycisk* k, void* dane) {
            (void)k;
            auto* okno = static_cast<OknoMotywow*>(dane);
            if (okno) okno->zastosuj_na_stale();
        }, this);
        btn_ustaw_ = &btn_ustaw;

        // Prawa kolumna: karta podglądu
        PodgladMotywuKontrolka& podglad_karta = glowny.dodaj<PodgladMotywuKontrolka>();
        podglad_karta.ustaw_okno(this);
        podglad_karta_ = &podglad_karta;

        // 3. Panel transakcji podglądu (odliczanie 15 s, Zachowaj, Przywróć)
        Panel& panel_podgladu = korzen.dodaj<Panel>(Orientacja::Pozioma);
        panel_podgladu.ustaw_preferowany_rozmiar(Rozmiar(-1, 36));
        panel_podgladu.ustaw_wypelnienie(Marginesy(8, 4, 8, 4));
        panel_podgladu.ustaw_odstep(10);
        panel_podgladu.ustaw_kolor_tla(motyw_kolor(MOTYW_KOLOR_PANEL));
        panel_podgladu.ustaw_kolor_ramki(motyw_kolor(MOTYW_KOLOR_AKCENT));
        panel_podgladu.ustaw_grubosc_ramki(1);
        panel_podgladu.ustaw_widoczna(false);
        panel_podgladu_ = &panel_podgladu;

        Etykieta& etykieta_podgladu = panel_podgladu.dodaj<Etykieta>("Czy zachować ten motyw? Pozostało: 15 s");
        etykieta_podgladu.ustaw_preferowany_rozmiar(Rozmiar(320, 26));
        etykieta_podgladu.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));
        etykieta_podgladu_ = &etykieta_podgladu;

        Przycisk& btn_zatwierdz = panel_podgladu.dodaj<Przycisk>("Zachowaj");
        btn_zatwierdz.ustaw_preferowany_rozmiar(Rozmiar(110, 28));
        btn_zatwierdz.przy_kliknieciu([](Przycisk* k, void* dane) {
            (void)k;
            auto* okno = static_cast<OknoMotywow*>(dane);
            if (okno) okno->zatwierdz_podglad();
        }, this);

        Przycisk& btn_przywroc = panel_podgladu.dodaj<Przycisk>("Przywróć");
        btn_przywroc.ustaw_preferowany_rozmiar(Rozmiar(110, 28));
        btn_przywroc.przy_kliknieciu([](Przycisk* k, void* dane) {
            (void)k;
            auto* okno = static_cast<OknoMotywow*>(dane);
            if (okno) okno->przywroc_poprzedni();
        }, this);

        // 4. Pasek stanu na dole
        PasekStanu& sb = korzen.dodaj<PasekStanu>();
        sb.ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
        sb.dodaj_sekcje("Gotowy", 380, RozmiarSekcjiStanu::Rozciagnij);
        sb.dodaj_sekcje("Bursztyn OS", 140, RozmiarSekcjiStanu::Stala);
        pasek_stanu_ = &sb;

        zaktualizuj_status();
    }

    void animuj_krok(uint64_t teraz_ns) {
        if (!podglad_aktywny_) return;
        if (teraz_ns == 0) teraz_ns = gui_czas_monotoniczny_ns();

        uint64_t uplynelo_ns = teraz_ns - podglad_start_ns_;
        uint64_t uplynelo_sek = uplynelo_ns / 1000000000ULL;

        if (uplynelo_sek >= 15) {
            podglad_aktywny_ = false;
            if (podglad_karta_) podglad_karta_->ustaw_animacje_podgladu(false);
            if (panel_podgladu_) panel_podgladu_->ustaw_widoczna(false);
            if (pasek_stanu_) pasek_stanu_->ustaw_tekst(0, "Upłynął limit 15 s. Przywrócono poprzedni motyw.");
            przelicz_uklad();
            uniewaznij();
            return;
        }

        uint32_t pozostalo = 15 - static_cast<uint32_t>(uplynelo_sek);
        if (pozostalo != ostatnie_pozostalo_sek_) {
            ostatnie_pozostalo_sek_ = pozostalo;
            char bufor[64];
            formatuj_pozostaly_czas(pozostalo, bufor, sizeof(bufor));
            if (etykieta_podgladu_) etykieta_podgladu_->ustaw_tekst(bufor);
            uniewaznij();
        }
    }

    void zmien_wybor(int indeks) {
        if (indeks < 0 || indeks >= LICZBA_MOTYWOW) return;
        wybrany_indeks_ = indeks;
        if (opis_) opis_->ustaw_tekst(DOSTEPNE_MOTYWY[indeks].opis);
        uniewaznij();
    }

    void uruchom_podglad() {
        if (wybrany_indeks_ < 0 || wybrany_indeks_ >= LICZBA_MOTYWOW) return;
        const char* id = DOSTEPNE_MOTYWY[wybrany_indeks_].id;
        if (motyw_polecenie_podglad(id)) {
            podglad_aktywny_ = true;
            podglad_start_ns_ = gui_czas_monotoniczny_ns();
            ostatnie_pozostalo_sek_ = 15;
            if (podglad_karta_) podglad_karta_->ustaw_animacje_podgladu(true);
            if (panel_podgladu_) panel_podgladu_->ustaw_widoczna(true);
            char bufor[64];
            formatuj_pozostaly_czas(15, bufor, sizeof(bufor));
            if (etykieta_podgladu_) etykieta_podgladu_->ustaw_tekst(bufor);
            if (pasek_stanu_) pasek_stanu_->ustaw_tekst(0, "Podgląd motywu uruchomiony na 15 sekund.");
            przelicz_uklad();
            uniewaznij();
        } else {
            if (pasek_stanu_) pasek_stanu_->ustaw_tekst(0, "Błąd uruchomienia podglądu motywu.");
        }
    }

    void zatwierdz_podglad() {
        if (motyw_polecenie_zatwierdz()) {
            podglad_aktywny_ = false;
            if (podglad_karta_) podglad_karta_->ustaw_animacje_podgladu(false);
            if (panel_podgladu_) panel_podgladu_->ustaw_widoczna(false);
            if (pasek_stanu_) pasek_stanu_->ustaw_tekst(0, "Motyw został zatwierdzony i zapisany.");
            przelicz_uklad();
            uniewaznij();
        }
    }

    void przywroc_poprzedni() {
        if (motyw_polecenie_przywroc()) {
            podglad_aktywny_ = false;
            if (podglad_karta_) podglad_karta_->ustaw_animacje_podgladu(false);
            if (panel_podgladu_) panel_podgladu_->ustaw_widoczna(false);
            if (pasek_stanu_) pasek_stanu_->ustaw_tekst(0, "Przywrócono poprzedni motyw systemowy.");
            przelicz_uklad();
            uniewaznij();
        }
    }

    void zastosuj_na_stale() {
        if (wybrany_indeks_ < 0 || wybrany_indeks_ >= LICZBA_MOTYWOW) return;
        const char* id = DOSTEPNE_MOTYWY[wybrany_indeks_].id;
        if (motyw_polecenie_ustaw(id)) {
            podglad_aktywny_ = false;
            if (podglad_karta_) podglad_karta_->ustaw_animacje_podgladu(false);
            if (panel_podgladu_) panel_podgladu_->ustaw_widoczna(false);
            if (pasek_stanu_) pasek_stanu_->ustaw_tekst(0, "Motyw zastosowany jako domyślny systemowy.");
            przelicz_uklad();
            uniewaznij();
        } else {
            if (pasek_stanu_) pasek_stanu_->ustaw_tekst(0, "Błąd zastosowania motywu.");
        }
    }

    void zaktualizuj_status() {
        const BursztynMotyw* m = gui_pobierz_motyw();
        if (m && pasek_stanu_) {
            char bufor[96] = "Bieżący motyw: ";
            size_t p = 15;
            for (size_t i = 0; m->nazwa[i] != '\0' && p + 1 < sizeof(bufor); ++i) {
                bufor[p++] = m->nazwa[i];
            }
            bufor[p] = '\0';
            pasek_stanu_->ustaw_tekst(0, bufor);
        }
    }

private:
    int wybrany_indeks_{0};
    bool podglad_aktywny_{false};
    uint64_t podglad_start_ns_{0};
    uint32_t ostatnie_pozostalo_sek_{15};

    ListaWyboru* lista_{nullptr};
    Etykieta* opis_{nullptr};
    Przycisk* btn_podglad_{nullptr};
    Przycisk* btn_ustaw_{nullptr};
    PodgladMotywuKontrolka* podglad_karta_{nullptr};
    Panel* panel_podgladu_{nullptr};
    Etykieta* etykieta_podgladu_{nullptr};
    PasekStanu* pasek_stanu_{nullptr};
};

void PodgladMotywuKontrolka::krok_animacji(uint64_t teraz_ns) {
    if (animacja_wlaczona_ && okno_) {
        okno_->animuj_krok(teraz_ns);
    }
}

} // namespace

extern "C" __attribute__((noreturn)) void _start() {
    AplikacjaGUI app;
    OknoMotywow okno;
    okno.inicjalizuj();
    (void)app.uruchom(okno);
    bws_zakoncz_proces();
}
