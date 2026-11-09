# Acer Nitro AN515-54 input coexistence and pre-splash regression — 2026-10-11

The current worktree contains a candidate repair, a production ISO, and a separately built bounded diagnostic ISO. No physical Acer runtime PASS is claimed. No QEMU or VirtualBox was launched.

Two source defects are repaired: exclusive pointer admission dropped valid reports from other devices, and synchronous xHCI enumeration ran before the first graphical publication. **The exact cause of the reported old production/diagnostic difference, the Acer-specific blocking hardware operation, and the origin of the visible dash remain unconfirmed without physical timing/source records.** The deterministic measurements below establish the software dependency and its removal; they are not Acer measurements. Those outstanding findings must not be inferred from a successful build or host test.

## Evidence and compilation audit

The user's physical observations are the runtime baseline: the old diagnostic ISO permits touchpad movement without an external mouse, USB movement suppresses touchpad movement, production suppresses touchpad movement even without an external mouse, and both ISOs pause before the welcome splash. Successful physical boot and both working device decoders are accepted as evidence. The touchpad driver was not rewritten.

The first work was to preserve the starting sources and preprocess the actual production compiler commands with each diagnostic flag disabled/enabled. Complete command diffs and complete preprocessed differences are in [the audit directory](input-coexistence-diagnostic-audit-2026-10-11/). Each `*-commands.diff` includes every changed compile command; each `*-preprocessed.diff` includes every changed translation unit. Unchanged translation units produce no diff. See [before summary](input-coexistence-diagnostic-audit-2026-10-11/before-compilation/summary.json) and [after summary](input-coexistence-diagnostic-audit-2026-10-11/after-compilation/summary.json).

`narzedzia/audit_input_diagnostic_compilation.py` reproduces the audit without building or launching a VM. The two older direct `BURSZTYN_DEBUG_*` defines are audited in the xHCI translation unit; their other, unrelated uses are outside this task. The starting source overlay used for their additional audit is `/tmp/bursztyn-coexistence-before-20261011`. The original four flag diffs were captured before edits.

| Flag | Starting changed translation units | Starting preprocessed diff lines |
|---|---|---:|
| `BURSZTYNDIAGXHCIPHYSICALMOUSE` | xhci, hid_pointer, input_device | 232 |
| `BURSZTYNDIAGXHCIPOINTING` | xhci, hid_pointer, ohci, idt, mysz, grafika, input_device | 268 |
| `BURSZTYNDIAGPS2` | mysz | 27 |
| `BURSZTYNDIAGUSBHID` | hid_pointer, ohci | 42 |
| `BURSZTYN_DEBUG_XHCI` | xhci | 11 |
| `BURSZTYN_DEBUG_GUI_PERF`, xHCI uses | xhci | 10 |

Every physical-mouse flag conditional was audited:

- `xhci_physical_diag.h`: the bounded emission budget and log helper.
- `xhci.cpp`, `physical_completion`: completion text only.
- `ext_capabilities`: final USBLEGSUP/USBLEGCTLSTS reads and a snapshot after the same unconditional handoff/SMI handling.
- `xhci_obsluz_hid`: at most 64 raw pointer bytes for each of the first 16 reports; no keyboard bytes. Decoding, admission, requeueing and barriers are outside this conditional.
- `enumerate_port`: a scoped final port/slot/state snapshot and its diagnostic slot assignment. It does not allocate, reset, enable or disable anything.
- `InitGuard`: IF/controller/IRQ snapshots on return.
- `xhci_inicjalizuj_pierwszy`: PCI command before/after reads, capability/revision/HCCPARAMS2 snapshots, final Bus Master command read, and post-run USBCMD/USBSTS reads.
- `usb_obsluz`: the first 16 deferred IRQ snapshots; IRQ draining and interrupt-mask writes are unconditional.
- `hid_pointer_publish`: bounded decoded pointer traces, saved pre-activation state/preferred-source values, and a post-activation snapshot. Registration, arming, proving, report decoding and dispatch were already unconditional.
- `input_device.cpp`: trace storage/emission and the boot device/geometry snapshot. No hardware fallback was hidden in these blocks.

The physical flag therefore had **no explicit functional fallback to move into production**. Its added PCI/MMIO reads are snapshots of registers without read-to-clear semantics. The shared `input_get_pointer_owner()` getter did have a lazy recalculation side effect when its cached value was null; diagnostic calls could invoke it. It is now strictly read-only, and preferred-source changes cannot clear reports or control admission. The audit and deterministic replay did not establish a reachable instance of that side effect explaining the Acer difference. Additional serial output also changes elapsed execution time, but a timing-race explanation remains a hypothesis, not a finding.

Related flag functional differences and corrections:

1. `idt_alokuj_wektor_irq()` had a `BURSZTYNDIAGXHCIPOINTING`-only trap for an occupied dynamic entry or vector 44. Allocation already ranges from vector 48. Occupied entries are now skipped unconditionally, with the same IF restoration in both builds.
2. The xHCI initialization guard had a pointing-flag-only trap when IF differed on return. It now records the discrepancy. Actual interrupt save/restore remains unconditional and is tested with both incoming IF states.
3. Diagnostic preferred-source reads no longer call a mutating getter. This applies to physical-mouse traces, pointing snapshots, HID traces and boot snapshots.

The remaining older pointing blocks emit completion, pointer and counter snapshots. PS/2 blocks emit command/response/state diagnostics. USB HID blocks emit descriptor/rejection/decoded-report or OHCI identity diagnostics. Their command submission, parsing, registration and service paths are unconditional. Pointing snapshots are capped at 32, xHCI configuration debug output at 64 lines, and xHCI periodic performance output at 16 batches.

No flag controls initialization order, polling, USB processing, deferred service, IRQ masks, ownership/admission, reset timing, report handling, queue sizes, barriers, DMA or registration. Compile-time differences remaining in those paths are diagnostics. The new `BURSZTYNDIAGINPUTCOEXISTENCE` defaults to zero and controls bounded emission only. The timing recorder and clock reads are identical in production and diagnostic builds; recorded values do not drive hardware decisions.

## Pointer coexistence repair

The old common dispatcher compared a reporting device with one preferred pointer owner. USB priority 20 displaced a PS/2 pointer at priority 10; VirtIO used priority 30. Nonowner SYN reports were cleared, and changing owners cleared the previous owner's pending movement. This precisely explains the software mechanism by which a proven USB mouse stopped another supported pointer contributing. It does not by itself identify the source that displaced the touchpad during the old production boot with no external mouse.

Admission now checks the reporting device's own registration and `PROVEN_ACTIVE` state. USB proves liveness only after successful descriptor-aware decoding of a structurally valid live report. PS/2 and VirtIO prove liveness on validated transport events committed by SYN. Registration and arming do not prove a device or publish movement. A second device becoming active does not change the first device's state.

`input_get_pointer_owner()` remains for compatibility as preferred-source metadata. Its priority calculation has no event acceptance role. `input_get_last_pointer_source()` tracks actual last publication separately. There is no owner switching on every event, device priority adjustment, Acer-specific policy, hysteresis, source delay or timestamp suppression.

Each `InputDevice` retains independent identity, bus, coordinate mode, registration, readiness/proven state, contact, accumulated report, last absolute coordinates, framebuffer geometry and published buttons. Disconnect removes only that source's queued reports and button contribution. Existing transport failure paths withdraw the failed device; another transport's active device remains usable. Button contributions are ORed so one source cannot release a button held by another.

Relative reports preserve signed deltas, saturate accumulation through signed 64-bit arithmetic, and use the existing graphics cursor adapter. That adapter adds to the current cursor with signed checked arithmetic and clamps to framebuffer bounds. The adapter cancels the legacy GUI Y inversion exactly once. Negative values are never accumulated as unsigned coordinates.

Absolute reports retain the existing independent X/Y range mapping. Scale and framebuffer-edge mapping were preserved. `BTN_TOUCH` is a separate contact capability/state, including VirtIO capability discovery. A focused HID audit found that descriptor-defined Digitizer Tip Switch fields inside supported pointer collections were discarded; the parser/decoder now retain that contact field. The working Boot Mouse and axis decoding paths were not rewritten.

A contact-dependent absolute source publishes coordinates only with active contact and changed coordinates, a new contact, or relevant geometry becoming different. Unchanged coordinates from a resting finger do not override USB movement. Inactive reports do not publish coordinates even if their stored coordinates change. Contactless absolute tablets publish changed coordinates; repeated stationary coordinates do not override a different source. A subsequent valid active movement works immediately after USB activity.

Reports received before geometry/readiness are stored in one bounded 128-report queue preserving arrival order across sources. Disconnect cancels only that device's entries, including before an address is reused. Overflow discards the oldest report; there is no allocation or unbounded queue. Once geometry becomes ready, the same cursor pipeline drains the reports in order. No registration, activation, preferred-source change, disconnect or reconnect assigns cursor coordinates.

## Boot dependency, timing and presentation

In the saved starting code, `xhci_inicjalizuj_pierwszy()` called `ports(xhc); enumerate_connected_ports()` synchronously after controller Run. The call chain was `kernel_main -> usb_inicjalizuj -> xHCI initialization -> full device enumeration`, before PS/2 negotiation and before `grafika_zakoncz_wybor_backendu()`. Enable Slot, Address Device, EP0 reads, configuration/HID setup and optional report descriptor requests could therefore block the first display publication. Those operations retain their existing one-second deadlines; several operations or devices can accumulate seconds. This dependency also existed in HEAD; the available worktree alone cannot prove which newly reachable hardware operation first produced the reported physical regression.

Full root-port discovery and device enumeration now execute through the normal existing `usb_obsluz()` deferred service, after framebuffer/backend publication and the existing final STI/scheduler barrier. Early setup retains BIOS ownership/legacy SMI handling, safe controller halt/reset/CNR waits, DMA/rings, validated MSI/MSI-X setup, Run and port power. This preserves the firmware-to-PS/2 ordering while allowing PS/2 negotiation earlier than the old full-enumeration path. No new thread, polling watchdog, sleep or timeout change was added.

The IRQ only acknowledges/masks its own interrupter and marks pending work; it never enumerates or consumes rings. Deferred service owns controller/ring state, with a reentry guard. A connected port is attempted once per connection, preventing its own reset change events from restarting a failed enumeration. A coalesced unplug/replug CSC retires the previous connection and re-enumerates its replacement without moving the cursor. A disconnected port is rejected from CCS immediately, without a reset or slot/control command. Initial discovery is bounded to one port per deferred call; genuine change events inspect ports through the same path.

The deferred enumeration operation still uses bounded synchronous command/control waits within that service. This change removes their dependence on the first splash frame; it is not a conversion of all USB transactions into a fully asynchronous runtime state machine.

Instrumentation uses the existing monotonic HPET facility for individual phases and TSC stamps for early C++ kernel entry, framebuffer mapping readiness, first successful framebuffer publication and first visible splash. The diagnostic summary calibrates the early TSC intervals against HPET and labels them estimates. The bounded recorder stores at most 768 detailed phase records. Detailed timing is emitted only with the coexistence flag, after publication or deferred service, never as a continuous production stream.

Measured phases include PCI scan, USB time before splash, xHCI discovery, BIOS ownership, controller halt/reset, pre/post reset CNR, scratchpads, DCBAA, command/event rings, IRQ setup, every inspected/reset root port, Enable Slot, Address Device, each control request, configuration parsing, HID endpoint setup and first interrupt transfer submission. Port detail identifies the port; control detail identifies request/value. Summary intervals distinguish framebuffer mapping readiness from actual publication, and publication from splash start.

The deterministic benchmark executes the [saved starting xHCI source](input-coexistence-diagnostic-audit-2026-10-11/baseline-xhci.cpp.txt) and repaired source against the same register model; reproduction commands are in the audit README. Each of seven successful control requests completes after **400 ms of simulated monotonic time**; no host sleeps are used. These are injected device latencies, not estimates of Acer latency. Clock reads advance a deterministic amount, so small instrumentation costs in the table are model costs, not CPU benchmarks.

| Deterministic measurement | Saved starting path | Repaired path |
|---|---:|---:|
| USB/controller call before display can publish | 2,801,500,000 ns | 2,800,000 ns |
| Control requests before publication | 7 | 0 |
| Root-port resets before publication | 1 | 0 |
| Deferred work after publication | not deferred | 2,808,100,000 ns |
| Successful control requests in the complete session | 7 | 7 |

Repaired detailed measurements for this scenario:

| Phase | Calls | Simulated total ns |
|---|---:|---:|
| Discovery | 1 | 100,000 |
| BIOS handoff | 1 | 200,000 |
| Halt / reset | 1 each | 200,000 each |
| CNR | 2 | 400,000 |
| Scratchpads / DCBAA / command ring / event ring / IRQ setup | 1 each | 100,000 each |
| Root-port inspection, inclusive of enumeration | 20 | 2,806,100,000 |
| Root-port reset | 1 | 200,000 |
| Enable Slot / Address Device | 1 each | 300,000 each |
| Control transfers | 7 | 2,800,700,000 |
| Configuration parsing | 1 | 100,000 |
| HID endpoint setup | 1 | 500,000 |
| First interrupt receive submission | 1 | 100,000 |
| Port enumeration, inclusive | 1 | 2,804,000,000 |

Nested phase totals must not be added together. In this scenario the delay is descriptor/class control enumeration, not BIOS ownership, reset, CNR, IRQ setup or disconnected ports. **Which of those stages accounted for the actual Acer pause is not measured yet.** Likewise, no physical before/after kernel-entry-to-publication totals are available; the new diagnostic ISO supplies their instrumentation for manual validation.

Disconnected-port tests measured 4,000 ns for one port, 80,000 ns for 20, and 1,020,000 ns for 255, including direct disconnected-reset rejection checks. All had zero enumeration controls and zero reset writes. BIOS ownership tests cover already completed ownership, immediate handoff, a simulated 100-ms handoff and timeout. Independent BIOS/halt/reset/CNR failures take one bounded controller wait, approximately 1.005–1.017 simulated seconds, with both incoming IF states preserved; a failed connected port is not retried on its own reset events.

PCI configuration access restores IF per short access; xHCI waits do not hold a PCI, global input or graphics lock. The existing kernel initialization policy keeps IF clear until the scheduler barrier. Slow device enumeration now occurs after that barrier with interrupts available. HPET's short counter critical sections retain their existing IF restoration. No new global interrupt-disabled wait was introduced.

Framebuffer publication still obeys the existing ready-frame/backend barrier. The welcome renderer starts its timing at its first successful visible publication, and its accepted durations/crossfades are unchanged. The old pre-publication path retains firmware contents until that point. `boot.S` contains no VGA/dash drawing; no changed USB/input path draws a dash. Firmware/GRUB remnants are a plausible explanation for the visible mark, but **the subsystem that actually left that physical dash is unconfirmed**. No extra black-frame clear or dash cover was added.

## Validation

Final host results are recorded in the [validation logs](input-coexistence-diagnostic-audit-2026-10-11/). The full transport/graphics fixtures simulate hardware in host processes; names mentioning VirtualBox describe descriptor streams, not a launched VM.

| Suite | Exact reported result |
|---|---:|
| PS/2 parser | 330,443 checks |
| HID descriptor/report | 6,051 checks |
| USB configuration | 48 checks |
| Common InputDevice mapping/admission | 146 checks |
| i8042 / IRQ1 / IRQ12 | 4,140 checks |
| OHCI stream + enumeration/transfers/failures | 5,400 + 5,855 checks |
| Existing xHCI IRQ / rings / protocol / first failure / stream / completions | 133 + 1,030 + 15 + 24 + 4,800 + 9,484 checks |
| Signed relative regression + API/readiness | 396,600 + 56 checks |
| Pointer boot-order barrier | 6 checks |
| Physical xHCI fixture, physical flag off/on | 20,322 checks each; 248 allocations/releases each |
| New input coexistence, four compilation modes | 132 checks each |
| New boot/coexistence fixture, four compilation modes | 23,622 checks each, including the physical fixture |
| Saved-source timing benchmark | 267 checks |
| Ordered functional parity | 6 comparisons; 51 input records and 18,432 controller records per comparison |
| VirtIO/PCI/input/keyboard lifecycle and stress | 166 tests, zero failures |
| Initialization locks / IF / contention / errors | 4 checks, zero failures |
| VirtIO production boot-order source audit | 1 check |
| IRQ allocation / exhaustion / IF | 3,740 checks |
| Real graphics / InputDevice / HID integration | 40 checks |
| Compositor lifecycle/scene | 74,293 checks; popup subset 73,816 |
| Splash timing / first frame / snapshots / production path | 2,185 + 101 + 342 + 722 checks |
| Production keyboard / text input / dialogs | 7,291 checks |
| Concurrency / IRQ / scheduler lock audit and host run | 10 checks |
| Cursor host clipping/coalescing/background/interleaved-producer scenarios | PASS; fixture has no numeric counter |

The counted fixtures total **988,872 reported check executions**, including repeated compilation modes and the saved-source benchmark. The popup subtotal is included in the compositor total. The six parity comparisons, 166 named VirtIO tests, four lock checks, one VirtIO source audit, ten concurrency checks and uncounted cursor suite are reported separately rather than represented as fabricated assertion counts.

The new tests cover registration/activation without movement, both event orders, alternating events, independent activity, inactive and stationary active contact, contactless stationary tablets, disconnect/reconnect/address reuse, negative/extreme deltas, clamping, zero/incomplete geometry and geometry becoming ready. The controller fixture also covers coalesced reconnect and delayed BIOS handoff. The parity runner compares GUI publications, explicit device states, ordered PCI/MMIO writes, DMA allocations/releases, TRB publication and report/requeue state. It compiles production, physical diagnostics, coexistence diagnostics, and all related input/xHCI diagnostics together. Diagnostic output/counters/timing records are excluded from functional comparison.

ASan: final relevant host executions PASS, no reported memory error; leak checks PASS where enabled by the targets. UBSan: final relevant host executions PASS, no reported undefined behavior. New parity tests use both sanitizers with halt-on-error and leak detection enabled.

During iteration, active-contact fixtures that advertised BTN_TOUCH but never sent it failed after contact gating was corrected. They now explicitly send valid contact through the real VirtIO queue, preserving their coordinate/count assertions. An obsolete cumulative-delta assertion was corrected from USB/VirtIO-only movement to the sum of both delivered sources. Early assertion returns had left fixture registrations/DMA alive, producing sanitizer reports; the final complete runs pass without disabling sanitizer checks. The splash production host target also needed its existing PMM host declarations and actual PMM object linked after its reused transport fixture changed. Production PMM/filesystem behavior was not changed. The first graphics iteration log retains that resolved harness compilation failure for transparency; the final production-path log passes.

Existing functional assertions were preserved or replaced with explicit coexistence requirements. No VM test, installer behavior test, networking/audio optimization, filesystem change, desktop-layout change or touchpad-driver rewrite was performed.

## Files and preserved physical fixes

Production/build files changed for this repair:

- `Makefile`
- `input_device.cpp`, `input_device.h`
- `virtio_input.cpp`
- `sterowniki/usb/hid_pointer.cpp`, `sterowniki/usb/hid_pointer.h`
- `sterowniki/usb/xhci.cpp`
- `idt.cpp`, `xhci_pointing_diag.h`
- `boot_phase_timing.h`, `input_coexistence_diag.h` (new)
- `kernel.cpp`, `grafika.cpp`, `ekran_powitalny.cpp` (timing stamps/scopes only)

Additional production files already changed/new in the inherited physical-mouse worktree and retained: `sterowniki/usb/xhci_ring.cpp`, `sterowniki/usb/xhci_ring.h`, `sterowniki/usb/xhci_hw.h`, `sterowniki/usb/xhci_physical_diag.h`. Together with the files above, this lists every changed/new production or build source relative to HEAD.

The inherited control-stage TRB fixes, legacy SMI mask, genuine USB2/USB3 reset handling, DMA address-width checks, MSI/MSI-X validation, Boot Mouse protocol support, Max ESIT Payload/context fields, Supported Protocol parsing, intervals, Link TRB publication ordering, unaligned event rejection and PCI RW1C protection are preserved. The inherited physical fixture continues to exercise them in every parity mode.

New test files for this repair: `narzedzia/test_input_coexistence.cpp`, `narzedzia/test_xhci_boot_coexistence.cpp`, `narzedzia/test_input_diagnostic_parity.py`. New audit tool: `narzedzia/audit_input_diagnostic_compilation.py`.

Existing tests updated: `narzedzia/test_pointing.cpp`, `narzedzia/test_pointer_regression.cpp`, `narzedzia/test_virtio_pci.cpp`, `narzedzia/test_virtio_input_faults.inc`. The inherited new `narzedzia/test_xhci_physical_mouse.cpp` was extended for deferred coldplug, deterministic register timing and ordered functional effects. New documentation includes this report and its complete audit/log directory.

## Builds, artifacts and manual acceptance

Diagnostic build: PASS, exit 0:

```sh
make -B -j2 all iso BURSZTYNDIAGINPUTCOEXISTENCE=1 BURSZTYNDIAGXHCIPHYSICALMOUSE=0 BURSZTYNDIAGXHCIPOINTING=0 BURSZTYNDIAGPS2=0 BURSZTYNDIAGUSBHID=0
cp BursztynOS.iso BursztynOS-input-coexistence-diag.iso
```

Final production build: PASS, exit 0, using the requested command:

```sh
make -B -j2 all iso
```

No `make clean` or `make rm` was used. The production build contains no enabled input diagnostic define; the separate diagnostic build enables only `BURSZTYNDIAGINPUTCOEXISTENCE`. Pointer traces are limited to 32 events per registry source, lifecycle diagnostics to 256 lines, and timing storage to 768 records. Production emits none of these new traces. Both use the same controller and input implementation.

Both ISO kernels were extracted into a fresh temporary directory and compared byte-for-byte with their separately built kernel artifacts. Production `.rodata` lacks the new coexistence/boot timing emission markers; diagnostic `.rodata` contains them. See [ISO verification](input-coexistence-diagnostic-audit-2026-10-11/iso-verification.json). Full build logs are `.pointingtest/coexistence-validation/production-build.txt` and `diagnostic-build.txt`.

| Artifact | SHA-256 |
|---|---|
| `BursztynOS.iso` | `8ed796871adf8e9f7bd423ac948bf8801dc8c7476c26238b5681617f67815155` |
| `BursztynOS-input-coexistence-diag.iso` | `121f9191ccdb8a60e24e8144947e3a98820322b1ded165fc4e65071f80206e33` |

Physical Acer acceptance remains manual, in this order:

1. Normal production ISO, no external USB mouse: built-in touchpad moves the cursor.
2. Normal production ISO, wired USB mouse connected: both sources move the same cursor; alternating sources does not teleport it. Inactive/resting touchpad reports do not fight USB movement. Unplug leaves touchpad usable; reconnect does not reset position.
3. Normal production startup: compare the period before the welcome splash with the prior baseline; preserve the splash sequence after its first frame. If any delay remains, the diagnostic ISO's per-phase records must identify the actual physical operation before further changes.

Diagnostic and production behavior should also be compared on the same device/port arrangement. A remaining discrepancy requires the captured registration/activation/IRQ/contact/timing records to distinguish a race or hardware state difference from a stale artifact. This report does not claim that the two unresolved physical causes were established by host simulation. Networking and audio work remains out of scope.
