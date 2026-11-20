// Kalendarz - testy hosta (ASan/UBSan): komponent BursztynUI::Kalendarz, ABI daty BWS 9 (czas lokalny V1),
// pomocnik bursztyn_ui/data_systemowa.h, aplikacja Kalendarz (okno BursztynUI) i polozenie panelu paska zadan.
// Daty sa sprawdzane wzgledem niezaleznej referencji (liczenie dni od 1900-01-01, poniedzialek), nie wzgledem
// tych samych wzorow co w kodzie.
#include "bursztyn_ui_host_fixture.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <new>

static size_t g_alokacje = 0;
void* operator new(size_t n) { ++g_alokacje; void* p = malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](size_t n) { ++g_alokacje; void* p = malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }

#define private public
#include "../bursztyn_ui/bursztyn_ui.h"
#include "../bursztyn_ui/data_systemowa.h"
#include "../programy/kalendarz/okno.h"
#include "kalendarz_paska.h"
#undef private

using namespace BursztynUI;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL test_kalendarz_host.cpp:%d: %s\n", __LINE__, #x); std::abort(); } } while (0)

// ---------------------------------------------------------------- referencja
static bool ref_przestepny(int r) {           // definicja: co 4 lata, poza latami stulecia niepodzielnymi przez 400
    if (r % 400 == 0) return true;
    if (r % 100 == 0) return false;
    return r % 4 == 0;
}
static int ref_dni(int r, int m) {
    const int t[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && ref_przestepny(r) ? 29 : t[m - 1];
}
static long ref_numer(int r, int m, int d) {    // dni od 1900-01-01
    long n = 0;
    for (int y = 1900; y < r; ++y) n += ref_przestepny(y) ? 366 : 365;
    for (int k = 1; k < m; ++k) n += ref_dni(r, k);
    return n + d - 1;
}
static int ref_dow(int r, int m, int d) { return int(ref_numer(r, m, d) % 7); }   // 1900-01-01 = poniedzialek = 0
static void ref_data(long n, int& r, int& m, int& d) {
    r = 1900; while (n >= (ref_przestepny(r) ? 366 : 365)) { n -= ref_przestepny(r) ? 366 : 365; ++r; }
    m = 1; while (n >= ref_dni(r, m)) { n -= ref_dni(r, m); ++m; }
    d = int(n) + 1;
}
static int ref_iso_tydzien(int r, int m, int d) {   // tydzien zawierajacy czwartek
    const long n = ref_numer(r, m, d);
    const long czw = n + (3 - ref_dow(r, m, d));
    int cr, cm, cd; ref_data(czw, cr, cm, cd);
    return int((ref_numer(cr, cm, cd) - ref_numer(cr, 1, 1)) / 7) + 1;
}

// ---------------------------------------------------------------- pomocnicze
struct Bufor { int w = 0, h = 0; std::vector<uint32_t> px; };
static Bufor narysuj(Kontrolka& k) {
    Bufor b; b.w = k.obszar().szerokosc; b.h = k.obszar().wysokosc; b.px.assign(size_t(b.w) * b.h, 0x123456);
    KontekstRysowania c(Prostokat(0, 0, b.w, b.h)); c.ustaw_cel(b.px.data(), Prostokat(0, 0, b.w, b.h));
    k.rysuj_siebie(c);
    return b;
}
static int ile(const Bufor& b, Prostokat r, uint32_t kolor) {
    int n = 0;
    for (int y = r.y < 0 ? 0 : r.y; y < r.dolny() && y < b.h; ++y)
        for (int x = r.x < 0 ? 0 : r.x; x < r.prawy() && x < b.w; ++x)
            if ((b.px[size_t(y) * b.w + x] & 0xFFFFFF) == (kolor & 0xFFFFFF)) ++n;
    return n;
}
static void klik(Kontrolka& k, Prostokat lokalny) {
    ZdarzenieMyszki e{}; e.typ = TypZdarzenia::WcisniecieMyszy; e.przycisk = 1;
    e.x = k.obszar_ekranu().x + lokalny.x + lokalny.szerokosc / 2; e.y = k.obszar_ekranu().y + lokalny.y + lokalny.wysokosc / 2;
    k.obsluz_wcisniecie_myszy(e);
    ZdarzenieMyszki u = e; u.typ = TypZdarzenia::ZwolnienieMyszy; k.obsluz_zwolnienie_myszy(u);
}
static void klik(Kontrolka& k, Prostokat lokalny);
static void nacisnij(Przycisk* b) { klik(*b, Prostokat(0, 0, b->obszar().szerokosc, b->obszar().wysokosc)); }
static void klawisz(Kontrolka& k, uint32_t kod) { ZdarzenieKlawiatury e{}; e.kod_klawisza = kod; k.obsluz_wcisniecie_klawisza(e); }

// ---------------------------------------------------------------- 1. obliczenia dat
static void obliczenia() {
    // dni w miesiacu i lata przestepne (takze reguly stuleci) - wszystkie 12 miesiecy, caly zakres komponentu
    for (int r = 1900; r <= 2100; ++r) {
        CHECK(DataKalendarza::czy_przestepny(r) == ref_przestepny(r));
        CHECK(CzasLokalny::przestepny(r) == ref_przestepny(r));
        for (int m = 1; m <= 12; ++m) {
            CHECK(DataKalendarza::dni_w_miesiacu(r, m) == ref_dni(r, m));
            CHECK(CzasLokalny::dni_w_miesiacu(r, m) == ref_dni(r, m));
            for (int d = 1; d <= ref_dni(r, m); ++d) {
                const int dow = ref_dow(r, m, d);
                if (DataKalendarza::dzien_tygodnia(r, m, d) != dow || CzasLokalny::dzien_tygodnia(r, m, d) != dow) { CHECK(false); return; }
                DataKalendarza x{r, m, d};
                if (x.tydzien_iso() != ref_iso_tydzien(r, m, d) || x.dzien_roku() != int(ref_numer(r, m, d) - ref_numer(r, 1, 1)) + 1)
                    { std::fprintf(stderr, "%d-%d-%d iso %d/%d\n", r, m, d, x.tydzien_iso(), ref_iso_tydzien(r, m, d)); CHECK(false); return; }
                if (KalendarzApp::numer_dnia(x) != ref_numer(r, m, d)) { CHECK(false); return; }
            }
            CHECK(DataKalendarza::dzien_tygodnia(r, m, ref_dni(r, m) + 1) == -1);   // 31 kwietnia itp.
        }
    }
    CHECK(!DataKalendarza::czy_przestepny(1900) && DataKalendarza::czy_przestepny(2000) && !DataKalendarza::czy_przestepny(2100));
    CHECK(DataKalendarza::dni_w_miesiacu(2024, 2) == 29 && DataKalendarza::dni_w_miesiacu(2026, 2) == 28);
    CHECK(DataKalendarza::dzien_tygodnia(2026, 11, 19) == 3);                     // czwartek
    CHECK((!DataKalendarza{2026, 2, 29}.czy_poprawna() && DataKalendarza{2028, 2, 29}.czy_poprawna()));
    CHECK((!DataKalendarza{2026, 13, 1}.czy_poprawna() && !DataKalendarza{2026, 0, 1}.czy_poprawna() && !DataKalendarza{2026, 4, 31}.czy_poprawna()));
    // polskie nazwy i format
    char t[64];
    DataKalendarza{2026, 11, 19}.formatuj(t, sizeof(t)); CHECK(!std::strcmp(t, "czwartek, 19 listopada 2026"));
    DataKalendarza{2024, 2, 29}.formatuj(t, sizeof(t)); CHECK(!std::strcmp(t, "czwartek, 29 lutego 2024"));
    DataKalendarza{2027, 1, 1}.formatuj(t, sizeof(t), false); CHECK(!std::strcmp(t, "1 stycznia 2027"));
    DataKalendarza{2026, 9, 7}.formatuj(t, sizeof(t)); CHECK(!std::strcmp(t, "poniedziałek, 7 września 2026"));
    DataKalendarza{2026, 4, 31}.formatuj(t, sizeof(t)); CHECK(t[0] == 0);
    char maly[8]; DataKalendarza{2026, 11, 19}.formatuj(maly, sizeof(maly)); CHECK(std::strlen(maly) == 7);   // bez przepelnienia
    CHECK(!std::strcmp(DataKalendarza::nazwa_miesiaca(2), "Luty") && !std::strcmp(DataKalendarza::nazwa_miesiaca_dopelniacz(10), "października"));
    CHECK(!std::strcmp(DataKalendarza::nazwa_dnia_tygodnia(2), "środa") && DataKalendarza::nazwa_miesiaca(13)[0] == 0);
    // ISO: 2026-12-31 (czw) tydzien 53; 2027-01-01 (pt) tez 53; 2024-12-30 (pn) tydzien 1 roku 2025
    CHECK((DataKalendarza{2026, 12, 31}.tydzien_iso() == 53 && DataKalendarza{2027, 1, 1}.tydzien_iso() == 53));
    CHECK((DataKalendarza{2024, 12, 30}.tydzien_iso() == 1 && DataKalendarza{2026, 11, 19}.tydzien_iso() == 47));
    // opis dnia
    char r[32];
    KalendarzApp::odleglosc(0, r, sizeof(r)); CHECK(!std::strcmp(r, "dzisiaj"));
    KalendarzApp::odleglosc(1, r, sizeof(r)); CHECK(!std::strcmp(r, "jutro"));
    KalendarzApp::odleglosc(-1, r, sizeof(r)); CHECK(!std::strcmp(r, "wczoraj"));
    KalendarzApp::odleglosc(12, r, sizeof(r)); CHECK(!std::strcmp(r, "za 12 dni"));
    KalendarzApp::odleglosc(-3, r, sizeof(r)); CHECK(!std::strcmp(r, "3 dni temu"));
    auto o = KalendarzApp::opisz({2027, 1, 1}, {2026, 12, 31}, true);
    CHECK(!std::strcmp(o.data, "piątek, 1 stycznia 2027") && std::strstr(o.szczegoly, "Dzień roku: 1 · Tydzień ISO: 53 · jutro"));
    o = KalendarzApp::opisz({2026, 11, 19}, {}, false);
    CHECK(std::strstr(o.szczegoly, "data systemowa niedostępna"));
}

// ---------------------------------------------------------------- 2. ABI czasu lokalnego (BWS 9)
static BwsCzasLokalny g_czas{};
static uint64_t g_wynik9 = 1, g_wywolan9 = 0;
static uint64_t bws_test(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t, uint64_t) {
    if (nr != 9 || a2 != BWS_CZAS_LOKALNY_V1) return 0;
    ++g_wywolan9;
    if (g_wynik9 == 1) *reinterpret_cast<BwsCzasLokalny*>(a1) = g_czas;
    return g_wynik9;
}
static void ustaw_czas(int r, int m, int d) { CHECK(CzasLokalny::wypelnij(g_czas, r, m, d, 12, 30, 5, BWS_CZAS_STREFA_CET)); g_wynik9 = 1; }

static void abi_czasu() {
    BwsCzasLokalny c{};
    CHECK(CzasLokalny::wypelnij(c, 2026, 11, 19, 23, 59, 59, BWS_CZAS_STREFA_CET));
    CHECK(c.wersja == BWS_CZAS_WERSJA && c.rozmiar == sizeof(c) && c.dzien_tygodnia == 3 && (c.flagi & BWS_CZAS_PROBKA_RTC) && CzasLokalny::poprawny(c));
    CHECK(!CzasLokalny::wypelnij(c, 2026, 2, 29, 0, 0, 0, 0) && !CzasLokalny::poprawny(c));
    CHECK(!CzasLokalny::wypelnij(c, 2026, 11, 19, 24, 0, 0, 0));
    BwsCzasLokalny z{}; CzasLokalny::wypelnij(z, 2026, 11, 19, 1, 2, 3, 0);
    z.dzien_tygodnia = 4; CHECK(!CzasLokalny::poprawny(z));                       // niespojny dzien tygodnia
    CzasLokalny::wypelnij(z, 2026, 11, 19, 1, 2, 3, 0); z.flagi = 0; CHECK(!CzasLokalny::poprawny(z));   // bez probki RTC
    CzasLokalny::wypelnij(z, 2026, 11, 19, 1, 2, 3, 0); z.wersja = 2; CHECK(!CzasLokalny::poprawny(z));
    CHECK(BWS_CZAS_LOKALNY_V1 != 0);                                               // arg2 == 0 = stary tekst "HH:MM:SS"
    CHECK(czas_lokalny_tryb_tekstowy(0) && !czas_lokalny_tryb_tekstowy(BWS_CZAS_LOKALNY_V1));
    CHECK(!czas_lokalny_tryb_tekstowy(BWS_CZAS_LOKALNY_V1 + 1) && !czas_lokalny_tryb_tekstowy(1));   // nieznane tryby: blad bez zapisu
    CHECK(sizeof(BwsCzasLokalny) == 2 * 4 + 2 + 6 * 1 + 4 + 4);                   // bez dopelnienia (zadnych bajtow stosu jadra)
    // pomocnik Ring 3
    DataKalendarza d{};
    ustaw_czas(2028, 2, 29);
    CHECK(pobierz_date_systemowa(&d, bws_test) && d == (DataKalendarza{2028, 2, 29}));
    g_wynik9 = 0; CHECK(!pobierz_date_systemowa(&d, bws_test));                  // brak probki RTC: zadnej daty zastepczej
    g_wynik9 = 1; g_czas.miesiac = 13; CHECK(!pobierz_date_systemowa(&d, bws_test));
    CzasLokalny::wypelnij(g_czas, 2150, 1, 1, 0, 0, 0, 0); CHECK(!pobierz_date_systemowa(&d, bws_test));   // poza zakresem komponentu
    CHECK(!pobierz_date_systemowa(nullptr, bws_test));
}

// ---------------------------------------------------------------- 3. komponent
static void komponent() {
    for (uint32_t skala : {100u, 150u, 200u}) {
        Kalendarz k({2026, 11, 19});
        k.ustaw_skale_dpi(SkalaUI::procent(skala));
        k.ustaw_geometrie(0, 0, 320 * int(skala) / 100, 280 * int(skala) / 100);
        // uklad: kazdy dzien kazdego miesiaca w kolumnie swojego dnia tygodnia (poniedzialek = kolumna 0)
        for (int r : {1900, 1999, 2000, 2024, 2026, 2027, 2100})
            for (int m = 1; m <= 12; ++m) {
                k.ustaw_widok(r, m);
                CHECK(k.widok_rok() == r && k.widok_miesiac() == m);
                CHECK(k.pierwszy_dzien_tygodnia() == ref_dow(r, m, 1) && k.dni_widocznego_miesiaca() == ref_dni(r, m));
                for (int d = 1; d <= ref_dni(r, m); ++d) CHECK(k.komorka_dla_dnia(d) % 7 == ref_dow(r, m, d) && k.komorka_dla_dnia(d) < 42);
            }
        // nawigacja miesiacami i latami, przejscie grudzien -> styczen, granice zakresu bez wyjscia poza tablice
        k.ustaw_widok(2026, 12); k.nastepny_miesiac(); CHECK(k.widok_rok() == 2027 && k.widok_miesiac() == 1);
        k.poprzedni_miesiac(); CHECK(k.widok_rok() == 2026 && k.widok_miesiac() == 12);
        k.nastepny_rok(); CHECK(k.widok_rok() == 2027 && k.widok_miesiac() == 12);
        k.ustaw_widok(1900, 1); k.poprzedni_miesiac(); k.poprzedni_rok(); CHECK(k.widok_rok() == 1900 && k.widok_miesiac() == 1);
        k.ustaw_widok(2100, 12); k.nastepny_miesiac(); k.nastepny_rok(); CHECK(k.widok_rok() == 2100 && k.widok_miesiac() == 12);
        k.ustaw_widok(2026, 13); k.ustaw_widok(0, 5); CHECK(k.widok_rok() == 2100 && k.widok_miesiac() == 12);   // odrzucone
        // przyciski nawigacji: rozlaczne, mieszcza swoj tekst, tytul miedzy nimi (natywnie "<<" i "<" zlewaly sie)
        {
            Prostokat p0 = k.prostokat_przycisku_nawigacji(0), p1 = k.prostokat_przycisku_nawigacji(1),
                      p2 = k.prostokat_przycisku_nawigacji(2), p3 = k.prostokat_przycisku_nawigacji(3);
            const SkalaUI sk = SkalaUI::procent(skala);
            CHECK(p0.prawy() < p1.x && p1.prawy() < p2.x && p2.prawy() < p3.x && p3.prawy() <= k.obszar().szerokosc);
            CHECK(p0.szerokosc >= CzcionkaUI::zmierz_tekst("<<", sk) + 8 * sk && p1.szerokosc >= CzcionkaUI::zmierz_tekst("<", sk) + 8 * sk);
            CHECK(p0.wysokosc >= CzcionkaUI::pobierz_wysokosc(sk) && k.prostokat_komorki(0).y >= p0.dolny());
        }
        // opcjonalny pasek nawigacji: domyslnie widoczny; ukryty = brak przyciskow i trafien, wiecej miejsca na siatke
        {
            CHECK(k.czy_pasek_nawigacji());
            const int y0 = k.prostokat_komorki(0).y, h0 = k.prostokat_komorki(0).wysokosc;
            const Prostokat btn = k.prostokat_przycisku_nawigacji(2);
            k.ustaw_pasek_nawigacji(false);
            CHECK(!k.czy_pasek_nawigacji() && k.prostokat_przycisku_nawigacji(2).czy_pusty());
            CHECK(k.prostokat_komorki(0).y < y0 && k.prostokat_komorki(0).wysokosc >= h0);
            const int m = k.widok_miesiac();
            klik(k, btn); CHECK(k.widok_miesiac() == m);                              // miejsce dawnego przycisku: bez nawigacji
            { Bufor nb = narysuj(k); CHECK(nb.w > 0); }                              // rysowanie bez paska (ASan)
            klawisz(k, 0x51); CHECK(k.wybrana_data().miesiac != 0);                  // klawiatura dziala dalej
            k.ustaw_pasek_nawigacji(true);
            CHECK(k.prostokat_komorki(0).y == y0 && !k.prostokat_przycisku_nawigacji(2).czy_pusty());
            k.ustaw_wybrana_data({2026, 11, 19});
        }
        // wybrana data w sasiednim miesiacu widoczna w szarej komorce: ramka (wybor czytelny po nawigacji)
        {
            k.ustaw_dzisiaj({0, 0, 0});
            k.ustaw_wybrana_data({2026, 11, 30});
            k.ustaw_widok(2026, 12);                                                   // 30.11 jest w pierwszym wierszu grudnia
            CHECK(!k.wybrana_w_widoku());
            const int kom = k.pierwszy_dzien_tygodnia() - 1;                          // komorka przed 1.12
            Bufor nb = narysuj(k);
            CHECK(ile(nb, k.prostokat_komorki(kom), motyw_kolor(MOTYW_KOLOR_AKCENT_NAJECHANY)) > 4);
            CHECK(ile(nb, k.prostokat_komorki(kom - 1), motyw_kolor(MOTYW_KOLOR_AKCENT_NAJECHANY)) == 0);
            k.ustaw_widok(2026, 11); CHECK(k.wybrana_w_widoku());
            k.ustaw_widok(2100, 12);                                                   // stan sprzed sekcji
        }
        // wywolanie zwrotne widoku
        int zmian = 0; DataKalendarza ost{};
        struct Ctx { int* z; DataKalendarza* o; } ctx{&zmian, &ost};
        k.przy_zmianie_widoku([](Kalendarz*, DataKalendarza d, void* p) { auto* c = static_cast<Ctx*>(p); ++*c->z; *c->o = d; }, &ctx);
        k.ustaw_widok(2026, 11); CHECK(zmian == 1 && ost == (DataKalendarza{2026, 11, 1}));
        k.ustaw_widok(2026, 11); CHECK(zmian == 1);                                       // bez zmiany - bez powiadomienia
        // wybor myszka (cala komorka) i przyciski nawigacji
        klik(k, k.prostokat_komorki(k.komorka_dla_dnia(25)));
        CHECK(k.wybrana_data() == (DataKalendarza{2026, 11, 25}));
        klik(k, k.prostokat_przycisku_nawigacji(2)); CHECK(k.widok_miesiac() == 12 && zmian == 2);
        klik(k, k.prostokat_przycisku_nawigacji(1)); klik(k, k.prostokat_przycisku_nawigacji(1)); CHECK(k.widok_miesiac() == 10);
        klik(k, k.prostokat_przycisku_nawigacji(3)); CHECK(k.widok_rok() == 2027);
        klik(k, k.prostokat_przycisku_nawigacji(0)); CHECK(k.widok_rok() == 2026);
        CHECK(k.wybrana_data() == (DataKalendarza{2026, 11, 25}));                    // nawigacja nie zmienia wyboru
        // klawiatura: strzalki, Page Up/Down, Home = dzisiaj
        k.ustaw_wybrana_data({2026, 12, 31});
        klawisz(k, 0x4D); CHECK(k.wybrana_data() == (DataKalendarza{2027, 1, 1}) && k.widok_rok() == 2027);
        klawisz(k, 0x4B); CHECK(k.wybrana_data() == (DataKalendarza{2026, 12, 31}));
        klawisz(k, 0x48); CHECK(k.wybrana_data() == (DataKalendarza{2026, 12, 24}));
        klawisz(k, 0x50); klawisz(k, 0x50); CHECK(k.wybrana_data() == (DataKalendarza{2027, 1, 7}));
        k.ustaw_wybrana_data({2026, 1, 31}); klawisz(k, 0x51); CHECK(k.wybrana_data() == (DataKalendarza{2026, 2, 28}));   // obciecie dnia
        k.ustaw_wybrana_data({2024, 1, 31}); klawisz(k, 0x51); CHECK(k.wybrana_data() == (DataKalendarza{2024, 2, 29}));
        klawisz(k, 0x49); klawisz(k, 0x49); CHECK(k.wybrana_data() == (DataKalendarza{2023, 12, 29}));
        klawisz(k, 0x47); CHECK(k.wybrana_data() == (DataKalendarza{2023, 12, 29}));   // Home bez daty systemowej: nic
        // dzisiaj: wyroznienie niezalezne od wyboru, akcja "Dzisiaj"
        CHECK(!k.ma_dzisiaj() && !k.pokaz_dzisiaj());
        k.ustaw_dzisiaj({2026, 11, 19}); CHECK(k.ma_dzisiaj());
        klawisz(k, 0x47); CHECK(k.wybrana_data() == (DataKalendarza{2026, 11, 19}) && k.widok_miesiac() == 11);
        k.ustaw_widok(2030, 5); CHECK(k.pokaz_dzisiaj() && k.widok_rok() == 2026 && k.widok_miesiac() == 11);   // juz wybrane: tylko widok
        k.ustaw_wybrana_data({2026, 11, 3});
        CHECK(k.czy_dzisiaj_w_komorce(k.komorka_dla_dnia(19)) && !k.czy_dzisiaj_w_komorce(k.komorka_dla_dnia(3)));
        Bufor b = narysuj(k);
        const uint32_t akcent = motyw_kolor(MOTYW_KOLOR_AKCENT), zazn = motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE);
        CHECK(ile(b, k.prostokat_komorki(k.komorka_dla_dnia(19)), akcent) > 20);         // dzisiaj: akcent
        CHECK(ile(b, k.prostokat_komorki(k.komorka_dla_dnia(3)), zazn) > 20);            // wybor: zaznaczenie
        CHECK(ile(b, k.prostokat_komorki(k.komorka_dla_dnia(10)), akcent) == 0 && ile(b, k.prostokat_komorki(k.komorka_dla_dnia(10)), zazn) == 0);
        k.ustaw_wybrana_data({2026, 11, 19});                                          // dzisiaj wybrane: zaznaczenie + ramka akcentu
        b = narysuj(k);
        CHECK(ile(b, k.prostokat_komorki(k.komorka_dla_dnia(19)), zazn) > 20 && ile(b, k.prostokat_komorki(k.komorka_dla_dnia(19)), akcent) > 4);
        // zmiana dzisiaj: przerysowanie tylko dwoch komorek, bez przeliczania siatki
        k.wyczysc_brudny_obszar();
        const uint64_t przel = k.liczba_przeliczen_miesiaca();
        k.ustaw_dzisiaj({2026, 11, 20});
        CHECK(k.liczba_przeliczen_miesiaca() == przel);
        k.ustaw_dzisiaj({2026, 2, 30}); CHECK(!k.ma_dzisiaj());                        // niepoprawna data = brak wyroznienia
        // ruch myszy: bez przeliczania miesiaca, bez alokacji
        const size_t a0 = g_alokacje; const uint64_t p0 = k.liczba_przeliczen_miesiaca();
        for (int i = 0; i < 400; ++i) {
            ZdarzenieMyszki e{}; e.typ = TypZdarzenia::RuchMyszy; e.x = (i * 7) % k.obszar().szerokosc; e.y = (i * 13) % k.obszar().wysokosc;
            k.obsluz_ruch_myszy(e);
        }
        CHECK(g_alokacje == a0 && k.liczba_przeliczen_miesiaca() == p0);
        k.przy_zmianie_widoku(nullptr);
    }
}

// ---------------------------------------------------------------- 4. aplikacja Kalendarz
static void aplikacja() {
    ustaw_czas(2026, 11, 19);
    auto* o = new KalendarzApp::OknoKalendarza;
    o->bws = bws_test;
    CHECK(o->szerokosc() == KalendarzApp::OknoKalendarza::SZEROKOSC);
    o->init(); o->przelicz_uklad();
    KontekstRysowania c0; o->rysuj(c0, true);
    Kalendarz* k = o->kalendarz();
    CHECK(o->ma_dzisiaj() && k->dzisiaj() == (DataKalendarza{2026, 11, 19}) && k->wybrana_data() == k->dzisiaj());
    CHECK(!std::strcmp(o->tytul()->tekst(), "Listopad 2026"));
    CHECK(!std::strcmp(o->etykieta_daty()->tekst(), "Wybrana data: czwartek, 19 listopada 2026"));
    CHECK(std::strstr(o->etykieta_szczegolow()->tekst(), "Dzień roku: 323 · Tydzień ISO: 47 · dzisiaj"));
    CHECK(o->przycisk_dzisiaj()->czy_wlaczona());
    // nawigacja przyciskami okna: rok sie zmienia na przelomie, tytul nadaza
    nacisnij(o->przycisk_nastepny()); nacisnij(o->przycisk_nastepny());
    CHECK(!std::strcmp(o->tytul()->tekst(), "Styczeń 2027") && k->widok_rok() == 2027);
    nacisnij(o->przycisk_poprzedni()); CHECK(!std::strcmp(o->tytul()->tekst(), "Grudzień 2026"));
    // jedna nawigacja: pasek komponentu ukryty w aplikacji, rok przyciskami naglowka
    CHECK(!k->czy_pasek_nawigacji());
    nacisnij(o->przycisk_nastepny_rok()); CHECK(!std::strcmp(o->tytul()->tekst(), "Grudzień 2027"));
    nacisnij(o->przycisk_poprzedni_rok()); nacisnij(o->przycisk_poprzedni_rok()); CHECK(!std::strcmp(o->tytul()->tekst(), "Grudzień 2025"));
    // wybor zachowany przy nawigacji, opis mowi, ze lezy poza widocznym miesiacem
    CHECK(std::strstr(o->etykieta_daty()->tekst(), "19 listopada 2026 · poza widocznym miesiącem"));
    nacisnij(o->przycisk_nastepny_rok());
    // wybor dnia i opis
    k->ustaw_wybrana_data({2026, 12, 24});
    CHECK(!std::strcmp(o->etykieta_daty()->tekst(), "Wybrana data: czwartek, 24 grudnia 2026"));   // w widoku: bez dopisku
    CHECK(std::strstr(o->etykieta_szczegolow()->tekst(), "za 35 dni"));
    // Dzisiaj
    nacisnij(o->przycisk_dzisiaj());
    CHECK(k->wybrana_data() == (DataKalendarza{2026, 11, 19}) && !std::strcmp(o->tytul()->tekst(), "Listopad 2026"));
    // zmiana daty systemowej (polnoc) przy otwartym oknie: wyroznienie i opis, wybor bez zmian
    ustaw_czas(2026, 11, 20);
    o->sprawdz_date();
    CHECK(k->dzisiaj() == (DataKalendarza{2026, 11, 20}) && k->wybrana_data() == (DataKalendarza{2026, 11, 19}));
    CHECK(std::strstr(o->etykieta_szczegolow()->tekst(), "wczoraj"));
    // bez zmiany daty: zadnych przerysowan ani alokacji (zadanie okresowe co ~30 s)
    { KontekstRysowania c; o->rysuj(c, false); }
    const size_t a0 = g_alokacje; const uint64_t pelne = o->liczba_pelnych_odrysowan(), px0 = o->liczba_brudnych_pikseli();
    for (int i = 0; i < 20; ++i) { o->sprawdz_date(); KontekstRysowania c; o->rysuj(c, false); }
    CHECK(g_alokacje == a0 && o->liczba_pelnych_odrysowan() == pelne && o->liczba_brudnych_pikseli() == px0);
    // brak daty systemowej: "niedostepna", Dzisiaj wylaczony, nic nie zgadujemy
    g_wynik9 = 0; o->sprawdz_date();
    CHECK(!o->ma_dzisiaj() && !k->ma_dzisiaj() && !o->przycisk_dzisiaj()->czy_wlaczona());
    CHECK(std::strstr(o->etykieta_szczegolow()->tekst(), "data systemowa niedostępna"));
    ustaw_czas(2026, 11, 20); o->sprawdz_date(); CHECK(o->ma_dzisiaj());
    // klawiatura trafia do kalendarza (fokus po starcie)
    k->ustaw_wybrana_data({2026, 11, 19});
    CHECK(o->fokus() == k);
    // rozmiary i skale: wszystko w obszarze klienta, siatka i etykiety widoczne
    for (uint32_t s : {100u, 150u, 200u}) {
        bws_zdarzenie ev{}; ev.typ = BWS_ZDARZENIE_EKRAN; ev.kod = s; o->obsluz_zdarzenie(ev);
        for (auto [w, h] : {std::pair{640, 420}, std::pair{720, 560}, std::pair{1280, 720}, std::pair{1920, 1080}}) {
            o->ustaw_geometrie(0, 0, w * int(s) / 100, h * int(s) / 100); o->przelicz_uklad();
            KontekstRysowania c; o->rysuj(c, true);
            const Prostokat kl = o->obszar_klienta(), kr = k->obszar_ekranu();
            CHECK(kr.szerokosc >= 7 * 20 * int(s) / 100 && kr.wysokosc >= 120 * int(s) / 100);
            CHECK(kr.x >= kl.x && kr.prawy() <= kl.prawy() && kr.dolny() <= kl.dolny());
            CHECK(o->tytul()->obszar_ekranu().wysokosc > 0 && o->etykieta_szczegolow()->obszar_ekranu().dolny() <= kl.dolny());
            CHECK(k->prostokat_komorki(41).dolny() <= kr.wysokosc && k->prostokat_komorki(41).prawy() <= kr.szerokosc);
            if (w == KalendarzApp::OknoKalendarza::MIN_SZEROKOSC) {   // najdluzszy tytul miesci sie obok przyciskow
                const int t = CzcionkaUI::zmierz_tekst("Październik 2026", SkalaUI::procent(2 * s));
                CHECK(t <= o->tytul()->obszar().szerokosc && o->tytul()->obszar_ekranu().prawy() <= o->przycisk_poprzedni_rok()->obszar_ekranu().x);
            }
        }
    }
    // motyw jasny/ciemny: zmiana motywu nie gubi stanu
    { bws_zdarzenie ev{}; ev.typ = BWS_ZDARZENIE_MOTYW_ZMIENIONY; o->obsluz_zdarzenie(ev); KontekstRysowania c; o->rysuj(c, true); }
    CHECK(k->dzisiaj() == (DataKalendarza{2026, 11, 20}));
    delete o;
    // start bez daty systemowej
    g_wynik9 = 0;
    auto* p = new KalendarzApp::OknoKalendarza; p->bws = bws_test; p->init();
    CHECK(!p->ma_dzisiaj() && !p->przycisk_dzisiaj()->czy_wlaczona() && std::strstr(p->etykieta_szczegolow()->tekst(), "niedostępna"));
    delete p;
    g_wynik9 = 1;
}

// ---------------------------------------------------------------- 5. polozenie panelu paska zadan
static void polozenie_panelu() {
    struct Ekran { int w, h; uint32_t s; };
    for (Ekran e : {Ekran{800, 600, 100}, Ekran{1024, 768, 100}, Ekran{1280, 720, 100}, Ekran{1366, 768, 125},
                    Ekran{1920, 1080, 150}, Ekran{1920, 1080, 200}, Ekran{640, 480, 200}}) {
        const SkalaUI s = SkalaUI::procent(e.s);
        const int pasek = 40 * s, pasek_y = e.h - pasek;
        const int zw = 150 * s;
        const Prostokat zegar{e.w - zw, pasek_y, zw, pasek};
        const Prostokat r = KalendarzPaska::polozenie(zegar, e.w, pasek_y, KalendarzPaska::Surface::SZEROKOSC * s,
                                                      KalendarzPaska::Surface::WYSOKOSC * s, 4 * s, 6 * s);
        CHECK(r.x >= 4 * s && r.prawy() <= e.w - 4 * s && r.y >= 4 * s);
        CHECK(r.dolny() <= pasek_y - 6 * s);                          // nad paskiem: zegar odsloniety
        CHECK(!r.przecina(zegar));
        if (KalendarzPaska::Surface::SZEROKOSC * s <= e.w - 8 * s) CHECK(r.prawy() == e.w - 4 * s || r.prawy() == zegar.prawy());
    }
}

int main() {
    obliczenia();
    abi_czasu();
    komponent();
    aplikacja();
    polozenie_panelu();
    std::printf("test-kalendarz-host: %u sprawdzen OK\n", checks);
    return 0;
}
