import re

with open("shell.cpp", "r") as f:
    content = f.read()

# 1. Add missing global variables
globals_add = """bool shell_drag = false;
int shell_drag_mouse_x = 0;
int shell_drag_mouse_y = 0;
int shell_drag_window_x = 0;
int shell_drag_window_y = 0;
"""
content = re.sub(r'(bool shell_maximized = false;\nbool shell_minimized = false;\nint shell_restore_x = 20, shell_restore_y = 20, shell_restore_w = 700, shell_restore_h = 450;\n)', r'\1\n' + globals_add, content)

# 2. Fix _start()
content = content.replace('    gui_ustaw_przejecie_myszy(true);\n', '')

# 3. Fix shell_rysuj_terminal() to draw window chrome
old_rysuj = """void shell_rysuj_terminal() {
    if (shell_minimized) return;


    const BursztynMotyw* motyw = gui_pobierz_motyw();
    if (!motyw) motyw = &g_domyslny_motyw_fallback;

    uint32_t tlo = motyw->oknotlo;
    uint32_t ramka = motyw->ramka;
    uint32_t tekst_kolor = motyw->tekst;
    uint32_t akcent = motyw->akcent;

    KontekstRysowania ctx;
    ctx.dodaj_przyciecie(Prostokat(shell_x, shell_y, shell_w, shell_h));

    ctx.rysuj_prostokat(Prostokat(shell_x, shell_y, shell_w, shell_h), tlo);

    int input_y = shell_y + shell_h - INPUT_LINE_HEIGHT;
    ctx.rysuj_linie_pozioma(shell_x, input_y, shell_w, ramka);

    int max_linii = (shell_h - INPUT_LINE_HEIGHT - 8) / 16;
    if (max_linii < 1) max_linii = 1;

    if (g_pasek_przewijania) {
        g_pasek_przewijania->ustaw_geometrie(shell_w - SCROLLBAR_WIDTH - 2, 4, SCROLLBAR_WIDTH, shell_h - INPUT_LINE_HEIGHT - 8);
        g_pasek_przewijania->ustaw_rozmiar_strony(max_linii);
        int max_scroll = g_liczba_linii - max_linii;
        if (max_scroll < 0) max_scroll = 0;
        g_pasek_przewijania->ustaw_zakres(0, max_scroll);

        if (!g_scrolling) {
            g_pasek_przewijania->ustaw_wartosc(max_scroll);
        }

        ctx.dodaj_przesuniecie(Punkt(shell_x, shell_y));
        g_pasek_przewijania->rysuj(ctx);
        ctx.zdejmij_przesuniecie();

        g_scroll_offset = max_scroll - g_pasek_przewijania->wartosc();
    }

    int pierwsza_do_rysowania = g_liczba_linii - max_linii - g_scroll_offset;
    if (pierwsza_do_rysowania < 0) pierwsza_do_rysowania = 0;

    int text_y = shell_y + 4;
"""

new_rysuj = """void shell_rysuj_terminal() {
    gui_rysuj_okno(shell_x, shell_y, shell_w, shell_h, "Powłoka Bursztynowa");
    gui_rysuj_standardowa_belke(shell_x, shell_y, shell_w, "Powłoka Bursztynowa", shell_maximized);

    if (shell_minimized) { gui_odswiez(); return; }

    const BursztynMotyw* motyw = gui_pobierz_motyw();
    if (!motyw) motyw = &g_domyslny_motyw_fallback;

    uint32_t tlo = motyw->oknotlo;
    uint32_t ramka = motyw->ramka;
    uint32_t tekst_kolor = motyw->tekst;
    uint32_t akcent = motyw->akcent;

    int client_x = shell_x + 4;
    int client_y = shell_y + 28;
    int client_w = shell_w - 8;
    int client_h = shell_h - 32;

    KontekstRysowania ctx;
    ctx.dodaj_przyciecie(Prostokat(client_x, client_y, client_w, client_h));

    ctx.rysuj_prostokat(Prostokat(client_x, client_y, client_w, client_h), tlo);

    int input_y = client_y + client_h - INPUT_LINE_HEIGHT;
    ctx.rysuj_linie_pozioma(client_x, input_y, client_w, ramka);

    int max_linii = (client_h - INPUT_LINE_HEIGHT - 8) / 16;
    if (max_linii < 1) max_linii = 1;

    if (g_pasek_przewijania) {
        g_pasek_przewijania->ustaw_geometrie(client_w - SCROLLBAR_WIDTH - 2, 4, SCROLLBAR_WIDTH, client_h - INPUT_LINE_HEIGHT - 8);
        g_pasek_przewijania->ustaw_rozmiar_strony(max_linii);
        int max_scroll = g_liczba_linii - max_linii;
        if (max_scroll < 0) max_scroll = 0;
        g_pasek_przewijania->ustaw_zakres(0, max_scroll);

        if (!g_scrolling) {
            g_pasek_przewijania->ustaw_wartosc(max_scroll);
        }

        ctx.dodaj_przesuniecie(Punkt(client_x, client_y));
        g_pasek_przewijania->rysuj(ctx);
        ctx.zdejmij_przesuniecie();

        g_scroll_offset = max_scroll - g_pasek_przewijania->wartosc();
    }

    int pierwsza_do_rysowania = g_liczba_linii - max_linii - g_scroll_offset;
    if (pierwsza_do_rysowania < 0) pierwsza_do_rysowania = 0;

    int text_y = client_y + 4;
"""
content = content.replace(old_rysuj, new_rysuj)

# 4. Fix input_text_x coordinate
old_input_text_x = """    int input_text_x = shell_x + 8;
    int input_text_y = input_y + 7;"""
new_input_text_x = """    int input_text_x = client_x + 8;
    int input_text_y = input_y + 7;"""
content = content.replace(old_input_text_x, new_input_text_x)

# 5. Fix mouse handling
old_mouse = """        if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN || e.typ == BWS_ZDARZENIE_MYSZ_RUCH || e.typ == BWS_ZDARZENIE_MYSZ_UP) {
            gui_akcja_belki akcja = gui_hit_test_belki(e.x, e.y, shell_x, shell_y, shell_w);
            if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN && akcja == GUI_BELKA_ZAMKNIJ) gui_zakoncz_aplikacje();

            if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN && akcja == GUI_BELKA_MINIMALIZUJ) {
                shell_minimized = gui_minimalizuj_okno();
                gui_ustaw_capture_myszy(false);
                continue;
            }
            if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN && akcja == GUI_BELKA_MAKSYMALIZUJ) {
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
                gui_ustaw_geometrie_okna(shell_x, shell_y, shell_w, shell_h);
                shell_rysuj_terminal();
                continue;
            }

            if (g_pasek_przewijania && e.x >= shell_x && e.y >= shell_y) {
                ZdarzenieMyszki mz{};
                mz.typ = e.typ == BWS_ZDARZENIE_MYSZ_DOWN ? TypZdarzenia::WcisniecieMyszy :
                         e.typ == BWS_ZDARZENIE_MYSZ_UP ? TypZdarzenia::ZwolnienieMyszy : TypZdarzenia::RuchMyszy;
                mz.x = e.x - shell_x;
                mz.y = e.y - shell_y;
                mz.przycisk = (e.kod == 1) ? 1 : 0;

                if (mz.typ == TypZdarzenia::WcisniecieMyszy) g_pasek_przewijania->przy_wcisnieciu_myszy(mz);
                else if (mz.typ == TypZdarzenia::ZwolnienieMyszy) g_pasek_przewijania->przy_zwolnieniu_myszy(mz);
                else if (mz.typ == TypZdarzenia::RuchMyszy) g_pasek_przewijania->przy_ruchu_myszy(mz);

                if (g_pasek_przewijania->czy_brudna()) {
                    g_scrolling = true;
                    shell_rysuj_terminal();
                    g_pasek_przewijania->uniewaznij();
                }
            }
            continue;
        }"""

new_mouse = """        if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN || e.typ == BWS_ZDARZENIE_MYSZ_RUCH || e.typ == BWS_ZDARZENIE_MYSZ_UP) {
            int client_x = shell_x + 4;
            int client_y = shell_y + 28;
            int client_w = shell_w - 8;
            int client_h = shell_h - 32;

            if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN) {
                gui_akcja_belki akcja = gui_hit_test_belki(e.x, e.y, shell_x, shell_y, shell_w);
                if (akcja == GUI_BELKA_ZAMKNIJ) gui_zakoncz_aplikacje();
                if (akcja == GUI_BELKA_MINIMALIZUJ) {
                    shell_minimized = gui_minimalizuj_okno();
                    shell_drag = false;
                    gui_ustaw_capture_myszy(false);
                    continue;
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
                    if (!gui_ustaw_geometrie_okna(shell_x, shell_y, shell_w, shell_h)) gui_zakoncz_aplikacje();
                    shell_rysuj_terminal();
                    continue;
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
                    bws_przesun_warstwe(shell_x, shell_y);
                }
            } else if (e.typ == BWS_ZDARZENIE_MYSZ_UP && shell_drag) {
                shell_drag = false;
                gui_ustaw_capture_myszy(false);
            }

            if (g_pasek_przewijania && e.x >= client_x && e.x < client_x + client_w && e.y >= client_y && e.y < client_y + client_h) {
                ZdarzenieMyszki mz{};
                mz.typ = e.typ == BWS_ZDARZENIE_MYSZ_DOWN ? TypZdarzenia::WcisniecieMyszy :
                         e.typ == BWS_ZDARZENIE_MYSZ_UP ? TypZdarzenia::ZwolnienieMyszy : TypZdarzenia::RuchMyszy;
                mz.x = e.x - client_x;
                mz.y = e.y - client_y;
                mz.przycisk = (e.kod == 1) ? 1 : 0;

                if (mz.typ == TypZdarzenia::WcisniecieMyszy) g_pasek_przewijania->przy_wcisnieciu_myszy(mz);
                else if (mz.typ == TypZdarzenia::ZwolnienieMyszy) g_pasek_przewijania->przy_zwolnieniu_myszy(mz);
                else if (mz.typ == TypZdarzenia::RuchMyszy) g_pasek_przewijania->przy_ruchu_myszy(mz);

                if (g_pasek_przewijania->czy_brudna()) {
                    g_scrolling = true;
                    shell_rysuj_terminal();
                    g_pasek_przewijania->uniewaznij();
                }
            }
            continue;
        }"""
content = content.replace(old_mouse, new_mouse)

with open("shell.cpp", "w") as f:
    f.write(content)
