# 12. Lokalna weryfikacja (bez GitHuba)

Mapa lokalnych gate’ów bezpieczeństwa i regresji Bursztyn OS. Nie wymaga GitHub Actions — wystarczy Linux/WSL z cross-toolchainem `x86_64-linux-gnu-*`, `make`, `python3` i QEMU.

Źródła jądra leżą w `jadro/` (wariant B); mapa katalogów: [00_mapa_repozytorium.md](00_mapa_repozytorium.md). Po przenosinach źródeł: `make clean` przed pierwszym `make kernel` / gate’ami (stale `*.d`).

Szczegóły hardeningu (SSP, fuzz, ASLR): [bursztyn_security_hardening_stack_fuzz_2026-11-04.md](audyty/bursztyn_security_hardening_stack_fuzz_2026-11-04.md).

---

## Co odpalić kiedy

| Moment | Cel | Sens |
|--------|-----|------|
| Przed commitem | `make test` lub `make test-smoke` | Szybki gate: SSP host, ASLR host, fuzz smoke, VirtIO PCI, motywy unit, WM click, normal boot, loader ELF64 (host + QEMU) |
| Przed merge / większą zmianą ABI | `make test-security` | Security host + QEMU SSP + BWS abuse + guard-page |
| Audyt stosu jądra (ciężki) | `make test-security STACK_AUDIT=1` | j.w. + `audit-stos-jadra` |
| Przed releasem | `make test-regression` | VirtIO QEMU, OTA, instalator, Husarz, input, HDA |
| Nocą | `make test-nightly` | security + regression + `fuzz-extended` |

Alias wsteczny: `make test-security-hardening` → to samo co `make test-security`.

---

## Orkiestrator z raportem JSON

```bash
make test-local-suite MODE=smoke
make test-local-suite MODE=security
make test-local-suite MODE=regression
make test-local-suite MODE=nightly

# albo bezpośrednio:
python3 narzedzia/run_local_suite.py --mode=smoke
python3 narzedzia/run_local_suite.py --mode=nightly --continue-on-error
python3 narzedzia/run_local_suite.py --mode=security --stack-audit
python3 narzedzia/run_local_suite.py --mode=smoke --dry-run
```

Raporty trafiają do `.local-ci/` (katalog w `.gitignore`):

- `raport-<mode>-<UTC>.json` — pełny przebieg
- `raport-<mode>-latest.json` — ostatni wynik danej warstwy
- `logs/<cel>.log` — stdout/stderr poszczególnych jobów

Kod wyjścia ≠ 0, gdy którykolwiek job ma status `fail` lub `timeout`.

- **smoke / security:** stop na pierwszym błędzie (domyślnie).
- **regression / nightly:** kontynuacja po błędzie (zbiorczy raport); `--continue-on-error` wymusza to także dla smoke/security.

---

## Kontrakt Python/POSIX i macierz gościa

Szybki, hostowy kontrakt nie pobiera sieci, nie uruchamia QEMU i nie buduje
CPythona:

```bash
make test-python-contract
make test-cpython-manifest
make test-musl-sysroot-contract
```

`test-python-contract` uruchamia walidację manifestu CPython, parser ELF64,
parser metadanych/stagingu sysrootu musl oraz hostowy test POSIX ABI. Brak
staged sysrootu jest jawnie raportowany jako `SKIP`; uszkodzony, istniejący
sysroot kończy się `FAIL`. `make cpython-build` pozostaje wyłącznie jawnym
targetem budującym i nie jest częścią smoke.

Cele macierzy są rozdzielone, aby brak funkcji nie udawał sukcesu:

```bash
make test-musl-guest-qemu
make test-python-hello-qemu
make test-python-stdlib-import-qemu
make test-python-threading-qemu
make test-python-subprocess-qemu
make test-python-socket-asyncio-qemu
make test-python-pip-pure-qemu
make test-python-c-extension-qemu

# warstwy:
make test-python-posix
make test-python-security
make test-python-regression
```

`test-musl-guest-qemu` i `test-python-hello-qemu` zgłaszają `SKIP` tylko przy
braku jasno wykrytej zależności (np. sysrootu, staged runtime, natywnego
`qemu-system-x86_64.exe` lub OVMF). Awaria budowy, niepoprawny ELF, niepełny
runtime albo brak oczekiwanego markera gościa to `FAIL`. CPython guest gate
oczekuje przygotowanego obrazu przez `CPYTHON_GUEST_ISO`; bez niego raportuje
brak integracji stagingu, a nie PASS.

Obecne wiersze `threading`, `subprocess`, `socket/asyncio`, `pip` i C
extension są jawnie `SKIP`, ponieważ manifest wyłącza wymagane jeszcze
pthread/process/event facilities, `ensurepip`/package manager oraz
`ld.so`/`dlopen`. Nie oznacza to obsługi tych funkcji.

---

## Warstwy (cele Makefile)

### `test-smoke` / `test`

1. `test-python-contract` — szybki manifest/ELF/sysroot/POSIX ABI
2. `test-stack-protector-host`
3. `test-bur-v2-aslr-host`
4. `test-fuzz-smoke`
5. `test-virtio-pci`
6. `test-motywy-unit`
7. `test-wm-click-routing`
8. `test-normal-boot`
9. `test-startup-visibility`
10. `test-compositor-cursor`
11. `test-powitanie`
12. `test-elf-loader-host` — walidacja i opis ELF64 (W^X, BSS, `elf_typ`)
13. `test-elf-exec-qemu` — ELF64 ET_EXEC uruchomiony w prawdziwym Ring 3
14. `test-vfs-host` — tablica deskryptorów: izolacja PID, limity, flagi, ABI `stat`

### `test-security`

1. `test-security-host` (= SSP host + ASLR host + fuzz smoke)
2. `test-stack-protector-qemu`
3. `test-bws-abuse-qemu`
4. `test-guard-page`
5. opcjonalnie `audit-stos-jadra` przy `STACK_AUDIT=1`

`fuzz-extended` **nie** wchodzi do security — należy do `test-nightly`.

### `test-regression`

VirtIO QEMU (`test-virtio-*-qemu`), `test-ota`, `installer-test`, `test-husarz-engine-host`, `test-input-coexistence-host`, `test-hda-regression-qemu`.

### `test-nightly`

`test-security` + `test-regression` + `fuzz-extended`.

### Warstwy Python

`test-python-security` obejmuje kontrakt hostowy i istniejący gate musl guest.
`test-python-posix` obejmuje hello oraz osobne wiersze stdlib/import,
threading, subprocess i socket/asyncio. `test-python-regression` dodaje
wiersze pip pure-Python i C extension; niewdrożone możliwości pozostają
jawnie `SKIP`.

---

## Wymagania środowiska

- `make`, `python3`
- Cross: `x86_64-linux-gnu-gcc/g++/as/ld` (jak w `Makefile`)
- natywny Windows QEMU (`qemu-system-x86_64.exe`) dla celów `*-qemu` i boot
- Opcjonalnie OVMF dla ścieżek UEFI poza tym dokumentem

WSL/Linux może służyć do builda i testów hostowych; nie uruchamiaj z niego
QEMU. Okno gościa uruchamiaj wyłącznie przez natywne Windows QEMU.

---

## Powiązane narzędzia

| Ścieżka | Rola |
|---------|------|
| [narzedzia/run_local_suite.py](../narzedzia/run_local_suite.py) | Orkiestrator warstw + JSON |
| [narzedzia/run_fuzz_suite.py](../narzedzia/run_fuzz_suite.py) | Fuzz smoke / extended |
| [testy/fuzz/](../testy/fuzz/) | Cele fuzz (BWS, BUR, BSP2) |
| `make help` | Lista celów w tym sekcja lokalnej weryfikacji |
