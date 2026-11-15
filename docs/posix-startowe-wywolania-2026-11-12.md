# Bramka SysV: wywołania startowe libc — etap 1.3 (2026-11-12)

Trzeci krok planu. Bramka ma teraz komplet wywołań, których `crt1` musl/glibc
domaga się **przed** `main()`. Wcześniej brak `set_tid_address`,
`set_robust_list` i `prlimit64` przerywał start programu libc, więc `hello
world` z musl nie miało szans się uruchomić niezależnie od loadera ELF.

## 1. Co dokładnie brakowało

| Wywołanie | Numer | Skutek braku |
|---|---|---|
| `set_tid_address` | 218 | musl woła bezwarunkowo w `crt1`; `-ENOSYS` przerywa start |
| `set_robust_list` | 273 | rejestracja listy robust futexów |
| `prlimit64` | 302 | alokator musl sprawdza `RLIMIT_AS` |
| `sched_getaffinity` | 204 | aplikacje wielowątkowe decydują o liczbie wątków |
| `getrlimit` | 97 | starsze libc zamiast `prlimit64` |
| `sysinfo` | 99 | część narzędzi sprawdza RAM przed alokacją |
| `access` | 21 | `crt1`/shell sprawdza istnienie pliku |

Dodatkowo `brk` zwracał `-ENOMEM` przy każdej próbie rozszerzenia sterty —
malloc nie mógł działać.

## 2. Semantyka: prawda zamiast zmyslonych wartości

Każde wywołanie zwraca **rzeczywistą** wartość z podsystemu, nie atrapę:

| Wywołanie | Zwraca | Źródło prawdy |
|---|---|---|
| `set_tid_address` | PID procesu | `aktualny_pid` |
| `set_robust_list` | `0` | rejestracja przyjęta; brak wątków = brak czego odzyskiwać |
| `prlimit64(RLIMIT_NOFILE)` | `64/64` | `VFS_MAKS_DESKRYPTOROW` |
| `prlimit64(RLIMIT_STACK)` | `2 MiB` | `LICZBA_STRON_STOSU_ELF` |
| `prlimit64(RLIMIT_NPROC)` | `MAKS_PROCESOW` | limit schedulera |
| `prlimit64(RLIMIT_AS/DATA/RSS)` | `RLIM_INFINITY` | brak limitu w VMM |
| `sched_getaffinity` | maska `1` | planista jednordzeniowy |
| `sysinfo` | `freeram` z PMM | `pmm_pobierz_wolne_ramki()` |
| `access` | istnienie w PSF | `vfs_jadro_istnieje()` |
| `brk` | nowa granica sterty | `mmap_jadro_sterta_rozszerz()` |

**Świadome odstępstwa** (jawne, nie ciche):

- `prlimit64` z **niezerowym** nowym limitem zwraca `-ENOSYS`. Ograniczenie
  zasobu nie jest implementowane; przyjęcie wartości i niestosowanie jej
  byłoby kłamstwem. `prlimit64(nowy=0)` (zapytanie o stary) działa.
- `prlimit64` dla nieznanego zasobu zwraca `-EINVAL`, a nie
  „nieskończoność". Nieznany zasób to nie to samo co nielimitowany.
- `brk` nie zwalnia stron przy zmniejszeniu. VMM nie ma API do odmapowania
  pojedynczej strony w przestrzeni procesu, a zwolnienie ramki bez usunięcia
  PTE daje UAF. Pamięć zostaje zmapowana do końca życia procesu — bezpieczne,
  kosztuje RAM, jawnie udokumentowane.

## 3. `brk`: prawdziwe mapowanie stron

`mmap_jadro_sterta_rozszerz()` w [jadro/mm/mmap_jadro.cpp](../jadro/mm/mmap_jadro.cpp):

1. Waliduje wyrównanie do strony i okno sterty (`sterta_baza..sterta_limit`
   z `uklady_procesow[pid]`).
2. Przy rozszerzeniu mapuje każdą nową stronę przez `zmapuj_strone()`
   (`PROT_READ|PROT_WRITE`, model eager jak `mmap`).
3. Przy braku ramki zatrzymuje się na ostatniej poprawnie zmapowanej
   pozycji i zwraca 0 — proces nie dostaje „pustej" pamięci wyglądającej
   jak dostępna.
4. Przy zmniejszeniu tylko przesuwa granicę (patrz odstępstwo wyżej).

`brk` w [jadro/posix/posix_jadro.cpp](../jadro/posix/posix_jadro.cpp)
odrzuca granicę niewyrównaną do strony (`-EINVAL`) zamiast wyrównywać ją po
cichu: aplikacja musi wiedzieć, że dostała dokładnie to, o co prosiła.

## 4. Telemetria `-ENOSYS` per numer

[`PosixDiagnostyka`](../jadro/posix/posix_dyspozytor.h) ma teraz dwie tablice
512 wpisów:

```cpp
uint32_t liczba_wywolan_per_numer[POSIX_TELEMETRIA_MAKS];
uint32_t liczba_enosys_per_numer[POSIX_TELEMETRIA_MAKS];
```

Powód: „musl nie startuje" bez telemetrii znaczy „zgaduj, którego wywołania
brakuje". Licznik per numer zamienia to na konkretną listę. Numer spoza
zakresu jest **pomijany**, a nie liczony pod indeksem 0 — indeks 0 to `read`,
więc mylenie ich fałszowałoby raport (test to sprawdza).

Koszt: 4 KiB w BSS, zero alokacji na ścieżce syscallu.

## 5. Weryfikacja

| Gate | Wynik |
|---|---|
| `make kernel` | PASS |
| `test-posix-abi-host` | **PASS 200 asercji** (było 131) |
| `test-elf-exec-qemu` | **PASS 9/9 kategorii w Ring 3** (było 8) |
| `test-posix-gate-qemu` | PASS 8/8 (brak regresji) |
| `test-bws-abuse-qemu` | PASS 6/6 (brak regresji) |
| `test-vfs-host` | PASS 116 asercji |
| `test-elf-loader-host` | PASS 1138 asercji |
| `test-smoke` (pełny) | PASS |

### 5.1 Nowa kategoria 9 w QEMU

Sprawdza w prawdziwym Ring 3, nie tylko kod wyjścia:

- `set_tid_address` zwraca PID w zakresie `1..63`,
- `set_robust_list` zwraca `0`,
- `prlimit64(RLIMIT_NOFILE)` zwraca `64/64`,
- `prlimit64(RLIMIT_AS)` zwraca `RLIM_INFINITY`,
- `prlimit64(zły zasób)` zwraca `-EINVAL`,
- `sched_getaffinity` zwraca maskę dokładnie `1` (jeden bit),
- `sysinfo` ma niezerowe `totalram`/`freeram` i `mem_unit == 1`,
- `access(istniejący)` → `0`, `access(nieistniejący)` → `-ENOENT`,
- `brk(0)` zwraca granicę, `brk(0+8192)` rozszerza, **zapis do nowej strony
  działa**, `brk(+1)` → `-EINVAL`.

Ostatni punkt jest najważniejszy: dowodzi, że `brk` realnie mapuje pamięć,
a nie tylko przesuwa licznik.

### 5.2 Błąd znaleziony w teście

Pierwsze uruchomienie nie skompilowało się: `prlimit64` ma **cztery**
argumenty, a mój `sys3` przyjmował trzy. Czwarty argument ABI SysV idzie do
`R10`, nie do `RCX` — dodałem osobną funkcję `sys4` z jawnym `r10` w clobber
liście. To ta sama klasa błędu, którą wcześniej popełniłem w `sys3` (RDX vs
R10), więc warto ją zapamiętać: **każdy test bramki SysV musi mieć osobną
funkcję na każdą liczbę argumentów**, inaczej kompilator przyjmie wywołanie,
a jądro dostanie śmieci.

## 6. Czego świadomie NIE ma

| Brak | Powód |
|---|---|
| `rt_sigaction` (obsługa sygnałów) | wymaga ram sygnałowych i `sigreturn`; wyjątki C++ są osobnym punktem |
| `ioctl` | zwraca `-ENOTTY`: PSF nie ma urządzeń sterowanych `ioctl` |
| `openat`/`newfstatat` | numery zarezerwowane, ale brak usług: musl używa ich dopiero gdy `open`/`stat` zawiodą; dodanie ich teraz bez `AT_FDCWD` i deskryptorów katalogów dałoby pozorną zgodność |
| `poll` | wymaga blokowania procesu — czeka na wątki (etap 4) |
| `writev` | wymaga iovec i wielu kopii; prosta pętla `write` wystarcza musl na start |
| `madvise` | VMM nie ma demand pagingu, więc `MADV_DONTNEED` nie miałoby znaczenia |
| `prctl` | brak realnych ustawień do raportowania |

Każdy z tych numerów jest zadeklarowany w `posix_abi.h` i ma nazwę w
`posix_nazwa_wywolania`, więc telemetria pokaże, którego libc faktycznie
zacznie wołać. To jest właściwa kolejność: brak najpierw **widać**, potem
się go uzupełnia.

## 7. Kolejny krok

Punkt 1.4: większe okno `mmap` (4 GiB) i druga pula PMM powyżej 1 GiB.
Obecne 256 MiB okna i 16 regionów wystarczy `hello world`, ale nie CPythonowi,
który mapuje dziesiątki megabajtów na stertę i biblioteki.
