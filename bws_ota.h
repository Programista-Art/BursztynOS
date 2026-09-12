/*
 * Bursztyn OS - Interfejs wywolan systemowych aktualizacji OTA (BWS 84/85).
 *
 * Plik wspoldzielony pomiedzy jadrem a aplikacjami uzytkownika (Ring 3).
 * Zachowuje scisla zgodnosc binarna (ABI) i deterministyczny uklad bajtow.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#define BWS_OTA_ABI_WERSJA 1U

/* Stany ogolne cyklu zycia aktualizacji. */
enum BwsOtaStan : uint32_t {
    BWS_OTA_STAN_BEZCZYNNA         = 0,  /* System aktualny / gotowy do sprawdzenia */
    BWS_OTA_STAN_SPRAWDZANIE       = 1,  /* Trwa sprawdzanie dostepnosci aktualizacji */
    BWS_OTA_STAN_DOSTEPNA          = 2,  /* Dostepna nowa wersja systemu */
    BWS_OTA_STAN_POBIERANIE        = 3,  /* Pobieranie pakietu aktualizacji */
    BWS_OTA_STAN_POBRANA           = 4,  /* Pakiet pobrany i gotowy do instalacji */
    BWS_OTA_STAN_WERYFIKACJA       = 5,  /* Weryfikacja kryptograficzna i strukturalna */
    BWS_OTA_STAN_INSTALACJA        = 6,  /* Instalacja pakietu w slocie nieaktywnym */
    BWS_OTA_STAN_WYMAGA_RESTARTU   = 7,  /* Zainstalowano; wymagany ponowny rozruch */
    BWS_OTA_STAN_ROLLBACK_WYKRYTY  = 8,  /* Wykryto powrot po nieudanym rozruchu */
    BWS_OTA_STAN_BLAD              = 9   /* Wystapil blad (opis w kod_bledu i komunikacie) */
};

/* Stan dostepnosci podsystemu aktualizacji OTA. */
enum BwsOtaDostepnosc : uint32_t {
    OTA_AVAILABLE                    = 0,  /* Pelna obsluga A/B na zainstalowanym systemie */
    OTA_UNAVAILABLE_LIVE_ENVIRONMENT = 1,  /* Srodowisko live ISO / instalator */
    OTA_UNAVAILABLE_LEGACY_LAYOUT    = 2,  /* Dysk legacy / brak ukladu partycji A/B */
    OTA_INVALID_DISK_LAYOUT          = 3,  /* Naruszenie ukladu GPT lub LBA 2046/2047 */
    OTA_BACKEND_ERROR                = 4   /* Blad urzadzenia pamieci masowej lub backendu */
};

/* Polecenia przesylane przez aplikacje uzytkownika do jadra (BWS 85). */
enum BwsOtaPolecenie : uint32_t {
    BWS_OTA_CMD_BRAK               = 0,
    BWS_OTA_CMD_SPRAWDZ            = 1,  /* Sprawdz dostepnosc aktualizacji */
    BWS_OTA_CMD_POBIERZ            = 2,  /* Pobierz dostepna aktualizacje */
    BWS_OTA_CMD_INSTALUJ           = 3,  /* Zainstaluj zweryfikowana aktualizacje */
    BWS_OTA_CMD_REBOOT             = 4,  /* Bezpieczny restart systemu */
    BWS_OTA_CMD_POTWIERDZ_ROZRUCH  = 5,  /* Potwierdz pomyslny rozruch nowego slotu */
    BWS_OTA_CMD_RESETUJ_STAN       = 6   /* Reset stanu bledu/powiadomienia */
};

/* Kody bledow zwracane przez operacje OTA. */
enum BwsOtaBlad : uint32_t {
    BWS_OTA_BLAD_BRAK              = 0,  /* Brak bledu (sukces) */
    BWS_OTA_BLAD_BRAK_UPRAWNIEN    = 1,  /* Brak wymaganych uprawnien PZB */
    BWS_OTA_BLAD_ARGUMENT          = 2,  /* Nieprawidlowy argument wywolania */
    BWS_OTA_BLAD_BRAK_WYKAZU       = 3,  /* Brak pliku wykazu aktualizacji */
    BWS_OTA_BLAD_FORMATU_WYKAZU    = 4,  /* Blad skladniowy wykazu */
    BWS_OTA_BLAD_PODPISU           = 5,  /* Blad weryfikacji podpisu cyfrowego RSA */
    BWS_OTA_BLAD_WERSJI            = 6,  /* Proba downgrade lub nieprawidlowa wersja */
    BWS_OTA_BLAD_SIECI             = 7,  /* Blad polaczenia sieciowego/DNS */
    BWS_OTA_BLAD_BRAK_PAKIETU      = 8,  /* Nie odnaleziono pakietu instalacyjnego */
    BWS_OTA_BLAD_SHA256            = 9,  /* Blad sumy kontrolnej SHA-256 */
    BWS_OTA_BLAD_INSTALACJI        = 10, /* Blad zapisu na dysk / do slotu docelowego */
    BWS_OTA_BLAD_UKLADU_DYSKU      = 11, /* Uklad dysku narusza sektory LBA 2046/2047 */
    BWS_OTA_BLAD_STANU_ROZRUCHU    = 12  /* Blad aktualizacji sektorow stanu rozruchu */
};

/*
 * Struktura stanu OTA (dokladnie 384 bajty).
 * Pobierana przez wywolanie BWS_OTA_STATUS (84).
 */
struct BwsOtaStatus {
    uint32_t wersja_abi;             /* BWS_OTA_ABI_WERSJA (1) */
    uint32_t stan;                   /* BwsOtaStan */
    uint32_t kod_bledu;              /* BwsOtaBlad */
    uint32_t postep_procent;         /* 0..100 */

    uint8_t  aktywny_slot;           /* 0=Slot A, 1=Slot B */
    uint8_t  nieaktywny_slot;        /* 0=Slot A, 1=Slot B */
    uint8_t  slot_oczekujacy;        /* 0=Slot A, 1=Slot B, 0xFF=brak */
    uint8_t  stan_oczekujacego;      /* 0..4 (bursztyn_stan_slotu_t) */
    uint8_t  podpis_ok;              /* 1 jesli podpis wykazu jest poprawny */
    uint8_t  sha256_ok;              /* 1 jesli SHA-256 pakietu jest poprawne */
    uint8_t  restart_wymagany;       /* 1 jesli wymagany restart do nowego slotu */
    uint8_t  rollback_wykryty;       /* 1 jesli nastapil powrot po bledzie */

    uint64_t pobrano_bajtow;         /* Liczba pobranych bajtow pakietu */
    uint64_t rozmiar_pakietu;        /* Oczekiwany pelny rozmiar pakietu z wykazu */

    char     biezaca_wersja[32];     /* Tekst biezacej wersji jadra, np. "0.1.0" */
    char     dostepna_wersja[32];    /* Tekst dostepnej nowej wersji, np. "0.2.0" */
    char     kanal[32];              /* Nazwa kanalu, np. "stabilny" */
    char     plik_pakietu[64];       /* Nazwa pliku .akt, np. "bursztyn-0.2.0.akt" */
    char     komunikat[128];         /* Komunikat dla uzytkownika w jezyku polskim */
    uint32_t dostepnosc;             /* BwsOtaDostepnosc (OTA_AVAILABLE, ...) */
    uint8_t  padding[52];            /* Rezerwa na przyszle pola ABI */
};

#ifdef __cplusplus
static_assert(sizeof(BwsOtaStatus) == 384, "Rozmiar BwsOtaStatus musi wynosic dokladnie 384 bajty");
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BURSZTYN_KERNEL
extern uint64_t bws_wywolaj(uint64_t nr_funkcji, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);

/*
 * Pomocnicze funkcje wywolan systemowych Ring 3.
 */
inline bool bws_ota_pobierz_status(BwsOtaStatus* out) {
    if (!out) return false;
    return bws_wywolaj(84, reinterpret_cast<uint64_t>(out), 0, 0, 0) == 1ULL;
}

inline uint64_t bws_ota_wykonaj_polecenie(uint32_t polecenie, const char* arg = nullptr) {
    return bws_wywolaj(85, static_cast<uint64_t>(polecenie),
                       reinterpret_cast<uint64_t>(arg), 0, 0);
}
#endif

#ifdef __cplusplus
}
#endif
