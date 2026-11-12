# BSF 2.2 — WOFF 1.0 and WOFF2

Code: `bsf/webfont.cpp`/`webfont.h` (container decoding), `bsf/inflate.inc`
(zlib), `bsf/vendor/brotli` (Brotli decoder). Both containers decode into a
plain sfnt that is then validated by `Face::open` like any local font; there
is no WOFF-specific rasterizer, shaper or layout path. Linked into the Husarz
resource worker (web fonts are browser-scoped) and the host tests only.

## WOFF 1.0

Header (signature, length = file size, reserved = 0, table count ≤ 64,
totalSfntSize ≤ limit), table directory (tags strictly ascending, 4-byte
aligned offsets inside the file and outside the header/directory,
compLength ≤ origLength, no overlap between tables or with the
metadata/private blocks), metadata/private blocks bounds-checked (not
decompressed: BSF does not use them). A pre-pass computes the declared
output size and the decompression ratio **before any allocation**. Each
compressed table must inflate to exactly `origLength` bytes (Adler-32
checked); stored tables are copied. Flavour `ttcf` (WOFF collections) is
reported as unsupported.

## WOFF2

Header (48 bytes), directory with known-tag indices and explicit tags,
UIntBase128 lengths (no leading zero byte, ≤ 2^32−1), transform versions
(glyf/loca: 0 = transformed, 3 = null; hmtx: 1 = transformed; any other
transform → unsupported), loca transformLength must be 0, glyf and loca
transformed together, hmtx only with transformed glyf. The Brotli stream is
decoded with BSF's bounded allocator (40 MiB budget) into a buffer of
exactly Σ transformLength bytes; an incomplete or over-long stream fails.

Reconstruction (W3C WOFF2 §5): `glyf`/`loca` from the seven streams
(nContour, nPoints, flags, glyph, composite, bbox incl. bitmap,
instruction) with the triplet encoding, explicit or computed bboxes,
composite records (bbox required), instructions, overlapSimpleBitmap
(OVERLAP_SIMPLE flag), loca in the declared index format (head
`indexToLocFormat` updated accordingly); `hmtx` transform 1 with
proportional/monospaced left side bearings taken from the reconstructed
`xMin`. Limits: 65,535 points/contours per glyph, 1,024 components,
output ≤ `max_output`. Flavour `ttcf` (WOFF2 collections) is not supported.

## Brotli (third-party)

google/brotli **v1.1.0**, MIT, decoder files only, unmodified
(`bsf/vendor/brotli/SOURCE.txt` records the tarball SHA-256
`e720a6ca29428b803f4ad165371771f5398faba397edf6778837a18599ea13ff`,
`LICENSE` preserved). No encoder, no shared libraries; uses only
`memcpy`/`memset`; Ring 3 provides refusing `malloc`/`free` stubs because the
decoder is always created with BSF's allocator. Built as C99 with the Ring 3
flags for the worker and with ASan/UBSan for host tests and fuzzing.

## Limits (WebFontLimits)

input ≤ 16 MiB (Husarz additionally ≤ 240 KiB per resource), output ≤
16 MiB (Husarz worker: the 1.79 MB shared-memory font area), decoded/input
ratio ≤ 128, 64 tables.

## Verification

`make test-bsf22-woff-host` (62,085 checks): 10 fixtures made by fontTools
from OFL fonts (TrueType Latin, CFF Noto Sans, variable Hebrew with
overlap bitmap, Noto Naskh Arabic; WOFF, WOFF2 with glyf/loca+hmtx
transforms, WOFF2 null transform). Every decoded font is compared with its
source: all untransformed tables byte-identical (head: only
checkSumAdjustment, the modified date written by the encoder, WOFF2 flag
bit 11 and indexToLocFormat may differ; `DSIG` is dropped by WOFF2
encoders), every glyph outline equal to FreeType on the source font, every
advance/side bearing equal; FreeType also accepts the decoded sfnt.
Adversarial: Brotli bomb (32 MiB of zeros in 103 bytes) → decompress-failed,
zlib table larger than declared → malformed, declared sizes far above the
input → too-large before allocation, wrong length/reserved/alignment/tag
order, UIntBase128 leading zero, WOFF2 collections, truncation at ~100
points per file and 7,500 random mutations (every successful decode
re-validated) under ASan/UBSan. Fuzzing: `make fuzz-bsf22`
(`fuzz_bsf22_webfont`, see the test report).
