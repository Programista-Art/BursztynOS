/*
 * Bursztyn OS - http_kody.h
 *
 * Bezpieczne funkcje pomocnicze dla odpowiedzi HTTP:
 *
 *   - odczyt 3-cyfrowego status code z linii HTTP/1.x,
 *   - opis popularnych kodow HTTP,
 *   - skladanie prostej strony HTML bledu do bufora o znanej pojemnosci.
 *
 * Ten plik jest naglowkiem header-only. Wszystkie funkcje maja inline,
 * dzieki czemu mozna go dolaczyc do wielu jednostek translacji bez
 * wielokrotnych definicji podczas linkowania.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * 1. STALE
 * ========================================================================= */

#define HTTP_KOD_NIEPRAWIDLOWY 0

#define HTTP_KOD_MIN 100
#define HTTP_KOD_MAX 599

/*
 * Maksymalna liczba bajtow badana przez wygodny wariant:
 *
 *   wyciagnij_kod_http(const char*)
 *
 * Status-line HTTP jest krotka. Limit chroni przed nieograniczonym
 * skanowaniem uszkodzonego lub niezaterminowanego bufora.
 *
 * Jezeli wywolujacy zna rzeczywista liczbe odebranych bajtow, powinien
 * uzyc wariantu:
 *
 *   wyciagnij_kod_http(bufor, dlugosc)
 */
#define HTTP_MAKS_SKAN_STATUSU ((size_t)1024)

/* =========================================================================
 * 2. PRYWATNE HELPERY HEADER-ONLY
 * ========================================================================= */

#ifdef __cplusplus

namespace bursztyn_http_detail {

inline constexpr bool jest_cyfra(
    char c
) noexcept {
    return
        c >= '0' &&
        c <= '9';
}

inline constexpr bool jest_spacja_statusu(
    char c
) noexcept {
    return
        c == ' ' ||
        c == '\t';
}

inline constexpr bool jest_koniec_statusu(
    char c
) noexcept {
    return
        c == '\0' ||
        c == '\r' ||
        c == '\n';
}

inline constexpr bool kod_poprawny(
    int kod
) noexcept {
    return
        kod >= HTTP_KOD_MIN &&
        kod <= HTTP_KOD_MAX;
}

/*
 * Minimalny builder bufora.
 *
 * Zawsze utrzymuje NUL na koncu, jezeli:
 *
 *   bufor != nullptr && pojemnosc > 0
 *
 * Po pierwszym overflow ustawia blad i nie wykonuje dalszych zapisow poza
 * zakresem. Czesciowy wynik pozostaje poprawnie zakonczonym C-stringiem.
 */
struct Builder {
    char* bufor;
    size_t pojemnosc;
    size_t pozycja;
    bool blad;

    inline Builder(
        char* b,
        size_t n
    ) noexcept
        : bufor(b),
          pojemnosc(n),
          pozycja(0),
          blad(false) {

        if (!bufor ||
            pojemnosc == 0) {

            blad = true;
            return;
        }

        bufor[0] = '\0';
    }

    inline bool dopisz_znak(
        char c
    ) noexcept {
        if (blad) {
            return false;
        }

        if (pozycja + 1U >=
            pojemnosc) {

            blad = true;

            /*
             * Poprzedni bajt NUL pozostaje w buforze.
             */
            return false;
        }

        bufor[pozycja++] =
            c;

        bufor[pozycja] =
            '\0';

        return true;
    }

    inline bool dopisz_tekst(
        const char* tekst
    ) noexcept {
        if (!tekst) {
            return true;
        }

        for (size_t i = 0;
             tekst[i] != '\0';
             ++i) {

            if (!dopisz_znak(
                    tekst[i])) {

                return false;
            }
        }

        return true;
    }

    /*
     * Escaping tekstu umieszczanego wewnatrz HTML.
     *
     * Chroni przed wstrzyknieciem znacznikow, gdy opis pochodzi z warstwy
     * sieciowej lub innego dynamicznego zrodla.
     */
    inline bool dopisz_html_escaped(
        const char* tekst
    ) noexcept {
        if (!tekst) {
            return true;
        }

        for (size_t i = 0;
             tekst[i] != '\0';
             ++i) {

            switch (tekst[i]) {
                case '&':
                    if (!dopisz_tekst("&amp;")) {
                        return false;
                    }
                    break;

                case '<':
                    if (!dopisz_tekst("&lt;")) {
                        return false;
                    }
                    break;

                case '>':
                    if (!dopisz_tekst("&gt;")) {
                        return false;
                    }
                    break;

                case '"':
                    if (!dopisz_tekst("&quot;")) {
                        return false;
                    }
                    break;

                case '\'':
                    if (!dopisz_tekst("&#39;")) {
                        return false;
                    }
                    break;

                default:
                    if (!dopisz_znak(
                            tekst[i])) {

                        return false;
                    }
                    break;
            }
        }

        return true;
    }
};

inline bool dopisz_kod_http(
    Builder& b,
    int kod
) noexcept {
    if (!kod_poprawny(kod)) {
        return
            b.dopisz_tekst(
                "???"
            );
    }

    return
        b.dopisz_znak(
            static_cast<char>(
                '0' +
                ((kod / 100) % 10)
            )
        ) &&
        b.dopisz_znak(
            static_cast<char>(
                '0' +
                ((kod / 10) % 10)
            )
        ) &&
        b.dopisz_znak(
            static_cast<char>(
                '0' +
                (kod % 10)
            )
        );
}

} // namespace bursztyn_http_detail

/* =========================================================================
 * 3. PARSER STATUS CODE
 * ========================================================================= */

/*
 * Bezpieczny wariant parsera, gdy wywolujacy zna liczbe dostepnych bajtow.
 *
 * Akceptowane przyklady:
 *
 *   HTTP/1.0 200 OK
 *   HTTP/1.1 404 Not Found
 *
 * Parser:
 *   - wymaga prefiksu "HTTP/",
 *   - wymaga numerycznej wersji major.minor,
 *   - wymaga spacji/tabulatora przed kodem,
 *   - wymaga DOKLADNIE trzech cyfr statusu,
 *   - odrzuca np. "HTTP/1.1 20X" i "HTTP/1.1 2000",
 *   - nie czyta poza `dlugosc`.
 *
 * Zwraca:
 *   100..599 - poprawny kod,
 *   0        - brak poprawnej linii statusu.
 */
inline int wyciagnij_kod_http(
    const char* odpowiedz,
    size_t dlugosc
) noexcept {
    using namespace bursztyn_http_detail;

    if (!odpowiedz ||
        dlugosc < 12U) {

        return
            HTTP_KOD_NIEPRAWIDLOWY;
    }

    if (odpowiedz[0] != 'H' ||
        odpowiedz[1] != 'T' ||
        odpowiedz[2] != 'T' ||
        odpowiedz[3] != 'P' ||
        odpowiedz[4] != '/') {

        return
            HTTP_KOD_NIEPRAWIDLOWY;
    }

    size_t i =
        5U;

    /*
     * Wersja major.
     */
    if (i >= dlugosc ||
        !jest_cyfra(
            odpowiedz[i])) {

        return
            HTTP_KOD_NIEPRAWIDLOWY;
    }

    while (i < dlugosc &&
           jest_cyfra(
               odpowiedz[i])) {

        ++i;
    }

    if (i >= dlugosc ||
        odpowiedz[i] != '.') {

        return
            HTTP_KOD_NIEPRAWIDLOWY;
    }

    ++i;

    /*
     * Wersja minor.
     */
    if (i >= dlugosc ||
        !jest_cyfra(
            odpowiedz[i])) {

        return
            HTTP_KOD_NIEPRAWIDLOWY;
    }

    while (i < dlugosc &&
           jest_cyfra(
               odpowiedz[i])) {

        ++i;
    }

    /*
     * Co najmniej jedna spacja/tabulator przed kodem.
     */
    if (i >= dlugosc ||
        !jest_spacja_statusu(
            odpowiedz[i])) {

        return
            HTTP_KOD_NIEPRAWIDLOWY;
    }

    while (i < dlugosc &&
           jest_spacja_statusu(
               odpowiedz[i])) {

        ++i;
    }

    /*
     * Potrzebujemy dokladnie trzech dostepnych cyfr.
     */
    if (i + 3U >
        dlugosc) {

        return
            HTTP_KOD_NIEPRAWIDLOWY;
    }

    if (!jest_cyfra(
            odpowiedz[i + 0U]) ||
        !jest_cyfra(
            odpowiedz[i + 1U]) ||
        !jest_cyfra(
            odpowiedz[i + 2U])) {

        return
            HTTP_KOD_NIEPRAWIDLOWY;
    }

    const int kod =
        static_cast<int>(
            odpowiedz[i + 0U] -
            '0') *
            100 +
        static_cast<int>(
            odpowiedz[i + 1U] -
            '0') *
            10 +
        static_cast<int>(
            odpowiedz[i + 2U] -
            '0');

    if (!kod_poprawny(
            kod)) {

        return
            HTTP_KOD_NIEPRAWIDLOWY;
    }

    i +=
        3U;

    /*
     * Po kodzie nie moze natychmiast wystapic kolejna cyfra/litera.
     *
     * Dozwolone:
     *   koniec dostepnego bufora,
     *   NUL,
     *   CR/LF,
     *   SP/HTAB przed reason phrase.
     */
    if (i < dlugosc) {
        const char nastepny =
            odpowiedz[i];

        if (!jest_koniec_statusu(
                nastepny) &&
            !jest_spacja_statusu(
                nastepny)) {

            return
                HTTP_KOD_NIEPRAWIDLOWY;
        }
    }

    return
        kod;
}

/*
 * Wygodny wariant dla poprawnie zakonczonego C-stringa.
 *
 * Nie wykonuje nieograniczonego strlen(). Skanowanie zatrzymuje sie:
 *   - na NUL,
 *   - po CR/LF,
 *   - po HTTP_MAKS_SKAN_STATUSU bajtach.
 *
 * Dla bufora sieciowego z jawna dlugoscia bezpieczniejszy jest overload
 * z parametrem `size_t dlugosc`.
 */
inline int wyciagnij_kod_http(
    const char* odpowiedz
) noexcept {
    if (!odpowiedz) {
        return
            HTTP_KOD_NIEPRAWIDLOWY;
    }

    size_t dlugosc =
        0;

    while (dlugosc <
           HTTP_MAKS_SKAN_STATUSU) {

        const char c =
            odpowiedz[
                dlugosc
            ];

        if (c == '\0') {
            break;
        }

        ++dlugosc;

        if (c == '\n') {
            break;
        }
    }

    if (dlugosc == 0) {
        return
            HTTP_KOD_NIEPRAWIDLOWY;
    }

    return
        wyciagnij_kod_http(
            odpowiedz,
            dlugosc
        );
}

/* =========================================================================
 * 4. OPIS KODU HTTP
 * ========================================================================= */

/*
 * Zwraca statyczny, niemodyfikowalny opis.
 *
 * Tekst jest celowo PLAIN TEXT. Nie zawiera <br> ani innych znacznikow.
 * Warstwa prezentacji odpowiada za HTML.
 */
inline const char* pobierz_opis_kodu_http(
    int kod
) noexcept {
    switch (kod) {
        /* 1xx - informacje */
        case 100:
            return "Continue - Serwer moze kontynuowac odbieranie zadania.";

        case 101:
            return "Switching Protocols - Serwer zaakceptowal zmiane protokolu.";

        case 103:
            return "Early Hints - Serwer wyslal wstepne naglowki odpowiedzi.";

        /* 2xx - sukces */
        case 200:
            return "OK - Zadanie wykonane poprawnie.";

        case 201:
            return "Created - Utworzono nowy zasob.";

        case 202:
            return "Accepted - Zadanie zostalo przyjete do przetworzenia.";

        case 204:
            return "No Content - Odpowiedz nie zawiera tresci.";

        case 206:
            return "Partial Content - Odebrano tylko wybrany zakres zasobu.";

        /* 3xx - przekierowania */
        case 300:
            return "Multiple Choices - Dostepnych jest kilka wariantow zasobu.";

        case 301:
            return "Moved Permanently - Zasob zostal trwale przeniesiony.";

        case 302:
            return "Found - Zasob jest tymczasowo dostepny pod innym adresem.";

        case 303:
            return "See Other - Odpowiedz wskazuje inny adres zasobu.";

        case 304:
            return "Not Modified - Lokalna kopia zasobu moze nadal byc aktualna.";

        case 307:
            return "Temporary Redirect - Tymczasowe przekierowanie z zachowaniem metody.";

        case 308:
            return "Permanent Redirect - Trwale przekierowanie z zachowaniem metody.";

        /* 4xx - blad klienta */
        case 400:
            return "Bad Request - Zadanie jest nieprawidlowe lub uszkodzone.";

        case 401:
            return "Unauthorized - Zasob wymaga uwierzytelnienia.";

        case 403:
            return "Forbidden - Serwer odmawia dostepu do zasobu.";

        case 404:
            return "Not Found - Nie znaleziono wskazanego zasobu.";

        case 405:
            return "Method Not Allowed - Ta metoda HTTP nie jest dozwolona.";

        case 406:
            return "Not Acceptable - Serwer nie moze przygotowac akceptowalnej reprezentacji.";

        case 408:
            return "Request Timeout - Serwer zbyt dlugo czekal na zadanie.";

        case 409:
            return "Conflict - Zadanie jest w konflikcie z aktualnym stanem zasobu.";

        case 410:
            return "Gone - Zasob zostal usuniety i nie jest juz dostepny.";

        case 413:
            return "Content Too Large - Tresc zadania jest zbyt duza.";

        case 414:
            return "URI Too Long - Adres zadania jest zbyt dlugi.";

        case 415:
            return "Unsupported Media Type - Nieobslugiwany typ danych.";

        case 416:
            return "Range Not Satisfiable - Zadanego zakresu nie mozna zwrocic.";

        case 421:
            return "Misdirected Request - Zadanie trafilo do niewlasciwego serwera.";

        case 422:
            return "Unprocessable Content - Tresc zadania jest poprawna skladniowo, ale nie moze zostac przetworzona.";

        case 426:
            return "Upgrade Required - Serwer wymaga przejscia na inny protokol.";

        case 429:
            return "Too Many Requests - Wyslano zbyt wiele zadan w krotkim czasie.";

        case 431:
            return "Request Header Fields Too Large - Naglowki zadania sa zbyt duze.";

        case 451:
            return "Unavailable For Legal Reasons - Zasob jest niedostepny z powodow prawnych.";

        /* 5xx - blad serwera */
        case 500:
            return "Internal Server Error - Wewnetrzny blad serwera.";

        case 501:
            return "Not Implemented - Serwer nie obsluguje wymaganej funkcji.";

        case 502:
            return "Bad Gateway - Serwer posredniczacy otrzymal bledna odpowiedz.";

        case 503:
            return "Service Unavailable - Usluga jest chwilowo niedostepna.";

        case 504:
            return "Gateway Timeout - Serwer posredniczacy przekroczyl czas oczekiwania.";

        case 505:
            return "HTTP Version Not Supported - Serwer nie obsluguje tej wersji HTTP.";

        default:
            if (kod >= 100 &&
                kod < 200) {

                return
                    "Informacyjna odpowiedz HTTP.";
            }

            if (kod >= 200 &&
                kod < 300) {

                return
                    "Odpowiedz HTTP oznaczajaca sukces.";
            }

            if (kod >= 300 &&
                kod < 400) {

                return
                    "Odpowiedz HTTP oznaczajaca przekierowanie.";
            }

            if (kod >= 400 &&
                kod < 500) {

                return
                    "Blad HTTP po stronie zadania klienta.";
            }

            if (kod >= 500 &&
                kod < 600) {

                return
                    "Blad HTTP po stronie serwera.";
            }

            return
                "Nierozpoznany lub nieprawidlowy kod odpowiedzi HTTP.";
    }
}

/* =========================================================================
 * 5. BUDOWANIE STRONY BLEDU
 * ========================================================================= */

/*
 * Bezpieczny wariant podstawowy.
 *
 * bufor:
 *   bufor docelowy.
 *
 * pojemnosc:
 *   CALKOWITA liczba bajtow bufora lacznie z miejscem na NUL.
 *
 * kod:
 *   kod HTTP.
 *
 * opis:
 *   opcjonalny opis. Gdy nullptr, uzywany jest pobierz_opis_kodu_http().
 *
 * Zwraca:
 *   true  - cala strona zmiescila sie w buforze,
 *   false - zly bufor/pojemnosc albo wynik zostal uciety.
 *
 * Przy pojemnosc > 0 bufor jest zawsze zakonczony NUL, rowniez przy
 * niepowodzeniu.
 */
inline bool zloz_strone_bledu(
    char* bufor,
    size_t pojemnosc,
    int kod,
    const char* opis
) noexcept {
    using namespace bursztyn_http_detail;

    Builder b(
        bufor,
        pojemnosc
    );

    if (b.blad) {
        return false;
    }

    const char* finalny_opis =
        opis
            ? opis
            : pobierz_opis_kodu_http(
                  kod
              );

    b.dopisz_tekst(
        "<!doctype html>"
        "<html><head>"
        "<meta charset=\"utf-8\">"
        "<title>Blad HTTP</title>"
        "</head><body>"
        "<h1>Status HTTP: "
    );

    dopisz_kod_http(
        b,
        kod
    );

    b.dopisz_tekst(
        "</h1><p>"
    );

    b.dopisz_html_escaped(
        finalny_opis
    );

    b.dopisz_tekst(
        "</p>"
        "<p>Sprawdz adres URL i polaczenie sieciowe, "
        "a nastepnie sprobuj ponownie.</p>"
        "</body></html>"
    );

    return
        !b.blad;
}

/*
 * Wariant automatycznie pobierajacy opis statusu.
 */
inline bool zloz_strone_bledu(
    char* bufor,
    size_t pojemnosc,
    int kod
) noexcept {
    return
        zloz_strone_bledu(
            bufor,
            pojemnosc,
            kod,
            pobierz_opis_kodu_http(
                kod
            )
        );
}

/*
 * Wygodny i BEZPIECZNY wariant dla prawdziwej tablicy:
 *
 *   char strona[1024];
 *   zloz_strone_bledu(strona, 404, nullptr);
 *
 * Rozmiar N jest wyliczany przez kompilator.
 *
 * Celowo NIE ma starego overloadu:
 *
 *   void zloz_strone_bledu(char*, int, const char*)
 *
 * bo przy samym wskazniku nie da sie sprawdzic rozmiaru bufora i nie da sie
 * zagwarantowac ochrony przed buffer overflow.
 */
template <size_t N>
inline bool zloz_strone_bledu(
    char (&bufor)[N],
    int kod,
    const char* opis
) noexcept {
    static_assert(
        N > 0,
        "Bufor strony HTTP nie moze miec rozmiaru 0"
    );

    return
        zloz_strone_bledu(
            bufor,
            N,
            kod,
            opis
        );
}

template <size_t N>
inline bool zloz_strone_bledu(
    char (&bufor)[N],
    int kod
) noexcept {
    return
        zloz_strone_bledu(
            bufor,
            N,
            kod,
            pobierz_opis_kodu_http(
                kod
            )
        );
}

/* =========================================================================
 * 6. HELPERY KLASYFIKUJACE STATUS
 * ========================================================================= */

inline constexpr bool http_kod_poprawny(
    int kod
) noexcept {
    return
        bursztyn_http_detail::kod_poprawny(
            kod
        );
}

inline constexpr bool http_kod_informacyjny(
    int kod
) noexcept {
    return
        kod >= 100 &&
        kod < 200;
}

inline constexpr bool http_kod_sukces(
    int kod
) noexcept {
    return
        kod >= 200 &&
        kod < 300;
}

inline constexpr bool http_kod_przekierowanie(
    int kod
) noexcept {
    return
        kod >= 300 &&
        kod < 400;
}

inline constexpr bool http_kod_blad_klienta(
    int kod
) noexcept {
    return
        kod >= 400 &&
        kod < 500;
}

inline constexpr bool http_kod_blad_serwera(
    int kod
) noexcept {
    return
        kod >= 500 &&
        kod < 600;
}

static_assert(
    HTTP_KOD_MIN == 100,
    "Minimalny kod HTTP powinien wynosic 100"
);

static_assert(
    HTTP_KOD_MAX == 599,
    "Maksymalna klasa kodow HTTP konczy sie na 5xx"
);

static_assert(
    HTTP_KOD_NIEPRAWIDLOWY <
        HTTP_KOD_MIN,
    "Kod bledu parsera nie moze kolidowac z poprawnym statusem HTTP"
);

#endif /* __cplusplus */
