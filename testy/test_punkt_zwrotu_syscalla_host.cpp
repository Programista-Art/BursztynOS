// Host (ASan/UBSan): decyzja punktu zwrotu syscallu z PRAWDZIWEGO naglowka jadro/bws/punkt_zwrotu_syscalla.h.
// Regresje: PID 0 jako konkurent (obsluga wejscia/kompozytora), wykluczenie procesu biezacego, sprzatanie,
// timeouty myszy/zdarzen (z zawinieciem licznika), leniwy termin okien podrzednych, IF=0 i PID 0 nigdy nie oddaja CPU.
#include "../jadro/bws/punkt_zwrotu_syscalla.h"
#include <cstdint>
#include <cstdio>
using namespace PunktZwrotuSyscalla;

static int bledy = 0, sprawdzenia = 0;
#define CHECK(c) do { ++sprawdzenia; if (!(c)) { ++bledy; std::printf("BLAD %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

constexpr int N = 8;
struct Model {
    Kandydat k[N]{};
    int okna_wolan = 0;
    bool okna = false;
    bool oczekuje(int biezacy, uint64_t tick) {
        return praca_oczekuje(biezacy, N, tick, [&](int pid) { return k[pid]; }, [&] { ++okna_wolan; return okna; });
    }
};

static void pid0_jest_konkurentem() {
    Model m;
    m.k[0].moze_byc_uruchomiony = true;            // PID 0 z zapisana rama: petla wejscia/kompozytora/USB
    m.k[1].moze_byc_uruchomiony = true;            // biezacy proces w serii syscalli
    CHECK(m.oczekuje(1, 100));                     // celowo: inaczej mysz i kompozycja glodzone
    Model pusty;
    pusty.k[1].moze_byc_uruchomiony = true;        // PID 0 bez ramy (przed pierwszym tickiem) i nikt inny
    CHECK(!pusty.oczekuje(1, 100));
    CHECK(pusty.okna_wolan == 1);                  // okna sprawdzone dopiero, gdy procesy nic nie wymagaja
}

static void biezacy_wykluczony() {
    Model m;
    m.k[3] = Kandydat{true, true, STAN_ZABLOKOWANY_MYSZ, 0, 0};
    CHECK(!m.oczekuje(3, 10));                     // wlasny slot nie jest powodem oddania CPU
}

static void sprzatanie() {
    Model m;
    m.k[2].sprzatanie_oczekuje = true;
    CHECK(m.oczekuje(1, 0));
    Model z0;
    z0.k[0].sprzatanie_oczekuje = true;            // PID 0 nigdy nie jest sprzatany
    CHECK(!z0.oczekuje(1, 0));
    CHECK(kandydat_wymaga_ticku(2, Kandydat{true, false, 6, 0, 0}, 0));
    CHECK(!kandydat_wymaga_ticku(0, Kandydat{true, false, 6, 0, 0}, 0));
}

static void timeout_myszy() {
    Kandydat k{false, false, STAN_ZABLOKOWANY_MYSZ, 50, 0};
    CHECK(!kandydat_wymaga_ticku(2, k, 49));
    CHECK(kandydat_wymaga_ticku(2, k, 50));
    CHECK(kandydat_wymaga_ticku(2, k, 51));
    k.termin_myszy = UINT64_MAX - 2;               // termin przed zawinieciem, tick po zawinieciu
    CHECK(kandydat_wymaga_ticku(2, k, 5));
    CHECK(!kandydat_wymaga_ticku(2, k, UINT64_MAX - 3));
    k.stan = 1;                                    // termin myszy liczy sie tylko w stanie ZABLOKOWANY_MYSZ
    CHECK(!kandydat_wymaga_ticku(2, k, 5));
}

static void timeout_zdarzenia() {
    Kandydat k{false, false, STAN_ZABLOKOWANY_ZDARZENIE, 0, 0};
    CHECK(!kandydat_wymaga_ticku(2, k, 1000000));  // termin 0 = czekanie bez timeoutu: nigdy
    k.termin_zdarzenia = 300;
    CHECK(!kandydat_wymaga_ticku(2, k, 299));
    CHECK(kandydat_wymaga_ticku(2, k, 300));
    k.termin_zdarzenia = UINT64_MAX;               // zawiniecie
    CHECK(kandydat_wymaga_ticku(2, k, 0));
    k.stan = STAN_ZABLOKOWANY_MYSZ; k.termin_myszy = 1000;   // inny stan: termin zdarzenia ignorowany
    CHECK(!kandydat_wymaga_ticku(2, k, 500));
}

static void okna_podrzedne() {
    Model m;
    m.okna = true;                                 // osierocone dziecko z terminem zamkniecia
    CHECK(m.oczekuje(1, 0));
    CHECK(m.okna_wolan == 1);
    Model g;
    g.k[4].moze_byc_uruchomiony = true; g.okna = true;
    CHECK(g.oczekuje(1, 0));
    CHECK(g.okna_wolan == 0);                      // leniwie: proces gotowy rozstrzyga bez blokady okien
}

static void ocena() {
    int praca_wolan = 0;
    auto tak = [&] { ++praca_wolan; return true; };
    auto nie = [&] { ++praca_wolan; return false; };
    CHECK(ocen(false, 1, N, true, true, tak) == Decyzja::BrakOdroczonegoTicku);
    CHECK(ocen(true, 0, N, true, true, tak) == Decyzja::PozaProcesem);      // PID 0 nigdy nie oddaje programowo
    CHECK(ocen(true, -1, N, true, true, tak) == Decyzja::PozaProcesem);
    CHECK(ocen(true, N, N, true, true, tak) == Decyzja::PozaProcesem);
    CHECK(ocen(true, 1, N, false, true, tak) == Decyzja::PozaProcesem);     // przed wlaczeniem wielozadaniowosci
    CHECK(ocen(true, 1, N, true, false, tak) == Decyzja::PrzerwaniaWylaczone);
    CHECK(praca_wolan == 0);                                                // praca() tylko po warunkach 1-3
    CHECK(ocen(true, 1, N, true, true, nie) == Decyzja::BrakPracy);
    CHECK(ocen(true, N - 1, N, true, true, tak) == Decyzja::Oddaj);
    CHECK(praca_wolan == 2);
}

// Model sprawiedliwosci: proces 1 w ciaglej serii syscalli (tick zawsze trafia w Ring 0), PID 0 i proces 2 gotowe.
// Kazdy odroczony tick daje najwyzej jedno oddanie; pozostale wejscia syscalli w tym ticku nie oddaja CPU.
static void jedno_oddanie_na_tick() {
    Model m;
    m.k[0].moze_byc_uruchomiony = m.k[1].moze_byc_uruchomiony = m.k[2].moze_byc_uruchomiony = true;
    bool tick_odroczony = false; int oddan = 0;
    for (int tick = 1; tick <= 100; ++tick) {
        tick_odroczony = true;                                  // scheduler_zarejestruj_odroczony_tick
        for (int syscall = 0; syscall < 50; ++syscall) {        // wiele syscalli w jednym ticku
            const bool t = tick_odroczony; tick_odroczony = false;
            if (ocen(t, 1, N, true, true, [&] { return m.oczekuje(1, uint64_t(tick)); }) == Decyzja::Oddaj) ++oddan;
        }
    }
    CHECK(oddan == 100);
}

int main() {
    pid0_jest_konkurentem();
    biezacy_wykluczony();
    sprzatanie();
    timeout_myszy();
    timeout_zdarzenia();
    okna_podrzedne();
    ocena();
    jedno_oddanie_na_tick();
    static_assert(tick_osiagnal_termin(5, UINT64_MAX - 2), "zawiniecie");
    if (bledy) { std::printf("FAIL: %d/%d sprawdzen\n", bledy, sprawdzenia); return 1; }
    std::printf("PASS punkt zwrotu syscallu: %d sprawdzen, 0 bledow\n", sprawdzenia);
    return 0;
}
