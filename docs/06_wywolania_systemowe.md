# 06. Bursztynowe Wywołania Systemowe (BWS)

Niniejszy dokument definiuje specyfikację warstwy **BWS (Bursztynowe Wywołania Systemowe)**, która zastępuje klasyczne, anglosaskie określenia typu *Syscall Interface* lub *Application Binary Interface (ABI)*. BWS stanowi jedyny, bezpieczny i kontrolowany most komunikacyjny, przez który programy użytkownika (`.bur`) działające w Ring 3 mogą żądać od Jądra Bursztyna (Ring 0) wykonania operacji uprzywilejowanych.

## 6.1 Mechanizm Sprzętowy `SYSCALL` i Przejście Ring 3 -> Ring 0

W architekturze x86-64 tradycyjne przerwania programowe (np. `int 0x80` znane z systemów 32-bitowych) zostały zastąpione dedykowanymi, ultraszybkimi instrukcjami sprzętowymi: `SYSCALL` (wywołanie) oraz `SYSRET` (powrót).

Proces przejścia ze strefy użytkownika do jądra w Bursztyn OS przebiega następująco:

1. **Inicjalizacja rejestrów MSR:** Podczas rozruchu jądro konfiguruje rejestry specyficzne dla modelu (MSR). Do rejestru `IA32_LSTAR` wpisywany jest adres wskaźnika niskopoziomowej funkcji obsługi przerwań systemowych (`bws_obsluga`). Do rejestru `IA32_STAR` ładowane są selektory segmentów kodu i danych dla Ring 0 oraz Ring 3 (zdefiniowane w GDT).
2. **Wywołanie instrukcji:** Program użytkownika umieszcza kod identyfikacyjny wywołania oraz wymagane parametry w rejestrach procesora, a następnie wykonuje instrukcję `SYSCALL`.
3. **Działanie procesora:** Procesor natychmiastowo przełącza segment kodu `CS` na Ring 0, zapisuje adres powrotny w rejestrze `RCX`, stan flag procesora w `R11` i wykonuje skok pod adres zapisany w `IA32_LSTAR` (`bws_obsluga`).
4. **Obsługa w jądrze:** Jądro zabezpiecza rejestry aplikacyjne na stosie systemowym, przeprowadza weryfikację zabezpieczeń BZL i wykonuje żądaną operację.

## 6.2 Konwencja Bursztyn OS BWS (Rejestry R8–R13)

Bursztyn OS wdraża unikalny standard przekazywania parametrów, odmienny od standardów POSIX czy Windows. W celu zachowania czytelności kodu i optymalizacji bare-metal, jądro wykorzystuje do komunikacji wyłącznie rejestry ogólnego przeznaczenia z rodziny **R8-R13**.

### Rozkład Rejestrów w Standardzie BWS:

* **`RAX`** – Numer identyfikacyjny wywołania systemowego (Kod BWS).
* **`R8`** – Pierwszy parametr wywołania (np. wskaźnik do ciągu znaków, uchwyt okna).
* **`R9`** – Drugi parametr wywołania (np. długość bufora, współrzędna X okna).
* **`R10`** – Trzeci parametr wywołania (np. flagi otwarcia pliku, współrzędna Y okna).
* **`R11`** – Czwarty parametr wywołania (*Zarezerwowany sprzętowo przez CPU dla flag RFLAGS, jądro używa go wyłącznie wewnętrznie*).
* **`R12`** – Piąty parametr wywołania.
* **`R13`** – Szósty parametr wywołania.

Po zakończeniu operacji, jądro zwraca wynik (kod błędu lub status sukcesu) zawsze w rejestrze **`RAX`**. Wartość ujemna (np. `-1`) oznacza błąd wykonania wywołania systemowego.

## 6.3 Oficjalna Rejestracja Wywołań BWS

Poniższa tabela stanowi kompletną mapę zarejestrowanych wywołań systemowych w Bursztyn OS. Każde wywołanie posiada swój unikalny identyfikator numeryczny (`Kod`), nazwę techniczną oraz zdefiniowane wymagania systemowe.

| Kod BWS | Nazwa Techniczna | Parametry wejściowe (R8-R13) | Zastosowanie / Opis | Wymagany Poziom / Flaga BZL |
| --- | --- | --- | --- | --- |
| **`BWS-001`** | `bws_pisz_tekst` | `R8`: adres tekstu (char*) | Wyświetlenie tekstu w powłoce systemowej lub konsoli. | `PRAWO_GUI` lub BZL-3+ |
| **`BWS-002`** | `bws_czytaj_klawisz` | Brak | Pobranie kodu naciśniętego klawisza ze sprzętowego bufora. | Wszystkie |
| **`BWS-003`** | `bws_otworz_plik` | `R8`: ścieżka pliku (char*) | Otwarcie uchwytu pliku w strukturach BSP. | `PRAWO_PLIKI_CZYTAJ` |
| **`BWS-004`** | `bws_zapisz_plik` | `R8`: ścieżka, `R9`: adres danych | Zapis bloku danych do pliku w systemie BSP. | `PRAWO_PLIKI_ZAPISZ` |
| **`BWS-005`** | `bws_zakoncz_program` | `R8`: kod wyjścia (int) | Bezpieczne zamknięcie procesu i zwolnienie jego pamięci. | Wszystkie |
| **`BWS-006`** | `bws_utworz_okno` | `R8`: tytuł, `R9`: X, `R10`: Y | Żądanie utworzenia okna graficznego w Składaczu Obrazu. | `PRAWO_GUI` |
| **`BWS-007`** | `bws_rysuj_tekst` | `R8`: id_okna, `R9`: tekst, `R10`: X | Renderowanie czcionki bitmapowej wewnątrz okna GUI. | `PRAWO_GUI` |
| **`BWS-008`** | `bws_pobierz_mysz` | `R8`: adres struktury myszy | Odczyt aktualnej pozycji i stanu przycisków myszy PS/2. | `PRAWO_GUI` |
| **`BWS-009`** | `bws_uruchom_program` | `R8`: ścieżka .bur (char*) | Załadowanie do pamięci i start nowego procesu w Ring 3. | `PRAWO_URUCHOM_PROGRAM` |

## 6.4 Kontrola Uprawnień BZL w Obsłudze BWS

Jądro Bursztyna kategorycznie odrzuca zasadę ślepego ufania aplikacjom przestrzeni użytkownika. Każde wywołanie trafiające do funkcji `bws_obsluga` jest poddawane dwufazowej weryfikacji logicznej w oparciu o strukturę wywołującego procesu (`proces_t`):

1. **Weryfikacja Flag Uprawnień:**
Przed wykonaniem kodu specyficznego dla danego wywołania, jądro wykonuje operację bitową AND na mapie bitowej praw procesu.

```cpp
// Przykład dla BWS-004 (Zapis pliku)
if (!(aktualny_proces->uprawnienia & PRAWO_PLIKI_ZAPISZ)) {
    return -1; // RAX = -1 -> Brak uprawnień do zapisu plików
}

```

2. **Weryfikacja Poziomu Zaufania BZL:**
Jeśli aplikacja posiada flagę ogólną, jądro sprawdza, czy jej poziom zaufania pozwala na modyfikację wybranego zasobu w relacji ze strukturą BSP.

```cpp
// Blokada zapisu do teczek systemowych dla zwykłych aplikacji (BZL-4) i piaskownicy (BZL-5)
if (aktualny_proces->poziom_zaufania >= 4 && sciezka_zaczyna_sie_od(sciezka, "/system")) {
    return -2; // RAX = -2 -> Odmowa dostępu: aplikacja użytkownika nie może pisać do teczki /system
}

```

Dzięki temu system BWS gwarantuje absolutną szczelność systemu – stabilność i bezpieczeństwo struktur pamięci oraz systemu plików BSP zależą wyłącznie od decyzji podejmowanych w Ring 0 przez Jądro Bursztyna.