# 05. Bursztynowy System Plików (BSP)

Niniejszy dokument opisuje architekturę, struktury danych oraz mechanizmy operacyjne **Bursztynowego Systemu Plików (BSP)**. BSP został zaprojektowany jako autorski, hierarchiczny system plików dla Bursztyn OS, odrzucający standardy zewnętrzne (takie jak FAT, ext czy NTFS) na rzecz uproszczonej struktury indeksowej zintegrowanej bezpośrednio z systemem uprawnień BZL.

## 5.1 Warstwa Sprzętowo-Pamięciowa (RAM-dysk)

W obecnej fazie rozwoju (Etap 3 i wyższe), BSP funkcjonuje w wydzielonym obszarze pamięci RAM jako **RAM-dysk**. Pozwala to na pełną emulację operacji dyskowych I/O przed implementacją natywnych sterowników dla fizycznych kontrolerów pamięci masowej (SATA/NVMe).

* **Rozmiar RAM-dysku:** 2 Megabajty (2 MB).
* **Adres wirtualny jądra:** `0x40000000` (1 GB).
* **Alokacja:** Podczas inicjalizacji w `kernel.cpp`, **Zarządca Pamięci Wirtualnej** alokuje 512 ramek fizycznych (po 4 KB każda) i mapuje je ciągle pod podany adres wirtualny z flagami `FLAGA_OBECNA | FLAGA_ZAPIS` (`0b00000011`).

## 5.2 Podstawowe Jednostki i Struktury Danych

Kod źródłowy BSP realizowany jest w rygorystycznej konwencji `snake_case`. Architektura systemu plików opiera się na trzech fundamentalnych filarach: blokach danych, węzłach indeksowych oraz wpisach teczek.

### 1. Bloki 512 B

Podstawową jednostką alokacji i przechowywania danych w BSP jest blok o rozmiarze **512 bajtów**. Każdy plik oraz teczka zajmuje wielokrotność bloków 512-bajtowych. Przekłada się to na bezpośrednią kompatybilność z tradycyjną strukturą sektorów na dyskach twardych.

### 2. Węzeł Indeksowy (`wezel_indeksowy`)

Wzorowany na uniksowych rozwiązaniach i-węzłów, `wezel_indeksowy` jest strukturą metadanych opisującą właściwości i fizyczne położenie pliku lub teczki. Sam węzeł nie przechowuje nazwy pliku.

Struktura węzła zawiera m.in.:

* Typ obiektu (plik regularny, teczka, urządzenie).
* Rozmiar danych w bajtach.
* Wskaźniki (indeksy) do bloków 512 B zawierających faktyczną treść.
* *Rozszerzenie docelowe:* Metadane bezpieczeństwa (identyfikator właściciela, wymagany poziom BZL, bitowa mapa uprawnień dostępu, sumy kontrolne).

### 3. Wpis Teczki (`teczka_wpis`)

Teczka w BSP jest specjalnym plikiem zawierającym tablicę wpisów strukturalnych. Zadaniem `teczka_wpis` jest powiązanie czytelnej dla użytkownika nazwy pliku/teczki z konkretnym numerem węzła indeksowego.

### 5.3 Hierarchiczna Struktura Teczek

BSP definiuje odgórnie ustrukturyzowane drzewo teczek, którego celem jest separacja krytycznych komponentów systemu od przestrzeni użytkownika. W technicznych ścieżkach systemowych kategorycznie pomija się polskie znaki diakrytyczne.

Początkowa struktura montowana w RAM-dysku przyjmuje postać:

```cpp
/ (Główna teczka - Root)
├── jadro/         # Pliki binarne jądra i najniższych modułów systemowych
├── system/        # Pliki konfiguracyjne i zasoby globalne (np. /system/kernel.cfg)
├── sterowniki/    # Moduły obsługi urządzeń i niskopoziomowe definicje sprzętowe
├── uslugi/        # Usługi systemowe działające w tle (np. serwer GUI, stos sieciowy)
├── programy/      # Zainstalowane natywne aplikacje systemowe (.kur)
├── ustawienia/    # Globalne i lokalne ustawienia środowiska użytkownika
├── logi/          # Logi jądra, podsystemu bezpieczeństwa BZL oraz usług
├── uzytkownicy/   # Teczki domowe użytkowników (np. /uzytkownicy/art/dokumenty)
├── piaskownica/   # Wyizolowana przestrzeń przeznaczona dla procesów z poziomem BZL-5
└── tymczasowe/    # Pliki tymczasowe generowane w czasie pracy systemu (tmp)

```

### 5.4 Mechanizm Parsowania Ścieżek

W celu odnalezienia pliku na dysku, jądro implementuje dedykowany komponent – parser ścieżek logicznych. Proces translacji ciągu znaków (np. `/system/notatnik`) na fizyczne bloki pamięci przebiega następująco:

1. **Tokenizacja:** Parser analizuje ciąg tekstowy, dzieląc go względem separatora `/`. Dla ścieżki `/system/notatnik` tokenami są kolejno `system` oraz `notatnik`.
2. **Punkt Startowy:** Analiza zawsze rozpoczyna się od węzła indeksowego o numerze 0 (węzeł głównej teczki `/`).
3. **Iteracja:** Jądro wczytuje bloki danych powiązane z bieżącym węzłem teczki i interpretuje je jako tablicę struktur `teczka_wpis`.
4. **Dopasowanie:** Nazwa z tokenu (`system`) jest porównywana z polem `nazwa_pliku`. Po znalezieniu dopasowania, parser pobiera `numer_wezla` podrzędnego i powtarza procedurę dla kolejnego tokenu (`notatnik`).
5. **Wynik:** Jeśli proces zakończy się sukcesem, jądro uzyskuje bezpośredni dostęp do końcowego węzła indeksowego pliku, skąd odczytuje adresy 512-bajtowych bloków danych. W przypadku braku dopasowania na dowolnym etapie, zwracany jest błąd -404 (Plik lub teczka nie istnieje).

### 5.5 Integracja z Modelem Bezpieczeństwa BZL

Bursztynowy System Plików ściśle współpracuje z podsystemem BZL (Bursztynowy Poziom Zaufania) zarządzanym przez jądro. Kontrola dostępu nie opiera się na uprawnieniach zaszytych wyłącznie w pliku, ale jest aktywnie walidowana przez jądro podczas obsługi wywołań systemowych (BWS).

Zasady Ochrony Ścieżek:

* **Modyfikacja `/jadro`, `/system`, `/sterowniki`:** Wymaga bezwzględnie poziomu BZL-0 lub odpowiednich flag systemowych. Zwykłe aplikacje użytkownika (BZL-4) lub programy w piaskownicy (BZL-5) otrzymają od jądra odmowę dostępu (`return -2;`) przy próbie otwarcia pliku do zapisu w tych obszarach.
* **Separacja Piaskownicy:** Procesy działające na poziomie BZL-5 (Piaskownica) mają sprzętowo i programowo zablokowany dostęp do teczki `/uzytkownicy`. Ich uprawnienia wejścia/wyjścia są ograniczone przez parser ścieżek wyłącznie do dedykowanej teczki `/piaskownica/[nazwa_aplikacji]` oraz `/tymczasowe`.

### Przyszłościowe Metadane Bezpieczeństwa:

W kolejnej fazie rozwoju, struktura `wezel_indeksowy` zostanie rozbudowana o natywne przechowywanie:

* Jawnej sygnatury i sumy kontrolnej aplikacji.
* Wymaganego poziomu zaufania (manifestu) powiązanego z programem (np. zapisane informacje o uprawnieniach z pliku `opis.aplikacji` dla paczek `.cebula`).