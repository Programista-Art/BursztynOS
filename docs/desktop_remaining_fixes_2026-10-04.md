# Pulpit — pozostałe poprawki, 2026-10-04

Zmiany wykonano w bieżącym worktree. Poniższe PASS dotyczą hosta/RAM/mocków i budowy. Nowego runtime nie zweryfikowano.

## Przyczyny i zachowanie po zmianie

1. **Zaznaczenie:** `RysujWpisPulpitu` rysował pełny, nieprzezroczysty prostokąt, a `motyw_pulpit_zaznaczenie` mieszał dwa kolory motywu bez udziału tapety. Kompozytor usuwał górny bajt koloru. Teraz role pulpitu mają wypełnienie o alpha 32/255 i ramkę 112/255, wyprowadzone z roli tekstu aktywnego motywu. Pełny i częściowy kompozytor mieszają te dwa znaczniki wyłącznie na root desktop (`z_order==0`). Pozostałe warstwy zachowują kontrakt RGB, zero jako dziurę oraz marker widocznej czerni. Geometria obejmuje ikonę i całą szerokość obszaru etykiety z małym marginesem, bez zajmowania całej komórki. Jasny motyw nie bierze koloru z żółtego akcentu.

2. **Długie nazwy:** parser pulpitu odrzucał całe listowanie, gdy dowolna nazwa przekraczała 55 bajtów; pojemność bufora wyświetlania była powieleniem limitu PSF. Wyświetlanie ma teraz pojemność 512 bajtów, zgodną z ograniczeniem ścieżek BWS. Bufory bieżącego i nowego listowania są alokowane raz na stercie. Zawijanie korzysta z pomiaru aktualnego fontu Unicode i granic kodpunktów UTF-8. Maksymalnie 16 linii, dodatkowo ograniczonych dostępną wysokością siatki; ostatnia linia dostaje mierzoną wielokropkę, gdy pełna nazwa nie mieści się w dostępnej przestrzeni. Layout nadal rezerwuje odpowiednią liczbę wierszy.

   **Ograniczenie:** PSF/BSP2 zapisuje nazwę w polu 56 bajtów, czyli maksymalnie 55 bajtów danych, nie 55 znaków Unicode. Tworzenie i rename nadal respektują ten limit. Nie zmieniono filesystemu ani ABI. Test dłuższego listowania używa mocka i potwierdza odporność UI, a nie możliwość zapisu takiej nazwy na obecnym dysku. Test maksymalnej legalnej nazwy z polskimi znakami sprawdza tworzenie, odrzucenie dodatkowego znaku i pełne wyświetlenie. Zwiększenie rzeczywistego limitu zapisu pozostaje poza tą poprawką UI.

3. **Drag and drop:** `obsluz_gest_pulpitu` kończył drag wyłącznie wywołaniem `Layout::move_group`; nie wykonywał przeniesienia filesystemu. MouseMove teraz wykrywa folder i podświetla strukturalnie prawidłowy cel. Nie wywołuje filesystemu. MouseUp kończy gest i zwalnia capture przed wywołaniem istniejącego `przenies_twor_uzytkownika` dla zapamiętanych źródeł. Zaznaczony cel, własny obiekt i własne poddrzewo są odrzucane; zwykły plik nie jest celem katalogowym. BWS pozostaje autorytetem dla PZB, uprawnień, kolizji i finalnego sprawdzenia struktury. Hover nie jest obietnicą uprawnień: odmowa może nadejść dopiero z BWS przy drop. Grupa kończy operację na pierwszym błędzie; komunikat pokazuje wykonany prefiks i obiekt, na którym zatrzymano operację. Pulpit odświeża rzeczywisty stan po przenoszeniu. Schowek pozostaje zachowany. Drop na puste tło nadal przesuwa ikony w siatce.

4. **Prawy klik w dialogu:** `ObslugaPopup::zdarzenie` bezwarunkowo wykonywała `popups.zamknij()` po `MYSZ_PRAWY_DOWN`. Adapter teraz konsumuje prawy klik i odpowiadające puszczenie bez zamykania stosu zawierającego powierzchnię modalną. Tekst, fokus i wynik dialogu pozostają zachowane także przy kliknięciu poza dialogiem. Istniejące niemodalne menu zachowują dotychczasowe zamykanie. Wybrano bezpieczny brak działania w polu tekstowym; nowego menu Copy/Cut/Paste nie dodano.

5. **Ikona tekstowa:** desktopowy renderer dla `.txt` zmieniał wyłącznie kolor prostokąta na niebieski. Teraz korzysta z istniejącego resolvera `PlikiUI::tekstowy`, również dla `.TXT`, i rysuje dokument z zagiętym rogiem oraz liniami tekstu za pomocą istniejących skalowanych fragmentów ikon. Papier i atrament używają ról panelu i tekstu motywu. Folder nazwany `*.txt` nadal otrzymuje ikonę folderu. Nie dodano osobnego loadera obrazów ani assetów bitmapowych.

## Zmienione pliki

Kod:
- `menedzer_okien.cpp`: buforowanie nazw, renderowanie dokumentu, pełna geometria etykiety, folder hover, drop pojedynczy/grupowy, kończenie capture, rejestracja pełnej wysokości folderów.
- `desktop_layout.h`: bufor zawijania, wielokropka UTF-8, geometria zaznaczenia.
- `motyw.h`: role zaznaczenia i ramki oraz mieszanie znaczników desktopowych.
- `skladacz_obrazu.cpp`: mieszanie w pełnej i częściowej ścieżce kompozycji root desktop.
- `plik_operacje_ui.h`: współdzielona walidacja relacji źródło/cel.
- `plik_popup_ui.h`: prawy klik w modalach.
- `bursztyn_ui/popup.h`: wykrywanie modalu w stosie popupów.

Testy:
- `narzedzia/desktop_remaining_cases.h`: nowe przypadki opisane poniżej.
- `narzedzia/test_desktop_files_host.cpp`: integracja nowych przypadków z produkcyjnym handlerem.
- `narzedzia/desktop_usability_cases.h`: istniejący test używa sprawdzanego budowania ścieżki zamiast potencjalnie ucinającego snprintf.
- `narzedzia/file_operations_host_fixture.h`: symulacja odmowy przeniesienia konkretnego źródła, umożliwiająca test częściowego błędu grupy.
- `narzedzia/test_compositor_cursor.cpp`: pełna/częściowa kompozycja alpha oraz zachowanie RGB i widocznej czerni.
- `docs/desktop_remaining_fixes_2026-10-04.md`: ten raport.

Testy przebudowały śledzone binaria `.wm-test`; przywrócono ich poprzednią wersję po zakończeniu testów. ISO, obiekty, ELF, `.su`, katalog `isodir` i pozostałe nieśledzone wyniki budowy pozostawiono jako artefakty. Nie edytowano źródeł Explorera, filesystemu, instalatora, GRUB, sterowników, Shell ani polityki uruchamiania QEMU.

## Testy dodane i zachowane

Nowe przypadki: geometria całego tile; alpha i role w jasnym/ciemnym motywie; długie polskie nazwy i granice UTF-8; nazwa legalna 55 bajtów i odmowa dodatkowego znaku; file→folder i folder→folder przez produkcyjny MouseDown/Move/Up; hover; self-drop; kolizja; odmowa uprawnień; zwolnienie capture; natychmiastowy model po drop; grupa i częściowy błąd; zachowanie schowka; relacje self/descendant/podobny prefiks; prawy klik na tekście, obramowaniu i poza modalem; mapping `.txt`/`.TXT` i folderów oraz prymitywy dokumentu. Skale 100% i 125%.

Istniejący zestaw nadal sprawdza Ctrl+click, multiselekcję, prostokąt zaznaczenia, drag grupy na puste tło, ograniczenia siatki, pojedynczy drag, menu pustego pulpitu/plików/folderów, tworzenie, clipboard i motywy. Testy Explorera obejmują istniejące zachowanie pola ścieżki, dialogów i repaintu. Testy storage/BWS obejmują autoryzację, przenoszenie katalogów, błędy i zwalnianie blokad. Nie wykazano braku GPU timeout w runtime — to pozostaje do obserwacji w gościu.

| Weryfikacja końcowa | Wynik |
|---|---|
| `make test-desktop-files-host` | 1810/1810 PASS |
| `make test-explorer-host` | 12191/12191 PASS |
| `make test-kontrolki4` | 1379/1379 PASS |
| `make test-bursztyn-ui-polecenia` | 23/23 PASS |
| Motywy unit | 155/155 PASS, również osobno ASan/UBSan |
| Motywy GUI | 25/25 PASS |
| `make test-wm-click-routing` | 152/152 PASS |
| `make test-wm-dekoracje` | PASS, 100%/125% |
| `make test-compositor-cursor` | PASS, wraz z nową kompozycją alpha |
| `make test-shell-host` | PASS |
| `make test-cursor-host` | PASS |
| `make test-filesystem-move-host` | Storage/BSP2 10493 PASS, 0 FAIL; move 138; BWS authorization 438 PASS |
| `git diff --check` | PASS |
| Końcowe `make -j2 all iso` | PASS, exit 0 |

ASan/UBSan: bez zgłoszeń w końcowych zestawach C++. Leak detection wyłączone dla większości targetów zgodnie z Makefile; test kompozytora korzysta z detect_leaks=1. Nie jest to ogólna deklaracja braku wycieków. Pierwsza budowa ISO wykryła przekroczenie DATA+BSS po statycznym poszerzeniu nazw; przeniesienie buforów na stertę usunęło ten problem bez zwiększania segmentów BUR. Końcowe WM DATA+BSS: 48320 + 55488 = 103808 B, poniżej 128 KiB.

## Stos

Końcowy pomiar `-fstack-usage`, pojedyncze ramki, nie łączny szczyt:
- WM `_start`: 1264 B.
- `odswiez_wpisy_pulpitu`: 240 B.
- Malowanie wpisu pulpitu, wydzielona część: 704 B.
- `Desktop::wrap`: 640 B.
- Walidacja celu drop: 568 B.
- Handler gestu: 128 B, dynamic bounded.
- Największa istniejąca ramka jednostki WM: dekoder PNG, 3552 B; normalizacja chooser'a 2624 B.
- Explorer `_start`: 2688 B; normalizacja chooser'a 2624 B.

## Runtime

**Nie zweryfikowano. Zero nowych sesji QEMU.** Nie znaleziono bieżącego obrazu `wirtualnydysk.img`/`wirtualny_dysk.img` ani działającego QEMU. Jedyny znaleziony obraz testowy należał do innego projektu. Wcześniejszy raport Live (`docs/explorer_live_runtime_2026-10-04.md`) opisuje zatrzymanie startu przed pulpitem. Nie utworzono nowego dysku, nie formatowano, nie instalowano i nie modyfikowano obrazów. Wyniki RAM/mocków i budowa ISO nie zastępują ręcznej weryfikacji nowego obrazu systemu.

Logi bieżącej weryfikacji w nietrwałym `/tmp`: `desktop-fixes-desktop-final.log`, `desktop-fixes-ui.log`, `desktop-fixes-wm.log`, `desktop-fixes-storage.log`, `desktop-fixes-final-desktop.log` (kompozytor), `desktop-fixes-theme-sanitized.log`, `desktop-fixes-iso-final.log`.
