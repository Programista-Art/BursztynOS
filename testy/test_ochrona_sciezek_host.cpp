// Testy hosta: ochrona sciezek systemowych + prywatne ustawienia aplikacji = decyzja zapisu zwyklego procesu (PZB 4).
#include "../jadro/bws/ochrona_sciezek.h"
#include "../jadro/bws/ustawienia_aplikacji.h"
#include <cstdio>
#include <initializer_list>
static int ok = 0, bad = 0;
#define CHECK(c) do { if (c) ++ok; else { ++bad; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
// Dokladnie ta kompozycja co proces_moze_modyfikowac_sciezke() dla procesu PZB >= UZYTKOWNIK.
static bool zapis(const char* proces, const char* sciezka, bool zapisz, bool ustawienia) {
    if (!zapisz && !ustawienia) return false;
    if (!OchronaSciezek::sciezka_kanoniczna(sciezka) || OchronaSciezek::sciezka_chroniona(sciezka)) return false;
    return UstawieniaAplikacji::ocen(proces, sciezka, zapisz, ustawienia) != UstawieniaAplikacji::Decyzja::Odmow;
}
int main() {
    const char* SNAKE = "/gry/snake.cebula/snake.bur";
    const char* NOTATNIK = "/programy/notatnik.cebula/notatnik.bur";
    // Wszystkie warianty drzew systemowych sa niedostepne dla procesu z ogolnym zapisem
    for (const char* p : {"/system/konfig/x", "/system", "/jadro/a", "/programy/notatnik.cebula/notatnik.bur", "/programy/x.cebula/opis.aplikacji",
                          "/gry/snake.cebula/opis.aplikacji", "/gry/snake.cebula/snake.bur", "/menedzer_okien.bur", "/shell.bur",
                          "/aktualizacje/stan_rozruchu.k0", "/aktualizacje/przygotowanie", "/aktualizacje/wykazaktualizacji.wykaz", "/aktualizacje"})
        CHECK(!zapis(NOTATNIK, p, true, false));
    // Fizyczne sloty A/B (alias /programy i /system po tlumaczeniu PSF) oraz /sterowniki
    for (const char* p : {"/sloty", "/sloty/a/programy/notatnik.cebula/notatnik.bur", "/sloty/a/programy/evil.cebula/opis.aplikacji",
                          "/sloty/b/system/wersja", "/sloty/a/system/wersja", "/sterowniki/x"})
        CHECK(!zapis(NOTATNIK, p, true, false));
    CHECK(zapis(NOTATNIK, "/sloty2/x", true, false));
    // Aliasy i sztuczki ze sciezka
    for (const char* p : {"/programy/../programy/x", "/tmp/../system/x", "//system/x", "/system//x", "/system/./x", "/uzytkownicy/../jadro/x",
                          "/uzytkownicy/domyslny/Ustawienia/../../../system/konfig", "system/x", ""})
        CHECK(!zapis(NOTATNIK, p, true, false));
    // "System2" i podobne nie sa chronionymi drzewami (prefiks segmentowy), a /aktualizacje2 tez nie
    CHECK(zapis(NOTATNIK, "/system2/plik.txt", true, false) && zapis(NOTATNIK, "/aktualizacje2/x", true, false));
    CHECK(zapis(NOTATNIK, "/uzytkownicy/Pulpit/notatka.txt", true, false) && zapis(NOTATNIK, "/dane/zdjecia/moje.jpg", true, false));
    // Prywatne ustawienia: Snake tylko we wlasnym katalogu, takze po utracie ogolnego zapisu
    CHECK(zapis(SNAKE, "/uzytkownicy/domyslny/Ustawienia/snake/stan", false, true));
    CHECK(!zapis(SNAKE, "/uzytkownicy/domyslny/Ustawienia/ustawienia/czcionka-rozmiar", false, true));
    CHECK(!zapis(NOTATNIK, "/uzytkownicy/domyslny/Ustawienia/snake/stan", true, false));          // Notatnik z ogolnym zapisem nie ruszy ustawien Snake
    CHECK(!zapis(SNAKE, "/uzytkownicy/domyslny/Ustawienia/notatnik/x", true, false));
    CHECK(zapis(NOTATNIK, "/uzytkownicy/domyslny/Ustawienia/notatnik/x", true, false));
    for (const char* p : {"/uzytkownicy/Pulpit/x.txt", "/dane/zdjecia/x.jpg", "/ustawienia/pulpit.cfg", "/tapety/x", "/uzytkownicy/domyslny/Ustawienia"})
        CHECK(!zapis(SNAKE, p, false, true));                                                      // samo prawo ustawien nie daje zapisu poza wlasnym katalogiem
    CHECK(!zapis(SNAKE, "/uzytkownicy/domyslny/Ustawienia/snake/stan", false, false));
    std::printf("%s test_ochrona_sciezek_host: %d checks, %d failed\n", bad ? "FAIL" : "PASS", ok + bad, bad);
    return bad ? 1 : 0;
}
