/*
 * Bursztyn OS - BursztynUI Demo Application (Ring 3)
 * Pokaz możliwości nowoczesnego polskiego toolkitu GUI BursztynUI:
 *  - Drzewo kontrolek w trybie retained-mode (Panel, Kolumna, Wiersz, Etykieta, PoleTekstowe, PoleWyboru, Przycisk)
 *  - Automatyczny układ (faza pomiaru i rozmieszczenia: zmierz & rozmiesc)
 *  - Odrysowywanie dirty-region (faza rysowania)
 *  - Obsługa zdarzeń i skupienia (fokus i dyspozytor zdarzeń)
 *  - Pełne wsparcie dla polskich znaków UTF-8 w Polu Tekstowym
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
    PoleWyboru* chk_opcja{nullptr};
    Przycisk* btn_akcja{nullptr};
    int licznik_klikniec{0};
};

static StanAplikacji g_stan;

static void przy_kliknieciu_przycisku(Przycisk* nadawca, void* dane) {
    (void)nadawca;
    StanAplikacji* stan = static_cast<StanAplikacji*>(dane);
    if (!stan || !stan->lbl_status || !stan->txt_input) return;

    stan->licznik_klikniec++;

    const char* txt = stan->txt_input->tekst();
    char bufor[256];
    if (txt && txt[0] != '\0') {
        size_t i = 0;
        const char* przedrostek = "Wpisano: ";
        while (przedrostek[i] != '\0' && i < sizeof(bufor) - 1) {
            bufor[i] = przedrostek[i];
            i++;
        }
        size_t j = 0;
        while (txt[j] != '\0' && i < sizeof(bufor) - 1) {
            bufor[i++] = txt[j++];
        }
        bufor[i] = '\0';
    } else {
        bufor[0] = 'K'; bufor[1] = 'l'; bufor[2] = 'i'; bufor[3] = 'k';
        bufor[4] = 'n'; bufor[5] = 'i'; bufor[6] = 'e'; bufor[7] = 't';
        bufor[8] = 'o'; bufor[9] = ':'; bufor[10] = ' ';
        int n = stan->licznik_klikniec;
        int cyfry[10];
        int ile = 0;
        if (n == 0) cyfry[ile++] = 0;
        while (n > 0) {
            cyfry[ile++] = n % 10;
            n /= 10;
        }
        size_t pos = 11;
        for (int k = ile - 1; k >= 0 && pos < sizeof(bufor) - 1; --k) {
            bufor[pos++] = '0' + cyfry[k];
        }
        bufor[pos] = '\0';
    }

    stan->lbl_status->ustaw_tekst(bufor);
}

static void przy_przelaczeniu_opcji(PoleWyboru* nadawca, bool czy_zaznaczone, void* dane) {
    (void)nadawca;
    StanAplikacji* stan = static_cast<StanAplikacji*>(dane);
    if (!stan || !stan->lbl_status) return;

    if (czy_zaznaczone) {
        stan->lbl_status->ustaw_tekst("Opcja wlaczona (zaznaczono PoleWyboru)");
    } else {
        stan->lbl_status->ustaw_tekst("Opcja wylaczona");
    }
}

// =============================================================================
// Punkt Wejścia
// =============================================================================

extern "C" void _start() {
    AplikacjaGUI aplikacja;

    Okno okno("BursztynUI Demo", 460, 300);
    okno.ustaw_pozycje(160, 120);

    // Główny panel pionowy (Kolumna)
    auto& kolumna = okno.dodaj<Kolumna>();
    kolumna.ustaw_wypelnienie(Marginesy(16, 16, 16, 16));
    kolumna.ustaw_odstep(10);

    // 1. Tytuł / Etykieta nagłówka
    auto& lbl_tytul = kolumna.dodaj<Etykieta>("BursztynUI v0.1 - Zestaw Kontrolek");
    lbl_tytul.ustaw_skale_fontu(1);
    lbl_tytul.ustaw_kolor_tekstu(Kolory::AkcentZloty);
    lbl_tytul.ustaw_margines(Marginesy(0, 0, 0, 4));

    // 2. Etykieta dynamicznego statusu [ Etykieta ]
    auto& lbl_status = kolumna.dodaj<Etykieta>("Wpisz polski tekst i kliknij przycisk:");
    lbl_status.ustaw_kolor_tekstu(Kolory::TekstWyciszony);
    lbl_status.ustaw_margines(Marginesy(0, 0, 0, 6));
    g_stan.lbl_status = &lbl_status;

    // 3. Pole edycyjne [ PoleTekstowe ]
    auto& txt_input = kolumna.dodaj<PoleTekstowe>("Wpisz np. Zażółć gęślą jaźń...");
    txt_input.ustaw_margines(Marginesy(0, 0, 0, 8));
    txt_input.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    txt_input.przy_zatwierdzeniu([](PoleTekstowe*, const char*, void* dane) {
        przy_kliknieciu_przycisku(nullptr, dane);
    }, &g_stan);
    g_stan.txt_input = &txt_input;

    // 4. Pole wyboru [ PoleWyboru ]
    auto& chk_opcja = kolumna.dodaj<PoleWyboru>("Wlacz zaawansowane formatowanie", false);
    chk_opcja.ustaw_margines(Marginesy(0, 0, 0, 10));
    chk_opcja.przy_przelaczeniu(przy_przelaczeniu_opcji, &g_stan);
    g_stan.chk_opcja = &chk_opcja;

    // 5. Przycisk akcji [ Przycisk ]
    auto& btn_akcja = kolumna.dodaj<Przycisk>("Zatwierdz tekst (Aktualizuj etykiete)");
    btn_akcja.ustaw_preferowany_rozmiar(Rozmiar(280, 32));
    btn_akcja.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    btn_akcja.przy_kliknieciu(przy_kliknieciu_przycisku, &g_stan);
    g_stan.btn_akcja = &btn_akcja;

    okno.ustaw_fokus(&txt_input);

    aplikacja.uruchom(okno);

    bws_zakoncz_proces();
}
