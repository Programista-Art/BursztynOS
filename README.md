
# 05. Bursztynowy System Plików (BSP64)

Niniejszy dokument opisuje architekturę, struktury danych oraz mechanizmy operacyjne **Bursztynowego Systemu Plików (BSP64)**. Jest to autorski, hierarchiczny system plików zaprojektowany specjalnie dla systemu operacyjnego **Bursztyn OS**.

BSP64 został zaprojektowany z wykorzystaniem nowoczesnej architektury blokowej opartej na blokach **4096 bajtów (4 KB)**. Rozwiązanie to zastąpiło wcześniejszą koncepcję bloków 512-bajtowych i lepiej odpowiada architekturze współczesnych systemów operacyjnych oraz urządzeń pamięci masowej.

System plików jest zintegrowany bezpośrednio z jądrem Bursztyna oraz systemem wywołań systemowych BWS (Bursztynowe Wywołania Systemowe).

---

## 5.1 Aktualny status implementacji

Bursztynowy System Plików jest obecnie działającym elementem systemu operacyjnego.

Aktualnie zaimplementowane i działające funkcje obejmują:

* tworzenie plików,
* odczyt danych z plików,
* zapis danych do plików,
* tworzenie katalogów,
* obsługę hierarchicznej struktury katalogów,
* listowanie zawartości dysku,
* wyszukiwanie elementów w systemie plików,
* obsługę ścieżek plików i katalogów,
* komunikację z przestrzenią użytkownika poprzez BWS.

BSP64 jest wykorzystywany przez powłokę systemową działającą w **Ring 3**, która komunikuje się z jądrem za pomocą sprzętowej instrukcji `syscall`.

---

## 5.2 Architektura blokowa BSP64

Podstawową jednostką przechowywania danych w BSP64 jest blok o rozmiarze:

```text
4096 bajtów (4 KB)
````

Wprowadzenie bloków 4 KB stanowi podstawę architektury BSP64.

### Główne zalety bloków 4 KB:

* lepsze dopasowanie do rozmiaru stron pamięci w architekturze x86_64,
* mniejszy narzut zarządzania dużymi strukturami danych,
* lepsza współpraca z systemem pamięci wirtualnej,
* naturalna integracja z mechanizmem VMM,
* zgodność z typowymi rozmiarami bloków stosowanymi przez nowoczesne systemy plików,
* możliwość dalszej rozbudowy systemu plików bez konieczności zmiany podstawowej architektury.

BSP64 nie jest już projektowany jako system oparty na archaicznych blokach 512-bajtowych. Wprowadzenie bloków 4 KB było jednym z kluczowych kroków rozwoju systemu plików.

---

## 5.3 Podstawowe elementy systemu plików

Architektura BSP64 opiera się na kilku podstawowych elementach:

1. blokach danych,
2. węzłach indeksowych,
3. wpisach katalogowych,
4. strukturze hierarchicznej katalogów,
5. mechanizmie obsługi ścieżek.

### 1. Bloki danych

Blok danych ma rozmiar:

```text
4096 bajtów
```

Dane plików są przechowywane w jednym lub wielu blokach. Rozmiar pliku może być większy niż pojedynczy blok, dlatego system plików musi obsługiwać przypisywanie wielu bloków do jednego pliku.

---

### 2. Węzeł indeksowy (`wezel_indeksowy`)

Węzeł indeksowy przechowuje metadane dotyczące pliku lub katalogu.

W zależności od aktualnej implementacji może zawierać informacje takie jak:

* typ obiektu,
* rozmiar pliku,
* liczba zajmowanych bloków,
* informacje o lokalizacji danych,
* informacje wymagane do obsługi katalogów i plików.

Węzeł indeksowy nie musi przechowywać nazwy pliku. Nazwa jest przechowywana w strukturze wpisu katalogowego, która wskazuje odpowiedni węzeł indeksowy.

Takie rozdzielenie nazwy oraz metadanych pozwala na stworzenie hierarchicznej struktury systemu plików.

---

### 3. Wpis katalogowy (`teczka_wpis`)

Katalog w BSP64 zawiera wpisy opisujące znajdujące się w nim pliki i podkatalogi.

Wpis katalogowy łączy:

```text
nazwa pliku lub katalogu
        ↓
numer odpowiedniego węzła indeksowego
```

Dzięki temu system może przejść od czytelnej dla użytkownika ścieżki tekstowej do właściwych danych przechowywanych przez system plików.

---

## 5.4 Hierarchiczna struktura katalogów

BSP64 obsługuje hierarchiczną strukturę katalogów.

Przykładowa struktura systemu plików może wyglądać następująco:

```text
/                    # Katalog główny (Root)
├── jadro/           # Komponenty i pliki związane z jądrem
├── system/          # Globalne pliki systemowe i konfiguracja
├── sterowniki/      # Sterowniki urządzeń
├── uslugi/          # Usługi systemowe
├── programy/        # Programy i aplikacje systemowe
├── ustawienia/      # Ustawienia systemu i użytkownika
├── logi/            # Logi systemowe
├── uzytkownicy/     # Dane użytkowników
├── piaskownica/     # Izolowana przestrzeń aplikacji
└── tymczasowe/      # Pliki tymczasowe
```

Struktura ta może być rozwijana wraz z rozwojem systemu operacyjnego.

---

## 5.5 Mechanizm obsługi ścieżek

Jądro Bursztyn OS posiada mechanizm interpretacji ścieżek systemu plików.

Przykładowa ścieżka:

```text
/system/konfiguracja.cfg
```

jest analizowana etapami.

### 1. Tokenizacja

Ścieżka jest dzielona na poszczególne elementy:

```text
system
konfiguracja.cfg
```

---

### 2. Rozpoczęcie od katalogu głównego

Analiza rozpoczyna się od katalogu głównego:

```text
/
```

---

### 3. Wyszukiwanie wpisu

System przeszukuje wpisy katalogu głównego i szuka elementu:

```text
system
```

Po znalezieniu odpowiedniego wpisu system uzyskuje dostęp do katalogu `/system`.

---

### 4. Przejście do kolejnego elementu

Następnie wyszukiwany jest:

```text
konfiguracja.cfg
```

---

### 5. Uzyskanie dostępu do danych

Po znalezieniu odpowiedniego elementu system plików uzyskuje dostęp do właściwego węzła indeksowego oraz bloków zawierających dane pliku.

Jeżeli element nie zostanie znaleziony, system zwraca odpowiedni kod błędu.

---

## 5.6 Integracja z jądrem Bursztyn OS

BSP64 jest bezpośrednio zintegrowany z jądrem systemu operacyjnego.

Operacje na plikach nie są wykonywane bezpośrednio przez programy użytkownika. Aplikacje działające w przestrzeni użytkownika komunikują się z jądrem za pomocą systemu:

# BWS — Bursztynowe Wywołania Systemowe

Powłoka systemowa działająca w Ring 3 wykorzystuje sprzętową instrukcję:

```asm
syscall
```

do przechodzenia z przestrzeni użytkownika do jądra.

Przykładowy przepływ operacji wygląda następująco:

```text
┌─────────────────────────────┐
│      Program użytkownika    │
│           Ring 3             │
└──────────────┬──────────────┘
               │
               │ syscall
               ▼
┌─────────────────────────────┐
│        BWS / Jądro           │
│           Ring 0              │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│          BSP64               │
│   Obsługa plików i katalogów │
└─────────────────────────────┘
```

Aktualnie system BWS obsługuje między innymi:

* pisanie na ekranie,
* odczyt klawiatury,
* tworzenie plików,
* odczyt plików,
* zapis plików,
* listowanie zawartości dysku.

---

## 5.7 System plików a przestrzeń użytkownika

BSP64 jest wykorzystywany przez programy działające w **Ring 3**.

Oznacza to, że aplikacja użytkownika nie posiada bezpośredniego dostępu do pamięci jądra ani do wewnętrznych struktur systemu plików.

Schemat działania:

```text
Program w Ring 3
       │
       ▼
Wywołanie BWS
       │
       ▼
Jądro w Ring 0
       │
       ▼
BSP64
       │
       ▼
Operacja na pliku lub katalogu
```

Taki model zapewnia podstawową separację pomiędzy przestrzenią użytkownika a jądrem systemu.

---

## 5.8 BSP64 a pamięć i zarządzanie zasobami

BSP64 współpracuje z podstawowymi mechanizmami pamięci Bursztyn OS.

System operacyjny posiada:

* PMM — Physical Memory Manager,
* VMM — Virtual Memory Manager,
* stronicowanie pamięci,
* mapowanie pamięci wirtualnej,
* zarządzanie pamięcią jądra.

Dzięki temu system plików może być rozwijany w kierunku pełnej obsługi fizycznych urządzeń pamięci masowej.

Obecna architektura BSP64 została zaprojektowana tak, aby w przyszłości mogła zostać połączona z natywnymi sterownikami urządzeń, takich jak:

* SATA,
* NVMe,
* inne kontrolery pamięci masowej.

---

## 5.9 Integracja z formatem wykonywalnym `.bur`

BSP64 współpracuje z natywnym formatem programów wykonywalnych Bursztyn OS:

```text
.bur
```

Format `.bur` został zaimplementowany zgodnie ze specyfikacją:

```text
Specyfikacja_BUR_v1
```

Loader systemu:

```text
loader.cpp
```

wyszukuje nagłówek:

```text
BUR\0
```

następnie odczytuje wymagane informacje z pliku, w tym rozmiar tekstu programu, i ładuje program do przestrzeni użytkownika.

Po załadowaniu program może zostać uruchomiony w:

```text
Ring 3
```

Dzięki temu BSP64, loader `.bur`, BWS oraz przestrzeń użytkownika tworzą wspólny, działający mechanizm uruchamiania programów.

---

## 5.10 Aktualny stan architektury systemu plików

Aktualna architektura Bursztyn OS przedstawia się następująco:

```text
┌──────────────────────────────────┐
│          Aplikacja .bur           │
│             Ring 3                │
└────────────────┬─────────────────┘
                 │
                 ▼
┌──────────────────────────────────┐
│          Powłoka Bursztyna        │
│            shell.cpp              │
└────────────────┬─────────────────┘
                 │
                 │ syscall
                 ▼
┌──────────────────────────────────┐
│              BWS                  │
│   Bursztynowe Wywołania Systemowe │
└────────────────┬─────────────────┘
                 │
                 ▼
┌──────────────────────────────────┐
│              Jądro                │
│             Ring 0                │
└────────────────┬─────────────────┘
                 │
                 ▼
┌──────────────────────────────────┐
│             BSP64                 │
│    System plików bloków 4 KB      │
└──────────────────────────────────┘
```

Wszystkie te elementy są obecnie częścią działającej architektury Bursztyn OS.

---

## 5.11 Planowany dalszy rozwój

Dalszy rozwój BSP64 obejmuje między innymi:

* rozbudowę obsługi fizycznych urządzeń pamięci masowej,
* sterowniki SATA i NVMe,
* trwałe przechowywanie danych,
* dalszą rozbudowę metadanych plików,
* system uprawnień i ochrony zasobów,
* rozszerzenie obsługi procesów,
* rozbudowę systemu aplikacji `.bur`,
* mechanizmy instalacji i zarządzania programami.

Docelowo BSP64 ma stać się pełnoprawnym, natywnym systemem plików Bursztyn OS, zintegrowanym z jądrem, pamięcią wirtualną, systemem BWS oraz przestrzenią użytkownika.

---

## Podsumowanie

BSP64 jest jednym z kluczowych fundamentów Bursztyn OS.

Aktualnie system operacyjny posiada:

* działający 64-bitowy Long Mode,
* GDT i IDT,
* kontroler przerwań APIC/IOAPIC,
* zarządzanie pamięcią PMM i VMM,
* działający system plików BSP64 oparty na blokach 4 KB,
* format programów wykonywalnych `.bur`,
* loader programów działający w Ring 3,
* system BWS oparty na instrukcji `syscall`,
* działającą powłokę systemową `shell.cpp`,
* izolację przestrzeni użytkownika od jądra.

Bursztyn OS posiada zatem działający fundament systemu operacyjnego: od uruchomienia procesora w trybie 64-bitowym, przez zarządzanie pamięcią i system plików, aż po uruchamianie programów użytkownika w Ring 3.

