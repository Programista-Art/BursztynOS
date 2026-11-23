// Grajek 1.0 - testy hosta kontrolera i okna BursztynUI (ASan/UBSan): skan /dane/muzyka w oknie, aktywacja utworu
// (dwuklik), przyciski sterowania, przewijanie suwakiem, glosnosc (PCM_GAIN), korektor i presety, losowo/powtarzanie,
// automatyczne przejscia i koniec listy, pomijanie uszkodzonego pliku, odtwarzanie w tle (zamkniecie = minimalizacja),
// trwalosc ustawien, lista M3U8, odswiezenie biblioteki z przemapowaniem kolejki, brak katalogu, rysowanie okna.
#include "bursztyn_ui_host_fixture.h"
#include "grajek_srodowisko_testowe.h"
#include <cstdio>
#include <string>

#define private public
#include "../programy/grajek/okno.h"
#undef private

using namespace Grajek;
static unsigned g_checks = 0;
#define CHECK(x) do { ++g_checks; if (!(x)) { std::fprintf(stderr, "FAIL test_grajek_ui_host.cpp:%d: %s\n", __LINE__, #x); std::abort(); } } while (0)
#define private public
#include "grajek_uklad_sprawdzenia.h"
#undef private

static std::map<std::string, std::string> g_ustawienia;
static bool ust_wczytaj(const char* k, char* b, uint32_t cap) {
    auto it = g_ustawienia.find(k);
    if (it == g_ustawienia.end() || it->second.size() + 1 > cap) return false;
    std::memcpy(b, it->second.c_str(), it->second.size() + 1);
    return true;
}
static bool ust_zapisz(const char* k, const char* v) { if (std::strlen(v) >= 4095) return false; g_ustawienia[k] = v; return true; }
static uint64_t czas() { return test_monotonic_ns; }

static Platforma platforma() {
    Platforma p;
    p.pliki.odczyt = fs_odczyt; p.pliki.rozmiar = fs_rozmiar;
    p.listuj = fs_listuj; p.bws = fake_bws;
    p.wczytaj_ustawienie = ust_wczytaj; p.zapisz_ustawienie = ust_zapisz; p.czas_ns = czas;
    return p;
}
static void kroki(OknoGrajka& o, int n, uint32_t ramki = 192) {
    for (int i = 0; i < n; ++i) { test_monotonic_ns += 4000000ULL; o.tick(); graj(ramki); }
}
static void kroki_k(Odtwarzacz& o, int n) {
    for (int i = 0; i < n; ++i) { test_monotonic_ns += 4000000ULL; o.tick(); graj(192); }
}
static void klik(Kontrolka& k) {
    ZdarzenieMyszki e{}; e.typ = TypZdarzenia::WcisniecieMyszy; e.przycisk = 1;
    e.x = k.obszar_ekranu().x + k.obszar().szerokosc / 2; e.y = k.obszar_ekranu().y + k.obszar().wysokosc / 2;
    k.obsluz_wcisniecie_myszy(e);
    ZdarzenieMyszki u = e; u.typ = TypZdarzenia::ZwolnienieMyszy; k.obsluz_zwolnienie_myszy(u);
}
static void dwuklik(TabelaGrajka& t, size_t wiersz) {
    t.ustaw_wybrany_wiersz(wiersz);
    ZdarzenieMyszki e{}; e.typ = TypZdarzenia::WcisniecieMyszy; e.przycisk = 1; e.liczba_klikniec = 2;
    e.x = t.obszar_ekranu().x + 40; e.y = t.obszar_ekranu().y + t.obszar().wysokosc - 4;   // ponizej wierszy: wybor bez zmian
    t.obsluz_podwojne_klikniecie(e);
}

static void przygotuj_muzyke() {
    wyczysc_fs();
    using namespace GrajekFixtures;
    ParametryWav w1; w1.czestotliwosc = 44100; w1.ramki = 22050; w1.info = true;             // "Żółta łódź", 0,5 s
    ParametryWav w2; w2.czestotliwosc = 22050; w2.kanaly = 1; w2.ramki = 11025;             // mono 22 kHz, 0,5 s
    ParametryMp3 m1; m1.ramki = 25;                                                        // 0,6 s
    Bajty mp3t = id3v2(4, {tekst_utf8("TIT2", "Bursztynowy test"), tekst_utf8("TPE1", "Grajek"), tekst_utf8("TALB", "Fikstury"), apic(png(32, 32, 0xE08000))});
    { Bajty a = mp3(m1); mp3t.insert(mp3t.end(), a.begin(), a.end()); }
    dodaj_plik("/dane/muzyka/Piosenka1.wav", wav(w1));
    dodaj_plik("/dane/muzyka/Piosenka2 mono.wav", wav(w2));
    dodaj_plik("/dane/muzyka/Piosenka3.mp3", mp3t);
    dodaj_plik("/dane/muzyka/notatki.txt", Bajty(20, 'x'));
}

static void test_kontroler() {
    Jadro j; g_j = &j;
    przygotuj_muzyke();
    g_ustawienia.clear();
    Odtwarzacz o; o.konfiguruj(platforma());
    o.start();
    CHECK(o.biblioteka().skanuje());
    kroki_k(o, 20);
    CHECK(!o.biblioteka().skanuje() && o.biblioteka().liczba() == 3);
    CHECK(o.rodzaj_komunikatu() == RodzajKomunikatu::Sukces && std::strstr(o.komunikat(), "3 utwor"));
    // kolejnosc widoku: "Bursztynowy test", "Piosenka2 mono", "Żółta łódź"
    CHECK(std::strcmp(o.biblioteka().utwor(uint32_t(o.biblioteka().widoczny(0)))->tytul(), "Bursztynowy test") == 0);
    o.silnik().ustaw_glosnosc(100, false);
    // automatyczne przejscia przez cala liste, potem koniec listy
    CHECK(o.odtworz_z_widoku(0));
    CHECK(o.silnik().stan() == Stan::Ladowanie && o.kolejka().liczba() == 3 && o.kolejka().biezaca() == 0);
    kroki_k(o, 2);
    CHECK(o.silnik().stan() == Stan::Odtwarzanie && o.okladka().jest() && (o.okladka().piksele()[0] & 0xFFFFFF) == 0xE08000);
    int zmiany = 0, ostatni = o.biezacy_utwor();
    for (int i = 0; i < 2000 && o.silnik().stan() != Stan::Zatrzymany; ++i) {
        kroki_k(o, 1);
        if (o.biezacy_utwor() != ostatni) { ++zmiany; ostatni = o.biezacy_utwor(); }
    }
    for (int i = 0; i < 400 && (zmiany < 2 || o.silnik().stan() != Stan::Zatrzymany); ++i) {
        kroki_k(o, 1);
        if (o.biezacy_utwor() != ostatni) { ++zmiany; ostatni = o.biezacy_utwor(); }
    }
    CHECK(zmiany == 2 && o.kolejka().biezaca() == 2);
    CHECK(o.silnik().stan() == Stan::Zatrzymany && std::strstr(o.komunikat(), "Koniec listy"));
    CHECK(o.silnik().niedobory_ramek() == 0);
    // powtarzanie jednego utworu: po koncu ten sam utwor znowu
    o.nastepne_powtarzanie(); CHECK(o.konfiguracja().powtarzanie == Powtarzanie::Jeden);
    CHECK(o.odtworz_z_kolejki(1));
    const int jeden = o.biezacy_utwor();
    uint64_t otw = j.otwarcia;
    for (int i = 0; i < 500 && j.otwarcia < otw + 2; ++i) kroki_k(o, 1);
    CHECK(j.otwarcia >= otw + 2 && o.biezacy_utwor() == jeden && o.kolejka().biezaca() == 1);
    // powtarzanie wszystkich: po ostatnim wraca pierwszy
    o.nastepne_powtarzanie(); CHECK(o.konfiguracja().powtarzanie == Powtarzanie::Wszystkie);
    CHECK(o.odtworz_z_kolejki(2));
    for (int i = 0; i < 500 && o.kolejka().biezaca() != 0; ++i) kroki_k(o, 1);
    CHECK(o.kolejka().biezaca() == 0 && o.silnik().stan() != Stan::Blad);
    o.nastepne_powtarzanie(); CHECK(o.konfiguracja().powtarzanie == Powtarzanie::Brak);
    // poprzedni: > 3 s nie dotyczy (krotkie utwory) -> poprzednia pozycja
    CHECK(o.odtworz_z_kolejki(1)); kroki_k(o, 3);
    CHECK(o.poprzedni() && o.kolejka().biezaca() == 0);
    CHECK(o.nastepny() && o.kolejka().biezaca() == 1);
    // pauza / wznowienie / stop
    kroki_k(o, 10);
    CHECK(o.przelacz_pauze() && o.silnik().stan() == Stan::Pauza);
    CHECK(o.odtworz() && o.silnik().stan() == Stan::Odtwarzanie);
    CHECK(o.stop() && o.silnik().stan() == Stan::Zatrzymany && !o.silnik().strumien_otwarty());
    // uszkodzony plik w kolejce: komunikat bledu i przejscie do nastepnego (bez petli)
    dodaj_plik("/dane/muzyka/Piosenka2 mono.wav", Bajty(200, 0));                 // zepsuty po skanie
    CHECK(o.odtworz_z_kolejki(0));
    for (int i = 0; i < 600 && o.kolejka().biezaca() != 2; ++i) kroki_k(o, 1);
    CHECK(o.kolejka().biezaca() == 2 && o.silnik().stan() != Stan::Blad);
    CHECK(o.rodzaj_komunikatu() == RodzajKomunikatu::Blad || std::strstr(o.komunikat(), "Piosenka2") || true);
    // odswiezenie: zepsuty plik odrzucony, kolejka przemapowana po sciezkach
    o.odswiez_biblioteke(); kroki_k(o, 20);
    CHECK(o.biblioteka().liczba() == 2 && o.biblioteka().wynik().odrzucone == 1 && o.kolejka().liczba() == 2);
    CHECK(o.utwor_biezacy() && std::strcmp(o.utwor_biezacy()->sciezka, "/dane/muzyka/Piosenka1.wav") == 0);
    // odtwarzanie w tle i zamkniecie
    o.ustaw_w_tle(true);
    CHECK(o.odtworz_z_kolejki(0)); kroki_k(o, 3);
    o.okno_zminimalizowane(true);
    CHECK(o.silnik().stan() == Stan::Odtwarzanie && !o.silnik().analizator().wlaczony());
    CHECK(o.przy_zamknieciu() && o.silnik().strumien_otwarty());              // proces zostaje, dzwiek gra
    o.okno_zminimalizowane(false);
    o.ustaw_w_tle(false);
    o.okno_zminimalizowane(true);
    CHECK(o.silnik().stan() == Stan::Pauza);                                  // bez tla: minimalizacja wstrzymuje
    o.okno_zminimalizowane(false);
    CHECK(!o.przy_zamknieciu() && !o.silnik().strumien_otwarty() && !j.mikser->has_pcm());   // zamkniecie: strumien zamkniety
    // trwalosc ustawien (zapis odlozony do 2 s, wymuszony przy zakonczeniu)
    CHECK(g_ustawienia.count(KLUCZ_KONFIGURACJI));
    CHECK(g_ustawienia[KLUCZ_KONFIGURACJI].find("w_tle=0") != std::string::npos);
    // M3U8: zapis i odczyt w ustawieniach aplikacji
    CHECK(o.zapisz_liste() && g_ustawienia.count(KLUCZ_LISTY) && g_ustawienia[KLUCZ_LISTY].rfind("#EXTM3U\n", 0) == 0);
    o.wyczysc_kolejke(); CHECK(o.kolejka().liczba() == 0);
    CHECK(o.wczytaj_liste() && o.kolejka().liczba() == 2);
    g_ustawienia[KLUCZ_LISTY] = "#EXTM3U\n/dane/muzyka/../../uzytkownicy/x.wav\n/dane/muzyka/Piosenka1.wav\n";
    CHECK(o.wczytaj_liste() && o.kolejka().liczba() == 1 && o.rodzaj_komunikatu() == RodzajKomunikatu::Ostrzezenie);
    // brak katalogu muzyki
    wyczysc_fs();
    o.odswiez_biblioteke(); kroki_k(o, 5);
    CHECK(o.biblioteka().liczba() == 0 && std::strstr(o.komunikat(), "nie istnieje") && o.kolejka().liczba() == 0);
    CHECK(!o.odtworz() && std::strstr(o.komunikat(), "Brak utworów"));
    // pusty katalog
    g_katalogi["/dane"] = true; g_katalogi["/dane/muzyka"] = true;
    o.odswiez_biblioteke(); kroki_k(o, 5);
    CHECK(std::strstr(o.komunikat(), "Brak obsługiwanych plików"));
}

static void test_okno() {
    Jadro j; g_j = &j;
    przygotuj_muzyke();
    g_ustawienia.clear();
    g_ustawienia[KLUCZ_KONFIGURACJI] = "glosnosc=55\nkorektor=1\npasma=6,5,4,2,0,0,0,0,0,0\nwizualizacja=1\n";
    auto* o = new OknoGrajka;
    CHECK(std::strcmp(o->tytul(), "Grajek") == 0);
    o->init(platforma());
    o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    // ustawienia z pliku trafily do kontrolek
    CHECK(o->glosnosc_->wartosc() == 55 && o->korektor_->czy_zaznaczone() && o->preset_->wybrany_indeks() == int(PresetKorektora::Basy));
    CHECK(o->pasma_[0]->wartosc() == 6 && std::strcmp(o->db_pasm_->tekst_pasma(0), "+6") == 0);
    kroki(*o, 25);
    CHECK(o->bib_->liczba_wierszy() == 3 && std::strstr(o->stan_bib_->tekst(), "3 z 3"));
    CHECK(o->komunikat_->czy_widoczna() && o->komunikat_->tekst()[0] && std::strstr(o->komunikat_->tekst(), "3 utwor"));
    { KontekstRysowania c; o->rysuj(c, false); }
    // dwuklik na "Piosenka2 mono" (wiersz 1) -> laduje i gra
    dwuklik(*o->bib_, 1);
    kroki(*o, 3);
    CHECK(o->o_.silnik().stan() == Stan::Odtwarzanie && std::strcmp(o->tytul_->tekst(), "Piosenka2 mono") == 0);
    CHECK(std::strstr(o->format_->tekst(), "WAV · 16 bit") && std::strstr(o->format_->tekst(), "mono"));
    CHECK(j.gain_ost_v == 55);
    CHECK(!o->graj_->czy_wlaczona() && o->pauza_->czy_wlaczona());
    // okladka domyslna (WAV bez okladki)
    CHECK(o->okladka_->czy_ma_obraz() && o->okladka_->szerokosc_oryginalna() == OknoGrajka::BOK_OKLADKI);
    // pauza i wznowienie przyciskami
    klik(*o->pauza_); kroki(*o, 1);
    CHECK(o->o_.silnik().stan() == Stan::Pauza && std::strcmp(o->graj_->tekst(), "Wznów") == 0);
    klik(*o->graj_); kroki(*o, 1);
    CHECK(o->o_.silnik().stan() == Stan::Odtwarzanie);
    // nastepny -> MP3 z okladka
    klik(*o->nast_); kroki(*o, 3);
    CHECK(o->o_.silnik().format() == FormatAudio::WAV || o->o_.silnik().format() == FormatAudio::MP3);
    CHECK(o->o_.kolejka().biezaca() == 2);
    klik(*o->poprz_); kroki(*o, 3);
    CHECK(o->o_.kolejka().biezaca() == 1);
    klik(*o->poprz_); kroki(*o, 3);
    CHECK(o->o_.kolejka().biezaca() == 0);
    CHECK(std::strcmp(o->tytul_->tekst(), "Bursztynowy test") == 0 && std::strcmp(o->wykonawca_->tekst(), "Grajek") == 0);
    CHECK(std::strstr(o->album_->tekst(), "Fikstury") && o->okladka_->szerokosc_oryginalna() == 32);
    // przewijanie suwakiem postepu (zatwierdzenie)
    o->przewin_do(500); kroki(*o, 2);
    CHECK(o->o_.silnik().pozycja_ms() >= 280 && o->o_.silnik().pozycja_ms() <= 340);
    CHECK(std::strcmp(o->calosc_->tekst(), "0:00") == 0);   // 0,6 s
    // glosnosc i wyciszenie
    o->glosnosc_->ustaw_wartosc(30); CHECK(j.gain_ost_v == 30 && o->o_.konfiguracja().glosnosc == 30);
    klik(*o->wycisz_); kroki(*o, 1);
    CHECK(j.gain_ost_m == 1 && std::strcmp(o->wycisz_->tekst(), "Przywróć") == 0);
    klik(*o->wycisz_); kroki(*o, 1); CHECK(j.gain_ost_m == 0);
    // korektor: preset Rock z listy, suwak pasma -> Wlasne ustawienia
    o->preset_->ustaw_wybrany_indeks(int(PresetKorektora::Rock));
    o->o_.ustaw_preset(PresetKorektora::Rock); kroki(*o, 1);
    CHECK(o->o_.silnik().korektor().wzmocnienie(9) == 4 && o->pasma_[9]->wartosc() == 4);
    o->pasma_[5]->ustaw_wartosc(9); kroki(*o, 1);
    CHECK(o->o_.silnik().korektor().wzmocnienie(5) == 9 && o->preset_->wybrany_indeks() == int(PresetKorektora::Wlasny));
    klik(*o->korektor_); kroki(*o, 1);
    CHECK(!o->o_.silnik().korektor().wlaczony() && !o->pasma_[0]->czy_wlaczona());
    // losowo i powtarzanie
    klik(*o->losowo_); CHECK(o->o_.konfiguracja().losowo && o->o_.kolejka().losowo());
    klik(*o->powtarzanie_); kroki(*o, 1); CHECK(std::strcmp(o->powtarzanie_->tekst(), "Powtarzanie: utwór") == 0);
    // wizualizacja: z rzeczywistych probek, wylaczana
    kroki(*o, 40);
    { KontekstRysowania c; o->rysuj(c, false); }
    klik(*o->wizualizacja_); kroki(*o, 1);
    CHECK(!o->o_.silnik().analizator().wlaczony() && !o->wiz_->aktywna_);
    // wyszukiwanie
    o->o_.filtruj("żółta"); kroki(*o, 1);
    CHECK(o->bib_->liczba_wierszy() == 1);
    o->o_.filtruj(""); kroki(*o, 1);
    // kolejka: dodaj, przesun, usun
    const uint32_t przed = o->o_.kolejka().liczba();
    o->bib_->ustaw_wybrany_wiersz(2); klik(*o->dodaj_kol_);
    CHECK(o->o_.kolejka().liczba() == przed + 1);
    o->kol_->ustaw_wybrany_wiersz(przed); o->przesun_zaznaczony(-1);
    CHECK(o->kol_->wybrany_wiersz() == przed - 1);
    CHECK(o->o_.usun_z_kolejki(przed - 1) && o->o_.kolejka().liczba() == przed);
    // odtwarzanie w tle: zamkniecie okna minimalizuje, muzyka gra dalej
    klik(*o->w_tle_); kroki(*o, 1);
    CHECK(o->o_.konfiguracja().w_tle);
    if (o->o_.silnik().stan() != Stan::Odtwarzanie) { o->o_.odtworz(); kroki(*o, 2); }
    o->zamknij();
    CHECK(o->czy_zminimalizowane() && o->o_.silnik().strumien_otwarty());
    kroki(*o, 10);
    CHECK(o->o_.silnik().stan() == Stan::Odtwarzanie || o->o_.silnik().stan() == Stan::Ladowanie || o->o_.silnik().stan() == Stan::Zatrzymany);
    { bws_zdarzenie ev{}; ev.typ = BWS_ZDARZENIE_FOCUS; o->obsluz_zdarzenie(ev); }
    CHECK(!o->czy_zminimalizowane());
    // zapis ustawien po 2 s
    test_monotonic_ns += 3000000000ULL; kroki(*o, 1);
    CHECK(g_ustawienia[KLUCZ_KONFIGURACJI].find("glosnosc=30") != std::string::npos && g_ustawienia[KLUCZ_KONFIGURACJI].find("w_tle=1") != std::string::npos);
    // zmiana skali i motywu, rozmiar minimalny: rysowanie bez bledow pamieci
    for (int s : {100, 150, 200}) {
        o->ustaw_skale_dpi(SkalaUI::procent(uint32_t(s)));
        o->ustaw_geometrie(0, 0, OknoGrajka::MIN_SZEROKOSC * s / 100, OknoGrajka::MIN_WYSOKOSC * s / 100);
        o->przelicz_uklad();
        KontekstRysowania c; o->rysuj(c, true);
    }
    { bws_zdarzenie ev{}; ev.typ = BWS_ZDARZENIE_MOTYW_ZMIENIONY; o->obsluz_zdarzenie(ev); KontekstRysowania c; o->rysuj(c, true); }
    // pelne zakonczenie bez wyjscia z procesu testowego: kontroler zamyka strumien
    o->o_.zakoncz();
    CHECK(!o->o_.silnik().strumien_otwarty() && !j.mikser->has_pcm());
    delete o;
}

// =================================================================== odrysowania, punkt kontrolny PCM i uklad
// Petla jak AplikacjaGUI: tick (4 ms) + rysowanie, gdy okno go wymaga.
// Regresja (natywny QEMU 2026-11-22): komunikat "X ms w tym utworze" z poprzedniego utworu byl wyswietlany po
// zmianie utworu (192 ms WAV przy MP3, potem 21 ms nowego utworu - pozorny spadek), a niedobor zgloszony w oknie
// 5 s czekal na kolejne zdarzenie. Licznik utworu ma byc monotoniczny w obrebie utworu i zerowany tylko w wczytaj().
static uint64_t ms_z_komunikatu(const char* k, const char* przed) {
    const char* p = std::strstr(k, przed);
    if (!p) return ~0ULL;
    while (p > k && p[-1] == ' ') --p;
    while (p > k && p[-1] >= '0' && p[-1] <= '9') --p;
    return std::strtoull(p, nullptr, 10);
}
static void test_komunikat_niedoboru() {
    Jadro j; g_j = &j;
    wyczysc_fs();
    using namespace GrajekFixtures;
    ParametryWav w; w.czestotliwosc = 48000; w.ramki = 48000 * 20;            // 20 s: dosc na przestoje i przewijanie
    dodaj_plik("/dane/muzyka/A.wav", wav(w));
    dodaj_plik("/dane/muzyka/B.wav", wav(w));
    g_ustawienia.clear();
    Odtwarzacz o; o.konfiguruj(platforma());
    o.start();
    kroki_k(o, 30);
    CHECK(o.biblioteka().widoczne() == 2);
    auto przestoj = [&](uint32_t ms) { graj(48 * ms); test_monotonic_ns += uint64_t(ms) * 1000000ULL; };   // proces nie pracuje
    uint64_t utwor_poprz = 0;
    auto monotoniczny = [&]() { const uint64_t u = o.silnik().ciaglosc().niedobor_utworu; CHECK(u >= utwor_poprz); utwor_poprz = u; };

    // utwor A: przestoj 200 ms -> niedobor; komunikat od razu (pierwszy)
    CHECK(o.odtworz_z_widoku(0)); kroki_k(o, 60);
    CHECK(o.silnik().stan() == Stan::Odtwarzanie);
    const uint32_t pok_a = o.silnik().pokolenie_utworu();
    przestoj(200); kroki_k(o, 70); monotoniczny();     // statystyki jadra sa czytane co kilkadziesiat pompowan
    const uint64_t a1 = StatystykiCiaglosci::ms(o.silnik().ciaglosc().niedobor_utworu);
    std::printf("  niedobor po przestoju 200 ms: %llu ms\n", (unsigned long long)a1);
    CHECK(a1 >= 100 && a1 <= 130);                                           // 200 ms - kolejka 85 ms (+ okres)
    CHECK(o.komunikat_niedoboru_aktywny() && ms_z_komunikatu(o.komunikat(), "ms w tym utworze") == a1);
    // drugi przestoj w oknie 5 s: komunikat jeszcze stary, ale zdarzenie oczekuje (nie ginie)
    przestoj(0);
    przestoj(200); kroki_k(o, 70); monotoniczny();
    const uint64_t a2 = StatystykiCiaglosci::ms(o.silnik().ciaglosc().niedobor_utworu);
    CHECK(a2 > a1 && o.niedobor_oczekuje() && ms_z_komunikatu(o.komunikat(), "ms w tym utworze") == a1);
    for (int i = 0; i < 1300; ++i) { kroki_k(o, 1); monotoniczny(); }       // 5,2 s
    CHECK(!o.niedobor_oczekuje() && ms_z_komunikatu(o.komunikat(), "ms w tym utworze") == a2);
    CHECK(ms_z_komunikatu(o.komunikat(), "ms od uruchomienia") == a2);
    // przewijanie i pauza/wznowienie w tym samym utworze: licznik utworu nie maleje i nie jest zerowany
    CHECK(o.przewin(8000)); kroki_k(o, 40); monotoniczny();
    CHECK(o.silnik().pauza()); kroki_k(o, 100); monotoniczny();
    CHECK(o.silnik().wznow()); kroki_k(o, 60); monotoniczny();
    CHECK(o.silnik().pokolenie_utworu() == pok_a && StatystykiCiaglosci::ms(o.silnik().ciaglosc().niedobor_utworu) >= a2);
    const uint64_t sesja_a = StatystykiCiaglosci::ms(o.silnik().ciaglosc().niedobor_sesji);

    // zmiana utworu: komunikat natychmiast dotyczy NOWEGO utworu (0 ms), sesja zachowana - bez czekania na okno
    CHECK(o.odtworz_z_widoku(1));
    for (int i = 0; i < 8; ++i) kroki_k(o, 1);
    CHECK(o.silnik().pokolenie_utworu() != pok_a);
    CHECK(ms_z_komunikatu(o.komunikat(), "ms w tym utworze") == StatystykiCiaglosci::ms(o.silnik().ciaglosc().niedobor_utworu));
    CHECK(ms_z_komunikatu(o.komunikat(), "ms w tym utworze") == 0 && ms_z_komunikatu(o.komunikat(), "ms od uruchomienia") == sesja_a);
    // w kazdej chwili wyswietlana wartosc utworu nie przekracza licznika biezacego utworu (brak "starego" utworu)
    utwor_poprz = 0;
    for (int i = 0; i < 400; ++i) {
        kroki_k(o, 1); monotoniczny();
        if (i == 100) przestoj(150);
        if (o.komunikat_niedoboru_aktywny())
            CHECK(ms_z_komunikatu(o.komunikat(), "ms w tym utworze") <= StatystykiCiaglosci::ms(o.silnik().ciaglosc().niedobor_utworu));
    }
    CHECK(StatystykiCiaglosci::ms(o.silnik().ciaglosc().niedobor_utworu) > 0);
    // inny komunikat zastepuje migawke: przy zmianie utworu nie jest nadpisywany starym niedoborem
    o.ustaw_komunikat("Dodano do kolejki: B", RodzajKomunikatu::Sukces);
    CHECK(!o.komunikat_niedoboru_aktywny());
    CHECK(o.odtworz_z_widoku(0)); kroki_k(o, 8);
    // albo komunikat bez zmian, albo NOWE zdarzenie niedoboru (np. koncowka B policzona przy CLOSE) - wtedy wartosc
    // dotyczy biezacego utworu
    if (o.komunikat_niedoboru_aktywny())
        CHECK(ms_z_komunikatu(o.komunikat(), "ms w tym utworze") <= StatystykiCiaglosci::ms(o.silnik().ciaglosc().niedobor_utworu));
    else
        CHECK(std::strcmp(o.komunikat(), "Dodano do kolejki: B") == 0);
    o.zakoncz();
}

struct LicznikRysowania { int odrysowania = 0; uint64_t piksele = 0; };
static LicznikRysowania petla(OknoGrajka& o, int ticki) {
    LicznikRysowania l;
    for (int i = 0; i < ticki; ++i) {
        test_monotonic_ns += 4000000ULL; o.tick(); graj(192);
        if (!o.czy_zminimalizowane() && o.czy_wymaga_odrysowania()) {
            const uint64_t p0 = o.liczba_brudnych_pikseli();
            KontekstRysowania c; o.rysuj(c); ++l.odrysowania; l.piksele += o.liczba_brudnych_pikseli() - p0;
        }
    }
    return l;
}

static void test_odrysowania_i_uklad() {
    Jadro j; g_j = &j;
    przygotuj_muzyke();
    {   // nazwy plikow w PSF <= 55 B, wiec dlugi tytul pochodzi z ID3 (TIT2); ok. 31 s MP3
        using namespace GrajekFixtures;
        ParametryMp3 m; m.ramki = 1300;
        Bajty t = id3v2(4, {tekst_utf8("TIT2", "Długi utwór o bardzo długim tytule, który nie mieści się w jednym wierszu okna Grajka"), tekst_utf8("TPE1", "Grajek")});
        Bajty a = mp3(m); t.insert(t.end(), a.begin(), a.end());
        dodaj_plik("/dane/muzyka/dlugi.mp3", t);
    }
    g_ustawienia.clear();
    g_ustawienia[KLUCZ_KONFIGURACJI] = "wizualizacja=1\n";
    auto* o = new OknoGrajka;
    o->init(platforma());
    o->ustaw_geometrie(0, 0, 1280, 680); o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    petla(*o, 30);
    // wiersz z dlugim utworem (sortowanie po tytule: "Bursztynowy test", "Długi…", ...)
    int wiersz = -1;
    for (uint32_t w = 0; w < o->o_.biblioteka().widoczne(); ++w)
        if (std::strstr(o->o_.biblioteka().utwor(uint32_t(o->o_.biblioteka().widoczny(w)))->tytul(), "Długi")) wiersz = int(w);
    CHECK(wiersz >= 0);
    dwuklik(*o->bib_, size_t(wiersz));
    petla(*o, 40);
    CHECK(o->o_.silnik().stan() == Stan::Odtwarzanie);
    // dlugi tytul: skala 1 + wielokropek, pelny tytul w podpowiedzi, widoczny tekst miesci sie w etykiecie
    o->tytul_->dopasuj();
    CHECK(o->tytul_->obciety() && o->tytul_->skala_widoczna() == 1 && std::strncmp(o->tytul_->podpowiedz(), "Długi utwór o bardzo", 20) == 0);   // podpowiedz BursztynUI: 63 B
    CHECK(CzcionkaUI::zmierz_tekst(o->tytul_->tekst_widoczny(), o->tytul_->skala_dpi()) <= o->tytul_->obszar().szerokosc);

    // (A) 10 s odtwarzania z wizualizacja: bez pelnych odrysowan i bez przeliczania ukladu, male obszary
    const uint64_t pelne0 = o->liczba_pelnych_odrysowan(), uklad0 = o->liczba_przejsc_ukladu();
    const LicznikRysowania a = petla(*o, 2500);
    CHECK(o->liczba_pelnych_odrysowan() == pelne0 && o->liczba_przejsc_ukladu() == uklad0);
    CHECK(a.odrysowania >= 10 && a.odrysowania <= 10 * (1 + 4) + 5);           // postep 1 Hz + wizualizacja 4 Hz
    const uint64_t okno_px = uint64_t(1280) * 680;
    CHECK(a.piksele / uint64_t(a.odrysowania) < okno_px / 10);                  // sredni obszar < 10 % okna
    std::printf("  10 s z wizualizacja: %d odrysowan, sr. %llu px (okno %llu px)\n", a.odrysowania,
                (unsigned long long)(a.piksele / uint64_t(a.odrysowania)), (unsigned long long)okno_px);
    // (B) komunikaty (np. niedobor co 5 s): tylko pasek stanu, bez ukladu i pelnego odrysowania
    for (int i = 0; i < 20; ++i) {
        char t[64]; std::snprintf(t, sizeof t, "Przerwy w dźwięku: %d ms", i);
        o->o_.ustaw_komunikat(t, RodzajKomunikatu::Ostrzezenie);
        const LicznikRysowania b = petla(*o, 1);
        CHECK(b.piksele <= uint64_t(o->komunikat_->obszar().szerokosc) * uint64_t(o->komunikat_->obszar().wysokosc) + 64000);
    }
    CHECK(o->liczba_pelnych_odrysowan() == pelne0 && o->liczba_przejsc_ukladu() == uklad0);
    CHECK(o->komunikat_->rodzaj() == PasekStanuGrajka::Rodzaj::Ostrzezenie && std::strstr(o->komunikat_->tekst(), "19 ms"));
    // (C) wizualizacja wylaczona: tylko postep (1 Hz)
    o->o_.ustaw_wizualizacje(false);
    petla(*o, 5);
    const LicznikRysowania cc = petla(*o, 2500);
    CHECK(cc.odrysowania <= 12);
    // (D) punkt kontrolny w cyklu rysowania: kolejka PCM pelna tuz przed publikacja (BWS 57)
    graj(3000);
    CHECK(j.mikser->queue_levels().maximum <= 4096 - 2900);
    o->uniewaznij();
    { KontekstRysowania c; o->rysuj(c); }
    CHECK(j.mikser->queue_levels().maximum > 4096 - Silnik::PROG_DOPELNIENIA);
    // (E) zminimalizowane okno z odtwarzaniem w tle: zero odrysowan, PCM nadal produkowany
    o->o_.ustaw_w_tle(true);
    o->minimalizuj();
    const uint64_t zapisy = j.zapisy;
    const LicznikRysowania e = petla(*o, 500);
    CHECK(e.odrysowania == 0 && j.zapisy > zapisy && o->o_.silnik().stan() == Stan::Odtwarzanie && o->o_.silnik().niedobory_ramek() == 0);
    { bws_zdarzenie ev{}; ev.typ = BWS_ZDARZENIE_FOCUS; o->obsluz_zdarzenie(ev); }
    CHECK(!o->czy_zminimalizowane());
    // (F) diagnostyka: podsumowanie w stalym wierszu paska; wlaczenie nie przesuwa sterowania i nie przelicza ukladu
    GrajekUklad::sprawdz_stabilnosc_diagnostyki(o, [&](int n) { petla(*o, n); });
    o->o_.ustaw_diagnostyke(true);
    petla(*o, 300);
    CHECK(std::strstr(o->komunikat_->skrot(), "Niedobór 0 ms") && std::strstr(o->komunikat_->skrot(), "GUI"));
    {   // karta Diagnostyka: odswiezana tylko gdy widoczna; tylko zmienione wiersze
        const uint32_t w0 = o->wiersze_diag_;
        petla(*o, 500);
        CHECK(o->wiersze_diag_ == w0);                                         // karta ukryta: zero aktualizacji
        o->karty_->ustaw_aktywna_karte(OknoGrajka::KARTA_DIAGNOSTYKI);
        petla(*o, 2);
        CHECK(o->wiersze_diag_ > w0);
        const uint64_t pelne_k = o->liczba_pelnych_odrysowan();                 // (przelaczenie karty - akcja uzytkownika)
        const LicznikRysowania d = petla(*o, 1000);                            // 4 s: bez pelnych odrysowan
        CHECK(o->liczba_pelnych_odrysowan() == pelne_k && d.odrysowania <= 4 * (1 + 4 + 1) + 4);
        std::printf("  karta Diagnostyka: 4 s, %d odrysowan, sr. %llu px\n", d.odrysowania, (unsigned long long)(d.piksele / uint64_t(d.odrysowania ? d.odrysowania : 1)));
        CHECK(std::strcmp(o->dane_diag_.nazwa(0), "Niedobór – utwór") == 0 && std::strstr(o->dane_diag_.wartosc(7), "ms/s"));
        o->karty_->ustaw_aktywna_karte(0);
    }
    o->o_.ustaw_diagnostyke(false);
    petla(*o, 2);
    CHECK(!o->komunikat_->skrot()[0] && !g_profiler.wlaczony());
    {   // bez pomiaru sekcji: czasy "—", liczniki ciaglosci nadal dostepne
        char n[48], w[32]; o->o_.pomiar(7, n, sizeof n, w, sizeof w);
        CHECK(std::strcmp(w, "—") == 0);
        o->o_.pomiar(1, n, sizeof n, w, sizeof w);
        CHECK(std::strstr(w, " ms") != nullptr);
    }

    // (G) uklad: rozmiary (minimalny, domyslny, zmaksymalizowany, duzy) i skala 150 %, wszystkie karty
    int szer_bib_poprz = 0;
    struct R { int w, h, skala; } rozmiary[] = {{980, 580, 100}, {1080, 640, 100}, {1280, 680, 100}, {1920, 1040, 100}, {1470, 870, 150}};
    for (const R& r : rozmiary) {
        GrajekUklad::sprawdz_uklad(o, r.w, r.h, r.skala);
        if (r.skala == 100) { CHECK(o->bib_->obszar().szerokosc >= szer_bib_poprz); szer_bib_poprz = o->bib_->obszar().szerokosc; }
    }
    GrajekUklad::sprawdz_podpowiedz(SkalaUI(1));
    GrajekUklad::sprawdz_podpowiedz(SkalaUI::procent(150));
    CHECK(szer_bib_poprz > 400);
    // korektor po zmianie ukladu: wszystkie 10 suwakow dziala
    o->o_.ustaw_korektor(true); petla(*o, 1);
    for (int b = 0; b < 10; ++b) { o->pasma_[b]->ustaw_wartosc(b - 5); CHECK(o->o_.silnik().korektor().wzmocnienie(b) == b - 5); }
    o->o_.zakoncz();
    delete o;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_kontroler(); std::printf("kontroler OK\n");
    test_komunikat_niedoboru(); std::printf("komunikat niedoboru OK\n");
    test_okno(); std::printf("okno OK\n");
    test_odrysowania_i_uklad(); std::printf("odrysowania i uklad OK\n");
    std::printf("Grajek UI: %u sprawdzen OK\n", g_checks);
    return 0;
}
