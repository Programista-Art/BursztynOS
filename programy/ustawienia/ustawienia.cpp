#include "../../bsf/runtime.h"
#include "../../bursztyn_gui.h"
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "../../loader.h"
using namespace BursztynUI;
extern "C" void _start();
extern "C" {
__attribute__((section(".naglowek"), used)) NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},
    reinterpret_cast<uint64_t>(&_start),
    0x1000,
    0x30000,
    0x1001000,
    0x31000,
    0x20000,
    0x1031000};
}
namespace {
constexpr int sizes[] = {12, 14, 16, 18, 20, 24};
class Settings : public Okno {
  Panel *font_page{};
  Panel *placeholder{};
  Etykieta *placeholder_text{};
  Etykieta *status{};
  ListaRozwijana *families{};
  ListaRozwijana *size{};
  PowierzchniaRysunkowa *preview{};
  uint32_t ids[BSF::MaxFiles]{};
  unsigned family_count{};
  void show(unsigned page) {
    font_page->ustaw_widoczna(page == 1);
    placeholder->ustaw_widoczna(page != 1);
    placeholder_text->ustaw_tekst(
        page == 0 ? "Wygląd: motywy są dostępne w aplikacji Motywy systemowe."
                  : "System: ta sekcja nie udostępnia jeszcze ustawień.");
    przelicz_uklad();
    uniewaznij();
  }
  void sample(KontekstRysowania &ctx) {
    int index = families->wybrany_indeks(), si = size->wybrany_indeks();
    if (si < 0 || si >= 6)
      si = 2;
    uint32_t id = index >= 0 && unsigned(index) < family_count ? ids[index] : 0;
    BSF::preview(id, sizes[si]);
    int px = sizes[si];
    const char *lines[] = {"Bursztyn OS", "Zażółć gęślą jaźń",
                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
                           "abcdefghijklmnopqrstuvwxyz", "0123456789"};
    int column = preview->obszar().szerokosc / 2;
    for (unsigned weight = 0; weight < 2; ++weight) {
      int x = 10 + int(weight) * column, y = 8;
      ctx.rysuj_tekst_bsf({x, y}, weight ? "Bold" : "Regular",
                          motyw_kolor(MOTYW_KOLOR_AKCENT), 14, true);
      y += 22;
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
          if (used && width + step > column - 20) {
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
  Settings() : Okno("Ustawienia", 950, 680) {}
  void init() {
    auto c = BSF::configuration();
    auto &root = dodaj<Panel>(Orientacja::Pozioma);
    root.ustaw_odstep(14);
    root.ustaw_wypelnienie({12, 12, 12, 12});
    auto &navigation = root.dodaj<Panel>(Orientacja::Pionowa);
    navigation.ustaw_preferowany_rozmiar({165, -1});
    navigation.ustaw_odstep(10);
    navigation.dodaj<Etykieta>("Ustawienia");
    const char *pages[] = {"Wygląd", "Czcionki", "System"};
    for (unsigned i = 0; i < 3; ++i) {
      auto &b = navigation.dodaj<Przycisk>(pages[i]);
      b.ustaw_preferowany_rozmiar({-1, 36});
      if (i == 0)
        b.przy_kliknieciu(
            [](Przycisk *, void *p) { static_cast<Settings *>(p)->show(0); },
            this);
      if (i == 1)
        b.przy_kliknieciu(
            [](Przycisk *, void *p) { static_cast<Settings *>(p)->show(1); },
            this);
      if (i == 2)
        b.przy_kliknieciu(
            [](Przycisk *, void *p) { static_cast<Settings *>(p)->show(2); },
            this);
    }
    auto &content = root.dodaj<Panel>(Orientacja::Pionowa);
    font_page = &content.dodaj<Panel>(Orientacja::Pionowa);
    font_page->ustaw_odstep(8);
    font_page->dodaj<Etykieta>("Ustawienia czcionki");
    auto &selectors = font_page->dodaj<Panel>(Orientacja::Pozioma);
    selectors.ustaw_preferowany_rozmiar({-1, -1});
    selectors.ustaw_odstep(12);
    auto &family_panel = selectors.dodaj<Panel>(Orientacja::Pionowa);
    family_panel.dodaj<Etykieta>("Systemowa czcionka");
    families = &family_panel.dodaj<ListaRozwijana>();
    families->ustaw_preferowany_rozmiar({-1, 34});
    for (unsigned i = 0; i < BSF::enumerate(); ++i) {
      auto *f = BSF::info(i);
      if (f->weight >= 600 || f->italic)
        continue;
      families->dodaj_element(f->family, f->id);
      ids[family_count] = f->id;
      if ((c.family ? c.family : BSF::default_id()) == f->id)
        families->ustaw_wybrany_indeks(int(family_count));
      ++family_count;
    }
    if (!family_count)
      families->dodaj_element("Bitmapowa czcionka awaryjna", 0);
    if (families->wybrany_indeks() < 0)
      families->ustaw_wybrany_indeks(0);
    auto &size_panel = selectors.dodaj<Panel>(Orientacja::Pionowa);
    size_panel.ustaw_preferowany_rozmiar({150, -1});
    size_panel.dodaj<Etykieta>("Rozmiar");
    size = &size_panel.dodaj<ListaRozwijana>();
    const char *labels[] = {"12 px", "14 px", "16 px",
                            "18 px", "20 px", "24 px"};
    for (unsigned i = 0; i < 6; ++i) {
      size->dodaj_element(labels[i]);
      if (c.size == unsigned(sizes[i]))
        size->ustaw_wybrany_indeks(int(i));
    }
    size->ustaw_preferowany_rozmiar({150, 34});
    preview = &font_page->dodaj<PowierzchniaRysunkowa>();
    preview->ustaw_preferowany_rozmiar({-1, 300});
    preview->przy_rysowaniu(
        [](PowierzchniaRysunkowa *, KontekstRysowania &ctx, void *p) {
          static_cast<Settings *>(p)->sample(ctx);
        },
        this);
    families->przy_zmianie_wyboru(
        [](ListaRozwijana *, int, void *p) {
          static_cast<Settings *>(p)->preview->uniewaznij();
        },
        this);
    size->przy_zmianie_wyboru(
        [](ListaRozwijana *, int, void *p) {
          static_cast<Settings *>(p)->preview->uniewaznij();
        },
        this);
    auto &buttons = font_page->dodaj<Panel>(Orientacja::Pozioma);
    buttons.ustaw_odstep(12);
    auto &apply = buttons.dodaj<Przycisk>("Zastosuj");
    apply.ustaw_preferowany_rozmiar({150, 36});
    apply.przy_kliknieciu(
        [](Przycisk *, void *p) {
          auto &s = *static_cast<Settings *>(p);
          auto c = BSF::configuration();
          int i = s.families->wybrany_indeks(), n = s.size->wybrany_indeks();
          c.family = i >= 0 && unsigned(i) < s.family_count ? s.ids[i] : 0;
          c.size = unsigned(sizes[n >= 0 && n < 6 ? n : 2]);
          s.status->ustaw_tekst(BSF::apply(c)
                                    ? "Zapisano ustawienia czcionki."
                                    : "Nie udało się zapisać ustawień. "
                                      "Poprzednia czcionka pozostaje aktywna.");
        },
        this);
    auto &reset = buttons.dodaj<Przycisk>("Przywróć domyślne");
    reset.przy_kliknieciu(
        [](Przycisk *, void *p) {
          auto &s = *static_cast<Settings *>(p);
          for (unsigned i = 0; i < s.family_count; ++i)
            if (s.ids[i] == BSF::default_id())
              s.families->ustaw_wybrany_indeks(int(i));
          s.size->ustaw_wybrany_indeks(2);
          s.preview->uniewaznij();
        },
        this);
    status = &font_page->dodaj<Etykieta>(
        "Podgląd jest lokalny. Zastosuj zapisuje zmianę dla aplikacji.");
    font_page->dodaj<Etykieta>(
        "Terminal zachowuje czcionkę o stałej szerokości.");
    placeholder = &content.dodaj<Panel>(Orientacja::Pionowa);
    placeholder_text = &placeholder->dodaj<Etykieta>("");
    show(1);
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
