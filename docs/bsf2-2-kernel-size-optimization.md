# BSF 2.2 — optymalizacja rozmiaru obrazu jądra

## Problem

Limit linkera (15 MiB dla `__kernel_end`, `jadro/arch/linker.ld`) nie został
zmieniony. Po BSF 2.2 `__kernel_end` wynosiło `0xE9F000` (zapas 388 KiB;
BSF 2.1: `0xD39000`). Pomiar sekcji pokazał, że ~9,7 MB `.data` jądra to
osadzone binaria aplikacji systemowych (22 pliki `.bin`, 7 729 584 B),
a nie kod BSF w Ring 0.

Każda aplikacja GUI linkuje statycznie BSF + BursztynUI. `nm` na `.elf`
aplikacji: ~146–155 KB symboli BSF w każdej (notatnik 40% rozmiaru,
kalkulator 59%, snake 72%, przeglądarka 19%; 10 aplikacji ≈ 1,5 MB).
Duplikacja jest więc rzeczywista, ale jej usunięcie wymaga współdzielonej
biblioteki w Ring 3 (loader BUR v2 jej nie obsługuje) — to osobny projekt.

## Odrzucone warianty

* **Aplikacje jako moduły Multiboot2.** Zainstalowany system ładuje tylko
  jądro (`narzedzia/instalator/grub-embedded.cfg`, sloty A/B
  `jadro_a/b.bin`), `instaluj_wbudowane_programy()` odtwarza aplikacje z
  blobów przy każdym starcie, a OTA (`jadro/aktualizacja`) podmienia wyłącznie
  slot jądra — aplikacje w modułach przestałyby się aktualizować.
* Usuwanie aplikacji, podnoszenie limitu, przenoszenie parserów fontów do
  Ring 0, wyłączanie weryfikacji — zabronione założeniami zadania.

## Wdrożone rozwiązanie: skompresowane bloby aplikacji

* `narzedzia/kompresuj_blob.py` — format `"BZB1" | u32 LE rozmiar | u32 LE
  CRC-32 | strumień zlib` (poziom 9), z samosprawdzeniem po kompresji.
* `Makefile` — 22 reguły `*_blob.o` linkują `X.bin.bz` zamiast `X.bin`;
  symbole `_binary_X_bin_bz_start/end`. `.gitignore`: `/*.bin.bz`.
* `jadro/rdzen/blob_skompresowany.h` (`BlobZ`) — własny dekoder DEFLATE
  (kanoniczny Huffman, tablica 9-bitowa), stały limit wyjścia 4 MiB znany z
  nagłówka, kontrola Adler-32 strumienia **i** CRC-32 całego wyniku
  (`Storage::crc32`). Każdy błąd → odrzucenie, log
  `[BSP-BLAD] Uszkodzony skompresowany blob aplikacji`, plik nie jest
  zapisywany.
* Dekoder jest **wznawialny** (`BlobZ::Strumien`, `otworz/dalej/zakoncz`):
  `dalej(z, cel)` dekoduje co najmniej do `cel` bajtów (miękki limit — bieżące
  dopasowanie ≤ 258 B albo blok stored), Adler-32 i CRC-32 liczone
  przyrostowo, `zakoncz()` akceptuje wynik dopiero po zgodności obu sum.
  Tablice Huffmana (~3,4 KiB) są w statycznym `Strumien`, nie na stosie jądra.
* `jadro/rdzen/kernel.cpp` — `zapisz_blob_bur` (zwykły start) rozpakowuje
  całość do bufora `kmalloc` (sterta jądra 16 MiB), zapisuje plik i zwalnia
  bufor. Ścieżka planowanej instalacji (`kernel_instaluj_system_step`)
  zapamiętuje źródło skompresowane (`PlanowanyPlik::koniec/skompresowany`);
  krok zapisuje porcję 4 KiB i **rozpakowuje tylko tę porcję** — pierwsza
  wersja rozpakowywała cały plik (do 700 KB) w jednym kroku, co w TCG
  wydłużyło najdłuższy blok bez oddania CPU w SYSTEMFILES do 3,7 s (wcześniej
  1,5–2,1 s); wersja porcjowa usuwa ten koszt. Ostatnia porcja wywołuje
  `zakoncz()`; błąd sumy → plik usuwany, instalacja przerwana, log
  `[BSP-BLAD]`. Bufor jest związany z indeksem pliku (ponowiony plan zawsze
  otwiera strumień od początku) i zwalniany po pliku oraz przy każdym błędzie.

Bloby są częścią zaufanego obrazu jądra (integralność jak dotąd: podpis/
suma obrazu, sloty A/B). Dane z zewnątrz nie trafiają do dekodera. Mapowania
W^X/NX nie zmieniły się — bufor jest danymi, kod aplikacji ładuje dalej
loader BUR z pliku PSF.

## Wynik

| | `__kernel_end` | zapas do 15 MiB |
|---|---|---|
| BSF 2.1 (`58131c0`) | `0xD39000` | — |
| BSF 2.2 przed (`0ef1ef7`) | `0xE9F000` | 388 KiB |
| **BSF 2.2 po** | **`0xABE000`** | **4 360 KiB** |

Odzyskano 3 972 KiB; `system_operacyjny.bin` 7,1 MB.

| blob | surowy B | `.bz` B | % |
|---|---|---|---|
| przegladarka.bin | 700592 | 368043 | 52.5% |
| test.bin | 690184 | 315196 | 45.7% |
| menedzer_okien.bin | 518312 | 225453 | 43.5% |
| kontrolki4.bin | 498368 | 226611 | 45.5% |
| eksplorator.bin | 448144 | 207318 | 46.3% |
| tapety.bin | 439296 | 212677 | 48.4% |
| demo_ui.bin | 397048 | 185879 | 46.8% |
| ekran.bin | 383888 | 176427 | 46.0% |
| kontrolki3.bin | 365368 | 174795 | 47.8% |
| diagnostyka.bin | 357840 | 169407 | 47.3% |
| test_dzwieku.bin | 357312 | 167281 | 46.8% |
| kontrolki2.bin | 350728 | 166414 | 47.4% |
| instalator.bin | 347680 | 160160 | 46.1% |
| notatnik.bin | 328792 | 159764 | 48.6% |
| shell.bin | 286808 | 140131 | 48.9% |
| aktualizacje.bin | 278048 | 131378 | 47.3% |
| kalkulator.bin | 262736 | 123926 | 47.2% |
| husarz_worker.bin | 251464 | 129324 | 51.4% |
| przegladarka_obrazow.bin | 223248 | 107994 | 48.4% |
| snake.bin | 215056 | 99515 | 46.3% |
| fpu_probe.bin | 16384 | 2427 | 14.8% |
| repro.bin | 12288 | 1542 | 12.5% |
| **razem** | **7729584** | **3651662** | **47.2%** |

## Weryfikacja

* `make test-kernel-blobz-host` (ASan+UBSan, `testy/test_blob_skompresowany.cpp`):
  wszystkie binaria aplikacji pakowane zlib i rozpakowane bajt w bajt,
  **22 rzeczywiste `*.bin.bz` linkowane do jądra rozpakowane bajt w bajt —
  jednorazowo i porcjami po 4 KiB dokładnie jak krok instalatora** (każdy
  prefiks sprawdzany po każdej porcji), wszystkie typy bloków DEFLATE
  (stored/fixed/dynamic, poziomy 0/1/6/9, strategie default/fixed/huffman/RLE)
  przy porcjach 1 B / 4 KiB / 70 001 B, obcięcia, zły rozmiar/CRC/magic,
  rozmiar > 4 MiB, 3000 losowych mutacji — wszystkie odrzucone w obu trybach
  (tryb porcjowy nigdy nie akceptuje tego, co odrzuca jednorazowy);
  3422 checks PASS.
* Natywnie (QEMU, `narzedzia/bsf2_native_qemu.py --workload bsf22`, ISO z
  blobami skompresowanymi): UEFI 1280x720 35/35, UEFI 1920x1080 35/35,
  BIOS 1280x720 35/35, bez timeoutów GPU. Czas do pulpitu UEFI 1280x720:
  11,1 s (wcześniejsze przebiegi bez kompresji: 10,8–11,3 s) — dekompresja
  ~7,7 MB przy starcie nie jest mierzalna na tle rozrzutu TCG.
  Logi: `.bsf-test/native22-blobz-{720,1080,bios}.log`.
* Po przejściu na dekoder porcjowy i zmianach CFF: ponownie UEFI 1280x720
  35/35 (`.bsf-test/native22-cffperf-720.log`), `__kernel_end` `0xABF000`.
* **Ścieżka instalatora (E2E) — niezweryfikowana do końca w tym środowisku.**
  `testy/test_bsf_installed_qemu.py` na czystym HEAD (`0ef1ef7`, worktree)
  nie przechodził: (1) GOP/GRUB podaje 1920x1080 mimo `gfxmode=1280x720`, a
  harness klikał współrzędne 1280x720 — naprawione w
  `narzedzia/instalator/qemu_install.py` (przesunięcie o offset centrowania
  odczytany z `[EKRAN INIT] boot=WxH`); (2) w każdym dzisiejszym przebiegu —
  także na czystym HEAD bez moich zmian — `[VIRTIO-BLK] ERROR: request timed
  out` w losowym etapie (GPT, ESP, SYSTEMFILES, WALLPAPERS), przy zdrowym
  `fdatasync` hosta (mediana 3 ms, maks. 8 ms). To problem środowiska/
  istniejący, nie skutek kompresji. Jeden przebieg (wersja jednorazowa
  dekodera) zakończył cały etap SYSTEMFILES (wszystkie skompresowane pliki
  zapisane) i padł dopiero na WALLPAPERS z tym samym timeoutem.
