#pragma once
// BSF 2.0 text layout primitives: Unicode 15.1.0 properties and UAX #9.
// Freestanding, userspace-only, integer-only. Every entry point is bounded.
#include <stddef.h>
#include <stdint.h>
namespace BSF {
// ---------------------------------------------------------------- properties
enum class BidiClass : uint8_t {
  L, R, AL, EN, ES, ET, AN, CS, NSM, BN, B, S, WS, ON,
  LRE, LRO, RLE, RLO, PDF, LRI, RLI, FSI, PDI
};
BidiClass bidi_class(uint32_t cp);
// Bidi_Mirroring_Glyph, or 0 when the scalar has no mirrored counterpart.
uint32_t bidi_mirror(uint32_t cp);
// Bidi_Paired_Bracket; type: 0 none, 1 open, 2 close.
uint32_t bidi_bracket(uint32_t cp, unsigned &type);
enum class Joining : uint8_t { U, R, D, C, L, T };
Joining joining_type(uint32_t cp);
// Script property index (0 Zyyy/Common, 1 Zinh/Inherited, 2 Zzzz/Unknown).
uint8_t script(uint32_t cp);
const char *script_code(uint8_t script); // ISO 15924, e.g. "Arab"
uint32_t script_tag(uint8_t script);     // OpenType script tag, e.g. 'arab'
enum : uint8_t { ScriptCommon = 0, ScriptInherited = 1, ScriptUnknown = 2 };
// UAX #14 Line_Break property (value order in generate_bsf_layout_data.py).
enum class LineBreak : uint8_t {
  XX, BK, CR, LF, NL, SP, ZW, GL, CM, ZWJ, WJ, BA, BB, HY, CB, CL, CP, EX, IN,
  NS, OP, QU, IS, NU, PO, PR, SY, AI, AL, CJ, EB, EM, H2, H3, HL, ID, JL, JV,
  JT, RI, SA, SG, AK, AP, AS, VF, VI, B2
};
LineBreak line_break(uint32_t cp);
unsigned mark_category(uint32_t cp); // 0 none, 1 Mn, 2 Mc, 3 Me
// Canonical two-scalar (or singleton, b=0) decomposition, including Hangul.
bool decompose(uint32_t cp, uint32_t &a, uint32_t &b);
// Canonical primary composite, or 0. Excludes composition exclusions.
uint32_t compose(uint32_t a, uint32_t b);
bool strong_rtl(uint32_t cp); // R or AL
bool needs_complex_layout(const char *utf8, size_t bytes);

// ------------------------------------------------------------------ UAX #9
constexpr size_t MaxBidiLength = 65536;  // scalars per paragraph
constexpr uint8_t BidiMaxDepth = 125;
constexpr uint8_t BidiRemoved = 0xff;    // X9-removed (BN/embedding) level
enum class BaseDirection : uint8_t { LTR = 0, RTL = 1, Auto = 2 };
// P2/P3 over classes[begin,end), skipping isolate content. Returns 0, 1 or
// `fallback` when no strong character is found.
uint8_t resolve_paragraph_level(const uint8_t *classes, size_t n,
                                uint8_t fallback = 0);
// X1-I2 for one paragraph (a B may only appear as the final scalar; use
// resolve_bidi_text to split text into paragraphs). `text` may be null, in
// which case N0 bracket pairing is skipped (BidiTest class-only input).
// levels[i] receives the resolved embedding level or BidiRemoved.
bool resolve_bidi_levels(const uint32_t *text, const uint8_t *classes,
                         size_t n, BaseDirection base, uint8_t *levels,
                         uint8_t &paragraph_level);
// L1 for one line [begin,end) of a resolved paragraph; out has end-begin
// entries. Removed scalars stay BidiRemoved.
void bidi_line_levels(const uint8_t *classes, const uint8_t *levels,
                      size_t begin, size_t end, uint8_t paragraph_level,
                      uint8_t *out);
// Replace BidiRemoved levels with the preceding level (or paragraph level),
// so zero-width controls travel with their neighbours during reordering.
void bidi_fill_removed(uint8_t *levels, size_t n, uint8_t paragraph_level);
// L2: visual_to_logical receives indices of non-removed entries in visual
// order. Returns the number written. O(n * depth), no allocation.
size_t bidi_reorder(const uint8_t *levels, size_t n,
                    uint32_t *visual_to_logical);
struct BidiRun {
  uint32_t begin{}, end{}; // logical scalar indices [begin,end)
  uint8_t level{};
  bool rtl() const { return level & 1; }
};
// Maximal same-level runs; returns count (<= capacity).
size_t bidi_runs(const uint8_t *levels, size_t n, BidiRun *runs,
                 size_t capacity);
// L2 at run granularity: order receives run indices in visual order.
void bidi_reorder_runs(const BidiRun *runs, size_t count, uint32_t *order);

// Owning analysis of one UTF-8 paragraph sequence. Logical text is never
// modified; every scalar keeps the byte offset it was decoded from.
struct BidiText {
  uint32_t *cp{};      // decoded scalars (U+FFFD for malformed bytes)
  uint32_t *offset{};  // byte offset of each scalar, offset[n] == bytes
  uint8_t *cls{};      // original Bidi_Class
  uint8_t *level{};    // resolved levels (BidiRemoved for X9 removals)
  size_t n{};
  uint8_t base_level{}; // level of the first paragraph
  BaseDirection direction{BaseDirection::Auto};
  BidiText() = default;
  ~BidiText();
  BidiText(const BidiText &) = delete;
  BidiText &operator=(const BidiText &) = delete;
  bool analyze(const char *utf8, size_t bytes, BaseDirection base);
  void clear();
  size_t scalar_at(size_t byte) const; // scalar index containing byte
};
// Logical<->visual maps for one line of a BidiText, at scalar granularity.
// logical_to_visual/visual_to_logical have (end-begin) entries; X9-removed
// scalars inherit their neighbour's level so every scalar is mapped.
bool bidi_line_map(const BidiText &, size_t begin, size_t end,
                   uint32_t *visual_to_logical, uint32_t *logical_to_visual);
} // namespace BSF
