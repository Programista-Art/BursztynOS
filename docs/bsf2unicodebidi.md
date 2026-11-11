# BSF 2.0 — Unicode Bidirectional Algorithm (UAX #9)

Implementation: `bsf/bidi.cpp`, API in `bsf/text.h`. Unicode **15.1.0**,
the same version as the existing grapheme data.

## Conformance

| Official file | Cases | Result |
|---|---|---|
| `BidiTest.txt` 15.1.0 (all three paragraph-direction bits) | 770,241 | **770,241 pass, 0 fail** |
| `BidiCharacterTest.txt` 15.1.0 (levels, paragraph level, order) | 91,707 | **91,707 pass, 0 fail** |

Run: `make test-bsf-bidi-host` (ASan + UBSan, ~20 s). Comparison is exact
for resolved levels (`x` for X9-removed characters) after L1, and for the L2
visual order excluding removed characters. Only after both files passed was
the implementation described as conformant.

## Rules implemented

- P2/P3 paragraph level with isolate skipping; explicit LTR/RTL override.
- X1–X8 with the 125-level directional status stack, overflow isolate /
  overflow embedding counters, valid isolate count; FSI direction from its
  isolate content (BD9 matching computed once per paragraph).
- X9 removal (embeddings, overrides, PDF, BN) via a filtered index list —
  no characters are deleted from the logical text.
- X10 level runs and isolating run sequences (BD13) with sos/eos computed
  from **explicit** embedding levels (a bug where implicit levels leaked into
  sos/eos was found by BidiTest and fixed).
- W1–W7, N0 with BD16 bracket pairing (63-entry stack, canonical
  equivalents U+2329/U+232A ≡ U+3008/U+3009, NSM following a resolved
  bracket), N1/N2, I1/I2.
- L1 (segment/paragraph separators, trailing whitespace and isolate
  formatting characters) per **line**, L2 reordering per line.
- L4 mirroring is applied in shaping (`Bidi_Mirroring_Glyph` or `rtlm`).

## Data model

`BidiText` keeps, per scalar: code point, source byte offset (with a
sentinel `offset[n] == bytes`), original Bidi_Class and resolved level. Text
is split into paragraphs at class B; each paragraph is resolved
independently with the caller's base direction (or P2/P3 when automatic).
The logical UTF-8 is never modified — tests assert byte equality before and
after analysis. Malformed UTF-8 decodes to U+FFFD per invalid byte.

## Safety properties

- Iterative only (no recursion); one allocation per paragraph (O(n)).
- Inputs larger than 65,536 scalars are rejected, not truncated.
- Directional depth is clamped by the standard's own overflow rules
  (a 400×RLE test ends at level 125/126).
- Invalid class values are rejected.
- Deterministic random testing: 20,000 mixed-class rounds in the unit test,
  plus the coverage-guided `fuzz_bsf_bidi` target.

## Known limitations

- Paragraph direction in Notepad is resolved per visual (wrapped) line,
  because each visual line is drawn as its own string; a wrapped RTL
  paragraph whose continuation line starts with LTR text will resolve that
  line as LTR.
- RTL paragraphs are not right-aligned in BursztynUI/Notepad (the layout
  supports `align_to_direction`; widgets do not request it yet).
