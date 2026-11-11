// BSF 2.0 Unicode Bidirectional Algorithm (UAX #9, Unicode 15.1.0).
// Iterative, allocation-bounded (one workspace per paragraph, O(n)), and
// never mutates logical text. Verified against BidiTest.txt and
// BidiCharacterTest.txt by testy/test_bsf_bidi.cpp.
#include "text.h"
#include "bsf.h"
#include <new>
#ifdef BSF_NATIVE
#define BSF_NEW new
#else
#define BSF_NEW new (std::nothrow)
#endif
namespace BSF {
namespace {
enum : uint8_t {
  L, R, AL, EN, ES, ET, AN, CS, NSM, BN, B, S, WS, ON,
  LRE, LRO, RLE, RLO, PDF, LRI, RLI, FSI, PDI, None = 0xfe
};
inline bool removed_x9(uint8_t t) {
  return t == LRE || t == RLE || t == LRO || t == RLO || t == PDF || t == BN;
}
inline bool initiator(uint8_t t) { return t == LRI || t == RLI || t == FSI; }
inline bool neutral_or_isolate(uint8_t t) {
  return t == B || t == S || t == WS || t == ON || initiator(t) || t == PDI;
}
// Strong direction for N0/N1: EN and AN behave as R.
inline uint8_t strong_for_neutrals(uint8_t t) {
  if (t == L)
    return L;
  if (t == R || t == AL || t == EN || t == AN)
    return R;
  return None;
}
inline uint32_t canonical_bracket(uint32_t cp) {
  // U+2329/U+232A are canonically equivalent to U+3008/U+3009 (BD16).
  return cp == 0x2329 ? 0x3008 : cp == 0x232a ? 0x3009 : cp;
}
// Workspace carved from one allocation; all arrays are n entries.
struct Work {
  uint8_t *type{};      // working types
  uint8_t *embedding{}; // explicit levels from X1-X8 (sos/eos source)
  int32_t *match{};     // BD9 matching PDI / initiator, or -1
  int32_t *filtered{};  // indices of non-removed scalars
  int32_t *runs{};      // level-run start positions in filtered (+ sentinel)
  int32_t *seq{};       // current isolating run sequence (indices)
  int32_t *stack{};     // scratch stack (isolate matching / brackets)
  int32_t *pair{};      // per seq position: closing position of bracket pair
  uint8_t *storage{};
  bool allocate(size_t n) {
    size_t bytes = n * (2 + 4 * 7) + 64 * 4 + 16;
    storage = BSF_NEW uint8_t[bytes];
    if (!storage)
      return false;
    uint8_t *p = storage;
    auto take = [&](size_t z) {
      uint8_t *r = p;
      p += (z + 3) & ~size_t(3);
      return r;
    };
    match = reinterpret_cast<int32_t *>(take(n * 4));
    filtered = reinterpret_cast<int32_t *>(take(n * 4));
    runs = reinterpret_cast<int32_t *>(take((n + 1) * 4));
    seq = reinterpret_cast<int32_t *>(take(n * 4));
    stack = reinterpret_cast<int32_t *>(take(n * 4));
    pair = reinterpret_cast<int32_t *>(take(n * 4));
    type = take(n);
    embedding = take(n);
    return true;
  }
  ~Work() { delete[] storage; }
};
// P2/P3 over [begin,end) with isolate skipping driven by `match`.
uint8_t first_strong(const uint8_t *cls, const int32_t *match, size_t begin,
                     size_t end) {
  for (size_t i = begin; i < end; ++i) {
    uint8_t t = cls[i];
    if (t == L)
      return 0;
    if (t == R || t == AL)
      return 1;
    if (initiator(t)) {
      if (match[i] < 0)
        return None;
      i = size_t(match[i]);
    } else if (t == B)
      break;
  }
  return None;
}
void match_isolates(const uint8_t *cls, size_t n, int32_t *match,
                    int32_t *stack) {
  size_t depth = 0;
  for (size_t i = 0; i < n; ++i) {
    match[i] = -1;
    uint8_t t = cls[i];
    if (initiator(t))
      stack[depth++] = int32_t(i);
    else if (t == PDI && depth) {
      int32_t j = stack[--depth];
      match[j] = int32_t(i);
      match[i] = j;
    } else if (t == B)
      depth = 0;
  }
}
struct Sequence {
  int32_t *idx;
  size_t len;
  uint8_t level, sos, eos;
};
void resolve_sequence(Work &w, const uint32_t *text, const uint8_t *cls,
                      Sequence s, uint8_t *levels) {
  uint8_t *t = w.type;
  const int32_t *x = s.idx;
  const size_t n = s.len;
  // W1
  uint8_t prev = s.sos;
  for (size_t k = 0; k < n; ++k) {
    uint8_t &c = t[x[k]];
    if (c == NSM)
      c = (initiator(prev) || prev == PDI) ? uint8_t(ON) : prev;
    prev = c;
  }
  // W2, W3
  uint8_t strong = s.sos;
  for (size_t k = 0; k < n; ++k) {
    uint8_t &c = t[x[k]];
    if (c == L || c == R || c == AL)
      strong = c;
    else if (c == EN && strong == AL)
      c = AN;
  }
  for (size_t k = 0; k < n; ++k)
    if (t[x[k]] == AL)
      t[x[k]] = R;
  // W4
  for (size_t k = 1; k + 1 < n; ++k) {
    uint8_t c = t[x[k]], a = t[x[k - 1]], b = t[x[k + 1]];
    if (c == ES && a == EN && b == EN)
      t[x[k]] = EN;
    else if (c == CS && a == b && (a == EN || a == AN))
      t[x[k]] = a;
  }
  // W5
  for (size_t k = 0; k < n;) {
    if (t[x[k]] != ET) {
      ++k;
      continue;
    }
    size_t e = k;
    while (e < n && t[x[e]] == ET)
      ++e;
    bool en = (k && t[x[k - 1]] == EN) || (e < n && t[x[e]] == EN);
    if (en)
      for (size_t j = k; j < e; ++j)
        t[x[j]] = EN;
    k = e;
  }
  // W6
  for (size_t k = 0; k < n; ++k) {
    uint8_t &c = t[x[k]];
    if (c == ES || c == ET || c == CS)
      c = ON;
  }
  // W7
  strong = s.sos;
  for (size_t k = 0; k < n; ++k) {
    uint8_t &c = t[x[k]];
    if (c == L || c == R)
      strong = c;
    else if (c == EN && strong == L)
      c = L;
  }
  const uint8_t e = (s.level & 1) ? R : L;
  // N0: BD16 bracket pairs, processed in order of opening position.
  if (text) {
    for (size_t k = 0; k < n; ++k)
      w.pair[k] = -1;
    size_t depth = 0;
    int32_t *open_pos = w.stack;        // positions
    uint32_t pending_close[63];         // expected closing bracket
    for (size_t k = 0; k < n; ++k) {
      int32_t i = x[k];
      if (t[i] != ON)
        continue;
      unsigned kind = 0;
      uint32_t other = bidi_bracket(text[i], kind);
      if (kind == 1) {
        if (depth == 63)
          break; // BD16: stop processing this sequence.
        pending_close[depth] = canonical_bracket(other);
        open_pos[depth++] = int32_t(k);
      } else if (kind == 2) {
        uint32_t self = canonical_bracket(text[i]);
        for (size_t d = depth; d--;)
          if (pending_close[d] == self) {
            w.pair[open_pos[d]] = int32_t(k);
            depth = d;
            break;
          }
      }
    }
    for (size_t k = 0; k < n; ++k) {
      if (w.pair[k] < 0)
        continue;
      size_t close = size_t(w.pair[k]);
      bool found_e = false, found_o = false;
      for (size_t j = k + 1; j < close; ++j) {
        uint8_t d = strong_for_neutrals(t[x[j]]);
        if (d == e)
          found_e = true;
        else if (d != None)
          found_o = true;
        if (found_e)
          break;
      }
      uint8_t set = None;
      if (found_e)
        set = e;
      else if (found_o) {
        uint8_t ctx = s.sos;
        for (size_t j = k; j--;) {
          uint8_t d = strong_for_neutrals(t[x[j]]);
          if (d != None) {
            ctx = d;
            break;
          }
        }
        set = ctx != e ? ctx : e;
      }
      if (set == None)
        continue;
      t[x[k]] = t[x[close]] = set;
      for (size_t j = k + 1; j < n && cls[x[j]] == NSM; ++j)
        t[x[j]] = set;
      for (size_t j = close + 1; j < n && cls[x[j]] == NSM; ++j)
        t[x[j]] = set;
    }
  }
  // N1, N2
  for (size_t k = 0; k < n;) {
    if (!neutral_or_isolate(t[x[k]])) {
      ++k;
      continue;
    }
    size_t end = k;
    while (end < n && neutral_or_isolate(t[x[end]]))
      ++end;
    uint8_t before = k ? strong_for_neutrals(t[x[k - 1]]) : s.sos;
    uint8_t after = end < n ? strong_for_neutrals(t[x[end]]) : s.eos;
    uint8_t set = before == after && before != None ? before : e;
    for (size_t j = k; j < end; ++j)
      t[x[j]] = set;
    k = end;
  }
  // I1, I2
  for (size_t k = 0; k < n; ++k) {
    int32_t i = x[k];
    uint8_t c = t[i], lv = levels[i];
    if (!(lv & 1)) {
      if (c == R)
        lv += 1;
      else if (c == AN || c == EN)
        lv += 2;
    } else if (c == L || c == EN || c == AN)
      lv += 1;
    levels[i] = lv;
  }
}
} // namespace

uint8_t resolve_paragraph_level(const uint8_t *cls, size_t n,
                                uint8_t fallback) {
  if (!cls || n > MaxBidiLength)
    return fallback;
  // Inline isolate skipping without a workspace: count nesting depth.
  size_t depth = 0;
  for (size_t i = 0; i < n; ++i) {
    uint8_t t = cls[i];
    if (initiator(t))
      ++depth;
    else if (t == PDI) {
      if (depth)
        --depth;
    } else if (t == B)
      break;
    else if (!depth && t == L)
      return 0;
    else if (!depth && (t == R || t == AL))
      return 1;
  }
  return fallback;
}

bool resolve_bidi_levels(const uint32_t *text, const uint8_t *cls, size_t n,
                         BaseDirection base, uint8_t *levels,
                         uint8_t &paragraph_level) {
  paragraph_level = base == BaseDirection::RTL ? 1 : 0;
  if (!n)
    return true;
  if (!cls || !levels || n > MaxBidiLength)
    return false;
  for (size_t i = 0; i < n; ++i)
    if (cls[i] > PDI)
      return false;
  Work w;
  if (!w.allocate(n))
    return false;
  match_isolates(cls, n, w.match, w.stack);
  if (base == BaseDirection::Auto) {
    uint8_t d = first_strong(cls, w.match, 0, n);
    paragraph_level = d == None ? 0 : d;
  }
  const uint8_t para = paragraph_level;
  // X1-X8: directional status stack (BD8), bounded at max_depth + 2.
  struct Status { uint8_t level, override_type; bool isolate; };
  Status stack[BidiMaxDepth + 2];
  size_t top = 0;
  stack[0] = {para, None, false};
  unsigned overflow_isolate = 0, overflow_embedding = 0, valid_isolate = 0;
  for (size_t i = 0; i < n; ++i) {
    uint8_t t = cls[i];
    w.type[i] = t;
    Status &cur = stack[top];
    auto least = [&](bool odd) {
      uint8_t lv = cur.level;
      return uint8_t(odd ? ((lv + 1) | 1) : ((lv + 2) & ~1));
    };
    switch (t) {
    case RLE: case LRE: case RLO: case LRO: {
      uint8_t next = least(t == RLE || t == RLO);
      levels[i] = cur.level;
      if (next <= BidiMaxDepth && !overflow_isolate && !overflow_embedding) {
        stack[++top] = {next, uint8_t(t == RLO ? R : t == LRO ? L : None), false};
      } else if (!overflow_isolate)
        ++overflow_embedding;
      break;
    }
    case RLI: case LRI: case FSI: {
      levels[i] = cur.level;
      if (cur.override_type != None)
        w.type[i] = cur.override_type;
      bool rtl = t == RLI;
      if (t == FSI) {
        size_t end = w.match[i] >= 0 ? size_t(w.match[i]) : n;
        rtl = first_strong(cls, w.match, i + 1, end) == 1;
      }
      uint8_t next = least(rtl);
      if (next <= BidiMaxDepth && !overflow_isolate && !overflow_embedding) {
        ++valid_isolate;
        stack[++top] = {next, None, true};
      } else
        ++overflow_isolate;
      break;
    }
    case PDI:
      if (overflow_isolate)
        --overflow_isolate;
      else if (valid_isolate) {
        overflow_embedding = 0;
        while (!stack[top].isolate && top)
          --top;
        if (top)
          --top;
        --valid_isolate;
      }
      levels[i] = stack[top].level;
      if (stack[top].override_type != None)
        w.type[i] = stack[top].override_type;
      break;
    case PDF:
      levels[i] = cur.level;
      if (overflow_isolate) {
      } else if (overflow_embedding)
        --overflow_embedding;
      else if (!cur.isolate && top)
        --top;
      break;
    case B:
      levels[i] = para;
      break;
    case BN:
      levels[i] = cur.level;
      break;
    default:
      levels[i] = cur.level;
      if (cur.override_type != None)
        w.type[i] = cur.override_type;
    }
  }
  // X9: work only on non-removed scalars.
  size_t m = 0;
  for (size_t i = 0; i < n; ++i)
    if (!removed_x9(cls[i]))
      w.filtered[m++] = int32_t(i);
  for (size_t i = 0; i < n; ++i)
    w.embedding[i] = levels[i];
  // X10: level runs over the filtered sequence.
  size_t runs = 0;
  for (size_t p = 0; p < m; ++p)
    if (!p || w.embedding[w.filtered[p]] != w.embedding[w.filtered[p - 1]])
      w.runs[runs++] = int32_t(p);
  w.runs[runs] = int32_t(m);
  // Run lookup by starting scalar, via `stack` reused as scalar->run map.
  for (size_t i = 0; i < n; ++i)
    w.stack[i] = -1;
  for (size_t r = 0; r < runs; ++r)
    w.stack[w.filtered[w.runs[r]]] = int32_t(r);
  for (size_t r = 0; r < runs; ++r) {
    int32_t first = w.filtered[w.runs[r]];
    if (cls[first] == PDI && w.match[first] >= 0)
      continue; // continuation of an earlier isolating run sequence
    size_t len = 0, run = r;
    for (;;) {
      for (int32_t p = w.runs[run]; p < w.runs[run + 1]; ++p)
        w.seq[len++] = w.filtered[p];
      int32_t last = w.seq[len - 1];
      if (!initiator(cls[last]) || w.match[last] < 0)
        break;
      int32_t next = w.stack[w.match[last]];
      if (next < 0)
        break; // matching PDI shares this level run (overflow case)
      run = size_t(next);
    }
    uint8_t level = w.embedding[w.seq[0]];
    // sos / eos from adjacent non-removed scalars.
    size_t first_pos = size_t(w.runs[r]);
    uint8_t before = first_pos ? w.embedding[w.filtered[first_pos - 1]] : para;
    int32_t last = w.seq[len - 1];
    uint8_t after = para;
    if (!initiator(cls[last])) {
      // position of last in filtered: binary search (filtered is sorted)
      size_t lo = 0, hi = m;
      while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (w.filtered[mid] < last)
          lo = mid + 1;
        else
          hi = mid;
      }
      if (lo + 1 < m)
        after = w.embedding[w.filtered[lo + 1]];
    }
    Sequence s{w.seq, len, level,
               uint8_t(((before > level ? before : level) & 1) ? R : L),
               uint8_t(((after > level ? after : level) & 1) ? R : L)};
    resolve_sequence(w, text, cls, s, levels);
  }
  for (size_t i = 0; i < n; ++i)
    if (removed_x9(cls[i]))
      levels[i] = BidiRemoved;
  return true;
}

void bidi_line_levels(const uint8_t *cls, const uint8_t *levels, size_t begin,
                      size_t end, uint8_t para, uint8_t *out) {
  if (!cls || !levels || !out || end < begin)
    return;
  for (size_t i = begin; i < end; ++i)
    out[i - begin] = levels[i];
  auto whitespace = [&](size_t i) {
    uint8_t t = cls[i];
    return t == WS || initiator(t) || t == PDI || removed_x9(t);
  };
  auto reset_before = [&](size_t i) {
    while (i > begin && whitespace(i - 1)) {
      --i;
      if (out[i - begin] != BidiRemoved)
        out[i - begin] = para;
    }
  };
  for (size_t i = begin; i < end; ++i)
    if (cls[i] == S || cls[i] == B) {
      out[i - begin] = para;
      reset_before(i);
    }
  reset_before(end);
}

void bidi_fill_removed(uint8_t *levels, size_t n, uint8_t para) {
  if (!levels)
    return;
  uint8_t previous = para;
  for (size_t i = 0; i < n; ++i) {
    if (levels[i] == BidiRemoved)
      levels[i] = previous;
    else
      previous = levels[i];
  }
}

size_t bidi_reorder(const uint8_t *levels, size_t n, uint32_t *order) {
  if (!levels || !order)
    return 0;
  size_t m = 0;
  uint8_t high = 0, low_odd = 0xff;
  for (size_t i = 0; i < n; ++i) {
    uint8_t lv = levels[i];
    if (lv == BidiRemoved)
      continue;
    order[m++] = uint32_t(i);
    if (lv > high)
      high = lv;
    if ((lv & 1) && lv < low_odd)
      low_odd = lv;
  }
  if (low_odd == 0xff)
    return m;
  for (unsigned lv = high; lv >= low_odd; --lv) {
    for (size_t k = 0; k < m;) {
      if (levels[order[k]] < lv) {
        ++k;
        continue;
      }
      size_t e = k;
      while (e < m && levels[order[e]] >= lv)
        ++e;
      for (size_t a = k, b = e - 1; a < b; ++a, --b) {
        uint32_t tmp = order[a];
        order[a] = order[b];
        order[b] = tmp;
      }
      k = e;
    }
    if (!lv)
      break;
  }
  return m;
}

size_t bidi_runs(const uint8_t *levels, size_t n, BidiRun *runs,
                 size_t capacity) {
  if (!levels || !runs)
    return 0;
  size_t count = 0;
  for (size_t i = 0; i < n && count < capacity;) {
    size_t e = i + 1;
    while (e < n && levels[e] == levels[i])
      ++e;
    runs[count++] = {uint32_t(i), uint32_t(e), levels[i]};
    i = e;
  }
  return count;
}

void bidi_reorder_runs(const BidiRun *runs, size_t count, uint32_t *order) {
  if (!runs || !order)
    return;
  uint8_t high = 0, low_odd = 0xff;
  for (size_t i = 0; i < count; ++i) {
    order[i] = uint32_t(i);
    if (runs[i].level > high)
      high = runs[i].level;
    if ((runs[i].level & 1) && runs[i].level < low_odd)
      low_odd = runs[i].level;
  }
  if (low_odd == 0xff)
    return;
  for (unsigned lv = high; lv >= low_odd; --lv) {
    for (size_t k = 0; k < count;) {
      if (runs[order[k]].level < lv) {
        ++k;
        continue;
      }
      size_t e = k;
      while (e < count && runs[order[e]].level >= lv)
        ++e;
      for (size_t a = k, b = e - 1; a < b; ++a, --b) {
        uint32_t tmp = order[a];
        order[a] = order[b];
        order[b] = tmp;
      }
      k = e;
    }
    if (!lv)
      break;
  }
}

// ----------------------------------------------------------- BidiText
BidiText::~BidiText() { clear(); }
void BidiText::clear() {
  delete[] cp;
  delete[] offset;
  delete[] cls;
  delete[] level;
  cp = offset = nullptr;
  cls = level = nullptr;
  n = 0;
  base_level = 0;
}
bool BidiText::analyze(const char *text, size_t bytes, BaseDirection base) {
  clear();
  if (!text && bytes)
    return false;
  if (bytes > MaxBidiLength * 4)
    return false;
  size_t count = 0;
  for (size_t at = 0; at < bytes; ++count) {
    uint32_t c;
    size_t k = utf8(text + at, bytes - at, c);
    at += k ? k : 1;
  }
  if (count > MaxBidiLength)
    return false;
  cp = BSF_NEW uint32_t[count + 1];
  offset = BSF_NEW uint32_t[count + 1];
  cls = BSF_NEW uint8_t[count + 1];
  level = BSF_NEW uint8_t[count + 1];
  if (!cp || !offset || !cls || !level) {
    clear();
    return false;
  }
  size_t i = 0;
  for (size_t at = 0; at < bytes; ++i) {
    uint32_t c;
    size_t k = utf8(text + at, bytes - at, c);
    if (!k)
      k = 1;
    cp[i] = c;
    offset[i] = uint32_t(at);
    cls[i] = uint8_t(bidi_class(c));
    at += k;
  }
  offset[count] = uint32_t(bytes);
  n = count;
  direction = base;
  // Each paragraph (terminated by B) is resolved independently.
  bool first = true;
  for (size_t begin = 0; begin < n;) {
    size_t end = begin;
    while (end < n && cls[end] != uint8_t(BidiClass::B))
      ++end;
    if (end < n)
      ++end;
    uint8_t para = 0;
    if (!resolve_bidi_levels(cp + begin, cls + begin, end - begin, base,
                             level + begin, para)) {
      clear();
      return false;
    }
    if (first)
      base_level = para;
    first = false;
    begin = end;
  }
  if (first)
    base_level = base == BaseDirection::RTL ? 1 : 0;
  return true;
}
size_t BidiText::scalar_at(size_t byte) const {
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    if (offset[m + 1] <= byte)
      lo = m + 1;
    else
      hi = m;
  }
  return lo;
}
bool bidi_line_map(const BidiText &t, size_t begin, size_t end,
                   uint32_t *v2l, uint32_t *l2v) {
  if (end < begin || end > t.n || !v2l)
    return false;
  size_t len = end - begin;
  if (!len)
    return true;
  uint8_t *line = BSF_NEW uint8_t[len];
  if (!line)
    return false;
  // Paragraph level of the paragraph containing `begin`.
  size_t p = begin;
  while (p > 0 && t.cls[p - 1] != uint8_t(BidiClass::B))
    --p;
  uint8_t para = t.base_level;
  if (t.direction == BaseDirection::Auto) {
    size_t e = p;
    while (e < t.n && t.cls[e] != uint8_t(BidiClass::B))
      ++e;
    para = resolve_paragraph_level(t.cls + p, e - p, 0);
  }
  bidi_line_levels(t.cls, t.level, begin, end, para, line);
  bidi_fill_removed(line, len, para);
  size_t m = bidi_reorder(line, len, v2l);
  delete[] line;
  if (m != len)
    return false;
  for (size_t k = 0; k < len; ++k) {
    v2l[k] += uint32_t(begin);
    if (l2v)
      l2v[v2l[k] - begin] = uint32_t(k);
  }
  return true;
}
} // namespace BSF
