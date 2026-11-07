/*
 * Bursztyn OS - loader programow .bur
 *
 * Loader:
 *   - bezpiecznie pobiera sciezke programu,
 *   - rezerwuje slot procesu bez wyscigu pomiedzy instancjami,
 *   - rygorystycznie waliduje naglowek BUR,
 *   - tworzy prywatna przestrzen adresowa Ring 3,
 *   - mapuje kod jako tylko do odczytu dla uzytkownika,
 *   - mapuje dane i stos jako zapisywalne,
 *   - przygotowuje prywatny stos Ring 0 i rame startowa iretq,
 *   - publikuje proces dopiero po calkowitym zakonczeniu inicjalizacji.
 */

#include "loader.h"
#include "pamiec.h"
#include "pzb.h"
#include "test_dzwieku_trace.h"

#include <stdint.h>
#include <stddef.h>

#ifndef BURSZTYN_LOADER_HOST_TEST
#include "grafika.h"
#include "heap.h"
#include "scheduler.h"
#include "fpu.h"
#include "stos_jadra.h"
#include "stack_protector.h"
#ifndef BURSZTYN_DIAG_BOOT
#define BURSZTYN_DIAG_BOOT 0
#endif
#ifndef BURSZTYN_DEBUG_LOADER_PERF
#define BURSZTYN_DEBUG_LOADER_PERF 0
#endif
#if BURSZTYN_DEBUG_LOADER_PERF
#include "sterowniki/czas/hpet.h"
extern void wypisz_log(const char*);
#endif

extern "C" void* PobierzAktualnePML4();
#include "psf.h"

/*
 * Funkcja z bezpieczenstwo.cpp.
 * Dla wywolania z Ring 3 nie wolno bezposrednio dereferencjonowac
 * wskaznika przekazanego przez aplikacje.
 */
extern bool skopiuj_string_z_uzytkownika(
    char* bufor_jadra,
    const char* ptr_uzytkownika,
    size_t max_rozmiar
);

/*
 * Opcjonalny hook VMM. Jezeli zostanie zaimplementowany, loader moze
 * calkowicie usunac niedokonczona przestrzen adresowa po bledzie.
 *
 * Bez tej funkcji loader porzuca niedokonczone PML4 zamiast ryzykowac
 * zwolnienie ramek nadal wskazywanych przez tablice stron.
 */
extern "C" void ZniszczPrzestrzenAdresowaProcesu(void* pml4)
    __attribute__((weak));

proces_t aktywny_proces;
#endif // !BURSZTYN_LOADER_HOST_TEST

void KopiujPamiec(void* cel, const void* zrodlo, uint64_t rozmiar) {
    if (rozmiar == 0) return;
    if (!cel || !zrodlo) return;
    uint8_t* c = static_cast<uint8_t*>(cel);
    const uint8_t* z = static_cast<const uint8_t*>(zrodlo);
    for (uint64_t i = 0; i < rozmiar; ++i) {
        c[i] = z[i];
    }
}

bool PorownajPamiec(const void* ptr1, const void* ptr2, uint64_t rozmiar) {
    if (rozmiar == 0) return true;
    if (!ptr1 || !ptr2) return false;
    const uint8_t* p1 = static_cast<const uint8_t*>(ptr1);
    const uint8_t* p2 = static_cast<const uint8_t*>(ptr2);
    for (uint64_t i = 0; i < rozmiar; ++i) {
        if (p1[i] != p2[i]) {
            return false;
        }
    }
    return true;
}

namespace {

constexpr uint64_t ROZMIAR_STRONY = 4096ULL;
constexpr uint64_t MASKA_STRONY = ROZMIAR_STRONY - 1ULL;

#if BURSZTYN_DEBUG_LOADER_PERF
static uint64_t loader_perf_now() {
    return hpet_dostepny() ? czas_monotoniczny_ns() : 0;
}

static void loader_perf_log(int pid, uint64_t rozmiar,
                            uint64_t total_start, uint64_t image_start,
                            uint64_t image_end, uint64_t map_start,
                            uint64_t map_end, uint64_t total_end) {
    if (!total_start || !image_start || !image_end || !map_start ||
        !map_end || !total_end) {
        return;
    }
    char line[192]{};
    size_t n = 0;
    auto text = [&](const char* value) {
        while (*value && n + 1 < sizeof(line)) line[n++] = *value++;
    };
    auto number = [&](uint64_t value) {
        char digits[24]{};
        size_t count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value && count < sizeof(digits));
        while (count && n + 1 < sizeof(line)) line[n++] = digits[--count];
    };
    text("[LOADER PERF] pid="); number(static_cast<uint64_t>(pid));
    text(" bytes="); number(rozmiar);
    text(" total_us="); number((total_end - total_start) / 1000ULL);
    text(" image_us="); number((image_end - image_start) / 1000ULL);
    text(" map_us="); number((map_end - map_start) / 1000ULL);
    line[n] = '\0';
    wypisz_log(line);
}
#endif

#ifndef BURSZTYN_LOADER_HOST_TEST
constexpr size_t MAKS_DLUGOSC_SCIEZKI = 64;
constexpr size_t MAKS_ARGUMENT_STARTOWY = 512;

/* Argument nie jest czescia ABI proces_t. Prywatna, indeksowana PID-em
   skrzynka zachowuje ABI schedulera i jest czyszczona przy kazdej rezerwacji. */
char argument_startowy_procesu[PZB_MAKS_PROCESOW][MAKS_ARGUMENT_STARTOWY] = {};
#endif // !BURSZTYN_LOADER_HOST_TEST

constexpr uint64_t MIN_ADRES_USER = RING3_BASE;
constexpr uint64_t KONIEC_ADRESOW_USER = 0x0000800000000000ULL;

/*
 * Sterta Ring 3 zaczyna sie od 32 GiB. Segmenty pliku BUR nie moga
 * wejsc w jej obszar, bo syscall przydzialu pamieci rozwija sterte
 * od tej granicy.
 */
constexpr uint64_t BAZA_STERTY_USER = RING3_HEAP_BASE;

/*
 * Stos Ring 3 znajduje sie wysoko w dolnej, kanonicznej polowie adresow.
 * Cztery strony = 16 KiB. Strona bezposrednio ponizej pozostaje
 * niezmapowana i naturalnie pelni role guard page.
 */
constexpr uint64_t BAZA_STOSU_USER = RING3_STACK_TOP;
constexpr uint64_t LICZBA_STRON_STOSU_USER = 4ULL;
constexpr uint64_t ROZMIAR_STOSU_USER =
    LICZBA_STRON_STOSU_USER * ROZMIAR_STRONY;
constexpr uint64_t LICZBA_STRON_STOSU_TEST = 16ULL;
constexpr uint64_t MAKS_ROZMIAR_STOSU_USER =
    LICZBA_STRON_STOSU_TEST * ROZMIAR_STRONY;

/*
 * Ograniczenia rozmiarow segmentow pochodza z loader.h:
 * BUR_MAKS_TEKST_ROZMIAR (64 MiB), BUR_MAKS_DANE_ROZMIAR (64 MiB),
 * BUR_MAKS_BSS_ROZMIAR (64 MiB), BUR_MAKS_CALKOWITY_ROZMIAR (128 MiB).
 */

constexpr uint64_t ROZMIAR_STOSU_JADRA = 64ULL * 1024ULL;

static_assert(sizeof(NaglowekBur) == 60,
              "NaglowekBur zmienil rozmiar - zaktualizuj format .bur");

static bool dodaj_bez_przepelnienia(
    uint64_t a,
    uint64_t b,
    uint64_t* wynik
) {
    if (!wynik) return false;
    if (a > UINT64_MAX - b) return false;

    *wynik = a + b;
    return true;
}

static bool zakres_w_przedziale(
    uint64_t poczatek,
    uint64_t rozmiar,
    uint64_t dolna_granica,
    uint64_t gorna_granica_wylaczna,
    uint64_t* koniec_wyj
) {
    if (rozmiar == 0) return false;
    if (poczatek < dolna_granica) return false;
    if (poczatek >= gorna_granica_wylaczna) return false;

    uint64_t koniec = 0;
    if (!dodaj_bez_przepelnienia(poczatek, rozmiar, &koniec)) {
        return false;
    }

    if (koniec > gorna_granica_wylaczna) return false;

    if (koniec_wyj) *koniec_wyj = koniec;
    return true;
}

static bool zakresy_nachodza(
    uint64_t a_poczatek,
    uint64_t a_koniec,
    uint64_t b_poczatek,
    uint64_t b_koniec
) {
    return a_poczatek < b_koniec &&
           b_poczatek < a_koniec;
}

#ifndef BURSZTYN_LOADER_HOST_TEST
/*
 * Prosty spinlock chroni tylko rezerwacje slotow i kontrole single-instance.
 * Nie jest trzymany podczas I/O ani alokacji pamieci.
 */
volatile uint32_t blokada_loadera = 0;

struct StanPrzerwan {
    uint64_t rflags;
};

static inline StanPrzerwan wylacz_przerwania() {
    StanPrzerwan stan{};
#ifndef BURSZTYN_LOADER_RUNTIME_HOST_TEST
    asm volatile(
        "pushfq\n\t"
        "popq %0\n\t"
        "cli"
        : "=r"(stan.rflags)
        :
        : "memory", "cc"
    );
#endif

    return stan;
}

static inline void przywroc_przerwania(StanPrzerwan stan) {
    /*
     * Przywracamy tylko stan IF. Pozostale flagi sa wlasnoscia
     * biezacego kodu i nie powinny byc nadpisywane przez helper.
     */
    if ((stan.rflags & (1ULL << 9)) != 0) {
#ifndef BURSZTYN_LOADER_RUNTIME_HOST_TEST
        asm volatile("sti" ::: "memory");
#endif
    }
}

static void zablokuj_loader() {
    while (__atomic_exchange_n(
               &blokada_loadera,
               1U,
               __ATOMIC_ACQUIRE) != 0U) {
        while (__atomic_load_n(
                   &blokada_loadera,
                   __ATOMIC_RELAXED) != 0U) {
            asm volatile("pause");
        }
    }
}

static void odblokuj_loader() {
    __atomic_store_n(
        &blokada_loadera,
        0U,
        __ATOMIC_RELEASE
    );
}

static bool takie_same_sciezki(
    const char* a,
    const char* b
) {
    if (!a || !b) return false;

    for (size_t i = 0; i < MAKS_DLUGOSC_SCIEZKI; ++i) {
        const char ca = a[i];
        const char cb = b[i];

        if (ca != cb) return false;
        if (ca == '\0') return true;
    }

    return false;
}

static bool kopiuj_sciezke_z_jadra(
    char* cel,
    const char* zrodlo
) {
    if (!cel || !zrodlo) return false;

    for (size_t i = 0; i < MAKS_DLUGOSC_SCIEZKI - 1; ++i) {
        const char c = zrodlo[i];
        cel[i] = c;

        if (c == '\0') {
            return i != 0;
        }
    }

    /*
     * Nie obcinamy sciezki po cichu. Rozne dlugie nazwy moglyby
     * w przeciwnym razie zostac uznane za ten sam proces.
     */
    cel[MAKS_DLUGOSC_SCIEZKI - 1] = '\0';
    return false;
}

static bool pobierz_bezpieczna_sciezke(
    char* cel,
    const char* sciezka,
    bool z_syscalla
) {
    if (!cel || !sciezka) return false;

    bool ok = false;

    if (z_syscalla) {
        ok = skopiuj_string_z_uzytkownika(
            cel,
            sciezka,
            MAKS_DLUGOSC_SCIEZKI
        );
    } else {
        ok = kopiuj_sciezke_z_jadra(cel, sciezka);
    }

    if (!ok) return false;
    return cel[0] != '\0';
}

static bool kopiuj_argument_startowy(char* cel, const char* zrodlo) {
    if (!cel) return false;
    cel[0] = '\0';
    if (!zrodlo) return true;
    size_t i = 0;
    for (; i + 1U < MAKS_ARGUMENT_STARTOWY && zrodlo[i] != '\0'; ++i)
        cel[i] = zrodlo[i];
    if (zrodlo[i] != '\0') {
        cel[0] = '\0';
        return false;
    }
    cel[i] = '\0';
    return true;
}

static void wyzeruj_znane_pola_procesu(
    proces_t& proces
) {
    proces.pid = 0;
    proces.poziom_zaufania = 0;
    proces.uprawnienia = 0;
    proces.przestrzen_adresowa = nullptr;
    proces.kernel_rsp = 0;
    proces.baza_stosu_jadra = 0;
    proces.szczyt_stosu_jadra = 0;
    proces.cr3 = 0;
    proces.granica_sterty = 0;
    proces.sciezka_pliku[0] = '\0';
}

static bool sciezka_juz_zarezerwowana(
    const char* sciezka
) {
    for (int i = 1; i < MAKS_PROCESOW; ++i) {
        const proces_t& proces = tablica_procesow[i];

        if (proces.stan == PROCES_PUSTY || proces.stan == PROCES_KONCZACY) continue;

        if (takie_same_sciezki(
                proces.sciezka_pliku,
                sciezka)) {
            return true;
        }
    }

    return false;
}

static int zarezerwuj_slot_procesu(
    const char* sciezka
) {
    if (!sciezka) return -1;

    const StanPrzerwan stan = wylacz_przerwania();
    zablokuj_loader();

    if (sciezka_juz_zarezerwowana(sciezka)) {
        odblokuj_loader();
        przywroc_przerwania(stan);
        return -2;
    }

    int pid = -1;

    for (int i = 1; i < MAKS_PROCESOW; ++i) {
        if (tablica_procesow[i].stan == PROCES_PUSTY) {
            pid = i;
            break;
        }
    }

    if (pid >= 0) {
        proces_t& proces = tablica_procesow[pid];

        wyzeruj_znane_pola_procesu(proces);
        fpu_reset_process(pid);
        argument_startowy_procesu[pid][0] = '\0';
        proces.pid = static_cast<uint64_t>(pid);

        size_t i = 0;
        for (; i < MAKS_DLUGOSC_SCIEZKI - 1 &&
               sciezka[i] != '\0'; ++i) {
            proces.sciezka_pliku[i] = sciezka[i];
        }
        proces.sciezka_pliku[i] = '\0';

        /*
         * Najpierw stan ZABLOKOWANY. Scheduler nie moze uruchomic
         * procesu przed zbudowaniem calej ramy i przestrzeni adresowej.
         */
        __atomic_store_n(
            &proces.stan,
            PROCES_ZABLOKOWANY,
            __ATOMIC_RELEASE
        );
    }

    odblokuj_loader();
    przywroc_przerwania(stan);
    return pid;
}

static void zwolnij_rezerwacje_procesu(
    int pid
) {
    if (pid <= 0 || pid >= MAKS_PROCESOW) return;

    const StanPrzerwan stan = wylacz_przerwania();
    zablokuj_loader();

    proces_t& proces = tablica_procesow[pid];

    wyzeruj_znane_pola_procesu(proces);
    argument_startowy_procesu[pid][0] = '\0';

    __atomic_store_n(
        &proces.stan,
        PROCES_PUSTY,
        __ATOMIC_RELEASE
    );

    odblokuj_loader();
    przywroc_przerwania(stan);
}
#endif // !BURSZTYN_LOADER_HOST_TEST

} // namespace

extern "C" const char* bur_blad_naglowka(
    const NaglowekBur& naglowek,
    uint64_t rozmiar_pliku
) {
    if (rozmiar_pliku < sizeof(NaglowekBur) ||
        rozmiar_pliku > BUR_MAKS_ROZMIAR_PLIKU) {
        return "file-size-out-of-range";
    }

    const uint8_t oczekiwana_magia[4] = {
        BUR_MAGIA_0, BUR_MAGIA_1, BUR_MAGIA_2, BUR_MAGIA_3
    };

    if (!PorownajPamiec(
            naglowek.magia,
            oczekiwana_magia,
            sizeof(oczekiwana_magia))) {
        return "invalid-magic";
    }

    /*
     * 1. Segment TEXT (kod i stale rodata).
     * Musi byc niepusty, nie przekraczac BUR_MAKS_TEKST_ROZMIAR (64 MiB),
     * a jego rozmiar, offset w pliku i adres wirtualny musza byc wyrownane
     * do pelnych stron 4 KiB.
     */
    if (naglowek.tekst_rozmiar == 0 ||
        naglowek.tekst_rozmiar > BUR_MAKS_TEKST_ROZMIAR) {
        return "text-size-out-of-range";
    }

    if ((naglowek.tekst_rozmiar & MASKA_STRONY) != 0 ||
        (naglowek.tekst_przesuniecie & MASKA_STRONY) != 0 ||
        (naglowek.tekst_wirtualny & MASKA_STRONY) != 0) {
        return "text-not-page-aligned";
    }

    if (naglowek.tekst_przesuniecie < sizeof(NaglowekBur) ||
        naglowek.tekst_przesuniecie > rozmiar_pliku) {
        return "text-file-offset-out-of-range";
    }

    uint64_t koniec_tekstu_w_pliku = 0;
    if (!dodaj_bez_przepelnienia(
            naglowek.tekst_przesuniecie,
            naglowek.tekst_rozmiar,
            &koniec_tekstu_w_pliku) ||
        koniec_tekstu_w_pliku > rozmiar_pliku) {
        return "text-file-end-overflow-or-truncated";
    }

    uint64_t koniec_tekstu_va = 0;
    if (!zakres_w_przedziale(
            naglowek.tekst_wirtualny,
            naglowek.tekst_rozmiar,
            MIN_ADRES_USER,
            BAZA_STERTY_USER,
            &koniec_tekstu_va)) {
        return "text-address-overflow-or-outside-user-image";
    }

    /*
     * 2. Segment DATA (dane zainicjalizowane i BSS).
     * Opcjonalny: jezeli rozmiar > 0, to podlega pelnej kontroli limitow,
     * wyrownania stron, nienakladania sie i sumarycznego rozmiaru pamieci.
     */
    uint64_t koniec_danych_va = naglowek.dane_wirtualny;

    if (naglowek.dane_rozmiar != 0) {
        if (naglowek.dane_rozmiar > BUR_MAKS_DANE_ROZMIAR) {
            return "data-size-out-of-range";
        }

        if ((naglowek.dane_rozmiar & MASKA_STRONY) != 0 ||
            (naglowek.dane_przesuniecie & MASKA_STRONY) != 0 ||
            (naglowek.dane_wirtualny & MASKA_STRONY) != 0) {
            return "data-not-page-aligned";
        }

        uint64_t calkowity_rozmiar_va = 0;
        if (!dodaj_bez_przepelnienia(
                naglowek.tekst_rozmiar,
                naglowek.dane_rozmiar,
                &calkowity_rozmiar_va) ||
            calkowity_rozmiar_va > BUR_MAKS_CALKOWITY_ROZMIAR) {
            return "mapped-size-overflow-or-exceeds-maximum";
        }

        if (naglowek.dane_przesuniecie < sizeof(NaglowekBur) ||
            naglowek.dane_przesuniecie > rozmiar_pliku) {
            return "data-file-offset-out-of-range";
        }

        if (!zakres_w_przedziale(
                naglowek.dane_wirtualny,
                naglowek.dane_rozmiar,
                MIN_ADRES_USER,
                BAZA_STERTY_USER,
                &koniec_danych_va)) {
            return "data-address-overflow-or-outside-user-image";
        }

        if (zakresy_nachodza(
                naglowek.tekst_wirtualny,
                koniec_tekstu_va,
                naglowek.dane_wirtualny,
                koniec_danych_va)) {
            return "segments-overlap-in-memory";
        }

        /*
         * Segment danych moze zawierac koncowa czesc zerowana (BSS).
         */
        const uint64_t pozostale_dane =
            rozmiar_pliku - naglowek.dane_przesuniecie;

        const uint64_t dane_w_pliku =
            naglowek.dane_rozmiar < pozostale_dane
                ? naglowek.dane_rozmiar
                : pozostale_dane;

        const uint64_t bss_rozmiar =
            naglowek.dane_rozmiar - dane_w_pliku;
        if (bss_rozmiar > BUR_MAKS_BSS_ROZMIAR) {
            return "zero-fill-exceeds-maximum";
        }

        if (dane_w_pliku != 0) {
            uint64_t koniec_danych_w_pliku = 0;

            if (!dodaj_bez_przepelnienia(
                    naglowek.dane_przesuniecie,
                    dane_w_pliku,
                    &koniec_danych_w_pliku)) {
                return "data-file-end-overflow";
            }

            if (zakresy_nachodza(
                    naglowek.tekst_przesuniecie,
                    koniec_tekstu_w_pliku,
                    naglowek.dane_przesuniecie,
                    koniec_danych_w_pliku)) {
                return "segments-overlap-in-file";
            }
        }
    }

    /*
     * 3. Sprawdzenie braku kolizji ze stosem Ring 3 oraz strona straznika.
     */
    const uint64_t stos_obszar_start = BAZA_STOSU_USER - ROZMIAR_STRONY;
    uint64_t stos_obszar_koniec = 0;
    if (!dodaj_bez_przepelnienia(
            BAZA_STOSU_USER,
            MAKS_ROZMIAR_STOSU_USER,
            &stos_obszar_koniec)) {
        return "stack-range-overflow";
    }

    if (zakresy_nachodza(
            naglowek.tekst_wirtualny,
            koniec_tekstu_va,
            stos_obszar_start,
            stos_obszar_koniec)) {
        return "text-overlaps-stack-guard";
    }

    if (naglowek.dane_rozmiar != 0) {
        if (zakresy_nachodza(
                naglowek.dane_wirtualny,
                koniec_danych_va,
                stos_obszar_start,
                stos_obszar_koniec)) {
            return "data-overlaps-stack-guard";
        }
    }

    /*
     * 4. Punkt wejscia musi wskazywac na kod, a nie dane lub stos.
     */
    if (naglowek.punkt_wejscia < naglowek.tekst_wirtualny ||
        naglowek.punkt_wejscia >= koniec_tekstu_va) {
        return "entry-outside-text";
    }

    /*
     * 5. Wszystkie adresy uzytkownika musza pozostac w kanonicznej,
     *    dolnej polowie przestrzeni x86_64.
     */
    if (BAZA_STOSU_USER < MIN_ADRES_USER ||
        BAZA_STOSU_USER >= KONIEC_ADRESOW_USER) {
        return "invalid-user-stack-base";
    }

    uint64_t koniec_stosu = 0;
    if (!dodaj_bez_przepelnienia(
            BAZA_STOSU_USER,
            ROZMIAR_STOSU_USER,
            &koniec_stosu) ||
        koniec_stosu > KONIEC_ADRESOW_USER) {
        return "stack-end-overflow";
    }

    return nullptr;
}

extern "C" bool bur_waliduj_naglowek(const NaglowekBur& n, uint64_t size) {
    return bur_blad_naglowka(n, size) == nullptr;
}

extern "C" void bur_formatuj_blad(char* out, size_t cap, const char* path,
                                   const BurObrazPliku& obraz) {
    if (!out || !cap) return;
    size_t pos = 0;
    auto str = [&](const char* p) {
        for (size_t i = 0; p && p[i] && pos + 1 < cap; ++i) {
            const unsigned char c = p[i];
            out[pos++] = c < 32 ? '?' : c;
        }
        out[pos] = 0;
    };
    auto num = [&](uint64_t n, unsigned base = 10) {
        char digits[24]; size_t count = 0;
        do { digits[count++] = "0123456789abcdef"[n % base]; n /= base; } while (n);
        while (count && pos + 1 < cap) out[pos++] = digits[--count];
        out[pos] = 0;
    };
    const auto& n = obraz.naglowek;
    const bool header = obraz.bajty_naglowka == sizeof(n);
    const bool magic = obraz.bajty_naglowka >= 4 && n.magia[0] == 'B' &&
        n.magia[1] == 'U' && n.magia[2] == 'R' && n.magia[3] == 0;
    uint64_t data = 0, mapped = 0;
    if (header && n.dane_przesuniecie <= obraz.rozmiar) {
        data = obraz.rozmiar - n.dane_przesuniecie;
        if (data > n.dane_rozmiar) data = n.dane_rozmiar;
    }
    str("[BUR FAIL] path='");
    for (size_t i = 0; path && path[i] && pos + 1 < cap; ++i) {
        const unsigned char c = path[i];
        out[pos++] = c < 32 || c == '\'' ? '?' : c;
    }
    out[pos] = 0;
    str("' file="); num(obraz.rozmiar);
    str(" max="); num(BUR_MAKS_ROZMIAR_PLIKU);
    str(" magic="); str(obraz.bajty_naglowka < 4 ? "unavailable" : magic ? "ok" : "bad");
    str(" version="); str(magic ? "v1-implicit" : "unavailable");
    str(" header="); str(header ? "complete" : "truncated");
    str(" text_off=0x"); num(n.tekst_przesuniecie, 16);
    str(" text_file="); num(n.tekst_rozmiar);
    str(" data_off=0x"); num(n.dane_przesuniecie, 16);
    str(" data_file="); num(data);
    str(" data_memory="); num(n.dane_rozmiar);
    str(" zero_fill="); num(n.dane_rozmiar - data);
    str(" mapped=");
    if (dodaj_bez_przepelnienia(n.tekst_rozmiar, n.dane_rozmiar, &mapped)) num(mapped);
    else str("overflow");
    str(" reason="); str(obraz.blad ? obraz.blad : "unspecified-launch-failure");
}

#ifndef BURSZTYN_LOADER_HOST_TEST
namespace {
static inline bool poprawny_naglowek_bur(
    const NaglowekBur& naglowek,
    uint64_t rozmiar_pliku
) {
    return bur_waliduj_naglowek(naglowek, rozmiar_pliku);
}

static bool mapuj_segment_z_pliku(
    void* pml4,
    uint64_t adres_wirtualny,
    uint64_t rozmiar_w_pamieci,
    const uint8_t* dane_pliku,
    uint64_t rozmiar_danych_pliku,
    uint32_t flagi
) {
    if (rozmiar_w_pamieci == 0) return true;
    if (!dane_pliku && rozmiar_danych_pliku != 0) return false;
    if (rozmiar_danych_pliku > rozmiar_w_pamieci) return false;

    uint64_t przesuniecie = 0;

    while (przesuniecie < rozmiar_w_pamieci) {
        void* ramka = ZaalokujRamke();
        if (!ramka) return false;

        const uint64_t pa = reinterpret_cast<uint64_t>(ramka);
        uint8_t* cel = static_cast<uint8_t*>(
            FizycznyZakresDoWirtualnego(
                pa,
                ROZMIAR_STRONY
            )
        );

        if (!cel) {
            ZwolnijRamke(ramka);
            return false;
        }

        for (uint64_t i = 0; i < ROZMIAR_STRONY; ++i) {
            cel[i] = 0;
        }

        if (przesuniecie < rozmiar_danych_pliku) {
            const uint64_t pozostalo =
                rozmiar_danych_pliku - przesuniecie;

            const uint64_t do_kopiowania =
                pozostalo < ROZMIAR_STRONY
                    ? pozostalo
                    : ROZMIAR_STRONY;

            KopiujPamiec(
                cel,
                dane_pliku + przesuniecie,
                do_kopiowania
            );
        }

        /*
         * Dane zapisujemy do fizycznej ramki PRZED mapowaniem.
         * Dzieki temu segment kodu mozna od razu zmapowac bez FLAGA_ZAPIS.
         */
        if (!VmmMapujNowaStroneWPrzestrzeniProcesu(
            pml4,
            reinterpret_cast<void*>(
                adres_wirtualny + przesuniecie),
            ramka,
            (flagi & FLAGA_ZAPIS) != 0)) {
            ZwolnijRamke(ramka);
            return false;
        }

        przesuniecie += ROZMIAR_STRONY;
    }

    return true;
}

static bool mapuj_stos_uzytkownika(
    void* pml4,
    uint32_t flagi,
    uint64_t liczba_stron
) {
    for (uint64_t i = 0;
         i < liczba_stron;
         ++i) {
        void* ramka = ZaalokujRamke();
        if (!ramka) return false;

        const uint64_t pa = reinterpret_cast<uint64_t>(ramka);
        uint8_t* p = static_cast<uint8_t*>(
            FizycznyZakresDoWirtualnego(
                pa,
                ROZMIAR_STRONY
            )
        );

        if (!p) {
            ZwolnijRamke(ramka);
            return false;
        }

        for (uint64_t j = 0; j < ROZMIAR_STRONY; ++j) {
            p[j] = 0;
        }

        if (!VmmMapujNowaStroneWPrzestrzeniProcesu(
            pml4,
            reinterpret_cast<void*>(
                BAZA_STOSU_USER +
                i * ROZMIAR_STRONY),
            ramka,
            (flagi & FLAGA_ZAPIS) != 0)) {
            ZwolnijRamke(ramka);
            return false;
        }
    }

    return true;
}

static void wyzeruj_rame_rejestrow(
    RejestryStanowe* rama
) {
    if (!rama) return;

    uint8_t* p =
        reinterpret_cast<uint8_t*>(rama);

    for (size_t i = 0;
         i < sizeof(RejestryStanowe);
         ++i) {
        p[i] = 0;
    }
}

static void posprzataj_przestrzen_po_bledzie(
    void* pml4_procesu
) {
    if (!pml4_procesu) return;

    /*
     * Slaby symbol pozwala wdrozyc poprawne zwalnianie drzewa stron
     * bez uzalezniania obecnej wersji loadera od nowego API VMM.
     */
    if (ZniszczPrzestrzenAdresowaProcesu) {
        ZniszczPrzestrzenAdresowaProcesu(
            pml4_procesu
        );
    }
}

} // namespace

namespace {
struct ProbaUruchomienia {
    char sciezka[MAKS_DLUGOSC_SCIEZKI]{};
    BurObrazPliku obraz{};
    bool sukces{false};
    ProbaUruchomienia() { obraz.blad = "invalid-launch-arguments"; }
    ~ProbaUruchomienia() {
        if (!sukces) {
            char rekord[640];
            bur_formatuj_blad(rekord, sizeof(rekord), sciezka, obraz);
            wypisz_log(rekord);
        }
        if (obraz.dane) kfree(obraz.dane);
    }
};
}

static bool uruchom_program_z_pliku_impl(
    const char* sciezka_pliku,
    uint8_t bzl_poziom,
    uint64_t flagi_praw,
    bool z_syscalla,
    const char* argument_startowy
) {
#if BURSZTYN_DEBUG_LOADER_PERF
    const uint64_t loader_perf_total_start = loader_perf_now();
#endif
    ProbaUruchomienia proba;
    /*
     * Bursztynowe Poziomy Zaufania maja zakres 0..5.
     * Nie korygujemy blednej wartosci po cichu.
     */
    if (bzl_poziom > 5) {
        proba.obraz.blad = "invalid-trust-level";
        return false;
    }

    auto& bezpieczna_sciezka = proba.sciezka;
    char bezpieczny_argument[MAKS_ARGUMENT_STARTOWY] = {};

    if (!pobierz_bezpieczna_sciezke(
            bezpieczna_sciezka,
            sciezka_pliku,
            z_syscalla)) {
        proba.obraz.blad = "invalid-or-overlong-app-path";
        return false;
    }

    if (!kopiuj_argument_startowy(bezpieczny_argument, argument_startowy)) {
        proba.obraz.blad = "invalid-or-overlong-launch-argument";
        return false;
    }

    bool aplikacja_testowa = true;
    static const char SCIEZKA_TEST[] = "/programy/test.cebula/test.bur";
    size_t indeks_sciezki = 0;
    while (bezpieczna_sciezka[indeks_sciezki] != '\0' &&
           SCIEZKA_TEST[indeks_sciezki] != '\0') {
        if (bezpieczna_sciezka[indeks_sciezki] !=
            SCIEZKA_TEST[indeks_sciezki]) {
            aplikacja_testowa = false;
            break;
        }
        ++indeks_sciezki;
    }
    if (bezpieczna_sciezka[indeks_sciezki] != '\0' ||
        SCIEZKA_TEST[indeks_sciezki] != '\0') {
        aplikacja_testowa = false;
    }
    const uint64_t liczba_stron_stosu = aplikacja_testowa
        ? LICZBA_STRON_STOSU_TEST
        : LICZBA_STRON_STOSU_USER;
    const uint64_t rozmiar_stosu = liczba_stron_stosu * ROZMIAR_STRONY;

    /*
     * Rezerwacja i kontrola single-instance sa jedna operacja atomowa
     * z punktu widzenia planisty.
     */
    const int pid =
        zarezerwuj_slot_procesu(
            bezpieczna_sciezka
        );

    if (pid == -2) {
        proba.obraz.blad = "already-running";
        return false;
    }

    if (pid < 0) {
        proba.obraz.blad = "no-process-slot";
        return false;
    }

#if BURSZTYN_DEBUG_LOADER_PERF
    const uint64_t loader_perf_image_start = loader_perf_now();
#endif
    if (!bsp_wczytaj_obraz_bur(bezpieczna_sciezka, &proba.obraz,
            BUR_MAKS_ROZMIAR_PLIKU, bur_blad_naglowka)) {
        zwolnij_rezerwacje_procesu(pid);
        return false;
    }
#if BURSZTYN_DEBUG_LOADER_PERF
    const uint64_t loader_perf_image_end = loader_perf_now();
#endif
    const uint64_t rozmiar_pliku = proba.obraz.rozmiar;
    const uint8_t* bufor_pliku = proba.obraz.dane;
    const NaglowekBur& naglowek = proba.obraz.naglowek;
#if BURSZTYNDIAGTESTDZWIEKU
    const bool trace_app = takie_same_sciezki(bezpieczna_sciezka,
        "/programy/test_dzwieku.cebula/test_dzwieku.bur");
    if (trace_app) {
        wypisz_log("[TEST DZWIEKU] BUR file accepted");
        wypisz_log("[TEST DZWIEKU] process created");
        wypisz_log("[TEST DZWIEKU] entry point validated");
    }
#endif
    /*
     * Prywatny stos Ring 0 alokujemy w dedykowanej galezi jadra z unmapped
     * guard page bezposrednio ponizej oraz kanarkiem na dnie.
     */
    uint64_t baza_stosu_jadra = 0;
    uint64_t szczyt_stosu_jadra = 0;

    if (!stos_jadra_przydziel(pid, &baza_stosu_jadra, &szczyt_stosu_jadra)) {
        proba.obraz.blad = "stos_jadra_przydziel-failed";
        zwolnij_rezerwacje_procesu(pid);
        return false;
    }

    RejestryStanowe* rama =
        reinterpret_cast<RejestryStanowe*>(
            szczyt_stosu_jadra -
            sizeof(RejestryStanowe)
        );

    wyzeruj_rame_rejestrow(rama);

    /*
     * Utworzenie osobnego PML4 sprawia, ze dwa programy .bur moga
     * korzystac z tych samych adresow wirtualnych bez kolizji.
     */
    void* poprzednie_pml4 =
        PobierzAktualnePML4();

    if (!poprzednie_pml4) {
        proba.obraz.blad = "PobierzAktualnePML4-failed";
        stos_jadra_zwolnij(pid);
        zwolnij_rezerwacje_procesu(pid);
        return false;
    }

    void* pml4_procesu =
        UtworzPrzestrzenAdresowaProcesu();

    if (!pml4_procesu) {
        proba.obraz.blad = "UtworzPrzestrzenAdresowaProcesu-failed";
        stos_jadra_zwolnij(pid);
        zwolnij_rezerwacje_procesu(pid);
        return false;
    }

#if BURSZTYN_DEBUG_LOADER_PERF
    const uint64_t loader_perf_map_start = loader_perf_now();
#endif

    const uint32_t flagi_kod =
        FLAGA_OBECNA |
        FLAGA_USER;

    const uint32_t flagi_dane =
        FLAGA_OBECNA |
        FLAGA_ZAPIS |
        FLAGA_USER;

    bool mapowanie_ok = true;

    /*
     * 1. Kod.
     * Segment jest mapowany bez prawa zapisu z Ring 3.
     */
    if (!mapuj_segment_z_pliku(
            pml4_procesu,
            naglowek.tekst_wirtualny,
            naglowek.tekst_rozmiar,
            bufor_pliku +
                naglowek.tekst_przesuniecie,
            naglowek.tekst_rozmiar,
            flagi_kod)) {
        proba.obraz.blad = "mapuj_segment_z_pliku-kod-failed";
        mapowanie_ok = false;
    }

    /*
     * 2. Dane.
     * Brakujaca koncowka pliku jest automatycznie zerowana,
     * co daje obecnemu formatowi BUR prosta obsluge BSS.
     */
    if (mapowanie_ok &&
        naglowek.dane_rozmiar != 0) {
        const uint64_t dostepne_dane =
            rozmiar_pliku -
            naglowek.dane_przesuniecie;

        const uint64_t kopiowane_dane =
            naglowek.dane_rozmiar <
                    dostepne_dane
                ? naglowek.dane_rozmiar
                : dostepne_dane;

        if (!mapuj_segment_z_pliku(
                pml4_procesu,
                naglowek.dane_wirtualny,
                naglowek.dane_rozmiar,
                bufor_pliku +
                    naglowek.dane_przesuniecie,
                kopiowane_dane,
                flagi_dane)) {
            proba.obraz.blad = "mapuj_segment_z_pliku-dane-failed";
            mapowanie_ok = false;
        }
#ifndef BURSZTYN_LOADER_RUNTIME_HOST_TEST
        if (mapowanie_ok && kopiowane_dane >= sizeof(uint64_t)) {
            const StanPrzerwan stan_ssp = wylacz_przerwania();
            UstawPrzestrzenAdresowa(pml4_procesu);
            (void)stack_protector_zainicjuj_segment_danych_procesu(
                static_cast<uint64_t>(pid),
                static_cast<uintptr_t>(naglowek.punkt_wejscia),
                reinterpret_cast<uint8_t*>(naglowek.dane_wirtualny),
                kopiowane_dane
            );
            UstawPrzestrzenAdresowa(poprzednie_pml4);
            przywroc_przerwania(stan_ssp);
        }
#endif
    }

    /*
     * 3. Stos Ring 3. Zwykle aplikacje zachowuja 16 KiB. Test dostaje
     * 64 KiB, poniewaz pelna regresja BursztynUI ma duze lokalne obiekty,
     * a tryb interaktywny musi bezpiecznie powtarzac ja z callbacku GUI.
     */
    if (mapowanie_ok &&
        !mapuj_stos_uzytkownika(
            pml4_procesu,
            flagi_dane,
            liczba_stron_stosu)) {
        proba.obraz.blad = "mapuj_stos_uzytkownika-failed";
        mapowanie_ok = false;
    }

    if (!mapowanie_ok) {
        posprzataj_przestrzen_po_bledzie(
            pml4_procesu
        );

        stos_jadra_zwolnij(pid);
        zwolnij_rezerwacje_procesu(pid);
        return false;
    }
#if BURSZTYNDIAGTESTDZWIEKU
    if (trace_app) wypisz_log("[TEST DZWIEKU] Ring3 pages mapped");
#endif

    /*
     * SysV AMD64: zwykla funkcja zaczyna z RSP == 16n + 8.
     * _start programow BUR jest obecnie kompilowany jak funkcja C/C++,
     * dlatego zachowujemy takie ustawienie.
     */
    const uint64_t wirtualny_szczyt_stosu =
        BAZA_STOSU_USER +
        rozmiar_stosu -
        8ULL;

    /*
     * Koncowka RejestryStanowe odpowiada ramie iretq wracajacej do Ring 3.
     *
     * GDT Bursztyna:
     *   0x1B = User Data | RPL 3
     *   0x23 = User Code | RPL 3
     */
    rama->wektor_przerwania = 0;
    rama->kod_bledu = 0;
    rama->adres_powrotu =
        naglowek.punkt_wejscia;
    rama->rejestr_cs = 0x23;
    rama->rflags = 0x202;
    rama->stary_rsp =
        wirtualny_szczyt_stosu;
    rama->stary_ss = 0x1B;

    uint64_t pml4_procesu_fiz = 0;
    if (!WirtualnyDoFizycznego(
            pml4_procesu,
            &pml4_procesu_fiz)) {
        proba.obraz.blad = "process-cr3-translation-failed";
        posprzataj_przestrzen_po_bledzie(
            pml4_procesu
        );
        stos_jadra_zwolnij(pid);
        zwolnij_rezerwacje_procesu(pid);
        return false;
    }

    /*
     * Wszystkie pola procesu musza byc gotowe zanim stan zmieni sie
     * z ZABLOKOWANY na GOTOWY.
     */
    proces_t& proces =
        tablica_procesow[pid];

    proces.pid =
        static_cast<uint64_t>(pid);
    proces.poziom_zaufania =
        bzl_poziom;
    proces.uprawnienia =
        flagi_praw;
    proces.przestrzen_adresowa =
        pml4_procesu;
    proces.kernel_rsp =
        reinterpret_cast<uint64_t>(
            rama
        );
    proces.baza_stosu_jadra =
        baza_stosu_jadra;
    proces.szczyt_stosu_jadra =
        szczyt_stosu_jadra;
    proces.cr3 =
        pml4_procesu_fiz;
    proces.granica_sterty =
        BAZA_STERTY_USER;

    if (!kopiuj_argument_startowy(
            argument_startowy_procesu[pid], bezpieczny_argument)) {
        return false;
    }

    if (takie_same_sciezki(sciezka_pliku, "/programy/aktualizacje.cebula/aktualizacje.bur")) {
#if BURSZTYN_OTA_TRACE
        wypisz_log("[OTA TRACE] Loader mapped segments, entering Ring 3\n");
#endif
    }

    /*
     * RELEASE gwarantuje, ze scheduler po zobaczeniu PROCES_GOTOWY
     * zobaczy rowniez wszystkie wyzej zapisane pola.
     */
    __atomic_store_n(
        &proces.stan,
        PROCES_GOTOWY,
        __ATOMIC_RELEASE
    );
#if BURSZTYNDIAGTESTDZWIEKU
    if (trace_app) wypisz_log("[TEST DZWIEKU] process scheduled (ready)");
#endif

#if BURSZTYN_DIAG_BOOT
    if (takie_same_sciezki(sciezka_pliku, "/menedzer_okien.bur")) {
        char msg[64] = "[WM-BOOT] process-created pid=";
        size_t n = 0;
        // PID is bounded by MAKS_PROCESOW; avoid allocation during boot.
        while (msg[n]) ++n;
        char digits[12]; unsigned count = 0, value = static_cast<unsigned>(pid);
        do { digits[count++] = '0' + value % 10; value /= 10; } while (value);
        while (count) msg[n++] = digits[--count];
        msg[n] = 0;
        wypisz_log(msg);
    }
#endif

    proba.sukces = true;

#if BURSZTYN_DEBUG_LOADER_PERF
    const uint64_t loader_perf_map_end = loader_perf_now();
    loader_perf_log(pid, rozmiar_pliku, loader_perf_total_start,
                    loader_perf_image_start, loader_perf_image_end,
                    loader_perf_map_start, loader_perf_map_end,
                    loader_perf_now());
#endif
    return true;
}

extern "C" bool bws_uruchom_program_z_pliku(
    const char* sciezka_pliku,
    uint8_t bzl_poziom,
    uint64_t flagi_praw,
    bool z_syscalla
) {
    return uruchom_program_z_pliku_impl(
        sciezka_pliku, bzl_poziom, flagi_praw, z_syscalla, nullptr);
}

extern "C" bool bws_uruchom_program_z_pliku_z_argumentem(
    const char* sciezka_pliku,
    uint8_t bzl_poziom,
    uint64_t flagi_praw,
    bool z_syscalla,
    const char* argument_startowy
) {
    if (!argument_startowy || argument_startowy[0] == '\0') return false;
    return uruchom_program_z_pliku_impl(
        sciezka_pliku, bzl_poziom, flagi_praw, z_syscalla,
        argument_startowy);
}

extern "C" bool loader_pobierz_argument_startowy(
    int pid,
    char* wynik,
    size_t pojemnosc
) {
    if (pid <= 0 || pid >= MAKS_PROCESOW || !wynik || pojemnosc == 0)
        return false;
    const char* zrodlo = argument_startowy_procesu[pid];
    size_t i = 0;
    for (; i + 1U < pojemnosc && zrodlo[i] != '\0'; ++i)
        wynik[i] = zrodlo[i];
    if (zrodlo[i] != '\0') {
        wynik[0] = '\0';
        return false;
    }
    wynik[i] = '\0';
    return i != 0;
}

extern "C" int loader_przekaz_argument_uruchomionemu(
    const char* sciezka_pliku,
    const char* argument_startowy
) {
    if (!sciezka_pliku || !argument_startowy || argument_startowy[0] == '\0')
        return -1;
    char kopia[MAKS_ARGUMENT_STARTOWY] = {};
    if (!kopiuj_argument_startowy(kopia, argument_startowy)) return -1;
    const StanPrzerwan stan = wylacz_przerwania();
    zablokuj_loader();
    int znaleziony = 0;
    for (int pid = 1; pid < MAKS_PROCESOW; ++pid) {
        const proces_t& proces = tablica_procesow[pid];
        if (proces.stan == PROCES_PUSTY || proces.stan == PROCES_KONCZACY)
            continue;
        if (takie_same_sciezki(proces.sciezka_pliku, sciezka_pliku)) {
            (void)kopiuj_argument_startowy(
                argument_startowy_procesu[pid], kopia);
            znaleziony = pid;
            break;
        }
    }
    odblokuj_loader();
    przywroc_przerwania(stan);
    return znaleziony;
}
#endif // !BURSZTYN_LOADER_HOST_TEST

/* =========================================================================
 * MANIFEST PACZKI .CEBULA (opis.aplikacji)
 * ========================================================================= */

static bool manifest_czy_bialy_znak(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool manifest_tekst_rowny(const char* a, const char* b) {
    if (!a || !b) return false;
    size_t i = 0;
    while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == '\0' && b[i] == '\0';
}

extern "C" bool pakiet_parsuj_manifest_tekst(
    const char* tekst,
    size_t dlugosc,
    PakietManifest* out_manifest
) {
    if (!out_manifest) return false;

    out_manifest->nazwa[0] = '\0';
    out_manifest->autor[0] = '\0';
    out_manifest->wersja[0] = '\0';
    out_manifest->plik_startowy[0] = '\0';
    out_manifest->poziom_zaufania = PZB_UZYTKOWNIK;
    out_manifest->uprawnienia = PRAWA_BRAK;

    if (!tekst || dlugosc == 0 || dlugosc > 4096) return false;

    size_t pos = 0;
    while (pos < dlugosc) {
        while (pos < dlugosc && manifest_czy_bialy_znak(tekst[pos])) ++pos;
        if (pos >= dlugosc) break;

        if (tekst[pos] == '#' || tekst[pos] == ';') {
            while (pos < dlugosc && tekst[pos] != '\n') ++pos;
            continue;
        }

        char klucz[40] = {};
        size_t klucz_len = 0;
        while (pos < dlugosc && tekst[pos] != '=' && tekst[pos] != ':' &&
               !manifest_czy_bialy_znak(tekst[pos])) {
            if (klucz_len + 1 < sizeof(klucz)) {
                klucz[klucz_len++] = tekst[pos];
            } else {
                return false;
            }
            ++pos;
        }
        klucz[klucz_len] = '\0';

        while (pos < dlugosc && manifest_czy_bialy_znak(tekst[pos]) && tekst[pos] != '\n') ++pos;
        if (pos >= dlugosc || (tekst[pos] != '=' && tekst[pos] != ':')) return false;
        ++pos;

        while (pos < dlugosc && manifest_czy_bialy_znak(tekst[pos]) && tekst[pos] != '\n') ++pos;
        if (pos >= dlugosc) break;

        if (manifest_tekst_rowny(klucz, "poziom_zaufania")) {
            while (pos < dlugosc && manifest_czy_bialy_znak(tekst[pos])) ++pos;
            uint32_t val = 0;
            bool got_digit = false;
            while (pos < dlugosc && tekst[pos] >= '0' && tekst[pos] <= '9') {
                val = val * 10 + static_cast<uint32_t>(tekst[pos] - '0');
                got_digit = true;
                ++pos;
            }
            if (got_digit && val <= PZB_MAX_POZIOM) {
                out_manifest->poziom_zaufania = static_cast<uint8_t>(val);
            }
        } else if (manifest_tekst_rowny(klucz, "nazwa") ||
                   manifest_tekst_rowny(klucz, "autor") ||
                   manifest_tekst_rowny(klucz, "wersja") ||
                   manifest_tekst_rowny(klucz, "plik_startowy")) {
            while (pos < dlugosc && manifest_czy_bialy_znak(tekst[pos])) ++pos;
            if (pos < dlugosc && tekst[pos] == '"') {
                ++pos;
                char str[64] = {};
                size_t str_len = 0;
                while (pos < dlugosc && tekst[pos] != '"' && tekst[pos] != '\n') {
                    if (str_len + 1 < sizeof(str)) {
                        str[str_len++] = tekst[pos];
                    }
                    ++pos;
                }
                if (pos < dlugosc && tekst[pos] == '"') ++pos;
                str[str_len] = '\0';

                if (manifest_tekst_rowny(klucz, "nazwa")) {
                    for (size_t i = 0; i + 1 < sizeof(out_manifest->nazwa); ++i) out_manifest->nazwa[i] = str[i];
                    out_manifest->nazwa[sizeof(out_manifest->nazwa) - 1] = '\0';
                } else if (manifest_tekst_rowny(klucz, "autor")) {
                    for (size_t i = 0; i + 1 < sizeof(out_manifest->autor); ++i) out_manifest->autor[i] = str[i];
                    out_manifest->autor[sizeof(out_manifest->autor) - 1] = '\0';
                } else if (manifest_tekst_rowny(klucz, "wersja")) {
                    for (size_t i = 0; i + 1 < sizeof(out_manifest->wersja); ++i) out_manifest->wersja[i] = str[i];
                    out_manifest->wersja[sizeof(out_manifest->wersja) - 1] = '\0';
                } else if (manifest_tekst_rowny(klucz, "plik_startowy")) {
                    for (size_t i = 0; i + 1 < sizeof(out_manifest->plik_startowy); ++i) out_manifest->plik_startowy[i] = str[i];
                    out_manifest->plik_startowy[sizeof(out_manifest->plik_startowy) - 1] = '\0';
                }
            }
        } else if (manifest_tekst_rowny(klucz, "uprawnienia")) {
            while (pos < dlugosc && manifest_czy_bialy_znak(tekst[pos])) ++pos;
            if (pos < dlugosc && tekst[pos] == '[') {
                ++pos;
                while (pos < dlugosc && tekst[pos] != ']') {
                    while (pos < dlugosc && (manifest_czy_bialy_znak(tekst[pos]) || tekst[pos] == ',')) ++pos;
                    if (pos < dlugosc && tekst[pos] == '#') {
                        while (pos < dlugosc && tekst[pos] != '\n') ++pos;
                        continue;
                    }
                    if (pos < dlugosc && tekst[pos] == '"') {
                        ++pos;
                        char prawo_nazwa[32] = {};
                        size_t pn_len = 0;
                        while (pos < dlugosc && tekst[pos] != '"' && tekst[pos] != '\n') {
                            if (pn_len + 1 < sizeof(prawo_nazwa)) {
                                prawo_nazwa[pn_len++] = tekst[pos];
                            }
                            ++pos;
                        }
                        if (pos < dlugosc && tekst[pos] == '"') ++pos;
                        prawo_nazwa[pn_len] = '\0';

                        if (manifest_tekst_rowny(prawo_nazwa, "okna")) {
                            out_manifest->uprawnienia |= PRAWO_GUI;
                        } else if (manifest_tekst_rowny(prawo_nazwa, "pliki_czytaj")) {
                            out_manifest->uprawnienia |= PRAWO_PLIKI_CZYTAJ;
                        } else if (manifest_tekst_rowny(prawo_nazwa, "pliki_zapisz")) {
                            out_manifest->uprawnienia |= PRAWO_PLIKI_ZAPISZ;
                        } else if (manifest_tekst_rowny(prawo_nazwa, "system_config")) {
                            out_manifest->uprawnienia |= PRAWO_SYSTEM_CONFIG;
                        } else if (manifest_tekst_rowny(prawo_nazwa, "siec")) {
                            out_manifest->uprawnienia |= PRAWO_SIEC;
                        } else if (manifest_tekst_rowny(prawo_nazwa, "uruchom_program")) {
                            out_manifest->uprawnienia |= PRAWO_URUCHOM_PROGRAM;
                        } else if (manifest_tekst_rowny(prawo_nazwa, "sterownik")) {
                            out_manifest->uprawnienia |= PRAWO_STEROWNIK;
                        } else if (manifest_tekst_rowny(prawo_nazwa, "debug")) {
                            out_manifest->uprawnienia |= PRAWO_DEBUG;
                        }
                    } else {
                        if (pos < dlugosc && tekst[pos] != ']') ++pos;
                    }
                }
                if (pos < dlugosc && tekst[pos] == ']') ++pos;
            }
        } else {
            while (pos < dlugosc && tekst[pos] != '\n') ++pos;
        }
    }

    return (out_manifest->plik_startowy[0] != '\0');
}

#ifndef BURSZTYN_LOADER_HOST_TEST
extern "C" bool czytaj_fragment_pliku(
    const char* sciezka,
    uint64_t offset,
    char* bufor,
    uint32_t dlugosc,
    uint32_t* odczytano
);
#else
#include <stdio.h>
#endif

extern "C" bool pakiet_wczytaj_manifest(
    const char* sciezka_programu,
    PakietManifest* out_manifest
) {
    if (!sciezka_programu || !out_manifest) return false;

    const char* ceb = nullptr;
    for (size_t i = 0; sciezka_programu[i] != '\0'; ++i) {
        if (sciezka_programu[i] == '.' &&
            sciezka_programu[i+1] == 'c' &&
            sciezka_programu[i+2] == 'e' &&
            sciezka_programu[i+3] == 'b' &&
            sciezka_programu[i+4] == 'u' &&
            sciezka_programu[i+5] == 'l' &&
            sciezka_programu[i+6] == 'a' &&
            sciezka_programu[i+7] == '/') {
            ceb = &sciezka_programu[i + 8];
            break;
        }
    }
    if (!ceb) return false;

    char manifest_sciezka[128] = {};
    size_t prefix_len = static_cast<size_t>(ceb - sciezka_programu);
    if (prefix_len + 16 >= sizeof(manifest_sciezka)) return false;

    for (size_t i = 0; i < prefix_len; ++i) {
        manifest_sciezka[i] = sciezka_programu[i];
    }
    const char opis_nazwa[] = "opis.aplikacji";
    for (size_t i = 0; opis_nazwa[i] != '\0'; ++i) {
        manifest_sciezka[prefix_len + i] = opis_nazwa[i];
    }
    manifest_sciezka[prefix_len + sizeof(opis_nazwa) - 1] = '\0';

    char bufor[2048] = {};
    uint32_t odczytano = 0;
#ifndef BURSZTYN_LOADER_HOST_TEST
    if (!czytaj_fragment_pliku(manifest_sciezka, 0, bufor, sizeof(bufor) - 1, &odczytano) || odczytano == 0) {
        return false;
    }
#else
    FILE* f = fopen(manifest_sciezka, "rb");
    if (!f) {
        const char* rel_sciezka = (manifest_sciezka[0] == '/') ? &manifest_sciezka[1] : manifest_sciezka;
        f = fopen(rel_sciezka, "rb");
    }
    if (!f) return false;
    odczytano = static_cast<uint32_t>(fread(bufor, 1, sizeof(bufor) - 1, f));
    fclose(f);
    if (odczytano == 0) return false;
#endif
    bufor[odczytano] = '\0';

    if (!pakiet_parsuj_manifest_tekst(bufor, odczytano, out_manifest)) {
        return false;
    }

    const char* base = sciezka_programu;
    for (size_t i = 0; sciezka_programu[i] != '\0'; ++i) {
        if (sciezka_programu[i] == '/') base = &sciezka_programu[i + 1];
    }

    if (!manifest_tekst_rowny(base, out_manifest->plik_startowy)) {
        return false;
    }

    return true;
}
