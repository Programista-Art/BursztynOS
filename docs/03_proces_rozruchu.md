# 03. Proces Rozruchu (Boot Process) i Sekwencja Long Mode

Niniejszy dokument opisuje krytyczną, najniższą fazę uruchamiania systemu Bursztyn OS. Sekwencja rozruchowa odpowiada za odebranie sterowania od programu rozruchowego (bootloadera) GRUB, konfigurację wczesnego stronicowania, przełączenie architektury procesora w tryb 64-bitowy (Long Mode) oraz bezpieczne przekazanie sterowania do kodu Jądra napisanego w C++ (`kernel_main`).

## 3.1 Standard Multiboot2 (Specyfikacja Nagłówka)

Bursztyn OS wykorzystuje program rozruchowy zgodny ze specyfikacją Multiboot2 (np. GRUB). Aby Jądro zostało poprawnie rozpoznane jako prawidłowy obraz binarny, na samym początku sekcji `.multiboot` (wyrównanej do 8 bajtów) zdefiniowany jest strukturalny nagłówek.

Nagłówek składa się z następujących pól:

1. **Magic Number (`0xE85250D6`):** Wiodąca sygnatura identyfikująca standard Multiboot2.
2. **Architektura (`0`):** Informuje program rozruchowy, że wczesny kod rozruchowy musi zostać wykonany w 32-bitowym trybie chronionym (Protected Mode) architektury i386.
3. **Rozmiar nagłówka:** Dynamicznie wyliczana różnica adresów etykiet `header_end - header_start`.
4. **Suma kontrolna (Checksum):** Wartość, która po zsumowaniu z powyższymi trzema polami daje wynik `0` w arytmetyce 32-bitowej bez znaku.

### Żądanie Taga Memory Map

Wewnątrz nagłówka Jądro jawnie deklaruje żądanie dostarczenia dodatkowych informacji sprzętowych od programu rozruchowego za pomocą dedykowanego znacznika (Tag typu `1` - *Information request*). System prosi o przekazanie mapy pamięci fizycznej (Tag typu `6` - *Memory Map*), co jest kluczowe dla późniejszej inicjalizacji **Zarządcy Pamięci Fizycznej**.

## 3.2 Wczesne Stronicowanie (Identity Paging 1:1)

Przełączenie procesora x86-64 w tryb Long Mode wymaga bezwzględnie aktywnego stronicowania (Paging) już w momencie ustawiania flagi systemowej. Ponieważ procesor w tym ułamku sekundy nadal wykonuje instrukcje pod bieżącymi adresami fizycznymi, system konfiguruje tzw. **Stronicowanie Tożsamościowe** (*Identity Paging*) – mapowanie, w którym adres wirtualny jest dokładnie tożsamy z adresem fizycznym (`Wirtualny = Fizyczny`).

W sekcji `.bss` rezerwowana jest przestrzeń dla trzech początkowych tablic stron (każda po 4096 bajtów, wyrównana do granicy strony 4 KB):

* `pml4_table` (Page Map Level 4)
* `pdp_table` (Page Directory Pointer Table)
* `pd_table` (Page Directory Table)

### Architektura Mapowania Pierwszych 2 Megabajtów:

Proces konfiguracji wierszy tablic realizowany w kodzie asemblerowym `boot.S` przebiega następująco:

1. Pierwszy wpis (indeks 0) tablicy `pml4_table` zostaje skierowany na adres bazowy `pdp_table` z flagami `0b11` (**Present** – strona obecna, **Writable** – zezwolenie na zapis).
2. Pierwszy wpis (indeks 0) tablicy `pdp_table` zostaje skierowany na adres bazowy `pd_table` z flagami `0b11` (Present, Writable).
3. Pierwszy wpis (indeks 0) tablicy `pd_table` mapuje bezpośredni adres fizyczny `0x0`. Wprowadzana jest flaga `0b10000011`. Bit nr 7 (**Page Size - PS**) ustawiony na `1` informuje procesor, że jest to tzw. **Wielka Strona** (*Huge Page*) o rozmiarze **2 Megabajtów**. Dzięki temu system nie potrzebuje w tej fazie czwartej tablicy stron (Page Table - PT).

## 3.3 Procedura Przełączenia w Tryb Long Mode (64-bit)

Po przygotowaniu struktur pamięci wirtualnej w trybie 32-bitowym, procesor wykonuje sekwencyjny algorytm przejścia architektonicznego:

1. **Załadowanie CR3:** Adres fizyczny wiodącej tablicy `pml4_table` zostaje wpisany do rejestru sterującego `CR3`.
2. **Aktywacja PAE (Physical Address Extension):** Poprzez odczyt rejestru `CR4`, nałożenie maski bitowej `0b100000` (bit 5) i ponowny zapis, procesor przechodzi w tryb rozszerzonego adresowania fizycznego, niezbędnego dla Long Mode.
3. **Włączenie EFER.LME (Long Mode Enable):** Do rejestru `ECX` ładowany jest adres rejestru specyficznego dla modelu (MSR) o numerze `0xC0000080` (Extended Feature Enable Register). Instrukcja `rdmsr` pobiera jego stan, bit nr 8 (LME) zostaje ustawiony na `1`, a instrukcja `wrmsr` zapisuje zmianę do procesora.
4. **Włączenie Stronicowania (Paging):** Rejestr `CR0` zostaje zmodyfikowany poprzez ustawienie bitu nr 31 (**PG - Paging Enable**). Od tego momentu procesor sprzętowo tłumaczy każdy adres za pomocą tablic zmapowanych w `CR3`.
5. **Ładowanie GDT64 i Daleki Skok (Far Jump):** Za pomocą instrukcji `lgdt` ładowany jest tymczasowy wskaźnik do struktury Globalnej Tablicy Deskryptorów (`gdt64_pointer`). Następnie wykonywany jest daleki skok asemblerowy:

```assembly
ljmp $0x08, $long_mode_start

```

Wartość selektora `0x08` (indeks 1 w GDT) nadpisuje rejestr selektora kodu (`CS`), co ostatecznie wprowadza procesor w natywne wykonywanie 64-bitowych instrukcji AMD64/Intel64.

## 3.4 Inicjalizacja Środowiska 64-bit i System V ABI

Po znalezieniu się w etykiecie `.code64` (`long_mode_start`), system wykonuje operacje czyszczące środowisko wykonawcze:

* Rejestry segmentowe danych (`DS`, `ES`, `FS`, `GS`, `SS`) zostają załadowane selektorem danych Jądra `0x10` (indeks 2 w GDT).
* Wskaźnik stosu `RSP` zostaje skierowany na szczyt dedykowanego, bezpiecznego stosu wczesnego Jądra (`stack_top`), którego rozmiar wynosi 16 KB.

### Przekazywanie Parametrów Multiboot2 do C++

Zgodnie ze specyfikacją standardu wywołań System V AMD64 ABI (stosowanego m.in. w systemach Unix/Linux x86-64), dwa pierwsze argumenty dla wywoływanej funkcji klastra C/C++ muszą zostać przekazane odpowiednio przez rejestry `RDI` oraz `RSI`.

Przed przełączeniem trybów, program rozruchowy dostarcza do Jądra dwie kluczowe wartości w rejestrach 32-bitowych:

1. `EAX` – Multiboot2 Magic Number (`0x36D76289`)
2. `EBX` – Wskaźnik fizyczny do struktury informacyjnej Multiboot (zawierającej m.in. wspomnianą mapę pamięci).

Ponieważ w trakcie przełączania rejestry te mogłyby ulec zatarciu lub modyfikacji, na samym początku funkcji `_start` ich wartości są bezpiecznie kopiowane:

```assembly
movl %eax, %edi  /* Kopia Magic Number do EDI */
movl %ebx, %esi  /* Kopia adresu struktury do ESI */

```

W trybie 64-bitowym rejestry te automatycznie stają się młodszymi częściami rejestrów `RDI` oraz `RSI`. Dzięki temu, w momencie wykonania instrukcji:

```assembly
call kernel_main

```

Funkcja wejściowa w pliku `kernel.cpp` odbiera te parametry w sposób w pełni zgodny z sygnaturą języka C++:

```cpp
extern "C" void kernel_main(uint64_t multiboot_magic, uint64_t multiboot_info_ptr);

```