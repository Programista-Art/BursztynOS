import re

with open("shell.cpp", "r") as f:
    content = f.read()

# Replace the inner body of shell_rysuj_terminal
rysuj_pattern = r'void shell_rysuj_terminal\(\) \{[\s\S]*?int input_text_x ='

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

    int text_x = client_x + 8;
    int cur_y = client_y + 4;

    for (int i = 0; i < max_linii; ++i) {
        int idx = pierwsza_do_rysowania + i;
        if (idx >= g_liczba_linii) break;
        int real_idx = (g_pierwsza_linia + idx) % MAXSCROLLBACKLINES;

        if (g_linie[real_idx].len > 0) {
            gui_wypisz_tekst_kolor_skala(text_x, cur_y, tekst_kolor, 100, g_linie[real_idx].tekst);
        }
        cur_y += 16;
    }

    int input_text_x ="""

content = re.sub(rysuj_pattern, new_rysuj, content)

with open("shell.cpp", "w") as f:
    f.write(content)
