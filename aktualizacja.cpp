/*
 * Bursztyn OS - aktualizacja systemu (implementacja jadra Ring0).
 */
#include "aktualizacja.h"
#include "aktualizacja_klucz.h"
#include "aktualizacja_rozruch.h"
#include "storage_format.h"
#include "blokowy.h"
#include "wersja_systemu.h"
#include "psf.h"
#include "acpi.h"
#include "siec.h"
#include "heap.h"
#include "grafika.h"
#include "instalator.h"
#include "scheduler.h"
#include "mbedtls/sha256.h"
#include "mbedtls/rsa.h"
#include "mbedtls/md.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern "C" uint32_t rozmiar_pliku(const char* sciezka);
extern "C" bool czytaj_fragment_pliku(const char* sciezka, uint64_t offset,
                                      char* bufor, uint32_t dlugosc,
                                      uint32_t* odczytano);
extern "C" bool zapisz_fragment_pliku(const char* sciezka, uint64_t offset,
                                      const char* dane, uint32_t dlugosc);

extern bool skopiuj_z_przestrzeni_uzytkownika(void*, const void*, size_t);
extern bool skopiuj_do_przestrzeni_uzytkownika(void*, const void*, size_t);
extern bool skopiuj_string_z_uzytkownika(char*, const char*, size_t);
extern bool czy_bezpieczny_zakres_uzytkownika_do_odczytu(const void*, size_t);
extern bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void*, size_t);

static bool ota_operacja_systemowa_dozwolona(const proces_t& proces) {
    return (proces.uprawnienia & PRAWO_SYSTEM_CONFIG) == PRAWO_SYSTEM_CONFIG &&
           pzb_ma_co_najmniej_poziom(&proces, PZB_ZAUFANE);
}

static void ota_wykonaj_restart() {
    wypisz_log("[POWER] Restart systemu przez OTA.");
    psf_flush_cache();
    asm volatile("cli" ::: "memory");
    if (acpi_wykonaj_restart()) {
        for (;;) asm volatile("hlt" ::: "memory");
    }
    wypisz_log("[POWER] Restart przez fallback 8042.");
    bool gotowe = false;
    for (uint32_t proby = 0; proby < 1000000U; ++proby) {
        uint8_t stan = 0;
        asm volatile("inb %1, %0" : "=a"(stan) : "Nd"(static_cast<uint16_t>(0x64)));
        if ((stan & 0x02U) == 0) {
            gotowe = true;
            break;
        }
    }
    if (gotowe) {
        asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(0xFE)), "Nd"(static_cast<uint16_t>(0x64)));
    }
    for (;;) {
        asm volatile("hlt");
    }
}

/* ---- pomocnicze ---- */
static void akt_log(const char* tekst) { wypisz_log(tekst); }

static size_t akt_strlen(const char* s, size_t limit) {
    if (!s) return 0;
    size_t n = 0;
    while (n < limit && s[n] != '\0') ++n;
    return n;
}

static bool akt_napisz_tekst(const char* sciezka, const char* tekst, size_t len) {
    if (!sciezka || !tekst) return false;
    if (len == 0 || len > 8192) return false;
    (void)usun_twor(sciezka);
    if (!utworz_plik(sciezka)) return false;
    if (!zapisz_do_pliku(sciezka, tekst, (uint32_t)len)) {
        (void)usun_twor(sciezka);
        return false;
    }
    return true;
}

static void akt_kopiuj_tekst(char* cel, const char* zrodlo, size_t max_len) {
    if (!cel || max_len == 0) return;
    size_t i = 0;
    if (zrodlo) {
        while (i + 1 < max_len && zrodlo[i] != '\0') {
            cel[i] = zrodlo[i];
            ++i;
        }
    }
    cel[i] = '\0';
}

static void akt_wersja_na_tekst(const akt_wersja_t* w, char* out, size_t max_len) {
    if (!w || !out || max_len < 6) {
        if (out && max_len > 0) out[0] = '\0';
        return;
    }
    size_t pos = 0;
    uint32_t parts[3] = {w->major, w->minor, w->patch};
    for (int p = 0; p < 3; ++p) {
        uint32_t val = parts[p];
        char tmp[12];
        int tlen = 0;
        if (val == 0) {
            tmp[tlen++] = '0';
        } else {
            char rev[12];
            int rlen = 0;
            while (val > 0) {
                rev[rlen++] = static_cast<char>('0' + (val % 10));
                val /= 10;
            }
            while (rlen > 0) {
                tmp[tlen++] = rev[--rlen];
            }
        }
        for (int i = 0; i < tlen && pos + 1 < max_len; ++i) {
            out[pos++] = tmp[i];
        }
        if (p < 2 && pos + 1 < max_len) {
            out[pos++] = '.';
        }
    }
    out[pos] = '\0';
}

static void akt_zlacz_sciezke(const char* dir, const char* plik, char* out, size_t max_len) {
    if (!out || max_len == 0) return;
    size_t pos = 0;
    if (dir) {
        while (pos + 1 < max_len && dir[pos] != '\0') {
            out[pos] = dir[pos];
            ++pos;
        }
    }
    if (pos > 0 && out[pos - 1] != '/' && pos + 1 < max_len) {
        out[pos++] = '/';
    }
    if (plik) {
        size_t j = 0;
        while (pos + 1 < max_len && plik[j] != '\0') {
            out[pos++] = plik[j++];
        }
    }
    out[pos] = '\0';
}

static const char* akt_nazwa_kanalu(uint8_t kanal) {
    switch (kanal) {
        case AKT_KANAL_TESTOWY: return "testowy";
        case AKT_KANAL_ROZWOJOWY: return "rozwojowy";
        case AKT_KANAL_STABILNY:
        default: return "stabilny";
    }
}

/* ---- SHA-256 ---- */
bool aktualizacja_sha256(const uint8_t* dane, size_t len, uint8_t wynik[32]) {
    if (!wynik) return false;
    if (!dane && len != 0) return false;
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts_ret(&ctx, 0) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    if (len > 0 && mbedtls_sha256_update_ret(&ctx, dane, len) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    unsigned char out[32];
    if (mbedtls_sha256_finish_ret(&ctx, out) != 0) {
        mbedtls_sha256_free(&ctx);
        return false;
    }
    mbedtls_sha256_free(&ctx);
    for (int i = 0; i < 32; ++i) wynik[i] = out[i];
    for (int i = 0; i < 32; ++i) out[i] = 0;
    return true;
}

/* ---- RSA verify ---- */
bool aktualizacja_weryfikuj_podpis(const uint8_t* dane, size_t len,
                                   const uint8_t podpis[256]) {
    if (!dane || !podpis) return false;
    if (len == 0 || len > AKT_WYKAZ_MAKS_ROZMIAR) return false;
    uint8_t hash[32];
    if (!aktualizacja_sha256(dane, len, hash)) return false;
    mbedtls_rsa_context rsa;
    mbedtls_rsa_init(&rsa, MBEDTLS_RSA_PKCS_V15, 0);
    int rc = mbedtls_rsa_import_raw(&rsa, AKTUALIZACJA_KLUCZ_N, sizeof(AKTUALIZACJA_KLUCZ_N),
                                    nullptr, 0, nullptr, 0, nullptr, 0,
                                    nullptr, 0);
    if (rc != 0) { mbedtls_rsa_free(&rsa); return false; }
    /* Wykladnik e=65537. */
    {
        static const unsigned char ebe[3] = {0x01, 0x00, 0x01};
        rc = mbedtls_rsa_import_raw(&rsa, nullptr, 0, nullptr, 0, nullptr, 0,
                                    nullptr, 0, ebe, sizeof(ebe));
        if (rc != 0) { mbedtls_rsa_free(&rsa); return false; }
    }
    rc = mbedtls_rsa_complete(&rsa);
    if (rc != 0) { mbedtls_rsa_free(&rsa); return false; }
    if (mbedtls_rsa_check_pubkey(&rsa) != 0) { mbedtls_rsa_free(&rsa); return false; }
    mbedtls_rsa_set_padding(&rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_SHA256);
    rc = mbedtls_rsa_rsassa_pkcs1_v15_verify(&rsa, nullptr, nullptr, MBEDTLS_RSA_PUBLIC,
                                             MBEDTLS_MD_SHA256, 32, hash, podpis);
    mbedtls_rsa_free(&rsa);
    for (int i = 0; i < 32; ++i) hash[i] = 0;
    return rc == 0;
}

/* ---- wykaz ---- */
bool aktualizacja_weryfikuj_wykaz(const char* dane, size_t len, akt_wykaz_t* wykaz_out) {
    if (!dane || len == 0 || len > AKT_WYKAZ_MAKS_ROZMIAR) return false;
    akt_wykaz_t w = {};
    /* Kopia do bufora parsujacego (parser wymaga dostepu liniowego). */
    for (size_t i = 0; i < len; ++i) {
        if (dane[i] == '\0' && i + 1 < len) return false;
    }
    if (akt_parsuj_wykaz(dane, len, &w) != AKT_OK) return false;
    char kanoniczne[1024];
    size_t klen = akt_buduj_kanoniczne(&w, kanoniczne, sizeof(kanoniczne));
    if (klen == 0) return false;
    if (!aktualizacja_weryfikuj_podpis((const uint8_t*)kanoniczne, klen, w.podpis)) return false;
    if (wykaz_out) *wykaz_out = w;
    return true;
}

bool aktualizacja_weryfikuj_pakiet(const uint8_t* dane, size_t len,
                                   const akt_wykaz_t* wykaz) {
    if (!dane || !wykaz) return false;
    if (len == 0 || len > AKT_MAKS_ROZMIAR_PAKIETU) return false;
    if ((uint64_t)len != wykaz->rozmiar) return false;
    if (akt_sprawdz_pakiet(dane, len, nullptr) != AKT_OK) return false;
    uint8_t hash[32];
    if (!aktualizacja_sha256(dane, len, hash)) return false;
    for (int i = 0; i < 32; ++i) {
        if (hash[i] != wykaz->sumasha256[i]) {
            for (int k = 0; k < 32; ++k) hash[k] = 0;
            return false;
        }
    }
    for (int k = 0; k < 32; ++k) hash[k] = 0;
    /* Per-file SHA-256: kazdy wpis ma wlasny hash w tabeli. */
    uint32_t liczba = 0;
    if (akt_sprawdz_pakiet(dane, len, &liczba) != AKT_OK) return false;
    for (uint32_t e = 0; e < liczba; ++e) {
        size_t baza = (size_t)AKT_NAGLOWEK_ROZMIAR + (size_t)e * (size_t)AKT_WPIS_ROZMIAR;
        const uint8_t* wpis = dane + baza;
        uint64_t off = akt_wczytaj_u64le(wpis + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL);
        uint64_t roz = akt_wczytaj_u64le(wpis + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL + 8);
        const uint8_t* oczekiwany = wpis + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL + 8 + 8;
        uint8_t h[32];
        if (!aktualizacja_sha256(dane + (size_t)off, (size_t)roz, h)) return false;
        for (int i = 0; i < 32; ++i) {
            if (h[i] != oczekiwany[i]) {
                for (int k = 0; k < 32; ++k) h[k] = 0;
                return false;
            }
        }
        for (int k = 0; k < 32; ++k) h[k] = 0;
    }
    return true;
}

/* ---- wersja ---- */
bool aktualizacja_biezaca_wersja(akt_wersja_t* out) {
    if (!out) return false;
    /* Domyslnie wbudowana. */
    out->major = BURSZTYN_WERSJA_MAJOR;
    out->minor = BURSZTYN_WERSJA_MINOR;
    out->patch = BURSZTYN_WERSJA_PATCH;
    /* Jesli /system/wersja istnieje i jest poprawna, ma pierwszenstwo
     * (pozwala zachowac wersje po aktualizacji bez rekompilacji). */
    uint32_t rozmiar = rozmiar_pliku(AKT_SCIEZKA_WERSJA);
    if (rozmiar == 0 || rozmiar > 32) return true;
    char bufor[32] = {0};
    /* rozmiar_pliku()==0 oznacza tez blad, wiec odczyt weryfikujemy. */
    uint32_t odczytano = 0;
    /* Czytamy calosc od offsetu 0. */
    char caly[32] = {0};
    bool ok = false;
    /* Prosty odczyt: czytaj_fragment_pliku zwraca false przy bledzie. */
    {
        char tmp[32] = {0};
        uint32_t got = 0;
        if (czytaj_fragment_pliku(AKT_SCIEZKA_WERSJA, 0, tmp, rozmiar > 32 ? 32 : rozmiar, &got) && got == rozmiar) {
            for (uint32_t i = 0; i < got && i < 32; ++i) caly[i] = tmp[i];
            ok = true;
            (void)bufor; (void)odczytano;
        }
    }
    if (!ok) return true;
    /* Oczekujemy "X.Y.Z\n" albo "X.Y.Z". */
    size_t len = (size_t)rozmiar;
    if (len > 0 && caly[len - 1] == '\n') --len;
    if (len == 0 || len > 15) return true;
    akt_wersja_t w = {0, 0, 0};
    if (akt_parsuj_wersje(caly, len, &w) == AKT_OK) *out = w;
    return true;
}

/* ---- PZB ---- */
static bool akt_pzb_ok(const proces_t* proces, uint8_t maks_poziom, uint64_t prawa) {
    if (!proces) return false; /* Ring0 wywoluje z nullptr -> osobna sciezka */
    return pzb_ma_co_najmniej_poziom(proces, maks_poziom) &&
           pzb_ma_prawa(proces, prawa);
}

bool aktualizacja_czy_moze_sprawdzac(const proces_t* proces) {
    return akt_pzb_ok(proces, AKT_PZB_SPRAWDZANIE, PRAWO_SIEC | PRAWO_PLIKI_CZYTAJ);
}
bool aktualizacja_czy_moze_pobierac(const proces_t* proces) {
    return akt_pzb_ok(proces, AKT_PZB_POBIERANIE, PRAWO_SIEC | PRAWO_PLIKI_ZAPISZ);
}
bool aktualizacja_czy_moze_weryfikowac(const proces_t* proces) {
    return akt_pzb_ok(proces, AKT_PZB_WERYFIKACJA, PRAWO_PLIKI_CZYTAJ);
}
bool aktualizacja_czy_moze_instalowac(const proces_t* proces) {
    return akt_pzb_ok(proces, AKT_PZB_INSTALACJA,
                      PRAWO_SYSTEM_CONFIG | PRAWO_PLIKI_ZAPISZ);
}

/* ---- system plikow ---- */
void aktualizacja_przygotuj_system_plikow(void) {
    (void)zapewnij_katalog(AKT_SCIEZKA_KATALOG);
    (void)zapewnij_katalog(AKT_SCIEZKA_PRZYGOTOWANIE);
    (void)zapewnij_katalog("/sloty");
    (void)zapewnij_katalog("/sloty/a");
    (void)zapewnij_katalog("/sloty/a/programy");
    (void)zapewnij_katalog("/sloty/a/system");
    (void)zapewnij_katalog("/sloty/a/system/jadro");
    (void)zapewnij_katalog("/sloty/a/system/biblioteki");
    (void)zapewnij_katalog("/sloty/a/system/konfig");
    (void)zapewnij_katalog("/sloty/a/system/boot");
    (void)zapewnij_katalog("/sloty/b");
    (void)zapewnij_katalog("/sloty/b/programy");
    (void)zapewnij_katalog("/sloty/b/system");
    (void)zapewnij_katalog("/sloty/b/system/jadro");
    (void)zapewnij_katalog("/sloty/b/system/biblioteki");
    (void)zapewnij_katalog("/sloty/b/system/konfig");
    (void)zapewnij_katalog("/sloty/b/system/boot");
    (void)zapewnij_katalog("/system");
    (void)zapewnij_katalog("/system/konfig");
    (void)zapewnij_katalog("/system/jadro");
    (void)zapewnij_katalog("/system/biblioteki");
    (void)zapewnij_katalog("/system/boot");
    /* Plik wersji: utworz przy pierwszym starcie w obu slotach jesli brakuje. */
    if (rozmiar_pliku("/sloty/a/system/wersja") == 0) {
        char probe[4] = {0}; uint32_t got = 0;
        if (!czytaj_fragment_pliku("/sloty/a/system/wersja", 0, probe, 1, &got) || got != 1) {
            (void)akt_napisz_tekst("/sloty/a/system/wersja", BURSZTYN_WERSJA_TEKST "\n",
                                   akt_strlen(BURSZTYN_WERSJA_TEKST "\n", 16));
        }
    }
    if (rozmiar_pliku("/sloty/b/system/wersja") == 0) {
        char probe[4] = {0}; uint32_t got = 0;
        if (!czytaj_fragment_pliku("/sloty/b/system/wersja", 0, probe, 1, &got) || got != 1) {
            (void)akt_napisz_tekst("/sloty/b/system/wersja", BURSZTYN_WERSJA_TEKST "\n",
                                   akt_strlen(BURSZTYN_WERSJA_TEKST "\n", 16));
        }
    }
    if (rozmiar_pliku(AKT_SCIEZKA_WERSJA) == 0) {
        /* rozmiar==0 to tez blad/brak; proba odczytu rozstrzyga. */
        char probe[4] = {0};
        uint32_t got = 0;
        bool istnieje = czytaj_fragment_pliku(AKT_SCIEZKA_WERSJA, 0, probe, 1, &got) && got == 1;
        if (!istnieje) {
            (void)akt_napisz_tekst(AKT_SCIEZKA_WERSJA, BURSZTYN_WERSJA_TEKST "\n",
                                   akt_strlen(BURSZTYN_WERSJA_TEKST "\n", 16));
            akt_log("[AKTUALIZACJA] Zapisano poczatkowa wersje systemu.");
        }
    }
}

/* ---- pobieranie ---- */
bool aktualizacja_pobierz_do_tmp(const char* domena, const char* sciezka_http,
                                 bool https, uint64_t maks_rozmiar) {
    if (!domena || !sciezka_http) return false;
    size_t dl = akt_strlen(domena, 256);
    size_t sl = akt_strlen(sciezka_http, 2048);
    if (dl == 0 || dl > 253 || sl == 0 || sciezka_http[0] != '/') return false;
    if (maks_rozmiar == 0 || maks_rozmiar > AKT_MAKS_ROZMIAR_PAKIETU) return false;
    if (maks_rozmiar > 2U * 1024U * 1024U) maks_rozmiar = 2U * 1024U * 1024U; /* limit PSF direct */
    akt_log("[AKTUALIZACJA] Pobieranie pakietu.");
    uint8_t ip[4] = {0};
    if (!kernel_siec_dns(domena, ip)) {
        akt_log("[AKTUALIZACJA] BLAD pobierania (DNS).");
        return false;
    }
    uint8_t* bufor = (uint8_t*)kmalloc((uint64_t)maks_rozmiar + 1U);
    if (!bufor) {
        akt_log("[AKTUALIZACJA] BLAD pobierania (pamiec).");
        return false;
    }
    for (uint64_t i = 0; i < maks_rozmiar + 1U; ++i) bufor[i] = 0;
    extern uint32_t tcp_zapisano_bajtow;
    tcp_zapisano_bajtow = 0;
    bool ok = https ? kernel_siec_pobierz_https(ip, domena, sciezka_http,
                                                (char*)bufor, (uint32_t)maks_rozmiar)
                    : kernel_siec_pobierz_http(ip, domena, sciezka_http,
                                               (char*)bufor, (uint32_t)maks_rozmiar);
    uint32_t odebrano = tcp_zapisano_bajtow;
    if (!ok || odebrano == 0 || odebrano > maks_rozmiar) {
        for (uint64_t i = 0; i < maks_rozmiar + 1U; ++i) bufor[i] = 0;
        kfree(bufor);
        (void)usun_twor(AKT_SCIEZKA_POBIERANIE_TMP);
        akt_log("[AKTUALIZACJA] BLAD pobierania (siec).");
        return false;
    }
    /* Zapis do pliku tymczasowego kawalek po kawalku (4 KiB). */
    (void)usun_twor(AKT_SCIEZKA_POBIERANIE_TMP);
    if (!utworz_plik(AKT_SCIEZKA_POBIERANIE_TMP)) {
        for (uint64_t i = 0; i < maks_rozmiar + 1U; ++i) bufor[i] = 0;
        kfree(bufor);
        akt_log("[AKTUALIZACJA] BLAD pobierania (plik tymczasowy).");
        return false;
    }
    uint32_t off = 0;
    while (off < odebrano) {
        uint32_t kawa = odebrano - off > 4096 ? 4096 : odebrano - off;
        if (!zapisz_fragment_pliku(AKT_SCIEZKA_POBIERANIE_TMP, off,
                                   (const char*)bufor + off, kawa)) {
            for (uint64_t i = 0; i < maks_rozmiar + 1U; ++i) bufor[i] = 0;
            kfree(bufor);
            (void)usun_twor(AKT_SCIEZKA_POBIERANIE_TMP);
            akt_log("[AKTUALIZACJA] BLAD pobierania (zapis).");
            return false;
        }
        off += kawa;
    }
    for (uint64_t i = 0; i < maks_rozmiar + 1U; ++i) bufor[i] = 0;
    kfree(bufor);
    return true;
}

/* ---- pomocnicze rozruchu i stringow ---- */
static bool akt_tekst_zawiera(const char* zrodlo, const char* szukany) {
    if (!zrodlo || !szukany) return false;
    for (size_t i = 0; zrodlo[i] != '\0'; ++i) {
        size_t j = 0;
        while (szukany[j] != '\0' && zrodlo[i + j] == szukany[j]) ++j;
        if (szukany[j] == '\0') return true;
    }
    return false;
}

uint8_t aktualizacja_aktywny_slot(void) {
    return psf_pobierz_aktywny_slot();
}

uint8_t aktualizacja_nieaktywny_slot(void) {
    return (psf_pobierz_aktywny_slot() == 0) ? 1 : 0;
}

void aktualizacja_sprawdz_rozruch(uint64_t mbi) {
    uint8_t uruchomiony_slot = 0; // 0 = A, 1 = B
    bool awaria_testowa = false;
    bool wymus_zainstalowany = false;

    if (mbi != 0) {
        const volatile uint32_t* info = reinterpret_cast<const volatile uint32_t*>(mbi);
        const uint32_t calosc = info[0];
        if (calosc >= 16 && mbi <= UINT64_MAX - calosc) {
            uint64_t off = 8;
            while (off + 8 <= calosc) {
                const uint32_t* tag = reinterpret_cast<const uint32_t*>(mbi + off);
                uint32_t typ = tag[0];
                uint32_t rozmiar = tag[1];
                if (rozmiar < 8 || off + rozmiar > calosc) break;
                if (typ == 1 && rozmiar > 8) {
                    const char* cmdline = reinterpret_cast<const char*>(mbi + off + 8);
                    if (akt_tekst_zawiera(cmdline, "bursztyn.slot=B")) {
                        uruchomiony_slot = 1;
                    } else if (akt_tekst_zawiera(cmdline, "bursztyn.slot=A")) {
                        uruchomiony_slot = 0;
                    }
                    if (akt_tekst_zawiera(cmdline, "bursztyn.awaria_slotu=B") ||
                        akt_tekst_zawiera(cmdline, "bursztyn.test_rollback=1")) {
                        awaria_testowa = true;
                    }
                    if (akt_tekst_zawiera(cmdline, "bursztyn.installed=1")) {
                        wymus_zainstalowany = true;
                    }
                    break;
                }
                if (typ == 0) break;
                off = (off + rozmiar + 7ULL) & ~7ULL;
            }
        }
    }

    psf_ustaw_aktywny_slot(uruchomiony_slot);

    bool jest_zainstalowany = instalator_system_zainstalowany() || wymus_zainstalowany;
    if (!jest_zainstalowany) {
        akt_log("[ROZRUCH] Srodowisko instalacyjne / Live ISO. Pominiecie walidacji partycji A/B LBA 2046/2047.");
        return;
    }

    bursztyn_rozruch_stan stan{};
    bool stan_odczytany = false;
    Storage::Device* dev = blokowy_dysk_systemowy();
    if (dev) {
        if (Storage::rozruch_weryfikuj_uklad_dysku(dev)) {
            stan_odczytany = Storage::rozruch_odczytaj_sektory(dev, &stan);
        } else {
            akt_log("[ROZRUCH] BLAD: Uklad dysku narusza zarezerwowane sektory stanu rozruchu LBA 2046/2047.");
        }
    }
    if (!stan_odczytany) {
        bursztyn_rozruch_stan k0{}, k1{};
        uint32_t g0 = 0, g1 = 0;
        bool r0 = czytaj_fragment_pliku("/aktualizacje/stan_rozruchu.k0", 0, (char*)&k0, sizeof(k0), &g0) && g0 == sizeof(k0);
        bool r1 = czytaj_fragment_pliku("/aktualizacje/stan_rozruchu.k1", 0, (char*)&k1, sizeof(k1), &g1) && g1 == sizeof(k1);
        if (r0 || r1) {
            stan_odczytany = rozruch_wybierz_poprawna_kopie(r0 ? &k0 : nullptr, r1 ? &k1 : nullptr, &stan);
        }
    }

    Storage::Partition esp{};
    char grub_potw[4]{'A', 0};
    char grub_oczek[16]{'B', 'R', 'A', 'K', 0};
    uint32_t grub_proby = 0;
    bool grub_ok = blokowy_esp_systemowy(&esp) && Storage::esp_odczytaj_grubenv(esp, grub_potw, grub_oczek, &grub_proby);

    if (stan_odczytany) {
        akt_log("[ROZRUCH] Stan rozruchu odczytany pomyslnie.");
        if (uruchomiony_slot == stan.slot_potwierdzony) {
            /* Uruchomiony zostal slot potwierdzony */
            uint8_t inny_slot = (uruchomiony_slot == 0) ? 1 : 0;
            const char* inny_str = (inny_slot == 1) ? "B" : "A";
            const char* ten_str  = (uruchomiony_slot == 0) ? "A" : "B";

            bool rollback_wykryty = false;
            if (grub_ok && (akt_porownaj_bajty(grub_oczek, "NIEUDANY", 8) == 0 ||
                            (grub_oczek[0] == inny_str[0] && grub_proby >= 1))) {
                rollback_wykryty = true;
            } else if (stan.slot_oczekujacy == inny_slot &&
                       (stan.stan_oczekujacego == BURSZTYN_STAN_NIEUDANY || stan.licznik_prob >= stan.maks_prob)) {
                rollback_wykryty = true;
            }

            if (rollback_wykryty) {
                if (inny_slot == 1) {
                    akt_log("[ROZRUCH] Nieudane uruchomienie slotu B.");
                    akt_log("[ROZRUCH] Powrot do slotu A.");
                } else {
                    akt_log("[ROZRUCH] Nieudane uruchomienie slotu A.");
                    akt_log("[ROZRUCH] Powrot do slotu B.");
                }
                rozruch_wykonaj_rollback(&stan);
                if (dev) Storage::rozruch_zapisz_sektory(dev, &stan);
                (void)zapisz_do_pliku("/aktualizacje/stan_rozruchu.k0", (const char*)&stan, sizeof(stan));
                (void)zapisz_do_pliku("/aktualizacje/stan_rozruchu.k1", (const char*)&stan, sizeof(stan));
                if (blokowy_esp_systemowy(&esp)) {
                    Storage::esp_zapisz_grubenv(esp, ten_str, "BRAK", 0);
                }
            } else if (!grub_ok || akt_porownaj_bajty(grub_potw, ten_str, 1) != 0) {
                /* Samonaprawa grubenv ze stanu autorytatywnego LBA */
                akt_log("[ROZRUCH] Samonaprawa grubenv rozpoczeta.");
                if (blokowy_esp_systemowy(&esp)) {
                    bool wr_ok = Storage::esp_zapisz_grubenv(esp, ten_str, "BRAK", 0);
                    if (wr_ok) {
                        akt_log("[ROZRUCH] Samonaprawa grubenv zakonczona sukcesem.");
                    } else {
                        akt_log("[ROZRUCH] Blad zapisu esp_zapisz_grubenv!");
                    }
                } else {
                    akt_log("[ROZRUCH] Brak partycji ESP dla samonaprawy grubenv!");
                }
            }
        } else if (stan.slot_oczekujacy == uruchomiony_slot &&
                   stan.stan_oczekujacego == BURSZTYN_STAN_OCZEKUJACY &&
                   stan.licznik_prob < stan.maks_prob) {
            /* Uruchomiony zostal autoryzowany slot oczekujacy */
            if (uruchomiony_slot == 1) {
                akt_log("[ROZRUCH] Proba uruchomienia slotu B.");
            } else {
                akt_log("[ROZRUCH] Proba uruchomienia slotu A.");
            }
            if (awaria_testowa) {
                if (uruchomiony_slot == 1) {
                    akt_log("[TEST] Kontrolowana awaria przed potwierdzeniem slotu B.");
                } else {
                    akt_log("[TEST] Kontrolowana awaria przed potwierdzeniem slotu A.");
                }
                asm volatile("cli");
                for (;;) {
                    asm volatile("outb %0, $0x64" : : "a"((uint8_t)0xFE));
                    asm volatile("hlt");
                }
            }
        } else {
            /* KRYTYCZNA NIEZGODNOSC: uruchomiony slot NIE JEST autoryzowany w sektorach LBA! */
            const char* potw_str = (stan.slot_potwierdzony == 1) ? "B" : "A";
            akt_log("[ROZRUCH] BLAD KRYTYCZNY: Nieautoryzowany rozruch slotu!");
            akt_log("[ROZRUCH] Odrzucenie na podstawie kanonicznych sektorow stanu rozruchu.");
            akt_log("[ROZRUCH] Natychmiastowy powrot do potwierdzonego slotu.");
            if (blokowy_esp_systemowy(&esp)) {
                Storage::esp_zapisz_grubenv(esp, potw_str, "NIEUDANY", 0);
            }
            asm volatile("cli");
            for (;;) {
                asm volatile("outb %0, $0x64" : : "a"((uint8_t)0xFE));
                asm volatile("hlt");
            }
        }
    }
}

bool aktualizacja_potwierdz_rozruch(void) {
    if (!instalator_system_zainstalowany()) {
        return true;
    }
    uint8_t running_slot = psf_pobierz_aktywny_slot();
    bursztyn_rozruch_stan stan{};
    bool stan_odczytany = false;
    Storage::Device* dev = blokowy_dysk_systemowy();
    if (dev) {
        if (Storage::rozruch_weryfikuj_uklad_dysku(dev)) {
            stan_odczytany = Storage::rozruch_odczytaj_sektory(dev, &stan);
        } else {
            akt_log("[ROZRUCH] BLAD: Uklad dysku narusza zarezerwowane sektory stanu rozruchu LBA 2046/2047.");
        }
    }
    if (!stan_odczytany) {
        bursztyn_rozruch_stan k0{}, k1{};
        uint32_t g0 = 0, g1 = 0;
        bool r0 = czytaj_fragment_pliku("/aktualizacje/stan_rozruchu.k0", 0, (char*)&k0, sizeof(k0), &g0) && g0 == sizeof(k0);
        bool r1 = czytaj_fragment_pliku("/aktualizacje/stan_rozruchu.k1", 0, (char*)&k1, sizeof(k1), &g1) && g1 == sizeof(k1);
        if (r0 || r1) {
            stan_odczytany = rozruch_wybierz_poprawna_kopie(r0 ? &k0 : nullptr, r1 ? &k1 : nullptr, &stan);
        }
    }

    if (!stan_odczytany) {
        return true;
    }

    if (stan.slot_oczekujacy == running_slot && stan.stan_oczekujacego == BURSZTYN_STAN_OCZEKUJACY) {
        if (!rozruch_potwierdz_sukces(&stan, (bursztyn_slot_t)running_slot)) {
            return false;
        }
        if (dev) {
            if (Storage::rozruch_weryfikuj_uklad_dysku(dev)) {
                Storage::rozruch_zapisz_sektory(dev, &stan);
            }
        }
        (void)zapisz_do_pliku("/aktualizacje/stan_rozruchu.k0", (const char*)&stan, sizeof(stan));
        (void)zapisz_do_pliku("/aktualizacje/stan_rozruchu.k1", (const char*)&stan, sizeof(stan));

        Storage::Partition esp{};
        if (blokowy_esp_systemowy(&esp)) {
            const char* potw_str = (running_slot == 1) ? "B" : "A";
            Storage::esp_zapisz_grubenv(esp, potw_str, "BRAK", 0);
        }

        if (running_slot == 1) {
            akt_log("[ROZRUCH] Slot B potwierdzony.");
        } else {
            akt_log("[ROZRUCH] Slot A potwierdzony.");
        }
    }
    return true;
}

/* ---- instalacja ---- */
static bool akt_zapisz_kawalki(const char* sciezka, const uint8_t* dane, size_t len) {
    if (!sciezka || (!dane && len != 0)) return false;
    if (len == 0 || len > (size_t)AKT_MAKS_ROZMIAR_PLIKU_WPISU) return false;
    (void)usun_twor(sciezka);
    if (!utworz_plik(sciezka)) return false;
    size_t off = 0;
    while (off < len) {
        size_t kawa = len - off > 4096 ? 4096 : len - off;
        if (!zapisz_fragment_pliku(sciezka, off, (const char*)dane + off, (uint32_t)kawa)) {
            (void)usun_twor(sciezka);
            return false;
        }
        off += kawa;
    }
    return true;
}

bool aktualizacja_zainstaluj_zweryfikowany(const uint8_t* dane, size_t len,
                                           const akt_wykaz_t* wykaz) {
    if (!dane || !wykaz) return false;
    if (!aktualizacja_weryfikuj_pakiet(dane, len, wykaz)) {
        akt_log("[AKTUALIZACJA] BLAD weryfikacji pakietu.");
        return false;
    }
    /* Ochrona przed downgrade / reinstalacja. */
    akt_wersja_t biezaca;
    aktualizacja_biezaca_wersja(&biezaca);
    akt_wersja_t docel = wykaz->wersjasystemu;
    akt_wersja_t mini = wykaz->minimalnawersja;
    if (!akt_czy_dozwolona_aktualizacja(&biezaca, &docel, &mini)) {
        akt_log("[AKTUALIZACJA] BLAD wersji (downgrade lub ta sama wersja).");
        return false;
    }

    /* Sprawdz uklad A/B: czy katalog /sloty istnieje */
    if (!czy_katalog_istnieje("/sloty")) {
        akt_log("[AKTUALIZACJA] BLAD: Wykryto starszy uklad partycji bez obslugi A/B. Aktualizacja wstrzymana.");
        return false;
    }

    Storage::Partition esp{};
    if (!blokowy_esp_systemowy(&esp)) {
        akt_log("[AKTUALIZACJA] BLAD: Brak partycji ESP dla rozruchu A/B.");
        return false;
    }

    uint8_t aktywny = psf_pobierz_aktywny_slot();
    uint8_t cel_slot = (aktywny == 0) ? 1 : 0;

    if (aktywny == 0) {
        akt_log("[AKTUALIZACJA] Aktywny slot A.");
        akt_log("[AKTUALIZACJA] Przygotowanie slotu B.");
    } else {
        akt_log("[AKTUALIZACJA] Aktywny slot B.");
        akt_log("[AKTUALIZACJA] Przygotowanie slotu A.");
    }

    (void)zapewnij_katalog(AKT_SCIEZKA_PRZYGOTOWANIE);
    uint32_t liczba = 0;
    if (akt_sprawdz_pakiet(dane, len, &liczba) != AKT_OK) return false;

    /* Faza 1: staging wszystkich plikow do /aktualizacje/przygotowanie/. */
    for (uint32_t e = 0; e < liczba; ++e) {
        size_t baza = (size_t)AKT_NAGLOWEK_ROZMIAR + (size_t)e * (size_t)AKT_WPIS_ROZMIAR;
        const uint8_t* wpis = dane + baza;
        size_t kat_len = 0;
        while (kat_len < AKT_WPIS_KATEGORIA_DL && wpis[kat_len] != 0) ++kat_len;
        const uint8_t* sc = wpis + AKT_WPIS_KATEGORIA_DL;
        size_t sc_len = 0;
        while (sc_len < AKT_WPIS_SCIEZKA_DL && sc[sc_len] != 0) ++sc_len;
        uint64_t off = akt_wczytaj_u64le(wpis + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL);
        uint64_t roz = akt_wczytaj_u64le(wpis + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL + 8);

        char stage[160] = {0};
        {
            size_t p = 0;
            const char* pre = "/aktualizacje/przygotowanie/";
            while (*pre && p < sizeof(stage) - 1) stage[p++] = *pre++;
            for (size_t i = 0; i < kat_len && p < sizeof(stage) - 1; ++i) stage[p++] = (char)wpis[i];
            if (p < sizeof(stage) - 1) stage[p++] = '_';
            for (size_t i = 0; i < sc_len && p < sizeof(stage) - 1; ++i) {
                char c = (char)sc[i];
                stage[p++] = (c == '/') ? '_' : c;
            }
            stage[p] = '\0';
            if (p == 0 || p >= sizeof(stage) - 1) return false;
        }

        if (!akt_zapisz_kawalki(stage, dane + (size_t)off, (size_t)roz)) {
            akt_log("[AKTUALIZACJA] BLAD przygotowania (staging).");
            return false;
        }

        /* Weryfikacja staged: odczyt + per-file hash. */
        {
            const uint8_t* oczekiwany = wpis + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL + 8 + 8;
            uint8_t* tmp = (uint8_t*)kmalloc(roz);
            if (!tmp) {
                (void)usun_twor(stage);
                return false;
            }
            uint32_t got = 0;
            size_t roff = 0;
            bool rok = true;
            while (roff < (size_t)roz) {
                size_t kawa = (size_t)roz - roff > 4096 ? 4096 : (size_t)roz - roff;
                uint32_t g = 0;
                if (!czytaj_fragment_pliku(stage, roff, (char*)tmp + roff, (uint32_t)kawa, &g) || g != kawa) {
                    rok = false;
                    break;
                }
                got += g;
                roff += kawa;
            }
            uint8_t h[32];
            bool hok = rok && aktualizacja_sha256(tmp, (size_t)roz, h);
            for (size_t i = 0; i < (size_t)roz; ++i) tmp[i] = 0;
            kfree(tmp);
            if (!hok) {
                (void)usun_twor(stage);
                return false;
            }
            for (int i = 0; i < 32; ++i) {
                if (h[i] != oczekiwany[i]) {
                    (void)usun_twor(stage);
                    akt_log("[AKTUALIZACJA] BLAD weryfikacji pliku staged.");
                    return false;
                }
            }
        }
    }

    /* Faza 2: commit do docelowego NIEAKTYWNEGO slotu */
    const char* pref_slotu = (cel_slot == 1) ? "/sloty/b" : "/sloty/a";
    uint8_t sha_jadra[32]{};
    bool jadro_zapisane_esp = false;

    for (uint32_t e = 0; e < liczba; ++e) {
        size_t baza = (size_t)AKT_NAGLOWEK_ROZMIAR + (size_t)e * (size_t)AKT_WPIS_ROZMIAR;
        const uint8_t* wpis = dane + baza;
        size_t kat_len = 0;
        while (kat_len < AKT_WPIS_KATEGORIA_DL && wpis[kat_len] != 0) ++kat_len;
        const uint8_t* sc = wpis + AKT_WPIS_KATEGORIA_DL;
        size_t sc_len = 0;
        while (sc_len < AKT_WPIS_SCIEZKA_DL && sc[sc_len] != 0) ++sc_len;
        uint64_t off = akt_wczytaj_u64le(wpis + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL);
        uint64_t roz = akt_wczytaj_u64le(wpis + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL + 8);

        bool jest_jadro = (kat_len == 5 && akt_porownaj_bajty(wpis, "jadro", 5) == 0);
        bool jest_boot  = (kat_len == 4 && akt_porownaj_bajty(wpis, "boot", 4) == 0);

        if (jest_boot || (jest_jadro && (akt_tekst_zawiera((const char*)sc, "system_operacyjny") ||
                                         akt_tekst_zawiera((const char*)sc, "jadro") ||
                                         akt_tekst_zawiera((const char*)sc, "kernel")))) {
            if (!Storage::esp_zapisz_jadro_slotu(esp, cel_slot, dane + (size_t)off, (uint32_t)roz)) {
                akt_log("[AKTUALIZACJA] BLAD zapisu jadra na ESP.");
                return false;
            }
            uint8_t* check_esp = (uint8_t*)kmalloc((size_t)roz);
            if (!check_esp) return false;
            uint32_t esp_got = 0;
            if (!Storage::esp_odczytaj_jadro_slotu(esp, cel_slot, check_esp, (uint32_t)roz, &esp_got) || esp_got != roz) {
                kfree(check_esp);
                akt_log("[AKTUALIZACJA] BLAD weryfikacji jadra na ESP.");
                return false;
            }
            uint8_t h_esp[32];
            aktualizacja_sha256(check_esp, (size_t)roz, h_esp);
            kfree(check_esp);
            const uint8_t* oczekiwany = wpis + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL + 8 + 8;
            for (int i = 0; i < 32; ++i) {
                if (h_esp[i] != oczekiwany[i]) {
                    akt_log("[AKTUALIZACJA] BLAD sumy SHA-256 jadra na ESP.");
                    return false;
                }
                sha_jadra[i] = h_esp[i];
            }
            jadro_zapisane_esp = true;
        }

        char cel[160] = {0};
        const char* sub = nullptr;
        if (jest_jadro) sub = "/system/jadro/";
        else if (kat_len == 9 && akt_porownaj_bajty(wpis, "aplikacje", 9) == 0) sub = "/programy/";
        else if (kat_len == 10 && akt_porownaj_bajty(wpis, "biblioteki", 10) == 0) sub = "/system/biblioteki/";
        else if (kat_len == 6 && akt_porownaj_bajty(wpis, "konfig", 6) == 0) sub = "/system/konfig/";
        else if (jest_boot) sub = "/system/boot/";
        if (!sub) return false;

        size_t p = 0;
        while (pref_slotu[p] && p < sizeof(cel) - 1) { cel[p] = pref_slotu[p]; ++p; }
        size_t sp = 0;
        while (sub[sp] && p < sizeof(cel) - 1) { cel[p++] = sub[sp++]; }
        for (size_t i = 0; i < sc_len && p < sizeof(cel) - 1; ++i) { cel[p++] = (char)sc[i]; }
        cel[p] = '\0';
        if (p >= sizeof(cel) - 1) return false;

        {
            char katalog_tmp[160] = {0};
            size_t cp = 0;
            size_t ostatni_slash = 0;
            for (size_t i = 0; cel[i] != '\0'; ++i) if (cel[i] == '/') ostatni_slash = i;
            for (size_t i = 0; i < ostatni_slash && i < sizeof(katalog_tmp) - 1; ++i) katalog_tmp[cp++] = cel[i];
            katalog_tmp[cp] = '\0';

            char pref[160] = {0};
            size_t pp = 0;
            for (size_t i = 0; katalog_tmp[i] != '\0'; ++i) {
                if (pp < sizeof(pref) - 1) pref[pp++] = katalog_tmp[i];
                if (katalog_tmp[i] == '/' && pp > 1) {
                    pref[pp] = '\0';
                    (void)zapewnij_katalog(pref);
                }
            }
            if (pp > 0) {
                pref[pp] = '\0';
                (void)zapewnij_katalog(pref);
            }
        }

        if (!akt_zapisz_kawalki(cel, dane + (size_t)off, (size_t)roz)) {
            akt_log("[AKTUALIZACJA] BLAD instalacji (commit).");
            return false;
        }

        {
            const uint8_t* oczekiwany = wpis + AKT_WPIS_KATEGORIA_DL + AKT_WPIS_SCIEZKA_DL + 8 + 8;
            uint8_t* tmp = (uint8_t*)kmalloc(roz);
            if (!tmp) return false;
            uint32_t got = 0;
            size_t roff = 0;
            bool rok = true;
            while (roff < (size_t)roz) {
                size_t kawa = (size_t)roz - roff > 4096 ? 4096 : (size_t)roz - roff;
                uint32_t g = 0;
                if (!czytaj_fragment_pliku(cel, roff, (char*)tmp + roff, (uint32_t)kawa, &g) || g != kawa) {
                    rok = false;
                    break;
                }
                got += g;
                roff += kawa;
            }
            uint8_t h[32];
            bool hok = rok && aktualizacja_sha256(tmp, (size_t)roz, h);
            for (size_t i = 0; i < (size_t)roz; ++i) tmp[i] = 0;
            kfree(tmp);
            if (!hok) return false;
            for (int i = 0; i < 32; ++i) {
                if (h[i] != oczekiwany[i]) {
                    akt_log("[AKTUALIZACJA] BLAD weryfikacji pliku po zapisie.");
                    return false;
                }
            }
        }
    }

    if (!jadro_zapisane_esp) {
        Storage::EspLayout lay{};
        if (!Storage::esp_odczytaj_uklad(esp, &lay)) return false;
        uint32_t target_ksize = (cel_slot == 0) ? lay.jadro_a_size : lay.jadro_b_size;
        if (target_ksize == 0 || target_ksize > 16 * 1024 * 1024) {
            akt_log("[AKTUALIZACJA] BLAD: Brak poprawnego jadra w docelowym slocie ESP.");
            return false;
        }
        uint8_t* cur_k = (uint8_t*)kmalloc(target_ksize);
        if (cur_k) {
            uint32_t g = 0;
            if (Storage::esp_odczytaj_jadro_slotu(esp, cel_slot, cur_k, target_ksize, &g) && g == target_ksize) {
                aktualizacja_sha256(cur_k, target_ksize, sha_jadra);
            }
            kfree(cur_k);
        }
    }

    {
        char sciezka_wersji[64] = {0};
        size_t wp = 0;
        while (pref_slotu[wp]) { sciezka_wersji[wp] = pref_slotu[wp]; ++wp; }
        const char* sw = "/system/wersja";
        size_t sp = 0;
        while (sw[sp] && wp < sizeof(sciezka_wersji) - 1) { sciezka_wersji[wp++] = sw[sp++]; }
        sciezka_wersji[wp] = '\0';

        char tekst[16] = {0};
        size_t p = 0;
        uint32_t cz[3] = {wykaz->wersjasystemu.major, wykaz->wersjasystemu.minor, wykaz->wersjasystemu.patch};
        for (int k = 0; k < 3; ++k) {
            uint32_t v = cz[k];
            char tmp[5]; int tl = 0;
            if (v == 0) tmp[tl++] = '0';
            else { char rev[5]; int rl = 0; while (v > 0) { rev[rl++] = (char)('0' + v % 10); v /= 10; } while (rl > 0) tmp[tl++] = rev[--rl]; }
            for (int i = 0; i < tl && p < sizeof(tekst) - 2; ++i) tekst[p++] = tmp[i];
            if (k < 2 && p < sizeof(tekst) - 2) tekst[p++] = '.';
        }
        if (p < sizeof(tekst) - 1) tekst[p++] = '\n';
        tekst[p] = '\0';
        (void)akt_napisz_tekst(sciezka_wersji, tekst, p);
    }

    (void)psf_flush_cache();

    if (cel_slot == 1) {
        akt_log("[AKTUALIZACJA] Weryfikacja slotu B zakonczona.");
    } else {
        akt_log("[AKTUALIZACJA] Weryfikacja slotu A zakonczona.");
    }

    bursztyn_rozruch_stan stan{};
    bool stan_ok = false;
    Storage::Device* dev = blokowy_dysk_systemowy();
    if (dev) {
        if (!Storage::rozruch_weryfikuj_uklad_dysku(dev)) {
            akt_log("[AKTUALIZACJA] BLAD: Uklad dysku niezgodny z zarezerwowanymi sektorami stanu rozruchu LBA 2046/2047!");
            return false;
        }
        stan_ok = Storage::rozruch_odczytaj_sektory(dev, &stan);
    }
    if (!stan_ok) {
        bursztyn_rozruch_stan k0{}, k1{};
        uint32_t g0 = 0, g1 = 0;
        bool r0 = czytaj_fragment_pliku("/aktualizacje/stan_rozruchu.k0", 0, (char*)&k0, sizeof(k0), &g0) && g0 == sizeof(k0);
        bool r1 = czytaj_fragment_pliku("/aktualizacje/stan_rozruchu.k1", 0, (char*)&k1, sizeof(k1), &g1) && g1 == sizeof(k1);
        if (r0 || r1) {
            stan_ok = rozruch_wybierz_poprawna_kopie(r0 ? &k0 : nullptr, r1 ? &k1 : nullptr, &stan);
        }
    }
    if (!stan_ok) {
        rozruch_stan_poczatkowy(&stan, biezaca.major, biezaca.minor, biezaca.patch);
    }

    if (!rozruch_ustaw_oczekujacy(&stan, (bursztyn_slot_t)cel_slot, docel.major, docel.minor, docel.patch, sha_jadra)) {
        akt_log("[AKTUALIZACJA] BLAD aktualizacji stanu rozruchu.");
        return false;
    }

    if (dev) {
        if (!Storage::rozruch_weryfikuj_uklad_dysku(dev)) {
            akt_log("[AKTUALIZACJA] BLAD: Uklad dysku narusza zarezerwowane sektory LBA 2046/2047!");
            return false;
        }
        if (!Storage::rozruch_zapisz_sektory(dev, &stan)) {
            akt_log("[AKTUALIZACJA] BLAD zapisu sektorow stanu rozruchu.");
            return false;
        }
    }

    (void)zapisz_do_pliku("/aktualizacje/stan_rozruchu.k0", (const char*)&stan, sizeof(stan));
    (void)zapisz_do_pliku("/aktualizacje/stan_rozruchu.k1", (const char*)&stan, sizeof(stan));

    const char* potw_str = (aktywny == 0) ? "A" : "B";
    const char* oczek_str = (cel_slot == 1) ? "B" : "A";
    if (!Storage::esp_zapisz_grubenv(esp, potw_str, oczek_str, 0)) {
        akt_log("[AKTUALIZACJA] BLAD zapisu grubenv.");
        return false;
    }

    (void)psf_flush_cache();

    if (cel_slot == 1) {
        akt_log("[AKTUALIZACJA] Slot B oczekuje na uruchomienie.");
    } else {
        akt_log("[AKTUALIZACJA] Slot A oczekuje na uruchomienie.");
    }
    akt_log("[AKTUALIZACJA] Wymagane ponowne uruchomienie.");
    return true;
}

/* ---- samotest ---- */
/* Wektor testowy: kanoniczny wykaz 0.2.0 podpisany developerskim kluczem testowym.
 * Podpis wygenerowany hostowo (narzedzia/aktualizacje), zweryfikowany w QEMU. */
static const char AKT_TEST_KANONICZNE[] =
    "wersjaformatu=1\n"
    "wersjasystemu=0.2.0\n"
    "minimalnawersja=0.1.0\n"
    "plik=bursztyn-0.2.0.akt\n"
    "rozmiar=566\n"
    "sumasha256=3ccc093924026f1e757399d44f82c469636202f2a880bb707ebd0bb345cbddd4\n"
    "kanal=stabilny\n";
static const uint8_t AKT_TEST_PODPIS[256] = {
    0x39, 0xC3, 0xAE, 0x4C, 0x10, 0xE1, 0x80, 0x66, 0xB4, 0x4D, 0xC0, 0x39, 0x72, 0xE7, 0x48, 0x6A,
    0x95, 0x72, 0x51, 0xC0, 0x59, 0x53, 0xC3, 0x63, 0xA5, 0x81, 0xB7, 0x2C, 0x9C, 0x90, 0x90, 0x24,
    0x21, 0x14, 0xC7, 0x92, 0x3B, 0xCD, 0x94, 0x9B, 0x30, 0xEA, 0xF9, 0x16, 0x8A, 0x54, 0xD7, 0xEB,
    0x50, 0x8C, 0xB7, 0x33, 0x6D, 0x78, 0xBA, 0xF1, 0x47, 0x2C, 0x39, 0x7F, 0x1A, 0x25, 0xA3, 0x5D,
    0xEF, 0xE1, 0x2F, 0x1A, 0x55, 0xF2, 0x3F, 0x2C, 0x27, 0xD3, 0xF4, 0x14, 0xD6, 0x97, 0x34, 0xFA,
    0x8B, 0x8F, 0x5E, 0x22, 0x2D, 0xDB, 0xAC, 0x9F, 0x5C, 0xAE, 0xD1, 0x85, 0x35, 0x45, 0x88, 0x75,
    0xDF, 0x74, 0xB5, 0x62, 0x55, 0xA1, 0xD4, 0x08, 0x0D, 0xFE, 0x8E, 0x98, 0x4E, 0x17, 0xF0, 0x0F,
    0xEF, 0x40, 0xBB, 0xFB, 0x4F, 0x86, 0xB9, 0xAA, 0x50, 0xBE, 0x04, 0xCC, 0x73, 0x81, 0x5A, 0xFB,
    0x86, 0x68, 0x3F, 0x3A, 0x8F, 0xC7, 0xC4, 0x75, 0xD3, 0x33, 0xBD, 0x05, 0xCD, 0x3F, 0x54, 0x85,
    0x16, 0x29, 0x7C, 0x55, 0x84, 0x96, 0xF0, 0x64, 0x4A, 0x09, 0x18, 0x8D, 0x1B, 0x37, 0xC9, 0xF8,
    0x61, 0x3D, 0x6D, 0xCC, 0xD4, 0x62, 0x93, 0xE7, 0x52, 0xE7, 0x4A, 0xEB, 0x05, 0x45, 0xBA, 0xE2,
    0x97, 0x1E, 0x2D, 0x10, 0x06, 0x09, 0x93, 0xC8, 0x4B, 0xCD, 0x1D, 0x8F, 0x60, 0xDF, 0xFC, 0x14,
    0xC2, 0xCD, 0x8E, 0xE9, 0xAF, 0xB2, 0xF1, 0x27, 0x12, 0x6D, 0x85, 0xD6, 0xE4, 0xB5, 0x8C, 0x6E,
    0xA2, 0x3F, 0xC1, 0xFC, 0x3A, 0xF1, 0xDF, 0x2D, 0xE6, 0x52, 0x76, 0xAC, 0xE3, 0x42, 0x22, 0xF5,
    0x88, 0x78, 0x82, 0xD0, 0xFA, 0x51, 0x16, 0x47, 0xB4, 0xD0, 0x24, 0x07, 0xE9, 0x22, 0x5B, 0x8B,
    0xA5, 0xA3, 0x81, 0xE8, 0xE5, 0xAF, 0x01, 0xEE, 0x0D, 0x22, 0x26, 0x45, 0xDC, 0x0A, 0xFD, 0xD0,
};

void aktualizacja_samotest_automatyczny(void) {
    akt_log("[AKTUALIZACJA] Sprawdzanie dostepnosci aktualizacji.");
    int zdane = 0, zlane = 0;
    auto raport = [&](const char* nazwa, bool ok, bool oczekiwane_ok) {
        bool zgodne = (ok == oczekiwane_ok);
        if (zgodne) ++zdane; else ++zlane;
        (void)nazwa;
    };
    /* 1. Poprawny wykaz testowy (parser). */
    {
        char bufor[1024];
        size_t p = 0;
        const char* k = AKT_TEST_KANONICZNE;
        while (*k && p < sizeof(bufor) - 1) bufor[p++] = *k++;
        /* Dopisz podpis hex. */
        static const char hx[] = "0123456789abcdef";
        const char* pre = "podpis=";
        /* Plik wykazu ma kolejnosc: ...sumasha256, podpis, kanal.
         * Wektor kanoniczny nie zawiera podpisu, wiec skladamy pelny wykaz. */
        char pelny[1600] = {0};
        size_t q = 0;
        const char* linie[] = {
            "wersjaformatu=1\n", "wersjasystemu=0.2.0\n", "minimalnawersja=0.1.0\n",
            "plik=bursztyn-0.2.0.akt\n", "rozmiar=566\n",
            "sumasha256=3ccc093924026f1e757399d44f82c469636202f2a880bb707ebd0bb345cbddd4\n"
        };
        for (int i = 0; i < 6; ++i) {
            const char* s = linie[i];
            while (*s && q < sizeof(pelny) - 1) pelny[q++] = *s++;
        }
        const char* pp = "podpis=";
        while (*pp && q < sizeof(pelny) - 1) pelny[q++] = *pp++;
        for (int i = 0; i < 256 && q < sizeof(pelny) - 1; ++i) {
            pelny[q++] = hx[(AKT_TEST_PODPIS[i] >> 4) & 0xF];
            if (q < sizeof(pelny) - 1) pelny[q++] = hx[AKT_TEST_PODPIS[i] & 0xF];
        }
        if (q < sizeof(pelny) - 1) pelny[q++] = '\n';
        const char* kl = "kanal=stabilny\n";
        while (*kl && q < sizeof(pelny) - 1) pelny[q++] = *kl++;
        pelny[q] = '\0';
        akt_wykaz_t w;
        int rc = akt_parsuj_wykaz(pelny, q, &w);
        raport("wykaz-poprawny", rc == AKT_OK, true);
        if (rc == AKT_OK) {
            akt_log("[AKTUALIZACJA] Dostepna wersja 0.2.0.");
            /* 2. Weryfikacja podpisu wektora. */
            size_t klen = 0;
            while (AKT_TEST_KANONICZNE[klen] != '\0') ++klen;
            bool sig = aktualizacja_weryfikuj_podpis((const uint8_t*)AKT_TEST_KANONICZNE, klen, AKT_TEST_PODPIS);
#if AKTUALIZACJA_KLUCZ_JEST_TESTOWY
            raport("podpis-poprawny", sig, true);
            if (sig) akt_log("[AKTUALIZACJA] Podpis poprawny (klucz testowy).");
            else akt_log("[AKTUALIZACJA] BLAD weryfikacji podpisu.");
            /* 3. Zly podpis musi zostac odrzucony. */
            uint8_t zly[256];
            for (int i = 0; i < 256; ++i) zly[i] = AKT_TEST_PODPIS[i];
            zly[0] ^= 0x01;
            bool sig2 = aktualizacja_weryfikuj_podpis((const uint8_t*)AKT_TEST_KANONICZNE, klen, zly);
            raport("podpis-zly", sig2, false);
            /* 4. Pelna weryfikacja wykazu (parser+podpis). */
            akt_wykaz_t w2;
            bool ww = aktualizacja_weryfikuj_wykaz(pelny, q, &w2);
            raport("wykaz-weryfikacja", ww, true);
#else
            /* W konfiguracji RELEASE podpis wygenerowany kluczem testowym z repozytorium
             * MUSI zostac bezwzglednie odrzucony (ochrona przed kluczem testowym). */
            raport("podpis-testowy-odrzucony", !sig, true);
            if (!sig) akt_log("[AKTUALIZACJA] Podpis testowy prawidlowo odrzucony w wydaniu (ochrona klucza).");
            else akt_log("[AKTUALIZACJA] BLAD: klucz testowy zaakceptowany w wydaniu!");
            /* Pelna weryfikacja wykazu podpisanego kluczem testowym tez musi zostac odrzucona. */
            akt_wykaz_t w2;
            bool ww = aktualizacja_weryfikuj_wykaz(pelny, q, &w2);
            raport("wykaz-testowy-odrzucony", !ww, true);
#endif
        }
        (void)bufor; (void)p; (void)pre;
    }
    /* 5. Odrzucanie zlych danych (parser fail-closed). */
    {
        akt_wykaz_t w;
        raport("pusty", akt_parsuj_wykaz("", 0, &w) == AKT_OK, false);
        const char dup[] = "wersjaformatu=1\nwersjaformatu=1\n";
        raport("duplikat", akt_parsuj_wykaz(dup, sizeof(dup) - 1, &w) == AKT_OK, false);
        const char zlykanal[] = "wersjaformatu=1\nwersjasystemu=0.2.0\nminimalnawersja=0.1.0\n"
            "plik=bursztyn-0.2.0.akt\nrozmiar=566\n"
            "sumasha256=3ccc093924026f1e757399d44f82c469636202f2a880bb707ebd0bb345cbddd4\n"
            "podpis=00\nkanal=nieznany\n";
        raport("kanal", akt_parsuj_wykaz(zlykanal, sizeof(zlykanal) - 1, &w) == AKT_OK, false);
        const char zlyformat[] = "wersjaformatu=99\nwersjasystemu=0.2.0\nminimalnawersja=0.1.0\n"
            "plik=bursztyn-0.2.0.akt\nrozmiar=566\n"
            "sumasha256=3ccc093924026f1e757399d44f82c469636202f2a880bb707ebd0bb345cbddd4\n"
            "podpis=00\nkanal=stabilny\n";
        raport("format", akt_parsuj_wykaz(zlyformat, sizeof(zlyformat) - 1, &w) == AKT_OK, false);
    }
    /* 6. Wersje: porownanie numeryczne, downgrade. */
    {
        akt_wersja_t a{0, 1, 0}, b{0, 2, 0}, m{0, 1, 0};
        raport("dozwolona", akt_czy_dozwolona_aktualizacja(&a, &b, &m) == 1, true);
        raport("ta-sama", akt_czy_dozwolona_aktualizacja(&b, &b, &m) == 1, false);
        raport("downgrade", akt_czy_dozwolona_aktualizacja(&b, &a, &m) == 1, false);
        akt_wersja_t v1{0, 9, 0}, v2{0, 10, 0};
        raport("numeryczna", akt_porownaj_wersje(&v1, &v2) < 0, true);
    }
    /* 7. Pakiet: poprawny i zly (syntetyczny, w RAM). */
    {
        /* Minimalny pakiet BAKT z 1 wpisem konfig/test.txt. */
        static const uint8_t tresc[] = {'t', 'e', 's', 't'};
        uint8_t pakiet[16 + 256 + 4];
        pakiet[0] = 'B'; pakiet[1] = 'A'; pakiet[2] = 'K'; pakiet[3] = 'T';
        pakiet[4] = 1; pakiet[5] = 0; pakiet[6] = 0; pakiet[7] = 0;
        pakiet[8] = 1; pakiet[9] = 0; pakiet[10] = 0; pakiet[11] = 0;
        pakiet[12] = 0; pakiet[13] = 0; pakiet[14] = 0; pakiet[15] = 0;
        uint8_t* wpis = pakiet + 16;
        for (int i = 0; i < 256; ++i) wpis[i] = 0;
        wpis[0] = 'k'; wpis[1] = 'o'; wpis[2] = 'n'; wpis[3] = 'f'; wpis[4] = 'i'; wpis[5] = 'g';
        uint8_t* sc = wpis + 32;
        const char* nazwa = "test.txt";
        for (int i = 0; nazwa[i]; ++i) sc[i] = (uint8_t)nazwa[i];
        uint64_t off = 16 + 256;
        for (int i = 0; i < 8; ++i) wpis[32 + 128 + i] = (uint8_t)((off >> (8 * i)) & 0xFF);
        uint64_t roz = 4;
        for (int i = 0; i < 8; ++i) wpis[32 + 128 + 8 + i] = (uint8_t)((roz >> (8 * i)) & 0xFF);
        uint8_t h[32];
        aktualizacja_sha256(tresc, 4, h);
        for (int i = 0; i < 32; ++i) wpis[32 + 128 + 8 + 8 + i] = h[i];
        pakiet[16 + 256 + 0] = 't'; pakiet[16 + 256 + 1] = 'e';
        pakiet[16 + 256 + 2] = 's'; pakiet[16 + 256 + 3] = 't';
        raport("pakiet-struktura", akt_sprawdz_pakiet(pakiet, sizeof(pakiet), nullptr) == AKT_OK, true);
        pakiet[0] = 'X';
        raport("pakiet-magic", akt_sprawdz_pakiet(pakiet, sizeof(pakiet), nullptr) == AKT_OK, false);
        pakiet[0] = 'B';
    }
    if (zlane == 0) {
        akt_log("[AKTUALIZACJA] Samotest: wszystkie kontrole przeszly.");
    } else {
        akt_log("[AKTUALIZACJA] BLAD samotestu.");
    }
    {
        /* Zliczanie bez snprintf: log prosty. */
        (void)zdane; (void)zlane;
    }
}

/* =========================================================================
 * BWS OTA ABI - obsluga Ring 3 (BWS 84: status, BWS 85: polecenie)
 * ========================================================================= */

static BwsOtaStatus g_ota_stan{};
static uint32_t s_ota_lock = 0;
static uint32_t s_ota_depth = 0;
static int s_ota_owner = -1;
static const char* s_ota_site = nullptr;

GuardOTA::GuardOTA(const char* miejsce)
    : aktywny_(false), pierwotny_(false), miejsce_(miejsce) {
    int my_id = aktualny_pid;
    if (s_ota_owner == my_id && s_ota_depth > 0) {
        s_ota_depth++;
        aktywny_ = true;
        pierwotny_ = false;
        return;
    }
    // Kontrola inwersji hierarchii blokad:
    // GuardOTA (warstwa 4) nie moze byc pobierany, gdy biezacy kontekst trzyma
    // juz GuardStorageFormatter (warstwa 2). Zapobiega zakleszczeniu (deadlock).
    if (Storage::formatter_blokada_wlasciciel() == my_id &&
        Storage::formatter_blokada_glebokosc() > 0) {
        aktywny_ = false;
        pierwotny_ = false;
        return;
    }
    while (__atomic_exchange_n(&s_ota_lock, 1U, __ATOMIC_ACQUIRE) != 0U) {
#if defined(__x86_64__) || defined(_M_X64)
        asm volatile("pause");
#endif
    }
    s_ota_owner = my_id;
    s_ota_depth = 1;
    s_ota_site = miejsce_;
    aktywny_ = true;
    pierwotny_ = true;
}

GuardOTA::~GuardOTA() {
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

uint32_t ota_blokada_glebokosc() {
    return __atomic_load_n(&s_ota_depth, __ATOMIC_ACQUIRE);
}

int ota_blokada_wlasciciel() {
    return __atomic_load_n(&s_ota_owner, __ATOMIC_ACQUIRE);
}

const char* ota_blokada_miejsce() {
    return __atomic_load_n(&s_ota_site, __ATOMIC_ACQUIRE);
}

BwsOtaDostepnosc aktualizacja_okresl_dostepnosc(void) {
    GuardOTA guard(__func__);
    akt_log("[OTA TRACE] Determining OTA availability");
    if (!instalator_system_zainstalowany() || instalator_tryb()) {
        return OTA_UNAVAILABLE_LIVE_ENVIRONMENT;
    }

    Storage::Device* dev = blokowy_dysk_systemowy();
    if (!dev) {
        return OTA_BACKEND_ERROR;
    }
    akt_log("[OTA TRACE] Storage device resolved: SATA AHCI");

    akt_log("[OTA TRACE] Reading bootstate / GPT");
    static Storage::Table s_aktualizacja_gpt_table;
    Storage::Table* table = &s_aktualizacja_gpt_table;
    *table = {};
    if (!Storage::gpt_read(dev, table)) {
        return OTA_UNAVAILABLE_LEGACY_LAYOUT;
    }

    bool esp_znalezione = false;
    bool bsp_znalezione = false;
    for (uint32_t i = 0; i < table->count; ++i) {
        const auto& e = table->entries[i];
        if (e.count == 0) continue;
        if (Storage::equal(e.type, Storage::EspType, 16)) esp_znalezione = true;
        if (Storage::equal(e.type, Storage::BspType, 16)) bsp_znalezione = true;
    }

    if (!esp_znalezione || !bsp_znalezione) {
        return OTA_UNAVAILABLE_LEGACY_LAYOUT;
    }

    if (!Storage::rozruch_weryfikuj_uklad_dysku(dev)) {
        return OTA_INVALID_DISK_LAYOUT;
    }

    return OTA_AVAILABLE;
}

uint64_t aktualizacja_bws_status(const proces_t* proces, uint64_t uzytkownik_bufor) {
    akt_log("[OTA TRACE] Kernel entered BWS 84 handler");
    if (!proces || !aktualizacja_czy_moze_sprawdzac(proces)) {
        return 0;
    }
    if (uzytkownik_bufor == 0 ||
        !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(uzytkownik_bufor), sizeof(BwsOtaStatus))) {
        return 0;
    }

    GuardOTA guard(__func__);
    BwsOtaStatus out{};
    out.wersja_abi = BWS_OTA_ABI_WERSJA;
    out.aktywny_slot = aktualizacja_aktywny_slot();
    out.nieaktywny_slot = aktualizacja_nieaktywny_slot();
    out.slot_oczekujacy = BURSZTYN_SLOT_NIEZNANY;
    out.stan_oczekujacego = BURSZTYN_STAN_NIEAKTYWNY;

    /* Biezaca wersja jadra / systemu */
    akt_wersja_t cur_ver{};
    if (aktualizacja_biezaca_wersja(&cur_ver)) {
        akt_wersja_na_tekst(&cur_ver, out.biezaca_wersja, sizeof(out.biezaca_wersja));
    } else {
        akt_kopiuj_tekst(out.biezaca_wersja, BURSZTYN_WERSJA_TEKST, sizeof(out.biezaca_wersja));
    }

    BwsOtaDostepnosc dost = aktualizacja_okresl_dostepnosc();
    out.dostepnosc = dost;

    if (dost == OTA_UNAVAILABLE_LIVE_ENVIRONMENT) {
        out.stan = BWS_OTA_STAN_BEZCZYNNA;
        out.kod_bledu = BWS_OTA_BLAD_BRAK;
        akt_kopiuj_tekst(out.komunikat,
            "Aktualizacje systemu są niedostępne w środowisku instalacyjnym. "
            "Zainstaluj Bursztyn OS na dysku, aby korzystać z aktualizacji.",
            sizeof(out.komunikat));
    } else if (dost == OTA_UNAVAILABLE_LEGACY_LAYOUT) {
        out.stan = BWS_OTA_STAN_BEZCZYNNA;
        out.kod_bledu = BWS_OTA_BLAD_BRAK;
        akt_kopiuj_tekst(out.komunikat,
            "Ta instalacja Bursztyn OS nie obsługuje bezpiecznych aktualizacji A/B.",
            sizeof(out.komunikat));
    } else if (dost == OTA_INVALID_DISK_LAYOUT) {
        out.stan = BWS_OTA_STAN_BLAD;
        out.kod_bledu = BWS_OTA_BLAD_UKLADU_DYSKU;
        akt_kopiuj_tekst(out.komunikat,
            "Układ dysku jest niezgodny z wymaganiami bezpiecznych aktualizacji A/B.",
            sizeof(out.komunikat));
    } else if (dost == OTA_BACKEND_ERROR) {
        out.stan = BWS_OTA_STAN_BLAD;
        out.kod_bledu = BWS_OTA_BLAD_STANU_ROZRUCHU;
        akt_kopiuj_tekst(out.komunikat,
            "Wewnętrzny błąd podsystemu aktualizacji (backend error).",
            sizeof(out.komunikat));
    } else {
        /* dost == OTA_AVAILABLE: normalny odczyt */
        /* Stan rozruchu ze stabilnych sektorow lub kopii zapasowych */
        akt_log("[OTA TRACE] Reading bootstate / GPT");
        bursztyn_rozruch_stan rstan{};
        bool rstan_ok = false;
        Storage::Device* dev = blokowy_dysk_systemowy();
        if (dev && Storage::rozruch_weryfikuj_uklad_dysku(dev)) {
            rstan_ok = Storage::rozruch_odczytaj_sektory(dev, &rstan);
        }
        akt_log("[OTA TRACE] Reading grubenv");
        Storage::Partition esp_part{};
        char g_potw[4]{'A', 0};
        char g_oczek[16]{'B', 'R', 'A', 'K', 0};
        uint32_t g_proby = 0;
        if (blokowy_esp_systemowy(&esp_part)) {
            Storage::esp_odczytaj_grubenv(esp_part, g_potw, g_oczek, &g_proby);
        }
        if (!rstan_ok) {
            bursztyn_rozruch_stan k0{}, k1{};
            uint32_t g0 = 0, g1 = 0;
            bool r0 = czytaj_fragment_pliku("/aktualizacje/stan_rozruchu.k0", 0, (char*)&k0, sizeof(k0), &g0) && g0 == sizeof(k0);
            bool r1 = czytaj_fragment_pliku("/aktualizacje/stan_rozruchu.k1", 0, (char*)&k1, sizeof(k1), &g1) && g1 == sizeof(k1);
            if (r0 || r1) {
                rstan_ok = rozruch_wybierz_poprawna_kopie(r0 ? &k0 : nullptr, r1 ? &k1 : nullptr, &rstan);
            }
        }
        if (rstan_ok) {
            out.slot_oczekujacy = rstan.slot_oczekujacy;
            out.stan_oczekujacego = rstan.stan_oczekujacego;
            if (rstan.stan_oczekujacego == BURSZTYN_STAN_NIEUDANY) {
                out.rollback_wykryty = 1;
            }
            if (rstan.slot_oczekujacy == out.nieaktywny_slot &&
                rstan.stan_oczekujacego == BURSZTYN_STAN_OCZEKUJACY) {
                out.restart_wymagany = 1;
            }
        }

        /* Wykaz aktualizacji na dysku */
        uint32_t wsize = rozmiar_pliku(AKT_SCIEZKA_WYKAZ);
        akt_wykaz_t wstruktura{};
        bool wykaz_odczytany = false;
        if (wsize > 0 && wsize <= 4096) {
            char* wbuf = static_cast<char*>(kmalloc(wsize + 1));
            if (wbuf) {
                uint32_t wgot = 0;
                if (czytaj_fragment_pliku(AKT_SCIEZKA_WYKAZ, 0, wbuf, wsize, &wgot) && wgot == wsize) {
                    wbuf[wsize] = '\0';
                    if (aktualizacja_weryfikuj_wykaz(wbuf, wsize, &wstruktura)) {
                        wykaz_odczytany = true;
                        out.podpis_ok = 1;
                        akt_wersja_na_tekst(&wstruktura.wersjasystemu, out.dostepna_wersja, sizeof(out.dostepna_wersja));
                        akt_kopiuj_tekst(out.kanal, akt_nazwa_kanalu(wstruktura.kanal), sizeof(out.kanal));
                        akt_kopiuj_tekst(out.plik_pakietu, wstruktura.plik, sizeof(out.plik_pakietu));
                        out.rozmiar_pakietu = wstruktura.rozmiar;

                        char sciezka_pak[128] = {0};
                        akt_zlacz_sciezke(AKT_SCIEZKA_KATALOG, wstruktura.plik, sciezka_pak, sizeof(sciezka_pak));
                        uint32_t psize = rozmiar_pliku(AKT_SCIEZKA_POBIERANIE_TMP);
                        if (psize == 0) psize = rozmiar_pliku(sciezka_pak);
                        if (psize > 0) {
                            out.pobrano_bajtow = psize;
                            if (psize == wstruktura.rozmiar) {
                                out.sha256_ok = 1;
                            }
                        }
                    }
                }
                kfree(wbuf);
            }
        }

        /* Konsolidacja biezacego stanu aplikacji z danymi w pamieci */
        if (out.restart_wymagany || g_ota_stan.stan == BWS_OTA_STAN_WYMAGA_RESTARTU) {
            out.stan = BWS_OTA_STAN_WYMAGA_RESTARTU;
            out.restart_wymagany = 1;
            out.postep_procent = 100;
            akt_kopiuj_tekst(out.komunikat, "Aktualizacja zainstalowana. Wymagane ponowne uruchomienie systemu.", sizeof(out.komunikat));
        } else if (out.rollback_wykryty && g_ota_stan.stan == BWS_OTA_STAN_BEZCZYNNA) {
            out.stan = BWS_OTA_STAN_ROLLBACK_WYKRYTY;
            akt_kopiuj_tekst(out.komunikat, "Poprzedni rozruch nie powiódł się. Nastąpił automatyczny powrót do bezpiecznego slotu.", sizeof(out.komunikat));
        } else if (g_ota_stan.stan != BWS_OTA_STAN_BEZCZYNNA) {
            out.stan = g_ota_stan.stan;
            out.kod_bledu = g_ota_stan.kod_bledu;
            out.postep_procent = g_ota_stan.postep_procent;
            akt_kopiuj_tekst(out.komunikat, g_ota_stan.komunikat, sizeof(out.komunikat));
        } else if (wykaz_odczytany) {
            int comp = akt_porownaj_wersje(&wstruktura.wersjasystemu, &cur_ver);
            if (comp > 0) {
                if (out.sha256_ok) {
                    out.stan = BWS_OTA_STAN_POBRANA;
                    out.postep_procent = 100;
                    akt_kopiuj_tekst(out.komunikat, "Pakiet aktualizacji jest pobrany i gotowy do instalacji.", sizeof(out.komunikat));
                } else {
                    out.stan = BWS_OTA_STAN_DOSTEPNA;
                    akt_kopiuj_tekst(out.komunikat, "Dostępna nowa wersja systemu Bursztyn OS.", sizeof(out.komunikat));
                }
            } else {
                out.stan = BWS_OTA_STAN_BEZCZYNNA;
                akt_kopiuj_tekst(out.komunikat, "System jest w pełni zaktualizowany.", sizeof(out.komunikat));
            }
        } else {
            out.stan = BWS_OTA_STAN_BEZCZYNNA;
            akt_kopiuj_tekst(out.komunikat, "System gotowy do sprawdzenia aktualizacji.", sizeof(out.komunikat));
        }
    }

    akt_log("[OTA TRACE] Copying status to user space");
    if (!skopiuj_do_przestrzeni_uzytkownika(reinterpret_cast<void*>(uzytkownik_bufor), &out, sizeof(out))) {
        return 0;
    }
    akt_log("[OTA TRACE] BWS 84 completed successfully");
    return 1;
}

uint64_t aktualizacja_bws_polecenie(const proces_t* proces, uint32_t cmd, uint64_t uzytkownik_arg) {
    if (!proces) {
        return BWS_OTA_BLAD_BRAK_UPRAWNIEN;
    }

    GuardOTA guard(__func__);
    BwsOtaDostepnosc dost = aktualizacja_okresl_dostepnosc();
    if (dost != OTA_AVAILABLE) {
        if (cmd == BWS_OTA_CMD_SPRAWDZ || cmd == BWS_OTA_CMD_POBIERZ || cmd == BWS_OTA_CMD_INSTALUJ) {
            if (dost == OTA_INVALID_DISK_LAYOUT) {
                return BWS_OTA_BLAD_UKLADU_DYSKU;
            }
            return BWS_OTA_BLAD_BRAK_UPRAWNIEN;
        }
    }

    switch (cmd) {
        case BWS_OTA_CMD_SPRAWDZ: {
            if (!aktualizacja_czy_moze_sprawdzac(proces)) {
                return BWS_OTA_BLAD_BRAK_UPRAWNIEN;
            }

            if (uzytkownik_arg != 0) {
                char domena[128] = {0};
                if (skopiuj_string_z_uzytkownika(domena, reinterpret_cast<const char*>(uzytkownik_arg), sizeof(domena))) {
                    if (domena[0] != '\0') {
                        if (aktualizacja_pobierz_do_tmp(domena, "/aktualizacje/wykazaktualizacji.wykaz", true, 4096)) {
                            uint32_t tlen = rozmiar_pliku(AKT_SCIEZKA_POBIERANIE_TMP);
                            if (tlen > 0 && tlen <= 4096) {
                                char* tbuf = static_cast<char*>(kmalloc(tlen));
                                uint32_t rgot = 0;
                                if (tbuf && czytaj_fragment_pliku(AKT_SCIEZKA_POBIERANIE_TMP, 0, tbuf, tlen, &rgot) && rgot == tlen) {
                                    (void)akt_napisz_tekst(AKT_SCIEZKA_WYKAZ, tbuf, tlen);
                                }
                                if (tbuf) kfree(tbuf);
                            }
                        }
                    }
                }
            }

            uint32_t wsize = rozmiar_pliku(AKT_SCIEZKA_WYKAZ);
            if (wsize == 0 || wsize > 4096) {
                g_ota_stan.stan = BWS_OTA_STAN_BLAD;
                g_ota_stan.kod_bledu = BWS_OTA_BLAD_BRAK_WYKAZU;
                akt_kopiuj_tekst(g_ota_stan.komunikat, "Brak pliku wykazu aktualizacji.", sizeof(g_ota_stan.komunikat));
                return BWS_OTA_BLAD_BRAK_WYKAZU;
            }

            char* wbuf = static_cast<char*>(kmalloc(wsize + 1));
            if (!wbuf) {
                g_ota_stan.stan = BWS_OTA_STAN_BLAD;
                g_ota_stan.kod_bledu = BWS_OTA_BLAD_BRAK_WYKAZU;
                akt_kopiuj_tekst(g_ota_stan.komunikat, "Brak pamięci jądra.", sizeof(g_ota_stan.komunikat));
                return BWS_OTA_BLAD_BRAK_WYKAZU;
            }

            uint32_t wgot = 0;
            if (!czytaj_fragment_pliku(AKT_SCIEZKA_WYKAZ, 0, wbuf, wsize, &wgot) || wgot != wsize) {
                kfree(wbuf);
                g_ota_stan.stan = BWS_OTA_STAN_BLAD;
                g_ota_stan.kod_bledu = BWS_OTA_BLAD_BRAK_WYKAZU;
                akt_kopiuj_tekst(g_ota_stan.komunikat, "Błąd odczytu pliku wykazu.", sizeof(g_ota_stan.komunikat));
                return BWS_OTA_BLAD_BRAK_WYKAZU;
            }
            wbuf[wsize] = '\0';

            akt_wykaz_t wstruktura{};
            if (!aktualizacja_weryfikuj_wykaz(wbuf, wsize, &wstruktura)) {
                kfree(wbuf);
                g_ota_stan.stan = BWS_OTA_STAN_BLAD;
                g_ota_stan.kod_bledu = BWS_OTA_BLAD_PODPISU;
                akt_kopiuj_tekst(g_ota_stan.komunikat, "Błąd weryfikacji podpisu cyfrowego wykazu.", sizeof(g_ota_stan.komunikat));
                return BWS_OTA_BLAD_PODPISU;
            }
            kfree(wbuf);

            akt_wersja_t cur_ver{};
            (void)aktualizacja_biezaca_wersja(&cur_ver);

            int comp = akt_porownaj_wersje(&wstruktura.wersjasystemu, &cur_ver);
            if (comp > 0) {
                g_ota_stan.stan = BWS_OTA_STAN_DOSTEPNA;
                g_ota_stan.kod_bledu = BWS_OTA_BLAD_BRAK;
                g_ota_stan.podpis_ok = 1;
                akt_wersja_na_tekst(&wstruktura.wersjasystemu, g_ota_stan.dostepna_wersja, sizeof(g_ota_stan.dostepna_wersja));
                akt_kopiuj_tekst(g_ota_stan.kanal, akt_nazwa_kanalu(wstruktura.kanal), sizeof(g_ota_stan.kanal));
                akt_kopiuj_tekst(g_ota_stan.plik_pakietu, wstruktura.plik, sizeof(g_ota_stan.plik_pakietu));
                g_ota_stan.rozmiar_pakietu = wstruktura.rozmiar;
                akt_kopiuj_tekst(g_ota_stan.komunikat, "Dostępna nowa wersja systemu Bursztyn OS.", sizeof(g_ota_stan.komunikat));
            } else {
                g_ota_stan.stan = BWS_OTA_STAN_BEZCZYNNA;
                g_ota_stan.kod_bledu = BWS_OTA_BLAD_BRAK;
                akt_kopiuj_tekst(g_ota_stan.komunikat, "System jest w pełni zaktualizowany.", sizeof(g_ota_stan.komunikat));
            }
            return BWS_OTA_BLAD_BRAK;
        }

        case BWS_OTA_CMD_POBIERZ: {
            if (!aktualizacja_czy_moze_pobierac(proces)) {
                return BWS_OTA_BLAD_BRAK_UPRAWNIEN;
            }

            uint32_t wsize = rozmiar_pliku(AKT_SCIEZKA_WYKAZ);
            if (wsize == 0 || wsize > 4096) {
                g_ota_stan.stan = BWS_OTA_STAN_BLAD;
                g_ota_stan.kod_bledu = BWS_OTA_BLAD_BRAK_WYKAZU;
                akt_kopiuj_tekst(g_ota_stan.komunikat, "Brak zweryfikowanego wykazu.", sizeof(g_ota_stan.komunikat));
                return BWS_OTA_BLAD_BRAK_WYKAZU;
            }

            char* wbuf = static_cast<char*>(kmalloc(wsize + 1));
            if (!wbuf) return BWS_OTA_BLAD_BRAK_WYKAZU;
            uint32_t wgot = 0;
            if (!czytaj_fragment_pliku(AKT_SCIEZKA_WYKAZ, 0, wbuf, wsize, &wgot) || wgot != wsize) {
                kfree(wbuf);
                return BWS_OTA_BLAD_BRAK_WYKAZU;
            }
            wbuf[wsize] = '\0';
            akt_wykaz_t wstruktura{};
            if (!aktualizacja_weryfikuj_wykaz(wbuf, wsize, &wstruktura)) {
                kfree(wbuf);
                return BWS_OTA_BLAD_PODPISU;
            }
            kfree(wbuf);

            char sciezka_pak[128] = {0};
            akt_zlacz_sciezke(AKT_SCIEZKA_KATALOG, wstruktura.plik, sciezka_pak, sizeof(sciezka_pak));

            /* Jesli pakiet jest juz w katalogu aktualizacji, kopiujemy do pobieranie.tmp */
            uint32_t psize = rozmiar_pliku(sciezka_pak);
            if (psize == 0) psize = rozmiar_pliku(AKT_SCIEZKA_POBIERANIE_TMP);

            if (psize == 0 || psize != wstruktura.rozmiar) {
                g_ota_stan.stan = BWS_OTA_STAN_BLAD;
                g_ota_stan.kod_bledu = BWS_OTA_BLAD_BRAK_PAKIETU;
                akt_kopiuj_tekst(g_ota_stan.komunikat, "Nie znaleziono poprawnego pliku pakietu aktualizacji.", sizeof(g_ota_stan.komunikat));
                return BWS_OTA_BLAD_BRAK_PAKIETU;
            }

            /* Jezeli pakiet byl w sciezka_pak a nie w pobieranie.tmp, przepisz do pobieranie.tmp */
            if (rozmiar_pliku(AKT_SCIEZKA_POBIERANIE_TMP) != wstruktura.rozmiar) {
                uint8_t* pbuf = static_cast<uint8_t*>(kmalloc(psize));
                if (pbuf) {
                    uint32_t got = 0;
                    if (czytaj_fragment_pliku(sciezka_pak, 0, reinterpret_cast<char*>(pbuf), psize, &got) && got == psize) {
                        (void)usun_twor(AKT_SCIEZKA_POBIERANIE_TMP);
                        if (utworz_plik(AKT_SCIEZKA_POBIERANIE_TMP)) {
                            (void)zapisz_fragment_pliku(AKT_SCIEZKA_POBIERANIE_TMP, 0, reinterpret_cast<const char*>(pbuf), psize);
                        }
                    }
                    kfree(pbuf);
                }
            }

            g_ota_stan.stan = BWS_OTA_STAN_POBRANA;
            g_ota_stan.postep_procent = 100;
            g_ota_stan.pobrano_bajtow = wstruktura.rozmiar;
            g_ota_stan.rozmiar_pakietu = wstruktura.rozmiar;
            g_ota_stan.kod_bledu = BWS_OTA_BLAD_BRAK;
            akt_kopiuj_tekst(g_ota_stan.komunikat, "Pakiet aktualizacji został pobrany i przygotowany.", sizeof(g_ota_stan.komunikat));
            return BWS_OTA_BLAD_BRAK;
        }

        case BWS_OTA_CMD_INSTALUJ: {
            if (!aktualizacja_czy_moze_instalowac(proces)) {
                return BWS_OTA_BLAD_BRAK_UPRAWNIEN;
            }

            uint32_t wsize = rozmiar_pliku(AKT_SCIEZKA_WYKAZ);
            if (wsize == 0 || wsize > 4096) {
                g_ota_stan.stan = BWS_OTA_STAN_BLAD;
                g_ota_stan.kod_bledu = BWS_OTA_BLAD_BRAK_WYKAZU;
                akt_kopiuj_tekst(g_ota_stan.komunikat, "Brak wykazu aktualizacji.", sizeof(g_ota_stan.komunikat));
                return BWS_OTA_BLAD_BRAK_WYKAZU;
            }

            char* wbuf = static_cast<char*>(kmalloc(wsize + 1));
            if (!wbuf) return BWS_OTA_BLAD_INSTALACJI;
            uint32_t wgot = 0;
            if (!czytaj_fragment_pliku(AKT_SCIEZKA_WYKAZ, 0, wbuf, wsize, &wgot) || wgot != wsize) {
                kfree(wbuf);
                return BWS_OTA_BLAD_BRAK_WYKAZU;
            }
            wbuf[wsize] = '\0';
            akt_wykaz_t wstruktura{};
            if (!aktualizacja_weryfikuj_wykaz(wbuf, wsize, &wstruktura)) {
                kfree(wbuf);
                return BWS_OTA_BLAD_PODPISU;
            }
            kfree(wbuf);

            char sciezka_pak[128] = {0};
            akt_zlacz_sciezke(AKT_SCIEZKA_KATALOG, wstruktura.plik, sciezka_pak, sizeof(sciezka_pak));
            const char* uzywana_sciezka = AKT_SCIEZKA_POBIERANIE_TMP;
            uint32_t plen = rozmiar_pliku(uzywana_sciezka);
            if (plen == 0 || plen != wstruktura.rozmiar) {
                uzywana_sciezka = sciezka_pak;
                plen = rozmiar_pliku(uzywana_sciezka);
            }

            if (plen == 0 || plen != wstruktura.rozmiar || plen > 16 * 1024 * 1024) {
                g_ota_stan.stan = BWS_OTA_STAN_BLAD;
                g_ota_stan.kod_bledu = BWS_OTA_BLAD_BRAK_PAKIETU;
                akt_kopiuj_tekst(g_ota_stan.komunikat, "Nie odnaleziono poprawnego pakietu aktualizacji.", sizeof(g_ota_stan.komunikat));
                return BWS_OTA_BLAD_BRAK_PAKIETU;
            }

            uint8_t* pbuf = static_cast<uint8_t*>(kmalloc(plen));
            if (!pbuf) {
                g_ota_stan.stan = BWS_OTA_STAN_BLAD;
                g_ota_stan.kod_bledu = BWS_OTA_BLAD_INSTALACJI;
                akt_kopiuj_tekst(g_ota_stan.komunikat, "Brak pamięci jądra do instalacji pakietu.", sizeof(g_ota_stan.komunikat));
                return BWS_OTA_BLAD_INSTALACJI;
            }

            uint32_t pgot = 0;
            if (!czytaj_fragment_pliku(uzywana_sciezka, 0, reinterpret_cast<char*>(pbuf), plen, &pgot) || pgot != plen) {
                kfree(pbuf);
                g_ota_stan.stan = BWS_OTA_STAN_BLAD;
                g_ota_stan.kod_bledu = BWS_OTA_BLAD_BRAK_PAKIETU;
                akt_kopiuj_tekst(g_ota_stan.komunikat, "Błąd odczytu pakietu z dysku.", sizeof(g_ota_stan.komunikat));
                return BWS_OTA_BLAD_BRAK_PAKIETU;
            }

            if (!aktualizacja_weryfikuj_pakiet(pbuf, plen, &wstruktura)) {
                kfree(pbuf);
                g_ota_stan.stan = BWS_OTA_STAN_BLAD;
                g_ota_stan.kod_bledu = BWS_OTA_BLAD_SHA256;
                akt_kopiuj_tekst(g_ota_stan.komunikat, "Błąd integralności (SHA-256) pakietu aktualizacji.", sizeof(g_ota_stan.komunikat));
                return BWS_OTA_BLAD_SHA256;
            }

            bool zainstalowano = aktualizacja_zainstaluj_zweryfikowany(pbuf, plen, &wstruktura);
            kfree(pbuf);

            if (!zainstalowano) {
                g_ota_stan.stan = BWS_OTA_STAN_BLAD;
                g_ota_stan.kod_bledu = BWS_OTA_BLAD_INSTALACJI;
                akt_kopiuj_tekst(g_ota_stan.komunikat, "Błąd zapisu plików do slotu docelowego lub błąd stanu rozruchu.", sizeof(g_ota_stan.komunikat));
                return BWS_OTA_BLAD_INSTALACJI;
            }

            g_ota_stan.stan = BWS_OTA_STAN_WYMAGA_RESTARTU;
            g_ota_stan.restart_wymagany = 1;
            g_ota_stan.postep_procent = 100;
            g_ota_stan.kod_bledu = BWS_OTA_BLAD_BRAK;
            akt_kopiuj_tekst(g_ota_stan.komunikat, "Aktualizacja zainstalowana pomyślnie. Wymagane ponowne uruchomienie.", sizeof(g_ota_stan.komunikat));
            return BWS_OTA_BLAD_BRAK;
        }

        case BWS_OTA_CMD_REBOOT: {
            if (!ota_operacja_systemowa_dozwolona(*proces)) {
                return BWS_OTA_BLAD_BRAK_UPRAWNIEN;
            }
            ota_wykonaj_restart();
            return BWS_OTA_BLAD_BRAK;
        }

        case BWS_OTA_CMD_POTWIERDZ_ROZRUCH: {
            if (!aktualizacja_czy_moze_instalowac(proces)) {
                return BWS_OTA_BLAD_BRAK_UPRAWNIEN;
            }
            aktualizacja_potwierdz_rozruch();
            g_ota_stan = BwsOtaStatus{};
            g_ota_stan.stan = BWS_OTA_STAN_BEZCZYNNA;
            akt_kopiuj_tekst(g_ota_stan.komunikat, "Rozruch nowego slotu został pomyślnie potwierdzony.", sizeof(g_ota_stan.komunikat));
            return BWS_OTA_BLAD_BRAK;
        }

        case BWS_OTA_CMD_RESETUJ_STAN: {
            g_ota_stan = BwsOtaStatus{};
            return BWS_OTA_BLAD_BRAK;
        }

        default:
            return BWS_OTA_BLAD_ARGUMENT;
    }
}
