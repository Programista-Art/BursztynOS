// Narzedzie TESTOWE hosta zbudowane z produkcyjnego kodu jadra (jadro/fs/psf.cpp, jadro/fs/storage_format.cpp).
//
// Sluzy jako niezalezny czytnik/walidator obrazow zmodyfikowanych przez Bursztynowy Importer Plikow (Python)
// oraz do tworzenia jednorazowych obrazow testowych o ukladzie instalatora (GPT + ESP + BSP2 + znacznik).
// Nie jest czescia importera i nie jest potrzebne uzytkownikowi.
//
//   psf_obraz_host utworz <obraz> <MiB>              nowy obraz rzadki: GPT, BSP2 instalatora, katalogi startowe
//   psf_obraz_host sprawdz <obraz> [korzen...]       tylko odczyt: GPT, znacznik, montowanie jak jadro, walidacja,
//                                                    lista "KAT <sciezka>" / "PLIK <sha256> <rozmiar> <sciezka>"
//   psf_obraz_host zapisz <obraz> <sciezka> <plik>   zapis pliku przez PSF jadra (krzyzowy test czytnika Pythona)
//   psf_obraz_host katalog <obraz> <sciezka>         utworzenie katalogu przez PSF jadra
//
// Kod wyjscia: 0 = OK, 1 = blad uzycia, 2 = obraz odrzucony / niespojny, 3 = blad zapisu.
#include "storage_format.h"
#include "psf.h"
#include "psf_bootstrap.h"
#include "katalogi_uzytkownika.h"
#include "../../sterowniki/czas/zegar-rtc.h"
#include <openssl/evp.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace Storage;
void* kmalloc(size_t n) { return std::malloc(n); }
void kfree(void* p) { std::free(p); }
void wypisz_log(const char*) {}
bool pobierz_czas_rtc_bezpiecznie(czas_rtc*) { return false; }
static bool g_zapis_dozwolony = false;
bool blokowy_zapis_instalatora() { return g_zapis_dozwolony; }

struct Plik {
    int fd = -1;
    bool tylko_odczyt = true;
    uint64_t proby_zapisu = 0;
    Device dev;
    Plik(int f, uint64_t sektory, bool ro) : fd(f), tylko_odczyt(ro), dev{this, sektory, 512, !ro, rd, wr, fl} {}
    static bool rd(void* c, uint64_t s, uint32_t n, void* b) {
        auto& p = *static_cast<Plik*>(c);
        if (!range(s, n, p.dev.sectors)) return false;
        const ssize_t r = pread(p.fd, b, size_t(n) * 512, off_t(s * 512));
        return r == ssize_t(size_t(n) * 512);
    }
    static bool wr(void* c, uint64_t s, uint32_t n, const void* b) {
        auto& p = *static_cast<Plik*>(c);
        ++p.proby_zapisu;
        if (p.tylko_odczyt || !range(s, n, p.dev.sectors)) return false;
        const ssize_t r = pwrite(p.fd, b, size_t(n) * 512, off_t(s * 512));
        return r == ssize_t(size_t(n) * 512);
    }
    static bool fl(void* c) { auto& p = *static_cast<Plik*>(c); return p.tylko_odczyt || fsync(p.fd) == 0; }
};

static std::vector<uint8_t> g_ram(128u * 1024u * 1024u);   // jak ROZMIAR_RAM_DYSKU_PSF jadra

static Plik* otworz(const char* sciezka, bool ro) {
    const int fd = open(sciezka, ro ? O_RDONLY : O_RDWR);
    if (fd < 0) { std::perror(sciezka); return nullptr; }
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size % 512) { std::fprintf(stderr, "nie zwykly plik / rozmiar\n"); close(fd); return nullptr; }
    return new Plik(fd, uint64_t(st.st_size) / 512, ro);
}

// Montowanie dokladnie jak jadro zainstalowanego systemu (kernel.cpp: blokowy_znajdz_bsp + psf_weryfikuj_dysk).
static bool zamontuj(Plik& p, Partition* bsp) {
    Table t{};
    if (!gpt_read(&p.dev, &t)) { std::fprintf(stderr, "GPT: odrzucony\n"); return false; }
    for (uint32_t i = 0; i < t.count; ++i) {
        if (!equal(t.entries[i].type, BspType, 16)) continue;
        Partition cand{&p.dev, t.entries[i].start, t.entries[i].count};
        if (!complete(cand, t.uuid)) { std::fprintf(stderr, "znacznik instalacji: niekompletny\n"); continue; }
        psf_ustaw_partycje(&cand);
        inicjalizuj_psf(g_ram.data(), uint32_t(g_ram.size()));
        if (!psf_czy_gotowy() || !psf_urzadzenie_trwale()) { std::fprintf(stderr, "BSP2: montowanie odrzucone\n"); continue; }
        if (!psf_weryfikuj_dysk()) { std::fprintf(stderr, "BSP2: walidacja metadanych nieudana\n"); continue; }
        *bsp = cand;
        return true;
    }
    return false;
}

static std::string hex(const uint8_t* d, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += H[d[i] >> 4]; s += H[d[i] & 15]; }
    return s;
}

static bool przejdz(const std::string& sciezka) {
    std::vector<char> lista(64 * 1024);
    if (!wylistuj_katalog(sciezka.c_str(), lista.data(), uint32_t(lista.size()))) { std::fprintf(stderr, "lista: %s\n", sciezka.c_str()); return false; }
    std::printf("KAT %s\n", sciezka.c_str());
    const char* p = lista.data();
    while (*p) {
        const char* koniec = std::strchr(p, '\n');
        std::string linia = koniec ? std::string(p, koniec) : std::string(p);
        p = koniec ? koniec + 1 : p + linia.size();
        // Format jadra: "[KAT]  nazwa" albo "[PLIK] nazwa" (oba prefiksy maja 7 znakow).
        const bool kat = linia.rfind("[KAT]  ", 0) == 0;
        if (!kat && linia.rfind("[PLIK] ", 0) != 0) continue;
        const std::string nazwa = linia.substr(7);
        const std::string pelna = (sciezka == "/" ? "" : sciezka) + "/" + nazwa;
        if (kat) { if (!przejdz(pelna)) return false; continue; }
        psf_metadane m{};
        if (!pobierz_metadane_tworu(pelna.c_str(), &m)) { std::fprintf(stderr, "metadane: %s\n", pelna.c_str()); return false; }
        EVP_MD_CTX* c = EVP_MD_CTX_new(); EVP_DigestInit_ex(c, EVP_sha256(), nullptr);
        std::vector<char> bufor(1u << 20);
        uint64_t off = 0;
        while (off < m.rozmiar) {
            const uint32_t n = uint32_t(m.rozmiar - off < bufor.size() ? m.rozmiar - off : bufor.size());
            uint32_t got = 0;
            if (!czytaj_fragment_pliku(pelna.c_str(), off, bufor.data(), n, &got) || got != n) { EVP_MD_CTX_free(c); std::fprintf(stderr, "odczyt: %s\n", pelna.c_str()); return false; }
            EVP_DigestUpdate(c, bufor.data(), got);
            off += got;
        }
        uint8_t d[32]; unsigned dl = 0; EVP_DigestFinal_ex(c, d, &dl); EVP_MD_CTX_free(c);
        std::printf("PLIK %s %llu %s\n", hex(d, 32).c_str(), (unsigned long long)m.rozmiar, pelna.c_str());
    }
    return true;
}

static int utworz(const char* sciezka, uint64_t mib) {
    const int fd = open(sciezka, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (fd < 0) { std::perror(sciezka); return 3; }
    if (ftruncate(fd, off_t(mib * 1024 * 1024)) != 0) { std::perror("ftruncate"); return 3; }
    Plik p(fd, mib * 2048, false);
    g_zapis_dozwolony = true;
    Layout l{};
    const uint8_t uuid[16] = {0x42, 0x75, 0x72, 0x73, 0x7a, 0x74, 0x79, 0x6e, 0x54, 0x65, 0x73, 0x74, 0x01, 0x02, 0x03, 0x04};
    if (!plan(&p.dev, &l) || !gpt_write(&p.dev, l, uuid)) { std::fprintf(stderr, "GPT: zapis nieudany\n"); return 3; }
    psf_ustaw_partycje(nullptr);
    inicjalizuj_psf(g_ram.data(), uint32_t(g_ram.size()));   // obszar RAM metadanych, jak w instalatorze
    if (!psf_formatuj_instalacje(l.bsp)) { std::fprintf(stderr, "BSP2: formatowanie nieudane\n"); return 3; }
    psf_wlacz_translacje_slotow(false);
    for (size_t i = 0; i < LICZBA_KATALOGOW_SYSTEMOWYCH; ++i)
        if (!zapewnij_katalog_startowy(KATALOGI_SYSTEMOWE[i])) { std::fprintf(stderr, "katalog %s\n", KATALOGI_SYSTEMOWE[i]); return 3; }
    for (const char* k : {KatalogiUzytkownika::Domowy, KatalogiUzytkownika::Dokumenty})
        if (!zapewnij_katalog_startowy(k)) { std::fprintf(stderr, "katalog %s\n", k); return 3; }
    if (!psf_flush_cache() || !psf_znacznik_instalacji(uuid, true)) { std::fprintf(stderr, "znacznik nieudany\n"); return 3; }
    close(fd);
    std::printf("utworzono %s: BSP %llu sektorow od LBA %llu\n", sciezka, (unsigned long long)l.bsp.count, (unsigned long long)l.bsp.start);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "uzycie: zobacz naglowek pliku\n"); return 1; }
    const std::string cmd = argv[1];
    if (cmd == "utworz" && argc == 4) return utworz(argv[2], std::strtoull(argv[3], nullptr, 10));
    const bool ro = cmd == "sprawdz";
    if (!ro && cmd != "zapisz" && cmd != "katalog") return 1;
    Plik* p = otworz(argv[2], ro);
    if (!p) return 2;
    Partition bsp{};
    if (!zamontuj(*p, &bsp)) return 2;
    psf_wlacz_translacje_slotow(false);
    if (ro) {
        std::vector<std::string> korzenie;
        for (int i = 3; i < argc; ++i) korzenie.push_back(argv[i]);
        if (korzenie.empty()) korzenie.push_back("/");
        for (const auto& k : korzenie) if (!przejdz(k)) return 2;
        std::printf("WOLNE %llu\n", (unsigned long long)psf_wolne_bajty());
        if (p->proby_zapisu) { std::fprintf(stderr, "czytnik probowal zapisu (%llu) - metadane wymagaly naprawy\n", (unsigned long long)p->proby_zapisu); return 2; }
        return 0;
    }
    if (cmd == "katalog" && argc == 4) return utworz_katalog_z_pzb(argv[3], 4) && psf_flush_cache() ? 0 : 3;
    if (cmd == "zapisz" && argc == 5) {
        FILE* f = std::fopen(argv[4], "rb");
        if (!f) return 1;
        if (!utworz_plik_z_pzb(argv[3], 4)) return 3;
        std::vector<char> b(1u << 20);
        uint64_t off = 0;
        size_t n;
        while ((n = std::fread(b.data(), 1, b.size(), f)) > 0) {
            if (!zapisz_fragment_pliku(argv[3], off, b.data(), uint32_t(n))) return 3;
            off += n;
        }
        std::fclose(f);
        return psf_flush_cache() ? 0 : 3;
    }
    return 1;
}
