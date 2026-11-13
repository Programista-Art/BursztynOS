# Eksplorator: „odmowa” przy uruchamianiu aplikacji — przyczyna i poprawka

Gałąź `feature/bsf-2-2-font-formats`. Zmiany niezatwierdzone.

## Objaw

Aplikacje uruchamiane ikoną z pulpitu startują; te same aplikacje uruchamiane z
Eksploratora kończą się komunikatem „BWS10: odmowa PZB, błąd loadera lub program
już działa”.

## Ścieżki uruchamiania (audyt)

| ścieżka | wywołanie | proces wywołujący |
|---|---|---|
| ikona pulpitu / menu Start | `uruchom_program_uzytkownika` → BWS 10 | Menedżer Okien, `PZB_USLUGI`, `PRAWA_MENEDZERA_OKIEN` (w tym `system_config`) |
| Eksplorator (wpis „Pulpit”, plik `.bur`, paczka `.cebula`) | ten sam BWS 10 | Eksplorator, `PZB_UZYTKOWNIK` (4), prawa `okna, pliki_czytaj, pliki_zapisz, uruchom_program` |

Ten sam syscall, ta sama ścieżka pliku, ten sam loader — różni się wyłącznie
**tożsamość rodzica**. Kod: `bws_uruchom()` w `jadro/bws/syscalls.cpp`.

## Przyczyna

`bws_uruchom()` czyta manifest `opis.aplikacji` dziecka. Jeżeli deklaruje
`poziom_zaufania < PZB_UZYTKOWNIK` albo prawo `system_config`, wymaga od rodzica
`PZB_ZAUFANE` **i** `PRAWO_SYSTEM_CONFIG`. Menedżer Okien to spełnia, Eksplorator —
słusznie — nie. Manifesty wbudowanych aplikacji o podwyższonym zaufaniu:

| aplikacja | poziom | prawa |
|---|---|---|
| Ustawienia | 3 | okna, pliki_czytaj, pliki_zapisz, system_config |
| Diagnostyka systemu | 3 | okna, pliki_czytaj, pliki_zapisz |
| Test dźwięku | 3 | okna, pliki_czytaj, pliki_zapisz |
| Aktualizacje Systemu | 2 | okna, pliki_czytaj, pliki_zapisz, system_config, siec |

Zwykłe aplikacje (Notatnik, Kalkulator, Husarz, Snake, …) mają poziom 4 i uruchamiają
się z Eksploratora bez problemu — odmowa dotyczy tylko tych czterech.

## Dlaczego nie „dać Eksploratorowi więcej praw”

Nie nadajemy Eksploratorowi żadnych dodatkowych uprawnień. Dodatkowo manifest na
dysku **nie może** być źródłem zaufania przy delegowaniu: pliki w `/programy`
mają etykietę PZB 4 i zwykły proces z `pliki_zapisz` może je zmienić. Gdyby
Eksplorator uruchamiał dowolny manifest „poziom_zaufania = 0”, stałby się
trampoliną do eskalacji.

## Poprawka (bez osłabiania zabezpieczeń)

`jadro/bws/uruchamianie_polityka.h` — skończona, wbudowana w jądro tablica czterech
aplikacji systemowych z **jawnym** poziomem i zestawem praw. Reguły w `bws_uruchom()`:

1. Rodzic musi mieć `PRAWO_URUCHOM_PROGRAM` i poziom ≤ `PZB_UZYTKOWNIK` (sandbox, poziom 5, nie
   może delegować).
2. Ścieżka musi być **dokładnie** jedną z czterech (bez normalizacji, bez
   prefiksów, bez `..`, rozróżnianie wielkości liter).
3. Dziecko dostaje poziom i prawa **z tablicy**, nigdy z manifestu na dysku.
4. Gdy rodzic nie ma pełnego zaufania (Eksplorator), loader dodatkowo wymaga, by plik
   był **bajt w bajt** identyczny z aplikacją wbudowaną w obraz jądra
   (`kernel_wbudowany_zgodny()`; trzy aplikacje z blobów BZB1, Ustawienia z kopii
   modułu zdjętej przy instalacji). Podmieniony plik dostaje
   `BLAD_INTEGRALNOSC`. Dla Menedżera Okien (pełne zaufanie) ścieżka jest bez zmian i
   bez kosztu dodatkowego porównania.
5. Podpis pakietów, W^X, NX, ASLR i SSP w loaderze bez zmian.

Dowolna inna ścieżka (np. własne `.cebula` użytkownika z manifestem „poziom_zaufania = 0”)
nadal wymaga zaufanego rodzica — kontrolę potwierdza test hosta.

## Komunikaty o błędach

Nowe wywołanie **BWS 100** (`BWS_OSTATNI_BLAD_URUCHOMIENIA`) zwraca kod przyczyny
ostatniej odmowy BWS 10 *dla wywołującego procesu*. Kody nie zawierają adresów ani
danych kryptograficznych:

| kod | znaczenie | komunikat w Eksploratorze |
|---|---|---|
| 2 | brak pliku / brak odczytu | Nie można uruchomić: brak pliku programu albo dostępu do odczytu. |
| 3 | zły format (nie `.bur`, zbyt mały, uszkodzony nagłówek) | …to nie jest poprawny program Bursztyn OS. |
| 4 | integralność (podpis, zgodność z wzorcem) | …weryfikacja integralności programu nie powiodła się. |
| 5 | instancja już działa | Program już działa — użyj jego okna. |
| 6 | brak zasobów (slot, pamięć) | …brak zasobów systemu. |
| 7 | rodzic bez `uruchom_program` | Ten program nie ma uprawnienia do uruchamiania aplikacji. |
| 8 | wymagany wyższy poziom zaufania | Odmowa: aplikacja wymaga wyższego poziomu zaufania. |
| 9 | zła ścieżka | …niepoprawna ścieżka programu. |
| 10 | nie udało się utworzyć procesu | …nie udało się utworzyć procesu. |

Klasyfikacja: `klasyfikuj_blad_loadera()` mapuje opisy błędów loadera
(`obraz.blad`). BWS 100 nie wymaga żadnego prawa — zwraca wyłącznie kod własnej próby.

## Dowody

* Host: `make test-launch-policy-host` (ASan/UBSan), 50 sprawdzeń: dokładne dopasowanie
  ścieżek (w tym próby `..`, `/`, sufiks, wielkość liter, `nullptr`), brak praw
  `sterownik`/`debug` w tablicy, sandbox nie deleguje, klasyfikacja błędów.
* Natywnie (QEMU/TCG, UEFI 1280x720), **ISO produkcyjne z kontrolą zgodności z wzorcem**: z Eksploratora
  (wpisy w katalogu Pulpit) otwierają się „Aktualizacje Systemu” (PZB 2), „Test dźwięku” (PZB 3) i
  „Ustawienia” (PZB 3, `system_config`; wzorzec z kopii modułu Multiboot);
  ponowne uruchomienie działającej „Diagnostyki systemu” aktywuje istniejące okno (bez duplikatu);
  pusty plik `fake.bur` w `/tymczasowe` daje komunikat o niepoprawnym programie (kod 3).
  Zrzuty: `.bsf-test/sess-prod/prod-aktualizacje.png`, `prod-testdzwieku2.png`,
  `.bsf-test/sess-prod2/prod2-settings.png`,
  `.bsf-test/sess-shadow/fake-bur.png`.
* **Nie sprawdzono natywnie:** ścieżki odmowy przy podmienionym pliku (kod 4) — tylko przegląd kodu;
  startu „Diagnostyki systemu” z Eksploratora, gdy nie była uruchomiona (sprawdzono tylko aktywację).

## Ograniczenia

* Odmowa sprzed poprawki **nie została odtworzona natywnie** na buildzie sprzed zmian;
  przyczyna wynika z analizy kodu (manifesty + warunek w `bws_uruchom()`), a poprawkę
  zweryfikowano natywnie i testem hosta.
* Ustawienia: wzorzec to kopia modułu Multiboot `bsf-settings` zdjęta przy
  instalacji wbudowanych programów. Gdy bootloader nie przekaże modułu (zainstalowany
  system bez modułu), wzorzec jest niedostępny i start z Eksploratora zostanie
  odrzucony kodem 4 — bezpieczniej niż zgoda bez weryfikacji. Start z pulpitu działa
  w obu przypadkach.
* Natywnie sprawdzono start z Eksploratora dla trzech z czterech aplikacji (Aktualizacje, Test dźwięku,
  Ustawienia); Diagnostyka korzysta z tej samej tablicy i tego samego kodu.
* Znane, wcześniejsze ryzyko (poza zakresem): pliki w `/programy` mają etykietę
  PZB 4, więc zwykły proces z `pliki_zapisz` może je podmienić; start z pulpitu
  (Menedżer Okien) ufa im z pochodzenia. Poprawka powyżej nie pogarsza tej sytuacji
  dla Eksploratora (kontrola zgodności), ale ochrona `/programy` przed zapisem
  zwykłych procesów byłaby osobną zmianą.
