# Naprawa konsolidacji i kompilacji aplikacji Tapety BUR (2026-10-13)

## 1. Geneza problemu i analiza kryminalistyczna (Root Cause Analysis)

### 1.1. Objawy błędu
Podczas wykonywania polecenia `make runuefi` po pomyślnym zbudowaniu `instalator_blob.o` oraz `ekran_blob.o`, konsolidator przerwał budowanie na etapie aplikacji Tapety:
```text
x86_64-linux-gnu-ld --gc-sections -T programy/tapety/tapety_linker.ld -nostdlib -no-pie --gc-sections tapety_tmp.o btp_ring3.o image_ring3.o bursztyn_gui.o -o tapety.elf
x86_64-linux-gnu-ld: KONTROLKI3 BUR: brak _start
x86_64-linux-gnu-ld: KONTROLKI3 BUR: brak naglowka
x86_64-linux-gnu-ld: warning: cannot find entry symbol _start; defaulting to 0000000001001000
make: *** [Makefile:1496: tapety_blob.o] Error 1
```

### 1.2. Prawdziwa przyczyna źródłowa
1. **Pusty plik obiektowy `tapety_tmp.o`**:
   - Bezpośrednią przyczyną zgłoszenia brakujących symboli `_start` oraz `naglowek` był fakt, że plik `tapety_tmp.o` na dysku miał rozmiar dokładnie **0 bajtów** (`-rw-rw-r-- 1 programista programista 0 2026-10-12 11:38:05 tapety_tmp.o`).
   - Plik ten powstał podczas przerwanej procedury kompilacji / dotknięcia (`make -t` / przerwanie procesu kompilacji sygnałem SIGKILL przy wcześniejszych sesjach diagnostycznych).
   - Ponieważ plik `tapety_tmp.o` istniał na dysku i posiadał znacznik czasu nowszy niż plik źródłowy `programy/tapety/tapety.cpp` (`mtime` 2026-10-12 11:38 > 2026-09-28 21:53), inkrementalny mechanizm GNU Make traktował `tapety_tmp.o` jako aktualny (`make: 'tapety_tmp.o' jest aktualne`).
   - Make pominął ponowną kompilację `programy/tapety/tapety.cpp` i bezpośrednio wywołał konsolidator `ld` z pustym obiektem `tapety_tmp.o`.
   - Z racji zerowego rozmiaru `tapety_tmp.o`, konsolidator nie odnalazł w nim żadnych symboli, co uaktywniło asercje `ASSERT(DEFINED(_start))` i `ASSERT(DEFINED(naglowek))`.

2. **Stan symbolu `_start` w kodzie źródłowym**:
   - `_start` **nie brakowało** w kodzie źródłowym `programy/tapety/tapety.cpp` (zdefiniowany w wierszu 593: `extern "C" __attribute__((noreturn)) void _start() { ... }`).
   - Symbol nie został usunięty przez mechanizm `--gc-sections`, ponieważ skrypt linkera jawnie zawiera dyrektywę `KEEP(*(.text._start)) KEEP(*(.text._start.*))`, a kompilator z opcją `-ffunction-sections` umieszcza `_start` w sekcji `.text._start`.
   - Żaden obiekt startowy nie został pominięty w liście obiektów (`tapety_tmp.o` sam zawiera punkt wejścia).

3. **Stan nagłówka BUR (`naglowek`)**:
   - Nagłówek **nie brakował** w kodzie źródłowym `programy/tapety/tapety.cpp` (zdefiniowany w wierszu 16: `__attribute__((section(".naglowek"), used)) NaglowekBur naglowek = { ... };`).
   - Nie doszło do rozbieżności nazw sekcji – zarówno kod, jak i skrypt linkera używają sekcji `.naglowek`.

4. **Nieaktualne etykiety asercji w `tapety_linker.ld`**:
   - Skrypt `programy/tapety/tapety_linker.ld` został pierwotnie skopiowany ze skryptu `programy/kontrolki3/kontrolki3_linker.ld`.
   - Choć limity pamięci zostały w nim zaktualizowane do parametrów Tapet (`BUR_TEXT_SIZE = 0x40000`, `BUR_DATA_OFFSET = 0x41000`), wszystkie komunikaty `ASSERT` zawierały przedrostek `KONTROLKI3 BUR:`, a asercja rozmiaru sekcji kodu meldowała nieprawidłowy limit: `TEXT przekroczyl 192 KiB` zamiast `256 KiB`.

5. **Odporność grafu zależności Make na puste pliki**:
   - Reguła `tapety_tmp.o` w Makefile nie weryfikowała, czy plik wyjściowy ma rozmiar większy niż 0.
   - Reguła `tapety_blob.o` nie sprawdzała, czy `tapety_tmp.o` nie jest uszkodzony/pusty przed przekazaniem go do `ld`.

---

## 2. Zastosowane naprawy produkcyjne

### 2.1. Skrypt linkera (`programy/tapety/tapety_linker.ld`)
- Zaktualizowano nagłówek pliku: `/* Tapety: 256 KiB TEXT, 128 KiB DATA. */`.
- Zastąpiono wszystkie prefiksy diagnostyczne `KONTROLKI3 BUR:` prawidłowymi `TAPETY BUR:`.
- Poprawiono asercję limitu kodu: `ASSERT(__bur_text_used_end <= BUR_TEXT_END, "TAPETY BUR: TEXT przekroczyl 256 KiB")`.

### 2.2. Reguły budowania w `Makefile`
- W regule `tapety_tmp.o` dodano twardy test niepustego pliku:
  ```makefile
  tapety_tmp.o: programy/tapety/tapety.cpp programy/tapety/klient.h programy/tapety/miniatury.h tapety_format.h obrazy/skalowanie.h obrazy/dekoder_obrazu.h narzedzia/tapety-btp/tapetybtp.h $(wildcard bursztyn_ui/*.h)
  	$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c $< -o $@
  	@test -s $@ || { rm -f $@; echo "[BLAD] tapety_tmp.o jest pusty"; exit 1; }
  ```
- W regule `tapety_blob.o` dodano automatyczne wykrywanie uszkodzonego lub zerowego obiektu z natychmiastową rekompilacją, a także walidację rozmiaru i formatu `tapety.bin`:
  ```makefile
  tapety_blob.o: tapety_tmp.o btp_ring3.o image_ring3.o bursztyn_gui.o programy/tapety/tapety_linker.ld narzedzia/test_tapety_size.py
  	@if [ ! -s tapety_tmp.o ]; then \
  		echo "[TAPETY BUILD] tapety_tmp.o jest pusty lub uszkodzony - ponowna kompilacja..."; \
  		$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c programy/tapety/tapety.cpp -o tapety_tmp.o; \
  	fi
  	$(LD) --gc-sections -T programy/tapety/tapety_linker.ld $(RING3_LDFLAGS) tapety_tmp.o btp_ring3.o image_ring3.o bursztyn_gui.o -o tapety.elf
  	$(OBJCOPY) -O binary tapety.elf tapety.bin
  	$(PYTHON) narzedzia/test_tapety_size.py tapety.bin
  	$(LD) -r -b binary tapety.bin -o $@
  ```

### 2.3. Narzędzie weryfikacji budżetu i nagłówka (`narzedzia/test_tapety_size.py`)
- Zaimplementowano skrypt sprawdzający:
  - Rozmiar pliku `tapety.bin` na dysku w granicach budżetu $\le 512$ KiB (`APP_MAXIMUM`).
  - Magię nagłówka `BUR\0`.
  - Wyrównanie wszystkich offsetów i rozmiarów segmentów do granic stron (4096 B).
  - Prawidłowe umiejscowienie punktu wejścia wewnątrz segmentu tekstu (`0x1001000`).
  - Ciągłość segmentów i zgodność adresów wirtualnych.

### 2.4. Zestaw testów regresyjnych (`narzedzia/test_tapety_regression.py`)
- Zaimplementowano kompleksowy zestaw 41 testów hosta weryfikujący:
  - Brak przestarzałych etykiet `KONTROLKI3`.
  - Istnienie i eksport symboli `_start` i `naglowek`.
  - Poprawność grafu zależności w `Makefile`.
  - Odrzucanie brakującego punktu wejścia (`TAPETY BUR: brak _start`).
  - Odrzucanie brakującego nagłówka (`TAPETY BUR: brak naglowka`).
  - Odrzucanie kodu przekraczającego 256 KiB.
  - Odrzucanie pliku przekraczającego budżet 512 KiB.
  - Bezpieczeństwo i determinizm równoległego budowania (`make -j2`).

---

## 3. Szczegóły techniczne i dowody binarne

### 3.1. Polecenie konsolidacji Tapety
- **Przed**:
  ```bash
  x86_64-linux-gnu-ld --gc-sections -T programy/tapety/tapety_linker.ld $(RING3_LDFLAGS) tapety_tmp.o btp_ring3.o image_ring3.o bursztyn_gui.o -o tapety.elf
  ```
  *(kończyło się błędem z powodu 0-bajtowego `tapety_tmp.o` oraz zawierało błędne komunikaty asercji)*
- **Po**:
  ```bash
  x86_64-linux-gnu-ld --gc-sections -T programy/tapety/tapety_linker.ld $(RING3_LDFLAGS) tapety_tmp.o btp_ring3.o image_ring3.o bursztyn_gui.o -o tapety.elf
  ```
  *(poprzedzone walidacją niepustości `tapety_tmp.o` i zasilone zaktualizowanym skryptem linkera)*

### 3.2. Weryfikacja symboli i punktu wejścia `tapety.elf`
```text
ELF Header:
  Class:                             ELF64
  Data:                              2's complement, little endian
  Type:                              EXEC (Executable file)
  Entry point address:               0x1001000

Program Headers:
  Type           Offset             VirtAddr           PhysAddr           FileSiz            MemSiz              Flags  Align
  LOAD           0x0000000000001000 0x0000000001000000 0x0000000000000000 0x0000000000001000 0x0000000000001000  R      0x1000
  LOAD           0x0000000000002000 0x0000000001001000 0x0000000000001000 0x0000000000040000 0x0000000000040000  R E    0x1000
  LOAD           0x0000000000042000 0x0000000001041000 0x0000000000041000 0x0000000000000120 0x0000000000008a20  RW     0x1000

Symbole:
  380: 0000000001000000    60 OBJECT  GLOBAL DEFAULT    1 naglowek
  550: 0000000001001000  6846 FUNC    GLOBAL DEFAULT    2 _start
```

### 3.3. Weryfikacja binarnego nagłówka `tapety.bin`
```text
magic: b'BUR\x00'
entry: 0x1001000
toff:  0x1000, tsz: 0x40000 (256 KiB), tva: 0x1001000
doff:  0x41000, dsz: 0x20000 (128 KiB), dva: 0x1041000
Rozmiar pliku tapety.bin na dysku: 266528 bajtów (budżet: 524288 B, margines: 257760 B)
```

---

## 4. Wyniki wykonanych testów automatycznych

1. **Zestaw regresyjny Tapety BUR (`narzedzia/test_tapety_regression.py`)**:
   - **41 testów wykonanych, 0 błędów (41 PASS, 0 FAIL)**.
   - W tym: audyt skryptu linkera, brak etykiet KONTROLKI3, weryfikacja symboli, poprawność nagłówka BUR, testy negatywne (brak `_start`, brak `naglowek`, przekroczenie limitu kodu, przekroczenie budżetu pliku), determinizm budowania równoległego `-j2`.

2. **Zestaw testów loadera BUR (`narzedzia/test_bur_loader.py`)**:
   - **17 testów formatu, 35 testów jednostkowych C++: 100% PASS**.

3. **Zestaw testów naprawczych i diagnostycznych loadera (`narzedzia/test_dzwieku_repair.py`)**:
   - Pakowanie i budżet binarza: 63 pass, 0 fail.
   - Testy ładowarki BUR pod ASan + UBSan: 30 scenariuszy, 4 701 423 asercje PASS.
   - Testy GUI/font/raport pod ASan + UBSan: 29 scenariuszy, 2 662 asercje PASS.
   - BWS i transport HDA pod ASan + UBSan: 8 scenariuszy, 2 010 asercji PASS.

4. **Audyt parzystości launcherów (`narzedzia/test_launcher_parity.py`)**:
   - **8/8 checks PASS**.

5. **Weryfikacja ścieżki rozruchu UEFI (`make -n runuefi`)**:
   - Graf zależności Make został pomyślnie rozwiązany bez zatrzymywania się na `tapety_blob.o`.
   - Generuje pełne, prawidłowe polecenie uruchomienia QEMU z zachowaniem konfiguracji urządzeń PCI oraz parametru `QEMU_DISPLAY_ARGS`.

---

## 5. Zmienione pliki

1. [`programy/tapety/tapety_linker.ld`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/programy/tapety/tapety_linker.ld) – uaktualnienie etykiet diagnostycznych z `KONTROLKI3 BUR:` na `TAPETY BUR:` oraz poprawienie limitu TEXT do 256 KiB.
2. [`Makefile`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/Makefile) – dodanie zabezpieczenia przed pustym `tapety_tmp.o`, wpięcie walidacji rozmiaru `test_tapety_size.py` oraz dodanie celu `.PHONY: test-tapety-bur`.
3. [`narzedzia/test_tapety_size.py`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/test_tapety_size.py) – nowe narzędzie walidacji budżetu i nagłówka BUR dla `tapety.bin`.
4. [`narzedzia/test_tapety_regression.py`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/test_tapety_regression.py) – kompletny zestaw testów regresyjnych hosta dla aplikacji Tapety.
5. [`docs/tapety_bur_link_repair_2026-10-13.md`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/docs/tapety_bur_link_repair_2026-10-13.md) – niniejszy raport dokumentacyjny.

---

## 6. Wynik kompilacji produkcyjnej i suma kontrolna ISO

Wykonano czystą kompilację całego systemu bez uruchamiania maszyn wirtualnych:
```bash
make -B -j2 all iso
```
Kompilacja zakończyła się pełnym sukcesem (`exit code 0`).

**Suma kontrolna SHA256 gotowego obrazu ISO**:
```text
763647b76d356e960163ad2592343fd787486e7f2bc06cbea6be592a977bf116  BursztynOS.iso
```

> **Uwaga**: Zgodnie z wytycznymi, emulatory QEMU ani VirtualBox nie były uruchamiane w środowisku agenta. Manualna akceptacja uruchomieniowa pozostaje do dyspozycji użytkownika.
