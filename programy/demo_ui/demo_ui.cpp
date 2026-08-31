/*
 * Bursztyn OS - BursztynUI Zaawansowane Demo Komponentów (Ring 3)
 * Pokaz zaawansowanych możliwości komponentów BursztynUI:
 *  - Kanoniczna czcionka extronic16B_unicode.h dla wszystkich kontrolek i pomiarów
 *  - Zaawansowane PoleTekstowe: bezpieczne UTF-8, zaznaczanie myszą/Shift, Ctrl+A/C/X/V/Z/Y, tryb hasła, przewijanie
 *  - Stylizowany Przycisk z niezależnymi stanami (normal, hover, pressed, fokus, disabled) i poprawną semantyką capture/release
 *  - PoleWyboru: tryb standardowy oraz trójstanowy (zaznaczone, niezaznaczone, nieokreślone)
 *  - Suwak: zakresy dodatnie i ujemne, orientacja pionowa, klawiatura, rolka i stan wyłączony
 *  - Zagnieżdżone panele (Kolumna, Wiersz) z precyzyjnym układem bez nakładania się
 *  - Blokada odrysowywania (rozpocznij_aktualizacje / zakoncz_aktualizacje)
 *  - Płynna nawigacja fokusu (Tab / Shift+Tab)
 */

#include "../../loader.h"
#include "../../bursztyn_ui/bursztyn_ui.h"

using namespace BursztynUI;

// =============================================================================
// Nagłówek formatu .bur
// =============================================================================

extern "C" void _start();

static constexpr uint64_t BUR_TEKST_ROZMIAR     = 0xC000ULL; // 48 KiB
static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 0x1000ULL;
static constexpr uint64_t BUR_TEKST_WIRTUALNY     = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE   = 0xD000ULL;
static constexpr uint64_t BUR_DANE_WIRTUALNY       = 0x100D000ULL;
static constexpr uint64_t BUR_DANE_ROZMIAR         = 131072ULL;

extern "C" {
    __attribute__((section(".naglowek"), used))
    NaglowekBur naglowek = {
        {'B', 'U', 'R', '\0'},
        (uint64_t)&_start,
        BUR_TEKST_PRZESUNIECIE,
        BUR_TEKST_ROZMIAR,
        BUR_TEKST_WIRTUALNY,
        BUR_DANE_PRZESUNIECIE,
        BUR_DANE_ROZMIAR,
        BUR_DANE_WIRTUALNY
    };
}

// =============================================================================
// Stan Aplikacji Demo
// =============================================================================

struct StanAplikacji {
    Etykieta* lbl_status{nullptr};
    PoleTekstowe* txt_input{nullptr};
    PoleWyboru* chk_haslo{nullptr};
    PoleWyboru* chk_trojstan{nullptr};
    Przycisk* btn_zatwierdz{nullptr};
    Przycisk* btn_batch{nullptr};
    Kolumna* glowna_kolumna{nullptr};
    Suwak* suwak_procent{nullptr};
    Suwak* suwak_ujemny{nullptr};
    Suwak* suwak_pionowy{nullptr};
    Suwak* suwak_wylaczony{nullptr};
    Etykieta* lbl_suwak_procent{nullptr};
    Etykieta* lbl_suwak_ujemny{nullptr};
    Etykieta* lbl_suwak_pionowy{nullptr};
    Etykieta* lbl_suwak_wylaczony{nullptr};
    int licznik_zatwierdzen{0};
    int licznik_batch{0};
};

static StanAplikacji g_stan;

static void dopisz_liczbe(char* bufor, size_t& pos, size_t pojemnosc, int n) {
    int cyfry[10];
    int ile = 0;
    int64_t liczba = n;
    if (liczba < 0) {
        if (pos < pojemnosc - 1) bufor[pos++] = '-';
        liczba = -liczba;
    }
    if (liczba == 0) cyfry[ile++] = 0;
    while (liczba > 0) {
        cyfry[ile++] = static_cast<int>(liczba % 10);
        liczba /= 10;
    }
    for (int k = ile - 1; k >= 0 && pos < pojemnosc - 1; --k) {
        bufor[pos++] = '0' + cyfry[k];
    }
    bufor[pos] = '\0';
}

static void aktualizuj_etykiete_suwaka(Suwak*, int wartosc, void* dane) {
    Etykieta* etykieta = static_cast<Etykieta*>(dane);
    if (!etykieta) return;
    char bufor[48] = "Wartosc: ";
    size_t pos = 9;
    dopisz_liczbe(bufor, pos, sizeof(bufor), wartosc);
    etykieta->ustaw_tekst(bufor);
}

static void aktualizuj_status_zatwierdzenia(StanAplikacji* stan) {
    if (!stan || !stan->lbl_status || !stan->txt_input) return;

    stan->licznik_zatwierdzen++;

    char bufor[256];
    const char* p = "Zatwierdzono [";
    size_t pos = 0;
    while (p[pos] != '\0' && pos < sizeof(bufor) - 1) {
        bufor[pos] = p[pos];
        pos++;
    }
    dopisz_liczbe(bufor, pos, sizeof(bufor), stan->licznik_zatwierdzen);

    const char* mid = "]: \"";
    size_t m = 0;
    while (mid[m] != '\0' && pos < sizeof(bufor) - 1) {
        bufor[pos++] = mid[m++];
    }

    const char* txt = stan->txt_input->tekst();
    size_t t = 0;
    while (txt[t] != '\0' && pos < sizeof(bufor) - 3) {
        bufor[pos++] = txt[t++];
    }
    bufor[pos++] = '"';
    bufor[pos] = '\0';

    stan->lbl_status->ustaw_tekst(bufor);
}

// =============================================================================
// Punkt Wejścia
// =============================================================================

extern "C" void _start() {
    AplikacjaGUI aplikacja;

    Okno okno("BursztynUI - Zaawansowane Komponenty GUI", 720, 700);
    okno.ustaw_pozycje(80, 20);

    // Główny kontener pionowy (Kolumna)
    auto& glowna = okno.dodaj<Kolumna>();
    glowna.ustaw_wypelnienie(Marginesy(16, 12, 16, 12));
    glowna.ustaw_odstep(8);
    g_stan.glowna_kolumna = &glowna;

    // 1. Tytuł / Nagłówek
    auto& lbl_tytul = glowna.dodaj<Etykieta>("BursztynUI Komponenty (extronic16B Unicode)");
    lbl_tytul.ustaw_skale_czcionki(1);
    lbl_tytul.ustaw_kolor_tekstu(Kolory::AkcentZloty);
    lbl_tytul.ustaw_margines(Marginesy(0, 0, 0, 2));

    // 2. Dynamiczny pasek statusu
    auto& lbl_status = glowna.dodaj<Etykieta>("Status: Gotowy do wprowadzania i edycji tekstu.");
    lbl_status.ustaw_kolor_tekstu(Kolory::TekstWyciszony);
    lbl_status.ustaw_margines(Marginesy(0, 0, 0, 4));
    g_stan.lbl_status = &lbl_status;

    // 3. Suwaki: dodatni, ujemny, pionowy oraz wyłączony
    auto& panel_suwakow = glowna.dodaj<Kolumna>();
    panel_suwakow.ustaw_kolor_tla(Kolory::PowierzchniaAlternatywna);
    panel_suwakow.ustaw_kolor_ramki(Kolory::AkcentRamka);
    panel_suwakow.ustaw_grubosc_ramki(1);
    panel_suwakow.ustaw_wypelnienie(Marginesy(8, 6, 8, 6));
    panel_suwakow.ustaw_odstep(4);
    panel_suwakow.ustaw_margines(Marginesy(0, 0, 0, 4));

    auto& wiersz_procent = panel_suwakow.dodaj<Wiersz>();
    wiersz_procent.ustaw_wypelnienie(Marginesy(0));
    wiersz_procent.ustaw_odstep(8);
    auto& nazwa_procent = wiersz_procent.dodaj<Etykieta>("0..100, krok 1:");
    nazwa_procent.ustaw_preferowany_rozmiar(Rozmiar(150, 24));
    auto& suwak_procent = wiersz_procent.dodaj<Suwak>();
    suwak_procent.ustaw_zakres(0, 100);
    suwak_procent.ustaw_krok(1);
    suwak_procent.ustaw_wartosc(35);
    suwak_procent.ustaw_pokazywanie_wartosci(true);
    auto& wartosc_procent = wiersz_procent.dodaj<Etykieta>("Wartosc: 35");
    wartosc_procent.ustaw_preferowany_rozmiar(Rozmiar(95, 24));
    suwak_procent.przy_zmianie_wartosci(aktualizuj_etykiete_suwaka, &wartosc_procent);
    g_stan.suwak_procent = &suwak_procent;
    g_stan.lbl_suwak_procent = &wartosc_procent;

    auto& wiersz_ujemny = panel_suwakow.dodaj<Wiersz>();
    wiersz_ujemny.ustaw_wypelnienie(Marginesy(0));
    wiersz_ujemny.ustaw_odstep(8);
    auto& nazwa_ujemny = wiersz_ujemny.dodaj<Etykieta>("-50..50, krok 5:");
    nazwa_ujemny.ustaw_preferowany_rozmiar(Rozmiar(150, 24));
    auto& suwak_ujemny = wiersz_ujemny.dodaj<Suwak>();
    suwak_ujemny.ustaw_zakres(-50, 50);
    suwak_ujemny.ustaw_krok(5);
    suwak_ujemny.ustaw_wartosc(-15);
    auto& wartosc_ujemny = wiersz_ujemny.dodaj<Etykieta>("Wartosc: -15");
    wartosc_ujemny.ustaw_preferowany_rozmiar(Rozmiar(95, 24));
    suwak_ujemny.przy_zmianie_wartosci(aktualizuj_etykiete_suwaka, &wartosc_ujemny);
    g_stan.suwak_ujemny = &suwak_ujemny;
    g_stan.lbl_suwak_ujemny = &wartosc_ujemny;

    auto& wiersz_pionowy = panel_suwakow.dodaj<Wiersz>();
    wiersz_pionowy.ustaw_wypelnienie(Marginesy(0));
    wiersz_pionowy.ustaw_odstep(8);
    auto& nazwa_pionowy = wiersz_pionowy.dodaj<Etykieta>("Suwak pionowy:");
    nazwa_pionowy.ustaw_preferowany_rozmiar(Rozmiar(150, 24));
    auto& suwak_pionowy = wiersz_pionowy.dodaj<Suwak>();
    suwak_pionowy.ustaw_orientacje(Orientacja::Pionowa);
    suwak_pionowy.ustaw_zakres(0, 100);
    suwak_pionowy.ustaw_krok(10);
    suwak_pionowy.ustaw_wartosc(60);
    suwak_pionowy.ustaw_pokazywanie_wartosci(true);
    suwak_pionowy.ustaw_preferowany_rozmiar(Rozmiar(52, 138));
    auto& wartosc_pionowy = wiersz_pionowy.dodaj<Etykieta>("Wartosc: 60");
    wartosc_pionowy.ustaw_preferowany_rozmiar(Rozmiar(95, 24));
    suwak_pionowy.przy_zmianie_wartosci(aktualizuj_etykiete_suwaka, &wartosc_pionowy);
    g_stan.suwak_pionowy = &suwak_pionowy;
    g_stan.lbl_suwak_pionowy = &wartosc_pionowy;

    auto& wiersz_wylaczony = panel_suwakow.dodaj<Wiersz>();
    wiersz_wylaczony.ustaw_wypelnienie(Marginesy(0));
    wiersz_wylaczony.ustaw_odstep(8);
    auto& nazwa_wylaczony = wiersz_wylaczony.dodaj<Etykieta>("Suwak wylaczony:");
    nazwa_wylaczony.ustaw_preferowany_rozmiar(Rozmiar(150, 24));
    auto& suwak_wylaczony = wiersz_wylaczony.dodaj<Suwak>();
    suwak_wylaczony.ustaw_zakres(0, 100);
    suwak_wylaczony.ustaw_wartosc(70);
    suwak_wylaczony.ustaw_wlaczona(false);
    auto& wartosc_wylaczony = wiersz_wylaczony.dodaj<Etykieta>("Wartosc: 70");
    wartosc_wylaczony.ustaw_preferowany_rozmiar(Rozmiar(95, 24));
    suwak_wylaczony.przy_zmianie_wartosci(aktualizuj_etykiete_suwaka, &wartosc_wylaczony);
    g_stan.suwak_wylaczony = &suwak_wylaczony;
    g_stan.lbl_suwak_wylaczony = &wartosc_wylaczony;

    // 4. Zaawansowane Pole Tekstowe (UTF-8, zaznaczanie, schowek, cofanie)
    auto& txt_input = glowna.dodaj<PoleTekstowe>("Wpisz tekst (np. Zażółć gęślą jaźń)...");
    txt_input.ustaw_tekst("Bursztyn OS: Zażółć gęślą jaźń!");
    txt_input.ustaw_margines(Marginesy(0, 0, 0, 6));
    txt_input.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    txt_input.przy_zmianie([](PoleTekstowe*, const char* t, void*) {
        if (g_stan.lbl_status) {
            char buf[128] = "Edycja: ";
            size_t i = 8;
            size_t j = 0;
            while (t[j] != '\0' && i < sizeof(buf) - 1) {
                buf[i++] = t[j++];
            }
            buf[i] = '\0';
            g_stan.lbl_status->ustaw_tekst(buf);
        }
    });
    txt_input.przy_zatwierdzeniu([](PoleTekstowe*, const char*, void* dane) {
        aktualizuj_status_zatwierdzenia(static_cast<StanAplikacji*>(dane));
    }, &g_stan);
    g_stan.txt_input = &txt_input;

    // 4. Zagnieżdżony panel opcji (Wiersz w ramce)
    auto& panel_opcji = glowna.dodaj<Wiersz>();
    panel_opcji.ustaw_kolor_tla(Kolory::PowierzchniaAlternatywna);
    panel_opcji.ustaw_kolor_ramki(Kolory::AkcentRamka);
    panel_opcji.ustaw_grubosc_ramki(1);
    panel_opcji.ustaw_promien_zaokraglenia(4);
    panel_opcji.ustaw_wypelnienie(Marginesy(10, 6, 10, 6));
    panel_opcji.ustaw_odstep(16);
    panel_opcji.ustaw_margines(Marginesy(0, 0, 0, 6));
    panel_opcji.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);

    // 4a. PoleWyboru 1: Przełącznik maskowania hasła
    auto& chk_haslo = panel_opcji.dodaj<PoleWyboru>("Tryb Hasla (*)", false);
    chk_haslo.przy_przelaczeniu([](PoleWyboru*, bool czy_zaznaczone, void*) {
        if (g_stan.txt_input) {
            g_stan.txt_input->ustaw_tryb_hasla(czy_zaznaczone);
        }
        if (g_stan.lbl_status) {
            g_stan.lbl_status->ustaw_tekst(czy_zaznaczone ? "Tryb hasla aktywny (znaki zamaskowane)" : "Tryb zwykly aktywny");
        }
    });
    g_stan.chk_haslo = &chk_haslo;

    // 4b. PoleWyboru 2: Trójstanowe pole wyboru
    auto& chk_trojstan = panel_opcji.dodaj<PoleWyboru>("Trojstan (Auto/Wl/Wyl)", false);
    chk_trojstan.ustaw_trojstanowe(true);
    chk_trojstan.przy_zmianie_stanu([](PoleWyboru*, StanWyboru stan, void*) {
        if (g_stan.lbl_status) {
            if (stan == StanWyboru::Zaznaczone) {
                g_stan.lbl_status->ustaw_tekst("Trojstan: ZAZNACZONE");
            } else if (stan == StanWyboru::Nieokreslone) {
                g_stan.lbl_status->ustaw_tekst("Trojstan: NIEOKRESLONE (Stan posredni)");
            } else {
                g_stan.lbl_status->ustaw_tekst("Trojstan: NIEZAZNACZONE");
            }
        }
    });
    g_stan.chk_trojstan = &chk_trojstan;

    // 5. Dolny wiersz przycisków akcji
    auto& wiersz_przyciskow = glowna.dodaj<Wiersz>();
    wiersz_przyciskow.ustaw_odstep(12);
    wiersz_przyciskow.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);

    // 5a. Przycisk Główny (Zatwierdź)
    auto& btn_zatwierdz = wiersz_przyciskow.dodaj<Przycisk>("Zatwierdz tekst");
    btn_zatwierdz.ustaw_preferowany_rozmiar(Rozmiar(230, 32));
    btn_zatwierdz.ustaw_kolor_tla(Kolory::AkcentWcisniety);
    btn_zatwierdz.ustaw_kolor_tekstu(Kolory::TekstCiemny);
    btn_zatwierdz.ustaw_promien_zaokraglenia(3);
    btn_zatwierdz.przy_kliknieciu([](Przycisk*, void* dane) {
        aktualizuj_status_zatwierdzenia(static_cast<StanAplikacji*>(dane));
    }, &g_stan);
    g_stan.btn_zatwierdz = &btn_zatwierdz;

    // 5b. Przycisk Batch Update (Demonstracja rozpoczęcia/zakończenia aktualizacji)
    auto& btn_batch = wiersz_przyciskow.dodaj<Przycisk>("Aktualizacja grupowa (Batch)");
    btn_batch.ustaw_preferowany_rozmiar(Rozmiar(260, 32));
    btn_batch.ustaw_kolor_tla(Kolory::PowierzchniaNajechana);
    btn_batch.ustaw_kolor_tekstu(Kolory::TekstBialy);
    btn_batch.ustaw_kolor_ramki(Kolory::RamkaDomyslna);
    btn_batch.ustaw_promien_zaokraglenia(3);
    btn_batch.przy_kliknieciu([](Przycisk*, void* dane) {
        StanAplikacji* stan = static_cast<StanAplikacji*>(dane);
        if (!stan || !stan->glowna_kolumna) return;

        stan->licznik_batch++;

        // Demonstracja grupowania zmian bez migotania odrysowywania
        stan->glowna_kolumna->rozpocznij_aktualizacje();

        if (stan->txt_input) {
            char nowy_tekst[128] = "Wsad wsadowy #";
            size_t p = 14;
            dopisz_liczbe(nowy_tekst, p, sizeof(nowy_tekst), stan->licznik_batch);
            const char* tail = ": Zażółć gęślą jaźń";
            size_t t = 0;
            while (tail[t] != '\0' && p < sizeof(nowy_tekst) - 1) {
                nowy_tekst[p++] = tail[t++];
            }
            nowy_tekst[p] = '\0';
            stan->txt_input->ustaw_tekst(nowy_tekst);
        }

        if (stan->lbl_status) {
            char buf[128] = "Batch #";
            size_t pos = 7;
            dopisz_liczbe(buf, pos, sizeof(buf), stan->licznik_batch);
            const char* tail = ": atomowa aktualizacja wykonana!";
            size_t t = 0;
            while (tail[t] != '\0' && pos < sizeof(buf) - 1) {
                buf[pos++] = tail[t++];
            }
            buf[pos] = '\0';
            stan->lbl_status->ustaw_tekst(buf);
        }

        stan->glowna_kolumna->zakoncz_aktualizacje();
    }, &g_stan);
    g_stan.btn_batch = &btn_batch;

    // Ustawienie początkowego skupienia na polu tekstowym
    okno.ustaw_fokus(&txt_input);

    aplikacja.uruchom(okno);

    bws_zakoncz_proces();
}
