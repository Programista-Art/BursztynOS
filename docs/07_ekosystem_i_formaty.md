# 07. Ekosystem Aplikacji i Formaty Plików

Niniejszy dokument opisuje strukturę logiczną i binarną komponentów wchodzących w skład wyższej warstwy **Bursztyn OS**, stanowiącej Przestrzeń Użytkownika (Ring 3). System kategorycznie odrzuca tradycyjne, obce standardy plików wykonywalnych (takie jak ELF w systemach Unix/Linux czy PE/COFF w systemach Windows) na rzecz autorskich formatów dostosowanych do tożsamości systemu oraz natywnej integracji z językiem **Avocado**.

## 7.1 Filozofia Natywnego Ekosystemu

Głównym celem ekosystemu Bursztyn OS jest dostarczenie programistom zunifikowanego środowiska, w którym dystrybucja, instalacja i uruchamianie aplikacji są ściśle powiązane z warstwą bezpieczeństwa **BZL (Bursztynowy Poziom Zaufania)**. System operuje na dwóch dedykowanych formatach plików:

1. **`.bur` (Bursztynowy Program Wykonywalny):** Plik binarny zawierający bezpośrednie instrukcje maszynowe przeznaczone do załadowania do pamięci wirtualnej procesu.
2. **`.cebula` (Paczka Aplikacji):** Struktura kontenerowa (archiwum), która wzorem warstw cebuli zamyka w sobie program wykonywalny, manifest uprawnień, ikony oraz zasoby statyczne aplikacji.

## 7.2 Format `.bur` (Bursztynowy Program Wykonywalny)

Format `.bur` stanowi uproszczony, lecz wysoce zoptymalizowany format binarny przeznaczony dla Long Mode w architekturze x86-64. Moduł jądra odpowiedzialny za ładowanie programów (`loader.bur`) przetwarza ten plik w celu zainicjalizowania nowej przestrzeni adresowej procesu.

### Struktura Nagłówka Pliku `.bur` (Specyfikacja Binarna):

Każdy prawidłowy plik `.bur` rozpoczyna się od niskopoziomowego nagłówka strukturalnego:

* **Sygnatura Magiczna (4 bajty):** Stała wartość identyfikująca format (np. `0x42 0x55 0x52 0x00` odpowiadająca ciągowi ASCII `BUR\0`).
* **Punkt Wejścia (`Entry Point` - 8 bajtów):** Adres wirtualny w przestrzeni Ring 3, pod który procesor ma wykonać skok po zakończeniu mapowania pamięci przez jądro.
* **Sekcja Kodu (`.tekst`):** Przesunięcie i rozmiar bloku instrukcji maszynowych przeznaczonych do wykonania (z flagami ochrony **Zarządcy Pamięci Wirtualnej**: *Present, Read, Execute*).
* **Sekcja Danych (`.dane`):** Przesunięcie i rozmiar bloku danych zainicjalizowanych aplikacji (z flagami ochrony **Zarządcy Pamięci Wirtualnej**: *Present, Read, Write*).

Podczas wywołania systemowego `bws_uruchom_program`, **jądro** analizuje nagłówek, alokuje ramki fizyczne przez **Zarządcę Pamięci Fizycznej**, mapuje je u **Zarządcy Pamięci Wirtualnej**, kopiuje sekcje z RAM-dysku (BSP) do nowo utworzonej przestrzeni adresowej, a następnie za pomocą instrukcji `SYSRET` przenosi egzekucję do Ring 3 na wskazany Punkt Wejścia.

## 7.3 Format `.cebula` (Paczka Aplikacji)

W przypadku aplikacji bardziej złożonych (np. posiadających interfejs graficzny GUI, własne pliki konfiguracyjne lub ikony), standardem dystrybucyjnym w Bursztyn OS jest paczka o rozszerzeniu `.cebula`. Nazwa ta w spójny sposób odzwierciedla wielowarstwową strukturę kontenera.

### Anatomia Paczki (Przykładowa struktura `notatnik.cebula`):

Po zamontowaniu paczki w systemie plików BSP, jądro i **zarządca paczek** interpretują ją jako odizolowaną **podteczkę** o zdefiniowanej strukturze:

```text
notatnik.cebula/
├── opis.aplikacji      # Krytyczny manifest tekstowy (metadane i uprawnienia)
├── notatnik.bur        # Główny plik wykonywalny binarny aplikacji
├── ikona.bmp           # Zasób graficzny (np. ikona wyświetlana w GUI Składacza Obrazu)
├── uprawnienia/        # Opcjonalne dodatkowe klucze weryfikacji kryptograficznej
└── zasoby/             # Teczka zawierająca czcionki, lokalizacje językowe i grafiki


```

**Zarządca Okien** skanuje domyślne **teczki** aplikacji (np. `/programy`), odczytuje zawartość kontenerów `.cebula`, renderuje ikony na pulpicie, a w momencie kliknięcia wywołuje wewnętrzny loader jądra wskazując na **binarium** zaszyte wewnątrz paczki.

## 7.4 Specyfikacja Manifestu `opis.aplikacji`

Plik `opis.aplikacji` jest tekstowym manifestem strukturalnym, kodowanym w formacie czystego ASCII. Stanowi on podstawę dla podsystemu **BZL** do podjęcia decyzji o restrykcjach nakładanych na uruchamiany proces. Jądro kategorycznie zabrania edycji lub modyfikacji tego pliku przez procesy o poziomie zaufania wyższym niż BZL-2.

### Oficjalna Struktura i Składnia Pliku Manifestu:

```ini
nazwa = "Notatnik"
autor = "Programista Art"
wersja = "0.1"
poziom_zaufania = 4
plik_startowy = "notatnik.bur"
uprawnienia = [
    "okna",
    "pliki_czytaj",
    "pliki_zapisz"
]


```

### Mapowanie Pól Manifestu na Struktury Jądra:

Podczas ładowania paczki `.cebula`, parser jądra dokonuje translacji zapisów tekstowych na typy binarne wpisywane bezpośrednio do struktury `proces_t`:

1. `poziom_zaufania = 4` -> Wartość ta mapuje pole `poziom_zaufania` w **jądrze** na poziom **BZL-4 (Aplikacje użytkownika)**.
2. `uprawnienia` -> Tablica tekstowa jest procesowana przez kompilator flag jądra. Każdy ciąg znaków odpowiada konkretnej masce bitowej praw BZL:

* `"pliki_czytaj"` -> Aktywuje bit `PRAWO_PLIKI_CZYTAJ` (`1 << 0`).
* `"pliki_zapisz"` -> Aktywuje bit `PRAWO_PLIKI_ZAPISZ` (`1 << 1`).
* `"okna"` -> Aktywuje bit `PRAWO_GUI` (`1 << 3`).

W rezultacie, mapa bitowa `uprawnienia` w strukturze procesu dla powyższego manifestu przyjmie końcową wartość binarną `0b00001011` (wartość dziesiętna `11`).

## 7.5 Natywna Integracja z Językiem Avocado

Największym, długoterminowym wyróżnikiem technologicznym i architektonicznym Bursztyn OS jest ścisła, natywna integracja z autorskim językiem programowania **Avocado**. System operacyjny nie jest traktowany wyłącznie jako warstwa sprzętowa, ale jako integralna część całego ekosystemu Avocado.

### Przepływ Kompilacji i Wdrożenia Aplikacji (Pipeline):

```text
+--------------------------+
|  Kod Źródłowy Avocado   | -> Deweloper implementuje logikę programu w Avocado
+--------------------------+
             |
             v
+--------------------------+
|    Kompilator Avocado    | -> Kompiluje składnię Avocado bezpośrednio do assemblera x86-64,
+--------------------------+    generując czysty kod binarny dla formatu .bur
             |
             +----------------+
             |                |
             v                v
      +--------------+ +--------------+
      |  program.bur | |opis.aplikacji| -> Kompilator automatycznie generuje plik manifestu
      +--------------+ +--------------+    na podstawie sekcji deklaratywnej w Avocado
             |                |
             +--------+-------+
                      |
                      v
+-------------------------------------+
| Paczkowanie: aplikacja.cebula       | -> Narzędzia deweloperskie zamykają strukturę w kontener
+-------------------------------------+
                      |
                      v  [Dystrybucja i instalacja w strukturze BSP]
+-------------------------------------+
|   JĄDRO BURSZTYNA - ŁADOWANIE       | -> Loader jądra (Ring 0) odczytuje manifest opis.aplikacji,
+-------------------------------------+    weryfikuje sumy kontrolne i nadaje poziom BZL-4
                      |
                      v  [SYSRET]
+-------------------------------------+
|  URUCHOMIENIE W RING 3 (APLIKACJA)  | -> Program wykonuje kod natywny w Ring 3, komunikując się
+-------------------------------------+    z systemem wyłącznie za pomocą instrukcji SYSCALL (BWS)


```

### Składnia Deklaratywna Uprawnień w Języku Avocado:

Język Avocado wspiera natywne, blokowe deklarowanie wymagań bezpieczeństwa bezpośrednio w kodzie źródłowym programu. Kompilator Avocado analizuje ten blok i automatycznie generuje prawidłowy plik `opis.aplikacji` wewnątrz struktury `.cebula`:

```avocado
aplikacja "Notatnik" {
    autor: "Programista Art",
    wersja: "0.1",
    poziom_zaufania: 4,
    uprawnienia: ["gui", "pliki_czytaj", "pliki_zapisz"]
}

funkcja glowna() {
    // Implementacja kodu aplikacji Avocado komunikującej się przez BWS...
    bws_pisz_tekst("Uruchomiono Notatnik Avocado w Ring 3!");
}


```

Dzięki tak zaprojektowanemu potokowi (Pipeline), ekosystem Bursztyn OS eliminuje ryzyko powstawania luk bezpieczeństwa wynikających z błędnej konfiguracji manifestów – bezpieczeństwo aplikacji jest definiowane na poziomie kodu źródłowego i rygorystycznie egzekwowane przez Jądro Bursztyna w Ring 0.