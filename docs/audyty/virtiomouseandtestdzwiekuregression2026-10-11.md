# VirtIO mouse + Test dźwięku regressions — 2026-10-11

## Scope

Two regressions after the recent input coexistence and HDA diagnostic work.
Physical Acer touchpad + USB mouse coexistence semantics were **not** changed.
No QEMU or VirtualBox was launched for this task. Manual runtime acceptance
is owned by the user.

---

## 1. VirtIO mouse huge-delta regression

### Symptom

```text
INPUT REGISTER dev='QEMU Virtio Mouse' bus=VIRTIO id=2048 kinds=0x2 prio=30
MOUSE BOUNDS source=QEMU Virtio Mouse screen=1280x720 max=1279x719 cur=703,300 dx=-27496 dy=-44561 res=0,719 gen=2
```

PS/2 in the same boot is normal (`dx=41`). Same failure on Linux `make runuefi`
and the Windows QEMU launcher. Device registration, role classification and
MSI-X are successful; corruption appears when movement reaches the pointer
pipeline.

### Audit (event layout / signedness / accumulation)

| Check | Result |
|-------|--------|
| `VirtioInputEvent` layout | `u16 type@0, u16 code@2, u32 value@4`, packed, 8 B — matches VirtIO 1.x §5.8 |
| `InputEvent` layout | identical, `static_assert` 8 B |
| LE wire image | `type=2,code=0,value=1` → `02 00 00 00 01 00 00 00` |
| `value` signedness | `input_signed_value()` correctly maps `0xFFFFFFFF→-1`, `0x80000000→INT32_MIN` |
| REL accumulation | `rel_x`/`rel_y` are `int32_t`, saturating add, cleared on `SYN_REPORT` |
| SYN isolation | new report does not inherit prior `dx`/`dy`; `SYN_DROPPED` discards |
| Multi-REL before SYN | `RELX 3, RELX 4, RELY -2, RELY -5, SYN` → `dx=7`, `dy` adapter `+7` |
| `RELX 3 + RELY -2` | publishes exactly `dx=3`, adapter `dy=+2` (single Y inversion at GUI adapter) |
| INT32_MIN / INT32_MAX | preserved as signed |
| Short `used.len` | rejected (`len != 8`) |
| Observed `-27496` | **only** when those exact LE bytes `98 94 FF FF` are in the event buffer |

**Host fixtures:** `narzedzia/test_virtio_input_events.cpp`
(`make test-virtio-input-events`) — **42 pass, 0 fail** under ASan+UBSan.
They feed raw little-endian byte sequences through the real
`input_dispatch_event` → accumulate → `submit_pointer_report` →
`zaktualizuj_mysze_zrodlo` path.

### Root cause (proven)

Two guest-side defects, both upstream of `zaktualizuj_mysze_zrodlo`:

**A. REL accumulation without a pointer publication path (primary).**
`virtio_input_prepare` set `pointer_mode = INPUT_POINTER_RELATIVE` for every
non-tablet device, including keyboard-only ones. `input_register_device`
did the same for `INPUT_POINTER_UNSPECIFIED`. Meanwhile `EV_REL` accumulated
whenever `pointer_mode & INPUT_POINTER_RELATIVE`, but `SYN_REPORT` only
publishes/clears when `input_is_pointer_device()` (needs MOUSE|TABLET kind).

So any EV_REL bytes seen on a device that is not (yet) classified as a pointer
were added to `rel_x`/`rel_y` and **never cleared**. The first SYN that did
publish (or a later coexistence/ownership edge) dumped the whole sum as one
huge delta — matching `dx=-27496 dy=-44561` on a small physical move.
PS/2 never hit this: its parser feeds `input_dispatch_event` on a true mouse
device with working SYN publish (`dx=41`).

**Fix:** keyboard-only devices stay `INPUT_POINTER_UNSPECIFIED`;
`EV_REL` X/Y are dropped unless the device is a real pointer with
`INPUT_POINTER_RELATIVE`. SYN publish path unchanged.

**B. Non-volatile DMA event load (hardening).**
`events[taken] = drv->dma_events[idx]` loaded DMA through a plain pointer.
The compiler may widen/hoist that load across slots. Now an explicit 8-byte
`memcpy` + compiler barrier.

Host fixtures (`test-virtio-input_events`) prove decode/accumulate are correct
when exact LE bytes are present: `RELX 3 + RELY -2` → `dx=3`, adapter `dy=+2`;
`-27496` appears only for bytes `98 94 FF FF`. New fixture
`test_keyboard_only_rel_does_not_accumulate` locks defect A.

### Why USB HID / PS/2 / Acer coexistence are unaffected

- USB HID report decode (`hid_pointer.cpp`) and PS/2 parser are untouched.
- `input_prove_pointer`, preferred-source metadata, `PROVEN_ACTIVE` admission
  and multi-owner coexistence are unchanged.
- No delta scaling, clamping or tablet substitution was introduced.
- Canonical QEMU profile remains `virtio-mouse-pci` (relative mouse),
  `tcg,thread=single`, one CPU, HDA before VirtIO PCI topology preserved.

### Changed production files (mouse)

| File | Change |
|------|--------|
| `virtio_input.cpp` | pointer_mode only for real pointers; DMA `memcpy` + barrier; bounded event trace |
| `input_device.cpp` | drop REL X/Y on non-owners; keyboard-only stays UNSPECIFIED; SYN trace |
| `Makefile` | `test-virtio-input-events` target |

---

## 2. Test dźwięku

### Audit (build → first window frame)

| Stage | Result |
|-------|--------|
| `programy/test_dzwieku` build | OK (`make test_dzwieku_blob.o`) |
| Linker script vs NaglowekBur | offsets/VAs match; now also `static_assert` 4 KiB + contiguity |
| `test_dzwieku.bin` | 659456 B, `BUR\0` magic, segments 0x1000+0x60000+0x40000 |
| Blob symbols | `_binary_test_dzwieku_bin_{start,end}` match `kernel.cpp` |
| PSF bootstrap | `/programy/test_dzwieku.cebula` present |
| `.cebula` install path | `kernel.cpp`, `desktop_entries.h`, `menedzer_okien.cpp` **exact match** |
| Case sensitivity | `test_dzwieku.cebula` / `test_dzwieku.bur` identical everywhere |
| BWS 89 / 90 numbers | 89 and 90 were free (`BWS_PIERWSZY_WOLNY` was 89, now 91) |
| Syscall ABI | `bws_wywolaj`: r8=nr, r9..r13=args — same as all working apps |
| PZB | manifest `okna` + `pliki_czytaj` + `pliki_zapisz`; BWS 89/90 require `PRAWO_GUI` |
| Snapshot ABI | every public struct has `wersja` + `rozmiar`; `static_assert` sizes |
| Host HDA tests | `test-hda-diag` 143 + `test-hda-bws` 148 — **0 errors** (ASan/UBSan) |
| Packaging audit | `make test-dzwieku-packaging` — **22 pass, 0 fail** |

**Root-cause class:** packaging/launcher/ABI numbers/paths are **not** broken.
The remaining failure surface is runtime (Ring3 GUI init, BWS answer when HDA
scan is empty, snapshot publication). The app now performs a startup self-check
and always shows a Polish status plus numeric codes instead of failing silently.

### App changes

- Startup statuses: `Uruchamianie` → `Łączenie z diagnostyką HDA` →
  `Gotowe` / `Brak kontrolera HDA` / `Błąd diagnostyki HDA` (with BWS/HDA code).
- Save-report failure text names BWS/PZB instead of a generic message.
- BUR segment `static_assert`s (alignment + contiguity).

### Security (unchanged)

- No raw HDA verbs, MMIO, DMA addresses or unrestricted NIDs on BWS 89/90.
- User pointers go through `skopiuj_*_przestrzeni_uzytkownika` + range checks.
- BDL physical addresses masked.

---

## 3. Test totals

| Suite | Result |
|-------|--------|
| `test-virtio-input-events` | **45 / 0** |
| `test-hda-diag` | **143 / 0** |
| `test-hda-bws` | **148 / 0** |
| `test-dzwieku-packaging` | **22 / 0** |
| ASan | no errors on the above |
| UBSan | `halt_on_error=1`, no errors on the above |

---

## 4. Files changed

**Production:** `virtio_input.cpp`, `input_device.cpp`, `Makefile`,
`programy/test_dzwieku/test_dzwieku.cpp`

**Tests (new/updated):** `narzedzia/test_virtio_input_events.cpp`,
`narzedzia/test_dzwieku_packaging.py`

**Docs:** this file, `docs/SHA256SUMS-regressions-2026-10-11`.

---

## 5. Builds (SHA 256)

| ISO | SHA 256 |
|-----|---------|
| `BursztynOS.iso` (production, diagnostics off) | `1482c0eb8316c369dcb7437f32a1f4b7e1a7b1e1eeb28beb93595fcfd82305d2` |
| `BursztynOS-production-2026-10-11.iso` | `1482c0eb8316c369dcb7437f32a1f4b7e1a7b1e1eeb28beb93595fcfd82305d2` |
| `BursztynOS-virtio-input-qemu-diag.iso` | `52d7fb956068e4bf92c0e2816ff81c032a5c785a2ecb429cfa8981f70ba46c42` |
| `BursztynOS-hda-app-diag.iso` | `818960b04af2c4e901f4f56225e695feb25d40bdbfaff3942aa681481fb2b4d7` |

**Runtime PASS is not claimed** for VirtIO mouse or Test dźwięku.
Manual QEMU acceptance remains with the user.
