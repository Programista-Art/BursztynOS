// Testy hosta: prywatne ustawienia aplikacji i izolacja (jadro/bws/ustawienia_aplikacji.h).
#include "../jadro/bws/ustawienia_aplikacji.h"
#include <cstdio>
#include <cstring>
#include <string>
using namespace UstawieniaAplikacji;
static int g_fail = 0, g_ok = 0;
#define CHECK(c) do { if (c) ++g_ok; else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static Decyzja d(const char* proc, const char* path, bool zapisz, bool ust) { return ocen(proc, path, zapisz, ust); }
int main() {
    char id[MAKS_ID];
    CHECK(id_z_sciezki_procesu("/gry/snake.cebula/snake.bur", id, sizeof(id)) && !strcmp(id, "snake"));
    CHECK(id_z_sciezki_procesu("/programy/przegladarka-obrazow.cebula/przegladarka-obrazow.bur", id, sizeof(id)) && !strcmp(id, "przegladarka-obrazow"));
    for (const char* zle : {"/uzytkownicy/x/snake.bur", "/gry/snake/snake.bur", "/gry/.cebula/x.bur", "/gry/../snake.cebula/x", "/gry/snake.cebula",
                            "/menedzer_okien.bur", "", "/gry/sn ake.cebula/x.bur", "/programy//a.cebula/a.bur"})
        CHECK(!id_z_sciezki_procesu(zle, id, sizeof(id)));
    CHECK(!id_z_sciezki_procesu("/gry/snake.cebula/snake.bur", id, 3));          // za maly bufor
    const char* SNAKE = "/gry/snake.cebula/snake.bur";
    const char* USTAW = "/programy/ustawienia.cebula/ustawienia.bur";
    const std::string moj = "/uzytkownicy/domyslny/Ustawienia/snake";
    // wlasny katalog: wystarczy samo prawo ustawien (bez PLIKI_ZAPISZ)
    CHECK(d(SNAKE, moj.c_str(), false, true) == Decyzja::Zezwol);
    CHECK(d(SNAKE, (moj + "/stan").c_str(), false, true) == Decyzja::Zezwol);
    CHECK(d(SNAKE, (moj + "/stan").c_str(), true, false) == Decyzja::Zezwol);
    // brak obu praw: odmowa
    CHECK(d(SNAKE, (moj + "/stan").c_str(), false, false) == Decyzja::Odmow);
    // inna aplikacja: odmowa takze przy ogolnym PLIKI_ZAPISZ (izolacja)
    CHECK(d(SNAKE, "/uzytkownicy/domyslny/Ustawienia/ustawienia/czcionka-rozmiar", false, true) == Decyzja::Odmow);
    CHECK(d(SNAKE, "/uzytkownicy/domyslny/Ustawienia/ustawienia/czcionka-rozmiar", true, false) == Decyzja::Odmow);
    CHECK(d(USTAW, "/uzytkownicy/domyslny/Ustawienia/snake/stan", true, false) == Decyzja::Odmow);
    CHECK(d(USTAW, "/uzytkownicy/domyslny/Ustawienia/ustawienia/czcionka-rozmiar", true, true) == Decyzja::Zezwol);
    // prefiks nazwy to nie ta sama aplikacja
    CHECK(d(SNAKE, "/uzytkownicy/domyslny/Ustawienia/snake2/stan", false, true) == Decyzja::Odmow);
    CHECK(d(SNAKE, "/uzytkownicy/domyslny/Ustawienia/snak/stan", false, true) == Decyzja::Odmow);
    // samo prawo ustawien nie daje zapisu poza katalogiem aplikacji
    for (const char* p : {"/uzytkownicy/domyslny/Ustawienia", "/uzytkownicy/domyslny", "/uzytkownicy/Pulpit/plik.txt", "/dane/zdjecia/x.jpg",
                          "/gry/snake.cebula/snake.bur", "/programy/menedzer.cebula/x.bur", "/ustawienia/pulpit.cfg", "/tapety/x", "/boot/zdjecia.pak",
                          "/system/config", "/", "/menedzer_okien.bur"})
        CHECK(d(SNAKE, p, false, true) == Decyzja::Odmow);
    // ogolny PLIKI_ZAPISZ poza ustawieniami: bez nowej reguly (decyduja pozostale sprawdzenia)
    CHECK(d(SNAKE, "/uzytkownicy/Pulpit/plik.txt", true, false) == Decyzja::BrakReguly);
    CHECK(d(SNAKE, "/uzytkownicy/domyslny/Ustawienia", true, false) == Decyzja::BrakReguly);
    // proces bez katalogu ustawien (np. spoza /gry i /programy): nigdy nie dostaje cudzego katalogu
    CHECK(d("/uzytkownicy/domyslny/x.bur", (moj + "/stan").c_str(), false, true) == Decyzja::Odmow);
    CHECK(d("/uzytkownicy/domyslny/x.bur", (moj + "/stan").c_str(), true, false) == Decyzja::Odmow);
    // sciezki zlosliwe (kanoniczna wersja jest egzekwowana przed ocen(); tu: poprawne segmenty)
    CHECK(klasyfikuj("/uzytkownicy/domyslny/Ustawienia/../snake/x", id, sizeof(id)) == Strefa::PozaUstawieniami);
    CHECK(klasyfikuj("/uzytkownicy/domyslny/Ustawienia/./x", id, sizeof(id)) == Strefa::PozaUstawieniami);
    CHECK(klasyfikuj("/uzytkownicy/domyslny/Ustawienia//x", id, sizeof(id)) == Strefa::PozaUstawieniami);
    CHECK(klasyfikuj("/uzytkownicy/../Ustawienia/x", id, sizeof(id)) == Strefa::PozaUstawieniami);
    CHECK(klasyfikuj("/uzytkownicy/domyslny/ustawienia/snake", id, sizeof(id)) == Strefa::PozaUstawieniami);   // wielkosc liter
    CHECK(klasyfikuj("/uzytkownicy/domyslny/Ustawienia/sn\xC4\x85ke/x", id, sizeof(id)) == Strefa::PozaUstawieniami);
    CHECK(klasyfikuj("/uzytkownicy/domyslny/UstawieniaX/snake/x", id, sizeof(id)) == Strefa::PozaUstawieniami);
    std::string dlugi = "/uzytkownicy/domyslny/Ustawienia/" + std::string(200, 'a') + "/x";
    CHECK(klasyfikuj(dlugi.c_str(), id, sizeof(id)) == Strefa::PozaUstawieniami);
    CHECK(klasyfikuj("/uzytkownicy/domyslny/Ustawienia", id, sizeof(id)) == Strefa::KatalogUstawien);
    CHECK(klasyfikuj("/uzytkownicy/domyslny/Ustawienia/snake", id, sizeof(id)) == Strefa::KatalogAplikacji && !strcmp(id, "snake"));
    std::printf("%s test_ustawienia_aplikacji_host: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_ok + g_fail, g_fail);
    return g_fail ? 1 : 0;
}
