# Cursor pipeline audit and host regressions, 2026-10-04

Runtime status: **not visually validated**. No QEMU/SDL session was launched because
nested QEMU previously froze the development VM. These changes eliminate proven
intermediate presentations in the code; they do not establish a runtime flicker or
stutter PASS. Exact attribution of the reported stutter and upstream input latency
remains pending a manual diagnostic run.

## End-to-end path and ownership

1. `virtio_input_msix_handler` increments `irq_count` and sets
   `has_pending_events`; it does not render or dispatch pointer positions.
2. PID 0's kernel idle loop calls `virtio_input_obsluz`, then the screen-locked
   `skladacz_obrazu_obsluz_dirty`, then `sti; hlt`. Each driver's
   `virtio_input_poll_events` consumes at most 64 raw events, copies the used-ring
   event, replenishes/kicks its descriptor, then calls `input_dispatch_event`.
3. `input_dispatch_event` stores ABS_X/ABS_Y as `abs_val_x/y` with
   `has_abs_x/y`. At SYN_REPORT it checks pointer ownership, scales/clamps the
   absolute axes using device min/max and current framebuffer dimensions, and
   converts target positions to deltas for `zaktualizuj_mysze_zrodlo`.
4. `zaktualizuj_mysze_zrodlo` commits `mysz_x/y`, performs hit testing, capture,
   focus and event routing, and enqueues pointer events through
   `scheduler_dodaj_zdarzenie`. Existing normal routing is unchanged. It calls
   `skladacz_obrazu_oznacz_ruch_kursora` afterwards. The timestamp labeled
   `wm_enqueued` measures completion of kernel routing/enqueue, not Ring 3
   event consumption.
5. `skladacz_obrazu_oznacz_ruch_kursora` validates coordinates, updates
   `dirty_generation`, and publishes `cursor_new_x/y` and `cursor_pending`
   under existing `irq_off/irq_restore`. These names alias the tested
   `KursorLatest` mailbox. There is one bounded latest position, not an event
   queue. Overwrites intentionally coalesce intermediate positions. The old
   rectangle comes from the last rendered overlay, never from a dropped input
   sample's old coordinates.
6. `skladacz_obrazu_obsluz_dirty` takes pending position and scene dirty state
   under IRQ exclusion. Scene frames have `ODSTEP_KLATKI_NS = 16666667`
   (approximately 60 Hz). Cursor-only work bypasses this deadline, including
   when a paced scene frame is waiting. It still waits for PID 0 to execute;
   synchronous composition/presentation cannot be preempted by another cursor
   presentation. LAPIC scheduler target is 250 Hz; the actual configured value
   is logged at boot by the existing timer calibration. No runtime cadence was
   measured in this task. HPET and scheduler were not changed.
7. Scene dirty work is composed completely into the clean scene backbuffer.
   `zloz_snapshot_region` composites app layers in z-order;
   `zloz_dekoracje` renders current-theme WM chrome when the clip touches
   decoration strips. Application and system popup overlays retain their
   existing ordering.
8. `grafika_prezentuj_scene_z_kursorem` snapshots cursor shape via
   `aktywny_stan_kursora`, computes hotspot/size through `parametry_kursora`,
   and uses `kursor_prezentuj_transakcje`. Its `old` rectangle comes from
   `kursor_overlay_x/y/w/h` and `kursor_overlay_widoczny`.
9. The old background already exists in the clean scene backbuffer. The
   transaction clips old/new rectangles, forms their union, and transitively
   absorbs scene dirty rectangles intersecting this union. It captures the
   freshly composed new background into `kursor_tlo_kopia[64*64]`, rasterizes
   through `piksel_kursora`, and presents the final union once. Unrelated scene
   dirty rectangles keep their separate small presents. No intermediate
   cursor-free union is presented.
10. `PrzeniesFragmentNaEkran` calls the active screen driver's
    `KopiujFragmentNaEkran`. For VirtIO-GPU, `SterownikVirtioGPU` copies into
    the device framebuffer and calls `virtio_gpu_transfer_and_flush`, which
    synchronously issues `virtio_gpu_cmd_transfer_to_host_2d` and
    `virtio_gpu_cmd_resource_flush`. The GPU driver and its synchronization
    were not changed.
11. After those calls return, the transaction restores the temporary background
    into the scene backbuffer and updates rendered overlay geometry. The
    renderer no longer writes a consumed position back into logical `mysz_x/y`.
    Paired logical position reads use brief IRQ exclusion. The framebuffer
    retains the final cursor; the scene backbuffer remains cursor-free.

The mailbox uses existing single-CPU IRQ exclusion; rendering and scratch-buffer
ownership use existing screen serialization/scene guard. No heavy input lock or
new SMP ownership claim was added. Retry after a failed scene guard restores a
consumed sample only when no newer sample is pending.

## Proven defects and limitations of attribution

Before the change, `grafika_prezentuj_kursor_w` presented the clean old rectangle
before drawing/presenting the new cursor. A visible move generated **two transfer
and flush pairs**, including a cursor-free intermediate state. Normal dirty
processing also presented scene dirty rectangles before overlaying the cursor;
when a dirty rectangle covered the pointer, this exposed another cursor-free
state. The full-frame completion path also presented a clean full screen before
presenting the cursor. This is a concrete flicker mechanism.

After the change, an ordinary consumed visible cursor move generates **one
transfer/flush pair**, confirmed by a mocked present counter using the production
transaction. A full-scene redraw with cursor generates one final full-screen
present. If independent scene dirty rectangles also exist, those generate their
own presents; the cursor transaction still has one final present. The existing
in-kernel cursor test now expects exactly two flushes for two movements, rather
than accepting more than two.

There was no duplicate rasterization per ordinary consumed cursor frame. The
problem was intermediate erasure/presentation. A rendered snapshot could also
write `mysz_x/y` back after a newer input update, a latent position rollback; this
write has been removed. A failed compositor-guard retry could similarly replace a
newer pending sample, and now preserves it. Neither latent case is claimed as a
measured cause of the normal VirtIO Tablet runtime stutter.

`kursor_tlo_kopia` was and remains a temporary capture/restore buffer. It does not
survive as an old-position scene cache. Underlying scene/theme/chrome changes are
read from the freshly composed backbuffer on each transaction. No stale retained
cursor-background theme cache was found. Host tests confirm this invariant.

Recent WM decorations increase scene composition work when decoration strips
actually become dirty. They do not invalidate windows on every pointer movement
and have no per-move titlebar-button hover redraw path. They may make the existing
intermediate scene presentation more noticeable; causal runtime attribution is
not established. No decoration implementation or launch routing was changed.

The exact stutter source is **unresolved**: two synchronous GPU round trips were
unnecessary work, but no real input, compositor, or GPU latency measurement was
obtained. A union spanning a large pointer jump can transfer more pixels than two
small rectangles. Diagnostic `present_area` makes that tradeoff measurable; no
claim that union batching alone cures stutter is made.

## Hover audit and measurements

- WM titlebar controls: no hover-triggered decoration invalidation in the audited
  routing/rendering code. Focus/lifecycle/theme changes invalidate existing strips.
- Start button: `menu_przycisk_hover` changes only on enter/leave. It redraws
  `RysujPrzyciskMenu` and calls `gui_odswiez`. Kernel `bws_gui_odswiez` publishes
  only `oczekujacy_dirty`, the accumulated drawing region, not inherently the
  full desktop. Start menu open/close is a different scene update.
- `WidokMenuPopup` invalidates old/new rows only when the active item changes;
  submenu changes likewise require a different state. Its popup manager and
  Shell behavior were not modified.
- `PasekPrzewijania::ustaw_czesc_najechana` returns immediately for the same
  scrollbar part. Dragging continues to update its model and affected geometry.
- `Kontrolka` changes visual hover on enter/leave; `Okno` updates hover identity
  only when its hit target changes.

`test_cursor_hover` uses existing UI mocks plus a counting parent and executes the
production popup, scrollbar and button handlers. At both 100% and 125% DPI,
**100 moves within the same item/part/control generate zero invalidations**.
Initial transitions measured:

| Component | Scale | Invalidation calls | Summed dirty pixel area |
|---|---:|---:|---:|
| Popup row | 100% | 1 | 4752 |
| Popup row | 125% | 1 | 5910 |
| Scrollbar part | 100% | 1 | 1782 |
| Scrollbar part | 125% | 1 | 1782 |
| Generic button | 100% | 1 | 3000 |
| Generic button | 125% | 1 | 3000 |

Scrollbar/button test geometry is fixed in physical pixels in this table. These
are host invalidation measurements, not runtime composition/GPU duration data.
Start and titlebar conclusions are code audits, not runtime hover measurements.

## Default-off diagnostics

`BURSZTYNDIAGCURSORPERF` defaults to 0. Enabled diagnostics log only consumed
samples taking at least 25 ms from driver dequeue to final present completion,
with at most one log per second. There is no normal per-packet serial logging.
The record contains nanosecond timestamps for input receipt, logical commit,
WM enqueue completion, cursor queue, compositor begin, old-background-ready,
rasterization complete, present begin/end. It includes pending scene rectangle
count/area, full-screen-pending and actual-composing flags, decoration rendering,
mailbox overwrite, cumulative coalesced/rejected samples, actual present count
and pixel area. Coalesced samples are intentional superseded positions, not
lost background state. Rejected coordinates are counted separately.

The input timestamp is the software driver's receipt/dequeue of pointer events,
not a host/device generation timestamp. Thus the trace distinguishes time spent
after dequeue but cannot prove absence of upstream input delivery delay.
`wm_enqueued` also does not measure when an app consumes its event.

To force compilation of the three diagnostic-bearing translation units before a
manual diagnostic ISO build (make does not track flag changes automatically):

```
make -B -j2 BURSZTYNDIAGCURSORPERF=1 grafika.o skladacz_obrazu.o virtio_input.o
make -j2 BURSZTYNDIAGCURSORPERF=1 all iso
```

Restore default builds by forcing those same three objects with the option set
to 0, then rebuilding `all iso`. No runtime launch is part of these commands.

## Validation

- Affected default component build: PASS.
- Diagnostic-enabled affected compilation: PASS (temporary /tmp objects).
- Production cursor transaction/mailbox host tests, ASan/UBSan: PASS.
- Enabled anomaly diagnostics with a mock clock: PASS (fast frames silent,
  anomaly timestamps/metrics/coalescing present).
- Hover measurement host tests, ASan/UBSan: PASS.
- Existing WM click-routing tests, ASan/UBSan: PASS.
- Existing WM decoration renderer tests, ASan/UBSan: PASS.
- Existing theme tests: 155/155 PASS.
- Existing UI controls tests, ASan/UBSan: 1379/1379 PASS.
- Existing command-binding tests, ASan/UBSan: 23/23 PASS.
- Sanitizer harnesses use `ASAN_OPTIONS=detect_leaks=0`, as existing UI harnesses.
- `make -j2 all iso`: PASS, exit 0; `BursztynOS.iso` created.
- `git diff --check`: PASS.
- Final visual runtime validation: pending manual user testing; no runtime PASS.

Cursor regression cases cover center-to-right, center-to-bottom, all four screen
edges and off-screen clipping, 100 positions coalescing to one final update,
producer activity after consumption and before retry, old/new dirty union,
transitive scene-region absorption, full-scene/decor/theme updates, changes
under a stationary cursor, exact clean-scene restoration, no stale old pixels,
and one final present per cursor transaction. Unrelated scene work is tested to
remain a separate present. Host tests cannot establish real screen scanout
atomicity or prove visually smooth cursor motion.

Changed source/report files: `Makefile`, `grafika.cpp`, `grafika.h`,
`skladacz_obrazu.cpp`, `virtio_input.cpp` (default-off receipt timestamp only),
`kursor_prezentacja.h`, `kursor_diag.h`, `narzedzia/test_cursor_host.cpp`,
`narzedzia/test_cursor_hover.cpp`, and this report. Shell, context-menu
implementation, app launch routing, SDL/tablet configuration, GPU driver, theme
implementation, reboot, scheduler, HPET, BSP2, GRUB, installer and BUR ABI sources
were not modified. Build outputs, including the ISO, remain in the worktree.
