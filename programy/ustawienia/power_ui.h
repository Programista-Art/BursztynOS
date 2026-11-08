#pragma once
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "content_metrics.h"
namespace SettingsUI {
using namespace BursztynUI;

/*
 * Strona "System i energia".
 *
 * Wszystkie wartosci sa CZYTANE z rzeczywistych zrodel albo z trwalych
 * ustawien per aplikacja - nic nie jest generowane dla wygladu:
 *
 *   - rozdzielczosc i skala pochodza z BWS 62 (monitor_info),
 *   - progi wygaszania i limit pamieci z /ustawienia/system.cfg,
 *   - status dzwieku z trwalego klucza zapisanego przez Test dzwieku.
 *
 * Kazda zmiana jest natychmiast zapisywana, wiec przetrwa restart.
 */
class PowerPage : public WidokPrzewijany {
public:
    Panel* content;
    Etykieta* title;
    Etykieta* info_heading;
    Etykieta* info_rows[4]{};
    Etykieta* power_heading;
    Etykieta* dim_label;
    ListaRozwijana* dim_choice;
    Etykieta* sleep_label;
    ListaRozwijana* sleep_choice;
    Etykieta* audio_heading;
    Etykieta* audio_value;
    Etykieta* status;
    Przycisk* save;
    Przycisk* reset;

    PowerPage() {
        ustaw_grubosc_ramki(0);
        ustaw_tryb_paska_poziomego(TrybPaskaPrzewijania::Nigdy);
        content = &dodaj<Panel>();
        content->ustaw_uklad_automatyczny(false);
        content->ustaw_wypelnienie({0, 0, 0, 0});

        title = &content->dodaj<Etykieta>("System i energia");
        title->ustaw_wypelnienie({0, 0, 0, 0});
        title->ustaw_skale_czcionki(2);

        info_heading = &content->dodaj<Etykieta>("Informacje o sprzecie");
        info_heading->ustaw_wypelnienie({0, 0, 0, 0});
        for (auto*& row : info_rows) {
            row = &content->dodaj<Etykieta>("");
            row->ustaw_wypelnienie({0, 0, 0, 0});
        }

        power_heading = &content->dodaj<Etykieta>("Zarzadzanie energia");
        power_heading->ustaw_wypelnienie({0, 0, 0, 0});

        dim_label = &content->dodaj<Etykieta>("Wygaszanie ekranu");
        dim_label->ustaw_wypelnienie({0, 0, 0, 0});
        dim_choice = &content->dodaj<ListaRozwijana>();

        sleep_label = &content->dodaj<Etykieta>("Usypianie po bezczynnosci");
        sleep_label->ustaw_wypelnienie({0, 0, 0, 0});
        sleep_choice = &content->dodaj<ListaRozwijana>();

        audio_heading = &content->dodaj<Etykieta>("Dzwiek");
        audio_heading->ustaw_wypelnienie({0, 0, 0, 0});
        audio_value = &content->dodaj<Etykieta>("");
        audio_value->ustaw_wypelnienie({0, 0, 0, 0});

        save = &content->dodaj<Przycisk>("Zapisz");
        reset = &content->dodaj<Przycisk>("Przywroc domyslne");
        status = &content->dodaj<Etykieta>("");
        status->ustaw_wypelnienie({0, 0, 0, 0});

        dim_choice->dodaj_element("Nigdy", 0);
        dim_choice->dodaj_element("1 minuta", 60);
        dim_choice->dodaj_element("5 minut", 300);
        dim_choice->dodaj_element("10 minut", 600);
        dim_choice->dodaj_element("30 minut", 1800);

        sleep_choice->dodaj_element("Nigdy", 0);
        sleep_choice->dodaj_element("15 minut", 900);
        sleep_choice->dodaj_element("30 minut", 1800);
        sleep_choice->dodaj_element("1 godzina", 3600);
    }

    /* Wczytuje wartosci z trwalych ustawien i z monitora. */
    void odswiez() {
        char buf[64];

        for (auto*& row : info_rows) {
            row->ustaw_tekst("");
        }

        GuiMonitorInfo m{};
        if (gui_pobierz_monitor(0, &m)) {
            char linia[96];

            int w = m.szer;
            int h = m.wys;
            int p = 0;
            auto dopisz = [&](const char* t) {
                while (*t && p + 1 < static_cast<int>(sizeof(linia))) linia[p++] = *t++;
            };
            auto liczba = [&](int v) {
                char tmp[12];
                int n = 0;
                if (v == 0) tmp[n++] = '0';
                while (v > 0 && n < 11) { tmp[n++] = char('0' + v % 10); v /= 10; }
                for (int i = n - 1; i >= 0; --i) { if (p + 1 < static_cast<int>(sizeof(linia))) linia[p++] = tmp[i]; }
            };

            dopisz("Rozdzielczosc: ");
            liczba(w);
            dopisz(" x ");
            liczba(h);
            linia[p] = '\0';
            info_rows[0]->ustaw_tekst(linia);

            int skala = m.scale_x1000 / 10;
            if (skala < 1) skala = 100;
            info_rows[1]->ustaw_tekst(skala == 100
                ? "Skala interfejsu: 100% (domyslna)"
                : "Skala interfejsu: ustawiona (HiDPI)");
        } else {
            info_rows[0]->ustaw_tekst("Rozdzielczosc: niedostepna");
        }

        if (ustawienia_wczytaj("ustawienia", "wygaszanie", buf, sizeof(buf))) {
            int wartosc = 0;
            for (int i = 0; buf[i] >= '0' && buf[i] <= '9'; ++i) {
                wartosc = wartosc * 10 + (buf[i] - '0');
            }
            const int opcje[] = {0, 60, 300, 600, 1800};
            for (int i = 0; i < 5; ++i) {
                if (opcje[i] == wartosc) { dim_choice->ustaw_wybrany_indeks(i); break; }
            }
        }

        if (ustawienia_wczytaj("ustawienia", "usypianie", buf, sizeof(buf))) {
            int wartosc = 0;
            for (int i = 0; buf[i] >= '0' && buf[i] <= '9'; ++i) {
                wartosc = wartosc * 10 + (buf[i] - '0');
            }
            const int opcje[] = {0, 900, 1800, 3600};
            for (int i = 0; i < 4; ++i) {
                if (opcje[i] == wartosc) { sleep_choice->ustaw_wybrany_indeks(i); break; }
            }
        }

        if (ustawienia_wczytaj("test_dzwieku", "wynik", buf, sizeof(buf))) {
            char linia[128];
            int p = 0;
            auto dopisz = [&](const char* t) {
                while (*t && p + 1 < static_cast<int>(sizeof(linia))) linia[p++] = *t++;
            };
            dopisz("Ostatni test dzwieku: ");
            dopisz(buf);
            linia[p] = '\0';
            audio_value->ustaw_tekst(linia);
        } else {
            audio_value->ustaw_tekst(
                "Ostatni test dzwieku: brak danych (uruchom Test dzwieku)");
        }
    }

    void zapisz() {
        char buf[16];

        auto liczba_na_tekst = [](int v, char* out) {
            int n = 0;
            char tmp[12];
            if (v == 0) tmp[n++] = '0';
            while (v > 0 && n < 11) { tmp[n++] = char('0' + v % 10); v /= 10; }
            int k = 0;
            for (int i = n - 1; i >= 0; --i) out[k++] = tmp[i];
            out[k] = '\0';
        };

        const int dim_opcje[] = {0, 60, 300, 600, 1800};
        int di = dim_choice->wybrany_indeks();
        if (di < 0 || di > 4) di = 0;
        liczba_na_tekst(dim_opcje[di], buf);
        bool ok = ustawienia_zapisz("ustawienia", "wygaszanie", buf);

        const int sleep_opcje[] = {0, 900, 1800, 3600};
        int si = sleep_choice->wybrany_indeks();
        if (si < 0 || si > 3) si = 0;
        liczba_na_tekst(sleep_opcje[si], buf);
        ok = ustawienia_zapisz("ustawienia", "usypianie", buf) && ok;

        status->ustaw_tekst(ok
            ? "Zapisano. Ustawienia przetrwaja restart."
            : "Nie udalo sie zapisac ustawien.");
    }

    void przywroc_domyslne() {
        (void)ustawienia_usun("ustawienia", "wygaszanie");
        (void)ustawienia_usun("ustawienia", "usypianie");
        dim_choice->ustaw_wybrany_indeks(0);
        sleep_choice->ustaw_wybrany_indeks(0);
        status->ustaw_tekst("Przywrocono wartosci domyslne.");
        odswiez();
    }

    void zaktualizuj_motyw() override {
        WidokPrzewijany::zaktualizuj_motyw();
        ustaw_grubosc_ramki(0);
    }

    void rozmiesc(Prostokat r) override {
        WidokPrzewijany::rozmiesc(r);
        auto s = skala_dpi_;
        int line = CzcionkaUI::pobierz_wysokosc(s);
        int gap = 12 * s, small = 6 * s, pad = ContentInset * s;
        int w = r.szerokosc - 18 * s - 2 * pad;
        if (w < 1) w = 1;

        int y = 2 * line;
        title->rozmiesc({0, 0, w, y});

        info_heading->rozmiesc({0, y, w, line});
        y += line + small;
        for (auto* row : info_rows) {
            row->rozmiesc({0, y, w, line});
            y += line + small / 2;
        }

        y += gap;
        power_heading->rozmiesc({0, y, w, line});
        y += line + small;

        int control = dim_choice->oblicz_naturalny_rozmiar().wysokosc;
        if (control < line + 8 * s) control = line + 8 * s;

        int label_w = CzcionkaUI::zmierz_tekst("Usypianie po bezczynnosci", s);
        int field_w = w - label_w - gap;
        if (field_w < 140 * s) { label_w = w / 2; field_w = w - label_w - gap; }

        dim_label->rozmiesc({0, y, label_w, line});
        dim_choice->rozmiesc({label_w + gap, y, field_w, control});
        y += control + small;

        sleep_label->rozmiesc({0, y, label_w, line});
        sleep_choice->rozmiesc({label_w + gap, y, field_w, control});
        y += control + gap;

        audio_heading->rozmiesc({0, y, w, line});
        y += line + small;
        audio_value->rozmiesc({0, y, w, line});
        y += line + gap;

        int button = save->zmierz({w, r.wysokosc}).wysokosc;
        if (button < line + 12 * s) button = line + 12 * s;
        int first = CzcionkaUI::zmierz_tekst("Zapisz", s) + 32 * s;
        int second = CzcionkaUI::zmierz_tekst("Przywroc domyslne", s) + 32 * s;
        if (first + second + gap > w) { first = (w - gap) / 2; second = w - gap - first; }

        save->rozmiesc({0, y, first, button});
        reset->rozmiesc({first + gap, y, second, button});
        y += button + small;

        status->rozmiesc({0, y, w, line});
        y += line;

        content->rozmiesc({pad, pad, w, y + gap});
        ustaw_rozmiar_zawartosci({w + 2 * pad, y + gap + 2 * pad});
    }
};
}
