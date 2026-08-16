/*
 * Bursztyn OS - Skladacz Obrazu
 *
 * Prosty kompozytor warstw Ring 3.
 *
 * Kontrakt:
 *
 *   - tablica_warstw[pid] przechowuje najwyzej jedna warstwe procesu,
 *   - bufor warstwy ma format 32-bit 0x00RRGGBB,
 *   - wartosc DOKLADNIE 0x00000000 oznacza przezroczystosc,
 *   - grafika.cpp moze uzyc gornego bajtu jako technicznego znacznika,
 *     aby narysowac wizualnie czarny piksel; kompozytor usuwa ten znacznik
 *     przed zapisem do backbufferu,
 *   - nizszy z_order jest skladany wczesniej,
 *   - przy jednakowym z_order stabilnym tie-breakerem jest PID,
 *   - zegar kernela jest rysowany nad warstwami, a kursor przez
 *     grafika_zakoncz_skladanie() nad gotowa klatka.
 *
 * Bezpieczenstwo:
 *
 *   - rozmiary powierzchni sa sprawdzane przed mnozeniem/alokacja,
 *   - pojedyncza warstwa i laczna pamiec warstw maja limity,
 *   - rozmiar bufora jest sledzony osobno od metadanych warstwy,
 *     co pozwala wykryc uszkodzone szerokosc/wysokosc przed odczytem,
 *   - tworzenie nowej warstwy nie niszczy starej, jezeli kmalloc() zawiedzie,
 *   - clipping uzywa int64_t, wiec x + szerokosc / y + wysokosc nie powoduje
 *     signed-overflow dla skrajnych wspolrzednych int,
 *   - aktywna warstwa jest publikowana dopiero po pelnej inicjalizacji,
 *   - kompozytor ma lekki guard przeciw reentrantnemu skladaniu.
 *
 * Synchronizacja:
 *
 * Aktualne BWS serializuje operacje GUI zewnetrzna blokada ekranu.
 * pobierz_warstwe() zwraca surowy wskaznik, wiec pelne SMP-safe lifetime
 * wymagaloby zmiany API (refcount/snapshot/lock trzymany przez czytelnika).
 * Ten plik wzmacnia publikacje acquire/release, ale nie udaje, ze samo to
 * rozwiazuje przyszly wielordzeniowy lifetime powierzchni.
 */

#include "skladacz_obrazu.h"
#include "heap.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * 1. STALE I GLOBALNY STAN
 * ========================================================================= */

namespace {

constexpr int MAKS_WARSTW =
    16;

/*
 * Obecny heap kernela ma 16 MiB. Nie pozwalamy powierzchniom GUI zjesc
 * calej sterty, bo scheduler, loader, siec i system plikow rowniez
 * potrzebuja kmalloc().
 *
 * 12 MiB pozwala np. na kilka warstw 1024x768, pozostawiajac zapas
 * dla reszty jadra.
 *
 * Po przejsciu na dedykowany allocator powierzchni / wiekszy heap limit
 * powinien zostac przeniesiony do konfiguracji pamieci.
 */
constexpr uint64_t MAKS_PAMIEC_WARSTW =
    12ULL * 1024ULL * 1024ULL;

/*
 * Maksymalnie 8 MiB na pojedyncza powierzchnie.
 * 1920x1080x4 = 8 294 400 B, czyli nadal miesci sie w tym limicie.
 */
constexpr uint64_t MAKS_PAMIEC_JEDNEJ_WARSTWY =
    8ULL * 1024ULL * 1024ULL;

/*
 * Absolutny bezpiecznik metadanych. Faktyczny rozmiar warstwy jest dodatkowo
 * ograniczony do aktualnego rozmiaru ekranu.
 */
constexpr int MAKS_WYMIAR_WARSTWY =
    8192;

/*
 * Warstwy z absurdalnym z_order sa odrzucane.
 * To nie jest jeszcze pelny model focus/raise-to-front - ten powinien byc
 * zarzadzany przez menedzer okien/kernel, nie dowolna aplikacje.
 */
constexpr int MIN_Z_ORDER =
    -32768;

constexpr int MAX_Z_ORDER =
    32767;

/*
 * Dokladna liczba bajtow zaalokowana dla danego PID.
 *
 * Nie wyliczamy jej przy zwalnianiu z warstwa.szerokosc/wysokosc, bo
 * uszkodzone metadane moglyby doprowadzic do blednego accounting albo OOB.
 */
uint64_t rozmiar_alokacji_warstwy[
    MAKS_WARSTW
] = {};

/*
 * Steady-state budzet powierzchni.
 * Atomiki przygotowuja accounting pod przyszle wielordzeniowe wywolania.
 */
uint64_t zajete_bajty_warstw =
    0;

/*
 * Guard chroni przed zagniezdzonym / rownoleglym rozpoczeciem skladania
 * tej samej klatki. Nie jest zamiennikiem lifetime-locka dla warstw.
 */
bool skladanie_w_toku =
    false;
bool compositor_dirty = true;

bool pid_poprawny(
    int pid
) {
    return
        pid >= 0 &&
        pid < MAKS_WARSTW;
}

bool z_order_poprawny(
    int z_order
) {
    return
        z_order >= MIN_Z_ORDER &&
        z_order <= MAX_Z_ORDER;
}

bool oblicz_rozmiar_powierzchni(
    int szer,
    int wys,
    uint64_t* liczba_pikseli,
    uint64_t* liczba_bajtow
) {
    if (!liczba_pikseli ||
        !liczba_bajtow) {

        return false;
    }

    if (szer <= 0 ||
        wys <= 0 ||
        szer > MAKS_WYMIAR_WARSTWY ||
        wys > MAKS_WYMIAR_WARSTWY) {

        return false;
    }

    const uint64_t w =
        static_cast<uint64_t>(
            static_cast<uint32_t>(
                szer
            )
        );

    const uint64_t h =
        static_cast<uint64_t>(
            static_cast<uint32_t>(
                wys
            )
        );

    if (w >
        UINT64_MAX / h) {

        return false;
    }

    const uint64_t piksele =
        w * h;

    if (piksele >
        UINT64_MAX /
            sizeof(uint32_t)) {

        return false;
    }

    const uint64_t bajty =
        piksele *
        sizeof(uint32_t);

    if (bajty >
            static_cast<uint64_t>(
                SIZE_MAX) ||
        bajty >
            MAKS_PAMIEC_JEDNEJ_WARSTWY) {

        return false;
    }

    *liczba_pikseli =
        piksele;

    *liczba_bajtow =
        bajty;

    return true;
}

bool wymiary_mieszcza_sie_na_ekranie(
    int szer,
    int wys
);

/* =========================================================================
 * 2. ACCOUNTING PAMIECI WARSTW
 * ========================================================================= */

bool zarezerwuj_dodatkowe_bajty(
    uint64_t delta
) {
    if (delta == 0) {
        return true;
    }

    uint64_t stare =
        __atomic_load_n(
            &zajete_bajty_warstw,
            __ATOMIC_RELAXED
        );

    for (;;) {
        if (stare >
            MAKS_PAMIEC_WARSTW) {

            return false;
        }

        if (delta >
            MAKS_PAMIEC_WARSTW -
                stare) {

            return false;
        }

        const uint64_t nowe =
            stare + delta;

        if (__atomic_compare_exchange_n(
                &zajete_bajty_warstw,
                &stare,
                nowe,
                false,
                __ATOMIC_ACQ_REL,
                __ATOMIC_RELAXED)) {

            return true;
        }

        /*
         * Przy nieudanym CAS "stare" zostaje uzupelnione aktualna wartoscia.
         */
    }
}

void oddaj_bajty(
    uint64_t bajty
) {
    if (bajty == 0) {
        return;
    }

    const uint64_t stare =
        __atomic_fetch_sub(
            &zajete_bajty_warstw,
            bajty,
            __ATOMIC_ACQ_REL
        );

    /*
     * Fail-safe accounting. Przy logicznym underflow nie zostawiamy
     * ogromnej wartosci UINT64_MAX, ktora zablokowalaby wszystkie
     * przyszle warstwy.
     *
     * Nie zwalnia to ani nie naprawia pamieci; jedynie ogranicza skutki
     * uszkodzenia licznika.
     */
    if (stare < bajty) {
        __atomic_store_n(
            &zajete_bajty_warstw,
            0ULL,
            __ATOMIC_RELEASE
        );
    }
}

/* =========================================================================
 * 3. WALIDACJA METADANYCH WARSTWY
 * ========================================================================= */

bool warstwa_ma_spojny_bufor(
    int indeks
) {
    if (!pid_poprawny(
            indeks)) {

        return false;
    }

    const warstwa_obrazu& w =
        tablica_warstw[indeks];

    if (!__atomic_load_n(
            &w.aktywna,
            __ATOMIC_ACQUIRE)) {

        return false;
    }

    if (!w.bufor_pikseli ||
        w.szerokosc <= 0 ||
        w.wysokosc <= 0) {

        return false;
    }

    uint64_t piksele = 0;
    uint64_t bajty = 0;

    if (!oblicz_rozmiar_powierzchni(
            w.szerokosc,
            w.wysokosc,
            &piksele,
            &bajty)) {

        return false;
    }

    (void)piksele;

    /*
     * Najwazniejsza kontrola OOB: metadane musza opisywac DOKLADNIE
     * rozmiar rzeczywistej alokacji tego slotu.
     */
    return
        bajty ==
        __atomic_load_n(
            &rozmiar_alokacji_warstwy[indeks],
            __ATOMIC_ACQUIRE
        );
}

/* =========================================================================
 * 4. GUARD SKLADANIA
 * ========================================================================= */

bool rozpocznij_guard_skladania() {
    return
        !__atomic_test_and_set(
            &skladanie_w_toku,
            __ATOMIC_ACQUIRE
        );
}

void zakoncz_guard_skladania() {
    __atomic_clear(
        &skladanie_w_toku,
        __ATOMIC_RELEASE
    );
}

class GuardSkladania {
public:
    GuardSkladania()
        : aktywny_(
              rozpocznij_guard_skladania()
          ) {
    }

    ~GuardSkladania() {
        if (aktywny_) {
            zakoncz_guard_skladania();
        }
    }

    bool aktywny() const {
        return aktywny_;
    }

    GuardSkladania(
        const GuardSkladania&
    ) = delete;

    GuardSkladania& operator=(
        const GuardSkladania&
    ) = delete;

private:
    bool aktywny_;
};

/* =========================================================================
 * 5. CLIPPING
 * ========================================================================= */

int64_t max_i64(
    int64_t a,
    int64_t b
) {
    return
        a > b
            ? a
            : b;
}

int64_t min_i64(
    int64_t a,
    int64_t b
) {
    return
        a < b
            ? a
            : b;
}

struct ProstokatWidoczny {
    int src_x;
    int src_y;

    int dst_x;
    int dst_y;

    int szer;
    int wys;
};

bool wyznacz_widoczny_fragment(
    const warstwa_obrazu& w,
    int ekran_szer,
    int ekran_wys,
    ProstokatWidoczny* wynik
) {
    if (!wynik ||
        ekran_szer <= 0 ||
        ekran_wys <= 0 ||
        w.szerokosc <= 0 ||
        w.wysokosc <= 0) {

        return false;
    }

    /*
     * int64_t eliminuje UB typu:
     *
     *   INT_MAX + szerokosc
     */
    const int64_t lewo =
        static_cast<int64_t>(
            w.x
        );

    const int64_t gora =
        static_cast<int64_t>(
            w.y
        );

    const int64_t prawo =
        lewo +
        static_cast<int64_t>(
            w.szerokosc
        );

    const int64_t dol =
        gora +
        static_cast<int64_t>(
            w.wysokosc
        );

    const int64_t clip_lewo =
        max_i64(
            lewo,
            0
        );

    const int64_t clip_gora =
        max_i64(
            gora,
            0
        );

    const int64_t clip_prawo =
        min_i64(
            prawo,
            static_cast<int64_t>(
                ekran_szer
            )
        );

    const int64_t clip_dol =
        min_i64(
            dol,
            static_cast<int64_t>(
                ekran_wys
            )
        );

    if (clip_lewo >=
            clip_prawo ||
        clip_gora >=
            clip_dol) {

        return false;
    }

    const int64_t src_x =
        clip_lewo -
        lewo;

    const int64_t src_y =
        clip_gora -
        gora;

    const int64_t szer =
        clip_prawo -
        clip_lewo;

    const int64_t wys =
        clip_dol -
        clip_gora;

    /*
     * Wszystkie wartosci sa ograniczone rozmiarem ekranu/warstwy, wiec
     * po poprzedniej walidacji bezpiecznie mieszcza sie w int.
     */
    wynik->src_x =
        static_cast<int>(
            src_x
        );

    wynik->src_y =
        static_cast<int>(
            src_y
        );

    wynik->dst_x =
        static_cast<int>(
            clip_lewo
        );

    wynik->dst_y =
        static_cast<int>(
            clip_gora
        );

    wynik->szer =
        static_cast<int>(
            szer
        );

    wynik->wys =
        static_cast<int>(
            wys
        );

    return
        wynik->szer > 0 &&
        wynik->wys > 0;
}

/* =========================================================================
 * 6. KOLEJNOSC Z
 * ========================================================================= */

bool warstwa_przed(
    int a,
    int b
) {
    if (!pid_poprawny(a)) {
        return false;
    }

    if (!pid_poprawny(b)) {
        return true;
    }

    const warstwa_obrazu& wa =
        tablica_warstw[a];

    const warstwa_obrazu& wb =
        tablica_warstw[b];

    if (wa.z_order <
        wb.z_order) {

        return true;
    }

    if (wa.z_order >
        wb.z_order) {

        return false;
    }

    /*
     * Stabilny tie-breaker.
     */
    return
        a < b;
}

} // namespace

/* =========================================================================
 * 7. GLOBALNA TABLICA WARSTW
 * ========================================================================= */

warstwa_obrazu tablica_warstw[
    16
] = {};

/* =========================================================================
 * 8. SUROWE API GRAFIKI
 * ========================================================================= */

/*
 * Operacje te omijaja przekierowanie PostawPiksel() do aktywnej warstwy.
 */
extern void grafika_rozpocznij_skladanie();
extern void grafika_odtworz_tlo_skladania();
extern void grafika_zapisz_surowy_piksel(
    int x,
    int y,
    uint32_t kolor
);
extern void grafika_zakoncz_skladanie();
extern "C" void grafika_naloz_okno_terminala();

extern int grafika_pobierz_szerokosc();
extern int grafika_pobierz_wysokosc();

extern void rysuj_zegar_rtc();

namespace {

bool wymiary_mieszcza_sie_na_ekranie(
    int szer,
    int wys
) {
    const int ekran_szer =
        grafika_pobierz_szerokosc();

    const int ekran_wys =
        grafika_pobierz_wysokosc();

    if (ekran_szer <= 0 ||
        ekran_wys <= 0) {

        return false;
    }

    /*
     * Powierzchnia wieksza od fizycznego ekranu nie wnosi obecnie wartosci,
     * a moze bardzo latwo wyczerpac 16-MiB heap kernela.
     *
     * Po wprowadzeniu scrollowalnych/offscreen surfaces polityke mozna
     * rozszerzyc razem z dedykowanym allocatorem grafiki.
     */
    return
        szer <= ekran_szer &&
        wys <= ekran_wys;
}

void wyczysc_bufor_pikseli(
    uint32_t* bufor,
    uint64_t liczba_pikseli
) {
    if (!bufor) {
        return;
    }

    for (uint64_t i = 0;
         i < liczba_pikseli;
         ++i) {

        bufor[i] =
            0x00000000U;
    }
}

} // namespace

/* =========================================================================
 * 9. DOSTEP DO WARSTWY
 * ========================================================================= */

warstwa_obrazu* pobierz_warstwe(
    int pid
) {
    if (!pid_poprawny(
            pid)) {

        return nullptr;
    }

    warstwa_obrazu& warstwa =
        tablica_warstw[pid];

    /*
     * Acquire paruje sie z release przy publikacji aktywnej warstwy.
     */
    if (!__atomic_load_n(
            &warstwa.aktywna,
            __ATOMIC_ACQUIRE)) {

        return nullptr;
    }

    /*
     * Nie zwracamy uszkodzonej powierzchni do grafika.cpp.
     */
    if (!warstwa_ma_spojny_bufor(
            pid)) {

        return nullptr;
    }

    return &warstwa;
}

/* =========================================================================
 * 10. TWORZENIE WARSTWY
 * ========================================================================= */

int utworz_warstwe(
    int pid,
    int x,
    int y,
    int szer,
    int wys,
    int z_order
) {
    if (!pid_poprawny(
            pid) ||
        !z_order_poprawny(
            z_order)) {

        return -1;
    }

    if (!wymiary_mieszcza_sie_na_ekranie(
            szer,
            wys)) {

        return -1;
    }

    uint64_t liczba_pikseli = 0;
    uint64_t nowe_bajty = 0;

    if (!oblicz_rozmiar_powierzchni(
            szer,
            wys,
            &liczba_pikseli,
            &nowe_bajty)) {

        return -1;
    }

    warstwa_obrazu& warstwa =
        tablica_warstw[pid];

    const uint64_t stare_bajty =
        __atomic_load_n(
            &rozmiar_alokacji_warstwy[pid],
            __ATOMIC_ACQUIRE
        );

    uint32_t* stary_bufor =
        warstwa.bufor_pikseli;

    /*
     * Najczestszy przypadek przy ponownym tworzeniu tej samej powierzchni:
     * wykorzystujemy istniejaca alokacje zamiast wymagac chwilowo dwoch
     * duzych buforow z malego heapu.
     */
    if (stary_bufor &&
        stare_bajty ==
            nowe_bajty &&
        warstwa.szerokosc ==
            szer &&
        warstwa.wysokosc ==
            wys) {

        __atomic_store_n(
            &warstwa.aktywna,
            false,
            __ATOMIC_RELEASE
        );

        wyczysc_bufor_pikseli(
            stary_bufor,
            liczba_pikseli
        );

        warstwa.pid =
            pid;

        warstwa.z_order =
            z_order;

        warstwa.x =
            x;

        warstwa.y =
            y;

        warstwa.szerokosc =
            szer;

        warstwa.wysokosc =
            wys;

        warstwa.bufor_pikseli =
            stary_bufor;

        __atomic_store_n(
            &warstwa.aktywna,
            true,
            __ATOMIC_RELEASE
        );

        skladacz_obrazu_oznacz_dirty();
        return pid;
    }

    /*
     * Accounting dotyczy stanu po udanej wymianie.
     *
     * Przy powiekszaniu rezerwujemy tylko roznice. kmalloc() nadal moze
     * odmowic z powodu chwilowej potrzeby posiadania starego i nowego
     * bufora jednoczesnie - wtedy stara warstwa pozostaje nietknieta.
     */
    uint64_t zarezerwowane_delta =
        0;

    if (nowe_bajty >
        stare_bajty) {

        zarezerwowane_delta =
            nowe_bajty -
            stare_bajty;

        if (!zarezerwuj_dodatkowe_bajty(
                zarezerwowane_delta)) {

            return -1;
        }
    }

    uint32_t* nowy_bufor =
        static_cast<uint32_t*>(
            kmalloc(
                nowe_bajty
            )
        );

    if (!nowy_bufor) {
        if (zarezerwowane_delta != 0) {
            oddaj_bajty(
                zarezerwowane_delta
            );
        }

        return -1;
    }

    wyczysc_bufor_pikseli(
        nowy_bufor,
        liczba_pikseli
    );

    /*
     * Dopiero teraz odpublikowujemy stara warstwe.
     */
    __atomic_store_n(
        &warstwa.aktywna,
        false,
        __ATOMIC_RELEASE
    );

    /*
     * Publikujemy wszystkie metadane przed aktywna=true.
     */
    warstwa.pid =
        pid;

    warstwa.z_order =
        z_order;

    warstwa.x =
        x;

    warstwa.y =
        y;

    warstwa.szerokosc =
        szer;

    warstwa.wysokosc =
        wys;

    warstwa.bufor_pikseli =
        nowy_bufor;

    __atomic_store_n(
        &rozmiar_alokacji_warstwy[pid],
        nowe_bajty,
        __ATOMIC_RELEASE
    );

    __atomic_store_n(
        &warstwa.aktywna,
        true,
        __ATOMIC_RELEASE
    );

    /*
     * Stary bufor nie jest juz osiagalny przez nowa publikacje.
     */
    if (stary_bufor &&
        stary_bufor !=
            nowy_bufor) {

        kfree(
            stary_bufor
        );
    }

    if (stare_bajty >
        nowe_bajty) {

        oddaj_bajty(
            stare_bajty -
            nowe_bajty
        );
    }

    skladacz_obrazu_oznacz_dirty();
    return pid;
}

/* =========================================================================
 * 11. PRZESUWANIE WARSTWY
 * ========================================================================= */

void zaktualizuj_pozycje_warstwy(
    int pid,
    int nowy_x,
    int nowy_y
) {
    warstwa_obrazu* warstwa =
        pobierz_warstwe(
            pid
        );

    if (!warstwa) {
        return;
    }

    /*
     * Nie wykonujemy x+szer tutaj, wiec same skrajne wartosci int sa
     * bezpieczne. Kompozytor uzyje int64_t podczas clippingu.
     */
    warstwa->x =
        nowy_x;

    warstwa->y =
        nowy_y;
    skladacz_obrazu_oznacz_dirty();
}

/* =========================================================================
 * 12. CZYSZCZENIE WARSTWY
 * ========================================================================= */

void wyczysc_warstwe(
    int pid
) {
    warstwa_obrazu* warstwa =
        pobierz_warstwe(
            pid
        );

    if (!warstwa ||
        !warstwa->bufor_pikseli) {

        return;
    }

    const uint64_t bajty =
        __atomic_load_n(
            &rozmiar_alokacji_warstwy[pid],
            __ATOMIC_ACQUIRE
        );

    if (bajty == 0 ||
        (bajty %
         sizeof(uint32_t)) != 0) {

        return;
    }

    const uint64_t liczba_pikseli =
        bajty /
        sizeof(uint32_t);

    wyczysc_bufor_pikseli(
        warstwa->bufor_pikseli,
        liczba_pikseli
    );
    skladacz_obrazu_oznacz_dirty();
}

/* =========================================================================
 * 13. USUWANIE WARSTWY
 * ========================================================================= */

void usun_warstwe(
    int pid
) {
    if (!pid_poprawny(
            pid)) {

        return;
    }

    warstwa_obrazu& warstwa =
        tablica_warstw[pid];

    /*
     * Najpierw przestajemy publikowac powierzchnie nowym czytelnikom.
     */
    __atomic_store_n(
        &warstwa.aktywna,
        false,
        __ATOMIC_RELEASE
    );

    uint32_t* bufor =
        warstwa.bufor_pikseli;

    const uint64_t bajty =
        __atomic_exchange_n(
            &rozmiar_alokacji_warstwy[pid],
            0ULL,
            __ATOMIC_ACQ_REL
        );

    /*
     * Zerujemy metadane przed kfree(), aby nowy pobierz_warstwe() nie mogl
     * znalezc wskaznika do zwalnianej pamieci.
     */
    warstwa = {};
    warstwa.pid =
        pid;

    if (bufor) {
        kfree(
            bufor
        );
    }
    skladacz_obrazu_oznacz_dirty();

    oddaj_bajty(
        bajty
    );
}

/* =========================================================================
 * 14. SKLADANIE POJEDYNCZEJ WARSTWY
 * ========================================================================= */

namespace {

void zloz_warstwe(
    int indeks,
    int ekran_szer,
    int ekran_wys
) {
    if (!warstwa_ma_spojny_bufor(
            indeks)) {

        return;
    }

    const warstwa_obrazu& w =
        tablica_warstw[indeks];

    ProstokatWidoczny clip{};

    if (!wyznacz_widoczny_fragment(
            w,
            ekran_szer,
            ekran_wys,
            &clip)) {

        return;
    }

    const uint64_t szerokosc_warstwy =
        static_cast<uint64_t>(
            static_cast<uint32_t>(
                w.szerokosc
            )
        );

    const uint64_t zaalokowane_piksele =
        __atomic_load_n(
            &rozmiar_alokacji_warstwy[indeks],
            __ATOMIC_ACQUIRE
        ) /
        sizeof(uint32_t);

    for (int y = 0;
         y < clip.wys;
         ++y) {

        const uint64_t src_y =
            static_cast<uint64_t>(
                static_cast<uint32_t>(
                    clip.src_y + y
                )
            );

        const uint64_t pierwszy =
            src_y *
                szerokosc_warstwy +
            static_cast<uint64_t>(
                static_cast<uint32_t>(
                    clip.src_x
                )
            );

        const uint64_t dlugosc =
            static_cast<uint64_t>(
                static_cast<uint32_t>(
                    clip.szer
                )
            );

        /*
         * Metadane byly juz sprawdzone, ale ten check jest celowo tuz przed
         * dereferencja - chroni przed ewentualnym uszkodzeniem indeksu.
         */
        if (pierwszy >
                zaalokowane_piksele ||
            dlugosc >
                zaalokowane_piksele -
                    pierwszy) {

            return;
        }

        const uint32_t* zrodlo =
            w.bufor_pikseli +
            pierwszy;

        const int dst_y =
            clip.dst_y +
            y;

        for (int x = 0;
             x < clip.szer;
             ++x) {

            uint32_t kolor =
                zrodlo[x];

            /*
             * DOKLADNE zero jest przezroczyste.
             */
            if (kolor ==
                0x00000000U) {

                continue;
            }

            /*
             * Gorny bajt jest technicznym markerem uzywanym m.in. przez
             * grafika.cpp do rozroznienia:
             *
             *   0x00000000 = przezroczysty
             *   0x01000000 = widoczna czern
             *
             * Framebuffer Bursztyna uzywa 0x00RRGGBB, wiec marker usuwamy.
             */
            kolor &=
                0x00FFFFFFU;

            grafika_zapisz_surowy_piksel(
                clip.dst_x + x,
                dst_y,
                kolor
            );
        }
    }
}

} // namespace

/* =========================================================================
 * 15. KOMPOZYCJA CALEJ KLATKI
 * ========================================================================= */

void skladacz_obrazu_zloz_klatke() {
    GuardSkladania guard;

    if (!guard.aktywny()) {
        /*
         * Klatka jest juz skladana. Pomijamy zagniezdzone zadanie zamiast
         * modyfikowac backbuffer rownolegle.
         */
        return;
    }

    const int ekran_szer =
        grafika_pobierz_szerokosc();

    const int ekran_wys =
        grafika_pobierz_wysokosc();

    if (ekran_szer <= 0 ||
        ekran_wys <= 0) {

        return;
    }

    /*
     * Najpierw przechodzimy w surowy tryb backbufferu. Od tego momentu
     * PostawPiksel() nie moze przypadkowo zapisac do warstwy aktualnego PID.
     */
    grafika_rozpocznij_skladanie();

    /*
     * Kazda pelna klatka zaczyna sie od tapety/koloru pulpitu. Usuwa to
     * slady po przesunietych lub zamknietych oknach.
     */
    grafika_odtworz_tlo_skladania();

    /*
     * Maksymalnie 16 wpisow, wiec prosty selection-sort jest szybszy
     * organizacyjnie i nie potrzebuje dynamicznej pamieci.
     */
    bool uzyta[
        MAKS_WARSTW
    ] = {};

    for (int numer = 0;
         numer < MAKS_WARSTW;
         ++numer) {

        int wybrana =
            -1;

        for (int i = 0;
             i < MAKS_WARSTW;
             ++i) {

            if (uzyta[i] ||
                !warstwa_ma_spojny_bufor(
                    i)) {

                continue;
            }

            if (wybrana < 0 ||
                warstwa_przed(
                    i,
                    wybrana)) {

                wybrana =
                    i;
            }
        }

        if (wybrana < 0) {
            break;
        }

        uzyta[wybrana] =
            true;

        zloz_warstwe(
            wybrana,
            ekran_szer,
            ekran_wys
        );
    }

    /* Shell pozostaje zwyklym oknem nad pulpitem i innymi warstwami. */
    grafika_naloz_okno_terminala();

    /*
     * Zegar jest nakladka kernela, nie powierzchnia dowolnego procesu.
     */
    rysuj_zegar_rtc();

    /*
     * grafika_zakoncz_skladanie():
     *   - rysuje kursor,
     *   - przenosi backbuffer do framebufferu,
     *   - opuszcza surowy tryb skladania.
     */
    grafika_zakoncz_skladanie();
}

void skladacz_obrazu_oznacz_dirty() {
    __atomic_store_n(&compositor_dirty, true, __ATOMIC_RELEASE);
}

void skladacz_obrazu_obsluz_dirty() {
    if (!__atomic_exchange_n(&compositor_dirty, false, __ATOMIC_ACQ_REL))
        return;
    skladacz_obrazu_zloz_klatke();
}
