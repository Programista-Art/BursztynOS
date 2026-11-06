# Ostatnie zmiany w BursztynOS

Podsumowanie pracy z ostatnich dni (październik–listopad 2026). Stan changelogu bazowego: **2026-10-11**; sekcja lokalnej weryfikacji: **2026-11-06**.

Plik obejmuje:
1. Ostatnie commity na `main` (już zatwierdzone).
2. Zmiany robocze w drzewie (jeszcze **niezacommitowane**).
3. Lokalną weryfikację i hardening bezpieczeństwa (listopad 2026).

---

## 0. Lokalna weryfikacja i hardening (2026-11)

### Bezpieczeństwo runtime (2026-11-04)

Zob. [bursztyn_security_hardening_stack_fuzz_2026-11-04.md](audyty/bursztyn_security_hardening_stack_fuzz_2026-11-04.md) oraz metryki w [security-hardening-2026-11-04/](security-hardening-2026-11-04/):

- SSP Ring 0/3 (`-fstack-protector-strong`, global guard),
- 7 fuzzerów (BWS / BUR / BSP2) + smoke/extended,
- poprawki memory-safety wykryte fuzzem / QEMU abuse,
- prototyp BUR v2 ASLR (host).

### Lokalne CI bez GitHuba (2026-11-06)

Mapa: [12_lokalna_weryfikacja.md](12_lokalna_weryfikacja.md).

| Cel | Rola |
|-----|------|
| `make test` / `test-smoke` | Gate przed commitem |
| `make test-security` | SSP + fuzz smoke + QEMU abuse + guard-page (`test-security-hardening` = alias) |
| `make test-regression` | VirtIO / OTA / instalator / Husarz / HDA / input |
| `make test-nightly` | security + regression + `fuzz-extended` |
| `make test-local-suite MODE=…` | Orkiestrator JSON → `.local-ci/` ([narzedzia/run_local_suite.py](../narzedzia/run_local_suite.py)) |

Higiena: artefakty `*.d`/`*.o`/`*.bak` wycofane z indeksu git; jednorazowe `patch_*.py` w `narzedzia/archiwum/patch/`.

---

## 1. Ostatnie commity (zatwierdzone)

| Hash | Data | Opis |
|------|------|------|
| `f6541df` | 2026-10-11 | `fix(input)` — koegzystencja touchpada z myszą USB i odroczona enumeracja xHCI |
| `f026e85` | 2026-10-10 | `fix(input)` — kolejność inicjalizacji USB przed PS/2 i gotowość geometrii wskaźnika |
| `7c7653a` | 2026-10-10 | `feat(input)` — deskryptorowa obsługa wskaźników USB i poprawki kontrolera PS/2 |
| `721be50` | 2026-10-10 | `refactor(virtio-input)` — dwufazowy cykl życia i opóźniona aktywacja `driver_ok` |
| `28216b8` | 2026-10-08 | `fix(gui)` — klawisze numeryczne, migotanie pulpitu, usuwanie folderów |
| `55d7266` | 2026-10-08 | `fix(psf)` — weryfikacja katalogów przez lookup inodów w bootstrapie |
| `91ab674` | 2026-10-08 | `fix(boot)` — deterministyczna publikacja ekranu powitalnego + sync scanoutu virtio-gpu |
| `a5742cd` | 2026-10-07 | `feat(boot)` — płynny ekran powitalny z crossfade, quiesce virtio-gpu, profile GTK |
| `7e3e98b` | 2026-10-07 | `fix(boot)` — ukrycie wczesnego terminala jadra, etapowa publikacja klatek |

### Główne wątki ostatnich commitów
- **Wejście (input):** USB/HID (xHCI) + PS/2 + VirtIO Input — kolejność inicjalizacji, deskryptory myszy, koegzystencja touchpad / mysz USB, opóźniona enumeracja xHCI, dwufazowy cykl VirtIO Input.
- **Boot / splash:** deterministyczna publikacja pierwszej klatki, crossfade ekranu powitalnego, synchronizacja scanoutu virtio-gpu.
- **GUI / pulpit:** migotanie, klawisze numeryczne, usuwanie folderów, trwały układ ikon.

---

## 2. Zmiany robocze (niezacommitowane)

**Skala:** 27 plików, ~**+1470 / −469** linii.

### 2.1. Aplikacja „Test dźwięku” (nowa)

Nowy program Ring 3 + integracja z pulpitem i Menu Start:

- `programy/test_dzwieku/` — źródła aplikacji (`test_dzwieku.cpp`, linker).
- `kernel.cpp` — instalacja paczki `.cebula` (`/programy/test_dzwieku.cebula/…`) z manifestem.
- `desktop_entries.h` — nowy typ ikony `TestDzwieku` i wpis launchera.
- `menedzer_okien.cpp` — ikona (głośnik + fala), wpis w Menu Start, ścieżka uruchomienia.
- `psf_bootstrap.h` — katalog systemowy w bootstrapie.
- `Makefile` — reguły budowania `test_dzwieku` (Ring 3 → `.bin` → blob).

### 2.2. Diagnostyka HDA (dźwięk)

Bezpieczne API diagnostyczne kernel-side, bez surowych verbów z Ring 3:

- **Nowe pliki:** `sterowniki/dzwiek/hda_wewnetrzny.h`, `hda_diag.h`, `hda_diag.cpp`, `hda_diag_format.h`.
- `sterowniki/dzwiek/hda.cpp` (+~397 linii) — wewnętrzne API diagnostyczne:
  - odczyt stanu kontrolera / strumienia (BDL z maską fizyczną),
  - skan kodeków (krokowo),
  - snapshoty i kontrolowany test tonu (L/R, sweep 200–4000 Hz, amplituda ~50%).
- `bws_api.h` — nowe polecenia **BWS 89 (`HDA_DIAG`)** i **90 (`HDA_TEST`)**; następny wolny numer = 91.
- `bws_nowoczesne.cpp` — warstwa BWS z walidacją wskaźników, limitami buforów i wymogiem `PRAWO_GUI`.

### 2.3. Oś czasu boot / timing (`boot_phase_timing.h`)

- Oś milestone’ów (`timeline`, do 192 wpisów) z etykietami i znacznikami czasu.
- Znaczniki w `kernel.cpp` i `grafika.cpp`: HPET, scheduler, crypto, APIC, block/graphics probe, backend graficzny, pierwsza klatka splash itd.
- Publikacja enumeracji zależna od stanu splash (`enumeration_publication_ready`).
- Emitowanie timeline’u przez serial (limit 2 rekordów na wywołanie) przy `BURSZTYNDIAGBOOTXHCITIMING`.

### 2.4. VirtIO Input — diagnostyka QEMU

- `virtio_input.cpp`, `input_device.cpp` — rekordy diagnostyczne (`virtio_input_qemu_diag.h`):
  - EVBITS / KEYBITS, generacje config, pierwsza publikacja wskaźnika.
- `Makefile` — flaga `BURSZTYNDIAGVIRTIOINPUTQEMU` w teście hostowym.

### 2.5. xHCI — maszyna stanów portów

`sterowniki/usb/xhci.cpp` (duży refactor, ~+103 / −187):

- `PortState`: `Empty → Queued → Enumerating → Active / Failed / Retiring`.
- Odroczona enumeracja, reconnect / `scan_pending`, `generation`.
- Przygotowanie endpointu HID bez blokowania (`prepare_hid_endpoint` + zwracany TRB).
- Milestone’y: „first HID input completion”, „xHCI device active”.
- Ścieżki testowe odseparowane pod `BURSZTYN_XHCI_HOST_TEST`.

### 2.6. Makefile / QEMU

- Nowe obiekty: `hda_diag.o`, `test_dzwieku_blob.o`.
- `QEMUACCEL` domyślnie `tcg,thread=single`.
- `rng-builtin` zamiast `rng-random` + `/dev/urandom`.
- VirtIO: **`virtio-mouse-pci`** zamiast `virtio-tablet-pci`.
- `-boot order=d,menu=on` we wszystkich targetach.
- `runuefi` — wspólna macierz argumentów, audyt launchera (`narzedzia/qemu_launcher_audit.py`), cele `runuefi-argv` / `runuefi-audit`.

### 2.7. `runwindows.ps1` (Windows / QEMU)

Duże rozbudowanie (+467 / −132):

- Walidacja ścieżki QEMU (`-QemuPath`), firmware UEFI, rozmiary dysków.
- Sprawdzanie dostępności urządzeń VirtIO (`Assert-QemuVirtioDevices`).
- Topologia PCI zgodna z Linux: **Intel HDA** + `virtio-blk` → `net` → `rng` → `gpu` → keyboard → mouse.
- Backend audio hosta: `none` (unika błędu DirectSound przy zachowaniu HDA w gościu).
- Audyty launchera (`testy/test_windows_launcher_config.py`).

### 2.8. Testy

Zaktualizowane / rozbudowane:

- `testy/test_xhci_boot_coexistence.cpp`
- `testy/test_xhci_physical_mouse.cpp`
- `testy/test_xhci_pointer.cpp`
- `testy/test_virtio_pci.cpp`
- `testy/test_powitanie.cpp`
- `testy/test_input_diagnostic_parity.py`
- `testy/test_windows_launcher_config.py`

Artefakty walidacji (nieśledzone): `.hda-test/`, `.pointingtest/coexistence-validation/parity/…`, `.pointingtest/launcher-audit/…`.

---

## 3. Dokumentacja z ostatnich dni (w `docs/`)

| Plik / katalog | Temat |
|----------------|-------|
| `testdzwiekuhdadiagnostics2026-10-11.md` | Test dźwięku + diagnostyka HDA |
| `acernitroinputcoexistencebootregression2026-10-11.md` | Regresja boot + koegzystencja wejścia |
| `acerbootdelayandqemulinuxinput2026-10-11.md` | Opóźnienie boot / wejście QEMU-Linux |
| `acer-boot-qemu-audit-2026-10-11/` | Audyt boot (benchmarki, timeline, replay) |
| `input-coexistence-diagnostic-audit-2026-10-11/` | Audyt diagnostyki koegzystencji wejścia |
| `xhci_pointing_regression_2026-10-10.md` | Regresja wskaźnika xHCI |
| `pointer_regression_repair_2026-10-10.md` | Naprawa regresji wskaźnika |
| `virtio_input_*_2026-10-09/10.md` | Cykl życia VirtIO Input |

---

## 4. Co dalej (roboczo)

1. Dokończyć i zweryfikować aplikację **Test dźwięku** (BWS 89/90) na HDA.
2. Przejść walidację xHCI po maszynie stanów portów (koegzystencja touchpad / mysz USB).
3. Złapać timeline boot pod `BURSZTYNDIAGBOOTXHCITIMING` i porównać z audytami w `docs/`.
4. Zacommitować spójny zestaw zmian (HDA diag + Test dźwięku + timing + xHCI + Makefile/runwindows).
5. Utrzymywać lokalne CI: `make test-smoke` przed commitem, `make test-nightly` nocą; porównywać metryki fuzz z `docs/security-hardening-2026-11-04/`.
6. Dokończyć produkcyjny BUR v2 ASLR w loaderze (dziś prototyp host).

---

*Wygenerowano automatycznie na podstawie `git log` i `git diff` (2026-10-11); uzupełniono o lokalną weryfikację (2026-11-06).*
