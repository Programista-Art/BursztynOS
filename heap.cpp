/*
 * Bursztyn OS - alokator sterty jadra typu first-fit.
 *
 * Lista blokow jest ulozona zgodnie z kolejnoscia adresow w pamieci. Pozwala
 * to dzielic duze bloki oraz laczyc sasiadujace wolne bloki bez dodatkowych
 * tablic i bez korzystania z biblioteki standardowej.
 */

#include "heap.h"

namespace {

constexpr uint64_t WYROWNANIE = 16;
constexpr uint64_t MINIMALNY_BLOK_UZYTKOWY = 16;
constexpr uint32_t MAGIC_STERTY = 0x12345678U;

BlokSterty* pierwszy_blok = nullptr;
uintptr_t poczatek_sterty = 0;
uintptr_t koniec_sterty = 0; // Pierwszy adres juz nienalezacy do sterty.

/*
 * Dla potegi dwojki A zaokraglenie x w gore ma postac:
 *
 *     (x + A - 1) & ~(A - 1)
 *
 * Dla A = 16 maska ~(16 - 1) zeruje cztery najmlodsze bity. Dodanie 15
 * sprawia, ze kazda wartosc niewyrownana przechodzi do nastepnej granicy
 * 16 bajtow, a wartosc juz wyrownana pozostaje bez zmian.
 */
bool wyrownaj_rozmiar(uint64_t wartosc, uint64_t& wynik) {
    if (wartosc > UINT64_MAX - (WYROWNANIE - 1)) {
        return false;
    }

    wynik = (wartosc + WYROWNANIE - 1) & ~(WYROWNANIE - 1);
    return true;
}

void polacz_z_nastepnym(BlokSterty* blok) {
    BlokSterty* nastepny = blok->nastepny;
    if (nastepny == nullptr || !nastepny->czy_wolny ||
        nastepny->magic != MAGIC_STERTY) {
        return;
    }

    blok->rozmiar += sizeof(BlokSterty) + nastepny->rozmiar;
    blok->nastepny = nastepny->nastepny;

    if (blok->nastepny != nullptr) {
        blok->nastepny->poprzedni = blok;
    }

    // Stary naglowek znajduje sie od tej chwili wewnatrz wolnego obszaru.
    nastepny->magic = 0;
}

} // namespace

void inicjalizuj_sterte_jadra(void* adres_poczatkowy,
                              uint64_t rozmiar_poczatkowy) {
    // Nie pozostawiamy aktywnej starej listy po nieudanej reinicjalizacji.
    pierwszy_blok = nullptr;
    poczatek_sterty = 0;
    koniec_sterty = 0;

    if (adres_poczatkowy == nullptr || rozmiar_poczatkowy == 0) {
        return;
    }

    const uintptr_t surowy_poczatek =
        reinterpret_cast<uintptr_t>(adres_poczatkowy);

    // Ochrona przed przepelnieniem podczas wyznaczania konca obszaru.
    if (rozmiar_poczatkowy > UINTPTR_MAX - surowy_poczatek) {
        return;
    }

    const uintptr_t surowy_koniec = surowy_poczatek + rozmiar_poczatkowy;
    if (surowy_poczatek > UINTPTR_MAX - (WYROWNANIE - 1)) {
        return;
    }

    const uintptr_t wyrownany_poczatek =
        (surowy_poczatek + WYROWNANIE - 1) & ~(uintptr_t)(WYROWNANIE - 1);
    const uintptr_t wyrownany_koniec =
        surowy_koniec & ~(uintptr_t)(WYROWNANIE - 1);

    if (wyrownany_koniec <= wyrownany_poczatek ||
        wyrownany_koniec - wyrownany_poczatek <
            sizeof(BlokSterty) + MINIMALNY_BLOK_UZYTKOWY) {
        return;
    }

    poczatek_sterty = wyrownany_poczatek;
    koniec_sterty = wyrownany_koniec;
    pierwszy_blok = reinterpret_cast<BlokSterty*>(wyrownany_poczatek);

    pierwszy_blok->rozmiar =
        wyrownany_koniec - wyrownany_poczatek - sizeof(BlokSterty);
    pierwszy_blok->czy_wolny = true;
    pierwszy_blok->nastepny = nullptr;
    pierwszy_blok->poprzedni = nullptr;
    pierwszy_blok->magic = MAGIC_STERTY;
}

void* kmalloc(uint64_t rozmiar) {
    if (rozmiar == 0 || pierwszy_blok == nullptr) {
        return nullptr;
    }

    uint64_t wyrownany_rozmiar = 0;
    if (!wyrownaj_rozmiar(rozmiar, wyrownany_rozmiar)) {
        return nullptr;
    }

    for (BlokSterty* blok = pierwszy_blok; blok != nullptr;
         blok = blok->nastepny) {
        if (blok->magic != MAGIC_STERTY || !blok->czy_wolny ||
            blok->rozmiar < wyrownany_rozmiar) {
            continue;
        }

        const uint64_t pozostalo = blok->rozmiar - wyrownany_rozmiar;

        /*
         * Dzielimy tylko wtedy, gdy reszta pomiesci caly nowy naglowek oraz
         * co najmniej jeden 16-bajtowy blok uzytkowy. W przeciwnym razie mala
         * koncowka zostaje czescia przydzielonego bloku.
         */
        if (pozostalo >= sizeof(BlokSterty) + MINIMALNY_BLOK_UZYTKOWY) {
            uintptr_t adres_nowego = reinterpret_cast<uintptr_t>(blok) +
                                     sizeof(BlokSterty) + wyrownany_rozmiar;
            BlokSterty* nowy = reinterpret_cast<BlokSterty*>(adres_nowego);

            nowy->rozmiar = pozostalo - sizeof(BlokSterty);
            nowy->czy_wolny = true;
            nowy->nastepny = blok->nastepny;
            nowy->poprzedni = blok;
            nowy->magic = MAGIC_STERTY;

            if (nowy->nastepny != nullptr) {
                nowy->nastepny->poprzedni = nowy;
            }

            blok->rozmiar = wyrownany_rozmiar;
            blok->nastepny = nowy;
        }

        blok->czy_wolny = false;
        return reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(blok) +
                                       sizeof(BlokSterty));
    }

    return nullptr; // Brak wystarczajaco duzego, ciaglego wolnego bloku.
}

void kfree(void* wskaznik) {
    if (wskaznik == nullptr || pierwszy_blok == nullptr) {
        return;
    }

    const uintptr_t adres_uzytkowy = reinterpret_cast<uintptr_t>(wskaznik);

    // Najpierw kontrola granic i wyrownania, dopiero potem odczyt naglowka.
    if (adres_uzytkowy < poczatek_sterty + sizeof(BlokSterty) ||
        adres_uzytkowy >= koniec_sterty ||
        (adres_uzytkowy & (WYROWNANIE - 1)) != 0) {
        return;
    }

    BlokSterty* blok = reinterpret_cast<BlokSterty*>(
        adres_uzytkowy - sizeof(BlokSterty));

    if (blok->magic != MAGIC_STERTY || blok->czy_wolny) {
        return; // Obcy wskaznik, uszkodzony naglowek albo podwojne zwolnienie.
    }

    blok->czy_wolny = true;

    // Najpierw wchlaniamy prawy blok, a potem ewentualnie calosc do lewego.
    polacz_z_nastepnym(blok);
    if (blok->poprzedni != nullptr && blok->poprzedni->czy_wolny &&
        blok->poprzedni->magic == MAGIC_STERTY) {
        polacz_z_nastepnym(blok->poprzedni);
    }
}

// Globalne operatory C++ korzystaja z tej samej sterty jadra.
void* operator new(size_t size) {
    return kmalloc(static_cast<uint64_t>(size));
}

void* operator new[](size_t size) {
    return kmalloc(static_cast<uint64_t>(size));
}

void operator delete(void* p) noexcept {
    kfree(p);
}

void operator delete[](void* p) noexcept {
    kfree(p);
}

void operator delete(void* p, size_t) noexcept {
    kfree(p);
}

void operator delete[](void* p, size_t) noexcept {
    kfree(p);
}
