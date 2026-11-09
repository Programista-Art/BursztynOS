# Production splash publication audit — 2026-10-08

This continues the current worktree, including the previous first-frame patch. The previous host PASS did not establish guest correctness. No QEMU guest or reboot loop was started. Linux and Windows manual acceptance is still required.

## Newly identified publication paths

The most significant omitted path does **not issue a GPU rendering command**: `virtio_pci_probe()` → `virtio_pci_reset()` → `virtio_pci_set_status(device_status=0)`, followed by another zero status store in `virtio_gpu_init()`. QEMU 8.2.2 routes a zero status write through device reset even if the previous status was already zero; `virtio_gpu_reset_bh()` replaces the shared console display surface with NULL. In virtio-vga this console also owns the firmware VGA image. `firstframeready` says that CPU splash pixels exist, and cannot prevent this hardware side effect. The previous mock reset only queue state, so previous command-order tests could not see it.

Primary source: [QEMU 8.2.2 VirtIO PCI status handling](https://github.com/qemu/qemu/blob/v8.2.2/hw/virtio/virtio-pci.c), [GPU reset and scanout](https://github.com/qemu/qemu/blob/v8.2.2/hw/display/virtio-gpu.c), [VGA/GPU console sharing](https://github.com/qemu/qemu/blob/v8.2.2/hw/display/virtio-vga.c). This is source-confirmed behavior and a host replay, not a capture of the user's guest.

Two additional unsafe boundaries remained:

- `powitanie_inicjalizuj()` → `PrzeniesNaEkran()` → GOP/VESA `KopiujCalyBufor32()` wrote the live firmware framebuffer before the final graphics backend was selected. Although its source was complete, a native copy is a sequence of visible memory writes. The GPU boot path now performs **zero** such writes, preserving firmware while it prepares the GPU.
- `ekran_inicjalizuj()` → `zastosuj(configrestore)` → `grafika_zmien_sprzet()` → `virtio_gpu_setup_framebuffer()` exposed a newly created host resource through `SET_SCANOUT` after an earlier valid GPU frame. The previous deferral only covered the initial resource and unpublished replacements. A startup configuration restore after initial publication escaped it.

The replay uses the saved pre-task GPU source and the pre-task PCI source with the current hardware fixture. It decodes real queued commands and backing scatter/gather into separate host resource images. Its exact old result is:

```text
Startup replacement: visible_before=4 visible_after=5
command=0 resource=0 dimensions=0x0 full_transfer=0 all_zero=1 hash=0
command=0 resource=0 dimensions=0x0 full_transfer=0 all_zero=1 hash=0
command=103 resource=1 dimensions=1280x720 full_transfer=1 all_zero=0 hash=15339559154842318014
command=104 resource=1 dimensions=1280x720 full_transfer=1 all_zero=0 hash=15339559154842318014
command=103 resource=2 dimensions=1024x768 full_transfer=0 all_zero=1 hash=4602708827301880613
```

`command=0` denotes a status-reset surface replacement in the hardware model; 0x103 is SET_SCANOUT and 0x104 is RESOURCE_FLUSH. The replacement resource is a zeroed **host** image, irrespective of the contents of its newly allocated CPU backing.

## Complete old and new startup orders

Old current-worktree order, with a cold GPU and a saved startup resolution:

1. Firmware/GRUB image; graphics maps GOP/VESA and clears only its off-screen backbuffer. The previous explicit initializer background publication was already removed.
2. Splash initialization builds a surface and publishes it through the native HAL. Its animation clock already runs while allocation and subsequent device probing take place.
3. PCI probe writes status zero: QEMU replaces the console with an empty surface.
4. GPU initialization writes status zero again: another console replacement.
5. Create/attach resource 1, upload full frame, SET_SCANOUT, RESOURCE_FLUSH.
6. `PokazKursor()` performs another splash-selected partial presentation and RESOURCE_FLUSH.
7. Startup saved-mode restore creates/attaches resource 2 and immediately SET_SCANOUTs its empty host image. The subsequent compositor redraw/transfer/flush finally fills it.

New cold-GPU order:

1. Keep firmware/GRUB pixels. Map graphics, clear scene backbuffer off-screen, reset generic publication readiness and backend selection.
2. Build the complete time-zero splash off-screen. CPU first-frame readiness becomes true; the generic gate stays closed while backend selection is pending.
3. PCI probe observes status zero and avoids rewriting it. It negotiates the usual VERSION_1-only 2D features. GPU initialization consumes that exact completed, queue-free probe instead of resetting/renegotiating it again.
4. Create/attach GPU resource off-screen. Select the final graphics backend, rebuild the complete welcome frame for that geometry, then authorize the generic publication gate.
5. Copy complete source to GPU CPU backing; complete full TRANSFER_TO_HOST_2D; SET_SCANOUT; **one** RESOURCE_FLUSH. The first Bursztyn GPU image contains title and `Witamy w Bursztyn OS`.
6. Mark successful publication and anchor the absolute animation clock there. Cursor-only requests during the system splash do not publish.
7. Startup mode replacement prepares an unbound resource and retains the previously visible resource/backing. Its first complete compositor image follows full TRANSFER_TO_HOST_2D → SET_SCANOUT → one RESOURCE_FLUSH; only then may the old backing be retired. Failed upload leaves the prior scanout intact.
8. After `ekran_inicjalizuj()`, close the startup initialization phase. Established interactive modesetting/confirmation behavior remains in place.

A **shared generic startup publication gate was required**. It is checked by native HAL copies, direct GOP pixels, graphics presentation entrances, GPU scanout/flush/transfer-and-present entrances, and first compositor region batching. It contains policy/complete-frame/backend readiness and generation, with no text content or application knowledge in the compositor/GPU. Native fallback is selected explicitly after GPU probing, then copies one completed CPU image.

SET_SCANOUT ordering changed again for **startup configuration restores after initial GPU publication**. Initial GPU upload-before-bind ordering from the previous patch is retained. The GPU helper still issues one RESOURCE_FLUSH per successful complete commit; activation loses the extra cursor flush (two → one activation flush). Active splash compositor transactions now issue one full logical commit; SET_SCANOUT and RESOURCE_FLUSH diagnostic events are two visibility boundaries within that single commit.

## Publication audit inventory

| Production entrance or operation | Audit outcome |
|---|---|
| `InicjalizujGrafike`, `SterownikEkranu::Inicjalizuj`, `SterownikGOP::Inicjalizuj`, `InicjalizujGOP` | Mapping/metadata and off-screen zero/background initialization only; no native framebuffer clear/copy. |
| `ZapiszPikselBackbuffer`, background restoration, text/title renderer | Write private CPU buffers; no internal present/flush. |
| `PrzeniesNaEkran`, `PrzeniesFragmentNaEkran` | Select complete presentation source; check generic readiness; active splash/first publication uses full copy. Successful driver commit alone marks publication. |
| `SterownikVESA/GOP::KopiujNaEkran`, fragment copies, `KopiujCalyBufor32`, `KopiujFragment32` | Low-level shared gate prevents bypasses before readiness/backend selection. |
| `GOP_PostawPiksel` | Shared gate before the direct physical store. |
| `grafika_aktywuj_virtio_gpu`, `grafika_zakoncz_wybor_backendu`, `grafika_przywroc_boot_backend` | GPU activation/fallback publishes a complete source after selecting a backend; restore uses guarded graphics entrance. |
| `OdswiezEkran`, `boot_log_gui`, terminal/cursor legacy entrances | Off-screen work/dirty requests or guarded presentation; cursor-only splash requests suppressed. |
| `skladacz_obrazu_zloz_klatke`, `skladacz_obrazu_obsluz_dirty`, `grafika_zakoncz_skladanie[_regionu]`, `grafika_prezentuj_scene_z_kursorem/region` | Completed scene before presentation; generic first region batch is full-screen; active system surface committed once. |
| GPU RESOURCE_CREATE_2D / attach backing / CPU framebuffer allocation | Off-screen resource preparation; create/attach alone do not bind scanout. |
| `virtio_gpu_cmd_transfer_to_host_2d` | Full successful transfer records resource completeness. First partial transfer cannot authorize a scanout. |
| `virtio_gpu_cmd_set_scanout[_locked]` | Current pending resource requires full successful transfer and generic readiness. Rollback to the retained valid old scanout remains possible. |
| `virtio_gpu_cmd_resource_flush`, `virtio_gpu_transfer_and_flush` | No flush of pending initial resource; full initial transfer precedes bind and flush. |
| `virtio_gpu_setup_framebuffer`, stale-resource release, rollback | Startup replacement retains old scanout; unpublished replacement cannot retire visible old backing; rollback preserves pending/visible state. |
| PCI GPU status reset in probe/init | Redundant cold/own-probe resets removed; diagnostic captures necessary resets separately from GPU commands. |
| GPU reset and reboot quiesce | Disable current **or retained-old** visible scanout; keep both CPU backings quarantined until a hardware reset is confirmed. Bounded quiesce and invalid used-ring handling remain; timeout values unchanged. |
| `bochs_wykryj` / GETCAPS | Preserves enabled firmware mode, no initial clear; `bochs_ustaw_tryb` still changes hardware mode during native interactive/startup modesetting. |
| `gui_image.cpp::load_and_draw_btp` | Caller-provided pointer writes; no callers or linked object in the production build. |

Native GOP/VESA exposes memory directly and has no atomic page-flip API here. The tests establish no initial generated blank/background publication and one completed-source native copy; they cannot guarantee that physical refresh never samples that copy mid-write. Likewise an initially active, nonzero-status firmware VirtIO owner still needs a real reset; no unsafe adoption of unknown queues was added. These are concrete limits of the audit, not claims of guest acceptance. The normal cold VirtIO path avoids the native copy and both redundant reset publications entirely.

## Rotating messages and deterministic composition

Every actual `powitanie_zrodlo()` rendering reconstructs the entire reusable splash surface: full background → title → one frozen absolute-time state → only that state's required message(s) → final scene blend if applicable. No incremental message-band clearing, old-frame pixels, partial accumulated fade blend, or rendering-internal present remains. The outer graphics/compositor path commits the completed image once.

The existing title/message design is preserved; the renderer had no separate underline to add or remove. All messages use one font cell and a shared alphabetic baseline:

```text
message_top = layout_top + title_cell_height + scaled_gap
baseline_y = message_top + ui_skaluj(13, message_font_scale)
render_y = baseline_y - ui_skaluj(13, message_font_scale)
center_x(text) = (frame_width - measured_width(text)) / 2
```

The renderer has a fixed 16-row bitmap cell rather than exported ascent/descent metrics; ascent 13 is the explicit baseline convention. At 1280×720 the baseline is 411 at 100%, 425 at 125%. Actual message-only pixel bounds:

| DPI | Second string | Third string | Shared baseline |
|---|---|---|---:|
| 100% | (469,388)–(808,417) | (512,388)–(765,417) | 411 |
| 125% | (428,395)–(848,432) | (481,395)–(794,432) | 425 |

**The previous sources already gave both crossfade strings the same Y and fixed font-cell height. They did not use per-string glyph bounding-box heights or overlapping independent time branches.** Thus those proposed causes are ruled out by source inspection. The observed lower line was not reproduced in the original renderer's correctly sampled, fully composed snapshots. Its exact guest cause, including whether stale pixels were actually involved, remains unproven without guest frames/trace. The fix removes incremental/history-dependent rendering and fragmented publications that could retain/display inconsistent content, and corrects the visible animation time origin. No invented definitive stacked-line diagnosis is asserted.

Exclusive state boundaries, relative to the first successful visible welcome:

| State | Interval |
|---|---|
| FIRSTHOLD | [0, 2000) ms |
| FIRSTTOSECOND | [2000, 2400) ms |
| SECONDHOLD | **[2400, 4200) ms** |
| SECONDTOTHIRD | **[4200, 4600) ms** |
| THIRDHOLD | [4600, 6400) ms, extended if desktop is not ready |
| FINALFADE | From max(6400 ms, desktop-ready timestamp), 600 ms |
| FINISHED | After that fade and one complete desktop reveal transaction |

At 4199 ms third alpha = 0. At 4200 ms transition state is selected with second/third alpha 255/0. At 4201 ms 8-bit quantization still gives 255/0; 4400 ms gives 128/127; 4599 ms gives 1/254. At 4600 ms and later second alpha = 0, third alpha = 255. Transition alphas sum to 255. Scheduler gaps sample only the new absolute timestamp, never queued/missed intermediate states.

Durations, crossfade boundaries, final fade, pacing, scheduler/HPET frequencies, GPU 500 ms logical timeout and 200 ms bounded quiesce are unchanged. **Only the clock origin changes** from creation to first successful visible commit: a 3000 ms allocation/probe delay previously allowed the first visible frame to skip the first message. New delayed-start tests always require the actual `Witamy…` image; later mode changes preserve the origin.

## Focused runtime diagnostics

`BURSZTYNDIAGSPLASHRUNTIMEFRAMES` defaults to 0. Set it to 1 for a separately rebuilt diagnostic image. The final requested normal fresh build uses the default 0.

Records identify publication sequence, timestamp, producer, origin path, backend, front/back pointers, scanout resource, dirty rectangle, active/readiness/gate state, title/message-region ink, current message index, full visible-pixel FNV-1a hash, publication generation and rendered frame number. Initialization, transfer, scanout, flush, native copies and status-reset display replacements are distinguishable. Uniform zero/background buffers report no text ink. Logging stops after 12 visibility boundaries, with at most 64 total preparation/publication records per startup. It does not log the entire animation. GPU hashes describe CPU command backing; the integration model independently checks the host-side transferred scanout pixels.

Sample host trace artifact: `splash_runtime_publications_2026-10-08.log`. This is not a guest serial trace.

## Changed production/build files in the current worktree

- `grafika.cpp`, `grafika.h`: generic readiness/backend selection, native copy protection, complete-frame publication and host publication hooks.
- `ekran_powitalny.cpp`, `ekran_powitalny.h`: full deterministic composition, frozen exclusive states, fixed shared baseline, visible clock origin.
- `ekran_powitalny_api.h`, `diagnostyka_startu.h`: prior first-frame API/diagnostic changes retained.
- `skladacz_obrazu.cpp`: generic first full transaction and diagnostic origin context.
- `sterowniki/grafika/uefi_gop.cpp`: direct-store gate and diagnostics.
- `virtio_gpu.cpp`, `virtio_gpu.h`: own-probe reuse, complete transfer before startup bind, retained old scanout/backing, teardown bookkeeping.
- `virtio_pci.cpp`: GPU cold-reset observation and reset-publication diagnostics; other device reset behavior unchanged.
- New `prezentacja_startu.h`, `diagnostyka_klatek_startu.h`: generic gate and bounded opt-in trace.
- `kernel.cpp`: final backend-selection boundary and startup phase completion.
- `Makefile`: option/dependencies and actual-production ASan/UBSan integration/snapshot targets.

Shell, Explorer, desktop layout, installer, BSP2, BWS ABI, PZB, scheduler, HPET, network, audio, input and both launchers have no source edits.

## Added/changed tests and verification

New: `narzedzia/test_splash_production_path.cpp` (real multiboot graphics initializer, native HALs, actual compositor, splash and GPU queue path), `narzedzia/mock_gpu_scanout.h` (host resources, scatter/gather transfer, visible scanout and status reset model), `narzedzia/test_splash_snapshots.cpp` (actual production module/Unicode pixel renderer). The prior `narzedzia/test_splash_first_frame.cpp` remains and is updated for the visible origin.

Changed: `narzedzia/test_virtio_pci.cpp`, `test_boot_graphics.cpp`, `test_compositor_cursor.cpp`, `test_powitanie.cpp`, `test_splash_timing.cpp`, `test_powitanie_reboot.cpp`, `test_gpu_quiesce_host.cpp`; prior static-contract changes retained in `test_startup_policy.py`, `test_powitanie_policy.py`.

Snapshots cover 0, 1, 1999, 2000, 2200, 2399, 2400, 4199, 4200, 4201, 4400, 4599, 4600, 4601, 6399, 6400, 6700, 6999, 7000, 7001 ms at 100%/125%, padded stride, actual final fade and desktop removal. Production frames at second hold → crossfade start → midpoint → third hold reuse the same allocation without harness clearing and equal a fresh zeroed buffer. Third-exclusive pixels are absent during second hold; message ink fits one shared row; different glyph shapes/heights and poisoned prior memory are covered. Full-frame golden hashes preserve the existing intended design.

The production-path suite verifies both GOP/VESA firmware sentinels, direct-publication bypasses, full GPU host pixels, no cold reset surface replacement, no untransferred first scanout, native fallback, allocation/probe delays, startup resolution restore and failed preparation/upload recovery, one commit/flush, and the 12-event diagnostic cap. The transport suite also checks necessary active-owner resets and own-probe reuse; reboot tests include quiesce while an old resource remains visible behind a pending replacement, and quarantine/release of both backings after a failed/confirmed reset.

Final focused host results (all successful, no guest execution):

| Suite | Result |
|---|---:|
| Actual graphics initializer/native/compositor/GPU first publication and modesets | 722/722 checks |
| Actual production splash snapshots / baselines / reused memory | 342/342 checks |
| Welcome lifecycle/rendering/options | 13703/13703 checks |
| Absolute splash timing/crossfade/stalls | 2185/2185 checks |
| Isolated first-frame ordering/readiness | 101/101 checks |
| Startup compositor visibility | 86/86 checks |
| Production boot graphics/commit | 64/64 checks |
| Compositor lifecycle/scene | 74293/74293 checks |
| Popup background restoration (subset of preceding suite) | 73816/73816 checks |
| Power teardown/concurrent splash | 601/601 checks, 100 host generations |
| GPU reboot/quiesce/pending old scanout | 41/41 checks |
| VirtIO transport/GPU/reset/used-ring suite | 146 cases passed, 0 failed |
| Static startup contracts | 20/20 |
| Welcome static integration contracts | 15/15 |
| Reboot/typography static integration contracts | 7/7 |

The C++ suites above use ASan and UBSan and reported no sanitizer error. LeakSanitizer cannot run under this environment's ptrace sandbox (its runtime explicitly rejects ptrace), so execution used `STARTUP_ASAN_OPTIONS=detect_leaks=0`. ASan/UBSan remain enabled. GPU integration/transport tests independently assert allocation-map cleanup and quarantine lifetimes; this is not a LeakSanitizer PASS. Counts are intentionally not added across case/assertion units or duplicated subset checks.

`make -B -j2 all iso` completed with exit status 0. The forced build compiled all production objects, passed the Multiboot2 image check and wallpaper package CRC/staging checks, then generated the ISO. No compiler warnings or errors were reported. No QEMU guest was started.

- ISO: `BursztynOS.iso`, 35,248,128 bytes; modification time 2026-10-08 00:19:51 +0200.
- ISO SHA256: `0f1fc91acb8142f14bfe543ea4c17d1f4e0f01fb07a51701d12b0710fe6c1010`.
- Kernel: 8,010,056 bytes; SHA256 `82633ccbf43657cfa70e03e1a9fc6144780803c242d28ea755c761681f259bcb`.
- The freshly linked kernel, staged `/boot/system_operacyjny.bin`, and kernel extracted from the actual ISO are byte-identical.
- The final normal kernel contains no `[SPLASH RUNTIME]` diagnostic string; the option is disabled by default.
- Build log: `/tmp/bursztyn-runtime-fresh-build.log`. Validation logs: `/tmp/bursztyn-runtime-production-final.log`, `/tmp/bursztyn-runtime-validation-final.log`, `/tmp/bursztyn-runtime-focused-complete.log`, `/tmp/bursztyn-runtime-gpu-validation-final.log`. These are host/build evidence only.

Manual Linux QEMU and Windows QEMU acceptance remains outstanding: firmware directly to complete welcome, no early/stacked third message or stale text, smooth transitions, full desktop reveal and responsive reboot. Neither runtime PASS nor a definitive guest stacked-line cause is claimed.
