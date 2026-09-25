# BTP — Bursztynowa TaPeta

Preferred format for new wallpapers: BTP2. See [BTP2.md](BTP2.md) for the complete
binary layout, codec, validation rules and CLI, and
[BTP2_BENCHMARK.md](BTP2_BENCHMARK.md) for measured compression. The BTP1 details below are
retained for legacy tools and reference assets.

Narzędzie **hostowe** do przygotowania plików `.btp` oraz samodzielny,
freestandingowy dekoder C++ używany przez pulpit OS.

## Uruchomienie konwertera

Wymagane: Python 3.10+ i Pillow 9.1+ (sprawdzono z Pythonem 3.12 i Pillow 10.2).
Na Debianie/Ubuntu Pillow jest dostępne jako `python3-pil`; w środowisku pip:
`pip install Pillow`. GUI dodatkowo wymaga Tkinter (`python3-tk`) i ekranu.
Program niczego nie instaluje automatycznie. CLI nie importuje Tkinter.

Uruchamiaj z głównego katalogu repozytorium:

```sh
# GUI: wielokrotny wybór PNG/JPG/JPEG/BMP, ustawienia i raport dla każdego pliku.
python3 narzedzia/tapety-btp/konwerterbtp.py

# CLI: pojedynczy plik, BTP2, rozdzielczość natywna.
python3 narzedzia/tapety-btp/konwerterbtp.py tapeta.png tapeta.btp

# Bez drugiej ścieżki: konwertowane/zdjęcie.btp, względem bieżącego katalogu.
python3 narzedzia/tapety-btp/konwerterbtp.py zdjęcie.jpg

# Własny folder i maksymalny rozmiar, z zachowaniem proporcji.
python3 narzedzia/tapety-btp/konwerterbtp.py obraz.bmp \
    --output-dir wynik --max-width 1280 --max-height 720

# Wymuszenie trybu dla diagnostyki.
python3 narzedzia/tapety-btp/konwerterbtp.py obraz.png raw.btp --force-raw
python3 narzedzia/tapety-btp/konwerterbtp.py obraz.png rle.btp --force-rle

# Starszy BTP1 i jego opcjonalna stratna kwantyzacja.
python3 narzedzia/tapety-btp/konwerterbtp.py obraz.png legacy.btp --btp-version 1 --optimize
```

CLI zwraca 0 po sukcesie, 1 po błędzie konwersji, 2 po błędzie składni CLI,
130 po przerwaniu przez użytkownika. Poniższy opis trybu AUTO dotyczy BTP1;
BTP2 porównuje RAW, RLE, LZ i hybrydę osobno dla każdego bloku.
Tryb AUTO BTP1 zawsze porównuje **payloady**:
RLE wygrywa wyłącznie, gdy `rle_size < raw_size`. Przy remisie wybierany jest RAW.
Oba tryby mają ten sam 14-bajtowy nagłówek. `--force-rle` może dać większy plik.

GUI oferuje presety 800×600, 1024×768, 1280×720 i 1920×1080 oraz własną szerokość
i wysokość. Wymiary oznaczają prostokąt maksymalny: proporcje są zachowane,
mniejsze obrazy nie są powiększane, a pomniejszanie korzysta z LANCZOS.
Orientacja EXIF jest uwzględniana. Konwersja działa w tle, kolejno po jednym pliku;
błąd jednego obrazu nie przerywa całej kolejki. Okno czeka z zamknięciem na zapis.

Nazwy zachowują polskie znaki i otrzymują małe `.btp`. Zarówno CLI, jak i GUI
stosują tę samą politykę kolizji: `tapeta.btp`, `tapeta_1.btp`, `tapeta_2.btp`, …
Istniejący plik nie jest nadpisywany, także gdy pojawi się podczas zapisu.
Kompletny, zsynchronizowany plik tymczasowy jest publikowany atomowo przez
`os.link`, po czym nazwa tymczasowa jest usuwana. Folder docelowy musi obsługiwać
twarde dowiązania; w przeciwnym razie konwersja zgłasza błąd. Błędy zapisu
sprzątają plik tymczasowy. Awaria procesu/systemu może pozostawić `.btp-*.tmp`.

## Kolory, alfa i rozmiary

Obraz jest przetwarzany jako RGBA w Pillow, a payload zawsze zawiera **BGRA**.
BTP2 domyślnie zachowuje bajt alfa; BTP1 domyślnie wymusza 255.
`--preserve-alpha` w BTP1 / „Zachowaj kanał alfa” zachowuje bajt alfa; nie dodaje
obsługi mieszania alfa w OS. Bursztyn traktuje tylko dokładne `0x00000000` jako
przezroczystość. `A=0` z niezerowym RGB nadal oznacza widoczny kolor.
Nieprzezroczysta czerń ma bajty `00 00 00 FF` i pozostaje widoczna.

„Optymalizuj kolory” / `--optimize` jest **stratne**: każdy kanał RGB otrzymuje
`channel & 0xF8` (5 bitów precyzji, wartości 0–248). Alfa nie jest kwantyzowana.
Bez tej opcji zachowana jest pełna 8-bitowa precyzja RGB. Po przygotowaniu
kolorów zawsze mierzone są oba payloady. Kwantyzacja nie gwarantuje poprawy RLE.

Raport pokazuje źródło, wynik, wymiary, tryb, rozmiary RAW/RLE, pełny rozmiar BTP,
stosunek BTP do 32-bitowych pikseli RAW i oszczędność. Stosunek i oszczędność
uwzględniają nagłówek, więc RAW ma narzut 14 bajtów, a oszczędność może być ujemna.

Przykłady z deterministycznych testów (rozmiary w bajtach):

| Obraz | RAW | RLE | AUTO | Pełny BTP |
|---|---:|---:|---|---:|
| Czarny 1×1 | 4 | 5 | RAW | 18 |
| Jednolity 10×10 | 400 | 5 | RLE | 19 |
| Jedna seria 600 pikseli | 2400 | 15 | RLE | 29 |
| Szum 31×29 | 3596 | 4495 | RAW | 3610 |
| Próbka fotograficzna 23×19 | 1748 | 2185 | RAW | 1762 |
| 5 pikseli, 4 serie | 20 | 20 | RAW | 34 |

Limit osi wynosi 8192 zarówno dla źródła, jak i ustawień celu. Źródła większe
od tego limitu są odrzucane przed dekodowaniem, nawet jeśli miałyby być zmniejszone.
Dodatkowy konserwatywny budżet buforów to 768 MiB: przed `load()` sprawdzane jest
`16 * source_pixels + 20 * min(source_pixels, max_width * max_height)`.
Może to odrzucić duży obraz mieszczący się w limicie osi; pomaga wtedy mniejszy
rozmiar docelowy lub wstępne zmniejszenie źródła. Budżet nie jest limitem RSS
ani izolacją procesową Pillow. Ostrzeżenia o bombach dekompresyjnych są błędami.
Animacje i wielostronicowe obrazy są odrzucane. Nie są kopiowane metadane ani ICC.

Konwerter przechowuje zwarte bufory, bez listy krotek dla wszystkich pikseli.
Rozmiar RLE jest najpierw liczony bez budowania payloadu. Jeśli RLE zostanie
wybrane lub wymuszone, drugi przebieg tworzy `bytearray`; RAW wykorzystuje
istniejące bajty BGRA bez kopii. To ogranicza pamięć kosztem dodatkowego przebiegu.

## BTP1 i dekoder C++

Nagłówek ma **14 bajtów**, jest packed i little-endian:

| Offset | Typ | Wartość |
|---:|---|---|
| 0 | 4 bajty | ASCII `BTP1` |
| 4 | uint32 LE | szerokość |
| 8 | uint32 LE | wysokość |
| 12 | uint8 | `32` |
| 13 | uint8 | `0` RAW lub `1` RLE |

RAW (`compressed=0`) to dokładnie `width * height * 4` bajtów, po cztery
`B G R A` na piksel. Nie ma wyrównania wierszy, metadanych ani końcowego paddingu.
RLE (`compressed=1`) to pakiety po pięć bajtów: `count B G R A`, count 1–255.
Serie dłuższe niż 255 są dzielone; mogą przecinać granice wierszy. Kolejność
w obu trybach: od lewej do prawej, następnie kolejny wiersz od góry do dołu.
Inne wartości kompresji są odrzucane. Format i magic nie zostały zmienione.

`tapetybtp.cpp` zachowuje API z etapu 1:

```cpp
BtpWynik btpwczytajirysuj(
    const uint8_t* danepliku, size_t rozmiarpliku,
    uint32_t* framebuffer, uint32_t szerokoscfb, uint32_t wysokoscfb,
    uint32_t stridepikseli, size_t rozmiarfb
) noexcept;
```

`stridepikseli` to odległość wierszy w pikselach (dla pitch w bajtach: `pitch/4`
po walidacji podzielności), a `rozmiarfb` to pojemność w bajtach obejmująca
`stridepikseli * wysokoscfb * 4`. Cel to poprawnie zmapowany RAM/backbuffer,
nie MMIO. Wywołujący odpowiada za mapowanie, synchronizację i niezmienność wejścia.
Obrazy większe od celu są odrzucane; rysowanie zaczyna się w `(0,0)`.

Kolor jest składany z osobnych bajtów jako `0xAARRGGBB`; RGB odpowiada natywnemu
XRGB, a górny bajt zachowuje marker widoczności. Dokładne zero pomija zapis.
Dekoder nie alokuje pamięci. RAW wymaga dokładnego rozmiaru przed pierwszym
zapisem (`BTPBLADRAW` przy różnicy); RLE zachowuje pełny przebieg walidacji przed
rysowaniem. Przy spełnieniu kontraktu błędny plik nie modyfikuje celu i nie
zwraca częściowego sukcesu. Nagłówek, geometria, bufory i arytmetyka są sprawdzane
tak jak w etapie 1. Nie jest wymagany nowy nagłówek C++ ani zmiana Makefile.

## Testy hostowe

```sh
python3 -B narzedzia/tapety-btp/testy/testy_btp.py
# Dodatkowo rzeczywisty test GUI, wymagający dostępnego ekranu:
BTP_TEST_GUI=1 python3 -B narzedzia/tapety-btp/testy/testy_btp.py
```

Testy wymagają Pillow, `g++`, `nm` i bibliotek sanitizerów na hoście.
Budują pliki wykonywalne i generują wszystkie obrazy w katalogach tymczasowych.
Nie zapisują tapet ani binariów w repozytorium lub w obrazie OS.

Sprawdzane są RAW/RLE/AUTO dla 16 próbek (czarny/biały piksel, jednolite kolory,
serie >255, szachownica, pasy, gradient, szum, próbka fotograficzna, oba ustawienia
alfa, nieparzyste wymiary, stres 257×255 i remis), oraz 500 losowych obrazów
w trzech trybach. Wszystkie 48 zakodowanych próbek trafiają także do dekodera C++.
Harness zachowuje przypadki błędów etapu 1, dodaje RAW i porównuje dekoder
z niezależną referencją dla 100 000 przypadków RLE oraz 100 000 RAW pod ASan/UBSan
i LeakSanitizer. Sprawdza padding, niezmieniony cel przy błędach, niewyrównane wejście,
alfa, przepełnienia, stride, pojemność i nakładanie buforów.

Osobny test kompiluje dekoder z flagami freestanding i wymaga pustego `nm -u`.
Na Linux x86-64 test i386 rzeczywiście uruchamia wariant 32-bitowy bez libc,
sprawdzając przepełnienia i widoczną czerń w obu trybach. Gdy host blokuje i386,
wynik jest jawnie oznaczony jako pominięty, nie jako sukces runtime.
LeakSanitizer nie działa pod ptrace; w takim sandboxie zgłaszany jest błąd testu
i zestaw należy uruchomić w środowisku pozwalającym na jego wykonanie.

BTP jest ładowany przez usługę tapet; szczegóły aktualnego formatu i limitów
runtime opisuje BTP2.md. Obsługa `tapeta.bmp` pozostaje nienaruszona.
