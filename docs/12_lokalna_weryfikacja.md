# 12. Lokalna weryfikacja (bez GitHuba)

Mapa lokalnych gate’ów bezpieczeństwa i regresji Bursztyn OS. Nie wymaga GitHub Actions — wystarczy Linux/WSL z cross-toolchainem `x86_64-linux-gnu-*`, `make`, `python3` i QEMU.

Szczegóły hardeningu (SSP, fuzz, ASLR): [bursztyn_security_hardening_stack_fuzz_2026-11-04.md](bursztyn_security_hardening_stack_fuzz_2026-11-04.md).

---

## Co odpalić kiedy

| Moment | Cel | Sens |
|--------|-----|------|
| Przed commitem | `make test` lub `make test-smoke` | Szybki gate: SSP host, ASLR host, fuzz smoke, VirtIO PCI, motywy unit, WM click, normal boot |
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

## Warstwy (cele Makefile)

### `test-smoke` / `test`

1. `test-stack-protector-host`
2. `test-bur-v2-aslr-host`
3. `test-fuzz-smoke`
4. `test-virtio-pci`
5. `test-motywy-unit`
6. `test-wm-click-routing`
7. `test-normal-boot`

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

---

## Wymagania środowiska

- `make`, `python3`
- Cross: `x86_64-linux-gnu-gcc/g++/as/ld` (jak w `Makefile`)
- QEMU (`qemu-system-x86_64`) dla celów `*-qemu` i boot
- Opcjonalnie OVMF dla ścieżek UEFI poza tym dokumentem

Na samym Windowsie bez WSL suite nie zbuduje jądra — użyj WSL/Linux Mint zgodnie z README.

---

## Powiązane narzędzia

| Ścieżka | Rola |
|---------|------|
| [narzedzia/run_local_suite.py](../narzedzia/run_local_suite.py) | Orkiestrator warstw + JSON |
| [narzedzia/run_fuzz_suite.py](../narzedzia/run_fuzz_suite.py) | Fuzz smoke / extended |
| [narzedzia/fuzz/](../narzedzia/fuzz/) | Cele fuzz (BWS, BUR, BSP2) |
| `make help` | Lista celów w tym sekcja lokalnej weryfikacji |
