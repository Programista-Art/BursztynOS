#pragma once
#include "bsf.h"
#include "config.h"
#include "layout.h"
// BSF_COMPLEX_TEXT=0 builds the legacy LTR-only runtime (no BSF 2.0 engine
// linked) for apps excluded by the kernel image budget; see docs/bsf2*.
#ifndef BSF_COMPLEX_TEXT
#define BSF_COMPLEX_TEXT 1
#endif
namespace BSF {
int bitmap_width(uint32_t);
const uint16_t *bitmap(uint32_t);
struct FontInfo {
  uint32_t id{};
  char path[160]{}, family[96]{}, style[48]{};
  uint16_t weight{};
  bool italic{}, mono{}, serif{};
  uint8_t coverage[34]{}; // Conservative cmap coverage in 4,096-scalar blocks.
};
enum class InstallResult { Ok, BadPath, InvalidFont, Exists, IO };
// Uses normal caller BWS permissions; never replaces an existing system font.
InstallResult install_font(const char* source);
struct ResourceInfo {unsigned examined{},rejected{},resident_bytes{},peak_resident_bytes{};bool catalog_limit{},directory_error{};uint64_t loads{},io_bytes{},scalar_hits{},cluster_hits{};};
ResourceInfo resources();
unsigned enumerate();
uint32_t default_id();
const FontInfo *info(unsigned);
Config configuration();
bool apply(Config);
void refresh();
// Explicit directory rescan, at an idle event boundary (not during paint).
void rescan();
uint32_t resolved_face(uint32_t cp, bool bold=false, const char* family=nullptr, bool italic=false);
// A zero result requests explicit per-scalar mixed-font fallback.
uint32_t resolved_cluster(const char*,size_t,bool bold=false,const char* family=nullptr,bool italic=false);
const Glyph* glyph_face(uint32_t cp,uint32_t face,int px,bool bold=false,bool italic=false);
int advance_face(uint32_t cp,uint32_t face,int px);
int kern_face(uint32_t left,uint32_t right,uint32_t face,int px);
void monospace(bool);
bool monospace();
int ui_size();
uint32_t generation();
// family is a CSS family list, never a filesystem path; null means system UI.
int measure_cp(uint32_t cp, int px, bool bold = false,
               const char *family = nullptr);
int measure_styled_cp(uint32_t cp,int px,bool bold,const char* family,bool italic);
int measure_text(const char *, int px, bool bold = false,
                 const char *family = nullptr, size_t limit = 65536);
struct TextMetrics {
  int64_t width{};uint32_t previous{},previous_face{};
  void cluster(const char*,size_t start,size_t end,int px,bool bold=false,const char* family=nullptr);
};
// Logical LTR caret metrics: offsets inside a grapheme floor to its start.
// The full bounded string determines cluster coverage, including trailing marks.
int measure_prefix(const char*,size_t offset,int px,bool bold=false,
                   const char* family=nullptr,size_t limit=65536);
size_t hit_test(const char*,int x,int px,bool bold=false,
                const char* family=nullptr,size_t limit=65536);
uint32_t resolve_family(const char *);
const char *family_name(uint32_t);
bool family_monospace(uint32_t);
int kern(uint32_t left, uint32_t right, int px, bool bold = false,
         const char *family = nullptr);
int height(int px, const char *family = nullptr);
int ascent(int px, const char *family = nullptr);
const Glyph *glyph(uint32_t cp, int px, bool bold = false,
                   const char *family = nullptr, bool italic = false);
bool cached_face(uint32_t cp,uint32_t face,int px,bool bold=false,bool italic=false);
bool cached(uint32_t cp, int px, bool bold, const char *family, bool italic);
void pin(const Glyph *);
void unpin(void *);
bool ready(const char *family = nullptr);
const Statistics &statistics();
void preview(uint32_t id, int size); // local to Settings, never publishes
void end_preview();
void shutdown();
// ---- BSF 2.0 shared layout ------------------------------------------------
// One cached, shaped layout used by measure_text/measure_prefix/hit_test and
// CzcionkaUI drawing whenever needs_complex_layout() is true (RTL, explicit
// bidi controls, joining scripts, combining marks). Borrowed until the next
// layout_text call; never retain across event-loop iterations.
const TextLayout *layout_text(const char *text, size_t bytes, int px,
                              bool bold = false, const char *family = nullptr,
                              bool italic = false, int64_t max_width = 0,
                              BaseDirection direction = BaseDirection::Auto);
// Rasterize a shaped glyph id from a catalog face (GSUB output).
const Glyph *glyph_id_face(uint32_t face, unsigned glyph, int px,
                           bool bold = false, bool italic = false);
// Draw a shared layout: mask(ctx,x,y,w,h,alpha) per glyph bitmap, lines
// stacked by height(px). GPOS-positioned marks use font anchors; marks in
// fonts without GPOS mark data use a bounded centring approximation.
using MaskCallback = void (*)(void *, int, int, int, int, const uint8_t *);
bool draw_layout(const TextLayout &, int x, int y, int px, bool bold,
                 const char *family, MaskCallback, void *ctx);
// ---- BSF 2.1 caller-owned layouts ------------------------------------------
// Editors and the browser keep their own bounded caches of paragraph/run
// layouts instead of borrowing the 4-slot shared cache. Same resolver,
// shaping and counters as layout_text. A built layout stays valid until the
// owner rebuilds it; owners must compare generation() (font configuration
// and catalog epoch) before reuse.
struct LayoutRequest {
  int px{16};
  bool bold{}, italic{};
  const char *family{}; // CSS family list or null (system UI font)
  int64_t max_width{};  // 16.16; 0 = no wrapping
  BaseDirection direction{BaseDirection::Auto};
  int32_t letter_spacing{}, word_spacing{}; // 16.16 px
  uint32_t context_before{}, context_after{};
};
bool build_layout(TextLayout &, const char *text, size_t bytes, const LayoutRequest &);
// Draw one visual line of a layout with its top-left at (x, y); x is the
// layout's line origin (alignment is the caller's or the layout's x_shift).
bool draw_layout_line(const TextLayout &, size_t line, int x, int y, int px,
                      bool bold, bool italic, const char *family, MaskCallback, void *ctx);
struct TextStatistics {
  uint64_t layout_hits{}, layout_misses{}, layout_failures{}, shaped_runs{},
      shaped_glyphs{}, fallback_searches{}, budget_failures{}, gsub_runs{},
      gpos_runs{}, longest_run{};
  size_t layout_bytes{};
  // Appended (keeps earlier field offsets for observers): union of
  // applied GSUB/GPOS lookup types, bit t = lookup type t.
  uint64_t gsub_type_mask{}, gpos_type_mask{};
  // Appended (BSF 2.1): builds into caller-owned layouts (build_layout).
  uint64_t owned_builds{};
};
TextStatistics text_statistics();
} // namespace BSF
