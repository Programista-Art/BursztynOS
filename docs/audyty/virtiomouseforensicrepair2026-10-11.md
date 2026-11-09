The user manually tested the production build containing the previous Mimo repair: the VirtIO mouse was still broken. That repair and its claimed root cause are not accepted as runtime evidence.

# VirtIO mouse forensic investigation — 2026-10-11

## Result and limits of the evidence

The claimed keyboard-to-mouse accumulation mechanism is disproven. This investigation reproduced and repaired two independent lifecycle defects: publishing pointer reports during the splash, and accepting the remainder of an invalid report after `SYN_DROPPED`. It also repaired the previous change's rejection of keyboard-only devices and made DMA event reads explicitly ordered, bytewise and little endian.

**The cause of the user's continuously unusable relative mouse is not yet established.** No available guest log contains the eight DMA bytes or report state preceding the supplied `dx=-27496 dy=-44561` observation. Correct small events do not become those values in the queue fixture. The lifecycle defects are proven bugs, but are not proof that either generated the user's particular deltas. This is a repair candidate and an instrumented image for manual investigation, not a runtime PASS.

Neither QEMU nor VirtualBox was executed. No `make clean`, `make rm`, device substitution, sensitivity adjustment, divisor, arbitrary delta limit, exclusive owner, or physical PS/2/USB/xHCI change was used. Existing unrelated work, including HDA/Test dźwięku, remains separate.

## Baselines and history

HEAD on entry was `f6541df`; the worktree already contained the previous repairs and extensive unrelated changes. Before editing, input sources, Makefile and the input worktree diff were saved outside the repository under `/tmp/bursztyn-virtio-forensic-20261011-dd1nc5wm/baseline`. Validation logs are under `.virtiotest/forensic-validation/`.

| Revision/change | Relevant semantic difference | Audit conclusion |
| --- | --- | --- |
| `28216b8`, before the two-phase change | Immediate initialization/DRIVER_OK; individual copy/repost/dispatch; per-driver InputDevice already present | Useful source comparison, **not an established last working relative-mouse runtime revision**. |
| `721be50` | PREPARED at status `0x0b`, late DRIVER_OK; per-driver service lock, ownership checks, private batch copies; saturating signed report addition | Does not merge mouse and keyboard queues. Host fixture verifies masked early IRQ, status progression and deferred consumer. |
| `7c7653a` | USB descriptor/PS2 work; extra mouse buttons in common input | No identified VirtIO mouse decoding or DMA change explaining large deltas. |
| `f026e85` | Geometry readiness; pointer mode assigned RELATIVE to every non-tablet, including keyboard | Incorrect keyboard metadata; no path from that keyboard's accumulator to the mouse's accumulator. |
| `f6541df` | Independent proven pointers, ordered pending reports and preferred-source metadata; VirtIO adds contact-capability flag | No exclusive admission remains. A preferred-source change does not transfer movement or change kinds. |
| Uncommitted changes on entry | Keyboard mode guard, kind guard on REL X/Y, DMA `memcpy`, bounded QEMU checkpoints, REL/SYN logging; launchers changed to relative mouse | Audited as untrusted. The keyboard mode guard retained an unconditional pointer-mode rejection, making keyboard registration fail. `memcpy` alone does not establish DMA visibility. |

No supplied revision is identified by manual evidence as the last working **relative** QEMU mouse. Existing `serial_output.log` describes `QEMU Virtio Tablet`, which is not evidence that the current relative configuration worked at that revision. No commit was reverted automatically.

## Previous diagnosis: direct counterexample

The old report claimed that REL values accumulated in a keyboard-only InputDevice, then a SYN or ownership change dumped that sum through the mouse. `input_dispatch_event` receives `&drv->input_dev`; `submit_pointer_report` constructs its report from that same object's fields. Neither function copies accumulator fields between devices. Kinds are determined before activation and are not promoted from keyboard to mouse by liveness or preference changes.

`narzedzia/test_virtio_keyboard_history.py` compiles the actual `f6541df:input_device.cpp`, then the current implementation, under ASan/UBSan. Both receive 1,000 unsupported keyboard reports of `REL_X=-27`, `REL_Y=44`, SYN, followed by a mouse report `3,-2`:

```text
before_Mimo=1 keyboard_pending=-27000,44000 mouse_cursor=643,358 checks=1005 PASS
before_Mimo=0 keyboard_pending=0,0         mouse_cursor=643,358 checks=1005 PASS
```

The historical keyboard accumulator bug existed. Its alleged transfer to the mouse did not. The complete queue fixture independently injects keyboard and mouse DMA/used entries on BDFs 00:07.0 and 00:08.0, verifying separate physical allocations, queues, descriptor metadata, notify mappings, vectors and accumulator state. Keyboard initialization, registration, interrupts and reset do not modify the mouse's report.

The previous unit test also failed to assert keyboard registration success, so an unregistered keyboard silently dropping every event could satisfy it. The test now requires successful registration. The former native-endian `memcpy` test decoder is explicit little endian, and comments no longer claim it proves a runtime root cause.

## Complete path audit

| Stage | Exact implementation/evidence and result |
| --- | --- |
| QEMU generation | `ui/sdl2.c` forwards SDL relative movement; GTK computes relative differences and handles edge recentering; HID emits REL_X/Y with LE conversion. A frontend-provided large relative value is possible and must not be scaled by the guest. Actual frontend values in the failing session are unknown. |
| PCI discovery | `virtio_pci.cpp` stores separate discovered PCI-function records; the input probe allocates one permanent `s_drivers[]` element per function. Actual supplied mouse BDF is 00:08.0; keyboard is 00:07.0. |
| Modern capability mapping | Per-function BAR bounds, common/device/ISR/notify regions, alignment and notify multiplier validation. No singleton current-device mapping. |
| Features | VERSION_1 is required. Unsupported packed/indirect/event-index features are not silently used; the fixture exercises the production split queue. |
| Status progression | Reset confirmation → ACKNOWLEDGE → DRIVER → FEATURES_OK; PREPARED stays `0x0b`; late activation writes/readbacks DRIVER_OK (`0x0f`). Fixture observes writes `0,1,3,11,15`. |
| Queue selection | Eventq is queue 0 on each function. Queue 1 is status feedback and is unused. Common-config queue selection belongs to each PCI function; IRQ never changes it. |
| Size | Queue creation validates the split-ring size; posted count is `min(queue_size,64)`. Existing capacity matrix covers small/large/rejected sizes. |
| Descriptors | Writable event buffers; trusted metadata associates each allocated single-chain head with one DMA buffer index. A nonsequential valid ID is tested. |
| Available publication | Payload initialization and descriptor/avail entries precede release publication of avail.idx. Ownership changes before returning to the consumer. |
| Notification | Per-queue notify offset and per-function multiplier/base; full ordering before kick. No initial notification before DRIVER_OK. |
| Used index | Volatile used.idx is read, then an explicit x86 DMA acquire (`lfence` plus compiler memory clobber) precedes used entries. Difference uses 16-bit modular arithmetic. |
| Used entry | Entire observed batch is checked against posted count, descriptor range/allocation/head identity, unique buffer ownership, length and physical address. Invalid data fails that device closed without freeing live DMA. |
| DMA storage | PMM contiguous RAM → validated HHDM mapping. RAM HHDM leaves use WB flags; MMIO BARs use their separate mappings. On coherent x86, WB RAM is appropriate; ordering is still necessary. No uncached remapping or cache-flush workaround was introduced. |
| Private copy | After generic queue acquire/metadata validation, exactly eight volatile byte loads copy into CPU-owned batch storage. All batch copies precede every repost and notification. Dispatch retains no pointer into DMA. Immediate overwrite of a recycled descriptor is tested. |
| Wire decode | Explicit LE16/LE16/LE32 byte arithmetic; compile-time size 8 and offsets 0/2/4. `input_signed_value` converts the 32-bit bit pattern without out-of-range unsigned-to-signed casts. |
| Classification | Mouse requires advertised REL_X **and** REL_Y. ABS X/Y require valid separate ranges. Buttons alone and absence of ABS cannot manufacture a relative mouse. EVBITS/RELBITS are recorded. Keyboard remains UNSPECIFIED. |
| EV_REL / REL_X / REL_Y | Only pointer kinds with relative mode accumulate; state belongs to that InputDevice. No framebuffer multiplication, coordinate scaling or current-cursor contribution occurs. |
| SYN_REPORT | Completes one admitted report and clears its pending state. Zero-activity reports do not move the cursor. Unsupported SYN codes do not publish. |
| SYN_DROPPED | Transport clears its own incomplete report and ignores the remainder through the next SYN_REPORT. Before the repair it only cleared at the dropped marker, then accepted the invalid remainder. |
| Per-device lifecycle | Noninteractive events are consumed/recycled and discarded. An admission/geometry boundary snapshots a used-index cutoff; already completed entries are discarded in order, including a partial report through its SYN. Reset/queue recreation cannot retain pending motion. |
| InputDevice report | Constructed from the same device; report fields are copied before clear. Existing common pending reports contain complete CPU-owned reports and a registered device identity, not DMA pointers. VirtIO's preinteractive reports do not enter that queue. |
| PROVEN_ACTIVE | Only a valid admitted report proves this pointer. Registering a keyboard or discarding preinteractive movement cannot prove the mouse. |
| Common publication | `publish_report` checks this device's active state, not global preferred ownership; button state combines active sources. Source priority remains metadata. |
| GUI adapter | VirtIO uses a screen-down signed entry that widens before the legacy upward-Y representation is formed. This preserves the valid INT32_MIN wire delta. Physical adapters retain their existing behavior. |
| Signed coordinates | Graphics uses `int64_t(current)+delta`; legacy Y translation cancels the adapter translation exactly once at the coordinate addition. Wire-down movement remains screen-down. |
| Bounds/generation | Only the resulting position is clamped to `[0,width-1]` / `[0,height-1]`. Relative deltas are not multiplied by dimensions or reinterpreted as ABS on generation change. Existing initial cursor position affects addition only. |

Global functional state is the driver registry, late activation gate, desktop readiness flag, InputDevice registry, preferred/last-source metadata, combined buttons and the bounded queue of complete deferred reports. There are no global functional mouse/keyboard DMA buffers, used shadows, free lists, queue selectors or relative accumulators. Shared diagnostic counters do not feed admission or decoding. Config locks, generations read, MSI-X mappings and IRQ contexts are per PCI function/driver.

## Specification basis

[VirtIO 1.2 §5.8.5–5.8.6](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html) defines the event as LE16 type, LE16 code and LE32 value; receive buffers are writable and at least the structure size. The split-ring used length describes bytes written and cannot exceed offered writable capacity. Consequently the consumer accepts `8 <= used.len <= offered capacity`, copying only the required eight bytes. Production offers eight, so a length greater than eight there is malformed; a fixture with genuinely larger offered capacity accepts length 16.

[QEMU 8.2.2 `virtio-input.c`](https://raw.githubusercontent.com/qemu/qemu/v8.2.2/hw/input/virtio-input.c) copies one eight-byte event per queue element. It drops input while inactive before DRIVER_OK, groups events through SYN, and acquires enough available elements for a complete update or drops that update. Thus a conforming PREPARED device does not collect seconds of guest raw REL data. Defensive pre-registration injection is still tested. Installed Linux package metadata is `qemu-system-x86 1:8.2.2+ds-0ubuntu1.18`; the actual Windows version remains unknown.

[Linux evdev SYN semantics](https://docs.kernel.org/input/event-codes.html) require ignoring the invalid remainder through the next SYN_REPORT after SYN_DROPPED. Motion resynchronization belongs to the VirtIO transport here; physical decoder behavior was not changed.

## First deterministic divergences, and what they do not prove

The baseline executable was built from the input worktree **before this investigation's functional edits**, including the previous Mimo repair.

| Injected sequence/state | Before repair | Independent reference / after repair |
| --- | --- | --- |
| Desktop not interactive, first `REL_X=-3`, `REL_Y=2`, SYN | Before SYN: pending `-3,2`; `submit_pointer_report` admitted/published it, moving `(640,360)` to `(637,362)` | No publication; pending `0,0`; cursor stays `(640,360)`. This is the **first startup reference divergence**, at admission into the common report publisher, not decoding. |
| 3,000 complete preinteractive small reports | 3,000 publications; pending ends `0,0`; cursor `(0,719)` | Zero publications; 3,000 complete discarded reports; no queued movement; cursor `(640,360)` |
| 3,000 preinteractive X=-3/Y=2 pairs with missing SYN | Pending `-9000,6000`; later SYN publishes it | Pending `0,0`; partial sequence remains discarded through SYN; first subsequent live report is `3,-2` |
| Same stream with SYN_DROPPED after pair 1,501 | Pending subsequently reaches `-4497,2998` | Pending remains `0,0` |
| Interactive X=4, SYN_DROPPED, twelve X=3, unsupported SYNs, SYN_REPORT | Drop clears `4`; invalid remainder accumulates `36`; final SYN publishes dx=36 | Drop enters resync; remainder is discarded; final SYN ends resync without publication. This is the **first dropped-report divergence**, at the first post-drop REL admission. |
| Exact synthetic wire X=-27496/Y=44561, SYN | Reports those same values | Reports those same values; legacy bounds arguments `-27496,-44561`, cursor `(0,719)`. This is deliberately preserved valid input, not a small-move test. |

Baseline: 3 valid-stream cases / 29,372 checks passed; 4 lifecycle cases / 468 checks had 9 failures. The valid-stream fixture includes 5,000 small reports with 4,857 nonzero publications. Boot drift from many individually correct reports is distinct from one enormous accumulated report. With proper SYN delimiters, the baseline did **not** combine thousands of complete reports into one delta.

The last row proves that the supplied MOUSE BOUNDS values are compatible with already-large raw relative input. It does **not** prove that those bytes occurred in the user's guest. Conversely, absence of corruption in simulated DMA does not prove the real guest sees small raw input.

## Startup boundary and clearing paths

Desktop-root readiness is set immediately after the existing validated desktop-ready publication in `kernel.cpp`. VirtIO pointer admission additionally waits for valid geometry, common delivery readiness and completion of the existing splash. No splash timing or physical-source gating was changed.

The first consumer turn observing an admission or geometry change takes a used-index snapshot. Completions already at that snapshot are stale; later completions are new, except an incomplete stale report is discarded through its SYN. The cutoff persists across scheduler budgets and 16-bit wrap. There is no wire timestamp, so this is explicitly a **consumer-observed boundary**, not a claim to know the physical event time. Events arriving after readiness but before that first service may conservatively be discarded. No timeout or delta threshold controls this decision.

The startup matrix covers stationary boot; small pre-registration input; queue-ready/preactivation injection; 1,000 complete splash reports; continuous boot movement; movement just before and after admission; multiple complete reports already used at admission; missing SYN; and SYN_DROPPED in the backlog. The three 3,000-motion fixtures use simulated time, including the missing/dropped SYN variants. Every post-boundary small report is `3,-2`, producing `(643,358)` from `(640,360)`.

Relative pending state is cleared by: successful registration; common `clear_report` after admitted SYN construction; common inactive-report rejection; VirtIO preinteractive/stale/resync discard; SYN_DROPPED; admission/readiness/width/height/geometry-generation transition; fail-closed handling; explicit device reset before unregister/free; and fresh preparation's zeroed driver state. Unregister forgets queued complete reports; transport reset clears the unpublished accumulator before it. None transfers movement to another device.

The common report API remains signed 32-bit. Its pre-existing representation overflow protection is retained; VirtIO's lower endpoint now includes INT32_MIN exactly. No diagnostic anomaly threshold changes that API or movement. Random reference sequences keep within its representable report sums; full signed wire endpoints are separately tested. This is not an exhaustive proof over all possible streams or hardware timing.

## Diagnostics and parity

`BURSZTYNDIAGVIRTIOINPUTFORENSIC=1` enables only deferred emission. The bounded recorder, readiness/clock observations, counters and functional decisions are identical with flag 0/1. First 128 events **per BDF** retain: observation time ns, BDF/role/lifecycle, queue and used indexes, used slot, descriptor ID and length, exact eight bytes, decoded type/code/signed value, pointer mode/kinds, readiness/proven state, pending X/Y before/after, SYN action/outcome, published deltas and cursor before/after.

Readiness bits are registered=1, geometry=2, interactive=4. Proven state is the InputDevice enum, not an ownership test. Phase counters overlap: before registration, before PROVEN_ACTIVE, before geometry and before desktop. Pre-registration events use the registration-time used-ring watermark, consumed once rather than a numeric comparison that breaks after wrap. Event timestamps mark guest consumption; the wire has no arrival timestamp, so counters describe observed publication/readiness phases, not exact host event times during an MMIO/registration race.

After 128 events, only lifecycle markers and anomaly samples are stored: at most 32 later anomalies and 48 lifecycle markers per BDF. An anomaly means absolute X delta exceeds width or Y exceeds height, including a large accumulated published report. This threshold **only records**; it never scales, clamps, suppresses or changes a valid interactive delta. Emission is at most two lines per service call and does not access device registers.

One `[VINPUT FORENSIC INTERACTIVE]` summary per BDF snapshots event counts and complete reports discarded/queued/published in the four phases after the activation backlog is drained. Empty no-activity reports are counted separately rather than mislabeled as queued. This transport's startup policy queues zero pointer reports. Recorder reset is a test operation; device reset retains lifecycle history for the same BDF.

`narzedzia/test_virtio_input_forensic_parity.py` preprocesses five actual production translation units and requires byte identity after removing only the diagnostic emitter's body. It then compiles/runs both consumer builds with ASan/UBSan and compares ordered model/report records and the report digest. Diagnostic formatting checks separately verify bounds, required fields and exactly one summary.

## Queue fixtures and validation

`narzedzia/test_virtio_input_forensic.cpp` uses real `VirtQueue`, descriptor table, available ring, used ring, descriptor ownership metadata and driver DMA buffers. Its device writer fills bytes first, then used entries, then publishes used.idx; production `virtio_input_poll_events` consumes them. The independent model parses byte arithmetic, owns separate accumulation/resync state and emits one logical report per admitted SYN_REPORT, including zero movement. Cursor-publication comparison projects away zero-motion reports, as the GUI intentionally suppresses them. An independent SYN counter is compared with the driver after every consumer invocation, including discarded reports. The fixture does not use production accumulation helpers in its reference model.

Coverage includes 16-bit ring wrap, descriptor reuse and immediate overwrite, old payload bytes, nonsequential valid IDs, out-of-table/unallocated/duplicate IDs, short/over-capacity lengths, valid larger capacity, corrupt descriptor address, 63/64 queued events, variable scheduler budgets, two live PCI functions, keyboard dispatch, PREPARED/DRIVER_OK/registration boundaries, masked and normal MSI-X delivery, polling/deferred service, reset/reinitialization, geometry loss/change, signed endpoints, 48,000 generated mixed events, and the startup fixtures above. Actual graphics integration tests the production cursor adapter and PS/2 activity after VirtIO activity.

Final host results (all C++ rows compiled with ASan and UBSan):

| Suite | Exact result |
| --- | --- |
| Full new queue/reference consumer, forensic flag 0 | 38 scenarios; 124,704 checks; 0 failures |
| Same consumer, forensic flag 1 | 38 scenarios; 124,704 checks; 0 failures |
| Diagnostic compile/execution parity | Five production translation units identical outside the emitter; ordered functional outputs identical; digest `c1a266193a85fe75` in both builds |
| Diagnostic emission bounds | Flag 0: 0 forensic event/summary lines; flag 1: 160 event lines (128 initial + 32 anomalies), exactly 1 interactive summary; required fields verified |
| Actual historical/current keyboard accumulators | 1,005 checks each; 2,010 combined; both isolate the mouse |
| Existing VirtIO transport suite | 167 named cases PASS; 0 FAIL |
| Explicit event/common input unit | 54 checks PASS; 0 FAIL |
| PS/2/HID/OHCI/xHCI pointing suites | 367,576 checks PASS combined; includes i8042, IRQ, DMA/rings and USB descriptor/report paths |
| Signed pointer regression | 396,600 cursor checks + 56 mode/readiness checks PASS |
| Real graphics + InputDevice + HID + PS/2/VirtIO integration | 66 checks PASS |
| Input coexistence, five diagnostic profiles | 132 checks per build; 660 combined; four parity comparisons PASS |
| Incremental xHCI, five diagnostic profiles | 17,466 checks per build; 87,330 combined; four parity comparisons of 10,822 ordered records PASS |
| Physical xHCI/boot coexistence, five profiles | 27,884 checks per build; 139,420 combined; four parity comparisons of 21,862 ordered records PASS. The 24,033 physical checks reported inside each run are a subset, not additional checks. |
| Static boot ordering | Input bootstrap: 1 PASS; pointer ordering: 6 PASS |
| Windows launcher static policy | 9 tests PASS; no Windows VM run |

ASan: no memory/leak error in the final queue, history, transport, graphics or physical/coexistence runs. UBSan: no reported violation; `halt_on_error=1` was used in queue, transport, history and coexistence runs. Initial failing legacy test assertions left stack-allocated drivers registered, and ASan caught their use in the following test's cleanup; this was not evidence of runtime DMA corruption. Final legacy fixtures explicitly finish initial contact reports and establish the intended modeset boundary, and all 167 cases pass. The original keyboard registration rejection was fixed in production rather than bypassed in tests.

Logs: `.virtiotest/forensic-validation/{parity-run.log,parity-0.log,parity-1.log,diagnostic-parity.txt,previous-claim-history.log,after-existing.log,final-events.log,physical-input.log,graphics-coexistence.log,coexistence-input-boot.log,launcher-static.log}`. `graphics-coexistence.log` contains the successful incremental runs followed by the obsolete narrow test-stub failure; the corrected full input/boot results are in `coexistence-input-boot.log`. No failing result is counted as a final PASS.

The second audit rechecked register/activate/reset ownership, all clear/admission paths, queue identity/length/wrap, private copies before recycle, per-device mappings and vector contexts, keyboard isolation, source coexistence and the actual graphics adapter after the fixes. No further small-event corruption was found. Production compiler stack-usage output reports a static 1,824-byte queue-consumer frame; diagnostic dumping is deferred outside that frame. This is code/test evidence, not a hardware cache-coherency measurement.

## Launchers and unresolved runtime questions

Canonical Linux and Windows launchers retain `virtio-mouse-pci`, `virtio-keyboard-pci`, `tcg,thread=single`, one vCPU and 1280×720. No launcher change was made in this investigation. Linux argument printing and Windows static policy tests are read-only. PowerShell executable parity could not run in this session because `pwsh` is unavailable; this is not a Windows runtime PASS.

[QEMU `ui/input.c`](https://raw.githubusercontent.com/qemu/qemu/v8.2.2/ui/input.c) selects one matching handler for each input event; active VirtIO HID is moved ahead of ordinary handlers. Source review does not establish duplicate VirtIO/PS2 host injection. Both guest sources remain registered. No redundant PS/2 suppression was justified or introduced.

[SDL2 pointer handling](https://raw.githubusercontent.com/qemu/qemu/v8.2.2/ui/sdl2.c) and [GTK pointer handling](https://raw.githubusercontent.com/qemu/qemu/v8.2.2/ui/gtk.c) differ in relative-motion/capture generation. They do not establish which raw values the two failing hosts generated. Linux and Windows share the guest decoding path and reported symptom; identical guest event streams have **not** been measured.

Manual trace interpretation: correct small bytes but wrong `signed_value` locates decoding corruption; correct values with wrong pending state locates report accumulation; correct pending/report with wrong published dx/dy locates publication; correct published values with wrong cursor result locates graphics. Already-large bytes require correlating lifecycle and frontend capture. Large values only before interaction should be discarded by the startup barrier; continuous large interactive values remain valid deltas and must be investigated at the host generation layer.

Manual acceptance remains outstanding for Linux, Windows, keyboard and both Acer pointer devices. Test fixtures are not runtime acceptance. A serial capture from the forensic ISO, including the summary and the first anomalous event/report, is still needed to establish the first corruption point of the reported regression.

For manual capture only (these commands were not executed here):

```sh
make runuefi RUNUEFI_ISO=BursztynOS-virtio-input-forensic-diag.iso 2>&1 | tee virtio-linux-forensic.log
```

```powershell
.\runwindows.ps1 -Iso .\BursztynOS-virtio-input-forensic-diag.iso 2>&1 | Tee-Object virtio-windows-forensic.log
```

Test once with a stationary mouse during boot, then with movement during the splash followed by small desktop movements. Retain the `[VINPUT FORENSIC EVENT]`, `[VINPUT FORENSIC LIFE]`, `[VINPUT FORENSIC INTERACTIVE]` and MOUSE BOUNDS lines. This permits testing the same relative device and guest image on both hosts without a configuration workaround.

## Files changed by this investigation

Production/build: `virtio_input.cpp`, `virtio_input.h`, new `virtio_input_forensic.h`, `input_device.cpp`, `input_device.h`, `grafika.cpp`, `grafika.h`, `kernel.cpp`, `Makefile`. Graphics changes are limited to preserving the valid signed VirtIO endpoint through the cursor entry; kernel change is the direct desktop-input readiness dependency. No decoder/controller, desktop layout, filesystem, installer, network, HDA or Test dźwięku source was edited here.

Tests: new `narzedzia/test_virtio_input_forensic.cpp`, `narzedzia/test_virtio_input_forensic_parity.py`, `narzedzia/test_virtio_keyboard_isolation.cpp`, `narzedzia/test_virtio_keyboard_history.py`; modified `narzedzia/mock_virtio_input_init.inc`, `narzedzia/test_virtio_input_init.inc`, `narzedzia/test_virtio_input_activation.inc`, `narzedzia/test_virtio_input_lifecycle.inc`, `narzedzia/test_virtio_pci.cpp`, `narzedzia/test_virtio_input_events.cpp`, `narzedzia/pointing_host_stubs.h`, `narzedzia/test_abs_map_diag.cpp`, `narzedzia/test_input_coexistence.cpp`, `narzedzia/test_input_diagnostic_parity.py`, `narzedzia/test_pointer_graphics.cpp`, `narzedzia/test_splash_production_path.cpp`. Existing tablet fixtures now complete their contact report and establish modeset admission before injecting fresh coordinates; fresh postactivation reports are explicitly checked rather than accepting stale cold reports.

Documentation: this report. Generated binaries/dependency files and ISO staging are build artifacts; the pre-existing worktree changes listed above remain independent.

## Final build artifacts

Both builds completed successfully, without C/C++ compiler errors or warnings. The wallpaper pack generator retains its existing warning about skipping `Zamek w Pszczynie 2026r..jpg`; the host, staged and ISO pack hashes match. Multiboot2 checks passed. ISO extraction verified each embedded kernel against its built artifact, and verified the forensic emission marker is present only in the diagnostic kernel.

Commands executed, sequentially:

```sh
make -B -j2 all iso BURSZTYNDIAGVIRTIOINPUTFORENSIC=1 ISO=BursztynOS-virtio-input-forensic-diag.iso ISO_DIR=isodir_virtio_forensic
make -B -j2 all iso
```

The final ordinary worktree kernel and `BursztynOS.iso` are production builds, with the forensic emission flag off. Only the separate diagnostic ISO emits the forensic stream. No VM was run after either build.

```text
13f5293efafc39479ab4b76deb745708f8a32fc87d7d9165aec6bf7b576add42  BursztynOS.iso
146a33c2458e81576acff2fd25c64ccd5376fb6f5555bcde26b6ccc1b57e377c  BursztynOS-virtio-input-forensic-diag.iso
```

Checksums are also in `docs/SHA256SUMS-virtio-input-forensic-2026-10-11`. Build logs are `.virtiotest/forensic-validation/build-production.log` and `build-diagnostic.log`. Embedded kernel SHA256: production `1d58307ea53384b11b81783368946650e5d8649b1f1279ab234574dd824e3c93`; diagnostic `881284e5c24c21c182b8fb56e15a9f161689856331bfa48553f8f18618795756`.

**Acceptance status: pending manual Linux/Windows/Acer testing; the first corruption point of the user's observed runtime regression remains unresolved until the guest byte/report trace is available.**
