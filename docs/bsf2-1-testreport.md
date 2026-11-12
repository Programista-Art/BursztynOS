# BSF 2.1 — test report (2026-11-12)

Branch `feature/bsf-2-1-text-integration` (from `test` @ `544a51e`),
uncommitted. All host suites below were run on the final code; every number
is copied from the run logs.

## Host suites (final sweep, all rc=0, ASan/UBSan unless noted)

| Command | Result |
|---|---|
| `make test-bsf-bidi-host` | Unicode 15.1.0 BidiTest 770,241 + BidiCharacterTest 91,707 cases, 0 failures (4,887,109 checks) |
| `make test-bsf-shaping-host` | 120/120 HarfBuzz oracle rows exact (1,222 glyphs) |
| `make test-bsf-layout-host` | 409,086 checks (BSF 2.0 suite, unchanged) |
| `make test-bsf21-layout-host` **new** | 241,797 checks: paragraph direction across wraps, `caret_x_on`/`move_visual_on`/`line_edge`, letter/word spacing, joining context, generations, 1,500 random wrapped paragraphs |
| `make test-bsf2-kernel-abi-host` | 41,018 checks |
| `make test-notepad-unicode-host` | 60,662 checks; 60,000 backward keys 8.40 s (baseline 8.37 s) |
| `make test-notepad-bidi-host` **new** | 6,100 checks: wrapped RTL paragraph with Latin continuation line, mixed Hebrew/Arabic/Polish/digits, explicit LTR/RTL, right alignment, visual walks (monotone, no revisits, full visual coverage, termination across opposite-direction paragraphs), Home/End with affinity, Ctrl+Home/End, Up/Down preferred x, Ctrl+word moves, mouse hit = caret, multi-span selection, drag selection, copy/cut/paste/save/reopen/undo/redo byte-exact on 6 samples incl. malformed UTF-8, niqqud, lam-alef, isolates, U+2029; long 20 KB mixed paragraph latency |
| `make test-bsf-ui-host` | 9,084 checks (unchanged count vs baseline) |
| `make test-bursztynui-bidi-host` **new** | 803 checks (see `docs/bsf2-1-bursztynui.md`) |
| `make test-gui-text-input-host` | 7,293/7,293 |
| `make test-shell-host`, `make test-shell-ttf-host` | pass (Terminal unchanged; no terminal/shell source modified) |
| `make test-husarz-engine-host` | 127,432 checks |
| `make test-husarz-layout-host` | 259,225 checks |
| `make test-husarz-selection-host` | 987,380 checks |
| `make test-husarz-fidelity-host` | 3,697 checks |
| `make test-husarz-css-host` | 362 checks |
| `make test-husarz-performance-host` | 4,637 checks (baseline 4,621: the suite counts per-slice checks and the new stages add `WorkPoint`s), slice cap 2,048 |
| `make test-husarz-javascript-host` | 10,865 checks |
| `make test-husarz-runtime-host` | 2,259 checks |
| `make test-husarz-bidi-host` **new** | 24,003 checks on `testy/husarz_bidi/01…12` + long (1,500 mixed words, sliced by the cooperative driver, `maximum <= MaxSliceWork`) + adversarial page (malformed UTF-8, 200 nested bdo/dir spans, 20,000 alternating-direction runs, 40 KB plaintext pre) |

Fuzzing (`make fuzz-bsf21-smoke`, ASan + UBSan + trace-pc):
`fuzz_bsf21_editing` 4,000 iterations, 2,944 coverage edges, 0 failures;
`fuzz_husarz_bidi` 1,500 iterations, 11,694 edges, 0 failures.

## Reference testing (E4)

UAX #9 against the official Unicode 15.1 conformance files (above); shaping
against host HarfBuzz (oracle rows; HarfBuzz is host-only). Browser
expectations (visual order of each page) were derived by hand from UAX #9
rules and the CSS Writing Modes 3 control mapping and are written in the test
with their reasoning; this is a regression set, **not** a web-standards
conformance claim (no WPT run).

## Native QEMU (production ISO SHA-256 `98d9c1d9152e9dfc26871b71cbf0c209b5d74a430f2f0c085f315141020a41cd`)

`python3 narzedzia/bsf2_native_qemu.py --workload bsf21 [--video 1920x1080 | --bios]`
— logs, JSON and screenshots in `.bsf-test/bsf21-native/<mode>/`.

| Mode | Result | VirtIO-GPU recovery |
|---|---|---|
| UEFI 1280×720 | **all checks PASS** | 4 soft timeouts, 4 late completions, 0 permanent |
| UEFI 1920×1080 (grub.cfg fixture) | **all checks PASS** | 1 / 1 / 0 |
| BIOS (guest 1280×720) | **all checks PASS** | 0 / 0 / 0 |

Checks per run: desktop + BMP modules, Settings → Fonts (Unicode blob, BSF
layouts built, GSUB and GPOS applied, no layout/budget failures), Husarz pages
01, 03, 04, 07, 09, 10, 12, 11 served over QEMU guestfwd (BSF counters read
from the browser process: shaped runs > 0, caller-owned layouts > 0, Husarz
shaped-paint counters > 0, no layout failures), RTL form value copied with the
real clipboard (click into the `dir=rtl` field, arrows, Shift+Left, Ctrl+A,
Ctrl+C), pasted into Notepad (Ctrl+V), arrows/Shift selection/End/Home; the
Notepad process counters show BSF 2.1 paragraph layouts built
(`owned_builds` 0 → 1) and shaped runs; Calculator, Snake, Kontrolki 4 launch
and close; serial log free of faults. Screenshots show e.g. `עולם Hello
world 123 שלום` right-aligned (03), joined `بيت كتاب` across `<b>`/`<i>` (09),
right-aligned RTL inputs (11), right-aligned Hebrew paragraph in Notepad.

Security state natively: the kernel, linker script and protection code are
unchanged; the runs above boot the same protection setup as the BSF 2.0
verification (SMEP/SMAP/NX on). No separate CR4/EFER read was repeated in
these runs.

## Not verified / manual

- Native typing of non-ASCII text (QMP cannot inject it); native RTL text
  came from fixtures and the real clipboard. Manual procedure: in Husarz open
  a page with `<input dir=rtl>`, type Hebrew via the OS keyboard layout, use
  Left/Right/Shift; in Notepad type Arabic, wrap a long RTL line, check Home/
  End/Up/Down and selection highlight.
- Native Notepad caret/selection pixels were captured but not asserted.
- Windows QEMU, installed-disk boot and physical hardware: **not tested**.
