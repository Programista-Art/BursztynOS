// Punkty kontrolne pracy odroczonej audio w dlugich petlach jadra (jadro/dzwiek/punkt_kontrolny_audio.h).
//
// Model workera jak hda_obsluz_okresowo / ac97_obsluz_okresowo: przerwanie zegara publikuje prace (flaga != 0, tylko
// gdy pusta), worker zdejmuje flage i wykonuje ograniczona porcje. Sprawdzane:
//  - szybka sciezka bez pracy, jedno wykonanie na publikacje (brak powtornego przetwarzania),
//  - IF=0, zagniezdzenie i zajeta blokada miksera: odroczenie bez utraty pracy (flaga zostaje) i bez rekursji,
//  - stan niezainicjalizowany / awaria urzadzenia: brak pracy albo jedno wykonanie na publikacje,
//  - harmonogram: opoznienie ograniczone kosztem JEDNEJ iteracji niezaleznie od dlugosci zadan; regresja starego
//    licznika "co 16" (lokalnego dla zadania), ktory przy krotkich zadaniach nie wykonywal pracy wcale,
//  - model bws_alokuj_sterte: spojnosc metadanych pamieci w kazdym punkcie (PMM wolny, strony [0,i) zmapowane,
//    granica sterty nieprzesunieta),
//  - przedluz_termin: brak zegara, cofniety zegar, nasycenie, limit laczny.
#define BURSZTYN_PUNKT_AUDIO_HOST_TEST 1
#include "../jadro/dzwiek/punkt_kontrolny_audio.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

using namespace PunktKontrolnyAudio;

static unsigned g_checks = 0;
#define CHECK(x) do { ++g_checks; if (!(x)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)

struct Srodowisko {
    uint64_t oczekuje = 0;        // jak pcm_pending_ns / g_oczekuje_ns
    bool przerwania = true;
    bool blokada = false;
    int wykonania = 0;
    std::function<void()> w_trakcie;
    bool praca_oczekuje() const { return oczekuje != 0; }
    bool przerwania_wlaczone() const { return przerwania; }
    bool blokada_audio_zajeta() const { return blokada; }
    void wykonaj() {
        ++wykonania;
        oczekuje = 0;             // worker zdejmuje publikacje (atomic exchange w sterowniku)
        if (w_trakcie) w_trakcie();
    }
    void zegar(uint64_t teraz) { if (!oczekuje) oczekuje = teraz ? teraz : 1; }   // cmpxchg 0 -> now
};

static bool zerowe(const Statystyki& s) {
    return !s.zgloszenia && !s.wykonane && !s.odroczone_przerwania && !s.odrzucone_zagniezdzone && !s.odroczone_blokada;
}

static void test_szybka_sciezka_i_wykonanie() {
    Srodowisko s; Statystyki st; bool w_toku = false;
    for (int i = 0; i < 1000; ++i) CHECK(obsluz(s, st, w_toku) == Wynik::BrakPracy);
    CHECK(s.wykonania == 0 && zerowe(st) && !w_toku);
    s.zegar(5);
    CHECK(obsluz(s, st, w_toku) == Wynik::Wykonano);
    CHECK(s.wykonania == 1 && s.oczekuje == 0 && st.zgloszenia == 1 && st.wykonane == 1 && !w_toku);
    for (int i = 0; i < 100; ++i) CHECK(obsluz(s, st, w_toku) == Wynik::BrakPracy);   // bez powtornej pracy
    CHECK(s.wykonania == 1);
}

static void test_przerwania_wylaczone() {
    Srodowisko s; Statystyki st; bool w_toku = false;
    s.zegar(7); s.przerwania = false;
    for (int i = 0; i < 50; ++i) CHECK(obsluz(s, st, w_toku) == Wynik::OdroczonoPrzerwania);
    CHECK(s.wykonania == 0 && s.oczekuje == 7 && st.odroczone_przerwania == 50 && !w_toku);
    s.przerwania = true;                                   // odroczona, nie porzucona
    CHECK(obsluz(s, st, w_toku) == Wynik::Wykonano && s.wykonania == 1 && s.oczekuje == 0);
}

static void test_zagniezdzenie() {
    Srodowisko s; Statystyki st; bool w_toku = false;
    Wynik wewn = Wynik::BrakPracy;
    int glebokosc = 0, maks = 0;
    s.w_trakcie = [&] {
        ++glebokosc; if (glebokosc > maks) maks = glebokosc;
        CHECK(w_toku);                                     // flaga trzymana przez caly czas pracy
        s.zegar(99);                                       // zegar publikuje nowa prace w trakcie
        wewn = obsluz(s, st, w_toku);                      // np. przyszly worker wola petle z punktem
        --glebokosc;
    };
    s.zegar(1);
    CHECK(obsluz(s, st, w_toku) == Wynik::Wykonano);
    CHECK(wewn == Wynik::OdrzuconoZagniezdzenie && maks == 1 && s.wykonania == 1);
    CHECK(st.odrzucone_zagniezdzone == 1 && !w_toku && s.oczekuje == 99);  // praca z wnetrza czeka na kolejny punkt
    s.w_trakcie = nullptr;
    CHECK(obsluz(s, st, w_toku) == Wynik::Wykonano && s.wykonania == 2 && s.oczekuje == 0);
}

static void test_blokada_miksera() {
    Srodowisko s; Statystyki st; bool w_toku = false;
    s.zegar(3); s.blokada = true;
    for (int i = 0; i < 20; ++i) CHECK(obsluz(s, st, w_toku) == Wynik::OdroczonoBlokada);
    CHECK(s.wykonania == 0 && s.oczekuje == 3 && st.odroczone_blokada == 20 && !w_toku);
    s.blokada = false;
    CHECK(obsluz(s, st, w_toku) == Wynik::Wykonano && s.wykonania == 1);
}

static void test_stany_urzadzenia() {
    // Przed inicjalizacja / bez urzadzenia: zegar nie publikuje pracy (pcm_interest=0, g_aktywny=0) - nic sie nie dzieje.
    Srodowisko s; Statystyki st; bool w_toku = false;
    for (int i = 0; i < 10000; ++i) CHECK(obsluz(s, st, w_toku) == Wynik::BrakPracy);
    CHECK(s.wykonania == 0 && zerowe(st));
    // Awaria urzadzenia (worker konczy na hda_gotowy=0 / pcm_shutdown): zdejmuje publikacje, wiec jedno wykonanie
    // na publikacje zegara - brak petli pracy w petli dysku.
    for (uint64_t tik = 1; tik <= 25; ++tik) {
        s.zegar(tik);
        for (int i = 0; i < 40; ++i) (void)obsluz(s, st, w_toku);
    }
    CHECK(s.wykonania == 25 && st.wykonane == 25 && st.zgloszenia == 25);
}

// Odtworzenie usunietego licznika (punkt "co 16 iteracji", lokalny dla kazdej petli oczekiwania).
struct StaryLicznik {
    uint32_t n = 0;
    template <class F> void krok(uint32_t co, F&& f) { if (++n < co) return; n = 0; f(); }
};

struct WynikSymulacji { int wykonania; uint64_t maks_opoznienie_us; int publikacje; };

// Jeden syscall: `zadania` odczytow dysku po `iteracje` iteracji kosztu `koszt_us` (z wtracona dluga iteracja),
// zegar co 4 ms. Zwraca liczbe wykonan pracy audio i najdluzsze opoznienie od publikacji do wykonania.
static WynikSymulacji symuluj(bool stary, int zadania, int iteracje, uint64_t koszt_us) {
    Srodowisko s; Statystyki st; bool w_toku = false;
    uint64_t t = 0, nastepny_tik = 4000, maks = 0;
    int publikacje = 0;
    for (int z = 0; z < zadania; ++z) {
        StaryLicznik licznik;                               // stary kod: nowy licznik dla kazdego zadania
        for (int i = 0; i < iteracje; ++i) {
            t += (i == iteracje / 2 && z % 7 == 0) ? 9 * koszt_us : koszt_us;   // zmienny koszt iteracji (MMIO)
            while (t >= nastepny_tik) { if (!s.oczekuje) ++publikacje; s.zegar(nastepny_tik); nastepny_tik += 4000; }
            const uint64_t opublikowano = s.oczekuje;
            bool wykonano = false;
            if (stary) licznik.krok(16, [&] { wykonano = obsluz(s, st, w_toku) == Wynik::Wykonano; });
            else wykonano = obsluz(s, st, w_toku) == Wynik::Wykonano;
            if (wykonano && t - opublikowano > maks) maks = t - opublikowano;
        }
    }
    if (s.oczekuje && t - s.oczekuje > maks) maks = t - s.oczekuje;   // praca pozostawiona do konca syscallu
    return {s.wykonania, maks, publikacje};
}

static void test_harmonogram() {
    // Eksplorator: 600 krotkich odczytow (12 iteracji po 20 us) w jednym syscallu = ~150 ms.
    const auto stary = symuluj(true, 600, 12, 20);
    const auto nowy = symuluj(false, 600, 12, 20);
    std::printf("krotkie zadania dysku: stary licznik %d wykonan (maks. opoznienie %.1f ms), nowy %d / %d publikacji"
                " (maks. %.3f ms)\n", stary.wykonania, stary.maks_opoznienie_us / 1000.0, nowy.wykonania,
                nowy.publikacje, nowy.maks_opoznienie_us / 1000.0);
    CHECK(stary.wykonania == 0 && stary.maks_opoznienie_us > 37000);   // potwierdzony defekt starego schematu
    CHECK(nowy.wykonania == nowy.publikacje && nowy.wykonania >= 30);  // jedno wykonanie na publikacje zegara
    CHECK(nowy.maks_opoznienie_us <= 9 * 20);                          // <= najdluzsza pojedyncza iteracja
    // Dlugie zadanie (40 ms, 1 us/iteracja): oba schematy dzialaja, nowy nie wykonuje pracy czesciej niz zegar.
    const auto dl = symuluj(false, 1, 40000, 1);
    CHECK(dl.wykonania == dl.publikacje && dl.wykonania == 10 && dl.maks_opoznienie_us <= 9);
}

// Model bws_alokuj_sterte: punkt przed kazda strona; worker sprawdza spojnosc metadanych pamieci.
static void test_model_sterty() {
    constexpr uint64_t STRONY = 18432;                     // powierzchnia 72 MiB Przegladarki Obrazow
    struct Proces { uint64_t granica = 0x400000; std::vector<bool> mapa = std::vector<bool>(STRONY, false); } p;
    bool pmm_zablokowany = false;
    uint64_t i = 0, sprawdzenia = 0, t = 0, nastepny_tik = 4000;
    const uint64_t granica_przed = p.granica;
    Srodowisko s; Statystyki st; bool w_toku = false;
    s.w_trakcie = [&] {
        CHECK(!pmm_zablokowany);                           // punkt poza sekcja krytyczna PMM (cli + spinlock)
        CHECK(p.granica == granica_przed);                 // granica przesuwana dopiero po petli
        for (uint64_t k = 0; k < STRONY; k += 997) CHECK(p.mapa[k] == (k < i));   // strony [0,i) w calosci
        ++sprawdzenia;
    };
    for (i = 0; i < STRONY; ++i) {
        t += 5;                                            // ~5 us / strona (zerowanie 4 KiB + mapowanie, TCG)
        while (t >= nastepny_tik) { s.zegar(nastepny_tik); nastepny_tik += 4000; }
        (void)obsluz(s, st, w_toku);
        pmm_zablokowany = true;  pmm_zablokowany = false;  // ZaalokujRamke
        p.mapa[i] = true;                                  // wyzeruj + ZmapujStrone
    }
    p.granica += STRONY * 4096;
    CHECK(sprawdzenia == static_cast<uint64_t>(s.wykonania) && s.wykonania >= 22 && p.granica == granica_przed + STRONY * 4096);
}

static void test_przedluz_termin() {
    uint64_t prz = 0;
    CHECK(przedluz_termin(1000, 0, 500, prz, 100) == 1000 && prz == 0);          // brak zegara przed
    CHECK(przedluz_termin(1000, 400, 0, prz, 100) == 1000 && prz == 0);          // brak zegara po
    CHECK(przedluz_termin(1000, 400, 300, prz, 100) == 1000 && prz == 0);        // zegar cofniety
    CHECK(przedluz_termin(UINT64_MAX, 1, 50, prz, 100) == UINT64_MAX && prz == 0);
    CHECK(przedluz_termin(1000, 100, 130, prz, 100) == 1030 && prz == 30);
    CHECK(przedluz_termin(1030, 200, 300, prz, 100) == 1100 && prz == 100);      // przyciete do limitu lacznego
    CHECK(przedluz_termin(1100, 300, 900, prz, 100) == 1100 && prz == 100);      // limit wyczerpany
    uint64_t prz2 = 0;
    CHECK(przedluz_termin(UINT64_MAX - 5, 10, 60, prz2, 100) == UINT64_MAX && prz2 == 50);   // nasycenie
}

static void test_liczniki_globalne() {
    Srodowisko s;
    s.zegar(1);
    CHECK(obsluz(s, g_statystyki, g_w_toku) == Wynik::Wykonano);
    s.zegar(2); s.przerwania = false;
    CHECK(obsluz(s, g_statystyki, g_w_toku) == Wynik::OdroczonoPrzerwania);
    zapisz_przedluzenie(0); zapisz_przedluzenie(1234);
    const Statystyki x = statystyki();
    CHECK(x.zgloszenia == 2 && x.wykonane == 1 && x.odroczone_przerwania == 1 && x.przedluzenia_terminu_ns == 1234);
    CHECK(!g_w_toku);
    // Build hosta bez modelu: punkt() nie ma pracy (sterowniki dysku w testach hosta nie linkuja miksera).
    CHECK(g_host_punkt == nullptr && punkt() == Wynik::BrakPracy);
    g_host_punkt = [] { return Wynik::Wykonano; };
    CHECK(punkt() == Wynik::Wykonano);
    g_host_punkt = nullptr;
}

int main() {
    test_szybka_sciezka_i_wykonanie();
    test_przerwania_wylaczone();
    test_zagniezdzenie();
    test_blokada_miksera();
    test_stany_urzadzenia();
    test_harmonogram();
    test_model_sterty();
    test_przedluz_termin();
    test_liczniki_globalne();
    std::printf("punkty kontrolne audio: %u sprawdzen OK\n", g_checks);
    return 0;
}
