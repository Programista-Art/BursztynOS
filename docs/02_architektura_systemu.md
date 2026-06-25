# 02. Architektura Systemu i Model Bezpieczeństwa BZL

Bursztyn OS implementuje ścisły podział uprawnień, łącząc sprzętowe mechanizmy ochrony procesora x86-64 z autorską, logiczną warstwą kontroli dostępu.

## 2.1 Podział Sprzętowy: Ring 0 vs Ring 3

System w pełni wykorzystuje architekturę segmentacji i stronicowania procesora do izolacji kodu:

* **Ring 0 (Tryb Jądra):** Wykonywane jest tu Jądro Bursztyna. Posiada ono nieograniczony dostęp do instrukcji procesora, pamięci fizycznej, tablic stron (CR3), konfiguracji przerwań i rejestrów MSR.
* **Ring 3 (Tryb Użytkownika):** Miejsce uruchamiania procesów aplikacyjnych (programy `.bur`). Kod w tym ringu nie ma bezpośredniego dostępu do rejestrów kontrolnych, pamięci jądra ani portów I/O. Każda próba nieautoryzowanego dostępu skutkuje sprzętowym wyjątkiem General Protection Fault (#GP).

Komunikacja między Ring 3 a Ring 0 odbywa się wyłącznie w sposób kontrolowany poprzez instrukcję `SYSCALL` realizowaną przez moduł **BWS**.

## 2.2 BZL – Bursztynowy Poziom Zaufania (0–5)

**BZL** to unikalna, logiczna warstwa zabezpieczeń zarządzana programowo przez Jądro Bursztyna. **BZL nie stanowi dodatkowych ringów sprzętowych procesora.** Cała przestrzeń użytkownika działa sprzętowo w Ring 3, jednak Jądro przypisuje każdemu procesowi poziom BZL od 0 do 5, determinując jego uprawnienia w systemie.

| Poziom BZL | Nazwa Poziomu | Przeznaczenie | Zakres Dostępności / Uprawnień |
| --- | --- | --- | --- |
| **BZL-0** | Jądro | Rdzeń systemu (Jądro) | Pełny dostęp do pamięci, tablic stron, Planistów Włókien. Sprzętowy Ring 0. |
| **BZL-1** | Sterowniki | Sterowniki sprzętowe (klawiatura, mysz, dysk, sieć) | Sprzętowy Ring 3 / wydzielony Ring 1. Dostęp do wybranych portów I/O i buforów DMA. |
| **BZL-2** | Usługi systemowe | Składacz Obrazu (GUI), Zarządca Okien, usługi sieciowe | Uprawnienia systemowe w tle, brak bezpośredniego wpływu na tablice stron jądra. |
| **BZL-3** | Aplikacje zaufane | Powłoka Bursztyna (Terminal), Notatnik, Edytor Avocado | Dostęp do podstawowych API systemowych, zasobów GUI i teczki domowej użytkownika. |
| **BZL-4** | Aplikacje użytkownika | Zwykłe programy instalowane przez użytkownika | Izolowane aplikacje. Dostęp do sieci czy plików zależny wyłącznie od deklaracji w manifeście. |
| **BZL-5** | Piaskownica | Programy testowe, niezaufane eksperymenty | Brak dostępu do prywatnych danych. Własne, odizolowane okno GUI, dostęp tylko do teczki `/piaskownica` lub `/tmp`. |

### 2.3 Flagi Uprawnień Procesu

Poziom BZL definiuje ogólne ramy, jednak precyzyjna kontrola opiera się na **bitowych flagach uprawnień** zaszytych w strukturze procesu (`proces_t`). Dzięki temu dwa procesy o tym samym poziomie BZL-4 mogą posiadać skrajnie różne uprawnienia.

W jądrze zdefiniowane są następujące maski bitowe uprawnień:

```cpp
#define PRAWO_PLIKI_CZYTAJ      (1 << 0)
#define PRAWO_PLIKI_ZAPISZ      (1 << 1)
#define PRAWO_SIEC              (1 << 2)
#define PRAWO_GUI               (1 << 3)
#define PRAWO_URUCHOM_PROGRAM   (1 << 4)
#define PRAWO_SYSTEM_CONFIG     (1 << 5)
#define PRAWO_STEROWNIK         (1 << 6)
#define PRAWO_DEBUG             (1 << 7)

### Struktura kontrolna procesu w pamięci jądra przyjmuje postać:

```

typedef struct proces {
uint64_t pid;                 // Unikalny identyfikator procesu
uint8_t  poziom_zaufania;     // Wartość BZL (0-5)
uint64_t uprawnienia;         // Bitowa mapa przyznanych praw
void* przestrzen_adresowa; // Wskaźnik na tablicę stron procesu (Zarządca Pamięci Wirtualnej)
} proces_t;

```

## 2.4 Walidacja Zabezpieczeń w Wywołaniach Systemowych (BWS)
Decyzję o przyznaniu dostępu do zasobu zawsze podejmuje jądro podczas obsługi wywołania BWS. Przykładem jest logiczna weryfikacja w funkcji zapisu pliku:

```

int bws_zapisz_plik(proces_t* p, const char* sciezka, const char* dane) {
// Sprawdzenie flagi bitowej uprawnień
if (!(p->uprawnienia & PRAWO_PLIKI_ZAPISZ)) {
return -1; // Błąd: Brak uprawnienia do zapisu plików
}

```
// Sprawdzenie poziomu BZL w relacji do chronionych ścieżek BSP
if (p->poziom_zaufania >= 4 && sciezka_zaczyna_sie_od(sciezka, "/system")) {
    return -2; // Błąd: Aplikacja użytkownika nie może modyfikować teczki /system
}

return bsp_zapisz_plik(sciezka, dane);

```

}

```

```