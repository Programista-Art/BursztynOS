/*
 * Bursztyn OS - Aplikacja graficzna "Aktualizacje Systemu" (.cebula)
 *
 * Komponenty BursztynUI w przestrzeni uzytkownika (Ring 3).
 * Caly interfejs w jezyku polskim z pelnym zakresem znakow Unicode.
 */

#include "../../loader.h"
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "../../bws_ota.h"
#include "../../wersja_systemu.h"

using namespace BursztynUI;

extern "C" void _start();
static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 0x1000ULL;
static constexpr uint64_t BUR_TEKST_ROZMIAR = 0x30000ULL;
static constexpr uint64_t BUR_TEKST_WIRTUALNY = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE = 0x31000ULL;
static constexpr uint64_t BUR_DANE_ROZMIAR = 0x20000ULL;
static constexpr uint64_t BUR_DANE_WIRTUALNY = 0x1031000ULL;

extern "C" {
__attribute__((section(".naglowek"), used)) NaglowekBur naglowek = {
    {'B','U','R','\0'}, reinterpret_cast<uint64_t>(&_start),
    BUR_TEKST_PRZESUNIECIE, BUR_TEKST_ROZMIAR, BUR_TEKST_WIRTUALNY,
    BUR_DANE_PRZESUNIECIE, BUR_DANE_ROZMIAR, BUR_DANE_WIRTUALNY
};
}
static_assert(sizeof(NaglowekBur) == 60U, "Naglowek .bur musi miec 60 bajtow");
static_assert((BUR_TEKST_PRZESUNIECIE & 0xFFFULL) == 0, "BUR_TEKST_PRZESUNIECIE musi byc wyrownane do 4 KiB");
static_assert((BUR_TEKST_ROZMIAR & 0xFFFULL) == 0, "BUR_TEKST_ROZMIAR musi byc wyrownane do 4 KiB");
static_assert((BUR_TEKST_WIRTUALNY & 0xFFFULL) == 0, "BUR_TEKST_WIRTUALNY musi byc wyrownane do 4 KiB");
static_assert((BUR_DANE_PRZESUNIECIE & 0xFFFULL) == 0, "BUR_DANE_PRZESUNIECIE musi byc wyrownane do 4 KiB");
static_assert((BUR_DANE_ROZMIAR & 0xFFFULL) == 0, "BUR_DANE_ROZMIAR musi byc wyrownane do 4 KiB");
static_assert((BUR_DANE_WIRTUALNY & 0xFFFULL) == 0, "BUR_DANE_WIRTUALNY musi byc wyrownane do 4 KiB");
static_assert(BUR_TEKST_PRZESUNIECIE + BUR_TEKST_ROZMIAR == BUR_DANE_PRZESUNIECIE, "Segmenty w pliku musza byc ciagle");
static_assert(BUR_TEKST_WIRTUALNY + BUR_TEKST_ROZMIAR == BUR_DANE_WIRTUALNY, "Segmenty w VA musza byc ciagle");
static_assert(BUR_TEKST_ROZMIAR <= BUR_MAKS_TEKST_ROZMIAR, "BUR_TEKST_ROZMIAR przekracza maksimum");
static_assert(BUR_DANE_ROZMIAR <= BUR_MAKS_DANE_ROZMIAR, "BUR_DANE_ROZMIAR przekracza maksimum");
static_assert(BUR_TEKST_ROZMIAR + BUR_DANE_ROZMIAR <= BUR_MAKS_CALKOWITY_ROZMIAR, "Calkowity rozmiar przekracza limit");

namespace {

static void formatuj_rozmiar(uint64_t bajty, char* buf, size_t cap) {
    if (!buf || cap < 2) return;
    if (bajty == 0) {
        buf[0] = '0';
        buf[1] = ' ';
        buf[2] = 'B';
        buf[3] = '\0';
        return;
    }
    if (bajty < 1024ULL) {
        char tmp[16]; size_t p = 0;
        uint64_t n = bajty;
        do { tmp[p++] = static_cast<char>('0' + (n % 10)); n /= 10; } while (n > 0 && p < sizeof(tmp));
        size_t o = 0; while (p && o + 1 < cap) buf[o++] = tmp[--p];
        const char suf[] = " B";
        for (int i = 0; suf[i] && o + 1 < cap; ++i) buf[o++] = suf[i];
        buf[o] = '\0';
        return;
    }
    if (bajty < 1024ULL * 1024ULL) {
        uint64_t kib = bajty / 1024ULL;
        char tmp[16]; size_t p = 0;
        do { tmp[p++] = static_cast<char>('0' + (kib % 10)); kib /= 10; } while (kib > 0 && p < sizeof(tmp));
        size_t o = 0; while (p && o + 1 < cap) buf[o++] = tmp[--p];
        const char suf[] = " KiB";
        for (int i = 0; suf[i] && o + 1 < cap; ++i) buf[o++] = suf[i];
        buf[o] = '\0';
        return;
    }
    uint64_t mib_calosci = bajty / (1024ULL * 1024ULL);
    uint64_t mib_ulamek = ((bajty % (1024ULL * 1024ULL)) * 10ULL) / (1024ULL * 1024ULL);
    char tmp[16]; size_t p = 0;
    uint64_t n = mib_calosci;
    do { tmp[p++] = static_cast<char>('0' + (n % 10)); n /= 10; } while (n > 0 && p < sizeof(tmp));
    size_t o = 0; while (p && o + 1 < cap) buf[o++] = tmp[--p];
    if (o + 1 < cap) buf[o++] = ',';
    if (o + 1 < cap) buf[o++] = static_cast<char>('0' + (mib_ulamek % 10));
    const char suf[] = " MiB";
    for (int i = 0; suf[i] && o + 1 < cap; ++i) buf[o++] = suf[i];
    buf[o] = '\0';
}

struct StanGui {
    BwsOtaStatus status{};
    bool pobrano_poczatkowy_stan{false};
    uint64_t ostatnie_odswiezenie_ns{0};

    // Kontrolki UI
    Okno* okno{nullptr};
    Panel* baner_rollback{nullptr};
    Etykieta* wartosc_wersja_biezaca{nullptr};
    Etykieta* wartosc_slot_aktywny{nullptr};
    Etykieta* wartosc_slot_zapasowy{nullptr};
    Etykieta* wartosc_magazyn_stanu{nullptr};

    Etykieta* wartosc_wersja_dostepna{nullptr};
    Etykieta* wartosc_kanal{nullptr};
    Etykieta* wartosc_rozmiar{nullptr};
    Etykieta* wartosc_podpis{nullptr};
    Etykieta* wartosc_sha256{nullptr};

    Etykieta* etykieta_komunikat{nullptr};
    Etykieta* etykieta_szczegoly{nullptr};
    PasekPostepu* pasek_postepu{nullptr};

    Przycisk* przycisk_sprawdz{nullptr};
    Przycisk* przycisk_pobierz{nullptr};
    Przycisk* przycisk_instaluj{nullptr};
    Przycisk* przycisk_reboot{nullptr};

    PasekStanu* pasek_stanu{nullptr};
};

StanGui g_stan;

static void ustaw_wartosc_statusu(Etykieta* etykieta, const char* tekst, uint32_t kolor) {
    if (!etykieta) return;
    etykieta->ustaw_tekst(tekst && tekst[0] ? tekst : "—");
    etykieta->ustaw_kolor_tekstu(kolor);
    // Wartości statusu muszą być płaskim tekstem bez ramek/obramowań.
    etykieta->ustaw_grubosc_ramki(0);
    etykieta->ustaw_kolor_tla(0);
}

static void ustaw_przyciski_niedostepne() {
    if (g_stan.przycisk_sprawdz) g_stan.przycisk_sprawdz->ustaw_wlaczona(false);
    if (g_stan.przycisk_pobierz) g_stan.przycisk_pobierz->ustaw_wlaczona(false);
    if (g_stan.przycisk_instaluj) g_stan.przycisk_instaluj->ustaw_wlaczona(false);
    if (g_stan.przycisk_reboot) g_stan.przycisk_reboot->ustaw_wlaczona(false);
}

void odswiez_widok() {
#if BURSZTYN_OTA_TRACE
    bws_wywolaj(1, reinterpret_cast<uint64_t>("[OTA TRACE] aktualizacje calling BWS 84\n"), 0, 0, 0);
#endif
    BwsOtaStatus s{};
    if (!bws_ota_pobierz_status(&s)) {
        // Prawdziwy błąd BWS: zły wskaźnik Ring3, brak uprawnień PZB,
        // nieprawidłowe ABI albo awaria jądra. Nie dotyczy Live/Legacy,
        // bo backend zwraca wtedy poprawną strukturę z polem dostepnosc.
        if (g_stan.etykieta_komunikat) {
            g_stan.etykieta_komunikat->ustaw_tekst("Błąd komunikacji z podsystemem aktualizacji jądra (BWS).");
            g_stan.etykieta_komunikat->ustaw_kolor_tekstu(0x00FF4444);
        }
        if (g_stan.etykieta_szczegoly) {
            g_stan.etykieta_szczegoly->ustaw_tekst("Sprawdź uprawnienia aplikacji (PZB) i ponów próbę.");
            g_stan.etykieta_szczegoly->ustaw_kolor_tekstu(Kolory::TekstWyciszony);
        }
        ustaw_przyciski_niedostepne();
        return;
    }
    g_stan.status = s;
    g_stan.pobrano_poczatkowy_stan = true;

    // 1. Ostrzezenie powrotu (rollback)
    if (g_stan.baner_rollback) {
        g_stan.baner_rollback->ustaw_widoczna(s.rollback_wykryty != 0);
    }

    // 2. Sekcja biezacego systemu — zawsze widoczna, także w Live/Legacy.
    //    Każdy wiersz zawiera etykietę i wartość (nigdy pusty prostokąt).
    if (g_stan.wartosc_wersja_biezaca) {
        ustaw_wartosc_statusu(g_stan.wartosc_wersja_biezaca,
            s.biezaca_wersja[0] ? s.biezaca_wersja : BURSZTYN_WERSJA_TEKST, Kolory::AkcentZloty);
    }
    if (g_stan.wartosc_slot_aktywny) {
        if (s.aktywny_slot == 0) {
            ustaw_wartosc_statusu(g_stan.wartosc_slot_aktywny,
                "Slot A (potwierdzony i aktywny)", motyw_kolor(MOTYW_KOLOR_TEKST));
        } else {
            ustaw_wartosc_statusu(g_stan.wartosc_slot_aktywny,
                "Slot B (potwierdzony i aktywny)", motyw_kolor(MOTYW_KOLOR_TEKST));
        }
    }
    if (g_stan.wartosc_slot_zapasowy) {
        if (s.nieaktywny_slot == 1) {
            ustaw_wartosc_statusu(g_stan.wartosc_slot_zapasowy,
                "Slot B (zapasowy / docelowy)", motyw_kolor(MOTYW_KOLOR_TEKST));
        } else {
            ustaw_wartosc_statusu(g_stan.wartosc_slot_zapasowy,
                "Slot A (zapasowy / docelowy)", motyw_kolor(MOTYW_KOLOR_TEKST));
        }
    }
    if (g_stan.wartosc_magazyn_stanu) {
        if (s.dostepnosc == OTA_UNAVAILABLE_LIVE_ENVIRONMENT) {
            ustaw_wartosc_statusu(g_stan.wartosc_magazyn_stanu,
                "Niedostępny (Live)", Kolory::TekstWyciszony);
        } else {
            ustaw_wartosc_statusu(g_stan.wartosc_magazyn_stanu,
                "LBA 2046 i LBA 2047 (Bursztyn Boot State)", motyw_kolor(MOTYW_KOLOR_TEKST));
        }
    }

    // 2b. Stany niedostępności OTA: jasny polski komunikat, brak BWS-failure.
    //     Pobieranie i instalacja pozostają zablokowane.
    if (s.dostepnosc == OTA_UNAVAILABLE_LIVE_ENVIRONMENT) {
        if (g_stan.wartosc_wersja_dostepna)
            ustaw_wartosc_statusu(g_stan.wartosc_wersja_dostepna, "Niedostępna (Live)", Kolory::TekstWyciszony);
        if (g_stan.wartosc_kanal)
            ustaw_wartosc_statusu(g_stan.wartosc_kanal, "—", Kolory::TekstWyciszony);
        if (g_stan.wartosc_rozmiar)
            ustaw_wartosc_statusu(g_stan.wartosc_rozmiar, "—", Kolory::TekstWyciszony);
        if (g_stan.wartosc_podpis)
            ustaw_wartosc_statusu(g_stan.wartosc_podpis, "Niedostępny", Kolory::TekstWyciszony);
        if (g_stan.wartosc_sha256)
            ustaw_wartosc_statusu(g_stan.wartosc_sha256, "Niedostępna", Kolory::TekstWyciszony);
        if (g_stan.pasek_postepu) g_stan.pasek_postepu->ustaw_wartosc(0);
        if (g_stan.etykieta_komunikat) {
            g_stan.etykieta_komunikat->ustaw_tekst("Aktualizacje systemu są niedostępne w środowisku instalacyjnym.");
            g_stan.etykieta_komunikat->ustaw_kolor_tekstu(Kolory::AkcentZloty);
        }
        if (g_stan.etykieta_szczegoly) {
            g_stan.etykieta_szczegoly->ustaw_tekst("Zainstaluj Bursztyn OS na dysku, aby korzystać z aktualizacji.");
            g_stan.etykieta_szczegoly->ustaw_kolor_tekstu(Kolory::TekstWyciszony);
        }
        ustaw_przyciski_niedostepne();
        if (g_stan.pasek_stanu) {
            g_stan.pasek_stanu->ustaw_tekst(0, "Środowisko Live — zainstaluj system");
        }
        return;
    }
    if (s.dostepnosc == OTA_UNAVAILABLE_LEGACY_LAYOUT) {
        if (g_stan.wartosc_wersja_dostepna)
            ustaw_wartosc_statusu(g_stan.wartosc_wersja_dostepna, "Niedostępna (Legacy)", Kolory::TekstWyciszony);
        if (g_stan.wartosc_kanal)
            ustaw_wartosc_statusu(g_stan.wartosc_kanal, "—", Kolory::TekstWyciszony);
        if (g_stan.wartosc_rozmiar)
            ustaw_wartosc_statusu(g_stan.wartosc_rozmiar, "—", Kolory::TekstWyciszony);
        if (g_stan.wartosc_podpis)
            ustaw_wartosc_statusu(g_stan.wartosc_podpis, "Niedostępny", Kolory::TekstWyciszony);
        if (g_stan.wartosc_sha256)
            ustaw_wartosc_statusu(g_stan.wartosc_sha256, "Niedostępna", Kolory::TekstWyciszony);
        if (g_stan.pasek_postepu) g_stan.pasek_postepu->ustaw_wartosc(0);
        if (g_stan.etykieta_komunikat) {
            g_stan.etykieta_komunikat->ustaw_tekst("Ta instalacja Bursztyn OS nie obsługuje bezpiecznych aktualizacji A/B.");
            g_stan.etykieta_komunikat->ustaw_kolor_tekstu(Kolory::AkcentZloty);
        }
        if (g_stan.etykieta_szczegoly) {
            g_stan.etykieta_szczegoly->ustaw_tekst("Wymagana nowa instalacja z układem A/B (ESP + BSP).");
            g_stan.etykieta_szczegoly->ustaw_kolor_tekstu(Kolory::TekstWyciszony);
        }
        ustaw_przyciski_niedostepne();
        if (g_stan.pasek_stanu) {
            g_stan.pasek_stanu->ustaw_tekst(0, "Instalacja Legacy bez A/B");
        }
        return;
    }
    if (s.dostepnosc == OTA_INVALID_DISK_LAYOUT) {
        if (g_stan.wartosc_wersja_dostepna)
            ustaw_wartosc_statusu(g_stan.wartosc_wersja_dostepna, "Błąd układu dysku", 0x00FF4444);
        if (g_stan.wartosc_kanal)
            ustaw_wartosc_statusu(g_stan.wartosc_kanal, "—", Kolory::TekstWyciszony);
        if (g_stan.wartosc_rozmiar)
            ustaw_wartosc_statusu(g_stan.wartosc_rozmiar, "—", Kolory::TekstWyciszony);
        if (g_stan.wartosc_podpis)
            ustaw_wartosc_statusu(g_stan.wartosc_podpis, "Błąd", 0x00FF4444);
        if (g_stan.wartosc_sha256)
            ustaw_wartosc_statusu(g_stan.wartosc_sha256, "Błąd", 0x00FF4444);
        if (g_stan.pasek_postepu) g_stan.pasek_postepu->ustaw_wartosc(0);
        if (g_stan.etykieta_komunikat) {
            g_stan.etykieta_komunikat->ustaw_tekst("Układ dysku jest niezgodny z wymaganiami bezpiecznych aktualizacji A/B.");
            g_stan.etykieta_komunikat->ustaw_kolor_tekstu(0x00FF4444);
        }
        if (g_stan.etykieta_szczegoly) {
            g_stan.etykieta_szczegoly->ustaw_tekst("Wykryto naruszenie LBA 2046/2047 lub GPT. Aktualizacja zablokowana.");
            g_stan.etykieta_szczegoly->ustaw_kolor_tekstu(Kolory::TekstWyciszony);
        }
        ustaw_przyciski_niedostepne();
        if (g_stan.pasek_stanu) {
            g_stan.pasek_stanu->ustaw_tekst(0, "Błąd układu dysku (fail-closed)");
        }
        return;
    }
    if (s.dostepnosc == OTA_BACKEND_ERROR) {
        if (g_stan.wartosc_wersja_dostepna)
            ustaw_wartosc_statusu(g_stan.wartosc_wersja_dostepna, "Błąd backendu", 0x00FF4444);
        if (g_stan.wartosc_kanal)
            ustaw_wartosc_statusu(g_stan.wartosc_kanal, "—", Kolory::TekstWyciszony);
        if (g_stan.wartosc_rozmiar)
            ustaw_wartosc_statusu(g_stan.wartosc_rozmiar, "—", Kolory::TekstWyciszony);
        if (g_stan.wartosc_podpis)
            ustaw_wartosc_statusu(g_stan.wartosc_podpis, "Błąd", 0x00FF4444);
        if (g_stan.wartosc_sha256)
            ustaw_wartosc_statusu(g_stan.wartosc_sha256, "Błąd", 0x00FF4444);
        if (g_stan.pasek_postepu) g_stan.pasek_postepu->ustaw_wartosc(0);
        if (g_stan.etykieta_komunikat) {
            g_stan.etykieta_komunikat->ustaw_tekst(s.komunikat[0] ? s.komunikat : "Wewnętrzny błąd podsystemu aktualizacji.");
            g_stan.etykieta_komunikat->ustaw_kolor_tekstu(0x00FF4444);
        }
        if (g_stan.etykieta_szczegoly) {
            g_stan.etykieta_szczegoly->ustaw_tekst("Sprawdź dysk i uruchom system ponownie.");
            g_stan.etykieta_szczegoly->ustaw_kolor_tekstu(Kolory::TekstWyciszony);
        }
        ustaw_przyciski_niedostepne();
        if (g_stan.pasek_stanu) {
            g_stan.pasek_stanu->ustaw_tekst(0, "Błąd backendu OTA");
        }
        return;
    }

    // 3. Sekcja dostepnej aktualizacji (OTA_AVAILABLE)
    //    Wiersze: Dostępna wersja / Kanał / Rozmiar / Podpis / SHA-256.
    if (g_stan.wartosc_wersja_dostepna) {
        if (s.dostepna_wersja[0] != '\0') {
            char bufor_wersji[48];
            size_t p = 0;
            const char* pre = "Dostępna wersja ";
            while (*pre && p < sizeof(bufor_wersji) - 1) bufor_wersji[p++] = *pre++;
            for (size_t i = 0; s.dostepna_wersja[i] && p < sizeof(bufor_wersji) - 1; ++i)
                bufor_wersji[p++] = s.dostepna_wersja[i];
            bufor_wersji[p] = '\0';
            // Zachowaj samą wersję jako wartość (testy GUI wymagają frazy).
            ustaw_wartosc_statusu(g_stan.wartosc_wersja_dostepna, s.dostepna_wersja, Kolory::AkcentZloty);
            (void)bufor_wersji;
        } else {
            ustaw_wartosc_statusu(g_stan.wartosc_wersja_dostepna,
                "Brak (kliknij „Sprawdź aktualizacje”)", Kolory::TekstWyciszony);
        }
    }
    if (g_stan.wartosc_kanal) {
        if (s.kanal[0]) {
            char bufor_kanalu[48];
            size_t p = 0;
            const char* pre = "Kanał ";
            while (*pre && p < sizeof(bufor_kanalu) - 1) bufor_kanalu[p++] = *pre++;
            for (size_t i = 0; s.kanal[i] && p < sizeof(bufor_kanalu) - 1; ++i)
                bufor_kanalu[p++] = s.kanal[i];
            bufor_kanalu[p] = '\0';
            (void)bufor_kanalu;
            ustaw_wartosc_statusu(g_stan.wartosc_kanal, s.kanal, motyw_kolor(MOTYW_KOLOR_TEKST));
        } else {
            ustaw_wartosc_statusu(g_stan.wartosc_kanal, "stabilny", motyw_kolor(MOTYW_KOLOR_TEKST));
        }
    }
    if (g_stan.wartosc_rozmiar) {
        if (s.rozmiar_pakietu > 0) {
            char rz[32];
            formatuj_rozmiar(s.rozmiar_pakietu, rz, sizeof(rz));
            ustaw_wartosc_statusu(g_stan.wartosc_rozmiar, rz, motyw_kolor(MOTYW_KOLOR_TEKST));
        } else {
            ustaw_wartosc_statusu(g_stan.wartosc_rozmiar, "—", Kolory::TekstWyciszony);
        }
    }
    if (g_stan.wartosc_podpis) {
        if (s.podpis_ok) {
            ustaw_wartosc_statusu(g_stan.wartosc_podpis,
                "Podpis poprawny (RSA-2048)", 0x0020C040);
        } else if (s.stan == BWS_OTA_STAN_BLAD && s.kod_bledu == BWS_OTA_BLAD_PODPISU) {
            ustaw_wartosc_statusu(g_stan.wartosc_podpis,
                "BŁĄD WERYFIKACJI PODPISU!", 0x00FF4444);
        } else {
            ustaw_wartosc_statusu(g_stan.wartosc_podpis,
                "Oczekuje na weryfikację", Kolory::TekstWyciszony);
        }
    }
    if (g_stan.wartosc_sha256) {
        if (s.sha256_ok) {
            ustaw_wartosc_statusu(g_stan.wartosc_sha256,
                "Zgodna z wykazem (SHA-256)", 0x0020C040);
        } else if (s.stan == BWS_OTA_STAN_BLAD && s.kod_bledu == BWS_OTA_BLAD_SHA256) {
            ustaw_wartosc_statusu(g_stan.wartosc_sha256,
                "BŁĄD SUMY SHA-256 PAKIETU!", 0x00FF4444);
        } else {
            ustaw_wartosc_statusu(g_stan.wartosc_sha256,
                "Oczekuje", Kolory::TekstWyciszony);
        }
    }

    // 4. Pasek postepu i komunikat (Stan: gotowa do instalacji itd.)
    if (g_stan.pasek_postepu) {
        g_stan.pasek_postepu->ustaw_wartosc(s.postep_procent);
    }
    if (g_stan.etykieta_komunikat) {
        if (s.komunikat[0] != '\0') {
            g_stan.etykieta_komunikat->ustaw_tekst(s.komunikat);
        } else {
            g_stan.etykieta_komunikat->ustaw_tekst("Gotowy do sprawdzenia dostępności aktualizacji.");
        }
        if (s.stan == BWS_OTA_STAN_BLAD) {
            g_stan.etykieta_komunikat->ustaw_kolor_tekstu(0x00FF4444);
        } else if (s.stan == BWS_OTA_STAN_WYMAGA_RESTARTU) {
            g_stan.etykieta_komunikat->ustaw_kolor_tekstu(Kolory::AkcentZloty);
        } else {
            g_stan.etykieta_komunikat->ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST));
        }
    }
    if (g_stan.etykieta_szczegoly) {
        // W trybie dostępnym druga linia jest pusta (ukryta treść).
        g_stan.etykieta_szczegoly->ustaw_tekst("");
    }

    // 5. Scisly przeplyw przyciskow (Strict Flow) — tylko gdy OTA_AVAILABLE.
    bool w_toku = (s.stan == BWS_OTA_STAN_SPRAWDZANIE ||
                   s.stan == BWS_OTA_STAN_POBIERANIE ||
                   s.stan == BWS_OTA_STAN_INSTALACJA);
    if (g_stan.przycisk_sprawdz) {
        g_stan.przycisk_sprawdz->ustaw_wlaczona(!w_toku && s.stan != BWS_OTA_STAN_WYMAGA_RESTARTU);
    }
    if (g_stan.przycisk_pobierz) {
        g_stan.przycisk_pobierz->ustaw_wlaczona(!w_toku && s.stan == BWS_OTA_STAN_DOSTEPNA);
    }
    if (g_stan.przycisk_instaluj) {
        g_stan.przycisk_instaluj->ustaw_wlaczona(!w_toku && s.stan == BWS_OTA_STAN_POBRANA);
    }
    if (g_stan.przycisk_reboot) {
        g_stan.przycisk_reboot->ustaw_wlaczona(s.stan == BWS_OTA_STAN_WYMAGA_RESTARTU || s.restart_wymagany != 0);
    }

    // 6. Pasek stanu
    if (g_stan.pasek_stanu) {
        if (s.stan == BWS_OTA_STAN_WYMAGA_RESTARTU) {
            g_stan.pasek_stanu->ustaw_tekst(0, "Wymagany restart systemu do nowego slotu");
        } else if (s.stan == BWS_OTA_STAN_BLAD) {
            g_stan.pasek_stanu->ustaw_tekst(0, "Wystąpił błąd");
        } else if (s.stan == BWS_OTA_STAN_DOSTEPNA) {
            g_stan.pasek_stanu->ustaw_tekst(0, "Dostępna nowa aktualizacja");
        } else if (s.stan == BWS_OTA_STAN_POBRANA) {
            g_stan.pasek_stanu->ustaw_tekst(0, "Pakiet pobrany - gotowy do instalacji");
        } else {
            g_stan.pasek_stanu->ustaw_tekst(0, "Gotowy");
        }
    }
}

void klik_sprawdz(Przycisk*, void*) {
    if (g_stan.status.dostepnosc != OTA_AVAILABLE) return;
    if (g_stan.etykieta_komunikat) {
        g_stan.etykieta_komunikat->ustaw_tekst("Sprawdzanie dostępności aktualizacji...");
    }
    if (g_stan.pasek_postepu) {
        g_stan.pasek_postepu->ustaw_wartosc(10);
    }
    (void)bws_ota_wykonaj_polecenie(BWS_OTA_CMD_SPRAWDZ);
    odswiez_widok();
}

void klik_pobierz(Przycisk*, void*) {
    if (g_stan.status.dostepnosc != OTA_AVAILABLE) return;
    if (g_stan.etykieta_komunikat) {
        g_stan.etykieta_komunikat->ustaw_tekst("Pobieranie pakietu aktualizacji...");
    }
    if (g_stan.pasek_postepu) {
        g_stan.pasek_postepu->ustaw_wartosc(50);
    }
    (void)bws_ota_wykonaj_polecenie(BWS_OTA_CMD_POBIERZ);
    odswiez_widok();
}

void klik_instaluj(Przycisk*, void*) {
    if (g_stan.status.dostepnosc != OTA_AVAILABLE) return;
    if (g_stan.etykieta_komunikat) {
        g_stan.etykieta_komunikat->ustaw_tekst("Instalowanie aktualizacji w slocie zapasowym...");
    }
    if (g_stan.pasek_postepu) {
        g_stan.pasek_postepu->ustaw_wartosc(80);
    }
    (void)bws_ota_wykonaj_polecenie(BWS_OTA_CMD_INSTALUJ);
    odswiez_widok();
}

void klik_reboot(Przycisk*, void*) {
    if (g_stan.status.dostepnosc != OTA_AVAILABLE) return;
    if (g_stan.etykieta_komunikat) {
        g_stan.etykieta_komunikat->ustaw_tekst("Restartowanie systemu do nowego slotu...");
    }
    (void)bws_ota_wykonaj_polecenie(BWS_OTA_CMD_REBOOT);
}

class PanelGlownyAktualizacji : public Panel {
public:
    explicit PanelGlownyAktualizacji(Orientacja o = Orientacja::Pionowa) : Panel(o) {}

    bool czy_wymaga_animacji() const override {
        // Ciągła animacja i pętla aktualizacji są aktywne WYŁĄCZNIE wtedy,
        // gdy trwa operacja w toku (sprawdzanie, pobieranie, weryfikacja, instalacja).
        // W stanie jałowym / bezczynnym animacja jest wyłączona (0 wywołań BWS 84/s).
        uint8_t stan = g_stan.status.stan;
        return (stan == BWS_OTA_STAN_SPRAWDZANIE ||
                stan == BWS_OTA_STAN_POBIERANIE ||
                stan == BWS_OTA_STAN_WERYFIKACJA ||
                stan == BWS_OTA_STAN_INSTALACJA);
    }

    void krok_animacji(uint64_t teraz_ns) override {
        Panel::krok_animacji(teraz_ns);
        if (teraz_ns - g_stan.ostatnie_odswiezenie_ns >= 250000000ULL || !g_stan.pobrano_poczatkowy_stan) {
            g_stan.ostatnie_odswiezenie_ns = teraz_ns;
            odswiez_widok();
        }
    }
};

} // namespace

extern "C" __attribute__((noreturn)) void _start() {
#if BURSZTYN_OTA_TRACE
    bws_wywolaj(1, reinterpret_cast<uint64_t>("[OTA TRACE] aktualizacje Ring 3 main entered\n"), 0, 0, 0);
#endif
    AplikacjaGUI app;
    // Okno zwymiarowane pod 1280x720: szerokie na pełne polskie etykiety,
    // wystarczająco wysokie na 2x4 + 5 wierszy statusu + operacje + pasek.
    // Czcionka extronic16Bunicode.h ma 16 px wysokości — wiersze 24 px
    // dają 4 px zapasu na linię bazową, przyciski 36 px mieszczą tekst
    // 155 px ("Zainstaluj aktualizację") bez przycinania.
    Okno okno("Aktualizacje Systemu", 920, 640);
    g_stan.okno = &okno;

    PanelGlownyAktualizacji& korzen = okno.dodaj<PanelGlownyAktualizacji>(Orientacja::Pionowa);
    korzen.ustaw_wypelnienie(Marginesy(12, 12, 12, 12));
    korzen.ustaw_odstep(8);

    // 1. Naglowek — 52 px: 2x22 px etykiety + 4 px odstępu + zapas.
    //     Zapobiega przycinaniu pionowemu tytułu i podtytułu (font 16 px).
    Panel& p_naglowek = korzen.dodaj<Panel>(Orientacja::Pionowa);
    p_naglowek.ustaw_odstep(4);
    p_naglowek.ustaw_preferowany_rozmiar(Rozmiar(-1, 52));
    Etykieta& tytul = p_naglowek.dodaj<Etykieta>("Aktualizacje Systemu Bursztyn OS");
    tytul.ustaw_preferowany_rozmiar(Rozmiar(-1, 22));
    tytul.ustaw_kolor_tekstu(Kolory::AkcentZloty);
    tytul.ustaw_grubosc_ramki(0);
    Etykieta& podtytul = p_naglowek.dodaj<Etykieta>("Bezpieczny podsystem aktualizacji A/B ze sprawdzaniem integralności i powrotem.");
    podtytul.ustaw_preferowany_rozmiar(Rozmiar(-1, 22));
    podtytul.ustaw_kolor_tekstu(Kolory::TekstWyciszony);
    podtytul.ustaw_grubosc_ramki(0);

    // 2. Ostrzezenie Rollback (domyslnie ukryte) — 64 px na 2 linie.
    Panel& baner_rb = korzen.dodaj<Panel>(Orientacja::Pionowa);
    baner_rb.ustaw_kolor_tla(0x003D1414);
    baner_rb.ustaw_promien_zaokraglenia(4);
    baner_rb.ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
    baner_rb.ustaw_preferowany_rozmiar(Rozmiar(-1, 64));
    baner_rb.ustaw_odstep(4);
    Etykieta& rb_t1 = baner_rb.dodaj<Etykieta>("Ostrzeżenie: Wykryto automatyczny powrót (rollback) po nieudanym rozruchu!");
    rb_t1.ustaw_preferowany_rozmiar(Rozmiar(-1, 22));
    rb_t1.ustaw_kolor_tekstu(0x00FF6666);
    rb_t1.ustaw_grubosc_ramki(0);
    Etykieta& rb_t2 = baner_rb.dodaj<Etykieta>("Poprzednia próba uruchomienia slotu docelowego nie powiodła się. Przywrócono stabilną wersję.");
    rb_t2.ustaw_preferowany_rozmiar(Rozmiar(-1, 22));
    rb_t2.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST));
    rb_t2.ustaw_grubosc_ramki(0);
    baner_rb.ustaw_widoczna(false);
    g_stan.baner_rollback = &baner_rb;

    // 3. Sekcja stanu biezacego — wysokość automatyczna (4x24 + odstępy
    //    + padding 22/10 grupie). Brak sztywnego 105 px które przycinało.
    RamkaGrupy& g_biezaca = korzen.dodaj<RamkaGrupy>("Stan bieżący systemu");
    g_biezaca.ustaw_odstep(4);

    auto dodaj_wiersz = [](Panel& rodzic, const char* etykieta, Etykieta*& out_wartosc) {
        Panel& row = rodzic.dodaj<Panel>(Orientacja::Pozioma);
        // Wiersz 24 px: etykieta/wartość 22 px + 2 px zapasu na baseline 16 px.
        row.ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
        row.ustaw_odstep(12);
        Etykieta& l = row.dodaj<Etykieta>(etykieta);
        // Etykieta 280 px mieści najdłuższą "Bieżąca wersja jądra i systemu:" (239 px).
        l.ustaw_preferowany_rozmiar(Rozmiar(280, 22));
        l.ustaw_minimalny_rozmiar(Rozmiar(240, 22));
        l.ustaw_kolor_tekstu(Kolory::TekstWyciszony);
        l.ustaw_grubosc_ramki(0);
        l.ustaw_kolor_tla(0);
        Etykieta& v = row.dodaj<Etykieta>("—");
        // Wartość rozciąga się na resztę szerokości klienta (Rozciagnij),
        // nigdy nie jest pustym obramowanym prostokątem.
        v.ustaw_preferowany_rozmiar(Rozmiar(-1, 22));
        v.ustaw_minimalny_rozmiar(Rozmiar(200, 22));
        v.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        v.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST));
        v.ustaw_grubosc_ramki(0);
        v.ustaw_kolor_tla(0);
        out_wartosc = &v;
    };

    dodaj_wiersz(g_biezaca, "Bieżąca wersja jądra i systemu:", g_stan.wartosc_wersja_biezaca);
    g_stan.wartosc_wersja_biezaca->ustaw_kolor_tekstu(Kolory::AkcentZloty);
    dodaj_wiersz(g_biezaca, "Aktywny slot rozruchowy:", g_stan.wartosc_slot_aktywny);
    dodaj_wiersz(g_biezaca, "Zapasowy slot aktualizacji:", g_stan.wartosc_slot_zapasowy);
    dodaj_wiersz(g_biezaca, "Magazyn stanu rozruchu:", g_stan.wartosc_magazyn_stanu);

    // 4. Sekcja dostepnej aktualizacji — 5 wierszy, wysokość automatyczna.
    RamkaGrupy& g_dostepna = korzen.dodaj<RamkaGrupy>("Dostępna aktualizacja");
    g_dostepna.ustaw_odstep(4);

    dodaj_wiersz(g_dostepna, "Dostępna nowa wersja:", g_stan.wartosc_wersja_dostepna);
    dodaj_wiersz(g_dostepna, "Kanał aktualizacji:", g_stan.wartosc_kanal);
    dodaj_wiersz(g_dostepna, "Rozmiar pakietu (.akt):", g_stan.wartosc_rozmiar);
    dodaj_wiersz(g_dostepna, "Podpis cyfrowy RSA-2048:", g_stan.wartosc_podpis);
    dodaj_wiersz(g_dostepna, "Integralność SHA-256:", g_stan.wartosc_sha256);

    // 5. Sekcja operacji i postepu — rozciąga się na wolne miejsce.
    RamkaGrupy& g_akcje = korzen.dodaj<RamkaGrupy>("Operacje i postęp");
    g_akcje.ustaw_odstep(8);
    g_akcje.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);

    // Stan operacji: dwie linie po 22 px mieszczą długie polskie komunikaty
    // Live ("...niedostępne w środowisku instalacyjnym." + "Zainstaluj ...").
    Etykieta& lb_kom = g_akcje.dodaj<Etykieta>("Gotowy do sprawdzenia dostępności aktualizacji.");
    lb_kom.ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
    lb_kom.ustaw_minimalny_rozmiar(Rozmiar(0, 24));
    lb_kom.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST));
    lb_kom.ustaw_grubosc_ramki(0);
    g_stan.etykieta_komunikat = &lb_kom;

    Etykieta& lb_szcz = g_akcje.dodaj<Etykieta>("");
    lb_szcz.ustaw_preferowany_rozmiar(Rozmiar(-1, 22));
    lb_szcz.ustaw_minimalny_rozmiar(Rozmiar(0, 22));
    lb_szcz.ustaw_kolor_tekstu(Kolory::TekstWyciszony);
    lb_szcz.ustaw_grubosc_ramki(0);
    g_stan.etykieta_szczegoly = &lb_szcz;

    PasekPostepu& bar = g_akcje.dodaj<PasekPostepu>(0, 100);
    bar.ustaw_preferowany_rozmiar(Rozmiar(-1, 26));
    bar.ustaw_minimalny_rozmiar(Rozmiar(0, 26));
    bar.ustaw_pokazywanie_procentu(true);
    g_stan.pasek_postepu = &bar;

    // Przyciski: panel 40 px, przyciski 36 px, rozciągane na całą szerokość
    // klienta (Rozciagnij) z minimum 180-200 px na pełne polskie etykiety.
    // Brak sztywnych 150 px które przycinały "Zainstaluj aktualizację" (195 px).
    Panel& r_przyciski = g_akcje.dodaj<Panel>(Orientacja::Pozioma);
    r_przyciski.ustaw_preferowany_rozmiar(Rozmiar(-1, 40));
    r_przyciski.ustaw_minimalny_rozmiar(Rozmiar(0, 40));
    r_przyciski.ustaw_odstep(10);

    auto konfiguruj_przycisk = [](Przycisk& b, int min_szer) {
        b.ustaw_preferowany_rozmiar(Rozmiar(-1, 36));
        b.ustaw_minimalny_rozmiar(Rozmiar(min_szer, 36));
        b.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        b.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Srodek);
        // Wyłączone przyciski korzystają z motywu WylaczonyTekst (0x9E9E9E
        // na 0x2A2420) — czytelne, bez ręcznego przyciemniania tekstu.
    };

    Przycisk& b_spr = r_przyciski.dodaj<Przycisk>("Sprawdź aktualizacje");
    konfiguruj_przycisk(b_spr, 190);
    b_spr.przy_kliknieciu(klik_sprawdz);
    g_stan.przycisk_sprawdz = &b_spr;

    Przycisk& b_pob = r_przyciski.dodaj<Przycisk>("Pobierz aktualizację");
    konfiguruj_przycisk(b_pob, 190);
    b_pob.przy_kliknieciu(klik_pobierz);
    b_pob.ustaw_wlaczona(false);
    g_stan.przycisk_pobierz = &b_pob;

    Przycisk& b_ins = r_przyciski.dodaj<Przycisk>("Zainstaluj aktualizację");
    konfiguruj_przycisk(b_ins, 200);
    b_ins.przy_kliknieciu(klik_instaluj);
    b_ins.ustaw_wlaczona(false);
    g_stan.przycisk_instaluj = &b_ins;

    Przycisk& b_reb = r_przyciski.dodaj<Przycisk>("Uruchom ponownie");
    konfiguruj_przycisk(b_reb, 180);
    b_reb.przy_kliknieciu(klik_reboot);
    b_reb.ustaw_wlaczona(false);
    g_stan.przycisk_reboot = &b_reb;

    // 6. Pasek stanu — 26 px (font 16 + padding), sekcja rozciągana na komunikaty.
    PasekStanu& sb = korzen.dodaj<PasekStanu>();
    sb.ustaw_preferowany_rozmiar(Rozmiar(-1, 26));
    sb.ustaw_minimalny_rozmiar(Rozmiar(0, 26));
    sb.dodaj_sekcje("Gotowy", 420, RozmiarSekcjiStanu::Rozciagnij);
    sb.dodaj_sekcje("Bursztyn OS A/B", 180, RozmiarSekcjiStanu::Stala);
    g_stan.pasek_stanu = &sb;

    odswiez_widok();

    (void)app.uruchom(okno);
    bws_zakoncz_proces();
}
