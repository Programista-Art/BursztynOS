// Host test polityki stosu: pasek zadan nad zwyklymi oknami, "nad paskiem" nad paskiem, hit-test = kolejnosc rysowania.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "bursztyn_ui_host_fixture.h"
#include "wm_input.h"
#include "bursztyn_ui/bursztyn_ui.h"
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static warstwa_obrazu warstwy[SKLADACZ_MAKS_WARSTW];
static std::vector<uint32_t> bufory[SKLADACZ_MAKS_WARSTW];
static bool flaga[SKLADACZ_MAKS_WARSTW];

static void dodaj(int pid, int z, int x, int y, int w, int h, bool nad = false, bool pelne_krycie = true) {
    warstwy[pid] = {};
    warstwy[pid].pid = pid; warstwy[pid].z_order = z; warstwy[pid].x = x; warstwy[pid].y = y;
    warstwy[pid].szerokosc = w; warstwy[pid].wysokosc = h; warstwy[pid].aktywna = true;
    bufory[pid].assign(size_t(w) * h, pelne_krycie ? 0xFF112233u : 0u);
    warstwy[pid].bufor_pikseli = bufory[pid].data();
    flaga[pid] = nad;
}
static int hit(int x, int y, WmPasek pasek, int overlay = -1) {
    return wm_warstwa_pod_punktem(x, y, overlay,
        [](int pid) -> const warstwa_obrazu* { return warstwy[pid].aktywna ? &warstwy[pid] : nullptr; },
        [](int, int, int) { return false; }, WmPelnePokrycie{}, pasek,
        [](int pid) { return flaga[pid]; });
}


// Ta sama sekwencja co "[BURSZTYNUI 4/4]" w programy/test/test.cpp, na
// prawdziwym BursztynUI::Okno: obszar roboczy 1280x720 bez paska zadan.
static void ruch_przy_dolnej_krawedzi() {
    using namespace BursztynUI;
    for (unsigned skala : {100U, 125U, 150U, 200U}) {
        ui_skala_procent = skala;
        const int pasek = ui_skaluj(40, skala);
        test_monitor_ok = true; test_monitor = {};
        test_monitor.work_x = 0; test_monitor.work_y = 0; test_monitor.work_szer = 1280; test_monitor.work_wys = 720 - pasek;
        const auto& m = test_monitor;
        Okno ograniczone("Work area clamp", m.work_szer + 200, m.work_wys + 200);
        ograniczone.ustaw_geometrie(m.work_x - 100, m.work_y + m.work_wys - 10, m.work_szer + 200, m.work_wys + 200);
        ograniczone.dopasuj_do_obszaru_roboczego(true);
        CHECK(ograniczone.x() == m.work_x && ograniczone.y() == m.work_y &&
              ograniczone.szerokosc() == m.work_szer && ograniczone.wysokosc() == m.work_wys);
        ograniczone.ustaw_geometrie(m.work_x + m.work_szer - 20, m.work_y + m.work_wys - 20, 300, 180);
        const int s_proc = ograniczone.skala_dpi().procent();
        const int belka = Okno::WYSOKOSC_BELKI_TYTULOWEJ * ograniczone.skala_dpi();
        const int uchwyt = ui_skaluj(96, s_proc);
        const int dol = m.work_y + m.work_wys, prawo = m.work_x + m.work_szer;
        // Dawna asercja (pelne zawarcie w obszarze roboczym) nie odpowiada polityce
        // z 7779afa: okno wystaje, ale belka zostaje nad paskiem.
        CHECK(!(ograniczone.y() + ograniczone.wysokosc() <= dol));
        CHECK(OknoGeometria::pasek_uchwytny({ograniczone.x(), ograniczone.y(), ograniczone.szerokosc(), ograniczone.wysokosc()},
                                            {m.work_x, m.work_y, m.work_szer, m.work_wys}, uchwyt, belka, belka / 2));
        CHECK(ograniczone.y() + belka <= dol);
        CHECK(ograniczone.x() + uchwyt <= prawo);
        CHECK(ograniczone.y() == dol - belka);
        CHECK(ograniczone.x() == prawo - uchwyt);
        CHECK(ograniczone.szerokosc() == 300 && ograniczone.wysokosc() == 180);
        ograniczone.ustaw_geometrie(m.work_x + 40, dol - belka - 10, 300, m.work_wys + 100);
        CHECK(ograniczone.y() + belka <= dol && ograniczone.y() >= m.work_y - belka / 2);
        // Czesc okna pod obszarem roboczym jest w prostokacie paska; hit-test i
        // kolejnosc skladania oddaja ten obszar paskowi (pasek nad oknem).
        dodaj(1, 0, 0, 0, 1280, 720);
        dodaj(2, 10, ograniczone.x(), ograniczone.y(), ograniczone.szerokosc(), ograniczone.wysokosc());
        WmPasek p{true, 1, 0, dol, 1280, pasek};
        CHECK(hit(ograniczone.x() + 50, dol + 2, p) == 1);
        CHECK(hit(ograniczone.x() + 50, dol - 2, p) == 2);
        warstwy[2] = {};
    }
    test_monitor_ok = false; ui_skala_procent = 100;
}
int main() {
    ruch_przy_dolnej_krawedzi();
    // Pulpit (pid 1, z=0, caly ekran 1280x720, przezroczysty poza paskiem) + pasek y=680..720.
    dodaj(1, 0, 0, 0, 1280, 720, false, false);
    WmPasek pasek; pasek.aktywny = true; pasek.pid = 1; pasek.x = 0; pasek.y = 680; pasek.w = 1280; pasek.h = 40;
    CHECK(pasek.zawiera(0, 680) && pasek.zawiera(1279, 719) && !pasek.zawiera(0, 679) && !pasek.zawiera(1280, 700));
    // Zwykle okno wchodzi pod pasek (czesciowo poza ekranem): x=100..600, y=100..800.
    dodaj(2, 5, 100, 100, 500, 700);
    CHECK(hit(300, 300, pasek) == 2);                       // poza paskiem: okno
    CHECK(hit(300, 679, pasek) == 2);                       // tuz nad paskiem: okno
    CHECK(hit(300, 680, pasek) == 1);                       // pasek jest widoczny i klikalny nad oknem
    CHECK(hit(300, 700, pasek) == 1 && hit(599, 719, pasek) == 1);
    CHECK(hit(50, 700, pasek) == 1);                        // poza oknem, na pasku
    CHECK(hit(50, 300, pasek) == 1);                        // tlo pulpitu poza oknem
    // wyzszy z_order zwyklego okna NIE daje mu priorytetu nad paskiem
    dodaj(3, 30000, 100, 100, 500, 700);
    CHECK(hit(300, 700, pasek) == 1 && hit(300, 300, pasek) == 3);
    warstwy[3].aktywna = false;
    // Brak paska (nie zarejestrowany): zachowanie historyczne - okno nad calym ekranem
    CHECK(hit(300, 700, WmPasek{}) == 2);
    // Jawne zezwolenie "nad paskiem": okno wygrywa w obszarze paska (zgodnie z rysowaniem)
    flaga[2] = true;
    CHECK(hit(300, 700, pasek) == 2 && hit(300, 300, pasek) == 2);
    CHECK(hit(50, 700, pasek) == 1);                        // poza oknem nadal pasek
    // odebranie zezwolenia przywraca pasek
    flaga[2] = false;
    CHECK(hit(300, 700, pasek) == 1);
    // kilka okien "nad paskiem": wygrywa wyzszy z_order; zwykle okno o wyzszym z nie wygrywa
    dodaj(4, 10, 200, 650, 300, 100, true);
    dodaj(5, 20, 250, 650, 300, 100, true);
    dodaj(6, 9999, 150, 650, 600, 100, false);
    CHECK(hit(300, 700, pasek) == 5);
    CHECK(hit(210, 700, pasek) == 4);
    CHECK(hit(155, 700, pasek) == 1);                       // tylko zwykle okno 6 pod punktem: wygrywa pasek
    flaga[5] = false;
    CHECK(hit(300, 700, pasek) == 4);
    // piksel przezroczysty okna "nad paskiem" nie przechwytuje myszy (bez niewidzialnych nakladek)
    dodaj(7, 50, 600, 650, 200, 100, true, false);
    CHECK(hit(700, 700, pasek) == 1);                       // okno 7 jest w calosci przezroczyste
    // overlay (menu start / dialog systemowy) ma pierwszenstwo nad wszystkim
    CHECK(hit(300, 700, pasek, 9) == 9);
    // wlasciciel paska nigdy nie jest wybierany przez petle okien (wybor tylko przez pasek)
    dodaj(8, 0, 0, 0, 1280, 720, false, true);
    pasek.pid = 8; for (int i = 1; i <= 7; ++i) warstwy[i].aktywna = false;
    CHECK(hit(900, 700, pasek) == 8 && hit(300, 300, pasek) == 8 && hit(300, 300, WmPasek{}) == 8);
    // granice obszaru paska i ekstremalne wartosci bez przepelnien
    WmPasek duzy; duzy.aktywny = true; duzy.pid = 8; duzy.x = INT32_MAX - 5; duzy.y = INT32_MAX - 5; duzy.w = 100; duzy.h = 100;
    CHECK(duzy.zawiera(INT32_MAX - 1, INT32_MAX - 1) && !duzy.zawiera(0, 0));
    // Odzyskiwanie (Ctrl+Alt+B = wylaczenie flag WSZYSTKICH okien): duze okno przykrywajace caly pasek.
    for (int i = 1; i <= 8; ++i) { warstwy[i].aktywna = false; flaga[i] = false; }
    dodaj(1, 0, 0, 0, 1280, 720, false, false);
    pasek.pid = 1;
    dodaj(10, 40, 0, 0, 1280, 720, true);                    // okno na caly ekran z zezwoleniem "nad paskiem"
    dodaj(11, 50, 200, 300, 600, 420, true);                 // drugie, czesciowo nad paskiem
    for (int x : {0, 100, 640, 1279}) CHECK(hit(x, 700, pasek) != 1);      // pasek calkowicie zasloniety
    auto cofnij_wszystkie = [] { int n = 0; for (int i = 0; i < SKLADACZ_MAKS_WARSTW; ++i) if (flaga[i]) { flaga[i] = false; ++n; } return n; };
    CHECK(cofnij_wszystkie() == 2);
    for (int x : {0, 100, 640, 1279}) CHECK(hit(x, 700, pasek) == 1);      // pasek znowu klikalny w calej szerokosci
    CHECK(hit(300, 300, pasek) == 11 || hit(300, 300, pasek) == 10);       // zwykle okna nadal obsluguja obszar roboczy
    CHECK(cofnij_wszystkie() == 0);                                         // idempotentne
    // Naroznik poza zaokraglonym ksztaltem okna jest legalnie "przezroczysty" dla myszy: trafia w okno pod spodem
    // (nie ma przepuszczania kliknieć przez WIDOCZNE okno: piksel naroznika jest tam, gdzie widac okno spod spodu).
    for (int i = 1; i <= 11; ++i) { warstwy[i].aktywna = false; flaga[i] = false; }
    dodaj(12, 11, 100, 30, 1040, 650);       // dolne okno (Diagnostyka)
    dodaj(13, 12, 100, 100, 860, 580);       // gorne okno (Przegladarka); prawy dolny naroznik zaokraglony
    auto zaokraglone = [](int pid, int lx, int ly) {
        if (pid != 13) return true;
        const int r = 8, w = 860, h = 580;
        const int dx = lx - (w - r), dy = ly - (h - r);
        return !(dx >= 0 && dy >= 0 && dx * dx + dy * dy > r * r);
    };
    auto hit2 = [&](int x, int y) { return wm_warstwa_pod_punktem(x, y, -1,
        [](int pid) -> const warstwa_obrazu* { return warstwy[pid].aktywna ? &warstwy[pid] : nullptr; },
        [](int, int, int) { return false; }, zaokraglone, WmPasek{}, [](int) { return false; }); };
    CHECK(hit2(100 + 859, 100 + 579) == 12);                 // ostatni piksel naroznika: poza ksztaltem -> okno pod spodem
    CHECK(hit2(100 + 852, 100 + 572) == 13);                 // wewnatrz ksztaltu (chwytak zmiany rozmiaru) -> gorne okno
    CHECK(hit2(100 + 859, 100 + 500) == 13 && hit2(100 + 700, 100 + 579) == 13);   // prosta krawedz nie jest zaokraglona
    printf("PASS test_taskbar_stacking_host: %d checks\n", checks);
    return 0;
}
