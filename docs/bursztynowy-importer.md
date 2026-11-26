# Bursztynowy Importer Plików

Graficzne narzędzie **hosta** (Python 3 + Tkinter) do kopiowania muzyki, obrazów i dokumentów do obrazu dysku
**zainstalowanego** Bursztyn OS (`wirtualny_dysk.img`). To nie jest aplikacja Bursztyn OS – działa na
Windows 11 i Linuksie, a Bursztyn OS nie potrzebuje Pythona ani Tkintera.

Na prawdziwym sprzęcie pliki kopiuje się natywnie Eksploratorem Bursztyn OS (patrz „Bez importera”).

## Uruchomienie

Wymagania: Python 3.8+ z Tkinterem (biblioteka standardowa; bez dodatkowych pakietów).

* **Windows 11**: zainstaluj Pythona z python.org (opcja „tcl/tk and IDLE” jest domyślnie zaznaczona), potem
  dwuklik `narzedzia\bursztynowy_importer_start.pyw` albo w PowerShell:
  `py narzedzia\bursztynowy_importer_start.pyw`
* **Linux**: `python3 narzedzia/bursztynowy_importer_start.pyw`
  (Debian/Ubuntu: Tkinter z pakietu `python3-tk`, jeżeli go brakuje).
* Alternatywnie z katalogu `narzedzia/`: `python3 -m bursztynowy_importer`.

Opcjonalnie (nie jest wymagane) można zbudować samodzielny plik `.exe`, np. PyInstallerem:
`pyinstaller --onefile --windowed narzedzia/bursztynowy_importer_start.pyw`. Repozytorium tego nie robi.

## Przebieg pracy

1. **Wyłącz całkowicie** maszynę wirtualną z Bursztyn OS (QEMU/VirtualBox).
2. *Wybierz obraz dysku* – importer od razu go sprawdza (tylko odczyt) i pokazuje system plików, partycje,
   pojemność, zajęte i wolne miejsce oraz wolne węzły. *Pokaż zawartość* wyświetla drzewo `/dane` i `/uzytkownicy`.
3. *Dodaj pliki* / *Dodaj folder* (rekurencyjnie, bez podążania za dowiązaniami symbolicznymi).
   Pliki są kategoryzowane automatycznie; *Muzyka*, *Obrazy*, *Dokumenty* albo *Własny katalog* zmienia cel
   zaznaczonych pozycji. *Usuń z listy importu* usuwa pozycje z listy (nie z dysku).
4. Konflikty nazw: *Pomiń* (domyślnie), *Zmień nazwę* („utwór (2).mp3”) albo *Zastąp* – zastępowanie wymaga
   osobnego potwierdzenia i nigdy nie jest domyślne.
5. *Rozpocznij import*. Postęp i komunikaty widać na dole okna; *Anuluj* przerywa na bezpiecznej granicy
   (między plikami / porcjami kopii) – oryginał pozostaje bez zmian.
6. Po udanej weryfikacji importer pyta, czy zastąpić obraz zweryfikowaną kopią. *Pokaż raport* wyświetla
   pełny raport z sumami SHA-256.
7. Uruchom Bursztyn OS z tego dysku – Grajek znajdzie muzykę w `/dane/muzyka` po starcie albo po
   „Odśwież bibliotekę”. Pliki pozostają po każdym kolejnym restarcie (dysk trwały).

## Katalogi docelowe (kanoniczne katalogi Bursztyn OS)

| Kategoria | Rozszerzenia | Katalog |
|---|---|---|
| Muzyka | `.mp3`, `.wav` | `/dane/muzyka` (Grajek) |
| Obrazy | `.jpg`, `.jpeg`, `.png`, `.webp`, `.gif`, `.bmp` | `/dane/zdjecia` (istniejący katalog zdjęć) |
| Dokumenty | `.txt`, `.pdf`, `.doc`, `.docx`, `.odt`, `.rtf` | `/uzytkownicy/domyslny/Dokumenty` |
| Inne | dowolne | *Własny katalog* pod `/dane` albo `/uzytkownicy` |

Katalogi `/dane/muzyka` i `/dane/zdjecia` tworzy jądro przy każdym starcie (`jadro/fs/psf_bootstrap.h`),
`/uzytkownicy/domyslny/Dokumenty` – `katalogi_uzytkownika.h`. Brakujący katalog importer tworzy z tymi samymi
metadanymi co jądro (PZB 4 – użytkownik). Drzewa systemowe (`/system`, `/programy`, `/jadro`, `/gry`,
`/aktualizacje`, `/sloty`) są odrzucane, tak samo ścieżki z `..`, `//` i `\`.

## Format dysku (zweryfikowany w kodzie, nie zakładany)

Obraz zainstalowanego systemu to **surowy** obraz dysku z GPT (`jadro/fs/storage_format.cpp`):
ESP (FAT, 512 MiB) i partycja Bursztyn OS (typ `b21fe73c-0414-4585-bd07-8ab4980f9c88`, do 2 GiB) z systemem
plików **BSP2/PSF** (`jadro/fs/psf.h`, `psf.cpp` – to nie jest FAT ani ext4) i znacznikiem instalacji
`BSPINST1` w dwóch ostatnich sektorach partycji. Importer odrzuca: QCOW2, VDI, ISO, obrazy bez GPT, obcięte,
z uszkodzoną GPT, bez kompletnego znacznika instalacji, z uszkodzonym superblokiem lub metadanymi
(nieznany typ węzła, wskaźnik poza obszarem danych lub do nieprzydzielonego bloku, bloki współdzielone przez
dwa pliki, cykle, powtórzone nazwy, wpisy do zwolnionych węzłów). Rozszerzenie `.img` niczego nie przesądza.

Limity formatu (stosowane przed zapisem, nigdy przez cichą zmianę):

* nazwa: 1–55 bajtów UTF-8 (polskie znaki zajmują 2 bajty) – dłuższa nazwa jest zgłaszana z bezpieczną
  propozycją (zachowane rozszerzenie), którą trzeba jawnie przyjąć przyciskiem *Przyjmij proponowaną nazwę*,
* plik: do 1 077 846 016 B (ok. 1,0 GiB) (490 wskaźników bezpośrednich + blok pośredni + blok podwójnie pośredni),
  ograniczony też wolnym miejscem partycji (do 2 GiB),
* **1024 węzły na cały system plików** (każdy plik i katalog zajmuje jeden) – importer pokazuje liczbę wolnych,
* ścieżka do 511 bajtów; muzyka ze ścieżką dłuższą niż 191 bajtów dostaje ostrzeżenie (limit Grajka).

## Bezpieczeństwo i transakcja

Oryginał **nigdy** nie jest modyfikowany w miejscu:

1. inspekcja i pełna walidacja oryginału (tylko odczyt),
2. plan: nazwy, konflikty, dokładne zapotrzebowanie na bloki i węzły w Bursztyn OS oraz miejsce na dysku
   komputera (kopia robocza + margines) – przy braku miejsca import jest odrzucany z wyjaśnieniem; zapis
   „w miejscu” nie jest wykonywany jako obejście,
3. wyłączność: plik blokady `<obraz>.importer-blokada`, blokada pliku (Linux), odmowa, gdy obraz jest otwarty
   przez inny proces (QEMU/VirtualBox: `/proc`, blokady OFD QEMU; Windows: otwarcie bez współdzielenia
   i lista procesów),
4. kopia robocza `<obraz>.importer-kopia-robocza` w tym samym katalogu, sprawdzona SHA-256,
5. zapis plików strumieniowo (porcje 1 MiB) na kopii; nowe bloki są brane wyłącznie z wolnych bloków
   oryginału, a modyfikowane bloki katalogów są kopiowane do nowych bloków,
6. weryfikacja całej kopii: GPT, znacznik, superblok, całe drzewo, zawartość (SHA-256) każdego importowanego
   pliku odczytana niezależnym czytnikiem oraz dowód, że **każdy bajt poza zmienionymi blokami i metadanymi
   jest identyczny z oryginałem** (GPT, ESP, znacznik, istniejące pliki),
7. dopiero po potwierdzeniu: oryginał → `<obraz>.przed-importem-RRRRMMDD-GGMMSS` (zachowany),
   kopia → nazwa oryginału. To dwie operacje zmiany nazwy w tym samym katalogu – **nie są razem atomowe**;
   jeśli druga się nie uda, oryginał wraca na miejsce. Kopię zapasową usuwa się ręcznie po sprawdzeniu
   plików w Bursztyn OS.

Każdy błąd, anulowanie albo nieudana weryfikacja usuwa kopię roboczą i pozostawia oryginał bez zmian.
Importer nie zapisuje do dysków fizycznych ani partycji systemu hosta – tylko do wybranego pliku obrazu.

## Bez importera: Eksplorator Bursztyn OS

Na zainstalowanym systemie `/dane/muzyka` leży na trwałym dysku. Eksplorator potrafi tworzyć foldery, kopiować
(w jądrze, blokami 4 KiB – bez wczytywania całego pliku), przenosić, zmieniać nazwy i usuwać z potwierdzeniem;
pasek stanu pokazuje wolne miejsce i rodzaj pamięci (BWS 111: „dysk trwały” albo „RAM: pliki znikną po
restarcie”). **Brakuje jednak źródła plików z zewnątrz**: Bursztyn OS nie ma sterownika pamięci masowej USB
ani montowania nośników wymiennych, więc import z pendrive'a nie działa.

## Testy

`make -j1 test-importer` – 22 testy na jednorazowych obrazach (nigdy na `wirtualny_dysk.img`). Obrazy tworzy
narzędzie zbudowane z produkcyjnego kodu jądra (`narzedzia/bursztynowy_importer/psf_obraz_host.cpp`:
`storage_format.cpp` + `psf.cpp`), które po imporcie montuje obraz jak zainstalowany system, waliduje metadane
(bez żadnych „napraw”) i liczy SHA-256 plików – niezależnie od zapisu w Pythonie. Test E2E
(`testy/test_importer_grajek_host.cpp`) montuje obraz po imporcie i uruchamia bibliotekę i dekodery Grajka
(WAV z metadanymi, MP3, WAV 130 MiB z przewijaniem za 128 MiB). To test ścieżki odczytu na hoście, nie test
w QEMU.
