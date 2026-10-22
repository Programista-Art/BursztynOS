#define HUSARZ_PERF 1
#include "../programy/husarz/paint.h"
#include "../bursztyn_ui/czcionka.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <string>
#include <fstream>
#include <iterator>
static uint64_t clock_ns(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static int glyph_advance(void*,uint32_t cp,int font,bool bold,bool){return Husarz::clamp64((int64_t(BursztynUI::CzcionkaUI::szerokosc_znaku(cp)+1)*font+8)/16+(bold?1:0),1,1024);}
static int glyph_height(void*,int font){return font;}
static const uint16_t *glyph_bitmap(void*,uint32_t cp){return BursztynUI::CzcionkaUI::bitmapa_znaku(cp);}
static int glyph_width(void*,uint32_t cp){return BursztynUI::CzcionkaUI::szerokosc_znaku(cp);}
int main(int argc,char **argv) {
    Husarz::Metrics metrics{nullptr,glyph_advance,glyph_height};Husarz::Glyphs glyphs{nullptr,glyph_bitmap,glyph_width};
    Husarz::perf_clock=clock_ns;
    for(int a=1;a<argc;++a){
        std::ifstream file(argv[a]);std::string html((std::istreambuf_iterator<char>(file)),{});
        if(!file || html.empty())return 2;
        auto *d=new Husarz::Document;Husarz::parse_html(*d,html.data(),html.size());Husarz::collect_styles(*d);Husarz::prepare_controls(*d);
        Husarz::reflow(*d,800,metrics);
        uint32_t *pixels=new uint32_t[800*600];Husarz::Surface surface{pixels,800,600};Husarz::paint(*d,surface,0,metrics,glyphs);
        auto &p=d->perf;
        std::printf("{\"fixture\":\"%s\",\"sizeof_document\":%zu,\"nodes\":%u,\"rules\":%u,\"declarations\":%u,\"fragments\":%u,\"height\":%d,\"truncated\":%d,\"selectorchecks\":%llu,\"parse_ns\":%llu,\"css_ns\":%llu,\"style_ns\":%llu,\"layout_ns\":%llu,\"table_ns\":%llu,\"paint_ns\":%llu}\n",argv[a],sizeof(*d),d->node_count,d->rule_count,d->declaration_count,d->fragment_count,d->height,d->truncated,(unsigned long long)p.selector_checks,(unsigned long long)p.duration[1],(unsigned long long)p.duration[2],(unsigned long long)p.duration[3],(unsigned long long)p.duration[4],(unsigned long long)p.duration[5],(unsigned long long)p.duration[6]);
        delete[] pixels;delete d;
    }
}
