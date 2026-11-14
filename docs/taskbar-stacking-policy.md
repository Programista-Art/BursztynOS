# Polityka stosu: pasek zadań nad zwykłymi oknami

## Problem

Pasek zadań jest rysowany w warstwie Menedżera Okien (WM, `z_order 0` – to samo, co pulpit i tapeta).
Każda warstwa aplikacji ma `z_order > 0`, więc okno wsunięte pod dolną krawędź ekranu zakrywało
pasek: kompozytor składa warstwy rosnąco po `z_order`, a mysz trafiała w okno, nie w pasek.
Nadanie pulpitowi wysokiego `z_order` nie wchodziło w grę (pulpit zakryłby wszystkie okna).

## Rozwiązanie: pasek jako niezależna powierzchnia kompozytora

```
tapeta + pulpit (WM, z=0)
zwykłe okna aplikacji (rosnące z)
PASEK ZADAŃ  – prostokąt paska z warstwy WM, składany ponownie PO zwykłych oknach
okna z jawnym zezwoleniem „nad paskiem” (w prostokącie paska, w kolejności z)
popup aplikacji → overlay systemowy (menu Start, dialogi) → kursor
```

* `jadro/gui/skladacz_obrazu.cpp`: stan `taskbar_overlay` (pid właściciela + prostokąt) i tablica flag
  `nad_paskiem_flaga[]`; `zloz_pasek_zadan()` wywoływana w obu ścieżkach kompozycji (dirty-region i
  pełna klatka), tylko w części wspólnej z regionem – bez nowych pełnych przerysowań i bez zmian w
  atomowej prezentacji klatki. Zmiana rejestracji/flagi oznacza dirty tylko okno i prostokąt paska.
* **Hit-test = kolejność rysowania** (`jadro/gui/wm_input.h`, `wm_warstwa_pod_punktem`): w prostokącie
  paska wygrywa pasek, chyba że punkt leży w oknie z zezwoleniem „nad paskiem” (z uwzględnieniem
  przezroczystych pikseli). Wyższy `z_order` zwykłego okna nie daje mu priorytetu nad paskiem.
  Overlay (menu Start/dialogi) ma pierwszeństwo przed wszystkim, jak dotąd. Capture myszy (przeciąganie,
  zmiana rozmiaru) działa bez zmian – cel ruchu wybiera `wm_cel_ruchu`.
* Maksymalizacja i obszar roboczy nie zmieniają się: zmaksymalizowane okna respektują `work_*`, okna
  można nadal częściowo wysuwać poza ekran (także pod pasek), a pasek pozostaje na wierzchu.

## „Zawsze na wierzchu także nad paskiem zadań” (opt-in)

* Domyślnie **wyłączone** dla każdego okna.
* Włączenie/wyłączenie: **prawy przycisk myszy na przycisku okna w pasku zadań** (przełącznik; okno z
  włączonym trybem ma znacznik `^` przed tytułem na przycisku). Ponowne kliknięcie cofa tryb.
* Uprawnienie przyznaje wyłącznie WM. Okno aplikacji nie ma API ani pola manifestu, które to ustawia.

## Autoryzacja (BWS 40, rozszerzony)

BWS 40 (`system overlay`) przyjmuje teraz operacje 2–5 (`2` zamknij pasek, `3` zarejestruj prostokąt
paska, `4`/`5` cofnij/przyznaj zezwolenie oknu o `window_id`). Wywołanie wymaga **kolejno**:
`PRAWO_GUI`, `PRAWO_SYSTEM_CONFIG` + poziom zaufania ≥ `PZB_ZAUFANE` (jak dotychczasowy overlay)
**oraz tożsamości procesu = Menedżer Okien** (`/menedzer_okien.bur`). Samo PZB lub manifest
aplikacji nie wystarcza; inny zaufany program (np. Ustawienia) dostaje odmowę i wpis w logu.
Dodatkowo: `window_id` zawiera generację (nieaktualny identyfikator nie zmienia cudzego okna),
flaga jest zerowana przy zamknięciu okna, pasek może zarejestrować tylko jeden właściciel, a sam
pasek nie może zostać „oknem nad paskiem”. Brak niewidzialnych nakładek: piksele przezroczyste okna
nie przechwytują myszy. WM trzyma limit 16 jednoczesnych okien „nad paskiem”.
Nie wprowadzono żadnego numeru BWS: `BWS_PIERWSZY_WOLNY` pozostaje 101.

Odzyskiwalność: tryb można cofnąć z paska (pasek zawsze jest klikalny, o ile okno nie ma trybu –
a okno „nad paskiem” nadal ma przycisk w pasku poza własnym prostokątem lub można je zamknąć/
zminimalizować jego własnymi kontrolkami).

## Testy

* Host: `make test-taskbar-stacking-host` (20 sprawdzeń: pasek nad zwykłymi oknami, wyższe `z_order`,
  zezwolenie, wiele okien, przezroczystość, overlay, wartości skrajne), `make test-wm-click-routing`
  (prawy klik na przycisku, przełączanie, limit, czyszczenie po zamknięciu okna, rejestracja prostokąta).
* Natywnie (QEMU, tablet VirtIO): patrz `docs/desktop-ui-regression-tests.md`.

## Ograniczenia

* Aplikacje starego GUI (legacy) są traktowane jak zwykłe okna: pasek jest nad nimi; zezwolenie
  „nad paskiem” działa także dla nich, jeśli mają warstwę.
* Brak menu kontekstowego okna w belce tytułowej – przełącznik jest w pasku zadań (prawy przycisk).
* Pasek składany jest z warstwy WM; półprzezroczyste tło paska nad oknami nie jest obsługiwane
  (tło paska jest kryjące).

## Aktualizacja: odzyskiwanie po oknie „nad paskiem”

Ograniczenie opisane wcześniej (okno przykrywające własny przycisk w pasku) ma teraz mechanizm odzyskiwania:
**Ctrl+Alt+B** (klawisz systemowy w jądrze, przed dostarczeniem do aplikacji) cofa tryb dla wszystkich okien;
Menedżer Okien odczytuje stan z jądra (`GuiOknoInfo::nad_paskiem`). Szczegóły i testy: `desktop-stabilization-followup.md`.
Nadawanie trybu bez zmian: wyłącznie Menedżer Okien (BWS 40, tożsamość procesu).
