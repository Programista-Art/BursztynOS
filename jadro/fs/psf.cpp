/*
 * Bursztyn OS - Polski System Plikow BSP2 / PSF
 *
 * Menedzer systemu plikow dzialajacy na ramdysku z opcjonalnym
 * write-through do glownego nosnika blokowego.
 *
 * Najwazniejsze zalozenia:
 *  - format BSP2 pozostaje zgodny z obecnym psf.h,
 *  - superblok znajduje sie w bloku 0,
 *  - bitmapa blokow danych zaczyna sie od bloku 1,
 *  - wezly indeksowe sa przechowywane od start_wezlow,
 *  - dane plikow i katalogow zaczynaja sie od start_danych,
 *  - wskazniki_blokow[] zawieraja RELATYWNE numery blokow danych,
 *  - id wezla 0 oznacza brak wezla,
 *  - BARK_BLOKU oznacza brak wskaznika do bloku danych.
 *
 * Bezpieczenstwo:
 *  - uszkodzony istniejacy BSP2 NIE jest automatycznie formatowany,
 *  - wszystkie adresy blokow i wezlow sa sprawdzane przed dereferencja,
 *  - sciezki i nazwy nie sa po cichu obcinane,
 *  - niepustego katalogu nie mozna usunac,
 *  - zapis pliku jest przygotowywany przed publikacja nowego rozmiaru,
 *  - niepotrzebne bloki sa zwalniane po skroceniu pliku,
 *  - zapis na dysk obejmuje tylko zmienione bloki PSF.
 *
 * Aktualny format nie posiada jeszcze dziennika transakcyjnego (journal).
 * Awaria zasilania pomiedzy zapisami kilku blokow nadal moze wymagac
 * przyszlego narzedzia fsck. Kod minimalizuje jednak zakres kazdego zapisu
 * i przed montowaniem rygorystycznie sprawdza strukture systemu plikow.
 */

#if !defined(BURSZTYN_PSF_HOST_TEST)
#include "jadro/arch/diag_opoznienia_irq.h"
#endif
#include "psf.h"
#include "blokowy.h"
#include "zegar-rtc.h"
#include "heap.h"
#include "loader.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Logger z podsystemu grafiki.
 */
void wypisz_log(const char* tekst);

namespace {

/* =========================================================================
 * STALE
 * ========================================================================= */

constexpr uint64_t ROZMIAR_SEKTORA = 512ULL;

constexpr uint8_t DOMYSLNY_PZB_PLIKU = 4U;

bool zapakuj_prawdziwy_czas_rtc(uint64_t* wynik) {
    if (!wynik) return false;
    czas_rtc czas{};
    if (!pobierz_czas_rtc_bezpiecznie(&czas)) return false;
    uint64_t wartosc = czas.rok;
    wartosc = wartosc * 100ULL + czas.miesiac;
    wartosc = wartosc * 100ULL + czas.dzien;
    wartosc = wartosc * 100ULL + czas.godziny;
    wartosc = wartosc * 100ULL + czas.minuty;
    wartosc = wartosc * 100ULL + czas.sekundy;
    *wynik = wartosc;
    return true;
}

bool znacznik_metadanych_poprawny(uint64_t flagi) {
    return (flagi & PSF_META_MAGIC_MASK) == PSF_META_MAGIC &&
           ((flagi & PSF_META_WERSJA_MASK) == PSF_META_WERSJA_1 ||
            (flagi & PSF_META_WERSJA_MASK) == PSF_META_WERSJA_2);
}

void ustaw_metadane_nowego_wezla(wezel_indeksowy* w, uint8_t pzb) {
    if (!w || pzb > 5U) return;
    uint64_t flagi = PSF_META_MAGIC | PSF_META_WERSJA_2 |
                     PSF_META_PZB_WAZNY |
                     static_cast<uint64_t>(pzb);
    uint64_t czas = 0;
    if (zapakuj_prawdziwy_czas_rtc(&czas)) {
        w->czas_utworzenia = czas;
        flagi |= PSF_META_CZAS_WAZNY;
    }
    w->flagi_zabezpieczen = flagi;
}

constexpr uint64_t PTRS_PER_BLOCK = PSF_ROZMIAR_BLOKU / sizeof(uint64_t);

// forward dla helperów pośrednich
uint64_t zaalokuj_wolny_blok_danych();
bool zwolnij_blok_danych(uint64_t id);
uint8_t* pobierz_blok(uint64_t nr_bloku, bool wczytaj_z_dysku = true);
uint8_t* pobierz_blok_danych(uint64_t id, bool wczytaj_z_dysku = true);
uint64_t absolutny_blok_danych(uint64_t id);
bool id_bloku_danych_poprawne(uint64_t id);
bool czy_blok_danych_zajety(uint64_t id);
bool wskazniki_pliku_poprawne(const wezel_indeksowy* w);
void oznacz_blok_jako_brudny(uint64_t nr);
void oznacz_wezel_jako_brudny(uint64_t id);

bool pobierz_wskaznik_bloku(const wezel_indeksowy* w, uint64_t idx, uint64_t* out){
    if(!w||!out) return false;
    if(idx < PSF_MAX_BLOKOW_W_WEZLE){
        *out = w->wskazniki_blokow[idx];
        return true;
    }
    idx -= PSF_MAX_BLOKOW_W_WEZLE;
    if(idx < PTRS_PER_BLOCK){
        if(w->blok_posredni_1==BARK_BLOKU){ *out=BARK_BLOKU; return true; }
        uint8_t* blk = pobierz_blok_danych(w->blok_posredni_1);
        if(!blk) return false;
        uint64_t* arr = reinterpret_cast<uint64_t*>(blk);
        *out = arr[idx];
        return true;
    }
    idx -= PTRS_PER_BLOCK;
    // double indirect via blok_posredni_2
    uint64_t per = PTRS_PER_BLOCK;
    uint64_t tbl = idx / per;
    uint64_t ent = idx % per;
    if(tbl >= PTRS_PER_BLOCK) return false;
    if(w->blok_posredni_2==BARK_BLOKU){ *out=BARK_BLOKU; return true; }
    uint8_t* blk1 = pobierz_blok_danych(w->blok_posredni_2);
    if(!blk1) return false;
    uint64_t* arr1 = reinterpret_cast<uint64_t*>(blk1);
    uint64_t id2 = arr1[tbl];
    if(id2==BARK_BLOKU){ *out=BARK_BLOKU; return true; }
    uint8_t* blk2 = pobierz_blok_danych(id2);
    if(!blk2) return false;
    uint64_t* arr2 = reinterpret_cast<uint64_t*>(blk2);
    *out = arr2[ent];
    return true;
}

bool ustaw_wskaznik_bloku(wezel_indeksowy* w, uint64_t idx, uint64_t blok_id, uint64_t wezel_id){
    if(!w) return false;
    if(idx < PSF_MAX_BLOKOW_W_WEZLE){
        w->wskazniki_blokow[idx]=blok_id;
        oznacz_wezel_jako_brudny(wezel_id);
        return true;
    }
    idx -= PSF_MAX_BLOKOW_W_WEZLE;
    if(idx < PTRS_PER_BLOCK){
        if(w->blok_posredni_1==BARK_BLOKU){
            uint64_t nid = zaalokuj_wolny_blok_danych();
            if(nid==BARK_BLOKU) return false;
            uint8_t* blk = pobierz_blok_danych(nid, false);
            if(!blk){ (void)zwolnij_blok_danych(nid); return false; }
            for(uint64_t i=0;i<PTRS_PER_BLOCK;i++) reinterpret_cast<uint64_t*>(blk)[i]=BARK_BLOKU;
            w->blok_posredni_1=nid;
            oznacz_wezel_jako_brudny(wezel_id);
            uint64_t abs = absolutny_blok_danych(nid);
            if(abs!=UINT64_MAX) oznacz_blok_jako_brudny(abs);
        }
        uint8_t* blk = pobierz_blok_danych(w->blok_posredni_1);
        if(!blk) return false;
        reinterpret_cast<uint64_t*>(blk)[idx]=blok_id;
        uint64_t abs = absolutny_blok_danych(w->blok_posredni_1);
        if(abs!=UINT64_MAX) oznacz_blok_jako_brudny(abs);
        return true;
    }
    idx -= PTRS_PER_BLOCK;
    uint64_t per = PTRS_PER_BLOCK;
    uint64_t tbl = idx / per;
    uint64_t ent = idx % per;
    if(tbl >= PTRS_PER_BLOCK) return false;
    if(w->blok_posredni_2==BARK_BLOKU){
        uint64_t nid = zaalokuj_wolny_blok_danych();
        if(nid==BARK_BLOKU) return false;
        uint8_t* blk = pobierz_blok_danych(nid, false);
        if(!blk){ (void)zwolnij_blok_danych(nid); return false; }
        for(uint64_t i=0;i<PTRS_PER_BLOCK;i++) reinterpret_cast<uint64_t*>(blk)[i]=BARK_BLOKU;
        w->blok_posredni_2=nid;
        oznacz_wezel_jako_brudny(wezel_id);
        uint64_t abs = absolutny_blok_danych(nid);
        if(abs!=UINT64_MAX) oznacz_blok_jako_brudny(abs);
    }
    uint8_t* blk1 = pobierz_blok_danych(w->blok_posredni_2);
    if(!blk1) return false;
    uint64_t id2 = reinterpret_cast<uint64_t*>(blk1)[tbl];
    if(id2==BARK_BLOKU){
        uint64_t nid = zaalokuj_wolny_blok_danych();
        if(nid==BARK_BLOKU) return false;
        uint8_t* blk2 = pobierz_blok_danych(nid, false);
        if(!blk2){ (void)zwolnij_blok_danych(nid); return false; }
        for(uint64_t i=0;i<PTRS_PER_BLOCK;i++) reinterpret_cast<uint64_t*>(blk2)[i]=BARK_BLOKU;
        blk1 = pobierz_blok_danych(w->blok_posredni_2);
        if(!blk1){ (void)zwolnij_blok_danych(nid); return false; }
        reinterpret_cast<uint64_t*>(blk1)[tbl]=nid;
        uint64_t abs1 = absolutny_blok_danych(w->blok_posredni_2);
        if(abs1!=UINT64_MAX) oznacz_blok_jako_brudny(abs1);
        uint64_t abs2 = absolutny_blok_danych(nid);
        if(abs2!=UINT64_MAX) oznacz_blok_jako_brudny(abs2);
        id2=nid;
    }
    uint8_t* blk2 = pobierz_blok_danych(id2);
    if(!blk2) return false;
    reinterpret_cast<uint64_t*>(blk2)[ent]=blok_id;
    uint64_t abs2 = absolutny_blok_danych(id2);
    if(abs2!=UINT64_MAX) oznacz_blok_jako_brudny(abs2);
    uint64_t abs1 = absolutny_blok_danych(w->blok_posredni_2);
    if(abs1!=UINT64_MAX) oznacz_blok_jako_brudny(abs1);
    return true;
}

bool zwolnij_wszystkie_bloki_wezla(wezel_indeksowy* w, uint64_t wezel_id) {
    if (!w) return false;
    // 1. Bezposrednie
    for (size_t k = 0; k < static_cast<size_t>(PSF_MAX_BLOKOW_W_WEZLE); ++k) {
        const uint64_t id = w->wskazniki_blokow[k];
        if (id != BARK_BLOKU) {
            (void)zwolnij_blok_danych(id);
            w->wskazniki_blokow[k] = BARK_BLOKU;
        }
    }
    // 2. Pojedyncze posrednie (odswiezamy wskaznik bloku w kazdej iteracji,
    //    aby zwolnij_blok_danych nie wywlaszczylo tablicy z 256-wpisowego cache LRU)
    if (w->blok_posredni_1 != BARK_BLOKU) {
        const uint64_t id1 = w->blok_posredni_1;
        for (uint64_t i = 0; i < PTRS_PER_BLOCK; ++i) {
            uint8_t* blk = pobierz_blok_danych(id1);
            if (!blk) break;
            const uint64_t child_id = reinterpret_cast<uint64_t*>(blk)[i];
            if (child_id != BARK_BLOKU && child_id != id1) {
                (void)zwolnij_blok_danych(child_id);
            }
        }
        (void)zwolnij_blok_danych(id1);
        w->blok_posredni_1 = BARK_BLOKU;
    }
    // 3. Podwojne posrednie
    if (w->blok_posredni_2 != BARK_BLOKU) {
        const uint64_t id_l1 = w->blok_posredni_2;
        for (uint64_t i = 0; i < PTRS_PER_BLOCK; ++i) {
            uint8_t* blk1 = pobierz_blok_danych(id_l1);
            if (!blk1) break;
            const uint64_t id2 = reinterpret_cast<uint64_t*>(blk1)[i];
            if (id2 != BARK_BLOKU && id2 != id_l1) {
                for (uint64_t j = 0; j < PTRS_PER_BLOCK; ++j) {
                    uint8_t* blk2 = pobierz_blok_danych(id2);
                    if (!blk2) break;
                    const uint64_t leaf_id = reinterpret_cast<uint64_t*>(blk2)[j];
                    if (leaf_id != BARK_BLOKU && leaf_id != id_l1 && leaf_id != id2) {
                        (void)zwolnij_blok_danych(leaf_id);
                    }
                }
                (void)zwolnij_blok_danych(id2);
            }
        }
        (void)zwolnij_blok_danych(id_l1);
        w->blok_posredni_2 = BARK_BLOKU;
    }
    w->blok_posredni_3 = BARK_BLOKU;
    if (wezel_id != 0) oznacz_wezel_jako_brudny(wezel_id);
    return true;
}

/* Każde I/O BSP2 jest względne wobec jawnie zamontowanej partycji. */
Storage::Partition bsp_partycja{};
constexpr uint64_t BSP_START_LBA = 0; // LBA względne; translacja wyłącznie w Storage::read/write.

constexpr uint32_t DOMYSLNY_LIMIT_SEKTOROW_OPERACJI = 32U;

constexpr uint64_t MAX_LOADER_BUF =
    512ULL * 1024ULL;

constexpr size_t MAKS_DLUGOSC_SCIEZKI = 512;

constexpr size_t MAKS_BRUDNYCH_BLOKOW = 256;

/*
 * Minimalna liczba blokow wymagana do sensownego podzialu:
 * superblok + bitmapa + wezly + dane.
 */
constexpr uint64_t MINIMALNA_LICZBA_BLOKOW = 10;

constexpr size_t POJEMNOSC_CACHE_BLOKOW = 256;

struct WpisCacheBloku {
    uint64_t nr_bloku;
    bool brudny;
    uint32_t wiek;
    alignas(16) uint8_t dane[PSF_ROZMIAR_BLOKU];
};

WpisCacheBloku* wpisy_cache = nullptr;
uint32_t licznik_wieku_cache = 0;

static void alokuj_cache_jesli_brak() {
    if (!wpisy_cache) {
        wpisy_cache = static_cast<WpisCacheBloku*>(kmalloc(POJEMNOSC_CACHE_BLOKOW * sizeof(WpisCacheBloku)));
        if (wpisy_cache) {
            for (size_t i = 0; i < POJEMNOSC_CACHE_BLOKOW; ++i) {
                wpisy_cache[i].nr_bloku = UINT64_MAX;
                wpisy_cache[i].brudny = false;
                wpisy_cache[i].wiek = 0;
            }
        }
    }
}

void wyczysc_cache_blokow() {
    alokuj_cache_jesli_brak();
    if (wpisy_cache) {
        for (size_t i = 0; i < POJEMNOSC_CACHE_BLOKOW; ++i) {
            wpisy_cache[i].nr_bloku = UINT64_MAX;
            wpisy_cache[i].brudny = false;
            wpisy_cache[i].wiek = 0;
        }
    }
    licznik_wieku_cache = 0;
}

/* =========================================================================
 * STAN GLOBALNY
 * ========================================================================= */

uint8_t* ram_dysk = nullptr;
uint32_t rozmiar_ram_dysku = 0;

superblok* dysk_superblok = nullptr;

uint64_t calkowita_liczba_wezlow = 0;
uint64_t calkowita_liczba_blokow_danych = 0;

uint64_t ostatni_szukany_blok_danych = 0;

bool psf_gotowy = false;

// Status is active only inside the locked base-tree operation. No global logs.
PsfStanStartu* stan_startu = nullptr;
void blad_startu(PsfBladStartu blad) {
    if(stan_startu && stan_startu->blad==PsfBladStartu::Ok)stan_startu->blad=blad;
}
struct ZakresStanuStartu {
    explicit ZakresStanuStartu(PsfStanStartu* s){stan_startu=s;}
    ~ZakresStanuStartu(){stan_startu=nullptr;}
};

/*
 * Nosnik blokowy moze byc niedostepny. BSP2 nadal dziala wtedy jako ramdysk.
 *
 * zapis_trwaly_dozwolony jest celowo ustawiany na false po wykryciu
 * uszkodzonego istniejacego BSP2. Chroni to dane na dysku przed
 * automatycznym nadpisaniem nowym formatem.
 */
bool nosnik_blokowy_dostepny = false;
bool zapis_trwaly_dozwolony = false;
bool ostatni_flush_ok = true;

/*
 * Dirty tracking.
 */
uint64_t brudne_bloki[MAKS_BRUDNYCH_BLOKOW] = {};
size_t liczba_brudnych_blokow = 0;
bool brudny_caly_system = false;

/*
 * API loadera zachowuje dotychczasowy kontrakt zwracania statycznego
 * wskaznika. Bufor jest chroniony podczas wypelniania przez blokade PSF.
 *
 * Docelowo lepszym ABI bedzie:
 *   bool bsp_wczytaj_plik(..., void* dst, size_t cap, size_t* out)
 * albo obiekt/refcount, aby calkowicie usunac wspoldzielony bufor.
 */
alignas(16)
uint8_t bufor_wymiany_plikow[MAX_LOADER_BUF] = {};

/*
 * Sektor do wstepnego odczytu superbloku.
 */
alignas(512)
uint8_t bufor_superbloku[ROZMIAR_SEKTORA] = {};

/* =========================================================================
 * SYNCHRONIZACJA
 * ========================================================================= */

/*
 * Obecny scheduler Bursztyna jest jednordzeniowy i wywlaszczajacy przez IRQ
 * timera. Sam spinlock nie wystracza: proces moglby zostac wywlaszczony
 * trzymajac blokade, a drugi proces zapetlilby sie na tym samym CPU.
 *
 * Dlatego cala operacja PSF jest wykonywana z lokalnie wylaczonym IF.
 * To jest bezpieczne funkcjonalnie, ale write-through do nosnika blokowego wydluza sekcje
 * krytyczna. Gdy pojawi sie scheduler z mutexami/usypianiem, blokade nalezy
 * zastapic mutexem PSF i przeniesc fizyczny flush do osobnego workera.
 */

uint32_t blokada_psf = 0;

struct StanPrzerwan {
    uint64_t rflags;
};

static inline StanPrzerwan wylacz_przerwania() {
    StanPrzerwan stan{};
#ifndef BURSZTYN_PSF_HOST_TEST
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

static inline void przywroc_przerwania(
    StanPrzerwan stan
) {
#ifdef BURSZTYN_PSF_HOST_TEST
    (void)stan;
#else
    if ((stan.rflags & (1ULL << 9)) != 0) {
        asm volatile(
            "sti"
            :
            :
            : "memory"
        );
    }
#endif
}

static void zablokuj_psf() {
    while (__atomic_exchange_n(
               &blokada_psf,
               1U,
               __ATOMIC_ACQUIRE) != 0U) {

        while (__atomic_load_n(
                   &blokada_psf,
                   __ATOMIC_RELAXED) != 0U) {

            asm volatile("pause");
        }
    }
}

static void odblokuj_psf() {
    __atomic_store_n(
        &blokada_psf,
        0U,
        __ATOMIC_RELEASE
    );
}

class BlokadaPSF {
public:
    BlokadaPSF()
        : stan_(wylacz_przerwania())
#if !defined(BURSZTYN_PSF_HOST_TEST)
        , pomiar_(DiagOpoznieniaIrq::Site::Psf, (stan_.rflags & (1ULL << 9)) != 0)
#endif
    {
        zablokuj_psf();
    }

    ~BlokadaPSF() {
        odblokuj_psf();
#if !defined(BURSZTYN_PSF_HOST_TEST)
        pomiar_.finish();
#endif
        przywroc_przerwania(stan_);
    }

    BlokadaPSF(const BlokadaPSF&) = delete;
    BlokadaPSF& operator=(const BlokadaPSF&) = delete;

private:
    StanPrzerwan stan_;
#if !defined(BURSZTYN_PSF_HOST_TEST)
    DiagOpoznieniaIrq::Scope pomiar_;
#endif
};

/* =========================================================================
 * NISKIE FUNKCJE PAMIECIOWE
 * ========================================================================= */

void wyzeruj_pamiec(
    void* wskaznik,
    uint64_t rozmiar
) {
    if (!wskaznik || rozmiar == 0) {
        return;
    }

    uint8_t* p =
        static_cast<uint8_t*>(
            wskaznik
        );

    for (uint64_t i = 0;
         i < rozmiar;
         ++i) {

        p[i] = 0;
    }
}

void kopiuj_pamiec(
    void* cel,
    const void* zrodlo,
    uint64_t rozmiar
) {
    if (rozmiar == 0) {
        return;
    }

    if (!cel || !zrodlo) {
        return;
    }

    uint8_t* dst =
        static_cast<uint8_t*>(
            cel
        );

    const uint8_t* src =
        static_cast<const uint8_t*>(
            zrodlo
        );

    const uintptr_t dst_adres =
        reinterpret_cast<uintptr_t>(dst);

    const uintptr_t src_adres =
        reinterpret_cast<uintptr_t>(src);

    /*
     * memmove-like copy, aby przypadkowe nakladanie zakresow nie
     * uszkadzalo danych. Porownujemy wartosci adresow jako uintptr_t,
     * a nie relacje wskaznikow nalezacych do roznych obiektow.
     */
    if (dst_adres < src_adres) {
        for (uint64_t i = 0;
             i < rozmiar;
             ++i) {

            dst[i] = src[i];
        }
    } else if (dst_adres > src_adres) {
        for (uint64_t i = rozmiar;
             i > 0;
             --i) {

            dst[i - 1] = src[i - 1];
        }
    }
}

bool mnoz_u64(
    uint64_t a,
    uint64_t b,
    uint64_t* wynik
) {
    if (!wynik) {
        return false;
    }

    if (a != 0 &&
        b > UINT64_MAX / a) {
        return false;
    }

    *wynik = a * b;
    return true;
}

bool dodaj_u64(
    uint64_t a,
    uint64_t b,
    uint64_t* wynik
) {
    if (!wynik) {
        return false;
    }

    if (a > UINT64_MAX - b) {
        return false;
    }

    *wynik = a + b;
    return true;
}

uint64_t podziel_w_gore(
    uint64_t wartosc,
    uint64_t dzielnik
) {
    if (dzielnik == 0) {
        return 0;
    }

    return
        wartosc / dzielnik +
        ((wartosc % dzielnik) != 0 ? 1ULL : 0ULL);
}

/* =========================================================================
 * WALIDACJA STALYCH FORMATU
 * ========================================================================= */

bool parametry_formatu_obslugiwane() {
    if (PSF_ROZMIAR_BLOKU == 0) {
        return false;
    }

    if ((static_cast<uint64_t>(
             PSF_ROZMIAR_BLOKU) %
         ROZMIAR_SEKTORA) != 0) {
        return false;
    }

    if (sizeof(superblok) >
        ROZMIAR_SEKTORA) {
        /*
         * Inicjalizacja najpierw czyta tylko jeden sektor, aby dopiero
         * potem bezpiecznie ustalic rozmiar calego systemu plikow.
         */
        return false;
    }

    if (sizeof(wezel_indeksowy) >
        static_cast<size_t>(
            PSF_ROZMIAR_BLOKU)) {
        return false;
    }

    if (sizeof(wpis_katalogowy) >
        static_cast<size_t>(
            PSF_ROZMIAR_BLOKU)) {
        return false;
    }

    if ((PSF_ROZMIAR_BLOKU /
         sizeof(wezel_indeksowy)) == 0) {
        return false;
    }

    if ((PSF_ROZMIAR_BLOKU /
         sizeof(wpis_katalogowy)) == 0) {
        return false;
    }

    return true;
}

/* =========================================================================
 * DIRTY TRACKING I WRITE-THROUGH
 * ========================================================================= */

void wyczysc_liste_brudnych() {
    liczba_brudnych_blokow = 0;
    brudny_caly_system = false;
}

void oznacz_caly_system_jako_brudny() {
    brudny_caly_system = true;
    liczba_brudnych_blokow = 0;
}

void oznacz_blok_jako_brudny(
    uint64_t nr_bloku
) {
    if (!dysk_superblok || nr_bloku >= dysk_superblok->ilosc_blokow) {
        return;
    }

    if (nr_bloku >= dysk_superblok->start_danych) {
        for (size_t i = 0; i < POJEMNOSC_CACHE_BLOKOW; ++i) {
            if (wpisy_cache[i].nr_bloku == nr_bloku) {
                wpisy_cache[i].brudny = true;
                break;
            }
        }
    }

    if (brudny_caly_system) {
        return;
    }

    for (size_t i = 0;
         i < liczba_brudnych_blokow;
         ++i) {

        if (brudne_bloki[i] ==
            nr_bloku) {
            return;
        }
    }

    if (liczba_brudnych_blokow >=
        MAKS_BRUDNYCH_BLOKOW) {

        oznacz_caly_system_jako_brudny();
        return;
    }

    brudne_bloki[
        liczba_brudnych_blokow++] =
        nr_bloku;
}

bool zapisz_sektory_porcjami(
    uint64_t lba,
    uint64_t liczba_sektorow,
    const uint8_t* dane
) {
    if (!dane || !bsp_partycja.device || !Storage::range(lba,liczba_sektorow,bsp_partycja.count)) {
        return false;
    }

    uint64_t zapisano = 0;
    uint32_t limit = (bsp_partycja.device && bsp_partycja.device->max_transfer_sectors > 0)
                         ? bsp_partycja.device->max_transfer_sectors
                         : DOMYSLNY_LIMIT_SEKTOROW_OPERACJI;

    while (zapisano <
           liczba_sektorow) {

        uint64_t pozostalo =
            liczba_sektorow -
            zapisano;

        uint32_t paczka =
            pozostalo > limit
                ? limit
                : static_cast<uint32_t>(
                    pozostalo);

        if(stan_startu && stan_startu->zapis!=PsfEtapStartu::Blad){stan_startu->zapis=PsfEtapStartu::Ok;stan_startu->blokZapisu=(lba+zapisano)/(PSF_ROZMIAR_BLOKU/ROZMIAR_SEKTORA);}
        const uint8_t* fragment=dane+zapisano*ROZMIAR_SEKTORA;
        const BlockError wynik=blokowy_zapis_instalatora()
            ? (Storage::write_verify(bsp_partycja,lba+zapisano,paczka,fragment)?BlockError::Ok:BlockError::IOError)
            : (bsp_partycja.device->sector_size==512
                ? Storage::partition_write(bsp_partycja,lba+zapisano,paczka,fragment)
                : BlockError::InvalidArgument);
        if(wynik!=BlockError::Ok) {
            if(stan_startu){stan_startu->zapis=PsfEtapStartu::Blad;stan_startu->bladBlokowy=wynik;}
            blad_startu(PsfBladStartu::Zapis);
            return false;
        }

        zapisano += paczka;
    }

    return true;
}

bool czytaj_sektory_porcjami(
    uint64_t lba,
    uint64_t liczba_sektorow,
    uint8_t* cel
) {
    if (!cel || !bsp_partycja.device || !Storage::range(lba,liczba_sektorow,bsp_partycja.count)) {
        return false;
    }

    uint64_t przeczytano = 0;
    uint32_t limit = (bsp_partycja.device && bsp_partycja.device->max_transfer_sectors > 0)
                         ? bsp_partycja.device->max_transfer_sectors
                         : DOMYSLNY_LIMIT_SEKTOROW_OPERACJI;

    while (przeczytano <
           liczba_sektorow) {

        uint64_t pozostalo =
            liczba_sektorow -
            przeczytano;

        uint32_t paczka =
            pozostalo > limit
                ? limit
                : static_cast<uint32_t>(
                    pozostalo);

        if (!Storage::read(
                bsp_partycja,
                lba + przeczytano,
                paczka,
                cel +
                    przeczytano *
                    ROZMIAR_SEKTORA)) {

            blad_startu(PsfBladStartu::Odczyt);
            return false;
        }

        przeczytano += paczka;
    }

    return true;
}

bool bsp_zapisz_zmiany() {
    if (!ram_dysk ||
        !dysk_superblok) {

        return false;
    }

    /*
     * Brak dysku nie psuje semantyki ramdysku.
     */
    if (!nosnik_blokowy_dostepny ||
        !zapis_trwaly_dozwolony) {

        /*
         * W trybie RAM-only nie akumulujemy bez konca listy dirty.
         * Obecne BSP2 nie obsluguje hot-plug/reconnect nosnika.
         */
        wyczysc_liste_brudnych();
        ostatni_flush_ok = !nosnik_blokowy_dostepny;
        return ostatni_flush_ok;
    }

    if (!brudny_caly_system &&
        liczba_brudnych_blokow == 0) {

        const BlockError wynik=Storage::partition_flush(bsp_partycja);
        ostatni_flush_ok = wynik==BlockError::Ok;
        if(stan_startu){stan_startu->flush=ostatni_flush_ok?PsfEtapStartu::Ok:PsfEtapStartu::Blad;stan_startu->bladBlokowy=wynik;}
        if(!ostatni_flush_ok)blad_startu(PsfBladStartu::Flush);
        return ostatni_flush_ok;
    }

    const uint64_t sektory_na_blok =
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU) /
        ROZMIAR_SEKTORA;

    bool sukces = true;

    if (brudny_caly_system) {
        const uint64_t sektory_meta =
            static_cast<uint64_t>(
                dysk_superblok->
                    start_danych) *
            sektory_na_blok;

        sukces =
            zapisz_sektory_porcjami(
                BSP_START_LBA,
                sektory_meta,
                ram_dysk
            );

        for (size_t i = 0; sukces && i < POJEMNOSC_CACHE_BLOKOW; ++i) {
            if (wpisy_cache[i].brudny && wpisy_cache[i].nr_bloku != UINT64_MAX) {
                uint64_t lba = BSP_START_LBA + wpisy_cache[i].nr_bloku * sektory_na_blok;
                if (!zapisz_sektory_porcjami(lba, sektory_na_blok, wpisy_cache[i].dane)) {
                    sukces = false;
                } else {
                    wpisy_cache[i].brudny = false;
                }
            }
        }
    } else {
        for (size_t i = 0;
             i < liczba_brudnych_blokow;
             ++i) {

            const uint64_t nr_bloku =
                brudne_bloki[i];

            uint64_t przesuniecie_lba = 0;

            if (!mnoz_u64(
                    nr_bloku,
                    sektory_na_blok,
                    &przesuniecie_lba)) {

                sukces = false;
                break;
            }

            if (nr_bloku < dysk_superblok->start_danych) {
                uint64_t przesuniecie_bajtowe = 0;

                if (!mnoz_u64(
                        nr_bloku,
                        static_cast<uint64_t>(
                            PSF_ROZMIAR_BLOKU),
                        &przesuniecie_bajtowe)) {

                    sukces = false;
                    break;
                }

                if (!zapisz_sektory_porcjami(
                        BSP_START_LBA +
                            przesuniecie_lba,
                        sektory_na_blok,
                        ram_dysk +
                            przesuniecie_bajtowe)) {

                    sukces = false;
                    break;
                }
            } else {
                for (size_t j = 0; j < POJEMNOSC_CACHE_BLOKOW; ++j) {
                    if (wpisy_cache[j].nr_bloku == nr_bloku) {
                        if (wpisy_cache[j].brudny) {
                            if (!zapisz_sektory_porcjami(
                                    BSP_START_LBA + przesuniecie_lba,
                                    sektory_na_blok,
                                    wpisy_cache[j].dane)) {
                                sukces = false;
                            } else {
                                wpisy_cache[j].brudny = false;
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    if (sukces) {
        const BlockError wynik=Storage::partition_flush(bsp_partycja);
        sukces = wynik==BlockError::Ok;
        if(stan_startu){stan_startu->flush=sukces?PsfEtapStartu::Ok:PsfEtapStartu::Blad;stan_startu->bladBlokowy=wynik;}
        if(!sukces)blad_startu(PsfBladStartu::Flush);
    }
    if (!sukces) zapis_trwaly_dozwolony = false;
    if (sukces) {
        wyczysc_liste_brudnych();
        for (size_t i = 0; i < POJEMNOSC_CACHE_BLOKOW; ++i) {
            wpisy_cache[i].brudny = false;
        }
    }

    ostatni_flush_ok = sukces;
    return sukces;
}

/* =========================================================================
 * RAW BLOKI I WEZLY
 * ========================================================================= */

uint8_t* pobierz_blok(
    uint64_t nr_bloku,
    bool wczytaj_z_dysku
) {
    if (!ram_dysk ||
        !dysk_superblok) {

        return nullptr;
    }

    if (nr_bloku >=
        dysk_superblok->ilosc_blokow) {

        return nullptr;
    }

    // Bloki metadanych (superblok, bitmapa, wezly indeksowe)
    if (!nosnik_blokowy_dostepny || nr_bloku < dysk_superblok->start_danych) {
        uint64_t offset = 0;

        if (!mnoz_u64(
                nr_bloku,
                static_cast<uint64_t>(
                    PSF_ROZMIAR_BLOKU),
                &offset)) {

            return nullptr;
        }

        uint64_t koniec = 0;

        if (!dodaj_u64(
                offset,
                static_cast<uint64_t>(
                    PSF_ROZMIAR_BLOKU),
                &koniec)) {

            return nullptr;
        }

        if (koniec >
            static_cast<uint64_t>(
                rozmiar_ram_dysku)) {

            return nullptr;
        }

        return ram_dysk + offset;
    }

    // Bloki danych - obsluga przez cache
    for (size_t i = 0; i < POJEMNOSC_CACHE_BLOKOW; ++i) {
        if (wpisy_cache[i].nr_bloku == nr_bloku) {
            wpisy_cache[i].wiek = ++licznik_wieku_cache;
            return wpisy_cache[i].dane;
        }
    }

    size_t victim = 0;
    uint32_t min_wiek = UINT32_MAX;
    for (size_t i = 0; i < POJEMNOSC_CACHE_BLOKOW; ++i) {
        if (wpisy_cache[i].nr_bloku == UINT64_MAX) {
            victim = i;
            break;
        }
        if (wpisy_cache[i].wiek < min_wiek) {
            min_wiek = wpisy_cache[i].wiek;
            victim = i;
        }
    }

    if (wpisy_cache[victim].brudny && wpisy_cache[victim].nr_bloku != UINT64_MAX) {
        if (nosnik_blokowy_dostepny && zapis_trwaly_dozwolony) {
            const uint64_t sektory_na_blok = static_cast<uint64_t>(PSF_ROZMIAR_BLOKU) / ROZMIAR_SEKTORA;
            const uint64_t lba = BSP_START_LBA + wpisy_cache[victim].nr_bloku * sektory_na_blok;
            if (!zapisz_sektory_porcjami(lba, sektory_na_blok, wpisy_cache[victim].dane)) {
                return nullptr;
            }
        }
        wpisy_cache[victim].brudny = false;
    }

    wpisy_cache[victim].nr_bloku = nr_bloku;
    wpisy_cache[victim].brudny = false;
    wpisy_cache[victim].wiek = ++licznik_wieku_cache;

    const uint64_t sektory_na_blok = static_cast<uint64_t>(PSF_ROZMIAR_BLOKU) / ROZMIAR_SEKTORA;
    const uint64_t lba = BSP_START_LBA + nr_bloku * sektory_na_blok;
    if (wczytaj_z_dysku && nosnik_blokowy_dostepny) {
        if (!czytaj_sektory_porcjami(lba, sektory_na_blok, wpisy_cache[victim].dane)) {
            wpisy_cache[victim].nr_bloku = UINT64_MAX;
            zapis_trwaly_dozwolony = false;
            return nullptr;
        }
    } else {
        wyzeruj_pamiec(wpisy_cache[victim].dane, PSF_ROZMIAR_BLOKU);
    }

    return wpisy_cache[victim].dane;
}

uint64_t wezlow_na_blok() {
    return
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU) /
        static_cast<uint64_t>(
            sizeof(wezel_indeksowy));
}

uint64_t wpisow_katalogowych_na_blok() {
    return
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU) /
        static_cast<uint64_t>(
            sizeof(wpis_katalogowy));
}

wezel_indeksowy* pobierz_wezel(
    uint64_t id_wezla
) {
    if (!dysk_superblok ||
        id_wezla == 0 ||
        id_wezla >
            calkowita_liczba_wezlow) {

        return nullptr;
    }

    const uint64_t na_blok =
        wezlow_na_blok();

    if (na_blok == 0) {
        return nullptr;
    }

    const uint64_t indeks =
        id_wezla - 1ULL;

    const uint64_t nr_bloku =
        dysk_superblok->
            start_wezlow +
        indeks / na_blok;

    if (nr_bloku >=
        dysk_superblok->
            start_danych) {

        return nullptr;
    }

    uint8_t* blok =
        pobierz_blok(nr_bloku);

    if (!blok) {
        return nullptr;
    }

    wezel_indeksowy* wezly =
        reinterpret_cast<
            wezel_indeksowy*>(
                blok
        );

    return &wezly[
        indeks % na_blok
    ];
}

void oznacz_wezel_jako_brudny(
    uint64_t id_wezla
) {
    if (!dysk_superblok ||
        id_wezla == 0) {

        return;
    }

    const uint64_t na_blok =
        wezlow_na_blok();

    if (na_blok == 0) {
        return;
    }

    const uint64_t indeks =
        id_wezla - 1ULL;

    const uint64_t nr_bloku =
        dysk_superblok->
            start_wezlow +
        indeks / na_blok;

    oznacz_blok_jako_brudny(
        nr_bloku
    );
}

bool id_bloku_danych_poprawne(
    uint64_t id
) {
    return
        id <
        calkowita_liczba_blokow_danych;
}

uint64_t absolutny_blok_danych(
    uint64_t id
) {
    if (!dysk_superblok ||
        !id_bloku_danych_poprawne(id)) {

        return UINT64_MAX;
    }

    return
        dysk_superblok->
            start_danych +
        id;
}

uint8_t* pobierz_blok_danych(
    uint64_t id,
    bool wczytaj_z_dysku
) {
    const uint64_t absolutny =
        absolutny_blok_danych(id);

    if (absolutny == UINT64_MAX) {
        return nullptr;
    }

    return pobierz_blok(
        absolutny,
        wczytaj_z_dysku
    );
}

wpis_katalogowy* pobierz_wpisy_katalogowe(
    uint64_t id_bloku_danych
) {
    uint8_t* blok =
        pobierz_blok_danych(
            id_bloku_danych
        );

    if (!blok) {
        return nullptr;
    }

    return reinterpret_cast<
        wpis_katalogowy*>(
            blok
    );
}

/* =========================================================================
 * BITMAPA BLOKOW DANYCH
 * ========================================================================= */

bool lokalizacja_bitmapy(
    uint64_t id_bloku_danych,
    uint64_t* nr_bloku_bitmapy,
    uint64_t* offset,
    uint8_t* maska
) {
    if (!dysk_superblok ||
        !nr_bloku_bitmapy ||
        !offset ||
        !maska) {

        return false;
    }

    if (!id_bloku_danych_poprawne(
            id_bloku_danych)) {

        return false;
    }

    const uint64_t bajt =
        id_bloku_danych / 8ULL;

    const uint64_t blok =
        1ULL +
        bajt /
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU);

    if (blok >=
        dysk_superblok->
            start_wezlow) {

        return false;
    }

    *nr_bloku_bitmapy =
        blok;

    *offset =
        bajt %
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU);

    *maska =
        static_cast<uint8_t>(
            1U <<
            static_cast<uint8_t>(
                id_bloku_danych &
                7ULL)
        );

    return true;
}

bool czy_blok_danych_zajety(
    uint64_t id_bloku_danych
) {
    uint64_t nr_bitmapy = 0;
    uint64_t offset = 0;
    uint8_t maska = 0;

    if (!lokalizacja_bitmapy(
            id_bloku_danych,
            &nr_bitmapy,
            &offset,
            &maska)) {

        return false;
    }

    uint8_t* blok =
        pobierz_blok(
            nr_bitmapy
        );

    if (!blok) {
        return false;
    }

    return
        (blok[offset] &
         maska) != 0;
}

bool ustaw_stan_bloku_danych(
    uint64_t id_bloku_danych,
    bool zajety
) {
    uint64_t nr_bitmapy = 0;
    uint64_t offset = 0;
    uint8_t maska = 0;

    if (!lokalizacja_bitmapy(
            id_bloku_danych,
            &nr_bitmapy,
            &offset,
            &maska)) {

        return false;
    }

    uint8_t* blok =
        pobierz_blok(
            nr_bitmapy
        );

    if (!blok) {
        return false;
    }

    if (zajety) {
        blok[offset] |= maska;
    } else {
        blok[offset] &=
            static_cast<uint8_t>(
                ~maska
            );
    }

    oznacz_blok_jako_brudny(
        nr_bitmapy
    );

    return true;
}

#ifdef BURSZTYN_PSF_HOST_TEST
bool psf_test_bootstrap_brak_bloku=false;
#endif
uint64_t zaalokuj_wolny_blok_danych() {
    if(stan_startu)stan_startu->alokacjaBloku=PsfEtapStartu::Blad;
#ifdef BURSZTYN_PSF_HOST_TEST
    if(stan_startu && psf_test_bootstrap_brak_bloku){blad_startu(PsfBladStartu::BrakBlokow);return BARK_BLOKU;}
#endif
    if (!dysk_superblok ||
        calkowita_liczba_blokow_danych == 0) {

        return BARK_BLOKU;
    }

    uint64_t start =
        ostatni_szukany_blok_danych;

    if (start >=
        calkowita_liczba_blokow_danych) {

        start = 0;
    }

    for (uint64_t przebieg = 0;
         przebieg < 2;
         ++przebieg) {

        const uint64_t od =
            przebieg == 0
                ? start
                : 0;

        const uint64_t do_ =
            przebieg == 0
                ? calkowita_liczba_blokow_danych
                : start;

        for (uint64_t id = od;
             id < do_;
             ++id) {

            if (czy_blok_danych_zajety(
                    id)) {

                continue;
            }

            if (!ustaw_stan_bloku_danych(
                    id,
                    true)) {

                continue;
            }

            uint8_t* dane =
                pobierz_blok_danych(
                    id
                );

            if (!dane) {
                (void)ustaw_stan_bloku_danych(
                    id,
                    false
                );
                continue;
            }

            wyzeruj_pamiec(
                dane,
                PSF_ROZMIAR_BLOKU
            );

            const uint64_t absolutny =
                absolutny_blok_danych(
                    id
                );

            if (absolutny != UINT64_MAX) {
                oznacz_blok_jako_brudny(
                    absolutny
                );
            }

            ostatni_szukany_blok_danych =
                id + 1ULL;

            if (ostatni_szukany_blok_danych >=
                calkowita_liczba_blokow_danych) {

                ostatni_szukany_blok_danych = 0;
            }

            if(stan_startu)stan_startu->alokacjaBloku=PsfEtapStartu::Ok;
            return id;
        }
    }

    blad_startu(PsfBladStartu::BrakBlokow);
    return BARK_BLOKU;
}

bool zwolnij_blok_danych(
    uint64_t id_bloku_danych
) {
    if (!id_bloku_danych_poprawne(
            id_bloku_danych)) {

        return false;
    }

    if (!czy_blok_danych_zajety(
            id_bloku_danych)) {

        /*
         * Double-free lub uszkodzone metadane.
         */
        return false;
    }

    uint8_t* dane =
        pobierz_blok_danych(
            id_bloku_danych
        );

    if (!dane) {
        return false;
    }

    /*
     * Zerowanie przy zwalnianiu ogranicza ryzyko odzyskania danych
     * starego pliku po ponownej alokacji.
     */
    wyzeruj_pamiec(
        dane,
        PSF_ROZMIAR_BLOKU
    );

    const uint64_t absolutny =
        absolutny_blok_danych(
            id_bloku_danych
        );

    if (absolutny != UINT64_MAX) {
        oznacz_blok_jako_brudny(
            absolutny
        );
    }

    if (!ustaw_stan_bloku_danych(
            id_bloku_danych,
            false)) {

        return false;
    }

    if (id_bloku_danych <
        ostatni_szukany_blok_danych) {

        ostatni_szukany_blok_danych =
            id_bloku_danych;
    }

    return true;
}

/* =========================================================================
 * WEZLY
 * ========================================================================= */

void wyzeruj_wezel(
    wezel_indeksowy* w,
    uint8_t typ
) {
    if (!w) {
        return;
    }

    /*
     * Nie zakladamy, ze TYP_WOLNY == 0.
     */
    wyzeruj_pamiec(
        w,
        sizeof(wezel_indeksowy)
    );

    w->typ = typ;
    w->rozmiar_w_bajtach = 0;
    w->czas_utworzenia = 0;
    w->flagi_zabezpieczen = 0;

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        w->wskazniki_blokow[k] =
            BARK_BLOKU;
    }

    w->blok_posredni_1 =
        BARK_BLOKU;

    w->blok_posredni_2 =
        BARK_BLOKU;

    w->blok_posredni_3 =
        BARK_BLOKU;
}

uint64_t zaalokuj_wolny_wezel(
    uint8_t typ
) {
    for (uint64_t id = 1;
         id <=
            calkowita_liczba_wezlow;
         ++id) {

        wezel_indeksowy* w =
            pobierz_wezel(id);

        if (!w ||
            w->typ != TYP_WOLNY) {

            continue;
        }

        wyzeruj_wezel(
            w,
            typ
        );

        oznacz_wezel_jako_brudny(
            id
        );

        return id;
    }

    return 0;
}

bool zwolnij_wezel(
    uint64_t id
) {
    wezel_indeksowy* w =
        pobierz_wezel(id);

    if (!w ||
        w->typ == TYP_WOLNY) {

        return false;
    }

    wyzeruj_wezel(
        w,
        TYP_WOLNY
    );

    oznacz_wezel_jako_brudny(
        id
    );

    return true;
}

/* =========================================================================
 * BEZPIECZNE NAZWY I SCIEZKI
 * ========================================================================= */

bool zakonczony_string_w_limicie(
    const char* tekst,
    size_t limit,
    size_t* dlugosc_wyj
) {
    if (!tekst ||
        limit == 0) {

        return false;
    }

    for (size_t i = 0;
         i < limit;
         ++i) {

        if (tekst[i] == '\0') {
            if (dlugosc_wyj) {
                *dlugosc_wyj = i;
            }

            return true;
        }
    }

    return false;
}

bool poprawna_nazwa(
    const char* nazwa
) {
    size_t dlugosc = 0;

    if (!zakonczony_string_w_limicie(
            nazwa,
            static_cast<size_t>(
                PSF_MAX_NAZWA),
            &dlugosc)) {

        return false;
    }

    if (dlugosc == 0 ||
        dlugosc >=
            static_cast<size_t>(
                PSF_MAX_NAZWA)) {

        return false;
    }

    if ((dlugosc == 1 &&
         nazwa[0] == '.') ||
        (dlugosc == 2 &&
         nazwa[0] == '.' &&
         nazwa[1] == '.')) {

        return false;
    }

    for (size_t i = 0;
         i < dlugosc;
         ++i) {

        const char c =
            nazwa[i];

        if (c == '/' ||
            c == '\\' ||
            static_cast<uint8_t>(c) < 0x20U) {

            return false;
        }
    }

    return true;
}

bool bezpieczna_sciezka(
    const char* sciezka
) {
    size_t dlugosc = 0;

    if (!zakonczony_string_w_limicie(
            sciezka,
            MAKS_DLUGOSC_SCIEZKI,
            &dlugosc)) {

        return false;
    }

    if (dlugosc == 0 ||
        sciezka[0] != '/') {

        return false;
    }

    /*
     * "/" jest prawidlowym korzeniem.
     */
    if (dlugosc == 1) {
        return true;
    }

    /*
     * Nie normalizujemy po cichu // ani koncowego /.
     * To eliminuje dwuznacznosci parsera.
     */
    if (sciezka[dlugosc - 1] == '/') {
        return false;
    }

    bool poprzedni_slash = true;

    for (size_t i = 1;
         i < dlugosc;
         ++i) {

        const char c =
            sciezka[i];

        if (c == '/') {
            if (poprzedni_slash) {
                return false;
            }

            poprzedni_slash = true;
        } else {
            poprzedni_slash = false;
        }
    }

    return true;
}

bool kopiuj_nazwe(
    char* cel,
    const char* zrodlo
) {
    if (!cel ||
        !poprawna_nazwa(zrodlo)) {

        return false;
    }

    size_t i = 0;

    for (;
         i <
             static_cast<size_t>(
                 PSF_MAX_NAZWA - 1) &&
         zrodlo[i] != '\0';
         ++i) {

        cel[i] = zrodlo[i];
    }

    cel[i] = '\0';
    return true;
}

bool nazwy_rowne(
    const char* a,
    const char* b
) {
    if (!a || !b) {
        return false;
    }

    for (size_t i = 0;
         i <
            static_cast<size_t>(
                PSF_MAX_NAZWA);
         ++i) {

        const char ca = a[i];
        const char cb = b[i];

        if (ca != cb) {
            return false;
        }

        if (ca == '\0') {
            return true;
        }
    }

    /*
     * Brak NUL w polu nazwy na dysku.
     */
    return false;
}

/* =========================================================================
 * KATALOGI
 * ========================================================================= */

struct ZnalezionyWpis {
    bool znaleziony;
    uint64_t id_wezla;
    uint64_t id_bloku_danych;
    uint64_t indeks_wpisu;
};

ZnalezionyWpis znajdz_wpis_w_katalogu(
    uint64_t katalog_id,
    const char* nazwa
) {
    ZnalezionyWpis wynik{};

    if (!poprawna_nazwa(nazwa)) {
        return wynik;
    }

    wezel_indeksowy* katalog =
        pobierz_wezel(
            katalog_id
        );

    if (!katalog ||
        katalog->typ != TYP_KATALOG) {

        return wynik;
    }

    const uint64_t wpisow_na_blok =
        wpisow_katalogowych_na_blok();

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        const uint64_t id_bloku =
            katalog->
                wskazniki_blokow[k];

        if (id_bloku ==
            BARK_BLOKU) {

            continue;
        }

        if (!id_bloku_danych_poprawne(
                id_bloku)) {

            return ZnalezionyWpis{};
        }

        wpis_katalogowy* wpisy =
            pobierz_wpisy_katalogowe(
                id_bloku
            );

        if (!wpisy) {
            return ZnalezionyWpis{};
        }

        for (uint64_t j = 0;
             j < wpisow_na_blok;
             ++j) {

            if (wpisy[j].id_wezla == 0) {
                continue;
            }

            if (nazwy_rowne(
                    wpisy[j].nazwa,
                    nazwa)) {

                wynik.znaleziony = true;
                wynik.id_wezla =
                    wpisy[j].id_wezla;
                wynik.id_bloku_danych =
                    id_bloku;
                wynik.indeks_wpisu =
                    j;

                return wynik;
            }
        }
    }

    return wynik;
}

bool katalog_pusty(
    uint64_t katalog_id
) {
    wezel_indeksowy* katalog =
        pobierz_wezel(
            katalog_id
        );

    if (!katalog ||
        katalog->typ != TYP_KATALOG) {

        return false;
    }

    const uint64_t wpisow_na_blok =
        wpisow_katalogowych_na_blok();

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        const uint64_t id_bloku =
            katalog->
                wskazniki_blokow[k];

        if (id_bloku ==
            BARK_BLOKU) {

            continue;
        }

        wpis_katalogowy* wpisy =
            pobierz_wpisy_katalogowe(
                id_bloku
            );

        if (!wpisy) {
            return false;
        }

        for (uint64_t j = 0;
             j < wpisow_na_blok;
             ++j) {

            if (wpisy[j].id_wezla != 0) {
                return false;
            }
        }
    }

    return true;
}

bool dodaj_wpis_do_katalogu(
    uint64_t katalog_id,
    uint64_t nowy_twor_id,
    const char* nazwa
) {
    if(stan_startu)stan_startu->wpis=PsfEtapStartu::Blad;
    if (!poprawna_nazwa(nazwa)) {
        return false;
    }

    if (!pobierz_wezel(
            nowy_twor_id)) {

        return false;
    }

    if (znajdz_wpis_w_katalogu(
            katalog_id,
            nazwa).znaleziony) {

        return false;
    }

    wezel_indeksowy* katalog =
        pobierz_wezel(
            katalog_id
        );

    if (!katalog ||
        katalog->typ != TYP_KATALOG) {

        return false;
    }

    const uint64_t wpisow_na_blok =
        wpisow_katalogowych_na_blok();

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        uint64_t id_bloku =
            katalog->
                wskazniki_blokow[k];

        bool nowo_przydzielony = false;

        if (id_bloku ==
            BARK_BLOKU) {

            id_bloku =
                zaalokuj_wolny_blok_danych();

            if (id_bloku ==
                BARK_BLOKU) {

                return false;
            }

            katalog->
                wskazniki_blokow[k] =
                id_bloku;

            oznacz_wezel_jako_brudny(
                katalog_id
            );

            nowo_przydzielony = true;
        }

        wpis_katalogowy* wpisy =
            pobierz_wpisy_katalogowe(
                id_bloku
            );

        if (!wpisy) {
            if (nowo_przydzielony) {
                katalog->
                    wskazniki_blokow[k] =
                    BARK_BLOKU;

                (void)zwolnij_blok_danych(
                    id_bloku
                );

                oznacz_wezel_jako_brudny(
                    katalog_id
                );
            }

            return false;
        }

        for (uint64_t j = 0;
             j < wpisow_na_blok;
             ++j) {

            if (wpisy[j].id_wezla != 0) {
                continue;
            }

            /*
             * Najpierw nazwa, na koncu id_wezla. W razie inspekcji
             * niedokonczonego RAM stanu wpis z id==0 jest ignorowany.
             */
            if (!kopiuj_nazwe(
                    wpisy[j].nazwa,
                    nazwa)) {

                if (nowo_przydzielony) {
                    katalog->
                        wskazniki_blokow[k] =
                        BARK_BLOKU;

                    (void)zwolnij_blok_danych(
                        id_bloku
                    );

                    oznacz_wezel_jako_brudny(
                        katalog_id
                    );
                }

                return false;
            }

            wpisy[j].id_wezla =
                nowy_twor_id;

            const uint64_t absolutny =
                absolutny_blok_danych(
                    id_bloku
                );

            if (absolutny != UINT64_MAX) {
                oznacz_blok_jako_brudny(
                    absolutny
                );
            }

            katalog->
                rozmiar_w_bajtach++;

            oznacz_wezel_jako_brudny(
                katalog_id
            );

            if(stan_startu)stan_startu->wpis=PsfEtapStartu::Ok;
            return true;
        }
    }

    blad_startu(PsfBladStartu::LimitKatalogu);
    return false;
}

/* =========================================================================
 * PARSER SCIEZEK
 * ========================================================================= */

bool pobierz_segment(
    const char* sciezka,
    size_t* indeks,
    char* segment,
    bool* ostatni
) {
    if (!sciezka ||
        !indeks ||
        !segment ||
        !ostatni) {

        return false;
    }

    size_t pos =
        *indeks;

    if (sciezka[pos] == '\0' ||
        sciezka[pos] == '/') {

        return false;
    }

    size_t dlugosc = 0;

    while (sciezka[pos] != '\0' &&
           sciezka[pos] != '/') {

        if (dlugosc + 1 >=
            static_cast<size_t>(
                PSF_MAX_NAZWA)) {

            return false;
        }

        segment[dlugosc++] =
            sciezka[pos++];
    }

    segment[dlugosc] = '\0';

    if (!poprawna_nazwa(
            segment)) {

        return false;
    }

    if (sciezka[pos] == '\0') {
        *ostatni = true;
        *indeks = pos;
        return true;
    }

    /*
     * bezpieczna_sciezka() zapewnia, ze po slashu istnieje kolejny segment.
     */
    *ostatni = false;
    *indeks = pos + 1;
    return true;
}

static uint8_t psf_aktywny_slot = 0; // 0 = Slot A, 1 = Slot B
static bool psf_sloty_wlaczone = true;

extern "C" void psf_ustaw_aktywny_slot(uint8_t slot) {
    psf_aktywny_slot = (slot == 1) ? 1 : 0;
}

extern "C" uint8_t psf_pobierz_aktywny_slot(void) {
    return psf_aktywny_slot;
}

extern "C" void psf_wlacz_translacje_slotow(bool wlaczone) {
    psf_sloty_wlaczone = wlaczone;
}

static bool psf_sprawdz_obecnosc_slotow() {
    if (!psf_gotowy || !dysk_superblok) return false;
    ZnalezionyWpis w = znajdz_wpis_w_katalogu(dysk_superblok->id_korzenia, "sloty");
    return w.znaleziony;
}

__attribute__((noinline)) static const char* psf_przetlumacz_sciezke_slotu(const char* s, char bufor[MAKS_DLUGOSC_SCIEZKI]) {
    if (!bezpieczna_sciezka(s)) return nullptr;
    if (!psf_sloty_wlaczone || !psf_sprawdz_obecnosc_slotow()) return s;

    bool jest_programy = false;
    bool jest_system = false;
    size_t reszta = 0;

    if (s[1] == 'p' && s[2] == 'r' && s[3] == 'o' && s[4] == 'g' &&
        s[5] == 'r' && s[6] == 'a' && s[7] == 'm' && s[8] == 'y') {
        if (s[9] == '\0' || s[9] == '/') {
            jest_programy = true;
            reszta = 9;
        }
    } else if (s[1] == 's' && s[2] == 'y' && s[3] == 's' && s[4] == 't' &&
               s[5] == 'e' && s[6] == 'm') {
        if (s[7] == '\0' || s[7] == '/') {
            jest_system = true;
            reszta = 7;
        }
    }

    if (!jest_programy && !jest_system) return s;

    const char* pref = (psf_aktywny_slot == 1) ? "/sloty/b" : "/sloty/a";
    size_t p = 0;
    while (pref[p] && p < MAKS_DLUGOSC_SCIEZKI - 1U) { bufor[p] = pref[p]; ++p; }

    const char* kat = jest_programy ? "/programy" : "/system";
    size_t k = 0;
    while (kat[k] && p < MAKS_DLUGOSC_SCIEZKI - 1U) { bufor[p++] = kat[k++]; }

    while (s[reszta] && p < MAKS_DLUGOSC_SCIEZKI - 1U) { bufor[p++] = s[reszta++]; }
    if (s[reszta] != '\0') {
        bufor[0] = '\0';
        return nullptr;
    }
    bufor[p] = '\0';
    return bufor;
}

__attribute__((noinline)) uint64_t rozwiaz_sciezke(
    const char* sciezka,
    char* nazwa_docelowa,
    bool zwroc_rodzica
) {
    char bufor_translacji[MAKS_DLUGOSC_SCIEZKI];
    const char* sciezka_rzeczywista = psf_przetlumacz_sciezke_slotu(sciezka, bufor_translacji);
    if (!psf_gotowy ||
        !dysk_superblok ||
        !sciezka_rzeczywista ||
        !bezpieczna_sciezka(
            sciezka_rzeczywista)) {

        return 0;
    }

    if (sciezka_rzeczywista[0] == '/' &&
        sciezka_rzeczywista[1] == '\0') {

        /*
         * Korzen nie posiada rodzica/nazwy katalogowej.
         */
        return
            zwroc_rodzica
                ? 0
                : dysk_superblok->
                    id_korzenia;
    }

    if (zwroc_rodzica &&
        !nazwa_docelowa) {

        return 0;
    }

    uint64_t aktualny =
        dysk_superblok->
            id_korzenia;

    size_t indeks = 1;

    while (sciezka_rzeczywista[indeks] != '\0') {
        char segment[
            PSF_MAX_NAZWA] = {};

        bool ostatni = false;

        if (!pobierz_segment(
                sciezka_rzeczywista,
                &indeks,
                segment,
                &ostatni)) {

            return 0;
        }

        if (ostatni &&
            zwroc_rodzica) {

            if (!kopiuj_nazwe(
                    nazwa_docelowa,
                    segment)) {

                return 0;
            }

            wezel_indeksowy* rodzic =
                pobierz_wezel(
                    aktualny
                );

            if (!rodzic ||
                rodzic->typ !=
                    TYP_KATALOG) {

                return 0;
            }

            return aktualny;
        }

        const ZnalezionyWpis krok =
            znajdz_wpis_w_katalogu(
                aktualny,
                segment
            );

        if (!krok.znaleziony) {
            return 0;
        }

        wezel_indeksowy* cel =
            pobierz_wezel(
                krok.id_wezla
            );

        if (!cel ||
            cel->typ == TYP_WOLNY) {

            return 0;
        }

        if (!ostatni &&
            cel->typ != TYP_KATALOG) {

            return 0;
        }

        aktualny =
            krok.id_wezla;

        if (ostatni) {
            return aktualny;
        }
    }

    return 0;
}

/* =========================================================================
 * WALIDACJA SUPERBLOKU
 * ========================================================================= */

bool sygnatura_bsp2(
    const superblok* sb
) {
    if (!sb) {
        return false;
    }

    return
        sb->sygnatura[0] == 'B' &&
        sb->sygnatura[1] == 'S' &&
        sb->sygnatura[2] == 'P' &&
        sb->sygnatura[3] == '2';
}

bool wylicz_geometrie(
    const superblok* sb,
    uint32_t dostepny_ram,
    uint64_t* wezly,
    uint64_t* bloki_danych
) {
    if (!sb ||
        !wezly ||
        !bloki_danych) {

        return false;
    }

    if (!sygnatura_bsp2(sb)) {
        return false;
    }

    const uint64_t calkowity =
        static_cast<uint64_t>(
            sb->calkowity_rozmiar);

    if (calkowity <
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU) *
            MINIMALNA_LICZBA_BLOKOW) {

        return false;
    }

    uint64_t bajty_metadanych = 0;
    if (!mnoz_u64(sb->start_danych,PSF_ROZMIAR_BLOKU,&bajty_metadanych)) return false;

    if (bajty_metadanych > static_cast<uint64_t>(dostepny_ram)) {
        return false;
    }

    if ((calkowity %
         static_cast<uint64_t>(
             PSF_ROZMIAR_BLOKU)) != 0 ||
        (calkowity %
         ROZMIAR_SEKTORA) != 0) {

        return false;
    }

    const uint64_t oczekiwane_bloki =
        calkowity /
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU);

    if (sb->ilosc_blokow !=
        oczekiwane_bloki) {

        return false;
    }

    if (sb->ilosc_blokow <
        MINIMALNA_LICZBA_BLOKOW) {

        return false;
    }

    if (sb->start_wezlow <= 1 ||
        sb->start_wezlow >=
            sb->start_danych ||
        sb->start_danych >=
            sb->ilosc_blokow) {

        return false;
    }

    const uint64_t bloki_bitmapy =
        sb->start_wezlow - 1ULL;

    const uint64_t region_wezlow =
        sb->start_danych -
        sb->start_wezlow;

    const uint64_t region_danych =
        sb->ilosc_blokow -
        sb->start_danych;

    uint64_t bity_bitmapy = 0;

    if (!mnoz_u64(
            bloki_bitmapy,
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU),
            &bity_bitmapy) ||
        !mnoz_u64(
            bity_bitmapy,
            8ULL,
            &bity_bitmapy)) {

        return false;
    }

    if (region_danych >
        bity_bitmapy) {

        return false;
    }

    uint64_t liczba_wezlow = 0;

    if (!mnoz_u64(
            region_wezlow,
            wezlow_na_blok(),
            &liczba_wezlow)) {

        return false;
    }

    if (liczba_wezlow == 0) {
        return false;
    }

    if (sb->id_korzenia == 0 ||
        sb->id_korzenia >
            liczba_wezlow) {

        return false;
    }

    const uint64_t sektory_na_blok =
        static_cast<uint64_t>(PSF_ROZMIAR_BLOKU) / ROZMIAR_SEKTORA;

    if (nosnik_blokowy_dostepny) {
        const uint64_t sektory_dysku = bsp_partycja.count;
        if (sektory_dysku > 0) {
            if (sb->ilosc_blokow == 0) {
                return false;
            }
            uint64_t potrzebne_sektory = 0;
            if (!mnoz_u64(sb->ilosc_blokow, sektory_na_blok, &potrzebne_sektory)) {
                return false;
            }
            uint64_t ostatnie_lba = 0;
            if (!dodaj_u64(BSP_START_LBA, potrzebne_sektory - 1ULL, &ostatnie_lba)) {
                return false;
            }
            if (ostatnie_lba >= sektory_dysku) {
                wypisz_log("[BSP-BLAD] Superblok deklaruje geometrie wychodzaca poza fizyczny dysk.");
                return false;
            }
        }
    }

    *wezly =
        liczba_wezlow;

    *bloki_danych =
        region_danych;

    return true;
}

/* =========================================================================
 * WALIDACJA METADANYCH PO WCZYTANIU
 * ========================================================================= */

bool wskazniki_wezla_poprawne(
    const wezel_indeksowy* w
) {
    if (!w) {
        return false;
    }

    if (w->typ == TYP_PLIK) {
        return wskazniki_pliku_poprawne(w);
    }

    if (w->blok_posredni_1 !=
            BARK_BLOKU ||
        w->blok_posredni_2 !=
            BARK_BLOKU ||
        w->blok_posredni_3 !=
            BARK_BLOKU) {

        return false;
    }

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        const uint64_t id =
            w->wskazniki_blokow[k];

        if (id ==
            BARK_BLOKU) {

            continue;
        }

        if (!id_bloku_danych_poprawne(
                id)) {

            return false;
        }

        if (!czy_blok_danych_zajety(
                id)) {

            return false;
        }

        /*
         * Jeden wezel nie moze wskazywac tego samego bloku dwa razy.
         */
        for (size_t j = 0;
             j < k;
             ++j) {

            if (w->wskazniki_blokow[j] ==
                id) {

                return false;
            }
        }
    }

    return true;
}

bool waliduj_katalog(
    uint64_t id,
    const wezel_indeksowy* w
) {
    if (!w ||
        w->typ != TYP_KATALOG) {

        return false;
    }

    const uint64_t wpisow_na_blok =
        wpisow_katalogowych_na_blok();

    uint64_t znalezionych = 0;

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        const uint64_t blok =
            w->wskazniki_blokow[k];

        if (blok ==
            BARK_BLOKU) {

            continue;
        }

        wpis_katalogowy* wpisy =
            pobierz_wpisy_katalogowe(
                blok
            );

        if (!wpisy) {
            return false;
        }

        for (uint64_t j = 0;
             j < wpisow_na_blok;
             ++j) {

            if (wpisy[j].id_wezla == 0) {
                continue;
            }

            if (wpisy[j].id_wezla >
                    calkowita_liczba_wezlow ||
                wpisy[j].id_wezla ==
                    id ||
                wpisy[j].id_wezla ==
                    dysk_superblok->id_korzenia) {

                return false;
            }

            if (!poprawna_nazwa(
                    wpisy[j].nazwa)) {

                return false;
            }

            wezel_indeksowy* cel =
                pobierz_wezel(
                    wpisy[j].id_wezla
                );

            if (!cel ||
                cel->typ == TYP_WOLNY) {
                wpisy[j].id_wezla = 0;
                wyzeruj_pamiec(wpisy[j].nazwa, PSF_MAX_NAZWA);
                const uint64_t abs = absolutny_blok_danych(blok);
                if (abs != UINT64_MAX) oznacz_blok_jako_brudny(abs);
                continue;
            }

            /*
             * Brak duplikatow nazw w jednym katalogu.
             */
            bool duplicate = false;
            for (size_t k2 = 0; k2 <= k; ++k2) {
                const uint64_t blok2 = w->wskazniki_blokow[k2];
                if (blok2 == BARK_BLOKU) continue;
                wpis_katalogowy* wpisy2 = pobierz_wpisy_katalogowe(blok2);
                if (!wpisy2) continue;
                const uint64_t limit_j = (k2 == k) ? j : wpisow_na_blok;
                for (uint64_t j2 = 0; j2 < limit_j; ++j2) {
                    if (wpisy2[j2].id_wezla != 0 && nazwy_rowne(wpisy2[j2].nazwa, wpisy[j].nazwa)) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) break;
            }
            if (duplicate) {
                wpisy[j].id_wezla = 0;
                wyzeruj_pamiec(wpisy[j].nazwa, PSF_MAX_NAZWA);
                const uint64_t abs = absolutny_blok_danych(blok);
                if (abs != UINT64_MAX) oznacz_blok_jako_brudny(abs);
                continue;
            }

            ++znalezionych;
        }
    }

    const_cast<wezel_indeksowy*>(w)->rozmiar_w_bajtach = znalezionych;
    return true;
}

bool waliduj_metadane() {
    if (!dysk_superblok ||
        calkowita_liczba_wezlow == 0) {
        wypisz_log("[WAL-DBG] no superblock or zero inodes");
        return false;
    }

    wezel_indeksowy* korzen =
        pobierz_wezel(
            dysk_superblok->
                id_korzenia
        );

    if (!korzen ||
        korzen->typ != TYP_KATALOG) {
        wypisz_log("[WAL-DBG] bad root");
        return false;
    }

    const uint64_t maks_rozmiar_pliku = PSF_MAKS_ROZMIAR_PLIKU;

    for (uint64_t id = 1;
         id <=
            calkowita_liczba_wezlow;
         ++id) {

        wezel_indeksowy* w =
            pobierz_wezel(id);

        if (!w) {
            wypisz_log("[WAL-DBG] null inode");
            return false;
        }

        if (w->typ == TYP_WOLNY) {
            continue;
        }

        if (w->typ != TYP_PLIK &&
            w->typ != TYP_KATALOG) {
            wypisz_log("[WAL-DBG] invalid node type");
            return false;
        }

        if (!wskazniki_wezla_poprawne(
                w)) {
            wypisz_log("[WAL-DBG] invalid pointers in inode");
            return false;
        }

        if (w->typ == TYP_PLIK) {
            if (w->rozmiar_w_bajtach >
                maks_rozmiar_pliku) {
                wypisz_log("[WAL-DBG] file exceeding max size limit");
                return false;
            }

            const uint64_t wymagane =
                podziel_w_gore(
                    w->rozmiar_w_bajtach,
                    PSF_ROZMIAR_BLOKU
                );

            if (wymagane > PSF_MAX_BLOCKS_DUZY) {
                wypisz_log("[WAL-DBG] file exceeding max block count");
                return false;
            }

            for (uint64_t k = 0;
                 k < wymagane;
                 ++k) {

                uint64_t bid = BARK_BLOKU;
                if (!pobierz_wskaznik_bloku(w, k, &bid)) {
                    wypisz_log("[WAL-DBG] failed to read block pointer");
                    return false;
                }

                if (bid == BARK_BLOKU) {
                    continue;
                }

                if (!id_bloku_danych_poprawne(bid) ||
                    !czy_blok_danych_zajety(bid)) {
                    wypisz_log("[WAL-DBG] file references unallocated/out-of-range data block");
                    return false;
                }
            }
        } else {
            if (!waliduj_katalog(
                    id,
                    w)) {
                wypisz_log("[WAL-DBG] directory validation failed");
                return false;
            }
        }
    }

    return true;
}

/* =========================================================================
 * FORMATOWANIE
 * ========================================================================= */

bool sformatuj_ram_dysk(
    bool pozwol_na_zapis_fizyczny,
    bool uzyj_geometrii_partycji = false,
    bool odrocz_inicjalizacje_wezlow = false
) {
    if (!ram_dysk ||
        rozmiar_ram_dysku == 0 ||
        !parametry_formatu_obslugiwane()) {

        return false;
    }

    uint64_t rozmiar_uzyteczny =
        (static_cast<uint64_t>(
             rozmiar_ram_dysku) /
         static_cast<uint64_t>(
             PSF_ROZMIAR_BLOKU)) *
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU);

    if ((pozwol_na_zapis_fizyczny || uzyj_geometrii_partycji) &&
        nosnik_blokowy_dostepny) {
        const uint64_t sektory_dysku = bsp_partycja.count;
        if (sektory_dysku > BSP_START_LBA) {
            // Ostatni blok 4 KiB partycji jest poza BSP2 i przechowuje
            // dwie kopie znacznika instalacji (pozostałe sektory zerowe).
            if (sektory_dysku <= 8) return false;
            const uint64_t dostepne_sektory = sektory_dysku - 8 - BSP_START_LBA;
            const uint64_t sektory_na_blok =
                static_cast<uint64_t>(PSF_ROZMIAR_BLOKU) / ROZMIAR_SEKTORA;
            const uint64_t max_blokow = dostepne_sektory / sektory_na_blok;
            rozmiar_uzyteczny =
                max_blokow * static_cast<uint64_t>(PSF_ROZMIAR_BLOKU);
        }
    }

    if (rozmiar_uzyteczny <
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU) *
            MINIMALNA_LICZBA_BLOKOW) {

        return false;
    }

    const uint64_t ilosc_blokow =
        rozmiar_uzyteczny /
        static_cast<uint64_t>(
            PSF_ROZMIAR_BLOKU);

    const uint64_t bajty_bitmapy =
        podziel_w_gore(
            ilosc_blokow,
            8ULL
        );

    const uint64_t bloki_bitmapy =
        podziel_w_gore(
            bajty_bitmapy,
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU)
        );

    if (bloki_bitmapy == 0 ||
        1ULL + bloki_bitmapy >=
            ilosc_blokow) {

        return false;
    }

    const uint64_t start_wezlow =
        1ULL +
        bloki_bitmapy;

    const uint64_t pozostale =
        ilosc_blokow -
        start_wezlow;

    if (pozostale < 2) {
        return false;
    }

    uint64_t bloki_wezlow = 1024ULL;
    if (bloki_wezlow >= pozostale / 2ULL) {
        bloki_wezlow = pozostale / 4ULL;
    }
    if (bloki_wezlow == 0) {
        bloki_wezlow = 1;
    }

    const uint64_t start_danych =
        start_wezlow +
        bloki_wezlow;

    if (start_danych >=
        ilosc_blokow) {

        return false;
    }

    const uint64_t bajty_metadanych =
        start_danych * static_cast<uint64_t>(PSF_ROZMIAR_BLOKU);

    if (bajty_metadanych > static_cast<uint64_t>(rozmiar_ram_dysku)) {
        return false;
    }

    wyzeruj_pamiec(
        ram_dysk,
        static_cast<size_t>(bajty_metadanych)
    );
    wyczysc_cache_blokow();

    dysk_superblok =
        reinterpret_cast<superblok*>(
            ram_dysk
        );

    dysk_superblok->
        sygnatura[0] = 'B';

    dysk_superblok->
        sygnatura[1] = 'S';

    dysk_superblok->
        sygnatura[2] = 'P';

    dysk_superblok->
        sygnatura[3] = '2';

    dysk_superblok->calkowity_rozmiar = rozmiar_uzyteczny;

    dysk_superblok->
        ilosc_blokow =
        ilosc_blokow;

    dysk_superblok->
        start_wezlow =
        start_wezlow;

    dysk_superblok->
        start_danych =
        start_danych;

    calkowita_liczba_wezlow =
        bloki_wezlow *
        wezlow_na_blok();

    calkowita_liczba_blokow_danych =
        ilosc_blokow -
        start_danych;

    if (calkowita_liczba_wezlow == 0 ||
        calkowita_liczba_blokow_danych == 0) {

        return false;
    }

    /*
     * Instalator inicjalizuje wezly w ograniczonych partiach kolejnych
     * STEP-ow. Geometria i wyzerowany obraz metadanych sa juz stabilne,
     * ale PSF pozostaje niedostepny az do utworzenia korzenia i zapisania
     * calego obrazu. Zwykly start zachowuje dotychczasowa sciezke atomowa.
     */
    if (odrocz_inicjalizacje_wezlow) {
        psf_gotowy = false;
        zapis_trwaly_dozwolony = false;
        return true;
    }

    /*
     * Jawnie inicjalizujemy kazdy wezel. Nie zakladamy, ze wartosc
     * TYP_WOLNY jest rowna zeru.
     */
    for (uint64_t id = 1;
         id <=
            calkowita_liczba_wezlow;
         ++id) {

        wezel_indeksowy* w =
            pobierz_wezel(id);

        if (!w) {
            return false;
        }

        wyzeruj_wezel(
            w,
            TYP_WOLNY
        );
    }

    const uint64_t korzen =
        zaalokuj_wolny_wezel(
            TYP_KATALOG
        );

    if (korzen == 0) {
        return false;
    }

    dysk_superblok->
        id_korzenia =
        korzen;

    ostatni_szukany_blok_danych = 0;

    oznacz_caly_system_jako_brudny();

    psf_gotowy = true;

    zapis_trwaly_dozwolony =
        pozwol_na_zapis_fizyczny &&
        nosnik_blokowy_dostepny;

    // Log geometrii
    {
        auto dopisz_dec = [](char* dst, size_t cap, uint64_t v) {
            char tmp[24] = {0}; int n = 0;
            if (v == 0) tmp[n++] = '0';
            else {
                char rev[24]; int r = 0;
                while (v) { rev[r++] = '0' + static_cast<char>(v % 10ULL); v /= 10ULL; }
                while (r--) tmp[n++] = rev[r];
            }
            tmp[n] = '\0';
            size_t p = 0; while (dst[p] && p < cap) p++;
            size_t i = 0; while (tmp[i] && p + 1 < cap) dst[p++] = tmp[i++];
            dst[p] = '\0';
        };
        auto dopisz_str = [](char* dst, size_t cap, const char* src) {
            size_t p = 0; while (dst[p] && p < cap) p++;
            size_t i = 0; while (src[i] && p + 1 < cap) dst[p++] = src[i++];
            dst[p] = '\0';
        };
        char b1[64] = {}; dopisz_str(b1, sizeof(b1), "[PSF] total_blocks="); dopisz_dec(b1, sizeof(b1), ilosc_blokow); wypisz_log(b1);
        char b2[64] = {}; dopisz_str(b2, sizeof(b2), "[PSF] bitmap_start=1 count="); dopisz_dec(b2, sizeof(b2), bloki_bitmapy); wypisz_log(b2);
        char b3[64] = {}; dopisz_str(b3, sizeof(b3), "[PSF] inode_start="); dopisz_dec(b3, sizeof(b3), start_wezlow); dopisz_str(b3, sizeof(b3), " count="); dopisz_dec(b3, sizeof(b3), bloki_wezlow); wypisz_log(b3);
        char b4[64] = {}; dopisz_str(b4, sizeof(b4), "[PSF] data_start="); dopisz_dec(b4, sizeof(b4), start_danych); dopisz_str(b4, sizeof(b4), " count="); dopisz_dec(b4, sizeof(b4), ilosc_blokow - start_danych); wypisz_log(b4);
    }

    if (zapis_trwaly_dozwolony) {
        return bsp_zapisz_zmiany();
    }

    return true;
}

/* =========================================================================
 * WCZYTYWANIE ISTNIEJACEGO BSP2
 * ========================================================================= */

bool wczytaj_istniejacy_bsp2() {
    if (!ram_dysk ||
        rozmiar_ram_dysku == 0) {

        return false;
    }

    wyzeruj_pamiec(
        bufor_superbloku,
        sizeof(bufor_superbloku)
    );

    if (!Storage::read(
            bsp_partycja,
            BSP_START_LBA,
            1,
            bufor_superbloku)) {

        nosnik_blokowy_dostepny = false;
        return false;
    }

    nosnik_blokowy_dostepny = true;

    superblok kandydat{};

    kopiuj_pamiec(
        &kandydat,
        bufor_superbloku,
        sizeof(superblok)
    );

    if (!sygnatura_bsp2(
            &kandydat)) {

        /*
         * Nosnik odpowiada, ale nie ma BSP2.
         * To bezpieczny przypadek do utworzenia nowego systemu plikow.
         */
        zapis_trwaly_dozwolony = false;
        return false;
    }

    uint64_t wezly = 0;
    uint64_t dane = 0;

    /*
     * Walidacja PRZED uzyciem calkowity_rozmiar jako liczby sektorow.
     */
    if (!wylicz_geometrie(
            &kandydat,
            rozmiar_ram_dysku,
            &wezly,
            &dane)) {

        /*
         * Sygnatura BSP2 istnieje, ale superblok jest uszkodzony.
         * Nie pozwalamy formatowac/niszczyc tego nosnika automatycznie.
         */
        zapis_trwaly_dozwolony = false;
        return false;
    }

    const uint64_t sektory_na_blok =
        static_cast<uint64_t>(PSF_ROZMIAR_BLOKU) / ROZMIAR_SEKTORA;

    const uint64_t liczba_sektorow_meta =
        static_cast<uint64_t>(kandydat.start_danych) * sektory_na_blok;

    if (!czytaj_sektory_porcjami(
            BSP_START_LBA,
            liczba_sektorow_meta,
            ram_dysk)) {

        zapis_trwaly_dozwolony = false;
        return false;
    }

    wyczysc_cache_blokow();

    dysk_superblok =
        reinterpret_cast<superblok*>(
            ram_dysk
        );

    /*
     * Po DMA sprawdzamy geometrie ponownie. Chroni to przed sytuacja,
     * w ktorej pierwszy sektor i pelny odczyt nie sa spojne.
     */
    if (!wylicz_geometrie(
            dysk_superblok,
            rozmiar_ram_dysku,
            &wezly,
            &dane)) {

        zapis_trwaly_dozwolony = false;
        return false;
    }

    calkowita_liczba_wezlow =
        wezly;

    calkowita_liczba_blokow_danych =
        dane;

    ostatni_szukany_blok_danych = 0;

    psf_gotowy = true;

    if (!waliduj_metadane()) {
        psf_gotowy = false;
        zapis_trwaly_dozwolony = false;
        return false;
    }

    wyczysc_liste_brudnych();

    zapis_trwaly_dozwolony = true;
    ostatni_flush_ok = true;

    return true;
}

/* =========================================================================
 * TWORZENIE PLIKU / KATALOGU
 * ========================================================================= */

bool utworz_twor(
    const char* sciezka,
    uint8_t typ,
    uint8_t poziom_pzb
) {
    if (!psf_gotowy ||
        !bezpieczna_sciezka(
            sciezka)) {
        blad_startu(psf_gotowy?PsfBladStartu::ZlaSciezka:PsfBladStartu::NieGotowy);
        return false;
    }

    if (rozwiaz_sciezke(
            sciezka,
            nullptr,
            false) != 0) {
        blad_startu(PsfBladStartu::JuzIstnieje);
        return false;
    }

    char nazwa[
        PSF_MAX_NAZWA] = {};

    const uint64_t rodzic_id =
        rozwiaz_sciezke(
            sciezka,
            nazwa,
            true
        );

    if (rodzic_id == 0 ||
        !poprawna_nazwa(nazwa)) {
        blad_startu(PsfBladStartu::BrakRodzica);
        return false;
    }

    wezel_indeksowy* rodzic =
        pobierz_wezel(
            rodzic_id
        );

    if (!rodzic ||
        rodzic->typ != TYP_KATALOG) {
        blad_startu(PsfBladStartu::NieKatalog);
        return false;
    }

    if (znajdz_wpis_w_katalogu(
            rodzic_id,
            nazwa).znaleziony) {
        return false;
    }

    if(stan_startu)stan_startu->alokacjaWezla=PsfEtapStartu::Blad;
    const uint64_t nowy_id =
        zaalokuj_wolny_wezel(
            typ
        );

    if (nowy_id == 0) {
        blad_startu(PsfBladStartu::BrakWezlow);
        return false;
    }

    if(stan_startu){stan_startu->alokacjaWezla=PsfEtapStartu::Ok;stan_startu->wezel=nowy_id;}
    ustaw_metadane_nowego_wezla(
        pobierz_wezel(nowy_id),
        poziom_pzb
    );
    oznacz_wezel_jako_brudny(nowy_id);

    if (!dodaj_wpis_do_katalogu(
            rodzic_id,
            nowy_id,
            nazwa)) {
        (void)zwolnij_wezel(
            nowy_id
        );

        return false;
    }

    /*
     * In-memory commit jest juz wykonany. Blad fizycznego dysku nie
     * odwraca logicznie udanej operacji RAM FS; dirty state pozostaje
     * do ewentualnej kolejnej proby zapisu.
     */
    const bool zapis_ok=bsp_zapisz_zmiany();
    if(!zapis_ok)blad_startu(PsfBladStartu::Zapis);

    return true;
}

/* =========================================================================
 * ZAPIS PLIKU
 * ========================================================================= */

bool wskazniki_pliku_poprawne(
    const wezel_indeksowy* w
) {
    if (!w ||
        w->typ != TYP_PLIK) {

        return false;
    }

    if (w->blok_posredni_3 != BARK_BLOKU) return false;

    if (w->blok_posredni_1 != BARK_BLOKU &&
        w->blok_posredni_2 != BARK_BLOKU &&
        w->blok_posredni_1 == w->blok_posredni_2) {
        return false;
    }

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        const uint64_t id =
            w->wskazniki_blokow[k];

        if (id == BARK_BLOKU) {
            continue;
        }

        if (!id_bloku_danych_poprawne(id) ||
            !czy_blok_danych_zajety(id) ||
            id == w->blok_posredni_1 ||
            id == w->blok_posredni_2) {

            return false;
        }

        for (size_t j = 0;
             j < k;
             ++j) {

            if (w->wskazniki_blokow[j] ==
                id) {

                return false;
            }
        }
    }

    if (w->blok_posredni_1 != BARK_BLOKU) {
        if (!id_bloku_danych_poprawne(w->blok_posredni_1) || !czy_blok_danych_zajety(w->blok_posredni_1)) return false;
        uint8_t* blk = pobierz_blok_danych(w->blok_posredni_1);
        if (!blk) return false;
        uint64_t* arr = reinterpret_cast<uint64_t*>(blk);
        for (uint64_t i = 0; i < PTRS_PER_BLOCK; ++i) {
            uint64_t id = arr[i];
            if (id == BARK_BLOKU) continue;
            if (!id_bloku_danych_poprawne(id) ||
                !czy_blok_danych_zajety(id) ||
                id == w->blok_posredni_1 ||
                id == w->blok_posredni_2) {
                return false;
            }
            for (size_t k = 0; k < static_cast<size_t>(PSF_MAX_BLOKOW_W_WEZLE); ++k) {
                if (w->wskazniki_blokow[k] == id) return false;
            }
            for (uint64_t j = 0; j < i; ++j) {
                if (arr[j] == id) return false;
            }
        }
    }

    if (w->blok_posredni_2 != BARK_BLOKU) {
        if (!id_bloku_danych_poprawne(w->blok_posredni_2) || !czy_blok_danych_zajety(w->blok_posredni_2)) return false;
        for (uint64_t i = 0; i < PTRS_PER_BLOCK; ++i) {
            uint8_t* blk1 = pobierz_blok_danych(w->blok_posredni_2);
            if (!blk1) return false;
            uint64_t* arr1 = reinterpret_cast<uint64_t*>(blk1);
            uint64_t id2 = arr1[i];
            if (id2 == BARK_BLOKU) continue;
            if (!id_bloku_danych_poprawne(id2) ||
                !czy_blok_danych_zajety(id2) ||
                id2 == w->blok_posredni_1 ||
                id2 == w->blok_posredni_2) {
                return false;
            }
            for (size_t k = 0; k < static_cast<size_t>(PSF_MAX_BLOKOW_W_WEZLE); ++k) {
                if (w->wskazniki_blokow[k] == id2) return false;
            }
            for (uint64_t j = 0; j < i; ++j) {
                if (arr1[j] == id2) return false;
            }
            uint8_t* blk2 = pobierz_blok_danych(id2);
            if (!blk2) return false;
            uint64_t* arr2 = reinterpret_cast<uint64_t*>(blk2);
            for (uint64_t j = 0; j < PTRS_PER_BLOCK; ++j) {
                uint64_t id = arr2[j];
                if (id == BARK_BLOKU) continue;
                if (!id_bloku_danych_poprawne(id) ||
                    !czy_blok_danych_zajety(id) ||
                    id == w->blok_posredni_1 ||
                    id == w->blok_posredni_2 ||
                    id == id2) {
                    return false;
                }
                for (size_t k = 0; k < static_cast<size_t>(PSF_MAX_BLOKOW_W_WEZLE); ++k) {
                    if (w->wskazniki_blokow[k] == id) return false;
                }
                for (uint64_t m = 0; m < j; ++m) {
                    if (arr2[m] == id) return false;
                }
            }
        }
    }

    return true;
}

constexpr uint32_t PSF_MAKS_GLEBOKOSC_KOPII = 32U;

/* Dotyczy wylacznie jeszcze nieopublikowanego poddrzewa utworzonego przez
 * kopiowanie. Nie przyjmuje sciezki ani nie usuwa istniejacych obiektow. */
bool zwolnij_nieopublikowana_kopie(uint64_t id, uint32_t glebokosc) {
    if (id == 0 || glebokosc > PSF_MAKS_GLEBOKOSC_KOPII) return false;
    wezel_indeksowy* w = pobierz_wezel(id);
    if (!w || w->typ == TYP_WOLNY) return false;

    if (w->typ == TYP_KATALOG) {
        const uint64_t na_blok = wpisow_katalogowych_na_blok();
        for (size_t k = 0; k < static_cast<size_t>(PSF_MAX_BLOKOW_W_WEZLE); ++k) {
            const uint64_t blok_id = w->wskazniki_blokow[k];
            if (blok_id == BARK_BLOKU) continue;
            wpis_katalogowy* wpisy = pobierz_wpisy_katalogowe(blok_id);
            if (!wpisy) return false;
            for (uint64_t j = 0; j < na_blok; ++j) {
                if (wpisy[j].id_wezla != 0 &&
                    !zwolnij_nieopublikowana_kopie(
                        wpisy[j].id_wezla, glebokosc + 1U)) return false;
            }
            if (!zwolnij_blok_danych(blok_id)) return false;
            w->wskazniki_blokow[k] = BARK_BLOKU;
        }
    } else if (w->typ == TYP_PLIK) {
        if (!zwolnij_wszystkie_bloki_wezla(w, id)) return false;
    } else {
        return false;
    }
    return zwolnij_wezel(id);
}

uint64_t sklonuj_wezel_rekurencyjnie(uint64_t zrodlo_id, uint8_t poziom_pzb,
                                     uint32_t glebokosc,
                                     uint64_t* przodkowie) {
    if (!przodkowie || glebokosc >= PSF_MAKS_GLEBOKOSC_KOPII) return 0;
    for (uint32_t i = 0; i < glebokosc; ++i)
        if (przodkowie[i] == zrodlo_id) return 0; /* uszkodzony cykl */

    wezel_indeksowy* zrodlo = pobierz_wezel(zrodlo_id);
    if (!zrodlo || (zrodlo->typ != TYP_PLIK && zrodlo->typ != TYP_KATALOG))
        return 0;
    if (zrodlo->typ == TYP_PLIK && !wskazniki_pliku_poprawne(zrodlo)) return 0;
    przodkowie[glebokosc] = zrodlo_id;

    const uint64_t nowy_id = zaalokuj_wolny_wezel(zrodlo->typ);
    wezel_indeksowy* nowy = pobierz_wezel(nowy_id);
    if (nowy_id == 0 || !nowy) return 0;
    ustaw_metadane_nowego_wezla(nowy, poziom_pzb);
    oznacz_wezel_jako_brudny(nowy_id);

    if (zrodlo->typ == TYP_PLIK) {
        const uint64_t potrzebne = podziel_w_gore(
            zrodlo->rozmiar_w_bajtach,
            static_cast<uint64_t>(PSF_ROZMIAR_BLOKU));
        for (uint64_t k = 0; k < potrzebne; ++k) {
            uint64_t stary_blok = BARK_BLOKU;
            if (!pobierz_wskaznik_bloku(zrodlo, k, &stary_blok) || stary_blok == BARK_BLOKU) {
                (void)zwolnij_nieopublikowana_kopie(nowy_id, glebokosc);
                return 0;
            }
            const uint64_t nowy_blok = zaalokuj_wolny_blok_danych();
            if (nowy_blok == BARK_BLOKU) {
                (void)zwolnij_nieopublikowana_kopie(nowy_id, glebokosc);
                return 0;
            }
            if (!ustaw_wskaznik_bloku(nowy, k, nowy_blok, nowy_id)) {
                (void)zwolnij_blok_danych(nowy_blok);
                (void)zwolnij_nieopublikowana_kopie(nowy_id, glebokosc);
                return 0;
            }
            uint8_t* src = pobierz_blok_danych(stary_blok);
            uint8_t* dst = pobierz_blok_danych(nowy_blok);
            if (!src || !dst) {
                (void)zwolnij_nieopublikowana_kopie(nowy_id, glebokosc);
                return 0;
            }
            uint64_t porcja = zrodlo->rozmiar_w_bajtach -
                               k * static_cast<uint64_t>(PSF_ROZMIAR_BLOKU);
            if (porcja > PSF_ROZMIAR_BLOKU) porcja = PSF_ROZMIAR_BLOKU;
            kopiuj_pamiec(dst, src, porcja);
            if (porcja < PSF_ROZMIAR_BLOKU)
                wyzeruj_pamiec(dst + porcja, PSF_ROZMIAR_BLOKU - porcja);
            const uint64_t absolutny = absolutny_blok_danych(nowy_blok);
            if (absolutny != UINT64_MAX) oznacz_blok_jako_brudny(absolutny);
        }
        nowy->rozmiar_w_bajtach = zrodlo->rozmiar_w_bajtach;
        oznacz_wezel_jako_brudny(nowy_id);
        return nowy_id;
    }

    const uint64_t na_blok = wpisow_katalogowych_na_blok();
    for (size_t k = 0; k < static_cast<size_t>(PSF_MAX_BLOKOW_W_WEZLE); ++k) {
        const uint64_t blok_id = zrodlo->wskazniki_blokow[k];
        if (blok_id == BARK_BLOKU) continue;
        wpis_katalogowy* wpisy = pobierz_wpisy_katalogowe(blok_id);
        if (!wpisy) {
            (void)zwolnij_nieopublikowana_kopie(nowy_id, glebokosc);
            return 0;
        }
        for (uint64_t j = 0; j < na_blok; ++j) {
            if (wpisy[j].id_wezla == 0) continue;
            const uint64_t dziecko = sklonuj_wezel_rekurencyjnie(
                wpisy[j].id_wezla, poziom_pzb, glebokosc + 1U, przodkowie);
            if (dziecko == 0 || !dodaj_wpis_do_katalogu(
                    nowy_id, dziecko, wpisy[j].nazwa)) {
                if (dziecko != 0)
                    (void)zwolnij_nieopublikowana_kopie(dziecko, glebokosc + 1U);
                (void)zwolnij_nieopublikowana_kopie(nowy_id, glebokosc);
                return 0;
            }
        }
    }
    return nowy_id;
}

/* =========================================================================
 * PUBLICZNE API - INICJALIZACJA
 * ========================================================================= */

} // namespace

extern "C" void inicjalizuj_psf(
    void* adres_ram_dysku,
    uint32_t rozmiar_w_bajtach
) {
    BlokadaPSF blokada;

    /*
     * Reset stanu poprzedniej instancji.
     */
    ram_dysk =
        static_cast<uint8_t*>(
            adres_ram_dysku
        );

    rozmiar_ram_dysku =
        rozmiar_w_bajtach;

    dysk_superblok = nullptr;
    calkowita_liczba_wezlow = 0;
    calkowita_liczba_blokow_danych = 0;
    ostatni_szukany_blok_danych = 0;

    psf_gotowy = false;
    nosnik_blokowy_dostepny = false;
    zapis_trwaly_dozwolony = false;
    ostatni_flush_ok = true;

    wyczysc_liste_brudnych();

    if (!ram_dysk ||
        !parametry_formatu_obslugiwane() ||
        rozmiar_w_bajtach <
            static_cast<uint64_t>(
                PSF_ROZMIAR_BLOKU) *
            MINIMALNA_LICZBA_BLOKOW) {

        wypisz_log(
            "[BSP-BLAD] Nieprawidlowy obszar ramdysku lub parametry formatu."
        );
        return;
    }

    wypisz_log(
        "[BSP] Sprawdzanie nosnika blokowego w poszukiwaniu BSP2..."
    );

    /*
     * Najpierw probujemy bezpiecznie zamontowac istniejacy system.
     */
    if (wczytaj_istniejacy_bsp2()) {
        wypisz_log(
            "[BSP] Trwaly BSP2 pomyslnie zaladowany z nosnika blokowego."
        );
        return;
    }

    /*
     * Jesli znaleziono sygnature BSP2, ale zapis_trwaly_dozwolony == false
     * przy dostepnym nosniku blokowym, traktujemy dysk jako potencjalnie uszkodzony.
     * Tworzymy tylko awaryjny RAM FS - bez nadpisania fizycznego nosnika.
     */
    const bool uszkodzony_istniejacy_bsp2 =
        nosnik_blokowy_dostepny &&
        !zapis_trwaly_dozwolony &&
        bufor_superbloku[0] == 'B' &&
        bufor_superbloku[1] == 'S' &&
        bufor_superbloku[2] == 'P' &&
        bufor_superbloku[3] == '2';

    if (uszkodzony_istniejacy_bsp2) {
        wypisz_log(
            "[BSP-BLAD] Wykryto uszkodzony BSP2. Dysk NIE zostanie automatycznie sformatowany."
        );

        nosnik_blokowy_dostepny = false;
        bsp_partycja = {};
        if (sformatuj_ram_dysk(false)) {
            wypisz_log(
                "[BSP] Uruchomiono awaryjny system plikow tylko w RAM."
            );
        }

        return;
    }

    nosnik_blokowy_dostepny = false;
    bsp_partycja = {};
    wypisz_log("[BSP] Brak zamontowanej partycji. System plikow tylko w RAM; zero zapisow dysku.");

    if (!sformatuj_ram_dysk(
            false)) {

        wypisz_log(
            "[BSP-BLAD] Nie mozna utworzyc systemu plikow."
        );
        return;
    }

    if (nosnik_blokowy_dostepny &&
        ostatni_flush_ok) {

        wypisz_log(
            "[BSP] Format BSP2 zapisany na nosniku blokowym."
        );
    } else if (nosnik_blokowy_dostepny) {
        wypisz_log(
            "[BSP] BSP2 dziala w RAM, ale zapis poczatkowy na nosniku blokowym nie powiodl sie."
        );
    }
}

void psf_ustaw_partycje(const Storage::Partition* p) { bsp_partycja=p?*p:Storage::Partition{}; }
const Storage::Device* psf_urzadzenie_trwale() { return nosnik_blokowy_dostepny ? bsp_partycja.device : nullptr; }

bool psf_formatuj_instalacje(const Storage::Partition& p) {
    BlokadaPSF guard;
    if(!blokowy_zapis_instalatora() || !p.device || !Storage::range(p.start,p.count,p.device->sectors) ||
       p.device->sector_size!=512 || p.count>Storage::BspMaxBytes/512) return false;
    bsp_partycja=p;nosnik_blokowy_dostepny=true;zapis_trwaly_dozwolony=false;
    psf_gotowy=false;wyczysc_liste_brudnych();wyczysc_cache_blokow();
    return sformatuj_ram_dysk(true);
}

constexpr uint64_t FORMAT_WEZLOW_NA_KROK=32;
static_assert(FORMAT_WEZLOW_NA_KROK<=32,
              "BSP2FORMAT STEP musi miec ograniczona partie wezlow");
uint64_t format_instalacji_blok=0,format_instalacji_razem=0;
uint64_t format_instalacji_wezel=0,format_instalacji_wykonane=0;
uint64_t format_instalacji_partie_wezlow=0;
uint32_t format_instalacji_faza=0;
bool format_instalacji_trwa=false;
bool psf_formatuj_instalacje_start(const Storage::Partition& p,uint64_t* razem) {
    BlokadaPSF guard;
    if(!blokowy_zapis_instalatora() || !p.device || !Storage::range(p.start,p.count,p.device->sectors) ||
       p.device->sector_size!=512 || p.count>Storage::BspMaxBytes/512)return false;
    bsp_partycja=p;nosnik_blokowy_dostepny=true;zapis_trwaly_dozwolony=false;
    psf_gotowy=false;wyczysc_liste_brudnych();wyczysc_cache_blokow();
    // Obraz metadanych powstaje w RAM, ale musi opisywac dokladnie te sama
    // partycje co dawny, atomowy format. Zapis wykonaja kolejne STEP-y.
    if(!sformatuj_ram_dysk(false,true,true)||!dysk_superblok)return false;
    format_instalacji_blok=0;format_instalacji_wezel=1;format_instalacji_wykonane=0;
    format_instalacji_partie_wezlow=
        (calkowita_liczba_wezlow+FORMAT_WEZLOW_NA_KROK-1)/FORMAT_WEZLOW_NA_KROK;
    if(dysk_superblok->start_danych>UINT64_MAX-format_instalacji_partie_wezlow-2)return false;
    format_instalacji_razem=format_instalacji_partie_wezlow+
        dysk_superblok->start_danych+2;
    format_instalacji_faza=0;
    format_instalacji_trwa=format_instalacji_razem!=0;psf_gotowy=false;
    zapis_trwaly_dozwolony=true;
    if(razem)*razem=format_instalacji_razem;
    return format_instalacji_trwa;
}
bool psf_formatuj_instalacje_step(uint64_t* wykonane,bool* gotowe) {
    BlokadaPSF guard;
    if(wykonane)*wykonane=format_instalacji_wykonane;
    if(gotowe)*gotowe=false;
    if(!format_instalacji_trwa||!dysk_superblok||!blokowy_zapis_instalatora())return false;

    if(format_instalacji_faza==0){
        uint64_t pozostalo=FORMAT_WEZLOW_NA_KROK;
        while(format_instalacji_wezel<=calkowita_liczba_wezlow&&pozostalo--){
            wezel_indeksowy* w=pobierz_wezel(format_instalacji_wezel);
            if(!w)return false;
            wyzeruj_wezel(w,TYP_WOLNY);
            ++format_instalacji_wezel;
        }
        ++format_instalacji_wykonane;
        if(format_instalacji_wezel>calkowita_liczba_wezlow)format_instalacji_faza=1;
        if(wykonane)*wykonane=format_instalacji_wykonane;
        return true;
    }

    if(format_instalacji_faza==1){
        const uint64_t korzen=zaalokuj_wolny_wezel(TYP_KATALOG);
        if(korzen==0)return false;
        dysk_superblok->id_korzenia=korzen;
        ostatni_szukany_blok_danych=0;
        oznacz_caly_system_jako_brudny();
        ++format_instalacji_wykonane;
        format_instalacji_faza=2;
        if(wykonane)*wykonane=format_instalacji_wykonane;
        return true;
    }

    if(format_instalacji_faza==2&&format_instalacji_blok<dysk_superblok->start_danych){
        const uint64_t block=format_instalacji_blok;
        if(!Storage::write_verify(bsp_partycja,BSP_START_LBA+block*8,8,ram_dysk+block*PSF_ROZMIAR_BLOKU))return false;
        ++format_instalacji_blok;++format_instalacji_wykonane;
        if(wykonane)*wykonane=format_instalacji_wykonane;
        return true;
    }

    if(!Storage::flush(bsp_partycja))return false;
    ++format_instalacji_wykonane;
    wyczysc_liste_brudnych();wyczysc_cache_blokow();ostatni_flush_ok=true;
    psf_gotowy=true;format_instalacji_trwa=false;format_instalacji_faza=3;
    if(wykonane)*wykonane=format_instalacji_wykonane;
    if(gotowe)*gotowe=true;
    return true;
}

bool psf_znacznik_instalacji(const uint8_t uuid[16],bool complete) {
    BlokadaPSF guard;
    if(!blokowy_zapis_instalatora()||!dysk_superblok||!bsp_zapisz_zmiany())return false;
    return Storage::marker(bsp_partycja,uuid,dysk_superblok->calkowity_rozmiar,complete);
}

bool psf_weryfikuj_dysk() {
    BlokadaPSF guard;
    if(!nosnik_blokowy_dostepny||!dysk_superblok||!bsp_zapisz_zmiany())return false;
    uint8_t check[4096];
    for(uint64_t i=0;i<dysk_superblok->start_danych;++i){
        if(!Storage::read(bsp_partycja,i*8,8,check)||!Storage::equal(check,ram_dysk+i*4096,4096))return false;
    }
    wyczysc_cache_blokow();
    return waliduj_metadane();
}
uint64_t weryfikacja_dysku_blok=0;
bool weryfikacja_dysku_trwa=false;
bool psf_weryfikuj_dysk_start(uint64_t* razem) {
    BlokadaPSF guard;
    if(!nosnik_blokowy_dostepny||!dysk_superblok||!bsp_zapisz_zmiany())return false;
    weryfikacja_dysku_blok=0;weryfikacja_dysku_trwa=true;
    if(razem)*razem=dysk_superblok->start_danych;
    return true;
}
bool psf_weryfikuj_dysk_step(uint64_t* wykonane,bool* gotowe) {
    BlokadaPSF guard;
    if(wykonane)*wykonane=weryfikacja_dysku_blok;
    if(gotowe)*gotowe=false;
    if(!weryfikacja_dysku_trwa||!dysk_superblok)return false;
    if(weryfikacja_dysku_blok<dysk_superblok->start_danych){
        uint8_t check[4096];const uint64_t block=weryfikacja_dysku_blok;
        if(!Storage::read(bsp_partycja,block*8,8,check)||!Storage::equal(check,ram_dysk+block*4096,4096))return false;
        ++weryfikacja_dysku_blok;if(wykonane)*wykonane=weryfikacja_dysku_blok;return true;
    }
    wyczysc_cache_blokow();if(!waliduj_metadane())return false;
    weryfikacja_dysku_trwa=false;if(gotowe)*gotowe=true;return true;
}

/* =========================================================================
 * PUBLICZNE API - TWORZENIE
 * ========================================================================= */

extern "C" bool utworz_katalog(
    const char* sciezka
) {
    BlokadaPSF blokada;

    return utworz_twor(
        sciezka,
        TYP_KATALOG,
        DOMYSLNY_PZB_PLIKU
    );
}

extern "C" bool utworz_plik(
    const char* sciezka
) {
    BlokadaPSF blokada;

    return utworz_twor(
        sciezka,
        TYP_PLIK,
        DOMYSLNY_PZB_PLIKU
    );
}

extern "C" bool utworz_katalog_z_pzb(
    const char* sciezka,
    uint8_t poziom_pzb
) {
    BlokadaPSF blokada;
    if (poziom_pzb > 5U) return false;
    return utworz_twor(sciezka, TYP_KATALOG, poziom_pzb);
}

extern "C" bool utworz_plik_z_pzb(
    const char* sciezka,
    uint8_t poziom_pzb
) {
    BlokadaPSF blokada;
    if (poziom_pzb > 5U) return false;
    return utworz_twor(sciezka, TYP_PLIK, poziom_pzb);
}

extern "C" bool psf_czy_gotowy() {
    return psf_gotowy;
}

/* =========================================================================
 * PUBLICZNE API - ZAPIS
 * ========================================================================= */

/* =========================================================================
 * PUBLICZNE API - ZAPIS I ODCZYT
 * ========================================================================= */

static bool zapisz_fragment_pliku_wewn(
    const char* sciezka,
    uint64_t offset,
    const char* dane,
    uint32_t len
) {
    if (!psf_gotowy || !sciezka || !dane || len == 0) return false;
    if (len > 1U * 1024U * 1024U) return false;
    uint64_t end = 0;
    if (__builtin_add_overflow(offset, static_cast<uint64_t>(len), &end)) return false;
    if (end > PSF_MAKS_ROZMIAR_PLIKU) return false;
    const uint64_t pid = rozwiaz_sciezke(sciezka, nullptr, false);
    if (pid == 0) return false;
    wezel_indeksowy* w = pobierz_wezel(pid);
    if (!w || w->typ != TYP_PLIK || !wskazniki_pliku_poprawne(w)) return false;
    const uint64_t potrzebne = (end + PSF_ROZMIAR_BLOKU - 1ULL) / PSF_ROZMIAR_BLOKU;
    uint64_t stare = (w->rozmiar_w_bajtach + PSF_ROZMIAR_BLOKU - 1ULL) / PSF_ROZMIAR_BLOKU;
    if (w->rozmiar_w_bajtach == 0) stare = 0;

    for (uint64_t k = stare; k < potrzebne; ++k) {
        uint64_t cur = BARK_BLOKU;
        if (!pobierz_wskaznik_bloku(w, k, &cur)) return false;
        if (cur != BARK_BLOKU) continue;
        const uint64_t nid = zaalokuj_wolny_blok_danych();
        if (nid == BARK_BLOKU) return false;
        if (!ustaw_wskaznik_bloku(w, k, nid, pid)) {
            (void)zwolnij_blok_danych(nid);
            return false;
        }
    }

    uint64_t napisano = 0;
    while (napisano < len) {
        const uint64_t cur_off = offset + napisano;
        const uint64_t blk_idx = cur_off / PSF_ROZMIAR_BLOKU;
        const uint64_t w_bloku = cur_off % PSF_ROZMIAR_BLOKU;
        uint64_t bid = BARK_BLOKU;
        if (!pobierz_wskaznik_bloku(w, blk_idx, &bid) || bid == BARK_BLOKU) return false;
        const uint64_t w_bloku_dost = PSF_ROZMIAR_BLOKU - w_bloku;
        uint64_t porcja = len - napisano;
        if (porcja > w_bloku_dost) porcja = w_bloku_dost;
        const bool pelny = (w_bloku == 0 && porcja == PSF_ROZMIAR_BLOKU);
        uint8_t* blk = pobierz_blok_danych(bid, !pelny);
        if (!blk) return false;
        kopiuj_pamiec(blk + w_bloku, dane + napisano, porcja);
        const uint64_t abs = absolutny_blok_danych(bid);
        if (abs != UINT64_MAX) oznacz_blok_jako_brudny(abs);
        napisano += porcja;
    }
    if (end > w->rozmiar_w_bajtach) {
        w->rozmiar_w_bajtach = end;
        oznacz_wezel_jako_brudny(pid);
    }
    return true;
}

extern "C" bool zapisz_fragment_pliku(
    const char* sciezka,
    uint64_t offset,
    const char* dane,
    uint32_t len
) {
    BlokadaPSF blokada;
    return zapisz_fragment_pliku_wewn(sciezka, offset, dane, len);
}

extern "C" bool zapisz_do_pliku(
    const char* sciezka,
    const char* dane,
    uint32_t dlugosc
) {
    BlokadaPSF blokada;

    if (!psf_gotowy ||
        !sciezka ||
        (dlugosc != 0 &&
          !dane)) {
        return false;
    }

    const uint64_t plik_id =
        rozwiaz_sciezke(
            sciezka,
            nullptr,
            false
        );

    if (plik_id == 0) {
        return false;
    }

    wezel_indeksowy* w =
        pobierz_wezel(
            plik_id
        );

    if (!w ||
        w->typ != TYP_PLIK ||
        !wskazniki_pliku_poprawne(
            w)) {
        return false;
    }

    if (static_cast<uint64_t>(dlugosc) > PSF_MAKS_ROZMIAR_PLIKU) {
        return false;
    }

    if (dlugosc == 0) {
        if (!zwolnij_wszystkie_bloki_wezla(w, plik_id)) return false;
        w->rozmiar_w_bajtach = 0;
        oznacz_wezel_jako_brudny(plik_id);
        return bsp_zapisz_zmiany();
    }

    // Skroc jesli plik byl wiekszy
    if (w->rozmiar_w_bajtach > static_cast<uint64_t>(dlugosc)) {
        const uint64_t potrzebne = (static_cast<uint64_t>(dlugosc) + PSF_ROZMIAR_BLOKU - 1ULL) / PSF_ROZMIAR_BLOKU;
        const uint64_t stare = (w->rozmiar_w_bajtach + PSF_ROZMIAR_BLOKU - 1ULL) / PSF_ROZMIAR_BLOKU;
        for (uint64_t k = potrzebne; k < stare; ++k) {
            uint64_t b = BARK_BLOKU;
            if (pobierz_wskaznik_bloku(w, k, &b) && b != BARK_BLOKU) {
                (void)zwolnij_blok_danych(b);
                (void)ustaw_wskaznik_bloku(w, k, BARK_BLOKU, plik_id);
            }
        }
        w->rozmiar_w_bajtach = dlugosc;
        oznacz_wezel_jako_brudny(plik_id);
    }

    uint32_t offset = 0;
    while (offset < dlugosc) {
        uint32_t chunk = dlugosc - offset;
        if (chunk > 1024U * 1024U) chunk = 1024U * 1024U;
        if (!zapisz_fragment_pliku_wewn(sciezka, static_cast<uint64_t>(offset), dane + offset, chunk)) {
            return false;
        }
        offset += chunk;
    }

    return bsp_zapisz_zmiany();
}

static bool czytaj_fragment_pliku_wewn(
    const char* sciezka,
    uint64_t offset,
    char* bufor,
    uint32_t dlugosc,
    uint32_t* odczytano
) {
    if (odczytano) *odczytano = 0;
    if (!psf_gotowy ||
        !sciezka ||
        !bufor ||
        dlugosc == 0) {
        return false;
    }
    const uint64_t plik_id = rozwiaz_sciezke(sciezka, nullptr, false);
    if (plik_id == 0) return false;
    wezel_indeksowy* w = pobierz_wezel(plik_id);
    if (!w || w->typ != TYP_PLIK || !wskazniki_pliku_poprawne(w)) return false;
    const uint64_t rozmiar = w->rozmiar_w_bajtach;
    if (offset >= rozmiar) {
        // EOF - normalny przypadek, odczytano 0
        return true;
    }
    const uint64_t dostepne = rozmiar - offset;
    const uint64_t do_odczytu = (static_cast<uint64_t>(dlugosc) < dostepne) ? static_cast<uint64_t>(dlugosc) : dostepne;
    uint64_t przeczytano = 0;
    while (przeczytano < do_odczytu) {
        const uint64_t cur_off = offset + przeczytano;
        const uint64_t blok_idx = cur_off / PSF_ROZMIAR_BLOKU;
        const uint64_t w_bloku = cur_off % PSF_ROZMIAR_BLOKU;
        uint64_t bid = BARK_BLOKU;
        if (!pobierz_wskaznik_bloku(w, blok_idx, &bid) || bid == BARK_BLOKU) return false;
        uint8_t* blk = pobierz_blok_danych(bid);
        if (!blk) return false;
        const uint64_t w_bloku_dost = PSF_ROZMIAR_BLOKU - w_bloku;
        uint64_t porcja = do_odczytu - przeczytano;
        if (porcja > w_bloku_dost) porcja = w_bloku_dost;
        kopiuj_pamiec(bufor + przeczytano, blk + w_bloku, porcja);
        przeczytano += porcja;
    }
    if (odczytano) *odczytano = static_cast<uint32_t>(przeczytano);
    return true;
}

extern "C" bool czytaj_fragment_pliku(
    const char* sciezka,
    uint64_t offset,
    char* bufor,
    uint32_t dlugosc,
    uint32_t* odczytano
) {
    BlokadaPSF blokada;
    return czytaj_fragment_pliku_wewn(sciezka, offset, bufor, dlugosc, odczytano);
}

extern "C" bool czytaj_z_pliku(
    const char* sciezka,
    char* bufor,
    uint32_t max_dlugosc
) {
    BlokadaPSF blokada;
    if (!psf_gotowy || !sciezka) return false;
    if (max_dlugosc == 0) {
        const uint64_t id = rozwiaz_sciezke(sciezka, nullptr, false);
        wezel_indeksowy* w = pobierz_wezel(id);
        return id != 0 && w && w->typ == TYP_PLIK;
    }
    if (!bufor) return false;
    uint32_t odczytano = 0;
    return czytaj_fragment_pliku_wewn(sciezka, 0, bufor, max_dlugosc, &odczytano);
}

/* =========================================================================
 * PUBLICZNE API - BUFOR DLA LOADERA
 * ========================================================================= */

extern "C" bool bsp_wczytaj_obraz_bur(const char* sciezka, BurObrazPliku* out,
    uint64_t maks, const char* (*walidator)(const NaglowekBur&, uint64_t)) {
    if (!out) return false;
    *out = {};
    out->blad = "filesystem-not-ready";
    if (!psf_gotowy || !sciezka || !walidator) return false;
    if (maks > BUR_MAKS_ROZMIAR_PLIKU || maks > UINT32_MAX || maks > SIZE_MAX)
        { out->blad = "invalid-image-limit"; return false; }
    BlokadaPSF blokada; // inode, prefix and complete image belong to one version
    const uint64_t id = rozwiaz_sciezke(sciezka, nullptr, false);
    const wezel_indeksowy* w = id ? pobierz_wezel(id) : nullptr;
    if (!w) { out->blad = "file-not-found"; return false; }
    out->rozmiar = w->rozmiar_w_bajtach;
    if (w->typ != TYP_PLIK) { out->blad = "not-a-file"; return false; }
    const uint32_t prefix = out->rozmiar < sizeof(NaglowekBur)
        ? static_cast<uint32_t>(out->rozmiar) : sizeof(NaglowekBur);
    uint32_t n = 0;
    if (prefix && (!czytaj_fragment_pliku_wewn(sciezka, 0,
            reinterpret_cast<char*>(&out->naglowek), prefix, &n) || n != prefix))
        { out->blad = "header-read-failed"; return false; }
    out->bajty_naglowka = n;
    if (out->rozmiar > maks) { out->blad = "file-size-exceeds-maximum"; return false; }
    if (n < sizeof(NaglowekBur)) { out->blad = "truncated-header"; return false; }
    const bool v2 = out->naglowek.magia[0] == 'B' && out->naglowek.magia[1] == 'U' &&
        out->naglowek.magia[2] == 'R' && out->naglowek.magia[3] == 2;
    // v2 is validated from the complete private snapshot by the production loader.
    out->blad = v2 ? nullptr : walidator(out->naglowek, out->rozmiar);
    if (out->blad) return false;
    auto* obraz = static_cast<uint8_t*>(kmalloc(out->rozmiar));
    if (!obraz) { out->blad = "image-allocation-failed"; return false; }
    if (!czytaj_fragment_pliku_wewn(sciezka, 0, reinterpret_cast<char*>(obraz),
            static_cast<uint32_t>(out->rozmiar), &n) || n != out->rozmiar) {
        kfree(obraz);
        out->blad = "image-read-failed";
        return false;
    }
    out->dane = obraz;
    return true;
}

extern "C" uint8_t* bsp_wczytaj_plik_do_pamieci(
    const char* sciezka,
    uint64_t* rozmiar_wyj
) {
    if (!rozmiar_wyj) {
        return nullptr;
    }

    *rozmiar_wyj = 0;

    if (!psf_gotowy ||
        !sciezka) {

        return nullptr;
    }

    BlokadaPSF blokada;

    const uint64_t id =
        rozwiaz_sciezke(
            sciezka,
            nullptr,
            false
        );

    if (id == 0) {
        wypisz_log("[LOADER-DBG] id is 0");
        return nullptr;
    }

    wezel_indeksowy* w =
        pobierz_wezel(id);

    if (!w ||
        w->typ != TYP_PLIK) {
        wypisz_log("[LOADER-DBG] not file");
        return nullptr;
    }

    if (w->rozmiar_w_bajtach == 0) {
        wypisz_log("[LOADER-DBG] rsz is 0");
        return nullptr;
    }

    if (w->rozmiar_w_bajtach > MAX_LOADER_BUF) {
        wypisz_log("[LOADER-DBG] rsz > MAX_LOADER_BUF");
        return nullptr;
    }

    const uint32_t rsz = static_cast<uint32_t>(w->rozmiar_w_bajtach);
    uint32_t odczytano = 0;
    if (!czytaj_fragment_pliku_wewn(sciezka, 0, reinterpret_cast<char*>(bufor_wymiany_plikow), rsz, &odczytano)) {
        wypisz_log("[LOADER-DBG] czytaj_fragment_pliku fail");
        *rozmiar_wyj = 0;
        return nullptr;
    }
    if (odczytano != rsz) {
        wypisz_log("[LOADER-DBG] odczytano != rsz");
        *rozmiar_wyj = 0;
        return nullptr;
    }

    *rozmiar_wyj = rsz;
    return bufor_wymiany_plikow;
}

/* =========================================================================
 * PUBLICZNE API - LISTOWANIE
 * ========================================================================= */

extern "C" bool wylistuj_katalog(
    const char* sciezka,
    char* bufor,
    uint32_t max_dlugosc
) {
    BlokadaPSF blokada;

    if (!psf_gotowy ||
        !sciezka ||
        !bufor ||
        max_dlugosc == 0) {

        return false;
    }

    const uint64_t katalog_id =
        rozwiaz_sciezke(
            sciezka,
            nullptr,
            false
        );

    if (katalog_id == 0) {
        return false;
    }

    wezel_indeksowy* katalog =
        pobierz_wezel(
            katalog_id
        );

    if (!katalog ||
        katalog->typ != TYP_KATALOG) {

        return false;
    }

    uint32_t pozycja = 0;
    bufor[0] = '\0';

    const uint64_t wpisow_na_blok =
        wpisow_katalogowych_na_blok();

    for (size_t k = 0;
         k <
            static_cast<size_t>(
                PSF_MAX_BLOKOW_W_WEZLE);
         ++k) {

        const uint64_t id_bloku =
            katalog->
                wskazniki_blokow[k];

        if (id_bloku ==
            BARK_BLOKU) {

            continue;
        }

        wpis_katalogowy* wpisy =
            pobierz_wpisy_katalogowe(
                id_bloku
            );

        if (!wpisy) {
            return false;
        }

        for (uint64_t j = 0;
             j < wpisow_na_blok;
             ++j) {

            if (wpisy[j].id_wezla == 0) {
                continue;
            }

            wezel_indeksowy* element =
                pobierz_wezel(
                    wpisy[j].id_wezla
                );

            if (!element ||
                element->typ == TYP_WOLNY ||
                !poprawna_nazwa(
                    wpisy[j].nazwa)) {

                return false;
            }

            const char* tag =
                element->typ ==
                        TYP_KATALOG
                    ? "[KAT]  "
                    : "[PLIK] ";

            for (size_t t = 0;
                 tag[t] != '\0';
                 ++t) {

                if (pozycja + 1 >=
                    max_dlugosc) {

                    bufor[pozycja] =
                        '\0';

                    return false;
                }

                bufor[pozycja++] =
                    tag[t];
            }

            for (size_t n = 0;
                 wpisy[j].nazwa[n] != '\0';
                 ++n) {

                if (n >=
                    static_cast<size_t>(
                        PSF_MAX_NAZWA)) {

                    return false;
                }

                if (pozycja + 1 >=
                    max_dlugosc) {

                    bufor[pozycja] =
                        '\0';

                    return false;
                }

                bufor[pozycja++] =
                    wpisy[j].nazwa[n];
            }

            if (pozycja + 1 <
                max_dlugosc) {

                bufor[pozycja++] =
                    '\n';
            } else {
                bufor[pozycja] =
                    '\0';

                return false;
            }
        }
    }

    bufor[pozycja] = '\0';
    return true;
}

/* =========================================================================
 * PUBLICZNE API - USUWANIE
 * ========================================================================= */

static bool usun_twor_bez_blokady(
    const char* sciezka
) {

    if (!psf_gotowy ||
        !bezpieczna_sciezka(
            sciezka)) {

        return false;
    }

    /*
     * Korzenia nie mozna usunac.
     */
    if (sciezka[0] == '/' &&
        sciezka[1] == '\0') {

        return false;
    }

    char nazwa[
        PSF_MAX_NAZWA] = {};

    const uint64_t rodzic_id =
        rozwiaz_sciezke(
            sciezka,
            nazwa,
            true
        );

    if (rodzic_id == 0) {
        return false;
    }

    const ZnalezionyWpis znaleziony =
        znajdz_wpis_w_katalogu(
            rodzic_id,
            nazwa
        );

    if (!znaleziony.znaleziony ||
        znaleziony.id_wezla ==
            dysk_superblok->
                id_korzenia) {

        return false;
    }

    wezel_indeksowy* cel =
        pobierz_wezel(
            znaleziony.id_wezla
        );

    wezel_indeksowy* rodzic =
        pobierz_wezel(
            rodzic_id
        );

    if (!cel ||
        !rodzic ||
        rodzic->typ != TYP_KATALOG ||
        cel->typ == TYP_WOLNY) {

        return false;
    }

    if (cel->typ == TYP_KATALOG &&
        !katalog_pusty(
            znaleziony.id_wezla)) {

        /*
         * Brak rekurencyjnego usuwania. Stara wersja osierocala dzieci.
         */
        return false;
    }

    if (cel->typ == TYP_PLIK) {
        (void)zwolnij_wszystkie_bloki_wezla(cel, znaleziony.id_wezla);
    } else if (cel->typ == TYP_KATALOG) {
        if (cel->blok_posredni_1 != BARK_BLOKU ||
            cel->blok_posredni_2 != BARK_BLOKU ||
            cel->blok_posredni_3 != BARK_BLOKU) {
            return false;
        }

        for (size_t k = 0;
             k < static_cast<size_t>(PSF_MAX_BLOKOW_W_WEZLE);
             ++k) {
            const uint64_t id = cel->wskazniki_blokow[k];
            if (id == BARK_BLOKU) continue;
            if (!id_bloku_danych_poprawne(id) || !czy_blok_danych_zajety(id)) {
                return false;
            }
        }

        for (size_t k = 0;
             k < static_cast<size_t>(PSF_MAX_BLOKOW_W_WEZLE);
             ++k) {
            const uint64_t id = cel->wskazniki_blokow[k];
            if (id == BARK_BLOKU) continue;
            if (!zwolnij_blok_danych(id)) {
                return false;
            }
            cel->wskazniki_blokow[k] = BARK_BLOKU;
        }
    }

    wpis_katalogowy* wpisy =
        pobierz_wpisy_katalogowe(
            znaleziony.
                id_bloku_danych
        );

    if (!wpisy ||
        znaleziony.indeks_wpisu >=
            wpisow_katalogowych_na_blok()) {

        return false;
    }

    wpis_katalogowy& wpis =
        wpisy[
            znaleziony.
                indeks_wpisu];

    wpis.id_wezla = 0;

    wyzeruj_pamiec(
        wpis.nazwa,
        PSF_MAX_NAZWA
    );

    const uint64_t absolutny_wpisow =
        absolutny_blok_danych(
            znaleziony.
                id_bloku_danych
        );

    if (absolutny_wpisow !=
        UINT64_MAX) {

        oznacz_blok_jako_brudny(
            absolutny_wpisow
        );
    }

    if (rodzic->
        rozmiar_w_bajtach > 0) {

        rodzic->
            rozmiar_w_bajtach--;
    }

    oznacz_wezel_jako_brudny(
        rodzic_id
    );

    if (!zwolnij_wezel(
            znaleziony.id_wezla)) {

        return false;
    }

    (void)bsp_zapisz_zmiany();

    return true;
}

extern "C" bool usun_twor(const char* sciezka) {
    BlokadaPSF blokada;
    return usun_twor_bez_blokady(sciezka);
}

/* =========================================================================
 * PUBLICZNE API - ROZMIAR
 * ========================================================================= */

/*
 * Wolne miejsce na dane PSF w bajtach (bloki danych oznaczone w bitmapie jako wolne). Tylko odczyt: liczy bity
 * bitmapy blok po bloku (bez zmiany stanu, bez alokacji). Uzywane przez import muzyki z modulu Multiboot2, aby
 * nie zapelnic RAM-dysku zywego systemu (zostawia rezerwe dla ustawien i dokumentow). 0 = PSF niegotowy / blad.
 */
extern "C" uint64_t psf_wolne_bajty() {
    BlokadaPSF blokada;
    if (!psf_gotowy || !dysk_superblok || calkowita_liczba_blokow_danych == 0) return 0;
    const uint64_t n = calkowita_liczba_blokow_danych;
    const uint64_t bajty_bitmapy = (n + 7ULL) / 8ULL;
    uint64_t wolne = 0;
    for (uint64_t bajt = 0; bajt < bajty_bitmapy;) {
        const uint64_t blok = 1ULL + bajt / static_cast<uint64_t>(PSF_ROZMIAR_BLOKU);
        if (blok >= dysk_superblok->start_wezlow) return 0;
        const uint8_t* b = pobierz_blok(blok);
        if (!b) return 0;
        uint64_t koniec = bajt + (PSF_ROZMIAR_BLOKU - bajt % PSF_ROZMIAR_BLOKU);
        if (koniec > bajty_bitmapy) koniec = bajty_bitmapy;
        for (; bajt < koniec; ++bajt) {
            const uint8_t v = b[bajt % PSF_ROZMIAR_BLOKU];
            for (uint32_t k = 0; k < 8; ++k) {
                if (bajt * 8ULL + k >= n) break;
                if (!(v & (1U << k))) ++wolne;
            }
        }
    }
    return wolne * static_cast<uint64_t>(PSF_ROZMIAR_BLOKU);
}

extern "C" uint32_t rozmiar_pliku(
    const char* sciezka
) {
    BlokadaPSF blokada;

    if (!psf_gotowy ||
        !sciezka) {

        return 0;
    }

    const uint64_t id =
        rozwiaz_sciezke(
            sciezka,
            nullptr,
            false
        );

    if (id == 0) {
        return 0;
    }

    wezel_indeksowy* w =
        pobierz_wezel(id);

    if (!w ||
        w->typ != TYP_PLIK) {

        return 0;
    }

    if (w->rozmiar_w_bajtach >
        UINT32_MAX) {

        /*
         * Publiczne API ma 32-bitowy wynik.
         */
        return UINT32_MAX;
    }

    return static_cast<uint32_t>(
        w->rozmiar_w_bajtach
    );
}

static bool czy_katalog_istnieje_wewn(const char* sciezka) {
    if (!psf_gotowy || !sciezka)
        return false;

    uint64_t id =
        rozwiaz_sciezke(
            sciezka,
            nullptr,
            false
        );

    if (id == 0)
        return false;

    wezel_indeksowy* w =
        pobierz_wezel(id);

    return
        w != nullptr &&
        w->typ == TYP_KATALOG;
}

extern "C" bool psf_przygotuj_katalog_startowy(const char* sciezka,PsfStanStartu* stan) {
    BlokadaPSF blokada;
    if(!stan)return false;
    *stan=PsfStanStartu{};
    ZakresStanuStartu zakres(stan);
    stan->gotowy=psf_gotowy;
    stan->trwaly=nosnik_blokowy_dostepny;
    stan->backend=stan->trwaly && bsp_partycja.device?bsp_partycja.device->device_type:BlockDeviceType::Ramdisk;
    stan->korzen=dysk_superblok?dysk_superblok->id_korzenia:0;
    if(!psf_gotowy){blad_startu(PsfBladStartu::NieGotowy);return false;}
    if(!bezpieczna_sciezka(sciezka)){blad_startu(PsfBladStartu::ZlaSciezka);return false;}
    auto* korzen=pobierz_wezel(stan->korzen);
    if(!korzen || korzen->typ!=TYP_KATALOG ||
       korzen->rozmiar_w_bajtach>PSF_MAX_BLOKOW_W_WEZLE*wpisow_katalogowych_na_blok()) {
        blad_startu(PsfBladStartu::ZlyKorzen);return false;
    }
    for(size_t i=0;i<PSF_MAX_BLOKOW_W_WEZLE;++i)if(korzen->wskazniki_blokow[i]!=BARK_BLOKU &&
        !id_bloku_danych_poprawne(korzen->wskazniki_blokow[i])){blad_startu(PsfBladStartu::ZlyKorzen);return false;}
#if defined(BURSZTYNDIAGPSFBOOTSTRAP) && BURSZTYNDIAGPSFBOOTSTRAP
    char translacja[MAKS_DLUGOSC_SCIEZKI]{};
    const char* fizyczna=psf_przetlumacz_sciezke_slotu(sciezka,translacja);
    size_t n=0;
    while(fizyczna && n+1<sizeof(stan->sciezkaFizyczna) && fizyczna[n]){stan->sciezkaFizyczna[n]=fizyczna[n];++n;}
    // Counts are not maintained globally; scan only in opt-in bootstrap diagnostics.
    for(uint64_t i=1;i<=calkowita_liczba_wezlow;++i){auto* w=pobierz_wezel(i);if(w && w->typ==TYP_WOLNY)++stan->wolneWezly;}
    for(uint64_t i=0;i<calkowita_liczba_blokow_danych;++i)if(!czy_blok_danych_zajety(i))++stan->wolneBloki;
#endif
    char nazwa[PSF_MAX_NAZWA]{};
    stan->rodzic=rozwiaz_sciezke(sciezka,nazwa,true);
    auto* rodzic=pobierz_wezel(stan->rodzic);
    if(rodzic)stan->wpisyRodzica=rodzic->rozmiar_w_bajtach;
    stan->wezel=rozwiaz_sciezke(sciezka,nullptr,false);
    stan->istnial=stan->wezel!=0;
    stan->lookup=stan->istnial?PsfEtapStartu::Ok:PsfEtapStartu::Blad;
    if(stan->blad!=PsfBladStartu::Ok)return false;
    if(stan->istnial) {
        auto* w=pobierz_wezel(stan->wezel);
        if(w && w->typ==TYP_KATALOG)return true;
        blad_startu(PsfBladStartu::NieKatalog);return false;
    }
    if(!rodzic){blad_startu(PsfBladStartu::BrakRodzica);return false;}
    stan->tworzenie=PsfEtapStartu::Blad;
    if(!utworz_twor(sciezka,TYP_KATALOG,DOMYSLNY_PZB_PLIKU)) {
        if(stan->blad==PsfBladStartu::Ok)blad_startu(PsfBladStartu::Odczyt);
        return false;
    }
    stan->tworzenie=PsfEtapStartu::Ok;
    // Generic CREATE commits RAM even on I/O failure. Boot reports it precisely.
    if(stan->blad!=PsfBladStartu::Ok)return false;
    if(!czy_katalog_istnieje_wewn(sciezka)){blad_startu(PsfBladStartu::Odczyt);return false;}
    return true;
}

extern "C" bool czy_katalog_istnieje(const char* sciezka) {
    BlokadaPSF blokada;
    return czy_katalog_istnieje_wewn(sciezka);
}

namespace { bool sciezka_usuwania(const char*,char*); }
extern "C" WynikPlikowy psf_utworz_wynik(const char* path,bool folder,uint8_t pzb) {
    BlokadaPSF lock;
    char canonical[512]{};
    if(!sciezka_usuwania(path,canonical) || canonical[1]==0)return WynikPlikowy::NieprawidlowaNazwa;
    PsfStanStartu status{};ZakresStanuStartu scope(&status);
    const bool ok=utworz_twor(canonical,folder?TYP_KATALOG:TYP_PLIK,pzb);
    switch(status.blad) {
    case PsfBladStartu::Ok:return ok?WynikPlikowy::Ok:WynikPlikowy::BladIO;
    case PsfBladStartu::JuzIstnieje:return WynikPlikowy::Istnieje;
    case PsfBladStartu::ZlaSciezka:return WynikPlikowy::NieprawidlowaNazwa;
    case PsfBladStartu::BrakRodzica:return WynikPlikowy::Brak;
    case PsfBladStartu::BrakWezlow:case PsfBladStartu::BrakBlokow:
    case PsfBladStartu::LimitKatalogu:return WynikPlikowy::BrakMiejsca;
    default:return WynikPlikowy::BladIO;
    }
}

extern "C" bool zapewnij_katalog(const char* sciezka) {
    BlokadaPSF blokada;
    if (czy_katalog_istnieje_wewn(sciezka))
        return true;

    return utworz_twor(sciezka, TYP_KATALOG, DOMYSLNY_PZB_PLIKU);
}


/* =========================================================================
 * PUBLICZNE API - ZMIANA NAZWY
 * ========================================================================= */

extern "C" bool zmien_nazwe_tworu(
    const char* sciezka,
    const char* nowa_nazwa
) {
    BlokadaPSF blokada;

    if (!psf_gotowy ||
        !bezpieczna_sciezka(
            sciezka) ||
        !poprawna_nazwa(
            nowa_nazwa)) {

        return false;
    }

    if (sciezka[0] == '/' &&
        sciezka[1] == '\0') {

        return false;
    }

    char stara_nazwa[
        PSF_MAX_NAZWA] = {};

    const uint64_t rodzic_id =
        rozwiaz_sciezke(
            sciezka,
            stara_nazwa,
            true
        );

    if (rodzic_id == 0) {
        return false;
    }

    if (nazwy_rowne(
            stara_nazwa,
            nowa_nazwa)) {

        return
            znajdz_wpis_w_katalogu(
                rodzic_id,
                stara_nazwa
            ).znaleziony;
    }

    if (znajdz_wpis_w_katalogu(
            rodzic_id,
            nowa_nazwa).znaleziony) {

        /*
         * Nie nadpisujemy istniejacego wpisu.
         */
        return false;
    }

    const ZnalezionyWpis znaleziony =
        znajdz_wpis_w_katalogu(
            rodzic_id,
            stara_nazwa
        );

    if (!znaleziony.znaleziony) {
        return false;
    }

    wpis_katalogowy* wpisy =
        pobierz_wpisy_katalogowe(
            znaleziony.
                id_bloku_danych
        );

    if (!wpisy ||
        znaleziony.indeks_wpisu >=
            wpisow_katalogowych_na_blok()) {

        return false;
    }

    wpis_katalogowy& wpis =
        wpisy[
            znaleziony.
                indeks_wpisu];

    if (!kopiuj_nazwe(
            wpis.nazwa,
            nowa_nazwa)) {

        return false;
    }

    const uint64_t absolutny =
        absolutny_blok_danych(
            znaleziony.
                id_bloku_danych
        );

    if (absolutny != UINT64_MAX) {
        oznacz_blok_jako_brudny(
            absolutny
        );
    }

    (void)bsp_zapisz_zmiany();

    return true;
}

/* =========================================================================
 * PUBLICZNE API - METADANE I PRZENOSZENIE
 * ========================================================================= */

static bool metadane_wezla(const wezel_indeksowy* w, psf_metadane* metadane) {
    metadane->rozmiar = w->rozmiar_w_bajtach;
    metadane->czas_utworzenia_rtc = 0;
    metadane->typ = w->typ;
    metadane->poziom_pzb = 0;
    metadane->czas_dostepny = 0;
    metadane->pzb_dostepny = 0;

    if (!znacznik_metadanych_poprawny(w->flagi_zabezpieczen))
        return true;

    if ((w->flagi_zabezpieczen & PSF_META_CZAS_WAZNY) != 0 &&
        w->czas_utworzenia != 0) {
        metadane->czas_utworzenia_rtc = w->czas_utworzenia;
        metadane->czas_dostepny = 1;
    }

    const uint8_t pzb = static_cast<uint8_t>(
        w->flagi_zabezpieczen & PSF_META_PZB_MASK);
    if ((w->flagi_zabezpieczen & PSF_META_PZB_WAZNY) != 0 && pzb <= 5U) {
        metadane->poziom_pzb = pzb;
        metadane->pzb_dostepny = 1;
    }
    return true;
}

extern "C" bool pobierz_metadane_tworu(
    const char* sciezka,
    psf_metadane* metadane
) {
    BlokadaPSF blokada;
    if (!psf_gotowy || !bezpieczna_sciezka(sciezka) || !metadane)
        return false;

    const uint64_t id = rozwiaz_sciezke(sciezka, nullptr, false);
    wezel_indeksowy* w = pobierz_wezel(id);
    if (id == 0 || !w || w->typ == TYP_WOLNY) return false;

    return metadane_wezla(w, metadane);
}

#ifdef BURSZTYN_PSF_HOST_TEST
static unsigned move_failure_stage = 0;
extern "C" void psf_test_move_failure(unsigned stage) { move_failure_stage=stage; }
extern "C" bool psf_test_lock_held() { return __atomic_load_n(&blokada_psf,__ATOMIC_RELAXED)!=0; }
#endif

extern "C" PsfMoveWynik psf_przenies_twor_wynik(const char* sciezka,const char* folder_docelowy) {
    BlokadaPSF blokada;
    if(!psf_gotowy || !bezpieczna_sciezka(sciezka) || !bezpieczna_sciezka(folder_docelowy))return PSF_MOVE_BAD_PATH;
    char nazwa[PSF_MAX_NAZWA]{};
    const uint64_t cel_id=rozwiaz_sciezke(sciezka,nullptr,false);
    if(!cel_id)return PSF_MOVE_MISSING_SOURCE;
    if(cel_id==dysk_superblok->id_korzenia)return PSF_MOVE_ROOT;
    const uint64_t stary_id=rozwiaz_sciezke(sciezka,nazwa,true);
    const uint64_t nowy_id=rozwiaz_sciezke(folder_docelowy,nullptr,false);
    auto* cel=pobierz_wezel(cel_id);
    auto* stary=pobierz_wezel(stary_id);
    auto* nowy=pobierz_wezel(nowy_id);
    if(!stary || !nowy || nowy->typ!=TYP_KATALOG || stary->typ!=TYP_KATALOG || !cel || !poprawna_nazwa(nazwa))
        return PSF_MOVE_BAD_DESTINATION;
    if(cel->typ!=TYP_PLIK && cel->typ!=TYP_KATALOG)return PSF_MOVE_MISSING_SOURCE;
    // Walk resolved destination inodes, not textual prefixes (slot aliases can differ).
    if(cel->typ==TYP_KATALOG) {
        // Chronione przez BlokadaPSF blokada (brak reentrancji, oszczednosc 512 B stosu Ring 0).
        static char translation[MAKS_DLUGOSC_SCIEZKI];
        const char* destination=psf_przetlumacz_sciezke_slotu(folder_docelowy,translation);
        if (!destination) return PSF_MOVE_BAD_DESTINATION;
        uint64_t ancestor=dysk_superblok->id_korzenia;size_t pos=1;
        while(destination[pos]) {
            char segment[PSF_MAX_NAZWA]{};bool last=false;
            if(!pobierz_segment(destination,&pos,segment,&last))return PSF_MOVE_BAD_DESTINATION;
            auto step=znajdz_wpis_w_katalogu(ancestor,segment);
            if(!step.znaleziony)return PSF_MOVE_BAD_DESTINATION;
            ancestor=step.id_wezla;
            if(ancestor==cel_id)return PSF_MOVE_SELF_DESCENDANT;
        }
    }
    const auto old_slot=znajdz_wpis_w_katalogu(stary_id,nazwa);
    if(!old_slot.znaleziony || old_slot.id_wezla!=cel_id)return PSF_MOVE_MISSING_SOURCE;
    // Confirming the current parent is a safe no-op, not a spurious error.
    if(stary_id==nowy_id)return PSF_MOVE_OK;
    if(znajdz_wpis_w_katalogu(nowy_id,nazwa).znaleziony)return PSF_MOVE_COLLISION;
    auto* old_entries=pobierz_wpisy_katalogowe(old_slot.id_bloku_danych);
    if(!old_entries || old_slot.indeks_wpisu>=wpisow_katalogowych_na_blok())return PSF_MOVE_PREPARE_FAILED;
    const wpis_katalogowy saved_entry=old_entries[old_slot.indeks_wpisu];
    const wezel_indeksowy saved_old=*stary,saved_new=*nowy;
#ifdef BURSZTYN_PSF_HOST_TEST
    if(move_failure_stage==1)return PSF_MOVE_PREPARE_FAILED;
#endif
    if(!dodaj_wpis_do_katalogu(nowy_id,cel_id,nazwa))return PSF_MOVE_PREPARE_FAILED;
    const auto new_slot=znajdz_wpis_w_katalogu(nowy_id,nazwa);
    auto rollback=[&]() {
        if(new_slot.znaleziony) {
            auto* entries=pobierz_wpisy_katalogowe(new_slot.id_bloku_danych);
            if(entries) {
                entries[new_slot.indeks_wpisu]={};
                oznacz_blok_jako_brudny(absolutny_blok_danych(new_slot.id_bloku_danych));
            }
        }
        // Return blocks allocated exclusively to prepare this directory entry.
        for(size_t i=0;i<PSF_MAX_BLOKOW_W_WEZLE;++i)
            if(saved_new.wskazniki_blokow[i]==BARK_BLOKU && nowy->wskazniki_blokow[i]!=BARK_BLOKU)
                (void)zwolnij_blok_danych(nowy->wskazniki_blokow[i]);
        *nowy=saved_new;*stary=saved_old;
        old_entries[old_slot.indeks_wpisu]=saved_entry;
        oznacz_wezel_jako_brudny(stary_id);oznacz_wezel_jako_brudny(nowy_id);
        oznacz_blok_jako_brudny(absolutny_blok_danych(old_slot.id_bloku_danych));
    };
    if(!new_slot.znaleziony) {rollback();return PSF_MOVE_PREPARE_FAILED;}
#ifdef BURSZTYN_PSF_HOST_TEST
    if(move_failure_stage==2) {rollback();return PSF_MOVE_PREPARE_FAILED;}
#endif
    old_entries[old_slot.indeks_wpisu]={};
    if(stary->rozmiar_w_bajtach)--stary->rozmiar_w_bajtach;
    oznacz_wezel_jako_brudny(stary_id);
    oznacz_blok_jako_brudny(absolutny_blok_danych(old_slot.id_bloku_danych));
#ifdef BURSZTYN_PSF_HOST_TEST
    if(move_failure_stage==3) {rollback();return PSF_MOVE_COMMIT_FAILED;}
#endif
    if(!bsp_zapisz_zmiany()) {
        rollback();
        // Best effort restore on media too. Dirty cache remains if I/O is still failing.
        (void)bsp_zapisz_zmiany();
        return PSF_MOVE_COMMIT_FAILED;
    }
    return PSF_MOVE_OK;
}

extern "C" bool przenies_twor(const char* sciezka,const char* folder_docelowy) {
    return psf_przenies_twor_wynik(sciezka,folder_docelowy)==PSF_MOVE_OK;
}

#include "psf_usuwanie.inc"

extern "C" bool kopiuj_twor_z_pzb(
    const char* sciezka,
    const char* folder_docelowy,
    uint8_t poziom_pzb
) {
    BlokadaPSF blokada;
    if (!psf_gotowy || poziom_pzb > 5U || !bezpieczna_sciezka(sciezka) ||
        !bezpieczna_sciezka(folder_docelowy) ||
        (sciezka[0] == '/' && sciezka[1] == '\0')) return false;

    char nazwa[PSF_MAX_NAZWA] = {};
    const uint64_t zrodlo_id = rozwiaz_sciezke(sciezka, nullptr, false);
    const uint64_t zrodlo_rodzic = rozwiaz_sciezke(sciezka, nazwa, true);
    const uint64_t cel_rodzic = rozwiaz_sciezke(folder_docelowy, nullptr, false);
    wezel_indeksowy* zrodlo = pobierz_wezel(zrodlo_id);
    wezel_indeksowy* cel = pobierz_wezel(cel_rodzic);
    if (zrodlo_id == 0 || zrodlo_rodzic == 0 || cel_rodzic == 0 ||
        !zrodlo || !cel || cel->typ != TYP_KATALOG || !poprawna_nazwa(nazwa) ||
        znajdz_wpis_w_katalogu(cel_rodzic, nazwa).znaleziony) return false;

    if (zrodlo->typ == TYP_KATALOG) {
        size_t i = 0;
        while (sciezka[i] != '\0' && folder_docelowy[i] == sciezka[i]) ++i;
        if (sciezka[i] == '\0' &&
            (folder_docelowy[i] == '\0' || folder_docelowy[i] == '/'))
            return false;
    }

    uint64_t przodkowie[PSF_MAKS_GLEBOKOSC_KOPII] = {};
    const uint64_t kopia_id = sklonuj_wezel_rekurencyjnie(
        zrodlo_id, poziom_pzb, 0, przodkowie);
    if (kopia_id == 0) return false;
    if (!dodaj_wpis_do_katalogu(cel_rodzic, kopia_id, nazwa)) {
        (void)zwolnij_nieopublikowana_kopie(kopia_id, 0);
        return false;
    }
    (void)bsp_zapisz_zmiany();
    return true;
}

extern "C" bool psf_flush_cache() {
    BlokadaPSF blokada;
    if (!psf_gotowy) {
        return false;
    }
    return bsp_zapisz_zmiany();
}
