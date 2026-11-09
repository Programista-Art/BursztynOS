# Prompt na Fazę 2 — Bursztyn OS (warstwa POSIX)

Skopiuj CAŁĄ treść poniżej (od `---PROMPT---` do `---KONIEC---`) i wklej jako
pierwszą wiadomość na nowym czacie w tym samym repozytorium.

---PROMPT---

Działamy w repozytorium `C:\Users\UNITRONIX\Documents\GitHub\BursztynOS`
(Bursztyn OS — 64-bitowy system operacyjny x86-64 pisany od zera, polskie
nazewnictwo: `jadro` = kernel, `pamiec` = memory, `proces` = process).

Najpierw przeczytaj `docs/audyty/faza0_faza1_stan_2026-11-08.md`. To dokument
stanu po Fazie 0 i Fazie 1: co zmieniono, dlaczego, jak zweryfikowano oraz jaka
jest lista długu technicznego i zasad, których trzeba przestrzegać. Nie powtarzaj
pracy z Fazy 0/1.

## Cel Fazy 2

Zbudować warstwę zgodności POSIX/Linux ABI, na której da się uruchomić musl libc
i docelowo CPython. Konkretny, weryfikowalny cel końcowy tej fazy:
**programy zlinkowane z musl (`hello world`, potem `ls`, `cat`) uruchamiają się
jako binaria ELF64 w QEMU.**

To NIE jest faza kosmetyczna. Każdy punkt ma gate testowy. Jeśli gate nie
przechodzi, punkt nie jest skończony.

## Stan wyjściowy — fakty, które musisz znać

Przeczytaj kod, ale to są rzeczy ustalone w Fazie 0/1, żeby nie tracić czasu:

- **ABI wywołań BWS jest niezgodne z SysV.** `jadro/arch/syscall.S`: numer
  w R8, argumenty w R9/R10/R12/R13, wynik w RAX. SysV wymaga
  RDI/RSI/RDX/R10/R8/R9. Dlatego potrzebna jest **druga** bramka.
- **Brak loadera ELF.** Jedyne binaria to własny format `.bur`
  (`jadro/fs/loader.cpp`). Zero trafień na `Elf64`/`e_ident`.
- **Brak linkowania dynamicznego.** Żadnego `dlopen`, `PLT`/`GOT`, `.so`.
  `bursztyn_ui` jest wkompilowane statycznie do każdego `.bur`.
- **Brak libc.** Zero `errno`, brak `printf`, `malloc`, `open`, `read`
  w warstwie Ring 3 poza własnymi odpowiednikami `gui_*`.
- **Brak TLS per proces.** `IA32_FS_BASE` nigdzie nie ustawiany.
  `pthreads` i `errno` wymagają `%fs`.
- **Planista: 16 slotów, brak wątków, brak `fork`, brak SMP.**
  `jadro/bws/pzb.h` (`PZB_MAKS_PROCESOW = 16`), model to proces = jeden kontekst
  CPU. Brak struktury wątku.
- **PMM alokuje wyłącznie poniżej 1 GiB** (`jadro/mm/pmm.cpp`,
  `MAKS_ADRES_ALOKOWALNY = 0x40000000`). Strony użytkownika mogą i powinny
  przychodzić spoza tego okna.
- **VMM nie ma demand pagingu, COW, `munmap`, `mprotect`.** Mapowanie jest
  eager, alokacja ramki + mapowanie strony.
- **Sieć: jedna globalna sesja TCP.** `BWS_SOCKET_CREATE` (79) odmawia, gdy
  inna sesja jest aktywna. Brak `listen`/`accept`, brak UDP datagramów,
  brak `epoll`, brak IPv6.
- **Stos Ring 3 = 256 KiB**, baza losowana w oknie 16 MiB (ASLR z Fazy 0.4).
  `GORNA_BAZA_STOSU_USER = RING3_STACK_TOP - ROZMIAR_STOSU_USER` — jeśli
  zmieniasz rozmiar stosu, musisz to skorygować.
- **NX/SMEP/SMAP działają.** Każdy dostęp jądra do pamięci Ring 3 MUSI być
  w `OknoUzytkownika` z `jadro/mm/pamiec.h`, inaczej dostaniesz `#PF` w Ring 0.
  To dotyczy też kodu, który tymczasowo przełącza CR3.
- **Nie używaj `FizycznyZakresDoWirtualnego()` pod `BlokadaPMM`** —
  zakleszczenie na spinlocku bez reentrancji.
- **PID-y są recyklingowane.** Każda tablica per-PID musi być zerowana
  w `zwolnij_rezerwacje_procesu` (`jadro/fs/loader.cpp`).

## Zadania Fazy 2 — w tej kolejności (zależności są realne)

### 2.12 Druga bramka wejściowa `int 0x80` z ABI SysV

Dodaj obok istniejącego BWS, nie zamiast niego. BWS musi działać bez zmian —
23 aplikacje `.bur` od niego zależą.

- Nowy katalog `jadro/posix/` z tablicą dyspozycji.
- Bramka DPL3 w `jadro/arch/idt.cpp` (obecnie wszystkie bramki DPL0).
- ABI: numer w RAX, argumenty RDI/RSI/RDX/R10/R8/R9, wynik w RAX,
  błędy jako `-errno` w RAX (`-1..-4095`).
- Osobny stub asemblerowy, wzorowany na `jadro/arch/syscall.S`, ale zapisujący
  pełną ramę `RejestryStanowe` — scheduler musi umieć wywłaszczyć proces
  w trakcie syscallu POSIX tak samo jak przy BWS.
- Wspólna walidacja wskaźników: użyj `copy_from_user`/`copy_to_user`
  z `jadro/bws/bezpieczenstwo.cpp` (mają już okna SMAP).

**Gate:** host test sprawdzający, że numer/argumenty lądują w spodziewanych
rejestrach i że błąd wraca jako `-errno`. Plus `test-normal-boot` — BWS bez zmian.

### 2.13 TLS per proces (`IA32_FS_BASE`)

Bez tego nie ma `errno` ani `pthreads`.

- Dodać pole `fs_base` do `RejestryStanowe` (`jadro/bws/scheduler.h`) —
  **uwaga: to zmiana ABI z `jadro/arch/przerwania.S`, `idt.cpp`, `scheduler.cpp`
  i `loader.cpp` jednocześnie.** Istnieją `static_assert` na offsety, więc
  kompilator cię zatrzyma, jeśli pominiesz któreś miejsce.
- Zapis/odczyt `IA32_FS_BASE` (MSR `0xC0000100`) przy przełączaniu kontekstu.
- Alokacja bloku TLS w przestrzeni procesu, wskaźnik w deskryptorze procesu.
- Syscall POSIX `arch_prctl(ARCH_SET_FS, addr)`.

**Gate:** host test — dwa procesy z różnymi `fs_base`, przełączenie kontekstu
nie miesza wartości.

### 2.14 Loader ELF64

- Nowy plik `jadro/fs/elf_loader.cpp/h`. Nie ruszaj `loader.cpp` (BUR) poza
  punktem zaczepienia wyboru formatu po magic bytes.
- `PT_LOAD` (RX/RW z zachowaniem W^X z Fazy 0.1), `PT_INTERP`, `PT_DYNAMIC`,
  `PT_GNU_STACK` (NX!), obsługa BSS (zero-fill).
- PIE: relokacje `R_X86_64_RELATIVE`, `R_X86_64_GLOB_DAT`, `R_X86_64_JUMP_SLOT`,
  `R_X86_64_64`.
- Odrzucanie: zły magic, klasy/endianness ≠ ELF64/LE, `e_type` spoza
  EXEC/DYN, segmenty poza dolną połową kanoniczną, nakładające się segmenty,
  `p_memsz < p_filesz`, punkt wejścia poza segmentem wykonywalnym.
- Fuzz target `fuzz_elf_loader` w `testy/fuzz/` wzorowany na `fuzz_bur_loader`.

**Gate:** `testy/test_elf_loader.cpp` (host, ASan/UBSan, ~30 scenariuszy
pozytywnych i negatywnych) + `make test-elf-loader-host` w Makefile.

### 2.15 Linker dynamiczny

- Mapowanie `.so`, rozwiązywanie `DT_NEEDED`, `RPATH`/`RUNPATH`.
- `dlopen`/`dlsym`/`dlclose`/`dlerror`.
- `PLT`/`GOT` z lazy binding (albo `BIND_NOW` na start — prościej).
- Wersjonowanie symboli można świadomie odłożyć, ale trzeba to odnotować.

**Gate:** musl `ld.so` ładuje `libc.so` i program startuje.

### 2.16 Rdzeń libc

Zbudować minimalną libc POSIX (albo portować musl w Fazie 3 — tutaj potrzebny
jest tylko interfejs, na którym musl się oprze):

- `malloc`/`free`/`realloc` na `mmap` (nie bump allocator).
- `open`/`close`/`read`/`write`/`lseek`/`stat`/`fstat` na PSF.
- `printf`/`snprintf`/`puts`, `errno`, `setjmp`/`longjmp`, `atexit`.
- `memcpy`/`memset`/`memmove`/`strlen`/`strcmp` — jako realne implementacje,
  nie makra.

**Gate:** host test `test_libc_host` — alokacja/zwolnienie, formatowanie,
operacje na plikach przez atrapę PSF.

### 2.17 Wątki: `clone`, `futex`, `pthreads` + planista poza 16 slotów

To najtrudniejszy punkt i wymaga rozbicia planisty na **procesy** i **wątki**.

- Struktura `watek_t` w jądrze; proces = wspólna przestrzeń adresowa,
  wątki = osobne stosy Ring 0/3 i osobne `RejestryStanowe`.
- `clone(CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_THREAD|CLONE_SIGHAND|CLONE_SETTLS)`.
- `futex` (WAIT/WAKE) — potrzebny do `pthread_mutex`/`pthread_cond`.
- Podnieść limit: co najmniej 64 wątki, docelowo 256. `PZB_MAKS_PROCESOW`
  i `SKLADACZ_MAKS_WARSTW` są powiązane — sprawdź konsekwencje dla kompozytora
  (16 warstw = 16 okien).
- `pthread_create`/`join`/`mutex`/`cond`/`detach`.

**Gate:** dwa wątki inkrementują wspólny licznik pod mutexem, wynik poprawny;
`test-normal-boot` nadal zielony.

### 2.18 `mmap` plikowy, COW, demand paging, `mprotect`, `munmap`

- `mmap(MAP_PRIVATE|MAP_SHARED|MAP_ANONYMOUS|MAP_FIXED)`.
- COW dla `MAP_PRIVATE` (`fork` będzie tego wymagał).
- Demand paging: mapowanie bez ramki, `#PF` dociąga stronę z pliku
  albo zeruje. **To zmienia model VMM — alokacja ramki przenosi się
  z loadera do handlera `#PF`.**
- `mprotect` (musi respektować W^X), `munmap` ze zwolnieniem ramek.
- Licznik RSS per proces (dziś `granica_sterty` to przybliżenie).

**Gate:** `test-mmap-host` + `test-guard-page` nadal PASS (guard page musi
pozostać niezmapowana — demand paging nie może jej „naprawić").

### 2.19 Gniazda POSIX + wiele sesji TCP

- Usunąć ograniczenie jednej globalnej sesji TCP (`jadro/siec/siec.cpp`).
  Potrzebna tablica połączeń z limitem (np. 32) i kluczowaniem po 4-tuple.
- `socket`/`bind`/`listen`/`accept`/`connect`/`send`/`recv`/`shutdown`/`close`.
- UDP: `sendto`/`recvfrom` (dziś UDP jest tylko w jądrze dla DHCP/DNS).
- `select`/`poll`/`epoll` — minimum `poll` + `epoll_wait`, bo CPython ich używa.
- Nie blokować całego jądra na I/O sieciowym (dziś spin-wait z odstępowaniem).

**Gate:** `test-net-multisession-qemu` — trzy równoległe połączenia TCP.

### 2.20 PMM: pula powyżej 1 GiB

- Dodać drugą pulę alokacji (powyżej 1 GiB) dla stron użytkownika.
- Niska pula (< 1 GiB) zostaje **wyłącznie** dla DMA sterowników.
- `ZaalokujRamke()` zachowuje dotychczasowe znaczenie (niska pula, dla sterowników).
  Nowe `ZaalokujRamkeUzytkownika()` sięga najpierw do wysokiej puli.

**Gate:** host test PMM — alokacja użytkownika nie schodzi do niskiej puli;
`test-virtio-*-qemu` nadal PASS (sterowniki dalej dostają adresy < 1 GiB).

### 2.21 SMP — MADT, AP bringup, per-CPU, spinlocki

Ostatni punkt i największy. Chromium bez tego nie ruszy.

- Parsowanie MADT (`sterowniki/platforma/acpi.cpp` — dziś czyta tylko FACP i HPET).
- AP bringup: `INIT`/`STARTUP` IPI, trampolina 16-bitowa, przejście do Long Mode.
- Struktury per-CPU: osobny `TSS.rsp0`, osobny stos jądra, `GS_BASE`.
- Spinlocki dla PMM/VMM/PSF (dziś `BlokadaPMM` to globalny spinlock + `cli`,
  co na SMP nie wystarczy).
- Kolejka runnable per rdzeń, balansowanie.
- Kompozytor i scheduler mają jawne założenia jednordzeniowe
  (komentarze w `skladacz_obrazu.h`, `fpu.cpp`, `tss.cpp`) — trzeba je zrewidować.

**Gate:** boot na `-smp 4` przechodzi, `test-normal-boot` PASS, brak
zakleszczeń w `test-bws-abuse-qemu`.

## Czego NIE robić w tej fazie

- Nie ruszać Husarza ani aplikacji `.bur` — mają działać na BWS bez zmian.
- Nie zmieniać formatu `.bur`.
- Nie dodawać Chromium — to Faza 4 i wymaga ukończonej Fazy 2 + SMP + ≥4 GiB RAM.
- Nie robić kosmetyki w `docs/` poza dopisaniem nowych dokumentów audytowych.
- Nie refaktoryzować `loader.cpp` „przy okazji".

## Zanim zaczniesz — rozwiąż dwa blokery

1. **`jadro/aktualizacja/aktualizacja_klucz.h` jest w `.gitignore`, ale jest
   wymagany do buildu.** Świeży klon się nie zbuduje. Sprawdź, czy plik istnieje
   lokalnie; jeśli nie, wygeneruj klucz testowy w Makefile z jasnym komunikatem,
   że to nie jest klucz release. To zadanie na 15 minut i odblokowuje wszystko.

2. **Znana regresja kursora QEMU** (`Kursor widoczny w (500,300)` FAIL) jest
   wcześniejsza — potwierdzona na baseline `3409e60`. Opis w
   `docs/audyty/ui_stability_modernization_inventory_2026-11-07.md`. Nie naprawiaj
   jej w tej fazie, chyba że stanie ci na drodze; tylko nie traktuj jej jako
   swojej regresji.

## Metodyka pracy

- **Build:** `make -j4 iso` w WSL (toolchain `x86_64-linux-gnu-*`).
  QEMU uruchamiaj **tylko natywnie na Windows** (`qemu-system-x86_64.exe`),
  nigdy przez WSL — to zasada projektu.
- **Po każdym punkcie:** `make test-smoke`, a przy zmianach VMM/BWS dodatkowo
  `test_bws_abuse_qemu.py`, `test_guard_page.py`, `run_loader_regression.py`.
- **Jeden punkt = jedna weryfikowalna zmiana.** Jeśli gate nie przechodzi,
  punkt nie jest skończony — nie przechodź dalej.
- **Sprzątaj artefakty testowe** przed podsumowaniem:
  `git checkout -- .hda-test .virtiotest`, żeby diff zawierał tylko źródła.
- **Przy każdej zmianie ABI** (`RejestryStanowe`, `proces_t`) sprawdź, gdzie
  są `static_assert` na offsety — kompilator wskaże wszystkie miejsca do zmiany.
- **Nie zgaduj** — jeśli nie wiesz, jak coś działa, przeczytaj kod i dokument
  stanu. `docs/` w części jest nieaktualne (patrz sekcja 6 dokumentu stanu).

## Raportowanie

Po zakończeniu każdego punktu podaj: co zmieniono (pliki), jaki gate przeszedł,
jaki był wynik liczbowy (iteracje, asercje, czasy bootu). Na końcu fazy —
zaktualizuj dokument stanu w `docs/audyty/` i podaj listę nowego długu.

Zacznij od przeczytania `docs/audyty/faza0_faza1_stan_2026-11-08.md`
i `jadro/arch/syscall.S`, potem zaproponuj plan punktu 2.12 zanim go napiszesz.

---KONIEC---
