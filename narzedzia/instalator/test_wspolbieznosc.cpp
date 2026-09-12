// Hostowy test regresyjny wspoldzielonej przestrzeni roboczej formatera
// (FormatterWorkspace + GuardStorageFormatter) oraz sekwencji PSF/storage.
//
// Budowanie (jak installer-unit-test):
//   g++ -std=c++17 -O1 -g -Wall -Wextra -DBURSZTYN_PSF_HOST_TEST
//       -fsanitize=address,undefined
//       narzedzia/instalator/test_wspolbieznosc.cpp storage_format.cpp psf.cpp
//       -o .installertest/test-wspolbieznosc
//
// Pokrywa (poziom hosta, jednowatkowo):
//   1. dwa kolejne odczyty GPT (re-uzycie workspace bez zatrucia),
//   2. zapis/odczyt grubenv w trakcie innych operacji storage,
//   3. zagniezdzone wejscie w GuardStorageFormatter (glebokosc PID-reentrant),
//   4. blad backendu w srodku operacji (fail_after) bez zatrucia blokady,
//   5. przeplot operacji PSF (pliki) i storage (GPT/grubenv),
//   6. diagnostyka wlascicielstwa blokady (owner/depth/site).
//
// Sciezki OTA (GuardOTA) wymagaja srodowiska jadra i sa weryfikowane
// audytem statycznym (narzedzia/test_wspolbieznosc.py) oraz zachowaniem
// live w QEMU (BWS 84 wywoluje zagniezdzony GuardOTA przy kazdym odswiezeniu).
#include "../../storage_format.h"
#include "../../psf.h"
#include "../../zegar-rtc.h"
#include <map>
#include <array>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cassert>

using namespace Storage;

static uint32_t s_ota_lock = 0;
static uint32_t s_ota_depth = 0;
static int s_ota_owner = -1;
static const char* s_ota_site = nullptr;

class GuardOTA {
public:
    explicit GuardOTA(const char* miejsce = nullptr)
        : aktywny_(false), pierwotny_(false), miejsce_(miejsce) {
        int my_id = 1;
        if (s_ota_owner == my_id && s_ota_depth > 0) {
            s_ota_depth++;
            aktywny_ = true;
            pierwotny_ = false;
            return;
        }
        // Inwersja blokad: jezeli formatter (nizsza warstwa 2) jest juz trzymany, odrzuc:
        if (Storage::formatter_blokada_wlasciciel() == my_id &&
            Storage::formatter_blokada_glebokosc() > 0) {
            aktywny_ = false;
            pierwotny_ = false;
            return;
        }
        while (__atomic_exchange_n(&s_ota_lock, 1U, __ATOMIC_ACQUIRE) != 0U) {}
        s_ota_owner = my_id;
        s_ota_depth = 1;
        s_ota_site = miejsce_;
        aktywny_ = true;
        pierwotny_ = true;
    }
    ~GuardOTA() {
        if (!aktywny_) return;
        if (s_ota_depth > 1) {
            s_ota_depth--;
            return;
        }
        s_ota_depth = 0;
        s_ota_owner = -1;
        s_ota_site = nullptr;
        __atomic_store_n(&s_ota_lock, 0U, __ATOMIC_RELEASE);
    }
    bool aktywny() const { return aktywny_; }
    const char* miejsce() const { return miejsce_; }
private:
    bool aktywny_;
    bool pierwotny_;
    const char* miejsce_;
};

uint32_t ota_blokada_glebokosc() { return __atomic_load_n(&s_ota_depth, __ATOMIC_ACQUIRE); }
int ota_blokada_wlasciciel() { return __atomic_load_n(&s_ota_owner, __ATOMIC_ACQUIRE); }
const char* ota_blokada_miejsce() { return __atomic_load_n(&s_ota_site, __ATOMIC_ACQUIRE); }

void* kmalloc(size_t n) { return std::malloc(n); }
void kfree(void* p) { std::free(p); }
void wypisz_log(const char*) {}
bool pobierz_czas_rtc_bezpiecznie(czas_rtc*) { return false; }
bool blokowy_zapis_instalatora() { return true; }

struct Fake {
    std::map<uint64_t, std::array<uint8_t, 512>> data;
    uint64_t calls = 0, sectors = 0, fail_after = UINT64_MAX;
    Device dev;
    explicit Fake(uint64_t n) : dev{this, n, 512, true, rd, wr, fl} {}
    static bool rd(void* c, uint64_t s, uint32_t n, void* b) {
        auto& f = *(Fake*)c;
        if (!range(s, n, f.dev.sectors)) return false;
        for (unsigned i = 0; i < n; ++i) {
            auto it = f.data.find(s + i);
            if (it == f.data.end()) std::memset((uint8_t*)b + 512 * i, 0, 512);
            else std::memcpy((uint8_t*)b + 512 * i, it->second.data(), 512);
        }
        return true;
    }
    static bool wr(void* c, uint64_t s, uint32_t n, const void* b) {
        auto& f = *(Fake*)c;
        assert(range(s, n, f.dev.sectors));
        if (f.calls++ >= f.fail_after) return false;
        f.sectors += n;
        for (unsigned i = 0; i < n; ++i)
            std::memcpy(f.data[s + i].data(), (const uint8_t*)b + 512 * i, 512);
        return true;
    }
    static bool fl(void*) { return true; }
};

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)

static bool tabele_rowne(const Table& a, const Table& b) {
    if (a.count != b.count) return false;
    for (uint32_t i = 0; i < a.count; ++i) {
        if (std::memcmp(a.entries[i].type, b.entries[i].type, 16) != 0) return false;
        if (a.entries[i].start != b.entries[i].start) return false;
        if (a.entries[i].count != b.entries[i].count) return false;
    }
    return true;
}

int main() {
    Fake disk(4ULL * 1024 * 1024 * 1024 / 512);
    Layout l{};
    uint8_t uuid[16] = {1,2,3,4,5,6,7,0x40,0x80,10,11,12,13,14,15,16};
    CHECK(plan(&disk.dev, &l));
    CHECK(gpt_write(&disk.dev, l, uuid));

    // Stan poczatkowy blokady: wolna.
    CHECK(formatter_blokada_glebokosc() == 0);
    CHECK(formatter_blokada_wlasciciel() == -1);
    CHECK(formatter_blokada_miejsce() == nullptr);

    // 1. Dwa kolejne odczyty GPT: ten sam wynik, blokada zwolniona.
    Table t1{}, t2{};
    CHECK(gpt_read(&disk.dev, &t1));
    CHECK(formatter_blokada_glebokosc() == 0);
    CHECK(formatter_blokada_wlasciciel() == -1);
    CHECK(gpt_read(&disk.dev, &t2));
    CHECK(tabele_rowne(t1, t2));
    CHECK(t1.count == 2);
    CHECK(formatter_blokada_glebokosc() == 0);

    // 2. grubenv wsrod innych operacji storage (wspoldzielony workspace).
    uint8_t efi_img[4096], kern_img[4096], cfg_img[512];
    for (unsigned i = 0; i < sizeof(efi_img); ++i) efi_img[i] = (uint8_t)(i * 7 + 1);
    for (unsigned i = 0; i < sizeof(kern_img); ++i) kern_img[i] = (uint8_t)(i * 13 + 5);
    for (unsigned i = 0; i < sizeof(cfg_img); ++i) cfg_img[i] = (uint8_t)(i + 3);
    BootFile fe{efi_img, sizeof(efi_img)}, fk{kern_img, sizeof(kern_img)}, fc{cfg_img, sizeof(cfg_img)};
    CHECK(fat_install(l.esp, fe, fk, fc));
    CHECK(esp_zapisz_grubenv(l.esp, "A", "BRAK", 0));
    char potw[4]{}, oczek[16]{}; uint32_t proby = 99;
    CHECK(esp_odczytaj_grubenv(l.esp, potw, oczek, &proby));
    CHECK(potw[0] == 'A' && std::strcmp(oczek, "BRAK") == 0 && proby == 0);
    // Kolejny odczyt GPT po operacjach FAT/grubenv: brak zatrucia workspace.
    Table t3{};
    CHECK(gpt_read(&disk.dev, &t3));
    CHECK(tabele_rowne(t1, t3));
    // I ponowny odczyt grubenv: brak przecieku miedzy operacjami.
    CHECK(esp_odczytaj_grubenv(l.esp, potw, oczek, &proby));
    CHECK(potw[0] == 'A' && std::strcmp(oczek, "BRAK") == 0 && proby == 0);
    CHECK(esp_zapisz_grubenv(l.esp, "A", "B", 1));
    CHECK(esp_odczytaj_grubenv(l.esp, potw, oczek, &proby));
    CHECK(potw[0] == 'A' && std::strcmp(oczek, "B") == 0 && proby == 1);
    CHECK(formatter_blokada_glebokosc() == 0);

    // 3. Zagniezdzone wejscie (ten sam watek): glebokosc 1 -> 2 -> 1 -> 0.
    {
        GuardStorageFormatter zew(__func__);
        CHECK(formatter_blokada_glebokosc() == 1);
        CHECK(formatter_blokada_wlasciciel() == 1);
        CHECK(formatter_blokada_miejsce() != nullptr);
        {
            GuardStorageFormatter wew(__func__);
            CHECK(wew.aktywny());
            CHECK(formatter_blokada_glebokosc() == 2);
            CHECK(formatter_blokada_wlasciciel() == 1);
        }
        CHECK(formatter_blokada_glebokosc() == 1);
        // Operacja w trakcie trzymania blokady (jak gpt_install_step): dziala.
        Table tn{};
        CHECK(gpt_read(&disk.dev, &tn));
        CHECK(tabele_rowne(t1, tn));
    }
    CHECK(formatter_blokada_glebokosc() == 0);
    CHECK(formatter_blokada_wlasciciel() == -1);
    CHECK(formatter_blokada_miejsce() == nullptr);

    // 4. Blad backendu w srodku operacji zapisu: porazka bez zatrucia blokady.
    // (S amac odczyt nie dotyka fail_after; celowo psujemy zapis GPT.)
    disk.fail_after = disk.calls;
    CHECK(!gpt_write(&disk.dev, l, uuid));
    disk.fail_after = UINT64_MAX;
    CHECK(formatter_blokada_glebokosc() == 0);
    CHECK(formatter_blokada_wlasciciel() == -1);
    CHECK(formatter_blokada_miejsce() == nullptr);
    CHECK(gpt_write(&disk.dev, l, uuid));
    Table tf{};
    CHECK(gpt_read(&disk.dev, &tf));
    CHECK(tabele_rowne(t1, tf));

    // 5. Przeplot PSF (pliki w RAM) i storage (dysk): oba poprawne.
    std::vector<uint8_t> ram(16 * 1024 * 1024);
    psf_ustaw_partycje(nullptr);
    inicjalizuj_psf(ram.data(), ram.size());
    CHECK(psf_czy_gotowy());
    CHECK(utworz_plik("/wsp"));
    const char tresc[] = "dane-testowe-wspolbieznosc";
    CHECK(zapisz_do_pliku("/wsp", tresc, sizeof(tresc)));
    Table tp{};
    CHECK(gpt_read(&disk.dev, &tp));
    CHECK(tabele_rowne(t1, tp));
    char odczyt[64]{}; uint32_t got = 0;
    CHECK(czytaj_fragment_pliku("/wsp", 0, odczyt, sizeof(tresc), &got));
    CHECK(got == sizeof(tresc) && std::memcmp(odczyt, tresc, sizeof(tresc)) == 0);
    CHECK(esp_odczytaj_grubenv(l.esp, potw, oczek, &proby));
    CHECK(potw[0] == 'A' && std::strcmp(oczek, "B") == 0 && proby == 1);

    // 6. Zagniezdzone wejscie OTA (recursive OTA entry): 1 -> 2 -> 1 -> 0
    {
        GuardOTA ota1(__func__);
        CHECK(ota1.aktywny());
        CHECK(ota_blokada_glebokosc() == 1);
        CHECK(ota_blokada_wlasciciel() == 1);
        CHECK(ota_blokada_miejsce() != nullptr);
        {
            GuardOTA ota2(__func__);
            CHECK(ota2.aktywny());
            CHECK(ota_blokada_glebokosc() == 2);
            CHECK(ota_blokada_wlasciciel() == 1);
        }
        CHECK(ota_blokada_glebokosc() == 1);
    }
    CHECK(ota_blokada_glebokosc() == 0);
    CHECK(ota_blokada_wlasciciel() == -1);
    CHECK(ota_blokada_miejsce() == nullptr);

    // 7. Status OTA w trakcie aktywnosci systemu plikow (przeplot i brak interferencji)
    {
        GuardOTA ota_guard("bws_ota_status");
        CHECK(ota_guard.aktywny());
        // Weryfikacja dysku przez OTA: odczyt GPT pod GuardOTA
        Table ota_t{};
        CHECK(gpt_read(&disk.dev, &ota_t));
        CHECK(tabele_rowne(t1, ota_t));

        // Rownolegla aktywnosc systemu plikow (PSF)
        CHECK(utworz_plik("/ota_fs.dat"));
        const char ota_dane[] = "ota-fs-interleaved-payload";
        CHECK(zapisz_do_pliku("/ota_fs.dat", ota_dane, sizeof(ota_dane)));
        char ota_odczyt[64]{}; uint32_t ota_got = 0;
        CHECK(czytaj_fragment_pliku("/ota_fs.dat", 0, ota_odczyt, sizeof(ota_dane), &ota_got));
        CHECK(ota_got == sizeof(ota_dane) && std::memcmp(ota_odczyt, ota_dane, sizeof(ota_dane)) == 0);
    }

    // 8. Naruszenie hierarchii blokad w sciezce testowej (lock-order violation):
    // Proba pobrania GuardOTA (wyzsza warstwa) podczas trzymania GuardStorageFormatter (nizsza warstwa).
    // System odrzuca operacje bez zakleszczenia.
    {
        GuardStorageFormatter fmt("test_trzyma_formatter");
        CHECK(fmt.aktywny());
        CHECK(formatter_blokada_glebokosc() == 1);

        // Proba pobrania OTA spod trzymanego Formattera -> odrzucona!
        GuardOTA bad_ota("test_nielegalne_ota");
        CHECK(!bad_ota.aktywny());
        CHECK(ota_blokada_glebokosc() == 0);
    }
    // Po zwolnieniu Formattera poprawna kolejnosc OTA -> Formatter dziala bez przeszkod:
    {
        GuardOTA good_ota("test_poprawne_ota");
        CHECK(good_ota.aktywny());
        GuardStorageFormatter good_fmt("test_poprawny_formatter");
        CHECK(good_fmt.aktywny());
        CHECK(formatter_blokada_glebokosc() == 1);
        CHECK(ota_blokada_glebokosc() == 1);
    }
    CHECK(formatter_blokada_glebokosc() == 0);
    CHECK(ota_blokada_glebokosc() == 0);

    std::printf("WSPOLBIEZNE: zdane=%u zlane=0\n", checks);
    return 0;
}
