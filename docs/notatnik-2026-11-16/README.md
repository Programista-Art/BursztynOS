# Notatnik: polskie znaki (AltGr), natywne dialogi plików, menu kontekstowe (2026-11-16)

## Przyczyna braku polskich znaków

Sterownik klawiatury (`sterowniki/wejscie/klawiatura.cpp`) poprawnie tłumaczył AltGr+litera na `TEXT_INPUT` (U+0105 itd.). Błąd leżał we wspólnym `BursztynUI::Okno::obsluz_zdarzenie` (`bursztyn_ui/okno.h`). Każde fizyczne wciśnięcie klawisza o kodzie `0x38` było tam traktowane jak Alt aktywujący pasek menu, także prawy Alt (`E0 38`, USB 230), czyli AltGr. Fokus przechodził więc do `MenuGlowne`, a następujący `TEXT_INPUT` trafiał do menu, które go ignoruje.

Dotyczyło to tylko okien BursztynUI z paskiem menu:

* Notatnik: edytor.
* Husarz: pasek adresu (pole strony miało własne obejście).

Zmiana nazwy w Eksploratorze działała, bo pole edycji obsługuje surowe zdarzenia BWS bezpośrednio, bez tej ścieżki `Okno`.

Reprodukcja na drzewie HEAD (minimalny program: Okno, MenuGlowne, pole edycji, zdarzenia sterownika dla AltGr+A):

| Wersja | Fokus | Tekst w polu |
|---|---|---|
| HEAD | `MenuGlowne` | pusty |
| Po poprawce | edytor | `ą` |

Drugi problem tej samej klasy pojawia się na hostach, które wysyłają AltGr jako LCtrl+RAlt. Sprawdzenia „Ctrl+litera” odpalały wtedy skróty dla polskich liter. Przykłady: AltGr+A → Ctrl+A, AltGr+X → wytnij, AltGr+Z → cofnij, AltGr+S → zapisz, AltGr+N → nowy dokument.

## Poprawka (centralna)

* `jadro/bws/bws_zdarzenia.h`: addytywny bit `BWS_MOD_ALTGR` (ABI 40 B bez zmian) oraz funkcje `bws_mod_ctrl_skrotu` i `bws_mod_alt_skrotu`.
* `sterowniki/wejscie/klawiatura.cpp`: prawy Alt ustawia `BWS_MOD_ALTGR`. `BWS_MOD_ALT` zostaje dla zgodności.
* `bursztyn_ui/zdarzenia.h`: `czy_ctrl()` i `czy_alt()` zwracają fałsz przy AltGr, a nowa metoda `czy_altgr()` go zgłasza.
* `bursztyn_ui/okno.h`: tylko lewy Alt aktywuje menu.
* `bursztyn_ui/polecenie_ui.h`: AltGr nigdy nie pasuje do skrótu.
* `sterowniki/grafika/grafika.cpp`: AltGr+Tab nie przełącza okien.
* `programy/przegladarka/przegladarka.cpp`: skróty Ctrl i Alt Husarza ignorują AltGr.

## Notatnik

* Otwórz i Zapisz jako używają istniejącego `BursztynUI::DialogPliku` (powierzchnia popup procesu, bez nowego API jądra i bez dodatkowych uprawnień PZB). Filtry: dokumenty tekstowe, `*.txt`, wszystkie pliki.
* Poprawki w `DialogPliku`:
  * podwójny callback przy Anuluj;
  * domyślne rozszerzenie z filtra wielorozszerzeniowego;
  * fokus startowy w polu nazwy.
* Ochrona niezapisanych zmian przy: Nowy, Otwórz, otwarciu pliku z systemu, zamknięciu okna. Pytanie Tak/Nie/Anuluj, a „Tak” dla nienazwanego dokumentu prowadzi przez Zapisz jako.
* Zapis jest weryfikowany ponownym odczytem. Przy błędzie dokument, ścieżka i znacznik `*` zostają bez zmian.
* Pasek narzędzi ma tylko Nowy, Otwórz i Zapisz. Edycja jest w menu Edycja i w menu kontekstowym BursztynUI (Cofnij, Ponów, Wytnij, Kopiuj, Wklej, Zaznacz wszystko), z dostępnością zgodną ze stanem edytora i schowka.
* `PoleWielowierszowe`:
  * tablica linii wizualnych rośnie dynamicznie (wcześniej dokument ucinany po 2048 liniach);
  * historia cofania jest czyszczona po wczytaniu pliku;
  * dodano `czy_mozna_cofnac/ponowic`.

## Dowody

* Test hosta `make -j1 test-notatnik-polski-host` (`testy/test_notatnik_polski_host.cpp`, ASan/UBSan): prawdziwy sterownik i pełne okno Notatnika.
* Rozszerzony `make -j1 test-husarz-runtime-host`: pasek adresu, AltGr przez PS/2 i USB, Ctrl+AltGr.
* `natywne/`: jeden przebieg QEMU (UEFI, 1280×720, `-smp 1`, USB kbd), 0 przestojów hosta. Zrzuty pokazują:
  * pisanie przez AltGr;
  * menu kontekstowe;
  * Zapisz jako „Notatka żródlo.txt” (72 B);
  * ponowne otwarcie przez dialog Otwórz.
* `natywne-husarz/`: osobny krótki przebieg, w którym pasek adresu Husarza przyjmuje `ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ`.
* Skrypt: `narzedzia/notatnik_polski_qemu.py KATALOG [husarz]`.

## Ograniczenia

* System plików nie ma atomowej podmiany (zob. „Bezpieczny zapis” niżej). Notatnik chroni dane kopią bezpieczeństwa, ale nie gwarantuje atomowości.
* Limit dokumentu to 64 KiB (bez zmian).
* Historia cofania ma 7 kroków, po jednym na każdą edycję.
* Natywnie nie weryfikowano: pól formularzy stron w Husarzu (tylko test hosta), zmiany nazwy w Eksploratorze (kod bez zmian, testy hosta przechodzą), BIOS, innych rozdzielczości, sprzętu fizycznego.

## Bezpieczny zapis (2026-11-17)

### Ryzyko przed zmianą

PSF (`jadro/fs/psf.cpp`, `zapisz_do_pliku`, wywoływane przez BWS 3) nadpisuje plik w miejscu, w tej kolejności:

1. zwalnia bloki ponad nowy rozmiar i ustawia nowy rozmiar;
2. kopiuje dane blok po bloku, dokładając brakujące bloki;
3. zapisuje brudne bloki na nośnik (`bsp_zapisz_zmiany`).

Skutki awarii:

* Błąd w kroku 2 (brak wolnego bloku, czyli pełny dysk) zostawia plik częściowo nadpisany, a BWS 3 zwraca błąd. Zmiana w RAM-dysku PSF zostaje.
* Błąd w kroku 3 (zapis na nośnik) wyłącza zapis trwały do końca sesji (`zapis_trwaly_dozwolony=false`), a kolejne zapisy zwracają błąd.
* Notatnik wcześniej wykrywał błąd i niezgodność przy odczycie kontrolnym. Oryginału jednak nie odtwarzał, a nieudany zapis nowego pliku zostawiał pusty lub niekompletny plik.

### Gwarancje systemu plików

| Operacja | Zachowanie w PSF |
|---|---|
| Utworzenie (BWS 2) | Odmowa, gdy nazwa jest zajęta. |
| Usunięcie (BWS 7) | Dostępne. |
| Zmiana nazwy (BWS 8) | Tylko w tym samym katalogu. Odmowa, gdy nazwa docelowa istnieje, bez nadpisania. Wynik zapisu na nośnik jest ignorowany. |
| Przeniesienie (BWS 48) | Do innego katalogu. |
| Kopiowanie (BWS 55) | Do innego katalogu, z tą samą nazwą. |
| Zapis na nośnik | Przy każdym zapisie. |
| Dziennik transakcji | Brak. |
| Atomowa podmiana | Brak. |
| Pliki tymczasowe | Brak osobnego mechanizmu, to zwykłe pliki. |

Wariant „plik tymczasowy, potem zmiana nazwy” wymagałby usunięcia albo przemianowania oryginału przed nadaniem nazwy nowemu plikowi. Przez chwilę pod nazwą dokumentu nie byłoby żadnego pliku. Plik dostałby też nowy węzeł, czyli inny poziom PZB i czas utworzenia. Dlatego go nie użyto.

### Wdrożona strategia (bezpieczna, nie atomowa)

`NotatnikApp::zapisz_z_kopia` w `programy/notatnik/notatnik.cpp`:

1. Oryginał jest czytany do pamięci. Jeśli jest identyczny z dokumentem, nic nie jest zapisywane.
2. Powstaje **nowy** plik kopii `~nazwa.kopia` obok oryginału, a gdy ta nazwa jest zajęta, `~nazwa.kopia1` … `~nazwa.kopia9`.
   * Istniejące pliki o tych nazwach nie są ani nadpisywane, ani usuwane.
   * Nazwa jest skracana na granicy UTF-8 do 55 bajtów.
   * Kopia jest weryfikowana ponownym odczytem.
   * Gdy kopii nie da się utworzyć lub zweryfikować (brak miejsca, pamięci, uprawnień, wolnej nazwy), plik docelowy nie jest w ogóle zapisywany.
3. Dokument jest zapisywany w miejscu i weryfikowany. Węzeł pliku, PZB i czas utworzenia zostają.
4. Sukces: kopia jest usuwana. Jeśli usunięcie się nie uda, zapis i tak się liczy, a pasek stanu podaje nazwę kopii.
5. Błąd: oryginał jest przywracany z pamięci i weryfikowany, a kopia usuwana.
6. Nieudane przywrócenie: kopia **zostaje**, a komunikat podaje jej pełną ścieżkę.
7. Nowy plik: przy błędzie zapisu lub weryfikacji usuwany jest wyłącznie plik utworzony przed chwilą przez ten zapis.
8. Otwarcie pliku, obok którego leży kopia (np. po przerwanym zapisie), pokazuje ostrzeżenie. Kopia nie jest usuwana.

Niezmiennik: po każdej pojedynczej operacji na plikach pełna stara treść jest w pliku docelowym albo w zweryfikowanej kopii, albo plik docelowy ma pełną nową treść. Znacznik `*` jest zdejmowany tylko po zweryfikowanym zapisie.

### Testy

`make -j1 test-notatnik-polski-host` (makieta systemu plików, bez dysku), sekcja 12:

* sukces i kolejność „kopia przed nadpisaniem”;
* zapis częściowy, przerwany w połowie;
* nieudane przywrócenie, po którym kopia zostaje;
* brak miejsca na kopię i na nową wersję;
* niezgodność odczytu kontrolnego pliku i kopii;
* odmowa utworzenia kopii, brak pamięci;
* plik użytkownika o nazwie kopii, wszystkie nazwy kopii zajęte;
* nieudane usunięcie kopii;
* pusty oryginał;
* nowe pliki (zapis częściowy, odmowa, uszkodzenie, pełny dysk, nieudane usunięcie);
* Zapisz jako z błędem;
* długie nazwy UTF-8;
* plik ponad 64 KiB;
* przepływ Ctrl+S z klawiatury.

Przerwanie jest symulowane migawką stanu po każdej mutacji, a każda migawka spełnia niezmiennik. Dwie celowe mutacje kodu (pominięcie przywracania, pominięcie kopii) są wykrywane.

### Ograniczenia

* Brak atomowości: przerwanie w trakcie zapisu w miejscu zostawia plik docelowy niekompletny. Pełny oryginał jest wtedy w `~nazwa.kopia*`, który użytkownik musi przywrócić ręcznie (np. w Eksploratorze). Notatnik tylko ostrzega przy otwarciu.
* Zapis wymaga wolnego miejsca na kopię oryginału i prawa tworzenia plików w katalogu (PZB wymaga tego samego dla modyfikacji pliku).
* Kopia jest tworzona tylko dla oryginałów do 16 MiB i wymaga tyle pamięci.
* Przy awarii zapisu na nośnik PSF wyłącza zapis trwały do końca sesji. Notatnik zgłasza wtedy błąd zamiast przywracać, a przywracanie dotyczy RAM-dysku.
* Nie testowano natywnie w QEMU.
