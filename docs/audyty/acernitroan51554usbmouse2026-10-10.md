# Physical Intel xHCI / wired USB mouse investigation — 2026-10-10

The starting worktree was clean at `f026e85`. The user’s physical observations are the acceptance baseline: the Acer Nitro AN515-54 reaches the desktop, its touchpad and keyboard work, and a directly attached wired USB mouse does not. No physical PCI dump, mouse descriptor, or controller log was supplied. **The actual Acer stopping stage and runtime root cause are not yet established.** Neither a virtual machine nor the physical laptop was run during this work.

Code inspection demonstrated generic defects below. The new host fixture reproduces controller register semantics and verifies the repaired boot-to-live-report path. This is evidence for the code repairs, not a claim that the Acer mouse now works. The diagnostic ISO is the first physical acceptance artifact.

## Demonstrated defects and repairs

1. `usb_control_transfer()` set Chain in the Setup Stage and in the sole Data Stage preceding Status. Setup’s bit is reserved; the final Data TRB must not chain into another control stage. The repaired construction uses separate stages, correct TRT/directions, immediate Setup data and Status IOC. It reserves space for the entire request and publishes Setup’s cycle only after Data/Status are complete. The old invalid control format is a concrete assumption an emulator might tolerate and real hardware can reject; whether Intel rejected it on this laptop is still unknown.
2. Legacy OS ownership was already requested and BIOS release waited for with a bound. However, USBLEGCTLSTS handling discarded the reserved fields at bits 17–19 and did not acknowledge the defined SMI event bits. The repair preserves the defined reserved fields, disables SMI enables, writes zero to reserved 21–28 and acknowledges only defined RW1C events 29–31. A truncated legacy capability is now rejected **before** either register is written. BIOS timeout fails safely; the kernel does not forcibly clear BIOS ownership.
3. Root-port CSC and other change flags were not consistently acknowledged at initial discovery or in deferred port-change service. A stale asserted change flag can prevent the next connection change from generating a fresh notification. The repair acknowledges only captured W1C change bits, before servicing a fresh attachment. Empty ports with stale CSC and insertion on port 9 after boot are covered by the fixture. It never writes cached PORTSC wholesale.
4. The previous USB3 path always issued Warm Port Reset. Enabled U0 SuperSpeed links now remain enabled; ordinary reset uses PR, and warm reset is reserved for recovery states (Inactive, Compliance, Recovery). USB2 still uses PR and waits for PRC/PED. Ports with per-port power control are powered explicitly. No sleep or retry loop was added.
5. AC64 was not enforced. Every xHCI-owned DMA allocation and ring now validates its physical extent against AC64 before hardware publication. A controller without AC64 rejects an above-4-GiB allocation cleanly. The existing physical allocator searches from low memory first. No address is truncated and no virtual pointer is handed to hardware. Firmware bus mastering is disabled before BAR probing/structure replacement and enabled only after ownership, reset, DMA and IRQ setup.
6. MSI-X supported only table/PBA BIR zero. The repaired path validates and maps each specified memory BAR separately, rejects upper halves of 64-bit BARs, checks table/PBA extents, and preserves MSI fallback. Both DWORDs of a 64-bit BAR are probed before either mask is read; PCI command writes continue to exclude status RW1C bits.
7. Boot Mouse was a dead classification: mouse interfaces always required a supported Report descriptor and Report protocol. Subclass 1/protocol 2 now selects Boot protocol explicitly and can use standard three-byte relative reports without a successfully compiled Report descriptor. An optional descriptor fetch can identify a conventional fourth-byte wheel; unknown device-specific extension bytes are not guessed to be wheels. Report-protocol mice still use the unchanged strict descriptor compiler. A stalled protocol request explicitly recovers halted EP0 once using Reset Endpoint and Set TR Dequeue, then allows descriptor-based default Report operation. No automatic reset loop is introduced.
8. Periodic endpoint metadata discarded high-speed transactions and SuperSpeed companion information. These are now parsed and validated, and Max Burst, Average TRB Length and Max ESIT Payload use the descriptor’s periodic payload. The single receive buffer covers that payload. A receive buffer crossing a 64-KiB TRB boundary is rejected safely. The existing nonzero Max ESIT fix and valid encoded interval zero are retained.

9. Disable Slot checked the command TRB and success code but not the returned slot ID. It now rejects a mismatched slot before considering DMA quiescent; the host test verifies the device remains enabled/quarantined in this case.

## Audit of the complete path

| Area | Finding / final behavior |
|---|---|
| PCI discovery / BDF | Generic class `0c:03:30` scan, no fixed BDF or VID/PID filter. Every write uses the discovered controller’s BDF. One controller is supported by the existing driver. |
| BAR address, sizing, mapping | Memory BAR type/size/alignment/range validated; 64-bit probe ordering repaired. MMIO mapped uncached. BAR size bound remains 16 MiB. MSI-X table/PBA may use different BARs. |
| PCI command | Memory Space enabled for register access, Bus Master enabled only when structures and routing are ready. I/O Space is not added. Status RW1C protection retained. |
| Capability registers | CAPLENGTH, operational/doorbell/runtime extents, nonzero slots/interrupters and bounded ports validated. Diagnostic logs all requested capability fields, revision and BDF. |
| Extended capabilities / PSI | Bounded aligned chain and capability extents. Previously corrected PSIC bits 31:28 retained. USB2 ports 1–14 and USB3 ports 15–20 are exercised independently. These are synthetic fixture values, **not observed Acer values**. |
| Legacy BIOS ownership | Ownership request/wait was present, not a newly discovered missing handoff. Reserved-field/SMI-event manipulation and pre-write bounds were defective and repaired. Logs original/final state and timeout. |
| Halt / reset / CNR | Existing halt → HCRST-clear → CNR-clear sequence retained. Added CNR check before operational writes, disabled controller interrupt enable while halting, and bounded halt on a post-IRQ-setup init error. No arbitrary delays or increased timeouts. |
| Page size | Existing PAGESIZE bit-zero check was correct. Requires supported 4096-byte pages; unsupported mask fails before DMA allocation. |
| Slots / scratchpads / DCBAA | Scratchpad extraction was already correct: `(((HCS2 >> 21) & 31) << 5) | ((HCS2 >> 27) & 31)`. Exact bounded count allocated; physical buffer-array address stored at DCBAA[0]. Four buffers and zero/max extraction are tested. Actual Acer count unknown. Disabled slot’s DCBAA entry is cleared before freeing its contexts. |
| Context size / streams | Existing CSZ-based 32/64-byte stride was correct; both complete enumeration paths now tested. All allocations are zeroed. No streams enabled, MaxPStreams zero. |
| Physical DMA / coherence | Existing contiguous PMM allocations and physical addresses were correct by inspection; missing AC64 guard repaired. Structures survive until quiescence. x86 coherent RAM plus release/acquire fences retained; MMIO is uncached. No evidence of an Acer IOMMU or cache problem was supplied. |
| CRCR / rings / ERST | Correct physical addresses, 64-byte alignment and initial cycle. Ring-full rejection, producer wrap, Link Toggle Cycle, Link-before-data publication and observed field-before-cycle ordering tested. Control Setup publication additionally deferred until the complete sequence exists. |
| Runtime / interrupter | Interrupter zero, ERSTSZ=1, validated ERSTBA/ERDP, IMOD=0. IRQ masks IE and acknowledges IP/EINT; deferred service advances ERDP with EHB W1C and re-enables IE without clearing a newly pending IP. |
| Actual PCI interrupt route | Common allocator, unique dynamic vector 48–254, separate from PS/2 vector 44. Valid MSI-X from any supported memory BAR, then validated MSI. No platform PCI INTx routing is implemented, so unusable MSI/MSI-X fails explicitly. Actual Acer mode/vector is unknown until the diagnostic log. No production polling fallback. |
| IRQ / EOI / IF / locks | Hard IRQ only counters, acknowledgement and deferred notification. Common dispatch issues EOI. No descriptor parsing/control waits in IRQ. xHCI does not use CLI/STI or hold an InputDevice/global interrupt lock across waits. PCI/IDT save/restore and lock suites pass for both prior IF states; no vector collision found. |
| Root ports / insertion | Every port logged; Supported Protocol association retained. Fresh captured W1C acknowledgement repairs stale-change handling. PORTSC writes preserve PP/PIC/wake fields, exclude PED and unselected W1C bits, and never echo cached status wholesale. |
| USB2 / USB3 reset | USB2 PR/PRC preserved. USB3 enabled U0 left alone; normal/hot reset and recovery warm reset separated. State-bit completion, CCS, PED and speed checked with existing bounded waits. |
| Slot / device context | Root route string zero, root-port number from discovered port, speed from PORTSC/protocol association. Enable Slot, Address Device, Evaluate Context and Configure Endpoint matched to exact command TRB and expected slot. Disable Slot now also rejects an unexpected completion slot before freeing DMA. Unaligned command pointers rejected. |
| EP0 MPS / Evaluate Context | Initial LS/FS 8, HS 64, SS/SS+ 512, then validated first descriptor MPS. Full-speed 64-byte EP0 update covered. Evaluate now preserves the existing endpoint context while replacing MPS. SuperSpeedPlus uses the SuperSpeed EP0 rules. |
| Control stages / completions | Reserved Chain defect repaired. IN/OUT/no-data directions and setup bytes tested. Valid Short Packet residual retained until Status; successful actual length is requested minus residual. Stall, TRB Error, Transaction Error, Babble and Data Buffer Error are distinguished. Stalled optional class requests quiesce/reposition EP0 before reuse. |
| Device / config descriptors | First 8 and complete 18 bytes validated, config header fetched before bounded complete config. Entire descriptor chain validated even after selection. Vendor interface precedes HID interface 3, endpoint `0x85`, in the synthetic test. No interface-zero or endpoint-one assumption. |
| HID configuration / protocol | SET_CONFIGURATION retained. Boot Mouse gets SET_PROTOCOL Boot, generic pointer uses Report descriptor. Boot Keyboard’s existing Boot protocol/idle path preserved; idle Stall is usable only if EP0 recovery succeeds. Report protocol refusal does not bypass decoding checks. |
| Interrupt endpoint / interval | IN DCI = `2*N+1`, OUT DCI = `2*N`, EP0=1 separately. Interrupt IN type 7, descriptor MPS, CErr=3, zero streams. LS/FS interval = `3 + floor(log2(bInterval))`; HS/SS/SS+ = `bInterval-1`, 1..16. Valid encoded zero retained. |
| Max ESIT / companion | Previous one-packet Max ESIT support was correct for ordinary mice, not the whole periodic descriptor space. New HS transactions and SS companion payload/burst supported. Context tests verify HS payload 192 and SS payload 96, including receive-buffer length and average TRB length. |
| Transfer matching / actual bytes | Existing exact outstanding TRB, alignment, slot/DCI, code and residual checks retained. Success/Short only; residual greater than submission rejected. Zero/short malformed reports do not prove readiness. Duplicate/stale/wrong-ID events do not retire/requeue the live buffer. |
| Event consumption / requeue | Existing consumer cycle toggles at segment wrap; ERDP advances to next unread event with EHB acknowledgement. Runtime processing is IRQ-notified and bounded to 256 events. One receive buffer outstanding; completion retires it once and submits exactly one successor. 800-report runs per context mode exercise repeated wraps. |
| HID Boot Mouse decode | New standard three-byte buttons/signed XY layout, including stationary report. Logical -127..127, optional descriptor-confirmed signed wheel. No absolute mapping or USB Y inversion. Unsupported Report descriptor compilation does not redefine the Boot protocol. |
| Generic HID decode | Existing compiler retained: Mouse/Pointer usages, Report IDs, packed signed Relative/Absolute fields, buttons, wheel and structural bounds. Added endpoint companion metadata, not a parser rewrite. Packed numbered 12-bit relative reports tested. |
| Signed movement / GUI | No defective unsigned cursor accumulation found. Wire `uint32_t` values are explicitly decoded to signed values before the existing accumulator. Relative values remain relative; checked wide accumulation precedes final screen clamping. InputDevice’s Y adapter cancels the legacy graphics API convention once; no additional USB Y inversion added. |
| Geometry / touchpad | Existing readiness barrier retains motion while geometry is zero; later service applies it safely. No framebuffer fabrication or initialization reorder. Working touchpad path, PS2 parser/hardware, keyboard and GUI production code unchanged. The physical touchpad transport was not measured; the host fixture represents an existing PS2 fallback owner. |
| Ownership | Existing priority VirtIO 30 / USB 20 / PS2 10 preserved. Registration and arming do not claim ownership. The first structurally valid successful live mouse report, including stationary XY, calls `input_arm_pointer()`/`input_prove_pointer()` and then central arbitration. Owner transitions do not alter coordinates. No ownership defect preventing a valid mouse from becoming active was found. |
| Failure / unplug | Pre-proof failure leaves fallback owner untouched. Error or disconnect unregisters USB pointer and restores fallback without resetting coordinates. Disable Slot must complete before DMA is freed. Failed quiescence retains/quarantines DMA; controller failure withdraws pointer eligibility and masks/releases its route. |
| Intel-specific initialization | Project had no relevant Intel mouse quirk table. No hardware evidence justifies a vendor/device quirk. Reviewed primary Linux Intel routing logic is conditional on an Intel EHCI companion, not an Acer-model rule. No model check or speculative PCI vendor register writes added. |

All waits remain bounded by the existing one-second HPET deadline and spin limit. Synchronous event consumption during command/control initialization is retained; normal runtime ring consumption requires the real IRQ notification. No watchdog, periodic ownership timer, polling fallback, sleep, VM change or priority workaround was introduced.

## Diagnostics and physical acceptance

`BURSZTYNDIAGXHCIPHYSICALMOUSE` defaults to zero. The diagnostic ISO enables only this new flag. Normal and diagnostic builds run the same controller, HID and ownership logic. Additional register reads/serial formatting are gated, and no serial formatting runs in hard IRQ. Diagnostics use the existing serial logger (`klog_serial` / `wypisz_log`).

The stream includes PCI identity/command before and final value, capability registers, legacy original/final ownership, halt/reset/CNR outcome, selected page/context sizes, slots/ports/interrupters/scratchpads/AC64, physical DCBAA/command/event/ERST addresses, IRQ mode/vector and initial/final IF. Per-port logs give protocol, PORTSC, reset type/result, speed, slot and final enabled/armed state. USB identity includes VID/PID/bcdUSB/class/raw EP0 MPS. Endpoint logs include configuration/interface/subclass/protocol/address/MPS/interval/DCI/encoded interval/average TRB/Max ESIT/burst.

Command logs include type, exact TRB, slot and numeric/symbolic completion. Control results are limited to 32 per device. Interrupt transfer/requeue and raw mouse reports are limited to 16 per device, raw bytes capped at 64. Shared decoded pointer logs include Report ID, signed XY, logical ranges, buttons, wheel, mode, readiness and owner. InputDevice traces (32 per registry slot) show accepted/rejected/deferred, signed values, cursor before/after and geometry. `[POINTER BOOT]` shows readiness, geometry and initial pointer states. Physical transport logs also have a 1024-line global ceiling. Keyboard report contents are never logged.

First boot the diagnostic ISO and establish the touchpad control. Connect a wired mouse directly to a root port. The expected sequence is `[XHCI-PHYSICAL controller]` → legacy handoff (if present) → halt/reset/run → port change/root-port number → reset → Enable Slot/Address Device → descriptors/configuration → HID endpoint → Boot or Report protocol → first receive → deferred IRQ/Transfer Event → valid decoded signed XY → `[XHCI-PHYSICAL live pointer]` → `[POINTER RELATIVE]` with correct cursor before/after. A stationary first report can prove liveness without producing a movement trace. If the progression stops, use the last successful stage and the adjacent completion/status line. The port final line distinguishes a discovered slot from an armed mouse.

No Acer runtime PASS is claimed. Required manual checks remain: working touchpad before attachment, controllable signed USB mouse movement and buttons, no registration-induced jump, clean fallback after removal, and preservation of existing emulator input behavior. No VM was launched here.

## Files

Production files changed:

- `Makefile`
- `input_device.cpp` (diagnostic filtering/boot snapshot only)
- `sterowniki/usb/hid_pointer.cpp`
- `sterowniki/usb/hid_pointer.h`
- `sterowniki/usb/xhci.cpp`
- `sterowniki/usb/xhci_ring.cpp`
- `sterowniki/usb/xhci_ring.h`
- `sterowniki/usb/xhci_hw.h` (new bounded register/control helpers)
- `sterowniki/usb/xhci_physical_diag.h` (new compile-time diagnostics)

New test file: `narzedzia/test_xhci_physical_mouse.cpp`. No existing test source/assertion was weakened or changed. This report is new. No touchpad/PS2 driver, I2C, kernel initialization order, graphics, launcher, installer, BSP2, filesystem, network, restart, splash, GPU mode or disk-image source was changed.

## Host validation

All C++ host suites below used ASan and UBSan. The physical fixture runs with diagnostics **off and on**, each with leak checking and halt-on-error, and uses the actual production PCI, IDT allocator, xHCI, ring, HID and InputDevice code. Only PCI/MMIO/DMA/clock/LAPIC/GUI boundaries are simulated. The fixture models RW1C, ownership handoff, reset completion, exact command events, control DMA, port insertion and IRQ/deferred delivery. It uses synthetic Intel-style IDs/capabilities, not an Acer-specific driver.

| Suite / category | Exact result |
|---|---:|
| New Intel capabilities / legacy / DMA width | 39 checks per build PASS |
| New control TRB construction | 32 per build PASS |
| New multi-interface / companion / interval / DCI / Boot / packed report | 1,597 per build PASS |
| New ring-full / wrap / Link / pre-cycle hardware observation | 1,497 per build PASS |
| New complete boot / hotplug / EP0 / live mouse / requeue / removal | 15,246 per build PASS |
| New control Short/Stall / port reset / failure / prior IF cases | 1,635 per build PASS |
| New already-connected-at-boot mouse | 275 per build PASS |
| **New physical xHCI suite** | **20,321 checks per build, 40,642 across both builds, PASS; 248 allocations / 248 releases per build** |
| Existing PS2 parser, unchanged | 330,443 checks PASS |
| Existing i8042 IRQ1/IRQ12/init/keyboard | 4,140 PASS |
| Existing HID descriptor/report | 6,051 PASS |
| Existing USB configuration | 48 PASS |
| Existing InputDevice ownership/mapping | 141 PASS |
| Existing relative cursor regression | 396,600 PASS |
| Existing coordinate modes/readiness/API | 56 PASS |
| Existing pointer boot-order/barrier script | 6 PASS |
| Existing OHCI production transport | 5,855 PASS, including 5,400 shared tablet-stream checks |
| Existing xHCI IRQ/BDF/MSI/MSI-X/failure/IF | 133 PASS |
| Existing xHCI ring/Link | 1,030 PASS |
| Existing xHCI context/protocol/interval | 15 PASS |
| Existing xHCI first-submission failure | 24 PASS |
| Existing xHCI completion/short/stale/removal | 9,484 PASS, including 4,800 shared tablet-stream checks |
| Existing common IRQ allocator | 3,740 PASS |
| Full existing VirtIO PCI/input/keyboard suite | 166 named cases PASS, 0 FAIL; includes 41 VirtIO input cases and 10 keyboard cases |
| Existing input initialization boot-order script | 1 named case PASS |
| Existing input-init heap/PMM/PCI lock suite | 4 named cases PASS |
| Existing cursor-host suite | Both cursor/coalescing and bounded diagnostics executables PASS (no numeric assertion total emitted) |
| Existing real graphics/InputDevice/HID integration | 40 PASS |
| Existing GUI keyboard/TEXTINPUT | 7,291 / 7,291 PASS |
| Existing installer/storage/BSP2 host suite | 10,493 PASS, 0 FAIL |
| Existing RAM file/directory move | 138 assertions PASS |
| Existing lock/concurrency suite | 10 named cases PASS |
| ASan | PASS; no address, use-after-free, double-free or leak findings |
| UBSan | PASS; no signed overflow, shifts, conversion-induced bounds or other runtime findings |

Commands: `make test-xhci-physical-mouse-host`; `make test-pointing-host test-pointer-regression-host test-irq-allocation-host`; `make test-virtio-pci test-input-init-locks`; `make test-cursor-host test-pointer-graphics-host test-gui-text-input-host installer-unit-test test-wspolbieznosc`. Full logs are in `.pointingtest/physical-mouse-validation/`. Stream subcounts are included in their suite totals and must not be summed twice.

## Builds and artifacts

Final builds completed on 2026-10-11, in this order:

```sh
make -B -j2 all iso BURSZTYNDIAGXHCIPHYSICALMOUSE=1 ISO=BursztynOS-xhci-physical-mouse-diag.iso
make -B -j2 all iso
```

Both returned exit 0 with zero compiler/linker warnings and passed the build's Multiboot2 validation. No `make clean`, `make rm`, QEMU or VirtualBox invocation was used.

| Artifact | Size | Diagnostic setting |
|---|---:|---|
| [BursztynOS.iso](../BursztynOS.iso) | 35,426,304 bytes | Disabled; default flags |
| [BursztynOS-xhci-physical-mouse-diag.iso](../BursztynOS-xhci-physical-mouse-diag.iso) | 35,442,688 bytes | Only `BURSZTYNDIAGXHCIPHYSICALMOUSE=1` |

SHA-256:

```text
6d0a7d5bd2009fabed5980dd13bfd52e9bc5e560ae8ab8496b2d64a400461c12  BursztynOS.iso
63d1bce004de4f98b6c7f5baab02e2b33ed7b9b46f491097fb0f9d494cc861d3  BursztynOS-xhci-physical-mouse-diag.iso
```

Each ISO's `/boot/system_operacyjny.bin` was extracted and compared byte-for-byte with its completed build output before starting another build. The diagnostic kernel contains controller, raw report, live-owner and cursor/geometry diagnostic markers. The production kernel contains none of the new physical-mouse or shared pointing diagnostic markers. The kernels differ. The diagnostic compiler log defines exactly the one requested `BURSZTYNDIAG` flag; the production compiler log defines none.

Build logs, extraction logs, artifact checks and a `sha256sum -c` compatible manifest are saved in [`.pointingtest/physical-mouse-validation/`](../.pointingtest/physical-mouse-validation/). The last build leaves the local kernel and staging directory in production configuration. **Physical Acer acceptance is pending the user's manual diagnostic-ISO test.**

## Primary references consulted

The audit needed precise hardware register/control semantics, so primary sources were consulted for those points only:

- [Linux xHCI control-stage publication](https://github.com/torvalds/linux/blob/master/drivers/usb/host/xhci-ring.c): separate Setup/Data/Status construction and first-cycle publication.
- [Linux xHCI extended-capability definitions](https://github.com/torvalds/linux/blob/master/drivers/usb/host/xhci-ext-caps.h) and [firmware/Intel PCI handoff](https://github.com/torvalds/linux/blob/master/drivers/usb/host/pci-quirks.c): legacy preservation masks, ownership and conditional EHCI-companion routing.
- [Linux xHCI endpoint setup](https://github.com/torvalds/linux/blob/master/drivers/usb/host/xhci-mem.c) and [root-port control](https://github.com/torvalds/linux/blob/master/drivers/usb/host/xhci-hub.c): interval/payload and port-state handling.
- [Linux PCI xHCI quirks](https://github.com/torvalds/linux/blob/master/drivers/usb/host/xhci-pci.c): reviewed without inventing a matching hardware quirk.
- [USB HID 1.11, §7.2.6 and Appendix B](https://www.usb.org/sites/default/files/hid1_11.pdf): protocol selection, relative Boot Mouse layout and device-specific optional bytes.
