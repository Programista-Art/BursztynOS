#include "bursztyn_ui_host_fixture.h"
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <memory>
#define HUSARZ_HOST_TEST 1
#include "../programy/przegladarka.cpp"
static unsigned checks = 0, publications = 0, requests = 0;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(x)) {                                                                                          \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                             \
            std::abort();                                                                                    \
        }                                                                                                    \
    } while (0)
static bool dns_fails = false, http_fails = false, attempt_reentry = false;
static std::string response;
static uint32_t last_response_bytes;
void wypisz(const char *) {}
bool utworz(const char *) { return true; }
bool zapisz_plik(const char *, const char *, uint32_t) { return true; }
bool czytaj_plik(const char *, char *, uint32_t) { return false; }
bool pobierz_argument_startowy(char *, uint32_t) { return false; }
static void validate_publication() {
    ++publications;
    auto &w = *g_husarz.okno;
    auto bar = g_husarz.pasek_stanu->obszar_ekranu();
    auto client = w.obszar_klienta();
    CHECK(bar.y + bar.wysokosc == client.y + client.wysokosc);
    CHECK(bar.y == w.wysokosc() - bar.wysokosc);
    for (const auto &r : test_paint_rects) {
        if(r.x<w.x()) std::fprintf(stderr,"publication=%u rect=%d,%d %dx%d window=%d,%d\n",publications,r.x,r.y,r.w,r.h,w.x(),w.y());
        CHECK(r.x >= w.x());
        CHECK(r.y >= w.y());
        CHECK(int64_t(r.x) + r.w <= int64_t(w.x()) + w.szerokosc());
        CHECK(int64_t(r.y) + r.h <= int64_t(w.y()) + w.wysokosc());
    }
    test_paint_rects.clear();
}
bool bws_siec_dns(const char *, uint8_t *ip) {
    if (dns_fails)
        return false;
    ip[0] = 10;
    ip[1] = 0;
    ip[2] = 2;
    ip[3] = 2;
    return true;
}
bool bws_siec_pobierz_http(uint8_t *, const char *, const char *path, char *data, uint32_t cap) {
    ++requests;
    if (attempt_reentry) {
        int tabs = liczba_zakladek;
        g_husarz.zamknij_zakladke_gui(0);
        g_husarz.nowa_zakladka_gui();
        g_husarz.przejdz_do_url("http://other/");
        CHECK(liczba_zakladek == tabs);
    }
    if (http_fails)
        return false;
    std::string payload =
        std::strcmp(path, "/site.css") == 0 ? "body {color:#123456} p{padding:4px}" : response;
    std::string type = std::strcmp(path, "/site.css") == 0 ? "text/css" : "text/html";
    for (const char* fixture : {"render.css","sample.png","sample.jpg","malformed.png"}) {
        if (std::string(path)==std::string("/")+fixture) {
            std::ifstream in(std::string("narzedzia/fixtures/husarz/")+fixture,std::ios::binary);
            payload=std::string((std::istreambuf_iterator<char>(in)),{});
            type=std::strstr(fixture,".css")?"text/css":std::strstr(fixture,".jpg")?"image/jpeg":"image/png";
        }
    }
    std::string all = "HTTP/1.1 200 OK\r\nContent-Type: " + type +
                      "\r\nContent-Length: " + std::to_string(payload.size()) + "\r\n\r\n" + payload;
    if (all.size() + 1 > cap)
        return false;
    last_response_bytes=uint32_t(all.size());
    std::memcpy(data, all.c_str(), all.size() + 1);
    return true;
}
bool bws_siec_pobierz_https(uint8_t *ip, const char *domain, const char *path, char *data, uint32_t cap) {
    return bws_siec_pobierz_http(ip, domain, path, data, cap);
}
uint32_t bws_siec_pobierz_bajty(bool, uint8_t* ip,const char* domain,const char* path,char* data,uint32_t cap) {
    return bws_siec_pobierz_http(ip,domain,path,data,cap) ? last_response_bytes : 0;
}
bool bws_tls_certyfikat_zaufany() { return true; }
static void wheel(int delta) {
    ZdarzenieRolki e;
    e.delta_y = delta;
    g_husarz.widok_strony->obsluz_rolke(e);
    CHECK(e.obsluzone);
}
static void network_parser_tests() {
    ParsedUrl url{};
    CHECK(parsuj_url("https://example.test/path?q=1", &url));
    CHECK(url.https);
    CHECK(Husarz::equal(url.domena, "example.test"));
    CHECK(Husarz::equal(url.sciezka, "/path?q=1"));
    char resolved[2048];
    CHECK(ustaw_url_z_location(resolved,sizeof(resolved),"?next=2",url));
    CHECK(Husarz::equal(resolved,"https://example.test/path?next=2"));
    CHECK(!ustaw_url_z_location(resolved,sizeof(resolved),"file:///local",url));
    CHECK(!ustaw_url_z_location(resolved,sizeof(resolved),"javascript:void(0)",url));
    auto doc=std::make_unique<Husarz::Document>();
    Husarz::parse_html(*doc,"<base href='/assets/'>",21);
    CHECK(document_url_base("https://example.test/path",doc.get(),url));
    CHECK(Husarz::equal(url.sciezka,"/assets/"));
    CHECK(!parsuj_url("javascript:alert(1)", &url));
    uint8_t ip[4]{};
    CHECK(adres_ipv4_literal("10.0.2.2", ip));
    CHECK(ip[0] == 10 && ip[3] == 2);
    CHECK(!adres_ipv4_literal("256.0.0.1", ip));
    CHECK(!adres_ipv4_literal("1.2.3", ip));
    CHECK(!adres_ipv4_literal("example.test", ip));
    const char *raw = "HTTP/1.1 200 OK\r\nContent-Type: text/css\r\nTransfer-Encoding: "
                      "chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n";
    OdpowiedzHttp parsed{};
    CHECK(parsuj_odpowiedz_http(raw, std::strlen(raw), &parsed));
    CHECK(parsed.kod == 200 && parsed.tekst && parsed.chunked && !parsed.html);
    char body[32];
    size_t len = 0;
    CHECK(dekoduj_chunked(parsed.body, parsed.body_len, body, sizeof(body), &len));
    CHECK(len == 3 && std::strcmp(body, "abc") == 0);
    CHECK(!dekoduj_chunked("fffffff\r\nx", 10, body, sizeof(body), &len));
    CHECK(!parsuj_odpowiedz_http("HTTP/1.1 xx", 11, &parsed));
}
int main() {
    network_parser_tests();
    Okno::g_tlum_operacje_systemowe = true;
    {
        OknoHusarz w("Husarz regression", 800, 550);
        g_husarz.okno = &w;
        g_husarz.zbuduj_ui();
        inicjalizuj_stan();
        g_husarz.przelacz_zakladke(0);
        w.ustaw_geometrie(137, 71, 800, 550);
        w.przelicz_uklad();
        test_capture_paints = true;
        test_publish_observer = validate_publication;
        response = "<title>Long page</title><link rel=stylesheet href=/site.css><body><h1>Husarz</h1>";
        for (unsigned i = 0; i < 180; ++i)
            response += "<p>Żółć i przewijanie: a long measured line with text.</p>";
        response += "</body>";
        auto *toolbar = g_husarz.pasek_narzedzi;
        auto *status = g_husarz.pasek_stanu;
        auto *viewport = g_husarz.widok_strony;
        g_husarz.przejdz_do_url("http://example.test/page");
        CHECK(requests == 2);
        CHECK(publications >= 3);
        CHECK(status == g_husarz.pasek_stanu && toolbar == g_husarz.pasek_narzedzi &&
              viewport == g_husarz.widok_strony);
        auto &bar = *g_husarz.suwak_pionowy;
        auto *doc = zakladki[0].dokument;
        CHECK(doc);
        CHECK(Husarz::ScriptDom(*doc).query_selector("title").id);
        auto body = Husarz::ScriptDom(*doc).query_selector("body");
        CHECK(body.id);
        CHECK(doc->styles[body.id].color == 0x123456);
        int height = viewport->obszar().wysokosc;
        CHECK(bar.maksimum() == doc->height - height);
        CHECK(bar.rozmiar_strony() == height);
        CHECK(bar.wartosc() == 0);
        wheel(-1);
        CHECK(bar.wartosc() == bar.maly_krok());
        int64_t after = bar.wartosc();
        wheel(1);
        CHECK(bar.wartosc() == 0);
        wheel(-120);
        CHECK(bar.wartosc() == after);
        wheel(-240);
        CHECK(bar.wartosc() == after * 3);
        wheel(0x7fffffff);
        CHECK(bar.wartosc() == 0);
        wheel(INT_MIN);
        CHECK(bar.wartosc() == bar.maksimum());
        bar.ustaw_wartosc(0);
        auto thumb = bar.prostokat_uchwytu();
        auto pos = bar.obszar_ekranu();
        ZdarzenieMyszki down;
        down.przycisk = 1;
        down.x = pos.x + thumb.x + thumb.szerokosc / 2;
        down.y = pos.y + thumb.y + thumb.wysokosc / 2;
        bar.obsluz_wcisniecie_myszy(down);
        CHECK(bar.czy_przeciagany());
        ZdarzenieMyszki drag = down;
        drag.y = pos.y + bar.obszar().wysokosc - 25;
        bar.obsluz_ruch_myszy(drag);
        CHECK(bar.wartosc() > 0);
        CHECK(bar.wartosc() <= bar.maksimum());
        bar.obsluz_zwolnienie_myszy(drag);
        CHECK(!bar.czy_przeciagany());
        // Track clicks page the same model, rather than a second scroll variable.
        bar.ustaw_wartosc(0);
        thumb = bar.prostokat_uchwytu();
        down.y = pos.y + thumb.dolny() + 20;
        bar.obsluz_wcisniecie_myszy(down);
        bar.obsluz_zwolnienie_myszy(down);
        CHECK(bar.wartosc() > 0);
        for (auto code : {0x47U, 0x4fU, 0x49U, 0x51U}) {
            ZdarzenieKlawiatury key;
            key.kod_klawisza = code;
            viewport->obsluz_wcisniecie_klawisza(key);
            CHECK(key.obsluzone);
            CHECK(bar.wartosc() >= 0 && bar.wartosc() <= bar.maksimum());
        }
        auto parses = doc->parse_count;
        bar.ustaw_wartosc(bar.maksimum());
        w.ustaw_rozmiar(600, 450);
        CHECK(doc == zakladki[0].dokument);
        CHECK(doc->parse_count == parses);
        CHECK(doc->layout_width == viewport->obszar().szerokosc);
        CHECK(bar.wartosc() <= bar.maksimum());
        for (int i = 0; i < 4; ++i) {
            bar.ustaw_wartosc(1000);
            g_husarz.odswiez();
            CHECK(bar.wartosc() == 0);
            CHECK(g_husarz.pasek_stanu == status);
            CHECK(g_husarz.pasek_narzedzi == toolbar);
            CHECK(g_husarz.widok_strony == viewport);
            CHECK(Husarz::equal(zakladki[0].url, "http://example.test/page"));
        }
        g_husarz.przejdz_do_url("http://example.test/second");
        CHECK(historia_idx >= 1);
        g_husarz.wstecz();
        CHECK(Husarz::equal(zakladki[0].url, "http://example.test/page"));
        g_husarz.dalej();
        CHECK(Husarz::equal(zakladki[0].url, "http://example.test/second"));
        unsigned before_key = requests;
        bws_zdarzenie f5{};
        f5.typ = BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;
        f5.kod = BWS_KEY_SOURCE_USB | 0x3e;
        f5.x = 1;
        w.obsluz_zdarzenie(f5);
        f5.typ = BWS_ZDARZENIE_KLAWISZ;
        f5.kod = 0x3f;
        w.obsluz_zdarzenie(f5);
        CHECK(requests == before_key + 1);
        attempt_reentry = true;
        g_husarz.odswiez();
        attempt_reentry = false;
        CHECK(liczba_zakladek == 1);
        dns_fails = true;
        g_husarz.odswiez();
        CHECK(g_husarz.pasek_stanu == status);
        CHECK(zakladki[0].dokument);
        CHECK(bar.wartosc() == 0);
        CHECK(bar.maksimum() == 0);
        dns_fails = false;
        http_fails = true;
        g_husarz.odswiez();
        CHECK(zakladki[0].dokument);
        CHECK(bar.maksimum() == 0);
        http_fails = false;
        response = "<body><p>Short page</p></body>";
        g_husarz.odswiez();
        CHECK(bar.maksimum() == 0);
        CHECK(bar.wartosc() == 0);
        CHECK(!bar.czy_wlaczona());
        wheel(-1);
        CHECK(bar.wartosc() == 0);
        thumb = bar.prostokat_uchwytu();
        CHECK(thumb.wysokosc > 0);
        // Actual WidokStrony pixels must survive both desktop palettes, including
        // unstyled content, author white/black pages, controls and external CSS/images.
        for (const char* fixture : {"theme-white.html","theme-black.html","render.html"}) {
            std::ifstream in(std::string("narzedzia/fixtures/husarz/")+fixture);
            response=std::string((std::istreambuf_iterator<char>(in)),{});
            g_husarz.przejdz_do_url("http://example.test/render.html");
            auto *document=zakladki[0].dokument;
            auto before=viewport->document_pixel_hash(); test_paint_rects.clear();
            auto reflows=document->reflow_count;
            BursztynMotyw dark=g_domyslny_motyw_fallback, light=dark;
            dark.poletekstowe=0x15171a; dark.tekst=0xffffff; dark.panel=0x202328;
            light.poletekstowe=0xffffff; light.tekst=0x101010; light.panel=0xeeeeee;
            uint64_t chrome_hash[2]{}; unsigned theme_index=0;
            for (auto* palette : {&dark,&light}) {
                test_ustaw_aktywny_motyw(palette); w.zaktualizuj_motyw();
                test_paint_rects.clear(); test_capture_pixels=true; test_pixels.clear();
                KontekstRysowania context; w.rysuj(context,true);
                test_capture_pixels=false;
                uint64_t h=0;
                for (auto pair:test_pixels) if (pair.first.second<viewport->obszar_ekranu().y) h=h*131+pair.second;
                test_pixels.clear();
                chrome_hash[theme_index++]=h; test_paint_rects.clear();
                CHECK(viewport->document_pixel_hash()==before); test_paint_rects.clear(); CHECK(document->reflow_count==reflows);
            }
            CHECK(chrome_hash[0]!=chrome_hash[1]);
            test_ustaw_aktywny_motyw(nullptr); w.zaktualizuj_motyw();
            if (std::strcmp(fixture,"render.html")==0) {
                CHECK(document->image_count==4); // PNG + JPEG + repeated PNG + background; malformed rejected
                CHECK(resource_cache && resource_cache->images_decoded==2);
                CHECK(resource_cache->image_failures>0);
                auto network_before=requests;
                g_husarz.odswiez(); CHECK(requests==network_before+1);
                CHECK(resource_cache->hits>=4);
                std::printf("cache hits=%llu misses=%llu decoded=%llu failures=%llu raw=%zu decoded_bytes=%zu\n",
                    (unsigned long long)resource_cache->hits,(unsigned long long)resource_cache->misses,
                    (unsigned long long)resource_cache->images_decoded,(unsigned long long)resource_cache->image_failures,
                    resource_cache->raw_bytes,resource_cache->decoded_bytes);
                g_husarz.nowa_zakladka_gui(); g_husarz.przejdz_do_url("http://example.test/render.html");
                CHECK(zakladki[1].dokument->image_count==4); CHECK(resource_cache->images_decoded==2);
                g_husarz.zamknij_zakladke_gui(1);
                CHECK(zakladki[0].dokument->image_count==4);
            }
        }
        {
            std::ifstream in("narzedzia/fixtures/husarz/forms-native.html");
            response=std::string((std::istreambuf_iterator<char>(in)),{});
            g_husarz.odswiez(); auto &d=*zakladki[0].dokument;
            auto id=[&](const char* name){return Husarz::ScriptDom(d).get_element_by_id(name).id;};
            w.ustaw_fokus(viewport); d.focused_control=id("q");
            auto physical=[&](unsigned usage,unsigned mods=0){bws_zdarzenie e{};e.typ=BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;e.kod=BWS_KEY_SOURCE_USB|usage;e.x=1;e.przyciski=mods;w.obsluz_zdarzenie(e);};
            physical(43); CHECK(w.fokus()==viewport); CHECK(d.focused_control==id("p"));
            bws_zdarzenie text{};text.typ=BWS_ZDARZENIE_TEXT_INPUT;text.kod='p';w.obsluz_zdarzenie(text);
            CHECK(Husarz::equal(Husarz::control(d,id("p"))->value,"p"));
            d.focused_control=id("note"); physical(230,BWS_MOD_ALT); CHECK(w.fokus()==viewport);
            text.kod=0x142;text.przyciski=BWS_MOD_ALT;w.obsluz_zdarzenie(text);
            CHECK(Husarz::equal(Husarz::control(d,id("note"))->value,"ł"));
            // Physical USB shortcuts use the same command decoder as release.
            d.focused_control=id("q");physical(4,BWS_MOD_CTRL);
            const uint32_t polish[]={0x105,0x107,0x119,0x142,0x144,0xf3,0x15b,0x17a,0x17c};
            for(auto cp:polish){text.kod=cp;text.przyciski=0;w.obsluz_zdarzenie(text);}
            auto *field=Husarz::control(d,id("q"));CHECK(Husarz::equal(field->value,"ąćęłńóśźż"));
            physical(4,BWS_MOD_CTRL);physical(6,BWS_MOD_CTRL);physical(27,BWS_MOD_CTRL);CHECK(!field->length);
            physical(25,BWS_MOD_CTRL);CHECK(Husarz::equal(field->value,"ąćęłńóśźż"));
            d.focused_control=id("p");physical(4,BWS_MOD_CTRL);physical(6,BWS_MOD_CTRL);physical(27,BWS_MOD_CTRL);
            CHECK(Husarz::equal(Husarz::control(d,id("p"))->value,"p"));
            d.focused_control=id("q");physical(4,BWS_MOD_CTRL);physical(25,BWS_MOD_CTRL);
            CHECK(Husarz::equal(field->value,"ąćęłńóśźż"));
            auto area_edit=viewport->obszar_ekranu();auto field_box=d.boxes[field->node];
            ZdarzenieMyszki down;down.przycisk=1;down.x=area_edit.x+field_box.content_x;down.y=area_edit.y+field_box.content_y+1;
            viewport->obsluz_wcisniecie_myszy(down);CHECK(field->cursor==0 && field->anchor==0);
            auto drag=down;drag.x+=30;viewport->obsluz_ruch_myszy(drag);CHECK(field->cursor>0 && field->anchor==0);
            viewport->obsluz_zwolnienie_myszy(drag);CHECK(drag.obsluzone);
            auto pos=field->cursor;viewport->obsluz_ruch_myszy(down);CHECK(field->cursor==pos);
            d.focused_control=d.open_select=id("select"); physical(81); CHECK(Husarz::control(d,id("select"))->option==1);
            physical(40); CHECK(d.open_select==0);
            // Open through the actual page mouse handler, then select by keyboard.
            d.focused_control=0;
            Husarz::choose_option(d,*Husarz::control(d,id("select")),0);
            viewport->przygotuj();
            auto area=viewport->obszar_ekranu(); auto box=d.boxes[id("select")];
            ZdarzenieMyszki click; click.przycisk=1; click.x=area.x+box.x+10;
            click.y=area.y+box.y+10-int(g_husarz.suwak_pionowy->wartosc());
            viewport->obsluz_wcisniecie_myszy(click);
            CHECK(click.obsluzone); CHECK(d.focused_control==id("select")); CHECK(d.open_select==id("select"));
            physical(81); CHECK(Husarz::control(d,id("select"))->option==1);
            physical(81); CHECK(Husarz::control(d,id("select"))->option==1);
            physical(40); CHECK(d.open_select==0);
        }
        {
            std::ifstream in("narzedzia/fixtures/husarz/js-forms.html");response=std::string((std::istreambuf_iterator<char>(in)),{});
            g_husarz.odswiez();auto &z=zakladki[0];ensure_tab_identity(z);auto &d=*z.dokument;z.javascript=new Husarz::Js::BrowserRuntime;z.javascript->bind(d,z.identity,z.navigation,z.url);
            auto pump=[&](){for(unsigned i=0;i<600;++i){z.javascript->publication_pending=false;z.work_pending=false;browser_script_step(uint64_t(i+1)*1000000);viewport->przygotuj();}};
            pump();CHECK(z.javascript->errors==0);
            auto id=[&](const char *name){return Husarz::ScriptDom(d).get_element_by_id(name).id;};
            auto area=viewport->obszar_ekranu();auto box=d.boxes[id("field")];ZdarzenieMyszki click;click.przycisk=1;click.x=area.x+box.x+12;click.y=area.y+box.y+12;
            viewport->obsluz_wcisniecie_myszy(click);pump();w.ustaw_fokus(viewport);
            const char *observe="var edits=0;document.getElementById('field').addEventListener('input',function(e){edits++;e.target.setAttribute('data-edits',''+edits);});";
            CHECK(z.javascript->start(observe,Husarz::length(observe,512)));pump();
            bws_zdarzenie backspace{};backspace.typ=BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;backspace.kod=BWS_KEY_SOURCE_USB|42;backspace.x=1;w.obsluz_zdarzenie(backspace);pump();
            CHECK(!d.has_attr(id("field"),"data-edits"));
            bws_zdarzenie physical{};physical.typ=BWS_ZDARZENIE_KLAWISZ_FIZYCZNY;physical.kod=BWS_KEY_SOURCE_USB|29;physical.x=1;w.obsluz_zdarzenie(physical);
            bws_zdarzenie text{};text.typ=BWS_ZDARZENIE_TEXT_INPUT;text.kod='z';w.obsluz_zdarzenie(text);pump();CHECK(!Husarz::control(d,id("field"))->length);
            physical.kod=BWS_KEY_SOURCE_USB|4;w.obsluz_zdarzenie(physical);text.kod='a';w.obsluz_zdarzenie(text);pump();CHECK(Husarz::equal(Husarz::control(d,id("field"))->value,"a"));
            CHECK(Husarz::equal(d.attr(id("field"),"data-edits"),"1"));
            box=d.boxes[id("submit")];click.x=area.x+box.x+12;click.y=area.y+box.y+12;viewport->obsluz_wcisniecie_myszy(click);CHECK(z.dokument==&d);pump();CHECK(z.dokument==&d);
            if(z.javascript->errors)std::fprintf(stderr,"JS native errors=%u last=%u line=%u col=%u\n",z.javascript->errors,unsigned(z.javascript->last_error),z.javascript->last_error_line,z.javascript->last_error_column);
            CHECK(z.javascript->errors==0);CHECK(Husarz::control(d,id("check"))->checked);CHECK(Husarz::control(d,id("choice"))->option==1);
            CHECK(tekst_zawiera_ci(d.attr(id("marker"),"style"),"#3366cc"));
            d.invalidate(0,8);z.work_pending=true;g_husarz.zatrzymaj();
            CHECK(!z.javascript);CHECK(z.work_pending);
            for(unsigned i=0;i<20000&&z.work_pending;++i)browser_step_work();
            CHECK(!z.work_pending);CHECK(!z.dokument->styles_dirty);CHECK(z.dokument->layout_revision==z.dokument->revision);CHECK(!z.javascript);
        }
        // Exercise the actual completion publisher with stale HTML, CSS and image
        // replies. An old navigation or a different document cannot alter this tab.
        browser_resources=new BrowserResources;
        browser_resources->payload=new Husarz::WorkerPayload;
        auto &state=*browser_resources;
        Zakladka blank_a{},blank_b{};ensure_tab_identity(blank_a);ensure_tab_identity(blank_b);CHECK(blank_a.identity && blank_b.identity && blank_a.identity!=blank_b.identity);
        auto &tab=zakladki[0]; tab.identity=77; tab.navigation=42;
        auto *retained=tab.dokument; unsigned revision=retained->revision;
        for (auto kind : {Husarz::RequestKind::Html,Husarz::RequestKind::Css,Husarz::RequestKind::Image}) {
            state.active={}; state.active.tab=77; state.active.navigation=41;
            state.active.document=retained->generation; state.active.kind=kind; state.active.request=9;
            state.payload->magic=Husarz::AsyncMagic; state.payload->request=9; state.payload->kind=uint32_t(kind);
            state.payload->success=1; state.payload->size=10;
            apply_worker_result(); CHECK(tab.dokument==retained); CHECK(retained->revision==revision);
        }
        state.active.navigation=42; state.active.kind=Husarz::RequestKind::Css; state.active.document=retained->generation+1;
        apply_worker_result(); CHECK(retained->revision==revision);
        cancel_async_tab(tab); state.active.document=retained->generation;
        apply_worker_result(); CHECK(retained->revision==revision);
        CHECK(state.queue.discarded==5);
        // Matching CSS is pinned for the next cooperative document transaction.
        state.active.navigation=tab.navigation; state.active.kind=Husarz::RequestKind::Css;
        kopiuj_limit(state.active.url,sizeof(state.active.url),"http://example.test/race.css");
        state.payload->kind=uint32_t(Husarz::RequestKind::Css);
        const char* css="body {color:#abcdef}";
        state.payload->size=uint32_t(std::strlen(css)); std::memcpy(state.payload->raw,css,state.payload->size);
        apply_worker_result(); CHECK(tab.dokument==retained); CHECK(retained->revision==revision);
        CHECK(tab.pending_css && tab.pending_css->count==1 && tab.work_pending);
        auto document_generation=retained->generation, parse_count=retained->parse_count;
        browser_viewport.request(762,410);
        unsigned cooperative_steps=0;
        while(browser_has_work()){browser_step_work();CHECK(++cooperative_steps<10000);}
        CHECK(cooperative_steps>0 && !tab.pending_css && !tab.work_pending);
        CHECK(tab.dokument->generation==document_generation && tab.dokument->parse_count==parse_count);
        CHECK(tab.dokument->revision>revision);
        CHECK(!tab.dokument->styles_dirty);
        cancel_async_tab(tab); CHECK(!tab.pending_css && !tab.work_pending);
        state.active.navigation=tab.navigation;state.active.document=tab.dokument->generation;state.active.kind=Husarz::RequestKind::Image;
        kopiuj_limit(state.active.url,sizeof(state.active.url),"http://example.test/cancel-image.png");
        state.payload->kind=uint32_t(Husarz::RequestKind::Image);state.payload->success=1;state.payload->size=32;state.payload->width=512;state.payload->height=512;
        auto raw_before=resource_cache->raw_bytes,decoded_before=resource_cache->decoded_bytes;
        Husarz::WorkDriver publication_driver;publication_driver.clock=[]()->uint64_t{static uint64_t now=1;return now+=3000000;};
        auto publication=apply_worker_result_work();CHECK(!publication.step(publication_driver));publication.cancel();
        CHECK(resource_cache->raw_bytes==raw_before && resource_cache->decoded_bytes==decoded_before);
        delete state.payload; state.payload=nullptr; delete browser_resources; browser_resources=nullptr;
        auto *theme_doc = zakladki[0].dokument;
        auto before_theme = theme_doc->parse_count;
        BursztynMotyw bright = g_domyslny_motyw_fallback;
        bright.poletekstowe = 0xffffff;
        bright.tekst = 0x101010;
        bright.panel = 0xe4e4e4;
        test_ustaw_aktywny_motyw(&bright);
        w.zaktualizuj_motyw();
        odswiez_pasek_stanu_natychmiast();
        CHECK(zakladki[0].dokument == theme_doc);
        CHECK(theme_doc->parse_count == before_theme);
        CHECK(theme_doc->styles[0].color == theme_doc->user_agent.text);
        test_ustaw_aktywny_motyw(nullptr);
        w.zaktualizuj_motyw();
        odswiez_pasek_stanu_natychmiast();
        for (int h : {200, 350, 600})
            for (int width : {500, 800, 1100}) {
                w.ustaw_rozmiar(width, h);
                ustaw_status("Loading transition");
                odswiez_pasek_stanu_natychmiast();
                validate_publication();
            }
        // Huge dimensions are bounded before entering scrollbar arithmetic.
        response = "<div style='height:999999999999999999999px'>Large</div>";
        g_husarz.odswiez();
        CHECK(bar.maksimum() <= Husarz::MaxDimension);
        wheel(INT_MIN);
        CHECK(bar.wartosc() == bar.maksimum());
        g_husarz.nowa_zakladka_gui();
        CHECK(liczba_zakladek == 2);
        g_husarz.przejdz_do_url("http://example.test/tab");
        g_husarz.przelacz_zakladke(0);
        g_husarz.zamknij_zakladke_gui(1);
        CHECK(liczba_zakladek == 1);
        g_husarz.nowa_zakladka_gui();
        CHECK(zakladki[1].identity==0 && zakladki[1].navigation==0);
        g_husarz.zamknij_zakladke_gui(1);
        {
            Husarz::ResourceCache isolated(gui_malloc,gui_free);
            for (const char *fixture : {"sample-alpha.png","oversized.png","malformed.jpg"}) {
                std::ifstream in(std::string("narzedzia/fixtures/husarz/")+fixture,std::ios::binary);
                std::string raw((std::istreambuf_iterator<char>(in)),{});
                auto *entry=isolated.insert(fixture,Husarz::ResourceType::Image,reinterpret_cast<const uint8_t*>(raw.data()),raw.size());
                CHECK(entry);
                bool success=decode_browser_image(isolated,*entry);
                if(std::strcmp(fixture,"sample-alpha.png")==0) {
                    CHECK(success); CHECK(entry->pixels[0]==0x801e64c8); CHECK(decode_workspace==0);
                } else CHECK(!success);
                CHECK(isolated.decoded_bytes<=Husarz::MaxCacheDecoded); CHECK(decode_workspace==0);
            }
        }
        {
            Husarz::ResourceCache fuzz_cache(gui_malloc,gui_free); uint32_t random=0xb83c0129;
            std::ifstream in("narzedzia/fixtures/husarz/sample.png",std::ios::binary);
            std::string prefix((std::istreambuf_iterator<char>(in)),{});
            for(unsigned sample=0;sample<384;++sample) {
                uint8_t data[128]{};
                for(unsigned j=0;j<128;++j){random=random*1664525+1013904223;data[j]=uint8_t(random>>24);}
                if(sample%2) for(unsigned j=0;j<33 && j<prefix.size();++j)data[j]=uint8_t(prefix[j]);
                else {data[0]=0xff;data[1]=0xd8;}
                if(sample%3==0){data[16]=0x7f;data[17]=data[18]=data[19]=0xff;}
                auto *entry=fuzz_cache.insert("http://fuzz.test/image",Husarz::ResourceType::Image,data,sizeof(data));CHECK(entry);
                (void)decode_browser_image(fuzz_cache,*entry);
                CHECK(decode_workspace==0);CHECK(fuzz_cache.raw_bytes<=Husarz::MaxCacheRaw);CHECK(fuzz_cache.decoded_bytes<=Husarz::MaxCacheDecoded);
            }
        }
        test_publish_observer = nullptr;
        test_capture_paints = false;
        zwolnij_wszystkie_zakladki();
        delete resource_cache; resource_cache=nullptr;
    }
    std::printf(
        "Husarz native runtime: %u checks PASS, %u loading publications, %u real HTTP parser requests\n",
        checks, publications, requests);
}
