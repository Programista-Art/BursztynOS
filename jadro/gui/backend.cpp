struct Argumenty {
    static constexpr size_t MAX_ARG = 8;
    char* dane[MAX_ARG];
    size_t liczba;
};

bool parse_argumenty(
    char* linia,
    Argumenty* wynik
) {
    if (!linia ||
        !wynik) {
        return false;
    }

    wynik->liczba = 0;

    for (size_t i = 0;
         i < Argumenty::MAX_ARG;
         ++i) {
        wynik->dane[i] = nullptr;
    }

    size_t i = 0;

    while (linia[i] != '\0') {
        while (jest_biala_spacja(
                   linia[i])) {
            ++i;
        }

        if (linia[i] == '\0') {
            break;
        }

        if (wynik->liczba >=
            Argumenty::MAX_ARG) {
            return false;
        }

        bool cudzyslow =
            linia[i] == '"';

        if (cudzyslow) {
            ++i;
            wynik->dane[wynik->liczba]=&linia[i];
            if(linia[i]=='"')return false;
            while(linia[i]!='\0'&&linia[i]!='"')++i;
            if(linia[i]!='"')return false;
            linia[i++]='\0';
            if(linia[i]!='\0'&&!jest_biala_spacja(linia[i]))return false;
        }else{
            wynik->dane[wynik->liczba]=&linia[i];
            while(linia[i]!='\0'&&!jest_biala_spacja(linia[i]))++i;
            if(linia[i]!='\0')linia[i++]='\0';
        }
        while(jest_biala_spacja(linia[i]))++i;

        ++wynik->liczba;
    }

    return true;
}

/* =========================================================================
 * 6. SCIEZKI
 * ========================================================================= */

bool utworz_sciezke_absolutna(
    const char* wejscie,
    char* wyjscie,
    size_t pojemnosc
) {
    if (!wejscie ||
        !wyjscie ||
        pojemnosc < 2) {
        return false;
    }

    size_t dlugosc = 0;

    if (!tekst_zakonczony_w_limicie(
            wejscie,
            pojemnosc,
            &dlugosc)) {
        return false;
    }

    if (dlugosc == 0) {
        return false;
    }

    if (wejscie[0] == '/') {
        return kopiuj_tekst(
            wyjscie,
            pojemnosc,
            wejscie
        );
    }

    /*
     * Potrzebujemy '/' + wejscie + '\0'.
     */
    if (dlugosc + 2 >
        pojemnosc) {
        return false;
    }

    wyjscie[0] = '/';

    for (size_t i = 0;
         i <= dlugosc;
         ++i) {
        wyjscie[i + 1] =
            wejscie[i];
    }

    return true;
}

/* =========================================================================
 * 7. IPv4 / DNS
 * ========================================================================= */

bool parsuj_ipv4(
    const char* tekst,
    uint8_t wynik[4]
) {
    if (!tekst ||
        !wynik) {
        return false;
    }

    size_t pos = 0;

    for (size_t oktet = 0;
         oktet < 4;
         ++oktet) {

        if (tekst[pos] < '0' ||
            tekst[pos] > '9') {
            return false;
        }

        uint32_t wartosc = 0;
        size_t cyfr = 0;

        while (tekst[pos] >= '0' &&
               tekst[pos] <= '9') {

            if (cyfr >= 3) {
                return false;
            }

            wartosc =
                wartosc * 10U +
                static_cast<uint32_t>(
                    tekst[pos] - '0'
                );

            if (wartosc > 255U) {
                return false;
            }

            ++cyfr;
            ++pos;
        }

        wynik[oktet] =
            static_cast<uint8_t>(
                wartosc
            );

        if (oktet < 3) {
            if (tekst[pos] != '.') {
                return false;
            }

            ++pos;
        } else {
            if (tekst[pos] != '\0') {
                return false;
            }
        }
    }

    return true;
}

void wypisz_ipv4(
    const uint8_t ip[4]
) {
    if (!ip) {
        return;
    }

    char liczba[4] = {};

    for (size_t i = 0;
         i < 4;
         ++i) {

        uint_do_str(
            ip[i],
            liczba,
            sizeof(liczba)
        );

        wypisz(liczba);

        if (i != 3) {
            wypisz(".");
        }
    }
}

/* =========================================================================
 * 8. HISTORIA
 * ========================================================================= */

void dodaj_do_historii(
    const char* komenda
) {
    if (!komenda ||
        komenda[0] == '\0') {
        return;
    }

    for (size_t i = HISTORIA_MAX - 1;
         i > 0;
         --i) {

        for (size_t j = 0;
             j < SHELL_HISTORY_ENTRY_CAPACITY;
             ++j) {

            historia[i][j] =
                historia[i - 1][j];
        }
    }

    wyzeruj(
        historia[0],
        SHELL_HISTORY_ENTRY_CAPACITY
    );

    (void)kopiuj_tekst(
        historia[0],
        SHELL_HISTORY_ENTRY_CAPACITY,
        komenda
    );

    if (historia_ilosc <
        HISTORIA_MAX) {
        ++historia_ilosc;
    }
}

/* =========================================================================
 * 9. RDTSC - TYLKO DO ZABAWY / LOSUJ
 * ========================================================================= */

uint64_t pobierz_cykle() {
    uint32_t lo = 0;
    uint32_t hi = 0;

    asm volatile(
        "rdtsc"
        : "=a"(lo),
          "=d"(hi)
        :
        : "memory"
    );

    return
        (static_cast<uint64_t>(hi) << 32) |
        static_cast<uint64_t>(lo);
}

/* =========================================================================
 * 10. POMOC
 * ========================================================================= */

void wypisz_pomoc() {
    wypisz(
        "Dostępne polecenia:

"
        "  Sieć
"
        "    ping <IPv4|domena>           Sprawdza dostępność hosta przez ICMP.
"
        "    pobierz <host> <url> <plik>  Pobiera plik HTTP i zapisuje go w BSP2.
"
        "
"
        "  Pliki i katalogi
"
        "    utworz <plik>                Tworzy pusty plik.
"
        "    zapisz <plik> <tekst>        Zapisuje tekst do pliku.
"
        "    czytaj <plik>                Wyświetla zawartość pliku.
"
        "    pliki [katalog]              Wyświetla zawartość katalogu.
"
        "    usun <ścieżka>               Usuwa plik lub pusty katalog.
"
        "    zmiennazwe <ścieżka> <nazwa> Zmienia nazwę pliku lub katalogu.
"
        "
"
        "  Powłoka
"
        "    gdzie                        Wyświetla bieżący katalog.
"
        "    historia                     Wyświetla historię poleceń.
"
        "    czysc                        Czyści terminal.
"
        "    dzwiek                       Odtwarza test HDA 880 Hz.
"
        "    losuj                        Losuje liczbę.
"
        "    pisz <tekst>                 Wyświetla tekst.
"
        "
"
        "  System
"
        "    notatnik                     Uruchamia Notatnik.
"
        "    kalkulator                   Uruchamia Kalkulator.
"
        "    przegladarka                 Uruchamia przeglądarkę Husarz.
"
        "    uruchom <plik.bur>           Uruchamia program .bur.
"
        "    pulpit | wyjdz | exit        Kończy powłokę.
"
        "    czas                         Pokazuje czas RTC.
"
        "    system                       Informacje o systemie.
"
        "    wersja                       Pokazuje aktualną wersję systemu.
"
        "    aktualizacja [sprawdz]       Pokazuje wersję i stan aktualizacji.
"
        "    pci                          Wyświetla /logi/pci.txt.
"
        "    pomoc                        Pokazuje ten ekran pomocy.
"
    );
}


} // namespace

/* =========================================================================
 * 13. NAGLOWEK BINARNY APLIKACJI
 * ========================================================================= */

extern "C"
__attribute__((noreturn))
void _start();

extern "C" {

__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},

    reinterpret_cast<uint64_t>(
        &_start
    ),

    BUR_TEXT_OFFSET,
    BUR_TEXT_SIZE,
    BUR_TEXT_VADDR,

    BUR_DATA_OFFSET,
    BUR_DATA_SIZE,
    BUR_DATA_VADDR
};

} // extern "C"

/* =========================================================================
 * 14. ENTRY POINT
 * ========================================================================= */

extern "C"
__attribute__((noreturn))
void _start() {
    gui_legacy_wlacz();
    gui_pobierz_rozdzielczosc(&shell_screen_w, &shell_screen_h);
    GuiMonitorInfo monitor{};
    if (gui_pobierz_monitor(0, &monitor)) {
        shell_work_x = monitor.work_x;
        shell_work_y = monitor.work_y;
        shell_work_w = monitor.work_szer;
        shell_work_h = monitor.work_wys;
    } else {
        shell_work_w = shell_screen_w;
        shell_work_h = shell_screen_h;
    }
    if (bws_utworz_warstwe(shell_x, shell_y, shell_w, shell_h, 10) < 0)
        gui_zakoncz_aplikacje();
    (void)gui_ustaw_tytul_okna("Powloka Bursztynowa");
    gui_ustaw_przejecie_myszy(true);
    shell_rysuj_terminal();

    wypisz(
        "\n"
        "==================================================\n"
        " Powłoka Bursztynowa bsh v2.3\n"
        " Autor: Dymitr Wygowski"
        " e-mail: programista.art@gmail.com"
        " Ring 3 / BWS / BSP2 / PZB\n"
        "==================================================\n"
        "Wpisz 'pomoc', aby zobaczyć polecenia.\n"
    );

    char linia[SHELL_INPUT_CAPACITY] = {};

    for (;;) {
        

        const bool cala_linia =
            pobierz_linie(
                linia,
                sizeof(linia)
            );

        wypisz("\n");

        if (!cala_linia) {
            wypisz(
                "Błąd: polecenie przekracza limit 2047 bajtow i zostało odrzucone.\n"
            );
            continue;
        }

        if (linia[0] == '\0') {
            continue;
        }

        Argumenty argumenty{};

        if(!kopiuj_tekst(komenda_dla_historii,sizeof(komenda_dla_historii),linia)){
            wypisz("Błąd: nie można zapisać komendy w historii.\n");
            continue;
        }

        if (!parse_argumenty(
                linia,
                &argumenty)) {

            wypisz(
                "Błąd składni: za dużo argumentów lub brakuje zamykającego cudzysłowu.\n"
            );
            continue;
        }

        if (argumenty.liczba == 0) {
            continue;
        }

        dodaj_do_historii(
            komenda_dla_historii
        );

        wykonaj_polecenie(
            argumenty
        );
    }
}
