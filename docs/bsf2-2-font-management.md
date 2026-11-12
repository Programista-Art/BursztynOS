# BSF 2.2 — font discovery, catalog, Settings and the Terminal font

## Directories

| Directory | Extension | Notes |
|---|---|---|
| `/czcionki/ttf` | `.ttf` | BSF 2.1 location (required); also licence notices |
| `/czcionki/otf` | `.otf` | new |
| `/czcionki/ttc` | `.ttc` | new |
| `/czcionki/otc` | `.otc` | new |

Each directory accepts exactly its own extension (`bsf/asset_path.h`, shared
by Ring 0 and Ring 3); the format is then identified from the data. The
three new directories are created at boot and are optional for discovery.
Boot assets `bsf/<name>` are routed by extension; Settings → Install copies
`.ttf/.otf/.ttc/.otc` into the matching directory after validating **every**
face (≤ 16 MiB). Web fonts are never written to these directories.

## Catalog (`FontInfo`, appended fields)

family, style, full name, PostScript name (UTF-16BE `name` records decoded
with surrogate validation; invalid records rejected; Windows English
preferred over other records, so localized records never create duplicate
entries), file size, file id, collection face index/count, container
(sfnt/collection), outline technology, weight, width class, italic, mono,
serif, axis count, named-instance count, colour tables (COLR/CBDT/sbix),
directory, load status (`FontOk` / `FontLoadFailed`), coverage bitmap.
Writing-system support is reported by Settings from actual per-face cmap
coverage of script samples (Latin, Cyrillic, Greek, Hebrew, Arabic,
Japanese, Chinese, Korean), not from metadata.

Limits: 128 faces, 1,024 directory records per directory, 64 KiB listing.

## Refresh (Settings → Fonts → Odśwież fonty)

`BSF::rescan()` re-lists all directories, re-validates every file, detects
new/removed/modified fonts (a file that no longer validates disappears),
bumps `generation()` (owners of layouts rebuild), clears glyph/colour caches
and releases all resident face data (no dangling face pointers: callers hold
ids, not pointers). Identifiers are stable (path / path#index), so valid
preferences survive. A selected system font that disappeared falls back to
the default face with the status "Wybrana czcionka zniknęła…"; a removed
Terminal font falls back to automatic.

## Settings → Fonts (BursztynUI, existing style)

- Family list (collection faces appear as styles of their family).
- Details line: outline technology (TrueType / OpenType CFF / CFF2), number
  of styles, collection face *i/n*, file size, axis count, colour tables,
  validation status and the writing systems the face itself covers.
- Preview samples: Polish, English, Cyrillic, Greek, combining marks,
  Hebrew, Arabic, Japanese, Chinese, Korean. A sample the selected face
  cannot render itself is replaced by "— brak glifów w tej czcionce —"
  instead of silently showing a fallback font. Indic/SE-Asian samples are
  not shown (no script shaping).
- Separate **Terminal font** list (automatic, or families passing the grid
  policy).
- Install accepts `.ttf/.otf/.ttc/.otc`.
- No full font-info window was added (the details line carries the data);
  axis sliders are not provided.

## System and Terminal fonts (Config v2)

`BSF::Config` v2 appends `terminal_family` (+ reserved = 0) to the v1 record;
the checksum covers the new fields. The kernel transport accepts only valid
v2 records and migrates a 24-byte v1 `/system/bsf.cfg` (system font and size
kept, Terminal automatic). `apply()` rejects unknown face ids.

Terminal policy: the Shell's grid renderer uses (1) the session override of
the `czcionka` command, else (2) the configured Terminal font, else (3)
automatic monospace. `terminal_face()` accepts a face only if every
printable ASCII glyph has the same unrounded advance; a proportional or
removed face never reaches the grid (fallback to automatic), so cell
coordinates, cursor addressing, logical cell order, history, escape
handling and copy/paste are unchanged. Settings only offers families that
pass this check. The interface font continues to apply to BursztynUI
(labels, menus, buttons, dialogs, Explorer, apps) without touching
application document fonts.

## Tests

`make test-bsf22-font-catalog-host` (307 checks): v1→v2 migration and v2
validation, catalog over four directories, metadata (UTF-8 names, no
duplicates, weights/widths), per-face coverage, Terminal policy and
persistence, independent system/Terminal fonts, refresh with new / modified
(invalid) / removed fonts, disappearing selections, stable ids, corrupt
directory entries. Kernel side: `make test-desktop-settings-host`
(authorization of the new directories and read bound), `test-psf-bootstrap-host`.
