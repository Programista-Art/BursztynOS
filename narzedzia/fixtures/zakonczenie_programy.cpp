// Programy testowe (tylko goscinne QEMU, nigdy w produkcyjnym Makefile) dla testu BWS 97 / okien podrzednych.
// Jedno zrodlo, rola wybierana -DZK_ROLA=1..4: 1 launcher (zastepuje menedzera okien), 2 ofiara (zastepuje "test"),
// 3 wlasciciel okien (zastepuje snake), 4 zawieszone dziecko (zastepuje okno informacyjne).
#include "narzedzia/fixtures/bur_v1_launcher_prefix.inc"
#include "jadro/bws/bws_procesy.h"
#include "jadro/bws/bws_api.h"

static void say(const char* s) { bws_wywolaj(BWS_WYPISZ, (uint64_t)s, 0, 0, 0); }
static void num(long v) {
    char b[24]; int n = 0; bool neg = v < 0; unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    char t[24]; int k = 0; do { t[k++] = char('0' + u % 10); u /= 10; } while (u);
    if (neg) b[n++] = '-'; while (k) b[n++] = t[--k]; b[n] = 0; say(b);
}
static uint64_t teraz() { return bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS, 0, 0, 0, 0); }
// Czekanie przez blokujace zdarzenie z timeoutem (jak prawdziwe aplikacje): proces oddaje CPU w punkcie oczekiwania,
// wiec scheduler moze sprzatac. Aktywne petle syscalli odsuwaja sprzatanie (tick trafia wtedy w Ring 0) - zob. docs.
static void pauza_ms(uint64_t ms) {
    const uint64_t do_ = teraz() + ms * 1000000ULL;
    bws_zdarzenie ev{};
    for (uint64_t t; (t = teraz()) < do_;) { const uint64_t ticki = (do_ - t) / 10000000ULL; gui_czekaj_na_zdarzenie_z_timeoutem(&ev, ticki ? ticki : 1); }
}
static bool nazwa_jest(const char* a, const char* b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }

#if ZK_ROLA == 1
static int znajdz(const char* nazwa, uint32_t* stan_out = nullptr) {
    BwsProcesInfo e[BWS_PROCESY_MAKS]{};
    const auto n = bws_wywolaj(BWS_PROCESY_LISTA, (uint64_t)e, BWS_PROCESY_MAKS, 0, 0);
    for (unsigned i = 0; i < n && i < BWS_PROCESY_MAKS; ++i)
        if (e[i].stan != 0 && nazwa_jest(nazwa, e[i].nazwa)) { if (stan_out) *stan_out = e[i].stan; return (int)e[i].pid; }
    return 0;
}
static bool czekaj_az_zniknie(const char* nazwa, uint64_t ms) {
    const uint64_t do_ = teraz() + ms * 1000000ULL;
    while (teraz() < do_) { if (!znajdz(nazwa)) return true; pauza_ms(20); }
    return !znajdz(nazwa);
}
static bool czekaj_az_pojawi(const char* nazwa, uint64_t ms, int* pid) {
    const uint64_t do_ = teraz() + ms * 1000000ULL;
    while (teraz() < do_) { *pid = znajdz(nazwa); if (*pid) return true; pauza_ms(20); }
    return false;
}
static int g_wyniki = 0, g_porazki = 0;
static void wynik(const char* nazwa, bool ok) {
    say("[ZK] "); say(nazwa); say(ok ? " PASS\n" : " FAIL\n"); if (ok) ++g_wyniki; else ++g_porazki;
}
static const char* SC_TEST = "/programy/test.cebula/test.bur";
static const char* SC_SNAKE = "/gry/snake.cebula/snake.bur";
static bool uruchom(const char* sc, const char* arg) { return bws_wywolaj(BWS_URUCHOM_PROGRAM, (uint64_t)sc, (uint64_t)arg, 0, 0) != 0; }

extern "C" __attribute__((noreturn)) void _start() {
    pauza_ms(1500);                                    // pulpit/loader ustabilizowany
    say("[ZK] start\n");
    const char* tryby[] = {"s", "w", "h", "g"};
    const char* opisy[] = {"s1 kill spin", "s2 kill event-wait", "s3 kill heap", "s4 kill rendering"};
    for (int t = 0; t < 4; ++t) {
        int pid = 0;
        bool ok = uruchom(SC_TEST, tryby[t]) && czekaj_az_pojawi("test.bur", 3000, &pid);
        pauza_ms(300);                                 // ofiara naprawde dziala (np. blokada na evencie)
        const uint64_t r1 = ok ? bws_wywolaj(BWS_PROCES_ZABIJ, pid, 0, 0, 0) : 99;
        const uint64_t r2 = ok ? bws_wywolaj(BWS_PROCES_ZABIJ, pid, 0, 0, 0) : 99;   // powtorzone: idempotentne
        const bool zniknal = ok && czekaj_az_zniknie("test.bur", 3000);
        say("[ZK] "); say(opisy[t]); say(" pid="); num(pid); say(" r1="); num((long)r1); say(" r2="); num((long)r2); say("\n");
        wynik(opisy[t], ok && r1 == BWS_ZABIJ_OK && (r2 == 0 || r2 == 1) && zniknal);
        const uint64_t r3 = ok ? bws_wywolaj(BWS_PROCES_ZABIJ, pid, 0, 0, 0) : 99;  // po sprzataniu: slot pusty -> odmowa
        wynik("s5 kill po sprzataniu = odmowa", ok && r3 == 0);
    }
    // nieprawidlowe / chronione PID
    {
        const uint64_t bad[] = {0, 99, 100, 1000, 16, 17, 0xFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0x100000001ULL};
        bool ok = true;
        for (uint64_t p : bad) { const uint64_t r = bws_wywolaj(BWS_PROCES_ZABIJ, p, 0, 0, 0);
            say("[ZK] invalid pid="); num((long)p); say(" r="); num((long)r); say("\n"); if (r != 0) ok = false; }
        int self = znajdz("menedzer_okien.bur"); if (!self) self = 1;
        const uint64_t rs = bws_wywolaj(BWS_PROCES_ZABIJ, self, 0, 0, 0);   // samego siebie (WM) - odmowa
        say("[ZK] self r="); num((long)rs); say("\n");
        wynik("s6 niepoprawne/chronione PID", ok && rs == 0);
    }
    // wiele cykli: PID reuse + pomiar ramek (log [PROC-CLEANUP] wolne_ramki)
    {
        say("[ZK] cycles begin\n");
        bool ok = true; int pierwszy = 0, ostatni = 0; int powt = 0;
        for (int i = 0; i < 40; ++i) {
            int pid = 0;
            if (!(uruchom(SC_TEST, (i & 1) ? "h" : "s") && czekaj_az_pojawi("test.bur", 3000, &pid))) { ok = false; break; }
            if (i == 0) pierwszy = pid; else if (pid == ostatni || pid == pierwszy) ++powt;
            ostatni = pid;
            if (bws_wywolaj(BWS_PROCES_ZABIJ, pid, 0, 0, 0) != BWS_ZABIJ_OK) { ok = false; break; }
            if (!czekaj_az_zniknie("test.bur", 3000)) { ok = false; break; }
        }
        say("[ZK] cycles end reuse="); num(powt); say("\n");
        wynik("s7 40 cykli uruchom/zabij (PID reuse)", ok && powt > 0);
    }
    // okna podrzedne: wlasciciel 'o' = jedno okno ponawiane co 200 ms; 'm' = trzy rodzaje; 'k' = proba zabicia launchera
    {
        int owner = 0, dziecko = 0;
        bool ok = uruchom(SC_SNAKE, "o") && czekaj_az_pojawi("okno-informacyjne.bur", 4000, &dziecko);
        owner = znajdz("snake.bur");
        pauza_ms(300);
        const uint64_t r = ok ? bws_wywolaj(BWS_PROCES_ZABIJ, dziecko, 0, 0, 0) : 99;       // bezposrednie zabicie dziecka
        const bool zniknalo = ok && czekaj_az_zniknie("okno-informacyjne.bur", 3000);
        int nowe = 0;
        const bool nowe_ok = ok && czekaj_az_pojawi("okno-informacyjne.bur", 4000, &nowe);   // wlasciciel zyje -> nowe dziecko
        say("[ZK] child kill r="); num((long)r); say(" old="); num(dziecko); say(" new="); num(nowe); say("\n");
        wynik("s8 zabicie dziecka, wlasciciel zyje, nowe dziecko", ok && owner && r == 1 && zniknalo && nowe_ok && znajdz("snake.bur") == owner);
        // zabicie wlasciciela: dziecko (zawieszone, ignoruje ZAMKNIJ) ma zniknac po terminie ~1.5 s
        const uint64_t t0 = teraz();
        const uint64_t rw = bws_wywolaj(BWS_PROCES_ZABIJ, owner, 0, 0, 0);
        const bool w_zn = czekaj_az_zniknie("snake.bur", 3000);
        const bool d_zn = czekaj_az_zniknie("okno-informacyjne.bur", 6000);
        say("[ZK] owner kill r="); num((long)rw); say(" ms="); num((long)((teraz() - t0) / 1000000ULL)); say("\n");
        wynik("s9 zabicie wlasciciela -> zawieszone dziecko wymuszone", rw == 1 && w_zn && d_zn);
        pauza_ms(500);
        // wiele dzieci naraz
        int o2 = 0; ok = uruchom(SC_SNAKE, "m") && czekaj_az_pojawi("snake.bur", 3000, &o2);
        pauza_ms(1200);
        int ile = 0; { BwsProcesInfo e[BWS_PROCESY_MAKS]{}; const auto n = bws_wywolaj(BWS_PROCESY_LISTA, (uint64_t)e, BWS_PROCESY_MAKS, 0, 0);
            for (unsigned i = 0; i < n && i < BWS_PROCESY_MAKS; ++i) if (e[i].stan != 0 && nazwa_jest("okno-informacyjne.bur", e[i].nazwa)) ++ile; }
        const uint64_t rm = ok ? bws_wywolaj(BWS_PROCES_ZABIJ, o2, 0, 0, 0) : 99;
        const bool wszystkie = czekaj_az_zniknie("okno-informacyjne.bur", 7000) && czekaj_az_zniknie("snake.bur", 3000);
        say("[ZK] multi children="); num(ile); say("\n");
        wynik("s10 wiele dzieci, zabicie wlasciciela", ok && ile == 3 && rm == 1 && wszystkie);
        pauza_ms(500);
        // brak uprawnien: wlasciciel probuje zabic launchera
        int me = znajdz("menedzer_okien.bur"); char arg[16] = "k"; { int k = 1; int v = me; char t[8]; int n = 0; do { t[n++] = char('0' + v % 10); v /= 10; } while (v); while (n) arg[k++] = t[--n]; arg[k] = 0; }
        int o3 = 0; ok = uruchom(SC_SNAKE, arg) && czekaj_az_pojawi("snake.bur", 3000, &o3);
        pauza_ms(800);
        wynik("s11 proces bez prawa nie zabije launchera", ok && znajdz("menedzer_okien.bur") == me);
        if (o3) bws_wywolaj(BWS_PROCES_ZABIJ, o3, 0, 0, 0);
        czekaj_az_zniknie("snake.bur", 3000);
        // petla szybkiego tworzenia/niszczenia dzieci
        bool petla = true;
        for (int i = 0; i < 12 && petla; ++i) {
            int o = 0, d = 0;
            if (!(uruchom(SC_SNAKE, "o") && czekaj_az_pojawi("okno-informacyjne.bur", 4000, &d))) { petla = false; break; }
            o = znajdz("snake.bur");
            const uint64_t rk = bws_wywolaj(BWS_PROCES_ZABIJ, o, 0, 0, 0);
            if (rk != 1) petla = false;
            const bool z1 = czekaj_az_zniknie("okno-informacyjne.bur", 6000), z2 = czekaj_az_zniknie("snake.bur", 3000);
            say("[ZK] s12 iter="); num(i); say(" o="); num(o); say(" d="); num(d); say(" rk="); num((long)rk); say(" z1="); num(z1); say(" z2="); num(z2); say("\n");
            if (!z1 || !z2) petla = false;
        }
        wynik("s12 12 cykli wlasciciel+dziecko", petla);
    }
    say("[ZK] KONIEC pass="); num(g_wyniki); say(" fail="); num(g_porazki); say("\n");
    for (;;) pauza_ms(1000);
}
#elif ZK_ROLA == 2
extern "C" __attribute__((noreturn)) void _start() {
    char tryb[16]{}; bws_wywolaj(BWS_ARGUMENT_STARTOWY, (uint64_t)tryb, sizeof(tryb), 0, 0);
    if (tryb[0] == 'w') { bws_zdarzenie ev{}; for (;;) gui_czekaj_na_zdarzenie(&ev); }
    if (tryb[0] == 'h') {
        auto p = (volatile char*)bws_wywolaj(BWS_ALOKUJ_STERTE, 4 * 1024 * 1024, 0, 0, 0);
        if (p) for (unsigned long i = 0; i < 4UL * 1024 * 1024; i += 4096) p[i] = 1;
    }
    if (tryb[0] == 'g') {
        for (unsigned long i = 0;; ++i) { gui_rysuj_okno(40, 40, 300, 200, "ofiara"); gui_wypisz_tekst(50, 80, "render"); gui_odswiez(); if ((i & 63) == 0) asm volatile("pause"); }
    }
    for (;;) asm volatile("pause");
}
#elif ZK_ROLA == 3
extern "C" __attribute__((noreturn)) void _start() {
    char tryb[24]{}; bws_wywolaj(BWS_ARGUMENT_STARTOWY, (uint64_t)tryb, sizeof(tryb), 0, 0);
    if (tryb[0] == 'k') {                               // brak PRAWA zarzadzania procesami: odmowa
        long pid = 0; for (int i = 1; tryb[i]; ++i) pid = pid * 10 + (tryb[i] - '0');
        const uint64_t r = bws_wywolaj(BWS_PROCES_ZABIJ, (uint64_t)pid, 0, 0, 0);
        say("[ZK] unpriv kill r="); num((long)r); say("\n");
        for (;;) pauza_ms(500);
    }
    static const char T[] = "Test dziecka";
    static const char C[] = "Tresc okna podrzednego do testu zakonczenia procesow.";
    for (;;) {
        if (tryb[0] == 'm') { for (uint32_t rodzaj = 1; rodzaj <= 3; ++rodzaj) gui_okno_podrzedne_otworz(rodzaj, T, C, sizeof(C) - 1); pauza_ms(1000000); }
        else gui_okno_podrzedne_otworz(1, T, C, sizeof(C) - 1);
        pauza_ms(200);
    }
}
#else
extern "C" __attribute__((noreturn)) void _start() { for (;;) asm volatile("pause"); }   // zawieszone dziecko: ignoruje ZAMKNIJ
#endif
