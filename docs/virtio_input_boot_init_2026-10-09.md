# VirtIO input boot initialization audit — 2026-10-09

Historical audit. The subsequent installer log identifies the DRIVER_OK boundary; see [the activation investigation and current artifacts](virtio_input_driverok_activation_2026-10-10.md). Results and hashes below describe the earlier build.

## Runtime status and scope

The supplied runtime log places the failure after keyboard initialization and before tablet InputDevice registration. It does **not** identify which tablet initialization operation failed to return. The real freeze has not been reproduced in a host test, and its root cause is not established. This change provides initialization checkpoints and fixes independently demonstrated validation/state-publication defects. It must not be described as a proven fix for the reported QEMU freeze.

Work continued from the existing worktree. The previous completion-polling rewrite remains present; this pass does not change its batching algorithm. No QEMU or nested virtualization was launched. Neither launcher nor the disk image was changed.

## Manual diagnostic artifact

`BursztynOS-input-init-diag.iso` enables `BURSZTYNDIAGVIRTIOINPUTINIT=1`; the normal `BursztynOS.iso` is built with that option disabled. Reproduce with the exact existing keyboard/tablet modern-only launcher configuration. Preserve the last `[VIRTIO-INPUT-INIT] bdf=00:07.0 ...` lines, including the final begin checkpoint and its preceding done checkpoint.

To rebuild the diagnostic image explicitly:

```sh
make -B -j2 all iso BURSZTYNDIAGVIRTIOINPUTINIT=1 ISO=BursztynOS-input-init-diag.iso
```

Do not launch QEMU automatically afterward. The default ISO build command is `make -B -j2 all iso`.

Checkpoints have a per-function sequence number, operation name, and hexadecimal value. They are capped at 256 per initialization. A begin checkpoint performs no extra MMIO reads: it is printed before the access being diagnosed. The log includes transport/config pointers and length, reset, status stores and readbacks, feature banks and negotiated mask, selectors/subselectors/size/payload completion, event bitmap result, ABS ranges, role/kinds, queue count/select/size/enable, queue allocation and physical addresses, notify offset/address, posted count and avail/used indices, IDT vector/context registration, APIC destination, MSI-X table programming/queue assignment/config vector readback, DRIVER_OK, registry publication, unmask, and initial notify. It emits no runtime input-event stream.

A final `status store begin value=...000f` without its matching `done` identifies the synchronous DRIVER_OK MMIO store, rather than a polling loop. QEMU's HID activation occurs on that transition. This is a candidate to distinguish in the manual log, **not** an established cause.

## Defects established in source/tests

1. The shared 16/32/64-bit config accessors checked `offset > device_cfg_len - width`. A capability shorter than the width underflows that subtraction and can permit an out-of-capability access. The check now validates the offset first, then compares the width with the remaining length. Sentinel/MMIO-access tests reproduce the boundary condition. **The input initialization path reads bytewise, so this defect is not evidence of the tablet freeze's cause.**
2. Driver readiness was published before InputDevice registration. There was no explicit initialization state, and the IRQ handler discarded pre-ready indications. Readiness is now published only after registration; the IRQ only retains pending state/count and never dereferences a queue or dispatches. Host tests inject IRQs and completions at initialization boundaries. The former handler already did no compositor work; this change does not prove an earlier deadlock.
3. Tablet classification accepted missing/truncated/invalid ABSINFO while retaining a tablet role with unspecified axis ranges. Initialization now fails on invalid X/Y ranges instead of registering that tablet. Devices with no recognized capabilities are also rejected.

4. If MSI-X queue-vector assignment failed before `pci_msix_enable`, polling fallback could retain enabled legacy INTx even though this driver has no INTx handler. An immediate report could then raise an unacknowledged level interrupt. Polling fallback now explicitly disables INTx with PCI Command readback (without echoing W1C Status bits), and disables MSI if present before DRIVER_OK. The rejected-vector test checks the interrupt-disable bit. This is a source-level fallback defect; the supplied log does not show whether the tablet entered fallback, so it is not claimed as the runtime root cause.

## Initialization sequence

Before this pass: reset; combined ACKNOWLEDGE/DRIVER write; VERSION_1 negotiation/FEATURES_OK readback; name/serial/IDs and EV/ABS queries; create/enable eventq; allocate/post buffers; register handler and program masked MSI-X; DRIVER_OK; publish `initialized`; InputDevice registration; unmask and initial notify.

Now: allocate permanent per-device state; RESETTING; reset with confirmation; separate ACKNOWLEDGE and DRIVER transitions; NEGOTIATING; negotiate VERSION_1 and verify FEATURES_OK; checked bytewise config queries and validated ABS X/Y; QUEUE_SETUP; validate queue count; create/enable queue 0; allocate/post event buffers; BUFFERS_POSTED; register permanent handler context; program masked entry 0 on this PCI function and check queue-vector readback; explicitly disable config MSI-X with NO_VECTOR and validate readback; INTERRUPTS_READY; DRIVER_OK write/readback; DRIVER_READY; register InputDevice; REGISTERED; publish normal service readiness; unmask entry and notify eventq.

Queue enable precedes posting, as allowed for queue setup, but DRIVER_OK and initial notification follow fully posted buffers. An early IRQ only records pending state. Normal service requires completed registry publication. An immediate ABS_X/ABS_Y/SYN completion remains in the used ring until service after initialization.

`statusq` (queue 1) remains unused because the driver sends no LED/status feedback. Only eventq receives writable event descriptors. Initialization validates that the device exposes the two input queues. No status-queue descriptor or notification is issued.

## Per-function transport/state audit

`virtio_input.cpp` owns a static array of separate driver instances, not a singleton current driver. Each owns its transport pointer, queue, DMA event allocation, ownership map, pending flags, vector, config lock, axis state, init state, and failure-stage number. `virtio_pci.cpp` owns an array of PCI-function records; common/notify/ISR/device config pointers, BAR metadata, features and MSI-X table pointer belong to each record. The new init checkpoint counter/active flag also belong to that record. The split queue owns its transport and queue number, DMA allocation and descriptor metadata. No mutable current-BDF/commoncfg/notifybase/selector singleton was found.

BAR capability regions are checked against each function's BAR size during probing. Notification calculation multiplies the per-function queue notify offset by its per-function multiplier in 64 bits, checks capability bounds, pointer overflow and alignment. Queue helpers explicitly select queue 0 before MSI-X assignment; queue creation selects its queue before reading/programming registers. No other input initialization helper changes that function's queue selection. Both functions may use queue 0 and MSI-X entry 0 because their transports/table bases are different.

The two-function test uses BDFs 00:06.0 and 00:07.0, independent common/notify/ISR/device config storage, independent PCI configuration and distinct MSI-X table bases. Keyboard memory is snapshotted after initialization; tablet initialization and tablet reset must leave it byte-for-byte unchanged. Guard bytes surround every modeled capability. Queue and event physical DMA addresses must be distinct and resolve to the correct owned host allocation.

The host pair allocates distinct APIC vectors; the test checks that they differ. Production vector numbers depend on earlier device allocations; no runtime vector or queue-size reading was obtained. The IDT allocator scans vectors 48..254 with local interrupts disabled; registration refuses an occupied handler. Context is the permanent driver object, never a local stack object. EOI remains in the existing common IDT path. Other device vector assignments were not changed.

## Wait/lock inventory and limits

- Device reset already had, and retains, a maximum of 10,000 status reads. No sleep, arbitrary wall-clock timeout, feature wait, queue-ready wait, completion wait or interrupt-driven initialization wait was added.
- Input config and service ownership are nonblocking atomic try-acquisitions. Contention returns failure/zero instead of waiting on an interrupted owner.
- Config payload copying is at most 128 bytes; per-query capability length is checked before payload access. Layout is packed with the payload at offset 8; ABSINFO is 20 bytes and input events are 8 bytes. Input uses volatile byte MMIO, not memcpy from device memory.
- Queue creation rejects zero, non-power-of-two and over-limit sizes. Posting is `min(queue size, 64)` single WRITE descriptors of 8 bytes. Tests cover sizes 0, 1, 2, 8, 32, 64 and 128. Queue allocations are page-aligned and use physical guest addresses from the DMA allocator.
- IDT allocation/registration has bounded array work and restores prior IF. InputDevice registration/owner selection scans 16 slots, has no wait lock and makes no GPU/compositor call.
- MSI-X table programming and readback are finite MMIO operations. A queue-vector readback of 0xffff is rejected as MSI-X success; existing bounded polling fallback remains available with INTx/MSI suppressed. The initialization does not wait for an interrupt.
- The general heap, PMM and PCI CF8/CFC layers still contain blocking spinlocks. They disable local interrupts before acquisition; the input IRQ never acquires these locks, so no input-initialization/IRQ lock cycle was found. These shared locks were **not** converted to bounded/nonblocking APIs in this pass. The broad requirement that every lower-layer wait be bounded is therefore not fully satisfied. Allocation and PCI MSI-X checkpoints identify their call sites for the manual trace.
- A synchronous guest MMIO access which stalls inside the emulator cannot be interrupted by a bound in a later guest loop. The diagnostics deliberately print before that access. Host watchdogs measure modeled MMIO access counts; they do not establish liveness of the real emulator, kernel allocator or global PCI lock.

## Failure behavior

A genuine rejection records FAILED and the last initialization stage, emits a concise BDF/stage/reason message, prevents normal service, masks MSI-X and unregisters its handler, then attempts the bounded reset. Queue/event DMA is released only after acknowledged reset; reset refusal quarantines it. Failed devices never become registry pointer owners. Probing continues with remaining devices, including the keyboard and existing PS/2 fallback. There is no initialization retry loop or kernel panic added.

## Specification and QEMU cross-check

The input spec defines no input-specific feature bits. `0x10130000000` offers bits 28 (INDIRECT_DESC), 29 (EVENT_IDX), 32 (VERSION_1), and 40 (RING_RESET); `0x100000000` accepts only VERSION_1. ACCESS_PLATFORM (bit 33) is not offered in that mask. Existing physical guest DMA addressing is retained. FEATURES_OK must remain set and is checked. Modern-only transport is already used by both input functions; no evidence supports removing disable-legacy.

The specification permits setting select/subsel in either order and requires checking the size before payload access. Unsupported combinations return zero size. QEMU's source calculates input config length from the largest supported payload plus the eight-byte header, supplies separate tablet ABSINFO queries, and creates two 64-entry input queues. This is source-model evidence, not a measurement of the user's QEMU build. Config notifications are suppressed before DRIVER_OK. DRIVER_OK activates the HID handler, including absolute-pointer mode for the tablet. Leaving statusq without feedback descriptors causes no completion wait in the guest.

Sources:

- [VirtIO 1.2, sections 3.1 and 5.8](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)
- [QEMU input configuration/activation and queues](https://github.com/qemu/qemu/blob/master/hw/input/virtio-input.c)
- [QEMU HID keyboard/tablet configuration and activation](https://github.com/qemu/qemu/blob/master/hw/input/virtio-input-hid.c)
- [QEMU config-notification readiness gate](https://github.com/qemu/qemu/blob/master/hw/virtio/virtio.c)

## Files and verification

Production/build files changed in this pass: `virtio_input.cpp`, `virtio_input.h`, `virtio_pci.cpp`, `virtio_pci.h`, `virtqueue.cpp`, and `Makefile`. Shared transport accessor bounds changed; shared virtqueue changes are initialization checkpoints. No BLK/NET/RNG/GPU protocol or timeout was changed. Existing modifications in IDT/InputDevice/syscalls and other queue hardening from the previous session are preserved. The forced build also refreshes the tracked generated `syscalls.d` dependency file and the host-test executable; generated build outputs are retained.

Tests: `narzedzia/test_virtio_pci.cpp` gains routing into the independent model and registers new tests; new files `narzedzia/mock_virtio_input_init.inc` and `narzedzia/test_virtio_input_init.inc` hold the model and tests. The prior `test_virtio_input_faults.inc` remains unchanged.

The new tests cover two-function isolation, status/reset sequencing, guard bytes, precise capability-length boundaries, wide-access underflow, X/Y valid/missing/truncated/equal/reversed/signed-extreme ranges, queue capacities, writable physical DMA buffers, readiness-boundary IRQs, early reports, FEATURES_OK rejection, queue/config setup failures, queue MSI-X NO_VECTOR, registry saturation and reset-refusal DMA quarantine. Interleavings are deterministic callback injections, not simultaneous host threads. Existing host tests also cover PCI capability parsing, MSI-X table bounds, virtqueue wrap/corruption, InputDevice and all shared VirtIO users. Host tests do not emulate kernel locks or QEMU's UI activation.

Final verification: **157/157 host tests passed** with ASan/UBSan and leak detection enabled. No sanitizer findings were emitted. The eight new initialization test groups passed, including the final polling-mode INTx suppression check. `make -B -j2 all iso` completed successfully after the last production change, with diagnostics disabled. The separate diagnostic ISO was rebuilt with the final source and diagnostic option enabled. Extracted ISO kernels confirm the diagnostic prefix is present only in the diagnostic image. `git diff --check` passes. The wallpaper packer retains its existing warning about the unsafe filename `Zamek w Pszczynie 2026r..jpg`.

TSan was not run. QEMU was not launched. Runtime acceptance and identification of the actual freeze stage remain pending the user's diagnostic boot log.

Artifact hashes (SHA-256):

- `BursztynOS.iso` — 35272704 bytes — `a086649d1c73811e151f5debc1d0b0126ab39ca2475f1454a6150ca969eda89d`
- `BursztynOS-input-init-diag.iso` — 35276800 bytes — `21cd439a589364e7fdaf5db3ff065e86e653e583ca5940504606f8a2f2021cdf`
