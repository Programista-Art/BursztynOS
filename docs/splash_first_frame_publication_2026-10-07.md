# First welcome frame publication — 2026-10-07

Scope: current worktree based on `a5742cd`. Host-only verification; no QEMU was started. Windows and Linux guest acceptance remains manual.

## What the old path published

The first **Bursztyn-generated** frame was the uniform fallback desktop color `#111315`, without the splash title or message. It was copied by `InicjalizujGrafike()` → `grafika_odtworz_tlo_skladania()` → `PrzeniesNaEkran()` → the GOP/VESA screen driver. At this point `powitanie_inicjalizuj()` had not run, so `powitanie_zrodlo()` returned the scene backbuffer. This was an intentional backbuffer initialization accidentally made visible, not an allocated/partially rendered splash surface. The current renderer already renders background, title and message synchronously before returning its surface.

A deterministic replay using original production graphics/welcome sources from HEAD, and the last two instructions of the old graphics initializer, recorded:

```text
BEFORE first published frame: 921600/921600 pixels #111315; title=0; message=0; presents=1
BEFORE second published frame: full splash; title_pixels=5657; message_pixels=4123; presents=2
```

Replay source/binary: `/tmp/splash-first-before/`; log: `/tmp/splash-first-before.log`. This uses a fake display, not hardware execution of the complete multiboot initializer. The source-order assertion checks that the real initializer no longer calls `PrzeniesNaEkran()`.

The subsequent VirtIO backend initialization had another premature visibility boundary: `RESOURCE_CREATE_2D` → framebuffer allocation → `RESOURCE_ATTACH_BACKING` → **`SET_SCANOUT` before any `TRANSFER_TO_HOST_2D`**. The resource had no uploaded splash pixels when attached. Host framebuffer allocation is zeroed in the mock; production allocation maps pages without explicitly clearing them. No evidence justifies calling the visible host resource “stale firmware pixels” or assigning a precise hardware color to it. This second unsafe boundary is also fixed.

The firmware/GRUB final image precedes all Bursztyn frames. GOP initialization only initializes metadata; it does not clear visible pixel memory. Firmware image contents and the particular display refresh that the user observed cannot be established without guest capture. No guest runtime PASS is claimed.

## Old publication order

1. `kernel_main()` initializes graphics before HPET and splash policy.
2. Map GOP/VESA framebuffer, allocate and initialize scene backbuffer.
3. Fill scene with fallback desktop background and copy it to the visible boot framebuffer (**unwanted background-only frame**).
4. Initialize HPET and welcome sequence; allocate welcome surface, render full Unicode splash, then copy it (**correct splash**).
5. Initialize VirtIO-GPU resource/backing and attach scanout (**unuploaded resource exposed**).
6. Activate GPU graphics backend, render/select splash, copy full CPU pixels, `TRANSFER_TO_HOST_2D`, `RESOURCE_FLUSH`.
7. Compositor composes dirty scene regions and selects the welcome presentation surface; the desktop stays behind the overlay until readiness/final-fade handoff.

## New publication order

1. Initialize/map graphics and initialize the scene **off-screen**, retaining the valid firmware framebuffer.
2. Start the welcome sequence with readiness false.
3. Allocate welcome surface; sample current absolute time; render full background, large smoothed Unicode title and complete current message (time zero: “Witamy w Bursztyn OS”, opacity 255).
4. Finish all CPU rendering; set first-frame readiness true.
5. Presentation checks readiness. Its first commit copies the **whole** welcome image, including when requested through a 1×1/cursor/partial dirty rectangle. No placeholder frame is presented while readiness is false.
6. GPU initialization creates/attaches backing but leaves initial scanout pending. The earlier valid native frame stays in place.
7. GPU backend activation invalidates the old welcome surface/readiness for its new graphics generation; re-render the current absolute-time state off-screen and publish only the new complete surface.
8. First full GPU commit: copy complete CPU frame → **`TRANSFER_TO_HOST_2D` completion → `SET_SCANOUT` completion → one `RESOURCE_FLUSH`**. Initial partial GPU publication is rejected without issuing commands. Failed upload or scanout attachment does not authorize a flush.
9. Pending first compositor dirty elements are collapsed into one full-screen region, producing one coherent first composition/presentation. Later dirty-region and cursor transactions retain their existing behavior.

`SET_SCANOUT` moved out of initial resource setup, after successful full upload. The initial command count remains one transfer, one scanout attachment, one flush; no extra flush or black frame was introduced. The native background-only presentation was removed. The first dirty-region transaction is full-screen instead of potentially publishing one element/fragment at a time. Established runtime modesetting retains its existing confirmation/rollback path; an unpublished startup resolution replacement also remains pending.

Explicit readiness and first-publication flags are under the existing screen-lock contract. Surface release, graphics reset, resolution/DPI rebuild and welcome reinitialization invalidate them. The renderer and first-time/fullscreen presentation complete before publication. No frontend/window model, compositor architecture, driver timeout, GPU queue completion mechanism or animation schedule changed.

## Preserved behavior

- Shared large smoothed Unicode renderer: 56 logical pixels for title, 36 for message. The current design has no separate underline; no design element was removed.
- First hold 2,000,000 µs; crossfades 400,000 µs at 2,000,000 and 4,200,000 µs; minimum 6,400,000 µs; final fade 600,000 µs; earliest desktop reveal 7,000,000 µs.
- Absolute time, with no replay of missed states, sleeps, arbitrary delays or retry loops.
- Desktop-ready/full-scene gating and splash release before compositor/GPU quiesce.
- Bounded GPU quiesce and `virtqueue_pop_used()` error handling; 500 ms logical command timeout unchanged.
- Shell, Explorer, desktop layout, installer logic, BSP2, BWS ABI, PZB, scheduler/HPET, audio/network/input and both launchers unchanged.

## Diagnostics

`BURSZTYNDIAGSPLASHFIRSTFRAME=1` enables sparse `[SPLASH FIRST]` events; default is 0. Events contain `now_us`, welcome `generation`, surface `frame`, GPU `resource`, title/message pixel presence and message index. They cover graphics ready, compositor ready, splash created, render begin/end, readiness set, first graphics submission, completed graphics publication, first compositor commit, GPU resource preparation, completed first upload, completed first visible scanout attachment and first flush. Native framebuffer publication and actual compositor commits have distinct event names. `graphics_ready` can report 0 because HPET initialization follows graphics setup; later first-frame events use the initialized monotonic clock.

Frame increments for a complete surface rebuild, not each animation step. Generation increments on welcome reinitialization; resource IDs alone can be reused and are not treated as unique frame identities. Pixel checks are diagnostic-only. Logs prove command and CPU rendering order, not host display refresh timing. Four affected translation units were compiled with diagnostics enabled; final diagnostic source was additionally checked with the actual freestanding compiler flags.

## Changed files

Production: `grafika.cpp`, `ekran_powitalny.cpp`, `ekran_powitalny_api.h`, `skladacz_obrazu.cpp`, `virtio_gpu.cpp`, `virtio_gpu.h`, `diagnostyka_startu.h`.

Build: `Makefile` (diagnostic switch, header dependency, host first-frame target).

New test: `narzedzia/test_splash_first_frame.cpp` (real lifecycle/renderer with deterministic fake compositor publication recorder).

Updated tests: `narzedzia/test_boot_graphics.cpp` (real full/partial graphics entry points and generation invalidation), `narzedzia/test_compositor_cursor.cpp` (production compositor first dirty-region batching), `narzedzia/test_virtio_pci.cpp` (real GPU command order, captured upload/resource identity, failed initial upload/scanout, unpublished resolution replacement and new generation; runtime lifecycle tests explicitly publish their initial framebuffer), `narzedzia/test_startup_policy.py` (no bootstrap presentation), `narzedzia/test_powitanie_policy.py` (report imported contract totals correctly).

No launcher file changed. Previously tracked generated test executables are restored after testing rather than included as source changes.

## Validation

| Suite | Result |
|---|---:|
| First-frame readiness/order/pixels; 0/500/1500/3000 ms; resolution/DPI/generation | 101 / 101 PASS |
| Welcome lifecycle/rendering/options; 1200 interrupted + 500 bypass/failure cycles | 13,703 / 13,703 PASS |
| Absolute-time states, crossfades, stalls, final handoff and timing diagnostics | 2185 / 2185 PASS |
| Production compositor startup visibility, 100% / 125% | 86 / 86 PASS |
| Production graphics entry points/full first publication/generation reset | 64 / 64 PASS |
| Concurrent power teardown/splash lifecycle, 100 generations | 601 / 601 PASS |
| GPU quiesce/held lock/long gap/invalid completion | 38 / 38 PASS |
| Production compositor lifecycle/scene/first dirty batching | 74,293 / 74,293 PASS |
| Full VirtIO host suite, including 24 GPU cases | 145 PASS, 0 FAIL |
| Startup policy contracts | 20 / 20 PASS |
| Welcome integration contracts | 15 / 15 PASS |
| Reboot/Unicode/typography contracts | 7 / 7 PASS |
| Diagnostic-enabled production translation units | 4 compiled successfully; final syntax checks PASS |
| `git diff --check` | PASS |

Popup restoration's 73,816 assertions are included in the compositor's 74,293 total. Repeated policy runs are counted once. Assertion totals and suite cases are reported separately rather than summed as equivalent units.

**ASan/UBSan: PASS, no reported memory or undefined-behavior errors.** LeakSanitizer passed for first-frame, splash timing, concurrent teardown, GPU quiesce, compositor and full VirtIO suites. Welcome/graphics/startup visibility preserve their existing `detect_leaks=0`. The old-frame replay also passed ASan/UBSan and LeakSanitizer; its renamed, unused old suite entry point produces a return-type warning only in that temporary probe.

Both requested host-test invocations exited 0. No QEMU, guest reboot loop, `make rm` or `make clean` was used.

Logs: `/tmp/splash-first-final-tests.log` (final welcome/first-frame/timing/compositor/teardown), `/tmp/splash-first-tests.log` (145-case VirtIO suite and initial checks), `/tmp/splash-first-build.log`, `/tmp/splash-first-final-build.log`, `/tmp/splash-first-before.log`.

**Final `make -j2 all iso`: PASS, exit 0.** The first build also exited 0; the final incremental build rebuilt objects affected by the last diagnostic/header edits and regenerated ISO. No production-build warnings/errors were reported.

ISO: `BursztynOS.iso`, 35,246,080 bytes. SHA-256: `f89598faa8f16212749974175a479f17d56cb34c5639ff924329265667d70d7f`.

Build outputs remain available in the worktree; no clean/removal of build objects was performed.

## Manual acceptance

Use the existing Windows and Linux launchers. Confirm direct firmware/GRUB → complete welcome first frame, immediately readable large title and “Witamy w Bursztyn OS”, no initial background-only flash, smooth later crossfades, complete desktop reveal, responsive startup and responsive reboot. **Windows/Linux QEMU runtime PASS is not claimed.**

## Session resume — 2026-10-07, 23:03 CEST

Recovered session `01a113fc-c737-7d13-a10a-d43cca2d5c34` and verified that its production changes, new first-frame test and report remain in the current worktree. The previously reported ISO and generated compilation outputs were missing, so `make -j2 all iso` was run again without changing production sources, cleaning or starting QEMU.

**Rebuild: PASS, exit 0**, with no compiler warnings/errors reported. `git diff --check`: PASS. Host tests and ASan/UBSan were not repeated; their results above belong to the completed original session. Manual Windows/Linux QEMU acceptance remains outstanding.

Current ISO: `BursztynOS.iso`, 35,246,080 bytes. Current SHA-256: `cb10fdf853c8674641d6ef48bd7ad2cf143e249ab3c98f6bab8db17769e9d2e1`. This replaces the earlier build artifact; its checksum above is historical. Rebuild log: `/tmp/bursztyn-resume-01a113fc-build.log`.
