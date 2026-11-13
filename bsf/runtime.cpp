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
// BSF 2.2 browser-scoped faces (never in `catalog`).
struct Scoped {
  FontInfo info{};
  Face face{};
  uint8_t *bytes{};
  size_t size{};
  uint64_t scope{};
  ScopedDescriptor desc{};
  bool used{};
};
Scoped *scoped{};
size_t scoped_bytes{};
uint32_t scoped_serial{};
bool is_scoped(const FontInfo *item) {
  return scoped && item >= &scoped[0].info &&
         item < reinterpret_cast<const FontInfo *>(scoped + MaxScopedFaces);
}
Scoped *scoped_of(const FontInfo *item) {
  if (!is_scoped(item))
    return nullptr;
  for (unsigned i = 0; i < MaxScopedFaces; ++i)
    if (&scoped[i].info == item)
      return scoped + i;
  return nullptr;
}
const FontInfo *info_by_id(uint32_t id);
bool scoped_covers(const Scoped &s, uint32_t cp) {
  if (!s.desc.range_count)
    return true;
  for (unsigned r = 0; r < s.desc.range_count; ++r)
    if (cp >= s.desc.ranges[r][0] && cp <= s.desc.ranges[r][1])
      return true;
  return false;
}
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
  uint8_t *bytes{};uint32_t size{}; // whole-file path (small plain sfnt)
  Face face{};
  uint64_t used{};
  // Table path (collections, large files): shared table buffers.
  uint16_t blob[MaxTables]{};
  uint8_t blobs{};
  bool active() const { return bytes || blobs; }
} loaded[MaxFaces];
// Reference-counted table buffers. Faces of one collection that share a
// table (same file, offset, length) share one buffer.
struct Blob { uint32_t file{},offset{},length{}; uint8_t* bytes{}; uint16_t refs{}; };
constexpr unsigned MaxBlobs=MaxFaces*MaxTables;
Blob* blobs{};
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
// Bounded ranged read (BWS 59, 64 KiB per call, end <= MaxFontFile).
bool read_range(const char* path,uint64_t offset,size_t n,uint8_t* out){
  if(offset>MaxFontFile||n>MaxFontFile-offset)return false;
  for(size_t at=0;at<n;){
    size_t k=n-at;if(k>65536)k=65536;
    uint64_t got=bws_wywolaj(59,reinterpret_cast<uint64_t>(path),offset+at,reinterpret_cast<uint64_t>(out+at),k);
    if(got!=uint64_t(k)+1)return false;at+=k;
  }
  resource_info.io_bytes+=n;return true;
}
struct PathReader{const char* path;};
bool path_read(void* ctx,uint64_t off,size_t n,uint8_t* out){return read_range(static_cast<PathReader*>(ctx)->path,off,n,out);}
uint32_t tag4(const char* s){return (uint32_t(uint8_t(s[0]))<<24)|(uint32_t(uint8_t(s[1]))<<16)|(uint32_t(uint8_t(s[2]))<<8)|uint8_t(s[3]);}
// Tables loaded for rendering. Vertical metrics and hinting programs are
// never read (BSF does not use them), which also bounds CJK memory.
bool render_table(uint32_t t){
  static const char* tags[]={"head","hhea","hmtx","loca","glyf","maxp","cmap","kern","name","OS/2","GDEF","GSUB","GPOS",
    "CFF ","CFF2","fvar","avar","gvar","HVAR","MVAR","STAT","COLR","CPAL","CBDT","CBLC","sbix","post"};
  for(auto* x:tags)if(tag4(x)==t)return true;
  return false;
}
// Tables read eagerly for the catalog (metadata mode); outline payloads are
// recorded as present only.
bool metadata_table(uint32_t t){
  static const char* tags[]={"head","hhea","hmtx","maxp","cmap","name","OS/2","fvar","avar","STAT","CPAL","CBLC","post"};
  for(auto* x:tags)if(tag4(x)==t)return true;
  return false;
}
void coverage(FontInfo& item,const Face& f){
  auto range=[&](uint32_t a,uint32_t b){if(b>0x10ffff)b=0x10ffff;for(uint32_t block=a>>12;block<=b>>12;++block)item.coverage[block/8]|=uint8_t(1u<<(block%8));};
  if(f.cmap.u16(0)==12){for(unsigned i=0;i<f.cmap.u32(12);++i)range(f.cmap.u32(16+i*12),f.cmap.u32(20+i*12));}
  else {unsigned n=f.cmap.u16(6)/2;for(unsigned i=0;i<n;++i)range(f.cmap.u16(16+n*2+i*2),f.cmap.u16(14+i*2));}
}
bool may_cover(const FontInfo& item,uint32_t cp){return cp<=0x10ffff && (item.coverage[(cp>>12)/8]&(1u<<((cp>>12)%8)));}
void fill_info(FontInfo& item,const Face& f,const char* path,unsigned directory,uint32_t size,ContainerFormat container){
  item=FontInfo{};
  item.id=f.id;
  byte_copy(item.path,path,sizeof(item.path)-1);
  byte_copy(item.family,f.family,sizeof(f.family));
  byte_copy(item.style,f.style,sizeof(f.style));
  byte_copy(item.full_name,f.full_name,sizeof(f.full_name));
  byte_copy(item.postscript,f.postscript,sizeof(f.postscript));
  item.weight=f.weight;item.italic=f.italic;item.mono=f.mono;item.serif=f.serif;
  coverage(item,f);
  item.file_size=size;item.file_id=identity(path);
  item.face_index=f.collection_index;item.face_count=f.collection_count;
  item.width=f.width_class;item.container=uint8_t(container);item.outline=uint8_t(f.outline);
  item.axis_count=f.axis_count;item.instances=uint16_t(named_instances(f,nullptr,0));
  item.color=f.color_tables;item.directory=uint8_t(directory);item.status=FontOk;
}
uint32_t face_identity(const char* path,unsigned index){
  if(!index)return identity(path);
  char key[176]{};size_t n=0;while(n<160&&path[n]){key[n]=path[n];++n;}
  key[n++]='#';char digits[8];unsigned d=0;do{digits[d++]=char('0'+index%10);index/=10;}while(index&&d<8);
  while(d)key[n++]=digits[--d];
  return identity(key);
}
bool known_id(uint32_t id){for(unsigned i=0;i<count;++i)if(catalog[i].id==id)return true;return false;}
// Plain sfnt files up to MaxFile keep the BSF 2.1 whole-file path; small
// collections are read whole too (one shared buffer for all their faces).
// Larger collections and files above MaxFile use per-table ranged reads.
bool whole_file(const char* path,uint32_t size){
  if(size>MaxFile)return false;
  if(size<=SmallFontFile)return true;
  uint8_t head[4]{};
  // Plain sfnt check via a ranged read; a reader without BWS 59 support
  // falls back to the whole-file path.
  if(!read_range(path,0,4,head))return true;
  return !(head[0]=='t'&&head[1]=='t'&&head[2]=='c'&&head[3]=='f');
}
// Opens one face of a large file / collection from its metadata tables read
// by range (outline payload is validated at first render) and calls `use`
// while the table buffers are alive. Shared by catalog scans and install.
template<class Use>
bool with_large_face(const char* path,const TableDirectory& dir,uint32_t id,Use use){
  TableSlice slices[MaxTables];unsigned n=0;uint8_t* owned[MaxTables]{};unsigned owned_count=0;size_t budget=0;bool ok=true;
  for(unsigned t=0;t<dir.count&&ok;++t){
    const auto& r=dir.records[t];
    if(!render_table(r.tag))continue;
    if(!metadata_table(r.tag)){slices[n++]={r.tag,Bytes{nullptr,r.length}};continue;}
    if(r.length>MaxFile||budget+r.length>8u*1024*1024){ok=false;break;}
    auto* buf=r.length?new uint8_t[r.length]:nullptr;
    if(r.length&&(!buf||!read_range(path,r.offset,r.length,buf))){delete[] buf;ok=false;break;}
    owned[owned_count++]=buf;budget+=r.length;slices[n++]={r.tag,Bytes{buf,r.length}};
  }
  Face f;
  ok=ok&&f.open_tables(slices,n,dir.version,id,true);
  if(ok)use(f);
  for(unsigned k=0;k<owned_count;++k)delete[] owned[k];
  return ok;
}
// Catalog entries for every usable face of one font file.
void examine(const char* path,unsigned directory){
  uint32_t size=0;
  if(!pobierz_rozmiar_pliku(path,&size)||size<12||size>MaxFontFile){++resource_info.rejected;wypisz("[BSF] Rejected font; fallback retained.\n");return;}
  if(whole_file(path,size)){
    // BSF 2.1 path (whole file, full validation), extended to every face of
    // a small collection.
    uint32_t bytes=0;auto* p=read(path,bytes);bool any=false;
    Bytes file{p,bytes};unsigned faces=p?collection_faces(file):0;
    bool collection=faces&&p[0]=='t'&&p[1]=='t'&&p[2]=='c'&&p[3]=='f';
    TableDirectory first{};
    for(unsigned index=0;index<faces&&count<MaxFiles;++index){
      TableDirectory dir;
      if(!face_directory(file,index,dir)){++resource_info.rejected;continue;}
      if(index&&dir.count==first.count){bool same_dir=true;
        for(unsigned t=0;t<dir.count&&same_dir;++t)same_dir=first.records[t].offset==dir.records[t].offset&&first.records[t].tag==dir.records[t].tag;
        if(same_dir)continue;}
      if(!index)first=dir;
      Face f;const uint32_t id=face_identity(path,index);
      if(!known_id(id)&&f.open_index(p,bytes,index,id)){
        fill_info(catalog[count++],f,path,directory,size,collection?ContainerFormat::Collection:ContainerFormat::SFNT);
        if(collection)++resource_info.collection_faces;any=true;
      }else ++resource_info.rejected;
    }
    if(!any){if(!faces)++resource_info.rejected;wypisz("[BSF] Rejected font; fallback retained.\n");}
    delete[] p;return;
  }
  PathReader pr{path};FileReader fr{path_read,&pr,size};
  unsigned faces=collection_faces(fr);
  if(!faces){++resource_info.rejected;wypisz("[BSF] Rejected font; fallback retained.\n");return;}
  uint8_t head[4]{};bool collection=read_range(path,0,4,head)&&head[0]=='t'&&head[1]=='t'&&head[2]=='c'&&head[3]=='f';
  // Collections and large files: metadata tables per face, outline payload
  // deferred to the first render (validated then).
  TableDirectory previous[4];unsigned remembered=0;bool any=false;
  for(unsigned index=0;index<faces&&count<MaxFiles;++index){
    TableDirectory dir;
    if(!face_directory(fr,index,dir)){++resource_info.rejected;continue;}
    // A directory repeated under another face index is listed once.
    bool repeated=false;
    for(unsigned k=0;k<remembered&&!repeated;++k){
      if(previous[k].count!=dir.count)continue;bool same_dir=true;
      for(unsigned t=0;t<dir.count&&same_dir;++t)same_dir=previous[k].records[t].tag==dir.records[t].tag&&previous[k].records[t].offset==dir.records[t].offset&&previous[k].records[t].length==dir.records[t].length;
      repeated=same_dir;
    }
    if(repeated)continue;
    if(remembered<4)previous[remembered++]=dir;else{previous[index%4]=dir;}
    const uint32_t id=face_identity(path,index);
    bool ok=!known_id(id)&&with_large_face(path,dir,id,[&](Face& f){
      f.collection_index=uint16_t(index);f.collection_count=uint16_t(faces);
      fill_info(catalog[count++],f,path,directory,size,collection?ContainerFormat::Collection:ContainerFormat::SFNT);
      if(collection)++resource_info.collection_faces;any=true;
    });
    if(!ok)++resource_info.rejected;
  }
  if(!any)wypisz("[BSF] Rejected font; fallback retained.\n");
}
void init() {
  if (initialized)
    return;
  initialized = true;
  if (!cache)
    cache = new Cache;
  count = 0;
  if (!catalog) catalog = new FontInfo[MaxFiles];
  if (!blobs) blobs = new Blob[MaxBlobs];
  if (!catalog || !blobs) { resource_info.directory_error=true; return; }
  config = Config{};
  config.checksum = checksum(config);
  Config c{};
  if (bws_wywolaj(92, 0, reinterpret_cast<uint64_t>(&c), sizeof(c), 0) == 1 &&
      valid(c))
    config = c;
  resource_info.examined=resource_info.rejected=resource_info.collection_faces=0;resource_info.catalog_limit=resource_info.directory_error=false;
  auto *list = new char[DirectoryBytes];
  auto** records=new char*[1024];
  if(!list || !records){
    delete[] records;delete[] list;
    resource_info.directory_error=true;
    wypisz("[BSF] Sort OOM.\n");
    return;
  }
  unsigned examined = 0, lines_total = 0, listed = 0;
  for(unsigned d=0;d<FontDirectoryCount;++d){
    byte_set(list, 0, DirectoryBytes);
    if (!wylistuj_katalog_uzytkownika(FontDirectories[d].path, list, DirectoryBytes)) {
      // /czcionki/ttf is required (BSF 2.1); the BSF 2.2 directories are optional.
      if(!d){resource_info.directory_error=true;
        wypisz("[BSF] Directory unavailable or exceeds 64 KiB; bitmap fallback.\n");}
      continue;
    }
    ++listed;
    // Sort all bounded directory records before applying the file-count cap.
    unsigned lines=0; size_t begin=0;
    for(size_t i=0;i<DirectoryBytes-1 && list[i];++i) if(list[i]=='\n'){
      list[i]=0;if(lines<1024)records[lines++]=list+begin;begin=i+1;
    }
    if(begin<DirectoryBytes-1 && list[begin] && lines<1024)records[lines++]=list+begin;
    for(unsigned i=1;i<lines;++i){auto* value=records[i];unsigned j=i;
      while(j && compare(records[j-1],value)>0){records[j]=records[j-1];--j;}records[j]=value;}
    lines_total+=lines;
    for(unsigned i=0;i<lines && examined < MaxExaminedFiles && count < MaxFiles;++i){
      const char* line=records[i];size_t end=0;while(line[end])++end;
      size_t start=0;while(start<end&&line[start]!=']')++start;
      if(start<end)++start;
      while(start<end&&line[start]==' ')++start;
      size_t n=end-start;
      if(!font_filename(line+start,n,FontDirectories[d].ext))continue;
      char path[160]{};byte_copy(path,FontDirectories[d].path,13);path[13]='/';
      byte_copy(path+14,line+start,n);path[14+n]=0;
      ++examined;resource_info.examined=examined;
      examine(path,d);
    }
  }
  delete[] records;delete[] list;
  if(!listed)return;
  resource_info.catalog_limit=count==MaxFiles || examined==MaxExaminedFiles || lines_total>=1024;
  if(resource_info.catalog_limit)wypisz("[BSF] Bounded catalog limit reached (128 faces / 1024 records).\n");
  // Stable ordering for family/style UI and fallback priority.
  for(unsigned i=1;i<count;++i){
    FontInfo value=catalog[i]; unsigned j=i;
    while(j && (compare(catalog[j-1].family,value.family)>0 ||
      (!compare(catalog[j-1].family,value.family) && (compare(catalog[j-1].path,value.path)>0 ||
        (!compare(catalog[j-1].path,value.path) && catalog[j-1].face_index>value.face_index))))){
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
      // Internal scoped name (only produced by the browser): best matching
      // registered face of that scope, by weight distance then style.
      if (name[0] == 1) {
        const FontInfo *best = nullptr;
        int best_score = 0;
        unsigned target = bold ? 700 : 400;
        for (unsigned i = 0; scoped && i < MaxScopedFaces; ++i) {
          const auto &c = scoped[i];
          if (!c.used || !same(name, c.info.family))
            continue;
          int distance = target < c.desc.weight_min ? int(c.desc.weight_min - target)
                         : target > c.desc.weight_max ? int(target - c.desc.weight_max) : 0;
          int score = distance + (c.desc.italic == italic ? 0 : 1000);
          if (!best || score < best_score) {
            best = &c.info;
            best_score = score;
          }
        }
        if (best)
          return best;
        continue;
      }
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
void release_blob(unsigned b){
  auto& x=blobs[b];if(!x.refs)return;
  if(--x.refs==0){resource_info.resident_bytes-=x.length;delete[] x.bytes;x=Blob{};--resource_info.blobs;}
}
void evict(unsigned index){
  auto& slot=loaded[index];
  if(slot.bytes){resource_info.resident_bytes-=slot.size;delete[] slot.bytes;}
  for(unsigned i=0;i<slot.blobs;++i)release_blob(slot.blob[i]);
  slot=Loaded{};
}
void mark_failed(const FontInfo* item){
  for(unsigned i=0;i<count;++i)if(catalog[i].id==item->id&&catalog[i].status!=FontLoadFailed){
    catalog[i].status=FontLoadFailed;++resource_info.load_failures;invalidate_resolution();
    wypisz("[BSF] Font face failed validation on load; fallback retained.\n");}
}
// Make room for `need` more resident bytes by evicting least recently used
// faces other than `keep`.
bool reserve(size_t need,unsigned keep){
  while(resource_info.resident_bytes+need>MaxResidentFontBytes){
    unsigned index=MaxFaces;
    for(unsigned i=0;i<MaxFaces;++i)if(i!=keep&&loaded[i].active()&&(index==MaxFaces||loaded[i].used<loaded[index].used))index=i;
    if(index==MaxFaces)return false;
    evict(index);
  }
  return true;
}
bool load_tables(const FontInfo* item,unsigned slot,Face& f){
  PathReader pr{item->path};FileReader fr{path_read,&pr,item->file_size};
  uint32_t now=0;
  if(!pobierz_rozmiar_pliku(item->path,&now)||now!=item->file_size)return false;
  TableDirectory dir;if(!face_directory(fr,item->face_index,dir))return false;
  TableSlice slices[MaxTables];unsigned n=0;
  auto& L=loaded[slot];
  for(unsigned t=0;t<dir.count;++t){
    const auto& r=dir.records[t];if(!render_table(r.tag))continue;
    if(r.length>MaxFile)return false;
    unsigned found=MaxBlobs,free_slot=MaxBlobs;
    for(unsigned b=0;b<MaxBlobs;++b){
      if(blobs[b].refs&&blobs[b].file==item->file_id&&blobs[b].offset==r.offset&&blobs[b].length==r.length){found=b;break;}
      if(!blobs[b].refs&&free_slot==MaxBlobs)free_slot=b;
    }
    if(found!=MaxBlobs){++blobs[found].refs;++resource_info.shared_table_hits;}
    else{
      if(free_slot==MaxBlobs||!reserve(r.length,slot))return false;
      auto* buf=r.length?new uint8_t[r.length]:nullptr;
      if(r.length&&(!buf||!read_range(item->path,r.offset,r.length,buf))){delete[] buf;return false;}
      blobs[free_slot]={item->file_id,r.offset,r.length,buf,1};found=free_slot;
      resource_info.resident_bytes+=r.length;++resource_info.blobs;
    }
    L.blob[L.blobs++]=uint16_t(found);
    slices[n++]={r.tag,Bytes{blobs[found].bytes,blobs[found].length}};
  }
  if(!f.open_tables(slices,n,dir.version,item->id))return false;
  f.collection_index=item->face_index;f.collection_count=item->face_count;
  return true;
}
Face *load(const FontInfo *item){
  if(!item||item->status==FontLoadFailed)return nullptr;
  if(auto* sc=scoped_of(item))return sc->used?&sc->face:nullptr;
  for(auto& slot:loaded)if(slot.active()&&slot.face.id==item->id){slot.used=++clock;return &slot.face;}
  if(item->file_size<12||item->file_size>MaxFontFile)return nullptr;
  auto oldest=[](){unsigned result=0;for(unsigned i=0;i<MaxFaces;++i)if(!loaded[i].active() || (loaded[result].active()&&loaded[i].used<loaded[result].used))result=i;return result;};
  unsigned slot=oldest();if(loaded[slot].active())evict(slot);
  Face f;bool ok=false;
  if(item->file_size<=MaxFile&&(item->container!=uint8_t(ContainerFormat::Collection)||item->file_size<=SmallFontFile)){
    // Whole-file buffer, shared by every resident face of the same file.
    unsigned found=MaxBlobs,free_slot=MaxBlobs;
    for(unsigned b=0;b<MaxBlobs;++b){
      if(blobs[b].refs&&blobs[b].file==item->file_id&&blobs[b].offset==0&&blobs[b].length==item->file_size){found=b;break;}
      if(!blobs[b].refs&&free_slot==MaxBlobs)free_slot=b;
    }
    if(found!=MaxBlobs){++blobs[found].refs;++resource_info.shared_table_hits;}
    else{
      uint32_t expected=0;
      if(free_slot==MaxBlobs||!pobierz_rozmiar_pliku(item->path,&expected)||expected!=item->file_size)return nullptr;
      if(!reserve(expected,slot))return nullptr;
      uint32_t size=0;auto* bytes=read(item->path,size,MaxFile);
      if(!bytes||size!=item->file_size){delete[] bytes;mark_failed(item);return nullptr;}
      blobs[free_slot]={item->file_id,0,size,bytes,1};found=free_slot;
      resource_info.resident_bytes+=size;++resource_info.blobs;
    }
    loaded[slot].blob[loaded[slot].blobs++]=uint16_t(found);
    ok=f.open_index(blobs[found].bytes,blobs[found].length,item->face_index,item->id);
    if(!ok){evict(slot);mark_failed(item);return nullptr;}
  }else{
    ok=load_tables(item,slot,f);
    if(!ok){evict(slot);mark_failed(item);return nullptr;}
  }
  ++resource_info.loads;
  loaded[slot].face=f;loaded[slot].used=++clock;
  if(resource_info.resident_bytes>resource_info.peak_resident_bytes)resource_info.peak_resident_bytes=resource_info.resident_bytes;
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
// Approximation of Emoji_Presentation (docs/bsf2-2-color-fonts.md): explicit
// VS16/VS15, keycaps, regional indicators, skin-tone modifiers, ZWJ between
// pictographs, and Extended_Pictographic scalars from U+1F000 upwards.
int emoji_presentation_scan(const char *t, size_t n) {
  int result = 0;
  uint32_t previous = 0;
  for (size_t at = 0; at < n;) {
    uint32_t cp;
    size_t k = utf8(t + at, n - at, cp);
    if (!k) break;
    at += k;
    if (cp == 0xfe0e) return -1;
    if (cp == 0xfe0f || cp == 0x20e3 || (cp >= 0x1f1e6 && cp <= 0x1f1ff) || (cp >= 0x1f3fb && cp <= 0x1f3ff) ||
        (cp >= 0x1f000 && extended_pictographic(cp)) || (previous == 0x200d && extended_pictographic(cp)))
      result = 1;
    previous = cp;
  }
  return result;
}
// One face resolution path for measurement, drawing and cache queries.
const FontInfo* resolve(uint32_t cp,const char* family,bool bold,bool italic){
  const auto* primary=choose(family,bold,italic);
  if(!primary || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return nullptr;
  unsigned style=unsigned(bold)|(unsigned(italic)<<1);
  for(const auto& r:resolutions) if(r.valid && r.primary==primary->id && r.cp==cp && r.style==style){
    ++resource_info.scalar_hits;
    if(!r.result) return nullptr;
    if(auto* hit=info_by_id(r.result))return hit;
  }
  const FontInfo* result=nullptr;
  auto supports=[&](const FontInfo* item){
    if(auto* sc=scoped_of(item)){if(!scoped_covers(*sc,cp))return false;return sc->face.glyph(cp)!=0;}
    if(!may_cover(*item,cp))return false;auto* f=load(item);return f && f->glyph(cp)!=0;};
  // Emoji-presentation scalars prefer a colour face (BSF 2.2).
  if(cp>=0x1f000&&extended_pictographic(cp))
    for(unsigned i=0;i<count&&!result;++i)if(catalog[i].color&&supports(catalog+i))result=catalog+i;
  if(!result&&supports(primary)) result=primary;
  // unicode-range composites: other faces of the same scoped family.
  for(unsigned i=0;!result&&scoped&&is_scoped(primary)&&i<MaxScopedFaces;++i)
    if(scoped[i].used&&&scoped[i].info!=primary&&same(scoped[i].info.family,primary->family)&&supports(&scoped[i].info))result=&scoped[i].info;
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
const FontInfo* info_by_id(uint32_t id){
  for(unsigned i=0;i<count;++i)if(catalog[i].id==id)return catalog+i;
  for(unsigned i=0;scoped&&i<MaxScopedFaces;++i)if(scoped[i].used&&scoped[i].info.id==id)return &scoped[i].info;
  return nullptr;
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
    if(auto* sc=scoped_of(item)){
      for(size_t at=0;at<size;){uint32_t cp;at+=utf8(text+at,size-at,cp);if(!default_ignorable(cp)&&(!scoped_covers(*sc,cp)||!sc->face.glyph(cp)))return false;}
      return true;
    }
    size_t index=size_t(item-catalog);if(index>=MaxFiles||tried[index])return false;tried[index]=true;
    for(size_t at=0;at<size;){uint32_t cp;at+=utf8(text+at,size-at,cp);if(!default_ignorable(cp)&&!may_cover(*item,cp))return false;}
    auto* f=load(item);if(!f)return false;
    for(size_t at=0;at<size;){uint32_t cp;at+=utf8(text+at,size-at,cp);if(!default_ignorable(cp)&&!f->glyph(cp))return false;}
    return true;
  };
  int emoji=emoji_presentation_scan(text,size);
  uint32_t result=0;
  // VS16 / emoji clusters: a colour face covering the whole cluster first.
  if(emoji>0)for(unsigned i=0;i<count&&!result;++i)if(catalog[i].color&&covers(catalog+i))result=catalog[i].id;
  // VS15 (text presentation): monochrome faces only, when one covers it.
  if(!result&&!(emoji<0&&primary->color))result=covers(primary)?primary->id:0;
  for(unsigned i=0;!result&&scoped&&is_scoped(primary)&&i<MaxScopedFaces;++i)
    if(scoped[i].used&&&scoped[i].info!=primary&&same(scoped[i].info.family,primary->family)&&covers(&scoped[i].info))result=scoped[i].info.id;
  for(unsigned pass=0;pass<3&&!result;++pass)for(unsigned i=0;i<count;++i){
    auto* candidate=catalog+i;
    bool eligible=pass==0?same(primary->family,candidate->family):pass==1?candidate->italic==italic&&(candidate->weight>=600)==bold:true;
    if(emoji<0&&candidate->color&&pass<2)eligible=false;
    if(eligible&&covers(candidate)){result=candidate->id;break;}
  }
  if(size<=128){auto& r=clusters[cluster_cursor++%16];r.primary=primary->id;r.result=result;r.style=style;r.size=unsigned(size);r.valid=true;byte_copy(r.text,text,size);}
  return result;
}
Face* by_id(uint32_t id){init();for(unsigned i=0;i<count;++i)if(catalog[i].id==id)return load(catalog+i);
  for(unsigned i=0;scoped&&i<MaxScopedFaces;++i)if(scoped[i].used&&scoped[i].info.id==id)return &scoped[i].face;return nullptr;}
const Glyph* glyph_face(uint32_t cp,uint32_t id,int px,bool bold,bool italic){auto* f=by_id(id);return f&&cache?cache->get(*f,cp,px,unsigned(bold)|(unsigned(italic)<<1)):nullptr;}
int advance_face(uint32_t cp,uint32_t id,int px){if(combining(cp)||default_ignorable(cp))return 0;auto* f=by_id(id);return f?f->advance(f->glyph(cp),px):-1;}
int kern_face(uint32_t l,uint32_t r,uint32_t id,int px){auto* f=by_id(id);return f?f->kerning(f->glyph(l),f->glyph(r),px):0;}
// ---- BSF 2.2 colour glyph cache (lazy; plain-text processes never allocate)
namespace {
constexpr unsigned ColorSlots = 16;
struct ColorSlot { uint32_t face{}, fg{}; uint16_t glyph{}, px{}; uint64_t used{}; ColorGlyph g{}; bool valid{}, none{}; };
ColorSlot *color_slots{};
uint32_t *color_storage{};
uint64_t color_clock{};
}
const ColorGlyph *color_glyph_id_face(uint32_t id, unsigned glyph, int px, uint32_t fg, const uint32_t **argb) {
  if (argb) *argb = nullptr;
  if (px < 1 || px > 128) return nullptr;
  Face *f = by_id(id);
  if (!f || !f->color_tables) return nullptr; // fast path: monochrome faces
  if (!color_slots) {
    color_slots = new ColorSlot[ColorSlots];
    color_storage = new uint32_t[size_t(ColorSlots) * MaxBitmap * MaxBitmap];
    if (!color_slots || !color_storage) {
      delete[] color_slots; delete[] color_storage; color_slots = nullptr; color_storage = nullptr;
      return nullptr;
    }
  }
  unsigned victim = 0;
  for (unsigned i = 0; i < ColorSlots; ++i) {
    auto &c = color_slots[i];
    if (c.valid && c.face == f->id && c.glyph == glyph && c.px == px && c.fg == fg) {
      c.used = ++color_clock;
      if (c.none) return nullptr;
      if (argb) *argb = color_storage + size_t(i) * MaxBitmap * MaxBitmap;
      return &c.g;
    }
    if (!c.valid || (color_slots[victim].valid && c.used < color_slots[victim].used)) victim = i;
  }
  auto &c = color_slots[victim];
  c = ColorSlot{};
  c.face = f->id; c.glyph = uint16_t(glyph); c.px = uint16_t(px); c.fg = fg; c.used = ++color_clock; c.valid = true;
  uint32_t *pixels = color_storage + size_t(victim) * MaxBitmap * MaxBitmap;
  if (color_glyph_format(*f, glyph) == ColorFormatNone ||
      !render_color_glyph(*f, glyph, px, fg, 0, c.g, pixels, size_t(MaxBitmap) * MaxBitmap)) {
    c.none = true; // monochrome or invalid colour data: outline fallback
    if (color_glyph_format(*f, glyph) != ColorFormatNone) ++resource_info.color_failures;
    return nullptr;
  }
  ++resource_info.color_glyphs;
  if (argb) *argb = pixels;
  return &c.g;
}
int emoji_presentation(const char *t, size_t n) { return emoji_presentation_scan(t, n); }
// Files above MaxFile (large TTC/OTC, up to MaxFontFile): every face is
// validated from its metadata tables by range (as the catalog scan does),
// then the file is copied in 1 MiB pieces (BWS 59 read, BWS 98 write) without
// a whole-file buffer. Any failure removes the partial target.
InstallResult install_large_font(const char* source,const char* target,uint32_t size){
  PathReader pr{source};FileReader fr{path_read,&pr,size};
  unsigned faces=collection_faces(fr);if(!faces)return InstallResult::InvalidFont;
  for(unsigned i=0;i<faces;++i){
    TableDirectory dir;
    if(!face_directory(fr,i,dir)||!with_large_face(source,dir,0xffffffffu-i,[](Face&){}))return InstallResult::InvalidFont;
  }
  constexpr uint32_t Piece=1024u*1024u;
  auto* buf=new uint8_t[Piece];if(!buf)return InstallResult::IO;
  // Only a target created here is ever removed on failure.
  if(!utworz(target)){delete[] buf;return InstallResult::IO;}
  bool ok=true;
  for(uint32_t off=0;ok&&off<size;){
    uint32_t n=size-off<Piece?size-off:Piece;
    ok=read_range(source,off,n,buf)&&
       bws_wywolaj(98,reinterpret_cast<uint64_t>(target),off,reinterpret_cast<uint64_t>(buf),n)==1;
    off+=n;
  }
  delete[] buf;
  uint32_t written=0;
  if(ok)ok=pobierz_rozmiar_pliku(target,&written)&&written==size;
  if(!ok){bws_wywolaj(7,reinterpret_cast<uint64_t>(target),0,0,0);return InstallResult::IO;}
  rescan();return InstallResult::Ok;
}
InstallResult install_font(const char* source){
  if(!source)return InstallResult::BadPath;
  size_t length=0,base=0;while(length<1024&&source[length]){if(source[length]=='/')base=length+1;++length;}
  if(!length||length==1024)return InstallResult::BadPath;
  int directory=font_directory_for(source+base,length-base);
  if(directory<0)return InstallResult::BadPath;
  char target[160]{};byte_copy(target,FontDirectories[directory].path,13);target[13]='/';
  byte_copy(target+14,source+base,length-base);target[14+length-base]=0;
  uint32_t existing=0;if(pobierz_rozmiar_pliku(target,&existing))return InstallResult::Exists;
  init();for(unsigned i=0;i<MaxFaces;++i)if(loaded[i].active())evict(i);
  uint32_t size=0;
  if(!pobierz_rozmiar_pliku(source,&size)||size<12||size>MaxFontFile)return InstallResult::InvalidFont;
  if(size>MaxFile)return install_large_font(source,target,size);
  auto* bytes=read(source,size);
  if(!bytes)return InstallResult::InvalidFont;
  // Every face of the file must pass full validation (collections included).
  Bytes file{bytes,size};unsigned faces=collection_faces(file);bool valid_font=faces>0;
  for(unsigned i=0;i<faces&&valid_font;++i){Face f;valid_font=f.open_index(bytes,size,i,1);}
  if(!valid_font){delete[] bytes;return InstallResult::InvalidFont;}
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
  bool found = !c.family, terminal = !c.terminal_family;
  for (unsigned i = 0; i < count; ++i) {
    found |= catalog[i].id == c.family;
    terminal |= catalog[i].id == c.terminal_family;
  }
  if (!found || !terminal)
    return false;
  if (bws_wywolaj(92, 1, reinterpret_cast<uint64_t>(&c), sizeof(c), 0) != 1)
    return false;
  refresh();
  return true;
}
uint32_t terminal_face(const char *family);
uint32_t terminal_family() {
  init();
  uint32_t id = config.terminal_family;
  for (unsigned i = 0; id && i < count; ++i)
    if (catalog[i].id == id && catalog[i].status == FontOk)
      return id;
  return 0; // automatic (missing or removed face: safe fallback)
}
bool terminal_suitable(uint32_t id) {
  const char *family = family_name(id);
  return family && terminal_face(family) == id;
}
bool face_covers(uint32_t id, const char *text, size_t n) {
  Face *f = by_id(id);
  if (!f || !text)
    return false;
  for (size_t at = 0; at < n;) {
    uint32_t cp;
    size_t k = utf8(text + at, n - at, cp);
    if (!k)
      return false;
    at += k;
    if (cp == ' ' || default_ignorable(cp))
      continue;
    if (!f->glyph(cp))
      return false;
  }
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
#if BSF_COMPLEX_TEXT
  if(needs_complex_layout(text,n))
    if(const auto* L=layout_text(text,n,px,bold,family))
      return int((L->width()+Fixed/2)/Fixed); // same rounding as legacy advances
#endif
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
#if BSF_COMPLEX_TEXT
  if(needs_complex_layout(text,n))
    if(const auto* L=layout_text(text,n,px,bold,family)){
      int64_t x=L->caret_x(offset<n?offset:n);
      return int((x+Fixed/2)/Fixed);
    }
#endif
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
  size_t n=text_size(text,limit);
#if BSF_COMPLEX_TEXT
  if(needs_complex_layout(text,n))
    if(const auto* L=layout_text(text,n,px,bold,family))
      return L->hit_test(int64_t(x)*Fixed,0);
#endif
  if(x<=0)return 0;
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
  for (unsigned i = 0; scoped && i < MaxScopedFaces; ++i)
    if (scoped[i].used && scoped[i].info.id == id)
      return scoped[i].info.family;
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
  for(const auto& f:loaded)if(f.active()&&f.face.id==face)
    return cache->contains(f.face,cp,px,unsigned(bold)|(unsigned(italic)<<1));
  return false;
}
bool cached(uint32_t cp, int px, bool bold, const char *family, bool italic) {
  const auto *chosen = resolve(cp, family, bold, italic);
  if (!cache || !chosen)
    return false;
  for (const auto &loaded_face : loaded)
    if (loaded_face.active() && loaded_face.face.id == chosen->id)
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
  if(color_slots)for(unsigned i=0;i<ColorSlots;++i)color_slots[i].valid=false;
  for(unsigned i=0;i<MaxFaces;++i)evict(i);
  resource_info.resident_bytes=0;
  invalidate_resolution(); initialized=false; init();
}
#if BSF_COMPLEX_TEXT
namespace {
// ---------------------------------------------------------------- layouts
constexpr unsigned LayoutSlots = 4;
struct LayoutSlot {
  TextLayout layout;
  uint64_t hash{}, used{};
  uint32_t generation{}, family{};
  int px{};
  int64_t max_width{};
  uint8_t style{}, direction{};
  bool valid{};
};
LayoutSlot *layout_slots{};
uint64_t layout_clock{};
TextStatistics text_stats{};
struct ResolverContext { bool bold, italic; const char *family; };
uint32_t resolver_run(void *u, const char *t, size_t a, size_t b) {
  auto *c = static_cast<ResolverContext *>(u);
  return resolved_cluster(t + a, b - a, c->bold, c->family, c->italic);
}
uint32_t resolver_cluster(void *u, const char *t, size_t a, size_t b) {
  auto *c = static_cast<ResolverContext *>(u);
  uint32_t id = resolved_cluster(t + a, b - a, c->bold, c->family, c->italic);
  if (id)
    return id;
  // Explicit per-scalar fallback: the base scalar decides the face.
  for (size_t at = a; at < b;) {
    uint32_t cp;
    at += utf8(t + at, b - at, cp);
    if (!combining(cp) && !default_ignorable(cp))
      return resolved_face(cp, c->bold, c->family, c->italic);
  }
  return 0;
}
const Face *resolver_face(void *, uint32_t id) { return by_id(id); }
int32_t resolver_fallback(void *, uint32_t cp, int px) {
  return int32_t((int64_t(bitmap_width(cp) + 1) * px * 65536) / 16);
}
uint64_t text_hash(const char *t, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; ++i)
    h = (h ^ uint8_t(t[i])) * 1099511628211ull;
  return h;
}
bool build_resolved(TextLayout &layout, const char *text, size_t bytes,
                    const LayoutRequest &r) {
  ResolverContext ctx{r.bold, r.italic, r.family};
  FontResolver fr;
  fr.user = &ctx;
  fr.run_face = resolver_run;
  fr.cluster_face = resolver_cluster;
  fr.face = resolver_face;
  fr.fallback_advance = resolver_fallback;
  LayoutOptions o;
  o.px = r.px;
  o.direction = r.direction;
  o.max_width = r.max_width;
  o.letter_spacing = r.letter_spacing;
  o.word_spacing = r.word_spacing;
  o.context_before = r.context_before;
  o.context_after = r.context_after;
  if (!layout.build(text, bytes, o, fr)) {
    ++text_stats.layout_failures;
    return false;
  }
  const LayoutStatistics &ls = layout.statistics();
  text_stats.shaped_runs += ls.runs;
  text_stats.shaped_glyphs += ls.shaped_glyphs;
  text_stats.fallback_searches += ls.fallback_searches;
  text_stats.budget_failures += ls.budget_failures;
  text_stats.gsub_runs += ls.gsub_runs;
  text_stats.gpos_runs += ls.gpos_runs;
  text_stats.gsub_type_mask |= ls.gsub_type_mask;
  text_stats.gpos_type_mask |= ls.gpos_type_mask;
  if (ls.longest_run > text_stats.longest_run)
    text_stats.longest_run = ls.longest_run;
  return true;
}
} // namespace
bool build_layout(TextLayout &layout, const char *text, size_t bytes,
                  const LayoutRequest &r) {
  if (!text || bytes > MaxLayoutBytes || r.px < 1 || r.px > 128) {
    layout.clear();
    return false;
  }
  init();
  ++text_stats.owned_builds;
  return build_resolved(layout, text, bytes, r);
}
const TextLayout *layout_text(const char *text, size_t bytes, int px, bool bold,
                              const char *family, bool italic, int64_t max_width,
                              BaseDirection direction) {
  if (!text || bytes > MaxLayoutBytes || px < 1 || px > 128)
    return nullptr;
  init();
  if (!layout_slots) {
    layout_slots = new LayoutSlot[LayoutSlots];
    if (!layout_slots)
      return nullptr;
  }
  uint64_t h = text_hash(text, bytes);
  uint32_t fam = family ? identity(family) : 0, gen = generation();
  uint8_t style = uint8_t(unsigned(bold) | (unsigned(italic) << 1));
  for (unsigned i = 0; i < LayoutSlots; ++i) {
    LayoutSlot &s = layout_slots[i];
    if (!s.valid || s.hash != h || s.px != px || s.style != style ||
        s.family != fam || s.generation != gen || s.max_width != max_width ||
        s.direction != uint8_t(direction) || s.layout.bytes() != bytes)
      continue;
    bool equal = true;
    const char *own = s.layout.text();
    for (size_t k = 0; k < bytes && equal; ++k)
      equal = own[k] == text[k];
    if (!equal)
      continue;
    s.used = ++layout_clock;
    ++text_stats.layout_hits;
    return &s.layout;
  }
  ++text_stats.layout_misses;
  if (!layout_slots)
    return nullptr;
  unsigned victim = 0;
  for (unsigned i = 1; i < LayoutSlots; ++i)
    if (!layout_slots[i].valid ||
        (layout_slots[victim].valid && layout_slots[i].used < layout_slots[victim].used))
      victim = i;
  LayoutSlot &s = layout_slots[victim];
  if (s.valid)
    text_stats.layout_bytes -= s.layout.bytes() + s.layout.glyph_count() * sizeof(LayoutGlyph);
  s.valid = false;
  LayoutRequest request;
  request.px = px;
  request.bold = bold;
  request.italic = italic;
  request.family = family;
  request.max_width = max_width;
  request.direction = direction;
  if (!build_resolved(s.layout, text, bytes, request))
    return nullptr;
  text_stats.layout_bytes += bytes + s.layout.glyph_count() * sizeof(LayoutGlyph);
  s.hash = h;
  s.px = px;
  s.style = style;
  s.family = fam;
  s.generation = gen;
  s.max_width = max_width;
  s.direction = uint8_t(direction);
  s.used = ++layout_clock;
  s.valid = true;
  return &s.layout;
}
const Glyph *glyph_id_face(uint32_t id, unsigned glyph, int px, bool bold, bool italic) {
  auto *f = by_id(id);
  return f && cache ? cache->get_glyph(*f, glyph, px, unsigned(bold) | (unsigned(italic) << 1))
                    : nullptr;
}
TextStatistics text_statistics() { return text_stats; }
namespace {
int fixed_round(int64_t v) {
  v += Fixed / 2;
  return int(v >= 0 ? v / Fixed : -((-v + Fixed - 1) / Fixed));
}
} // namespace
namespace {
// Lines [l0, l1) of a layout; line l is drawn with its top at
// y + (l - l0) * line_h.
bool draw_lines(const TextLayout &L, size_t l0, size_t l1, int x, int y, int line_h,
                int px, bool bold, bool italic, const char *family,
                MaskCallback mask, void *ctx, ColorCallback color = nullptr,
                uint32_t foreground = 0xff000000u) {
  if (!mask || px < 1 || px > 128)
    return false;
  if (l1 > L.line_count())
    l1 = L.line_count();
  if (l0 >= l1)
    return true;
  const int asc = ascent(px, family);
  const size_t n = L.glyph_count();
  // Pass 1: base bitmap extents (fallback mark placement needs the base,
  // which follows the mark in RTL visual order).
  struct Extent { int x, y, w, h; bool valid; };
  Extent *ext = n ? new Extent[n] : nullptr;
  if (n && !ext)
    return false;
  for (size_t l = l0; l < l1; ++l) {
    const LayoutLine &line = L.lines()[l];
    int base_y = y + int(l - l0) * line_h + asc;
    for (size_t i = line.glyph_begin; i < line.glyph_end; ++i) {
      const LayoutGlyph &g = L.glyphs()[i];
      ext[i] = Extent{};
      if (g.flags & (GlyphMark | GlyphHidden | GlyphMissing))
        continue;
      if (const Glyph *b = glyph_id_face(g.face, g.glyph, px, bold, italic))
        ext[i] = {x + fixed_round(g.x + g.x_offset) + b->x,
                  base_y - fixed_round(g.y_offset) + b->y, b->width, b->height, true};
    }
  }
  for (size_t l = l0; l < l1; ++l) {
    const LayoutLine &line = L.lines()[l];
    int top = y + int(l - l0) * line_h, base_y = top + asc;
    int above = 0, below = 0;
    uint32_t stacked = 0xffffffffu;
    for (size_t i = line.glyph_begin; i < line.glyph_end; ++i) {
      const LayoutGlyph &g = L.glyphs()[i];
      if (g.flags & GlyphHidden)
        continue;
      int gx = x + fixed_round(g.x + g.x_offset), gy = base_y - fixed_round(g.y_offset);
      if (g.flags & GlyphMissing) {
        // Bitmap fallback for scalars no installed face covers.
        if (g.flags & GlyphMark)
          continue;
        int w = (bitmap_width(g.codepoint) * px + 8) / 16, hh = px;
        if (w > 128) w = 128;
        if (hh > 128) hh = 128;
        if (w < 1)
          continue;
        auto *a = new uint8_t[size_t(w) * hh];
        if (!a)
          continue;
        const uint16_t *bits = bitmap(g.codepoint);
        for (int yy = 0; yy < hh; ++yy)
          for (int xx = 0; xx < w; ++xx)
            a[yy * w + xx] = bits && (bits[yy * 16 / hh] & (1u << (15 - xx * 16 / px))) ? 255 : 0;
        mask(ctx, gx, top, w, hh, a);
        delete[] a;
        continue;
      }
      if (color) {
        const uint32_t *argb = nullptr;
        if (const ColorGlyph *cg = color_glyph_id_face(g.face, g.glyph, px, foreground, &argb)) {
          if (cg->width && cg->height)
            color(ctx, gx + cg->x, gy + cg->y, cg->width, cg->height, argb);
          continue;
        }
      }
      const Glyph *b = glyph_id_face(g.face, g.glyph, px, bold, italic);
      if (!b)
        continue;
      int dx = gx + b->x, dy = gy + b->y;
      if (g.flags & GlyphFallbackMark) {
        const Extent *base = nullptr;
        for (size_t k = line.glyph_begin; k < line.glyph_end && !base; ++k)
          if (ext[k].valid && L.glyphs()[k].cluster == g.cluster)
            base = &ext[k];
        if (base) {
          if (stacked != g.cluster) {
            stacked = g.cluster;
            above = below = 0;
          }
          unsigned ccc = combining_class(g.codepoint);
          bool low = ccc == 200 || ccc == 202 || ccc == 218 || ccc == 220 ||
                     ccc == 222 || ccc == 233 || ccc == 240;
          dx = base->x + base->w / 2 - b->width / 2;
          if (low) {
            dy = base->y + base->h + 1 + below;
            below += b->height + 1;
          } else if (ccc != 1) {
            dy = base->y - b->height - 1 - above;
            above += b->height + 1;
          }
        }
      }
      mask(ctx, dx, dy, b->width, b->height, b->alpha);
    }
  }
  delete[] ext;
  return true;
}
} // namespace
bool draw_layout(const TextLayout &L, int x, int y, int px, bool bold,
                 const char *family, MaskCallback mask, void *ctx) {
  const int h = height(px, family);
  return draw_lines(L, 0, L.line_count(), x, y, h > 0 ? h : px, px, bold, false,
                    family, mask, ctx);
}
bool draw_layout_line(const TextLayout &L, size_t line, int x, int y, int px,
                      bool bold, bool italic, const char *family, MaskCallback mask,
                      void *ctx) {
  const int h = height(px, family);
  return draw_lines(L, line, line + 1, x, y, h > 0 ? h : px, px, bold, italic,
                    family, mask, ctx);
}
bool draw_layout_line_color(const TextLayout &L, size_t line, int x, int y, int px, bool bold,
                            bool italic, const char *family, uint32_t foreground,
                            MaskCallback mask, ColorCallback color, void *ctx) {
  const int h = height(px, family);
  return draw_lines(L, line, line + 1, x, y, h > 0 ? h : px, px, bold, italic, family, mask, ctx,
                    color, foreground);
}
#endif
bool scoped_family(uint64_t scope, const char *family, char *out, size_t cap) {
  // "\x01" + 16 hex digits + ':' + lower-case family (bounded by FontInfo).
  if (!family || !out || cap < 20)
    return false;
  size_t n = 0;
  out[n++] = 1;
  for (int shift = 60; shift >= 0; shift -= 4)
    out[n++] = "0123456789abcdef"[(scope >> shift) & 15];
  out[n++] = ':';
  for (size_t i = 0; family[i]; ++i) {
    char c = family[i];
    if (uint8_t(c) < 32)
      return false;
    if (c >= 'A' && c <= 'Z')
      c = char(c + 32);
    if (n + 1 >= cap || n + 1 >= sizeof(FontInfo{}.family))
      return false;
    out[n++] = c;
  }
  out[n] = 0;
  return n > 18;
}
uint32_t register_scoped_face(uint64_t scope, const char *family, const ScopedDescriptor &desc,
                              uint8_t *bytes, size_t size) {
  init();
  if (!scope || !bytes || size < 12 || size > MaxFile || desc.range_count > MaxScopedRanges)
    return 0;
  if (!scoped) {
    scoped = new Scoped[MaxScopedFaces];
    if (!scoped)
      return 0;
  }
  char name[96];
  if (!scoped_family(scope, family, name, sizeof(name)) || scoped_bytes > MaxScopedBytes - size)
    return 0;
  unsigned slot = MaxScopedFaces;
  for (unsigned i = 0; i < MaxScopedFaces; ++i)
    if (!scoped[i].used) {
      slot = i;
      break;
    }
  if (slot == MaxScopedFaces)
    return 0;
  // Unique, never-reused identity (high bit marks browser scope).
  uint32_t id = 0;
  do {
    id = 0x80000000u | (++scoped_serial & 0x7fffffffu);
  } while (info_by_id(id));
  auto &s = scoped[slot];
  Face f;
  if (!f.open(bytes, size, id))
    return 0;
  s = Scoped{};
  f.browser_scope = true;
  s.face = f;
  s.bytes = bytes;
  s.size = size;
  s.scope = scope;
  s.desc = desc;
  s.info.id = id;
  byte_copy(s.info.family, name, sizeof(name));
  byte_copy(s.info.style, f.style, sizeof(f.style));
  byte_copy(s.info.full_name, f.full_name, sizeof(f.full_name));
  s.info.weight = desc.weight_min;
  s.info.italic = desc.italic;
  s.info.mono = f.mono;
  s.info.serif = f.serif;
  s.info.outline = uint8_t(f.outline);
  s.info.file_size = uint32_t(size);
  s.info.status = FontOk;
  s.info.directory = 0xff; // browser scope, not a font directory
  coverage(s.info, f);
  s.used = true;
  ++resource_info.scoped_registered;
  scoped_bytes += size;
  invalidate_resolution();
  return id;
}
void release_scope(uint64_t scope) {
  if (!scoped || !scope)
    return;
  bool any = false;
  for (unsigned i = 0; i < MaxScopedFaces; ++i)
    if (scoped[i].used && scoped[i].scope == scope) {
      scoped_bytes -= scoped[i].size;
      ++resource_info.scoped_released;
      delete[] scoped[i].bytes;
      scoped[i] = Scoped{};
      any = true;
    }
  if (any) {
    invalidate_resolution();
#if BSF_COMPLEX_TEXT
    // The shared layout cache may still hold glyph ids of released faces.
    if (layout_slots)
      for (unsigned i = 0; i < LayoutSlots; ++i)
        layout_slots[i].valid = false;
#endif
  }
}
unsigned scoped_faces(uint64_t scope) {
  unsigned n = 0;
  for (unsigned i = 0; scoped && i < MaxScopedFaces; ++i)
    n += scoped[i].used && (!scope || scoped[i].scope == scope);
  return n;
}
void shutdown() {
#if BSF_COMPLEX_TEXT
  delete[] layout_slots;
  layout_slots = nullptr;
  text_stats = TextStatistics{};
#endif
  invalidate_resolution();
  for (unsigned i = 0; i < MaxFaces; ++i)
    evict(i);
  delete[] blobs;
  blobs = nullptr;
  delete[] color_slots;
  delete[] color_storage;
  color_slots = nullptr;
  color_storage = nullptr;
  for (unsigned i = 0; scoped && i < MaxScopedFaces; ++i)
    delete[] scoped[i].bytes;
  delete[] scoped;
  scoped = nullptr;
  scoped_bytes = 0;
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
