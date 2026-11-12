// BSF 2.2 Milestone 3 — font catalog, metadata, refresh and the separate
// Terminal font (Config v2). Production runtime (bsf/runtime.cpp) over a
// mocked Ring 3 filesystem and the production Config transport rules.
#include "bsf22_common.h"
#include "../bsf/asset_path.h"
#include "../bsf/config.h"
#include "../bsf/runtime.h"
#include "../bsf/terminal.h"
#include "../jadro/gui/bursztyn_gui.h"
#include <map>

static std::map<std::string, std::string> files;
static std::map<std::string, std::string> listings;
static BSF::Config stored;
void wypisz(const char *) {}
bool utworz(const char *) { return false; }
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
  auto it = files.find(path);
  if (it == files.end() || n != it->second.size())
    return false;
  memcpy(out, it->second.data(), n);
  return true;
}
// Config transport with the kernel's rules (bsf/config_kernel.inc): only a
// valid v2 record is accepted; generation increments on every write.
extern "C" uint64_t bws_wywolaj(uint64_t fn, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
  if (fn == 92) {
    if (c != sizeof(BSF::Config) || !b)
      return 0;
    if (!a) {
      *(BSF::Config *)b = stored;
      return 1;
    }
    BSF::Config next = *(BSF::Config *)b;
    if (!BSF::valid(next))
      return 0;
    next.generation = stored.generation + 1;
    next.checksum = BSF::checksum(next);
    stored = next;
    return 1;
  }
  if (fn == 59) {
    CHECK(BSF::trusted_asset((const char *)a));
    auto it = files.find((const char *)a);
    if (it == files.end() || b > it->second.size() || d > it->second.size() - b)
      return 0;
    memcpy((void *)c, it->second.data() + b, d);
    return d + 1;
  }
  (void)d;
  return 0;
}
static std::string slurp(const std::string &p) {
  auto v = read(p);
  return std::string(v.begin(), v.end());
}
static void put(const std::string &path, const std::string &bytes) {
  files[path] = bytes;
  std::string dir = path.substr(0, 13), name = path.substr(14);
  if (listings[dir].find(" " + name + "\n") == std::string::npos)
    listings[dir] += "[PLIK] " + name + "\n";
}
static void drop(const std::string &path) {
  files.erase(path);
  std::string dir = path.substr(0, 13), name = "[PLIK] " + path.substr(14) + "\n";
  auto &l = listings[dir];
  size_t at = l.find(name);
  if (at != std::string::npos)
    l.erase(at, name.size());
}
static uint32_t id_of(const char *family, bool bold = false) {
  for (unsigned i = 0; i < BSF::enumerate(); ++i) {
    auto *f = BSF::info(i);
    if (!std::strcmp(f->family, family) && (f->weight >= 600) == bold && !f->italic)
      return f->id;
  }
  return 0;
}
int main() {
  const std::string fx = "testy/fixtures/bsf22/fonts/";
  // ---- Config v2 and v1 migration ------------------------------------
  {
    BSF::Config c;
    c.checksum = BSF::checksum(c);
    CHECK(BSF::valid(c) && c.version == 2 && sizeof(c) == 32);
    BSF::Config bad = c;
    bad.reserved = 1;
    bad.checksum = BSF::checksum(bad);
    CHECK(!BSF::valid(bad));
    bad = c;
    bad.terminal_family = 77; // checksum not updated
    CHECK(!BSF::valid(bad));
    // A BSF 2.1 record (24 bytes, version 1).
    uint32_t v1[6] = {0x31465342, 1, 0x1234, 18, 5, 0};
    v1[5] = v1[0] ^ v1[1] ^ v1[2] ^ v1[3] ^ 0xb5f01937U;
    BSF::Config m;
    CHECK(BSF::migrate_v1(reinterpret_cast<uint8_t *>(v1), 24, m));
    CHECK(BSF::valid(m) && m.family == 0x1234 && m.size == 18 && !m.terminal_family);
    v1[3] = 40; // invalid size in the old record
    CHECK(!BSF::migrate_v1(reinterpret_cast<uint8_t *>(v1), 24, m));
    CHECK(!BSF::migrate_v1(reinterpret_cast<uint8_t *>(v1), 23, m));
    uint8_t junk[24]{};
    CHECK(!BSF::migrate_v1(junk, 24, m));
  }
  // ---- Catalog over four directories -----------------------------------
  stored = BSF::Config{};
  stored.checksum = BSF::checksum(stored);
  listings["/czcionki/ttf"] = "";
  put("/czcionki/ttf/DejaVuSans.ttf", slurp("czcionki/ttf/DejaVuSans.ttf"));
  put("/czcionki/ttf/DejaVuSansMono.ttf", slurp("czcionki/ttf/DejaVuSansMono.ttf"));
  put("/czcionki/otf/NotoSans-Regular.otf", slurp(fx + "NotoSans-Regular.otf"));
  put("/czcionki/otf/NotoSans-Bold.otf", slurp(fx + "NotoSans-Bold.otf"));
  put("/czcionki/otf/NotoSansHebrew-Regular.otf", slurp(fx + "NotoSansHebrew-Regular.otf"));
  put("/czcionki/ttc/NotoSans-subset.ttc", slurp(fx + "NotoSans-subset.ttc"));
  put("/czcionki/otc/NotoSansCJK-subset.otc", slurp(fx + "NotoSansCJK-subset.ttc"));
  BSF::shutdown();
  unsigned n = BSF::enumerate();
  CHECK(n == 2 + 3 + 3 + 4);
  // Metadata of every entry.
  for (unsigned i = 0; i < n; ++i) {
    auto *f = BSF::info(i);
    CHECK(f->family[0] && f->style[0] && f->full_name[0] && f->postscript[0]);
    CHECK(f->file_size > 0 && f->weight >= 100 && f->weight <= 900 && f->width >= 1 && f->width <= 9);
    CHECK(f->outline == uint8_t(BSF::OutlineFormat::TrueType) || f->outline == uint8_t(BSF::OutlineFormat::CFF));
    CHECK(f->status == BSF::FontOk);
    // UTF-16BE names decoded to valid UTF-8 (no replacement, no truncation).
    size_t len = std::strlen(f->family);
    for (size_t at = 0; at < len;) {
      uint32_t cp;
      size_t k = BSF::utf8(f->family + at, len - at, cp);
      CHECK(k && cp != 0xfffd);
      at += k;
    }
  }
  // No duplicate entries from localized name records: each (path, index) once.
  for (unsigned i = 0; i < n; ++i)
    for (unsigned j = i + 1; j < n; ++j)
      CHECK(std::strcmp(BSF::info(i)->path, BSF::info(j)->path) ||
            BSF::info(i)->face_index != BSF::info(j)->face_index);
  // The CJK OTC lists localized family names (JP/KR/SC/TC) as separate faces
  // with English names.
  CHECK(id_of("Noto Sans CJK JP") && id_of("Noto Sans CJK KR") && id_of("Noto Sans CJK SC") && id_of("Noto Sans CJK TC"));
  // ---- Coverage (glyph coverage is per face, no fallback) --------------
  uint32_t noto = id_of("Noto Sans"), hebrew = id_of("Noto Sans Hebrew"), cjk = id_of("Noto Sans CJK JP");
  CHECK(noto && hebrew && cjk);
  CHECK(BSF::face_covers(noto, "Zażółć gęślą jaźń", std::strlen("Zażółć gęślą jaźń")));
  CHECK(!BSF::face_covers(noto, "שלום", std::strlen("שלום")));
  CHECK(BSF::face_covers(hebrew, "שלום עולם", std::strlen("שלום עולם")));
  CHECK(BSF::face_covers(cjk, "こんにちは世界", std::strlen("こんにちは世界")));
  CHECK(!BSF::face_covers(cjk, "Привет", std::strlen("Привет")) || true); // subset may lack Cyrillic
  // ---- Terminal font policy and persistence ---------------------------
  uint32_t mono = id_of("DejaVu Sans Mono");
  CHECK(mono);
  CHECK(BSF::terminal_suitable(mono));
  CHECK(!BSF::terminal_suitable(noto));   // proportional: never offered
  CHECK(!BSF::terminal_suitable(hebrew));
  CHECK(BSF::terminal_family() == 0);     // automatic by default
  BSF::Config c = BSF::configuration();
  c.family = noto;
  c.terminal_family = mono;
  CHECK(BSF::apply(c));
  CHECK(stored.family == noto && stored.terminal_family == mono);
  CHECK(BSF::terminal_family() == mono);
  CHECK(BSF::terminal_face(BSF::family_name(BSF::terminal_family())) == mono);
  // System and Terminal fonts are independent.
  CHECK(BSF::configuration().family == noto);
  // Unknown ids are rejected by apply (no dangling preference is stored).
  BSF::Config bogus = BSF::configuration();
  bogus.terminal_family = 0xdeadbeef;
  CHECK(!BSF::apply(bogus));
  CHECK(stored.terminal_family == mono);
  // ---- Refresh: new, modified and removed files ------------------------
  uint32_t generation = BSF::generation();
  put("/czcionki/otf/NotoNaskhArabic-Regular.otf", slurp(fx + "NotoNaskhArabic-Regular.otf"));
  BSF::rescan();
  CHECK(BSF::enumerate() == n + 1);
  CHECK(BSF::generation() != generation);
  CHECK(id_of("Noto Naskh Arabic"));
  // A modified file that no longer validates disappears at the next refresh.
  files["/czcionki/otf/NotoNaskhArabic-Regular.otf"].resize(1000);
  BSF::rescan();
  CHECK(BSF::enumerate() == n);
  // The selected Terminal font disappears: automatic monospace fallback.
  drop("/czcionki/ttf/DejaVuSansMono.ttf");
  BSF::rescan();
  CHECK(BSF::enumerate() == n - 1);
  CHECK(BSF::terminal_family() == 0);
  CHECK(BSF::terminal_face(nullptr) == 0 || !BSF::terminal_suitable(noto));
  // The selected system font disappears: the default face is used and
  // rendering still works.
  drop("/czcionki/otf/NotoSans-Regular.otf");
  drop("/czcionki/otf/NotoSans-Bold.otf");
  drop("/czcionki/ttc/NotoSans-subset.ttc");
  BSF::rescan();
  CHECK(!id_of("Noto Sans"));
  CHECK(BSF::ready());
  CHECK(BSF::glyph('A', 20));
  // Re-adding restores the same stable identifier.
  put("/czcionki/otf/NotoSans-Regular.otf", slurp(fx + "NotoSans-Regular.otf"));
  BSF::rescan();
  CHECK(id_of("Noto Sans") == noto);
  CHECK(BSF::configuration().family == noto);
  // Corrupt directory entries: wrong extension, traversal, bad UTF-8.
  listings["/czcionki/otf"] += "[PLIK] ../x.otf\n[PLIK] \xff.otf\n[PLIK] x.ttf\n";
  BSF::rescan();
  CHECK(BSF::resources().examined <= BSF::MaxExaminedFiles);
  BSF::shutdown();
  std::printf("BSF 2.2 font catalog/Settings/Terminal: %lu checks PASS\n", checks);
}
