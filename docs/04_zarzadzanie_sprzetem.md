# 04. Zarządzanie Sprzętem i Inicjalizacja Podsystemów Procesora

Niniejszy dokument opisuje niskopoziomowe mechanizmy zarządzania sprzętem zaimplementowane w Jądrze Bursztyn OS. Sekcja ta obejmuje konfigurację deskryptorów segmentów (wliczając izolację Przestrzeni Użytkownika Ring 3), zaawansowaną siatkę przerwań z wizualizacją wyjątków, przejęcie kontroli nad systemem za pomocą kontrolera APIC oraz asynchroniczne zarządzanie wejściem (Klawiatura i Mysz PS/2) na potrzeby interfejsu graficznego.

## 4.1 Globalna Tablica Deskryptorów (GDT64) i TSS

W trybie Long Mode (64-bit) stronicowanie VMM gra główną rolę w zarządzaniu pamięcią, jednak struktura GDT pozostaje krytyczna dla ustalenia sprzętowych poziomów zaufania (Privilege Rings) oraz poprawnego przełączania kontekstu w wywołaniach `syscall`.

Bursztyn OS implementuje klasyczną płaską strukturę segmentacji, ale została ona rozbudowana o **TSS (Task State Segment)**:

| Indeks | Selektor | Typ Segmentu | Ring | Bajt Dostępu | Flagi (Wyższe) | Przeznaczenie |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 0 | `0x00` | Null Descriptor | - | 0x00 | 0x00 | Wymagany sprzętowo deskryptor zerowy |
| 1 | `0x08` | Kod Jądra | 0 | 0x9A | 0x20 (Bit L) | Wykonywalny kod Jądra |
| 2 | `0x10` | Dane Jądra | 0 | 0x92 | 0x00 | Odczyt/Zapis (Stosy Jądra) |
| 3 | `0x1B` | Kod Użytkownika | 3 | 0xFA | 0x20 (Bit L) | Kod aplikacji (.bur) |
| 4 | `0x23` | Dane Użytkownika | 3 | 0xF2 | 0x00 | Dane aplikacji w Ring 3 |
| 5 | `0x28` | **Segment TSS** | 0 | 0x89 | 0x00 | Segment specjalny o szerokości 16 bajtów |

### Task State Segment (TSS)
Pod adresem `0x28` Jądro alokuje 16-bajtowy segment systemowy TSS. Jest to mechanizm sprzętowy wymuszony przez procesory x86_64 do poprawnego obsługiwania aplikacji w Ring 3. Kiedy aplikacja z przestrzeni użytkownika wykonuje komendę `syscall` lub wywoła sprzętowy wyjątek (np. Page Fault), procesor musi natychmiast zaprzestać używania potencjalnie uszkodzonego stosu aplikacji. Odczytuje on z TSS adres wskaźnika czystego, zabezpieczonego stosu Jądra (Ring 0) (Rejestr `RSP0`) i powraca do stabilnej pracy. Jądro ładuje ten rejestr podczas startu instrukcją `ltr 0x28`.

## 4.2 Tablica Deskryptorów Przerwań (IDT) i BSOD

IDT składa się z 256 bramek o szerokości 16 bajtów każda, zawierających 64-bitowe wskaźniki na procedury obsługi przerwań (ISR) i żądania przerwań (IRQ) ubrane we flagę `0x8E`.

Wszystkie procedury asemblerowe zrzucają pełny stan rejestrów (Push) do struktury i wywołują zunifikowany parser w kodzie C++:

### System Wyjątków i Wizualizacja
Wektory 0–31 stanowią wyjątki procesora. Jądro przechwytuje zdarzenie (np. Wektor 6 - Invalid Opcode lub Wektor 14 - Page Fault) i przekazuje pełen zrzut rejestrów do podsystemu graficznego (LFB). Procesor generuje okienkowy, pomarańczowy **Błąd Krytyczny Systemu (BSOD)** na ekranie wyrysowanym przez Bursztynowy HAL (Bochs/VESA/UEFI), wskazując programiście dokładny kod oraz Adres Załamania (RIP), na którym wylądował, po czym wprowadza jednostkę w tryb `hlt`.

## 4.3 Demontaż starych PIC i Konfiguracja APIC

W celu zagwarantowania nowoczesnej komunikacji sprzętowej bez zjawiska "wąskich gardeł", Bursztyn OS bezwzględnie usypia stare programowalne układy PIC, maskując je za pomocą wartości `0xFF` wysyłanych na porty `0x21` i `0xA1`. W ich miejsce zostaje podniesiony nowożytny kontroler `APIC`.

* **Aktywacja:** Jądro zczytuje fizyczny adres APIC (często `0xFEE00000`) ze sprzętowego rejestru specjalnego MSR (`IA32_APIC_BASE`). Rejestr ten mapowany jest bezpośrednio w układzie VMM z pominięciem optymalizacji Cache (`Cache Disable`).
* Bit nr 11 na szynie jest ustalany na `1`, po czym kontroler aktywowany jest programowo za pomocą maski `0x100` (APIC Software Enable).

## 4.4 Zegar APIC oraz Czas Rzeczywisty (RTC CMOS)

Architektura czasu w Bursztynie została podzielona na dwa autonomiczne współpracujące rejestry.

1. **APIC Timer (Wektor 32):** Bardzo precyzyjny zegar cykliczny operujący bezpośrednio na szynie wewnętrznej APIC. Jego inicjalizacja polega na podziale zegara szyny przez 16 (dzielnik 0x3 w rejestrze `0x3E0`) oraz uruchomieniu pętli licznika dla wektora `32`. Po odebraniu przerwania, system wpisuje wartość `0` do rejestru EOI (`0x0B0`).
2. **Real-Time Clock (RTC CMOS):** Prawdziwy zegar płyty głównej (Porty I/O `0x70` i `0x71`), wykorzystywany przez podsystem GUI do odświeżania aktualnej godziny użytkownika na Pasku Zadań w lewym dolnym rogu. System odczytuje wartości BCD dla sekund, minut i godzin z dedykowanych rejestrów z uwzględnieniem bitu NMI (Non-Maskable Interrupt).

## 4.5 Wejście Asynchroniczne: Klawiatura i Mysz PS/2 (Wektory 33 i 44)

Do obsługi zorientowanego obiektowo środowiska GUI, Bursztyn OS wdraża nasłuchujące w tle wektory sprzętowe dla kontrolera `i8042`.

### Klawiatura (Wektor 33 - IRQ 1)
Przerwanie uruchamia czytanie kodu klawisza (Scancode) z portu `0x60`. Bit nr 7 decyduje, czy mamy do czynienia ze zwolnieniem, czy wciśnięciem klawisza. Dane przefiltrowane przez mapę ASCII przekazywane są jako sygnał do aplikacji GUI uruchomionych w Ring 3.

### Mysz PS/2 (Wektor 44 - IRQ 12)
Podsystem myszy przeszedł zawiłą inicjalizację portu, która:
1. Odblokowuje pomocniczy interfejs myszy w głównym kontrolerze PS/2 (komenda `0xA8`).
2. Wymusza sprzętową flagę wysyłania przerwań myszy pod IRQ 12 (Bit 1 w bajcie statusu).
3. Włącza tryb swobodnego strumieniowania danych z myszy (komenda `0xF4` do myszy).

Procedura obsługi `44` kumuluje otrzymywane bajty w tzw. "Trójbajtowe pakiety PS/2". Kiedy pakiet jest kompletny, system dekoduje flagi ruchu (Sign Bits) i generuje wektory relatywne (Delta X, Delta Y) oraz status przycisków myszy. BWS natychmiast synchronizuje je ze statusem Menedżera Okien, co umożliwia natywny rendering kursora, system kliknięć (Z-Order Focus) oraz asynchroniczne przeciąganie okien użytkownika (Drag & Drop) bez przerw w pracy jądra.