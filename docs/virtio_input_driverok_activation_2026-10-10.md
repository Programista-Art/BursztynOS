# VirtIO tablet DRIVER_OK investigation — 2026-10-10

**Superseded:** newer instruction-adjacent runtime evidence led to the [two-phase PREPARED / late-activation lifecycle](virtio_input_two_phase_lifecycle_2026-10-10.md). The observations and one-phase design below describe the preceding investigation, not the current driver architecture.

The reported installer freeze is **not yet explained or proven fixed**. The user's last runtime checkpoint is the wrapper's `status store begin` at stage 180, for BDF `00:07.0`, changing status `0x0B` to `0x0F`. That observation alone cannot establish whether the guest executed or completed the actual MMIO instruction. No QEMU or nested virtualization was launched in this investigation. Runtime acceptance remains manual.

This report continues [the earlier initialization audit](virtio_input_boot_init_2026-10-09.md). Work resumed from session `01a11dfe-b20c-7023-a4ac-49dd4d0f9fdd` and its existing worktree; earlier fixes and generated files were preserved.

## What the evidence establishes

| Question | Result |
| --- | --- |
| Exact runtime root cause | Unknown; no host reproduction of the installer freeze. |
| Why stage 180 was the final visible line | It precedes activation, but the old trace does not distinguish a stalled MMIO instruction from an intervening IRQ, exception, or another stall before its next print. |
| Did the failing guest complete the MMIO write? | Unknown in the supplied log. New instruction-adjacent RAM breadcrumbs distinguish this on a subsequent diagnostic boot. |
| Did QEMU synchronously consume eventq at DRIVER_OK? | Unknown for the failing Windows QEMU run. Upstream source activates the HID handler synchronously, but does not itself unconditionally consume eventq or generate ABS events in that callback. |
| Did an interrupt fire before the next guest instruction? | Unknown in the supplied log. An injected interrupt inside the host status-store callback is handled safely. |
| Did an exception occur? | Unknown in the supplied log. Diagnostic entry records NMI/exceptions/IRQs before ordinary C dispatcher processing. |
| Which lock caused a deadlock? | None demonstrated. Source shows no heap/PMM/PCI/service/config lock carried into DRIVER_OK by this initializer. |
| Did DMA overlap installer modules or reserved memory? | Not in the tested production-PMM layouts. The actual failing guest's memory ownership still needs its diagnostic log. |
| Was PMM responsible? | Not established. No PMM reservation algorithm was changed. |
| Was memory ordering responsible? | Not established. Existing per-post barriers order ring publication; an additional activation fence is defensive. |
| Why normal boot succeeds and installer boot fails | Not established. Extra installer modules change reservations and allocation addresses; no separate input activation branch was found. |
| Why keyboard DRIVER_OK is unaffected | Not established. Both follow the same driver sequence, with separate transport/IRQ/DMA state. |
| Did the real tablet emit immediate ABS events at activation? | Unknown. Tests deliberately inject the whole report synchronously, without claiming QEMU actually did so. |

## Instruction-level diagnostics

`BURSZTYNDIAGVIRTIOINPUTINIT` defaults to zero. In the diagnostic build the input activation store records these ordinary RAM observations in `input_activation_trace`:

1. `phase=0`: activation trace armed; MMIO address and unchanged RFLAGS recorded.
2. `phase=1`: instruction immediately before the byte MMIO store completed.
3. Byte store to this function's `common_cfg->device_status`.
4. `phase=2`: instruction immediately after the MMIO store completed, before any function call, serial output, or readback.

The three critical stores are in one inline assembly block with a compiler memory clobber. The block also records the exact addresses of the MMIO instruction and the following instruction using local assembly labels. It does not change IF. The ordinary checkpoint sequence is retained; use labels rather than assuming stage numbers remain stable between builds.

The emergency path emits bounded `[VI-ACT]` records directly to COM1 and port `0xE9`, with no allocation, filesystem, GUI, timer, scheduler, PCI lock, or UART-ready loop. The existing `SerialLog` also has no lock; logger lock reentry was not found. Raw output may interleave under an interrupt, so RAM observations remain independent of printed lines.

The common IDT C entry records vector, RIP, error code, and CR2 for page/double faults. IRQ entries/exits are capped at sixteen printed records per activation; exceptions are recorded independently. `phase` is preserved on entry and exit. Entry is observed before stack sampling, handler invocation and EOI. Timer scheduling has a separate `scheduler enter` observation; `IRQ exit` is emitted only after the C dispatcher/scheduler call returns. This does not instrument an assembly stub failure before C entry or prove that the eventual IRET completed.

Interpretation:

| New observation | Meaning |
| --- | --- |
| `STORE returned`, phase 2 | The guest passed the MMIO store and immediate following RAM store. |
| Entry phase 1, RIP equals `instruction after MMIO RIP` | The MMIO instruction completed; IRQ/exception entry preceded the following RAM breadcrumb. |
| Exception phase 1, RIP equals `MMIO store RIP` | The store faulted before advancing; inspect vector/error/CR2. |
| IRQ enter without IRQ exit | The C dispatcher did not reach its return observation; check stack sampling, handler, EOI, and scheduler records. |
| Only the pre-store logs, with no entry/return evidence | Still insufficient to identify an emulator stall versus loss of guest diagnostic execution. A guest timeout cannot rescue a synchronous emulator stall. |

Immediately before activation, a nonblocking snapshot logs heap/PMM/PCI/service/config lock words, PCI Command including INTx-disable, MSI-X Message Control including enable/function-mask, entry Vector Control, queue vector, LAPIC IRR bit, MSI-X PBA bit, ISR value, software pending flag, avail/used indices, queue addresses, module ranges and ownership of every queue/event DMA page. PCI snapshot acquisition tries once; it does not spin on the lock being inspected. Diagnostic activation rejects an observed busy lock, unsafe mask, or DMA page outside allocated ordinary RAM.

Reading ISR acknowledges its legacy state; this happens only in the diagnostic snapshot and does not consume used-ring entries. The snapshot runs with normal service still disabled. This instrumentation may affect timing and is not itself a proof of production runtime behavior.

## Activation sequence and lifecycle

The sequence at the start of the latest activation investigation was:

`reset -> ACK -> DRIVER -> VERSION_1 / FEATURES_OK verification -> config/ABS discovery -> create and enable eventq -> allocate and post buffers -> install permanent IRQ context -> program masked MSI-X entry -> assign queue vector/readback -> enable PCI MSI-X -> disable config MSI-X/readback -> INTERRUPTS_READY -> DRIVER_OK/write/readback -> DRIVER_READY -> InputDevice registration -> REGISTERED -> initialized publication -> MSI-X unmask -> initial notify`.

That lifecycle order remains unchanged. This investigation adds an explicit full compiler/CPU publication fence before DRIVER_OK and the diagnostic observations around activation. It does **not** move registry publication before activation or introduce a registered-but-inactive state: no regression demonstrates that such a change would solve the reported freeze.

The existing explicit states are UNINITIALIZED, RESETTING, NEGOTIATING, QUEUE_SETUP, BUFFERS_POSTED, INTERRUPTS_READY, DRIVER_READY, REGISTERED and FAILED. Before activation the driver is INTERRUPTS_READY (5); its software queue and permanent IRQ target are complete. It remains unregistered and normal service remains disabled. The IRQ only increments an atomic counter and records pending state. It does not dereference eventq, dispatch InputDevice events, repost, notify, allocate, take a lock, or wait. Polling refuses normal processing until initialized/registered. Thus synchronous DMA completions can remain in the ring until registry publication.

The previous initialization pass had already moved readiness publication after InputDevice registration. No additional MSI-X ordering change is made here: entry zero remains masked while enabled and across DRIVER_OK; queue zero uses entry zero on its own PCI function, configuration interrupts use NO_VECTOR, and unmasking follows registry/readiness publication. PCI MSI-X enable suppresses INTx. Polling fallback separately suppresses INTx/MSI before activation.

The initial notify is retained. The spec prohibits available-buffer notification before DRIVER_OK. Upstream QEMU's input eventq notify callback is empty, but that implementation detail does not justify removing the portable notification after publishing buffers.

## Queue/DMA and installer audit

For N=64, the descriptor table is `64*16=0x400` bytes. The avail area starts at `+0x400`, contains `4+64*2+2=0x86` bytes, and ends at `+0x486`. Aligning used to 64 bytes puts it at `+0x4C0`; its size is `4+64*8+2=0x206`, so the allocation requires `0x6C6` bytes and fits one 4096-byte page. The reported installer addresses `0x03875000`, `0x03875400`, and `0x038754C0` match this layout. The normal pattern at `0x030AE000` uses identical offsets. Struct-size assertions and host assertions cover the event/descriptor/used-element sizes and exact programmed offsets.

Sixty-four event buffers occupy `0x200` bytes, each eight bytes with WRITE only, no NEXT chain. Tests check every buffer's physical address, length, flag and distinct position; an event-page tail canary and allocator guard detect overruns. The device writes the full ABS_X/ABS_Y/SYN_REPORT into successive buffers and used-ring elements. Only VERSION_1 (`0x100000000`, bit 32) is negotiated: INDIRECT_DESC, EVENT_IDX and RING_RESET are not accepted. Ring flags start at zero; optional event-index semantics are not used.

Production DMA obtains page-aligned contiguous physical frames from PMM, validates RAM/HHDM coverage, zeroes full pages, and supplies guest physical addresses to VirtIO. No heap virtual address is substituted for a DMA address. Queue and event memory remains owned until reset is acknowledged.

Production PMM reserves the entire page-rounded module interval, the kernel, Multiboot metadata, framebuffer and fixed APIC MMIO pages, and excludes firmware-reserved memory from allocation. Tests execute that production tag parser/bitmap/contiguous allocator with extra nonaligned modules and both reported tablet address patterns. Physical module sentinels are connected to the same modeled physical address map as device DMA; allocations are checked for overlap and sentinels remain intact. Tests cover aligned/nonaligned module ends, allocation immediately following a module, event allocation following eventq, framebuffer exclusion, exhaustion, invalid request sizes and lock release. This does not validate the actual runtime memory map or all hardware BAR ranges.

`instalator_boot` parses policy flags and retains installer module pointers before PMM initialization; it does not change IF, PCI status or driver sequencing. The installer GRUB entry adds EFI and kernel modules. PMM therefore sees a different reservation set and may return higher addresses. Input probing occurs after the same RNG/BLK/GPU/NET order in both modes. Splash is disabled by installer policy, graphics selection precedes input, and wallpaper-module release occurs later than input initialization. No earlier installer-specific call to InputDevice initialization was found. The actual PMM cursor, runtime allocation/interrupt timing, graphics state, and Windows host UI behavior have not been measured in the failing run.

## Lock graph and limits

At initialization the call edges are separate, finite transactions:

```text
input init -> DMA allocation -> PMM [local IF off] -> unlock/restore
input init -> queue/software allocation -> heap [local IF off] -> unlock/restore
input init -> MSI-X setup -> PCI CF8/CFC [local IF off] -> unlock/restore
input init -> config query -> per-device try-lock -> release
input init -> IDT registration [local IF off, bounded array work] -> restore
input init -> DRIVER_OK MMIO [none of these locks held]
input IRQ -> IDT dispatch -> atomic counter/pending stores -> APIC EOI
normal poll -> service try-lock -> queue completion -> InputDevice ingestion -> scheduler wake counters
diagnostics -> direct serial writes; PCI snapshot uses one try-acquisition
```

No heap/PMM/PCI lock is nested across DRIVER_OK, and the input IRQ does not acquire any of them. InputDevice registration and event ingestion do not use a blocking registry lock; queue operations have no separate blocking queue lock. The scheduler mouse wake path uses atomic counters without taking an allocator or PCI lock. Existing timer/other-device paths remain outside the input IRQ model and are observable through emergency entry records.

`test-input-init-locks` executes the production heap, PMM and PCI lock implementations, replacing only privileged IRQ operations and port I/O. It checks IF preservation for enabled and disabled callers, release before IRQ restoration on success/failure, PCI nonblocking snapshot rejection while the real lock is owned, and four-thread contention/unique PMM frames/heap sentinels/CF8-CFC transaction isolation. It is not an IDT or hardware interrupt emulator. The general spinlocks still have no global liveness bound if an owner is permanently stuck; no blanket allocator timeout or arbitrary sleep was added.

## Verification and artifacts

Final verification completed successfully:

| Check | Result |
| --- | --- |
| `make test-virtio-pci` | 161 PASS, 0 FAIL, diagnostics disabled. |
| `make test-virtio-pci BURSZTYNDIAGVIRTIOINPUTINIT=1` | 162 PASS, 0 FAIL, including emergency exception observations. |
| `make test-input-init-locks` | 4 PASS, 0 FAIL. |
| `make installer-unit-test` | Storage/BSP2: 10,493 assertions PASS, 0 FAIL; also reports 138 native move assertions PASS. |
| ASan / UBSan | No findings in the completed runs. VirtIO/lock suites use leak detection and UBSan halt-on-error; existing Storage suite disables leak detection. |
| Final `make -B -j2 all iso` | Exit 0, diagnostics disabled. |
| Diagnostic `make -j2 all iso BURSZTYNDIAGVIRTIOINPUTINIT=1 ISO=BursztynOS-input-init-diag.iso` | Exit 0 after correcting the APIC diagnostic helper's C/C++ linkage. |
| Extracted ISO kernels | Trace prefixes absent in the normal image and present in the diagnostic image. Final local kernel matches the normal ISO kernel. |
| Diagnostic object disassembly | Confirms phase=1 store, byte MMIO store, phase=2 store are consecutive instructions; stored instruction addresses match the labels. |
| `git diff --check` and new-file whitespace checks | PASS. |

The build retains the existing wallpaper warning about the skipped unsafe filename `Zamek w Pszczynie 2026r..jpg`. No kernel compiler warning was emitted by the final normal or diagnostic build.

Artifacts (SHA-256):

| Filename | Bytes | SHA-256 |
| --- | ---: | --- |
| `BursztynOS.iso` | 35,272,704 | `11f3ed089fe0ff4adbaafd7f88a8b4a0a119e29a0c7887750448b716afd4feb4` |
| `BursztynOS-input-init-diag.iso` | 35,282,944 | `ce7b8cf4394ec7d8f2b50f803b988a2e8a793533240fdc2cf28900c898fed849` |

`runwindows.ps1` remains SHA-256 `b98a5d033842b53affab8707d80596a947672b1bd487a0a70672c9eb75103e81`. The existing `wirtualny_dysk.img` retains its 4,294,967,296-byte size and original modification timestamp `1791478640`.

Host coverage includes synchronous DRIVER_OK activity with three delivery modes (forced early IRQ, masked pending IRQ delivered on unmask, and immediate service after readiness), prior early IRQ/state tests, MSI-X masking/NO_VECTOR and INTx suppression, descriptor ownership/layout/fault tests, PMM module/DMA tests, emergency exception-record tests, and the shared BLK/NET/RNG/GPU tests. The synchronous model is deliberately stronger than the upstream activation callback and is not a reproduction of QEMU's Windows UI implementation.

No change was made to the installer write-confirmation gate, block driver protocol, automatic Installer launch, splash policy, GPU publication/500 ms timeout, PSF, Unicode, folder deletion, network policy or restart behavior. Their runtime acceptance is still pending. `runwindows.ps1` and `wirtualny_dysk.img` were preserved; no clean/rm target or QEMU test was run.

Production/build files modified in the current worktree, including inherited changes: `Makefile`, `apic.cpp`, `heap.cpp`, `idt.cpp`, `input_device.cpp`, `input_device.h`, `kernel.cpp`, `pci.cpp`, `pmm.cpp`, `syscalls.cpp`, `virtio_input.cpp`, `virtio_input.h`, `virtio_pci.cpp`, `virtio_pci.h`, `virtqueue.cpp`, `virtqueue.h`; new diagnostic headers are `virtio_input_activation_diag.h` and `pmm_diagnostics.h`. Shared PCI transport and virtqueue changes remain present. No common runtime freeze affecting BLK/NET/RNG/GPU was demonstrated; the new instruction-level activation trace and activation fence are input-specific.

Test files modified/added across this worktree: `narzedzia/test_virtio_pci.cpp`, `narzedzia/mock_virtio_input_init.inc`, `narzedzia/test_virtio_input_init.inc`, `narzedzia/test_virtio_input_activation.inc`, `narzedzia/test_virtio_input_faults.inc`, and `narzedzia/test_input_init_locks.cpp`. Tracked generated dependency files (`apic.d`, `heap.d`, `idt.d`, `kernel.d`, `pci.d`, `pmm.d`, `syscalls.d`, `virtio_input.d`, `virtio_pci.d`) and `.virtiotest/test-virtio-pci` are refreshed by builds/tests.

For manual acceptance, boot the same diagnostic ISO twice with the unchanged modern-only keyboard/tablet hardware configuration: once with the normal entry and once with Installer. Preserve the `[VIRTIO-INPUT-INIT]` and `[VI-ACT]` records for both functions. Installer must pass DRIVER_OK, register QEMU Virtio Tablet, complete the sanity check, reach its GUI with keyboard and absolute pointer working, preserve zero disk-sector writes before confirmation, finish installation, and restart without a QEMU freeze. Normal boot must reach its working desktop with both input functions. Neither run is claimed PASS here.

## Primary source cross-check

The checked upstream QEMU source is a current source snapshot, not an identification of the user's installed Windows QEMU version. The PCI common-status write calls `virtio_set_status` synchronously; input sets active and calls its HID activation callback, which activates the host input handler and notifies host input-change observers. Event delivery copies a report into guest writable descriptors and notifies only when `virtio_input_send` receives a complete report. Therefore synchronous activation side effects are real, while automatic ABS DMA during the status write is unproven.

- [VirtIO 1.2, sections 2.1, 3.1, split rings and input](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)
- [QEMU PCI common-status write](https://github.com/qemu/qemu/blob/master/hw/virtio/virtio-pci.c)
- [QEMU input activation, event delivery and empty eventq notify callback](https://github.com/qemu/qemu/blob/master/hw/input/virtio-input.c)
- [QEMU HID activation](https://github.com/qemu/qemu/blob/master/hw/input/virtio-input-hid.c)
- [QEMU host input-handler activation and observers](https://github.com/qemu/qemu/blob/master/ui/input.c)
