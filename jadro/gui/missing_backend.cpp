            gui_ustaw_capture_myszy(false);
            return;
        }
        if (akcja == GUI_BELKA_MAKSYMALIZUJ) {
            if (!shell_maximized) {
                shell_restore_x=shell_x; shell_restore_y=shell_y;
                shell_restore_w=shell_w; shell_restore_h=shell_h;
                shell_x=shell_work_x; shell_y=shell_work_y;
                shell_w=shell_work_w; shell_h=shell_work_h;
                shell_maximized=true;
            } else {
                shell_x=shell_restore_x; shell_y=shell_restore_y;
                shell_w=shell_restore_w; shell_h=shell_restore_h;
                shell_maximized=false;
            }
            if (!gui_ustaw_geometrie_okna(
                    shell_x, shell_y, shell_w, shell_h))
                gui_zakoncz_aplikacje();
            shell_rysuj_terminal();
            return;
        }
        if (akcja == GUI_BELKA_DRAG && !shell_maximized) {
            shell_drag = true;
            shell_drag_mouse_x = e.x;
            shell_drag_mouse_y = e.y;
            shell_drag_window_x = shell_x;
            shell_drag_window_y = shell_y;
            gui_ustaw_capture_myszy(true);
        }
    } else if (e.typ == BWS_ZDARZENIE_MYSZ_RUCH && shell_drag) {
        const int nx = shell_drag_window_x + (e.x - shell_drag_mouse_x);
        const int ny = shell_drag_window_y + (e.y - shell_drag_mouse_y);
        if (nx != shell_x || ny != shell_y) {
            shell_x = nx;
            shell_y = ny;
            /* Przesuwamy metadane warstwy. Bufor pikseli pozostaje nietkniety. */
            bws_przesun_warstwe(shell_x, shell_y);
        }
    } else if (e.typ == BWS_ZDARZENIE_MYSZ_UP && shell_drag) {
        shell_drag = false;
        gui_ustaw_capture_myszy(false);
    }
}

constexpr uint64_t BUR_TEXT_OFFSET = 0x1000ULL;
constexpr uint64_t BUR_TEXT_SIZE   = 0x8000ULL;
constexpr uint64_t BUR_TEXT_VADDR  = 0x1001000ULL;

constexpr uint64_t BUR_DATA_OFFSET = 0x9000ULL;
constexpr uint64_t BUR_DATA_SIZE   = 0x20000ULL;
constexpr uint64_t BUR_DATA_VADDR  = 0x1009000ULL;

constexpr size_t SHELL_INPUT_CAPACITY = 2048;
constexpr size_t SHELL_HISTORY_ENTRY_CAPACITY = 2048;
constexpr size_t MAX_SCIEZKA = 512;
constexpr size_t MAX_NAZWA_HOSTA = 254;
constexpr size_t MAX_SCIEZKA_HTTP = 2048;

constexpr size_t HISTORIA_MAX = 5;

constexpr uint32_t BUF_CZYTAJ_PLIK = 4096;
constexpr uint32_t BUF_LISTA = 1024;
constexpr uint32_t BUF_PCI = 2048;

/*
 * Bufor jest STATYCZNY celowo.
 *
 * Aktualny wrapper bws_siec_pobierz_http() pakuje wskaznik do gornych
 * 32 bitow jednego argumentu ABI. Dziala wiec tylko, jezeli wskaznik bufora
 * miesci sie w 32 bitach. Statyczne .data/.bss obecnej aplikacji znajduje
 * sie przy 0x1009000, natomiast stos Ring 3 lezy wysoko i nie nadaje sie
 * do tego ABI.
 *
 * Po poprawieniu ABI BWS 29 ten warunek mozna usunac.
 */
constexpr uint32_t HTTP_BUF_SIZE = 32U * 1024U;

alignas(16)
char bufor_http[HTTP_BUF_SIZE] = {};

char historia[HISTORIA_MAX][SHELL_HISTORY_ENTRY_CAPACITY] = {};
char komenda_dla_historii[SHELL_HISTORY_ENTRY_CAPACITY] = {};
size_t historia_ilosc = 0;

/* =========================================================================
 * 2. PROSTE WRAPPERY BWS NIEOBECNE W bursztyn_gui.h
 * ========================================================================= */

bool bsh_wylistuj_katalog(
    const char* sciezka,
    char* bufor,
    uint32_t max_dlugosc
) {
    if (!sciezka ||
        !bufor ||
        max_dlugosc == 0) {
        return false;
    }

    return
        bws_wywolaj(
            6,
            reinterpret_cast<uint64_t>(sciezka),
            reinterpret_cast<uint64_t>(bufor),
            static_cast<uint64_t>(max_dlugosc),
            0
        ) != 0;
}

bool bsh_usun_twor(
    const char* sciezka
) {
    if (!sciezka) {
        return false;
    }

    return
        bws_wywolaj(
            7,
            reinterpret_cast<uint64_t>(sciezka),
            0, 0, 0
        ) != 0;
}

bool bsh_zmien_nazwe(
    const char* sciezka,
    const char* nowa_nazwa
) {
    if (!sciezka ||
        !nowa_nazwa) {
        return false;
    }

    return
        bws_wywolaj(
            8,
            reinterpret_cast<uint64_t>(sciezka),
            reinterpret_cast<uint64_t>(nowa_nazwa),
            0, 0
        ) != 0;
}

bool bsh_pobierz_czas(
    char* bufor
) {
    if (!bufor) {
        return false;
    }

    return
        bws_wywolaj(
            9,
            reinterpret_cast<uint64_t>(bufor),
            0, 0, 0
        ) != 0;
}

bool bsh_uruchom_program(
    const char* sciezka
) {
    if (!sciezka) {
        return false;
    }

    return
        bws_wywolaj(
            10,
            reinterpret_cast<uint64_t>(sciezka),
            0, 0, 0
        ) != 0;
}

bool bsh_ping_ipv4(
    const uint8_t ip[4]
) {
    if (!ip) {
        return false;
    }

    return
        bws_wywolaj(
            11,
            ip[0],
            ip[1],
            ip[2],
            ip[3]
        ) != 0;
}

/* =========================================================================
 * 3. FUNKCJE STRING / PAMIEC BEZ LIBC
 * ========================================================================= */

size_t dlugosc_tekstu_limit(
    const char* tekst,
    size_t limit
) {
    if (!tekst) {
        return 0;
    }

    size_t i = 0;

    while (i < limit &&
           tekst[i] != '\0') {
        ++i;
    }

    return i;
}

bool tekst_zakonczony_w_limicie(
    const char* tekst,
    size_t limit,
    size_t* dlugosc_wyj = nullptr
) {
    if (!tekst ||
        limit == 0) {
        return false;
    }

    for (size_t i = 0;
         i < limit;
         ++i) {

        if (tekst[i] == '\0') {
            if (dlugosc_wyj) {
                *dlugosc_wyj = i;
            }

            return true;
        }
    }

    return false;
}

void wyzeruj(
    void* ptr,
    size_t rozmiar
) {
    if (!ptr) {
        return;
    }

    uint8_t* p =
        static_cast<uint8_t*>(ptr);

    for (size_t i = 0;
         i < rozmiar;
         ++i) {
        p[i] = 0;
    }
}

bool kopiuj_tekst(
    char* cel,
    size_t pojemnosc,
    const char* zrodlo
) {
    if (!cel ||
        pojemnosc == 0 ||
        !zrodlo) {
        return false;
    }

    size_t dlugosc = 0;

    if (!tekst_zakonczony_w_limicie(
            zrodlo,
            pojemnosc,
            &dlugosc)) {
        cel[0] = '\0';
        return false;
    }

    for (size_t i = 0;
         i <= dlugosc;
         ++i) {
        cel[i] = zrodlo[i];
    }

    return true;
}

bool tekst_rowny(
    const char* a,
    const char* b
) {
    if (!a ||
        !b) {
        return false;
    }

    while (*a != '\0' &&
           *b != '\0') {

        if (*a != *b) {
            return false;
        }

        ++a;
        ++b;
    }

    return
        *a == '\0' &&
        *b == '\0';
}

bool jest_biala_spacja(
    char c
) {
    return
        c == ' ' ||
        c == '\t';
}

void wypisz_znak(
    char c
) {
    const int old_row=shell_wiersz;
    shell_dopisz_znak(c);
    shell_rysuj_zmiane_wejscia(old_row);
}

void usun_znak_wejscia_wizualnie() {
    const int old_row=shell_wiersz;
    if(shell_kolumna>0){
        shell_ekran[shell_wiersz][--shell_kolumna]='\0';
    }else if(shell_wiersz>0){
        --shell_wiersz;shell_kolumna=static_cast<int>(VISUAL_COLUMNS);
        shell_ekran[shell_wiersz][--shell_kolumna]='\0';
    }else return;
    if(old_row!=shell_wiersz)shell_rysuj_wiersz(old_row,false);
    shell_rysuj_wiersz(shell_wiersz,true);gui_odswiez();++shell_perf.compose_requests;
}

void odtworz_widoczny_ogon_wejscia(const char* tekst,size_t len){
    if(!tekst)return;
    for(int r=0;r<SHELL_ROWS;++r)for(int c=0;c<=SHELL_COLS;++c)shell_ekran[r][c]='\0';
    const size_t cap=static_cast<size_t>(SHELL_ROWS)*VISUAL_COLUMNS;
    size_t start=len>cap?len-cap:0;shell_wiersz=0;shell_kolumna=0;
    for(size_t i=start;i<len;++i)shell_dopisz_znak(tekst[i]);
    shell_rysuj_terminal();
}

void uint_do_str(
    uint64_t wartosc,
    char* bufor,
    size_t pojemnosc
) {
    if (!bufor ||
        pojemnosc == 0) {
        return;
    }

    if (wartosc == 0) {
        if (pojemnosc >= 2) {
            bufor[0] = '0';
            bufor[1] = '\0';
        } else {
            bufor[0] = '\0';
        }

        return;
    }

    char odwrotnie[32] = {};
    size_t n = 0;

    while (wartosc != 0 &&
           n < sizeof(odwrotnie)) {

        odwrotnie[n++] =
            static_cast<char>(
                '0' +
                (wartosc % 10ULL)
            );

        wartosc /= 10ULL;
    }

    if (n + 1 > pojemnosc) {
        bufor[0] = '\0';
        return;
    }

    size_t out = 0;

    while (n > 0) {
        bufor[out++] =
            odwrotnie[--n];
    }

    bufor[out] = '\0';
}

/* =========================================================================
 * 4. WEJSCIE TERMINALA
 * ========================================================================= */

char pobierz_znak_blokujaco() {
    for (;;) {
        bws_zdarzenie e{};
        if (!gui_czekaj_na_zdarzenie(&e)) continue;
        if (e.typ == BWS_ZDARZENIE_EKRAN) {
            gui_pobierz_rozdzielczosc(&shell_screen_w, &shell_screen_h);
            GuiMonitorInfo monitor{};
            if (gui_pobierz_monitor(0, &monitor)) {
                shell_work_x=monitor.work_x; shell_work_y=monitor.work_y;
                shell_work_w=monitor.work_szer; shell_work_h=monitor.work_wys;
            }
            if (shell_maximized) {
                shell_x=shell_work_x; shell_y=shell_work_y;
                shell_w=shell_work_w; shell_h=shell_work_h;
                (void)gui_ustaw_geometrie_okna(shell_x,shell_y,shell_w,shell_h);
            } else {
                shell_w = 660; shell_h = 360;
                if (bws_utworz_warstwe(shell_x, shell_y, shell_w, shell_h, 10) < 0)
                    gui_zakoncz_aplikacje();
                (void)gui_ustaw_tytul_okna("Powloka Bursztynowa");
            }
            shell_rysuj_terminal();
            continue;
        }
        if (e.typ == BWS_ZDARZENIE_FOCUS && shell_minimized)
            shell_minimized = false;
        if (e.typ == BWS_ZDARZENIE_ZAMKNIJ) gui_zakoncz_aplikacje();
        if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN ||
            e.typ == BWS_ZDARZENIE_MYSZ_RUCH ||
            e.typ == BWS_ZDARZENIE_MYSZ_UP) {
            shell_obsluz_mysz(e);
            continue;
        }
        if (e.typ == BWS_ZDARZENIE_KLAWISZ && e.kod != 0)
            return static_cast<char>(e.kod);
    }
}

/*
 * Zwraca true, jezeli cala linia zmiescila sie w buforze.
 * Po przekroczeniu limitu reszta wejscia jest konsumowana, ale nie zapisywana.
 */
bool pobierz_linie(
    char* bufor,
    size_t pojemnosc
) {
    if (!bufor ||
        pojemnosc < 2) {
        return false;
    }

    size_t pozycja = 0;
    bool przepelnienie = false;

    bufor[0] = '\0';

    for (;;) {
        char c =
            pobierz_znak_blokujaco();

        if (c == '\n' ||
            c == '\r') {

            bufor[pozycja] = '\0';
            return !przepelnienie;
        }

        /*
         * Backspace lub DEL.
         */
        if (c == '\b' ||
            static_cast<uint8_t>(c) == 0x7FU) {

            if (!przepelnienie &&
                pozycja > 0) {
                size_t usunieto=1U;--pozycja;
                while(pozycja>0&&
                      (static_cast<uint8_t>(bufor[pozycja])&0xC0U)==0x80U){
                    --pozycja;++usunieto;
                }
                bufor[pozycja] = '\0';

                if(shell_wiersz==0&&shell_kolumna==0)
                    odtworz_widoczny_ogon_wejscia(bufor,pozycja);
                else for(size_t i=0;i<usunieto;++i)usun_znak_wejscia_wizualnie();
                ++shell_perf.key_count;
            }

            continue;
        }

        /*
         * Klawiatura moze emitowac sekwencje ANSI dla klawiszy specjalnych.
         * Nie wprowadzamy surowego ESC do komendy.
         *
         * Aktualny byte-stream BWS nie pozwala niezawodnie rozpoznac calego
         * zdarzenia klawisza, wiec po prostu pomijamy sam ESC.
         */
        if (static_cast<uint8_t>(c) ==
            0x1BU) {
            continue;
        }

        /*
         * Pomijamy pozostale znaki sterujace poza TAB.
         */
        if (static_cast<uint8_t>(c) <
                0x20U &&
            c != '\t') {
            continue;
        }

        if (przepelnienie) {
            continue;
        }

        if (pozycja + 1 >=
            pojemnosc) {

            przepelnienie = true;
            continue;
        }

        bufor[pozycja++] = c;
        bufor[pozycja] = '\0';

        ++shell_perf.key_count;
        wypisz_znak(c);
    }
}

/* =========================================================================
 * 5. PARSER WIERSZA POLECEN
 * ========================================================================= */

