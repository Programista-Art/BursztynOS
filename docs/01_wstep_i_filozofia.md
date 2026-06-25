# 01. Wstęp i Filozofia Bursztyn OS

## 1.1 Cel Projektu

Bursztyn OS powstał jako manifest niezależności technologicznej i inżynieryjnej. Głównym założeniem jest stworzenie w pełni funkcjonalnego środowiska operacyjnego od absolutnego zera (bare-metal), odrzucając architekturę jądra Linux, systemów z rodziny BSD czy Windows.

System projektowany jest jako edukacyjna, eksperymentalna, a docelowo użytkowa platforma uruchomieniowa, zorientowana wokół dedykowanego języka programowania **Avocado**.

## 1.2 Tożsamość Techniczna i Nazewnictwo

W celu odcięcia się od anglosaskich standardów i powielanych schematów, kluczowe warstwy i mechanizmy systemu zyskały autorskie, polskie nazewnictwo techniczne. Tożsamość systemu budowana jest wokół trzech filarów:

1. **Jądro Bursztyna** – Centralny komponent systemu (kernel) operujący w Ring 0.
2. **BSP (Bursztynowy System Plików)** – Autorski system organizacji danych i struktur **teczek** na nośnikach.
3. **BWS (Bursztynowe Wywołania Systemowe)** – Interfejs programistyczny aplikacji (zastępujący pojęcia syscall/ABI).
4. **BZL (Bursztynowy Poziom Zaufania)** – Logiczna matryca uprawnień procesów.

W warstwie kodu niskopoziomowego (np. struktury systemowe) oraz ścieżkach BSP stosowana jest konwencja `snake_case` bez polskich znaków diakrytycznych (np. `wezel_indeksowy`, `/uzytkownicy`), natomiast warstwa interfejsu graficznego (GUI) docelowo prezentuje pełne polskie nazewnictwo.

## 1.3 Roadmapa Rozwoju

Rozwój Bursztyn OS został podzielony na fazy inżynieryjne:

* **Etap 1-2 (Zakończone sukcesem):** Stabilny rozruch w trybie 64-bit Long Mode (GRUB), uśpienie PIC, konfiguracja APIC i precyzyjnego LAPIC Timer, implementacja **Zarządców Pamięci: Fizycznej** (na bazie bitmapy) oraz **Wirtualnej** (odpowiedzialnej za 4-poziomowe tablice stron). Podstawowe IO (klawiatura PS/2, ekran tekstowy VGA).
* **Etap 3 (Zakończone sukcesem):** Implementacja standardu BSP w wydzielonym RAM-dysku 2 MB pod adresem wirtualnym `0x40000000`. Wsparcie dla pełnej struktury hierarchicznego **drzewa teczek** oraz parser ścieżek.
* **Etap 4-5 (Bieżący):** Izolacja przestrzeni użytkownika (Ring 3), wdrożenie mechanizmu instrukcji `SYSCALL` (BWS) z przekazywaniem parametrów przez rejestry R8-R13, napisanie pierwszego loadera programów `.bur`.
* **Etap 6-7 (Planowany):** Integracja kompilatora Avocado do generowania natywnych plików `.bur`. Przejście z trybu tekstowego VGA do Linear Framebuffer (LFB) przez standard VBE/VESA, implementacja Składacza Obrazu (Compositora) oraz GUI.
* **Etap 8 (Daleki cel):** Implementacja stosu sieciowego TCP/IP (karta Intel PRO/1000), przeglądarka internetowa oraz obsługa paczek aplikacyjnych `.cebula`.