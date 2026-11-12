# BSF 2.2 — test report (2026-11-12)

## Commands

```
make test-bsf22-host      # all BSF 2.2 host suites (ASan + UBSan)
make fuzz-bsf22           # coverage-guided fuzzing (BSF22_FUZZ_ITERS)
make bench-bsf22          # micro-benchmarks
make iso
python3 narzedzia/bsf2_native_qemu.py --workload bsf22 [--video 1920x1080 | --bios]
```

Reference tools (host only, never shipped): FreeType 2.13 (system lib),
uharfbuzz 0.45.0, fontTools 4.55.3, blackrenderer 0.6.0 + skia-python,
brotli 1.1.0 (Python, fixture generation).

## Host results

| Target | Checks | Key evidence |
|---|---|---|
| test-bsf22-cff-host | 65,702 | 16,395/16,395 outlines = FreeType; CFF raster mean diff 1.8–4.0 % (TrueType 4.4 %); adversarial charstrings; 3,000 mutations |
| test-bsf22-shaping-host | 120 + 44 + 51 + 22 rows | all exact vs HarfBuzz (base, CFF, variable instances, emoji) |
| test-bsf22-collections-host | 19,122 | 3 collections = FreeType; malformed headers; runtime catalog; Noto CJK 19.5 MB lazy load |
| test-bsf22-font-catalog-host | 307 | Config v2/v1 migration, Terminal policy, refresh |
| test-bsf22-woff-host | 62,085 | 10 fonts decoded = source; bombs; 7,500 mutations |
| test-bsf22-husarz-webfonts-host | 185 | CSS/src/CORS/isolation/layout widths |
| test-bsf22-variations-host | 43,107 | 14,267 instance outlines = FreeType; 3,000 mutations |
| test-bsf22-color-host | 1,093 | CBDT = FreeType; COLRv1 vs Skia 0.38 % mean; limits; 6,000 mutations |
| test-bsf22-emoji-host | 192 | sequences, presentation, baseline, runtime |

## Regression (BSF 2.0 / 2.1 and neighbours)

Passing: test-bsf-host (12,204), test-bsf-shaping-host (120/120),
test-bsf-bidi-host (770,241 + 91,707 conformance cases), test-bsf-layout-host
(409,086), test-bsf21-layout-host (241,797), test-bsf-ui-host (9,069),
test-bsf-resources-host (4,698), test-bursztynui-bidi-host (803),
test-notepad-bidi-host (6,100), test-notepad-unicode-host (60,662),
test-husarz-bidi-host (24,003), test-husarz-{engine,runtime,fidelity,layout,
selection,css,javascript}-host, test-shell-ttf-host, test-shell-host,
test-desktop-rubber-host, test-desktop-settings-host (234),
test-psf-bootstrap-host, test-bsf2-kernel-abi-host, test-settings-themes-power-host.

Failing, pre-existing: **test-desktop-ttf-host** fails at
`desktop_display_cases.h:63` on the untouched BSF 2.1 baseline as well
(verified with `git stash`); not caused by BSF 2.2.

Assertions changed (new contract, not removed):
1. Font-listing mocks (`test_bsf_ui`, `test_desktop_ttf`, `test_husarz_bidi`,
   `bench_husarz_bidi`) report the new optional directories as absent.
2. `test_bsf_resources`: a file one byte over 16 MiB is now accepted via
   per-table reads (renders bit-identically, only tables resident); files
   over 64 MiB are rejected.
3. `test_desktop_settings_authorization`: BWS 59 bound is `MaxFontFile`;
   new checks for `/czcionki/otf` and extension/directory mismatch.
4. WOFF comparison ignores encoder-written head bytes and `DSIG` (WOFF2).

`test-bsf-ui-host` executes 9,069 instead of 9,084 checks: the
regional-indicator caret sample now takes the shaped-layout path (emoji),
its width differs, and a per-pixel loop iterates less (verified by
reverting the `needs_complex_layout` change: 9,084 again).

## Fuzzing

| Target | Executions | Edges | Corpus | Crashes |
|---|---|---|---|---|
| fuzz_bsf22_font | 300,016 | 3,447 | 954 | 0 |
| fuzz_bsf22_webfont | 300,013 | 2,737 | 293 | 0 |
| fuzz_bsf22_png | 300,012 | 373 | 115 | 0 |

ASan + UBSan, trace-pc engine. Short campaigns; not proof of absence of bugs.

## Native (QEMU, production ISO + collection fixtures)

| Mode | Result |
|---|---|
| UEFI 1280×720 (`--workload bsf22`) | **35/35** checks, 0 faults, 0 GPU soft timeouts |
| UEFI 1920×1080 (`--video 1920x1080`) | **35/35**, 0 faults, 0 GPU timeouts |
| BIOS | **35/35**, 0 faults, 0 GPU timeouts |

Checks include: catalog in Settings (OTF CFF, 3 TTC faces, 4 CJK OTC faces,
colour emoji font, DejaVu, all status OK); Husarz system CFF rasterizations,
CJK collection, colour emoji glyphs (COLRv1); web fonts WOFF2, WOFF Arabic,
OTF Hebrew, variable WOFF2, two weights (2 faces loaded), `src` order with a
skipped format, invalid data fallback, oversized (331 KB) font fallback,
CORS (1 denied, 1 allowed), release on navigation (scoped faces released);
Notepad, Shell, Calculator, Snake, Kontrolki 4. Results:
`.bsf-test/bsf2-native/<mode>/bsf2-native.json` + screenshots.

Not automated natively: system/Terminal font persistence across reboot,
Settings font refresh, low-memory scenarios. Not tested: physical hardware,
Windows QEMU, installed-disk boot.

## Kernel image

`__kernel_end`: 0xD39000 (BSF 2.1 baseline built in a worktree) →
0xE9F000 (BSF 2.2), limit 15 MiB unchanged.
