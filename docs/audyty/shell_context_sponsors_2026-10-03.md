# Shell: menu, wersja i wsparcie — 2026-10-03

## Zakres i implementacja

Zmiany opierają się na obecnym Shellu. Edytor, model 512 linii, zawijanie,
zaznaczanie, pasek przewijania, font i częściowe odmalowanie pozostają istniejącymi
mechanizmami. Nie zmieniano źródeł VirtIO-GPU, SDL, tabletu, restartu, schedulera,
instalatora, GRUB, BSP2 ani ABI BUR. Nie uruchamiano `make rm`.

Menu używa `BursztynUI::MenuKontekstowe`, `WidokMenuPopup` i `ZarzadcaPopupow`.
Jest osobnym natywnym popupem BWS (`gui_ustaw_popup_aplikacji`, `gui_blit_popup`),
z bieżącym motywem, fontem, hover i standardowym stylem wyłączenia. Parametr
`MenuKontekstowe::pokaz` zmieniono z `int` na `SkalaUI`, ponieważ konwersja do
liczby całkowitej gubiła skalę 125% przy wyznaczaniu wymiarów.
Położenie oblicza `umiesc_przy_punkcie_ekranu`, uwzględniając całe menu i cień.
Renderowanie i hit-test korzystają z tego samego widoku i prostokątów.

Prawy przycisk zachowuje zaznaczenie, tak jak natywne `PoleTekstowe` (zmienia je
wyłącznie lewy przycisk). Kliknięcia menu i pierwszy klik poza nim są konsumowane;
po kliknięciu zamykającym konsumowane jest także zwolnienie przycisku. Compositor
już kieruje pierwszy MOUSE_DOWN do właściciela otwartego popupu, również poza nim.
Escape, blur, zmiana ekranu i zamknięcie Shellu zamykają popup. Zarządca usuwa jego
rejestrację; istniejący compositor unieważnia poprzedni prostokąt. Shell nie
próbuje usuwać menu przez malowanie tła na swojej powierzchni.

Schowek: wyłącznie istniejące `clipboard_size`, `clipboard_read`, `clipboard_set`,
MIME `text/plain`, standardowy kończący NUL przy kopiowaniu. Kopiowanie korzysta
z dotychczasowego logicznego zakresu wyjścia albo zakresu wejścia. `Wytnij` usuwa
wejście dopiero po udanym zapisie do schowka. `Wstaw` jest aliasem `Wklej`:
nie znaleziono istniejącej odrębnej semantyki tej operacji.

Po otwarciu nad wyjściem wyłączone są Wstaw, Wklej, Wytnij, Usuń zaznaczone
i Usuń wszystko. Kopiuj jest dostępne tylko dla niepustego zaznaczenia.
Nad wejściem Wytnij i Usuń zaznaczone wymagają zaznaczenia wejścia;
Usuń wszystko czyści wyłącznie polecenie. Wyjście jest tylko do odczytu.

BWS wymaga bufora mieszczącego cały rekord schowka; nie udostępnia odczytu
fragmentu. Przed alokacją sprawdzany jest systemowy limit `BWS_CLIPBOARD_FORMAT_MAX`
(1 MiB), następnie wynik odczytu nie większy od pojemności. Do statycznego
bufora wklejania trafia najwyżej 2047 bajtów. Wstawiany jest jedynie prefiks
kompletnych, poprawnych kodów UTF-8 mieszczący się w aktualnej pojemności wejścia,
z uwzględnieniem zastępowanego zaznaczenia. CR, LF i TAB są zamieniane na spacje:
wklejenie nie wykonuje poleceń. Kursor i kotwica pozostają offsetami bajtowymi na
granicach kodów UTF-8. Nie dodano drugiego schowka.

## Wersja i zasób sponsorów

Publiczne polecenie to `wsparcie`, z opisem pomocy
`Wyświetla listę wspierających Bursztyn OS.` Ścieżka zasobu pozostaje bez zmian.

Centralne źródło to `wersja_systemu.h`: `BURSZTYN_WERSJA_TEKST` i złożony z niego
`BURSZTYN_NAZWA_WERSJI` (`Bursztyn OS v1.0.1.6`). Shell używa go w bannerze,
system, wersja i informacji o bieżącej wersji w aktualizacja. Nie ufa staremu
plikowi `/system/wersja` przy wyświetlaniu wersji uruchomionego Shellu.
Samodzielne `bsh v3.0` nie zmienia się. Trzyczęściowa struktura wersji aktualizacji
pozostaje bez zmian ABI; czwarty składnik jest rewizją wyświetlanej wersji.

Źródło zasobu: `dane/sponsorze/sponsorzy`; dokładna ścieżka runtime:
`/dane/sponsorze/sponsorzy`. Dodano minimalny pusty zasób (jedynie LF), bez
wymyślonych sponsorów. `sponsorzy_blob.o` powstaje przez istniejące `ld -r -b binary`,
jest linkowany do jądra w ISO, a istniejąca ścieżka zasobów tworzy katalogi i zapisuje
go do BSP2 przez `zapisz_plik_od_nowa`. Ta sama ścieżka już zbiera plan plików dla
instalacji; format i kod instalatora nie były zmieniane. Zasób, tak jak cytaty,
jest ponownie instalowany ze źródła podczas zwykłego przygotowania zasobów.

Shell czyta plik przez `pobierz_rozmiar_pliku` i ograniczony `czytaj_plik`.
Limit pliku: 16384 bajty; statyczny bufor: 16385 bajtów z NUL;
limit pojedynczego rekordu: 4095 bajtów. Nie alokuje pamięci na podstawie długości
pliku. Rozmiar jest sprawdzany również po odczycie. Zanim cokolwiek wypisze,
waliduje UTF-8 i długość każdej linii, odrzucając m.in. wewnętrzny NUL.
Pomija puste linie, usuwa końcowe CR z CRLF; nazwy nie są zaszyte w Shellu.
Każdy wpis przechodzi przez normalne `shell_wypisz` z LF, zawijanie i scrollback.
Pomoc zawiera polecenie w kategorii System. Polecenie nie przyjmuje argumentów.

## Pliki źródłowe zmienione lub dodane

- `shell.cpp`
- `shell_terminal.h`
- `shell_context.h`
- `shell_sponsors.h`
- `bursztyn_ui/menu_kontekstowe.h`
- `wersja_systemu.h`
- `kernel.cpp` (wyłącznie dodanie zasobu i katalogów)
- `Makefile`
- `dane/sponsorze/sponsorzy`
- `narzedzia/test_shell_terminal.cpp`
- `narzedzia/shell_runtime_probe.py` (wybór prawego przycisku w QMP)
- `docs/shell_context_sponsors_2026-10-03.md`

Normalna kompilacja pozostawia również obiekty, ELF/bin, staging ISO i ISO;
normalny runuefi utworzył brakujący `wirtualny_dysk.img`.

## Budowa i testy hostowe — nie runtime

`make shell_blob.o -j2` zakończyło się powodzeniem. Początkowy wariant z `-O2`
przekroczył segment tekstu po dodaniu natywnych menu; zastosowano `-Os`, bez zmiany
linkera i nagłówka BUR. Następnie `make -j2 all iso` zakończyło się powodzeniem,
włącznie z kontrolą Multiboot2 i generowaniem ISO.

Testy produkcyjnego kodu Shellu z mockami BWS przeszły przy 100% i 125%, również
z AddressSanitizer i UndefinedBehaviorSanitizer. Pokrycie obejmuje:
dotychczasowy edytor/history/scroll/wrapping/selection; ASCII i polski schowek;
puste wejście, środek, zastępowanie, cięcie, usuwanie, alias Wstaw, limit wejścia,
duży rekord schowka; rzeczywisty klik w element natywnego menu; normalne położenie
i cztery narożniki, geometrię wierszy; blokowanie akcji wyjścia, kopiowanie wyjścia,
Escape, klik poza, zwolnienie bez fallthrough, blur; centralną wersję;
sponsorów z CRLF, pustymi liniami i polskimi znakami, brak pliku, pusty plik,
zbyt duży plik, zbyt długi rekord, długi zawijany rekord i 100 wpisów.

## Runtime

Poprzednia sesja została przerwana po zawieszeniu zewnętrznej VM Linux i QEMU.
W kontynuacji celowo wyłączono wszelkie testy QEMU ze względu na stabilność
VirtualBox. Zachowanie runtime NIE jest zweryfikowane; nie ma runtime PASS.
Końcowy test interaktywny wykonuje użytkownik ręcznie.


## Kontynuacja po zawieszeniu VM — kompilacja i testy hostowe

Stan początkowy: osiem zmodyfikowanych plików śledzonych (Makefile,
bursztyn_ui/menu_kontekstowe.h, kernel.cpp, narzedzia/shell_runtime_probe.py,
narzedzia/test_shell_terminal.cpp, shell.cpp, shell_terminal.h, wersja_systemu.h).
Diff: 122 dodania, 40 usunięć. Nieśledzone źródła: shell_context.h,
shell_sponsors.h, dane/sponsorze/sponsorzy i ten raport. Były też liczne
nieśledzone artefakty .o/.su/.elf/.bin, staging isodir, ISO i manifest tapet.
Wszystkie zasadnicze funkcje menu, wersji i sponsorów były już zaimplementowane.
Zachowano poprzednie zmiany, w tym niewykonywany skrypt QMP.

W tej kontynuacji zmieniono wyłącznie Makefile, shell_context.h,
shell_sponsors.h, shell_terminal.h, narzedzia/test_shell_terminal.cpp i ten raport
(oraz artefakty wskazanych buildów). Dodano alias shellblob.o do shell_blob.o
oraz -fstack-usage dla Shellu. Pierwsze literalne make shellblob.o zwróciło
brak reguły; po dodaniu aliasu polecenie przeszło z kodem 0.
Dodano kontrolę liczby dostępnych bajtów przed dekodowaniem UTF-8, ochronę indeksów
wejścia, kopiowania i usuwania, obsługę bajtowego Escape oraz pominięcie końcowego
CR przy liczeniu limitu treści wiersza sponsorów. Testy rozszerzono o dokładną
granicę wiersza z CRLF, dokładną granicę pliku, NUL, niepoprawne i urwane UTF-8,
ogromne indeksy i zamknięcie Shellu.

Wyniki w tej sesji, w wymaganej kolejności:
- make shellblob.o: kod 0, kompilacja i linkowanie Shellu;
- host: kod 0, produkcyjny kod z mockami BWS, skale 100% i 125%;
- AddressSanitizer + UndefinedBehaviorSanitizer: kod 0; detect_leaks=0,
  ponieważ test utrzymuje globalne zasoby aplikacji do końca procesu;
- make -j2 all iso: kod 0, kontrola Multiboot2 i generowanie ISO przeszły.

Polecenia hostowe:
```sh
g++ -std=c++17 -O1 -ffunction-sections -fdata-sections narzedzia/test_shell_terminal.cpp -Wl,--gc-sections -o /tmp/bursztyn-shell-host-test
/tmp/bursztyn-shell-host-test
g++ -std=c++17 -O1 -ffunction-sections -fdata-sections -fsanitize=address,undefined narzedzia/test_shell_terminal.cpp -Wl,--gc-sections -o /tmp/bursztyn-shell-host-sanitized
ASAN_OPTIONS=detect_leaks=0 /tmp/bursztyn-shell-host-sanitized
```

shell_tmp.su dla finalnego builda: shell_context_action 112 B,
shell_valid_codepoint (constprop) 64 B, shell_context_close 8 B,
shell_copy_selection 48 B. Pozostałe nowe funkcje zostały włączone przez
kompilator do wywołujących, więc nie mają osobnej ramki w raporcie.
Największa ramka w całym Shellu: _start 6896 B; pobierz_znak_blokujaco 208 B;
ZarzadcaPopupow::odrysuj_popupy 544 B. To rozmiary pojedynczych ramek,
a nie pomiar całego łańcucha wywołań runtime.
Statyczne bufory sponsorów i schowka nie zajmują stosu. DATA+BSS finalnego ELF
zajmuje 93184 B z limitu linkera 262144 B; wszystkie asercje linkera przeszły.

Po wskazanych testach i kompilacji nie uruchamiano dalszych testów.
Nie uruchomiono QEMU, runuefi, GDB, pętli obciążeniowych ani procesów QEMU w tle.
Nie wykonywano make clean ani make rm, nie instalowano pakietów hosta.
