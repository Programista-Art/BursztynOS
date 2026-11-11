#define BSF_NATIVE 1
#include "../bursztyn_ui/bursztyn_ui.h"
#include "../programy/husarz/paint.h"
#define BURSZTYN_HOST_CUSTOM_ALPHA 1
#include "bursztyn_ui_host_fixture.h"
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
using namespace BursztynUI;
static unsigned checks;
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL %d %s\n", __LINE__, #x);                           \
      abort();                                                                 \
    }                                                                          \
  } while (0)
static std::map<std::string, std::string> files;
static BSF::Config saved;
static std::string directory;
static unsigned directory_reads;
static uint64_t hook(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t) {
  if (n == 92) {
    auto *config = reinterpret_cast<BSF::Config *>(b);
    if (c != sizeof(*config))
      return 0;
    if (!a) {
      *config = saved;
      return 1;
    }
    if (!BSF::valid(*config))
      return 0;
    uint32_t generation=saved.generation+1;
    saved = *config;
    saved.generation=generation?generation:1;
    return 1;
  }
  if (n == 6) {
    CHECK(!strcmp(reinterpret_cast<const char *>(a), "/czcionki/ttf"));
    ++directory_reads;
    if (directory.size() + 1 > c)
      return 0;
    memcpy(reinterpret_cast<void *>(b), directory.c_str(),
           directory.size() + 1);
    return 1;
  }
  if (n == 4) {
    auto it = files.find(reinterpret_cast<const char *>(a));
    if (it == files.end())
      return 0;
    *reinterpret_cast<uint32_t *>(b) = uint32_t(it->second.size());
    return 1;
  }
  return 0;
}
bool czytaj_plik(const char *path, char *out, uint32_t capacity) {
  CHECK(!strncmp(path, "/czcionki/ttf/", 14));
  auto i = files.find(path);
  if (i == files.end() || i->second.size() > capacity)
    return false;
  memcpy(out, i->second.data(), i->second.size());
  return true;
}
void wypisz(const char *) {}
static unsigned alpha_calls;
static Prostokat alpha_bounds;
static std::vector<uint32_t> alpha_pixels;
bool gui_blit_alpha(int x, int y, int w, int h, const uint32_t* pixels) {
  ++alpha_calls;
  alpha_bounds = {x,y,w,h};
  alpha_pixels.assign(pixels,pixels+size_t(w)*h);
  return true;
}
static int native_advance(void *, uint32_t cp, int px, bool bold, bool,
                          uint32_t id) {
  int a = BSF::measure_cp(cp, px, bold, BSF::family_name(id));
  return a < 0 ? px : a;
}
static int native_height(void *, int px, uint32_t id) {
  int h = BSF::height(px, BSF::family_name(id));
  return h < 0 ? px : h;
}
static bool native_cached(void *, uint32_t cp, int px, bool bold, bool italic,
                          uint32_t id) {
  return BSF::cached(cp, px, bold, BSF::family_name(id), italic);
}
static bool native_alpha(void *, uint32_t cp, int px, bool bold, bool italic,
                         uint32_t id, Husarz::AlphaGlyph &out) {
  const char *family = BSF::family_name(id);
  auto *g = BSF::glyph(cp, px, bold, family, italic);
  if (!g)
    return false;
  BSF::pin(g);
  out.width = g->width;
  out.height = g->height;
  out.x = g->x;
  out.y = BSF::ascent(px, family) + g->y;
  out.pixels = g->alpha;
  out.lease = const_cast<BSF::Glyph *>(g);
  out.release = BSF::unpin;
  return true;
}
int main() {
  saved.checksum = BSF::checksum(saved);
  g_test_bws_hook = hook;
  for (const char *name :
       {"DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "DejaVuSansMono.ttf"}) {
    std::ifstream f(std::string("czcionki/ttf/") + name, std::ios::binary);
    files[std::string("/czcionki/ttf/") + name] = {
        std::istreambuf_iterator<char>(f), {}};
    directory += std::string("[PLIK] ") + name + "\n";
  }
  directory += "[PLIK] ../../outside.ttf\n[PLIK] ignored.otf\n";
  CHECK(BSF::enumerate() == 3);
  CHECK(directory_reads == 1);
  CHECK(BSF::ready());
  CHECK(BSF::default_id() != 0);
  uint32_t sans =
               BSF::resolve_family("MissingWebFont, 'DejaVu Sans', sans-serif"),
           mono = BSF::resolve_family("monospace");
  CHECK(sans && mono && sans != mono);
  CHECK(BSF::measure_cp('W', 16, false, "DejaVu Sans") >
        BSF::measure_cp('i', 16, false, "DejaVu Sans"));
  CHECK(BSF::measure_cp('W', 16, false, "monospace") ==
        BSF::measure_cp('i', 16, false, "monospace"));
  auto previous = BSF::configuration();
  BSF::preview(mono, 24);
  CHECK(BSF::ui_size() == 24);
  CHECK(BSF::configuration().family == previous.family);
  BSF::end_preview();
  CHECK(BSF::ui_size() == 16);
  {
    CHECK(BSF::glyph('W',16));
    CHECK(BSF::cached('W',16,false,nullptr,false));
    auto resized=previous;resized.size=20;resized.family=BSF::default_id();
    CHECK(BSF::apply(resized));
    CHECK(BSF::cached('W',16,false,nullptr,false));
    CHECK(!BSF::cached('W',20,false,nullptr,false));
    CHECK(BSF::glyph('W',20));
    auto hits=BSF::statistics().hits;
    CHECK(BSF::glyph('W',16));
    CHECK(BSF::statistics().hits==hits+1);
    CHECK(BSF::apply(previous));
  }
  {
    Panel parent;
    auto& bar=parent.dodaj<PasekStanu>();
    bar.ustaw_preferowany_rozmiar({-1,24});
    bar.dodaj_sekcje("Zażółć gęślą jaźń gy");
    CHECK(bar.zmierz({800,600}).wysokosc==24);
    BSF::preview(sans,24);
    parent.zmierz({800,600});parent.rozmiesc({0,0,800,600});
    CHECK(bar.obszar().wysokosc>=CzcionkaUI::pobierz_wysokosc()+4);
    TabelaWirtualna table;
    CHECK(table.wysokosc_wiersza()>=CzcionkaUI::pobierz_wysokosc()+2);
    CHECK(table.wysokosc_naglowka()>=CzcionkaUI::pobierz_wysokosc()+4);
    bar.ustaw_skale_dpi(SkalaUI::procent(125));
    CHECK(bar.zmierz({1000,750}).wysokosc>=CzcionkaUI::pobierz_wysokosc(SkalaUI::procent(125))+5);
    BSF::end_preview();
    bar.ustaw_skale_dpi(SkalaUI::procent(100));
    CHECK(bar.zmierz({800,600}).wysokosc==24);
  }
  {
    Przycisk wide("WWWW"), narrow("iiii");
    CHECK(wide.zmierz({800, 600}).szerokosc >
          narrow.zmierz({800, 600}).szerokosc);
    Etykieta label("Zażółć gęślą jaźń");
    CHECK(label.zmierz({800, 600}).wysokosc >= BSF::height(16));
    const char *polish = "Wi ąęłóśźż";
    for (size_t at = 0; polish[at];) {
      uint32_t cp;
      size_t n = CzcionkaUI::dekoduj_utf8(polish, at, &cp);
      CHECK(CzcionkaUI::znak_z_pozycji_x(
                polish, CzcionkaUI::pozycja_x_z_bajtu(polish, at)) == at);
      at += n;
    }
    CHECK(CzcionkaUI::pozycja_x_z_bajtu("ą", 1) == 0);
    uint32_t replacement;
    CHECK(CzcionkaUI::dekoduj_utf8("\xff", 0, &replacement) == 1);
    CHECK(replacement == 0xfffd);
    CHECK(CzcionkaUI::pozycja_x_z_bajtu("\xff", 1) ==
          BSF::measure_cp(0xfffd, 16));
    std::vector<uint32_t> pixels(300 * 90, 0xffeef0f2);
    KontekstRysowania ctx({0, 0, 300, 90});
    ctx.ustaw_cel(pixels.data(), {0, 0, 300, 90});
    ctx.dodaj_przyciecie({30, 10, 200, 45});
    ctx.rysuj_tekst({20, 10}, "Zażółć gęślą jaźń", 0x112233);
    unsigned ink = 0, aa = 0;
    for (int y = 0; y < 90; ++y)
      for (int x = 0; x < 300; ++x) {
        uint32_t c = pixels[y * 300 + x];
        if (c != 0xffeef0f2) {
          CHECK(x >= 30 && x < 230 && y >= 10 && y < 55);
          ++ink;
          if ((c & 0xffffff) != 0x112233)
            ++aa;
        }
      }
    CHECK(ink > 100 && aa > 10);
    ctx.ustaw_cel(nullptr, {});
    alpha_calls = 0;
    ctx.rysuj_tekst({20, 10}, "Zażółć gęślą jaźń", 0x112233);
    CHECK(alpha_calls == 1);
    CHECK(alpha_bounds.x >= 30 && alpha_bounds.y >= 10);
    CHECK(alpha_bounds.prawy() <= 230 && alpha_bounds.dolny() <= 55);
    aa = 0;
    for (uint32_t pixel : alpha_pixels) {
      CHECK(!(pixel >> 24) || (pixel & 0xffffff) == 0x112233);
      aa += (pixel >> 24) > 0 && (pixel >> 24) < 255;
    }
    CHECK(aa > 10);
    PoleTekstowe field;
    field.ustaw_tekst("Zażółć");
    CHECK(!strcmp(field.tekst(), "Zażółć"));
    field.ustaw_tekst("A\xcc\x81x");
    for(size_t offset=0;offset<3;++offset){
      field.ustaw_pozycje_kursora(offset);
      CHECK(field.pozycja_kursora()==0);
    }
    field.ustaw_pozycje_kursora(3);CHECK(field.pozycja_kursora()==3);
    field.ustaw_zaznaczenie(1,4);
    CHECK(field.poczatek_zaznaczenia()==0 && field.koniec_zaznaczenia()==4);
    field.ustaw_zaznaczenie(0,2);CHECK(!field.czy_ma_zaznaczenie());
    field.ustaw_zaznaczenie(0,3);field.zastap_zaznaczenie("Z");
    CHECK(!strcmp(field.tekst(),"Zx"));
    auto config = previous;
    config.family = mono;
    config.size = 20;
    CHECK(BSF::apply(config));
    CHECK(BSF::generation() != previous.generation);
    CHECK(BSF::ui_size() == 20);
    CHECK(BSF::measure_cp('W', 16) == BSF::measure_cp('i', 16));
    CHECK(BSF::resolve_family("'DejaVu Sans', sans-serif") == sans);
    CHECK(BSF::resolve_family("MissingWebFont, sans-serif") == mono);
    CHECK(wide.zmierz({800, 600}).wysokosc > BSF::height(16));
  }
  Husarz::font_family_resolver = BSF::resolve_family;
  Husarz::Metrics metrics{nullptr, nullptr, nullptr, native_advance,
                          native_height};
  Husarz::Glyphs glyphs{nullptr, nullptr, nullptr, native_alpha, native_cached};
  auto d = std::make_unique<Husarz::Document>();
  const char *html =
      "<html><body "
      "style=\"margin:0;background:#112233;color:#e74c3c;font-family:"
      "MissingWebFont,sans-serif;font-size:16px\"><h1 "
      "style=\"font-size:48px;font-weight:700;font-family:'DejaVu "
      "Sans'\">Bursztyn ąęłóźż</h1><p>WWWW iiii WWWW iiii</p></body></html>";
  Husarz::parse_html(*d, html, strlen(html));
  Husarz::collect_styles(*d);
  Husarz::reflow(*d, 780, metrics);
  CHECK(!d->truncated);
  bool found = false;
  for (unsigned i = 0; i < d->node_count; ++i)
    if (d->tag(i, "h1")) {
      CHECK(d->styles[i].native_family == sans);
      CHECK(d->styles[i].bold);
      CHECK(d->styles[i].font == 48);
      found = true;
    }
  CHECK(found);
  std::vector<uint32_t> page(780 * 420);
  Husarz::Surface surface{page.data(), 780, 420};
  Husarz::WorkDriver driver;
  driver.allow_clock = false;
  auto paint =
      Husarz::paint_work(*d, surface, 0, metrics, glyphs, {0, 0, 780, 420});
  while (!paint.step(driver)) {
  }
  CHECK(!driver.failed && driver.maximum <= 2048);
  unsigned red = 0, aa = 0;
  for (auto pixel : page) {
    if ((pixel & 0xffffff) == 0xe74c3c)
      ++red;
    if ((pixel & 0xffffff) != 0xe74c3c && (pixel & 0xffffff) != 0x112233)
      ++aa;
  }
  CHECK(red > 100 && aa > 100);
  CHECK(BSF::statistics().hits > 0);
  {
    Husarz::Style style;
    style.font = 127;
    style.native_family = sans;
    bool rendered = false;
    Husarz::WorkDriver cancelled;
    cancelled.allow_clock = false;
    auto work = Husarz::paint_alpha_glyph_work(surface, 'B', 0, 0, style,
                                              glyphs, {0,0,780,420}, rendered);
    CHECK(!work.step(cancelled));
    const auto* lease = BSF::glyph('B', 127, false, BSF::family_name(sans));
    CHECK(lease && lease->pins == 1);
    work.cancel();
    CHECK(lease->pins == 0);
    CHECK(!cancelled.failed && cancelled.maximum <= 2048);
  }

  BSF::shutdown();
  CHECK(BSF::ui_size() == 20);
  CHECK(BSF::resolve_family("sans-serif") == mono); // process restart
  BSF::shutdown();
  saved.magic = 0;
  CHECK(BSF::ui_size() == 16);
  CHECK(BSF::ready());
  BSF::shutdown();
  {
    // A real TTF, with a valid restricted cmap: only A is available.
    // It is never packaged; this proves actual fallback, not Sans coverage.
    std::string subset=files["/czcionki/ttf/DejaVuSans.ttf"];
    BSF::Face full;CHECK(full.open(subset.data(),subset.size(),99));
    unsigned gid=full.glyph('A');
    auto put16=[&](size_t at,unsigned v){subset[at]=char(v>>8);subset[at+1]=char(v);};
    auto put32=[&](size_t at,unsigned v){put16(at,v>>16);put16(at+2,v);};
    BSF::Bytes bytes{reinterpret_cast<const uint8_t*>(subset.data()),subset.size()};
    for(unsigned i=0;i<bytes.u16(4);++i){size_t o=12+i*16;
      if(bytes.u32(o)!=0x636d6170)continue;
      size_t at=bytes.u32(o+8);put16(at,0);put16(at+2,1);put16(at+4,3);put16(at+6,10);put32(at+8,12);
      put16(at+12,12);put16(at+14,0);put32(at+16,28);put32(at+20,0);put32(at+24,1);
      put32(at+28,'A');put32(at+32,'A');put32(at+36,gid);
    }
    const char oldname[]="DejaVu Sans",newname[]="Subset Sans";
    for(size_t at=0;at+22<=subset.size();++at){bool match=true;
      for(unsigned j=0;j<11;++j)match&=subset[at+2*j]==0&&subset[at+2*j+1]==oldname[j];
      if(match)for(unsigned j=0;j<11;++j)subset[at+2*j+1]=newname[j];}
    BSF::Face limited;CHECK(limited.open(subset.data(),subset.size(),100));
    CHECK(limited.glyph('A') && !limited.glyph(0x3a9));
    files["/czcionki/ttf/custom.TTF"]=subset;directory+="[PLIK] custom.TTF\n";
    auto old_generation=BSF::generation();
    BSF::rescan();CHECK(BSF::generation()!=old_generation);CHECK(BSF::enumerate()==4);
    auto primary=BSF::resolve_family("Subset Sans");CHECK(primary && primary!=0xffffffffU);
    CHECK(BSF::resolved_face('A',false,"Subset Sans")==primary);
    auto cluster=BSF::resolved_cluster("A\xcc\x81",3,false,"Subset Sans");
    CHECK(cluster && cluster!=primary);
    auto* cluster_base=BSF::glyph_face('A',cluster,20);CHECK(cluster_base && cluster_base->face==cluster);
    auto* cluster_mark=BSF::glyph_face(0x301,cluster,20);CHECK(cluster_mark && cluster_mark->face==cluster);
    CHECK(BSF::advance_face(0x301,cluster,20)==0);

    CHECK(BSF::resolved_face(0x3a9,false,"Subset Sans")!=primary);
    auto* fallback=BSF::glyph(0x3a9,20,false,"Subset Sans");CHECK(fallback&&fallback->face!=primary);
    CHECK(BSF::measure_cp(0x3a9,20,false,"Subset Sans")==fallback->advance);
    CHECK(BSF::resolved_face(0x10ffff,false,"Subset Sans")==0);
    CHECK(BSF::resolved_face(0xd800,false,"Subset Sans")==0);
    unsigned reads=directory_reads;
    for(unsigned i=0;i<1000;++i){CHECK(BSF::resolved_face(0x3a9,false,"Subset Sans")!=primary);CHECK(!BSF::resolved_face(0x10ffff,false,"Subset Sans"));}
    CHECK(directory_reads==reads);
    CHECK(BSF::measure_text("e\xcc\x81",20)==BSF::measure_text("e",20));
    CHECK(BSF::measure_text("a\xcc\x88\xcc\x81",20)==BSF::measure_text("a",20));
    CHECK(BSF::next_cluster("e\xcc\x81x",0,4)==3);
    CHECK(BSF::previous_cluster("e\xcc\x81x",3)==0);
    std::vector<int> positions;
    CHECK(CzcionkaUI::rysuj_bsf("e\xcc\x81x",0,0,20,[&](int x,int,int,int,const uint8_t*){positions.push_back(x);}));
    // BSF 2.0: e+U+0301 is canonically composed to the font's precomposed
    // glyph (shaped path), so two bitmaps are drawn; the drawn position of
    // 'x' must equal the measured caret from the same shared layout.
    CHECK(positions.size()==2);
    auto* final_glyph=BSF::glyph('x',20);CHECK(final_glyph);
    CHECK(positions[1]==BSF::measure_prefix("e\xcc\x81x",3,20)+final_glyph->x);
    uint32_t supplementary=0;
    CHECK(BSF::utf8("\xf0\x9f\x98\x80",4,supplementary)==4 && supplementary==0x1f600);

    for(auto* sample:{"Привет мир", "Україна Київ Ґґ Єє Іі Її Ўў", "Καλημέρα κόσμε Ελληνικά Αθήνα Ωμέγα", "ά έ ή ί ό ύ ώ ϊ ϋ ΐ ΰ"}){
      CHECK(BSF::measure_text(sample,20)>0);
      size_t size=strlen(sample);
      for(size_t at=0;at<size;){uint32_t cp;at+=BSF::utf8(sample+at,size-at,cp);CHECK(BSF::glyph(cp,20));}
    }
    // Carets must use the same complete-cluster face and advances as drawing.
    const char* caret_samples[]={"A\xcc\x81x", "a\xcc\x88\xcc\x81z",
      "\xf0\x9f\x87\xb5\xf0\x9f\x87\xb1x", "\xff\xcc\x81x", "Привіт Ελληνικά"};
    for(const char* sample:caret_samples){
      size_t size=strlen(sample);
      for(size_t offset=0;offset<=size;++offset){
        size_t floor=BSF::cluster_floor(sample,size,offset);
        CHECK(BSF::measure_prefix(sample,offset,20,false,"Subset Sans") ==
              BSF::measure_text(sample,20,false,"Subset Sans",floor));
      }
      int width=BSF::measure_text(sample,20,false,"Subset Sans");
      size_t previous_hit=0;
      for(int x=0;x<=width+10;++x){
        size_t hit=BSF::hit_test(sample,x,20,false,"Subset Sans");
        CHECK(hit<=size && BSF::cluster_boundary(sample,size,hit));
        CHECK(hit>=previous_hit);previous_hit=hit;
      }
      CHECK(BSF::hit_test(sample,width+10,20,false,"Subset Sans")==size);
      CHECK(CzcionkaUI::pozycja_x_z_bajtu(sample,1)==
            BSF::measure_prefix(sample,1,BSF::ui_size()));
      for(int x=0;x<=width;++x)
        CHECK(BSF::cluster_boundary(sample,size,CzcionkaUI::znak_z_pozycji_x(sample,x)));
    }
    CHECK(BSF::measure_prefix("A\xcc\x81",1,20,false,"Subset Sans")==0);
    CHECK(BSF::measure_prefix("A\xcc\x81",3,20,false,"Subset Sans")==BSF::advance_face('A',cluster,20));
    CHECK(BSF::measure_prefix(nullptr,10,20)==0);
    CHECK(BSF::hit_test(nullptr,10,20)==0);
    CHECK(BSF::hit_test("abc",-1,20)==0);
    // Nonterminated buffers are safe when an explicit length is provided.
    const char bounded[]={'A',char(0xcc),char(0x81)};
    CHECK(BSF::measure_prefix(bounded,3,20,false,"Subset Sans",3)==BSF::advance_face('A',cluster,20));
    CHECK(BSF::hit_test(bounded,1000,20,false,"Subset Sans",3)==3);
    CHECK(!Husarz::text_space(" \xcc\x81",3,0));
    CHECK(Husarz::text_space(" x",2,0));
    // Real browser layout and paint share the exact face for every cluster.
    Husarz::Style cluster_style;cluster_style.font=20;cluster_style.native_family=primary;
    auto browser_cluster=Husarz::text_cluster("A\xcc\x81x",4,0,metrics,cluster_style);
    CHECK(browser_cluster.end==3 && browser_cluster.face==cluster);
    CHECK(Husarz::cluster_width("A\xcc\x81x",browser_cluster,metrics,cluster_style)==BSF::advance_face('A',cluster,20));
    auto browser=std::make_unique<Husarz::Document>();
    const char* cluster_html="<body style=\"font-family:'Subset Sans';font-size:20px;margin:0\"><p>ÁÁÁ</p><input value=\"ä́x\"><textarea>ä́x</textarea></body>";
    Husarz::parse_html(*browser,cluster_html,strlen(cluster_html));Husarz::collect_styles(*browser);Husarz::reflow(*browser,4,metrics);
    for(unsigned i=0;i<browser->fragment_count;++i){const auto& f=browser->fragments[i];
      const char* text=browser->str(f.text);CHECK(BSF::cluster_boundary(text,f.length,f.length));
      if(f.length && text[0]=='A')CHECK(f.length%3==0);
    }
    for(unsigned i=0;i<browser->control_count;++i){auto& c=browser->controls[i];if(!Husarz::selectable(*browser,c))continue;
      browser->focused_control=c.node;Husarz::form_move(*browser,c,0,false);
      CHECK(Husarz::form_key(*browser,0x4d));CHECK(c.cursor==5);
      CHECK(Husarz::form_key(*browser,8));CHECK(!strcmp(c.value,"x"));
      CHECK(Husarz::form_replace(*browser,c,"a\xcc\x88\xcc\x81",5));
      for(int x=0;x<100;++x)CHECK(BSF::cluster_boundary(c.value,c.length,Husarz::control_text_hit(*browser,c,metrics,x,0)));
    }
    // Positive and negative cluster results must both be invalidated when
    // the same path/identity acquires a new validated cmap during refresh.
    CHECK(BSF::resolved_cluster("A\xcc\x81",3,false,"Subset Sans")!=primary);
    CHECK(!BSF::resolved_cluster("\xf4\x8f\xbf\xbf",4,false,"Subset Sans"));
    for(unsigned i=0;i<bytes.u16(4);++i){size_t o=12+i*16;if(bytes.u32(o)!=0x636d6170)continue;
      size_t at=bytes.u32(o+8);put32(at+16,52);put32(at+24,3);
      put32(at+40,0x301);put32(at+44,0x301);put32(at+48,full.glyph(0x301));
      put32(at+52,0x10ffff);put32(at+56,0x10ffff);put32(at+60,gid);
    }
    files["/czcionki/ttf/custom.TTF"]=subset;BSF::rescan();
    CHECK(BSF::resolved_cluster("A\xcc\x81",3,false,"Subset Sans")==primary);
    CHECK(BSF::resolved_cluster("\xf4\x8f\xbf\xbf",4,false,"Subset Sans")==primary);
    BSF::shutdown();
  }
  for (auto &f : files)
    f.second = "bad";
  CHECK(!BSF::ready());
  CHECK(CzcionkaUI::pobierz_wysokosc() == 16);
  CHECK(CzcionkaUI::zmierz_tekst("Czytelny pulpit") > 0);
  BSF::shutdown();
  Husarz::font_family_resolver = nullptr;
  printf("BSF UI/browser: %u checks PASS; proportional geometry, clipping/AA, "
         "Polish UTF-8, previews, persistence, font priority, cooperative "
         "paint and corrupt-font fallback\n",
         checks);
}
