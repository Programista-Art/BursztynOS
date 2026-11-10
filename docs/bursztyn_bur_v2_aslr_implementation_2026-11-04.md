# Bursztyn OS — Implementacja relokowalnego formatu wykonywalnego BUR v2 (`BUR\x02`) oraz ASLR dla procesów Ring 3 (2026-11-04)

> Dokument kanoniczny (identyczny z [`docs/bursztynburv2aslrimplementation2026-11-04.md`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/docs/bursztynburv2aslrimplementation2026-11-04.md)). Artefakty maszynowe znajdują się w katalogu [`docs/bur-v2-aslr-2026-11-04/`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/docs/bur-v2-aslr-2026-11-04/).

## 1. Streszczenie wykonawcze

Niniejszy dokument opisuje pełną architekturę, specyfikację binarną, implementację jądra oraz weryfikację testową relokowalnego formatu wykonywalnego **BUR v2 (`BUR\x02`)** i wieloregionowego **ASLR (Address Space Layout Randomization)** dla procesów przestrzeni użytkownika (Ring 3) w systemie **Bursztyn OS**.

Kluczowe rezultaty wdrożenia:
- **Dwuformatowy loader produkcyjny** ([`loader.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/loader.cpp), [`loader.h`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/loader.h), [`bur_v2.h`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/bur_v2.h)): pełna obsługa nowego formatu relokowalnego **BUR v2** (`BUR\x02`, nagłówek 160 B) przy zachowaniu 100% wstecznej kompatybilności z klasycznym formatem stałoadresowym **BUR v1** (`BUR\0`, nagłówek 60 B).
- **Domyślna kompilacja wszystkich 23 aplikacji Ring 3 jako PIE BUR v2** (`BUR_FORMAT ?= 2` w [`Makefile`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/Makefile#L188)) przy użyciu `-fPIE -mcmodel=small -fvisibility=hidden`, skryptu [`bur_v2_linker.ld`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/bur_v2_linker.ld) oraz konwertera [`narzedzia/elftoburv2.py`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/elftoburv2.py).
- **Kryptograficzne źródło entropii ChaCha20 CSPRNG** ([`entropia.h`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/entropia.h), [`entropia.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/entropia.cpp)) zasilane ze sprzętowych źródeł (`VirtIO-RNG`, `RDSEED`, `RDRAND`, `RDTSC`, `CMOS RTC`) z automatycznym, ciągłym dosypywaniem entropii (`re-seeding`).
- **Wieloregionowy ASLR per-proces** ([`aslr_wylosuj_uklad_procesu`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/bur_v2.h#L168-L192)): niezależna randomizacja bazy obrazu PIE (20 bitów entropii stron), sterty (20 bitów), pamięci współdzielonej SHM (20 bitów) oraz szczytu stosu (16 bitów).
- **Rygorystyczne W^X i izolacja kanarka SSP (`.ssp_guard`)**: separacja `.text` (`RX`), `.rodata` (`RO+NX`), dedykowanej strony `.ssp_guard` (`RO+NX`, inicjalizowanej przez jądro przez okno HHDM przed wejściem do Ring 3) oraz `.data`/`.bss` (`RW+NX`).
- **Symbolikacja awarii ASLR** ([`idt.cpp`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/idt.cpp#L830-L845), [`narzedzia/symbolicate_bur_v2_crash.py`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/narzedzia/symbolicate_bur_v2_crash.py)): logowanie `base=0x... rva=0x... bur=v...` przy każdym wyjątku `[USER FAULT]` oraz automatyczne tłumaczenie `RVA = RIP - load_base` na plik/linię kodu za pomocą prywatnych artefaktów `.release-private/apps/<sha256>/app.elf`.

---

## 2. Artefakty dowodowe JSON

W katalogu [`docs/bur-v2-aslr-2026-11-04/`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/docs/bur-v2-aslr-2026-11-04/) zapisano komplet maszynowych danych pomiarowych:
- [`baseline.json`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/docs/bur-v2-aslr-2026-11-04/baseline.json) oraz [`production-baseline.json`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/docs/bur-v2-aslr-2026-11-04/production-baseline.json) — stan bazowy BUR v1 przed migracją.
- [`after.json`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/docs/bur-v2-aslr-2026-11-04/after.json) — pełny inwentarz artefaktów produkcyjnych BUR v2 (`BursztynOS.iso`, `system_operacyjny.bin`, 23 aplikacje `.bin`) wraz ze skrótami SHA-256 i parametrami okien ASLR.
- [`apps-v1-vs-v2.json`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/docs/bur-v2-aslr-2026-11-04/apps-v1-vs-v2.json) — szczegółowe porównanie rozmiarów, układów segmentów (`.text`, `.rodata`, `.ssp_guard`, `.data`, `.bss`) i liczby relokacji dla wszystkich 23 aplikacji.
- [`qemu-results.json`](file:///home/programista/Pulpit/Burstzyn/BursztynOS/docs/bur-v2-aslr-2026-11-04/qemu-results.json) — wyniki testów wykonania w QEMU (26 uruchomień ASLR, symbolikacja awarii, odporność BWS oraz rozruch produkcyjnego pulpitu ISO).
