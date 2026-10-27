# Atomic taskbar clock and Settings Support

## Root cause audited before implementation

The WM polls BWS9 and compares its formatted string, already limiting updates to
visible changes. But each change calls `RysujTaskbar`, which repaints the entire
bar with individual BWS21 rectangles, redraws Start/app buttons, clears the clock
pane, then calls the BSF renderer. The BSF path prepares glyph coverage after
that clear and issues separate BWS57 alpha blits. These mutate the live WM layer
between syscalls. BWS17 marks pending dirty regions for presentation; it is not
a transaction or a separate committed snapshot of those preceding writes.
The compositor can read that same layer while servicing independent damage.
Thus background-only pixels can be observed before replacement text is ready.
The clock's literal 150-unit width was stable, but every second unnecessarily
invalidated/repainted the full bar. BSF glyph masks themselves are not the cause.

Use the existing off-screen BursztynUI rendering target and a single BWS57 mode-2
region replacement under the existing screen lock. No compositor replacement or
reduced refresh rate is needed.

## Implemented clock path

`taskbar_clock.h` owns a reusable off-screen pixel surface. On a changed visible
time, it draws the complete raised theme background, top/left borders and
centered BSF text into that surface. `RysujZegar` then publishes it through
`gui_zastap_region` (BWS57 mode 2). The existing GUI region replacement uses the
screen lock; composition cannot observe a partially cleared replacement. The
existing compositor/backbuffer and dirty-region presentation remain in use.
There is no new compositor and no change to the clock's seconds cadence.

The clock requests damage only for its reserved rectangle, including its
borders. It does not redraw Start, application buttons or the desktop. Other
independent damage may still be coalesced by the existing compositor.
`RysujTaskbar` remains available for actual taskbar changes and includes a forced
clock draw when required.

Geometry is computed by `metryki_ekranu` after reading the real resolution and
when font, scale or theme metrics change. Width reserves six maximum digit
advances, two colons and padding, with the previous 150 logical pixels as a
minimum. Normal seconds updates never measure or recompute these bounds.
The visible-string comparison skips unchanged values; a failed clock read keeps
the previous valid value. A failed publication remains pending for retry.

BSF continues to rasterize antialiased TTF glyphs into the off-screen target.
Its existing color-independent glyph masks and cached font faces are reused.
Theme background and borders are restored before text within the same prepared
surface. Changing digit advances cannot move neighboring taskbar items.

## Native Settings Support

The BursztynUI navigation now contains Wygląd, Czcionki, System and Wsparcie.
The Support page uses `Panel`, `Etykieta`, `Karty`, `TabelaWirtualna` and
`Przycisk`; it does not introduce a parallel widget framework. Standard `Karty`
gained optional measured header wrapping so the long Polish tab stays reachable
without overlapping another header. The existing Settings scrollable viewport
preserves its logical minimum below the usual window size.

The exact runtime mapping is:

| Tab | Source asset and installed path |
| --- | --- |
| MECENASI | `dane/sponsorze/mecenasi.txt` → `/dane/sponsorze/mecenasi.txt` |
| SPONSORZY | `dane/sponsorze/sponsorzy.txt` → `/dane/sponsorze/sponsorzy.txt` |
| WKŁAD TWÓRCZY I RZECZOWY | `dane/sponsorze/wkladtworczy.txt` → `/dane/sponsorze/wkladtworczy.txt` |

These source files preserve existing repository data. The canonical sponsor
filename is `sponsorzy.txt`, as specified in the detailed FILE SOURCES and
acceptance requirements. Settings contains paths and labels, not supporter
names. It reads through normal filesystem size/fragment APIs. Entries are plain
display text, never paths, commands, HTML or URLs.

`support_data.h` limits each file to 65,536 bytes, each category to 256 entries,
and each entry to 160 UTF-8 bytes plus its terminator. It supports LF, CRLF,
standalone CR, an optional initial UTF-8 BOM, surrounding spaces/tabs and a final
line without a newline. Empty lines are skipped and source order is preserved.
BSF's bounded decoder validates UTF-8; malformed, truncated, control-containing
and overlong lines are rejected whole, avoiding partial UTF-8 characters. A
contained status reports rejected lines. Too many entries or oversized files
reject the replacement list.

Odśwież reloads all three categories independently into temporary storage and
publishes each successfully validated replacement. Missing files, failed/short
reads, size changes during reading and parsing failures preserve an existing
list, with an explanatory status. An initially missing file shows Brak danych;
an empty valid file shows Brak wpisów. Other categories continue working.
Temporary buffers are released, and strings are owned by the persistent model.
Live font changes preserve loaded data and active tab while rebuilding layout.

The three category models occupy approximately 121 KiB on the heap. Reloading
one category temporarily needs approximately 104 KiB (64 KiB input plus one
replacement category); categories reload sequentially. Release stack-usage
records show 96 bytes for `Settings::reload_support`, 128 bytes for `parse`,
and 512 bytes for clock surface update, excluding called functions. No complete
file or category array is placed on the stack. The clock at the default
150×40 rectangle reserves 24,000 pixel bytes; its buffer grows only as needed.

## Packaging and validation

`package_support.py` stages the three text files at the same logical ISO paths.
Kernel asset packaging follows the existing live/installed PSF file plan and
logs failures for these optional assets without making them boot requirements.
`verify_support_packaging.py` compares the exact source bytes with ISO files,
embedded kernel payloads and, optionally, installed PSF files. It also checks
that source supporter names are absent from `ustawienia.bin`.

Packaging validation was repeated after resuming on 2026-10-27: ISO, kernel and
the private installed disk all passed exact-byte comparison.

Release artifacts matched the recorded native acceptance runs:

* ISO SHA-256: `0ca255368d657e767e5b42d2a1aafb949b0eb7e5ca9c2ea9dd58b1883a630895`.
* Kernel SHA-256: `edff8b8d10a8defb5a6f78a0f52689199d773fb0315561d5b18bc3065758e39e`.

## Tests and evidence

The ASan/UBSan desktop integration suite includes bounded parser edge cases,
independent category mapping, missing/failed/short reads, safe replacement,
virtualized 256-row scrolling, Refresh activation and preservation of data
through font changes. Layout checks cover 12, 16, 20 and 24 px with
100%, 125%, 150% and 200% UI scale. Clock tests cover 09:59:59→10:00:00,
19:59:59→20:00:00 and 23:59:59→00:00:00, fixed bounds, unchanged-string skips,
complete background/text/antialiased pixels at publication and exactly one
region replacement without separate clearing GUI operations. Existing desktop
grid, Start search and Settings font-page checks remain in this suite.

Ordinary release QEMU acceptance completed for both live ISO and disk-only
installation. Each run sampled 100 consecutive taskbar screenshots spanning
20 distinct clock images. The sampler asserted nonempty clock text in every
sample, identical neighboring taskbar pixels and stable background/border
samples. Both runs passed, with no recorded guest fault. This is sampled frame
evidence; the locked single-publication path provides the atomicity guarantee
between samples.

Each QEMU run also exercised all three Support tabs, distinct rendered category
rows, Refresh, a live 16→24 px font update, another Refresh, and reopening
Settings with the same lists. These are pixel/mapping consistency checks, not
OCR assertions; exact file-to-category mapping is additionally checked by host
tests. The 12/20 px and scale combinations are covered by host layout tests.

Saved evidence (workspace-local, ignored build artifacts):

* `.bsf-test/clock-support-release-r2/result.json`, `clock-frames.json`,
  `clock-frames/` and `support-*.png`.
* `.bsf-test/clock-support-installed-9_lpcox7/native-r2/result.json`,
  `clock-frames.json`, `clock-frames/` and `support-*.png`.
* Installed fixture: `.bsf-test/clock-support-installed-9_lpcox7/target.img`.
* Resumed sanitizer logs: `.bsf-test/clock-support-resumed-host.log` and
  `.bsf-test/clock-support-resumed-regressions.log`.

Resumed ASan/UBSan results: desktop/Settings/Start **126,191 checks PASS**;
BSF **12,201 checks PASS**; BSF UI/browser **5,876 checks PASS**.
Compositor lifecycle/scene **74,293/74,293 PASS** and popup background
restoration **73,816/73,816 PASS**, including dirty geometry, cursor preservation
and theme/decor transactions. Python helper syntax and `git diff --check` also
passed. The earlier desktop-grid report retains the separate exact-anchor
restart evidence; it is not presented as a new QEMU run for this milestone.
WM click routing **214/214 PASS** (desktop, Start, launch, client PID, titlebar,
close, transparency, z-order, popup ownership, theme and redraw); user-fault
isolation policy **32,768 checks PASS**. Both resumed make invocations completed
successfully with no sanitizer findings.
