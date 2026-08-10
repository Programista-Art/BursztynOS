# 01. Wstęp i Filozofia Bursztyn OS
## 1.1 Cel Projektu

Bursztyn OS powstał jako manifest niezależności technologicznej i inżynieryjnej. Głównym założeniem jest stworzenie w pełni funkcjonalnego środowiska operacyjnego od absolutnego zera (bare-metal), odrzucając architekturę jądra Linux, systemów z rodziny BSD czy Windows.

System zaprojektowany jest jako edukacyjna, eksperymentalna, a docelowo użytkowa platforma uruchomieniowa ze zorientowanym obiektowo interfejsem graficznym, zintegrowana z natywnym językiem programowania Avocado.

## 1.2 Tożsamość Techniczna i Nazewnictwo

W celu odcięcia się od anglosaskich standardów i powielanych schematów, kluczowe warstwy i mechanizmy systemu zyskały autorskie, polskie nazewnictwo techniczne. Tożsamość systemu budowana jest wokół czterech filarów:

1. Jądro Bursztyna – Centralny komponent systemu (kernel) operujący w sprzętowym Ring 0.

1. BSP (Bursztynowy System Plików) – Autorski system organizacji danych i struktury teczek utrwalany na dyskach AHCI SATA.

1. BWS (Bursztynowe Wywołania Systemowe) – Interfejs programistyczny zawierający 26 zaimplementowanych wywołań (zastępujący pojęcia syscall/ABI).

1. PZB (Poziom Zaufania Bursztyna) – Logiczna matryca uprawnień procesów w skali 0–5.

W warstwie kodu niskopoziomowego oraz ścieżkach BSP stosowana jest konwencja snake_case bez polskich znaków diakrytycznych (np. wezel_indeksowy, /uzytkownicy), natomiast warstwa interfejsu graficznego (GUI) oraz powłoki prezentuje pełne polskie nazewnictwo z obsługą kodowania UTF-8 i czcionek proporcjonalnych.

## 1.3 Stan Projektu i Roadmapa Rozwoju

Rozwój Bursztyn OS osiągnął stan pełnej dojrzałości operacyjnej w następujących etapach inżynieryjnych:

### * Etap 1–2 (Zakończone sukcesem):

* Stabilny rozruch w trybie 64-bit Long Mode za pomocą nagłówka Multiboot2 (z wymuszeniem trybu panoramicznego 1280x720/1024x768).

* Całkowity demontaż układów PIC na rzecz kontrolera APIC i cyklicznego APIC Timera.

* Zegar Czasu Rzeczywistego (RTC CMOS) podłączony do podsystemu BWS.

* Zarządca Pamięci Fizycznej (PMM) na bazie bitmapy oraz Zarządca Pamięci Wirtualnej (VMM) z 4-poziomowym stronicowaniem i optymalizacją Wielkimi Stronami (2 MB Pages).

### * Etap 3–5 (Zakończone sukcesem):

* Natywna obsługa kontrolera pamięci masowej AHCI SATA oraz rejestrów LBA.

* Struktura BSP utrwalana na dysku z buforowaniem pamięci VMM powyżej granicy 4 GB (0x130000000ULL).

* Fizyczna izolacja Przestrzeni Użytkownika (Ring 3) za pomocą MSR (IA32_LSTAR) z instrukcjami SYSCALL/SYSRET oraz sprzętowym segmentem TSS (Task State Segment) chroniącym stos Jądra.

* Kompletny interfejs BWS (26 wywołań systemowych) z kontrolą flag weryfikowanych przez PZB.

* Ładowanie natywnych binarów .bur do pamięci użytkownika (np. pod adres 0x600000).

### * Etap 6–7 (Zakończone sukcesem):

* Wieloplatformowa, obiektowa warstwa HAL z polimorficznymi sterownikami: UEFI GOP, VESA VBE oraz Bochs VBE.

* System Podwójnego Buforowania (Double Buffering) z Backbufferem alokowanym w bezpiecznych obszarach VMM.

* Silnik renderowania proporcjonalnych czcionek Unicode UTF-8 (16x16 pikseli).

* Systemowy Menedżer Okien (Pulpit - menedzer_okien.bur) w Ring 3 z Paskiem Zadań, rozwijanym Menu Start, zegarem, ikonalnym starterem oraz obsługą myszy PS/2 (Z-Order Focus, Drag & Drop).

### * Etap 8 (Zakończone sukcesem):

* Pełny, natywny stos sieciowy TCP/IP z obsługą karty Intel PRO/1000 (E1000).

* Zintegrowany klient DHCP (automatyczny przydział IP, np. 10.0.2.15), klient DNS UDP, obsługa komend ICMP Ping oraz pobieranie danych po HTTP (BWS-013) z zapisem na dysk AHCI.

* Dystrybucja graficznych aplikacji użytkownika w formacie paczek .cebula (Edytor Notatnik, Kalkulator) z manifestami opis.aplikacji egzekwowanymi przez PZB.

### 1.4 Kolejne Kroki (Roadmapa na Nowe Etapy)

Mając w pełni stabilne i sprawne na fizycznym sprzęcie oraz emulatorach Jądro, projekt przechodzi do nowej fazy rozwoju:

* Dynamiczny Alokator Pamięci (Ring 3): Wdrożenie funkcji malloc() i free() dla aplikacji w przestrzeni użytkownika (pobieranie dodatkowych stron od VMM).

* Podsystem Dźwiękowy: Natywny sterownik dla układu audio (Intel ICH AC97) w celu odtwarzania plików powitalnych .wav.

* Przeglądarka Grafik: Okienkowa aplikacja .cebula dekodująca i wyświetlająca pliki obrazów .bmp odczytane z systemu plików BSP.

* Kompilator Avocado: Natywny potok kompilacji języka Avocado generujący bezpośrednie binaria .bur i pakujący je w struktury .cebula.