#pragma once
#include "bsf.h"
#include "config.h"
namespace BSF {
int bitmap_width(uint32_t);
const uint16_t *bitmap(uint32_t);
struct FontInfo {
  uint32_t id{};
  char path[160]{}, family[96]{}, style[48]{};
  uint16_t weight{};
  bool italic{}, mono{}, serif{};
};
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
uint32_t resolve_family(const char *);
const char *family_name(uint32_t);
bool family_monospace(uint32_t);
int kern(uint32_t left, uint32_t right, int px, bool bold = false,
         const char *family = nullptr);
int height(int px, const char *family = nullptr);
int ascent(int px, const char *family = nullptr);
const Glyph *glyph(uint32_t cp, int px, bool bold = false,
                   const char *family = nullptr, bool italic = false);
bool cached(uint32_t cp, int px, bool bold, const char *family, bool italic);
void pin(const Glyph *);
void unpin(void *);
bool ready(const char *family = nullptr);
const Statistics &statistics();
void preview(uint32_t id, int size); // local to Settings, never publishes
void end_preview();
void shutdown();
} // namespace BSF
