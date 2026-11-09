# Raport lokalnej weryfikacji BursztynOS — 2026-11-06

Środowisko: WSL2 Ubuntu, `x86_64-linux-gnu-*`, QEMU, bez GitHuba.
Orkiestracja: `narzedzia/run_local_suite.py --mode=nightly --stack-audit` + rerun po łatach.

Artefakty JSON: `.local-ci/raport-nightly-*.json`, `.local-ci/raport-rerun-*.json`.

---

## Werdykt

| Warstwa | Wynik |
|---------|--------|
| Bezpieczeństwo host (SSP, ASLR, fuzz smoke) | **PASS** |
| Fuzz extended (165k iter, 0 crashy) | **PASS** |
| Bezpieczeństwo QEMU (SSP, BWS abuse, guard-page) | **PASS** (po naprawie buildu ISO) |
| VirtIO (pci/blk/net/input) | **PASS** |
| VirtIO-GPU kursor/modeset | **FAIL** (częściowe; sondy kolorów PASS) |
| OTA host + GUI ABI | **PASS** |
| OTA/installer QEMU | QMP `screendump format` **naprawione** w kodzie; pełny przebieg QEMU **nie czekany** (długi) |
| HDA QEMU regression | **FAIL** (prerequisites transport/route) |
| Audyt stosu jądra | **FAIL** (3 funkcje > 6144 B) |
| Build ISO Multiboot2 | **PASS** (po seriach łatek linkera) |

**Podsumowanie:** krytyczna ścieżka bezpieczeństwa (SSP/fuzz/BWS abuse/guard-page) jest zielona. Regresja produktowa wciąż ma 3–5 otwartych punktów (GPU kursor, HDA, stack audit, ewent. installer po restarcie QMP).

---

## Przebieg 1 — nightly (przed łatkami)

`failed=12/16`, czas ~596 s.

| PASS | FAIL (przyczyna) |
|------|------------------|
| `test-security-host` | Wszystkie cele zależne od `iso` — overflow TEXT **tapety** (`0x44000` → used `0x450e0`) |
| `test-husarz-engine-host` | Potem overflow TEXT **przeglądarki** (`0x88000` → used `0xD6160`) |
| `test-input-coexistence-host` | Brak `test_gui_aktualizacje.py` (cel w Makefile, plik nieistniejący) |
| `fuzz-extended` | `test_dzwieku` LMA overlap (SIZEOF(.text)=0 w AT()) |
| | Kernel > 14 MiB po powiększeniu Husarza |
| | `generujpakiet.py` f-string + brak Pillow |
| | brak `grub-efi-amd64-bin` |

Host security + fuzz extended **nie wykryły nowych crashy** (0 unhandled).

---

## Łatki wprowadzone w sesji (stabilność + bezpieczeństwo buildu)

1. **`programy/tapety/tapety_linker.ld`** — TEXT `0x44000` → `0x48000` (eliminacja overflow).
2. **`programy/przegladarka_linker.ld` + `przegladarka.cpp`** — TEXT `0x88000` → `0xD8000` (ALIGN nad `0xD6160`).
3. **`linker.ld`** — limit jądra 14 MiB → **15 MiB** (guard Ring3: 1 MiB; uzasadnienie: wzrost osadzonego Husarza).
4. **`programy/test_dzwieku/test_dzwieku_linker.ld`** (+ diagnostyka) — DATA LMA z `LOADADDR(.text)+SIZEOF(.text)` zamiast wczesnego `SIZEOF=0`.
5. **`narzedzia/aktualizacje/test_gui_aktualizacje.py`** — odtworzony host gate ABI/GUI OTA (33 asercje).
6. **`narzedzia/tapety-btp/generujpakiet.py`** — poprawka f-string (backslash).
7. **`narzedzia/instalator/qemu_install.py`** — `screendump` bez `format=png` (kompatybilność QEMU Ubuntu).

---

## Przebieg 2 — rerun po łatkach buildu

`failed=5/12`, czas ~648 s.

### PASS (7)

- `test-stack-protector-qemu`
- `test-bws-abuse-qemu`
- `test-guard-page`
- `test-virtio-qemu` / `virtio-blk` / `virtio-net` / `virtio-input`

### FAIL (5) — status i rekomendacja

| Cel | Objaw | Priorytet |
|-----|--------|-----------|
| `test-virtio-gpu-qemu` | Kernel cursor suite FAIL; sondy kolorów i część USB/QMP PASS | Średni — regresja kompozytora/kursora |
| `test-ota` / `installer-test` | QMP `screendump format` unexpected | **Naprawione** w `qemu_install.py` — wymaga ponownego przebiegu |
| `test-hda-regression-qemu` | `FAIL exact Intel/QEMU transport... TEST_AVAILABLE=0`; wcześniej też timeout VirtIO-GPU | Średni — środowisko HDA/QEMU |
| `audit-stos-jadra` | Limit 6144 B: `virtio_blk_stress_test` 8848 B, `psf_przenies_twor_wynik` 8656 B, `shell _start` 6896 B | **Wysoki** — ryzyko przepełnienia stosu Ring 0/3 |

---

## Fuzz / podatności

| Tryb | Executions | Crashes |
|------|------------|---------|
| smoke (w security-host) | ~35 033 | 0 |
| extended | ~165 062 | 0 |

Brak nowych crashy w BWS / BUR / BSP2 względem raportu hardening 2026-11-04.
Regresje fuzz (`regressions=` w logach) to znane seed regressione, nie nowe CVE.

---

## Co odpalać dalej

```bash
# po commicie łatek:
make test-security          # gate bezpieczeństwa
make installer-test test-ota
make test-virtio-gpu-qemu   # osobna sesja na kursor/GPU
make audit-stos-jadra       # po redukcji ramek stosu w 3 funkcjach
```

Redukcja stosu (kolejna sesja): przeniesienie dużych buforów w `virtio_blk_stress_test` / `psf_przenies_*` / `shell.cpp:_start` na heap lub `static` scratch z guardami.

---

## Pliki referencyjne

- [12_lokalna_weryfikacja.md](12_lokalna_weryfikacja.md)
- [bursztyn_security_hardening_stack_fuzz_2026-11-04.md](bursztyn_security_hardening_stack_fuzz_2026-11-04.md)
- `.local-ci/logs/` — pełne logi jobów
