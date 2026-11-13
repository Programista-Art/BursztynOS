// BSF 2.2 Milestone 2 — TrueType/OpenType collections (TTC/OTC).
//
// A. In-memory collections through Face::open_index: every face of every
//    collection fixture is compared with FreeType (face count, names, glyph
//    count, every outline).
// B. Malformed collection headers and directories (ASan/UBSan).
// C. The production runtime catalog (bsf/runtime.cpp) over a mocked Ring 3
//    filesystem: /czcionki/{ttf,otf,ttc,otc}, per-face catalog entries, stable
//    face identifiers, shared table buffers, refresh, fallback across faces.
// D. Host-only: the real Noto Sans CJK collection (19.5 MB, 10 faces, CFF
//    15.4 MB shared by all faces) through the lazy table loader, if present.
#include "bsf22_common.h"
#include "../bsf/asset_path.h"
#include "../bsf/runtime.h"
#include "../jadro/gui/bursztyn_gui.h"
#include <map>
#include <memory>
#include <random>

static std::map<std::string, std::string> files;
static std::map<std::string, std::string> listings; // directory -> listing
static BSF::Config config;
static uint64_t ranged_reads, whole_reads;
static std::map<std::string, unsigned> whole_read_paths;
// Section E (large install): Settings holds the file-read right, so ranged
// reads of a user path are allowed; BWS 98 writes and BWS 7 deletes.
static bool user_reads, fail_fragment;
static unsigned fragment_writes, fragment_fail_at;
static uint64_t largest_fragment;
void wypisz(const char *) {}
bool utworz(const char *path) {                                     // install_font
  if (files.count(path))
    return false;
  files[path].clear();
  std::string p = path;
  listings[p.substr(0, 13)] += "[PLIK] " + p.substr(14) + "\n";
  return true;
}
bool zapisz_plik(const char *, const char *, uint32_t) { return false; }
bool wylistuj_katalog_uzytkownika(const char *path, char *out, uint32_t n) {
  auto it = listings.find(path);
  if (it == listings.end() || it->second.size() >= n)
    return false;
  memcpy(out, it->second.c_str(), it->second.size() + 1);
  return true;
}
bool pobierz_rozmiar_pliku(const char *path, uint32_t *n) {
  auto it = files.find(path);
  if (it == files.end())
    return false;
  *n = uint32_t(it->second.size());
  return true;
}
bool czytaj_plik(const char *path, char *out, uint32_t n) {
  CHECK(BSF::trusted_asset(path));
  auto it = files.find(path);
  if (it == files.end() || n != it->second.size())
    return false;
  ++whole_reads;
  ++whole_read_paths[path];
  memcpy(out, it->second.data(), n);
  return true;
}
extern "C" uint64_t bws_wywolaj(uint64_t fn, uint64_t a, uint64_t b, uint64_t c,
                                uint64_t d) {
  if (fn == 92 && b && c == sizeof(config)) {
    if (!a) {
      *(BSF::Config *)b = config;
      return 1;
    }
    config = *(BSF::Config *)b;
    return 1;
  }
  if (fn == 59) {
    // Same policy as the kernel handler: trusted path, 64 KiB per call,
    // end <= MaxFontFile.
    CHECK(user_reads || BSF::trusted_asset((const char *)a));
    CHECK(d <= 65536);
    CHECK(b + d <= BSF::MaxFontFile);
    auto it = files.find((const char *)a);
    if (it == files.end() || b > it->second.size() || d > it->second.size() - b)
      return 0;
    ++ranged_reads;
    memcpy((void *)c, it->second.data() + b, d);
    return d + 1;
  }
  if (fn == 98) {
    // Kernel policy (jadro/bws/syscalls.cpp bws_zapisz_fragment_pliku):
    // existing file, 1..1 MiB per call, offset <= current size (no holes).
    CHECK(d >= 1 && d <= 1024 * 1024);
    auto it = files.find((const char *)a);
    if (it == files.end() || b > it->second.size())
      return 0;
    if (fail_fragment && ++fragment_writes == fragment_fail_at)
      return 0;
    if (!fail_fragment)
      ++fragment_writes;
    if (d > largest_fragment)
      largest_fragment = d;
    if (it->second.size() < b + d)
      it->second.resize(b + d);
    memcpy(&it->second[b], (const void *)c, d);
    return 1;
  }
  if (fn == 7) {
    std::string p = (const char *)a;
    files.erase(p);
    auto &l = listings[p.substr(0, 13)];
    auto at = l.find("[PLIK] " + p.substr(14) + "\n");
    if (at != std::string::npos)
      l.erase(at, p.size() - 14 + 8);
    return 1;
  }
  return 0;
}
static void put32(std::vector<uint8_t> &v, size_t o, uint32_t n) {
  v[o] = uint8_t(n >> 24);
  v[o + 1] = uint8_t(n >> 16);
  v[o + 2] = uint8_t(n >> 8);
  v[o + 3] = uint8_t(n);
}
static void put16(std::vector<uint8_t> &v, size_t o, unsigned n) {
  v[o] = uint8_t(n >> 8);
  v[o + 1] = uint8_t(n);
}
static const std::string dir = "testy/fixtures/bsf22/fonts/";
static void in_memory(FT_Library lib) {
  struct Case {
    const char *file;
    unsigned faces;
    OutlineFormat outlines[4];
  } cases[] = {
      {"NotoSans-subset.ttc", 3, {OutlineFormat::TrueType, OutlineFormat::TrueType, OutlineFormat::TrueType}},
      {"NotoSansCJK-subset.ttc", 4, {OutlineFormat::CFF, OutlineFormat::CFF, OutlineFormat::CFF, OutlineFormat::CFF}},
      {"NotoSans-mixed.otc", 2, {OutlineFormat::TrueType, OutlineFormat::CFF}},
  };
  for (const auto &c : cases) {
    auto bytes = read(dir + c.file);
    CHECK(!bytes.empty());
    Bytes file{bytes.data(), bytes.size()};
    CHECK(collection_faces(file) == c.faces);
    FT_Face probe;
    CHECK(!FT_New_Memory_Face(lib, bytes.data(), FT_Long(bytes.size()), 0, &probe));
    CHECK(unsigned(probe->num_faces) == c.faces);
    FT_Done_Face(probe);
    std::string names;
    for (unsigned i = 0; i < c.faces; ++i) {
      Face f;
      CHECK(f.open_index(bytes.data(), bytes.size(), i, 100 + i));
      CHECK(f.outline == c.outlines[i]);
      CHECK(f.collection_index == i && f.collection_count == c.faces);
      FT_Face ft;
      CHECK(!FT_New_Memory_Face(lib, bytes.data(), FT_Long(bytes.size()), FT_Long(i), &ft));
      CHECK(!std::strcmp(f.family, ft->family_name));
      CHECK(!std::strcmp(f.style, ft->style_name));
      FT_Done_Face(ft);
      CHECK(compare_outlines(lib, bytes, i, f, c.file) == 0);
      names += std::string(f.family) + "/" + f.style + "; ";
      // Shared tables: faces reference the same bytes for shared tables.
      TableDirectory d0, di;
      CHECK(face_directory(file, 0, d0) && face_directory(file, i, di));
    }
    CHECK(!Face{}.open_index(bytes.data(), bytes.size(), c.faces, 1)); // out of range
    std::printf("  %-26s %u faces: %s\n", c.file, c.faces, names.c_str());
  }
  // Shared-table check on the CJK collection: CFF/cmap/hmtx are stored once.
  auto bytes = read(dir + "NotoSansCJK-subset.ttc");
  Bytes file{bytes.data(), bytes.size()};
  TableDirectory a, b;
  // Faces 0 (JP) and 1 (KR) were subset to the same glyph set, so their
  // CFF/GPOS/hmtx/maxp/... are stored once; SC/TC have their own CFF.
  CHECK(face_directory(file, 0, a) && face_directory(file, 1, b));
  unsigned shared = 0;
  for (unsigned i = 0; i < a.count; ++i)
    for (unsigned j = 0; j < b.count; ++j)
      if (a.records[i].tag == b.records[j].tag && a.records[i].offset == b.records[j].offset)
        ++shared;
  std::printf("  NotoSansCJK-subset.ttc: %u of %u tables of face 0 shared with face 1\n",
              shared, a.count);
  CHECK(shared >= 8);
}
static void malformed() {
  auto base = read(dir + "NotoSans-subset.ttc");
  Bytes b{base.data(), base.size()};
  auto opens = [](const std::vector<uint8_t> &v, unsigned index) {
    Face f;
    return f.open_index(v.data(), v.size(), index, 7);
  };
  CHECK(opens(base, 0) && opens(base, 2));
  { // numFonts 0, too many, version 3
    auto v = base;
    put32(v, 8, 0);
    CHECK(collection_faces(Bytes{v.data(), v.size()}) == 0 && !opens(v, 0));
    v = base;
    put32(v, 8, MaxCollectionFaces + 1);
    CHECK(collection_faces(Bytes{v.data(), v.size()}) == 0);
    v = base;
    put32(v, 8, 0x7fffffff);
    CHECK(collection_faces(Bytes{v.data(), v.size()}) == 0);
    v = base;
    put16(v, 4, 3);
    CHECK(collection_faces(Bytes{v.data(), v.size()}) == 0);
  }
  { // invalid face offsets: inside the header, past EOF, misaligned junk
    for (uint32_t off : {0u, 8u, 12u, uint32_t(base.size()), uint32_t(base.size() - 6), 0xffffffffu}) {
      auto v = base;
      put32(v, 12, off);
      CHECK(!opens(v, 0));
      CHECK(opens(v, 1)); // other faces stay usable
    }
  }
  { // repeated face references: both indices open the same directory
    auto v = base;
    put32(v, 16, b.u32(12));
    CHECK(opens(v, 0) && opens(v, 1));
    Face f0, f1;
    CHECK(f0.open_index(v.data(), v.size(), 0, 1) && f1.open_index(v.data(), v.size(), 1, 2));
    CHECK(!std::strcmp(f0.style, f1.style));
  }
  uint32_t dir0 = b.u32(12);
  { // very large table directory (numTables 65535 / MaxTables+1)
    auto v = base;
    put16(v, dir0 + 4, 65535);
    CHECK(!opens(v, 0));
    put16(v, dir0 + 4, MaxTables + 1);
    CHECK(!opens(v, 0));
  }
  { // table record past EOF, overlapping the directory, overlapping tables,
    // duplicate tags
    auto v = base;
    put32(v, dir0 + 12 + 12, uint32_t(v.size())); // first table length
    CHECK(!opens(v, 0));
    v = base;
    put32(v, dir0 + 12 + 8, dir0); // offset inside the directory
    CHECK(!opens(v, 0));
    v = base;
    put32(v, dir0 + 12 + 16 + 8, b.u32(dir0 + 12 + 8)); // two tables same bytes
    put32(v, dir0 + 12 + 16 + 12, b.u32(dir0 + 12 + 12));
    CHECK(!opens(v, 0));
    v = base;
    std::memcpy(v.data() + dir0 + 12 + 16, v.data() + dir0 + 12, 4); // dup tag
    CHECK(!opens(v, 0));
  }
  { // truncated files at every interesting boundary
    size_t last_end = 0; // end of the last table byte (excluding padding)
    for (unsigned i = 0; i < 3; ++i) {
      TableDirectory d;
      CHECK(face_directory(b, i, d));
      for (unsigned t = 0; t < d.count; ++t)
        last_end = std::max<size_t>(last_end, size_t(d.records[t].offset) + d.records[t].length);
    }
    for (size_t cut : {size_t(4), size_t(11), size_t(12), size_t(15), size_t(dir0 + 6),
                       size_t(dir0 + 20), base.size() / 3, last_end - 1}) {
      std::vector<uint8_t> v(base.begin(), base.begin() + cut);
      unsigned ok = 0;
      for (unsigned i = 0; i < 3; ++i) {
        Face f;
        ok += f.open_index(v.data(), v.size(), i, 9); // must not crash
      }
      // Inside the header or the first face directory every face fails; a
      // later cut only invalidates faces whose tables reach past it.
      if (cut <= dir0 + 20)
        CHECK(ok == 0);
      CHECK(ok < 3);
    }
  }
  // Random mutations of the header/directory area.
  std::mt19937 rng(2203);
  unsigned opened = 0;
  auto cache = std::make_unique<Cache>();
  for (unsigned iter = 0; iter < 4000; ++iter) {
    auto v = base;
    for (unsigned e = 0, n = 1 + rng() % 8; e < n; ++e)
      v[rng() % std::min<size_t>(v.size(), 512)] = uint8_t(rng());
    for (unsigned i = 0; i < 3; ++i) {
      Face f;
      if (f.open_index(v.data(), v.size(), i, 1000 + iter)) {
        ++opened;
        cache->get(f, 'A', 24);
      }
    }
  }
  std::printf("  malformed collections: header/offset/directory cases + 4000 "
              "mutations (%u faces opened)\n", opened);
}
static void add_file(const std::string &path, std::string bytes) {
  files[path] = std::move(bytes);
  std::string d = path.substr(0, 13), name = path.substr(14);
  listings[d] += "[PLIK] " + name + "\n";
}
static std::string slurp(const std::string &p) {
  auto v = read(p);
  return std::string(v.begin(), v.end());
}
static void runtime_catalog(FT_Library lib) {
  files.clear();
  listings.clear();
  config = BSF::Config{};
  config.checksum = BSF::checksum(config);
  listings["/czcionki/ttf"] = "";
  add_file("/czcionki/ttf/DejaVuSans.ttf", slurp("czcionki/ttf/DejaVuSans.ttf"));
  add_file("/czcionki/otf/NotoSans-Regular.otf", slurp(dir + "NotoSans-Regular.otf"));
  add_file("/czcionki/ttc/NotoSans-subset.ttc", slurp(dir + "NotoSans-subset.ttc"));
  add_file("/czcionki/otc/NotoSansCJK-subset.otc", slurp(dir + "NotoSansCJK-subset.ttc"));
  add_file("/czcionki/otc/NotoSans-mixed.otc", slurp(dir + "NotoSans-mixed.otc"));
  // Extension/directory mismatch and traversal are never examined.
  add_file("/czcionki/ttc/wrong.otf", slurp(dir + "NotoSans-Regular.otf"));
  listings["/czcionki/ttc"] += "[PLIK] ../escape.ttc\n";
  BSF::shutdown();
  unsigned n = BSF::enumerate();
  // DejaVu 1 + NotoSans OTF 1 + subset TTC 3 + CJK OTC 4 + mixed 2 = 11.
  CHECK(n == 11);
  auto r = BSF::resources();
  CHECK(r.collection_faces == 9);
  unsigned per_file[5]{};
  std::map<uint32_t, int> ids;
  for (unsigned i = 0; i < n; ++i) {
    const auto *f = BSF::info(i);
    CHECK(f && f->status == BSF::FontOk);
    CHECK(!ids.count(f->id));
    ids[f->id] = int(i);
    std::string p = f->path;
    if (p.find("ttf/") != std::string::npos) ++per_file[0];
    if (p.find("otf/Noto") != std::string::npos) {
      ++per_file[1];
      CHECK(f->outline == uint8_t(OutlineFormat::CFF) && f->container == uint8_t(ContainerFormat::SFNT));
    }
    if (p.find("subset.ttc") != std::string::npos) {
      ++per_file[2];
      CHECK(f->container == uint8_t(ContainerFormat::Collection) && f->face_count == 3);
    }
    if (p.find("CJK") != std::string::npos) {
      ++per_file[3];
      CHECK(f->outline == uint8_t(OutlineFormat::CFF) && f->face_count == 4);
    }
    if (p.find("mixed") != std::string::npos) ++per_file[4];
    std::printf("    catalog %2u: %-22s %-8s idx %u/%u outline %u w%u %s\n", i, f->family,
                f->style, f->face_index, f->face_count, f->outline, f->weight, f->path);
  }
  CHECK(per_file[0] == 1 && per_file[1] == 1 && per_file[2] == 3 && per_file[3] == 4 && per_file[4] == 2);
  // Stable identifiers across a refresh.
  std::vector<uint32_t> before;
  for (unsigned i = 0; i < n; ++i)
    before.push_back(BSF::info(i)->id);
  BSF::rescan();
  CHECK(BSF::enumerate() == n);
  for (unsigned i = 0; i < n; ++i)
    CHECK(BSF::info(i)->id == before[i]);
  // Individual face selection by family/style and rendering through the
  // shared rasterizer; bold face of the TTC is a different face.
  const BSF::Glyph *regular = BSF::glyph('a', 40, false, "Noto Sans");
  CHECK(regular);
  int regular_w = regular->width;
  const BSF::Glyph *bold = BSF::glyph('a', 40, true, "Noto Sans");
  CHECK(bold && bold->width >= regular_w);
  // Two faces of the same small collection share one whole-file buffer.
  BSF::shutdown();
  BSF::enumerate();
  uint64_t whole_before = whole_reads;
  CHECK(BSF::glyph(0x4f60, 32, false, "Noto Sans CJK JP"));
  CHECK(BSF::glyph(0x4f60, 32, false, "Noto Sans CJK KR"));
  r = BSF::resources();
  CHECK(r.shared_table_hits >= 1);
  CHECK(whole_reads - whole_before == 1);
  CHECK(r.resident_bytes == files["/czcionki/otc/NotoSansCJK-subset.otc"].size());
  // CJK fallback: DejaVu has no Han glyphs; the resolver picks a CJK face.
  uint32_t han = BSF::resolved_face(0x4e16, false, "DejaVu Sans");
  CHECK(han);
  bool from_cjk = false;
  for (unsigned i = 0; i < BSF::enumerate(); ++i)
    if (BSF::info(i)->id == han)
      from_cjk = std::string(BSF::info(i)->family).find("CJK") != std::string::npos;
  CHECK(from_cjk);
  // Hangul / kana / Polish in the same CJK face.
  CHECK(BSF::glyph(0xc548, 24, false, "Noto Sans CJK KR"));
  CHECK(BSF::glyph(0x3053, 24, false, "Noto Sans CJK JP"));
  CHECK(BSF::glyph(0x17c, 24, false, "Noto Sans CJK JP"));
  // Removing a collection: refresh drops its faces; ids of the rest remain.
  files.erase("/czcionki/otc/NotoSans-mixed.otc");
  listings["/czcionki/otc"] = "[PLIK] NotoSansCJK-subset.otc\n";
  BSF::rescan();
  CHECK(BSF::enumerate() == n - 2);
  // Modified file (truncated): its faces are rejected at refresh.
  files["/czcionki/ttc/NotoSans-subset.ttc"].resize(200);
  BSF::rescan();
  CHECK(BSF::enumerate() == n - 5);
  (void)lib;
  std::printf("  runtime catalog: %u faces from 5 files, ids stable across refresh, "
              "shared buffer hits %u\n", n, r.shared_table_hits);
}
static void large_cjk(FT_Library lib) {
  const char *host = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";
  auto bytes = read(host);
  if (bytes.empty()) {
    std::printf("  large CJK collection: SKIPPED (host file %s absent)\n", host);
    return;
  }
  files.clear();
  listings.clear();
  listings["/czcionki/ttf"] = "";
  add_file("/czcionki/ttf/DejaVuSans.ttf", slurp("czcionki/ttf/DejaVuSans.ttf"));
  add_file("/czcionki/ttc/NotoSansCJK-Regular.ttc", std::string(bytes.begin(), bytes.end()));
  BSF::shutdown();
  ranged_reads = 0;
  unsigned n = BSF::enumerate();
  auto r = BSF::resources();
  std::printf("  large CJK: file %zu B, catalog %u faces (+DejaVu), discovery io %llu B, "
              "ranged reads %llu\n",
              bytes.size(), n - 1, (unsigned long long)r.io_bytes,
              (unsigned long long)ranged_reads);
  CHECK(n == 11);
  CHECK(r.io_bytes < bytes.size()); // outline data is not read at discovery
  // Render through face 0 (JP) and face 2 (SC): the 15.4 MB CFF table is
  // read once and shared.
  const BSF::Glyph *g = BSF::glyph(0x4f60, 48, false, "Noto Sans CJK JP");
  CHECK(g && g->width > 10);
  r = BSF::resources();
  unsigned after_first = r.resident_bytes;
  CHECK(after_first < bytes.size());
  CHECK(BSF::glyph(0x4f60, 48, false, "Noto Sans CJK SC"));
  r = BSF::resources();
  std::printf("  large CJK: resident after JP %u B, after JP+SC %u B (shared hits %u, "
              "peak %u, limit %u)\n",
              after_first, r.resident_bytes, r.shared_table_hits, r.peak_resident_bytes,
              BSF::MaxResidentFontBytes);
  CHECK(r.shared_table_hits >= 3);
  CHECK(r.resident_bytes < after_first + 2u * 1024 * 1024);
  CHECK(r.peak_resident_bytes <= BSF::MaxResidentFontBytes);
  // The in-memory API is bounded by MaxFile; collections above it go through
  // table slices (the runtime loader's path). Compare outlines of a glyph
  // sample of every face with FreeType.
  {
    Face whole;
    CHECK(!whole.open_index(bytes.data(), bytes.size(), 0, 1));
    Bytes file{bytes.data(), bytes.size()};
    FT_Outline_Funcs funcs{ft_move, ft_line, ft_conic, ft_cubic, 0, 0};
    unsigned compared = 0, mismatches = 0;
    for (unsigned face = 0; face < 10; face += 3) {
      TableDirectory d;
      CHECK(face_directory(file, face, d));
      TableSlice slices[MaxTables];
      for (unsigned t = 0; t < d.count; ++t)
        slices[t] = {d.records[t].tag, file.sub(d.records[t].offset, d.records[t].length)};
      Face f;
      CHECK(f.open_tables(slices, d.count, d.version, 50 + face));
      CHECK(f.outline == OutlineFormat::CFF && f.glyphs == 65535 && f.cff_face.cid);
      FT_Face ft;
      CHECK(!FT_New_Memory_Face(lib, bytes.data(), FT_Long(bytes.size()), FT_Long(face), &ft));
      CHECK(!std::strcmp(f.family, ft->family_name));
      for (unsigned gid = 1; gid < f.glyphs; gid += 97) {
        Path mine;
        CHECK(glyph_path(f, gid, PathSink{bsf_segment, &mine}));
        CHECK(!FT_Load_Glyph(ft, gid, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING));
        FtCtx ref;
        CHECK(!FT_Outline_Decompose(&ft->glyph->outline, &funcs, &ref));
        std::string why;
        ++compared;
        if (!same_path(normalize(mine, .55), normalize(ref.p, .55), 1.01, why) && ++mismatches <= 3)
          std::fprintf(stderr, "  CJK face %u gid %u: %s\n", face, gid, why.c_str());
      }
      FT_Done_Face(ft);
    }
    std::printf("  large CJK: %u sampled outlines (faces 0,3,6,9) vs FreeType, %u mismatches\n",
                compared, mismatches);
    CHECK(mismatches == 0);
  }
  BSF::shutdown();
  std::printf("  large CJK collection: loaded lazily, CFF shared across faces\n");
  (void)lib;
}
// E. Installing a collection larger than MaxFile (16 MiB): validated per face
// from metadata tables by range, copied in 1 MiB pieces (BWS 98) without a
// whole-file buffer; corrupt files and write failures leave no target.
static void large_install() {
  files.clear();
  listings.clear();
  config = BSF::Config{};
  config.checksum = BSF::checksum(config);
  add_file("/czcionki/ttf/DejaVuSans.ttf", slurp("czcionki/ttf/DejaVuSans.ttf"));
  std::string big = slurp(dir + "NotoSans-subset.ttc");
  CHECK(!big.empty());
  const size_t base = big.size();
  big.resize(BSF::MaxFile + 3 * 1024 * 1024 + 12345, '\0'); // > 16 MiB, ragged tail
  for (size_t i = base; i < big.size(); i += 4093)
    big[i] = char(i * 131); // non-zero tail: the copy must be exact
  const char *src = "/uzytkownicy/Pobrane/NotoSans-Big.ttc";
  files[src] = big;
  user_reads = true;
  BSF::shutdown();
  unsigned before = BSF::enumerate();
  ranged_reads = whole_reads = 0;
  whole_read_paths.clear();
  fragment_writes = 0;
  largest_fragment = 0;
  CHECK(BSF::install_font(src) == BSF::InstallResult::Ok);
  auto it = files.find("/czcionki/ttc/NotoSans-Big.ttc");
  CHECK(it != files.end() && it->second == big);
  CHECK(fragment_writes == (big.size() + 1024 * 1024 - 1) / (1024 * 1024));
  CHECK(largest_fragment == 1024 * 1024);
  // Never a whole-file read of the 19 MiB source or the installed copy.
  CHECK(!whole_read_paths.count(src) && !whole_read_paths.count("/czcionki/ttc/NotoSans-Big.ttc"));
  unsigned after = BSF::enumerate(), found = 0;
  for (unsigned i = 0; i < after; ++i)
    if (strstr(BSF::info(i)->path, "NotoSans-Big.ttc"))
      ++found;
  CHECK(found == 3 && after == before + 3); // every face catalogued
  CHECK(BSF::install_font(src) == BSF::InstallResult::Exists);
  // Corrupt collection header: rejected before any write.
  std::string bad = big;
  bad[12] = bad[13] = bad[14] = bad[15] = '\xff'; // face 0 offset past EOF
  files["/uzytkownicy/Pobrane/Bad-Big.ttc"] = bad;
  fragment_writes = 0;
  CHECK(BSF::install_font("/uzytkownicy/Pobrane/Bad-Big.ttc") == BSF::InstallResult::InvalidFont);
  CHECK(!files.count("/czcionki/ttc/Bad-Big.ttc") && fragment_writes == 0);
  // Write failure in the middle of the copy: partial target removed.
  files["/uzytkownicy/Pobrane/Fail-Big.ttc"] = big;
  fail_fragment = true;
  fragment_writes = 0;
  fragment_fail_at = 7;
  CHECK(BSF::install_font("/uzytkownicy/Pobrane/Fail-Big.ttc") == BSF::InstallResult::IO);
  CHECK(!files.count("/czcionki/ttc/Fail-Big.ttc"));
  fail_fragment = false;
  // Above MaxFontFile (64 MiB): rejected without reading.
  files["/uzytkownicy/Pobrane/Huge.ttc"] = std::string(BSF::MaxFontFile + 1, '\0');
  CHECK(BSF::install_font("/uzytkownicy/Pobrane/Huge.ttc") == BSF::InstallResult::InvalidFont);
  files.erase("/uzytkownicy/Pobrane/Huge.ttc");
  user_reads = false;
  BSF::shutdown();
  std::printf("  large install: %zu B collection (> MaxFile) copied in %u x <=1 MiB BWS 98 writes, "
              "3 faces catalogued; corrupt / failed-write / >64 MiB leave no target\n",
              big.size(), unsigned((big.size() + 1024 * 1024 - 1) / (1024 * 1024)));
}
int main() {
  FT_Library lib;
  CHECK(!FT_Init_FreeType(&lib));
  in_memory(lib);
  malformed();
  runtime_catalog(lib);
  large_cjk(lib);
  large_install();
  FT_Done_FreeType(lib);
  std::printf("BSF 2.2 collections: %lu checks PASS\n", checks);
}
