#include "../../bsf/runtime.h"
#include "bursztyn_gui.h"
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "loader.h"
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
  SettingsUI::PowerPage* power_page{};
  SettingsUI::SupportPage* support_page{};
  Panel *placeholder{};
  Etykieta *placeholder_text{};
  Etykieta *status{};
  ListaRozwijana *families{};
  ListaRozwijana *size{};
  PowierzchniaRysunkowa *preview{};
  ListaRozwijana *terminal{};
  Etykieta *details{};
  uint32_t ids[BSF::MaxFiles]{}, terminal_ids[BSF::MaxFiles + 1]{};
  unsigned family_count{}, terminal_count{};
  char details_text[256]{};
  static void append(char *out, size_t cap, const char *text) {
    size_t n = 0;
    while (n + 1 < cap && out[n]) ++n;
    for (size_t i = 0; text[i] && n + 1 < cap; ++i) out[n++] = text[i];
    out[n] = 0;
  }
  static void append_number(char *out, size_t cap, unsigned v) {
    char d[12]; unsigned k = 0;
    do { d[k++] = char('0' + v % 10); v /= 10; } while (v && k < 11);
    char t[12]; unsigned j = 0; while (k) t[j++] = d[--k]; t[j] = 0;
    append(out, cap, t);
  }
  const BSF::FontInfo *info_for(uint32_t id) {
    for (unsigned i = 0; i < BSF::enumerate(); ++i)
      if (BSF::info(i)->id == id) return BSF::info(i);
    return nullptr;
  }
  // Format, outline technology, collection face, axes, colour tables and the
  // writing systems this face itself covers (no fallback).
  void update_details() {
    int index = families->wybrany_indeks();
    uint32_t id = index >= 0 && unsigned(index) < family_count ? ids[index] : 0;
    const BSF::FontInfo *f = id ? info_for(id) : nullptr;
    details_text[0] = 0;
    if (!f) { append(details_text, sizeof details_text, "Bitmapowa czcionka awaryjna."); details->ustaw_tekst(details_text); return; }
    static const char *outline[] = {"?", "TrueType", "OpenType CFF", "OpenType CFF2"};
    append(details_text, sizeof details_text, f->outline < 4 ? outline[f->outline] : "?");
    unsigned styles = 0;
    for (unsigned i = 0; i < BSF::enumerate(); ++i) if (same_font_name(BSF::info(i)->family, f->family)) ++styles;
    append(details_text, sizeof details_text, " · style: "); append_number(details_text, sizeof details_text, styles);
    if (f->container == uint8_t(BSF::ContainerFormat::Collection)) {
      append(details_text, sizeof details_text, " · kolekcja, twarz "); append_number(details_text, sizeof details_text, f->face_index + 1u);
      append(details_text, sizeof details_text, "/"); append_number(details_text, sizeof details_text, f->face_count);
    }
    append(details_text, sizeof details_text, " · "); append_number(details_text, sizeof details_text, (f->file_size + 1023) / 1024);
    append(details_text, sizeof details_text, " KiB");
    if (f->axis_count) { append(details_text, sizeof details_text, " · osie: "); append_number(details_text, sizeof details_text, f->axis_count); }
    if (f->color) append(details_text, sizeof details_text, " · tabele koloru");
    if (f->status != BSF::FontOk) append(details_text, sizeof details_text, " · BŁĄD WALIDACJI");
    struct Script { const char *name, *sample; } scripts[] = {
        {"łac", "Zażółć"}, {"cyr", "Привет"}, {"gr", "Καλημέρα"}, {"hebr", "שלום"},
        {"arab", "مرحبا"}, {"jap", "こんにちは"}, {"chiń", "你好"}, {"kor", "안녕"}};
    append(details_text, sizeof details_text, " · pismo:");
    for (auto &sc : scripts) {
      size_t n = 0; while (sc.sample[n]) ++n;
      if (BSF::face_covers(id, sc.sample, n)) { append(details_text, sizeof details_text, " "); append(details_text, sizeof details_text, sc.name); }
    }
    details->ustaw_tekst(details_text);
  }
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
    // Terminal: automatic, or a family whose regular face keeps the cell grid.
    if (terminal) {
      terminal->wyczysc(); terminal_count = 0;
      terminal->dodaj_element("Automatyczna (stała szerokość)", 0); terminal_ids[terminal_count++] = 0;
      uint32_t chosen = BSF::terminal_family();
      for (unsigned j = 0; j < family_count; ++j)
        if (BSF::terminal_suitable(ids[j])) {
          terminal->dodaj_element(BSF::family_name(ids[j]), ids[j]);
          if (ids[j] == chosen) terminal->ustaw_wybrany_indeks(int(terminal_count));
          terminal_ids[terminal_count++] = ids[j];
        }
      if (terminal->wybrany_indeks() < 0) terminal->ustaw_wybrany_indeks(0);
    }
    if (details) update_details();
  }
  /* Strony: 0=System, 1=Energia, 2=Wyglad, 3=Motywy, 4=Czcionki, 5=Wsparcie. */
  void show(unsigned page) {
    font_page->ustaw_widoczna(page == 4);
    system_page->ustaw_widoczna(page==0);
    if(power_page)power_page->ustaw_widoczna(page==1);
    placeholder->ustaw_widoczna(page==2);
    support_page->ustaw_widoczna(page==5);
    themes_view->ustaw_widoczna(page==3);
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
                           "The quick brown fox jumps over the lazy dog.",
                           "Привет мир · Привіт світ",
                           "Καλημέρα κόσμε · Αθήνα", "e\xcc\x81 a\xcc\x88 · Ґґ Єє Іі Її Ўў",
                           // BSF 2.0 bidi + shaping samples (wrapped by word).
                           "\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd (2026)",
                           "\u0645\u0631\u062d\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645 \u0644\u0627",
                           // BSF 2.2: CJK needs no script-specific shaping.
                           "こんにちは世界", "你好世界", "안녕하세요"};
    int column = preview->obszar().szerokosc / 2;
    for (unsigned weight = 0; weight < 2; ++weight) {
      int x = ui_skaluj(10,preview->skala_dpi().procent()) + int(weight) * column, y = ui_skaluj(8,preview->skala_dpi().procent());
      ctx.rysuj_tekst_bsf({x, y}, weight ? "Bold" : "Regular",
                          motyw_kolor(MOTYW_KOLOR_AKCENT), ui_skaluj(14,preview->skala_dpi().procent()), true);
      y += ui_skaluj(22,preview->skala_dpi().procent());
      for (auto *line : lines) {
        size_t length = 0;
        while (line[length])
          ++length;
        // A sample the selected face cannot render itself is not shown in a
        // fallback font: the preview says so instead (BSF 2.2).
        if (id && !BSF::face_covers(id, line, length)) {
          ctx.rysuj_tekst_bsf({x, y}, "— brak glifów w tej czcionce —",
                              motyw_kolor(MOTYW_KOLOR_TEKST_DRUGORZEDNY), ui_skaluj(12,preview->skala_dpi().procent()), false);
          y += BSF::height(px) > 0 ? BSF::height(px) + 2 : 18;
          continue;
        }
#if BSF_COMPLEX_TEXT
        // Samples wrap at spaces so one shaping run is never split inside a
        // word (per-scalar splitting would break joining). BSF 2.2: plain
        // samples use the same word wrapping (no mid-word breaks).
        {
          int limit = column - ui_skaluj(20,preview->skala_dpi().procent());
          char part[256]{};
          size_t start = 0;
          while (start < length) {
            size_t end = start, fit = start;
            while (end < length) {
              size_t next = end;
              while (next < length && line[next] != ' ') ++next;
              if (next - start >= sizeof(part)) break;
              for (size_t k = start; k < next; ++k) part[k - start] = line[k];
              part[next - start] = 0;
              if (fit > start && BSF::measure_text(part, px, weight != 0) > limit) break;
              fit = next;
              end = next < length ? next + 1 : next;
            }
            if (fit == start) fit = end > start ? end : length;
            for (size_t k = start; k < fit; ++k) part[k - start] = line[k];
            part[fit - start] = 0;
            ctx.rysuj_tekst_bsf({x, y}, part, motyw_kolor(MOTYW_KOLOR_TEKST), px, weight != 0);
            y += BSF::height(px) > 0 ? BSF::height(px) + 2 : 18;
            start = fit < length && line[fit] == ' ' ? fit + 1 : fit;
          }
          continue;
        }
#endif
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

    /*
     * Preferencje lokalne maja pierwszenstwo nad konfiguracja systemowa.
     * Klucz jest zapisywany przez apply (patrz nizej), wiec ostatni wybor
     * uzytkownika przezywa restart niezaleznie od /system/konfig.
     */
    {
      char zapisany_rozmiar[8];
      if (ustawienia_wczytaj("ustawienia", "czcionka-rozmiar",
                             zapisany_rozmiar, sizeof(zapisany_rozmiar))) {
        unsigned wartosc = 0;
        bool poprawna = zapisany_rozmiar[0] != '\0';
        for (unsigned i = 0; zapisany_rozmiar[i] != '\0'; ++i) {
          if (zapisany_rozmiar[i] < '0' || zapisany_rozmiar[i] > '9') {
            poprawna = false;
            break;
          }
          wartosc = wartosc * 10U + unsigned(zapisany_rozmiar[i] - '0');
        }
        if (poprawna && wartosc >= 8U && wartosc <= 64U) {
          c.size = wartosc;
        }
      }
    }
    auto &viewport = dodaj<SettingsUI::Viewport>();
    auto &root = *viewport.body;
    themes=root.themes;themes_view=root.themes_view;themes->load();
    system_page=root.system;
    power_page=root.power;
    if(power_page){
      power_page->odswiez();
      power_page->save->przy_kliknieciu(
        [](Przycisk*,void* p){
          auto*s=static_cast<Settings*>(p);
          if(s->power_page)s->power_page->zapisz();
        },this);
      power_page->reset->przy_kliknieciu(
        [](Przycisk*,void* p){
          auto*s=static_cast<Settings*>(p);
          if(s->power_page)s->power_page->przywroc_domyslne();
        },this);
    }    support_page=root.support;support_page->on_reload=[](SettingsUI::SupportPage*,void* p){static_cast<Settings*>(p)->reload_support();};support_page->reload_context=this;
    reload_support();
    font_page=root.fonts;placeholder=root.placeholder;placeholder_text=root.placeholder_text;
    auto* page=root.fonts;
    families=page->families;size=page->size;preview=page->preview;status=page->status;
    terminal=page->terminal;details=page->details;
    for(unsigned i=0;i<6;++i){
      root.nav[i]->przy_kliknieciu([](Przycisk*,void* p){static_cast<Settings*>(p)->show(0);},this);
    }
    /* Indeksy stron: 0=System, 1=Energia, 2=Wyglad, 3=Motywy, 4=Czcionki, 5=Wsparcie. */
    root.nav[0]->przy_kliknieciu([](Przycisk*,void* p){static_cast<Settings*>(p)->show(0);},this);
    root.nav[1]->przy_kliknieciu([](Przycisk*,void* p){
      auto*s=static_cast<Settings*>(p);
      if(s->power_page)s->power_page->odswiez();
      s->show(1);
    },this);
    root.nav[2]->przy_kliknieciu([](Przycisk*,void* p){static_cast<Settings*>(p)->show(2);},this);
    root.nav[3]->przy_kliknieciu([](Przycisk*,void* p){static_cast<Settings*>(p)->show(3);},this);
    root.nav[4]->przy_kliknieciu([](Przycisk*,void* p){static_cast<Settings*>(p)->show(4);},this);
    root.nav[5]->przy_kliknieciu([](Przycisk*,void* p){static_cast<Settings*>(p)->show(5);},this);
    reload_fonts();
    page->refresh->przy_kliknieciu([](Przycisk*,void* p){auto* self=static_cast<Settings*>(p);
      BSF::rescan();self->reload_fonts();auto limits=BSF::resources();
      bool missing=BSF::configuration().family&&!self->info_for(BSF::configuration().family);
      self->status->ustaw_tekst(limits.directory_error?"Nie odczytano katalogu fontów (limit 64 KiB).":limits.catalog_limit?"Osiągnięto limit katalogu: 128 fontów / 1024 wpisy.":
        missing?"Wybrana czcionka zniknęła; używana jest czcionka domyślna.":"Odświeżono listę fontów.");
      self->przelicz_uklad();self->preview->uniewaznij();},this);
    page->install->przy_kliknieciu([](Przycisk*,void* p){auto* self=static_cast<Settings*>(p);
      auto result=BSF::install_font(self->font_page?static_cast<SettingsUI::FontPage*>(self->font_page)->import_path->tekst():nullptr);
      const char* message=result==BSF::InstallResult::Ok?"Zainstalowano font. Uruchom ponownie inne aplikacje, aby go wykryły.":
        result==BSF::InstallResult::Exists?"Font o tej nazwie pliku już istnieje; nie został zastąpiony.":
        result==BSF::InstallResult::BadPath?"Podaj ścieżkę do pliku .ttf, .otf, .ttc lub .otc (nazwa do 55 bajtów).":
        result==BSF::InstallResult::InvalidFont?"Plik nie jest poprawnym, obsługiwanym fontem (każda twarz jest sprawdzana; limit 16 MiB).":"Nie udało się zainstalować fontu (uprawnienia lub błąd zapisu).";
      if(result==BSF::InstallResult::Ok)self->reload_fonts();self->status->ustaw_tekst(message);self->przelicz_uklad();self->preview->uniewaznij();
    },this);
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
          static_cast<Settings *>(p)->update_details();
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
          int t = s.terminal ? s.terminal->wybrany_indeks() : 0;
          c.terminal_family = t >= 0 && unsigned(t) < s.terminal_count ? s.terminal_ids[t] : 0;
          s.status->ustaw_tekst(BSF::apply(c)
                                    ? "Zapisano ustawienia czcionki."
                                    : "Nie zapisano. Poprzednia czcionka jest aktywna.");
          /*
           * Trwaly zapis preferencji per aplikacja. Dzieki temu ustawienia
           * przezywaja restart: aplikacja nie musi polegac wylacznie na
           * konfiguracji BSF zapisanej w /system/konfig.
           */
          char rozmiar_txt[8];
          unsigned wartosc = c.size;
          int indeks = 7;
          rozmiar_txt[indeks--] = '\0';
          if (wartosc == 0) rozmiar_txt[indeks--] = '0';
          while (wartosc != 0 && indeks >= 0) {
            rozmiar_txt[indeks--] = char('0' + (wartosc % 10U));
            wartosc /= 10U;
          }
          (void)ustawienia_zapisz("ustawienia", "czcionka-rozmiar",
                                  &rozmiar_txt[indeks + 1]);
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
