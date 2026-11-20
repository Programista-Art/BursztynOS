#pragma once

#include <stdint.h>

/*
 * Jedno zrodlo numerow publicznego ABI BWS. Istniejace wartosci 1..60 sa
 * zamrozone; nowe wywolania sa przydzielane kolejno od pierwszego wolnego
 * numeru 61. Numer 0 nigdy nie byl obslugiwany przez dispatcher.
 */
enum BwsNumer : uint64_t {
    BWS_BRAK = 0,
    BWS_WYPISZ = 1,
    BWS_UTWORZ_PLIK = 2,
    BWS_ZAPISZ_PLIK = 3,
    BWS_POBIERZ_ZNAK = 4,
    BWS_CZYTAJ_PLIK = 5,
    BWS_LISTUJ_KATALOG = 6,
    BWS_USUN_TWOR = 7,
    BWS_ZMIEN_NAZWE = 8,
    BWS_CZAS_RTC = 9,
    BWS_URUCHOM_PROGRAM = 10,
    BWS_PING = 11,
    BWS_DNS_LEGACY = 12,
    BWS_HTTP_DO_PLIKU_LEGACY = 13,
    BWS_GUI_RYSUJ_OKNO = 14,
    BWS_GUI_TEKST = 15,
    BWS_GUI_WYCZYSC = 16,
    BWS_GUI_ODSWIEZ = 17,
    BWS_GUI_POBIERZ_MYSZ = 18,
    BWS_GUI_ODSWIEZ_PULPIT = 19,
    BWS_GUI_TEKST_KOLOR = 20,
    BWS_GUI_PROSTOKAT = 21,
    BWS_GUI_PRZEJECIE_MYSZY = 22,
    BWS_GUI_ROZDZIELCZOSC = 23,
    BWS_GUI_SZEROKOSC_ZNAKU = 24,
    BWS_RESET = 25,
    BWS_SHUTDOWN = 26,
    BWS_DZWIEK_TON = 27,
    BWS_DNS = 28,
    BWS_HTTP = 29,
    BWS_HTTPS = 30,
    BWS_TLS_CERTYFIKAT = 31,
    BWS_ZAKONCZ_PROCES = 32,
    BWS_GUI_UTWORZ_WARSTWE = 33,
    BWS_GUI_PRZESUN_WARSTWE = 34,
    BWS_ALOKUJ_STERTE = 35,
    BWS_GUI_CZY_ZAMKNIETO_POWLOKE = 36,
    BWS_ZDARZENIE_POLL = 37,
    BWS_ZDARZENIE_CZEKAJ = 38,
    BWS_GUI_CAPTURE = 39,
    BWS_GUI_SYSTEM_OVERLAY = 40,
    BWS_GUI_MINIMALIZUJ = 41,
    BWS_GUI_OKNA_SNAPSHOT = 42,
    BWS_GUI_AKTYWUJ_OKNO = 43,
    BWS_ROZMIAR_PLIKU = 44,
    BWS_ARGUMENT_STARTOWY = 45,
    BWS_UTWORZ_KATALOG = 46,
    BWS_METADANE_PLIKU = 47,
    BWS_PRZENIES_TWOR = 48,
    BWS_GUI_CELE_DROP = 49,
    BWS_GUI_AKTUALIZUJ_DRAG = 50,
    BWS_GUI_WYSOKOSC_FONTU = 51,
    BWS_SCHOWEK_PLIK_USTAW = 52,
    BWS_SCHOWEK_PLIK_POBIERZ = 53,
    BWS_SCHOWEK_PLIK_WYCZYSC = 54,
    BWS_KOPIUJ_TWOR = 55,
    BWS_GUI_POPUP_APLIKACJI = 56,
    BWS_GUI_BLIT = 57,
    BWS_GUI_TYTUL_OKNA = 58,
    BWS_CZYTAJ_FRAGMENT_PLIKU = 59,
    BWS_CZAS_MONOTONICZNY_NS = 60,

    BWS_GUI_GEOMETRIA = 61,
    BWS_GUI_MONITOR_INFO = 62,
    BWS_GUI_USTAW_WORK_AREA = 63,
    BWS_SHM_CREATE = 64,
    BWS_SHM_MAP = 65,
    BWS_SHM_UNMAP = 66,
    BWS_SHM_CLOSE = 67,
    BWS_IPC_CREATE = 68,
    BWS_IPC_SEND = 69,
    BWS_IPC_RECEIVE = 70,
    BWS_IPC_CLOSE = 71,
    BWS_FILE_WATCH = 72,
    BWS_CLIPBOARD_ENUM = 73,
    BWS_CLIPBOARD_SIZE = 74,
    BWS_CLIPBOARD_READ = 75,
    BWS_CLIPBOARD_SET = 76,
    BWS_GUI_CURSOR_SHAPE = 77,
    BWS_GUI_CURSOR_CUSTOM = 78,
    BWS_SOCKET_CREATE = 79,
    BWS_SOCKET_CONNECT = 80,
    BWS_SOCKET_SEND = 81,
    BWS_SOCKET_RECV = 82,
    BWS_SOCKET_CLOSE = 83,

    BWS_OTA_STATUS = 84,
    BWS_OTA_POLECENIE = 85,

    BWS_MOTYW_POBIERZ = 86,
    BWS_MOTYW_POLECENIE = 87,
    BWS_PULPIT_GOTOWY = 88,

    /*
     * Diagnostyka HDA (Test dźwięku). Znaczenie zamrozone.
     *  89 = wylacznie odczyt diagnostyczny / kontrola skanu
     *  90 = kontrolowany test odtwarzania
     * Brak syscalla z surowymi verbami HDA.
     */
    BWS_HDA_DIAG = 89,
    BWS_HDA_TEST = 90,

    BWS_DIAGNOSTYKA_SYSTEMU = 91,
    BWS_BSF_CONFIG = 92,
    BWS_GUI_TYTUL_ALFA = 93,
    BWS_HTTP64 = 94,
    BWS_HTTPS64 = 95,

    /*
     * Zarzadzanie procesami.
     *
     * BWS_PROCESY_LISTA wypelnia tablice BwsProcesInfo dla WSZYSTKICH
     * aktywnych slotow (nie wymyslonych danych). BWS_PROCES_ZABIJ konczy
     * proces o podanym PID - to jedyny sposob na zamkniecie zawieszonej
     * aplikacji bez restartu systemu.
     *
     * Wymagane: PRAWO_SYSTEM_CONFIG i PZB <= PZB_USLUGI.
     */
    BWS_PROCESY_LISTA = 96,
    BWS_PROCES_ZABIJ = 97,

    /*
     * BSF 2.2: zapis fragmentu istniejacego pliku od offsetu (<= 1 MiB na
     * wywolanie, offset <= biezacy rozmiar - bez dziur). Te same kontrole
     * PZB co BWS 3; pozwala kopiowac pliki > 16 MiB bez bufora calego pliku.
     */
    BWS_ZAPISZ_FRAGMENT_PLIKU = 98,
    /* 99 jest zarezerwowany jako trwale nieprawidlowy (historyczna regresja Bug #1:
     * BWS 99 + 0xDEADBEEFCAFE); dispatcher zawsze zwraca dla niego UINT64_MAX. */
    BWS_OSTATNI_BLAD_URUCHOMIENIA = 100,
    /* Okna podrzedne (modeless, wlasnosc wlasciciela): wymagaja PRAWO_OKNO_PODRZEDNE (jadro/bws/okna_podrzedne.h).
     * 101: (rodzaj, tresc*, dlugosc, tytul*) otwiera/aktywuje okno informacyjne; zwraca 1 = utworzono, 2 = aktywowano
     *      istniejace, 0 = odmowa. 102: (bufor*, pojemnosc) - tylko dla procesu okna podrzednego: pobiera tresc/tytul. */
    BWS_OKNO_PODRZEDNE_OTWORZ = 101,
    BWS_OKNO_PODRZEDNE_TRESC = 102,
    /* Menedzer zadan (jadro/bws/procesy_migawka.h, docs/menedzer-zadan.md): wymagaja PRAWO_ZARZADZANIE_PROCESAMI.
     * 103: (bufor*, rozmiar, wersja) migawka procesow z generacja, czasem CPU i pamiecia; 104: (pid, generacja)
     * zadanie zakonczenia procesu o dokladnie tej tozsamosci. */
    BWS_PROCESY_MIGAWKA = 103,
    BWS_PROCES_ZAKONCZ = 104,
    /* Telemetria urzadzen i I/O procesow (jadro/bws/urzadzenia_telemetria.h, docs/menedzer-zadan-monitorowanie.md):
     * to samo prawo co 103. 105: (bufor*, rozmiar, wersja) dyski, karty sieciowe, grafika; 106: (bufor*, rozmiar,
     * wersja) skumulowane liczniki plikow i sieci kazdej generacji procesu. Tylko odczyt. */
    BWS_URZADZENIA_TELEMETRIA = 105,
    BWS_PROCESY_IO = 106,
    /* Bursztynowy Zasobnik (jadro/bws/zasobnik_api.h, docs/bursztynowy-zasobnik.md). 107: (bufor*, rozmiar, wersja)
     * migawka stanu; 108: (polecenie, wartosc) glosnosc/wyciszenie/powiadomienia - oba tylko Menedzer Okien.
     * 109: (polecenie, bufor*, rozmiar) POBIERZ (Menedzer Okien) albo ZGLOS (PRAWO_GUI, limit, nadawca z jadra). */
    BWS_ZASOBNIK_STAN = 107,
    BWS_ZASOBNIK_POLECENIE = 108,
    BWS_POWIADOMIENIA = 109,
    BWS_PIERWSZY_WOLNY = 110
};

/* Additive ABI for buffers above 4 GiB. Arguments 1..3 retain BWS29/30
 * meanings; argument 4 points to this snapshot. flags bit 0 requests count+1. */
struct BwsBuforSieci64 {
    uint64_t adres;
    uint32_t pojemnosc;
    uint32_t flagi;
};
static_assert(sizeof(BwsBuforSieci64) == 16, "BWS HTTP64 ABI");
/*
 * BSF 2.2: flaga BWS_HTTP64_ORIGIN (bit 1) oznacza, ze arg4 wskazuje
 * BwsBuforSieci64Origin; jadro dodaje naglowek "Origin: <origin>"
 * (zserializowany origin, walidowany: jadro/siec/http_origin.h).
 * Bez flagi ABI jest dokladnie jak dotad (16 bajtow).
 */
constexpr uint32_t BWS_HTTP64_COUNTED = 1u, BWS_HTTP64_ORIGIN = 2u;
struct BwsBuforSieci64Origin {
    BwsBuforSieci64 bufor;
    uint64_t origin; /* const char*, NUL, <= 255 znakow */
};
static_assert(sizeof(BwsBuforSieci64Origin) == 24, "BWS HTTP64 origin ABI");

