# xHCI pointing investigation and repair — 2026-10-10

Status: source audit and host validation; **VirtualBox runtime acceptance pending**. No VirtualBox or QEMU was launched. These results do not establish a runtime PASS or prove which defect caused the manually observed PS/2 failure.

## Findings and limits of the evidence

The starting worktree already deferred `hid_pointer_publish()` registration until HID decoding succeeded, for both OHCI and xHCI. Therefore enumeration, descriptor parsing and Configure Endpoint did **not** steal ownership in the starting code. The shared registry nevertheless treated every registered pointer as eligible. A compiled descriptor with separate button-only and axis report IDs could let a button-only first report claim an absolute pointer without proving its axes. The new shared eligibility state and first-report axis requirement close that gap. Reports without axes remain usable after readiness has been proved, preserving split-report buttons.

Concrete xHCI defects found and repaired:

- Endpoint Context DWORD 4 contained only `wMaxPacketSize` in its low 16 bits (Average TRB Length). Max ESIT Payload, in bits 31:16, remained zero. Both fields are now populated and validation checks the actual Max ESIT field.
- Supported Protocol PSI count was read from DWORD 3 instead of DWORD 2 bits 31:28. Bounds checking now also limits the PSI array to the BAR.
- High/Super-speed `bInterval=1` maps to the valid context Interval value zero, which initialization incorrectly rejected. Validation now checks descriptor input separately from its encoded result.
- There was **no xHCI interrupt route**: no vector allocation, MSI/MSI-X configuration, handler or interrupter IE; runtime depended on unconditional event-ring polling. Runtime event consumption now follows a real IRQ notification. Existing bounded command/control waits during initialization remain; no interrupt-replacement polling loop was added.
- Producer rings published the last data TRB before updating the Link TRB cycle. Hardware could observe the preceding TRB and follow an old-cycle Link. The Link is now published first.
- Event TRB addresses were rounded down before validation. Unaligned addresses are now rejected, rather than being converted into a valid outstanding address. Duplicate command completions cannot retire another command.
- PCI command writes echoed RW1C status bits. Writes now put zero in the status half. MSI capability lengths and complete capability-chain validation prevent overlapping headers, loops and config-space overrun.

No PS/2-vector collision existed in the audited original xHCI code: it allocated no vector at all. No xHCI IOAPIC route write, IRQ12 mask change, LAPIC reconfiguration, handler replacement, CLI/STI sequence or leaked global interrupt lock was found. Shared PCI transactions already save IF and restore it conditionally; xHCI takes no lock across a synchronous command/control wait. **Whether VirtualBox xHCI activation actually suppresses IRQ12 remains unproved until the manual diagnostic run.** Host tests cannot establish physical interrupt delivery. An IF=0 boot snapshot with zero IRQ counts is not proof of broken IRQ12 delivery.

## Shared lifecycle and arbitration audit

All pointer ownership changes still go through `input_recalc_pointer_owner()`; priorities remain VirtIO 30, USB 20, PS/2 10. Ties are deterministic by registry slot order. Unregistration and registry reset remove eligibility and recalculate/reset ownership. Input events from an inactive USB pointer cannot bypass arbitration even when there is no owner.

| Backend | Registration / eligibility | Failure, disconnect, destruction, quarantine |
| --- | --- | --- |
| PS/2 | Existing initialization registers the pointer; its existing immediate eligibility is preserved. Initialization now precedes USB, establishing the fallback first. Parser unchanged. IRQ12 queues reports; deferred InputDevice service publishes them. | Existing initialized/streaming checks and registry reset remain. No new parser, timer or health heuristic. |
| VirtIO | Existing prepare phase remains unregistered. Existing activation registers after transport readiness; immediate eligibility is preserved. | Existing initialization failure, reset, teardown and DMA quarantine unregister through the same registry. VirtIO production code unchanged. |
| OHCI HID | Enumerate/compile registers `REGISTERED_INACTIVE`; queueing the real interrupt transfer arms it. Successful retired TD and safe report decoding prove readiness. | Existing TD/controller failure and disconnect call `hid_pointer_remove()`; controller-lifetime DMA retention preserved. |
| xHCI HID | Descriptor compilation registers `REGISTERED_INACTIVE`; successful interrupt IN submission changes it to `ARMED`. Configuration and submission alone never affect owner. | Invalid/error completion, requeue failure, disconnect and controller fatal/halted status remove eligibility. Slot/controller DMA stays allocated until quiescence is confirmed; failed Disable Slot cannot leave a registered owner. `free_device()` refuses to free an enabled slot. |

The absolute pointer becomes `PROVEN_ACTIVE` in `input_prove_pointer()`, called from `hid_pointer_publish()` **after** the expected live slot/endpoint/outstanding TRB has completed successfully, residual length is bounded, the report ID and precompiled report layout decode successfully, and both required absolute axes are present in the first report. A stationary report qualifies. That state transition calls common arbitration; only an eligible pointer that wins unchanged priority/tie rules may replace PS/2. Repeated valid reports neither re-register nor reassign ownership.

A first transfer that never completes leaves USB armed and PS/2 eligible indefinitely. Enumeration/configuration/submission errors, short/malformed first reports, unknown report IDs and button-only first reports cannot promote USB. Explicit failure after ownership unregisters USB and restores the best remaining eligible owner. Later malformed reports are rejected and the healthy endpoint is requeued; they do not invent a timer-based liveness policy.

## Interrupt, transfer and DMA behavior

xHCI allocates one common IDT vector from 48..254; fixed PS/2 vector 44 cannot enter that pool. The existing allocator rejects handler replacement. Diagnostic builds also assert that a free slot has no handler and excludes IRQ12. Repeated successful initialization returns without allocating again; repeated failed initialization preserves quarantined state instead of zeroing live DMA. Failed setup releases its vector and restores the controller's prior PCI command bits.

Interrupt mode is selected from this controller's validated capabilities: MSI-X when its table and PBA are in the mapped BAR0 and both ranges fit, otherwise MSI. MSI-X enable/readback failure attempts MSI with the allocated vector. No usable message route fails cleanly; no legacy shared INTx or PS/2 IOAPIC route is installed. The actual VirtualBox mode/vector is printed by the diagnostic image, not assumed here. MSI-X tables outside BAR0 are not newly mapped by this change.

All xHCI PCI operations use its own bus/device/function. Capability walks are bounded to conventional 256-byte config space, reject loops/overlapping payloads, and validate aligned access extent. MMIO bounds cover the complete port array, doorbell array and first interrupter. DMA allocations remain common page-backed, zeroed, contiguous kernel allocations; rings/contexts are not stack DMA. All physical pointer high DWORDs are preserved; host producer/context tests exercise addresses above 4 GiB. x86 coherent DMA assumptions are unchanged.

The IRQ handler increments a cheap counter, acknowledges USBSTS.EINT and IMAN.IP, masks only its interrupter IE and sets a deferred notification. It takes no global, InputDevice or PCI lock, consumes no ring and sends no independent EOI. The existing common IDT dispatcher provides EOI. Deferred service drains at most 256 events; a full budget retains a notification with IE masked, otherwise IE is restored without clearing a new IP. No steady-state ring polling substitutes for a missing IRQ. Controller fatal/halted status removes owners and quarantines DMA.

Interrupt IN uses endpoint type 7 and descriptor MaxPacketSize. Low/full-speed context interval is `3 + floor(log2(bInterval))`; high/Super-speed is `bInterval - 1`, with descriptor range 1..16. For example full-speed 10 ms becomes 6; high-speed 1 becomes 0. Slot speed continues to use the port's raw speed ID.

Exactly one HID buffer/Normal TRB is outstanding per device. IOC and ISP request completion/short-packet events. Slot ID, endpoint ID, event-data flag, full aligned TRB address, outstanding-transfer identity, completion code and residual length are validated. Successful/short completion retires one TRB, safely decodes the actual bytes and submits exactly one replacement while alive. Duplicate/stale events do not retire or requeue a buffer. A halted/error endpoint is removed and not requeued; other controller work remains available. Failed resubmission removes the owner.

Event consumption uses an acquire cycle read, toggles consumer cycle on wrap, and advances ERDP to the next entry with EHB write-one-clear. Producer rings update Link cycle before publishing the preceding final TRB and toggle producer cycle at wrap. Runtime tests cross multiple producer/event wraps, including duplicate and erroneous events.

## Diagnostic image

`BURSZTYNDIAGXHCIPOINTING=0` is the production default. Setting it to 1 enables the shared bounded snapshot stream. Cheap saturating counters are maintained in both builds. Serial formatting occurs only in deferred/initialization contexts, never hard IRQ.

Snapshot order is: monotonic timestamp ns, owner ID, owner bus, PS/2 IRQ12 entries, PS/2 bytes, decoded packets, published reports, owner transitions, owner-accepted reports, nonowner-ignored reports, xHCI IRQs, event-ring events, Transfer Events, command completions, invalid completion codes, USB HID completions, structurally accepted reports, rejected reports, proven transitions, graphics pointer consumptions, cursor commits. Device lines carry ID/bus/state (0 inactive, 1 armed, 2 proven). Counters count reports/commits where indicated, rather than every axis/button field.

Snapshots occur at controller ready/error, USB registration, first IN submission, first Transfer Event, first valid report, owner changes, first PS/2 IRQ seen after xHCI activation and first ownership drop of a PS/2 report. HID completion diagnostics include slot, endpoint, completion code, TRB, residual and requested length. The one-shot `[XHCI INIT]` line includes `PS2IRQ12BEFORE`, `PS2IRQ12AFTER`, `CURRENTPOINTEROWNER`, `XHCIVECTOR`, `PS2VECTOR`, `XHCIIRQMODE`, `IFSTATE`. Diagnostic initialization asserts that IF equals its entry state on every return path.

Boot initialization normally has IF clear; inspect the later `ps2-irq12-after-xhci`, first-report and ownership snapshots during manual movement to distinguish missing IRQs from ownership suppression. No periodic owner reset or silent owner timer exists.

## Validation

All results below are **host results**, not VM runtime results. Assertions/checks are reported separately from the existing suite's named test cases.

| Area | Result |
| --- | --- |
| Shared ownership/mapping and USB readiness | 141 checks PASS |
| Failed first xHCI submission preserving PS/2 | 24 checks PASS |
| Common production IRQ allocator | 3,740 checks PASS |
| Production xHCI IRQ setup, BDF, capability boundaries, MSI/MSI-X readback failure/fallback and IF | 133 checks PASS (3,873 combined interrupt checks) |
| Command/transfer ring wrap and Link cycle | 1,030 checks PASS |
| Runtime completions, repeated event wraps, ERDP/EHB, requeue, bad IDs, stale/duplicate events, removal, post-free events and controller failure | 4,684 checks PASS (5,714 combined ring/completion checks) |
| PSI parsing, endpoint fields, high-speed interval zero and 64-bit dequeue | 15 checks PASS |
| OHCI production enumeration/transfers/failure regression | 444 checks PASS |
| PS/2 packet parser regression | 330,443 checks PASS |
| i8042 initialization, IRQ1/IRQ12 and deferred delivery | 4,132 checks PASS |
| HID descriptor/report regression | 6,051 checks PASS |
| USB configuration parser regression | 48 checks PASS |
| Full existing PCI/VirtIO/input/USB keyboard suite | 166 named cases PASS, 0 FAIL; includes complete existing VirtIO input lifecycle/activation/fault/queue/IRQ coverage and shared PS/2/USB/VirtIO keyboard cases |
| Existing boot-order checks | PASS |
| Production heap/PMM/PCI locks and prior-IF restoration | 4 cases PASS |
| Absolute mapping diagnostics | PASS |
| Cursor host regression | PASS |
| Production keyboard/TEXTINPUT/dialog regression | 7,291/7,291 PASS |
| ASan | PASS, no findings; new/affected pointer, interrupt and full VirtIO suites used leak detection |
| UBSan | PASS, no findings; final core suites used `halt_on_error=1` |
| Exact `make -B -j2 all iso` | PASS, exit 0, final normal build with diagnostics disabled |
| Diagnostic build and ISO extraction | PASS; embedded diagnostic markers verified |

ASan and UBSan ran together via `-fsanitize=address,undefined`. Both normal and diagnostic variants of the pointer/IRQ host target passed. The latest normal variant additionally covers the filled-ring first-submission failure and late post-free/continued-command cases. GUI host targets use their existing leak settings. Preserved logs are under `.pointingtest/validation/`.

Final normal build command: `make -B -j2 all iso`. The diagnostic image was fully rebuilt with `BURSZTYNDIAGXHCIPOINTING=1`, then its affected production objects were refreshed after the split-report preservation adjustment and relinked using `make -j2 all iso BURSZTYNDIAGXHCIPOINTING=1 ISO=BursztynOS-xhci-pointing-diag.iso`. The final normal rebuild restored all current objects and ISO staging to diagnostics off.

The kernels were extracted from both final ISOs with xorriso. All new one-shot markers were present in the diagnostic kernel and absent in the normal kernel. The extracted normal kernel equals the current `system_operacyjny.bin`.

SHA-256:

```text
b32885b55e1db5e27fcc035b8bd20cf2f72a7721ebe66e345fd35a80cae24eb2  BursztynOS.iso
8bba192735834fe075148ab77d035971e5105c3f114aed303a301955e1f0ede4  BursztynOS-xhci-pointing-diag.iso
```

Hardware controller reset, physical MSI/MSI-X delivery, IRQ12 continuity and cursor/button behavior in the five requested VM scenarios still require manual acceptance. The host suite validates production logic with substituted MMIO/PCI/IRQ/DMA boundaries; it does not emulate an entire running USB controller or prove every physical initialization failure path.

## Changed source files

Production/build files: `Makefile`, `grafika.cpp` (counters only), `idt.cpp`, `input_device.cpp`, `input_device.h`, `kernel.cpp` (PS/2-before-USB order), `mysz.cpp` (counters only), `pci.cpp`, `pointing_diag.h`, `sterowniki/usb/hid_pointer.cpp`, `sterowniki/usb/hid_pointer.h`, `sterowniki/usb/ohci.cpp`, `sterowniki/usb/xhci.cpp`, `sterowniki/usb/xhci_ring.cpp`, and new `xhci_pointing_diag.h`.

Tests changed: `narzedzia/test_pointing.cpp`, `narzedzia/test_xhci_pointer.cpp`, `narzedzia/mock_virtio_input_init.inc` (complete the fixture's PCI capability chain). Test added: `narzedzia/test_irq_allocation.cpp`. This report is `docs/xhci_pointing_regression_2026-10-10.md`.

PS/2 packet parsing, HID descriptor/report parsing, launchers, VM configuration, disk images, installer disk-write rules, BSP2, GPU timing, splash, filesystem bootstrap, network and restart behavior were not changed. New kernel/ISO/build artifacts are generated outputs. Runtime acceptance for all five requested VirtualBox/OHCI/PS2/VirtIO scenarios remains the user's task; no VM was launched.
