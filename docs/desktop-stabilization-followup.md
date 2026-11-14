# Stabilizacja pulpitu — uzupełnienie (duże obrazy, regresje, uprawnienia Snake, pasek zadań)

## B. Dwa testy hosta, które padały

### `test-security-host` — UBSan w `fuzz_bur_v2`
* Co zgłoszono wcześniej: UBSan „pointer overflow” w `testy/fuzz/fuzz_bur_v2.cpp:18` (w wersji z HEAD to wyrażenie
  `copy.data()+n.reloc_przesuniecie`), cel `test-security-host`.
* **Nie udało się odtworzyć awarii w tej sesji**: dwa przebiegi `make test-security-host` na kodzie sprzed poprawki
  zakończyły się kodem 0 (8/8 fuzzerów, ziarno stałe `0x42555202`). Awaria z poprzedniej sesji nie jest więc
  dowiedziona jako deterministyczna dla tego drzewa roboczego.
* Analiza kodu wskazuje realny wzorzec: walidator (`bur_v2_waliduj_naglowek_kod`) sprawdza zakres
  `reloc_przesuniecie` tylko dla `reloc_liczba > 0`; dla `reloc_liczba == 0` przesunięcie bywa dowolne i
  `copy.data()+n.reloc_przesuniecie` kończy się arytmetyką wskaźnika poza obiektem (UB), choć wskaźnik nie jest
  dereferencjonowany (`bur_v2_zastosuj_relokacje_danych` wraca przy `reloc_liczba == 0`). Zbudowany ręcznie nagłówek
  (`reloc_liczba=0`, przesunięcie `0xFFFFFFFFFFFFFFF0`, poprawna suma) przechodzi walidator (`BUR_V2_OK`), ale przy
  `-O1` kompilator usuwa martwe wyrażenie, więc UBSan go nie zgłasza — dlatego diagnostyka zależy od optymalizacji
  i układu kodu (co tłumaczy przypadkowość).
* Ten sam wzorzec był w produkcyjnym loaderze (`jadro/fs/loader.cpp`: `bufor_pliku + n2.reloc_przesuniecie`).
  Poprawiono oba miejsca: wskaźnik jest liczony tylko dla `reloc_liczba > 0`. Walidacja loadera i testy fuzz nie
  zostały osłabione; UBSan nie jest wyłączony. Po poprawce `make test-security-host` → 0.
* Uboczny skutek: `docs/security-hardening-2026-11-04/fuzzing-metrics.json` jest nadpisywany przez ten test
  (plik śledzony przez Git) — zmiana pozostawiona w drzewie, do decyzji właściciela.

### `test-image` — brak `obrazy/tapety/nowatapeta.png`
* Przyczyna: plik (4,2 MB, fotografia PNG, SHA-256 `729f4ed4…`) został dodany w commicie `96827a5` jako wzorzec
  prawdziwego PNG i usunięty z `obrazy/tapety/` w `fea7026` (modernizacja Eksploratora), bez aktualizacji testu.
* Poprawka: plik odtworzony z historii Git **bez zmiany treści** (`git show 96827a5:…`) do
  `obrazy/testy/dane/nowatapeta.png` (katalog fixture’ów testu — nie `obrazy/tapety/`, żeby nie trafił do
  pakietu tapet); `test_png.py` wskazuje ten plik (zmienna `BURSZTYN_TEST_PNG` nadal działa).
* Drugi, niezależny błąd w tym samym celu: `test_tapety_jpeg.cpp`/`test_tapety_manifest.cpp` nie kompilowały się
  (brak `-I.` i `-Ijadro/gui` w regule `test-image`). Dodano brakujące ścieżki.
* Wynik: `make test-image` → kod 0.

## C. Prawa plikowe Snake (PZB)

Audyt: wcześniej Snake dostał ogólne `pliki_zapisz` tylko po to, by zapisać rekord. `proces_moze_modyfikowac_sciezke`
blokuje zapis w `/system`, `/jadro`, `/programy`, `/gry` i `*.bur` w korzeniu dla każdego procesu PZB ≥ UŻYTKOWNIK
(więc zaufane pliki wykonywalne, manifesty `opis.aplikacji` w `/programy|/gry` i pliki aktualizacji w drzewach
systemowych były chronione), oraz etykiety PZB plików (np. `/ustawienia`). Luka: ogólne `pliki_zapisz` pozwalało
Snake zapisać w katalogu ustawień **innej** aplikacji (`/uzytkownicy/<u>/Ustawienia/<inna>/…`, `app_id` jest
dowolnym napisem klienta) i w ogóle w danych użytkownika.

Zmiana — wielokrotnego użytku, bez wyjątku dla Snake:
* nowe prawo PZB `PRAWO_USTAWIENIA_APLIKACJI` (manifest: `"ustawienia_aplikacji"`): zapis wyłącznie w
  `/uzytkownicy/<u>/Ustawienia/<id-aplikacji>/…`; `id` wyprowadza **jądro** ze ścieżki pliku procesu
  (`/gry/snake.cebula/snake.bur` → `snake`), nie aplikacja ani manifest;
* izolacja dla wszystkich zwykłych procesów (PZB ≥ UŻYTKOWNIK): katalog ustawień innej aplikacji jest
  niemodyfikowalny, nawet z `pliki_zapisz`; prawo ustawień nie daje zapisu nigdzie indziej (w tym w sam katalog
  `Ustawienia`, `/uzytkownicy/Pulpit`, `/dane`, `/tapety`, `/boot`, `/system`);
* Snake: manifest `okna, pliki_czytaj, ustawienia_aplikacji` (bez `pliki_zapisz`);
* testy: `make test-ustawienia-aplikacji-host` (48 sprawdzeń: wyprowadzanie id, własny/obcy katalog, prefiks
  nazwy, traversal, wielkość liter, procesy bez id, brak zapisu poza katalogiem).

Natywnie (guest, tablet): Snake zapisał rekord **1** po zjedzeniu jabłka („Nowy rekord: 1”), po zamknięciu i
ponownym uruchomieniu pokazuje „Rekord: 1” (z nowym prawem, bez `pliki_zapisz`). Próba zapisu cudzych ustawień
z Notatnika (`file_create=DENY`) była niejednoznaczna (pole ścieżki zawierało śmieci po edycji), więc dowodem
dla odmowy zapisu pozostaje test jednostkowy decyzji + ścieżka `proces_moze_modyfikowac_sciezke`; uszkodzone
ustawienia — test hosta Snake (walidacja formatu ≤ 128 B). Odczyt: Snake nadal ma `pliki_czytaj`
(ogólny odczyt, bez zapisu) — zawężenie odczytu wymaga zmian w ~10 punktach BWS i jest poza zakresem.

## E. Interakcje pulpitu

### Odzyskiwanie po oknie „nad paskiem”
* **Ctrl+Alt+B** (klawisz systemowy w jądrze, jak Alt+Tab: przed dostarczeniem do aplikacji, więc działa nawet
  gdy aplikacja się zawiesiła) wyłącza tryb „nad paskiem” dla WSZYSTKICH okien. Tylko odbiera uprawnienie,
  więc nie może być nadużyty do eskalacji; nadawanie nadal wyłącznie przez Menedżer Okien (BWS 40 +
  tożsamość `/menedzer_okien.bur`).
* Menedżer Okien czyta stan z jądra (nowe pole `GuiOknoInfo::nad_paskiem` w snapshotcie okien), więc znacznik `^` i
  lista okien są zawsze zgodne z jądrem (także po Ctrl+Alt+B i po zamknięciu okna).
* Test: host (31 sprawdzeń: duże okna przykrywające cały pasek, cofnięcie, idempotencja) i natywnie: okno
  Notatnika z trybem „nad paskiem” przykrywa pasek, Ctrl+Alt+B → pasek znów widoczny, log
  `[TASKBAR] Ctrl+Alt+B: cofnieto tryb nad paskiem dla okien: 1`. Uwaga: naciśnięcie Alt trafia do aplikacji
  przed akordem (podświetla menu) — kosmetyczne.

### Kółko myszy na tablecie VirtIO
* Diagnoza: QEMU przekazuje kółko jako `EV_REL / REL_WHEEL`, a `input_dispatch_event` odrzucał **wszystkie**
  zdarzenia EV_REL dla urządzeń w trybie absolutnym (tablet). Zdarzenia dochodziły do jądra i były gubione.
* Poprawka: `REL_WHEEL`/`REL_HWHEEL` są przyjmowane niezależnie od trybu (X/Y w trybie absolutnym nadal ignorowane;
  brak duplikatów — jedno zdarzenie na raport). Test: `test-virtio-input-events` (59 sprawdzeń, m.in. kółko
  z tabletu absolutnego dociera raz, REL_X/REL_Y ignorowane, dwa zdarzenia w raporcie sumują się).
* Natywnie (tablet): Ctrl+kółko w Przeglądarce: 18% → 20% → … → 50% (w górę), 50% → 33% (w dół).
  Suwak, przyciski +/− i klawisze działają niezależnie od kółka.

## F. Kursor — test natywny (tablet VirtIO, UEFI 1280×720, plik `ikonki/kursor.png` i `ikonki/kursor_rozciagania.png` bez zmian)

Wszystkie osiem stref krawędzi okna (lewa/prawa ↔, góra/dół ↕, NW/SE ⤡, NE/SW ⤢) pokazuje właściwy kursor,
środek kursora leży na krawędzi (hotspot); strzałka w obszarze roboczym; ciemny motyw „Bursztyn Nocny” i jasny
„Bursztyn Jasny” (oba kursory widoczne dzięki ciemnemu obrysowi). USB mysz: wcześniejsze sesje (strzałka),
nie powtarzano ośmiu stref. Nieweryfikowane natywnie: przywrócenie po zamknięciu/minimalizacji/zmianie fokusu
(mechanizm bez zmian), UEFI 1920×1080 (ISO startuje w 1280×720; GRUB `gfxmode=1280x720x32`).

BIOS 1920×1080 (mysz USB): Przeglądarka, ISO startuje, pulpit i okna poprawne; galeria: 5/6 przed zmianą kolejności
importu (RAM-dysk), patrz `gallery-packaging-security.md`.
