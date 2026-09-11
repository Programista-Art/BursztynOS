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
#include "siec.h"
#include "heap.h"
#include "grafika.h"
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
                    break;
                }
                if (typ == 0) break;
                off = (off + rozmiar + 7ULL) & ~7ULL;
            }
        }
    }

    psf_ustaw_aktywny_slot(uruchomiony_slot);

    bursztyn_rozruch_stan stan{};
    bool stan_odczytany = false;
    Storage::Device* dev = blokowy_dysk_systemowy();
    if (dev) {
        stan_odczytany = Storage::rozruch_odczytaj_sektory(dev, &stan);
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

    if (uruchomiony_slot == 1) {
        akt_log("[ROZRUCH] Proba uruchomienia slotu B.");
        if (awaria_testowa) {
            akt_log("[TEST] Kontrolowana awaria przed potwierdzeniem slotu B.");
            asm volatile("cli");
            for (;;) {
                asm volatile("outb %0, $0x64" : : "a"((uint8_t)0xFE));
                asm volatile("hlt");
            }
        }
    } else {
        bool rollback_wykryty = false;
        if (grub_ok && (akt_porownaj_bajty(grub_oczek, "NIEUDANY", 8) == 0 ||
                        (grub_oczek[0] == 'B' && grub_proby >= 1))) {
            rollback_wykryty = true;
        } else if (stan_odczytany && stan.slot_oczekujacy == BURSZTYN_SLOT_B &&
                   (stan.stan_oczekujacego == BURSZTYN_STAN_NIEUDANY || stan.licznik_prob >= stan.maks_prob)) {
            rollback_wykryty = true;
        }

        if (rollback_wykryty) {
            akt_log("[ROZRUCH] Nieudane uruchomienie slotu B.");
            akt_log("[ROZRUCH] Powrot do slotu A.");
            if (stan_odczytany) {
                rozruch_wykonaj_rollback(&stan);
                if (dev) Storage::rozruch_zapisz_sektory(dev, &stan);
                (void)zapisz_do_pliku("/aktualizacje/stan_rozruchu.k0", (const char*)&stan, sizeof(stan));
                (void)zapisz_do_pliku("/aktualizacje/stan_rozruchu.k1", (const char*)&stan, sizeof(stan));
            }
            if (grub_ok) {
                Storage::esp_zapisz_grubenv(esp, "A", "BRAK", 0);
            }
        }
    }
}

bool aktualizacja_potwierdz_rozruch(void) {
    uint8_t running_slot = psf_pobierz_aktywny_slot();
    bursztyn_rozruch_stan stan{};
    bool stan_odczytany = false;
    Storage::Device* dev = blokowy_dysk_systemowy();
    if (dev) {
        stan_odczytany = Storage::rozruch_odczytaj_sektory(dev, &stan);
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
            Storage::rozruch_zapisz_sektory(dev, &stan);
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
    if (dev) stan_ok = Storage::rozruch_odczytaj_sektory(dev, &stan);
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
