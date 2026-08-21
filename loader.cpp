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
#include "grafika.h"
#include "pamiec.h"
#include "heap.h"
#include "scheduler.h"
#include "pzb.h"

#include <stdint.h>
#include <stddef.h>

extern "C" void* PobierzAktualnePML4();
extern "C" uint8_t* bsp_wczytaj_plik_do_pamieci(
    const char* sciezka,
    uint64_t* rozmiar_wyj
);

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

void KopiujPamiec(void* cel, const void* zrodlo, uint64_t rozmiar);
bool PorownajPamiec(const void* ptr1, const void* ptr2, uint64_t rozmiar);

namespace {

constexpr uint64_t ROZMIAR_STRONY = 4096ULL;
constexpr uint64_t MASKA_STRONY = ROZMIAR_STRONY - 1ULL;

constexpr size_t MAKS_DLUGOSC_SCIEZKI = 64;
constexpr size_t MAKS_ARGUMENT_STARTOWY = 512;

/* Argument nie jest czescia ABI proces_t. Prywatna, indeksowana PID-em
   skrzynka zachowuje ABI schedulera i jest czyszczona przy kazdej rezerwacji. */
char argument_startowy_procesu[PZB_MAKS_PROCESOW][MAKS_ARGUMENT_STARTOWY] = {};

constexpr uint64_t MIN_ADRES_USER = 0x0000000000001000ULL;
constexpr uint64_t KONIEC_ADRESOW_USER = 0x0000800000000000ULL;

/*
 * Sterta Ring 3 zaczyna sie od 32 GiB. Segmenty pliku BUR nie moga
 * wejsc w jej obszar, bo syscall przydzialu pamieci rozwija sterte
 * od tej granicy.
 */
constexpr uint64_t BAZA_STERTY_USER = 0x0000000800000000ULL;

/*
 * Stos Ring 3 znajduje sie wysoko w dolnej, kanonicznej polowie adresow.
 * Cztery strony = 16 KiB. Strona bezposrednio ponizej pozostaje
 * niezmapowana i naturalnie pelni role guard page.
 */
constexpr uint64_t BAZA_STOSU_USER = 0x00007FFFF0000000ULL;
constexpr uint64_t LICZBA_STRON_STOSU_USER = 4ULL;
constexpr uint64_t ROZMIAR_STOSU_USER =
    LICZBA_STRON_STOSU_USER * ROZMIAR_STRONY;

/*
 * Bezpiecznik przeciwko zlosliwemu/uszkodzonemu plikowi BUR, ktory
 * zadeklarowalby wielogigabajtowy segment i wyczerpal PMM.
 * Limit mozna pozniej podniesc razem ze specyfikacja formatu BUR.
 */
constexpr uint64_t MAKS_ROZMIAR_SEGMENTU_BUR =
    256ULL * 1024ULL * 1024ULL;

constexpr uint64_t ROZMIAR_STOSU_JADRA = 16ULL * 1024ULL;

static_assert(sizeof(NaglowekBur) == 60,
              "NaglowekBur zmienil rozmiar - zaktualizuj format .bur");

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

    asm volatile(
        "pushfq\n\t"
        "popq %0\n\t"
        "cli"
        : "=r"(stan.rflags)
        :
        : "memory", "cc"
    );

    return stan;
}

static inline void przywroc_przerwania(StanPrzerwan stan) {
    /*
     * Przywracamy tylko stan IF. Pozostale flagi sa wlasnoscia
     * biezacego kodu i nie powinny byc nadpisywane przez helper.
     */
    if ((stan.rflags & (1ULL << 9)) != 0) {
        asm volatile("sti" ::: "memory");
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

static bool poprawny_naglowek_bur(
    const NaglowekBur& naglowek,
    uint64_t rozmiar_pliku
) {
    const uint8_t oczekiwana_magia[4] = {
        'B', 'U', 'R', '\0'
    };

    if (!PorownajPamiec(
            naglowek.magia,
            oczekiwana_magia,
            sizeof(oczekiwana_magia))) {
        return false;
    }

    if (naglowek.tekst_rozmiar == 0 ||
        naglowek.tekst_rozmiar > MAKS_ROZMIAR_SEGMENTU_BUR ||
        naglowek.dane_rozmiar > MAKS_ROZMIAR_SEGMENTU_BUR) {
        return false;
    }

    if ((naglowek.tekst_wirtualny & MASKA_STRONY) != 0) {
        return false;
    }

    if (naglowek.dane_rozmiar != 0 &&
        (naglowek.dane_wirtualny & MASKA_STRONY) != 0) {
        return false;
    }

    /*
     * Kod musi faktycznie znajdowac sie w calosci w pliku.
     * Niedopuszczalne jest uruchamianie przycietego kodu i uzupelnianie
     * jego konca zerami.
     */
    if (naglowek.tekst_przesuniecie < sizeof(NaglowekBur) ||
        naglowek.tekst_przesuniecie > rozmiar_pliku) {
        return false;
    }

    uint64_t koniec_tekstu_w_pliku = 0;
    if (!dodaj_bez_przepelnienia(
            naglowek.tekst_przesuniecie,
            naglowek.tekst_rozmiar,
            &koniec_tekstu_w_pliku) ||
        koniec_tekstu_w_pliku > rozmiar_pliku) {
        return false;
    }

    /*
     * Segment danych moze zawierac koncowa czesc zerowana (BSS),
     * dlatego jego rozmiar w pamieci moze byc wiekszy niz liczba bajtow
     * pozostajacych w pliku.
     */
    if (naglowek.dane_rozmiar != 0) {
        if (naglowek.dane_przesuniecie < sizeof(NaglowekBur) ||
            naglowek.dane_przesuniecie > rozmiar_pliku) {
            return false;
        }
    }

    uint64_t koniec_tekstu_va = 0;
    if (!zakres_w_przedziale(
            naglowek.tekst_wirtualny,
            naglowek.tekst_rozmiar,
            MIN_ADRES_USER,
            BAZA_STERTY_USER,
            &koniec_tekstu_va)) {
        return false;
    }

    uint64_t koniec_danych_va = naglowek.dane_wirtualny;

    if (naglowek.dane_rozmiar != 0) {
        if (!zakres_w_przedziale(
                naglowek.dane_wirtualny,
                naglowek.dane_rozmiar,
                MIN_ADRES_USER,
                BAZA_STERTY_USER,
                &koniec_danych_va)) {
            return false;
        }

        if (zakresy_nachodza(
                naglowek.tekst_wirtualny,
                koniec_tekstu_va,
                naglowek.dane_wirtualny,
                koniec_danych_va)) {
            return false;
        }

        /*
         * Odrzucamy rowniez nakladajace sie fragmenty pliku.
         * Zapobiega to nietypowym obrazom, w ktorych te same bajty
         * stanowia jednoczesnie kod i inicjalizowane dane.
         */
        const uint64_t pozostale_dane =
            rozmiar_pliku - naglowek.dane_przesuniecie;

        const uint64_t dane_w_pliku =
            naglowek.dane_rozmiar < pozostale_dane
                ? naglowek.dane_rozmiar
                : pozostale_dane;

        if (dane_w_pliku != 0) {
            uint64_t koniec_danych_w_pliku = 0;

            if (!dodaj_bez_przepelnienia(
                    naglowek.dane_przesuniecie,
                    dane_w_pliku,
                    &koniec_danych_w_pliku)) {
                return false;
            }

            if (zakresy_nachodza(
                    naglowek.tekst_przesuniecie,
                    koniec_tekstu_w_pliku,
                    naglowek.dane_przesuniecie,
                    koniec_danych_w_pliku)) {
                return false;
            }
        }
    }

    /*
     * Punkt wejscia musi wskazywac na kod, a nie dane lub stos.
     */
    if (naglowek.punkt_wejscia < naglowek.tekst_wirtualny ||
        naglowek.punkt_wejscia >= koniec_tekstu_va) {
        return false;
    }

    /*
     * Wszystkie adresy uzytkownika musza pozostac w kanonicznej,
     * dolnej polowie przestrzeni x86_64.
     */
    if (BAZA_STOSU_USER < MIN_ADRES_USER ||
        BAZA_STOSU_USER >= KONIEC_ADRESOW_USER) {
        return false;
    }

    uint64_t koniec_stosu = 0;
    if (!dodaj_bez_przepelnienia(
            BAZA_STOSU_USER,
            ROZMIAR_STOSU_USER,
            &koniec_stosu) ||
        koniec_stosu > KONIEC_ADRESOW_USER) {
        return false;
    }

    return true;
}

static bool mapuj_segment_z_pliku(
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

        uint8_t* cel = static_cast<uint8_t*>(ramka);

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
        ZmapujStrone(
            reinterpret_cast<void*>(
                adres_wirtualny + przesuniecie),
            ramka,
            flagi
        );

        przesuniecie += ROZMIAR_STRONY;
    }

    return true;
}

static bool mapuj_stos_uzytkownika(
    uint32_t flagi
) {
    for (uint64_t i = 0;
         i < LICZBA_STRON_STOSU_USER;
         ++i) {
        void* ramka = ZaalokujRamke();
        if (!ramka) return false;

        uint8_t* p = static_cast<uint8_t*>(ramka);

        for (uint64_t j = 0; j < ROZMIAR_STRONY; ++j) {
            p[j] = 0;
        }

        ZmapujStrone(
            reinterpret_cast<void*>(
                BAZA_STOSU_USER +
                i * ROZMIAR_STRONY),
            ramka,
            flagi
        );
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

void KopiujPamiec(
    void* cel,
    const void* zrodlo,
    uint64_t rozmiar
) {
    if (rozmiar == 0) return;
    if (!cel || !zrodlo) return;

    uint8_t* c =
        static_cast<uint8_t*>(cel);

    const uint8_t* z =
        static_cast<const uint8_t*>(zrodlo);

    for (uint64_t i = 0; i < rozmiar; ++i) {
        c[i] = z[i];
    }
}

bool PorownajPamiec(
    const void* ptr1,
    const void* ptr2,
    uint64_t rozmiar
) {
    if (rozmiar == 0) return true;
    if (!ptr1 || !ptr2) return false;

    const uint8_t* p1 =
        static_cast<const uint8_t*>(ptr1);

    const uint8_t* p2 =
        static_cast<const uint8_t*>(ptr2);

    for (uint64_t i = 0; i < rozmiar; ++i) {
        if (p1[i] != p2[i]) {
            return false;
        }
    }

    return true;
}

static bool uruchom_program_z_pliku_impl(
    const char* sciezka_pliku,
    uint8_t bzl_poziom,
    uint64_t flagi_praw,
    bool z_syscalla,
    const char* argument_startowy
) {
    /*
     * Bursztynowe Poziomy Zaufania maja zakres 0..5.
     * Nie korygujemy blednej wartosci po cichu.
     */
    if (bzl_poziom > 5) {
        return false;
    }

    char bezpieczna_sciezka[MAKS_DLUGOSC_SCIEZKI];
    char bezpieczny_argument[MAKS_ARGUMENT_STARTOWY] = {};

    if (!pobierz_bezpieczna_sciezke(
            bezpieczna_sciezka,
            sciezka_pliku,
            z_syscalla)) {
        return false;
    }

    if (!kopiuj_argument_startowy(bezpieczny_argument, argument_startowy))
        return false;

    /*
     * Rezerwacja i kontrola single-instance sa jedna operacja atomowa
     * z punktu widzenia planisty.
     */
    const int pid =
        zarezerwuj_slot_procesu(
            bezpieczna_sciezka
        );

    if (pid == -2) {
        wypisz_log(
            "[LOADER] Aplikacja juz dziala! "
            "Blokada wielu instancji."
        );
        return false;
    }

    if (pid < 0) {
        return false;
    }

    uint64_t rozmiar_pliku = 0;

    uint8_t* bufor_pliku =
        bsp_wczytaj_plik_do_pamieci(
            bezpieczna_sciezka,
            &rozmiar_pliku
        );

    if (!bufor_pliku ||
        rozmiar_pliku < sizeof(NaglowekBur)) {
        zwolnij_rezerwacje_procesu(pid);
        return false;
    }

    /*
     * Naglowek kopiujemy do poprawnie wyrownanej lokalnej struktury.
     * Plik jest tablica bajtow i nie zakladamy wyrownania jego adresu.
     */
    NaglowekBur naglowek{};
    KopiujPamiec(
        &naglowek,
        bufor_pliku,
        sizeof(naglowek)
    );

    if (!poprawny_naglowek_bur(
            naglowek,
            rozmiar_pliku)) {
        zwolnij_rezerwacje_procesu(pid);
        return false;
    }

    /*
     * Prywatny stos Ring 0 alokujemy jeszcze na aktualnej przestrzeni
     * jadra. Nie zalezymy wtedy od tego, czy nowa przestrzen procesu
     * ma juz poprawnie skopiowane mapowania kernel-space.
     */
    uint8_t* baza_stosu_jadra =
        static_cast<uint8_t*>(
            kmalloc(ROZMIAR_STOSU_JADRA)
        );

    if (!baza_stosu_jadra) {
        zwolnij_rezerwacje_procesu(pid);
        return false;
    }

    const uint64_t szczyt_stosu_jadra =
        reinterpret_cast<uint64_t>(
            baza_stosu_jadra +
            ROZMIAR_STOSU_JADRA
        );

    if (szczyt_stosu_jadra <
        reinterpret_cast<uint64_t>(
            baza_stosu_jadra) ||
        ROZMIAR_STOSU_JADRA <
            sizeof(RejestryStanowe)) {
        kfree(baza_stosu_jadra);
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
        kfree(baza_stosu_jadra);
        zwolnij_rezerwacje_procesu(pid);
        return false;
    }

    void* pml4_procesu =
        UtworzPrzestrzenAdresowaProcesu();

    if (!pml4_procesu) {
        kfree(baza_stosu_jadra);
        zwolnij_rezerwacje_procesu(pid);
        return false;
    }

    /*
     * ZmapujStrone() pracuje na aktualnym CR3. Na czas budowania nowej
     * przestrzeni blokujemy przerwania, aby timer nie przelaczyl procesu
     * podczas gdy CR3 wskazuje na niedokonczona hierarchie stron.
     */
    const StanPrzerwan stan_przerwan =
        wylacz_przerwania();

    UstawPrzestrzenAdresowa(
        pml4_procesu
    );

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
            naglowek.tekst_wirtualny,
            naglowek.tekst_rozmiar,
            bufor_pliku +
                naglowek.tekst_przesuniecie,
            naglowek.tekst_rozmiar,
            flagi_kod)) {
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
                naglowek.dane_wirtualny,
                naglowek.dane_rozmiar,
                bufor_pliku +
                    naglowek.dane_przesuniecie,
                kopiowane_dane,
                flagi_dane)) {
            mapowanie_ok = false;
        }
    }

    /*
     * 3. Stos Ring 3 - 16 KiB RW.
     */
    if (mapowanie_ok &&
        !mapuj_stos_uzytkownika(
            flagi_dane)) {
        mapowanie_ok = false;
    }

    /*
     * Przed jakakolwiek obsluga bledu lub publikacja procesu wracamy
     * do przestrzeni adresowej, na ktorej loader zostal wywolany.
     */
    UstawPrzestrzenAdresowa(
        poprzednie_pml4
    );

    if (!mapowanie_ok) {
        przywroc_przerwania(
            stan_przerwan
        );

        posprzataj_przestrzen_po_bledzie(
            pml4_procesu
        );

        kfree(baza_stosu_jadra);
        zwolnij_rezerwacje_procesu(pid);
        return false;
    }

    /*
     * SysV AMD64: zwykla funkcja zaczyna z RSP == 16n + 8.
     * _start programow BUR jest obecnie kompilowany jak funkcja C/C++,
     * dlatego zachowujemy takie ustawienie.
     */
    const uint64_t wirtualny_szczyt_stosu =
        BAZA_STOSU_USER +
        ROZMIAR_STOSU_USER -
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
        reinterpret_cast<uint64_t>(
            baza_stosu_jadra
        );
    proces.szczyt_stosu_jadra =
        szczyt_stosu_jadra;
    proces.cr3 =
        reinterpret_cast<uint64_t>(
            pml4_procesu
        );
    proces.granica_sterty =
        BAZA_STERTY_USER;

    if (!kopiuj_argument_startowy(
            argument_startowy_procesu[pid], bezpieczny_argument)) {
        przywroc_przerwania(stan_przerwan);
        return false;
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

    przywroc_przerwania(
        stan_przerwan
    );

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
