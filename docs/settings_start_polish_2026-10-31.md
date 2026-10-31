# Settings and Start polish — 2026-10-31

The follow-up to the themes integration keeps the existing global theme transaction, persistence format, BursztynUI controls and power popup.

## Changes

- Start uses a centered 36 × 36 logical-pixel `PrzyciskObrazkowy`, with rounded theme styling and native power glyph 102. Its tooltip remains “Zasilanie”; the visible text is empty. The fixed lower region and theme-aware focus/hover/pressed/disabled styles remain. The popup retains its left alignment, 300 ms hover delay, 350 ms leave grace and keyboard navigation.
- Themes shows “Podgląd aktywny: 15 s”, then updates once per second using the existing monotonic deadline and animation scheduler. Only the status label invalidates. Apply, timeout, cancel and close clear the visible countdown; selecting another theme restarts it at 15 seconds with the original committed rollback target.
- Preview, Apply and Restore share one row. Their minimum widths follow actual font metrics. The Themes viewport scrolls horizontally when necessary, keeping labels readable rather than overlapping. Changing the description remeasures its height before placing actions; theme repaint preserves the viewport border and padding.
- All five Settings sections use a shared 12 logical-pixel inner inset. System, font and Support content retain their existing scrolling and models.
- Empty Start search uses a normal BursztynUI label at the top of the results area, with the same 8 logical-pixel left inset as app rows. Matching results immediately hide it.

There are no additional BWS calls, theme engine copies or power paths in this follow-up. Lock and Sleep remain disabled. Existing shutdown/restart dispatch stays unchanged. The empty-search label adds one bounded control allocation; countdown formatting uses a 48-byte local buffer and retains no per-tick allocation.

## Validation

ASan/UBSan:

- Desktop, Settings, Start, BSF and font/scale/layout matrix: **140021 checks PASS**. Fonts 12/16/20/24 px; scales 100/125/150/200%; narrow and wide content layouts.
- Theme lifecycle, every countdown second and exact dirty rectangle, Apply/cancel/restart/close, mouse click/hover and keyboard power navigation, all five bundled themes and both screen bounds: **2507 checks PASS**.
- BursztynUI controls: **1379/1379 PASS**.

Additional optimized host theme-engine unit tests: **155/155 PASS** (this target does not enable sanitizers).

Release QEMU:

- [Themes and power acceptance](settings-start-polish-2026-10-31/themes-result.json): PASS for all five bundled themes, visible countdown pixels, top-aligned empty-search text, rollback palette repaint, Apply with no delayed rollback, reopening, close rollback, hover, keyboard and outside-click dismissal.
- [Resolution/scale acceptance](settings-start-polish-2026-10-31/scales-result.json): PASS at 1920×1080 with 125/150/200%, then 1280×720 at 100%. Each transition opens the centered icon and submenu, checks published bounds (including the translated shadow) and dismisses them by keyboard. This focused run omits the full QEMU desktop-anchor walk; the host matrix covers desktop regressions.
- [Font/Support acceptance](settings-start-polish-2026-10-31/font-result.json): PASS, including 100 clock frames, live 16 → 24 px font Apply, three Support categories, Refresh, Settings reopening, a visible changing 24 px countdown and the power popup.

Evidence: [Settings](settings-start-polish-2026-10-31/settings-themes.png), [countdown first](settings-start-polish-2026-10-31/countdown-first.png), [countdown next](settings-start-polish-2026-10-31/countdown-next.png), [empty search](settings-start-polish-2026-10-31/start-empty-search.png), [power in Grafit](settings-start-polish-2026-10-31/power-grafit.png), [power at 200%](settings-start-polish-2026-10-31/power-scale-3.png).

All three successful sequential release runs have [no panic, unexpected Ring 3 fault or critical GPU timeout markers](settings-start-polish-2026-10-31/health.json).

Host logs: [layout/BSF](settings-start-polish-2026-10-31/host-desktop.log), [countdown/power](settings-start-polish-2026-10-31/host-themes-power.log), [controls/theme engine](settings-start-polish-2026-10-31/host-controls-theme-engine.log).

## Test limitations and failed attempts

One [parallel QEMU display run](settings-start-polish-2026-10-31/qemu-parallel-display-failure.log) stopped on a VirtIO-GPU transfer timeout after five mode/scale transitions. The diagnostic reported the expected response already present, with a large elapsed time. Its underlying cause is not established; no GPU/kernel workaround was added in this UI change. Subsequent acceptance runs use one VM at a time. The focused sequential scale run passes; the failed 13-transition run is not counted as passing.

An initial Escape test asserted against kernel popup state before waiting for event completion; it now waits for Start dismissal. Another harness assertion counted Python’s half-to-even rounding for a 125% shadow; it now uses the renderer’s half-up rule. Host input tests and sequential QEMU pass.

The final review also covers selection arriving at the preview deadline before the next animation callback: selection now reconciles the countdown label together with the transaction token. The added ASan/UBSan test passes. The final ISO repeated theme-cycle QEMU acceptance after this change: PASS. Font and display acceptance above used the preceding ISO (`a42ba48d…`); the kernel/WM image is identical, and this last Settings-only change does not alter layout or font rendering.

## Artifact hashes

```text
0079c9c972228b1446bf28f1e0e75be71ce16aa37e67bfecec1555c4bc678c0c  system_operacyjny.bin
56d6e6a2d5f71e6541aa7baa2f08eca8637896afa9d51fa967652cf88d2d74d4  BursztynOS.iso
```
