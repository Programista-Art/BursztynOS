# Opóźnienie uruchamiania aplikacji z pulpitu

Gałąź `feature/bsf-2-2-font-formats`. Zmiany niezatwierdzone. Wszystkie czasy z QEMU/TCG
(bez KVM, host współdzielony z innymi procesami) — wartości względne, nie bezwzględne
dla sprzętu.

## Metoda

Build diagnostyczny (`BURSZTYNDIAGGUIRUNTIME=1 BURSZTYN_DEBUG_LOADER_PERF=1`) loguje na porcie
szeregowym: `[LOADER PERF]` (czas loadera: odczyt obrazu, mapowanie), `[APP START]` (Ring 3: koszt
budowy katalogu czcionek BSF i liczba odczytanych bajtów) oraz `[APP LAUNCH]` (czas od wejścia do
loadera do **pierwszej opublikowanej klatki** okna, mierzony w jądrze). Skrypt
`narzedzia/guest_session.py` (`bench`) uruchamia aplikację z menu Start tak jak użytkownik.
Pomiar nie zmienia semantyki loadera ani walidacji pliku.

## Wynik: dominujący etap

Przed zmianą (każda aplikacja, pierwszy start po starcie systemu):

| etap | czas |
|---|---|
| loader (odczyt + weryfikacja + mapowanie) | 5–16 ms (jednorazowo 445 ms, mapowanie przy starcie obciążonego systemu) |
| **katalog czcionek BSF (Ring 3)** | **437–808 ms**, odczyt **7 628 828 B** |
| razem do pierwszej klatki | 0,51–1,89 s |

Dominował nie loader, nie dekompresja BZB1 (aplikacje są rozpakowywane tylko przy
instalacji, nie przy starcie), nie sprawdzanie podpisu, tylko `BSF::init()`:
każdy proces przy pierwszym rysowaniu tekstu czytał **całe pliki** wszystkich 7 czcionek
i walidował je w całości, aby zbudować katalog (rodziny, style, pokrycie).

## Poprawka

`bsf/runtime.cpp` (`whole_file`): pliki sfnt > 64 KiB są katalogowane z tablic metadanych
odczytem zakresowym (`with_large_face`, ta sama ścieżka co dla kolekcji i plików > 16 MiB),
a nie przez odczyt całego pliku. Walidacja nie została zluzowana: `load()` czyta i w
całości waliduje plik (`open_index`) przed narysowaniem pierwszego glifu, a uszkodzony plik
dostaje `mark_failed` i fallback. Zmienia się tylko moment wykrycia uszkodzeń kształtów
(przy pierwszym użyciu zamiast przy skanie katalogu).

## Po zmianie — pomiar A/B (TCG, jeden przebieg, szum tła)

Ten sam skrypt (`narzedzia/app_launch_bench.sh`: sześć aplikacji × zimny/ciepły start z menu Start),
dwa buildy diagnostyczne różniące się wyłącznie progiem `TinyFontFile` (4 MiB = zachowanie sprzed
zmiany, 64 KiB = nowe). Czasy w ms (loader / katalog czcionek / do pierwszej klatki), odczyt I/O katalogu:

| aplikacja | start | przed: loader/katalog/pierwsza klatka | po: loader/katalog/pierwsza klatka |
|---|---|---|---|
| Kalkulator | zimny | 4,8 / 272 / 348 | 3,6 / 67 / 146 |
| Kalkulator | ciepły | 4,6 / 436 / 558 | 4,4 / 121 / 236 |
| Notatnik | zimny | 4,5 / 99 / 182 | 5,5 / 91 / 299 |
| Notatnik | ciepły | 6,2 / 100 / 188 | 14,2 / 86 / 273 |
| Eksplorator | zimny | 6,1 / 162 / 724 | 6,3 / 64 / 343 |
| Eksplorator | ciepły | 5,4 / 177 / 602 | 5,6 / 64 / 335 |
| Terminal | zimny | 4,6 / 134 / 221 | 10,0 / 122 / 403 |
| Terminal | ciepły | 19,3 / 134 / 232 | 4,3 / 65 / 151 |
| Ustawienia | zimny | 6,4 / 173 / 1037 | 5,6 / 72 / 971 |
| Ustawienia | ciepły | 6,8 / 174 / 1839 | 5,2 / 62 / 925 |
| Husarz | zimny | 9,0 / 187 / 358 | 16,4 / 164 / 642 |
| Husarz | ciepły | 7,9 / 207 / 620 | 10,1 / 100 / 530 |

* **I/O katalogu: 3,01 MB → 0,58 MB (−81%) na każdy start** (w tym buildzie 7 czcionek; wcześniejszy
  pomiar z innego przebiegu pokazywał 7,6 MB).
* **Czas katalogu: mediana 168 → 79 ms (−53%)**; wszystkie aplikacje poza Notatnikiem i Husarzem
  poprawiły się o ≥ 40%.
* **Czas do pierwszej klatki: mediana 580 → 339 ms (−42%)**, ale pojedyncze wartości są głośne
  (TCG, jeden przebieg, inne procesy na tym samym vCPU): Notatnik, Terminal (zimny) i Husarz
  w tym przebiegu wyszły **gorzej** mimo krótszego katalogu — czas „do pierwszej klatki” zawiera
  też pracę własną aplikacji i wywłaszczenia. Nie twierdzę poprawy dla każdej aplikacji.
* Loader sam (odczyt + walidacja + mapowanie) to 4–19 ms — nie jest wąskim gardłem; dekompresja BZB1
  nie bierze udziału w starcie (tylko w instalacji); podpis RSA nie jest liczony dla ścieżek
  systemowych (pochodzenie z obrazu) — niezmienione.
* Pozostały duży koszt własny: **Ustawienia ~0,9–1,0 s**, Husarz ~0,5–0,6 s (inicjalizacja własna
  aplikacji; nie badano).

Pamięć i pierwszy renderowany kadr: nie mierzono osobno; „pierwsza klatka” to pierwsza
opublikowana klatka okna w kompozytorze. „Zimny” = pierwszy start aplikacji po starcie systemu, „ciepły” = ponowny
start po zamknięciu okna; system plików live nie ma osobnego cache, więc różnice wynikają głównie z szumu.

## Informacja zwrotna i duplikaty

* **Kursor „zajęty”** (`GUI_CURSOR_WAIT`): Menedżer Okien ustawia go natychmiast po udanym
  `BWS 10` i zdejmuje przy zdarzeniu `OKNO_POKAZANE` albo po 3 s (`menedzer_okien.cpp`).
  Efekt wizualny **nie był weryfikowany** (tylko kompilacja i brak błędów w logu).
* **Duplikaty procesów**: start aplikacji jest jednoinstancyjny (`zarezerwuj_slot_procesu`
  z kontrolą ścieżki → kod „już działa”), a `uruchom_lub_aktywuj` najpierw aktywuje istniejące
  okno — szybkie podwójne kliknięcie nie tworzy drugiego procesu (istniejące zachowanie,
  nie zmieniane; potwierdzone dla Diagnostyki z Eksploratora).

## Czego nie zrobiono

* Cache katalogu współdzielony między procesami (wymagałby zaufanego magazynu — wspólny stan
  między niezwiązanymi procesami) — świadomie pominięty.
* Dalsza redukcja kosztu katalogu (nadal 60–120 ms pod TCG: ~7 plików × kilka odczytów zakresowych).
* Analiza własnego startu Ustawień i Husarza.
