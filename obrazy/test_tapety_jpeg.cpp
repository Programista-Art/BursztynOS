#include "tapety_format.h"
#include <cassert>

int main() {
    assert(tapety_sciezka_jpeg("/uzytkownicy/Pulpit/tapeta.jpg"));
    assert(tapety_sciezka_jpeg("/uzytkownicy/Zdjecia/photo.JPEG"));
    assert(!tapety_sciezka_jpeg("/uzytkownicy/../system/x.jpg"));
    assert(!tapety_sciezka_jpeg("/uzytkownicy//x.jpg"));
    assert(!tapety_sciezka_jpeg("/uzytkownicy/x.png"));
    assert(tapety_sciezka(TAPETY_SCIEZKI[0]));
    assert(tapety_sciezka_powierzchni("/uzytkownicy/Pulpit/obraz.btp"));
    assert(!tapety_sciezka_powierzchni("/uzytkownicy/../system/obraz.btp"));
    assert(!tapety_sciezka_powierzchni(TAPETY_SCIEZKI[0]));
    TapetyKonfiguracja original{};
    assert(tapety_kopiuj(original.sciezka, "/uzytkownicy/Pulpit/tapeta.jpg", sizeof(original.sciezka)));
    original.tryb = TrybSkalowaniaObrazu::Rozciagnij;
    char buffer[TAPETY_CFG_MAX]{};
    const size_t bytes = tapety_serializuj(original, buffer, sizeof(buffer));
    TapetyKonfiguracja decoded{};
    assert(bytes && tapety_parsuj(buffer, bytes, &decoded));
    assert(tapety_rowne(original.sciezka, decoded.sciezka));
    assert(decoded.tryb == TrybSkalowaniaObrazu::Rozciagnij);
    const uint32_t source[4] = {0xff000001, 0xff000002, 0xff000003, 0xff000004};
    uint32_t dest[24]{};
    assert(skaluj_obraz(source, 2, 2, sizeof(source), dest, 6, 4, 6,
                        sizeof(dest), TrybSkalowaniaObrazu::Rozciagnij, 0));
    assert(dest[0] == source[0] && dest[5] == source[1] &&
           dest[18] == source[2] && dest[23] == source[3]);
    // --- 1. Walidacja polskich nazw UTF-8, spacji, myslnikow i nawiasow ---
    const char* poprawne_pliki[] = {
        "Żółta łąka.jpg",
        "BursztynowaŁąka.jpg",
        "morze-zachód.jpeg",
        "Tapeta Bursztyn OS.jpg",
        "góry2026-zima.jpg",
        "łąka zachód słońca.jpg",
        "Tapeta (nowa) 2026.jpg",
        "a.jpg",
        "bursztyn01.btp",
        "BURSZTYN10.BTP"
    };

    for (const char* nazwa : poprawne_pliki) {
        assert(tapeta_sprawdz_nazwe_utf8(nazwa));
        char canon[64]{};
        assert(tapeta_kanoniczna_nazwa(nazwa, canon, sizeof(canon)));
        assert(tapeta_czy_kanoniczna_nazwa(canon));
        char path[128]{};
        assert(tapeta_kanoniczna_sciezka(nazwa, path, sizeof(path)));
        assert(tapeta_czy_kanoniczna_sciezka(path));
        // Sprawdz scisly alfabet [a-z0-9_.]
        for (size_t k = 0; canon[k]; ++k) {
            char c = canon[k];
            assert((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.');
        }
    }

    // --- 2. Odrzucanie nieprawidlowych i niebezpiecznych nazw ---
    const char* bledne_pliki[] = {
        "",
        ".",
        "..",
        ".ukryty.jpg",
        "../traversal.jpg",
        "katalog/plik.jpg",
        "katalog\\plik.jpg",
        "plik\r\n.jpg",
        "\"cudzyslow\".jpg",
        "brak_rozszerzenia",
        "zly_format.gif",
        "zly_format.bmp",
        ".jpg",
        "a.j",
        "\xc0\xaf_overlong.jpg",
        "\xed\xa0\x80_surrogate.jpg"
    };
    for (const char* zla_nazwa : bledne_pliki) {
        assert(!tapeta_sprawdz_nazwe_utf8(zla_nazwa));
        char canon[64]{};
        assert(!tapeta_kanoniczna_nazwa(zla_nazwa, canon, sizeof(canon)));
    }

    // --- 3. Zachowanie nazw wbudowanych tapet systemowych ---
    char b01[64]{};
    assert(tapeta_kanoniczna_nazwa("bursztyn01.btp", b01, sizeof(b01)));
    assert(tapety_rowne(b01, "bursztyn01.btp"));
    char b10[64]{};
    assert(tapeta_kanoniczna_nazwa("BURSZTYN10.BTP", b10, sizeof(b10)));
    assert(tapety_rowne(b10, "bursztyn10.btp"));

    // --- 4. Determinizm i stabilnosc FNV-1a ---
    char c1[64]{}, c2[64]{};
    assert(tapeta_kanoniczna_nazwa("Żółta łąka.jpg", c1, sizeof(c1)));
    assert(tapeta_kanoniczna_nazwa("Żółta łąka.jpg", c2, sizeof(c2)));
    assert(tapety_rowne(c1, c2));
    assert(tapety_rowne(c1, "wp_f0eeee7f3f3099f8.jpg"));

    // --- 5. Serializacja i deserializacja konfiguracji (z CRLF i nazwa=) ---
    TapetyKonfiguracja wp_cfg{};
    assert(tapety_kopiuj(wp_cfg.sciezka, "/tapety/wp_f0eeee7f3f3099f8.jpg", sizeof(wp_cfg.sciezka)));
    wp_cfg.tryb = TrybSkalowaniaObrazu::Wypelnij;
    assert(tapety_kopiuj(wp_cfg.wyswietlana, "Żółta łąka.jpg", sizeof(wp_cfg.wyswietlana)));

    char wp_buf[TAPETY_CFG_MAX]{};
    const size_t wp_bytes = tapety_serializuj(wp_cfg, wp_buf, sizeof(wp_buf));
    assert(wp_bytes > 0);

    TapetyKonfiguracja wp_parsed{};
    assert(tapety_parsuj(wp_buf, wp_bytes, &wp_parsed));
    assert(tapety_rowne(wp_parsed.sciezka, "/tapety/wp_f0eeee7f3f3099f8.jpg"));
    assert(wp_parsed.tryb == TrybSkalowaniaObrazu::Wypelnij);
    assert(tapety_rowne(wp_parsed.wyswietlana, "Żółta łąka.jpg"));

    // Deserializacja z Windows CRLF (\r\n)
    const char crlf_cfg[] = "tapeta=/tapety/wp_f0eeee7f3f3099f8.jpg\r\ntryb=dopasuj\r\nnazwa=Żółta łąka.jpg\r\n";
    TapetyKonfiguracja crlf_parsed{};
    assert(tapety_parsuj(crlf_cfg, sizeof(crlf_cfg) - 1, &crlf_parsed));
    assert(tapety_rowne(crlf_parsed.sciezka, "/tapety/wp_f0eeee7f3f3099f8.jpg"));
    assert(crlf_parsed.tryb == TrybSkalowaniaObrazu::Dopasuj);
    assert(tapety_rowne(crlf_parsed.wyswietlana, "Żółta łąka.jpg"));

    // Fallback przy uszkodzonej konfiguracji
    TapetyKonfiguracja bad_cfg{};
    const char corrupted_cfg[] = "tapeta=niepoprawna_sciezka\ntryb=nieznany\n";
    assert(!tapety_parsuj(corrupted_cfg, sizeof(corrupted_cfg) - 1, &bad_cfg));
    assert(tapety_rowne(bad_cfg.sciezka, TAPETY_SCIEZKI[0]));
    return 0;
}
