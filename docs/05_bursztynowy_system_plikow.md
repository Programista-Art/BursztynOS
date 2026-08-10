# 05. Bursztynowy System Plików (BSP)

Niniejszy dokument opisuje architekturę, struktury danych oraz mechanizmy operacyjne **Bursztynowego Systemu Plików (BSP)**. BSP został zaprojektowany jako autorski, hierarchiczny system plików dla Bursztyn OS, odrzucający standardy zewnętrzne (takie jak FAT, ext czy NTFS) na rzecz uproszczonej struktury indeksowej zintegrowanej bezpośrednio z systemem uprawnień BZL / PZB.

## 5.1 Warstwa Sprzętowo-Pamięciowa (AHCI SATA & RAM-dysk)

BSP działa w oparciu o architekturę hybrydową, łączącą bezpośredni dostęp do kontrolera pamięci masowej AHCI z buforowaniem pamięci wirtualnej:

* **Trwałość danych (SATA/AHCI):** System plików jest utrwalany na fizycznym lub emulowanym dysku SATA poprzez natywny sterownik AHCI. Podczas startu jądro weryfikuje nagłówek partycji (LBA) i w razie potrzeby formatuje struktury BSP.
* **Rozmiar partycji/wolumenu:** 2 Megabajty (2 MB).
* **Adres wirtualny jądra:** `0x130000000ULL` (powyżej 4 GB, w bezpiecznej strefie adresowej VMM).
* **Alokacja w VMM:** Podczas inicjalizacji w `kernel.cpp`, **Zarządca Pamięci Wirtualnej (VMM)** alokuje ramki fizyczne i mapuje je ciągle pod adres `0x130000000ULL` z flagami `FLAGA_OBECNA | FLAGA_ZAPIS` (`0b00000011`), co eliminuje konflikty adresowe z Wielkimi Stronami (2 MB Pages) zmapowanymi w niższych rejonach RAM.

## 5.2 Podstawowe Jednostki i Struktury Danych

Kod źródłowy BSP realizowany jest w rygorystycznej konwencji `snake_case`. Architektura systemu plików opiera się na trzech fundamentalnych filarach: blokach danych, węzłach indeksowych oraz wpisach teczek.

### 1. Bloki 512 B
Podstawową jednostką alokacji i przechowywania danych w BSP jest blok o rozmiarze **512 bajtów**. Każdy plik oraz teczka zajmuje wielokrotność bloków 512-bajtowych, cozapewnia bezpośrednią 1:1 kompatybilność z sektorami LBA kontrolerów dyskowych AHCI.

### 2. Węzeł Indeksowy (`wezel_indeksowy`)
Wzorowany na uniksowych rozwiązaniach i-węzłów, `wezel_indeksowy` jest strukturą metadanych opisującą właściwości i fizyczne położenie pliku lub teczki. Sam węzeł nie przechowuje nazwy pliku.

Struktura węzła zawiera m.in.:
* Typ obiektu (plik regularny, teczka, urządzenie).
* Rozmiar danych w bajtach.
* Wskaźniki (indeksy) do bloków 512 B zawierających faktyczną treść.
* Metadane bezpieczeństwa i Poziomu Zaufania BZL / PZB.

### 3. Wpis Teczki (`teczka_wpis`)
Teczka w BSP jest specjalnym plikiem zawierającym tablicę wpisów strukturalnych. Zadaniem `teczka_wpis` jest powiązanie czytelnej dla użytkownika nazwy pliku/teczki z konkretnym numerem węzła indeksowego.

### 4. Pliki Wykonywalne (.bur) i Paczki Aplikacji (.cebula)
* **Pliki `.bur`:** Binarne pliki wykonywalne dla architektury x86_64 przeznaczone do uruchamiania w przestrzeni użytkownika (Ring 3).
* **Paczki `.cebula`:** Katalogi strukturalne zawierające plik wykonywalny `.bur` oraz manifest `opis.aplikacji`.

## 5.3 Hierarchiczna Struktura Teczek

BSP definiuje odgórnie ustrukturyzowane drzewo teczek, którego celem jest separacja krytycznych komponentów systemu od przestrzeni użytkownika. W technicznych ścieżkach systemowych kategorycznie pomija się polskie znaki diakrytyczne.

Początkowa struktura montowana na dysku AHCI przyjmuje postać:

```text
/ (Główna teczka - Root)
├── shell.bur              # Główna powłoka systemowa (Ring 3 Terminal)
├── menedzer_okien.bur     # Menedżer Okien i Pulpit (Ring 3 GUI)
├── jadro/                 # Pliki binarne jądra i najniższych modułów systemowych
├── system/                # Pliki konfigurowalne i zasoby globalne
├── sterowniki/            # Moduły obsługi urządzeń i niskopoziomowe definicje
├── uslugi/                # Usługi systemowe działające w tle
├── programy/              # Zainstalowane paczki aplikacji (.cebula)
│   ├── notatnik.cebula/   # Paczka Aplikacji Notatnik (opis.aplikacji + notatnik.bur)
│   └── kalkulator.cebula/ # Paczka Aplikacji Kalkulator (opis.aplikacji + kalkulator.bur)
├── ustawienia/            # Globalne i lokalne ustawienia środowiska użytkownika
├── logi/                  # Logi jądra, PCI (`/logi/pci.txt`) oraz BZL
├── uzytkownicy/           # Teczki domowe użytkowników
├── piaskownica/           # Wyizolowana przestrzeń przeznaczona dla procesów z BZL-5
└── tymczasowe/            # Pliki tymczasowe generowane w czasie pracy (tmp)

```

## 5.4 Mechanizm Parsowania Ścieżek

W celu odnalezienia pliku na dysku, jądro implementuje dedykowany komponent – parser ścieżek logicznych. Proces translacji ciągu znaków (np. `/programy/notatnik.cebula/notatnik.bur`) na fizyczne bloki dyskowe AHCI przebiega następująco:

1. **Tokenizacja:** Parser analizuje ciąg tekstowy, dzieląc go względem separatora `/`. Dla podanej ścieżki tokenami są kolejno `programy`, `notatnik.cebula` oraz `notatnik.bur`.
2. **Punkt Startowy:** Analiza zawsze rozpoczyna się od węzła indeksowego o numerze 0 (węzeł głównej teczki `/`).
3. **Iteracja:** Jądro wczytuje bloki danych powiązane z bieżącym węzłem teczki i interpretuje je jako tablicę struktur `teczka_wpis`.
4. **Dopasowanie:** Nazwa z tokenu jest porównywana z polem `nazwa_pliku`. Po znalezieniu dopasowania, parser pobiera `numer_wezla` podrzędnego i powtarza procedurę dla kolejnego tokenu.
5. **Wynik:** Jeśli proces zakończy się sukcesem, jądro uzyskuje bezpośredni dostęp do końcowego węzła indeksowego pliku, skąd odczytuje adresy 512-bajtowych bloków danych. W przypadku braku dopasowania na dowolnym etapie, zwracany jest błąd (np. brak pliku).

## 5.5 Integracja z Modelem Bezpieczeństwa BZL / PZB i Paczkami .cebula

Bursztynowy System Plików ściśle współpracuje z podsystemem BZL (Bursztynowy Poziom Zaufania) oraz Loaderem programów. Kontrola dostępu nie opiera się wyłącznie na tradycyjnych bitach rwx, ale jest aktywnie walidowana przez jądro podczas obsługi wywołań systemowych (BWS) i parsowania manifestów:

* **Ochrona Ścieżek Systemowych:** Modyfikacja `/jadro`, `/system`, `/sterowniki` wymaga bezwzględnie poziomu BZL-0. Zwykłe aplikacje użytkownika (BZL-4) lub programy w piaskownicy (BZL-5) otrzymają odmowę dostępu przy próbie modyfikacji tych obszarów.
* **Manifest Paczki (`opis.aplikacji`):** Każda aplikacja w katalogu `/programy/*.cebula/` posiada plik konfiguracyjny definiujący wymogi bezpieczeństwa (poziom zaufania, nazwa, autor oraz tablica przyznanych uprawnień, np. `okna`, `pliki_czytaj`, `pliki_zapisz`).
* **Separacja Piaskownicy:** Procesy działające na poziomie BZL-5 (Piaskownica) mają zablokowany dostęp do teczek użytkowników `/uzytkownicy`. Parser ścieżek ogranicza ich operacje I/O wyłącznie do dedykowanej teczki `/piaskownica/[nazwa_aplikacji]` oraz `/tymczasowe`.

