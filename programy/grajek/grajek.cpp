/*
 * Grajek 1.0 - natywny odtwarzacz muzyki Bursztyn OS (Ring 3, BursztynUI, BWS 110).
 *
 * Uprawnienia (manifest w jadro/rdzen/kernel.cpp): "okna", "pliki_czytaj" (odczyt /dane/muzyka),
 * "ustawienia_aplikacji" (konfiguracja i lista M3U8 w /uzytkownicy/<u>/Ustawienia/grajek/). Grajek nie ma prawa
 * zapisu plikow uzytkownika, wiec nie moze zmienic, przeniesc ani usunac muzyki. Strumien PCM (BWS 110) nie wymaga
 * dodatkowego prawa; jadro sprzata strumienie procesu przy jego zakonczeniu.
 *
 * Jedna instancja: menu Start uzywa uruchom_lub_aktywuj (aktywuje istniejace okno, takze grajace w tle).
 */
#define BURSZTYN_UI_CZCIONKA_SYSTEMOWA 1
#include "bursztyn_gui.h"
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "okno.h"

namespace {

bool odczyt(void*, const char* sciezka, uint64_t offset, void* bufor, uint32_t n, uint32_t* odczytano) {
    return czytaj_fragment_pliku(sciezka, offset, static_cast<char*>(bufor), n, odczytano);
}
bool rozmiar(void*, const char* sciezka, uint64_t* r) {
    BwsMetadanePliku m{};
    if (!pobierz_metadane_pliku(sciezka, &m) || m.typ != BWS_TYP_PLIK || !(m.flagi & BWS_META_ROZMIAR_DOSTEPNY)) return false;
    *r = m.rozmiar;
    return true;
}
bool listuj(void*, const char* sciezka, char* bufor, uint32_t pojemnosc) {
    return wylistuj_katalog_uzytkownika(sciezka, bufor, pojemnosc);
}
uint64_t bws(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) { return bws_wywolaj(nr, a1, a2, a3, a4); }
bool wczytaj_ustawienie(const char* klucz, char* bufor, uint32_t pojemnosc) {
    return ustawienia_wczytaj(Grajek::ID_APLIKACJI, klucz, bufor, pojemnosc);
}
bool zapisz_ustawienie(const char* klucz, const char* wartosc) { return ustawienia_zapisz(Grajek::ID_APLIKACJI, klucz, wartosc); }
uint64_t czas_ns() { return gui_czas_monotoniczny_ns(); }

BursztynUI::AplikacjaGUI* g_aplikacja = nullptr;
uint32_t g_odstep = 1;

// Zadanie okresowe: 1 tick (~4 ms) podczas odtwarzania/skanu, 25 tickow (~100 ms) w bezczynnosci (oszczednosc CPU).
void zadanie(void* p) {
    auto* okno = static_cast<Grajek::OknoGrajka*>(p);
    okno->tick();
    const uint32_t chciany = okno->bezczynny() ? 25 : 1;
    if (chciany != g_odstep && g_aplikacja) {
        g_odstep = chciany;
        g_aplikacja->ustaw_zadanie_okresowe(chciany, zadanie, p);
    }
}

} // namespace

extern "C" __attribute__((noreturn)) void _start() {
    BursztynUI::AplikacjaGUI app;
    g_aplikacja = &app;
    auto* okno = new Grajek::OknoGrajka;
    Grajek::Platforma p;
    p.pliki.odczyt = odczyt;
    p.pliki.rozmiar = rozmiar;
    p.listuj = listuj;
    p.bws = bws;
    p.wczytaj_ustawienie = wczytaj_ustawienie;
    p.zapisz_ustawienie = zapisz_ustawienie;
    p.czas_ns = czas_ns;
    okno->init(p);
    app.ustaw_odstep_animacji(1);
    app.ustaw_zadanie_okresowe(1, zadanie, okno);
    (void)app.uruchom(*okno);
    okno->odtwarzacz().zakoncz();
    delete okno;
    bws_zakoncz_proces();
}
