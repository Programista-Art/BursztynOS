// Grajek 1.0 - uklad okna z PRAWDZIWYMI czcionkami TTF (BSF + DejaVu Sans, -DBSF_NATIVE=1): te same metryki co
// natywny BursztynUI. Defekty zgloszone z natywnego QEMU (przyciety "j" w "Grajek", stykajace sie "125/250/500",
// ucieta linia opisu korektora, obciete naglowki biblioteki przy 1080 px, "Wyczyść", "Wizualizacja" na
// "Diagnostyka" przy minimalnym oknie, przycieta podpowiedz, skok sterowania po wlaczeniu diagnostyki) nie byly
// widoczne w testach z czcionka bitmapowa hosta. Rozmiary: minimalny, domyslny (1080 px przy pulpicie 1280x720),
// zmaksymalizowany 1280x680, 1920x1040 i 150 %.
#define BSF_NATIVE 1
#include "../bursztyn_ui/bursztyn_ui.h"
#include "bursztyn_ui_host_fixture.h"
#include "bsf_ttf_host_fixture.h"
#include "grajek_srodowisko_testowe.h"
#include <cstdio>
#include <string>

#define private public
#include "../programy/grajek/okno.h"
#undef private

using namespace Grajek;
static unsigned g_checks = 0;
#define CHECK(x) do { ++g_checks; if (!(x)) { std::fprintf(stderr, "FAIL test_grajek_ui_ttf_host.cpp:%d: %s\n", __LINE__, #x); std::abort(); } } while (0)
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
static bool ust_zapisz(const char* k, const char* v) { g_ustawienia[k] = v; return true; }
static uint64_t czas() { return test_monotonic_ns; }

struct Licznik { int odrysowania = 0; uint64_t piksele = 0; };
static Licznik petla(OknoGrajka& o, int ticki) {
    Licznik l;
    for (int i = 0; i < ticki; ++i) {
        test_monotonic_ns += 4000000ULL; o.tick(); graj(192);
        if (!o.czy_zminimalizowane() && o.czy_wymaga_odrysowania()) {
            const uint64_t p0 = o.liczba_brudnych_pikseli();
            KontekstRysowania c; o.rysuj(c); ++l.odrysowania; l.piksele += o.liczba_brudnych_pikseli() - p0;
        }
    }
    return l;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    CHECK(zaladuj_ttf());
    CHECK(BSF::ready());
    // TTF ma inne metryki niz bitmapowa czcionka hosta (16 px) - inaczej test nie sprawdzalby niczego nowego
    std::printf("BSF: rozmiar UI %d, wysokosc linii %d px, \"Wykonawca\" %d px, \"125\" %d px\n", BSF::ui_size(),
                CzcionkaUI::pobierz_wysokosc(1), CzcionkaUI::zmierz_tekst("Wykonawca", 1), CzcionkaUI::zmierz_tekst("125", 1));
    CHECK(CzcionkaUI::pobierz_wysokosc(1) != 16);

    Jadro j; g_j = &j;
    wyczysc_fs();
    using namespace GrajekFixtures;
    ParametryWav w; w.czestotliwosc = 48000; w.ramki = 48000 * 30;
    dodaj_plik("/dane/muzyka/Electric Pulse.wav", wav(w));
    {
        ParametryMp3 m; m.ramki = 1300;
        Bajty t = id3v2(4, {tekst_utf8("TIT2", "Bursztyn OS 2 – bardzo długi tytuł utworu do sprawdzenia przycinania w TTF"), tekst_utf8("TPE1", "cuttingedgesongs550")});
        Bajty a = mp3(m); t.insert(t.end(), a.begin(), a.end());
        dodaj_plik("/dane/muzyka/Bursztyn OS 2.mp3", t);
    }
    g_ustawienia[KLUCZ_KONFIGURACJI] = "wizualizacja=1\nkorektor=1\n";
    auto* o = new OknoGrajka;
    Platforma p;
    p.pliki.odczyt = fs_odczyt; p.pliki.rozmiar = fs_rozmiar; p.listuj = fs_listuj; p.bws = fake_bws;
    p.wczytaj_ustawienie = ust_wczytaj; p.zapisz_ustawienie = ust_zapisz; p.czas_ns = czas;
    o->init(p);
    o->ustaw_geometrie(0, 0, 1080, 640); o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    petla(*o, 40);
    CHECK(o->o_.biblioteka().widoczne() == 2);
    CHECK(o->o_.odtworz_z_widoku(0));
    petla(*o, 100);
    CHECK(o->o_.silnik().stan() == Stan::Odtwarzanie);

    // uklad przy wszystkich rozmiarach (z diagnostyka wylaczona i wlaczona - podsumowanie w pasku)
    struct R { int w, h, skala; } rozmiary[] = {{OknoGrajka::MIN_SZEROKOSC, OknoGrajka::MIN_WYSOKOSC, 100}, {1080, 640, 100}, {1280, 680, 100}, {1280, 720, 100}, {1920, 1040, 100}, {1920, 1080, 100}, {OknoGrajka::MIN_SZEROKOSC * 3 / 2, 870, 150}};
    for (int d = 0; d < 2; ++d) {
        o->o_.ustaw_diagnostyke(d == 1);
        petla(*o, 300);
        for (const R& r : rozmiary) GrajekUklad::sprawdz_uklad(o, r.w, r.h, r.skala);
    }
    std::printf("uklad TTF (7 rozmiarow x 2 stany diagnostyki, 4 karty) OK\n");

    // domyslny rozmiar: odtwarzanie z wizualizacja - bez pelnych odrysowan i przeliczania ukladu
    o->ustaw_skale_dpi(SkalaUI(1)); o->ustaw_geometrie(0, 0, 1080, 640); o->przelicz_uklad();
    { KontekstRysowania c; o->rysuj(c, true); }
    petla(*o, 10);
    GrajekUklad::sprawdz_stabilnosc_diagnostyki(o, [&](int n) { petla(*o, n); });
    const uint64_t pelne0 = o->liczba_pelnych_odrysowan(), uklad0 = o->liczba_przejsc_ukladu();
    const Licznik a = petla(*o, 2500);
    CHECK(o->liczba_pelnych_odrysowan() == pelne0 && o->liczba_przejsc_ukladu() == uklad0);
    CHECK(a.odrysowania >= 10 && a.piksele / uint64_t(a.odrysowania) < uint64_t(1080) * 640 / 10);
    std::printf("10 s z wizualizacja (TTF, 1080x640): %d odrysowan, sr. %llu px\n", a.odrysowania, (unsigned long long)(a.piksele / uint64_t(a.odrysowania)));
    // tlo: zminimalizowane okno - zero odrysowan, PCM bez niedoboru
    o->o_.ustaw_w_tle(true);
    o->minimalizuj();
    const Licznik e = petla(*o, 500);
    CHECK(e.odrysowania == 0 && o->o_.silnik().stan() == Stan::Odtwarzanie && o->o_.silnik().niedobory_ramek() == 0);

    // podpowiedzi BursztynUI w TTF (100 % i 150 %)
    GrajekUklad::sprawdz_podpowiedz(SkalaUI(1));
    GrajekUklad::sprawdz_podpowiedz(SkalaUI::procent(150));
    // podpowiedzi Grajka miesza sie w buforze Kontrolki (63 B) bez obcinania
    Kontrolka* z_podpowiedzia[] = {o->odswiez_, o->dodaj_kol_, o->poprz_, o->graj_, o->stop_, o->nast_, o->wycisz_,
                                   o->glosnosc_, o->losowo_, o->powtarzanie_, o->w_tle_, o->wizualizacja_, o->diagnostyka_,
                                   o->korektor_, o->preset_, o->podkatalogi_};
    for (Kontrolka* k : z_podpowiedzia) CHECK(k->podpowiedz()[0] && std::strlen(k->podpowiedz()) < 63);

    o->o_.zakoncz();
    delete o;
    std::printf("Grajek UI (TTF): %u sprawdzen OK\n", g_checks);
    return 0;
}
