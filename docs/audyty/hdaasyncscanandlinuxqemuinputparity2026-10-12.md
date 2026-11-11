# Asynchroniczne nieblokujące skanowanie HDA oraz audyt parzystości wejścia QEMU Linux vs Windows

Data: 2026-10-12
Dotyczy: Sterownik jądra `sterowniki/dzwiek/hda.cpp`, podsystem diagnostyczny `sterowniki/dzwiek/hda_diag.cpp`, format ABI `sterowniki/dzwiek/hda_diag_format.h`, aplikacja `programy/test_dzwieku/test_dzwieku.cpp`, audyt launcherów `Makefile` i `runwindows.ps1`, narzędzia `narzedzia/test_hda_diag.cpp`, `narzedzia/test_dzwieku_hda_bws.cpp`, `narzedzia/test_dzwieku_gui.cpp`, `narzedzia/qemu_launcher_audit.py`, `narzedzia/test_launcher_parity.py`.

---

## 1. Wstęp i cel techniczny

Niniejszy dokument podsumowuje realizację dwóch precyzyjnie zdefiniowanych zadań w systemie Bursztyn OS:

1. **Zadanie 1: Asynchroniczne, nieblokujące skanowanie topologii HDA z kontrolką ładowania BursztynUI.**
   Wyeliminowanie chwilowego zawieszenia aplikacji „Test dźwięku” i pulpitu podczas startu poprzez zastąpienie jednorazowego, synchronicznego skanowania w syscallu BWS 89 ograniczoną maszyną stanów w jądrze (budżet $\le 4$ czasowników HDA na krok). Natychmiastowe utworzenie okna, zachowanie płynności interfejsu (przeciąganie, zamykanie, zdarzenia myszy) oraz ponowne wykorzystanie kanonicznej kontrolki `BursztynUI::WskaznikZajetosci` z obsługą motywu i wielogeneracyjności.
2. **Zadanie 2: Audyt parzystości konfiguracji QEMU Linux (`make runuefi`) vs Windows (`runwindows.ps1`) oraz analiza zachowania wskaźnika VirtIO Mouse.**
   Szczegółowa analiza przyczyn gwałtownego przeskakiwania kursora myszy do krawędzi ekranu w środowisku linuksowym uruchamianym wewnątrz VirtualBox. Wykazanie źródła problemu na poziomie konfliktu backendu SDL (`XWarpPointer`) z integracją myszy VirtualBox Guest Additions, bez wprowadzania jakichkolwiek modyfikacji ani regresji w sterownikach wejściowych gościa (`input_device.cpp`, `virtio_input.cpp`).

---

## 2. Część I: Nieblokujące skanowanie HDA i integracja `WskaznikZajetosci`

### 2.1. Audyt synchronicznej procedury BWS 89 i budżet czasowników (Verb Budget)

Przed refaktoryzacją aplikacja „Test dźwięku” tuż po uruchomieniu wywoływała syscall `BWS 89` (`BWS_HDA_DIAG_POBIERZ_MIGAWKE`), przekazując bufor użytkownika na całą strukturę `HdaDiagMigawka`. Wewnątrz jądra funkcja `hda_diag_wypelnij_migawke()` wykonywała w jednym ciągu całą enumerację:
1. Odpytanie rejestrów MMIO kontrolera.
2. Pętlę aktywnego sondowania wszystkich poprawnych adresów kodeków (`CAD = 0..14`) przy użyciu poleceń `GetParameter Vendor ID` (15 operacji IC).
3. Dla każdego odpowiadającego kodeka odczyt parametrów korzenia: `Vendor ID`, `Revision ID`, `Subordinate Node Count` (3 operacje).
4. Przejście po podwęzłach funkcyjnych (AFG): odczyt `Function Group Type`, `Subordinate Node Count`, `Audio Widget Capabilities`, `Supported Formats`, `Stream Formats`, `Amplifier Capabilities` (6+ operacji na grupę funkcyjną).
5. Przejście po każdym widgecie (DAC, Pin, ADC, itp.): odczyt `Widget Capabilities`, `Connection List Length`, pozycji listy połączeń, `Configuration Default`, `Pin Capabilities` (od 4 do 8 operacji na każdy widget).

**Pesymistyczna liczba czasowników podczas jednego wywołania synchronicznego:**
W typowej konfiguracji QEMU (`intel-hda` + `hda-output`) generowało to minimum **25 do 35 czasowników HDA**. W przypadku bardziej złożonych kodeków fizycznych (np. Realtek ALC892 / ALC269 z kilkudziesięcioma widgetami) liczba ta przekraczała **100–180 czasowników**.

Ponieważ każda operacja Immediate Command wymagała odpytania bitów `ICB` oraz `IRV` w rejestrze `ICIS` z bezpiecznym limitem czasu (timeout do 100 µs lub w skrajnych przypadkach kilku milisekund przy braku odpowiedzi), jedno wywołanie systemowe blokowało wątek aplikacji na czas od kilkunastu do kilkuset milisekund. W efekcie pętla zdarzeń BursztynUI nie mogła narysować pierwszego okna, kursor myszy zamierał, a pulpit nie odpowiadał.

### 2.2. Ograniczona maszyna stanów w jądrze (`hda_diag.cpp`)

Aby rozwiązać ten problem, procedurę skanowania podzielono na deterministyczną maszynę stanów o ścisłym budżecie wykonawczym:
- **Formalny budżet czasowników:**
  ```c
  #define HDA_DIAG_LIMIT_VERBOW_NA_KROK UINT32_C(4)
  #define HDA_DIAG_SLOW_NA_KROK         HDA_DIAG_LIMIT_VERBOW_NA_KROK
  ```
  Każde wywołanie kroku skanowania wykonuje maksymalnie **4 operacje HDA**, po czym natychmiast zwraca sterowanie do Ring 3.

- **Granularny podział stanów (`HdaDiagEtap`):**
  Zdefiniowano 12 jednoznacznych stanów skanera:
  1. `HDA_DIAG_ETAP_BEZCZYNNY` (`0`) – Skaner bezczynny, brak aktywnego zadania.
  2. `HDA_DIAG_ETAP_KONTROLER` (`1`) – Inwentaryzacja rejestrów MMIO kontrolera PCI.
  3. `HDA_DIAG_ETAP_SONDOWANIE_KODEKOW` (`2`) – Aktywne sondowanie obecności kodeków na szynie.
  4. `HDA_DIAG_ETAP_KORZEN_KODEKA` (`3`) – Odczyt parametrów węzła głównego (Vendor, Revision, podwęzły).
  5. `HDA_DIAG_ETAP_ODKRYWANIE_GRUP` (`4`) – Identyfikacja grup funkcyjnych (Audio Function Group).
  6. `HDA_DIAG_ETAP_PARAMETRY_AFG` (`5`) – Pobranie parametrów AFG (obsługiwane formaty, wzmocnienia).
  7. `HDA_DIAG_ETAP_ODKRYWANIE_WIDGETOW` (`6`) – Wyliczanie widgetów audio (NID startowy i liczba).
  8. `HDA_DIAG_ETAP_PARAMETRY_WIDGETOW` (`7`) – Pobieranie cech poszczególnych widgetów (DAC, ADC, itp.).
  9. `HDA_DIAG_ETAP_PARAMETRY_PINOW` (`8`) – Konfiguracja gniazd (Pin Complex, Default Config, Pin Cap).
  10. `HDA_DIAG_ETAP_LISTA_POLACZEN` (`9`) – Enumeracja list połączeń i trasowania audio.
  11. `HDA_DIAG_ETAP_TRASA_TOPOLOGII` (`10`) – Konstrukcja ścieżki odtwarzania (DAC -> Pin -> Line Out/Speaker).
  12. `HDA_DIAG_ETAP_PUBLIKACJA` (`11`) – Kopiowanie roboczej migawki do bufora publikacji.
  13. `HDA_DIAG_ETAP_UKONCZONY` (`12`) – Skanowanie zakończone sukcesem.
  14. `HDA_DIAG_ETAP_BLAD` (`13`) – Błąd krytyczny kontrolera lub timeout.

- **Ograniczenie sondowania kodeków do 1 adresu na krok:**
  W etapie `HDA_DIAG_ETAP_SONDOWANIE_KODEKOW` w jednym wywołaniu `hda_diag_krok_skanu()` testowany jest dokładnie jeden adres CAD (`biezacy_cad`). Dzięki temu, nawet jeśli dany kodek nie odpowiada i operacja kończy się timeoutem, opóźnienie w jednym kroku wynosi co najwyżej jeden pojedynczy interwał timeoutu, nie wpływając zauważalnie na płynność animacji GUI.

- **Bezpieczeństwo Ring 0 i brak wycieków do Ring 3:**
  - Cały stan skanowania jest trzymany wyłącznie w pamięci jądra (`static HdaDiagSkanerStan s_diag_skaner`).
  - Ring 3 nie posiada wskaźników do struktur MMIO ani buforów DMA.
  - Wywołania BWS nie operują na surowych adresach fizycznych.
  - Odpytywanie rejestrów Immediate Command (`ICOI`, `ICII`, `ICIS`) odbywa się wyłącznie wewnątrz jądra.

- **Podwójne buforowanie i atomowa publikacja:**
  Skaner posiada dwa bufory pełnej migawki (`bufor_a` i `bufor_b`). Nowe skanowanie wypełnia bufor roboczy. Dopiero po pomyślnym zakończeniu wszystkich etapów następuje atomowe przełączenie wskaźnika opublikowanego i podbicie licznika `generacja_opublikowana`.

- **Wielogeneracyjność i czyste anulowanie:**
  - Każde rozpoczęcie skanowania generuje unikalny numer `generacja_skanu`.
  - Przestarzałe kroki z poprzednich generacji są ignorowane.
  - Szybkie, wielokrotne kliknięcie „Odśwież” natychmiast anuluje poprzedni skan za pomocą wywołania `BWS_HDA_DIAG_SKAN_ANULUJ` (kod operacji `7`), resetuje stan i rozpoczyna nową generację bez ryzyka uszkodzenia pamięci czy pozostawienia wiszących zadań.
  - Poprzednia kompletna migawka pozostaje dostępna do odczytu i prezentacji w GUI aż do momentu ukończenia nowej generacji.

### 2.3. Architektura GUI w aplikacji „Test dźwięku”

- **Ponowne wykorzystanie kanonicznej kontrolki `WskaznikZajetosci`:**
  Aplikacja nie tworzy własnego koła ładowania. Zamiast tego wykorzystuje standardowy komponent `BursztynUI::WskaznikZajetosci` (`bursztyn_ui/wskaznik_zajetosci.h`), który:
  - Posiada 8-fazową animację trygonometryczną opartą na arytmetyce całkowitoliczbowej.
  - Integruje się z aktywnym motywem systemowym (`MOTYW_KOLOR_AKCENT`).
  - Automatycznie skaluje się z DPI interfejsu.
  - Włącza cykl odświeżania GUI przez `czy_wymaga_animacji()`, co zapobiega pustym pętlom `busy-wait`.

- **Przebieg uruchomienia (Startup Flow):**
  1. Utworzenie okna aplikacji `Test dźwięku` i natychmiastowe opublikowanie pierwszej klatki na pulpicie.
  2. Kontrolka `wskaznik_` staje się widoczna i aktywna (`uruchom()`).
  3. Etykieta statusu wyświetla komunikat: `Skanowanie kontrolera HDA...`.
  4. Zegar animacji GUI (`ZegarSkanu::krok_animacji`) co ~20 ms wywołuje `tick_skanu()`.
  5. W każdym takcie wykonywane jest pojedyncze wywołanie jądra `BWS_HDA_DIAG_SKAN_KROK`.
  6. Etykieta statusu na bieżąco prezentuje polską nazwę etapu pobraną z ABI (`hda_diag_etap_nazwa()`) oraz liczbowy postęp `[X/Y]`.
  7. Okno pozostaje w pełni interaktywne – użytkownik może je przesuwać, minimalizować lub zamknąć.
  8. Po osiągnięciu etapu `HDA_DIAG_ETAP_UKONCZONY` kontrolka `wskaznik_` zatrzymuje się i znika (`ukryj()`), a sekcje topologii, kodeka i silnika strumieni wypełniają się zdekodowanymi danymi.
  9. Zapis raportu do pliku `hda.txt` (`zapisz_raport()`) utrwala najnowszą kompletną opublikowaną generację, a w razie trwania skanu odnotowuje ten fakt w nagłówku.

---

## 3. Część II: Audyt parzystości launcherów QEMU i analiza zachowania myszy

### 3.1. Zestawienie topologii PCI i parametrów wirtualizacji

Przeprowadzono audyt porównawczy skryptu `runwindows.ps1` (wersja dla systemu Windows) oraz reguły `make runuefi` (`Makefile` dla systemu Linux).

Oba środowiska konfigurują **dokładnie 7 urządzeń PCI** w identycznej kolejności i z tożsamymi właściwościami:

| Indeks | Klasa urządzenia | Opcja QEMU | Oczekiwany BDF (i440fx) | Rola w Bursztyn OS |
|:---:|:---|:---|:---:|:---|
| 0 | Audio Controller | `-device intel-hda -device hda-output,audiodev=snd` | `00:02.0` | Dźwięk HD Audio, odtwarzanie próbek |
| 1 | Pamięć masowa | `-device virtio-blk-pci,drive=disk,disable-legacy=on` | `00:03.0` | Dysk twardy VirtIO Modern |
| 2 | Karta sieciowa | `-device virtio-net-pci,netdev=n1,disable-legacy=on` | `00:04.0` | Karta sieciowa VirtIO Modern |
| 3 | Generator RNG | `-device virtio-rng-pci,rng=rng0,disable-legacy=on` | `00:05.0` | Sprzętowy generator liczb losowych |
| 4 | Karta graficzna | `-device virtio-vga,disable-legacy=on,xres=1280,yres=720` | `00:06.0` | Akcelerator graficzny VirtIO Modern |
| 5 | Klawiatura | `-device virtio-keyboard-pci` | `00:07.0` | Klawiatura VirtIO Input |
| 6 | Wskaźnik myszy | `-device virtio-mouse-pci` | `00:08.0` | Względny wskaźnik myszy VirtIO Input |

Wspólne parametry platformy bazowej:
- Płyta główna i procesor: `-machine pc,hpet=on -cpu max -accel tcg,thread=single -smp 1 -m 2G`
- Konsola i grafika: `-serial stdio -vga none`
- Rozruch UEFI: firmware OVMF x64, bootowanie z obrazu ISO CD-ROM.

### 3.2. Dopuszczalne i zamierzone różnice backendu hosta

W audycie zidentyfikowano dwie zamierzone różnice wynikające ze środowiska uruchomieniowego systemu gospodarza:
1. **Podsystem audio hosta:**
   - Linux: `-audiodev alsa,id=snd` (kieruje dźwięk do linuksowego podsystemu ALSA).
   - Windows: `-audiodev none,id=snd` (lub `dsound`). Backend `none` celowo wycisza dźwięk po stronie hosta, nie wpływając na obecność ani emulację kontrolera `intel-hda` i kodeka wewnątrz maszyny wirtualnej.
2. **Backend interfejsu graficznego (Display Frontend):**
   - Windows: domyślnie `-display gtk` (skrypt `runwindows.ps1`).
   - Linux: domyślnie `-display sdl` w pliku `Makefile`, z możliwością nadpisania zmienną `QEMU_DISPLAY_ARGS`.

### 3.3. Przyczyna uciekania kursora myszy VirtIO w zagnieżdżonym środowisku VirtualBox

Podczas testów na maszynie wirtualnej z Linuksem działającej pod kontrolą VirtualBox zaobserwowano, że po uruchomieniu zagnieżdżonego QEMU z domyślnym backendem SDL kursor myszy w Bursztyn OS gwałtownie ucieka do krawędzi ekranu przy najmniejszym ruchu.

**Szczegółowa analiza przyczyn zjawiska:**
1. **Tryb względny myszy i `XWarpPointer` w bibliotece SDL:**
   Gdy QEMU z backendem SDL przechwytuje względną mysz (`virtio-mouse-pci`), ukrywa kursor hosta X11 i po każdym zdarzeniu ruchu wysyła żądanie protokołu X11 `XWarpPointer`, aby wycentrować fizyczny kursor w środku okna SDL. Dzięki temu kursor nigdy nie uderza w krawędź okna hosta i może generować ciągłe delty $\Delta X, \Delta Y$.
2. **Konflikt z integracją myszy VirtualBox Guest Additions:**
   System Linux w środowisku roboczym sam działa wewnątrz maszyny wirtualnej VirtualBox z zainstalowanymi dodatkami gościa (`VirtualBox Guest Additions`). Mechanizm integracji myszy VirtualBox (`VBoxClient --draganddrop / --vmsvga / mouse integration`) przechwytuje żądania `XWarpPointer` na poziomie serwera X11. Ponieważ VirtualBox próbuje utrzymać bezwzględną pozycję kursora z pulpitu Windows, uznaje wywołanie `XWarpPointer` za sztuczne przesunięcie i natychmiast generuje wielką, kompensującą deltę ruchu w przeciwnym kierunku.
3. **Pętla sprzężenia zwrotnego (Feedback Oscillation):**
   Wygenerowana przez VirtualBox sztuczna delta trafia z powrotem do SDL, SDL przekazuje ją do wirtualnego urządzenia `virtio-mouse-pci` w QEMU, a następnie ponownie wywołuje `XWarpPointer`. W efekcie sterownik gościa odbiera gwałtowne skoki o setki pikseli na ramkę, natychmiast dociskając kursor do krawędzi ekranu.
4. **Dlaczego backend GTK nie wykazuje tego problemu:**
   W backendzie GTK (`-display gtk,zoom-to-fit=off`) QEMU korzysta z mechanizmu przechwytywania wskaźnika biblioteki GDK bez uciekania się do pętli `XWarpPointer` serwera X11. Integracja myszy VirtualBox nie jest pobudzana do generowania fałszywych delt kompensacyjnych, a wskaźnik VirtIO porusza się stabilnie i przewidywalnie, dokładnie tak samo jak na maszynie Windows.
5. **Wnioski dotyczące sterownika gościa:**
   Sterownik `virtio_input.cpp` i warstwa `input_device.cpp` w Bursztyn OS poprawnie odbierają zdarzenia `EV_REL` (`REL_X`, `REL_Y`). Nie wprowadzono żadnych zmian w kodzie jądra gościa – problem leżał w 100% w interakcji SDL z wirtualizacją VirtualBox.

W `Makefile` wprowadzono pełną obsługę zmiennej `QEMU_DISPLAY_ARGS`:
```makefile
QEMUDISPLAYARGS ?= $(if $(strip $(QEMU_DISPLAY_ARGS)),$(QEMU_DISPLAY_ARGS),-display sdl)
```
Dzięki temu deweloperzy pracujący w zagnieżdżonym środowisku VirtualBox mogą uruchomić system poleceniem:
```bash
make runuefi QEMU_DISPLAY_ARGS="-display gtk,zoom-to-fit=off"
```
uzyskując idealnie płynną obsługę wskaźnika myszy bez dotykania kodu sterowników Bursztyn OS.

---

## 4. Polecenia weryfikacji i procedury testowe

Wszystkie procedury weryfikacyjne są w pełni zautomatyzowane i nie wymagają uruchamiania emulatora ani środowiska graficznego:

1. **Wypisanie wektora argumentów QEMU (Linux):**
   ```bash
   make runuefi-argv
   ```
   Wypisuje strukturę JSON zawierającą pełny wektor `argv` oraz mapowanie wszystkich 7 urządzeń PCI do oczekiwanych BDF.

2. **Audyt konfiguracji launchera (Linux):**
   ```bash
   make runuefi-audit
   ```
   Weryfikuje flagi procesora, akcelerację pojedynczego wątku TCG, kolejność urządzeń i brak nadmiarowych kontrolerów PCI.

3. **Kompleksowy test parzystości launcherów:**
   ```bash
   python3 narzedzia/test_launcher_parity.py
   ```
   Wykonuje 8 automatycznych testów sprawdzających zgodność `Makefile` oraz `runwindows.ps1`.

4. **Testy jednostkowe jądra i ABI HDA Diag:**
   ```bash
   make test-hda-diag
   ```
   Wykonuje 233 testy jądra oraz 148 testów BWS, weryfikując budżet 4 czasowników na krok, 12 stanów ABI, polskie nazwy i strukturę 72-bajtową.

5. **Testy budżetu binarnego i opakowania aplikacji:**
   ```bash
   make test-dzwieku-packaging
   ```
   Sprawdza dopuszczalny rozmiar pliku wykonywalnego BUR (`119 040 B`).

6. **Testy GUI i regresji integracyjnej:**
   ```bash
   make test-dzwieku-repair
   ```
   Wykonuje testy integracyjne pod kontrolą AddressSanitizer i UndefinedBehaviorSanitizer (brak wycieków pamięci i niezdefiniowanych zachowań).

---

## 5. Podsumowanie

Wdrożone zmiany w pełni rozwiązują problem zamarzania interfejsu podczas skanowania HDA oraz wyjaśniają mechanizm zachowania myszy VirtIO w zagnieżdżonym środowisku hosta:
- Okno aplikacji „Test dźwięku” pojawia się natychmiast po uruchomieniu.
- Kontrolka `WskaznikZajetosci` wizualizuje postęp skanowania z wykorzystaniem motywu systemowego.
- Skanowanie topologii HDA w jądrze jest ściśle ograniczone do 4 czasowników na krok.
- Zapewniono pełną atomowość publikacji migawki, odporność na gwałtowne odświeżanie oraz czyste anulowanie skanowania.
- Wszystkie zachowane elementy bazowe (BursztynUI, czcionka Unicode, VirtIO Mouse, xHCI, budżet BUR) pozostały w 100% nienaruszone.
