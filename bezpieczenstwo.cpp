/*
 * Bursztyn OS - bezpieczenstwo.cpp
 *
 * Bezpieczna komunikacja pomiedzy jadrem (Ring 0) a aplikacjami Ring 3.
 *
 * Ten modul realizuje trzy poziomy ochrony:
 *   1. Walidacja numeryczna adresu i calego zakresu (canonical lower half).
 *   2. Opcjonalna walidacja mapowania przez VMM, jezeli VMM udostepnia hook.
 *   3. Ograniczone kopiowanie zamiast bezposredniego uzywania wskaznikow
 *      przekazanych przez aplikacje w kodzie obslugi syscalli.
 *
 * UWAGA:
 * Sama kontrola "adres < granica Ring 3" NIE dowodzi, ze strona jest
 * zmapowana. Dlatego modul wspiera opcjonalny hook VMM:
 *
 *   extern "C" bool bws_vmm_sprawdz_zakres_uzytkownika(
 *       const void* adres, size_t rozmiar, bool zapis);
 *
 * Jesli funkcja istnieje, musi sprawdzic wszystkie strony zakresu oraz bity:
 *   - Present,
 *   - User,
 *   - Writable, gdy zapis == true.
 *
 * Do czasu dodania tego hooka modul zachowuje zgodnosc ze starszym VMM i
 * wykonuje tylko bezpieczna walidacje numeryczna zakresu. Jest to lepsze od
 * bezposredniej dereferencji wskaznikow Ring 3, ale pelna odpornosc na
 * niezmapowane strony wymaga walidacji tablic stron w VMM.
 */

#include <stdint.h>
#include <stddef.h>
#include "virtio_rng.h"

namespace {

/*
 * x86-64, 4-poziomowe stronicowanie (48-bit virtual addresses):
 * dolna polowa kanoniczna to:
 *
 *   0x0000000000000000 .. 0x00007FFFFFFFFFFF
 *
 * Uzywamy granicy WYŁĄCZNEJ, co upraszcza kontrole zakresow i usuwa
 * problemy typu off-by-one.
 */
constexpr uint64_t BWS_USER_SPACE_END = 0x0000800000000000ULL;

/*
 * Strona zerowa nie powinna byc prawidlowym obszarem aplikacji. Odrzucenie
 * pierwszych 4 KiB dodatkowo pomaga wykrywac NULL i wskazniki NULL+offset.
 */
constexpr uint64_t BWS_USER_SPACE_MIN = 0x0000000000001000ULL;

/*
 * Rozmiar strony uzywany tylko do dzielenia duzych kopii na mniejsze
 * fragmenty. Nie zakladamy tutaj, jak VMM przechowuje swoje PTE.
 */
constexpr size_t BWS_ROZMIAR_STRONY = 4096;

/*
 * Opcjonalny hook VMM.
 *
 * Symbol weak pozwala skompilowac obecne jadro bez natychmiastowego
 * dodawania funkcji do pamiec.cpp. Gdy pozniej VMM ja zaimplementuje,
 * bezpieczenstwo.cpp automatycznie zacznie sprawdzac mapowania stron.
 */
extern "C" bool bws_vmm_sprawdz_zakres_uzytkownika(
    const void* adres,
    size_t rozmiar,
    bool zapis
);

/*
 * Sprawdza tylko matematyczna poprawnosc zakresu.
 * Nie wykonuje dereferencji ptr.
 */
bool zakres_numerycznie_poprawny(const void* ptr, size_t rozmiar) {
    const uint64_t poczatek = reinterpret_cast<uint64_t>(ptr);

    if (rozmiar == 0) {
        /*
         * Dla pustego zakresu nie ma bajtow do odczytu/zapisu, ale nadal
         * nie akceptujemy adresow jadra ani niekanonicznych adresow.
         */
        return poczatek < BWS_USER_SPACE_END;
    }

    if (ptr == nullptr) return false;
    if (poczatek < BWS_USER_SPACE_MIN) return false;
    if (poczatek >= BWS_USER_SPACE_END) return false;

    /*
     * Zamiast liczyc poczatek + rozmiar (co mogloby overflowac), sprawdzamy
     * ile bajtow pozostalo do konca przestrzeni uzytkownika.
     */
    const uint64_t pozostalo = BWS_USER_SPACE_END - poczatek;
    if (static_cast<uint64_t>(rozmiar) > pozostalo) return false;

    return true;
}

/*
 * Laczy kontrole numeryczna z opcjonalna kontrola tablic stron VMM.
 */
bool zakres_dostepny(const void* ptr, size_t rozmiar, bool zapis) {
    if (!zakres_numerycznie_poprawny(ptr, rozmiar)) return false;

    if (rozmiar == 0) return true;

    /*
     * VMM dostarcza prawdziwa walidacje PTE, wymagamy jej sukcesu.
     */
    if (!bws_vmm_sprawdz_zakres_uzytkownika(ptr, rozmiar, zapis)) {
        return false;
    }

    return true;
}

/*
 * Liczba bajtow do konca aktualnej strony. Dzielenie kopiowania na strony
 * upraszcza diagnostyke i przyszle dodanie fault-recovery/copy fixup.
 */
size_t bajtow_do_konca_strony(uint64_t adres) {
    const size_t przesuniecie = static_cast<size_t>(adres & (BWS_ROZMIAR_STRONY - 1));
    return BWS_ROZMIAR_STRONY - przesuniecie;
}

} // namespace

/*
 * Zachowana publiczna stala ze starego pliku.
 * Jest to najwyzszy dozwolony adres bajtu Ring 3 (granica wlaczna).
 */
const uint64_t BWS_USER_SPACE_LIMIT = BWS_USER_SPACE_END - 1ULL;

/**
 * Kompatybilna funkcja ze starego API.
 * Sprawdza zakres przeznaczony do ODCZYTU przez jadro.
 */
bool czy_bezpieczny_wskaznik_uzytkownika(const void* ptr, size_t rozmiar) {
    return zakres_dostepny(ptr, rozmiar, false);
}

/**
 * Jawna wersja do odczytu z Ring 3.
 */
bool czy_bezpieczny_zakres_uzytkownika_do_odczytu(
    const void* ptr,
    size_t rozmiar
) {
    return zakres_dostepny(ptr, rozmiar, false);
}

/**
 * Jawna wersja do zapisu do Ring 3.
 * Jesli VMM ma hook walidacyjny, wymagany jest bit Writable.
 */
bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(
    void* ptr,
    size_t rozmiar
) {
    return zakres_dostepny(ptr, rozmiar, true);
}

/**
 * Odpowiednik copy_from_user.
 *
 * Kopiuje dane:
 *   Ring 3 -> Ring 0
 *
 * Nie wolno po tej funkcji ponownie uzywac oryginalnego wskaznika aplikacji
 * do odczytu tych samych danych w syscallu. Jadro powinno pracowac na swojej
 * kopii.
 */
bool skopiuj_z_przestrzeni_uzytkownika(
    void* bufor_jadra,
    const void* ptr_uzytkownika,
    size_t rozmiar
) {
    if (rozmiar == 0) return true;
    if (bufor_jadra == nullptr || ptr_uzytkownika == nullptr) return false;

    if (!czy_bezpieczny_zakres_uzytkownika_do_odczytu(
            ptr_uzytkownika, rozmiar)) {
        return false;
    }

    uint8_t* dst = static_cast<uint8_t*>(bufor_jadra);
    const uint8_t* src = static_cast<const uint8_t*>(ptr_uzytkownika);

    size_t pozostalo = rozmiar;
    size_t offset = 0;

    while (pozostalo > 0) {
        const uint64_t adres_src =
            reinterpret_cast<uint64_t>(ptr_uzytkownika) + offset;

        size_t fragment = bajtow_do_konca_strony(adres_src);
        if (fragment > pozostalo) fragment = pozostalo;

        /*
         * Ponowne sprawdzenie fragmentu jest tanie, a po dodaniu hooka VMM
         * daje walidacje zgodna z granicami stron.
         */
        if (!zakres_dostepny(src + offset, fragment, false)) {
            return false;
        }

        for (size_t i = 0; i < fragment; i++) {
            dst[offset + i] = src[offset + i];
        }

        offset += fragment;
        pozostalo -= fragment;
    }

    return true;
}

/* Kernelowy provider entropii. mbedTLS zna tylko ten kontrakt i nie posiada
 * polityki sprzetowej. Laczy sprzetowy RDSEED, RDRAND (jesli dostepne w CPU)
 * oraz urzadzenie sprzetowe VirtIO-RNG (jesli dostepne). Brak wszystkich
 * zaufanych zrodel sprzetowych odmawia pracy (fail-closed) — zegar RDTSC/RTC
 * nigdy nie jest traktowany jako samodzielne zrodlo entropii kryptograficznej. */
extern "C" bool bursztyn_krypto_wypelnij_losowe(uint8_t* bufor,
                                                 size_t dlugosc) {
    if (!bufor && dlugosc != 0) return false;
    if (dlugosc == 0) return true;
    virtio_rng_secure_zero(bufor, dlugosc);

    bool has_rdrand = false;
    bool has_rdseed = false;
    {
        uint32_t a = 0, b = 0, c = 0, d = 0;
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(0U), "c"(0U));
        const uint32_t max_leaf = a;
        a = 1U; c = 0U;
        asm volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
        if ((c & (1U << 30)) != 0) {
            has_rdrand = true;
        }
        if (max_leaf >= 7U) {
            a = 7U; c = 0U;
            asm volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
            if ((b & (1U << 18)) != 0) {
                has_rdseed = true;
            }
        }
    }

    const bool has_virtio = virtio_rng_is_ready();

    if (!has_rdseed && !has_rdrand && !has_virtio) {
        return false;
    }

    bool rdseed_ok = false;
    if (has_rdseed) {
        size_t off = 0;
        rdseed_ok = true;
        while (off < dlugosc) {
            uint64_t v = 0;
            unsigned char ok = 0;
            for (unsigned proba = 0; proba < 16 && !ok; ++proba) {
                asm volatile("rdseed %0; setc %1" : "=r"(v), "=qm"(ok) :: "cc");
                if (ok && (v == 0ULL || v == ~0ULL)) {
                    ok = 0;
                }
            }
            if (!ok) {
                rdseed_ok = false;
                break;
            }
            for (unsigned i = 0; i < 8 && off < dlugosc; ++i, ++off) {
                bufor[off] ^= static_cast<uint8_t>(v >> (i * 8));
            }
        }
        if (!rdseed_ok) {
            virtio_rng_secure_zero(bufor, dlugosc);
        }
    }

    bool rdrand_ok = false;
    if (has_rdrand) {
        size_t off = 0;
        rdrand_ok = true;
        while (off < dlugosc) {
            uint64_t v = 0;
            unsigned char ok = 0;
            for (unsigned proba = 0; proba < 16 && !ok; ++proba) {
                asm volatile("rdrand %0; setc %1" : "=r"(v), "=qm"(ok) :: "cc");
                if (ok && (v == 0ULL || v == ~0ULL)) {
                    ok = 0;
                }
            }
            if (!ok) {
                rdrand_ok = false;
                break;
            }
            for (unsigned i = 0; i < 8 && off < dlugosc; ++i, ++off) {
                bufor[off] ^= static_cast<uint8_t>(v >> (i * 8));
            }
        }
        if (!rdrand_ok && !rdseed_ok) {
            virtio_rng_secure_zero(bufor, dlugosc);
        }
    }

    bool virtio_ok = false;
    if (has_virtio) {
        uint8_t mix_buf[64];
        size_t off = 0;
        virtio_ok = true;
        while (off < dlugosc) {
            size_t chunk = dlugosc - off;
            if (chunk > sizeof(mix_buf)) {
                chunk = sizeof(mix_buf);
            }
            const size_t got = virtio_rng_read(mix_buf, chunk);
            for (size_t i = 0; i < got; ++i) {
                bufor[off + i] ^= mix_buf[i];
            }
            virtio_rng_secure_zero(mix_buf, sizeof(mix_buf));
            if (got < chunk) {
                virtio_ok = false;
                break;
            }
            off += got;
        }
    }

    if (!rdseed_ok && !rdrand_ok && !virtio_ok) {
        virtio_rng_secure_zero(bufor, dlugosc);
        return false;
    }

    return true;
}

/**
 * Odpowiednik copy_to_user.
 *
 * Kopiuje dane:
 *   Ring 0 -> Ring 3
 *
 * Uzywaj tej funkcji m.in. dla:
 *   - pozycji myszy,
 *   - rozdzielczosci,
 *   - czasu RTC,
 *   - wynikow DNS,
 *   - odczytu plikow,
 *   - innych syscalli zapisujacych wynik do bufora aplikacji.
 */
bool skopiuj_do_przestrzeni_uzytkownika(
    void* ptr_uzytkownika,
    const void* bufor_jadra,
    size_t rozmiar
) {
    if (rozmiar == 0) return true;
    if (ptr_uzytkownika == nullptr || bufor_jadra == nullptr) return false;

    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            ptr_uzytkownika, rozmiar)) {
        return false;
    }

    uint8_t* dst = static_cast<uint8_t*>(ptr_uzytkownika);
    const uint8_t* src = static_cast<const uint8_t*>(bufor_jadra);

    size_t pozostalo = rozmiar;
    size_t offset = 0;

    while (pozostalo > 0) {
        const uint64_t adres_dst =
            reinterpret_cast<uint64_t>(ptr_uzytkownika) + offset;

        size_t fragment = bajtow_do_konca_strony(adres_dst);
        if (fragment > pozostalo) fragment = pozostalo;

        if (!zakres_dostepny(dst + offset, fragment, true)) {
            return false;
        }

        for (size_t i = 0; i < fragment; i++) {
            dst[offset + i] = src[offset + i];
        }

        offset += fragment;
        pozostalo -= fragment;
    }

    return true;
}

/**
 * Bezpieczne pobranie napisu C z Ring 3.
 *
 * Sukces oznacza, ze w zakresie max_rozmiar znaleziono znak '\0'.
 * Na bledzie bufor_jadra jest zawsze zakonczony '\0', o ile max_rozmiar > 0.
 */
bool skopiuj_string_z_uzytkownika(
    char* bufor_jadra,
    const char* ptr_uzytkownika,
    size_t max_rozmiar
) {
    if (bufor_jadra == nullptr || max_rozmiar == 0) return false;

    /* Bezpieczny stan wyjsciowy nawet przy natychmiastowym bledzie. */
    bufor_jadra[0] = '\0';

    if (ptr_uzytkownika == nullptr) return false;

    for (size_t i = 0; i < max_rozmiar - 1; i++) {
        const char* adres_znaku = ptr_uzytkownika + i;

        if (!zakres_dostepny(adres_znaku, 1, false)) {
            bufor_jadra[i] = '\0';
            return false;
        }

        const char znak = *adres_znaku;
        bufor_jadra[i] = znak;

        if (znak == '\0') {
            return true;
        }
    }

    /*
     * Brak terminatora w dozwolonym limicie. Nie traktujemy obcietego napisu
     * jako sukcesu, bo syscall moglby wtedy wykonac operacje na innej sciezce
     * niz zamierzala aplikacja.
     */
    bufor_jadra[max_rozmiar - 1] = '\0';
    return false;
}

/**
 * Kopiuje napis C z jadra do Ring 3 wraz z terminatorem.
 * Przydatne np. do syscalla czasu i innych krotkich odpowiedzi tekstowych.
 *
 * max_rozmiar ogranicza zarowno odczyt bufora jadra, jak i zapis do Ring 3.
 */
bool skopiuj_string_do_uzytkownika(
    char* ptr_uzytkownika,
    const char* tekst_jadra,
    size_t max_rozmiar
) {
    if (ptr_uzytkownika == nullptr || tekst_jadra == nullptr ||
        max_rozmiar == 0) {
        return false;
    }

    size_t dlugosc = 0;
    while (dlugosc < max_rozmiar) {
        if (tekst_jadra[dlugosc] == '\0') {
            /* Kopiujemy razem z terminatorem. */
            return skopiuj_do_przestrzeni_uzytkownika(
                ptr_uzytkownika,
                tekst_jadra,
                dlugosc + 1
            );
        }
        dlugosc++;
    }

    /* Brak terminatora w limicie - odmawiamy kopiowania. */
    return false;
}
