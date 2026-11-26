// Grajek - regresja ciaglosci odtwarzania i przewijania (host, ASan/UBSan).
//
// Sprawdza architekture z audytu wydajnosci audio (docs/grajek.md, "Ciaglosc odtwarzania i przewijanie"):
//  1. mikser jadra: gleboka kolejka (BwsPcmFormat.reserved), PCM_PAUSE/RESUME, zawijanie bufora, sprzatanie slotu,
//  2. silnik: przestoj producenta (GUI, przeciaganie okna, Eksplorator, Przegladarka Obrazow - z punktu widzenia
//     Grajka kazdy z nich to okres bez pompowania) krotszy niz rezerwa nie daje niedoboru; dluzszy JEST liczony,
//  3. pauza natychmiastowa (bez dogrywania kolejki), wznowienie bez utraty i powtorzen probek,
//  4. przewijanie: zadne probki sprzed przewiniecia nie trafiaja do wyjscia (takze przy 512 ms kolejki),
//  5. scalanie zadan przewijania (najnowszy cel, jedno wykonanie na tick, anulowanie przy zmianie utworu),
//  6. MP3: przewijanie bez pelnego skanu pliku (ograniczony I/O), indeks przyrostowy, dokladnosc z indeksem,
//     przyblizenie wg TOC Xing / CBR, uszkodzone TOC, uszkodzone ramki, obciety plik, poczatek/srodek/koniec.
// Czasy z std::chrono to POMIARY HOSTA (dekoder na syntetycznych ramkach) - nie dowodza zachowania natywnego.
#include "grajek_srodowisko_testowe.h"
#include "programy/grajek/silnik.h"
#include "programy/grajek/kontroler.h"
#include "jadro/dzwiek/mikser_pcm.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>

using namespace Grajek;

static unsigned g_checks = 0;
#define CHECK(x) do { ++g_checks; if (!(x)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (0)

static uint64_t g_ns = 1000000000ULL;
static uint64_t zegar() { return g_ns; }

// "Sprzet" HDA: zuzywa ramki z miksera i zapisuje je (jak graj() srodowiska) - z przesunieciem zegara testu.
static void sprzet(uint32_t ramki) {
    graj(ramki);
    g_ns += uint64_t(ramki) * 1000000000ULL / 48000ULL;
}
// Tick petli Grajka: pompuj + 4 ms sprzetu.
static void tykaj(Silnik& s, int n) { for (int i = 0; i < n; ++i) { s.pompuj(); sprzet(192); } }

static size_t poczatek(const std::vector<int16_t>& w, size_t od = 0) {
    for (size_t i = od; i < w.size(); ++i) if (w[i]) return i;
    return w.size();
}

// Referencja: dekoder -> resampler (jak silnik, bez korektora), od ramki zrodla.
static std::vector<int16_t> referencja(const char* sc, FormatAudio f, uint64_t od) {
    std::unique_ptr<Dekoder> d(utworz_dekoder(f));
    CHECK(d->otworz(dostep(), sc));
    if (od) CHECK(d->szukaj(od));
    Resampler r; r.ustaw(d->parametry().czestotliwosc, d->parametry().kanaly);
    std::vector<int16_t> out, src(4096 * 2), buf(512 * 2);
    for (;;) {
        const uint32_t n = d->dekoduj(src.data(), 2048);
        if (!n) break;
        uint32_t poz = 0;
        while (poz < n) {
            uint32_t z = 0;
            const uint32_t o = r.przetworz(src.data() + size_t(poz) * d->parametry().kanaly, n - poz, &z, buf.data(), 512);
            out.insert(out.end(), buf.begin(), buf.begin() + o * 2);
            poz += z;
            if (!o && !z) break;
        }
    }
    return out;
}

// =================================================================== 1. mikser jadra
static void test_mikser() {
    CHECK(bws_pcm_pojemnosc_poprawna(0) && !bws_pcm_pojemnosc_poprawna(1) && !bws_pcm_pojemnosc_poprawna(4095));
    CHECK(bws_pcm_pojemnosc_poprawna(4096) && bws_pcm_pojemnosc_poprawna(BWS_PCM_DEEP_FRAMES));
    CHECK(!bws_pcm_pojemnosc_poprawna(BWS_PCM_DEEP_FRAMES + 1) && !bws_pcm_pojemnosc_poprawna(0xFFFFFFFFu));
    auto m = std::make_unique<Pcm::Mixer>();
    const Pcm::Owner a{10, 1}, b{11, 1};
    uint64_t ha = 0, hb = 0;
    CHECK(m->open(a, {48000, 2, 1}, &ha, 0, BWS_PCM_DEEP_FRAMES) == Pcm::OK);
    CHECK(m->open(b, {48000, 2, 1}, &hb, 0, BWS_PCM_DEEP_FRAMES) == Pcm::OK);
    Pcm::Status st{};
    CHECK(m->status(a, ha, &st) == Pcm::OK && st.capacity == Pcm::DEEP_CAPACITY_FRAMES);
    CHECK(m->status(b, hb, &st) == Pcm::OK && st.capacity == Pcm::CAPACITY_FRAMES);   // slot zajety: domyslna
    CHECK(m->close(a, ha) == Pcm::OK);
    uint64_t hc = 0;
    CHECK(m->open(a, {48000, 1, 1}, &hc, 0, BWS_PCM_DEEP_FRAMES) == Pcm::OK);         // slot zwolniony przez CLOSE
    CHECK(m->status(a, hc, &st) == Pcm::OK && st.capacity == Pcm::DEEP_CAPACITY_FRAMES);
    CHECK(m->cleanup(a) == 1);                                                          // zakonczenie procesu
    CHECK(m->open(a, {48000, 2, 1}, &ha, 0, BWS_PCM_DEEP_FRAMES) == Pcm::OK);
    CHECK(m->status(a, ha, &st) == Pcm::OK && st.capacity == Pcm::DEEP_CAPACITY_FRAMES);
    CHECK(m->close(b, hb) == Pcm::OK);

    // zawijanie: 10 obiegow bufora glebokiego, kazda ramka dokladnie raz i w kolejnosci, bez niedoboru
    uint32_t nast_zapis = 0, nast_odczyt = 0;
    int16_t blok[512 * 2];
    Pcm::Frame out[1024];
    while (nast_odczyt < 10 * Pcm::DEEP_CAPACITY_FRAMES) {
        for (int k = 0; k < 6; ++k) {
            for (uint32_t i = 0; i < 512; ++i) { blok[2 * i] = int16_t(nast_zapis + i); blok[2 * i + 1] = int16_t(~(nast_zapis + i)); }
            uint32_t acc = 0;
            if (m->write(a, ha, blok, 512, &acc, 1) != Pcm::OK) break;
            nast_zapis += acc;
            if (acc < 512) break;
        }
        CHECK(m->preview(out, 1024) == Pcm::OK);
        for (uint32_t i = 0; i < 1024; ++i) {
            CHECK(out[i].left == int16_t(nast_odczyt + i) && out[i].right == int16_t(~(nast_odczyt + i)));
        }
        CHECK(m->consume(1024, 1) == 0);
        nast_odczyt += 1024;
    }
    BwsPcmStats sx{};
    CHECK(m->statistics(a, ha, sx) == Pcm::OK && sx.starved == 0 && sx.max_queued == Pcm::DEEP_CAPACITY_FRAMES);

    // pauza: nic nie jest miksowane ani zuzywane, brak niedoboru, transport moze stanac (has_pcm == false)
    CHECK(m->status(a, ha, &st) == Pcm::OK);
    const uint32_t przed = st.queued;
    CHECK(przed > 1024);
    CHECK(m->pause(a, ha) == Pcm::OK);
    CHECK(!m->has_pcm() && !m->has_ready_pcm() && m->queue_levels().expecting == 0);
    for (int k = 0; k < 50; ++k) {
        CHECK(m->preview(out, 1024) == Pcm::OK);
        for (uint32_t i = 0; i < 1024; ++i) CHECK(out[i].left == 0 && out[i].right == 0);
        CHECK(m->consume(1024, 1) == 0);
    }
    CHECK(m->status(a, ha, &st) == Pcm::OK && st.queued == przed);
    CHECK(m->statistics(a, ha, sx) == Pcm::OK && sx.starved == 0);
    // dopelnienie w pauzie dozwolone, nadal nic nie jest oczekiwane
    for (uint32_t i = 0; i < 512; ++i) { blok[2 * i] = int16_t(nast_zapis + i); blok[2 * i + 1] = int16_t(~(nast_zapis + i)); }
    uint32_t acc = 0;
    CHECK(m->write(a, ha, blok, 512, &acc, 1) == Pcm::OK && acc == 512); nast_zapis += 512;
    CHECK(m->queue_levels().expecting == 0);
    CHECK(m->resume(a, ha) == Pcm::OK && m->has_ready_pcm());
    CHECK(m->preview(out, 1024) == Pcm::OK && out[0].left == int16_t(nast_odczyt));   // ta sama ramka co przed pauza
    // FINISH -> PAUSE -> RESUME: dogrywanie, wyczerpanie kolejki nie jest niedoborem
    CHECK(m->finish(a, ha) == Pcm::OK && m->pause(a, ha) == Pcm::OK && m->resume(a, ha) == Pcm::OK);
    for (int k = 0; k < 40; ++k) (void)m->consume(1024, 1);
    CHECK(m->statistics(a, ha, sx) == Pcm::OK && sx.starved == 0);
    // nieznany uchwyt / cudzy wlasciciel
    CHECK(m->pause(b, ha) == Pcm::BAD_HANDLE && m->resume(a, 999) == Pcm::BAD_HANDLE);
    std::printf("mikser: gleboka kolejka, zawijanie, PAUSE/RESUME OK\n");
}

// =================================================================== 2-4. silnik
static void dodaj_wav(const char* sc, uint32_t sekundy) {
    GrajekFixtures::ParametryWav p; p.czestotliwosc = 48000; p.ramki = 48000 * sekundy; p.amplituda = 0.4;
    dodaj_plik(sc, GrajekFixtures::wav(p));
}

// Przestoj producenta `ms` (Grajek nie dostaje CPU albo czeka w wywolaniu systemowym), sprzet gra dalej.
static void przestoj(uint32_t ms) { sprzet(ms * 48); }

static uint64_t niedobor_po_przestoju(bool rozszerzenia, uint32_t ms, uint32_t* pojemnosc, bool* ciagle) {
    Jadro j; j.rozszerzenia = rozszerzenia; g_j = &j;
    Silnik s; s.konfiguruj(dostep(), fake_bws); s.ustaw_zegar(zegar); s.ustaw_glosnosc(100, false);
    CHECK(s.wczytaj("/dane/muzyka/d.wav", FormatAudio::WAV) && s.odtwarzaj());
    tykaj(s, 400);                                    // stan ustalony (kolejka pelna)
    przestoj(ms);
    tykaj(s, 300);
    *pojemnosc = s.pomiary().pojemnosc_kolejki;
    if (ciagle) {
        const auto ref = referencja("/dane/muzyka/d.wav", FormatAudio::WAV, 0);
        const size_t st = poczatek(j.wyjscie);
        size_t i = st, r = poczatek(ref);
        while (i < j.wyjscie.size() && r < ref.size() && j.wyjscie[i] == ref[r]) { ++i; ++r; }
        *ciagle = i == j.wyjscie.size();               // kazda probka zagrana raz, w kolejnosci, bez dziur
    }
    const uint64_t n = s.niedobory_ramek();
    if (rozszerzenia && ms > 520) CHECK(s.pomiary().przerwy_ponad_rezerwe >= 1);
    CHECK(s.pomiary().przerwa_producenta.maks_ns() >= uint64_t(ms) * 1000000ULL);
    s.zwolnij();
    g_j = nullptr;
    return n;
}

static void test_rezerwa() {
    wyczysc_fs();
    dodaj_wav("/dane/muzyka/d.wav", 12);
    uint32_t poj = 0; bool ciagle = false;
    // stare jadro (85 ms): przestoj 342 ms (wartosc ze zrzutu ekranu uzytkownika) = niedobor - odtworzenie defektu
    const uint64_t stare = niedobor_po_przestoju(false, 342, &poj, nullptr);
    CHECK(poj == Pcm::CAPACITY_FRAMES && stare > 0);
    // obecne jadro (512 ms): te same 342 ms, a takze 450 ms - bez niedoboru i bez dziur
    for (uint32_t ms : {100u, 342u, 450u}) {
        CHECK(niedobor_po_przestoju(true, ms, &poj, &ciagle) == 0);
        CHECK(poj == Pcm::DEEP_CAPACITY_FRAMES && ciagle);
    }
    // przestoj dluzszy niz rezerwa: niedobor JEST zgloszony (diagnostyka nie ukrywa prawdziwych przerw)
    const uint64_t dlugi = niedobor_po_przestoju(true, 700, &poj, nullptr);
    CHECK(dlugi > 0 && dlugi <= (700 - 400) * 48);    // ~ (700 ms - rezerwa) x 48 ramek/ms
    std::printf("silnik: przestoj 342 ms -> stare jadro %llu ramek niedoboru, gleboka kolejka 0; 700 ms -> %llu (zgloszone)\n",
                (unsigned long long)stare, (unsigned long long)dlugi);
}

static void test_pauza_i_przewijanie() {
    wyczysc_fs();
    dodaj_wav("/dane/muzyka/p.wav", 8);
    Jadro j; j.rozszerzenia = true; g_j = &j;
    Silnik s; s.konfiguruj(dostep(), fake_bws); s.ustaw_zegar(zegar); s.ustaw_glosnosc(100, false);
    CHECK(s.wczytaj("/dane/muzyka/p.wav", FormatAudio::WAV) && s.odtwarzaj());
    tykaj(s, 200);
    CHECK(s.w_kolejce() > 8192);                        // gleboka kolejka wypelniona z wyprzedzeniem
    // pauza: natychmiastowa (pozycja stoi od razu - bez ~85-512 ms dogrywania), bez FINISH
    const uint64_t fin = j.finish;
    CHECK(s.pauza() && j.pauzy == 1 && j.finish == fin && s.pomiary().pauza_w_jadrze);
    const uint64_t poz = s.pozycja_ms();
    const size_t wyj_pauzy = j.wyjscie.size();
    tykaj(s, 150);
    CHECK(s.pozycja_ms() == poz && s.strumien_otwarty());
    for (size_t i = wyj_pauzy; i < j.wyjscie.size(); ++i) CHECK(j.wyjscie[i] == 0);   // w pauzie nic nie gra
    CHECK(s.wznow() && j.wznowienia == 1);
    tykaj(s, 400);
    CHECK(s.niedobory_ramek() == 0 && s.pomiary().pauzy == 1);
    {   // ciaglosc: po usunieciu ciszy pauzy wyjscie == referencja od poczatku (bez utraty i powtorzen)
        const auto ref = referencja("/dane/muzyka/p.wav", FormatAudio::WAV, 0);
        std::vector<int16_t> bez;
        size_t i = poczatek(j.wyjscie);
        for (; i + 1 < j.wyjscie.size(); i += 2) {
            if (i >= wyj_pauzy && i < wyj_pauzy + 150 * 192 * 2 && j.wyjscie[i] == 0 && j.wyjscie[i + 1] == 0) continue;
            bez.push_back(j.wyjscie[i]); bez.push_back(j.wyjscie[i + 1]);
        }
        const size_t r0 = poczatek(ref);
        CHECK(bez.size() > 100000);
        CHECK(std::equal(bez.begin(), bez.end(), ref.begin() + long(r0)));
    }
    // przewijanie przy pelnej (512 ms) kolejce: pierwsze probki po przewinieciu == dekodowanie od celu
    tykaj(s, 100);
    CHECK(s.w_kolejce() > 16384);
    const size_t przed = j.wyjscie.size();
    const uint64_t zamk = j.zamkniecia;
    CHECK(s.przewin(5000) && j.zamkniecia == zamk + 1 && s.pozycja_ms() == 5000);
    tykaj(s, 120);
    {
        const auto ref = referencja("/dane/muzyka/p.wav", FormatAudio::WAV, 5000ULL * 48);
        const size_t st = poczatek(j.wyjscie, przed);
        CHECK(st < j.wyjscie.size());
        for (size_t k = 0; k < 20000 && st + k < j.wyjscie.size(); ++k) CHECK(j.wyjscie[st + k] == ref[poczatek(ref) + k]);
    }
    CHECK(s.niedobory_ramek() == 0 && s.pomiary().przewiniecia == 1 && s.pomiary().wznowienie.liczba() == 1);
    // przewijanie w pauzie: pozostaje pauza, nic nie gra, wznowienie startuje od nowej pozycji
    CHECK(s.pauza() && s.przewin(1000) && s.stan() == Stan::Pauza && s.pozycja_ms() == 1000);
    const size_t w_pauzie = j.wyjscie.size();
    tykaj(s, 50);
    for (size_t i = w_pauzie; i < j.wyjscie.size(); ++i) CHECK(j.wyjscie[i] == 0);
    CHECK(s.wznow());
    tykaj(s, 60);
    CHECK(s.pozycja_ms() > 1000 && s.pozycja_ms() < 1400);
    // przewijanie przy koncu i za koniec: bez awarii, koniec utworu
    CHECK(s.przewin(7990));
    for (int i = 0; i < 600 && s.stan() == Stan::Odtwarzanie; ++i) tykaj(s, 1);
    CHECK(s.stan() == Stan::Zatrzymany && (s.zdarzenia() & ZD_KONIEC_UTWORU));
    CHECK(s.niedobory_ramek() == 0);
    s.zwolnij();
    // zamkniecie w trakcie odtwarzania: strumien zamkniety, slot glebokiej kolejki zwolniony
    {
        Silnik t; t.konfiguruj(dostep(), fake_bws); t.ustaw_zegar(zegar);
        CHECK(t.wczytaj("/dane/muzyka/p.wav", FormatAudio::WAV) && t.odtwarzaj());
        tykaj(t, 20);
        CHECK(t.strumien_otwarty());
        t.zwolnij();
        CHECK(!t.strumien_otwarty());
        uint64_t h = 0;
        CHECK(j.mikser->open({1, 1}, {48000, 2, 1}, &h, 0, BWS_PCM_DEEP_FRAMES) == Pcm::OK);
        Pcm::Status st{}; CHECK(j.mikser->status({1, 1}, h, &st) == Pcm::OK && st.capacity == Pcm::DEEP_CAPACITY_FRAMES);
        CHECK(j.mikser->close({1, 1}, h) == Pcm::OK);
    }
    g_j = nullptr;
    std::printf("silnik: pauza natychmiastowa, wznowienie ciagle, przewijanie bez starych probek OK\n");
}

// =================================================================== 5. scalanie zadan przewijania
static void test_scalanie() {
    wyczysc_fs();
    dodaj_wav("/dane/muzyka/s1.wav", 6);
    dodaj_wav("/dane/muzyka/s2.wav", 6);
    Jadro j; j.rozszerzenia = true; g_j = &j;
    Platforma p; p.pliki = dostep(); p.listuj = fs_listuj; p.bws = fake_bws; p.czas_ns = zegar;
    auto o = std::make_unique<Odtwarzacz>();
    o->konfiguruj(p);
    o->start();
    for (int i = 0; i < 200 && o->biblioteka().skanuje(); ++i) { g_ns += 4000000; o->tick(); }
    CHECK(o->biblioteka().liczba() == 2);
    CHECK(o->odtworz_z_widoku(0));
    for (int i = 0; i < 50; ++i) { g_ns += 4000000; o->tick(); sprzet(192); }
    CHECK(o->silnik().stan() == Stan::Odtwarzanie);
    // 30 zdarzen klawiatury w jednym ticku: jedno wykonanie, najnowszy cel
    const uint64_t zamk = j.zamkniecia;
    for (int k = 1; k <= 30; ++k) o->zadaj_przewiniecia(uint64_t(k) * 100);
    CHECK(o->przewiniecie_oczekuje() && o->cel_przewiniecia_ms() == 3000 && j.zamkniecia == zamk);
    g_ns += 4000000; o->tick();
    CHECK(!o->przewiniecie_oczekuje() && o->wykonane_przewiniecia() == 1 && o->zadania_przewiniecia() == 30);
    CHECK(j.zamkniecia == zamk + 1 && o->silnik().pozycja_ms() == 3000);
    CHECK(o->przygotowanie_przewiniecia().liczba() == 1 && o->przygotowanie_przewiniecia().maks_ns() <= 4000000);
    // zmiana utworu anuluje oczekujace przewiniecie (cel dotyczyl poprzedniego utworu)
    o->zadaj_przewiniecia(4500);
    CHECK(o->nastepny());
    for (int i = 0; i < 5; ++i) { g_ns += 4000000; o->tick(); sprzet(192); }
    CHECK(o->wykonane_przewiniecia() == 1 && o->silnik().pozycja_ms() < 200);
    // stop anuluje
    o->zadaj_przewiniecia(2000); CHECK(o->stop() && !o->przewiniecie_oczekuje());
    o->zakoncz();
    o.reset();
    g_j = nullptr;
    std::printf("scalanie przewijania: 30 zadan -> 1 wykonanie, anulowanie przy zmianie utworu i stopie OK\n");
}

// =================================================================== 6. MP3
using GrajekFixtures::ParametryMp3;

// Plik z segmentami o roznych tonach (rozpoznanie segmentu = kontrola dokladnosci przyblizonego skoku).
struct PlikMp3 { Bajty b; std::vector<uint64_t> offsety; uint32_t ramek; uint32_t linie[4]; };
static PlikMp3 segmenty(uint32_t ramek_na_segment, bool vbr, int xing /*0 brak, 1 TOC ok, 2 TOC malejaca, 3 bajty>plik*/) {
    PlikMp3 w{}; w.ramek = 4 * ramek_na_segment;
    const uint32_t linie[4] = {12, 24, 40, 56};
    Bajty audio;
    for (int sgm = 0; sgm < 4; ++sgm) {
        w.linie[sgm] = linie[sgm];
        ParametryMp3 p; p.ramki = 1; p.linia_l = int(linie[sgm]); p.linia_p = int(linie[sgm]);
        for (uint32_t i = 0; i < ramek_na_segment; ++i) {
            const int idx = vbr && (i % 3 == 1) ? 11 : 9;
            w.offsety.push_back(audio.size());
            Bajty r = GrajekFixtures::ramka_mp3(p, idx, false);
            audio.insert(audio.end(), r.begin(), r.end());
        }
    }
    if (xing) {
        Bajty x; GrajekFixtures::tekst(x, "Xing");
        GrajekFixtures::be32(x, 7); GrajekFixtures::be32(x, w.ramek);
        GrajekFixtures::be32(x, xing == 3 ? uint32_t(audio.size() * 4) : uint32_t(audio.size()));
        for (int i = 0; i < 100; ++i) {
            const uint64_t f = uint64_t(i) * w.ramek / 100;
            uint8_t v = uint8_t(w.offsety[f] * 256 / audio.size());
            if (xing == 2) v = uint8_t(255 - i);                   // malejaca: musi zostac odrzucona
            x.push_back(v);
        }
        ParametryMp3 p;
        Bajty r = GrajekFixtures::ramka_mp3(p, 9, true, &x);
        w.b = r;
    }
    const size_t baza = w.b.size();
    for (auto& o : w.offsety) o += baza;
    w.b.insert(w.b.end(), audio.begin(), audio.end());
    return w;
}
static double moc(const std::vector<int16_t>& x, uint32_t linia) {
    // pasmo linii k ~ k * 48000 / 1152 Hz; prosty Goertzel na kanale L (stereo)
    const double f = linia * 48000.0 / 1152.0, w = 2 * 3.14159265358979 * f / 48000.0, c = 2 * std::cos(w);
    double s1 = 0, s2 = 0;
    const size_t n = std::min<size_t>(x.size() / 2, 4608);
    for (size_t i = 0; i < n; ++i) { const double s0 = x[2 * i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / double(n ? n : 1);
}
static uint32_t segment_dzwieku(Dekoder& d, const PlikMp3& w) {
    std::vector<int16_t> b(4608 * 2);
    uint32_t got = 0;
    while (got < 4608) { const uint32_t n = d.dekoduj(b.data() + got * 2, 4608 - got); if (!n) break; got += n; }
    b.resize(got * 2);
    uint32_t najl = 0; double m = -1;
    for (uint32_t k = 0; k < 4; ++k) { const double v = moc(b, w.linie[k]); if (v > m) { m = v; najl = k; } }
    return najl;
}

static void test_mp3_przewijanie() {
    wyczysc_fs();
    std::vector<double> czasy_ms;
    // (a) duzy plik CBR bez Xing (4 x 4000 ramek = 16000 ramek, ~6,1 MB, ~6 min): pierwsze przewiniecie daleko za
    //     zasiegiem synchronicznego indeksu -> przyblizone, z ograniczonym I/O; segment poprawny
    {
        const PlikMp3 w = segmenty(4000, false, 0);
        dodaj_plik("/m/cbr.mp3", w.b);
        std::unique_ptr<Dekoder> d(utworz_dekoder(FormatAudio::MP3));
        CHECK(d->otworz(dostep(), "/m/cbr.mp3"));
        for (uint32_t sgm : {3u, 2u, 1u}) {
            const uint64_t cel = (uint64_t(sgm) * 4000 + 2000) * 1152;
            g_bajty_odczytane = 0;
            const auto t0 = std::chrono::steady_clock::now();
            CHECK(d->szukaj(cel));
            czasy_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            CHECK(d->pozycja() == cel);
            CHECK(g_bajty_odczytane < w.b.size() / 2);          // nigdy pelny skan pliku
            CHECK(segment_dzwieku(*d, w) == sgm);
        }
        CHECK(d->ostatnie_szukanie() == Dekoder::Szukanie::Dokladne || d->ostatnie_szukanie() == Dekoder::Szukanie::Przyblizone);
        // indeks w tle: ograniczone porcje, az do kompletu; potem kazde przewiniecie dokladne
        int kroki = 0;
        while (d->praca_w_tle(64) && kroki < 100000) ++kroki;
        CHECK(d->indeks_kompletny() && kroki < 1000);
        // dokladnosc z pelnym indeksem: probki == sekwencyjne dekodowanie (okna w kilku miejscach)
        std::vector<uint64_t> cele = {1, 1151, 1152, 4000ULL * 1152 + 7, 9999ULL * 1152 + 600, 15990ULL * 1152};
        std::mt19937 r(5);
        for (int i = 0; i < 6; ++i) cele.push_back(r() % (16000ULL * 1152 - 2000));
        std::sort(cele.begin(), cele.end());
        // referencja: JEDEN przebieg dekodowania od poczatku, z ktorego wycinane sa okna [cel, cel + 600)
        std::vector<std::vector<int16_t>> okna(cele.size());
        {
            std::unique_ptr<Dekoder> ref(utworz_dekoder(FormatAudio::MP3));
            CHECK(ref->otworz(dostep(), "/m/cbr.mp3"));
            std::vector<int16_t> bufor(4096 * 2);
            uint64_t poz = 0;
            for (;;) {
                const uint32_t n = ref->dekoduj(bufor.data(), 4096);
                if (!n) break;
                for (size_t k = 0; k < cele.size(); ++k) {
                    const uint64_t a = cele[k], b = cele[k] + 600;
                    const uint64_t od = std::max(a, poz), doo = std::min(b, poz + n);
                    for (uint64_t f = od; f < doo; ++f) {
                        okna[k].push_back(bufor[(f - poz) * 2]); okna[k].push_back(bufor[(f - poz) * 2 + 1]);
                    }
                }
                poz += n;
            }
        }
        for (size_t k = 0; k < cele.size(); ++k) {
            const uint64_t cel = cele[k];
            CHECK(okna[k].size() == 1200);
            const auto t0 = std::chrono::steady_clock::now();
            CHECK(d->szukaj(cel) && d->pozycja() == cel && d->ostatnie_szukanie() == Dekoder::Szukanie::Dokladne);
            czasy_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            std::vector<int16_t> b(1200);
            CHECK(d->dekoduj(b.data(), 600) == 600);
            CHECK(std::equal(b.begin(), b.end(), okna[k].begin()));
        }
        // szybka seria przewiniec (przeciaganie/klawiatura bez scalania): ograniczony koszt kazdego
        g_bajty_odczytane = 0;
        for (int i = 0; i < 200; ++i) CHECK(d->szukaj(r() % (16000ULL * 1152)));
        CHECK(g_bajty_odczytane < uint64_t(200) * 256 * 1024);
    }
    // (b) VBR z Xing i poprawna TOC: przyblizenie wg TOC trafia w segment
    {
        const PlikMp3 w = segmenty(4000, true, 1);
        dodaj_plik("/m/vbr.mp3", w.b);
        Czytnik c; CHECK(c.otworz(dostep(), "/m/vbr.mp3"));
        Mp3::Info mi; CHECK(Mp3::analizuj(c, mi, nullptr, nullptr, 0) == Mp3::Blad::OK && mi.ma_toc && mi.bajty_xing && mi.vbr);
        std::unique_ptr<Dekoder> d(utworz_dekoder(FormatAudio::MP3));
        CHECK(d->otworz(dostep(), "/m/vbr.mp3") && d->parametry().ramki == 16000ULL * 1152);
        for (uint32_t sgm : {3u, 1u, 2u, 0u}) {
            const uint64_t cel = (uint64_t(sgm) * 4000 + 2000) * 1152;
            CHECK(d->szukaj(cel) && d->pozycja() == cel);
            CHECK(segment_dzwieku(*d, w) == sgm);
        }
    }
    // (c) uszkodzone naglowki Xing: TOC malejaca odrzucona, dlugosc strumienia > plik odrzucona; przewijanie dziala
    {
        const PlikMp3 w = segmenty(4000, true, 2);
        dodaj_plik("/m/zla_toc.mp3", w.b);
        Czytnik c; CHECK(c.otworz(dostep(), "/m/zla_toc.mp3"));
        Mp3::Info mi; CHECK(Mp3::analizuj(c, mi, nullptr, nullptr, 0) == Mp3::Blad::OK && !mi.ma_toc);
        std::unique_ptr<Dekoder> d(utworz_dekoder(FormatAudio::MP3));
        CHECK(d->otworz(dostep(), "/m/zla_toc.mp3"));
        CHECK(d->szukaj(3ULL * 4000 * 1152 + 1000) && segment_dzwieku(*d, w) == 3);
        const PlikMp3 w3 = segmenty(4000, false, 3);
        dodaj_plik("/m/zle_bajty.mp3", w3.b);
        Czytnik c3; CHECK(c3.otworz(dostep(), "/m/zle_bajty.mp3"));
        Mp3::Info m3; CHECK(Mp3::analizuj(c3, m3, nullptr, nullptr, 0) == Mp3::Blad::OK && m3.bajty_xing == 0);
        std::unique_ptr<Dekoder> e(utworz_dekoder(FormatAudio::MP3));
        CHECK(e->otworz(dostep(), "/m/zle_bajty.mp3"));
        CHECK(e->szukaj(2ULL * 4000 * 1152 + 500) && segment_dzwieku(*e, w3) == 2);
    }
    // (d) uszkodzone ramki w srodku i obciety plik: przewijanie w uszkodzony obszar i na koniec bez awarii i petli
    {
        PlikMp3 w = segmenty(2500, false, 0);
        std::mt19937 r(9);
        for (size_t i = w.b.size() / 2; i < w.b.size() / 2 + 200000; ++i) w.b[i] = uint8_t(r());
        w.b.resize(w.b.size() - 777);
        dodaj_plik("/m/uszk.mp3", w.b);
        std::unique_ptr<Dekoder> d(utworz_dekoder(FormatAudio::MP3));
        CHECK(d->otworz(dostep(), "/m/uszk.mp3"));
        std::vector<int16_t> b(4096 * 2);
        for (uint64_t cel : {uint64_t(5000) * 1152, uint64_t(5300) * 1152, uint64_t(9990) * 1152, uint64_t(9999) * 1152,
                             d->parametry().ramki ? d->parametry().ramki - 1 : 1, uint64_t(3)}) {
            if (d->szukaj(cel)) {
                for (int k = 0; k < 40; ++k) if (!d->dekoduj(b.data(), 4096)) break;
            } else {
                CHECK(d->ma_blad());
                d.reset(utworz_dekoder(FormatAudio::MP3));
                CHECK(d->otworz(dostep(), "/m/uszk.mp3"));
            }
        }
        int kroki = 0;
        while (d->praca_w_tle(64) && kroki < 100000) ++kroki;
        CHECK(kroki < 2000);                               // indeks konczy sie mimo smieci (ograniczona praca)
    }
    // (e) krotki plik: cel w pierwszej ramce, poczatek, koniec
    {
        ParametryMp3 p; p.ramki = 12;
        dodaj_plik("/m/krotki.mp3", GrajekFixtures::mp3(p));
        std::unique_ptr<Dekoder> d(utworz_dekoder(FormatAudio::MP3));
        CHECK(d->otworz(dostep(), "/m/krotki.mp3"));
        for (uint64_t cel : {uint64_t(0), uint64_t(5), uint64_t(1100), uint64_t(12 * 1152 - 1)}) CHECK(d->szukaj(cel) && d->pozycja() == cel);
    }
    std::sort(czasy_ms.begin(), czasy_ms.end());
    const auto pct = [&](double p) { return czasy_ms[std::min(czasy_ms.size() - 1, size_t(p * double(czasy_ms.size() - 1) + 0.5))]; };
    std::printf("mp3: przewijanie (host, %zu prob): mediana %.2f ms, p95 %.2f ms, maks %.2f ms\n",
                czasy_ms.size(), pct(0.5), pct(0.95), czasy_ms.back());
}

// =================================================================== 7. silnik MP3: przewijanie przy odtwarzaniu
static void test_silnik_mp3() {
    wyczysc_fs();
    const PlikMp3 w = segmenty(3000, false, 0);
    dodaj_plik("/dane/muzyka/s.mp3", w.b);
    Jadro j; j.rozszerzenia = true; g_j = &j;
    Silnik s; s.konfiguruj(dostep(), fake_bws); s.ustaw_zegar(zegar); s.ustaw_glosnosc(100, false);
    CHECK(s.wczytaj("/dane/muzyka/s.mp3", FormatAudio::MP3) && s.odtwarzaj());
    tykaj(s, 300);
    // w tle buduje sie indeks (tylko przy pelnej kolejce), bez niedoboru
    CHECK(s.dekoder()->zaindeksowane_ramki() > 0 && s.niedobory_ramek() == 0);
    // przewiniecia w kazdy segment przy odtwarzaniu: brak niedoboru, wznowienie mierzone, brak starych probek
    for (uint32_t sgm : {3u, 0u, 2u, 1u}) {
        const size_t przed = j.wyjscie.size();
        CHECK(s.przewin((uint64_t(sgm) * 3000 + 1500) * 1152 * 1000 / 48000));
        tykaj(s, 150);
        std::vector<int16_t> po(j.wyjscie.begin() + long(poczatek(j.wyjscie, przed)), j.wyjscie.end());
        CHECK(po.size() > 9216);
        uint32_t najl = 0; double m = -1;
        for (uint32_t k = 0; k < 4; ++k) { const double v = moc(po, w.linie[k]); if (v > m) { m = v; najl = k; } }
        CHECK(najl == sgm);
    }
    for (int i = 0; i < 4000 && !s.dekoder()->indeks_kompletny(); ++i) tykaj(s, 1);
    CHECK(s.dekoder()->indeks_kompletny());
    CHECK(s.niedobory_ramek() == 0);
    CHECK(s.pomiary().przewiniecia == 4 && s.pomiary().wznowienie.liczba() == 4 && s.pomiary().indeks.liczba() > 0);
    std::printf("silnik MP3: przewijanie w trakcie odtwarzania (wznowienie p95 %.1f ms czasu testu), indeks w tle OK\n",
                double(s.pomiary().wznowienie.percentyl_ns(950)) / 1e6);
    s.zwolnij();
    g_j = nullptr;
}

static void test_histogram() {
    Histogram h;
    CHECK(h.percentyl_ns(950) == 0 && h.min_ns() == 0);
    for (int i = 1; i <= 100; ++i) h.dodaj(uint64_t(i) * 1000000ULL);   // 1..100 ms
    CHECK(h.liczba() == 100 && h.min_ns() == 1000000ULL && h.maks_ns() == 100000000ULL && h.srednia_ns() == 50500000ULL);
    const uint64_t p50 = h.percentyl_ns(500), p95 = h.percentyl_ns(950);
    CHECK(p50 >= 50000000ULL && p50 <= 2 * 50000000ULL + 2000000ULL);   // gorna granica kubelka (<= 2x)
    CHECK(p95 >= 95000000ULL && p95 <= 100000000ULL);                    // ograniczona maksimum
    h.dodaj(UINT64_MAX / 2); CHECK(h.maks_ns() == UINT64_MAX / 2);
    std::printf("histogram OK\n");
}

int main() {
    test_histogram();
    test_mikser();
    test_rezerwa();
    test_pauza_i_przewijanie();
    test_scalanie();
    test_mp3_przewijanie();
    test_silnik_mp3();
    std::printf("Grajek ciaglosc: %u sprawdzen OK\n", g_checks);
    return 0;
}
