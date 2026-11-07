#include "../../bsf/runtime.h"
#include "../../bursztyn_gui.h"
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "../../loader.h"
#include "layout.h"
#include <string.h>
using namespace BursztynUI;
extern "C" void _start();
extern "C" {
__attribute__((section(".naglowek"), used)) NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},
    reinterpret_cast<uint64_t>(&_start),
    0x1000,
    0x33000,
    0x1001000,
    0x34000,
    0x20000,
    0x1034000};
}
namespace {
bool same_font_name(const char* a,const char* b){for(unsigned i=0;i<96;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}return false;}
constexpr int sizes[] = {12, 14, 16, 18, 20, 24};
class Settings : public Okno {
  SettingsUI::ThemesPage* themes{};
  SettingsUI::ThemesViewport* themes_view{};
  Panel *font_page{};
  SettingsUI::SystemPage* system_page{};
  SettingsUI::SupportPage* support_page{};
  Panel *placeholder{};
  Etykieta *placeholder_text{};
  Etykieta *status{};
  ListaRozwijana *families{};
  ListaRozwijana *size{};
  PowierzchniaRysunkowa *preview{};
  uint32_t ids[BSF::MaxFiles]{};
  unsigned family_count{};
  void reload_fonts() {
    auto c=BSF::configuration(); families->wyczysc(); family_count=0;
    for(unsigned i=0;i<BSF::enumerate();++i){
      auto* f=BSF::info(i);bool duplicate=false;
      for(unsigned j=0;j<family_count;++j) if(same_font_name(BSF::family_name(ids[j]),f->family))duplicate=true;
      if(duplicate)continue;
      // Prefer a regular representative, retain families with incomplete styles.
      for(unsigned j=i;j<BSF::enumerate();++j){auto* candidate=BSF::info(j);
        if(same_font_name(candidate->family,f->family)&&!candidate->italic&&candidate->weight<600){f=candidate;break;}}
      families->dodaj_element(f->family,f->id);ids[family_count]=f->id;
      if(c.family==f->id || (!c.family && f->id==BSF::default_id()))families->ustaw_wybrany_indeks(int(family_count));
      ++family_count;
    }
    if(!family_count)families->dodaj_element("Bitmapowa czcionka awaryjna",0);
    if(families->wybrany_indeks()<0)families->ustaw_wybrany_indeks(0);
  }
  void show(unsigned page) {
    font_page->ustaw_widoczna(page == 3);
    system_page->ustaw_widoczna(page==0);
    placeholder->ustaw_widoczna(page==1);
    support_page->ustaw_widoczna(page==4);
    themes_view->ustaw_widoczna(page==2);
    placeholder_text->ustaw_tekst("Wybierz Motywy, aby zmienić wygląd całego systemu.");
    przelicz_uklad();
    uniewaznij();
  }
  void reload_support(){
    if(!support_page->data)return;
    for(unsigned i=0;i<3;++i)SupportData::reload(support_page->data[i],i,pobierz_rozmiar_pliku,
      [](const char* path,char* out,uint32_t bytes,size_t* got){uint32_t n=0;bool ok=czytaj_fragment_pliku(path,0,out,bytes,&n);*got=n;return ok;});
    support_page->refresh();uniewaznij();
  }
  void sample(KontekstRysowania &ctx) {
    int index = families->wybrany_indeks(), si = size->wybrany_indeks();
    if (si < 0 || si >= 6)
      si = 2;
    uint32_t id = index >= 0 && unsigned(index) < family_count ? ids[index] : 0;
    BSF::preview(id, sizes[si]);
    int px = ui_skaluj(sizes[si],preview->skala_dpi().procent());
    const char *lines[] = {"Bursztyn OS", "Zażółć gęślą jaźń",
                           "Привет мир · Привіт світ",
                           "Καλημέρα κόσμε · Αθήνα", "e\xcc\x81 a\xcc\x88 · Ґґ Єє Іі Її Ўў"};
    int column = preview->obszar().szerokosc / 2;
    for (unsigned weight = 0; weight < 2; ++weight) {
      int x = ui_skaluj(10,preview->skala_dpi().procent()) + int(weight) * column, y = ui_skaluj(8,preview->skala_dpi().procent());
      ctx.rysuj_tekst_bsf({x, y}, weight ? "Bold" : "Regular",
                          motyw_kolor(MOTYW_KOLOR_AKCENT), ui_skaluj(14,preview->skala_dpi().procent()), true);
      y += ui_skaluj(22,preview->skala_dpi().procent());
      for (auto *line : lines) {
        char part[256]{};
        unsigned used = 0;
        int width = 0;
        for (size_t at = 0; line[at];) {
          uint32_t cp;
          size_t bytes = CzcionkaUI::dekoduj_utf8(line, at, &cp);
          int step = BSF::measure_cp(cp, px, weight != 0);
          if (step < 0)
            step = px / 2 + 1;
          if (used && width + step > column - ui_skaluj(20,preview->skala_dpi().procent())) {
            part[used] = 0;
            if (!ctx.rysuj_tekst_bsf({x, y}, part,
                                     motyw_kolor(MOTYW_KOLOR_TEKST), px,
                                     weight != 0))
              ctx.rysuj_tekst({x, y}, part, motyw_kolor(MOTYW_KOLOR_TEKST));
            y += BSF::height(px) > 0 ? BSF::height(px) + 2 : 18;
            used = 0;
            width = 0;
          }
          for (size_t j = 0; j < bytes && used + 1 < sizeof(part); ++j)
            part[used++] = line[at + j];
          at += bytes;
          width += step;
        }
        part[used] = 0;
        if (!ctx.rysuj_tekst_bsf({x, y}, part, motyw_kolor(MOTYW_KOLOR_TEKST),
                                 px, weight != 0))
          ctx.rysuj_tekst({x, y}, part, motyw_kolor(MOTYW_KOLOR_TEKST));
        y += BSF::height(px) > 0 ? BSF::height(px) + 2 : 18;
      }
    }
    BSF::end_preview();
  }

public:
  void zamknij()override{if(themes)themes->cancel_preview();Okno::zamknij();}
  Settings() : Okno("Ustawienia", 950, 540) {}
  void init() {
    auto c = BSF::configuration();
    auto &viewport = dodaj<SettingsUI::Viewport>();
    auto &root = *viewport.body;
    themes=root.themes;themes_view=root.themes_view;themes->load();
    system_page=root.system;
    support_page=root.support;support_page->on_reload=[](SettingsUI::SupportPage*,void* p){static_cast<Settings*>(p)->reload_support();};support_page->reload_context=this;
    reload_support();
    font_page=root.fonts;placeholder=root.placeholder;placeholder_text=root.placeholder_text;
    auto* page=root.fonts;
    families=page->families;size=page->size;preview=page->preview;status=page->status;
    root.nav[2]->przy_kliknieciu([](Przycisk*,void* p){static_cast<Settings*>(p)->show(2);},this);
    for(unsigned i=0;i<5;++i){
      if(i==0)root.nav[i]->przy_kliknieciu([](Przycisk*,void* p){static_cast<Settings*>(p)->show(0);},this);
      if(i==1)root.nav[i]->przy_kliknieciu([](Przycisk*,void* p){static_cast<Settings*>(p)->show(1);},this);
      if(i==4)root.nav[i]->przy_kliknieciu([](Przycisk*,void* p){static_cast<Settings*>(p)->show(4);},this);
      if(i==3)root.nav[i]->przy_kliknieciu([](Przycisk*,void* p){static_cast<Settings*>(p)->show(3);},this);
    }
    reload_fonts();
    page->refresh->przy_kliknieciu([](Przycisk*,void* p){auto* self=static_cast<Settings*>(p);
      BSF::rescan();self->reload_fonts();self->status->ustaw_tekst("Odświeżono listę fontów.");
      self->przelicz_uklad();self->preview->uniewaznij();},this);
    const char *labels[] = {"12 px", "14 px", "16 px",
                            "18 px", "20 px", "24 px"};
    for (unsigned i = 0; i < 6; ++i) {
      size->dodaj_element(labels[i]);
      if (c.size == unsigned(sizes[i]))
        size->ustaw_wybrany_indeks(int(i));
    }
    preview->przy_rysowaniu(
        [](PowierzchniaRysunkowa *, KontekstRysowania &ctx, void *p) {
          static_cast<Settings *>(p)->sample(ctx);
        },
        this);
    families->przy_zmianie_wyboru(
        [](ListaRozwijana *, int, void *p) {
          static_cast<Settings *>(p)->przelicz_uklad();
          static_cast<Settings *>(p)->preview->uniewaznij();
        },
        this);
    size->przy_zmianie_wyboru(
        [](ListaRozwijana *, int, void *p) {
          static_cast<Settings *>(p)->przelicz_uklad();
          static_cast<Settings *>(p)->preview->uniewaznij();
        },
        this);
    auto &apply=*page->apply;
    apply.przy_kliknieciu(
        [](Przycisk *, void *p) {
          auto &s = *static_cast<Settings *>(p);
          auto c = BSF::configuration();
          int i = s.families->wybrany_indeks(), n = s.size->wybrany_indeks();
          c.family = i >= 0 && unsigned(i) < s.family_count ? s.ids[i] : 0;
          c.size = unsigned(sizes[n >= 0 && n < 6 ? n : 2]);
          s.status->ustaw_tekst(BSF::apply(c)
                                    ? "Zapisano ustawienia czcionki."
                                    : "Nie zapisano. Poprzednia czcionka jest aktywna.");
        },
        this);
    auto &reset = *page->reset;
    reset.przy_kliknieciu(
        [](Przycisk *, void *p) {
          auto &s = *static_cast<Settings *>(p);
          for (unsigned i = 0; i < s.family_count; ++i)
            if (s.ids[i] == BSF::default_id())
              s.families->ustaw_wybrany_indeks(int(i));
          s.size->ustaw_wybrany_indeks(2);
          s.przelicz_uklad();
          s.preview->uniewaznij();
        },
        this);
    show(0);
  }
};
} // namespace
extern "C" __attribute__((noreturn)) void _start() {
  AplikacjaGUI app;
  Settings window;
  window.init();
  (void)app.uruchom(window);
  bws_zakoncz_proces();
}
