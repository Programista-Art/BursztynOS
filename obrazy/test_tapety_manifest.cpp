#include "tapety_format.h"
#include <cassert>
#include <cstdio>
#include <cstring>

// Symulacja struktur i parsera inwentarza zgodna z jadrem Ring 0
struct TestWpisInwentarza {
    char nazwa[TAPETY_SCIEZKA_MAX];
    char wyswietlana[TAPETY_SCIEZKA_MAX];
    uint32_t rozmiar;
    uint32_t crc32;
};

static size_t test_parsuj_inwentarz(const char* buf, size_t n,
                                    TestWpisInwentarza* wpisy, size_t max_wpisow,
                                    char* domyslna_out, size_t domyslna_cap) {
    if (!wpisy || !max_wpisow) return 0;
    if (domyslna_out && domyslna_cap) domyslna_out[0] = '\0';
    if (!buf || !n) return 0;

    size_t count = 0;
    size_t pos = 0;
    while (pos < n && count < max_wpisow) {
        size_t end = pos;
        while (end < n && buf[end] != '\n') ++end;

        constexpr char def_prefix[] = "default=";
        constexpr size_t def_len = sizeof(def_prefix) - 1;
        constexpr char dom_prefix[] = "domyslna=";
        constexpr size_t dom_len = sizeof(dom_prefix) - 1;
        if (domyslna_out && domyslna_cap > 0 && domyslna_out[0] == '\0') {
            size_t p_prefix = 0;
            if (end - pos > def_len) {
                bool match = true;
                for (size_t k = 0; k < def_len; ++k) {
                    if (buf[pos + k] != def_prefix[k]) { match = false; break; }
                }
                if (match) p_prefix = def_len;
            }
            if (!p_prefix && end - pos > dom_len) {
                bool match = true;
                for (size_t k = 0; k < dom_len; ++k) {
                    if (buf[pos + k] != dom_prefix[k]) { match = false; break; }
                }
                if (match) p_prefix = dom_len;
            }
            if (p_prefix > 0) {
                size_t d_start = pos + p_prefix;
                size_t d_end = end;
                while (d_end > d_start && (buf[d_end - 1] == '\r' || buf[d_end - 1] == ' ' || buf[d_end - 1] == '\t')) --d_end;
                while (d_start < d_end && (buf[d_start] == ' ' || buf[d_start] == '\t')) ++d_start;
                size_t dlen = d_end - d_start;
                if (dlen > 0 && dlen < domyslna_cap) {
                    for (size_t k = 0; k < dlen; ++k) domyslna_out[k] = buf[d_start + k];
                    domyslna_out[dlen] = '\0';
                }
            }
        }

        constexpr char prefix[] = "wpis=";
        constexpr size_t plen = sizeof(prefix) - 1;
        if (end - pos > plen) {
            bool matches_prefix = true;
            for (size_t k = 0; k < plen; ++k) {
                if (buf[pos + k] != prefix[k]) { matches_prefix = false; break; }
            }
            if (matches_prefix) {
                size_t cur = pos + plen;
                size_t p1 = cur;
                while (p1 < end && buf[p1] != '|') ++p1;
                if (p1 < end && buf[p1] == '|') {
                    size_t nlen = p1 - cur;
                    if (nlen > 0 && nlen < TAPETY_SCIEZKA_MAX) {
                        size_t p2 = p1 + 1;
                        size_t p3 = p2;
                        while (p3 < end && buf[p3] != '|') ++p3;
                        if (p3 < end && buf[p3] == '|') {
                            uint32_t rsz = 0;
                            bool rsz_ok = true;
                            for (size_t k = p2; k < p3; ++k) {
                                if (buf[k] < '0' || buf[k] > '9') { rsz_ok = false; break; }
                                rsz = rsz * 10 + (buf[k] - '0');
                            }
                            size_t p4 = p3 + 1;
                            size_t p5 = p4;
                            while (p5 < end && buf[p5] != '|') ++p5;
                            uint32_t crc = 0;
                            bool crc_ok = true;
                            for (size_t k = p4; k < p5; ++k) {
                                char ch = buf[k];
                                if (ch >= '0' && ch <= '9') crc = (crc << 4) | (ch - '0');
                                else if (ch >= 'a' && ch <= 'f') crc = (crc << 4) | (ch - 'a' + 10);
                                else if (ch >= 'A' && ch <= 'F') crc = (crc << 4) | (ch - 'A' + 10);
                                else if (ch == '\r') { /* ignoruj */ }
                                else { crc_ok = false; break; }
                            }
                            char wyswietlana[TAPETY_SCIEZKA_MAX]{};
                            size_t wlen = 0;
                            if (p5 < end && buf[p5] == '|') {
                                size_t w_start = p5 + 1;
                                size_t w_end = end;
                                if (w_end > w_start && buf[w_end - 1] == '\r') --w_end;
                                wlen = w_end - w_start;
                                if (wlen >= sizeof(wyswietlana)) wlen = sizeof(wyswietlana) - 1;
                                for (size_t k = 0; k < wlen; ++k) wyswietlana[k] = buf[w_start + k];
                                wyswietlana[wlen] = '\0';
                            }
                            if (wlen == 0) {
                                for (size_t k = 0; k < nlen; ++k) wyswietlana[k] = buf[cur + k];
                                wyswietlana[nlen] = '\0';
                                wlen = nlen;
                            }
                            if (rsz_ok && crc_ok && rsz > 0) {
                                for (size_t k = 0; k < nlen; ++k) wpisy[count].nazwa[k] = buf[cur + k];
                                wpisy[count].nazwa[nlen] = '\0';
                                for (size_t k = 0; k <= wlen; ++k) wpisy[count].wyswietlana[k] = wyswietlana[k];
                                wpisy[count].rozmiar = rsz;
                                wpisy[count].crc32 = crc;
                                ++count;
                            }
                        }
                    }
                }
            }
        }
        pos = end + 1;
    }
    if (domyslna_out && domyslna_cap > 0 && domyslna_out[0] == '\0' && count > 0) {
        tapety_kopiuj(domyslna_out, wpisy[0].nazwa, domyslna_cap);
    }
    return count;
}

int main() {
    // -------------------------------------------------------------
    // Test 1: Parsowanie manifestu z default= oraz polskimi znakami
    // -------------------------------------------------------------
    {
        const char manifest[] =
            "default=wp_f0eeee7f3f3099f8.jpg\r\n"
            "wpis=wp_f0eeee7f3f3099f8.jpg|12345|1A2B3C4D|Żółta łąka.jpg\r\n"
            "wpis=wp_b1e5fa3a6e648a13.jpeg|67890|AABBCCDD|morze-zachód.jpeg\r\n"
            "wpis=ciemna.btp|4444|DEADBEEF|Ciemna.btp\r\n";

        TestWpisInwentarza wpisy[16]{};
        char domyslna[64]{};
        size_t n = test_parsuj_inwentarz(manifest, sizeof(manifest) - 1, wpisy, 16, domyslna, sizeof(domyslna));
        assert(n == 3);
        assert(tapety_rowne(domyslna, "wp_f0eeee7f3f3099f8.jpg"));
        assert(tapety_rowne(wpisy[0].nazwa, "wp_f0eeee7f3f3099f8.jpg"));
        assert(tapety_rowne(wpisy[0].wyswietlana, "Żółta łąka.jpg"));
        assert(wpisy[0].rozmiar == 12345);
        assert(wpisy[0].crc32 == 0x1A2B3C4D);
        assert(tapety_rowne(wpisy[2].nazwa, "ciemna.btp"));
        assert(tapety_rowne(wpisy[2].wyswietlana, "Ciemna.btp"));
    }

    // -------------------------------------------------------------
    // Test 2: Manifest bez linii default= -> automatyczny pierwszy wpis
    // -------------------------------------------------------------
    {
        const char manifest[] =
            "wpis=gory.jpg|1000|12345678|Góry zima.jpg\n"
            "wpis=las.btp|2000|87654321|Las.btp\n";

        TestWpisInwentarza wpisy[16]{};
        char domyslna[64]{};
        size_t n = test_parsuj_inwentarz(manifest, sizeof(manifest) - 1, wpisy, 16, domyslna, sizeof(domyslna));
        assert(n == 2);
        assert(tapety_rowne(domyslna, "gory.jpg"));
    }

    // -------------------------------------------------------------
    // Test 3: Pusty manifest
    // -------------------------------------------------------------
    {
        TestWpisInwentarza wpisy[16]{};
        char domyslna[64]{};
        size_t n = test_parsuj_inwentarz("", 0, wpisy, 16, domyslna, sizeof(domyslna));
        assert(n == 0);
        assert(domyslna[0] == '\0');
    }

    // -------------------------------------------------------------
    // Test 4: Uszkodzony manifest (smieci, niepoprawne linie)
    // -------------------------------------------------------------
    {
        const char bad_manifest[] =
            "nieznany_klucz=wartosc\n"
            "wpis=uszkodzony_brak_pol\n"
            "wpis=za_malo|100\n"
            "wpis=poprawny.btp|500|FFFFFFFF|Poprawna.btp\n"
            "\0\xff\xfe\n"
            "wpis=zly_rozmiar|abc|1234|Zly\n";

        TestWpisInwentarza wpisy[16]{};
        char domyslna[64]{};
        size_t n = test_parsuj_inwentarz(bad_manifest, sizeof(bad_manifest) - 1, wpisy, 16, domyslna, sizeof(domyslna));
        assert(n == 1);
        assert(tapety_rowne(wpisy[0].nazwa, "poprawny.btp"));
        assert(tapety_rowne(domyslna, "poprawny.btp"));
    }

    // -------------------------------------------------------------
    // Test 5: Czterostopniowy tapety_fallback - scenariusz 1: Zapisana poprawna JPEG
    // -------------------------------------------------------------
    {
        bool solid_wywolany = false;
        TapetyZrodlo z = tapety_fallback(
            [&]() { return TAPETY_JPEG; },  // Zapisana poprawna JPEG
            [&]() { return TAPETY_BTP; },   // Domyslna manifestu
            [&]() { return TAPETY_BTP; },   // Pierwsza z manifestu
            [&]() { solid_wywolany = true; }
        );
        assert(z == TAPETY_JPEG);
        assert(!solid_wywolany);
    }

    // -------------------------------------------------------------
    // Test 6: Czterostopniowy tapety_fallback - scenariusz 2: Zapisana poprawna BTP
    // -------------------------------------------------------------
    {
        bool solid_wywolany = false;
        TapetyZrodlo z = tapety_fallback(
            [&]() { return TAPETY_BTP; },   // Zapisana poprawna BTP
            [&]() { return TAPETY_JPEG; },  // Domyslna manifestu
            [&]() { return TAPETY_JPEG; },  // Pierwsza z manifestu
            [&]() { solid_wywolany = true; }
        );
        assert(z == TAPETY_BTP);
        assert(!solid_wywolany);
    }

    // -------------------------------------------------------------
    // Test 7: Czterostopniowy tapety_fallback - scenariusz 3: Zapisana brakujaca -> domyslna z manifestu
    // -------------------------------------------------------------
    {
        bool solid_wywolany = false;
        TapetyZrodlo z = tapety_fallback(
            [&]() { return TAPETY_SOLID; }, // Zapisana brakujaca / invalid
            [&]() { return TAPETY_JPEG; },  // Domyslna manifestu
            [&]() { return TAPETY_BTP; },   // Pierwsza z manifestu
            [&]() { solid_wywolany = true; }
        );
        assert(z == TAPETY_JPEG);
        assert(!solid_wywolany);
    }

    // -------------------------------------------------------------
    // Test 8: Czterostopniowy tapety_fallback - scenariusz 4: Domyslna brakujaca -> pierwsza poprawna z manifestu
    // -------------------------------------------------------------
    {
        bool solid_wywolany = false;
        TapetyZrodlo z = tapety_fallback(
            [&]() { return TAPETY_SOLID; }, // Zapisana brak
            [&]() { return TAPETY_SOLID; }, // Domyslna manifestu brak/uszkodzona
            [&]() { return TAPETY_BTP; },   // Pierwsza z manifestu poprawna
            [&]() { solid_wywolany = true; }
        );
        assert(z == TAPETY_BTP);
        assert(!solid_wywolany);
    }

    // -------------------------------------------------------------
    // Test 9: Czterostopniowy tapety_fallback - scenariusz 5: Brak jakiejkolwiek tapety -> solid
    // -------------------------------------------------------------
    {
        bool solid_wywolany = false;
        TapetyZrodlo z = tapety_fallback(
            [&]() { return TAPETY_SOLID; }, // Zapisana brak
            [&]() { return TAPETY_SOLID; }, // Domyslna manifestu brak
            [&]() { return TAPETY_SOLID; }, // Pierwsza z manifestu brak
            [&]() { solid_wywolany = true; }
        );
        assert(z == TAPETY_SOLID);
        assert(solid_wywolany);
    }

    std::printf("[TEST MANIFEST] Wszystkie testy regresji C++ zaliczone pomyslnie (PASS).\n");
    return 0;
}
