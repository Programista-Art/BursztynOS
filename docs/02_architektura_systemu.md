# 02. Architektura Systemu i Model Bezpieczeństwa PZB

Bursztyn OS implementuje ścisły, hybrydowy podział uprawnień, łącząc natywne, sprzętowe mechanizmy ochrony procesora x86-64 z autorską, logiczną warstwą kontroli dostępu PZB (Poziom Zaufania Bursztyna).
## 2.1 Podział Sprzętowy: Ring 0 vs Ring 3 oraz TSS

System w pełni wykorzystuje architekturę stronicowania (VMM) i segmentacji (GDT) procesora do fizycznej izolacji kodu i danych:

* Ring 0 (Tryb Jądra): Miejsce, w którym wykonywane jest Jądro Bursztyna oraz natywne sterowniki monolityczne (HAL, AHCI, E1000). Kod w Ring 0 posiada nieograniczony, absolutny dostęp do instrukcji uprzywilejowanych, tablic stron (rejestr CR3), konfiguracji przerwań APIC oraz portów I/O.

* Ring 3 (Przestrzeń Użytkownika): Środowisko uruchomieniowe dla procesów aplikacyjnych (Menedżer Okien, Terminal, paczki .cebula). Kod w tym ringu nie ma dostępu do pamięci Jądra. Próba nieautoryzowanego wykonania instrukcji (np. outb lub hlt) skutkuje natychmiastowym wyjątkiem sprzętowym (General Protection Fault / Invalid Opcode) przechwytywanym przez Jądro (BSOD).

## Mechanizm TSS (Task State Segment): Komunikacja między Ring 3 a Ring 0 odbywa się za pomocą sprzętowych instrukcji SYSCALL. Aby zapobiec atakom na stos (Stack Smashing), procesor wykorzystuje zdefiniowany w GDT segment TSS. W momencie wywołania SYSCALL lub sprzętowego przerwania, TSS automatycznie podmienia wskaźnik stosu aplikacji (RSP) na bezpieczny, odizolowany w pamięci stos Jądra, chroniąc system przed awarią.
2.2 PZB - Poziomy Zaufania Bursztyna (0–5)

## PZB to unikalna, logiczna warstwa zabezpieczeń zarządzana programowo przez Jądro. Nie stanowi ona dodatkowych ringów sprzętowych procesora. Cała przestrzeń użytkownika działa sprzętowo w Ring 3, jednak Jądro podczas ładowania procesu z dysku przypisuje mu logiczny poziom zaufania, determinując jego wpływ na system.

Poziom PZB,Nazwa Poziomu,Przeznaczenie i Zakres Dostępności

PZB-0,Jądro / HAL,"Działa w sprzętowym Ring 0. Pełny dostęp do Zarządcy Pamięci, Planisty Włókien i sprzętu."
PZB-1,Usługi Niskopoziomowe,Przyszłościowe sterowniki ładowane w Ring 3 (mikrojądrowe). Specjalne prawa dostępu do sprzętu.
PZB-2,Środowisko GUI,"Zarezerwowane dla systemowego Menedżera Okien (/menedzer_okien.bur). Może renderować na cały ekran, przechwytywać ruchy myszy i wywoływać aplikacje."
PZB-3,Aplikacje Zaufane,Systemowa Powłoka (/shell.bur). Szeroki dostęp do operacji na plikach (odczyt/zapis struktury BSP).
PZB-4,Aplikacje Użytkownika,"Zwykłe programy instalowane z paczek .cebula (np. Notatnik, Kalkulator). Jądro nadaje im PZB-4 na podstawie pliku opis.aplikacji. Izolacja logiki."
PZB-5,Piaskownica (Sandbox),Niezaufane programy. Restrykcyjna izolacja. Dostęp wyłącznie do teczki /piaskownica/[app] oraz /tymczasowe. Odcięcie od plików innych użytkowników.

## 2.3 Flagi Uprawnień Procesu (Maski Bitowe)

Poziom PZB definiuje nadrzędne ramy izolacji, jednak precyzyjna kontrola opiera się na bitowych flagach uprawnień zaszytych w strukturze procesu. Flagi te są dynamicznie wczytywane przez Jądro na podstawie manifestu opis.aplikacji.

W Jądrze zdefiniowane są następujące maski uprawnień wywołań BWS:

```
#define PRAWO_PLIKI_CZYTAJ      (1 << 0)
#define PRAWO_PLIKI_ZAPISZ      (1 << 1)
#define PRAWO_SIEC              (1 << 2)
#define PRAWO_GUI               (1 << 3)
#define PRAWO_URUCHOM_PROGRAM   (1 << 4)
#define PRAWO_SYSTEM_CONFIG     (1 << 5)
#define PRAWO_STEROWNIK         (1 << 6)
#define PRAWO_DEBUG             (1 << 7)
```
Struktura kontrolna procesu w pamięci Jądra przyjmuje postać:
```
typedef struct proces {
    uint64_t pid;                  // Unikalny identyfikator procesu w systemie
    uint8_t  poziom_zaufania;      // Wartość PZB (0-5)
    uint64_t uprawnienia;          // Bitowa mapa przyznanych praw BWS (np. 0b00001011)
    void* przestrzen_adresowa;     // Tablica stron VMM zmapowana pod dany proces
    // ... dodatkowe informacje o otwartych plikach, gniazdach sieciowych i włóknach
} proces_t;
```

## 2.4 Walidacja Zabezpieczeń w Wywołaniach Systemowych (BWS)

Decyzję o przyznaniu dostępu do wrażliwego zasobu (dysku, ekranu, sieci) zawsze podejmuje niezależnie Jądro podczas obsługi bramki asynchronicznej bws_obsluga (w pliku syscalls.cpp). Weryfikacja jest rygorystyczna i składa się z dwóch etapów.

Przykład weryfikacji żądania zapisu na dysku:

```

int bws_zapisz_plik(proces_t* p, const char* sciezka, const char* dane) {
    // 1. Sprawdzenie flagi bitowej uprawnień (Nadanej z pliku opis.aplikacji)
    if (!(p->uprawnienia & PRAWO_PLIKI_ZAPISZ)) {
        return 0; // Odmowa: Brak uprawnienia do zapisu plików w manifeście
    }

    // 2. Sprawdzenie poziomu PZB w relacji do chronionych ścieżek jądra
    // Aplikacja o PZB-4 (Użytkownik) nie ma prawa niszczyć rdzenia systemu.
    if (p->poziom_zaufania >= 4 && (sciezka_zaczyna_sie_od(sciezka, "/system") || sciezka_zaczyna_sie_od(sciezka, "/jadro"))) {
        return 0; // Odmowa: Integralność systemu chroniona sprzętowo!
    }

    return bsp_zapisz_plik(sciezka, dane);
}
```

Dzięki oddzieleniu warstwy sprzętowej (Ring 0 / Ring 3) od warstwy logicznej (PZB i maski praw BWS), Bursztyn OS gwarantuje absolutną stabilność i ochronę przed złośliwym oprogramowaniem. Aplikacja .bur, niezależnie od tego co spróbuje zrobić w pamięci, jest ograniczona precyzyjnym parasolem bezpieczeństwa Jądra.