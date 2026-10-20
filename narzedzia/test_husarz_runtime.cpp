#include "bursztyn_ui_host_fixture.h"
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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
    std::string all = "HTTP/1.1 200 OK\r\nContent-Type: " + type +
                      "\r\nContent-Length: " + std::to_string(payload.size()) + "\r\n\r\n" + payload;
    if (all.size() + 1 > cap)
        return false;
    std::memcpy(data, all.c_str(), all.size() + 1);
    return true;
}
bool bws_siec_pobierz_https(uint8_t *ip, const char *domain, const char *path, char *data, uint32_t cap) {
    return bws_siec_pobierz_http(ip, domain, path, data, cap);
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
        CHECK(requests == before_key + 2);
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
        CHECK(theme_doc->styles[0].color == bright.tekst);
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
        test_publish_observer = nullptr;
        test_capture_paints = false;
        zwolnij_wszystkie_zakladki();
    }
    std::printf(
        "Husarz native runtime: %u checks PASS, %u loading publications, %u real HTTP parser requests\n",
        checks, publications, requests);
}
