## Bursztynowy Słownik

Niniejszy dokument formalizuje przejście Bursztyn OS z klasycznych, anglosaskich i uniksowych konwencji terminologicznych na natywne, polskie określenia techniczne. Zmiana ta ma na celu głębsze oddanie tożsamości systemu oraz pełną integrację językową na poziomie najniższych struktur jądra, systemu plików oraz planisty.

## 8.1 Słownik Pojęć Rdzennych (Bursztynowe Nazewnictwo)

Wprowadza się następujące oficjalne odpowiedniki struktur technicznych w kodzie źródłowym i dokumentacji:

### 1. Teczka (zamiast Katalog / Directory / Folder)
* **Definicja:** Specjalny plik w Bursztynowym Systemie Plików (BSP), który zamiast danych surowych zawiera tablicę struktur powiązań nazw z węzłami indeksowymi.
* **Konwencja w kodzie:** `struktura teczka_wpis` (zamiast `wpis_katalogowy`), funkcje systemowe: `utworz_teczke()`, `usun_teczke()`.
* **Uzasadnienie:** Słowo "katalog" kojarzy się ze spisem treści, natomiast "teczka" idealnie oddaje fizyczny i logiczny kontener na dokumenty (pliki) w polskiej przestrzeni biurowej i administracyjnej.

### 2. Włókno / Włókna (zamiast Wątek / Thread)
* **Definicja:** Najmniejsza jednostka wykonawcza zarządzana przez procesor i planistę jądra, współdzieląca przestrzeń adresową w ramach jednego procesu.
* **Konwencja w kodzie:** `wlokno_t`, `struktura wlokno_kontrolne`, funkcje: `utworz_wlokno()`, `usypij_wlokno()`.
* **Uzasadnienie:** Słowo "wątek" jest kalką z angielskiego "thread". "Włókno" brzmi bardziej inżynieryjnie, spójnie i doskonale oddaje tkankę współbieżną, z której utkany jest działający proces.

### 3. Planista (zamiast Scheduler)
* **Definicja:** Centralny moduł Jądra Bursztyna odpowiedzialny za przydział czasu procesora (kwantów czasu) dla poszczególnych włókien w oparciu o algorytmy priorytetowe i poziomy zaufania BZL.
* **Konwencja w kodzie:** `planista_wybierz_nastepne_wlokno()`, `planista_inicjalizacja()`.
* **Uzasadnienie:** Czyste, precyzyjne polskie określenie dla podmiotu, który organizuje i planuje harmonogram pracy procesora.

### 4. Zarządca Pamięci (zamiast Menedżer Pamięci / Memory Manager)
* **Definicja:** Warstwa jądra odpowiedzialna za alokację pamięci fizycznej (wcześniej PMM - Zarządca Pamięci Fizycznej) oraz wirtualnej (wcześniej VMM - Zarządca Pamięci Wirtualnej).
* **Konwencja w kodzie:** `zarzadca_pamieci_anuluj_rzut()`.

---

## 8.2 Strategia Implementacji UTF-8 i Obsługi Polskich Znaków

Aby system w pełni posługiwał się polską duszą inżynieryjną, Jądro Bursztyna kategorycznie odrzuca ograniczenia 7-bitowego standardu ASCII oraz trybu tekstowego Legacy VGA (80x25), który uniemożliwia poprawne renderowanie narodowych glifów bez modyfikacji tablicy znaków w locie.

### 1. Przejście na Linear Framebuffer (LFB) przez VBE/VESA
W Etapie 6 system przełącza kartę graficzną w tryb graficzny o wysokiej rozdzielczości (np. 1024x768 w 32-bitowym kolorze). Jądro uzyskuje bezpośredni dostęp do liniowego bufora ramki, gdzie każdy piksel kontrolowany jest binarnie.

### 2. Autorski Silnik Renderowania Czcionki Bitmapowej (UTF-8)
Jądro implementuje interpreter kodowania UTF-8. Znaki wielobajtowe (takie jak `ą`, `ć`, `ę`, `ł`, `ń`, `ó`, `ś`, `ź`, `ż` oraz ich wielkie odpowiedniki) są poprawnie dekodowane z sekwencji bajtów na punkty kodowe Unicode (Code Points), a następnie mapowane na autorską matrycę czcionki bitmapowej (np. w układzie 8x16 pikseli).

### 3. Obsługa Modyfikatora Prawy ALT w Sterowniku Klawiatury
Sterownik klawiatury PS/2 w pliku `klawiatura.cpp` zostanie rozbudowany o obsługę stanów klawiszy modyfikujących. Wykrycie Make Code prawego Alt (w standardzie Scancode Set 1 często poprzedzonego prefiksem `0xE0`) ustawia flagę bitową `MODYFIKATOR_ALT_PRAWY`.
* Kombinacja `Alt + A` automatycznie wygeneruje w buforze sekwencję UTF-8 dla znaku `ą` (`0xC4 0x85`).

---

## 8.3 Przykładowe Struktury w Jądrze (Konwencja Nowego Nazewnictwa)

Poniższy kod ilustruje, jak polskie nazewnictwo techniczne integruje się ze strukturami kontrolnymi jądra:

Wynik działania kodu
File created successfully: 08_bursztynowy_slownik_i_architektura.md

```cpp
#pragma once
#include <stdint.h>

// Definicje stanów włókna w Planiście
enum StanWlokna {
    STAN_GOTOWE = 0,
    STAN_WYKONYWANE = 1,
    STAN_OCZEKUJACE = 2,
    STAN_ZAKONCZONE = 3
};

// Struktura kontrolna pojedynczego Włókna (zamiast Thread Control Block)
struct WloknoKontrolne {
    uint64_t wlokno_id;          // Unikalny identyfikator włókna
    uint64_t rejestr_rsp;        // Zachowany wskaźnik stosu włókna
    uint64_t rejestr_rip;        // Wskaźnik instrukcji (punkt wznowienia)
    StanWlokna stan;             // Aktualny stan wykonawczy
    uint32_t kwant_czasu;        // Pozostały czas procesora dla włókna
    struct proces* rodzic;       // Odnośnik do procesu macierzystego
} __attribute__((packed));

// Zaktualizowany wpis strukturalny dla Teczki (dawny wpis_katalogowy)
struct teczka_wpis {
    uint32_t id_wezla;           // Odnośnik do węzła indeksowego BSP
    char nazwa[32];              // Nazwa pliku lub podteczki zakodowana w UTF-8
} __attribute__((packed));