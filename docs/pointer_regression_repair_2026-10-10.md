# Pointer regression repair candidate — 2026-10-10

**Status: host-validated candidate; VirtualBox runtime regression and its exact physical cause remain unconfirmed.** No VirtualBox or QEMU was launched. The user's observations that production PS/2 and OHCI tablet behavior regressed are accepted. Passing host tests do not supersede those observations or establish runtime PASS.

This investigation used `docs/xhci_pointing_regression_2026-10-10.md`, the complete incoming worktree diff, and immediately preceding commit `7c7653a`. The latest xHCI patch was uncommitted, so there is no separate introducing commit hash. Its incoming diff was saved at `/tmp/bursztyn-before-pointer-repair.patch` before editing.

## Attribution and evidence

| Question | Finding |
| --- | --- |
| Exact runtime regression root cause | **Not established.** The reported uncontrolled PS/2 motion and dead OHCI tablet have not been reproduced with the available host hardware substitutions. These ISOs require manual acceptance. |
| Identifiable introducing difference | The incoming patch changed `kernel_main()` from USB setup → PS/2 negotiation/streaming to PS/2 negotiation/streaming → USB setup. The known-working hardware order is restored. This is the substantive shared hardware initialization change; its physical contribution is still a hypothesis. |
| Relative PS/2 interpreted as absolute | No such conversion found in the incoming diff or traced production path. EV_REL and EV_ABS were already distinct. The new registered mode additionally rejects mismatched coordinate events. |
| Signed promoted to unsigned | InputEvent's existing 32-bit wire value stores negative deltas as unsigned bit patterns. They were decoded back to signed before accumulation. No unsigned cursor arithmetic was found. Decoding now uses an explicit portable signed-bit-pattern helper. |
| Y inverted twice | No effective double inversion found. The unchanged PS/2 parser converts wire-upward Y to screen-down Y once. InputDevice negates Y to enter the legacy graphics API, whose subtraction cancels that adapter sign. This convention matches `7c7653a`. |
| Relative delta clamped before accumulation | No. Both preceding versions and the final code add signed deltas to the current position in a wide signed intermediate, then clamp the final position. |
| Invalid geometry | A deterministic host test demonstrated that the incoming implementation discarded owner movement when geometry was unavailable. This is a latent readiness defect also present before the xHCI patch; it is **not proof of the reported runtime cause**. An explicit display barrier and retained pending input repair it. |
| PS/2-before-USB order | Reverted to the preceding working order. Starting streaming before bounded USB initialization with IF clear lengthens the interval during which i8042 output is unserviced. FIFO/protocol consequences in VirtualBox have not been measured. Normal PS/2 deferred publication already occurs in the idle service after graphics bootstrap, so the order change alone does not prove early graphics publication. |
| Ownership changes | Registration, proof and fallback do not write cursor coordinates. Inactive/armed USB remains ineligible. No premature takeover reproduced for the VirtualBox fixture. Pending motion belonging to an old owner is now discarded on transition so it cannot replay on fallback. |
| Why OHCI tablet stopped at runtime | **Unconfirmed.** The incoming OHCI completion → decode → prove → owner → mapping path passes the representative VirtualBox descriptor/report stream. It would be inaccurate to attribute the observed failure to an axis-validation defect without a runtime report/completion trace. |
| First-axis proof rule rejects valid VirtualBox reports | Not for the examined eight-byte VirtualBox fixture: both absolute axes decode, including a stationary first report, and the state reaches PROVEN_ACTIVE. Separate button-only IDs remain rejected before proof and accepted afterwards. No descriptor/report parser rules were loosened. |
| What changed for OHCI | Restored the shared boot hardware order; explicit descriptor-derived pointer mode, shared geometry deferral and controller provenance for diagnostics. OHCI TD scheduling/completion-length arithmetic and the HID compiler/decoder are unchanged. |
| Additional xHCI changes | Controller provenance for diagnostics only in this repair. The inherited IRQ, context, interval, ring and completion fixes remain intact. No new transport fix has been claimed as a demonstrated VirtualBox runtime fix. |

The same 396,600 signed movement assertions pass against `7c7653a`, against the reconstructed incoming xHCI patch, and against the repaired implementation. Thus the requested deterministic reproduction of the particular VirtualBox teleport symptom has **not** been achieved. The pre-fix failing host test reproduces lost movement before valid geometry, not the uncontrolled-motion symptom. Logs preserve this distinction.

## Production pipeline and coordinate contract

PS/2: i8042 IRQ12/vector 44 drains only AUX bytes, at most 64 per entry → unchanged `Ps2MouseParser` validates framing/signs and normalizes wire Y → complete signed reports enter the existing 64-slot SPSC queue → `poll_mouse()` publishes EV_REL/KEY/SYN in deferred context → InputDevice accumulates signed deltas and checks ownership/readiness → graphics adds to the current cursor, clamps the final coordinate and commits GUI/cursor state. IRQ1 does not consume AUX bytes. No serial formatting occurs in the hard IRQ.

OHCI: interrupt TD is queued → matching retired TD and completion code/current-buffer pointer determine actual report length → shared descriptor-compiled HID decoder emits EV_ABS or EV_REL according to HID Input flags → first valid absolute X+Y report proves eligibility → common arbitration → shared mapping and graphics commit → exactly one next interrupt TD. Empty/short initial reports keep PS/2 as owner and requeue. Explicit controller/TD failure or disconnect removes the USB pointer and restores the best fallback.

xHCI: interrupt IN TRB/doorbell → real MSI/MSI-X notification → bounded deferred event consumption → expected live slot, endpoint, aligned outstanding TRB, completion code and residual validation → the **same** HID publication, eligibility, mapping and cursor path → exactly one requeue while alive. Unexpected/stale/duplicate completions cannot consume or requeue the live buffer.

| Backend | Declared coordinate mode |
| --- | --- |
| PS/2 Mouse | RELATIVE; parser outputs signed screen-down deltas |
| USB Tablet | ABSOLUTE, from compiled HID Input flags |
| USB HID Mouse | RELATIVE, from compiled HID Input flags |
| VirtIO Mouse | RELATIVE, from discovered capabilities |
| VirtIO Tablet | ABSOLUTE, from discovered capabilities |
| Hybrid VirtIO pointer advertising both | Explicit RELATIVE_AND_ABSOLUTE capability; each EV_REL/EV_ABS event still selects its semantics |

`InputPointerMode` is an enum. Legacy API clients that declare mouse/tablet capabilities receive the corresponding mode at registration; bus, priority, device ID and numerical values never choose coordinate semantics. All production pointer backends explicitly assign their mode. InputEvent remains the eight-byte Linux/VirtIO wire ABI. `input_signed_value()` decodes it before signed arithmetic. Unknown codes, invalid key values, unregistered senders and mode-mismatched axes cannot move the cursor.

`pointer_accumulate()` is used by the real graphics function and host cursor adapter. It computes `current + signed_delta` in int64, then clamps to `[0, dimension-1]`. The legacy graphics Y adapter preserves the existing effective screen-down result. Absolute HID axes alone use logical min/max scaling, in int64. No relative delta passes through that scaling. Large reported display dimensions are bounded to INT32_MAX before signed conversion.

## Readiness, USB lifecycle and ownership

The display delivery barrier starts closed. Kernel closes it explicitly before device setup, completes USB controller/firmware setup, then negotiates PS/2 hardware streaming. It opens the common barrier after `ekran_inicjalizuj()` and `PrezentacjaStartu::zakoncz_inicjalizacje()`, before existing late VirtIO activation. This ordering is unconditional in normal, diagnostic and Installer builds.

With the barrier closed or either dimension nonpositive, pointer-only poll callbacks retain their hardware report queue. Direct InputDevice owner input retains signed accumulated deltas, last absolute axes and button state instead of clearing them. Existing deferred service flushes retained owner input when geometry is valid. Keyboard and combined keyboard/pointer callbacks remain serviceable; only pointer delivery waits. No width/height-minus-one arithmetic executes with zero dimensions. No resolution is fabricated. No sleep, delay, extra transport polling, periodic ownership timer or watchdog was added.

USB lifecycle remains REGISTERED_INACTIVE → ARMED after successful interrupt submission → PROVEN_ACTIVE only after a successful valid runtime completion/report. Enumeration, descriptor compilation, endpoint configuration, queue allocation and registration alone confer no eligibility. The first absolute proof requires both safely decoded axes in the valid report, even if stationary. A button-only report cannot establish initial absolute readiness. Split button-only report IDs work after proof. The HID descriptor compiler and report decoder were not rewritten.

`input_recalc_pointer_owner()` remains the sole arbitrator: VirtIO 30, USB 20, PS/2 10, ties by registry slot. Transition, registration and fallback never reposition the cursor. An accepted absolute report can naturally reposition it. Failure before proof leaves PS/2 eligible; failure/disconnect after proof unregisters USB and restores the best eligible fallback at the existing cursor location. Pending old-owner motion is not replayed after fallback. Explicit failure/disconnect is used; silence alone does not trigger a timer.

## Preserved interrupt and xHCI behavior

No source evidence of an IRQ12 vector collision or xHCI IOAPIC route modification was found. Whether physical IRQ12 delivery is affected remains a manual diagnostic question. Dynamic xHCI allocation remains through the common allocator, vectors 48..254, excluding PS/2 vector 44. Repeated initialization and allocation failure tests pass. Prior IF state is preserved by the audited PCI/IDT helpers; no new CLI/STI or interrupt lock was added. Host IF/lock tests pass; a boot IF=0 snapshot alone is not a failure.

xHCI selects validated MSI-X within BAR0 when available, otherwise MSI; unusable message routing fails cleanly. Actual VirtualBox vector/mode still require runtime observation. PCI RW1C-safe command writes, BDF isolation, capability extent validation and existing MSI/MSI-X failure handling are preserved. OHCI-only enumeration does not invoke xHCI message programming without a matching xHCI device.

Preserved xHCI fixes include Max ESIT Payload, PSI count extraction, valid interval zero, Link-before-data publication, unaligned event rejection, DMA lifetime/quarantine and full 64-bit addresses. Low/full-speed `bInterval` maps to `3 + floor(log2(bInterval))`; high/Super-speed maps to `bInterval - 1`, with descriptor range validation. Producer cycle toggles at Link wrap; event consumer cycle toggles at event-ring wrap. ERDP advances to the next event with EHB write-one-clear. Runtime events remain IRQ-notified and deferred/bounded. One outstanding HID transfer is retired once and requeued once; error/removal withdraws readiness without freeing controller-owned DMA.

## Diagnostics

Production defaults remain `BURSZTYNDIAGXHCIPOINTING=0`, `BURSZTYNDIAGPS2=0`, `BURSZTYNDIAGUSBHID=0`. The diagnostic ISO enables those three flags. Production behavior/readiness/order is identical; flags enable bounded formatting only. Cheap counters remain present in both builds.

The first 32 pointer SYN frames per registered device record device ID, bus, owner, actual coordinate mode, unsigned wire bits, signed values, cursor before/after, width/height, owner acceptance/rejection or geometry deferral, lifecycle state and USB controller provenance. USB's first 64 decoded reports record report ID, decoded axes, logical min/max, state and first-valid status; the central trace supplies mapped/committed positions. Existing bounded PS/2 byte/packet samples include raw bytes, signed deltas and the parser Y-inversion location. IRQ context stores samples/counters only.

The one-shot `[POINTER BOOT]` line contains INPUTSUBSYSTEMREADY, CURSORGEOMETRYREADY, SCREENWIDTH, SCREENHEIGHT, PS2REGISTERED, PS2ELIGIBLE, CURRENTPOINTEROWNER, OHCIPOINTERSTATE and XHCIPOINTERSTATE. Existing xHCI boot/first-completion/error/ownership snapshots and PS2IRQ12BEFORE/AFTER, XHCIVECTOR, PS2VECTOR, XHCIIRQMODE, IFSTATE are preserved. Inactive/armed/proven states are 0/1/2; missing controller pointer state is -1. Controller provenance is explicit, not inferred from device ID.

## Host validation

Totals below are assertion/check totals unless identified as named cases. Normal and diagnostic pointer variants pass. ASan and UBSan are enabled together with `-fsanitize=address,undefined`; final runs use `UBSAN_OPTIONS=halt_on_error=1`. Existing GUI/installer targets retain their established leak-detection settings. No sanitizer memory, overflow, shift, bounds or lifetime findings occurred.

| Suite | Final result |
| --- | --- |
| Signed relative pointer regression | 396,600 checks PASS; center/edges/corners, 1280×720, 1920×1080, 1×1 and INT_MAX dimensions; requested signed deltas, long directional/alternating movement, exact coordinates and physical Y direction |
| Existing InputDevice ownership/mapping | 141 checks PASS |
| New InputDevice modes/readiness/API | 56 checks PASS; 197 combined shared InputDevice checks |
| Hardware initialization/display barrier | 6 source order checks PASS; real i8042 deferred-ready cases are included below |
| OHCI production enumeration/completion/failure | 5,855 checks PASS, including 600 shared VirtualBox-style reports, empty/short first reports, stationary initial proof, mapping/buttons and requeue |
| xHCI production runtime completions | 9,484 checks PASS, including the identical 600-report stream, expected mapping/buttons, repeated wraps, stale/duplicate/bad IDs, removal and error handling |
| xHCI producer/Link rings | 1,030 checks PASS; 10,514 combined ring/runtime checks |
| xHCI first submission failure | 24 checks PASS |
| xHCI contexts/protocol/interval | 15 checks PASS |
| Production interrupt allocator | 3,740 checks PASS |
| Production xHCI MSI/MSI-X/BDF/capability/IF setup | 133 checks PASS; 3,873 combined interrupt checks |
| Existing PS/2 parser, unchanged | 330,443 checks PASS |
| Production i8042/IRQ1/IRQ12/init/deferred queue | 4,140 checks PASS, including hardware-ready before pointer-delivery-ready and producer/consumer concurrency |
| HID descriptor/report | 6,051 checks PASS |
| USB configuration parsing | 48 checks PASS |
| Full existing PCI/VirtIO/input/USB keyboard | 166 named cases PASS, 0 FAIL; retains full VirtIO lifecycle/activation/IRQ/fault/input and keyboard coverage |
| VirtIO input subset of the 166-case suite | 41 named cases PASS (activation, preparation, PMM/DMA, initialization and input protocol) |
| Keyboard subset of the 166-case suite | 10 named cases PASS, including shared PS/2/USB/VirtIO keyboard behavior |
| Existing VirtIO boot-order audit | PASS |
| Production keyboard/TEXTINPUT/dialogs | 7,291/7,291 PASS |
| Real graphics + InputDevice + HID integration | 40 checks PASS; real cursor functions, relative signs, absolute mapping and transitions |
| Existing cursor/absolute diagnostics host suites | PASS |
| Installer/storage/BSP2 host tests | 10,493 checks PASS, 0 FAIL; RAM directory/file move 138 checks PASS |
| Production heap/PMM/PCI locks | 4 named cases PASS |
| Storage/workspace concurrency | 10 cases PASS |
| ASan | PASS, no findings |
| UBSan | PASS, no findings |

Both transports consume the same descriptor and 600-report coordinate/button sequence, with the same exact expected final coordinates. These transport tests exercise production TD/TRB completion/service code with substituted hardware boundaries. The real-graphics integration separately links production graphics/InputDevice/HID, avoiding cursor-stub-only validation. It does not emulate VirtualBox's physical controller behavior.

Logs are under `.pointingtest/regression-validation/`, including `pre-fix.txt`, `baseline-relative.txt`, `latest-patch-relative.txt`, `core.txt`, `diagnostic-core.txt`, `final-trace-test.txt`, `real-graphics.txt`, `virtio-locks.txt` and `gui-installer-concurrency.txt`.

## Source changes

Production/build files changed in this repair relative to the incoming worktree: `Makefile`, `grafika.cpp`, `input_device.cpp`, `input_device.h`, `kernel.cpp`, `mysz.cpp`, `virtio_input.cpp`, `sterowniki/usb/hid_pointer.cpp`, `sterowniki/usb/ohci.cpp`, `sterowniki/usb/xhci.cpp`; new `pointer_coordinates.h`.

Inherited uncommitted production changes retained: `idt.cpp`, `pci.cpp`, `pointing_diag.h`, `sterowniki/usb/hid_pointer.h`, `sterowniki/usb/xhci_ring.cpp`, new `xhci_pointing_diag.h`, plus the inherited portions of production files listed above. No wholesale revert was performed.

Test files changed in this repair: `narzedzia/pointing_host_stubs.h`, `narzedzia/test_abs_map_diag.cpp`, `narzedzia/test_ohci.cpp`, `narzedzia/test_ps2_controller.cpp`, `narzedzia/test_splash_production_path.cpp` (fixture/main conditional only), `narzedzia/test_virtio_pci.cpp` (readiness setup only), `narzedzia/test_xhci_pointer.cpp`.

Test files added in this repair: `narzedzia/test_pointer_regression.cpp`, `narzedzia/test_pointer_graphics.cpp`, `narzedzia/test_pointer_boot_order.py`, `narzedzia/pointer_transport_stream.h`.

Inherited test changes retained: `narzedzia/test_pointing.cpp`, `narzedzia/mock_virtio_input_init.inc`, new `narzedzia/test_irq_allocation.cpp`.

The PS/2 parser and HID compiler/decoder are unchanged. Launchers, VM configuration, disk images, installer disk-write behavior, BSP2 implementation, GPU timeouts, splash behavior, filesystem bootstrap, network and restart behavior are unchanged. No unrelated GUI change, `make rm`, `make clean`, VirtualBox or QEMU run was performed.

## Build artifacts and remaining acceptance

The diagnostic image was fully rebuilt with all three pointing flags, then its affected objects were refreshed after correcting the trace label for hybrid capabilities. The final normal build is the exact `make -B -j2 all iso`, with all three flags at their default zero values. Build exit codes, hashes and extraction verification are recorded below after completion.

Manual acceptance remains required for OHCI + PS/2, OHCI + USB Tablet, xHCI + PS/2 without any proven tablet, xHCI + USB Tablet, and existing VirtIO input behavior. Occasional cursor stalls were not optimized. The diagnostic should first determine whether IRQ12 packets are valid, which owner accepts each report, and where coordinates change. Until that evidence or successful manual acceptance arrives, this report does **not** claim the production runtime regression is conclusively fixed.

Final exact `make -B -j2 all iso`: **PASS, exit 0**, diagnostics disabled.
Diagnostic full build and final diagnostic object refresh/relink: **PASS, exit 0**.
ISO kernel extraction: all seven tested diagnostic markers present in diagnostic kernel and absent in normal kernel. The normal extracted kernel exactly matches final `system_operacyjny.bin`.

SHA-256:

```text
31c643d28bbfbf08bb6c4768b76b300f2a470a02bb6425fd9f9ab32b1b864b99  BursztynOS.iso
abd8016c03599e5df8b216bd3e31572463c75dcbe93c9d2091c4208e4ae7dbb8  BursztynOS-pointer-regression-diag.iso
```
