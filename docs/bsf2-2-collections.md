# BSF 2.2 — TrueType/OpenType collections (TTC/OTC)

## Container

`collection_faces()` validates the `ttcf` header (major 1 or 2, 1..256 faces,
header incl. the version-2 DSIG fields inside the file); `face_directory()`
validates one face: offset beyond the collection header, sfnt version,
1..64 table records, every table inside the file, no table overlapping the
collection header or this face's directory, no duplicate tags and no two
tables of the same face sharing bytes. Tables shared between faces (same
offset/length) are normal and expected. Both run over a bounded
`FileReader`, so the same validation is used for memory and for Ring 3 file
reads. Repeated face references (two face offsets pointing to one directory)
are listed once in the catalog.

## Face identity and selection

Every usable face is a catalog entry with id `FNV(path)` for face 0 and
`FNV(path + "#<index>")` otherwise — stable across refreshes and reboots.
`FontInfo` carries `face_index`, `face_count`, `container`. Faces are chosen
by family/weight/style like single-file fonts (Settings lists families; the
faces of a collection appear as styles of their family).

## Loading strategy (bounded, lazy, face-oriented)

| File | Discovery | Load for rendering |
|---|---|---|
| plain sfnt ≤ 16 MiB | whole file (BSF 2.1 path) | whole file |
| collection ≤ 4 MiB | whole file, every face validated | one whole-file buffer shared by all resident faces of the file |
| collection > 4 MiB or file > 16 MiB (≤ 64 MiB) | per face: directory + metadata tables only (`head hhea hmtx maxp cmap name OS/2 fvar avar STAT CPAL CBLC post`); outline tables recorded as present | only the tables BSF uses (no `vmtx`, `VORG`, `BASE`, hinting programs), read with BWS 59 ranged reads; each table is a reference-counted buffer keyed by (file, offset, length), so faces of one collection share `CFF `/`glyf`/`hmtx`/`cmap` |

Limits: 64 MiB per font file (BWS 59 bound), 16 MiB per table (one Ring 3
allocation), 40 MiB resident font data per process (LRU eviction of whole
faces), 4 resident faces, 256 faces per collection, 128 catalog entries.

Measured with the real Noto Sans CJK Regular collection (19.5 MB, 10 faces,
CFF table 15.4 MB shared by all faces) through the production runtime over a
mocked filesystem (`make test-bsf22-collections-host`): discovery reads
5.8 MB (192 ranged reads); the first face makes 16.2 MB resident; a second
face of the same collection adds 0.45 MB (7 shared-table hits).

## Fallback

The resolver distinguishes character coverage (cmap through the 4,096-scalar
coverage bitmap, then the face cmap), style/weight and face identity, and
works across individual collection faces. Cluster resolution prefers a face
that covers the whole grapheme cluster. Glyph coverage is not treated as
shaping support: CJK needs no script-specific shaping; Indic/SE-Asian
shaping is still not implemented regardless of coverage
(`bsf2supportmatrix.md`).

## Native delivery limits (blocker)

Fonts reach the native system as boot assets (Multiboot2 modules imported by
the kernel, ≤ 16 MiB per file and in total) or through Settings → Install
(whole-file copy, ≤ 16 MiB, no chunked write syscall exists in Ring 3).
Therefore a collection larger than 16 MiB (e.g. the full Noto Sans CJK TTC)
is supported by the loader and verified on the host, but cannot currently be
installed on a running Bursztyn OS. Lifting this needs a chunked/append file
write BWS call or a larger boot-import policy (owner decision); neither was
changed. The native test uses the CJK subset collection (246 KB, 4 faces).

## Tests

`make test-bsf22-collections-host` (19,122 checks): FreeType comparison of
every face of three collections (TrueType TTC, CFF OTC with shared tables,
mixed TrueType+CFF OTC: names, glyph counts, all outlines), malformed
headers (numFonts 0 / >256 / 0x7fffffff, version 3), invalid face offsets
(header, EOF, misaligned), repeated face references, huge table directories,
records past EOF / overlapping the directory / overlapping each other /
duplicate tags, truncation at every boundary, 4,000 mutations; runtime
catalog over `/czcionki/{ttf,otf,ttc,otc}`, stable ids, shared buffers,
removal/modification on refresh, CJK fallback, the large collection above.
