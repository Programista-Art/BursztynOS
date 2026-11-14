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

---

# Etap końcowy przed commitem (po 6f98e52)

## G. Zmiana rozmiaru za prawy dolny róg — przyczyna: brak błędu routingu

Zinstrumentowano ścieżkę mouse-down (`BURSZTYN_DIAG_RESIZE_ROUTING=1`, log `[ROUTE]`: punkt, trafione okno,
fokus, stos warstw w punkcie z geometrią, `shape`/`frame`/`alpha0`/`above`, pasek). Dla punktu (957,679):

* Przeglądarka (pid 2, rect 100,100 860×580, z=12) ma w tym punkcie współrzędne lokalne (857,579) i **`shape=0`**:
  piksel leży poza zaokrąglonym narożnikiem okna (promień ok. 8 px). Kompozytor rysuje tam okno spod spodu, więc
  mysz słusznie trafia w nie (`wm_warstwa_pod_punktem`: piksel poza kształtem nie należy do okna).
* Pod spodem jest Diagnostyka (pid 3, rect 100,30 1040×650): jej dolna krawędź kończy się w tym samym wierszu
  (lokalnie 857,649, `frame=1`), czyli w jej strefie zmiany rozmiaru — stąd zmieniła się Diagnostyka.
* Po pierwszym takim kliknięciu Diagnostyka jest podnoszona (z rośnie), więc kolejne kliknięcia w obszarze
  wspólnym trafiają w nią zgodnie z widocznym stosem (zweryfikowane w logu: z=11→12…21).
* Pasek zadań nie brał udziału (`taskbar=0`); fokus przed kliknięciem zgodny z oknem wierzchnim.

Wniosek: legalnie przykryty/wycięty piksel narożnika, nie błąd. Po wskazaniu piksela **wewnątrz** kształtu (955,676)
przy oknie Przeglądarki na wierzchu zmiana rozmiaru działa (kursor ⤡, okno 860→~1000 px). Nie dodano obejścia
„przepuszczania kliknięć przez widoczne okno”. Test regresji: `test-taskbar-stacking-host` (34 sprawdzenia, w tym
narożnik poza kształtem → okno pod spodem, piksel w kształcie → okno wierzchnie).
Macierz natywna (tablet VirtIO, UEFI 1280×720; Snake, Przeglądarka, Diagnostyka, Notatnik nakładające się):
osiem stref (W/E/N/S/NW/NE/SW/SE) każdego okna zmienia geometrię zgodnie z oczekiwaniem; strefy dolne okien, których
dolna krawędź leży pod paskiem (y ≥ 680), są niedostępne, dopóki okno nie zostanie przesunięte w górę (pasek jest
ponad zwykłymi oknami). Strefa chwytu ma 5 px (lx < 5), więc punkt o 5 px od krawędzi to już pasek tytułu.
Minimalny rozmiar: Przeglądarka zatrzymuje się na 520×320 (MIN_SZER/MIN_WYS). Mysz USB: przypadek z BIOS 1080p miał identyczną
geometrię okien (wniosek z geometrii; logu `[ROUTE]` dla myszy USB nie zbierano, macierz zmiany rozmiaru wykonano tylko na tablecie).

## H. Przywracanie kursora (natywnie, tablet VirtIO)

Zweryfikowane: po puszczeniu przycisku po zmianie rozmiaru i po odsunięciu wskaźnika wraca strzałka; po Alt+Tab
(okno Diagnostyki nakryło punkt) strzałka; krawędź z kursorem ↔ po ponownym najechaniu. Niezweryfikowane
natywnie: zamknięcie okna klawiaturą, gdy wskaźnik stoi na krawędzi (brak sposobu zamknięcia bez ruchu myszy
w tym harnessie) i minimalizacja z krawędzi; mechanizm czyszczenia stanu per proces bez zmian. PNG właściciela
(`ikonki/kursor.png`, `ikonki/kursor_rozciagania.png`) niezmienione.

## I. Audyt bezpieczeństwa

* **Wyjątek 72 MiB:** czysta polityka `polityka_alokacji.h` (test 11 sprawdzeń: granice 16/40/72 MiB, bez wyjątku bez
  weryfikacji). Flaga tożsamości ustawiana wyłącznie w loaderze po SHA-256 *dokładnie tych bajtów, które za chwilę
  są mapowane* (ten sam bufor, brak ponownego odczytu), czyszczona przy rezerwacji slotu (PID reuse), plus ścieżka
  i `PRAWO_GUI|PRAWO_PLIKI_CZYTAJ`; łączny limit sterty 256 MiB bez zmian. **SHA-256 z tablicy w jądrze to lista
  dopuszczonych binarek (tożsamość zakotwiczona w obrazie jądra), nie podpis cyfrowy**; faktyczny podpis to tylko
  ścieżka `podpis_paczki_weryfikuj` (klucz release). Binarka o tej samej nazwie, ale innych bajtach nie dostaje wyjątku.
* **Kontener galerii:** CRC-32 i SHA-256 tabeli to kontrola integralności (uszkodzenia, błędy), nie uwierzytelnienie —
  kto może podmienić ISO, podmieni też sumy. Moduł pochodzi z medium rozruchowego; zdjęcia nie są wykonywalne.
* **Ochrona ścieżek:** wydzielono `ochrona_sciezek.h` i dodano `/aktualizacje` do drzew chronionych dla procesów
  PZB ≥ 4 (wcześniej etykieta katalogu to PZB 4, więc zwykła aplikacja z `pliki_zapisz` mogła zmienić
  `stan_rozruchu.k0/k1` i pliki aktualizacji). Test hosta `test-ochrona-sciezek-host` (35): /system, /jadro, /programy,
  /gry, *.bur w korzeniu, /aktualizacje, aliasy (`..`, `//`, `.`), `/system2` nie jest chroniony, cudze ustawienia,
  prawo ustawień nie daje zapisu poza własnym katalogiem. Natywna próba z Notatnika (pole „Zapisz jako”) nie dała
  jednoznacznych wyników (edycja pola w harnessie dopisywała do starej treści), więc dowodem są testy decyzji.
* Poprawka UB loadera BUR v2 zachowana (`reloc_liczba == 0`), PZB bez zmian.

## J. Wydajność podglądu 8000×8000

Host `-O2` (30,9 MB JPEG): skala 1/4 = 1,17 s, skala 1/8 (tylko DC, bez IDCT) = 0,83 s, odczyt pliku 0,016 s ⇒
**dominuje dekodowanie Huffmana (~70%)**, IDCT 4×4 ≈ 30%, I/O pomijalne. Zmiana: obiekt dekodera budowany z `-O2`
zamiast `-Os` (RING3 domyślnie `-Os` dla tego pliku): natywnie 90 s → **80,8 s** (QEMU/TCG bez KVM, ~70× wolniej
niż host). Podział pracy (12 ms z 16 ms), anulowanie przy nawigacji i limity pamięci bez zmian; podczas dekodowania
okno Przeglądarki dało się przeciągać (zrzut „Wczytywanie podglądu… 12%” po przesunięciu o 60,100 px).
Nie wprowadzono ryzykownych optymalizacji (np. tablic wielobitowych, pomijania współczynników).

## K. Instalacja zdjęć na dysk

* Test E2E instalatora (`narzedzia/instalator/qemu_install.py test`) uruchomiony: GUI instalatora, potwierdzenie,
  start zapisu — i **awaria środowiska jak na czystym HEAD**: `[VIRTIO-BLK] ERROR: request timed out` na etapie ESP
  (progress 20%), przed etapem plików i galerii. Ścieżka zainstalowanego systemu **nie jest zweryfikowana natywnie**.
* Zamiast tego wydzielono i przetestowano mechanizm kopiowania (`galeria_kopiowanie.h`, `test-galeria-kopiowanie-host`,
  25 sprawdzeń): istniejące pliki nietknięte, brak miejsca, przerwanie w środku (nigdy niepełne zdjęcie pod finalną
  nazwą), błąd zmiany nazwy, zły CRC, wyścig z plikiem użytkownika, sprzątanie pliku tymczasowego.
  Kopiowanie idzie przez plik `zdjecie.bzp3tmp` i zmianę nazwy bez nadpisywania; ten sam kod obsługuje import żywego
  systemu (natywnie 6/6) i etap instalatora.
* Wymaga ręcznego testu: instalacja → pierwszy start → `/dane/zdjecia` ma 6 plików; mały dysk → instalacja kończy się
  sukcesem (log `[INSTALL] galeria: blad zapisu`), brak pozostawionych plików `.bzp3tmp` (stary plik tymczasowy jest
  usuwany przy każdym starcie kopiarki).

## L. Drzewo robocze i dysk (po commicie właściciela 6f98e52)

Poprzednia praca jest już zacommitowana (80 plików). W commicie znalazły się też **śledzone artefakty testów**:
`.fuzz-build/*_metrics.json` (8, plik ignorowany przez `.gitignore`, ale śledzony), `.fuzz-corpus/fuzz_bws/seed_0{2,4}.bin`,
`docs/security-hardening-2026-11-04/fuzzing-metrics.json` oraz 8 plików `__pycache__/*.pyc`. Rekomendacja: osobny commit
`git rm --cached` dla `.fuzz-build/*`, `*.pyc` (i opcjonalnie metryk fuzz), bo są generowane i zmieniają się przy każdym
teście; `docs/.../fuzzing-metrics.json` zostawić tylko jeśli traktowany jako raport referencyjny. Nic nie usunięto.
Bieżące zmiany (5 zmodyfikowanych + 8 nowych): patrz raport końcowy (plan staging).
Dysk: wolne ok. 1,1 GB z 49 GB. Kandydaci do czyszczenia (bez automatycznego usuwania): `.bsf-test/` 8,1 GB (ignorowany
przez Git; największe: `bsf2-native` 485 MB, `native-profile-build` 456 MB, `stab` 334 MB, `gpu-recovery` 140 MB,
kilka `installed-*` ok. 120 MB), `.release-private/` 760 MB (w tym `apps` 89 MB; potrzebny kernel.elf bieżącego wydania),
`~/.cache` 752 MB, `.installertest/` (guest-* 244 MB z dzisiejszego uruchomienia), `.wm-test` 143 MB,
`.bursztyn-ui-test` 166 MB, `.aslr-test` 73 MB, `.fuzz-build` 79 MB, kopie ISO w katalogu sesji `/tmp/claude-1000` (ok. 120 MB).

