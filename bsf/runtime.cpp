#include "runtime.h"
#include "bursztyn_gui.h"
#include <string.h>
namespace BSF {
namespace {
void byte_copy(void *target, const void *source, size_t n) {
  auto *d = static_cast<volatile uint8_t *>(target);
  auto *p = static_cast<const uint8_t *>(source);
  for (size_t i = 0; i < n; ++i)
    d[i] = p[i];
}
void byte_set(void *target, int v, size_t n) {
  auto *d = static_cast<volatile uint8_t *>(target);
  for (size_t i = 0; i < n; ++i)
    d[i] = uint8_t(v);
}
FontInfo catalog[MaxFiles];
unsigned count{};
bool initialized{}, disabled{};
Config config{}, temporary{};
bool previewing{};
struct Loaded {
  uint8_t *bytes{};
  Face face{};
  uint64_t used{};
} loaded[MaxFaces];
Cache *cache{};
uint64_t clock{};
uint32_t identity(const char *name) {
  uint32_t h = 2166136261u;
  for (unsigned i = 0; name[i]; ++i) {
    unsigned c = uint8_t(name[i]);
    h = (h ^ c) * 16777619u;
  }
  return h && h != 0xffffffffU ? h : 1;
}
bool same(const char *a, const char *b) {
  for (unsigned i = 0; i < 160; ++i) {
    unsigned x = uint8_t(a[i]), y = uint8_t(b[i]);
    if (x >= 'A' && x <= 'Z')
      x += 32;
    if (y >= 'A' && y <= 'Z')
      y += 32;
    if (x != y)
      return false;
    if (!x)
      return true;
  }
  return false;
}
uint8_t *read(const char *path, uint32_t &size) {
  size = 0;
  if (!pobierz_rozmiar_pliku(path, &size) || size < 12 || size > MaxFile)
    return nullptr;
  auto *p = new uint8_t[size];
  if (p)
    byte_set(p, 0, size);
  if (p && !czytaj_plik(path, reinterpret_cast<char *>(p), size)) {
    delete[] p;
    p = nullptr;
  }
  return p;
}
void init() {
  if (initialized)
    return;
  initialized = true;
  if (!cache)
    cache = new Cache;
  count = 0;
  config = Config{};
  config.checksum = checksum(config);
  Config c{};
  if (bws_wywolaj(92, 0, reinterpret_cast<uint64_t>(&c), sizeof(c), 0) == 1 &&
      valid(c))
    config = c;
  auto *list = new char[8192];
  if (!list)
    return;
  byte_set(list, 0, 8192);
  if (!wylistuj_katalog_uzytkownika("/czcionki/ttf", list, 8192)) {
    delete[] list;
    wypisz("[BSF] Font directory unavailable; bitmap fallback.\n");
    return;
  }
  unsigned examined = 0;
  for (size_t at = 0; at < 8191 && list[at] && examined < MaxFiles;) {
    size_t end = at;
    while (end < 8191 && list[end] && list[end] != '\n')
      ++end;
    size_t start = at;
    while (start < end && list[start] != ']')
      ++start;
    if (start < end)
      ++start;
    while (start < end && list[start] == ' ')
      ++start;
    size_t n = end - start;
    bool safe = n > 4 && n < 120;
    for (size_t i = start; i < end && safe; ++i) {
      unsigned c = uint8_t(list[i]);
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' ||
            c == ' '))
        safe = false;
    }
    char path[160] = "/czcionki/ttf/";
    if (safe) {
      byte_copy(path + 14, list + start, n);
      path[14 + n] = 0;
      safe = path[14 + n - 4] == '.' &&
             (path[14 + n - 3] == 't' || path[14 + n - 3] == 'T') &&
             (path[14 + n - 2] == 't' || path[14 + n - 2] == 'T') &&
             (path[14 + n - 1] == 'f' || path[14 + n - 1] == 'F');
    }
    if (safe) {
      ++examined;
      uint32_t bytes = 0;
      auto *p = read(path, bytes);
      Face f;
      bool unique = true;
      const uint32_t id = identity(path);
      for (unsigned i = 0; i < count; ++i)
        if (catalog[i].id == id)
          unique = false;
      if (p && unique && f.open(p, bytes, id)) {
        auto &item = catalog[count++];
        item.id = f.id;
        byte_copy(item.path, path, sizeof(path));
        byte_copy(item.family, f.family, sizeof(f.family));
        byte_copy(item.style, f.style, sizeof(f.style));
        item.weight = f.weight;
        item.italic = f.italic;
        item.mono = f.mono;
        item.serif = f.serif;
      } else
        wypisz("[BSF] Rejected font; fallback retained.\n");
      delete[] p;
    }
    at = end + (end < 8191 && list[end] == '\n');
  }
  delete[] list;
  bool selected_found = !config.family;
  for (unsigned i = 0; i < count; ++i)
    selected_found |= catalog[i].id == config.family;
  if (!selected_found)
    wypisz("[BSF] Selected font missing; using bundled/bitmap fallback.\n");
  if (!count)
    wypisz("[BSF] No usable TTF; bitmap fallback remains active.\n");
}
const FontInfo *default_info() {
  for (unsigned i = 0; i < count; ++i)
    if (!catalog[i].mono && !catalog[i].italic && catalog[i].weight < 600)
      return &catalog[i];
  for (unsigned i = 0; i < count; ++i)
    if (!catalog[i].italic && catalog[i].weight < 600)
      return &catalog[i];
  return count ? catalog : nullptr;
}
const FontInfo *selected() {
  const auto &c = previewing ? temporary : config;
  for (unsigned i = 0; i < count; ++i)
    if (catalog[i].id == c.family)
      return catalog + i;
  return default_info();
}
const FontInfo *choose(const char *families, bool bold, bool italic) {
  if (disabled)
    return nullptr;
  init();
  const FontInfo *base = selected();
  if (families) {
    size_t at = 0;
    while (families[at] && at < 2048) {
      char name[96]{};
      unsigned used = 0;
      while (families[at] == ' ' || families[at] == '\'' || families[at] == '"')
        ++at;
      while (families[at] && families[at] != ',' && at < 2048) {
        char c = families[at++];
        if (c != '\'' && c != '"' && used + 1 < sizeof(name))
          name[used++] = c;
      }
      while (used && name[used - 1] == ' ')
        name[--used] = 0;
      if (families[at] == ',')
        ++at;
      const FontInfo *found = nullptr;
      for (unsigned i = 0; i < count; ++i)
        if (same(name, catalog[i].family)) {
          found = catalog + i;
          if (!catalog[i].italic && catalog[i].weight < 600)
            break;
        }
      if (found) {
        base = found;
        break;
      }
      if (same(name, "monospace")) {
        base = nullptr;
        for (unsigned i = 0; i < count; ++i)
          if (catalog[i].mono && !catalog[i].italic &&
              catalog[i].weight < 600) {
            base = catalog + i;
            break;
          }
        return base;
      }
      if (same(name, "serif")) {
        for (unsigned i = 0; i < count; ++i)
          if (catalog[i].serif && !catalog[i].italic &&
              catalog[i].weight < 600) {
            base = catalog + i;
            break;
          }
        break;
      }
      if (same(name, "sans-serif") || same(name, "system-ui"))
        break;
    }
  }
  if (!base)
    return nullptr;
  const FontInfo *regular = base;
  for (unsigned i = 0; i < count; ++i)
    if (same(base->family, catalog[i].family) && catalog[i].italic == italic &&
        ((catalog[i].weight >= 600) == bold))
      return catalog + i;
  for (unsigned i = 0; i < count; ++i)
    if (same(base->family, catalog[i].family) && !catalog[i].italic &&
        ((catalog[i].weight >= 600) == bold))
      return catalog + i;
  return regular;
}
Face *load(const FontInfo *item) {
  if (!item)
    return nullptr;
  unsigned oldest = 0;
  for (unsigned i = 0; i < MaxFaces; ++i) {
    if (loaded[i].face.id == item->id) {
      loaded[i].used = ++clock;
      return &loaded[i].face;
    }
    if (!loaded[i].bytes ||
        (loaded[oldest].bytes && loaded[i].used < loaded[oldest].used))
      oldest = i;
  }
  uint32_t n;
  auto *bytes = read(item->path, n);
  Face face;
  if (!bytes || !face.open(bytes, n, item->id)) {
    delete[] bytes;
    return nullptr;
  }
  delete[] loaded[oldest].bytes;
  loaded[oldest].bytes = bytes;
  loaded[oldest].face = face;
  loaded[oldest].used = ++clock;
  return &loaded[oldest].face;
}
Face *face(const char *families, bool bold = false, bool italic = false) {
  const auto *choice = choose(families, bold, italic);
  bool mono = choice ? choice->mono : families && same(families, "monospace");
  auto *f = load(choice);
  if (f)
    return f;
  for (unsigned i = 0; i < count; ++i)
    if (!catalog[i].italic && catalog[i].mono == mono) {
      f = load(catalog + i);
      if (f)
        return f;
    }
  return nullptr;
}
} // namespace
unsigned enumerate() {
  init();
  return count;
}
uint32_t default_id() {
  init();
  auto *f = default_info();
  return f ? f->id : 0;
}
const FontInfo *info(unsigned i) {
  init();
  return i < count ? catalog + i : nullptr;
}
Config configuration() {
  init();
  return config;
}
void refresh() {
  init();
  Config c{};
  if (bws_wywolaj(92, 0, reinterpret_cast<uint64_t>(&c), sizeof(c), 0) == 1 &&
      valid(c) && c.generation != config.generation) {
    bool known = !c.family;
    for (unsigned i = 0; i < count; ++i)
      known |= catalog[i].id == c.family;
    uint32_t old_family = config.family ? config.family : default_id();
    uint32_t next_family = c.family ? c.family : default_id();
    bool changed_face = !known || next_family != old_family;
    if (!known) {
      initialized = false;
      init();
    } else
      config = c;
    if (cache && changed_face)
      cache->clear();
  }
}
bool apply(Config c) {
  init();
  c.checksum = checksum(c);
  if (!valid(c))
    return false;
  bool found = !c.family;
  for (unsigned i = 0; i < count; ++i)
    found |= catalog[i].id == c.family;
  if (!found)
    return false;
  if (bws_wywolaj(92, 1, reinterpret_cast<uint64_t>(&c), sizeof(c), 0) != 1)
    return false;
  refresh();
  return true;
}
void monospace(bool value) { disabled = value; }
bool monospace() { return disabled; }
int ui_size() {
  init();
  return int((previewing ? temporary : config).size);
}
uint32_t generation() {
  init();
  return config.generation;
}
int measure_cp(uint32_t cp, int px, bool bold, const char *family) {
  auto *f = face(family, bold);
  if (!f)
    return -1;
  if (f->glyph(cp))
    return f->advance(f->glyph(cp), px);
  for (unsigned i = 0; i < count; ++i) {
    f = load(catalog + i);
    if (f && f->glyph(cp))
      return f->advance(f->glyph(cp), px);
  }
  return -1;
}
int measure_text(const char *text, int px, bool bold, const char *family,
                 size_t limit) {
  if (!text)
    return 0;
  size_t n = 0;
  while (n < limit && text[n])
    ++n;
  int64_t width = 0;
  uint32_t previous = 0;
  for (size_t at = 0; at < n;) {
    uint32_t cp;
    at += utf8(text + at, n - at, cp);
    int a = measure_cp(cp, px, bold, family);
    if (a < 0)
      a = px / 2 + 1;
    width += a + (previous ? kern(previous, cp, px, bold, family) : 0);
    if (width > INT32_MAX)
      return INT32_MAX;
    previous = cp;
  }
  return int(width);
}
uint32_t resolve_family(const char *list) {
  if (!list || !*list)
    return 0;
  const auto *value = choose(list, false, false);
  return value ? value->id : 0xffffffffU;
}
const char *family_name(uint32_t id) {
  init();
  if (id == 0xffffffffU)
    return "monospace";
  for (unsigned i = 0; i < count; ++i)
    if (catalog[i].id == id)
      return catalog[i].family;
  return nullptr;
}
int kern(uint32_t l, uint32_t r, int px, bool bold, const char *family) {
  auto *f = face(family, bold);
  return f ? f->kerning(f->glyph(l), f->glyph(r), px) : 0;
}
int height(int px, const char *family) {
  auto *f = face(family);
  return f ? f->line_height(px) : -1;
}
int ascent(int px, const char *family) {
  auto *f = face(family);
  return f ? int((int64_t(f->ascent) * px + f->units - 1) / f->units) : px;
}
bool ready(const char *family) { return face(family) != nullptr; }
const Glyph *glyph(uint32_t cp, int px, bool bold, const char *family,
                   bool italic) {
  auto *f = face(family, bold, italic);
  if (!f || !cache)
    return nullptr;
  if (!f->glyph(cp)) {
    for (unsigned i = 0; i < count; ++i) {
      auto *other = load(catalog + i);
      if (other && other->glyph(cp))
        return cache->get(*other, cp, px,
                          unsigned(bold) | (unsigned(italic) << 1));
    }
    return nullptr;
  }
  return cache->get(*f, cp, px, unsigned(bold) | (unsigned(italic) << 1));
}
bool family_monospace(uint32_t id) {
  init();
  if (id == 0xffffffffU)
    return true;
  for (unsigned i = 0; i < count; ++i)
    if (catalog[i].id == id)
      return catalog[i].mono;
  return false;
}
const Statistics &statistics() {
  static const Statistics empty{};
  return cache ? cache->stats : empty;
}
bool cached(uint32_t cp, int px, bool bold, const char *family, bool italic) {
  const auto *chosen = choose(family, bold, italic);
  if (!cache || !chosen)
    return false;
  for (const auto &loaded_face : loaded)
    if (loaded_face.face.id == chosen->id)
      return cache->contains(loaded_face.face, cp, px,
                             unsigned(bold) | (unsigned(italic) << 1));
  return false;
}
void pin(const Glyph *g) {
  if (g && g->pins < 65535)
    ++const_cast<Glyph *>(g)->pins;
}
void unpin(void *value) {
  auto *g = static_cast<Glyph *>(value);
  if (g && g->pins)
    --g->pins;
}
void preview(uint32_t id, int size) {
  temporary = config;
  temporary.family = id;
  temporary.size = unsigned(size);
  previewing = true;
}
void end_preview() { previewing = false; }
void shutdown() {
  for (auto &item : loaded) {
    delete[] item.bytes;
    item = Loaded{};
  }
  delete cache;
  cache = nullptr;
  count = 0;
  initialized = false;
  previewing = false;
  disabled = false;
}
} // namespace BSF
