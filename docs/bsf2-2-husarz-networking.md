# BSF 2.2 — Husarz: nagłówek Origin dla @font-face

## Problem

Fonty z `@font-face` są zasobami CORS. Husarz sprawdzał odpowiedź
(`Access-Control-Allow-Origin`, `Husarz::font_cors_allowed`), ale zapytanie
budowane w jądrze (`jadro/siec/siec.cpp` `zbuduj_http_get`,
`jadro/siec/tls.cpp` `zbuduj_zadanie_http`) miało stałe nagłówki, bez
`Origin`. Serwery/CDN-y, które odsyłają ACAO tylko przy obecnym `Origin`
(lub różnicują odpowiedź po nim), były dla Husarza nieosiągalne.

## Zmiany

* `jadro/siec/http_origin.h` — `http_origin_poprawny()`: akceptuje tylko
  zserializowany origin (`null` albo `http(s)://host[:port]`, host: małe
  litery ASCII/cyfry/`.`/`-`, bez kropki/myślnika na brzegach, port 1–65535
  bez zer wiodących, ≤ 255 znaków). CR/LF, spacje, ścieżki, wielkie litery —
  odrzucane, więc nie da się wstrzyknąć nagłówka.
* Budowniczowie zapytań HTTP i HTTPS przyjmują opcjonalny `origin`
  (domyślnie `nullptr` — dotychczasowe wywołania bez zmian) i dopisują
  `Origin: …` po `Host`. `kernel_siec_pobierz_http(s)` przekazują parametr.
* ABI BWS 94/95 (`BWS_HTTP64`/`BWS_HTTPS64`) rozszerzone zgodnie wstecz:
  flaga `BWS_HTTP64_ORIGIN` (bit 1) w `BwsBuforSieci64::flagi` oznacza, że
  `arg4` wskazuje `BwsBuforSieci64Origin` (24 B, dodatkowy wskaźnik na
  origin). Rozszerzona struktura jest kopiowana tylko przy fladze; origin
  kopiowany bajt po bajcie z przestrzeni użytkownika do 256 B i walidowany
  w jądrze. Bez flagi zachowanie identyczne jak dotąd (16 B).
* Ring 3: `bws_siec_pobierz_bajty_origin()` (`jadro/gui/bursztyn_gui.*`).
* Husarz: `Origin::serialize()` (RFC 6454, port domyślny pomijany, host
  małymi literami); `ResourceTask::origin` ustawiany dla zadań `Font` z URL
  dokumentu; `WorkerPayload::request_origin`; worker przekazuje go do
  `fetch_browser_resource(url, Font, origin)` — origin wysyłany na każdym
  przekierowaniu; nieprawidłowy origin jest porzucany już w Ring 3. Inne
  typy zasobów (HTML, CSS, obrazy, fetch) nie wysyłają `Origin`.

Uproszczenie względem Fetch: po przekierowaniu cross-origin specyfikacja
zmienia origin na `null`; Husarz wysyła origin dokumentu bez zmian
(decyzja CORS po stronie odpowiedzi pozostaje bez zmian i jest ścisła).

## Nie zrobiono (świadomie)

* **Większe fonty sieciowe** — limity bez zmian: `MaxResourceBytes`
  240 KiB (wejście WOFF/WOFF2/TTF), bufor HTTP jądra `MAX_HTTP_BWS` 256 KiB,
  wyjście dekodera `AsyncFontCapacity` ≈ 1,79 MB. Zwiększenie wymaga
  większego bufora TCP w jądrze i walidacji pod TCG; odłożone.

## Weryfikacja

* `make test-husarz-runtime-host` (2312 checks PASS): zapytania o font
  niosą `Origin: https://page.test` na obu skokach przekierowania; zasoby
  nie-fontowe nigdy nie niosą `Origin`; origin z `\r\n` porzucony; testy
  `Origin::serialize` (wielkość liter, port domyślny/niestandardowy,
  `about:blank`, za mały bufor) i 4 poprawne / 13 niepoprawnych wejść
  `http_origin_poprawny`.
* Jądro i wszystkie aplikacje kompilują się (`make iso`); natywny workload
  bsf22 (strony `testy/husarz_webfonts/01..11`, @font-face przez worker)
  35/35 na ISO z tą zmianą.
