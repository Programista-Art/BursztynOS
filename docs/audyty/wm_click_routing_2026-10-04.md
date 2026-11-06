# GUI click-routing audit — 2026-10-04

Status: **NOT confirmed fixed in OS runtime.** A concrete titlebar hit-test defect
was reproduced with host tests and corrected. The reported global failure to
launch apps is not yet explained. The sole SDL VM session did not reach desktop
readiness within its 120-second deadline, and no app click actions ran. No retry.

## Initial worktree audit

Inspected the existing diff in `bursztyn_gui.cpp`, `skladacz_obrazu.cpp`,
`skladacz_obrazu.h`, `wm_dekoracje.h` (initially untracked), `motyw.cpp`,
`bursztyn_ui/widok_menu_popup.h`, `menedzer_okien.cpp`, `shell.cpp`, and `Makefile`.
`menedzer_okien.cpp` initially had no changes relative to HEAD. Existing changes:

- GUI standard-titlebar helper registers titles and stops painting normal chrome.
- Compositor skips retained titlebar rows and draws chrome directly into the scene;
  both full and dirty-region paths invoke the new renderer.
- Title changes and theme broadcasts invalidate titlebar and border strips.
- Compact shared popup measures Unicode labels and allocates optional columns.
- Shell refreshes styles/client/menu on theme changes before popup dispatch.
- Make dependencies cover the new decoration and popup headers.

The decoration renderer **does not create a layer**, allocate an input surface,
change window rectangles, raise z-order, set overlays, or deliver/consume events.
There is no full-screen decoration input layer in these changes. No z-order
regression was demonstrated. The scene and owning retained buffer have diverged
for decoration pixels, which matters to pixel-based input picking.

## Concrete defect and correction

`grafika.cpp::znajdz_warstwe_pod_punktem` rejected any layer whose retained pixel
under the pointer was `0x00000000`. After moving titlebar rendering into the
compositor, a visible titlebar/control can have a zero retained pixel. The owning
window then fails the hit-test and the click targets a lower layer, often the
WM root. This is **click-through of visible chrome**, not interception by an
invisible decoration layer. It does not establish the cause of desktop/Start
launch failure before any normal window exists.

The picker now uses bounded compositor-decoration geometry in addition to
retained client pixels. It still selects the existing owning PID, preserving
focus, capture, app decoration actions, dragging, minimize, maximize and close.
No compositor event consumer or separate input object was added. The shared GUI
library's existing titlebar action classification was extracted for direct host
coverage, preserving its control thresholds and rounded-corner rule; boundary
arithmetic uses 64-bit comparisons. Client transparency remains unchanged.

`0x00000000` is the existing transparency sentinel; this compositor has no layer
opacity field. Nonzero pixels with a zero high byte are normal RGB pixels, not
zero-opacity objects. Treating them as alpha-transparent would break existing
apps. The desktop root keeps z=0 and is never raised by the window-raise helper.

## Complete pipeline traced in source

1. `virtio_input.cpp` drains VirtIO events into `input_device.cpp`.
2. `INPUT_EV_KEY` updates button state. ABS axes are accumulated until SYN_REPORT.
   The selected tablet maps its axis range to screen width/height minus one.
3. `zaktualizuj_mysze_pozycja_zrodlo` calls `zaktualizuj_mysze_zrodlo` with global
   coordinates expressed through a delta; the latter clamps to the screen.
4. A new DOWN clears stale capture, hit-tests, raises a normal target and changes
   focus. Capture or active PID determines delivery.
5. Picking checks the existing modal popup owner first, then bounded, active
   layers in z-order (PID breaks ties). Decoration geometry is checked separately
   from client-buffer transparency. No new visual layer receives events.
6. `scheduler_dodaj_zdarzenie(cel, &e)` receives global x/y, button state and event
   type. BWS38/37 delivers queued events to the target's GUI event loop. Scheduler
   source was not changed; there is no evidence of dropped events.
7. WM's root loop handles Start through `obsluz_klikniecie`. Start menu row indices
   dispatch through `obsluz_element_menu`, `uruchom_lub_aktywuj`, then
   `uruchom_program_gui` and BWS10. Built-in desktop icons use the same launch path.
8. App client events remain app-owned. The shared titlebar classifier returns
   drag/minimize/maximize/close actions to existing app/GUI handlers. Rendering
   ownership does not take over those interaction semantics.

An open popup intentionally gets the first outside DOWN to dismiss it. The
existing rule consumes that DOWN/UP pair; it does not launch an underlying target
on the same click. Subsequent clicks route normally. This modal policy is
preserved rather than changed to click-through.

## Files changed in this task

- `grafika.cpp`: use tested picker plus geometry-aware decoration eligibility;
  gate existing rate-limited click diagnostics behind BURSZTYN_DEBUG_GUI_INPUT.
- `wm_input.h`: extracted production topmost interactive-layer selection.
- `skladacz_obrazu.cpp`, `skladacz_obrazu.h`: bounded owning-layer decoration query.
- `wm_dekoracje.h`: shared bounded titlebar/border geometry predicate.
- `wm_belka_hit.h`, `bursztyn_gui.cpp`: extract existing action classifier for tests.
- `menedzer_okien.cpp`: gate existing menu-hit diagnostics behind DIAG_BOOT.
- `Makefile`: dependencies and sanitizer host test targets.
- `narzedzia/test_wm_click_routing.cpp`: new actual-WM-handler/mocked-BWS harness.
- `narzedzia/test_shell_terminal.cpp`: assert normal routing resumes after dismissal.
- `narzedzia/test_wm_click_runtime.py`: new single-session production SDL/QMP probe.
- `docs/wm_click_routing_2026-10-04.md`: this report.

The pre-existing live-theme, compact-menu and Shell changes were retained.
No scheduler, SDL profile, virtio-tablet-pci, VirtIO-GPU timeout, reboot handling,
BSP2, clipboard, wsparcie or version changes. No GTK or make rm. Version remains
Bursztyn OS v1.0.1.6. UTF-8 tests remain in place.

## Automated coverage and results

New WM host coverage executes production Start and desktop handlers with a
mocked BWS launch action: Start opens; Shell and Notatnik entries select their
correct paths; a desktop icon invokes its action. The production picker is tested
for normal client PID, transparent client pass-through, visible chrome with zero
retained pixels (including reproduction of the old rejection), titlebar drag and
close classification, topmost z and PID ties, inactive layers, a full-screen
transparent surface above the desktop, modal owner priority, theme-independent
layer geometry, and renderer redraw leaving client pixels and metadata intact.
Scales: 100% and 125%.

Existing Shell host tests verify compact Unicode popup dimensions, inside
activation, disabled entries, outside dismissal, clipboard and theme refresh.
New assertion verifies a subsequent click is no longer consumed after dismissal.

| Check | Result |
|---|---|
| Affected kernel/compositor/theme/GUI/WM/Shell/Notatnik component build | PASS |
| WM click-routing host + ASan/UBSan | PASS |
| WM decoration rendering host + ASan/UBSan | PASS |
| Shell host + ASan/UBSan | PASS |
| UI controls (ASan/UBSan) | 1379/1379 PASS |
| Command bindings (ASan/UBSan) | 23/23 PASS |
| Theme unit tests | 155/155 PASS |
| Theme GUI/source integration | 25/25 PASS |
| Notatnik GUI/source checks | 14/14 PASS |
| Kalkulator GUI/source checks | 14/14 PASS |
| make -j2 all iso | PASS, exit 0 |
| git diff --check | PASS |

Sanitizers use detect_leaks=0, consistent with the existing UI/Shell harnesses.
Host logs: `/tmp/wm-components.log`, `/tmp/wm-host-tests.log`,
`/tmp/wm-routing-final.log`, `/tmp/wm-final-regressions.log`, `/tmp/wm-all-iso.log`.

## Single automated runtime session — separately

Executed once via `make runuefi`, retaining the production SDL, TCG, 1 vCPU,
2 GiB, VirtIO GPU/network/storage/RNG/keyboard/tablet configuration. Added only
QMP access and redirected serial output to a file. Boot reached OVMF and GRUB;
the normal entry was selected automatically. Desktop readiness timed out at
120 seconds. QEMU was terminated; no second VM/session was started.

Evidence: `.wm-test/runtime/serial.log`, `.wm-test/runtime/result.json`,
`/tmp/wm-runtime-driver.log`. There are no runtime app screenshots or launch/PID
messages: no click actions ran. Start, Shell, Notatnik, focus, close, and theme
runtime behavior are **UNVERIFIED**, not PASS or proof of a GUI routing failure.

After stopping, the probe's future-run deadline was changed to 300 seconds
(the existing single-SDL infrastructure's limit), and its pre-desktop failure
capture was corrected. Those runner changes were syntax checked only; the VM
was not repeated. The implemented runtime assertions require visible close-button
pixels and client-image changes, not merely an assumed successful click or PID.
It also compares guest layer titles and focus via read-only QMP physical-memory
snapshots; it never edits guest memory. Existing theme automation is reused.

The exact cause of the user's all-app-launch regression remains unresolved.
The source/host evidence establishes a narrower chrome picking defect only.
