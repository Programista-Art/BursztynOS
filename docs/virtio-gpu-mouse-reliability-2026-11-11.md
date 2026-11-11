# VirtIO-PCI tests, VirtIO-GPU recovery and mouse direction — 2026-11-11

Branch `feature/bsf-2-stabilization` (HEAD `833e246`, previous BSF 2.0
stabilization already committed by the owner). The work below is
**uncommitted**. `main`, `test` and `UNITRONIX` were not touched. BSF 2.0,
Unicode data, GSUB/GPOS, fonts, Notepad editing, Husarz, filesystem, loader,
syscall ABI and bootloader were not modified (`git diff --stat` lists no file
under `bsf/`, `programy/notatnik/`, `programy/husarz/`, `jadro/fs*`, `jadro/bws/`).

Environment: Linux x86-64 **running inside VirtualBox** (host pointer is
"VirtualBox mouse integration"), 4 vCPUs, QEMU 8.2.2 TCG, OVMF, SDL 2.30.0.
Host tests use ASan + UBSan (`halt_on_error=1`).

Results tables marked *measured* were produced in this session; logs are under
`.bsf-test/` (not committed).

---

## Phase A — VirtIO-PCI host tests

`make test-virtio-pci` linked since the previous session (host clock stub) but
aborted under ASan.

### Root cause 1: ASan stack-use-after-return (`test_virtio_gpu_init_and_cleanup`)

Not a driver bug. Three GPU tests asserted the *old* initial-mode policy
("GET_DISPLAY_INFO wins", 1280x800). Since `950352a` the explicit guest policy
(`display_mode_policy`, default 1280x720, `bursztyn.video=WxH`) owns the mode
(documented in `docs/graphics-performance-2026-11-09.md`). The failing
`TEST_ASSERT` **returns early**, skipping `virtio_gpu_reset`; the global
`g_gpu_drivers[0].dev` kept pointing at the test's stack `VirtioPciDevice`; the
*next* test's `virtio_gpu_reset_all()` used that dead pointer
(`virtio_pci_notify_address`, ASan stack-use-after-return).

### Root cause 2: UBSan "load of value 133 … bool" (`virtio_pci.cpp:416`)

Same dangling pointer: with stack-use-after-return detection off, the reused
stack bytes were read as `dev->input_init_active`. A second instance of the
same lifetime defect existed for VirtIO-Input tests (InputDevice registry
pointing into a dead `VirtioInputDriver`), found while adding new cases.

### Fixes

| Change | File |
|---|---|
| `GpuDriverScope` RAII guard declared after the stack device in all 25 GPU tests: resets drivers while the device is still alive, also on early return | `testy/test_virtio_pci.cpp` |
| `InputRegistryScope` guard in all 23 VirtIO-Input tests | same |
| Stale mode expectations rewritten to the documented guest policy (incl. policy 1024x768 beating host 1920x1080 and GOP 1280x720); SG-coalescing test selects its 1024x768 mode through the policy | same |
| Mock device used a `uint32_t` processed counter against the 16-bit `avail->idx` — would loop forever at index wrap; now 16-bit | same |
| Regression `test_gpu_driver_scope_releases_device_on_early_return` | same |
| Host clock stub guarded (`BURSZTYN_HOST_KERNEL_CLOCK_PROVIDED`): the stub added in the previous session broke `test-pointer-graphics-host`/`test-splash-production-path`, which `#include` this file and define their own clock | `testy/test_virtio_pci.cpp`, `testy/test_splash_production_path.cpp` |
| Pre-existing broken includes (`narzedzia/…` → `testy/…`) made `test-pointing-host`/`test-pointer-regression-host` not compile | 4 pointer test sources |

No assertion was removed or relaxed; no sanitizer option was changed.

*Measured:* `make test-virtio-pci` **185/185 PASS**, ASan+UBSan clean, also
with `detect_stack_use_after_return=0` and `=1`. `make
test-virtio-input-qemu-parity-host` (production + diagnostic builds) PASS.

---

## Phase B — VirtIO-GPU bounded late-completion recovery

### Previous behaviour

One shared command buffer (`cmd_vaddr`: request @0, response @128 KiB), one
controlq chain in flight. On the 500 ms deadline the driver logged, kept the
DMA (correct) and set `has_error` **permanently**: the display froze for the
rest of the boot. `RESOURCE_ATTACH_BACKING` had its own copy of the wait loop
and did not check whether the buffer was free.

### State machine (`sterowniki/virtio/virtio_gpu.{h,cpp}`)

```
IDLE --submit--> SUBMITTED --kick--> WAITING --valid completion--> IDLE
                                       | 500 ms (soft, unchanged)
                                       v
                              TIMED_OUT_PENDING
         validated late completion |            | no completion at a check
         (same head, valid len)    |            | after hard deadline (10 s),
                                   v            | corrupt/foreign entry,
                              RECOVERED -> IDLE | no clock
                                                v
                                       PERMANENT_FAILURE (has_error, DMA kept
                                       until a confirmed device reset)
```

All controlq users go through `virtio_gpu_controlq_ready_locked()` and one
executor `virtio_gpu_execute_locked()` (also used by ATTACH_BACKING now).

### Ownership rules

* While a chain is outstanding the device owns its two descriptors **and** the
  whole command/response buffer; nothing writes `cmd_vaddr` (the ready gate is
  checked before the request is copied/built).
* Nothing is ever resubmitted. A recovered command was executed once by the
  device; the next command is a new request.
* DMA, framebuffer and descriptors are freed only after a completion was
  reaped or the device **confirmed** a reset (`virtio_gpu_reset`). Reset while
  pending sends no command into the owned buffer.
* A completion is accepted only if `virtqueue_pop_used` validates the chain
  *and* its head is the outstanding head *and* no second used entry exists
  *and* `used_len` is within `[sizeof(hdr), resp_len]` (exact for ATTACH).
  A used entry while IDLE is treated as corruption.
* Locks: the spin lock is the RAII guard; every exit path releases it
  (tested: `drv->lock == 0` after failures). Waits are bounded by the clock
  and by a spin guard (no clock → fail closed).

### Which commands may recover how

| Command | Policy | Reason |
|---|---|---|
| `TRANSFER_TO_HOST_2D`, `RESOURCE_FLUSH` | **asynchronous**: return failure at 500 ms (frame dropped), stay `TIMED_OUT_PENDING`; later frames are rejected without submission until the late completion is reaped (next frame or idle loop), then the next publication is a full frame | Idempotent pixel copy/present; the compositor rewrites the frame |
| `RESOURCE_CREATE_2D`, `RESOURCE_ATTACH_BACKING`, `SET_SCANOUT`, `RESOURCE_UNREF`, `RESOURCE_DETACH_BACKING`, `GET_DISPLAY_INFO` | **synchronous**: the same call keeps waiting until the hard deadline and returns the real, validated result | They change resource/scanout state the caller must know; reporting failure while the device may still execute them would desynchronise driver and device |
| cursor queue | unchanged (separate queue/buffer) | |

Permanent failure: no completion visible at the first check after 10 s;
corrupt or foreign used entry; invalid `used_len`; used entry without an
outstanding command; timeout without a monotonic clock. A completion that
arrives after the permanent decision is ignored.

Hard deadline 10 s: first implemented with 5 s; native runs (below) measured
late completions up to 3.8 s and 7 commands not completing within 5 s, so the
bound was raised. The 500 ms normal deadline is unchanged.

Healthy path cost: one `cmd_state` comparison per frame and per idle-loop
iteration (`skladacz_obrazu_wykonaj_zmiane_geometrii`); no extra polling,
logging or full redraws unless a recovery happened.

### Fault injection (`testy/test_virtio_gpu_recovery.inc`, mock clock advancing per read)

| Case | Test | Result |
|---|---|---|
| Normal completion, recovery dormant | `normal_completion_dormant` | PASS |
| Completion just before timeout (~450 ms) | `completion_just_before_timeout` | PASS |
| Completion just after timeout, synchronous (GET_DISPLAY_INFO, SET_SCANOUT, ATTACH) — executed once, real response | `sync_command_completes_after_soft_deadline` | PASS |
| Late completion during recovery (FLUSH), rejection without submission, untouched request bytes, full-frame republish, normal frames after | `async_late_completion_then_normal_operation` | PASS |
| Idle-loop reaping without a new frame | `service_reaps_without_new_frame` | PASS |
| Completion visible at the deadline check | `completion_visible_at_deadline_check` | PASS |
| No completion → permanent; later completion ignored; DMA kept until confirmed reset | `completion_after_hard_deadline_fails_closed` | PASS |
| No completion, sync (waits exactly to the bound) and async | `no_completion_sync_and_async` | PASS |
| Device error response late (consistent queue) / invalid `used_len` (fail closed) | `invalid_late_responses` | PASS |
| Foreign descriptor id | `unexpected_descriptor_index` | PASS |
| Spurious used entry while IDLE | `spurious_used_entry_while_idle` | PASS |
| Used-ring wraparound 65534→65535→0→3 across a recovery | `used_ring_wraparound` | PASS |
| 6 repeated timeouts, buffer/DMA lifetime, reset while pending | `repeated_timeouts_and_lifetime` | PASS |
| No monotonic clock | `without_clock_fails_closed` | PASS |

The old `test_virtio_gpu_command_timeout` asserted the old permanent latch; it
now asserts `TIMED_OUT_PENDING` and rejection without submission.

### Native recovery results (*measured*)

Harness `narzedzia/virtio_gpu_ab_qemu.py` (both ISOs concurrently per round,
`bsf2_native_qemu.py --workload apps`: boot → desktop → Settings → Notepad →
Shell, UEFI 1280x720, `-smp 1`, TCG). *base* = ISO built from `833e246`
(old driver), *recovery* = this work. A run "passes" when the whole workload
completes without kernel/user faults and without PERMANENT_FAILURE.

| Host condition | base (old driver) pass | recovery pass | recovery: soft timeouts / validated late completions / permanent |
|---|---|---|---|
| Heavy: 2 busy loops + 2 VMs, 4 rounds | **0 / 4** (every run stopped at its first timeout, all during boot) | **3 / 4** | 11 / 10 / 1 |
| Moderate: 2 VMs, 4 rounds | 3 / 4 (1 harness error "Text file busy", no GPU event) | **4 / 4** | 0 / 0 / 0 |
| Idle: 1 VM, 3 rounds | not repeated (0/5 timeouts at idle in the previous session) | **3 / 3** | 0 / 0 / 0 |

Additional runs during development (heavy, with the first 5 s bound, mouse
harness at 1920x1080/2560x1440 next to other VMs): many recoveries
(TRANSFER/FLUSH reaped 502–3,772 ms after submission) and 7 TRANSFER commands
without a completion within 5 s — the reason for the 10 s bound and the
"inspect the ring once at the deadline" rule.

Late completion latency seen natively (all runs): 502 ms … 3,772 ms.

The remaining permanent failure under heavy load was a **synchronous**
`SET_SCANOUT` at boot that the device did not complete in 10 s; the driver
failed closed as designed. Boot-time synchronous commands can therefore still
fail on an extremely overloaded TCG host.

---

## Phase C — reversed mouse movement in `make runuefi`

### Reproduction and configuration

`make runuefi` (`make -s runuefi-argv`): `pc,hpet=on`, TCG single thread,
`-display sdl`, VirtIO keyboard and **only relative** pointers:
`virtio-mouse-pci` plus the implicit i8042 PS/2 mouse. No USB, no tablet.
QEMU (`query-mice`) routes host pointer events to *QEMU Virtio Mouse*
(current, relative). In relative mode SDL hides the host cursor and grabs on
click (Ctrl+Alt+G releases).

Guest pipeline, verified natively with the exact runuefi argv
(`narzedzia/mouse_pointer_qemu.py`, QMP `input-send-event`, guest cursor read
from `mysz_x/mysz_y`): every direction, diagonal, ±1, zero, 20 reversals and
all four edges are exact for **both** virtio-mouse and PS/2; window drag,
icon double click, Start menu and close button land where the cursor is.
**The guest does not invert relative motion.**

### Root cause

1. **Host frontend.** This host's pointer is an XI2 device with *absolute*
   valuators (`xinput list --long`: "VirtualBox mouse integration", Abs X/Y
   0..65535, mode absolute). When QEMU's SDL frontend grabs for a relative
   guest pointer it enables `SDL_SetRelativeMouseMode`; SDL 2.30.0
   (`src/video/x11/SDL_x11xinput2.c:301`, unchanged in 2.30.9) converts raw
   motion of absolute devices with
   `processed_coords[i] = devinfo->prev_coords[i] - coords[i]` —
   **reversed sign, in device units** (≈34× at 1920 px). QEMU forwards that as
   relative motion, the guest applies it faithfully → the cursor runs the
   wrong way and jumps. It happens only while grabbed and only for absolute
   host pointers (VirtualBox/VMware integration, tablets, touch screens);
   a plain USB/PS/2 host mouse is unaffected — hence "sometimes".
   Modelled in `testy/test_mouse_pipeline_host.cpp` (case 1).
2. **Guest defect on the absolute path (found while fixing 1).** QEMU ≥ 8.0
   adds `BTN_TOUCH` to every `virtio-tablet` (shared `keymap_button` for
   multitouch, `hw/input/virtio-input-hid.c`) without `INPUT_PROP_DIRECT`.
   Bursztyn set `abs_contact_required` from `BTN_TOUCH` alone, i.e. treated the
   tablet as a touch screen and dropped **every hover move**. Natively the
   cursor did not move at all with a tablet (raw ABS reached InputDevice:
   `s_last_tablet_raw_x/y = 32741/32721`). This is why a tablet could not be
   used as the fix before.

### Fix

* `sterowniki/virtio/virtio_input.cpp`: contact gates absolute motion only for
  direct-touch devices (`BTN_TOUCH` **and** `INPUT_PROP_DIRECT`, Linux
  semantics). USB HID digitizer handling is unchanged.
* `jadro/gui/pointer_coordinates.h` `pointer_map_absolute()`: one shared,
  rounded, endpoint-preserving absolute→pixel conversion (min→0,
  max→size−1, 64-bit, no overflow for 32-bit ranges and `INT_MAX` sizes).
  Truncation biased every position towards the origin (−2 px at the far edge
  with QEMU's `px*32767/size` encoding); now ≤ 1 px. Oracles in 6 existing
  tests updated from the floor formula to the rounded formula.
* `Makefile` `QEMU_VIRTIO_COMMON_ARGS`: `virtio-tablet-pci` appended **last**
  (BDF 00:09.0; the other seven devices keep their BDFs). Intended primary
  device: absolute virtio-tablet (QEMU never enters relative mode, no grab,
  host and guest pointer coincide). Fallback: virtio-mouse (relative, used if
  the tablet is absent/not activated), then PS/2. Applies to every Linux VirtIO
  target using the common args (`run`, `runuefi`, `run8k`, `run-installed`, …).
  `runwindows.ps1` is **unchanged** (Windows SDL/GTK use raw relative input,
  not affected); `test_launcher_parity.py` now permits exactly this one
  trailing device, `test_windows_launcher_config.py` checks its position.
* No sign inversion anywhere; device-specific semantics stay in the drivers
  (PS/2 Y inversion remains only in its packet parser).

### Coordinate convention

Desktop origin top-left, +X right, +Y down, in physical framebuffer pixels.
Relative events: signed screen-down deltas at the InputDevice boundary
(PS/2 parser converts its Y-up protocol), clamped only when accumulated into
the cursor. Absolute events: clamped to the advertised range, then
`pointer_map_absolute` against the *current* mode, so mode changes need no
re-registration. An identical absolute state is not a new motion (reports are
state; a repeated tablet sample must not steal the cursor from another
device). Cursor drawing, hit-testing (`znajdz_warstwe_pod_punktem`) and event
coordinates all use the same `mysz_x/mysz_y`.

### Tests

* `make test-mouse-pipeline-host` (new; production InputDevice + graphics +
  window hit-testing; run at 800x600, 1024x768, 1280x720, 1920x1080,
  2560x1440): SDL root-cause model, relative directions/diagonals/zero/
  ±1/±127/INT32 extremes/reversals/edges for virtio and PS/2 buses, absolute
  min/mid/max/out-of-range, every host pixel of a row and column (max error
  1 px, monotonic), duplicate prevention, device switching, malformed range,
  SYN_DROPPED, click focus and z-order with overlapping windows, re-init.
* `test_virtio_input_qemu_tablet_hover_without_contact` (virtio-pci suite):
  QEMU tablet without PROP_DIRECT moves on hover; with PROP_DIRECT contact is
  still required.

### Native results (*measured*, `narzedzia/mouse_pointer_qemu.py`)

Exact `make runuefi` argv with host-only substitutions (`-display none`,
serial file, QMP socket, `-snapshot`, `-audiodev none`; `--bios` drops OVMF =
`make run` device set; `--video` = GRUB `bursztyn.video` fixture).

| Configuration | QEMU current pointer | Relative (virtio + PS/2): directions, diagonals, ±1, zero, reversals, edges | Absolute: min / max / corners / monotonic / max error | Cursor pixels at logical position | Start click | Icon double click, drag (150,90)→exact, close-button hit | Result |
|---|---|---|---|---|---|---|---|
| Baseline: old `runuefi` (no tablet), old guest | Virtio Mouse (relative) | all exact | n/a | yes | yes | yes | guest correct; host SDL path is the defect |
| Old guest + tablet (A/B) | Virtio Tablet | all exact | **cursor never moved** (contact gating) | yes | yes (via relative fallback) | yes | guest defect found |
| New `runuefi`, UEFI 1280x720 | Virtio Tablet (absolute) | all exact | exact / exact / exact / yes / 1 px | yes | yes | yes | **PASS** |
| UEFI 800x600 | Virtio Tablet | all exact | exact, 1 px | yes | yes | n/a* | **PASS** (first attempt: harness region bug, fixed) |
| UEFI 1024x768 | Virtio Tablet | all exact | exact, 1 px | yes | yes | n/a* | **PASS** |
| UEFI 1920x1080 | Virtio Tablet | all exact | exact, 1 px | yes | yes | n/a* | **PASS** |
| UEFI 2560x1440 | Virtio Tablet | all exact | exact, 1 px | yes | yes | n/a* | **PASS** |
| BIOS (SeaBIOS, same devices) | Virtio Tablet | all exact | exact, 1 px | yes | yes | yes | **PASS** |

\* the desktop-icon coordinates of the drag/close scenario are fixed for the
1280x720 icon grid. No faults and no GPU soft timeouts in these runs.

Not covered natively: QEMU window resizing (the absolute path is computed by
QEMU against the guest surface, so window size does not enter the guest),
runtime mode switch via Settings (covered by the host tablet modeset tests),
Notepad text selection and scrollbars (not automated; same `mysz_x/mysz_y`
path as the drag/click checks).

### Not verified / manual procedure

The SDL conversion of the **physical** host mouse cannot be driven from this
session (injecting X events would move the user's real pointer; the VirtualBox
absolute device cannot be synthesised). Native confirmation with the physical
mouse is therefore **pending**. Procedure:

1. `make runuefi` (unchanged command; it now adds `virtio-tablet-pci`).
2. Wait for the desktop. Do **not** press Ctrl+Alt+G; the cursor should follow
   the host pointer without a click/grab.
3. Move right, left, up, down and diagonally; the guest cursor must stay under
   the host pointer position. Resize the QEMU window and repeat.
4. Drag a window title bar, double click a desktop icon, open Start.
5. Optional A/B: `make runuefi QEMU_DISPLAY_ARGS="-display gtk"` and the old
   behaviour by removing the tablet (`make -s runuefi-argv` shows the argv).

Windows hosts and physical machines were not tested. Real hardware relevance:
the PROP_DIRECT rule follows Linux semantics for evdev-style devices; PS/2,
USB boot mouse and USB HID tablet decoding are unchanged and their suites pass.

---

## Phase E — performance

*Measured* with the existing benchmark `narzedzia/graphics_perf_qemu.py
--production-iso` (UEFI 1280x720, 24 window drags, idle host, same session,
run back to back), old driver ISO (`833e246`) vs this work:

| Metric (drag phase) | base | recovery |
|---|---|---|
| Frames / full frames | 28 / 0 | 27 / 0 |
| Compose, mean | 23.28 ms | 23.10 ms |
| Present, mean | 1.40 ms | 1.17 ms |
| Frame total mean / p95 | 24.68 / 33.85 ms | 24.28 / 30.16 ms |
| Input → submit, mean | 2.23 ms | 2.03 ms |
| GPU commands / GPU wait | 103 / 48.6 ms | 101 / 40.4 ms |
| Framebuffer re-creations | 0 | 0 |
| Cursor presents | 23 | 23 |
| CR4 / EFER (SMEP, SMAP, NX) | 0x300678 / 0xd01 (all on) | identical |

Differences are within run-to-run noise; no extra full redraws, framebuffer
reallocations, polling or logging on the healthy path. Mouse input stays on
the existing path (one shared rounding function replaces the previous
truncating expression).

---

## Security

No change to Ring 0/3 isolation, NX/W^X, SMEP/SMAP, ASLR, stack protector,
executable verification or mappings. DMA ownership is stricter than before
(ATTACH_BACKING no longer writes a possibly device-owned buffer). All new
arithmetic on coordinates and queue indices is 64-bit/16-bit modular and
range-checked. `__kernel_end` 0xD1A000 → **0xD1B000** (headroom 1,940 KiB;
15 MiB limit unchanged).

## Changed files (this task only)

Production: `sterowniki/virtio/virtio_gpu.{h,cpp}`,
`sterowniki/virtio/virtio_input.{h,cpp}`, `sterowniki/wejscie/input_device.cpp`,
`jadro/gui/pointer_coordinates.h`, `jadro/gui/skladacz_obrazu.cpp` (idle-loop
recovery service), `Makefile` (tablet, new test target).
Tests/tools: `testy/test_virtio_pci.cpp`, new `testy/test_virtio_gpu_recovery.inc`,
new `testy/test_mouse_pipeline_host.cpp`, `testy/test_splash_production_path.cpp`,
`testy/test_pointing.cpp`, `testy/test_pointer_regression.cpp`,
`testy/test_pointer_graphics.cpp`, `testy/test_input_coexistence.cpp`,
`testy/pointer_transport_stream.h`, `testy/test_ohci.cpp`,
`testy/test_xhci_pointer.cpp`, `testy/test_launcher_parity.py`,
`testy/test_windows_launcher_config.py`, `testy/test_virtio_input_qemu_parity.py`,
new `narzedzia/mouse_pointer_qemu.py`, `narzedzia/bsf2_native_qemu.py`
(PERMANENT_FAILURE fault + recovery counters), `narzedzia/virtio_gpu_ab_qemu.py`
(recovery totals). Docs: this file.

## Reproduction

```
make test-virtio-pci                 # 185 cases, ASan/UBSan
make test-mouse-pipeline-host        # 5 resolutions
make test-pointing-host test-pointer-regression-host test-pointer-graphics-host
make test-launcher-parity test-windows-launcher
make iso
python3 narzedzia/mouse_pointer_qemu.py [--video 1920x1080] [--bios] --out .bsf-test/mouse
python3 narzedzia/virtio_gpu_ab_qemu.py --rounds 4 [--cpu-burners 2] \
    --variant base=<old.iso>:<old-tree> --variant recovery=BursztynOS.iso:. --out DIR
```
Final ISO SHA-256 `f70851d8673a660b12ccd694287cb2431c273accd7094688d1d08919387901ea`
(41,596,928 B, `make -j4 iso`).

Logs: `.bsf-test/mouse-final/`, `.bsf-test/mouse/<mode>/{mouse-native.json,serial.log,*.png}`,
`.bsf-test/gpu-recovery/{heavy,moderate,idle}/summary.json`, `.bsf-test/perf/{base,recovery}/result.json`.
