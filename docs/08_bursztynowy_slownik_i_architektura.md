# 08. Bursztynowy Słownik i Lokalizacja Systemu

Niniejszy dokument formalizuje przejście Bursztyn OS z klasycznych, anglosaskich i uniksowych konwencji terminologicznych na natywne, polskie określenia techniczne. Zmiana ta ma na celu głębsze oddanie tożsamości systemu oraz pełną integrację językową na poziomie najniższych struktur jądra, systemu plików oraz przestrzeni użytkownika.
# 8.1 Słownik Pojęć Rdzennych (Bursztynowe Nazewnictwo)

Wprowadza się następujące oficjalne odpowiedniki struktur technicznych w kodzie źródłowym i dokumentacji:
## 1. Teczka (zamiast Katalog / Directory / Folder)

1. Definicja: Specjalny plik w Bursztynowym Systemie Plików (BSP), który zamiast danych surowych zawiera tablicę struktur powiązań nazw z węzłami indeksowymi.

1. Konwencja w kodzie: struktura teczka_wpis, funkcje systemowe: utworz_katalog() (wewnętrznie), wylistuj_katalog().

1. Uzasadnienie: Słowo "katalog" kojarzy się ze spisem treści, natomiast "teczka" idealnie oddaje fizyczny i logiczny kontener na dokumenty (pliki) w polskiej przestrzeni biurowej i administracyjnej.

## 2. Paczka Cebula (zamiast AppImage / APK / Tarball)

1. Definicja: Ustrukturyzowany format dystrybucji aplikacji z interfejsem graficznym. Jest to dedykowana teczka z rozszerzeniem .cebula, zawierająca plik wykonywalny oraz plik konfiguracyjny z wymogami bezpieczeństwa (opis.aplikacji).

1. Uzasadnienie: Nazwa z przymrużeniem oka nawiązująca do polskiego folkloru internetowego, posiadająca logiczny sens ("cebula ma warstwy" - aplikacja jest oddzielona od systemu warstwami uprawnień BZL).

## 3. Plik BUR (zamiast EXE / ELF / BIN)

1. Definicja: Skompilowany, 64-bitowy plik wykonywalny przestrzeni użytkownika (Ring 3), zawierający własny nagłówek z sygnaturą "BUR" oraz przesunięciami pamięci wirtualnej (zazwyczaj na adres 0x600000).

1. Konwencja: Pliki z rozszerzeniem .bur (np. shell.bur, notatnik.bur).

## 4. Włókno i Planista (zamiast Thread i Scheduler)

1. Definicja: Najmniejsza jednostka wykonawcza zarządzana przez procesor i Planistę jądra, współdzieląca przestrzeń adresową w ramach jednego procesu. Planista to centralny moduł rozdzielający czas procesora.

1. Uzasadnienie: "Włókno" brzmi bardziej inżynieryjnie niż "wątek" (kalka z angielskiego "thread"), doskonale oddając tkankę współbieżną systemu. "Planista" to precyzyjne określenie dla podmiotu harmonogramującego.

## 5. Zarządca Pamięci (zamiast Memory Manager)

1. Definicja: Warstwa jądra odpowiedzialna za alokację pamięci fizycznej (PMM) oraz wirtualnej (VMM).

1. Konwencja w kodzie: Funkcje ZaalokujRamke(), ZmapujStrone().

## 8.2 Strategia Implementacji UTF-8 i Obsługi Polskich Znaków

Aby system w pełni posługiwał się polską duszą inżynieryjną, Jądro Bursztyna kategorycznie odrzuciło ograniczenia 7-bitowego standardu ASCII oraz trybu tekstowego Legacy VGA.
## 1. Zorientowana Obiektowo Warstwa Abstrakcji Sprzętu (HAL)

Bursztyn OS renderuje obraz poprzez autorską warstwę HAL wykorzystującą polimorfizm w C++ (technika Placement New). System operuje bezpośrednio na Liniowym Buforze Ramki (LFB) za pomocą jednego z dwóch nowoczesnych sterowników:

1. UEFI GOP (Dla nowoczesnych maszyn z firmware EFI).

1. VESA VBE (Dla tradycyjnych BIOS-ów i emulatorów).
    Każdy piksel kontrolowany jest binarnie w 32-bitowej lub 24-bitowej głębi kolorów.
1. Bochs VBE - dla środowisk wirtualnych, takich jak QEMU   

2. Autorski Silnik Renderowania Proporcjonalnego (UTF-8)

Jądro implementuje zaawansowany interpreter kodowania UTF-8. Znaki wielobajtowe (takie jak ą, ć, ę, ł, ń, ó, ś, ź, ż) są poprawnie dekodowane z sekwencji bajtów w Terminalu i interfejsie graficznym.

1. System wykorzystuje proporcjonalną matrycę czcionki w rozmiarze 16x16 pikseli (extronic16B_unicode.h).

1. Funkcja RysujZnak w klasie Menedżera Grafiki dynamicznie dobiera szerokość znaku, zapewniając estetyczny wygląd polskich tekstów.

3. Obsługa Klawiatury PS/2

Sterownik klawiatury PS/2 przechwytuje przerwania sprzętowe i przetwarza uderzenia klawiszy (Make Code / Break Code). Wykrycie Prawego Alt (w standardzie Scancode Set 1) ustawia flagę w Jądrze, co w połączeniu z odpowiednimi klawiszami literowymi pozwala na natychmiastowe wstrzyknięcie pełnoprawnych bajtów UTF-8 do buforów aplikacji w Ring 3.
8.3 Przykładowe Struktury w Jądrze (Konwencja Nowego Nazewnictwa)

Poniższy kod ilustruje, jak polskie nazewnictwo techniczne integruje się ze strukturami kontrolnymi jądra:

```
#pragma once
#include <stdint.h>

// Definicje stanów włókna w Planiście
enum StanWlokna {
    STAN_GOTOWE = 0,
    STAN_WYKONYWANE = 1,
    STAN_OCZEKUJACE = 2,
    STAN_ZAKONCZONE = 3
};

// Struktura kontrolna pojedynczego Włókna
struct WloknoKontrolne {
    uint64_t wlokno_id;          // Unikalny identyfikator włókna
    uint64_t rejestr_rsp;        // Zachowany wskaźnik stosu jądra (Ring 0)
    uint64_t rejestr_rip;        // Wskaźnik instrukcji (punkt wznowienia)
    StanWlokna stan;             // Aktualny stan wykonawczy
    uint32_t kwant_czasu;        // Pozostały czas procesora dla włókna
    struct proces* rodzic;       // Odnośnik do procesu macierzystego
} __attribute__((packed));

// Struktura powiązań w systemie BSP
struct teczka_wpis {
    uint32_t id_wezla;           // Odnośnik do węzła indeksowego
    char nazwa[32];              // Nazwa pliku zakodowana w UTF-8
} __attribute__((packed));

// Nagłówek polskiego formatu wykonywalnego (Ring 3)
struct NaglowekBur {
    uint8_t  magia[4];           // Magiczna sygnatura "BUR\0"
    uint64_t punkt_wejscia;      // Adres procedury _start
    uint64_t tekst_wirtualny;    // Wirtualny adres ładowania kodu (np. 0x600000)
} __attribute__((packed));

```