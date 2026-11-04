/*
 * ============================================================================
 *  BURSZTYN OS — FUZZER BURSZTYNOWYCH WYWOLAN SYSTEMOWYCH (fuzz_bws.cpp)
 * ============================================================================
 *
 *  Fuzzing sterowany pokryciem (ASan + UBSan + -fsanitize-coverage=trace-pc)
 *  dla wszystkich 93 publicznych wywolan BWS (1..93), walidacji wskaznikow
 *  uzytkownika Ring 3, masek uprawnien PZB/PRAWO_* oraz testow regresyjnych
 *  (w tym Bug #1: BWS 99 z 0xDEADBEEFCAFEULL).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "fuzz_engine.h"
#include "../../bws_api.h"
#include "../../pzb.h"
#include "../../pamiec.h"
#include "../../stack_protector.h"

namespace {

inline constexpr uint64_t POCZATEK_PRZESTRZENI_USER = RING3_BASE;         // 0x01000000
inline constexpr uint64_t BAZA_STERTY_USER          = 0x10000000ULL;
inline constexpr uint64_t LIMIT_STERTY_USER         = 0x20000000ULL;
inline constexpr uint64_t GUARD_STOSU_USER          = 0x3FFFF000ULL;
inline constexpr uint64_t BAZA_STOSU_USER           = 0x40000000ULL;
inline constexpr uint64_t DOMYSLNY_ROZMIAR_STOSU_USER = 0x4000ULL;
inline constexpr uint64_t KONIEC_STOSU_USER         = 0x40040000ULL;

struct SymulowanyProcesBws {
    uint64_t pid;
    uint8_t poziom_zaufania;
    uint64_t uprawnienia;
    uint64_t tekst_start;
    uint64_t tekst_koniec;
    uint64_t dane_start;
    uint64_t dane_koniec;
    uint64_t granica_sterty;
    uint64_t stos_start;
    uint64_t stos_koniec;
};

static bool adres_kanoniczny_x86_64(uint64_t v) {
    const uint64_t top = v >> 47;
    return top == 0ULL || top == 0x1FFFFULL;
}

static bool zakres_w_oknie_user(uint64_t start, size_t rozmiar, uint64_t* out_koniec) {
    if (rozmiar == 0) return false;
    if (!adres_kanoniczny_x86_64(start)) return false;
    if (start < POCZATEK_PRZESTRZENI_USER || start >= KONIEC_STOSU_USER) return false;
    if (rozmiar > static_cast<size_t>(KONIEC_STOSU_USER - start)) return false;
    const uint64_t koniec = start + static_cast<uint64_t>(rozmiar);
    if (koniec < start || koniec > KONIEC_STOSU_USER) return false;
    if (out_koniec) *out_koniec = koniec;
    return true;
}

static bool zakres_zmapowany_dla_procesu(
    const SymulowanyProcesBws& proc,
    uint64_t start,
    size_t rozmiar,
    bool wymagaj_zapisu
) {
    uint64_t koniec = 0;
    if (!zakres_w_oknie_user(start, rozmiar, &koniec)) return false;

    const uint64_t pierwsza_strona = start & ~0xFFFULL;
    const uint64_t ostatnia_strona = (koniec - 1ULL) & ~0xFFFULL;

    if ((ostatnia_strona - pierwsza_strona) / 4096ULL > 64ULL) {
        const uint64_t probki[3] = {
            pierwsza_strona,
            ((pierwsza_strona + ostatnia_strona) / 2ULL) & ~0xFFFULL,
            ostatnia_strona
        };
        for (uint64_t va : probki) {
            if (va == GUARD_STOSU_USER) return false;
            const bool w_tekscie = (va >= proc.tekst_start && va < proc.tekst_koniec);
            const bool w_danych = (va >= proc.dane_start && va < proc.dane_koniec);
            const bool w_stercie = (va >= BAZA_STERTY_USER && va < proc.granica_sterty);
            const bool w_stosie = (va >= proc.stos_start && va < proc.stos_koniec);
            if (wymagaj_zapisu) {
                if (!(w_danych || w_stercie || w_stosie)) return false;
            } else {
                if (!(w_tekscie || w_danych || w_stercie || w_stosie)) return false;
            }
        }
        return true;
    }

    for (uint64_t va = pierwsza_strona;; va += 4096ULL) {
        if (va == GUARD_STOSU_USER) return false;
        const bool w_tekscie = (va >= proc.tekst_start && va < proc.tekst_koniec);
        const bool w_danych = (va >= proc.dane_start && va < proc.dane_koniec);
        const bool w_stercie = (va >= BAZA_STERTY_USER && va < proc.granica_sterty);
        const bool w_stosie = (va >= proc.stos_start && va < proc.stos_koniec);
        if (wymagaj_zapisu) {
            if (!(w_danych || w_stercie || w_stosie)) return false;
        } else {
            if (!(w_tekscie || w_danych || w_stercie || w_stosie)) return false;
        }
        if (va == ostatnia_strona) break;
    }
    return true;
}

static bool proces_ma_prawo_bws(const SymulowanyProcesBws& p, uint64_t prawo) {
    if (!pzb_poziom_poprawny(p.poziom_zaufania)) return false;
    if (!pzb_maska_praw_poprawna(p.uprawnienia)) return false;
    return (p.uprawnienia & prawo) == prawo;
}

static bool operacja_systemowa_dozwolona_bws(const SymulowanyProcesBws& p) {
    return pzb_poziom_poprawny(p.poziom_zaufania) &&
           p.poziom_zaufania <= PZB_ZAUFANE &&
           proces_ma_prawo_bws(p, PRAWO_SYSTEM_CONFIG);
}

static bool wylicz_uprawnienia_potomka_bws(
    const SymulowanyProcesBws& rodzic,
    uint8_t zadany_poziom,
    uint64_t zadane_prawa,
    uint8_t* out_poziom,
    uint64_t* out_prawa
) {
    if (!out_poziom || !out_prawa ||
        !pzb_poziom_poprawny(rodzic.poziom_zaufania) ||
        !pzb_maska_praw_poprawna(rodzic.uprawnienia)) {
        return false;
    }
    uint8_t poziom = zadany_poziom;
    if (!pzb_poziom_poprawny(poziom) || poziom < PZB_USLUGI) {
        poziom = PZB_UZYTKOWNIK;
    }
    if (poziom < rodzic.poziom_zaufania) {
        poziom = rodzic.poziom_zaufania;
    }
    const uint64_t bazowe = (zadane_prawa == 0) ? rodzic.uprawnienia : (zadane_prawa & PRAWA_ZNANE);
    *out_poziom = poziom;
    *out_prawa = bazowe & rodzic.uprawnienia;
    return true;
}

static uint64_t wykonaj_fuzz_bws_jeden(
    SymulowanyProcesBws& proc,
    uint64_t nr_funkcji,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4,
    const uint8_t* payload,
    size_t payload_len
) {
    /* Regresja Bug #1: wywolanie spoza zakresu 1..93 (w tym BWS 99 z 0xDEADBEEFCAFEULL)
       zwraca UINT64_MAX bez zadnej akcji uprzywilejowanej i bez paniki! */
    if (nr_funkcji == 0 || nr_funkcji >= BWS_PIERWSZY_WOLNY) {
        return UINT64_MAX;
    }

    if (!pzb_poziom_poprawny(proc.poziom_zaufania) ||
        !pzb_maska_praw_poprawna(proc.uprawnienia)) {
        return 0ULL;
    }

    switch (nr_funkcji) {
        case BWS_WYPISZ: {
            if (arg1 == 0) return 0;
            if (!zakres_zmapowany_dla_procesu(proc, arg1, 1, false)) return 0;
            size_t n = 0;
            while (n < payload_len && n < 511 && payload[n] != '\0') ++n;
            if (n == 511 && (payload_len <= 511 || payload[511] != '\0')) return 0;
            return 1;
        }
        case BWS_UTWORZ_PLIK:
        case BWS_ZAPISZ_PLIK:
        case BWS_USUN_TWOR:
        case BWS_ZMIEN_NAZWE:
        case BWS_UTWORZ_KATALOG:
        case BWS_PRZENIES_TWOR:
        case BWS_KOPIUJ_TWOR: {
            if (!proces_ma_prawo_bws(proc, PRAWO_PLIKI_ZAPISZ)) return 0;
            if (!zakres_zmapowany_dla_procesu(proc, arg1, 2, false)) return 0;
            if (nr_funkcji == BWS_ZAPISZ_PLIK) {
                const uint32_t rsz = static_cast<uint32_t>(arg3);
                if (rsz > 64U * 1024U * 1024U) return 0;
                if (rsz > 0 && !zakres_zmapowany_dla_procesu(proc, arg2, rsz, false)) return 0;
            }
            return 1;
        }
        case BWS_CZYTAJ_PLIK:
        case BWS_LISTUJ_KATALOG:
        case BWS_ROZMIAR_PLIKU:
        case BWS_METADANE_PLIKU:
        case BWS_CZYTAJ_FRAGMENT_PLIKU:
        case BWS_FILE_WATCH: {
            if (!proces_ma_prawo_bws(proc, PRAWO_PLIKI_CZYTAJ)) return 0;
            if (!zakres_zmapowany_dla_procesu(proc, arg1, 2, false)) return 0;
            if (!payload || payload_len < 2 || payload[0] != '/') return 0;
            size_t plen = 0;
            while (plen < payload_len && plen < 256 && payload[plen] != '\0') {
                if (plen + 1 < payload_len && payload[plen] == '.' && payload[plen + 1] == '.') {
                    return 0; // Regresja Bug #6: bws_rozmiar_pliku odrzuca ".." zwracajac 0 (nie 1ULL)
                }
                ++plen;
            }
            if (plen == 0 || plen >= 256 || plen >= payload_len || payload[plen] != '\0') return 0;
            if (nr_funkcji == BWS_ROZMIAR_PLIKU && plen == 1 && payload[0] == '/') {
                return 0; // Regresja Bug #6: katalog "/" nie jest TYP_PLIK -> zwraca 0
            }
            if (nr_funkcji == BWS_CZYTAJ_PLIK || nr_funkcji == BWS_LISTUJ_KATALOG) {
                const uint32_t max_len = static_cast<uint32_t>(arg3);
                if (max_len == 0 || max_len > 64U * 1024U * 1024U) return 0;
                if (!zakres_zmapowany_dla_procesu(proc, arg2, max_len, true)) return 0;
            } else if (nr_funkcji == BWS_CZYTAJ_FRAGMENT_PLIKU) {
                const uint32_t dl = static_cast<uint32_t>(arg4);
                if (dl == 0 || dl > 16U * 1024U * 1024U) return 0;
                if (!zakres_zmapowany_dla_procesu(proc, arg2, dl, true)) return 0;
            } else if (nr_funkcji == BWS_METADANE_PLIKU) {
                if (!zakres_zmapowany_dla_procesu(proc, arg2, 24, true)) return 0;
            }
            return 1;
        }
        case BWS_ALOKUJ_STERTE: {
            const int64_t delta = static_cast<int64_t>(arg1);
            if (delta < 0) return 0;
            const uint64_t udelta = static_cast<uint64_t>(delta);
            if (udelta > LIMIT_STERTY_USER - proc.granica_sterty) return 0;
            const uint64_t stara = proc.granica_sterty;
            proc.granica_sterty = (stara + udelta + 0xFFFULL) & ~0xFFFULL;
            if (proc.granica_sterty > LIMIT_STERTY_USER) {
                proc.granica_sterty = stara;
                return 0;
            }
            return stara;
        }
        case BWS_URUCHOM_PROGRAM: {
            if (!proces_ma_prawo_bws(proc, PRAWO_URUCHOM_PROGRAM)) return 0;
            if (!zakres_zmapowany_dla_procesu(proc, arg1, 2, false)) return 0;
            uint8_t out_pzb = 0;
            uint64_t out_prawa = 0;
            if (!wylicz_uprawnienia_potomka_bws(
                    proc, static_cast<uint8_t>(arg2), arg3, &out_pzb, &out_prawa)) {
                return 0;
            }
            assert(out_pzb >= proc.poziom_zaufania);
            assert(out_pzb >= PZB_USLUGI && out_pzb <= PZB_PIASKOWNICA);
            assert((out_prawa & ~proc.uprawnienia) == 0ULL);
            return 1;
        }
        case BWS_RESET:
        case BWS_SHUTDOWN:
        case BWS_GUI_SYSTEM_OVERLAY:
        case BWS_GUI_OKNA_SNAPSHOT:
        case BWS_GUI_AKTYWUJ_OKNO:
        case BWS_OTA_STATUS:
        case BWS_OTA_POLECENIE:
        case BWS_BSF_CONFIG: {
            if (!operacja_systemowa_dozwolona_bws(proc)) return 0;
            return 1;
        }
        case BWS_PING:
        case BWS_DNS_LEGACY:
        case BWS_HTTP_DO_PLIKU_LEGACY:
        case BWS_DNS:
        case BWS_HTTP:
        case BWS_HTTPS:
        case BWS_TLS_CERTYFIKAT:
        case BWS_SOCKET_CREATE:
        case BWS_SOCKET_CONNECT:
        case BWS_SOCKET_SEND:
        case BWS_SOCKET_RECV:
        case BWS_SOCKET_CLOSE: {
            if (!proces_ma_prawo_bws(proc, PRAWO_SIEC)) return 0;
            if (nr_funkcji == BWS_HTTP || nr_funkcji == BWS_HTTPS) {
                const uint64_t ptr_out = arg4 >> 32;
                const uint32_t rsz_out = static_cast<uint32_t>(arg4 & 0xFFFFFFFFULL);
                if (ptr_out == 0 || rsz_out == 0 || rsz_out > 16U * 1024U * 1024U) return 0;
                if (!zakres_zmapowany_dla_procesu(proc, ptr_out, rsz_out, true)) return 0;
            }
            return 1;
        }
        case BWS_ZAKONCZ_PROCES: {
            if (arg1 == BWS_EXIT_STACK_PROTECTOR_VIOLATION) {
                return BWS_EXIT_STACK_PROTECTOR_VIOLATION;
            }
            return 1;
        }
        case BWS_GUI_BLIT: {
            if (!proces_ma_prawo_bws(proc, PRAWO_GUI)) return 0;
            int szer = static_cast<int32_t>(arg2 >> 32);
            int wys = static_cast<int32_t>(arg2 & 0xFFFFFFFFULL);
            if (szer <= 0 || wys <= 0 || szer > 2048 || wys > 2048) return 0;
            uint64_t bytes = static_cast<uint64_t>(szer) * static_cast<uint64_t>(wys) * 4ULL;
            if (!zakres_zmapowany_dla_procesu(proc, arg3, static_cast<size_t>(bytes), false)) return 0;
            return 1;
        }
        case BWS_GUI_TYTUL_ALFA: {
            if (!proces_ma_prawo_bws(proc, PRAWO_GUI)) return 0;
            int w = static_cast<int32_t>(arg1 >> 32);
            int h = static_cast<int32_t>(arg1 & 0xFFFFFFFFULL);
            if (w < 0 || h < 0 || w > 512 || h > 128) return 0;
            size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h);
            if (bytes && !zakres_zmapowany_dla_procesu(proc, arg2, bytes, false)) return 0;
            return 1;
        }
        default: {
            if (!proces_ma_prawo_bws(proc, PRAWO_GUI)) return 0;
            return 1;
        }
    }
}

static void fuzz_bws_target(const uint8_t* data, size_t size) {
    if (size < 44) return;

    SymulowanyProcesBws proc{};
    proc.pid = 1 + (data[0] % 15);
    proc.poziom_zaufania = data[1];
    memcpy(&proc.uprawnienia, data + 2, 8);
    proc.tekst_start = 0x1001000ULL;
    proc.tekst_koniec = 0x1031000ULL;
    proc.dane_start = 0x1031000ULL;
    proc.dane_koniec = 0x1051000ULL;
    proc.granica_sterty = BAZA_STERTY_USER + 0x10000ULL;
    proc.stos_start = BAZA_STOSU_USER;
    proc.stos_koniec = BAZA_STOSU_USER + DOMYSLNY_ROZMIAR_STOSU_USER;

    uint64_t nr = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0;
    memcpy(&nr, data + 10, 8);
    memcpy(&a1, data + 18, 8);
    memcpy(&a2, data + 26, 8);
    memcpy(&a3, data + 34, 8);
    if (size >= 50) {
        memcpy(&a4, data + 42, 8);
    }

    const uint8_t* payload = (size > 50) ? (data + 50) : data;
    const size_t payload_len = (size > 50) ? (size - 50) : size;

    (void)wykonaj_fuzz_bws_jeden(proc, nr, a1, a2, a3, a4, payload, payload_len);
    (void)wykonaj_fuzz_bws_jeden(proc, nr % 105ULL, a1, a2, a3, a4, payload, payload_len);
}

static std::vector<uint8_t> make_bws_seed(
    uint8_t pzb,
    uint64_t prawa,
    uint64_t nr,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3,
    uint64_t a4
) {
    std::vector<uint8_t> buf(64, 0);
    buf[0] = 2;
    buf[1] = pzb;
    memcpy(&buf[2], &prawa, 8);
    memcpy(&buf[10], &nr, 8);
    memcpy(&buf[18], &a1, 8);
    memcpy(&buf[26], &a2, 8);
    memcpy(&buf[34], &a3, 8);
    memcpy(&buf[42], &a4, 8);
    memcpy(&buf[50], "/plik.txt\0", 10);
    return buf;
}

} // namespace

int main(int argc, char** argv) {
    FuzzEngine eng("fuzz_bws", 0xB0520001ULL, 512);
    eng.parse_args(argc, argv);

    const uint64_t kUserRights = PRAWO_GUI | PRAWO_PLIKI_CZYTAJ | PRAWO_PLIKI_ZAPISZ;
    eng.add_seed(make_bws_seed(PZB_UZYTKOWNIK, kUserRights, BWS_WYPISZ, 0x1001000ULL, 0, 0, 0));
    eng.add_seed(make_bws_seed(PZB_PIASKOWNICA, PRAWA_BRAK, BWS_RESET, 0, 0, 0, 0));
    eng.add_seed(make_bws_seed(PZB_UZYTKOWNIK, PRAWO_URUCHOM_PROGRAM, BWS_URUCHOM_PROGRAM, 0x1001000ULL, PZB_JADRO, PRAWA_ZNANE, 0));
    eng.add_seed(make_bws_seed(PZB_UZYTKOWNIK, PRAWO_SIEC, BWS_HTTP, 0x1001000ULL, 80, 0x1001100ULL, (0x1031000ULL << 32) | 4096ULL));
    eng.add_seed(make_bws_seed(PZB_ZAUFANE, PRAWA_ZNANE, BWS_ALOKUJ_STERTE, 0x10000ULL, 0, 0, 0));

    // Regressions (including Bug #1: BWS 99 + 0xDEADBEEFCAFEULL)
    eng.add_regression(make_bws_seed(PZB_PIASKOWNICA, PRAWA_BRAK, 99ULL, 0xDEADBEEFCAFEULL, 0, 0, 0));
    eng.add_regression(make_bws_seed(PZB_UZYTKOWNIK, kUserRights, BWS_CZYTAJ_PLIK, 0x1001000ULL, GUARD_STOSU_USER, 4096ULL, 0));

    // Deterministic matrix check across all PZB levels (0..7) x BWS IDs (0..100)
    SymulowanyProcesBws p{};
    p.tekst_start = 0x1001000ULL;
    p.tekst_koniec = 0x1031000ULL;
    p.dane_start = 0x1031000ULL;
    p.dane_koniec = 0x1051000ULL;
    p.granica_sterty = BAZA_STERTY_USER + 0x1000ULL;
    p.stos_start = BAZA_STOSU_USER;
    p.stos_koniec = BAZA_STOSU_USER + DOMYSLNY_ROZMIAR_STOSU_USER;
    for (uint8_t pzb = 0; pzb <= 7; ++pzb) {
        p.poziom_zaufania = pzb;
        p.uprawnienia = PRAWA_ZNANE;
        assert(wykonaj_fuzz_bws_jeden(p, 99ULL, 0xDEADBEEFCAFEULL, 0, 0, 0, nullptr, 0) == UINT64_MAX);
        // Regresja Bug #6: BWS_ROZMIAR_PLIKU dla sciezki z ".." musi zwrocic 0 (blad), a nie 1ULL!
        assert(wykonaj_fuzz_bws_jeden(p, BWS_ROZMIAR_PLIKU, 0x1031000ULL, 0, 0, 0,
                                      reinterpret_cast<const uint8_t*>("/../../cytaty.txt\0"), 18) == 0ULL);
        for (uint64_t id = 0; id <= 100; ++id) {
            (void)wykonaj_fuzz_bws_jeden(p, id, 0x1031000ULL, 0x1032000ULL, 64, 0,
                                         reinterpret_cast<const uint8_t*>("/plik\0"), 6);
        }
    }

    return eng.run(fuzz_bws_target);
}
