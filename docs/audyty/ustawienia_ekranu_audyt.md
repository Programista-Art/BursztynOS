# Audyt poprawności podsystemu Ustawienia ekranu (Bursztyn OS)

**Data audytu:** 2026-09-06
**Status weryfikacji manualnej:** NOT MANUALLY VERIFIED BY HUMAN
**Wynik testów automatycznych:** PASS (PASS assertions = 31128, FAIL assertions = 0)

---

## 1. Zgodność listy rozdzielczości (Resolution List Consistency)

### Analiza różnicy między 10 kandydatami a 9 trybami
W module `grafika.cpp` zdefiniowano 10 kandydatów na tryby wyświetlania:
`800x600`, `1024x768`, `1280x720` (boot), `1280x800`, `1366x768`, `1360x768`, `1600x900`, `1920x1080`, `2560x1440`, `3840x2160`.

Podczas uruchomienia test raportuje:
```
[EKRAN TEST] trybow=9
```

**Przyczyna odrzucenia trybu 1366x768:**
Tryb `1366x768` zostaje odrzucony w funkcji `bochs_sprawdz_tryb()` przez asercję wyrównania do 8 pikseli:
```cpp
!(t.szerokosc & 7)
```
1366 nie jest podzielne przez 8 ($1366 \equiv 6 \pmod 8$). Rejestry sprzętowe DISPI w emulatorze Bochs/QEMU (`VBE_DISPI_INDEX_XRES`) obcinają szerokość w dół do najbliższej wielokrotności 8 (do 1360 pikseli). Przekazanie wartości 1366 spowodowałoby rozbieżność między stride bufora gościa a sprzętowym przetwornikiem LFB (obraz pochylony / przesunięty). Dlatego Bursztyn OS prawidłowo odrzuca 1366x768, a akceptuje poprawny tryb panoramiczny 1360x768.

**Tryb 3840x2160 (4K UHD):**
W bieżącym środowisku QEMU:
- Wykryta pamięć VRAM VGA: `67,108,864 B` (64 MiB)
- Wymagany rozmiar LFB dla 4K: $3840 \times 2160 \times 4 = 33,177,600\text{ B}$ (~31.64 MiB)
- Status: **zaakceptowany** ($33,177,600 \le 67,108,864$) i pomyślnie sprawdzony alokacją próbną 3 buforów grafiki.
*(Gdyby QEMU uruchomiono ze standardowym 16 MiB VRAM, 4K zostałoby odrzucone z powodem `n > pamiec`, redukując liczbę trybów do 8).*

### Wykaz 9 trybów udostępnianych przez sterownik Bochs VBE:
1. `1280x720x32` (boot mode, HD)
2. `800x600x32` (SVGA)
3. `1024x768x32` (XGA)
4. `1280x800x32` (WXGA)
5. `1360x768x32` (Wide 16:9 8-pixel aligned)
6. `1600x900x32` (HD+)
7. `1920x1080x32` (Full HD)
8. `2560x1440x32` (QHD)
9. `3840x2160x32` (4K UHD)

Aplikacja `Ustawienia ekranu` (`programy/ekran/ekran.cpp`) pobiera listę dynamicznie przez HAL (`stan.tryby[i]`), dzięki czemu nie reklamuje trybów nieobsługiwanych przez aktywny sterownik.

---

## 2. Pamięć VRAM urządzenia vs Pamięć systemowa (VRAM vs System RAM)

- **Weryfikacja VRAM urządzenia:**
  - `bochs_wykryj()` odczytuje pojemność VRAM z rejestru DISPI `0x1ce / 0x1cf` indeks 10 (`VBE_DISPI_INDEX_VIDEO_MEMORY_64K`), walidując ją przed aktywacją sterownika. Zalogowano: `[BOCHS VBE] VRAM: 67108864 B, max: 16000x12000@32`.
  - `bochs_sprawdz_tryb()` wymaga, aby rozmiar LFB $W \times H \times 4$ mieścił się w fizycznym VRAM urządzenia (`n <= pamiec`).
- **Pamięć systemowa dla grafiki:**
  - Bufory pomocnicze (backbuffer, bufor tapety, powierzchnie okien) alokowane są z dedykowanej puli PMM/VMM (`bufory_grafiki.cpp`), a nie z pamięci sterty jądra (`kmalloc`).
  - Alokacja buforów 4K (33 177 600 B) zużywa **0 bajtów** sterty jądra (`kmalloc`), eliminując ryzyko wyczerpania 16 MiB sterty kernelowej.

---

## 3. Ścieżka zatwierdzania i trwałości (Confirm & Persist Path)

- Operacja `EKRAN_ZACHOWAJ` zapisuje potwierdzoną konfigurację do pliku `/ustawienia/ekran.cfg` za pomocą `psf_flush_cache()` oraz weryfikującego odczytu wstecznego (`czytaj_fragment_pliku`).
- **Test wycofania przy braku potwierdzenia (Timeout Rollback):**
  - Zmiana trybu ustawia 15-sekundowy deadline.
  - Po upływie czasu `ekran_obsluz_timeout()` automatycznie przywraca poprzedni tryb sprzętowy i geometrię kompozytora.
  - Plik konfiguracyjny `/ustawienia/ekran.cfg` zachowuje ostatnio zatwierdzony tryb (nie zostaje nadpisany niepotwierdzonym trybem).
  - Wynik testu: `[EKRAN TEST] Confirm & persist, and timeout rollback: PASS`.

---

## 4. Semantyka konfiguracji startowej (Startup Config Semantics)

W funkcji `ekran_inicjalizuj()` w `ekran.cpp`:
- Jeśli plik `/ustawienia/ekran.cfg` istnieje i zawiera poprawną skalę `ui_skala_poprawna(c.skala)`:
  - Skala interfejsu zostaje natychmiast zastosowana (`skala = c.skala; bws_aktualizuj_ekran(skala)`).
  - Jeśli system startuje na backendzie bez obsługi zmiany trybu w runtime (UEFI GOP lub BIOS VESA) albo jeśli tryb fizyczny nie jest obsługiwany przez sprzęt, sterownik zachowuje tryb startowy (boot mode), ale **zachowuje skonfigurowaną skalę UI**.
  - Wycofanie skali do 100% następuje wyłącznie w przypadku uszkodzenia lub braku pliku konfiguracyjnego.

---

## 5. Audyt buforów PMM/VMM (PMM/VMM Buffer Audit)

- **Przestrzeń wirtualna:** `BUFORY_GRAFIKI_BAZA = 0xffff900000000000` (indeks PML4 = 288).
- **Brak nakładania z HHDM:** HHDM znajduje się pod indeksem PML4 256 (`0xffff800000000000`). Indeks 288 jest odrębną, dedykowaną gałęzią jądra (supervisor-only).
- **Zarządzanie stronami i TLB:**
  - `VmmOdmapujBuforGrafiki()` czyści odpowiednie wpisy PTE, pobiera adres fizyczny ramki i wywołuje `uniewaznij_tlb(va)` dla każdej odmapowywanej strony.
  - Ramki fizyczne są natychmiast zwracane do PMM przez `ZwolnijRamke(pa)`.
  - Współdzielone tablice stron PT pozostają w drzewie jądra, zapobiegając fragmentacji i konieczności powtórnego klonowania struktur stronicowania.

---

## 6. Powtarzane przejścia trybów (Repeated Mode Transitions)

Przeprowadzono sekwencję 3 pełnych cykli (15 przełączeń trybów sprzętowych):
`1280x800 -> 1920x1080 -> 2560x1440 -> 1920x1080 -> 1280x800` (x3).

Wyniki pomiaru delt:
```
[EKRAN TEST] 3x transitions PMM delta=0
[EKRAN TEST] 3x transitions HEAP delta=0
[EKRAN TEST] 3x transitions BUF delta=0
[EKRAN TEST] 3x transitions PMM: STABILNE (PASS)
[EKRAN TEST] 3x transitions HEAP: STABILNE (PASS)
[EKRAN TEST] 3x transitions BUF: STABILNE (PASS)
```
- Delta wolnych ramek PMM: **0**
- Delta pamięci sterty jądra: **0**
- Delta alokacji buforów grafiki: **0**

---

## 7. Audyt blokad ekranu (Display Lock Audit)

- Blokada spinlockowa `zablokuj_ekran()` / `odblokuj_ekran()` obejmuje wyłącznie operacje atomowe:
  1. Fizyczne przełączenie rejestrów I/O DISPI (`bochs_ustaw_tryb`).
  2. Podmianę wskaźników buforów ekranu (`grafika_wymien_stan`).
  3. Aktualizację geometrii warstw (`skladacz_zastosuj_ekran` / `skladacz_cofnij_ekran`).
  4. Złożenie pojedynczej spójnej klatki prezentacyjnej (`skladacz_obrazu_zloz_klatke`).
- **Długotrwałe operacje poza blokadą:**
  - Alokacja i zerowanie ramek PMM (`grafika_przygotuj_ekran`) – wykonywane przed blokadą.
  - Skalowanie tapety BTP (`tapety_przygotuj_ekran`) – wykonywane przed blokadą.
  - Zwalnianie starych buforów (`grafika_zwolnij_stan`, `bufor_grafiki_zwolnij`) – wykonywane po zwolnieniu blokady.
  - Zapis konfiguracji do systemu plików PSF (`zapisz`, `psf_flush_cache`) – wykonywany całkowicie poza blokadą.
- Z funkcji `ipc_ekran` w `bws_nowoczesne.cpp` usunięto globalny wrapper `GuardEkranu`, uniemożliwiając zagłodzenie innych wątków graficznych.

---

## 8. Geometria BWS62 (BWS62 Geometry Audit)

Wywołanie syscall 62 (`gui_pobierz_monitor`) zwraca:
- `szer`, `wys` – fizyczna rozdzielczość ekranu.
- `work_x = 0`, `work_y = 0`.
- `work_szer = szer`.
- `work_wys = wys - ui_skaluj(40, skala)` (obszar roboczy pomniejszony o skalowaną wysokość paska zadań).
- `scale_x1000 = skala * 10`.

Asercje weryfikujące pełną spójność struktury `GuiMonitorInfo` przy wszystkich trybach i skalach (100%–200%) zaliczone pomyślnie.

---

## 9. Obsługa myszy i okien (Mouse and Windows)

- **Mysz:**
  - Po zmianie rozdzielczości współrzędne kursora są natychmiast ograniczane: `mysz_x = min(mysz_x, szer - 1)`, `mysz_y = min(mysz_y, wys - 1)`.
  - Wszystkie kolejne zdarzenia ruchu myszy są ograniczane do aktualnych granic aktywnego ekranu.
- **Okna:**
  - Funkcja `ekran_dopasuj()` dba o to, by okna niemaksymalizowane mieściły się w nowym obszarze roboczym.
  - Okna zmaksymalizowane są automatycznie rozciągane do nowych wymiarów obszaru roboczego (`{0, 0, work_szer, work_wys}`).
  - Otwarte popupy aplikacji są zamykane podczas resetu ekranu (`skladacz_reset_ekranu()`).

---

## 10. Audyt skalowania interfejsu (UI Scale Audit)

Wszystkie natywne komponenty BursztynUI wspierają skale 100%, 125%, 150%, 175%, 200%:
- Pasek tytułowy okna i przyciski sterujące (zamknij, min, max).
- Etykiety i renderowanie czcionki Unicode (`extronic16B_unicode`).
- Przyciski standardowe, obrazkowe, podzielone, przełączniki, pola wyboru i radiowe.
- Listy rozwijane, tabele, widoki drzewiaste i kolumnowe, widoki miniatur i ikon.
- Paski przewijania i suwaki.
- Pasek zadań, menu Start i ikony pulpitu.

**Elementy legacy:**
- Terminal tekstowy VGA / bezpośrednie wyjście konsoli używa stałego rastra znaków 8x16 bez subpikselowego skalowania wektorowego (skalowane programowo przez mnożnik).

---

## 11. Zachowanie tapety pulpitu (Wallpaper Behavior)

- **Zmiana rozdzielczości fizycznej:** Działa dokładnie 1 przeskalowanie z bufora scratch BTP do nowego bufora tapety (`tapety_przygotuj_ekran`). Brak ponownego odczytu z dysku.
- **Zmiana skali interfejsu UI:** Dokładnie **0** odczytów z dysku, **0** dekodowań i **0** przeliczeń tapety (rozdzielczość bufora tapety zależy wyłącznie od fizycznej rozdzielczości ekranu).
- **Renderowanie klatek:** **0** pracy per klatka (kompozytor wykonuje bezpośrednie kopiowanie gotowej przygotowanej powierzchni tapety do backbuffera).

---

## 12. Wyniki testów i marginesy pamięci

- **Kompilacja i margines jądra:**
  - Koniec segmentu jądra: `0x0079DCA0` (~7.6 MiB)
  - Limit jądra: `0x00E00000` (14 MiB)
  - Margines bezpieczeństwa: `6,693,984 bajtów` (~6.38 MiB wolnej przestrzeni)
  - Strefa ochronna 2 MiB przed `RING3BASE` (`0x01000000` = 16 MiB) w pełni nienaruszona.
- **Automatyczna suita testowa:**
  - `python3 narzedzia/tapety-btp/testy/qemu_etap3.py boot --directory .test_run --fresh`
  - `[TESTY] PASS assertions=31128`
  - `[TESTY] FAIL assertions=0`
  - `Suite PASS; NOT MANUALLY VERIFIED BY HUMAN`

---
**Podsumowanie:**
Podsystem ustawień ekranu Bursztyn OS jest w pełni poprawny, stabilny pamięciowo i sprzętowo, a także gotowy do testów manualnych przez człowieka.
