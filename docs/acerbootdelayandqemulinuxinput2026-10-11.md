# Acer boot delay and Linux QEMU input — 2026-10-11

## 1. Physical Acer pre-splash delay

Status: candidate repair, awaiting manual Acer Nitro AN515-54 acceptance. The user's physical evidence establishes that the current touchpad and wired USB mouse coexist correctly. Their decoders, cursor mapping, contact handling and pointer admission semantics were preserved. No physical boot measurements or runtime PASS are claimed for these new images.

The previous `usb_obsluz()` could execute an entire connected-port enumeration in one call. `execute_command()` polled command completion, and `usb_control_transfer()` polled transfer completion, each with a one-second deadline. Seven slow successful EP0 requests alone could monopolize the boot CPU for seconds. Deferring that call did not make it incremental, and framebuffer allocation did not establish that a welcome frame was visible.

The replacement is in `sterowniki/usb/xhci_enumeration.inc`. The existing singleton controller has one serialized command/EP0 enumeration lane. Root ports independently retain Empty, Queued, Enumerating, Active, Failed and Retiring state, slot identity, connection generation and pending scan state. Devices independently retain enabled, addressed, retiring, control failure, HID readiness and InputDevice registration/armed/proven state. Live HID rings operate alongside the cold lane.

Progression is: select connected port; submit USB2 reset or appropriate USB3 reset (skip enabled USB3 U0); wait reset change; submit Enable Slot; wait completion; prepare contexts; submit Address Device; wait completion; submit first descriptor; wait completion; validate MPS; optionally submit/wait Evaluate Context; submit/wait complete device descriptor; submit/wait configuration header and body; parse bounded configuration; prepare endpoint context; submit/wait Configure Endpoint; submit/wait SET_CONFIGURATION; submit/wait class protocol; submit/wait report descriptor or keyboard idle request; register pointer; submit first interrupt IN. Registration leaves a pointer armed, and the existing valid-live-report path proves it active. Optional Boot Mouse report descriptors and asynchronous EP0 Stall recovery preserve the working fallback and wheel behavior.

Every command, control transfer and reset request records its identity and a saturating monotonic deadline. A submit state always returns; completion is consumed in a later service call, including when hardware completed immediately. The event ring validates aligned in-ring physical TRB identity, slot and endpoint; control completion additionally validates completion code and residual bounds. A pending operation checks its deadline once per invocation. A completed valid event wins over a later service-time deadline check. Timeout fails that operation/device; it does not spin or retry on every tick. No timeout was shortened or increased. Reset Endpoint and Set TR Dequeue recovery are themselves separate submit/wait states.

Work budget per deferred xHCI invocation: at most **32 already-completed event TRBs**, **one root-port scan** (pending port events take priority), **one enumeration state transition**, and **eight completed HID reports** while scanning at most the configured slot limit (32). Idle selection scans at most two bounded arrays of 255 software port entries. Descriptor parsing/allocation is bounded by the existing 4096-byte configuration/report limit. There is no loop waiting for a future completion. OHCI deferred service still processes its existing completed input work; it does not enumerate from this service call. No watchdog, source-switch delay, sleep or global lock was added.

Disconnect withdraws only that device's input source. Pending command identity is resolved before the shared lane is reused. Disable Slot is submitted and awaited asynchronously. DMA is freed only after confirmed Disable Slot; failed retirement retains/quarantines it. Coalesced reconnect CSC changes connection generation and retires the previous slot before replacement enumeration. A corrupt Enable Slot completion cannot retire another live device. A failed connection is tried again only on a new connection/CSC, not on its own PRC/WRC event. Empty ports never receive reset or enumeration commands.

The hard IRQ handler is unchanged: acknowledge/mask its interrupter and mark pending work. It performs no enumeration, descriptor parsing or ring draining. New deferred transitions do not change IF, acquire global input/PCI/graphics locks, or wait with IF disabled. Existing IRQ setup/cleanup uses its save/restore guards; both incoming IF values and error paths remain covered by host tests.

**What remains synchronous:** existing early PCI/BAR discovery, BIOS ownership handoff and legacy SMI masking, controller halt/HCRST/CNR/Run MMIO transitions, scratchpad/DCBAA/ring allocation and IRQ setup. These precede controller DMA operation, retain their existing bounded hardware deadlines and early PS/2 ordering, and are timed individually. Root-port reset, every enumeration command, EP0 request, recovery command and disconnect Disable Slot now yield. Early bring-up has not been claimed to be the measured Acer culprit. In particular, an actual slow early handoff/reset or a non-USB boot stage remains distinguishable in the new physical timeline.

The functional publication barrier is independent of diagnostic flags. `grafika.cpp` records successful framebuffer publication only after `PublikacjaUdana()`. The splash module records first visible welcome publication only when the complete welcome surface is ready and that publication succeeds. Deferred enumeration cannot start until this **visible splash** milestone. Mapping, backend selection and framebuffer publication alone cannot release an enabled splash's barrier. For explicitly disabled splash modes, a successful first framebuffer publication releases input work; existing bypass behavior remains usable. Both native and VirtIO GPU backend-selection paths are recorded. The splash duration, holds, crossfades, exit fade and absolute-time origin were not changed.

No source of the visible dash was proven. No dash-hiding code was added. There are no physical before/after measurements yet; therefore the exact Acer hardware stage, or whether firmware before kernel entry contributes, is still open. The code audit and deterministic replay prove the synchronous software defect and remove it from presentation admission, not the physical runtime result.

### Deterministic measurements and diagnostics

`narzedzia/test_xhci_incremental_before_after.py` replays the saved pre-task driver and current driver on the same simulated registers. Clock reads advance by 100 us, successful device control latency is 400 ms/request, and resumed service has 10 ms scheduler intervals. These are **host simulated timings**, not CPU benchmarks or physical Acer measurements. Extra recorder clock reads explain the synthetic early-controller difference.

| Measurement (ns) | Before | After |
|---|---:|---:|
| Early controller ready | 2,800,000 | 5,100,000 |
| Work before modeled first visible frame | 2,810,900,000 | 5,100,000 |
| Largest deferred invocation | 2,804,300,000 | 800,000 |
| Total to HID armed, including yielding | 2,810,900,000 | 3,171,300,000 |
| Control requests | 7 | 7 |

The independent incremental tests, using 1 us clock-read increments, exercised 2,699 service calls with a maximum 8,000 ns modeled invocation. They also test Enable/Address/control submission returning before completion, later completion, a near-deadline descriptor, command/transfer failures, short packets, timeouts, no tick retries, 1/20/255 empty ports, multiple ports, a slow device, disconnect during EP0 DMA, reconnect after failure, reconnect during pending Enable Slot, and rejection of a completion naming an existing live slot. BIOS ownership already completed/requiring handoff and controller timeout bounds remain covered separately. Their timing figures must not be interpreted as measured instruction execution time.

`BURSZTYNDIAGBOOTXHCITIMING=1` enables only emission of the common bounded recorder. It retains up to 192 timeline transitions and emits at most two lines per service call. The clock reads, barrier, states, deadlines, DMA, masks and submissions are identical when the flag is zero. There are no per-tick waiting messages. Each timeline line has `label elapsed-ns detail`; early pre-HPET timestamps use a frozen TSC/HPET calibration, later timestamps use monotonic HPET time relative to that anchor. Existing bounded phase records additionally measure controller and USB stages. Paired early-stage labels delimit entry/exit.

On the Acer, look for this ordering:

```text
kernel entry
framebuffer mapping ready
HPET ready
PCI scan
USB synchronous before splash
BIOS handoff
controller halt
controller reset
CNR wait
scratchpads / DCBAA / command ring / event ring / IRQ setup
xHCI controller ready
PS2 initialization
block probe begin / block probe end
graphics probe begin / graphics probe end
graphics backend selected
first framebuffer publication
first visible splash frame
xHCI deferred enumeration begin
connected port selected
port reset submit / port reset complete
Enable Slot submit / Enable Slot complete
Address Device submit / Address Device complete
control submit / control complete (each request)
Configure Endpoint submit / Configure Endpoint complete
first interrupt IN submit
xHCI device armed
first HID input completion
xHCI device active
```

The first two visible-publication markers can share nearly the same time. The first control submit must follow first visible splash. Control-submit detail is `(bRequest << 16) | wValue`; completion detail is `(completion_code << 24) | actual_length`. Command detail identifies command type (9 Enable Slot, 11 Address Device, 12 Configure Endpoint); completion detail includes slot and completion code. Timeout records appear once per failed operation. HPET, scheduler, crypto, APIC, PCI, PS/2 and block/graphics probe milestones expose gaps outside enumeration. Subtract milestone timestamps to distinguish kernel-entry-to-frame, framebuffer-ready-to-splash and post-splash device availability. The exact physical delay stage remains unmeasured until these logs are supplied.

Changed production files for boot/input observation: `boot_phase_timing.h`, `ekran_powitalny.cpp`, `grafika.cpp`, `kernel.cpp`, `sterowniki/usb/xhci.cpp`, new `sterowniki/usb/xhci_enumeration.inc`. The additional VirtIO observation files and launcher/build changes are listed in section 2. Working touchpad and USB HID decoders, coordinate/contact mapping and source acceptance were not modified.

Manual physical acceptance: production ISO without USB mouse, then with both devices, alternation without teleporting, USB disconnect leaving touchpad working, and first visible splash without the former pause. The timing diagnostic ISO must show slow enumeration after first visible splash. Physical runtime acceptance remains manual.

## 2. Linux make runuefi versus current Windows launcher

The pre-task worktree's Windows launcher was already manually edited and working according to the user. Those edits were preserved. The comparison used that current file, not its older Git version or comments. Read-only capture files are in [the audit directory](acer-boot-qemu-audit-2026-10-11/). PowerShell 7.6.6 was executed on Linux to evaluate the actual argument-construction path, with explicit host binary/firmware fixture paths. The Windows QEMU installation itself was not available or measured. Default Windows print/audit mode now resolves its actual installed binary and read-only firmware selection; CODE+VARS selection computes the same merged-image path without writing it.

### Exact launcher comparison

The saved pre-task Linux argv differs from the current Windows profile as follows:

| Setting | Linux before | Working Windows / canonical after |
|---|---|---|
| Accelerator | `tcg,thread=multi` | `tcg,thread=single` |
| Pointer device | `virtio-tablet-pci` | `virtio-mouse-pci` |
| RNG object | `rng-random,id=rng0,filename=/dev/urandom` | `rng-builtin,id=rng0` |
| Boot order/options | `order=d` | `order=d,menu=on` |

These unintended profile differences were corrected in Makefile. No guest VirtIO Input functional defect was demonstrated, and no guest driver behavior was changed. Windows' relative mouse is the canonical input type; the tablet was not substituted as a workaround. Existing Windows single-thread TCG and HDA-before-VirtIO ordering were retained. Linux ALSA and Windows `none` audio backends remain as they were; both still instantiate Intel HDA and its codec before VirtIO, preserving PCI topology.

Intentional host differences: SDL versus GTK display, ALSA versus no host audio, host binary/disk/ISO/firmware paths, Windows VM display name, and serial-console option placement. The `-serial stdio` value is identical. All device order and device/property strings remain compared exactly. Input devices have no explicit `disable-legacy` override in either profile; BLK/NET/RNG/GPU have `disable-legacy=on`. Both use machine `pc,hpet=on`, CPU `max`, one vCPU, 2G RAM, `-vga none`, VirtIO VGA 1280x720, disk ID `disk`, netdev ID `n1`, RNG ID `rng0`, and audio backend ID `snd`. No optional hardware properties or device IDs are normalized away.

Exact normalized final argv arrays (path placeholders only; order and all properties retained):

```json
{
  "linux": [
    "<QEMU>",
    "-bios",
    "<FIRMWARE>",
    "-machine",
    "pc,hpet=on",
    "-cpu",
    "max",
    "-accel",
    "tcg,thread=single",
    "-smp",
    "1",
    "-m",
    "2G",
    "-audiodev",
    "alsa,id=snd",
    "-device",
    "intel-hda",
    "-device",
    "hda-output,audiodev=snd",
    "-serial",
    "stdio",
    "-display",
    "sdl",
    "-drive",
    "id=disk,file=<DISK>,format=raw,if=none",
    "-device",
    "virtio-blk-pci,drive=disk,disable-legacy=on",
    "-netdev",
    "user,id=n1",
    "-device",
    "virtio-net-pci,netdev=n1,disable-legacy=on",
    "-object",
    "rng-builtin,id=rng0",
    "-device",
    "virtio-rng-pci,rng=rng0,disable-legacy=on",
    "-vga",
    "none",
    "-device",
    "virtio-vga,disable-legacy=on,xres=1280,yres=720",
    "-device",
    "virtio-keyboard-pci",
    "-device",
    "virtio-mouse-pci",
    "-boot",
    "order=d,menu=on",
    "-cdrom",
    "<ISO>"
  ],
  "windows": [
    "<QEMU>",
    "-bios",
    "<FIRMWARE>",
    "-machine",
    "pc,hpet=on",
    "-cpu",
    "max",
    "-accel",
    "tcg,thread=single",
    "-smp",
    "1",
    "-m",
    "2G",
    "-serial",
    "stdio",
    "-display",
    "gtk",
    "-audiodev",
    "none,id=snd",
    "-device",
    "intel-hda",
    "-device",
    "hda-output,audiodev=snd",
    "-drive",
    "id=disk,file=<DISK>,format=raw,if=none",
    "-device",
    "virtio-blk-pci,drive=disk,disable-legacy=on",
    "-netdev",
    "user,id=n1",
    "-device",
    "virtio-net-pci,netdev=n1,disable-legacy=on",
    "-object",
    "rng-builtin,id=rng0",
    "-device",
    "virtio-rng-pci,rng=rng0,disable-legacy=on",
    "-vga",
    "none",
    "-device",
    "virtio-vga,disable-legacy=on,xres=1280,yres=720",
    "-device",
    "virtio-keyboard-pci",
    "-device",
    "virtio-mouse-pci",
    "-boot",
    "order=d,menu=on",
    "-cdrom",
    "<ISO>",
    "-name",
    "Bursztyn OS - UEFI VirtIO Full"
  ]
}
```

The machine-comparable arrays, including their pre-task versions, are also retained as `linux-before.json`, `windows-before.json`, `linux.json`, `windows.json`, and `normalized-comparison.json`. Firmware mode is **not** normalized away: this Linux installation selects monolithic `/usr/share/ovmf/OVMF.fd` with `-bios`, matching Windows' `-bios` mode. Linux's existing CODE+VARS pflash fallback remains supported and will be exposed as a distinct argv if selected; a comparison against Windows then requires explicit acknowledgment of that firmware-mode difference. No pflash path is silently turned into a BIOS option.

Canonical PCI creation order and expected BDFs under this unextended i440fx profile:

| Order | Device | Guest class | Expected BDF |
|---|---|---|---|
| 1 | intel-hda | audio | 00:02.0 |
| 2 | virtio-blk-pci | storage | 00:03.0 |
| 3 | virtio-net-pci | network | 00:04.0 |
| 4 | virtio-rng-pci | entropy | 00:05.0 |
| 5 | virtio-vga | graphics | 00:06.0 |
| 6 | virtio-keyboard-pci | keyboard | 00:07.0 |
| 7 | virtio-mouse-pci | relative pointer | 00:08.0 |

`hda-output` is a codec on HDA's bus, not another PCI function. The expected BDFs are inferred from default host bridge/PIIX occupancy at 00:00.0 and 00:01.*, `-vga none`, no other added PCI devices, and QEMU's first-free-slot allocator. They are not runtime measurements and are not hardcoded in the kernel. The derivation uses [QEMU i440fx initialization](https://raw.githubusercontent.com/qemu/qemu/v8.2.2/hw/pci-host/i440fx.c), [PC machine initialization](https://raw.githubusercontent.com/qemu/qemu/v8.2.2/hw/i386/pc_piix.c), and [PCI allocation](https://raw.githubusercontent.com/qemu/qemu/v8.2.2/hw/pci/pci.c). A different `pc` alias or added device can change these predictions; the guest diagnostic prints actual BDFs.

`make runuefi-argv` and `./runwindows.ps1 -PrintArgv` print JSON without creating a disk, copying firmware or starting a VM. The normal launchers print intended PCI topology before runtime. `make runuefi-audit` and `./runwindows.ps1 -Audit` also query binary path, host OS, QEMU version, machine availability and `virtio-mouse-pci,help` properties. Only version/help queries were executed here. The collected Linux version is **QEMU 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18)**. Its mouse supports `disable-legacy=auto`, `disable-modern=false`, vectors, event_idx and the other properties retained in `linux-version-properties.json`. The PowerShell collector was separately exercised against that Linux binary; its output is explicitly labeled as Linux-host collection. The actual Windows QEMU release and its resolved `pc` alias remain unknown. No guest change was made on a version hypothesis.

### Guest VirtIO Input checkpoints

`BURSZTYNDIAGVIRTIOINPUTQEMU=1` enables only bounded deferred emission. Both builds execute the same recorder and observation reads. It cannot select another driver path, set status, change queue depth/timing, notify a queue, acknowledge an ISR, alter a mask, register a device or admit input. The recorder has 512 records, eight source identities and eight bounded names; it emits at most two lines per deferred call. First-event categories are recorded once per source. The IRQ handler remains unchanged and bounded; the first IRQ is observed later from its existing atomic count.

Look for these per-BDF markers:

```text
[VINPUT PCI] vendor/device/virtio-id
[VINPUT transport] modern/transitional
[VINPUT status] reset
[VINPUT status] ACK/DRIVER
[VINPUT features] offered/accepted/FEATURES_OK
[VINPUT name]
[VINPUT EVBITS] type/low64/buttons272-279
[VINPUT KEYBITS] buttons272-279/touch330
[VINPUT config generations] select-subsel/before/after
[VINPUT classification] keyboard/relative/absolute
[VINPUT queue count]
[VINPUT queue ready] selected/size/posted
[VINPUT ring physical] desc/avail/used
[VINPUT interrupts] MSIX/MSI/vector
[VINPUT status] PREPARED
[VINPUT DRIVER_OK store]
[VINPUT DRIVER_OK readback]
[VINPUT registered] state/active
[VINPUT first IRQ]
[VINPUT first used ring change]
[VINPUT first event] type/code/value
[VINPUT first EVREL] code/value
[VINPUT first SYN] code/value
[VINPUT first pointer publication] mode
```

EVBITS type 1 is KEY, 2 is REL and 3 is ABS. Empty capability responses remain unsupported by the existing classifier; ordinary initialization failure messages identify unsupported/invalid capabilities. Names are readable; bitmap/feature/address fields are numeric. BDF is encoded `(bus << 16) | (slot << 8) | function`. The ring bases are derived from the existing queue's DMA base and offsets, not extra queue setup. Config generation before/after is observation only, with no new retry or acceptance behavior.

The initialization audit confirms the existing two-phase sequence: reset; ACKNOWLEDGE; DRIVER; FEATURES_OK; queue 0 ready; writable buffers posted; interrupt route established while masked; PREPARED without DRIVER_OK/registration; common late activation barrier; one DRIVER_OK store and readback; registration; interrupt unmask; initial completion drain and notification. Registration cannot mark PROVEN_ACTIVE. Preferred source metadata does not gate the common pipeline. The new common-pipeline hook records only its first actual successful VirtIO pointer publication; it does not alter it. Existing queue ownership, feature negotiation, IRQ delivery, decode and coexistence logic were preserved.

Compare manual Linux/Windows logs at the first missing marker for the same relative device. DRIVER_OK on both but IRQ/used activity on only Windows leaves host topology/interrupt delivery open. IRQ without used advance narrows it to queue/transport publication or notification. Used events without EVREL suggest device/type/capability differences. EVREL plus SYN without final publication narrows it to report/common pipeline admission. No such runtime divergence has yet been supplied, so a guest driver defect remains unproven. Matching argv is not a mouse runtime PASS.

Manual launch examples (these commands were **not** executed here):

```sh
make runuefi
make runuefi RUNUEFI_ISO=BursztynOS-virtio-input-qemu-diag.iso
make runuefi-audit > linux-launcher-audit.json
```

```powershell
.\runwindows.ps1 -Iso .\BursztynOS-virtio-input-qemu-diag.iso
.\runwindows.ps1 -Audit > windows-launcher-audit.json
```

`RUNUEFI_ISO` changes the runtime CD only; launch preparation does not rebuild that diagnostic image from a production kernel. Default `make runuefi` retains production ISO selection. The two generated hardware profiles match except for the documented host-only differences. Linux keyboard/mouse runtime acceptance remains manual.

### Changed files, validation and artifacts

All changed production code: `boot_phase_timing.h`, `ekran_powitalny.cpp`, `grafika.cpp`, `input_device.cpp` (observation hook only), `kernel.cpp` (milestones only), `sterowniki/usb/xhci.cpp`, new `sterowniki/usb/xhci_enumeration.inc`, `virtio_input.cpp` (observation only), new `virtio_input_qemu_diag.h`. Launcher/build files: `Makefile`, `runwindows.ps1`, new `narzedzia/qemu_launcher_audit.py`. No touchpad decoder, HID decoder, filesystem, installer, application, networking, audio driver or desktop layout source changed. Generated `.d` dependency updates are build metadata.

New test files: `narzedzia/test_xhci_incremental.cpp`, `narzedzia/test_xhci_incremental_before_after.py`, `narzedzia/test_launcher_parity.py`, `narzedzia/test_virtio_input_qemu_checkpoints.inc`, `narzedzia/test_virtio_input_qemu_parity.py`. Existing test changes: `test_input_diagnostic_parity.py` adds the new state-machine/flag cases; `test_xhci_physical_mouse.cpp` supplies delayed command fixtures and drives enumeration across service calls; `test_xhci_boot_coexistence.cpp` retains failure/ownership/coexistence assertions across incremental service; `test_xhci_pointer.cpp` explicitly submits/awaits Configure Endpoint instead of calling the removed synchronous wrapper; `test_powitanie.cpp` supplies the shared nanosecond clock stub; `test_virtio_pci.cpp` includes the new synthetic input case; `test_windows_launcher_config.py` replaces historical tablet/multi expectations with the user-required current mouse/single profile and retains nine policy tests. Runtime/input assertions were not weakened to permit broken behavior.

Exact final host results (component totals are not added again where they appear inside a larger fixture):

| Suite | Result |
|---|---|
| PS/2 parser / HID descriptors / USB config / InputDevice | 330,443 / 6,051 / 48 / 146 checks PASS |
| i8042 controller / OHCI stream / OHCI production path | 4,140 / 5,400 / 5,855 checks PASS |
| xHCI IRQ / ring / context / failed submit / stream / completions | 133 / 1,030 / 22 / 24 / 4,800 / 9,484 checks PASS |
| Signed cursor / modes-readiness / boot order | 396,600 / 56 / 6 checks PASS |
| Real physical-style xHCI fixture | 24,033 checks each, production and old physical diagnostic builds; 248 allocations/releases each |
| IRQ allocator / init locks | 3,740 / 4 checks PASS |
| Incremental state machine | 17,466 checks per mode, five modes; 2,699 invocations; 10,822 identical ordered functional records |
| Direct input coexistence | 132 checks per mode, five modes; 51 identical ordered records |
| Boot/controller/coexistence fixture | 27,884 checks per mode, five modes; 21,862 identical ordered records |
| Diagnostic functional comparisons | 12 comparisons PASS across those three fixtures |
| VirtIO transport/input full suite | 167 named cases each with new diagnostic off/on; 334 total named executions, zero failures |
| VirtIO diagnostic parity | 6 ordered lifecycle/report records identical |
| Real pointer/graphics | 40 checks PASS |
| Compositor | 74,293 checks PASS, including its 73,816 popup-restoration checks |
| Cursor / cursor diagnostics | Both host suites PASS (no numeric case counter) |
| Splash timing / first frame / snapshots / production scanout | 2,185 / 101 / 342 / 722 checks PASS |
| Keyboard / text input | 7,291 checks PASS |
| Scheduler/concurrency | 10 cases PASS |
| Launcher generated argv parity / policy | 8 checks / 9 unit cases PASS |
| Before/after controller replay | 2 driver variants PASS |

ASan: PASS for applicable C++ host suites, including both VirtIO modes, every diagnostic parity variant and both before/after replays; no sanitizer finding. UBSan: PASS for the same applicable suites; no finding. The Python policy/argv checks have no sanitizer instrumentation. Test logs, exact argv, source snapshots and modeled timings are retained in the linked audit directory. No QEMU or VirtualBox VM was launched, no physical runtime PASS is claimed, and no `make clean` or `make rm` was used.

Final production build: **PASS**, using exactly `make -B -j2 all iso` (exit 0). The boot timing diagnostic was built separately with `BURSZTYNDIAGBOOTXHCITIMING=1`; the VirtIO diagnostic was built separately with `BURSZTYNDIAGVIRTIOINPUTQEMU=1`. Both diagnostic builds passed. Production defaults leave both flags, the old physical-mouse flag and input-coexistence diagnostic emission disabled. No initialization or input feature depends on the new diagnostic flags.

Each ISO's `/boot/system_operacyjny.bin` was extracted and compared byte-for-byte with its saved final kernel. All three matched. Symbol inspection confirms the production kernel has neither new diagnostic emitter; each diagnostic kernel has only its intended emitter. Common bounded observation records are deliberately present in all builds to keep observation reads and functional timing paths equivalent.

| Artifact | SHA-256 |
|---|---|
| `BursztynOS.iso` | `6d555ad7a9c7da9e9b2739fcc478302c1181ee1d0a7e3e4931842e69b3b2d95e` |
| `BursztynOS-boot-xhci-timing-diag.iso` | `29900f028f25ac277e75afec08b53c2246dfb6ef1473331c26d1a05a873b553a` |
| `BursztynOS-virtio-input-qemu-diag.iso` | `97e1ee1025e52d7113fbbce03744d15bf6e844309ffba06fbbffb74dce528262` |

The [machine-readable hash manifest](acer-boot-qemu-audit-2026-10-11/SHA256SUMS) and [ISO verification results](acer-boot-qemu-audit-2026-10-11/iso-verification.json) accompany the build and test transcripts. Both remaining runtime issues await manual acceptance: these results establish build correctness, deterministic behavior and launcher parity, not Acer boot-delay or Linux mouse runtime PASS.
