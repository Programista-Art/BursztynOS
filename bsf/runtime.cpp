#include "runtime.h"
#include "asset_path.h"
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
// Bounded process-local storage; expanding the catalog must not consume every
// application's fixed BUR DATA reservation, including apps using bitmap only.
FontInfo *catalog{};
unsigned count{};
ResourceInfo resource_info{};
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
  uint8_t *bytes{};uint32_t size{};
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
uint8_t *read(const char *path,uint32_t& size,unsigned budget=MaxFile){
  size=0;
  if(!pobierz_rozmiar_pliku(path,&size)||size<12||size>MaxFile||size>budget)return nullptr;
  auto* p=new uint8_t[size];if(!p)return nullptr;
  bool ok=true;
  if(size<=1024*1024){byte_set(p,0,size);ok=czytaj_plik(path,reinterpret_cast<char*>(p),size);}
  else for(uint32_t at=0;at<size;){
    uint32_t n=size-at;if(n>65536)n=65536;
    uint64_t got=bws_wywolaj(59,reinterpret_cast<uint64_t>(path),at,reinterpret_cast<uint64_t>(p+at),n);
    if(got!=uint64_t(n)+1){ok=false;break;}at+=n;
  }
  uint32_t after=0;if(!pobierz_rozmiar_pliku(path,&after)||after!=size)ok=false;
  if(!ok){delete[] p;return nullptr;}resource_info.io_bytes+=size;return p;
}
void coverage(FontInfo& item,const Face& f){
  auto range=[&](uint32_t a,uint32_t b){if(b>0x10ffff)b=0x10ffff;for(uint32_t block=a>>12;block<=b>>12;++block)item.coverage[block/8]|=uint8_t(1u<<(block%8));};
  if(f.cmap.u16(0)==12){for(unsigned i=0;i<f.cmap.u32(12);++i)range(f.cmap.u32(16+i*12),f.cmap.u32(20+i*12));}
  else {unsigned n=f.cmap.u16(6)/2;for(unsigned i=0;i<n;++i)range(f.cmap.u16(16+n*2+i*2),f.cmap.u16(14+i*2));}
}
bool may_cover(const FontInfo& item,uint32_t cp){return cp<=0x10ffff && (item.coverage[(cp>>12)/8]&(1u<<((cp>>12)%8)));}
void init() {
  if (initialized)
    return;
  initialized = true;
  if (!cache)
    cache = new Cache;
  count = 0;
  if (!catalog) catalog = new FontInfo[MaxFiles];
  if (!catalog) { resource_info.directory_error=true; return; }
  config = Config{};
  config.checksum = checksum(config);
  Config c{};
  if (bws_wywolaj(92, 0, reinterpret_cast<uint64_t>(&c), sizeof(c), 0) == 1 &&
      valid(c))
    config = c;
  resource_info.examined=resource_info.rejected=0;resource_info.catalog_limit=resource_info.directory_error=false;
  auto *list = new char[DirectoryBytes];
  if (!list){resource_info.directory_error=true;wypisz("[BSF] Directory allocation failed; bitmap fallback.\n");return;}
  byte_set(list, 0, DirectoryBytes);
  if (!wylistuj_katalog_uzytkownika("/czcionki/ttf", list, DirectoryBytes)) {
    delete[] list;
    resource_info.directory_error=true;
    wypisz("[BSF] Directory unavailable or exceeds 64 KiB; bitmap fallback.\n");
    return;
  }
  // Sort all bounded directory records before applying the file-count cap.
  auto** records=new char*[1024]; auto* sorted=new char[DirectoryBytes];
  if(!records || !sorted){
    delete[] records;delete[] sorted;delete[] list;
    resource_info.directory_error=true;
    wypisz("[BSF] Sort OOM.\n");
    return;
  }
  unsigned lines=0; size_t begin=0;
  for(size_t i=0;i<DirectoryBytes-1 && list[i];++i) if(list[i]=='\n'){
    list[i]=0;if(lines<1024)records[lines++]=list+begin;begin=i+1;
  }
  if(begin<DirectoryBytes-1 && list[begin] && lines<1024)records[lines++]=list+begin;
  for(unsigned i=1;i<lines;++i){auto* value=records[i];unsigned j=i;
    while(j && compare(records[j-1],value)>0){records[j]=records[j-1];--j;}records[j]=value;}
  size_t bytes=0;
  for(unsigned i=0;i<lines;++i){size_t n=0;while(records[i][n])++n;if(n+1>=DirectoryBytes-bytes)break;
    byte_copy(sorted+bytes,records[i],n);bytes+=n;sorted[bytes++]='\n';}
  sorted[bytes]=0;delete[] records;delete[] list;list=sorted;
  unsigned examined = 0;
  for (size_t at = 0; at < DirectoryBytes-1 && list[at] && examined < MaxExaminedFiles && count < MaxFiles;) {
    size_t end = at;
    while (end < DirectoryBytes-1 && list[end] && list[end] != '\n')
      ++end;
    size_t start = at;
    while (start < end && list[start] != ']')
      ++start;
    if (start < end)
      ++start;
    while (start < end && list[start] == ' ')
      ++start;
    size_t n = end - start;
    bool safe=font_filename(list+start,n);
    char path[160]="/czcionki/ttf/";
    if(safe){byte_copy(path+14,list+start,n);path[14+n]=0;}
    if (safe) {
      ++examined;resource_info.examined=examined;
      uint32_t bytes = 0;
      auto *p = read(path, bytes);
      Face f;
      bool unique = true;
      const uint32_t id = identity(path);
      for (unsigned i = 0; i < count; ++i)
        if (catalog[i].id == id)
          unique = false;
      if (p && unique && f.open(p, bytes, id)) {
        auto &item = catalog[count++];item=FontInfo{};
        item.id = f.id;
        byte_copy(item.path, path, sizeof(path));
        byte_copy(item.family, f.family, sizeof(f.family));
        byte_copy(item.style, f.style, sizeof(f.style));
        item.weight = f.weight;
        item.italic = f.italic;
        item.mono = f.mono;
        item.serif = f.serif;coverage(item,f);
      } else {++resource_info.rejected;
        wypisz("[BSF] Rejected font; fallback retained.\n");}
      delete[] p;
    }
    at = end + (end < DirectoryBytes-1 && list[end] == '\n');
  }
  resource_info.catalog_limit=count==MaxFiles || examined==MaxExaminedFiles || lines==1024;
  if(resource_info.catalog_limit)wypisz("[BSF] Bounded catalog limit reached (128 faces / 1024 records).\n");
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
  // Config family=0 means the stable system default. A newly installed family
  // sorting before DejaVu must not silently become the selected UI font.
  for (unsigned i = 0; i < count; ++i)
    if (same(catalog[i].family,"DejaVu Sans") && !catalog[i].italic &&
        catalog[i].weight < 600 && !catalog[i].mono)
      return &catalog[i];
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
void evict(unsigned index){
  resource_info.resident_bytes-=loaded[index].size;delete[] loaded[index].bytes;loaded[index]=Loaded{};
}
Face *load(const FontInfo *item){
  if(!item)return nullptr;
  for(auto& slot:loaded)if(slot.face.id==item->id){slot.used=++clock;return &slot.face;}
  uint32_t expected=0;
  if(!pobierz_rozmiar_pliku(item->path,&expected)||expected<12||expected>MaxFile)return nullptr;
  auto oldest=[](){unsigned result=0;for(unsigned i=0;i<MaxFaces;++i)if(!loaded[i].bytes || (loaded[result].bytes&&loaded[i].used<loaded[result].used))result=i;return result;};
  unsigned slot=oldest();if(loaded[slot].bytes)evict(slot);
  while(resource_info.resident_bytes>MaxLoadedBytes-expected){
    unsigned index=0;while(index<MaxFaces&&!loaded[index].bytes)++index;
    for(unsigned i=index+1;i<MaxFaces;++i)if(loaded[i].bytes&&loaded[i].used<loaded[index].used)index=i;
    if(index==MaxFaces)return nullptr;
    evict(index);
  }
  uint32_t size=0;auto* bytes=read(item->path,size,MaxLoadedBytes-resource_info.resident_bytes);Face f;
  if(!bytes||!f.open(bytes,size,item->id)){delete[] bytes;return nullptr;}
  ++resource_info.loads;
  loaded[slot].bytes=bytes;loaded[slot].size=size;loaded[slot].face=f;loaded[slot].used=++clock;
  resource_info.resident_bytes+=size;if(resource_info.resident_bytes>resource_info.peak_resident_bytes)resource_info.peak_resident_bytes=resource_info.resident_bytes;
  return &loaded[slot].face;
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
    ++resource_info.scalar_hits;
    if(!r.result) return nullptr;
    for(unsigned i=0;i<count;++i) if(catalog[i].id==r.result) return catalog+i;
  }
  const FontInfo* result=nullptr;
  auto supports=[&](const FontInfo* item){if(!may_cover(*item,cp))return false;auto* f=load(item);return f && f->glyph(cp)!=0;};
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
    bool equal=true;for(size_t i=0;i<size;++i)equal&=r.text[i]==text[i];if(equal){++resource_info.cluster_hits;return r.result;}
  }
  bool tried[MaxFiles]{};
  auto covers=[&](const FontInfo* item){
    size_t index=size_t(item-catalog);if(tried[index])return false;tried[index]=true;
    for(size_t at=0;at<size;){uint32_t cp;at+=utf8(text+at,size-at,cp);if(!default_ignorable(cp)&&!may_cover(*item,cp))return false;}
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
InstallResult install_font(const char* source){
  if(!source)return InstallResult::BadPath;
  size_t length=0,base=0;while(length<1024&&source[length]){if(source[length]=='/')base=length+1;++length;}
  if(!length||length==1024||!font_filename(source+base,length-base))return InstallResult::BadPath;
  char target[160]="/czcionki/ttf/";byte_copy(target+14,source+base,length-base);target[14+length-base]=0;
  uint32_t existing=0;if(pobierz_rozmiar_pliku(target,&existing))return InstallResult::Exists;
  init();for(unsigned i=0;i<MaxFaces;++i)if(loaded[i].bytes)evict(i);
  uint32_t size=0;auto* bytes=read(source,size);Face f;
  if(!bytes||!f.open(bytes,size)){delete[] bytes;return InstallResult::InvalidFont;}
  if(!utworz(target)){delete[] bytes;return InstallResult::IO;}
  bool ok=zapisz_plik(target,reinterpret_cast<const char*>(bytes),size);delete[] bytes;
  if(!ok){bws_wywolaj(7,reinterpret_cast<uint64_t>(target),0,0,0);return InstallResult::IO;}
  rescan();return InstallResult::Ok;
}
ResourceInfo resources(){init();return resource_info;}
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
      rescan();
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
void TextMetrics::cluster(const char* text,size_t start,size_t end,int px,bool bold,const char* family){
    uint32_t selected=resolved_cluster(text+start,end-start,bold,family);
    for(size_t at=start;at<end;){
      uint32_t cp;at+=utf8(text+at,end-at,cp);
      if(default_ignorable(cp)||combining(cp))continue;
      uint32_t id=selected?selected:resolved_face(cp,bold,family);
      int advance=id?advance_face(cp,id,px):px/2+1;
      width+=advance+(previous&&id&&id==previous_face?kern_face(previous,cp,id,px):0);
      if(width>INT32_MAX)width=INT32_MAX;
      previous=cp;previous_face=id;
    }

}
namespace {
size_t text_size(const char* text,size_t limit){
  size_t n=0;if(text)while(n<limit&&text[n])++n;return n;
}
}
int measure_text(const char *text, int px, bool bold, const char *family,
                 size_t limit) {
  size_t n=text_size(text,limit);
  TextMetrics pen;
  for(size_t start=0;start<n;){
    size_t end=next_cluster(text,start,n);
    pen.cluster(text,start,end,px,bold,family);start=end;
  }
  return int(pen.width);
}
int measure_prefix(const char* text,size_t offset,int px,bool bold,
                   const char* family,size_t limit){
  size_t n=text_size(text,limit);
  TextMetrics pen;
  for(size_t start=0;start<n&&start<offset;){
    size_t end=next_cluster(text,start,n);
    if(end>offset)break;
    pen.cluster(text,start,end,px,bold,family);start=end;
  }
  return int(pen.width);
}
size_t hit_test(const char* text,int x,int px,bool bold,const char* family,
                size_t limit){
  if(x<=0)return 0;
  size_t n=text_size(text,limit);
  TextMetrics pen;
  for(size_t start=0;start<n;){
    size_t end=next_cluster(text,start,n);
    int64_t before=pen.width;
    pen.cluster(text,start,end,px,bold,family);
    // Retain the existing integer midpoint policy for ordinary LTR text.
    if(int64_t(x)<(before+pen.width)/2)return start;
    start=end;
  }
  return n;
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
uint32_t terminal_face(const char* family) {
  init();
  auto suitable=[](const FontInfo* item) {
    auto* f=load(item);if(!f)return false;
    unsigned space=f->glyph(' ');if(!space)return false;
    unsigned step=f->hmtx.u16((space<f->metrics?space:f->metrics-1)*4);
    if(!step)return false;
    for(uint32_t cp=33;cp<127;++cp){unsigned g=f->glyph(cp);
      if(!g||f->hmtx.u16((g<f->metrics?g:f->metrics-1)*4)!=step)return false;
    }return true;
  };
  if(family){auto* item=choose(family,false,false);
    return item&&suitable(item)?item->id:0;}
  auto* selected=choose(nullptr,false,false);
  if(selected&&selected->mono&&suitable(selected))return selected->id;
  for(unsigned i=0;i<count;++i)if(catalog[i].mono&&!catalog[i].italic&&
      catalog[i].weight<600&&suitable(catalog+i))return catalog[i].id;
  return 0;
}
const Statistics &statistics() {
  static const Statistics empty{};
  return cache ? cache->stats : empty;
}
bool cached_face(uint32_t cp,uint32_t face,int px,bool bold,bool italic){
  if(!cache||!face)return false;
  for(const auto& f:loaded)if(f.face.id==face)
    return cache->contains(f.face,cp,px,unsigned(bold)|(unsigned(italic)<<1));
  return false;
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
  resource_info.resident_bytes=0;
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
  delete[] catalog;
  catalog = nullptr;
  count = 0;
  initialized = false;
  previewing = false;
  disabled = false;
  resource_info=ResourceInfo{};
}
} // namespace BSF
