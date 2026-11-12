#pragma once
#include <stddef.h>
#include <stdint.h>
#include "limits.h"
// Bursztynowy System Czcionek. This API and all parsers are userspace only.
namespace BSF {
// MaxGlyphs is the sfnt limit (uint16 numGlyphs) so CJK faces load.
constexpr unsigned MaxTables = 64, MaxGlyphs = 65535;
constexpr unsigned MaxPoints = 4096, MaxContours = 128, MaxVertices = 8192,
                   MaxCompoundDepth = 8;
constexpr unsigned MaxFaces = 4, MaxBitmap = 128,
                   CacheSlots = 128;
constexpr unsigned CacheBytes = CacheSlots * MaxBitmap * MaxBitmap;
struct Bytes {
  const uint8_t *p{};
  size_t n{};
  bool has(size_t o, size_t z) const { return o <= n && z <= n - o; }
  uint16_t u16(size_t o) const {
    return has(o, 2) ? uint16_t((unsigned(p[o]) << 8) | p[o + 1]) : 0;
  }
  int16_t s16(size_t o) const { return int16_t(u16(o)); }
  uint32_t u32(size_t o) const {
    return has(o, 4) ? (uint32_t(u16(o)) << 16) | u16(o + 2) : 0;
  }
  Bytes sub(size_t o, size_t z) const {
    return has(o, z) ? Bytes{p + o, z} : Bytes{};
  }
};
// ---- BSF 2.2 outline formats -------------------------------------------
// Bitmap: colour-strike-only face (CBDT/CBLC or sbix, no outlines).
enum class OutlineFormat : uint8_t { None, TrueType, CFF, CFF2, Bitmap };
enum class ContainerFormat : uint8_t { SFNT, Collection, WOFF, WOFF2 };
// Resource limits for CFF / CFF2 (justified in docs/bsf2-2-security.md).
constexpr unsigned CffMaxStack = 513, CffMaxSubrDepth = 10,
                   CffMaxOperations = 200000, CffMaxStems = 96 * 4,
                   CffMaxContours = 1024, CffMaxRegions = 64,
                   CffMaxIndexCount = 1u << 20;
constexpr unsigned MaxAxes = 16;
enum : uint8_t { ColorCOLR = 1, ColorCBDT = 2, ColorSbix = 4 };
struct CffIndex {
  bool present{};
  uint8_t off_size{};
  uint32_t count{}, offsets{}, data{}; // positions inside the CFF table
  bool item(Bytes table, uint32_t index, Bytes &out) const;
};
struct CffPrivate {
  CffIndex subrs{};
  double default_width{}, nominal_width{};
  unsigned vsindex{};
};
struct CffFace {
  Bytes table{}, private_dict{}, fd_select{}, charset{}, vstore{};
  CffIndex charstrings{}, global_subrs{}, fd_array{};
  uint32_t glyph_count{}, string_count{};
  double matrix[6]{.001, 0, 0, .001, 0, 0};
  uint8_t version{}; // 0 = none, 1 = CFF, 2 = CFF2
  uint8_t charset_predefined{0xff};
  bool cid{}, custom_matrix{};
  bool open(Bytes table, unsigned glyphs, bool cff2);
  unsigned fd_for(unsigned glyph) const;
  bool private_for(unsigned fd, CffPrivate &, const int16_t *coords,
                   unsigned axes) const;
  unsigned glyph_for_sid(unsigned sid) const;

private:
  bool parse_private(Bytes dict, uint32_t at, CffPrivate &,
                     const int16_t *coords, unsigned axes) const;
};
// One table of a face, possibly in its own buffer (collection faces loaded
// lazily from a file keep large shared tables in shared buffers).
struct TableSlice {
  uint32_t tag{};
  Bytes bytes{};
};
constexpr unsigned MaxCollectionFaces = 256;
// Validated sfnt table directory of one face (plain sfnt or collection).
struct TableDirectory {
  uint32_t version{};
  unsigned count{};
  struct Record {
    uint32_t tag{}, offset{}, length{};
  } records[MaxTables]{};
};
// Bounded random access to a font file (memory or a Ring 3 file reader).
struct FileReader {
  bool (*read)(void *ctx, uint64_t offset, size_t n, uint8_t *out){};
  void *ctx{};
  uint64_t size{};
};
// Number of faces in a container (1 for a plain sfnt, 0 if invalid) and the
// validated table directory of face `index`.
unsigned collection_faces(const FileReader &);
bool face_directory(const FileReader &, unsigned index, TableDirectory &out);
unsigned collection_faces(Bytes file);
bool face_directory(Bytes file, unsigned index, TableDirectory &out);
struct Face {
  Bytes data{}, head{}, hhea{}, hmtx{}, loca{}, glyf{}, cmap{}, kern{}, names{};
  Bytes gdef{}, gsub{}, gpos{}, cff{}, cff2{}, os2{}, fvar{}, colr{}, cpal{};
  uint32_t id{};
  uint16_t glyphs{}, units{}, metrics{}, weight{400};
  int ascent{}, descent{}, gap{};
  bool long_loca{}, italic{}, mono{}, serif{};
  char family[96]{}, style[48]{}, full_name[128]{}, postscript[96]{};
  // ---- BSF 2.2 (appended) ----
  Bytes maxp{}, post{}, avar{}, gvar{}, hvar{}, vvar{}, mvar{}, stat{};
  Bytes cbdt{}, cblc{}, sbix{}, vhea{}, vmtx{};
  uint32_t sfnt_version{};
  OutlineFormat outline{OutlineFormat::None};
  uint16_t width_class{5}, collection_index{}, collection_count{1};
  CffFace cff_face{};
  // Variation instance: normalized (avar-mapped) coordinates in F2Dot14.
  uint8_t axis_count{};
  int16_t coords[MaxAxes]{};
  uint32_t base_id{};
  // Catalog-only face: outline tables were not loaded (see open_tables).
  bool metadata_only{};
  // Colour glyph tables present (bit set): ColorCOLR, ColorCBDT, ColorSbix.
  uint8_t color_tables{};
  // Registered by a browser document (BSF::register_scoped_face).
  bool browser_scope{};
  bool open(const void *, size_t, uint32_t identity = 1);
  // Face `index` of a plain sfnt (index 0) or of a TTC/OTC collection held
  // entirely in memory.
  bool open_index(const void *, size_t, unsigned index, uint32_t identity);
  // Tables supplied individually (lazy collection-face loading). The caller
  // keeps every buffer alive for the lifetime of the Face. With
  // `metadata_only`, slices with p == nullptr and n > 0 stand for tables that
  // exist but were not read (outline data); the face is then usable for the
  // catalog only and never for rendering.
  bool open_tables(const TableSlice *, unsigned count, uint32_t version,
                   uint32_t identity, bool metadata_only = false);
  unsigned glyph(uint32_t) const;
  bool location(unsigned, Bytes &) const;
  int advance(unsigned, int) const;
  int bearing(unsigned, int) const;
  int kerning(unsigned, unsigned, int) const;
  int line_height(int px) const;
  unsigned advance_units(unsigned glyph) const;
  int kerning_units(unsigned left, unsigned right) const;
  bool variable() const {
    for (unsigned i = 0; i < axis_count; ++i)
      if (coords[i])
        return true;
    return false;
  }

private:
  bool parse(const void *, size_t, uint32_t);
  bool assemble(const TableSlice *, unsigned count, uint32_t version,
                uint32_t identity, bool metadata_only = false);
};
// ---- BSF 2.2 variation axes (fvar/avar) --------------------------------
struct VariationAxis {
  uint32_t tag{};
  float min{}, def{}, max{};
  uint16_t flags{}, name_id{};
};
struct NamedInstance {
  uint16_t subfamily_name_id{}, postscript_name_id{0xffff}, flags{};
  float coords[MaxAxes]{}; // user-space
};
struct VariationSetting {
  uint32_t tag{};
  double value{};
};
// Axes of a variable face (0 if the face is static or its fvar is outside
// the supported model, e.g. more than MaxAxes axes).
unsigned variation_axes(const Face &, VariationAxis *out, unsigned capacity);
unsigned named_instances(const Face &, NamedInstance *out, unsigned capacity);
// Select a variation instance from user-space settings. Unknown tags are
// ignored; values are clamped to the axis range; non-finite values are
// rejected. Axes not mentioned use their default. The face identity (id)
// changes with the instance so glyph caches never mix instances.
bool set_variations(Face &, const VariationSetting *, unsigned count);
// Normalized coordinate of a user value on one axis (before avar).
int normalize_axis(const VariationAxis &, double value);
// ---- BSF 2.2 colour glyphs -------------------------------------------------
enum : uint8_t { ColorFormatNone, ColorFormatCOLRv0, ColorFormatCOLRv1, ColorFormatCBDT, ColorFormatSbix };
struct ColorGlyph {
  int width{}, height{}, x{}, y{}, advance{}; // x/y relative to pen/baseline
  uint8_t format{};
};
struct ColorStats {
  unsigned paints{}, layers{}, glyphs{}, max_depth{}, images{};
  bool limited{};
};
// Which colour representation BSF would render for `glyph` (COLRv1, then
// COLRv0, then sbix, then CBDT); ColorFormatNone for monochrome glyphs.
unsigned color_glyph_format(const Face &, unsigned glyph);
// Renders into `argb` (straight alpha 0xAARRGGBB, width x height, capacity
// >= MaxBitmap^2). `foreground` is the text colour (palette index 0xFFFF).
bool render_color_glyph(const Face &, unsigned glyph, int px, uint32_t foreground, unsigned palette,
                        ColorGlyph &, uint32_t *argb, size_t capacity, ColorStats * = nullptr);
// PNG decoder limits (colour strikes): side and total pixels.
constexpr unsigned MaxPngSide = 1024, MaxPngPixels = 512 * 512;
bool decode_png(const uint8_t *, size_t, uint32_t *&argb, unsigned &w, unsigned &h);
// ItemVariationStore region scalars for VariationData `outer` at the given
// normalized coordinates. `count` receives the region count of that data.
bool ivs_scalars(Bytes store, unsigned outer, const int16_t *coords,
                 unsigned axes, float *out, unsigned &count);
// Interpolated delta of item (outer, inner) of an ItemVariationStore.
float ivs_delta(Bytes store, unsigned outer, unsigned inner, const int16_t *coords,
                unsigned axes);
struct Glyph {
  uint32_t face{};
  uint16_t id{}, px{}, style{}, pins{};
  uint64_t used{};
  int width{}, height{}, x{}, y{}, advance{}; // x/y relative to baseline
  uint8_t *alpha{};
};
enum class TextDirection : uint8_t { LogicalLTR, ExperimentalRTL };
// Position records retain logical source offsets; no visual reordering is enabled.
struct PositionedGlyph {
  uint32_t face{}, glyph{};
  int advance{}, offset_x{}, offset_y{};
  size_t byte_begin{}, byte_end{}, cluster_begin{};
  TextDirection direction{TextDirection::LogicalLTR};
};
struct Statistics {
  uint64_t hits{}, misses{}, evictions{}, rasterizations{}, rejected{};
  size_t bitmap_bytes{};
  // BSF 2.2 (appended): rasterizations by outline technology / instance.
  uint64_t cff_rasterizations{}, cff2_rasterizations{}, variable_rasterizations{},
      scoped_rasterizations{};
};
class Cache {
  Glyph *slots_{};
  uint8_t *storage_{};
  uint64_t clock_{};

public:
  Statistics stats{};
  Cache() = default;
  ~Cache();
  Cache(const Cache &) = delete;
  Cache &operator=(const Cache &) = delete;
  void clear();
  // Borrowed until the next lookup. Paint immediately; never retain in
  // DOM/layout.
  const Glyph *get(const Face &, uint32_t cp, int px, unsigned style = 0);
  // Glyph-ID access for shaped text (GSUB output has no code point).
  const Glyph *get_glyph(const Face &, unsigned glyph, int px, unsigned style = 0);
  bool contains_glyph(const Face &, unsigned glyph, int px, unsigned style = 0) const;
  bool contains(const Face &, uint32_t cp, int px, unsigned style = 0) const;
};
bool rasterize(const Face &, unsigned glyph, int px, Glyph &, uint8_t *mask,
               size_t capacity);
// Glyph outline in font units (the same validated path the rasterizer
// consumes): kind 1 move, 2 line, 3 quadratic (c1), 4 cubic (c1, c2).
struct PathSink {
  void (*segment)(void *ctx, int kind, double x, double y, double c1x,
                  double c1y, double c2x, double c2y){};
  void *ctx{};
};
bool glyph_path(const Face &, unsigned glyph, const PathSink &);
// Bounded zlib (RFC 1950) decoder: exactly `cap` output bytes at most,
// Adler-32 verified. Used by WOFF 1.0 and PNG colour-bitmap strikes.
bool inflate_zlib(const uint8_t *in, size_t n, uint8_t *out, size_t cap,
                  size_t *produced);
size_t utf8(const char *, size_t available, uint32_t &);
// Unicode 15.1.0 properties and extended grapheme boundaries (UAX #29).
unsigned combining_class(uint32_t);
bool default_ignorable(uint32_t);
bool combining(uint32_t);
size_t next_cluster(const char*,size_t at,size_t size);
size_t previous_cluster(const char*,size_t at);
size_t cluster_floor(const char*,size_t size,size_t offset);
bool cluster_boundary(const char*,size_t size,size_t offset);
size_t cluster_count(const char*,size_t size);
// UAX #29 Extended_Pictographic (Unicode 15.1).
bool extended_pictographic(uint32_t);
// Bounded approximate positioning when no OpenType anchors are available.
struct MarkPen {
  int base{}, advance{}, above{}, below{}, top{}, bottom{}, center{};
  bool has_base{};
  void place(uint32_t cp,int pen,int& x,int& y,int width,int height,int step){
    if(combining(cp)){
      x=(has_base?center:base+advance/2)-width/2;
      unsigned ccc=combining_class(cp);
      if(ccc==1||!has_base)return;
      bool low=ccc==200||ccc==202||ccc==218||ccc==220||ccc==222||ccc==233||ccc==240;
      int gap=ccc==200||ccc==202||ccc==214||ccc==216?0:1;
      if(low){y=bottom+gap+below;below+=height+gap;}
      else {y=top-height-gap-above;above+=height+gap;}
    }else{base=pen;advance=step;above=below=0;top=y;bottom=y+height;center=width?x+width/2:pen+step/2;has_base=true;}
  }
};
} // namespace BSF
