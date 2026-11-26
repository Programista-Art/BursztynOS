// Host: instalacja Bursztyn OS na jednorazowych obrazach z RZECZYWISTYM manifestem instalacji
// (narzedzia/instalator/manifest_instalacji.py: te same pliki, sciezki i bajty co plan jadra) i produkcyjnym
// kodem jadra: PSF/BSP2 + GPT (jadro/fs/psf.cpp, storage_format.cpp) oraz modul kontroli plikow systemowych
// i diagnostyki (jadro/gui/instalacja_plikow.h: CRC zapisu, krok weryfikacji, komunikat GUI, linia logu).
//
// Etapy jak w maszynie stanow jadro/gui/instalator.cpp: GPT -> BSP2FORMAT (kroki + odczyt kontrolny) ->
// SYSTEMFILES (katalogi KATALOGI_SYSTEMOWE, pliki porcjami 4 KiB + FLUSH) -> WALLPAPERS -> GALLERY ->
// VERIFY (FLUSH, metadane krokami, pliki systemowe krokami) -> znacznik ukonczenia; potem niezalezny montaz
// (nowa instancja PSF jak start zainstalowanego systemu) i porownanie bajt po bajcie z plikami zrodlowymi.
// Krok SYSTEMFILES jest tu odwzorowany (kernel.cpp nie buduje sie na hoscie) razem z planem krokowym: wpis planu
// przechowuje tylko WSKAZNIK do bajtow (jak PlanowanyPlik) i migawke CRC zrodla (IP::migawka_zrodla); CRC/segmenty,
// kontrola zrodla i weryfikacja to kod produkcyjny. Zapis blokowy jest dozwolony tylko "w oknie instalatora" (jak blokowy_zezwol_instalatorowi).
//
//   test_instalacja_host <manifest.tsv> <zdjecia.pak> <katalog roboczy>
#include "storage_format.h"
#include "psf.h"
#include "psf_bootstrap.h"
#include "galeria_pak.h"
#include "galeria_kopiowanie.h"
#include "jadro/gui/instalacja_plikow.h"
#include "../sterowniki/czas/zegar-rtc.h"
#include <openssl/evp.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Storage;
namespace IP = InstalacjaPlikow;
void* kmalloc(size_t n) { return std::malloc(n); }
void kfree(void* p) { std::free(p); }
void wypisz_log(const char*) {}
bool pobierz_czas_rtc_bezpiecznie(czas_rtc*) { return false; }
static bool g_okno_instalatora = true;                 // blokowy_zezwol_instalatorowi(selected, true/false)
bool blokowy_zapis_instalatora() { return g_okno_instalatora; }

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

// Obraz rzadki z wstrzykiwaniem bledow. Zapis poza oknem instalatora jest odrzucany (blokowy.cpp: filter_write).
struct Obraz {
    int fd = -1;
    Device dev;
    uint64_t zapisy = 0;
    uint64_t blad_zapisu_po = UINT64_MAX;
    bool blad_flush = false, blad_odczytu = false;
    uint64_t przeklam_lba = UINT64_MAX, blad_odczytu_lba = UINT64_MAX;
    bool przeklam_raz = false;                    // przeklamanie tylko przy pierwszym odczycie (niestabilny odczyt)
    Obraz(int f, uint64_t sektory) : fd(f), dev{this, sektory, 512, true, rd, wr, fl} {}
    static bool rd(void* c, uint64_t s, uint32_t n, void* b) {
        auto& o = *static_cast<Obraz*>(c);
        if (o.blad_odczytu || !range(s, n, o.dev.sectors)) return false;
        if (o.blad_odczytu_lba >= s && o.blad_odczytu_lba < s + n) return false;    // np. timeout VIRTIO-BLK
        if (pread(o.fd, b, size_t(n) * 512, off_t(s * 512)) != ssize_t(size_t(n) * 512)) return false;
        if (o.przeklam_lba >= s && o.przeklam_lba < s + n) {
            static_cast<uint8_t*>(b)[(o.przeklam_lba - s) * 512 + 7] ^= 0x5A;
            if (o.przeklam_raz) o.przeklam_lba = UINT64_MAX;
        }
        return true;
    }
    static bool wr(void* c, uint64_t s, uint32_t n, const void* b) {
        auto& o = *static_cast<Obraz*>(c);
        if (!g_okno_instalatora || o.zapisy++ >= o.blad_zapisu_po || !range(s, n, o.dev.sectors)) return false;
        return pwrite(o.fd, b, size_t(n) * 512, off_t(s * 512)) == ssize_t(size_t(n) * 512);
    }
    static bool fl(void* c) { return !static_cast<Obraz*>(c)->blad_flush; }
};

struct Wpis { std::string rodzaj, sciezka; std::vector<uint8_t> dane; };

static std::vector<Wpis> wczytaj_manifest(const char* tsv) {
    std::vector<Wpis> w;
    std::ifstream f(tsv);
    CHECK(f.good());
    std::string linia;
    while (std::getline(f, linia)) {
        if (linia.empty()) continue;
        std::istringstream s(linia);
        Wpis e; std::string plik;
        std::getline(s, e.rodzaj, '\t'); std::getline(s, e.sciezka, '\t'); std::getline(s, plik, '\t');
        std::ifstream d(plik, std::ios::binary);
        CHECK(d.good());
        e.dane.assign(std::istreambuf_iterator<char>(d), {});
        w.push_back(std::move(e));
    }
    return w;
}

static bool sha256(const uint8_t* d, uint64_t n, uint8_t out[32]) {
    unsigned dl = 0;
    return EVP_Digest(d, size_t(n), out, &dl, EVP_sha256(), nullptr) == 1 && dl == 32;
}

static const uint8_t UUID[16] = {0x42, 0x75, 0x72, 0x73, 0x7a, 0x74, 0x79, 0x6e, 0x49, 0x6e, 0x73, 0x74, 0x01, 0x02, 0x03, 0x04};
static std::vector<uint8_t> g_ram(128u * 1024u * 1024u);   // ROZMIAR_RAM_DYSKU_PSF jadra

// Dostep weryfikacji do PSF: dokladnie jak odczyt_weryfikacji w kernel.cpp.
static const IP::Odczyt ODCZYT{
    [](const char* p, uint64_t* r) { psf_metadane m{}; if (!pobierz_metadane_tworu(p, &m) || m.typ != TYP_PLIK) return false; *r = m.rozmiar; return true; },
    [](const char* p, uint64_t off, void* b, uint32_t n, uint32_t* got) { return czytaj_fragment_pliku(p, off, static_cast<char*>(b), n, got); },
    [](const char* p, uint64_t blok, uint64_t* lba) { return psf_lba_bloku_pliku(p, blok, lba); },
    []() { return psf_zapis_trwaly_aktywny(); },
    []() { return psf_porzuc_czyste_bloki_cache(); },
    []() -> uint64_t { static uint64_t t = 0; return t += 7; },   // deterministyczny "zegar"
    []() { return psf_ostatni_flush_ok(); },
};

enum class Wynik { OK, Format, Pliki, Tapety, Metadane, Weryfikacja, Znacznik };
static const char* nazwa(Wynik w) {
    static const char* n[] = {"OK", "BSP2FORMAT", "SYSTEMFILES", "WALLPAPERS", "VERIFY-META", "VERIFY-FILES", "MARKER"};
    return n[int(w)];
}

struct Stan {
    std::vector<IP::Kontrolny> kontrolne;
    std::vector<uint32_t> segmenty = std::vector<uint32_t>(IP::MAKS_SEGMENTOW);
    uint32_t liczba_segmentow = 0;
    IP::Diagnoza diag{};
    Partition bsp{};
    Obraz* o = nullptr;
    uint32_t liczba_planu = 0;
};

// Wpis planu krokowego jak PlanowanyPlik w kernel.cpp: tylko wskaznik do danych + migawka CRC z chwili planowania.
struct PlanWpis { std::string sciezka; const uint8_t* dane; uint32_t rozmiar; IP::Zrodlo zrodlo; };

struct Scenariusz {
    uint64_t mib = 4096;
    bool kontrola_zrodla = true;     // false = zachowanie sprzed poprawki (bez porownania z migawka zrodla)
    // Zrodlo jednego pliku planu w zmiennym buforze (jak martwa ramka stosu): zastepuje bajty manifestu.
    std::string zmienne_zrodlo;
    // Wywolywane przy kazdej porcji zrodla `zmienne_zrodlo`: przed zapisem i po zapisie+FLUSH (przed CRC rejestru).
    std::function<void(std::vector<uint8_t>& bufor, uint32_t offset, bool po_zapisie)> mutacja_zrodla;
    bool galeria = true, tapety = true;
    bool porzuc_cache = true;        // drugi odczyt segmentu omija cache (false = bez porzucenia przed ponownym odczytem)
    std::vector<std::pair<std::string, std::vector<uint8_t>>> dodatkowe;   // dodatkowe pliki planu (granice nazw)
    std::function<void(Stan&)> przed_plikami, po_plikach, przed_weryfikacja;
};

static void zglos(Stan& st, IP::Etap e, IP::Przyczyna p, const std::string& s, uint64_t off = 0, uint64_t dl = 0) {
    IP::ustaw(&st.diag, e, p, s.c_str(), 0, 0, off, dl);
    st.diag.zapis_trwaly = psf_zapis_trwaly_aktywny();
}

// Kopia kroku SYSTEMFILES z kernel.cpp (usun -> utworz -> porcje 4 KiB + FLUSH -> rejestr CRC -> kontrola zrodla).
// Bajty sa czytane przez wskaznik z planu w chwili zapisu - jak w jadrze, a nie z trwalej kopii.
static bool zapisz_plik(Stan& st, const Scenariusz& sc, const PlanWpis& e, std::vector<uint8_t>* zmienny) {
    const std::string& sciezka = e.sciezka;
    (void)usun_twor(sciezka.c_str());
    if (!utworz_plik(sciezka.c_str())) { zglos(st, IP::Etap::Zapis, IP::Przyczyna::Utworzenie, sciezka); return false; }
    IP::Rejestr r;
    IP::rejestr_start(r, st.liczba_segmentow);
    for (uint32_t off = 0; off < e.rozmiar; off += IP::PORCJA) {
        const uint32_t n = std::min<uint32_t>(IP::PORCJA, e.rozmiar - off);
        if (zmienny && sc.mutacja_zrodla) sc.mutacja_zrodla(*zmienny, off, false);
        if (!zapisz_fragment_pliku(sciezka.c_str(), off, reinterpret_cast<const char*>(e.dane + off), n)) {
            zglos(st, IP::Etap::Zapis, IP::Przyczyna::Zapis, sciezka, off, n); (void)usun_twor(sciezka.c_str()); return false;
        }
        if (!psf_flush_cache()) { zglos(st, IP::Etap::Zapis, IP::Przyczyna::Flush, sciezka, off, n); (void)usun_twor(sciezka.c_str()); return false; }
        if (zmienny && sc.mutacja_zrodla) sc.mutacja_zrodla(*zmienny, off, true);
        IP::rejestr_dodaj(r, e.dane + off, n, st.segmenty.data(), &st.liczba_segmentow);
    }
    if (st.kontrolne.size() >= 128) { zglos(st, IP::Etap::Zapis, IP::Przyczyna::Limit, sciezka); return false; }
    IP::Kontrolny k{};
    IP::rejestr_zakoncz(r, k, sciezka.c_str(), e.rozmiar, st.segmenty.data(), &st.liczba_segmentow);
    if (sc.kontrola_zrodla && !IP::zrodlo_zgodne(e.zrodlo, k)) {
        IP::ustaw(&st.diag, IP::Etap::Zapis, IP::Przyczyna::ZrodloZmienione, sciezka.c_str(), e.zrodlo.crc, k.crc, 0, e.rozmiar);
        st.diag.zapis_trwaly = psf_zapis_trwaly_aktywny();
        st.diag.indeks_pliku = uint32_t(st.kontrolne.size()); st.diag.liczba_plikow = st.liczba_planu;
        (void)usun_twor(sciezka.c_str()); return false;
    }
    st.kontrolne.push_back(k);
    return true;
}

static Wynik instaluj(const std::string& img, const std::vector<Wpis>& m, const std::vector<uint8_t>& zdjecia,
                      const Scenariusz& sc, Stan& st) {
    ::unlink(img.c_str());
    const int fd = open(img.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
    CHECK(fd >= 0);
    CHECK(ftruncate(fd, off_t(sc.mib * 1024 * 1024)) == 0);
    st.o = new Obraz(fd, sc.mib * 2048);
    g_okno_instalatora = true;
    Layout l{};
    CHECK(plan(&st.o->dev, &l));
    CHECK(gpt_write(&st.o->dev, l, UUID));
    st.bsp = l.bsp;
    psf_ustaw_partycje(nullptr);                     // sesja Live: RAM-dysk w tym samym obszarze metadanych
    inicjalizuj_psf(g_ram.data(), uint32_t(g_ram.size()));
    uint64_t razem = 0, wyk = 0; bool gotowe = false;
    if (!psf_formatuj_instalacje_start(l.bsp, &razem)) return Wynik::Format;
    while (!gotowe) if (!psf_formatuj_instalacje_step(&wyk, &gotowe)) return Wynik::Format;
    if (!psf_znacznik_instalacji(UUID, false) || !psf_weryfikuj_dysk_start(&razem)) return Wynik::Format;
    gotowe = false;
    while (!gotowe) if (!psf_weryfikuj_dysk_step(&wyk, &gotowe)) return Wynik::Format;

    // SYSTEMFILES: bsf_import_fonts tworzy katalogi czcionek w trakcie planowania, potem KATALOGI_SYSTEMOWE.
    for (const char* k : {"/czcionki", "/czcionki/ttf", "/czcionki/otf", "/czcionki/ttc", "/czcionki/otc"})
        CHECK(zapewnij_katalog_startowy(k));
    if (sc.przed_plikami) sc.przed_plikami(st);
    for (size_t i = 0; i < LICZBA_KATALOGOW_SYSTEMOWYCH; ++i)
        if (!zapewnij_katalog_startowy(KATALOGI_SYSTEMOWE[i]) || !psf_flush_cache()) {
            zglos(st, IP::Etap::Katalogi, IP::Przyczyna::Katalog, KATALOGI_SYSTEMOWE[i]); return Wynik::Pliki;
        }
    // Plan (kernel_instaluj_system_start): wskazniki + migawki CRC; zapis w kolejnych krokach.
    std::vector<uint8_t> zmienny;
    std::vector<PlanWpis> plan;
    auto planuj = [&](const std::string& sciezka, const std::vector<uint8_t>& d) {
        const uint8_t* zrodlo = d.data();
        if (sciezka == sc.zmienne_zrodlo) { zmienny = d; zrodlo = zmienny.data(); }
        plan.push_back(PlanWpis{sciezka, zrodlo, uint32_t(d.size()), IP::migawka_zrodla(zrodlo, uint32_t(d.size()))});
    };
    for (const auto& e : m)
        if (e.rodzaj == "SYSTEM") planuj(e.sciezka, e.dane);
    for (const auto& e : sc.dodatkowe) planuj(e.first, e.second);
    st.liczba_planu = uint32_t(plan.size());
    for (const auto& e : plan)
        if (!zapisz_plik(st, sc, e, e.sciezka == sc.zmienne_zrodlo ? &zmienny : nullptr)) return Wynik::Pliki;
    if (sc.po_plikach) sc.po_plikach(st);

    // WALLPAPERS (tapety.cpp: usun -> utworz -> porcje 4 KiB) i GALLERY (produkcyjna GaleriaPak::Kopiarka).
    CHECK(zapewnij_katalog_startowy("/tapety") && zapewnij_katalog_startowy("/tapety/miniatury"));
    for (const auto& e : m) {
        if (e.rodzaj != "TAPETA" || !sc.tapety) continue;
        (void)usun_twor(e.sciezka.c_str());
        if (!utworz_plik(e.sciezka.c_str())) return Wynik::Tapety;
        for (uint32_t off = 0; off < e.dane.size(); off += 4096) {
            const uint32_t n = uint32_t(std::min<size_t>(4096, e.dane.size() - off));
            if (!zapisz_fragment_pliku(e.sciezka.c_str(), off, reinterpret_cast<const char*>(e.dane.data() + off), n) ||
                !psf_flush_cache()) return Wynik::Tapety;
        }
    }
    if (sc.galeria && !zdjecia.empty()) {
        static GaleriaPak::Wpis wpisy[GaleriaPak::MAKS_WPISOW];
        uint32_t liczba = 0;
        CHECK(GaleriaPak::parsuj(zdjecia.data(), zdjecia.size(), wpisy, &liczba, sha256) == GaleriaPak::Blad::OK);
        CHECK(zapewnij_katalog_startowy("/dane/zdjecia"));
        GaleriaPak::Io io;
        io.rozmiar = [](const char* p) -> uint32_t { return rozmiar_pliku(p); };
        io.utworz = [](const char* p) { return utworz_plik(p); };
        io.usun = [](const char* p) { return usun_twor(p); };
        io.zapisz = [](const char* p, uint32_t off, const uint8_t* d, uint32_t n) { return zapisz_fragment_pliku(p, off, reinterpret_cast<const char*>(d), n); };
        io.zmien_nazwe = [](const char* p, const char* nowa) { return zmien_nazwe_tworu(p, nowa); };
        io.flush = []() { return psf_flush_cache(); };
        static GaleriaPak::Kopiarka kop;
        kop.start(zdjecia.data(), wpisy, liczba, io);
        while (kop.krok(64u * 1024u) == GaleriaPak::Krok::Dalej) {}
        CHECK(kop.zainstalowane == liczba && kop.bledne == 0);
    }

    // VERIFY: FLUSH, metadane BSP2 krokami, pliki systemowe produkcyjnym krokiem (8 porcji na "perform").
    if (sc.przed_weryfikacja) sc.przed_weryfikacja(st);
    if (!psf_flush_cache() || !psf_weryfikuj_dysk_start(&razem)) return Wynik::Metadane;
    gotowe = false;
    while (!gotowe) if (!psf_weryfikuj_dysk_step(&wyk, &gotowe)) return Wynik::Metadane;
    IP::Weryfikacja w{};
    uint8_t bufor[IP::PORCJA];
    IP::Odczyt odczyt = ODCZYT;
    if (!sc.porzuc_cache) odczyt.porzuc_cache = nullptr;
    for (bool k = false; !k;)
        if (!IP::weryfikuj_krok(w, st.kontrolne.data(), uint32_t(st.kontrolne.size()), st.segmenty.data(), odczyt, bufor, &k, &st.diag))
            return Wynik::Weryfikacja;
    if (!psf_znacznik_instalacji(UUID, true) || !complete(l.bsp, UUID)) return Wynik::Znacznik;
    return Wynik::OK;
}

static void zamknij(Stan& st) {
    psf_ustaw_partycje(nullptr);
    inicjalizuj_psf(g_ram.data(), uint32_t(g_ram.size()));    // odlaczenie PSF od obrazu przed zamknieciem pliku
    g_okno_instalatora = true;
    if (st.o) { close(st.o->fd); delete st.o; st.o = nullptr; }
}

static bool znacznik_ukonczenia(Stan& st) {
    st.o->blad_flush = false; st.o->blad_odczytu = false; st.o->przeklam_lba = UINT64_MAX;
    return complete(st.bsp, UUID);
}

// Niezalezny odczyt: nowa instancja PSF montuje partycje jak zainstalowany system (bez stanu instalatora).
static void sprawdz_po_restarcie(Stan& st, const std::vector<Wpis>& m) {
    Table t{};
    CHECK(gpt_read(&st.o->dev, &t));
    Partition bsp{};
    for (uint32_t i = 0; i < t.count; ++i)
        if (equal(t.entries[i].type, BspType, 16)) bsp = {&st.o->dev, t.entries[i].start, t.entries[i].count};
    CHECK(bsp.device && complete(bsp, t.uuid));
    static std::vector<uint8_t> ram2(128u * 1024u * 1024u);
    psf_ustaw_partycje(&bsp);
    inicjalizuj_psf(ram2.data(), uint32_t(ram2.size()));
    CHECK(psf_czy_gotowy() && psf_urzadzenie_trwale() && psf_weryfikuj_dysk());
    const uint64_t zapisy = st.o->zapisy;
    std::vector<char> b(1u << 20);
    unsigned zgodne = 0;
    for (const auto& e : m) {
        psf_metadane md{};
        CHECK(pobierz_metadane_tworu(e.sciezka.c_str(), &md) && md.typ == TYP_PLIK && md.rozmiar == e.dane.size());
        for (uint64_t off = 0; off < md.rozmiar;) {
            const uint32_t n = uint32_t(std::min<uint64_t>(b.size(), md.rozmiar - off));
            uint32_t got = 0;
            CHECK(czytaj_fragment_pliku(e.sciezka.c_str(), off, b.data(), n, &got) && got == n);
            CHECK(std::memcmp(b.data(), e.dane.data() + off, n) == 0);
            off += n;
        }
        ++zgodne;
    }
    for (size_t i = 0; i < LICZBA_KATALOGOW_SYSTEMOWYCH; ++i) CHECK(czy_katalog_istnieje(KATALOGI_SYSTEMOWE[i]));
    // Muzyka jest opcjonalna i domyslnie poza ISO: /dane/muzyka istnieje i jest pusty.
    std::vector<char> lista(4096);
    CHECK(wylistuj_katalog("/dane/muzyka", lista.data(), uint32_t(lista.size())));
    CHECK(std::strstr(lista.data(), "[PLIK]") == nullptr);
    CHECK(st.o->zapisy == zapisy);                                   // odczyt bez "napraw"
    std::printf("  po restarcie: %u plikow zgodnych bajt po bajcie (niezalezny montaz PSF), /dane/muzyka pusty\n", zgodne);
}

static void opis(const Stan& st, const char* tytul) {
    char gui[256], log[384];                                         // = InstalatorOdpowiedz::blad
    IP::opisz(st.diag, gui, sizeof(gui));
    IP::linia_logu(st.diag, log, sizeof(log));
    std::printf("%s\n  GUI: %s\n  LOG: %s\n", tytul, gui, log);
    CHECK(std::strlen(gui) < sizeof(gui) && std::strlen(log) < sizeof(log));
}

// Manifest: sprawdzenia statyczne zgodnosci z planem jadra i limitami formatu.
static unsigned sprawdz_manifest(const std::vector<Wpis>& m) {
    std::set<std::string> sciezki;
    unsigned systemowe = 0;
    uint64_t bajty = 0;
    bool grajek = false, grajek_manifest = false, winyl = false, emoji = false, ekran = false, tapety = false;
    for (const auto& e : m) {
        CHECK(sciezki.insert(e.sciezka).second);                     // bez duplikatow (nadpisanie = falszywe CRC)
        CHECK(e.sciezka.size() + 1 <= IP::MAKS_SCIEZKA);             // PlikKontrolny/PlanowanyPlik: 128 B z NUL
        const std::string liscie = e.sciezka.substr(e.sciezka.rfind('/') + 1);
        CHECK(!liscie.empty() && liscie.size() <= 55);               // nazwa PSF: 55 B UTF-8
        CHECK(e.sciezka.rfind("/dane/muzyka/", 0) != 0);             // muzyka nie jest czescia instalacji
        if (e.rodzaj != "SYSTEM") continue;
        ++systemowe; bajty += e.dane.size();
        CHECK(e.dane.size() <= 0xFFFFFFFFu);
        grajek |= e.sciezka == "/programy/grajek.cebula/grajek.bur";
        winyl |= e.sciezka == "/ikonki/grajek_plyta_winylowa.jpg";
        emoji |= e.sciezka == "/czcionki/ttf/NotoColorEmoji-COLRv1.ttf";
        ekran |= e.sciezka == "/programy/ekran.cebula/ekran.bur";
        tapety |= e.sciezka == "/programy/tapety.cebula/tapety.bur";
        if (e.sciezka == "/programy/grajek.cebula/opis.aplikacji") {
            const std::string t(e.dane.begin(), e.dane.end());
            grajek_manifest = t.find("plik_startowy = \"grajek.bur\"") != std::string::npos;
        }
        // Kazdy manifest .cebula wskazuje plik startowy obecny w tym samym katalogu planu.
        if (liscie == "opis.aplikacji") {
            const std::string t(e.dane.begin(), e.dane.end());
            const size_t p = t.find("plik_startowy = \"");
            CHECK(p != std::string::npos);
            const std::string start = t.substr(p + 17, t.find('"', p + 17) - p - 17);
            const std::string kat = e.sciezka.substr(0, e.sciezka.rfind('/') + 1);
            bool jest = false;
            for (const auto& x : m) jest |= x.sciezka == kat + start;
            CHECK(jest);
        }
    }
    CHECK(grajek && grajek_manifest && winyl && emoji && ekran && tapety);
    unsigned ikony_grajka = 0;
    for (const auto& e : m) ikony_grajka += e.sciezka.rfind("/ikonki/grajek_", 0) == 0;
    // 19 ikon przyciskow (14 grafik uzytkownika + 5 przelacznikow) + plyta winylowa
    CHECK(ikony_grajka == 20);
    CHECK(systemowe > 100 && systemowe <= 128);                      // MAKS_PLANOWANYCH_PLIKOW / MAKS_PLIKOW_KONTROLNYCH
    std::printf("manifest: %u plikow systemowych (%llu B), %zu wpisow lacznie, zapas planu %u\n",
                systemowe, (unsigned long long)bajty, m.size(), 128 - systemowe);
    return systemowe;
}

// Jednostkowe: komunikaty sa ograniczone, UTF-8 nie jest ciete w polowie znaku, rejestr segmentow.
static void testy_modulu() {
    IP::Diagnoza d{};
    std::string dluga = "/programy/";
    for (int i = 0; i < 8; ++i) dluga += "żółć-ąę/";
    dluga += "plik.bur";
    IP::ustaw(&d, IP::Etap::Weryfikacja, IP::Przyczyna::Crc, dluga.c_str(), 0x1234, 0xabcd, 65536, 4096);
    char gui[256], log[256];
    IP::opisz(d, gui, sizeof(gui));
    IP::linia_logu(d, log, sizeof(log));
    CHECK(std::strlen(gui) < 256 && std::strlen(log) < 256);
    for (const char* t : {gui, log}) {                                  // poprawny UTF-8 po obcieciu
        for (size_t i = 0; t[i];) {
            const uint8_t c = uint8_t(t[i]);
            const size_t k = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
            CHECK(k != 0);
            for (size_t j = 1; j < k; ++j) CHECK((uint8_t(t[i + j]) & 0xC0) == 0x80);
            i += k;
        }
    }
    CHECK(std::strstr(gui, "Niezgodna zawartość") && std::strstr(gui, "bloki 16–16") == nullptr && std::strstr(gui, "blok 16."));
    // GUI: etap, sciezka i przyczyna w osobnych wierszach (zawijana Etykieta), bez obciecia przyczyny.
    {
        IP::Diagnoza p{};
        IP::ustaw(&p, IP::Etap::Weryfikacja, IP::Przyczyna::Crc, "/programy/pcm440.cebula/opis.aplikacji", 0x559d4d22, 0x163a1d8b, 0, 130);
        p.indeks_pliku = 107; p.liczba_plikow = 120;
        char g[256];
        IP::opisz(p, g, sizeof(g));
        CHECK(std::strcmp(g, "Weryfikacja plików systemowych (plik 108 z 120):\n/programy/pcm440.cebula/opis.aplikacji\n"
                             "Niezgodna zawartość (CRC-32), blok 0.") == 0);
        // Najdluzsza przyczyna + sciezka 127 B miesci sie w 256 B (sciezka obcinana do 100 B od konca).
        std::string s127 = "/programy/" + std::string(116, 'a');
        IP::ustaw(&p, IP::Etap::Zapis, IP::Przyczyna::ZrodloZmienione, s127.c_str(), 1, 2, 0, 130);
        IP::opisz(p, g, sizeof(g));
        CHECK(std::strlen(g) < sizeof(g) - 1 && std::strstr(g, "(błąd wewnętrzny instalatora), blok 0."));
    }
    CHECK(std::strstr(log, "cause=crc-mismatch") && std::strstr(log, "expected=0x1234") && std::strstr(log, "actual=0xabcd") && std::strstr(log, "blocks=16-16"));
    char male[8];
    IP::opisz(d, male, sizeof(male));
    CHECK(std::strlen(male) < 8);
    // Rejestr: CRC segmentow = CRC kolejnych 64 KiB; CRC pliku = Storage::crc32.
    std::vector<uint8_t> dane(3 * IP::SEGMENT + 123);
    for (size_t i = 0; i < dane.size(); ++i) dane[i] = uint8_t(i * 131 + 7);
    std::vector<uint32_t> seg(IP::MAKS_SEGMENTOW);
    uint32_t n = 5;
    IP::Rejestr r; IP::rejestr_start(r, n);
    for (size_t off = 0; off < dane.size(); off += 4096)
        IP::rejestr_dodaj(r, dane.data() + off, uint32_t(std::min<size_t>(4096, dane.size() - off)), seg.data(), &n);
    IP::Kontrolny k{};
    IP::rejestr_zakoncz(r, k, "/x", uint32_t(dane.size()), seg.data(), &n);
    CHECK(k.crc == Storage::crc32(dane.data(), dane.size()) && k.pierwszy_segment == 5 && k.segmentow == 4 && n == 9);
    CHECK(seg[5] == Storage::crc32(dane.data(), IP::SEGMENT) && seg[8] == Storage::crc32(dane.data() + 3 * IP::SEGMENT, 123));
    // Przepelnienie tablicy segmentow: tylko CRC calego pliku (nadal weryfikowane).
    uint32_t pelne = IP::MAKS_SEGMENTOW - 1;
    IP::rejestr_start(r, pelne);
    IP::rejestr_dodaj(r, dane.data(), uint32_t(dane.size()), seg.data(), &pelne);
    IP::rejestr_zakoncz(r, k, "/x", uint32_t(dane.size()), seg.data(), &pelne);
    CHECK(k.segmentow == 0 && k.crc == Storage::crc32(dane.data(), dane.size()));
    // Migawka zrodla: male pliki surowe (<= 64 KiB) maja CRC z chwili planowania; zgodnosc z rejestrem.
    const IP::Zrodlo z = IP::migawka_zrodla(dane.data(), 4096);
    CHECK(z.znane && z.crc == Storage::crc32(dane.data(), 4096));
    CHECK(!IP::migawka_zrodla(dane.data(), IP::MAKS_MIGAWKI_ZRODLA + 1).znane);
    IP::Kontrolny kk{}; kk.crc = z.crc;
    CHECK(IP::zrodlo_zgodne(z, kk));
    kk.crc ^= 1;
    CHECK(!IP::zrodlo_zgodne(z, kk) && IP::zrodlo_zgodne(IP::Zrodlo{0, false}, kk));
    // Zakresy: [p, p+n) vs [dol, gora).
    alignas(8) static uint8_t pas[64];
    const uintptr_t a = uintptr_t(pas);
    CHECK(IP::zakres_nachodzi(pas + 8, 8, a, a + 16) && !IP::zakres_nachodzi(pas + 16, 8, a, a + 16));
    CHECK(IP::zakres_nachodzi(pas, 8, a + 7, a + 16) && !IP::zakres_nachodzi(pas, 8, a + 8, a + 16));
    CHECK(IP::zakres_nachodzi(pas + 15, 0, a, a + 16) && !IP::zakres_nachodzi(pas, 8, a + 16, a));   // n=0; pusty zakres
    CHECK(!IP::zakres_nachodzi(nullptr, 8, 0, UINTPTR_MAX));
    CHECK(IP::zakres_nachodzi(reinterpret_cast<void*>(UINTPTR_MAX - 4), 64, a, UINTPTR_MAX));          // przepelnienie
}

// Osłona planu na prawdziwych ramkach stosu (jak planuj_plik + kernel_instaluj_system_start): adres miedzy
// biezacym RSP a ramka funkcji startujacej plan nalezy do ramki, ktora nie przetrwa do krokow zapisu.
static uintptr_t g_granica_planu = 0;
__attribute__((noinline)) static uintptr_t biezacy_rsp() { uintptr_t sp; asm volatile("mov %%rsp,%0" : "=r"(sp)); return sp; }
__attribute__((noinline)) static bool planuj_zrodlo(const void* d, uint64_t n) {
    return !IP::zakres_nachodzi(d, n, biezacy_rsp(), g_granica_planu);
}
static const char MANIFEST_STATYCZNY[] = "nazwa = \"PCM 440 Hz\"\n";
// Dokladnie dawny wzorzec z kernel.cpp: lokalna tablica (kopiowana na stos) przekazana do planu.
// no_sanitize_address: ASan moze przeniesc lokalne tablice na "falszywy stos" na stercie.
__attribute__((noinline, no_sanitize_address)) static bool planuj_lokalna_tablice() {
    const char manifest440[] = "nazwa = \"PCM 440 Hz\"\nautor = \"Bursztyn OS\"\n";
    asm volatile("" : : "r"(manifest440) : "memory");
    return planuj_zrodlo(manifest440, sizeof(manifest440) - 1);
}
__attribute__((noinline, no_sanitize_address)) static void test_oslony_stosu_planu() {
    g_granica_planu = uintptr_t(__builtin_frame_address(0));
    CHECK(!planuj_lokalna_tablice());                                          // stos -> odrzucone
    CHECK(planuj_zrodlo(MANIFEST_STATYCZNY, sizeof(MANIFEST_STATYCZNY) - 1));  // dane statyczne -> OK
    std::vector<uint8_t> sterta(4096);
    CHECK(planuj_zrodlo(sterta.data(), sterta.size()));                        // nie-stos -> OK (zakres tylko stosu)
    g_granica_planu = 0;
    CHECK(planuj_lokalna_tablice());                                           // poza planowaniem bez ograniczen
    std::printf("osłona planu: lokalna tablica na stosie odrzucona, dane statyczne i sterta przyjete\n");
}

int main(int argc, char** argv) {
    if (argc != 4) { std::fprintf(stderr, "uzycie: test_instalacja_host <manifest.tsv> <zdjecia.pak> <katalog>\n"); return 1; }
    testy_modulu();
    test_oslony_stosu_planu();
    const std::vector<Wpis> m = wczytaj_manifest(argv[1]);
    std::ifstream z(argv[2], std::ios::binary);
    const std::vector<uint8_t> zdjecia((std::istreambuf_iterator<char>(z)), {});
    CHECK(!zdjecia.empty());
    const unsigned systemowe = sprawdz_manifest(m);
    const std::string img = std::string(argv[3]) + "/instalacja-test.img";

    auto lba_bloku = [](const char* sciezka, uint64_t blok) { uint64_t lba = 0; CHECK(psf_lba_bloku_pliku(sciezka, blok, &lba)); return lba; };

    // 1. Czysty dysk 4 GiB (ESP 512 MiB + BSP2 2 GiB): pelna instalacja, weryfikacja, niezalezny odczyt.
    {
        Stan st; Scenariusz sc;
        const Wynik w = instaluj(img, m, zdjecia, sc, st);
        if (w != Wynik::OK) opis(st, nazwa(w));
        CHECK(w == Wynik::OK);
        CHECK(st.kontrolne.size() == systemowe && st.diag.etap == IP::Etap::Brak);
        CHECK(st.bsp.count == 2048ull * 2048ull);                     // 2 GiB BSP2
        std::printf("1. czysty dysk 4 GiB: instalacja + weryfikacja OK (%zu plikow, %u segmentow 64 KiB)\n",
                    st.kontrolne.size(), st.liczba_segmentow);
        // Wlasnosc blokow: kazdy blok danych pliku systemowego, tapety i zdjecia galerii (po WALLPAPERS i
        // GALLERY) ma jednego wlasciciela i lezy w BSP2. Nakladanie na metadane wykrywa VERIFY-META (bajt po bajcie).
        std::vector<std::string> wszystkie;
        for (const auto& e : m) wszystkie.push_back(e.sciezka);
        std::vector<char> lista(64 * 1024);
        CHECK(wylistuj_katalog("/dane/zdjecia", lista.data(), uint32_t(lista.size())));
        unsigned zdjec = 0;
        for (std::istringstream ls(lista.data()); ;) {
            std::string w; if (!std::getline(ls, w)) break;
            if (w.rfind("[PLIK] ", 0) == 0) { wszystkie.push_back("/dane/zdjecia/" + w.substr(7)); ++zdjec; }
        }
        CHECK(zdjec > 0);
        std::set<uint64_t> wlasciciele; uint64_t blokow = 0;
        for (const auto& sciezka : wszystkie) {
            psf_metadane md{};
            CHECK(pobierz_metadane_tworu(sciezka.c_str(), &md) && md.typ == TYP_PLIK);
            for (uint64_t b = 0; b * 4096u < md.rozmiar; ++b, ++blokow) {
                uint64_t lba = 0;
                CHECK(psf_lba_bloku_pliku(sciezka.c_str(), b, &lba));
                CHECK(lba >= st.bsp.start && lba + 8 <= st.bsp.start + st.bsp.count);
                CHECK(wlasciciele.insert(lba).second);
            }
        }
        std::printf("  bloki danych: %llu blokow %zu plikow (w tym %u zdjec), kazdy z jednym wlascicielem, w BSP2\n",
                    (unsigned long long)blokow, wszystkie.size(), zdjec);
        sprawdz_po_restarcie(st, m);
        zamknij(st);
    }
    // 2. Przeklamany odczyt bloku pliku systemowego w VERIFY -> CRC segmentu, plik, blok i LBA w diagnostyce.
    {
        Stan st; Scenariusz sc; sc.galeria = false;
        uint64_t lba = 0;
        sc.przed_weryfikacja = [&](Stan& s) { lba = lba_bloku("/programy/grajek.cebula/grajek.bur", 20); s.o->przeklam_lba = lba; };
        CHECK(instaluj(img, m, zdjecia, sc, st) == Wynik::Weryfikacja);
        CHECK(st.diag.etap == IP::Etap::Weryfikacja && st.diag.przyczyna == IP::Przyczyna::Crc);
        CHECK(std::strcmp(st.diag.sciezka, "/programy/grajek.cebula/grajek.bur") == 0);
        CHECK(st.diag.offset == 65536 && st.diag.dlugosc == 65536 && st.diag.lba_znane);   // segment z blokiem 20
        CHECK(st.diag.oczekiwano != st.diag.odczytano && st.diag.zapis_trwaly);
        CHECK(st.diag.ponowny_znany && st.diag.ponowny_odczyt_ok && st.diag.crc_ponowny == st.diag.odczytano);   // trwale
        CHECK(st.diag.flush_znany && st.diag.flush_ok && st.diag.maks_czas_odczytu_us > 0);
        opis(st, "2. przeklamany odczyt (blok 20 grajek.bur):");
        CHECK(!znacznik_ukonczenia(st));
        zamknij(st);
    }
    // 3. Utracony zapis (sektor na nosniku inny niz zapisany) wczesnego pliku -> CRC w VERIFY.
    {
        Stan st; Scenariusz sc; sc.galeria = false;
        sc.przed_weryfikacja = [&](Stan& s) {
            const uint64_t lba = lba_bloku("/czcionki/ttf/DejaVuSans.ttf", 3);
            std::vector<uint8_t> zera(512, 0);
            CHECK(pwrite(s.o->fd, zera.data(), 512, off_t(lba * 512)) == 512);
        };
        CHECK(instaluj(img, m, zdjecia, sc, st) == Wynik::Weryfikacja);
        CHECK(st.diag.przyczyna == IP::Przyczyna::Crc && std::strcmp(st.diag.sciezka, "/czcionki/ttf/DejaVuSans.ttf") == 0);
        CHECK(st.diag.offset == 0 && st.diag.dlugosc == 65536);
        opis(st, "3. utracony zapis (blok 3 DejaVuSans.ttf):");
        zamknij(st);
    }
    // 4. Blad odczytu jednego bloku danych w VERIFY (np. timeout VIRTIO-BLK) -> read-error z plikiem i LBA.
    {
        Stan st; Scenariusz sc; sc.galeria = false;
        uint64_t lba = 0;
        sc.przed_weryfikacja = [&](Stan& s) { lba = lba_bloku("/czcionki/ttf/DejaVuSans.ttf", 5); s.o->blad_odczytu_lba = lba; };
        CHECK(instaluj(img, m, zdjecia, sc, st) == Wynik::Weryfikacja);
        CHECK(st.diag.przyczyna == IP::Przyczyna::Odczyt && std::strcmp(st.diag.sciezka, "/czcionki/ttf/DejaVuSans.ttf") == 0);
        CHECK(st.diag.offset == 5 * 4096 && st.diag.dlugosc == 4096);
        CHECK(!st.diag.zapis_trwaly);                // PSF wylacza zapis trwaly po bledzie odczytu (psf.cpp pobierz_blok)
        opis(st, "4. blad odczytu nosnika w VERIFY:");
        CHECK(!znacznik_ukonczenia(st));
        zamknij(st);
    }
    // 5. Blad zapisu i blad FLUSH w SYSTEMFILES -> etap Zapis z plikiem; brak znacznika ukonczenia.
    for (int tryb = 0; tryb < 2; ++tryb) {
        Stan st; Scenariusz sc; sc.galeria = false;
        sc.przed_plikami = [tryb](Stan& s) { if (tryb == 0) s.o->blad_zapisu_po = s.o->zapisy + 400; else s.o->blad_flush = true; };
        const Wynik w = instaluj(img, m, zdjecia, sc, st);
        CHECK(w == Wynik::Pliki);
        CHECK(st.diag.etap == IP::Etap::Zapis || st.diag.etap == IP::Etap::Katalogi);
        if (tryb == 0) CHECK(st.diag.przyczyna == IP::Przyczyna::Zapis || st.diag.przyczyna == IP::Przyczyna::Flush ||
                             st.diag.przyczyna == IP::Przyczyna::Utworzenie || st.diag.przyczyna == IP::Przyczyna::Katalog);
        else CHECK(st.diag.przyczyna == IP::Przyczyna::Flush || st.diag.przyczyna == IP::Przyczyna::Katalog);
        CHECK(!st.diag.zapis_trwaly);                                  // PSF wylaczyl zapis trwaly po bledzie I/O
        opis(st, tryb == 0 ? "5a. blad zapisu w SYSTEMFILES:" : "5b. blad FLUSH w SYSTEMFILES:");
        st.o->blad_zapisu_po = UINT64_MAX;
        CHECK(!znacznik_ukonczenia(st));
        zamknij(st);
    }
    // 6. Plik usuniety / skrocony po zapisie (np. przez inny etap) -> missing / size-mismatch.
    for (int tryb = 0; tryb < 2; ++tryb) {
        Stan st; Scenariusz sc; sc.galeria = false;
        sc.przed_weryfikacja = [tryb](Stan&) {
            if (tryb == 0) CHECK(usun_twor("/ikonki/grajek_stop.png"));
            else CHECK(zapisz_fragment_pliku("/programy/grajek.cebula/opis.aplikacji", 4000, "X", 1));
        };
        CHECK(instaluj(img, m, zdjecia, sc, st) == Wynik::Weryfikacja);
        if (tryb == 0) CHECK(st.diag.przyczyna == IP::Przyczyna::BrakPliku && std::strcmp(st.diag.sciezka, "/ikonki/grajek_stop.png") == 0);
        else CHECK(st.diag.przyczyna == IP::Przyczyna::Rozmiar && st.diag.odczytano == 4001);
        opis(st, tryb == 0 ? "6a. plik usuniety po zapisie:" : "6b. plik zmieniony po zapisie:");
        zamknij(st);
    }
    // 7. Zapis spoza okna instalatora (inny proces miedzy krokami): PSF wylacza zapis trwaly i instalacja
    //    konczy sie bledem przed znacznikiem - nigdy cichym sukcesem.
    {
        Stan st; Scenariusz sc; sc.galeria = false;
        sc.przed_weryfikacja = [](Stan&) {
            g_okno_instalatora = false;
            (void)zapewnij_katalog_startowy("/ustawienia");
            (void)utworz_plik("/ustawienia/obcy.cfg");
            CHECK(!zapisz_do_pliku("/ustawienia/obcy.cfg", "abc", 3));
            g_okno_instalatora = true;
            CHECK(!psf_zapis_trwaly_aktywny());
            CHECK(!psf_porzuc_czyste_bloki_cache());                 // brudne bloki: nic nie jest porzucane
        };
        const Wynik w = instaluj(img, m, zdjecia, sc, st);
        CHECK(w == Wynik::Metadane);
        CHECK(!znacznik_ukonczenia(st));
        std::printf("7. zapis poza oknem instalatora: zatrzymanie w VERIFY (FLUSH), znacznik nie zapisany\n");
        zamknij(st);
    }
    // 8. Pelny dysk: najmniejszy uklad (BSP2 128 MiB) zapelniony przed SYSTEMFILES -> blad zapisu z plikiem.
    {
        Stan st; Scenariusz sc; sc.galeria = false; sc.mib = 512 + 1 + 128 + 1;
        sc.przed_plikami = [](Stan&) {
            CHECK(utworz_plik("/wypelniacz"));
            std::vector<char> mb(1u << 20, 'W');
            uint64_t off = 0;
            while (psf_wolne_bajty() > (4u << 20) && zapisz_fragment_pliku("/wypelniacz", off, mb.data(), uint32_t(mb.size()))) off += mb.size();
            CHECK(psf_flush_cache());
        };
        CHECK(instaluj(img, m, zdjecia, sc, st) == Wynik::Pliki);
        CHECK(st.diag.etap == IP::Etap::Zapis && (st.diag.przyczyna == IP::Przyczyna::Zapis || st.diag.przyczyna == IP::Przyczyna::Utworzenie));
        opis(st, "8. pelny dysk:");
        CHECK(!znacznik_ukonczenia(st));
        zamknij(st);
    }
    // 9. Pelna tablica wezlow (1024) przed SYSTEMFILES -> nie mozna utworzyc pliku / katalogu.
    {
        Stan st; Scenariusz sc; sc.galeria = false;
        sc.przed_plikami = [](Stan&) {
            CHECK(zapewnij_katalog_startowy("/tymczasowe"));
            char p[64];
            unsigned n = 0;
            for (unsigned i = 0; i < 1100; ++i) { std::snprintf(p, sizeof(p), "/tymczasowe/w%04u", i); if (!utworz_plik(p)) break; ++n; }
            CHECK(n > 800 && n < 1024);
            CHECK(psf_flush_cache());
        };
        CHECK(instaluj(img, m, zdjecia, sc, st) == Wynik::Pliki);
        CHECK(st.diag.przyczyna == IP::Przyczyna::Utworzenie || st.diag.przyczyna == IP::Przyczyna::Katalog);
        opis(st, "9. pelna tablica wezlow:");
        zamknij(st);
    }
    // 10. Nazwy na granicy limitu: 55 B (polskie znaki, 2 B/znak) przechodza, 56 B jest odrzucane przy utworzeniu.
    {
        std::string n55;
        while (n55.size() + 2 <= 51) n55 += "ż";
        n55 += ".bin";
        while (n55.size() < 55) n55.insert(0, "a");
        CHECK(n55.size() == 55);
        std::vector<uint8_t> d(5000, 0x33);
        {
            Stan st; Scenariusz sc; sc.galeria = false;
            sc.dodatkowe = {{"/dane/sponsorze/" + n55, d}};
            CHECK(instaluj(img, m, zdjecia, sc, st) == Wynik::OK);
            zamknij(st);
        }
        {
            Stan st; Scenariusz sc; sc.galeria = false;
            sc.dodatkowe = {{"/dane/sponsorze/x" + n55, d}};
            CHECK(instaluj(img, m, zdjecia, sc, st) == Wynik::Pliki);
            CHECK(st.diag.przyczyna == IP::Przyczyna::Utworzenie);
            opis(st, "10. nazwa 56 B:");
            zamknij(st);
        }
        std::printf("10. nazwa 55 B: zainstalowana i zweryfikowana\n");
    }
    // 11. Uszkodzone metadane na nosniku (wezel pliku systemowego) -> weryfikacja metadanych, bez znacznika.
    {
        Stan st; Scenariusz sc; sc.galeria = false;
        sc.przed_weryfikacja = [](Stan& s) {
            // pierwszy blok wezlow (start_wezlow) zaraz za bitmapa: nadpisanie sektora smieciami na nosniku
            std::vector<uint8_t> smieci(512, 0xA5);
            const uint64_t lba = s.bsp.start + 8 * 2;                // blok 2 partycji (bitmapa/wezly)
            CHECK(pwrite(s.o->fd, smieci.data(), 512, off_t(lba * 512)) == 512);
        };
        const Wynik w = instaluj(img, m, zdjecia, sc, st);
        CHECK(w == Wynik::Metadane);
        CHECK(!znacznik_ukonczenia(st));
        std::printf("11. uszkodzone metadane na nosniku: wykryte w VERIFY (metadane BSP2), znacznik nie zapisany\n");
        zamknij(st);
    }
    // 12. Czy VERIFY czyta nosnik, a nie bajty z pamieci podrecznej PSF? Plan mieszczacy sie w cache (40 KB,
    //     bez tapet i galerii): sektor uszkodzony na nosniku po zapisie MUSI zostac wykryty, bo faza metadanych
    //     (psf_weryfikuj_dysk_step) konczy sie wyczysc_cache_blokow(). Drugi odczyt (diagnostyczny) bez porzucenia
    //     czystego cache trafilby w pamiec - z porzuceniem daje CRC nosnika.
    for (int z_porzuceniem = 0; z_porzuceniem < 2; ++z_porzuceniem) {
        Stan st; Scenariusz sc; sc.galeria = false; sc.tapety = false; sc.porzuc_cache = z_porzuceniem != 0;
        std::vector<uint8_t> d(40000);
        for (size_t i = 0; i < d.size(); ++i) d[i] = uint8_t(i * 7 + 1);
        sc.dodatkowe = {{"/dane/sponsorze/maly.bin", d}};
        sc.przed_weryfikacja = [&](Stan& s) {
            std::vector<uint8_t> zera(512, 0);
            CHECK(pwrite(s.o->fd, zera.data(), 512, off_t(lba_bloku("/dane/sponsorze/maly.bin", 2) * 512)) == 512);
        };
        const std::vector<Wpis> pusty;
        CHECK(instaluj(img, pusty, zdjecia, sc, st) == Wynik::Weryfikacja && st.diag.przyczyna == IP::Przyczyna::Crc);
        CHECK(std::strcmp(st.diag.sciezka, "/dane/sponsorze/maly.bin") == 0 && st.diag.ponowny_znany);
        // drugi odczyt = to samo CRC co pierwszy (tresc nosnika) w obu wariantach: pierwszy odczyt wczytal z dysku
        CHECK(st.diag.crc_ponowny == st.diag.odczytano);
        if (z_porzuceniem) opis(st, "12. plan w cache, uszkodzony sektor na nosniku -> wykryty (odczyt z dysku):");
        CHECK(!znacznik_ukonczenia(st));
        zamknij(st);
    }
    // 13. Niestabilny odczyt: przeklamanie tylko przy pierwszym odczycie -> drugi odczyt zgodny z oczekiwanym.
    {
        Stan st; Scenariusz sc; sc.galeria = false;
        sc.przed_weryfikacja = [&](Stan& s) { s.o->przeklam_lba = lba_bloku("/shell.bur", 4); s.o->przeklam_raz = true; };
        CHECK(instaluj(img, m, zdjecia, sc, st) == Wynik::Weryfikacja);
        CHECK(st.diag.przyczyna == IP::Przyczyna::Crc && st.diag.ponowny_znany && st.diag.crc_ponowny == st.diag.oczekiwano);
        char log[384]; IP::linia_logu(st.diag, log, sizeof(log));
        CHECK(std::strstr(log, "matches-expected:unstable-read") != nullptr);
        opis(st, "13. niestabilny odczyt:");
        CHECK(!znacznik_ukonczenia(st));
        zamknij(st);
    }
    // 13b. Ten sam niestabilny odczyt, ale drugi odczyt BEZ porzucenia czystego cache: trafia w przeklamane bajty
    //      zapamietane w cache i blednie wyglada na tresc nosnika - dlatego diagnostyka porzuca czysty cache.
    {
        Stan st; Scenariusz sc; sc.galeria = false; sc.porzuc_cache = false;
        sc.przed_weryfikacja = [&](Stan& s) { s.o->przeklam_lba = lba_bloku("/shell.bur", 4); s.o->przeklam_raz = true; };
        CHECK(instaluj(img, m, zdjecia, sc, st) == Wynik::Weryfikacja);
        CHECK(st.diag.ponowny_znany && st.diag.crc_ponowny == st.diag.odczytano && st.diag.crc_ponowny != st.diag.oczekiwano);
        std::printf("13b. drugi odczyt bez porzucenia cache: falszywie 'same-as-first' (uzasadnienie porzucenia)\n");
        zamknij(st);
    }
    // 14. Natywny blad 7 (Windows 11, plik 108/120): zrodlo wpisu planu w martwej ramce stosu. Bajty zmieniaja
    //     sie po planowaniu (dysk dostaje smieci A), a potem jeszcze w trakcie zapisu (CRC rejestru z B).
    const std::string pcm = "/programy/pcm440.cebula/opis.aplikacji";
    uint32_t indeks_pcm = 0, systemowych = 0; const std::vector<uint8_t>* pcm_dane = nullptr;
    for (const auto& e : m) {
        if (e.rodzaj != "SYSTEM") continue;
        if (e.sciezka == pcm) { indeks_pcm = systemowych; pcm_dane = &e.dane; }
        ++systemowych;
    }
    CHECK(pcm_dane && pcm_dane->size() == 130 && Storage::crc32(pcm_dane->data(), 130) == 0xb1ce25d5u);
    auto stos = [](std::vector<uint8_t>& b, uint32_t, bool po) {              // nadpisanie przez kolejne ramki
        for (size_t i = 0; i < b.size(); ++i) b[i] = uint8_t((po ? 0xA5u : 0x3Cu) ^ (i * 7u));
    };
    {   // 14a. przed poprawka (bez kontroli zrodla): dokladnie natywna sygnatura
        Stan st; Scenariusz sc; sc.galeria = false; sc.kontrola_zrodla = false; sc.zmienne_zrodlo = pcm; sc.mutacja_zrodla = stos;
        CHECK(instaluj(img, m, zdjecia, sc, st) == Wynik::Weryfikacja);
        CHECK(st.diag.przyczyna == IP::Przyczyna::Crc && pcm == st.diag.sciezka && st.diag.indeks_pliku == indeks_pcm);
        CHECK(st.diag.offset == 0 && st.diag.dlugosc == 130 && st.diag.ponowny_znany && st.diag.ponowny_odczyt_ok);
        CHECK(st.diag.crc_ponowny == st.diag.odczytano);                     // stabilna tresc nosnika
        CHECK(st.diag.oczekiwano != 0xb1ce25d5u && st.diag.odczytano != 0xb1ce25d5u && st.diag.oczekiwano != st.diag.odczytano);
        char log[384]; IP::linia_logu(st.diag, log, sizeof(log));
        char plik[32]; std::snprintf(plik, sizeof(plik), "file=%u/%u", indeks_pcm + 1, systemowych);
        CHECK(std::strstr(log, "same-as-first:medium-content") && std::strstr(log, plik) && std::strstr(log, "blocks=0-0"));
        opis(st, "14a. zrodlo planu na stosie, bez kontroli zrodla (natywna sygnatura):");
        CHECK(!znacznik_ukonczenia(st));
        zamknij(st);
    }
    {   // 14b. po poprawce: ta sama mutacja zatrzymana w SYSTEMFILES, oczekiwane CRC = zamierzone bajty
        Stan st; Scenariusz sc; sc.galeria = false; sc.zmienne_zrodlo = pcm; sc.mutacja_zrodla = stos;
        CHECK(instaluj(img, m, zdjecia, sc, st) == Wynik::Pliki);
        CHECK(st.diag.etap == IP::Etap::Zapis && st.diag.przyczyna == IP::Przyczyna::ZrodloZmienione && pcm == st.diag.sciezka);
        CHECK(st.diag.oczekiwano == 0xb1ce25d5u && st.diag.odczytano != 0xb1ce25d5u && st.diag.indeks_pliku == indeks_pcm);
        { psf_metadane md{}; CHECK(!pobierz_metadane_tworu(pcm.c_str(), &md)); }   // bez polowicznego pliku
        char log[384]; IP::linia_logu(st.diag, log, sizeof(log));
        CHECK(std::strstr(log, "phase=WRITE cause=source-changed") && std::strstr(log, "expected=0xb1ce25d5"));
        opis(st, "14b. ta sama mutacja z kontrola zrodla:");
        CHECK(!znacznik_ukonczenia(st));
        zamknij(st);
    }
    {   // 14c. spojne smieci (zmiana tylko przed zapisem): bez kontroli instalacja "przechodzi" z bledna trescia,
        //      z kontrola - blad przed znacznikiem. Dlatego oczekiwane CRC pochodzi z migawki planu.
        auto przed = [](std::vector<uint8_t>& b, uint32_t, bool po) { if (!po) for (size_t i = 0; i < b.size(); ++i) b[i] = uint8_t(0x5A ^ i); };
        {
            Stan st; Scenariusz sc; sc.galeria = false; sc.tapety = false; sc.kontrola_zrodla = false; sc.zmienne_zrodlo = pcm; sc.mutacja_zrodla = przed;
            CHECK(instaluj(img, m, zdjecia, sc, st) == Wynik::OK);
            char b[130]; uint32_t got = 0;
            CHECK(czytaj_fragment_pliku(pcm.c_str(), 0, b, 130, &got) && got == 130 && std::memcmp(b, pcm_dane->data(), 130) != 0);
            zamknij(st);
        }
        Stan st; Scenariusz sc; sc.galeria = false; sc.tapety = false; sc.zmienne_zrodlo = pcm; sc.mutacja_zrodla = przed;
        CHECK(instaluj(img, m, zdjecia, sc, st) == Wynik::Pliki && st.diag.przyczyna == IP::Przyczyna::ZrodloZmienione);
        CHECK(!znacznik_ukonczenia(st));
        std::printf("14c. spojne smieci: bez kontroli zrodla falszywy sukces, z kontrola blad SYSTEMFILES\n");
        zamknij(st);
    }
    ::unlink(img.c_str());
    std::printf("test_instalacja_host: %u sprawdzen OK\n", checks);
    return 0;
}
