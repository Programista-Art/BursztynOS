# Two-phase VirtIO input startup — 2026-10-10

Continued from the current worktree, preserving the preceding fixes. The new runtime evidence locates the Installer freeze at the tablet's synchronous `device_status` MMIO activation store: phase 0 is visible, phase 2 is absent, IF is clear, and no interrupt/exception/NMI entry is recorded. This change removes that store from early input probing for **both keyboard and tablet**. It adds no sleep, timeout, activation retry, device substitution, or Installer-only hardware path. Runtime acceptance remains manual; neither ISO has been launched here.

This report supersedes the activation design in [the preceding diagnostic investigation](virtio_input_driverok_activation_2026-10-10.md).

## Old and new startup boundary

Previously `kernel_main` called `virtio_input_probe_and_init()` after RNG/BLK/GPU/NET discovery, before HDA, PSF/filesystem/service bootstrap, application resource preparation and final display startup. The probe called `virtio_input_init()`, which performed the complete sequence including DRIVER_OK, registration, unmask and notification in that early hardware path.

Now that location calls `virtio_input_probe_and_prepare()`. Each device stops at **PREPARED**, with exact status `0x0B`, existing DMA allocations and negotiated features retained, queue buffers already posted, permanent IRQ context installed, MSI-X enabled but its queue entry masked, and config interrupts set to NO_VECTOR. No InputDevice is registered, readiness is false, polling is inert, and no queue notification is sent. PS/2 remains the temporary pointer owner.

The single mode-independent late barrier is in `kernel_main`, immediately **after** `PrezentacjaStartu::zakoncz_inicjalizacje()` and the existing optional GPU modeset self-test, **before** the first `bws_uruchom_program_z_pliku("/menedzer_okien.bur", ...)`. At this point memory/IDT/APIC, PCI and hardware bootstrap, PSF and filesystem scaffolding, embedded application resources (including Installer resources when selected), theme/wallpaper/display startup are complete. No Ring3 GUI process exists yet.

`InicjalizujPlaniste()` already occurs before PCI probing in this source. It remains there; the late barrier is after its completion in both modes. PID0 is initialized, while scheduler dispatch and the existing final STI remain at their original later locations. Activation neither enables IF nor changes scheduler policy. Thus both normal and Installer activation occur with the same scheduler-readiness guarantees and the same completed-bootstrap boundary, although wall-clock timing and allocated physical addresses can still differ.

The host boot-order regression checks these necessary relations, rather than requiring scheduler initialization to be moved past unrelated hardware work:

```text
memory / IDT / APIC / scheduler construction
PCI and hardware discovery
VirtIO input preparation only
remaining hardware and filesystem/service/application/display bootstrap
common late input activation
Ring3 window manager and desktop / Installer process creation
existing scheduler-dispatch enable and STI
```

## Software and hardware lifecycle

Successful state transitions are:

```text
UNINITIALIZED -> RESETTING -> NEGOTIATING -> QUEUE_SETUP
 -> BUFFERS_POSTED -> INTERRUPTS_READY -> PREPARED
 -> ACTIVATING -> DRIVER_READY -> REGISTERED -> ACTIVE
```

The previous state numeric values are retained. PREPARED=9, ACTIVATING=10 and ACTIVE=11 are appended; DRIVER_READY=6 and REGISTERED=7 still describe their original software milestones.

Normal initialization failure follows `current -> FAILED -> RESETTING -> UNINITIALIZED -> FAILED` when reset is acknowledged. A rejected reset ends in FAILED with DMA quarantined instead of released. Explicit successful teardown follows `current -> RESETTING -> UNINITIALIZED`, including teardown of a PREPARED device.

`virtio_input_activate_prepared()` iterates the retained driver instances through the same `input_activate_batch()` / `virtio_input_activate()` implementation in every boot mode. The late pass opens an activation gate; per-device activation refuses an unopened barrier. A PREPARED-to-ACTIVATING atomic transition reserves the one activation attempt. The pass does not reset, negotiate, allocate, free, recreate queues, replace buffers or reinstall IRQ contexts for a prepared device.

For each device it checks the retained pre-activation status, performs a full compiler/CPU publication fence, executes the one DRIVER_OK status store and requires a successful readback. It then sets DRIVER_READY, registers the InputDevice, sets REGISTERED and publishes `initialized` with release ordering. Only then is the queue MSI-X table entry unmasked. A bounded used-ring service pass drains completions that arrived synchronously during DRIVER_OK or while masked, even if no IRQ hint was delivered. Finally the required forced initial queue notification is sent and state becomes ACTIVE.

Registration is after successful DRIVER_OK readback; no registered-but-inactive object is needed. Before registration the IRQ handler only counts/records pending work, and polling cannot consume or dispatch it. The successful tablet takes pointer ownership at registration; failed DRIVER_OK does not register it and leaves PS/2 as owner. Failure after publication unregisters the device through the existing cleanup path and restores remaining input ownership.

MSI-X entry zero is programmed masked, assigned to queue zero with readback, and PCI MSI-X is enabled during preparation. Config MSI-X is NO_VECTOR with readback. The queue entry remains masked across the entire PREPARED interval and DRIVER_OK. Its first unmask follows InputDevice registration and readiness publication. Existing INTx suppression and the existing polling fallback for unsupported/rejected MSI-X remain intact; MSI-X has not been disabled to work around the reported stall.

Reposting completed buffers may generate a queue notification during the post-readiness drain; the forced initial notification follows that drain. Both occur after successful DRIVER_OK. [VirtIO 1.2 §3.1](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html) permits queue population before DRIVER_OK and prohibits available-buffer notification before it. No notification is issued during preparation or after a rejected activation.

Normal status rejection fails that optional input device, preserves the pass over the remaining devices and uses existing confirmed-reset cleanup. DMA is freed only after reset acknowledgement; a stuck reset retains its quarantine. A synchronous store that never returns cannot be rescued by a one-vCPU guest timeout; no such recovery is modeled or claimed.

An ACTIVE instance returns success immediately on a repeated activation pass without hardware mutation, registration or allocation. FAILED and other non-PREPARED states do not retry the activation store. Repeated probing skips already retained PREPARED/ACTIVE instances. Preparing an already-owned instance is rejected.

## Call sites and early-activation assertions

The former early DRIVER_OK was removed from the body of `virtio_input_init()` by splitting preparation and activation. The compatibility `virtio_input_init()` and Polish `virtio_input_zainicjalizuj()` now call preparation only, as does compatibility `virtio_input_probe_and_init()`. The normal/Installer kernel probe uses the explicit preparation API. No PCI probe callback activates input.

Automated input stress/teardown tests moved from the early probe to after the same late barrier. Their explicit reinitialization cycles use prepare followed by the authoritative late pass, and cannot start before the barrier has been opened. Reset, global reset, reboot and shutdown paths do not activate; the two existing syscall handoffs still only quiesce input. No copied Installer activation implementation exists.

The shared status writer rejects an input DRIVER_OK request while preparation is active and activation permission is absent. The host fake MMIO writer asserts that an activation store sees ACTIVATING and explicit permission. The host fake notify asserts successful DRIVER_OK plus software readiness and registration. Source regressions assert that preparation contains no DRIVER_OK, registration, activation or queue-kick operation; aliases remain prepare-only; activation contains one status write and no reset/queue creation/allocation/feature negotiation; no other production caller bypasses the centralized mechanism.

## Diagnostics

`BURSZTYNDIAGVIRTIOINPUTINIT` remains disabled by default. Enabled builds record:

- each device's PREPARED state and exact `0x0B` status;
- the common late kernel phase, normal/Installer mode, scheduler PID, PID0 state and dispatch-enabled flag;
- state immediately before and after activation;
- retained instruction-adjacent phase 0 / phase 1 / phase 2 observations around the late DRIVER_OK store, address, RFLAGS and store/next instruction addresses;
- DRIVER_OK readback, InputDevice registration/readiness publication, MSI-X unmask, first used index and initial notify;
- the first actual input event's type/code/value once per device, without logging every runtime event.

Existing bounded IRQ/exception/NMI observations and nonblocking lock/PCI/MSI-X/DMA ownership snapshots remain around the late activation store. Stage numbers may change; checkpoint labels and phase records identify the operation. The phase 1 RAM store, byte MMIO store and phase 2 RAM store remain consecutive instructions in diagnostic object code.

## Host coverage and results

New lifecycle tests cover keyboard and tablet individually and together; exact pre-activation `0x0B` versus successful late `0x0F`; retained queue/DMA/features/IRQ resources; masking, publication and notification order; one failed function with the other successful; PS/2 ownership before activation and after tablet rejection; PREPARED cleanup and changed-status failure before activation; synchronous ABS_X/ABS_Y/SYN_REPORT; forced early IRQ, masked pending IRQ and no-IRQ completion; disabled PREPARED delivery; no duplicate registration and repeated-pass idempotence. Existing initialization, descriptor ownership, InputDevice ownership, real PMM/DMA/module reservation, fault/quarantine and other VirtIO device suites remain included.

| Check | Result |
| --- | --- |
| `make test-virtio-pci` | 166 PASS, 0 FAIL, diagnostics disabled. |
| `make test-virtio-pci BURSZTYNDIAGVIRTIOINPUTINIT=1` | 167 PASS, 0 FAIL. |
| Production boot-order / caller regression | 1 PASS, 0 FAIL; also runs as part of both VirtIO targets. |
| `make test-input-init-locks` | 4 PASS, 0 FAIL. |
| `UBSAN_OPTIONS=halt_on_error=1 make installer-unit-test` | Storage/BSP2: 10,493 PASS, 0 FAIL; native directory/file move: 138 assertions PASS. |
| ASan | No findings in the completed final runs; leak detection enabled for VirtIO/lock suites, disabled by the existing Storage harness. |
| UBSan | No findings; halt-on-error enabled in the completed final runs. |
| Whitespace / `git diff --check` | PASS. |

| Build / artifact check | Result |
| --- | --- |
| `make -B -j2 all iso BURSZTYNDIAGVIRTIOINPUTINIT=1 ISO=BursztynOS-input-init-diag.iso` | Exit 0; diagnostic ISO produced. |
| Final `make -B -j2 all iso` | Exit 0; normal ISO produced with input-init diagnostics disabled. |
| Diagnostic object disassembly | PASS: phase 1 RAM store, byte MMIO store, phase 2 RAM store are consecutive; saved store/next addresses match the assembly labels. |
| Extracted ISO kernels | PASS: diagnostic markers present only in the diagnostic kernel; final local kernel matches the normal ISO kernel. |
| Extracted GRUB configurations | PASS: identical between normal and diagnostic ISO, with the same normal/Installer entries. |
| Launcher hash / disk size and mtime | PASS: unchanged. |

Build order was diagnostic first, then the required complete normal rebuild, leaving default workspace objects and kernel in the production configuration. Both builds passed Multiboot2 checks and emitted no compiler warnings. Existing asset-packaging warnings remain: absent `dzwiek.wav` uses the existing empty placeholder rule, and the unsafe wallpaper filename `Zamek w Pszczynie 2026r..jpg` is skipped.

| ISO | Bytes | SHA-256 |
| --- | ---: | --- |
| `BursztynOS.iso` | 35,274,752 | `9d66fd41aaddc44bb10f1fdc8a0bd6159948887ed545b214a147b2ef5a225713` |
| `BursztynOS-input-init-diag.iso` | 35,287,040 | `dbab3ea61dbc181f531757eca746297981ec829742ca11472bace2e84918586e` |

Verification logs are `/tmp/bursztyn-two-phase-host-normal.log`, `/tmp/bursztyn-two-phase-host-diag.log`, `/tmp/bursztyn-two-phase-locks.log`, `/tmp/bursztyn-two-phase-installer.log`, `/tmp/bursztyn-two-phase-build-diag.log` and `/tmp/bursztyn-two-phase-build-normal.log`. Extracted kernels/configurations and artifact checks are in `/tmp/bursztyn-two-phase-iso-check/`; diagnostic disassembly is `/tmp/bursztyn-two-phase-diag-virtio-pci.asm`.

## Files changed

Production/build files changed for this lifecycle implementation: `kernel.cpp`, `virtio_input.cpp`, `virtio_input.h`, `virtio_pci.cpp`, `virtio_pci.h`, `Makefile`.

Test files updated for two-phase use: `narzedzia/test_virtio_pci.cpp`, `narzedzia/mock_virtio_input_init.inc`, `narzedzia/test_virtio_input_init.inc`, `narzedzia/test_virtio_input_activation.inc`, `narzedzia/test_virtio_input_faults.inc`. New tests: `narzedzia/test_virtio_input_lifecycle.inc`, `narzedzia/test_input_boot_order.py`. The existing `narzedzia/test_input_init_locks.cpp` was retained and rerun.

The complete accumulated production/build worktree also retains preceding changes in `apic.cpp`, `heap.cpp`, `idt.cpp`, `input_device.cpp`, `input_device.h`, `pci.cpp`, `pmm.cpp`, `syscalls.cpp`, `virtqueue.cpp`, `virtqueue.h` and the new diagnostic headers `virtio_input_activation_diag.h`, `pmm_diagnostics.h`. These were not redesigned in this lifecycle change. Generated dependency/object/test artifacts are refreshed by compilation.

This report is new; the preceding activation report gains a superseded notice. The preceding boot-init report remains historical.

## Preservation and manual acceptance

No unrelated GUI source was changed for this request. Installer disk-write confirmation policy, VirtIO-BLK installer support, automatic Installer launch, normal/Installer splash policy, GPU publication and 500 ms timeout, PSF bootstrap, desktop overflow, recursive nonempty-directory deletion, Unicode/numeric input fixes, network initialization and restart behavior are preserved from the current worktree. Host tests validate their covered paths; this is not a runtime boot acceptance claim.

`runwindows.ps1` remains SHA-256 `b98a5d033842b53affab8707d80596a947672b1bd487a0a70672c9eb75103e81`, size 10,147, mtime `1791305757`. The existing `wirtualny_dysk.img` remains size 4,294,967,296 and mtime `1791478640`; no disk image was opened for writing. No QEMU, nested virtualization, QEMU test, installer QEMU test, `make rm` or `make clean` was run.

Manual acceptance uses the unchanged Windows launcher with both modern-only `virtio-keyboard-pci,disable-legacy=on` and `virtio-tablet-pci,disable-legacy=on`. Normal boot must reach a working desktop with keyboard and absolute tablet. Installer boot must pass the previous stall, show tablet late activation `phase=2 STORE returned`, complete registration and reach its GUI, accept both input devices, preserve zero disk-sector writes before GUI confirmation, finish installation and restart without freezing QEMU. These remain the user's runtime checks.
