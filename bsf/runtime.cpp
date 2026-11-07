#include "runtime.h"
#include "../bursztyn_gui.h"
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
uint32_t catalog_epoch{};
struct Resolution { uint32_t primary{}, cp{}, result{}; unsigned style{}; bool valid{}; };
constexpr unsigned ResolutionSlots=128;
Resolution resolutions[ResolutionSlots];
unsigned resolution_cursor{};
struct ClusterResolution { uint32_t primary{},result{}; unsigned style{},size{}; bool valid{}; char text[128]{}; };
ClusterResolution clusters[16];unsigned cluster_cursor{};
void invalidate_resolution() { for(auto& r:resolutions) r=Resolution{}; resolution_cursor=0;for(auto& r:clusters)r.valid=false;cluster_cursor=0; }

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
int compare(const char* a,const char* b){
  for(unsigned i=0;i<8192;++i){unsigned x=uint8_t(a[i]),y=uint8_t(b[i]);if(x!=y)return int(x)-int(y);if(!x)return 0;}return 0;
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
  // Sort all bounded directory records before applying the file-count cap.
  auto** records=new char*[1024]; auto* sorted=new char[8192];
  if(!records || !sorted){delete[] records;delete[] sorted;delete[] list;return;}
  unsigned lines=0; size_t begin=0;
  for(size_t i=0;i<8191 && list[i];++i) if(list[i]=='\n'){
    list[i]=0;if(lines<1024)records[lines++]=list+begin;begin=i+1;
  }
  if(begin<8191 && list[begin] && lines<1024)records[lines++]=list+begin;
  for(unsigned i=1;i<lines;++i){auto* value=records[i];unsigned j=i;
    while(j && compare(records[j-1],value)>0){records[j]=records[j-1];--j;}records[j]=value;}
  size_t bytes=0;
  for(unsigned i=0;i<lines;++i){size_t n=0;while(records[i][n])++n;if(n+1>=8192-bytes)break;
    byte_copy(sorted+bytes,records[i],n);bytes+=n;sorted[bytes++]='\n';}
  sorted[bytes]=0;delete[] records;delete[] list;list=sorted;
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
  // Stable ordering for family/style UI and fallback priority.
  for(unsigned i=1;i<count;++i){
    FontInfo value=catalog[i]; unsigned j=i;
    while(j && (compare(catalog[j-1].family,value.family)>0 ||
      (!compare(catalog[j-1].family,value.family) && compare(catalog[j-1].path,value.path)>0))){
      catalog[j]=catalog[j-1];--j;
    }catalog[j]=value;
  }
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
// One face resolution path for measurement, drawing and cache queries.
const FontInfo* resolve(uint32_t cp,const char* family,bool bold,bool italic){
  const auto* primary=choose(family,bold,italic);
  if(!primary || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return nullptr;
  unsigned style=unsigned(bold)|(unsigned(italic)<<1);
  for(const auto& r:resolutions) if(r.valid && r.primary==primary->id && r.cp==cp && r.style==style){
    if(!r.result) return nullptr;
    for(unsigned i=0;i<count;++i) if(catalog[i].id==r.result) return catalog+i;
  }
  const FontInfo* result=nullptr;
  auto supports=[&](const FontInfo* item){auto* f=load(item);return f && f->glyph(cp)!=0;};
  if(supports(primary)) result=primary;
  // Same family first, then matching weight/style, then remaining installed faces.
  for(unsigned pass=0;pass<3 && !result;++pass)
    for(unsigned i=0;i<count;++i){
      const auto* candidate=catalog+i;
      bool eligible=pass==0 ? same(primary->family,candidate->family) :
        pass==1 ? candidate->italic==italic && (candidate->weight>=600)==bold : true;
      if(eligible && supports(candidate)){result=candidate;break;}
    }
  auto& r=resolutions[resolution_cursor++%ResolutionSlots];
  r={primary->id,cp,result?result->id:0,style,true};
  return result;
}
} // namespace
uint32_t resolved_cluster(const char* text,size_t size,bool bold,const char* family,bool italic){
  if(!text || !size || size>65536)return 0;
  const auto* primary=choose(family,bold,italic);if(!primary)return 0;
  unsigned style=unsigned(bold)|(unsigned(italic)<<1);
  if(size<=128)for(const auto& r:clusters)if(r.valid&&r.primary==primary->id&&r.style==style&&r.size==size){
    bool equal=true;for(size_t i=0;i<size;++i)equal&=r.text[i]==text[i];if(equal)return r.result;
  }
  bool tried[MaxFiles]{};
  auto covers=[&](const FontInfo* item){
    size_t index=size_t(item-catalog);if(tried[index])return false;tried[index]=true;
    auto* f=load(item);if(!f)return false;
    for(size_t at=0;at<size;){uint32_t cp;at+=utf8(text+at,size-at,cp);if(!default_ignorable(cp)&&!f->glyph(cp))return false;}
    return true;
  };
  uint32_t result=covers(primary)?primary->id:0;
  for(unsigned pass=0;pass<3&&!result;++pass)for(unsigned i=0;i<count;++i){
    auto* candidate=catalog+i;
    bool eligible=pass==0?same(primary->family,candidate->family):pass==1?candidate->italic==italic&&(candidate->weight>=600)==bold:true;
    if(eligible&&covers(candidate)){result=candidate->id;break;}
  }
  if(size<=128){auto& r=clusters[cluster_cursor++%16];r.primary=primary->id;r.result=result;r.style=style;r.size=unsigned(size);r.valid=true;byte_copy(r.text,text,size);}
  return result;
}
Face* by_id(uint32_t id){init();for(unsigned i=0;i<count;++i)if(catalog[i].id==id)return load(catalog+i);return nullptr;}
const Glyph* glyph_face(uint32_t cp,uint32_t id,int px,bool bold,bool italic){auto* f=by_id(id);return f&&cache?cache->get(*f,cp,px,unsigned(bold)|(unsigned(italic)<<1)):nullptr;}
int advance_face(uint32_t cp,uint32_t id,int px){if(combining(cp)||default_ignorable(cp))return 0;auto* f=by_id(id);return f?f->advance(f->glyph(cp),px):-1;}
int kern_face(uint32_t l,uint32_t r,uint32_t id,int px){auto* f=by_id(id);return f?f->kerning(f->glyph(l),f->glyph(r),px):0;}
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
  return config.generation+catalog_epoch;
}
uint32_t resolved_face(uint32_t cp,bool bold,const char* family,bool italic){
  const auto* item=resolve(cp,family,bold,italic);return item?item->id:0;
}
int measure_cp(uint32_t cp,int px,bool bold,const char* family){
  return measure_styled_cp(cp,px,bold,family,false);
}
int measure_styled_cp(uint32_t cp, int px, bool bold, const char *family, bool italic) {
  if(combining(cp)||default_ignorable(cp)) return 0;
  auto* f=load(resolve(cp,family,bold,italic));
  return f?f->advance(f->glyph(cp),px):-1;
}
int measure_text(const char *text, int px, bool bold, const char *family,
                 size_t limit) {
  if (!text)
    return 0;
  size_t n = 0;
  while (n < limit && text[n])
    ++n;
  int64_t width = 0;
  uint32_t previous=0,previous_face=0;
  for(size_t start=0;start<n;){
    size_t end=next_cluster(text,start,n);uint32_t selected=resolved_cluster(text+start,end-start,bold,family);
    for(size_t at=start;at<end;){uint32_t cp;at+=utf8(text+at,end-at,cp);
      if(default_ignorable(cp)||combining(cp))continue;
      uint32_t id=selected?selected:resolved_face(cp,bold,family);
      int advance=id?advance_face(cp,id,px):px/2+1;
      width+=advance+(previous&&id&&id==previous_face?kern_face(previous,cp,id,px):0);
      if(width>INT32_MAX)return INT32_MAX;
      previous=cp;previous_face=id;
    }start=end;
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
  if(combining(l) || combining(r))return 0;
  const auto* left=resolve(l,family,bold,false);
  const auto* right=resolve(r,family,bold,false);
  if(!left || !right || left->id!=right->id) return 0;
  auto* f=load(left);
  return f?f->kerning(f->glyph(l),f->glyph(r),px):0;
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
  auto* f=load(resolve(cp,family,bold,italic));
  return f && cache?cache->get(*f,cp,px,unsigned(bold)|(unsigned(italic)<<1)):nullptr;
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
  const auto *chosen = resolve(cp, family, bold, italic);
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
void rescan() {
  ++catalog_epoch;
  // Font operations are serialized by the app event loop. Pinned masks stay
  // valid across Cache::clear; file-backed Faces are never exposed to callers.
  if(cache) cache->clear();
  for(auto& item:loaded){delete[] item.bytes;item=Loaded{};}
  invalidate_resolution(); initialized=false; init();
}
void shutdown() {
  invalidate_resolution();
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
