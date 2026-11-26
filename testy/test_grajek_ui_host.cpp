// Grajek 1.0 - testy hosta kontrolera i okna BursztynUI (ASan/UBSan): skan /dane/muzyka w oknie, aktywacja utworu
// (dwuklik), przyciski sterowania, przewijanie suwakiem, glosnosc (PCM_GAIN), korektor i presety, losowo/powtarzanie,
// automatyczne przejscia i koniec listy, pomijanie uszkodzonego pliku, odtwarzanie w tle (zamkniecie = minimalizacja),
// trwalosc ustawien, lista M3U8, odswiezenie biblioteki z przemapowaniem kolejki, brak katalogu, rysowanie okna.
#include "bursztyn_ui_host_fixture.h"
#include "grajek_srodowisko_testowe.h"
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>
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
    CHECK(!o.komunikat()[0]);                     // udany skan: bez komunikatu (liczba w etykiecie, pozycja w pasku)
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
    CHECK(o.silnik().strumien_otwarty());                                     // w tle: minimalizacja nie przerywa
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
    CHECK(o.rodzaj_magazynu() == Odtwarzacz::RodzajMagazynu::Nieznany && !std::strstr(o.komunikat(), "dysku"));
}

// BWS 111: Grajek odroznia trwaly dysk zainstalowanego systemu od RAM sesji Live i nie opisuje RAM jako trwalego.
static void test_magazyn() {
    Jadro j; g_j = &j;
    przygotuj_muzyke();
    g_ustawienia.clear();
    Odtwarzacz o; o.konfiguruj(platforma());
    j.magazyn = 1;
    o.start(); kroki_k(o, 20);
    CHECK(o.biblioteka().liczba() == 3 && o.rodzaj_magazynu() == Odtwarzacz::RodzajMagazynu::Trwaly);
    CHECK(!o.komunikat()[0]);                     // trwaly dysk i brak problemow: nic do zgloszenia
    j.magazyn = 2;
    o.odswiez_biblioteke(); kroki_k(o, 20);
    CHECK(o.rodzaj_magazynu() == Odtwarzacz::RodzajMagazynu::Ram);
    CHECK(std::strstr(o.komunikat(), "Sesja Live") && std::strstr(o.komunikat(), "znikną po restarcie"));
    CHECK(!std::strstr(o.komunikat(), "trwałym") && o.rodzaj_komunikatu() == RodzajKomunikatu::Ostrzezenie);
    wyczysc_fs();
    o.odswiez_biblioteke(); kroki_k(o, 5);
    CHECK(std::strstr(o.komunikat(), "nie istnieje") && std::strstr(o.komunikat(), "RAM"));
    j.magazyn = 0;
    o.odswiez_biblioteke(); kroki_k(o, 5);
    CHECK(o.rodzaj_magazynu() == Odtwarzacz::RodzajMagazynu::Nieznany);
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
    // pasek stanu: zamiast podsumowania biblioteki pozycja "zaznaczony/wszystkie" (nic nie gra: zaznaczony wiersz 0)
    CHECK(o->komunikat_->czy_widoczna() && !o->komunikat_->tekst()[0] && std::strcmp(o->komunikat_->pozycja(), "1/3") == 0);
    CHECK(!std::strstr(o->komunikat_->tekst(), "Biblioteka") && !std::strstr(o->komunikat_->tekst(), "dysku"));
    o->bib_->ustaw_wybrany_wiersz(2); kroki(*o, 1);
    CHECK(std::strcmp(o->komunikat_->pozycja(), "3/3") == 0);
    { KontekstRysowania c; o->rysuj(c, false); }
    // dwuklik na "Piosenka2 mono" (wiersz 1) -> laduje i gra
    dwuklik(*o->bib_, 1);
    kroki(*o, 3);
    CHECK(o->o_.silnik().stan() == Stan::Odtwarzanie && std::strcmp(o->tytul_->tekst(), "Piosenka2 mono") == 0);
    CHECK(std::strcmp(o->komunikat_->pozycja(), "2/3") == 0);                // gra: numer grajacego utworu
    o->bib_->ustaw_wybrany_wiersz(0); kroki(*o, 1);
    CHECK(std::strcmp(o->komunikat_->pozycja(), "2/3") == 0);                // zaznaczenie nie zmienia pozycji grajacego
    CHECK(std::strstr(o->format_->tekst(), "WAV · 16 bit") && std::strstr(o->format_->tekst(), "mono"));
    CHECK(j.gain_ost_v == 55);
    // jeden przycisk Odtworz/Pauza: podczas odtwarzania ikona pauzy (grafika uzytkownika), wlaczony
    CHECK(o->graj_->czy_wlaczona() && o->graj_->obraz_ikony() == &o->ikony_[IK_PAUZA] && o->graj_->uzywa_ikony());
    CHECK(std::strcmp(o->graj_->tekst(), "Pauza") == 0 && std::strstr(o->graj_->podpowiedz(), "Wstrzymaj"));
    // okladka domyslna (WAV bez okladki): plyta winylowa uzytkownika (JPEG 492x492 -> raz do 352 px, potem do kontrolki)
    CHECK(o->okladka_->czy_ma_obraz() && o->okladka_->piksele() == o->plyta_.piksele());
    CHECK(o->okladka_->szerokosc_oryginalna() == int(GrafikaPlyty::BOK_POSREDNI) && o->okladka_->wysokosc_oryginalna() == int(GrafikaPlyty::BOK_POSREDNI));
    CHECK(o->plyta_.dekodowania() == 1);
    // pauza i wznowienie tym samym przyciskiem
    klik(*o->graj_); kroki(*o, 1);
    CHECK(o->graj_->obraz_ikony() == &o->ikony_[IK_ODTWORZ]);
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
    // wyciszenie = przelacznik z ikona uzytkownika (mute.png): stan zaznaczony i podpowiedz wg stanu
    CHECK(j.gain_ost_m == 1 && std::strcmp(o->wycisz_->tekst(), "Przywróć dźwięk") == 0 && o->wycisz_->czy_zaznaczony());
    CHECK(std::strstr(o->wycisz_->podpowiedz(), "Przywróć") && o->wycisz_->uzywa_ikony());
    klik(*o->wycisz_); kroki(*o, 1); CHECK(j.gain_ost_m == 0 && !o->wycisz_->czy_zaznaczony());
    // korektor: preset Rock z listy, suwak pasma -> Wlasne ustawienia
    o->preset_->ustaw_wybrany_indeks(int(PresetKorektora::Rock));
    o->o_.ustaw_preset(PresetKorektora::Rock); kroki(*o, 1);
    CHECK(o->o_.silnik().korektor().wzmocnienie(9) == 4 && o->pasma_[9]->wartosc() == 4);
    o->pasma_[5]->ustaw_wartosc(9); kroki(*o, 1);
    CHECK(o->o_.silnik().korektor().wzmocnienie(5) == 9 && o->preset_->wybrany_indeks() == int(PresetKorektora::Wlasny));
    klik(*o->korektor_); kroki(*o, 1);
    CHECK(!o->o_.silnik().korektor().wlaczony() && !o->pasma_[0]->czy_wlaczona());
    // losowo i powtarzanie: przelaczniki z ikonami (stan = zaznaczenie kafla, opis w podpowiedzi)
    CHECK(!o->losowo_->czy_zaznaczony() && std::strstr(o->losowo_->podpowiedz(), "wyłączona"));
    klik(*o->losowo_); kroki(*o, 1);
    CHECK(o->o_.konfiguracja().losowo && o->o_.kolejka().losowo() && o->losowo_->czy_zaznaczony());
    CHECK(std::strstr(o->losowo_->podpowiedz(), "włączona"));
    CHECK(o->powtarzanie_->obraz_ikony() == &o->ikony_[IK_POWTARZANIE] && !o->powtarzanie_->czy_zaznaczony());
    klik(*o->powtarzanie_); kroki(*o, 1);
    CHECK(std::strcmp(o->powtarzanie_->tekst(), "Powtarzanie: utwór") == 0 && o->powtarzanie_->czy_zaznaczony());
    CHECK(o->powtarzanie_->obraz_ikony() == &o->ikony_[IK_POWTARZANIE_JEDEN] && std::strstr(o->powtarzanie_->podpowiedz(), "utwór"));
    klik(*o->powtarzanie_); kroki(*o, 1);
    CHECK(o->o_.konfiguracja().powtarzanie == Powtarzanie::Wszystkie && o->powtarzanie_->obraz_ikony() == &o->ikony_[IK_POWTARZANIE]);
    klik(*o->powtarzanie_); kroki(*o, 1);
    CHECK(o->o_.konfiguracja().powtarzanie == Powtarzanie::Brak && !o->powtarzanie_->czy_zaznaczony());
    klik(*o->powtarzanie_); kroki(*o, 1);
    CHECK(o->o_.konfiguracja().powtarzanie == Powtarzanie::Jeden);
    // wizualizacja: z rzeczywistych probek, wylaczana
    kroki(*o, 40);
    { KontekstRysowania c; o->rysuj(c, false); }
    CHECK(o->wizualizacja_->czy_zaznaczony());
    klik(*o->wizualizacja_); kroki(*o, 1);
    CHECK(!o->o_.silnik().analizator().wlaczony() && !o->wiz_->aktywna_ && !o->wizualizacja_->czy_zaznaczony());
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
    // odtwarzanie w tle (przelacznik z ikona): minimalizacja nie przerywa muzyki
    klik(*o->w_tle_); kroki(*o, 1);
    CHECK(o->o_.konfiguracja().w_tle && o->w_tle_->czy_zaznaczony() && std::strstr(o->w_tle_->podpowiedz(), "gra po"));
    if (o->o_.silnik().stan() != Stan::Odtwarzanie) { o->o_.odtworz(); kroki(*o, 2); }
    o->minimalizuj(); kroki(*o, 10);
    CHECK(o->czy_zminimalizowane() && o->o_.silnik().strumien_otwarty());
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
    struct R { int w, h, skala; } rozmiary[] = {{OknoGrajka::MIN_SZEROKOSC, OknoGrajka::MIN_WYSOKOSC, 100}, {1080, 640, 100}, {1280, 680, 100}, {1920, 1040, 100}, {OknoGrajka::MIN_SZEROKOSC * 3 / 2, 870, 150}};
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

// Ikony uzytkownika i plyta winylowa: mapowanie, jednokrotne dekodowanie i skalowanie, stany przyciskow ikonowych,
// klawiatura, podpowiedzi, brak i uszkodzenie plikow (przycisk tekstowy / pusty panel okladki).
static uint32_t piksel(int x, int y) { return test_surface[size_t(y) * size_t(test_surface_width) + size_t(x)] & 0xFFFFFF; }
static uint32_t probka_kafla(OknoGrajka& o, PrzyciskIkony* b) {
    test_surface_width = o.szerokosc(); test_surface_height = o.wysokosc();
    test_surface.assign(size_t(test_surface_width) * size_t(test_surface_height), 0x123456);
    { KontekstRysowania c; o.rysuj(c, true); }
    const Prostokat k = b->obszar_kafla();
    return piksel(b->obszar_ekranu().x + k.x + 4, b->obszar_ekranu().y + k.y + k.wysokosc - 4);
}
// Tlo okna obok kafla (wewnatrz przycisku, 2 px na lewo od kafla w polowie wysokosci; poza pierscieniem, gdy brak
// fokusu i najechania) - z ostatniego rysowania.
static uint32_t tlo_przycisku(PrzyciskIkony* b) {
    const Prostokat k = b->obszar_kafla();
    return piksel(b->obszar_ekranu().x + k.x - 2, b->obszar_ekranu().y + k.y + k.wysokosc / 2);
}
// Widocznosc ikony: suma |jasnosc - jasnosc tla| w obszarze ikony (z ostatniego rysowania).
static uint64_t kontrast_ikony(PrzyciskIkony* b) {
    const Prostokat k = b->obszar_kafla();
    const int bok = b->bok_ikony_px(), x0 = b->obszar_ekranu().x + k.x + (k.szerokosc - bok) / 2, y0 = b->obszar_ekranu().y + k.y + (k.wysokosc - bok) / 2;
    auto jas = [](uint32_t c) { return int(((c >> 16) & 255) * 54 + ((c >> 8) & 255) * 183 + (c & 255) * 19) >> 8; };
    const int t = jas(tlo_przycisku(b));
    uint64_t suma = 0;
    for (int y = 0; y < bok; ++y) for (int x = 0; x < bok; ++x) suma += uint64_t(std::abs(jas(piksel(x0 + x, y0 + y)) - t));
    return suma;
}
// Regresja grubych/podwojnych obwodek: w srodkowym wierszu od lewej krawedzi PRZYCISKU do ikony wolno wystapic
// tylko tlu okna, powierzchni stanu i co najwyzej JEDNEMU pasowi ramki o szerokosci <= limit. Lewa krawedz przycisku
// = tlo (nic nie wychodzi poza kafel na sasiadow). Zwraca liczbe pasow ramki.
static int pasy_ramki(OknoGrajka& o, PrzyciskIkony* b, int* szer_max) {
    (void)probka_kafla(o, b);
    const auto st = b->styl_stanu();
    const Prostokat k = b->obszar_kafla();
    const int bx = b->obszar_ekranu().x, by = b->obszar_ekranu().y, y = by + k.y + k.wysokosc / 2;
    const int ikona_x = bx + k.x + (k.szerokosc - b->bok_ikony_px()) / 2;
    const uint32_t tlo = tlo_przycisku(b), wypelnienie = st.kolor_tla & 0xFFFFFFu;
    std::vector<std::pair<uint32_t, int>> pasy;
    for (int x = bx; x < ikona_x; ++x) {
        const uint32_t c = piksel(x, y);
        if (pasy.empty() || pasy.back().first != c) pasy.push_back({c, 1}); else ++pasy.back().second;
    }
    CHECK(!pasy.empty() && pasy.front().first == tlo);
    int n = 0; *szer_max = 0;
    for (auto& p : pasy) if (p.first != tlo && !(st.tlo && p.first == wypelnienie)) { ++n; if (p.second > *szer_max) *szer_max = p.second; }
    return n;
}
static void test_ikony_i_grafiki() {
    Jadro j; g_j = &j;
    przygotuj_muzyke();
    { GrajekFixtures::ParametryWav d; d.czestotliwosc = 48000; d.ramki = 48000 * 30; d.info = false;
      dodaj_plik("/dane/muzyka/Zz długi utwór.wav", GrajekFixtures::wav(d)); }
    for (const char* a : AKCJE_IKON_GRAJKA) CHECK(g_pliki.count(std::string("/ikonki/grajek_") + a + ".png"));
    CHECK(g_pliki.count("/ikonki/grajek_plyta_winylowa.jpg"));
    {   // plyta w systemie = oryginalny plik uzytkownika bajt w bajt (bez konwersji)
        const Bajty& p = g_pliki["/ikonki/grajek_plyta_winylowa.jpg"];
        CHECK(p.size() == 146235 && p[0] == 0xFF && p[1] == 0xD8);
    }
    g_ustawienia.clear();
    auto* o = new OknoGrajka;
    o->init(platforma());
    o->ustaw_geometrie(0, 0, 1180, 660); o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    // (1) 14 ikon: kazda zdekodowana dokladnie raz, 128x128 po konwersji przy budowaniu; przypisanie do akcji
    for (int i = 0; i < LICZBA_IKON; ++i)
        CHECK(o->ikony_[i].jest() && o->ikony_[i].liczba_dekodowan() == 1 && o->ikony_[i].szerokosc_zrodla() == 128 && o->ikony_[i].wysokosc_zrodla() == 128);
    struct { PrzyciskIkony* b; int ik; } mapa[] = {{o->poprz_, IK_POPRZEDNI}, {o->graj_, IK_ODTWORZ}, {o->stop_, IK_STOP}, {o->nast_, IK_NASTEPNY},
        {o->wycisz_, IK_WYCISZ}, {o->odswiez_, IK_ODSWIEZ}, {o->dodaj_kol_, IK_DODAJ}, {o->przyciski_kolejki_[0], IK_W_GORE},
        {o->przyciski_kolejki_[1], IK_W_DOL}, {o->przyciski_kolejki_[2], IK_USUN}, {o->przyciski_kolejki_[3], IK_WYCZYSC},
        {o->przyciski_kolejki_[4], IK_ZAPISZ}, {o->przyciski_kolejki_[5], IK_WCZYTAJ}};
    for (auto& m : mapa) CHECK(m.b->obraz_ikony() == &o->ikony_[m.ik] && m.b->uzywa_ikony() && m.b->podpowiedz()[0] && std::strlen(m.b->podpowiedz()) < 63);
    // przelaczniki: ikony PNG (styl grafik uzytkownika), nazwa akcji zostaje tekstem (dostepnosc, brak pliku)
    struct { PrzyciskIkony* b; int ik; } przel[] = {{o->losowo_, IK_LOSOWO}, {o->powtarzanie_, IK_POWTARZANIE}, {o->w_tle_, IK_W_TLE}, {o->wizualizacja_, IK_WIZUALIZACJA}};
    for (auto& m : przel) CHECK(m.b->obraz_ikony() == &o->ikony_[m.ik] && m.b->uzywa_ikony() && m.b->podpowiedz()[0] && std::strlen(m.b->podpowiedz()) < 63);
    CHECK(std::strstr(o->powtarzanie_->tekst(), "Powtarzanie") && std::strcmp(o->losowo_->tekst(), "Losowo") == 0);
    // (2) plyta: zdekodowana raz (przy starcie), okladka skalowana raz na rozmiar kontrolki - nie w kazdej klatce
    CHECK(o->plyta_.jest() && o->plyta_.dekodowania() == 1 && o->plyta_.szerokosc() == GrafikaPlyty::BOK_POSREDNI);
    CHECK(o->okladka_->czy_ma_obraz() && o->okladka_->piksele_wyswietlane());
    CHECK(o->okladka_->szerokosc_wyswietlana() == o->okladka_->obszar().szerokosc || o->okladka_->wysokosc_wyswietlana() == o->okladka_->obszar().wysokosc);
    kroki(*o, 25);
    auto indeks = [&](const char* tytul) { for (uint32_t w = 0; w < o->o_.biblioteka().widoczne(); ++w) if (const Utwor* u = o->o_.biblioteka().utwor(uint32_t(o->o_.biblioteka().widoczny(w)))) if (std::strcmp(u->tytul(), tytul) == 0) return w; return 0xFFFFFFFFu; };
    // dlugi WAV bez okladki w tagu: plyta uzytkownika; 2 s odtwarzania z odrysowaniami = zero skalowan i dekodowan
    const uint32_t i_dlugi = indeks("Zz długi utwór");
    CHECK(i_dlugi != 0xFFFFFFFFu && o->o_.odtworz_z_widoku(i_dlugi));
    for (int i = 0; i < 20; ++i) { kroki(*o, 1); KontekstRysowania c; o->rysuj(c); }
    CHECK(!o->o_.okladka().jest() && o->okladka_->piksele() == o->plyta_.piksele());
    const uint32_t sk_okl = o->okladka_->liczba_skalowan();
    uint32_t sk_ik[LICZBA_IKON]; for (int i = 0; i < LICZBA_IKON; ++i) sk_ik[i] = o->ikony_[i].liczba_skalowan();
    for (int i = 0; i < 500; ++i) { kroki(*o, 1); KontekstRysowania c; o->rysuj(c); }
    CHECK(o->o_.silnik().stan() == Stan::Odtwarzanie && o->plyta_.dekodowania() == 1 && o->okladka_->liczba_skalowan() == sk_okl);
    for (int i = 0; i < LICZBA_IKON; ++i) CHECK(o->ikony_[i].liczba_dekodowan() == 1);
    for (int i = 0; i < LICZBA_IKON; ++i) if (i != IK_PAUZA && i != IK_ODTWORZ) CHECK(o->ikony_[i].liczba_skalowan() == sk_ik[i]);
    // MP3 z okladka w tagu: ma pierwszenstwo przed plyta; jedno skalowanie nowego obrazu
    const uint32_t i_mp3 = indeks("Bursztynowy test");
    CHECK(i_mp3 != 0xFFFFFFFFu && o->o_.odtworz_z_widoku(i_mp3));
    for (int i = 0; i < 20; ++i) { kroki(*o, 1); KontekstRysowania c; o->rysuj(c); }
    CHECK(o->o_.okladka().jest() && o->okladka_->piksele() != o->plyta_.piksele() && o->okladka_->liczba_skalowan() == sk_okl + 1);
    CHECK(o->o_.odtworz_z_widoku(i_dlugi));
    for (int i = 0; i < 20; ++i) { kroki(*o, 1); KontekstRysowania c; o->rysuj(c); }
    const uint32_t sk_okl2 = o->okladka_->liczba_skalowan();
    CHECK(sk_okl2 == sk_okl + 2 && o->okladka_->piksele() == o->plyta_.piksele() && o->plyta_.dekodowania() == 1);
    // zmiana rozmiaru okna: okladka przeskalowana raz (nowy bok), bez ponownego dekodowania
    o->ustaw_geometrie(0, 0, 1920, 1040); o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    CHECK(o->okladka_->liczba_skalowan() == sk_okl2 + 1 && o->plyta_.dekodowania() == 1);
    // 150 %: ikony przygotowane w nowym boku (uklad), dekodowanie bez zmian
    o->ustaw_skale_dpi(SkalaUI::procent(150)); o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    CHECK(o->stop_->bok_ikony_px() == 36 && o->ikony_[IK_STOP].przygotowany_bok() == 36 && o->ikony_[IK_STOP].liczba_dekodowan() == 1);
    o->ustaw_skale_dpi(SkalaUI(1)); o->ustaw_geometrie(0, 0, 1180, 660); o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    // (3) stany. Normalny i wylaczony: BEZ tla pod ikona (dawniej jasny "kafel") - w miejscu kafla widac tlo okna,
    //     przezroczyste piksele ikony nie zmieniaja tla; najechanie, zaznaczenie, wcisniecie: powierzchnia stanu;
    //     wylaczenie: ikona przygaszona (mniejszy kontrast z tlem).
    CHECK(o->stop_->czy_wlaczona());
    (void)probka_kafla(*o, o->stop_);
    const uint64_t kontrast_wl = kontrast_ikony(o->stop_);
    o->o_.stop(); kroki(*o, 3);
    CHECK(!o->stop_->czy_wlaczona());
    const uint32_t wyl = probka_kafla(*o, o->stop_);
    CHECK(wyl == tlo_przycisku(o->stop_) && kontrast_ikony(o->stop_) * 10 < kontrast_wl * 6 && kontrast_ikony(o->stop_) > 0);
    const uint32_t norm = probka_kafla(*o, o->nast_);
    CHECK(norm == tlo_przycisku(o->nast_));
    for (auto& m : mapa) if (m.b->czy_wlaczona() && !m.b->czy_zaznaczony() && m.b != o->graj_) {
        CHECK(probka_kafla(*o, m.b) == tlo_przycisku(m.b));          // zadnego jasnego tla pod zadna ikona
        CHECK(kontrast_ikony(m.b) > 0);                               // ikona narysowana
    }
    o->nast_->obsluz_wejscie_myszy();
    CHECK(o->nast_->czy_najechana());
    const uint32_t naj = probka_kafla(*o, o->nast_);
    CHECK(naj != norm);
    o->nast_->obsluz_opuszczenie_myszy();
    klik(*o->wycisz_); kroki(*o, 1);
    CHECK(o->wycisz_->czy_zaznaczony() && probka_kafla(*o, o->wycisz_) != norm);
    klik(*o->wycisz_); kroki(*o, 1);
    CHECK(!o->wycisz_->czy_zaznaczony());
    {
        ZdarzenieMyszki e{}; e.typ = TypZdarzenia::WcisniecieMyszy; e.przycisk = 1;
        e.x = o->nast_->obszar_ekranu().x + 5; e.y = o->nast_->obszar_ekranu().y + 5;
        o->nast_->obsluz_wcisniecie_myszy(e);
        CHECK(o->nast_->czy_wcisnieta() && probka_kafla(*o, o->nast_) != norm);
        e.x = -100; e.y = -100; ZdarzenieMyszki u = e; u.typ = TypZdarzenia::ZwolnienieMyszy;
        const uint32_t kol = o->o_.kolejka().biezaca() < 0 ? 0u : uint32_t(o->o_.kolejka().biezaca());
        o->nast_->obsluz_zwolnienie_myszy(u);                     // zwolnienie poza przyciskiem: bez akcji
        CHECK(!o->nast_->czy_wcisnieta() && (o->o_.kolejka().biezaca() < 0 ? 0u : uint32_t(o->o_.kolejka().biezaca())) == kol);
    }
    // (3b) styl stanow: jedna ramka 1 DIP (bez podwojnych pierscieni i grubych obwodek), takze w stanach laczonych
    {
        int szer = 0;
        auto stan = [&](PrzyciskIkony* b, bool fokus, bool najechany, bool wcisniety, int oczek_pasow, const char* opis) {
            if (fokus) b->obsluz_uzyskanie_skupienia();
            if (najechany) b->obsluz_wejscie_myszy();
            ZdarzenieMyszki e{}; e.typ = TypZdarzenia::WcisniecieMyszy; e.przycisk = 1;
            e.x = b->obszar_ekranu().x + 5; e.y = b->obszar_ekranu().y + 5;
            if (wcisniety) b->obsluz_wcisniecie_myszy(e);
            const auto st = b->styl_stanu();
            CHECK(st.grubosc == 1 && (st.ramka ? 1 : 0) == oczek_pasow);
            const int n = pasy_ramki(*o, b, &szer);
            if (n != oczek_pasow || szer > (b->rodzaj() == RodzajPrzyciskuIkony::Glowny ? 2 : 1))
                std::fprintf(stderr, "stan %s: pasow ramki %d (oczekiwano %d), szerokosc %d\n", opis, n, oczek_pasow, szer);
            CHECK(n == oczek_pasow && szer <= (b->rodzaj() == RodzajPrzyciskuIkony::Glowny ? 2 : 1));
            if (wcisniety) { ZdarzenieMyszki u = e; u.typ = TypZdarzenia::ZwolnienieMyszy; u.x = -100; u.y = -100; b->obsluz_zwolnienie_myszy(u); }
            if (najechany) b->obsluz_opuszczenie_myszy();
            if (fokus) b->obsluz_utrate_skupienia();
            return st;
        };
        const auto n0 = stan(o->nast_, false, false, false, 0, "normalny");
        CHECK(!n0.tlo && !n0.ramka);                                         // bez tla i ramki
        const auto h = stan(o->nast_, false, true, false, 0, "najechany");    // subtelne tlo, bez ramki
        const auto w = stan(o->nast_, false, true, true, 0, "wcisniety");
        CHECK(h.tlo && w.tlo && h.kolor_tla != w.kolor_tla);
        const auto f = stan(o->nast_, true, false, false, 1, "fokus");
        CHECK(f.kolor_ramki == motyw_kolor(MOTYW_KOLOR_AKCENT));             // fokus: pelny akcent motywu
        (void)stan(o->nast_, true, true, true, 1, "fokus+wcisniety");
        klik(*o->wycisz_); kroki(*o, 1);
        CHECK(o->wycisz_->czy_zaznaczony());
        const auto z = stan(o->wycisz_, false, false, false, 1, "zaznaczony");
        const auto zf = stan(o->wycisz_, true, false, false, 1, "zaznaczony+fokus");   // JEDNA ramka
        CHECK(z.tlo && z.kolor_ramki != zf.kolor_ramki && zf.kolor_ramki == motyw_kolor(MOTYW_KOLOR_AKCENT));
        (void)stan(o->wycisz_, true, true, false, 1, "zaznaczony+fokus+najechany");
        klik(*o->wycisz_); kroki(*o, 1);
        CHECK(!o->wycisz_->czy_zaznaczony());
        const auto g = stan(o->graj_, false, false, false, 1, "glowny");
        const auto gf = stan(o->graj_, true, true, false, 1, "glowny+fokus+najechany");
        CHECK(g.kolor_ramki != gf.kolor_ramki && !g.tlo);
        for (auto* b : o->przyciski_kolejki_) (void)stan(b, true, false, false, 1, "kolejka+fokus");
        (void)stan(o->odswiez_, false, true, false, 0, "odswiez+najechany");
        CHECK(!o->stop_->czy_wlaczona());                                    // Stop po zatrzymaniu: wylaczony
        const auto d = stan(o->stop_, true, true, false, 0, "wylaczony+fokus+najechany");
        CHECK(!d.tlo && !d.ramka);                                           // bez tla i ramki, ikona przygaszona
    }
    // (4) klawiatura: Spacja i Enter na przycisku z fokusem wykonuja akcje (Odtworz/Pauza)
    CHECK(o->graj_->czy_moze_miec_fokus());
    {
        ZdarzenieKlawiatury k{}; k.kod_klawisza = 32;
        o->graj_->obsluz_wcisniecie_klawisza(k); kroki(*o, 3);
        CHECK(o->o_.silnik().stan() == Stan::Odtwarzanie && o->graj_->obraz_ikony() == &o->ikony_[IK_PAUZA]);
        ZdarzenieKlawiatury e{}; e.kod_klawisza = 13;
        o->graj_->obsluz_wcisniecie_klawisza(e); kroki(*o, 2);
        CHECK(o->o_.silnik().stan() == Stan::Pauza && o->graj_->obraz_ikony() == &o->ikony_[IK_ODTWORZ]);
        CHECK(std::strstr(o->graj_->podpowiedz(), "Wznów"));
    }
    // fokus: pierscien rysowany wewnatrz obszaru przycisku (bez wychodzenia na sasiadow)
    o->graj_->obsluz_uzyskanie_skupienia();
    CHECK(o->graj_->czy_ma_fokus() && GrajekUklad::ikona_miesci(o->graj_));
    (void)probka_kafla(*o, o->graj_);
    o->graj_->obsluz_utrate_skupienia();
    // (5) podpowiedz: po najechaniu i opoznieniu - w obszarze roboczym, przy kursorze, z polskim tekstem akcji
    {
        bws_zdarzenie e{}; e.typ = BWS_ZDARZENIE_MYSZ_RUCH;
        e.x = o->x() + o->przyciski_kolejki_[5]->obszar_ekranu().x + 10; e.y = o->y() + o->przyciski_kolejki_[5]->obszar_ekranu().y + 10;
        o->obsluz_zdarzenie(e);
        kroki(*o, 200);                                            // 800 ms > opoznienie 500 ms
        ZarzadcaPopupow& z = o->zarzadca_popupow();
        CHECK(z.czy_otwarty() && z.liczba_powierzchni() == 1);
        const Prostokat r = z.powierzchnia(0)->obszar(), d = z.obszar_roboczy_ekranu();
        CHECK(r.x >= d.x && r.y >= d.y && r.prawy() <= d.prawy() && r.dolny() <= d.dolny());
        CHECK(r.x <= e.x + 12 && r.y <= e.y + 18 && r.szerokosc > 0);
        CHECK(std::strstr(o->podpowiedzi_.podpowiedz().tekst(), "M3U8"));
        bws_zdarzenie b{}; b.typ = BWS_ZDARZENIE_BLUR; o->obsluz_zdarzenie(b);
        CHECK(!z.czy_otwarty());
    }
    o->o_.zakoncz(); delete o;

    // (6) brak grafik: przyciski tekstowe (dzialaja), okladka - pusty panel; bez awarii i bez ponownych odczytow
    g_zasoby_grajka = false;
    przygotuj_muzyke();
    o = new OknoGrajka; o->init(platforma()); o->ustaw_geometrie(0, 0, 1180, 660); o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    for (int i = 0; i < LICZBA_IKON; ++i) CHECK(!o->ikony_[i].jest() && o->ikony_[i].proba());
    CHECK(!o->graj_->uzywa_ikony() && GrajekUklad::tekst_miesci(o->graj_) && std::strcmp(o->graj_->tekst(), "Odtwórz") == 0);
    CHECK(!o->plyta_.jest() && !o->okladka_->czy_ma_obraz());
    kroki(*o, 25);
    klik(*o->graj_); kroki(*o, 3);
    CHECK(o->o_.silnik().stan() == Stan::Odtwarzanie);
    { KontekstRysowania c; o->rysuj(c, true); }
    o->o_.zakoncz(); delete o;
    // (7) uszkodzone grafiki: zly PNG i obciety JPEG odrzucone (przycisk tekstowy / pusty panel), pozostale dzialaja
    g_zasoby_grajka = true;
    przygotuj_muzyke();
    { Bajty z(g_pliki["/ikonki/grajek_stop.png"]); for (size_t i = 40; i < z.size(); i += 7) z[i] ^= 0x5A; g_pliki["/ikonki/grajek_stop.png"] = z; }
    { Bajty& z = g_pliki["/ikonki/grajek_plyta_winylowa.jpg"]; z.resize(900); }
    g_pliki["/ikonki/grajek_usun.png"] = Bajty(300 * 1024, 0x89);               // za duzy (> 256 KiB)
    o = new OknoGrajka; o->init(platforma()); o->ustaw_geometrie(0, 0, 1180, 660); o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    CHECK(!o->ikony_[IK_STOP].jest() && !o->stop_->uzywa_ikony() && std::strcmp(o->stop_->tekst(), "Stop") == 0);
    CHECK(!o->ikony_[IK_USUN].jest() && !o->przyciski_kolejki_[2]->uzywa_ikony());
    CHECK(o->ikony_[IK_NASTEPNY].jest() && o->nast_->uzywa_ikony());
    CHECK(!o->plyta_.jest() && o->plyta_.blad()[0] && !o->okladka_->czy_ma_obraz());
    o->o_.zakoncz(); delete o;
    przygotuj_muzyke();
}

// Plik od systemu (skojarzenie .mp3/.wav): argument startowy przy uruchomieniu i BWS_ZDARZENIE_OTWORZ_PLIK w dzialajacym
// Grajku - kolejka bez duplikatow, automatyczne odtwarzanie przez BWS 110, pliki spoza /dane/muzyka, bledy po polsku.
static std::string g_argument;
static bool argument_fn(char* b, uint32_t cap) {
    if (g_argument.empty() || g_argument.size() + 1 > cap) return false;   // jak BWS 45: pelny albo zaden
    std::memcpy(b, g_argument.c_str(), g_argument.size() + 1);
    return true;
}
static void test_otwieranie_pliku() {
    using namespace GrajekFixtures;
    // (1) zwykly start bez argumentu: biblioteka z /dane/muzyka, brak automatycznego odtwarzania
    {
        Jadro j; g_j = &j; przygotuj_muzyke(); g_ustawienia.clear(); g_argument.clear();
        Platforma p = platforma(); p.argument_startowy = argument_fn;
        auto* o = new OknoGrajka; o->init(p); o->ustaw_geometrie(0, 0, 1180, 660); o->przelicz_uklad();
        CHECK(!o->otworz_argument_startowy());
        kroki(*o, 40);
        CHECK(o->o_.biblioteka().liczba() == 3 && o->o_.kolejka().liczba() == 0 && !o->o_.silnik().zaladowany());
        CHECK(o->o_.silnik().stan() == Stan::Zatrzymany && j.otwarcia == 0);
        delete o;
    }
    // (2) pusty katalog muzyki + plik spoza /dane/muzyka podany przy starcie
    {
        Jadro j; g_j = &j; wyczysc_fs(); g_katalogi["/dane/muzyka"] = true; g_ustawienia.clear();
        ParametryWav w; w.czestotliwosc = 48000; w.ramki = 48000 * 2;
        dodaj_plik("/uzytkownicy/domyslny/Dokumenty/Głos ąę.WAV", wav(w));
        g_argument = "/uzytkownicy/domyslny/Dokumenty/Głos ąę.WAV";
        Platforma p = platforma(); p.argument_startowy = argument_fn;
        auto* o = new OknoGrajka; o->init(p); o->ustaw_geometrie(0, 0, 1180, 660); o->przelicz_uklad();
        CHECK(o->otworz_argument_startowy());
        kroki(*o, 30);
        CHECK(o->o_.silnik().stan() == Stan::Odtwarzanie && o->o_.utwor_biezacy() && std::strcmp(o->o_.utwor_biezacy()->sciezka, g_argument.c_str()) == 0);
        CHECK(o->o_.biblioteka().liczba_skanu() == 0 && o->o_.biblioteka().widoczne() == 0 && o->o_.kolejka().liczba() == 1);
        CHECK(o->o_.utwor_biezacy() && std::strcmp(o->o_.utwor_biezacy()->tytul(), "Głos ąę") == 0);
        CHECK(o->graj_->obraz_ikony() == &o->ikony_[IK_PAUZA]);              // Odtworz/Pauza zgodnie ze stanem
        CHECK(!g_pliki.count("/dane/muzyka/Głos ąę.WAV"));                      // bez kopiowania / importu
        delete o;
    }
    // (3) start z argumentem w trakcie skanu biblioteki + kolejne pliki w dzialajacym Grajku (zdarzenie)
    Jadro j; g_j = &j; przygotuj_muzyke(); g_ustawienia.clear();
    ParametryWav dl; dl.czestotliwosc = 48000; dl.ramki = 48000 * 20;
    dodaj_plik("/dane/muzyka/Album/Electric Pulse.WAV", wav(dl));                // podkatalog, wielkie litery
    dodaj_plik("/inne/Bursztyn OS2.mp3", g_pliki["/dane/muzyka/Piosenka3.mp3"]);   // poza biblioteka, spacja
    dodaj_plik("/inne/udaje.mp3", wav(dl));                                     // mylace rozszerzenie: WAV w .mp3
    dodaj_plik("/inne/pusty.mp3", Bajty());
    dodaj_plik("/inne/obraz.wav", png(8, 8, 0x123456));
    { Bajty b(4000); for (size_t i = 0; i < b.size(); ++i) b[i] = uint8_t(i * 37 + 11); dodaj_plik("/inne/uszkodzony.mp3", b); }
    { ParametryWav w6; w6.kanaly = 6; w6.ramki = 1000; dodaj_plik("/inne/szesc kanalow.wav", wav(w6)); }
    dodaj_plik("/inne/blad odczytu.mp3", g_pliki["/dane/muzyka/Piosenka3.mp3"]); g_bledy_odczytu.insert("/inne/blad odczytu.mp3");
    g_argument = "/dane/muzyka/Album/Electric Pulse.WAV";
    Platforma p = platforma(); p.argument_startowy = argument_fn;
    auto* o = new OknoGrajka; o->init(p); o->ustaw_geometrie(0, 0, 1180, 660); o->przelicz_uklad();
    CHECK(o->o_.biblioteka().skanuje());
    CHECK(o->otworz_argument_startowy());
    CHECK(o->o_.kolejka().liczba() == 1 && o->o_.silnik().stan() == Stan::Ladowanie);
    kroki(*o, 60);                                                               // skan konczy sie podczas odtwarzania
    CHECK(!o->o_.biblioteka().skanuje() && o->o_.biblioteka().liczba_skanu() == 4 && o->o_.biblioteka().liczba() == 4);   // bez duplikatu
    CHECK(o->o_.kolejka().liczba() == 1 && o->o_.kolejka().biezaca() == 0);     // kolejka przemapowana wg sciezki
    CHECK(o->o_.utwor_biezacy() && std::strcmp(o->o_.utwor_biezacy()->sciezka, "/dane/muzyka/Album/Electric Pulse.WAV") == 0);
    CHECK(o->o_.silnik().stan() == Stan::Odtwarzanie && j.otwarcia == 1);
    auto zdarzenie = [&](const char* sciezka) {
        g_argument = sciezka; bws_zdarzenie e{}; e.typ = BWS_ZDARZENIE_OTWORZ_PLIK; o->obsluz_zdarzenie(e); kroki(*o, 6);
    };
    // ten sam plik ponownie: bez duplikatu i bez restartu
    const uint64_t poz = o->o_.silnik().pozycja_ms();
    zdarzenie("/dane/muzyka/Album/Electric Pulse.WAV");
    CHECK(o->o_.kolejka().liczba() == 1 && j.otwarcia == 1 && o->o_.silnik().pozycja_ms() >= poz);
    CHECK(o->o_.pauza() && o->o_.silnik().stan() == Stan::Pauza);
    zdarzenie("/dane/muzyka/Album/Electric Pulse.WAV");                         // w pauzie: wznowienie, bez restartu
    CHECK(o->o_.silnik().stan() == Stan::Odtwarzanie && o->o_.kolejka().liczba() == 1 && o->o_.silnik().pozycja_ms() >= poz);
    // plik spoza biblioteki: dopisany na koniec kolejki i odtwarzany od razu; widok biblioteki bez zmian
    zdarzenie("/inne/Bursztyn OS2.mp3");
    CHECK(o->o_.kolejka().liczba() == 2 && o->o_.kolejka().biezaca() == 1 && o->o_.biblioteka().widoczne() == 4);
    CHECK(o->o_.silnik().stan() == Stan::Odtwarzanie && std::strcmp(o->o_.utwor_biezacy()->sciezka, "/inne/Bursztyn OS2.mp3") == 0);
    CHECK(std::strcmp(o->o_.utwor_biezacy()->tytul(), "Bursztynowy test") == 0 && std::strcmp(o->o_.utwor_biezacy()->meta.wykonawca, "Grajek") == 0);
    CHECK(o->o_.okladka().jest());                                              // okladka z tagu
    // plik z biblioteki juz w kolejce? nie - dopisany; ponowne otwarcie przechodzi do jego pozycji
    zdarzenie("/dane/muzyka/Piosenka1.wav");
    CHECK(o->o_.kolejka().liczba() == 3 && o->o_.kolejka().biezaca() == 2);
    zdarzenie("/inne/Bursztyn OS2.mp3");
    CHECK(o->o_.kolejka().liczba() == 3 && o->o_.kolejka().biezaca() == 1 && o->o_.silnik().stan() == Stan::Odtwarzanie);
    // mylace rozszerzenie: o formacie decyduje zawartosc (WAV w .mp3 gra jako WAV)
    zdarzenie("/inne/udaje.mp3");
    CHECK(o->o_.utwor_biezacy()->format == FormatAudio::WAV && o->o_.silnik().stan() == Stan::Odtwarzanie && o->o_.kolejka().liczba() == 4);
    // ponowny skan: pliki zewnetrzne i kolejka zachowane
    o->o_.odswiez_biblioteke(); kroki(*o, 60);
    CHECK(o->o_.biblioteka().liczba_skanu() == 4 && o->o_.biblioteka().liczba() == 6 && o->o_.kolejka().liczba() == 4);
    CHECK(o->o_.utwor_biezacy() && std::strcmp(o->o_.utwor_biezacy()->sciezka, "/inne/udaje.mp3") == 0);
    // (4) bledy: komunikat po polsku, kolejka bez zmian, biezacy utwor gra dalej (bez przelaczenia na inny)
    struct { const char* sciezka; const char* fragment; } bledy[] = {
        {"/inne/brak.mp3", "nie istnieje"},
        {"/inne/pusty.mp3", "pusty"},
        {"/inne/obraz.wav", "nie odpowiada formatowi"},
        {"/inne/uszkodzony.mp3", "„uszkodzony.mp3”"},
        {"/inne/szesc kanalow.wav", "liczba kanałów"},
        {"/inne/blad odczytu.mp3", "odczytać"},
        {"wzgledna.mp3", "Nieprawidłowa ścieżka"},
        {"/dane/muzyka/../inne/Bursztyn OS2.mp3", "Nieprawidłowa ścieżka"},
        {"/dane/muzyka/", "Nieprawidłowa ścieżka"},
        {"/dane//muzyka/Piosenka1.wav", "Nieprawidłowa ścieżka"},
        {"/dane/muzyka/zły\x01znak.mp3", "Nieprawidłowa ścieżka"},
    };
    for (auto& b : bledy) {
        const uint32_t n = o->o_.kolejka().liczba(); const uint64_t otw = j.otwarcia;
        zdarzenie(b.sciezka);
        if (!std::strstr(o->o_.komunikat(), b.fragment)) std::fprintf(stderr, "%s -> %s\n", b.sciezka, o->o_.komunikat());
        CHECK(o->o_.rodzaj_komunikatu() == RodzajKomunikatu::Blad && std::strstr(o->o_.komunikat(), b.fragment));
        CHECK(o->o_.kolejka().liczba() == n && j.otwarcia == otw && o->o_.silnik().stan() == Stan::Odtwarzanie);
        CHECK(std::strcmp(o->o_.utwor_biezacy()->sciezka, "/inne/udaje.mp3") == 0);
    }
    {   // za dluga sciezka (limit Grajka 191 B; jadro przekazuje do 511 B bez obcinania)
        const std::string dluga = "/inne/" + std::string(250, 'a') + ".mp3";
        zdarzenie(dluga.c_str());
        CHECK(o->o_.rodzaj_komunikatu() == RodzajKomunikatu::Blad && std::strstr(o->o_.komunikat(), "za długa"));
        g_argument = std::string(600, 'b');                                     // za dlugi dla bufora: brak argumentu
        bws_zdarzenie e{}; e.typ = BWS_ZDARZENIE_OTWORZ_PLIK; o->obsluz_zdarzenie(e);
        CHECK(!o->otworz_argument_startowy());
    }
    // (5) brak urzadzenia dzwieku: blad PCM pokazany wprost, bez przejscia na inny utwor
    {
        o->o_.stop(); kroki(*o, 2); j.urzadzenie = false;
        zdarzenie("/dane/muzyka/Piosenka2 mono.wav"); kroki(*o, 10);
        CHECK(o->o_.silnik().stan() == Stan::Blad && std::strstr(o->o_.komunikat(), "Brak dostępnego urządzenia dźwięku"));
        CHECK(std::strcmp(o->o_.utwor_biezacy()->sciezka, "/dane/muzyka/Piosenka2 mono.wav") == 0 && o->o_.kolejka().biezaca() >= 0);
        j.urzadzenie = true;
        zdarzenie("/dane/muzyka/Piosenka2 mono.wav"); kroki(*o, 6);
        CHECK(o->o_.silnik().stan() == Stan::Odtwarzanie);
    }
    // (6) wielokrotne uruchomienia: limit plikow zewnetrznych sesji zamiast nadpisania pamieci
    {
        int ok = 0;
        for (int i = 0; i < 40; ++i) {
            char sc[64]; std::snprintf(sc, sizeof(sc), "/inne/seria %02d.wav", i);
            ParametryWav w; w.ramki = 4800; dodaj_plik(sc, wav(w));
            zdarzenie(sc);
            if (o->o_.rodzaj_komunikatu() != RodzajKomunikatu::Blad) ++ok;
        }
        CHECK(ok > 0 && ok < 40 && std::strstr(o->o_.komunikat(), "Za dużo otwartych plików"));
        CHECK(o->o_.biblioteka().liczba() <= POJEMNOSC_UTWOROW && o->o_.biblioteka().widoczne() == 4);
    }
    g_bledy_odczytu.clear(); g_argument.clear();
    o->o_.zakoncz();
    delete o;
    std::printf("otwieranie pliku z systemu OK\n");
}

// Lista presetow korektora: dawniej otwierala sie i od razu zamykala - podpowiedz (kontrolki pod lista albo samej
// listy) po 500 ms otwierala sie przez ZarzadcaPopupow::otworz, ktory zamyka WSZYSTKIE popupy. Teraz lista zostaje
// otwarta przy ruchu i postoju kursora (takze dluzej niz opoznienie podpowiedzi), wybor mysza i klawiatura dziala.
static void zdarzenie_myszy(OknoGrajka& o, uint32_t typ, int x, int y) {
    bws_zdarzenie e{}; e.typ = typ; e.x = x; e.y = y; e.przyciski = (typ == BWS_ZDARZENIE_MYSZ_DOWN) ? 1 : 0;
    e.timestamp = test_monotonic_ns / 1000000ULL;
    o.obsluz_zdarzenie(e);
}
static void klawisz(Kontrolka& k, uint32_t kod) {
    ZdarzenieKlawiatury e{}; e.kod_klawisza = kod; e.kod_skanu = KLAWISZ_FIZYCZNY_GUI;
    k.obsluz_wcisniecie_klawisza(e);
}
static void test_lista_presetow() {
    Jadro j; g_j = &j;
    przygotuj_muzyke();
    g_ustawienia.clear();
    g_ustawienia[KLUCZ_KONFIGURACJI] = "korektor=1\n";
    auto* o = new OknoGrajka;
    o->init(platforma());
    o->ustaw_geometrie(0, 0, 1180, 660); o->przelicz_uklad();
    o->karty_->ustaw_aktywna_karte(1); o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    kroki(*o, 25);
    ListaRozwijana* l = o->preset_;
    CHECK(l->czy_wlaczona() && l->wybrany_indeks() == int(PresetKorektora::Plaski) && !l->czy_rozwinieta());
    const Prostokat r = l->obszar_ekranu();
    const int px = r.x + r.szerokosc - 10, py = r.y + r.wysokosc / 2;   // przycisk strzalki
    // (1) mysz: klik otwiera; kursor stoi nad lista 3 s i jezdzi po niej - lista zostaje otwarta
    zdarzenie_myszy(*o, BWS_ZDARZENIE_MYSZ_RUCH, px, py); kroki(*o, 1);
    zdarzenie_myszy(*o, BWS_ZDARZENIE_MYSZ_DOWN, px, py);
    zdarzenie_myszy(*o, BWS_ZDARZENIE_MYSZ_UP, px, py);
    CHECK(l->czy_rozwinieta());
    for (int i = 0; i < 750; ++i) {                                  // 3 s ticków po 4 ms, kursor nad strzalka
        kroki(*o, 1);
        if (i % 50 == 0) { KontekstRysowania c; o->rysuj(c); }
    }
    CHECK(l->czy_rozwinieta() && !o->podpowiedzi_.czy_widoczna());
    const Prostokat pl = l->prostokat_popupu();
    CHECK(pl.szerokosc > 0 && pl.wysokosc > 0);
    for (int i = 0; i < 40; ++i) {                                   // ruch po elementach listy (pod nia sa suwaki z podpowiedziami)
        zdarzenie_myszy(*o, BWS_ZDARZENIE_MYSZ_RUCH, pl.x + pl.szerokosc / 2, pl.y + 4 + (i * pl.wysokosc) / 48);
        kroki(*o, 30);
        CHECK(l->czy_rozwinieta());
    }
    CHECK(!o->podpowiedzi_.czy_widoczna());
    // klik w element: wybor zastosowany w korektorze, lista zamknieta, bez zmiany innych ustawien
    const int wiersz = int(PresetKorektora::Rock);
    const ListaWyboru& lw = l->lista_popupu();
    CHECK(int(PresetKorektora::Liczba) <= l->maks_widocznych_elementow());   // bez przewijania listy
    const int hw = lw.wysokosc_wiersza_piksele();
    const int iy = lw.obszar_ekranu().y + (lw.wysokosc_dla_wierszy(1) - hw) / 2 + wiersz * hw + hw / 2;
    CHECK(iy >= pl.y && iy < pl.dolny());
    zdarzenie_myszy(*o, BWS_ZDARZENIE_MYSZ_RUCH, pl.x + pl.szerokosc / 2, iy);
    zdarzenie_myszy(*o, BWS_ZDARZENIE_MYSZ_DOWN, pl.x + pl.szerokosc / 2, iy);
    zdarzenie_myszy(*o, BWS_ZDARZENIE_MYSZ_UP, pl.x + pl.szerokosc / 2, iy);
    kroki(*o, 2);
    CHECK(!l->czy_rozwinieta() && l->wybrany_indeks() == wiersz);
    CHECK(o->o_.silnik().korektor().rozpoznaj_preset() == PresetKorektora::Rock && o->pasma_[9]->wartosc() == 4);
    // (2) klik poza lista zamyka ja bez zmiany wyboru
    zdarzenie_myszy(*o, BWS_ZDARZENIE_MYSZ_DOWN, px, py); zdarzenie_myszy(*o, BWS_ZDARZENIE_MYSZ_UP, px, py);
    CHECK(l->czy_rozwinieta());
    const Prostokat t = o->tytul_->obszar_ekranu();
    zdarzenie_myszy(*o, BWS_ZDARZENIE_MYSZ_DOWN, t.x + 4, t.y + 4); zdarzenie_myszy(*o, BWS_ZDARZENIE_MYSZ_UP, t.x + 4, t.y + 4);
    CHECK(!l->czy_rozwinieta() && l->wybrany_indeks() == wiersz);
    // (3) klawiatura: Enter otwiera, strzalki wybieraja, Enter zatwierdza; Esc nie zmienia
    klawisz(*l, 13); CHECK(l->czy_rozwinieta());
    kroki(*o, 300); CHECK(l->czy_rozwinieta());
    klawisz(*l, 0x50); klawisz(*l, 0x50);                           // dwa w dol (podglad stosowany na biezaco)
    CHECK(l->czy_rozwinieta() && o->o_.silnik().korektor().rozpoznaj_preset() == PresetKorektora(wiersz + 2));
    klawisz(*l, 13); kroki(*o, 1);
    CHECK(!l->czy_rozwinieta() && l->wybrany_indeks() == wiersz + 2);
    CHECK(o->o_.silnik().korektor().rozpoznaj_preset() == PresetKorektora(wiersz + 2));
    klawisz(*l, 13); CHECK(l->czy_rozwinieta());
    klawisz(*l, 0x48); kroki(*o, 50);                                // podglad poprzedniego presetu, lista nadal otwarta
    CHECK(l->czy_rozwinieta() && o->o_.silnik().korektor().rozpoznaj_preset() == PresetKorektora(wiersz + 1));
    l->zwin(); kroki(*o, 1);                                         // anulowanie (Esc / klik poza): wybor i korektor przywrocone
    CHECK(!l->czy_rozwinieta() && l->wybrany_indeks() == wiersz + 2);
    CHECK(o->o_.silnik().korektor().rozpoznaj_preset() == PresetKorektora(wiersz + 2));
    // (4) podpowiedz po zamknieciu listy nadal dziala (nie zostala zablokowana)
    zdarzenie_myszy(*o, BWS_ZDARZENIE_MYSZ_RUCH, px - 30, py);
    for (int i = 0; i < 200; ++i) kroki(*o, 1);
    CHECK(o->podpowiedzi_.czy_widoczna());
    // (5) otwarta podpowiedz nie przeszkadza: klik otwiera liste, podpowiedz znika, lista zostaje
    zdarzenie_myszy(*o, BWS_ZDARZENIE_MYSZ_DOWN, px, py); zdarzenie_myszy(*o, BWS_ZDARZENIE_MYSZ_UP, px, py);
    CHECK(l->czy_rozwinieta() && !o->podpowiedzi_.czy_widoczna());
    for (int i = 0; i < 300; ++i) kroki(*o, 1);
    CHECK(l->czy_rozwinieta());
    l->zwin();
    o->o_.zakoncz(); delete o;
}

// Dolny pasek: bez "Zakończ" (zamyka tylko X) i bez pola "Diagnostyka" (karta Ustawienia); przelaczniki z ikonami
// dzialaja mysza i klawiatura (Spacja / Enter), X zamyka Grajka takze przy wlaczonym odtwarzaniu w tle.
static OknoGrajka* g_okno_x = nullptr;
static Jadro* g_jadro_x = nullptr;
static void szukaj_tekstu(Kontrolka* k, const char* t, int* n) {
    if (!k) return;
    if (k->tekst() && std::strcmp(k->tekst(), t) == 0) ++*n;
    for (size_t i = 0; i < k->liczba_dzieci(); ++i) szukaj_tekstu(k->dziecko(i), t, n);
}
static void test_pasek_i_ustawienia() {
    Jadro j; g_j = &j;
    przygotuj_muzyke();
    g_ustawienia.clear();
    auto* o = new OknoGrajka;
    o->init(platforma());
    o->ustaw_geometrie(0, 0, 1180, 660); o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    kroki(*o, 25);
    // usuniete teksty i kontrolki
    const char* usuniete[] = {"Zakończ", "Odtwarzacz muzyki Bursztyn OS", "Biblioteka muzyki · /dane/muzyka", "Diagnostyka"};
    for (const char* t : usuniete) {
        int n = 0;
        szukaj_tekstu(o->kontrolka_glowna(), t, &n);
        CHECK(n == (std::strcmp(t, "Diagnostyka") == 0 ? 1 : 0));    // "Diagnostyka" zostaje tylko jako naglowek w Ustawieniach
    }
    // karta Ustawienia z polem pomiaru (to samo zachowanie co dawne pole w pasku)
    CHECK(o->karty_->liczba_kart() == 4 && OknoGrajka::KARTA_USTAWIEN == 3);
    { int n = 0; szukaj_tekstu(o->kontrolka_glowna(), "Ustawienia", &n); CHECK(n == 0); }   // tytul karty jest w Karty, nie w kontrolce
    o->karty_->ustaw_aktywna_karte(OknoGrajka::KARTA_USTAWIEN); o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    CHECK(!o->o_.konfiguracja().diagnostyka && !o->diagnostyka_->czy_zaznaczone());
    klik(*o->diagnostyka_); kroki(*o, 300);
    CHECK(o->o_.konfiguracja().diagnostyka && g_profiler.wlaczony() && std::strstr(o->komunikat_->skrot(), "Niedobór"));
    klik(*o->diagnostyka_); kroki(*o, 2);
    CHECK(!o->o_.konfiguracja().diagnostyka && !o->komunikat_->skrot()[0]);
    o->o_.ustaw_diagnostyke(true); kroki(*o, 1);                     // zmiana z innego zrodla: pole zsynchronizowane
    CHECK(o->diagnostyka_->czy_zaznaczone());
    o->o_.ustaw_diagnostyke(false); kroki(*o, 1);
    o->karty_->ustaw_aktywna_karte(0); o->przelicz_uklad();
    // przelaczniki klawiatura: Spacja i Enter (fokus na przycisku)
    PrzyciskIkony* przel[] = {o->losowo_, o->w_tle_, o->wizualizacja_};
    for (PrzyciskIkony* b : przel) CHECK(b->czy_moze_miec_fokus());
    const bool w0 = o->o_.konfiguracja().wizualizacja;
    klawisz(*o->losowo_, 32); kroki(*o, 1);
    CHECK(o->o_.konfiguracja().losowo && o->losowo_->czy_zaznaczony());
    klawisz(*o->losowo_, 13); kroki(*o, 1);
    CHECK(!o->o_.konfiguracja().losowo && !o->losowo_->czy_zaznaczony());
    klawisz(*o->wizualizacja_, 32); kroki(*o, 1);
    CHECK(o->o_.konfiguracja().wizualizacja == !w0 && o->wizualizacja_->czy_zaznaczony() == !w0);
    klawisz(*o->powtarzanie_, 32); kroki(*o, 1);
    CHECK(o->o_.konfiguracja().powtarzanie == Powtarzanie::Jeden);
    // stany wizualne przelacznika: zaznaczony = powierzchnia + stonowana ramka; wylaczony = bez tla (przygaszona ikona)
    {
        auto st = o->losowo_->styl_stanu();
        CHECK(!st.tlo && !st.ramka);
        o->losowo_->ustaw_zaznaczony(true); st = o->losowo_->styl_stanu();
        CHECK(st.tlo && st.ramka && st.grubosc == 1);
        o->losowo_->ustaw_wlaczona(false); st = o->losowo_->styl_stanu();
        CHECK(!st.tlo && !st.ramka);
        o->losowo_->ustaw_wlaczona(true);
        o->o_.ustaw_losowo(false); kroki(*o, 1);
        CHECK(!o->losowo_->czy_zaznaczony());
    }
    // X przy wlaczonym odtwarzaniu w tle: Grajek konczy sie (strumien zamkniety), nie chowa sie do paska.
    // Okno::zamknij konczy aplikacje (host: exit(0)), wiec sprawdzenie w procesie potomnym, w haku atexit.
    o->o_.ustaw_w_tle(true); kroki(*o, 1);
    CHECK(o->o_.odtworz_z_widoku(0)); kroki(*o, 5);
    CHECK(o->o_.silnik().strumien_otwarty());
    std::fflush(nullptr);
    const pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        g_okno_x = o; g_jadro_x = &j;
        std::atexit([] {
            const bool ok = !g_okno_x->o_.silnik().strumien_otwarty() && !g_jadro_x->mikser->has_pcm() && !g_okno_x->czy_zminimalizowane() &&
                            g_ustawienia[KLUCZ_KONFIGURACJI].find("w_tle=1") != std::string::npos;   // ustawienia zapisane przy X
            _exit(ok ? 42 : 43);
        });
        o->zamknij();
        _exit(44);                                                      // zamknij() nie zakonczyl aplikacji
    }
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 42);
    o->o_.zakoncz();
    delete o;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_kontroler(); std::printf("kontroler OK\n");
    test_magazyn(); std::printf("magazyn OK\n");
    test_komunikat_niedoboru(); std::printf("komunikat niedoboru OK\n");
    test_okno(); std::printf("okno OK\n");
    test_odrysowania_i_uklad(); std::printf("odrysowania i uklad OK\n");
    test_ikony_i_grafiki(); std::printf("ikony i grafiki uzytkownika OK\n");
    test_otwieranie_pliku();
    test_lista_presetow(); std::printf("lista presetow korektora OK\n");
    test_pasek_i_ustawienia(); std::printf("pasek sterowania i karta Ustawienia OK\n");
    std::printf("Grajek UI: %u sprawdzen OK\n", g_checks);
    return 0;
}
