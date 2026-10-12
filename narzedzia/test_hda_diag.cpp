/*
 * Bursztyn OS - testy hostowe czystego formatu i topologii HDA
 *
 * Kompilacja:
 *   g++ -std=c++17 -O1 -g -fsanitize=address,undefined \
 *       narzedzia/test_hda_diag.cpp -o .hda-test/test-hda-diag
 *
 * Zakres: parsowanie AWCAP, Pin Default Config, listy polaczen,
 * graf topologii (cykle, sciezki), raport, obciazenia, limity.
 * Bez MMIO, bez BWS, bez sprzetu.
 */

#include "../sterowniki/dzwiek/hda_diag_format.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

static int testy = 0;
static int bledy = 0;

#define CHECK(cond) do { \
    ++testy; \
    if (!(cond)) { \
        ++bledy; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static void test_awcap_typy() {
    CHECK(hda_wcap_typ(0x00000001) == 0); /* Audio Output + Stereo */
    CHECK(hda_wcap_typ(0x00100001) == 1);
    CHECK(hda_wcap_typ(0x00200000) == 2);
    CHECK(hda_wcap_typ(0x00300000) == 3);
    CHECK(hda_wcap_typ(0x0040018F) == 4); /* Pin Complex */
    CHECK(hda_wcap_typ(0x00500000) == 5);
    CHECK(hda_wcap_typ(0x00600000) == 6);
    CHECK(hda_wcap_typ(0x00700000) == 7);
    CHECK(hda_wcap_typ(0x00F00000) == 0xF);

    CHECK(std::strcmp(hda_nazwa_typu_widgetu(0), "Audio Output") == 0);
    CHECK(std::strcmp(hda_nazwa_typu_widgetu(4), "Pin Complex") == 0);
    CHECK(std::strcmp(hda_nazwa_typu_widgetu(0xF), "Vendor Defined") == 0);
    CHECK(std::strcmp(hda_nazwa_typu_widgetu(8), "Reserved") == 0);

    CHECK(hda_typ_widgetu_jest_audio(0));
    CHECK(hda_typ_widgetu_jest_audio(2));
    CHECK(!hda_typ_widgetu_jest_audio(4));
    CHECK(hda_typ_widgetu_posrednik(2));
    CHECK(hda_typ_widgetu_posrednik(3));
    CHECK(!hda_typ_widgetu_posrednik(0));
    CHECK(hda_typ_widgetu_pin(4));

    /* Bezpieczenstwo verbów wg typu. */
    CHECK(hda_typ_wolno_pin_caps(4));
    CHECK(!hda_typ_wolno_pin_caps(0));
    CHECK(hda_typ_wolno_pcm_caps(0));
    CHECK(hda_typ_wolno_pcm_caps(1));
    CHECK(!hda_typ_wolno_pcm_caps(4));
    CHECK(hda_typ_wolno_conn_list(0x00400100));
    CHECK(!hda_typ_wolno_conn_list(0x00000001));
}

static void test_awcap_flagi() {
    const uint32_t w = 0x0040018F;
    CHECK((w & HDA_WCAP_STEREO) != 0);
    CHECK((w & HDA_WCAP_IN_AMP) != 0);
    CHECK((w & HDA_WCAP_OUT_AMP) != 0);
    CHECK(hda_typ_wolno_amp_in(w));
    CHECK(hda_typ_wolno_amp_out(w));
    CHECK(!hda_typ_wolno_amp_in(0x00000001));
}

static void test_pincfg() {
    /* Speaker / Internal / Fixed Function. */
    const uint32_t speaker =
        (2u << 30) | (0x30u << 24) | (0x1u << 20) | (0x3u << 16);
    CHECK(hda_pincfg_port(speaker) == 2);
    CHECK(hda_lokacja_wewnetrzna(hda_pincfg_lokacja(speaker)));
    CHECK(hda_pincfg_dev(speaker) == 1);
    CHECK(hda_klasyfikuj_pin(speaker) == HDA_CEL_GLOSNIK);
    CHECK(hda_cel_jest_glosnikiem(hda_klasyfikuj_pin(speaker)));
    CHECK(std::strcmp(hda_nazwa_domyslnego_urzadzenia(1), "Speaker") == 0);
    CHECK(std::strcmp(hda_nazwa_lokacji(0x30), "Internal") == 0);
    CHECK(std::strcmp(hda_nazwa_portu(2), "Fixed Function") == 0);

    /* Headphone / External / Jack. */
    const uint32_t hp = (0u << 30) | (0u << 24) | (0x2u << 20) | (0x1u << 16);
    CHECK(hda_klasyfikuj_pin(hp) == HDA_CEL_SLUCHAWKI);
    CHECK(!hda_lokacja_wewnetrzna(hda_pincfg_lokacja(hp)));
    CHECK(std::strcmp(hda_nazwa_domyslnego_urzadzenia(2), "Headphone Out") == 0);
    CHECK(std::strcmp(hda_nazwa_lokacji(0), "External") == 0);

    /* Line Out. */
    const uint32_t line = (0u << 30) | (0u << 24) | (0x0u << 20);
    CHECK(hda_klasyfikuj_pin(line) == HDA_CEL_WYJSCIE_LINIOWE);

    /* Mic In. */
    const uint32_t mic = (0u << 30) | (0u << 24) | (0xAu << 20);
    CHECK(hda_klasyfikuj_pin(mic) == HDA_CEL_MIC_IN);
    CHECK(std::strcmp(hda_nazwa_domyslnego_urzadzenia(0xA), "Mic In") == 0);

    /* Disconnected: port = No Physical Connection. */
    const uint32_t disc = (1u << 30);
    CHECK(hda_klasyfikuj_pin(disc) == HDA_CEL_NIEPODLACZONY);
    CHECK(hda_cel_jest_niepodlaczony(hda_klasyfikuj_pin(disc)));

    /* HDMI / Digital Other Out. */
    const uint32_t hdmi = (0u << 30) | (0u << 24) | (0x5u << 20);
    CHECK(hda_klasyfikuj_pin(hdmi) == HDA_CEL_HDMI);
    CHECK(hda_cel_jest_hdmi(hda_klasyfikuj_pin(hdmi)));

    /* Pola bitowe. */
    const uint32_t c = 0x02123456;
    CHECK(hda_pincfg_sekwencja(c) == 6);
    CHECK(hda_pincfg_asocjacja(c) == 5);
    CHECK(hda_pincfg_misc(c) == 4);
    CHECK(hda_pincfg_kolor(c) == 3);
}

static void test_parametry() {
    CHECK(hda_vendor_id(0x10EC0235) == 0x10EC);
    CHECK(hda_device_id(0x10EC0235) == 0x0235);
    CHECK(hda_sub_start(0x00020004) == 0x02);
    CHECK(hda_sub_count(0x00020004) == 0x04);
    CHECK(hda_zakres_wezlow_poprawny(0x01, 3));
    CHECK(!hda_zakres_wezlow_poprawny(0x01, 0));
    CHECK(!hda_zakres_wezlow_poprawny(0x7F, 2)); /* overflow */
    CHECK(hda_zakres_wezlow_poprawny(0x7E, 2));
    CHECK(!hda_zakres_wezlow_poprawny(0x80, 1));
    CHECK(hda_fg_jest_audio(1));
    CHECK(!hda_fg_jest_audio(2));
}

static void test_amp_pcm() {
    const uint32_t amp = (1u << 31) | (0x7F) | (0x20u << 8) | (2u << 16);
    CHECK(hda_amp_mute(amp));
    CHECK(hda_amp_offset(amp) == 0x7F);
    CHECK(hda_amp_kroki(amp) == 0x20);
    CHECK(hda_amp_rozmiar_kroku_25(amp) == 2);

    const uint32_t pcm = HDA_PCM_16BIT | HDA_PCM_24BIT | HDA_RATE_48K | HDA_RATE_44K;
    CHECK((pcm & HDA_PCM_16BIT) != 0);
    CHECK((pcm & HDA_RATE_48K) != 0);
    CHECK((pcm & HDA_PCM_8BIT) == 0);
}

static void test_connlist_short() {
    const uint32_t param = 2; /* 2 wpisy, short form */
    CHECK(hda_connlist_liczba(param) == 2);
    CHECK(!hda_connlist_long_form(param));

    const uint32_t slowa[] = { 0x00000302 }; /* NID 2, 3 */
    uint8_t out[8] = {0};
    uint32_t n = 0;
    bool trunc = false;
    CHECK(hda_rozszerz_liste_polaczen(slowa, 1, false, 2, out, 8, &n, &trunc));
    CHECK(n == 2);
    CHECK(out[0] == 2 && out[1] == 3);
    CHECK(!trunc);
}

static void test_connlist_long() {
    const uint32_t param = 4 | HDA_CONNLIST_LONG;
    CHECK(hda_connlist_long_form(param));

    /* 2 slowa * 2 wpisy 16-bit: NID 4, 5, 6, 7 */
    const uint32_t slowa[] = { 0x00050004, 0x00070006 };
    uint8_t out[8] = {0};
    uint32_t n = 0;
    bool trunc = false;
    CHECK(hda_rozszerz_liste_polaczen(slowa, 2, true, 4, out, 8, &n, &trunc));
    CHECK(n == 4);
    CHECK(out[0] == 4 && out[3] == 7);
}

static void test_connlist_range() {
    /* Long form: bit 15 = range, bits 14:8 = end, bits 7:0 = start. */
    const uint16_t wpis = 0x8000 | (0x05u << 8) | 0x02; /* range 2..5 */
    /* Long form: dwa 16-bitowe wpisy w jednym slowie 32-bit. */
    const uint32_t slowa[] = { static_cast<uint32_t>(wpis) | (0x0008u << 16) };
    uint8_t out[12] = {0};
    uint32_t n = 0;
    bool trunc = false;
    CHECK(hda_rozszerz_liste_polaczen(slowa, 1, true, 2, out, 12, &n, &trunc));
    CHECK(n == 5); /* 2,3,4,5 + 8 */
    CHECK(out[0] == 2 && out[3] == 5 && out[4] == 8);
}

static void test_connlist_malformed() {
    /* Range konczy sie przed startem. */
    const uint16_t zly = 0x8000 | (0x01u << 8) | 0x05; /* end 1 < start 5 */
    const uint32_t slowa[] = { zly };
    uint8_t out[8] = {0};
    uint32_t n = 0;
    bool trunc = false;
    CHECK(!hda_rozszerz_liste_polaczen(slowa, 1, true, 1, out, 8, &n, &trunc));

    /* NID > 0x7F w short form. */
    const uint32_t zly2[] = { 0x00000080 };
    CHECK(!hda_rozszerz_liste_polaczen(zly2, 1, false, 1, out, 8, &n, &trunc));

    /* Niezgodnosc liczby wpisow: slowo niesie 4 wpisy, oczekiwane 5. */
    const uint32_t zly3[] = { 0x04030201 };
    CHECK(!hda_rozszerz_liste_polaczen(zly3, 1, false, 5, out, 8, &n, &trunc));
}

static void test_connlist_truncacja() {
    /* 3 wpisy short, ale bufor na 2 NID-y z range rozbudowanym. */
    const uint16_t zakres = 0x8000 | (0x0Au << 8) | 0x01; /* 1..10 */
    const uint32_t slowa[] = { static_cast<uint32_t>(zakres) | (0x0020u << 16) };
    uint8_t out[4] = {0};
    uint32_t n = 0;
    bool trunc = false;
    CHECK(hda_rozszerz_liste_polaczen(slowa, 1, true, 2, out, 4, &n, &trunc));
    CHECK(trunc);
    CHECK(n == 4);
}

static HdaDiagWidget zrob_widget(uint8_t nid, uint8_t typ, uint32_t defcfg = 0,
                                 const uint8_t* conn = nullptr,
                                 uint8_t conn_n = 0) {
    HdaDiagWidget w{};
    w.wersja = HDA_DIAG_WERSJA_ABI;
    w.rozmiar = sizeof(HdaDiagWidget);
    w.nid = nid;
    w.typ = typ;
    w.awcap = static_cast<uint32_t>(typ) << HDA_WCAP_TYPE_SHIFT;
    if (typ == HDA_WID_PIN) {
        w.awcap |= HDA_WCAP_CONN_LIST | HDA_WCAP_STEREO;
        w.default_config = defcfg;
    }
    if (typ == HDA_WID_OUT) {
        w.awcap |= HDA_WCAP_STEREO | HDA_WCAP_OUT_AMP;
    }
    if (typ == HDA_WID_MIX || typ == HDA_WID_SEL) {
        w.awcap |= HDA_WCAP_CONN_LIST;
    }
    if (conn && conn_n) {
        w.awcap |= HDA_WCAP_CONN_LIST;
        w.liczba_polaczen = conn_n;
        for (uint8_t i = 0; i < conn_n && i < HDA_DIAG_MAKS_POLACZEN; ++i) {
            w.polaczenia[i] = conn[i];
            w.polaczenia_surowe[i] = conn[i];
        }
    }
    return w;
}

static void test_sciezka_prosta() {
    /* DAC(2) <- Pin(4) : pin ma polaczenie do 2. */
    const uint8_t conn[] = {2};
    HdaDiagWidget w[2] = {
        zrob_widget(2, HDA_WID_OUT),
        zrob_widget(4, HDA_WID_PIN, (2u << 30) | (0x30u << 24) | (1u << 20), conn, 1)
    };
    HdaDiagSciezka s[HDA_DIAG_MAKS_SCIEZEK]{};
    uint32_t n = 0;
    bool obc = false;
    CHECK(hda_zbuduj_sciezki(w, 2, s, HDA_DIAG_MAKS_SCIEZEK, &n, &obc));
    CHECK(n == 1);
    CHECK(s[0].konwerter_nid == 2);
    CHECK(s[0].pin_nid == 4);
    CHECK(s[0].dlugosc == 2);
    CHECK(s[0].cel == HDA_CEL_GLOSNIK);
}

static void test_sciezka_z_mixerem() {
    /* DAC(2) <- Mixer(5) <- Pin(4) */
    const uint8_t c_mix[] = {2};
    const uint8_t c_pin[] = {5};
    HdaDiagWidget w[3] = {
        zrob_widget(2, HDA_WID_OUT),
        zrob_widget(5, HDA_WID_MIX, 0, c_mix, 1),
        zrob_widget(4, HDA_WID_PIN, (2u << 30) | (0x30u << 24) | (1u << 20), c_pin, 1)
    };
    HdaDiagSciezka s[8]{};
    uint32_t n = 0;
    bool obc = false;
    CHECK(hda_zbuduj_sciezki(w, 3, s, 8, &n, &obc));
    CHECK(n == 1);
    CHECK(s[0].dlugosc == 3);
    CHECK(s[0].nids[0] == 2 && s[0].nids[1] == 5 && s[0].nids[2] == 4);
    CHECK(s[0].cel == HDA_CEL_GLOSNIK);
}

static void test_sciezka_selector() {
    /* DAC(2) <- Sel(6) <- Pin(4) sluchawki */
    const uint8_t c_sel[] = {2};
    const uint8_t c_pin[] = {6};
    HdaDiagWidget w[3] = {
        zrob_widget(2, HDA_WID_OUT),
        zrob_widget(6, HDA_WID_SEL, 0, c_sel, 1),
        zrob_widget(4, HDA_WID_PIN, (0u << 30) | (0u << 24) | (2u << 20), c_pin, 1)
    };
    HdaDiagSciezka s[8]{};
    uint32_t n = 0;
    bool obc = false;
    CHECK(hda_zbuduj_sciezki(w, 3, s, 8, &n, &obc));
    CHECK(n == 1);
    CHECK(s[0].cel == HDA_CEL_SLUCHAWKI);
}

static void test_sciezka_wiele_glosnikow() {
    /* Jeden DAC, dwa piny Speaker. */
    const uint8_t c1[] = {2};
    const uint8_t c2[] = {2};
    const uint32_t spk = (2u << 30) | (0x30u << 24) | (1u << 20);
    HdaDiagWidget w[3] = {
        zrob_widget(2, HDA_WID_OUT),
        zrob_widget(4, HDA_WID_PIN, spk, c1, 1),
        zrob_widget(7, HDA_WID_PIN, spk, c2, 1)
    };
    HdaDiagSciezka s[8]{};
    uint32_t n = 0;
    bool obc = false;
    CHECK(hda_zbuduj_sciezki(w, 3, s, 8, &n, &obc));
    CHECK(n == 2);
    CHECK(hda_sciezka_glosnik_istnieje(s, n));
    CHECK(hda_liczba_sciezek_glosnik(s, n) == 2);
}

static void test_sciezka_cykl() {
    /* DAC(2) <- Mix(5) <- Mix(5) — cykl na 5. */
    const uint8_t c5[] = {2, 5};
    const uint8_t c4[] = {5};
    HdaDiagWidget w[3] = {
        zrob_widget(2, HDA_WID_OUT),
        zrob_widget(5, HDA_WID_MIX, 0, c5, 2),
        zrob_widget(4, HDA_WID_PIN, (2u << 30) | (0x30u << 24) | (1u << 20), c4, 1)
    };
    HdaDiagSciezka s[8]{};
    uint32_t n = 0;
    bool obc = false;
    CHECK(hda_zbuduj_sciezki(w, 3, s, 8, &n, &obc));
    /* DFS nie zawiesza sie; sciezka przez 5 do 4 istnieje. */
    CHECK(n >= 1);
    if (n > 0) CHECK(s[0].pin_nid == 4);
}

static void test_sciezka_brak_pinow() {
    const uint8_t c[] = {2};
    HdaDiagWidget w[2] = {
        zrob_widget(2, HDA_WID_OUT),
        zrob_widget(5, HDA_WID_MIX, 0, c, 1)
    };
    HdaDiagSciezka s[4]{};
    uint32_t n = 0;
    bool obc = false;
    CHECK(hda_zbuduj_sciezki(w, 2, s, 4, &n, &obc));
    CHECK(n == 0);
    CHECK(!hda_sciezka_glosnik_istnieje(s, n));
}

static void test_sciezka_hdmi_i_niepodlaczony() {
    const uint8_t c[] = {2};
    const uint32_t hdmi = (0u << 30) | (0u << 24) | (5u << 20);
    const uint32_t disc = (1u << 30);
    HdaDiagWidget w[3] = {
        zrob_widget(2, HDA_WID_OUT),
        zrob_widget(4, HDA_WID_PIN, hdmi, c, 1),
        zrob_widget(7, HDA_WID_PIN, disc, c, 1)
    };
    HdaDiagSciezka s[8]{};
    uint32_t n = 0;
    bool obc = false;
    CHECK(hda_zbuduj_sciezki(w, 3, s, 8, &n, &obc));
    CHECK(n == 2);
    CHECK(hda_sciezka_hdmi_istnieje(s, n));
    CHECK(!hda_sciezka_glosnik_istnieje(s, n));
}

static void test_sciezka_limit() {
    /* 3 piny, limit 2 sciezek. */
    const uint8_t c[] = {2};
    const uint32_t spk = (2u << 30) | (0x30u << 24) | (1u << 20);
    HdaDiagWidget w[4] = {
        zrob_widget(2, HDA_WID_OUT),
        zrob_widget(4, HDA_WID_PIN, spk, c, 1),
        zrob_widget(5, HDA_WID_PIN, spk, c, 1),
        zrob_widget(6, HDA_WID_PIN, spk, c, 1)
    };
    HdaDiagSciezka s[2]{};
    uint32_t n = 0;
    bool obc = false;
    CHECK(hda_zbuduj_sciezki(w, 4, s, 2, &n, &obc));
    CHECK(n == 2);
    CHECK(obc);
}

static void test_raport() {
    char buf[256];
    HdaRaportBufor rb;
    hda_raport_init(&rb, buf, sizeof(buf));
    hda_raport_linia(&rb, "HDA Controller 0");
    hda_raport_klucz_hex32(&rb, "GCAP raw", 0x1234ABCD);
    CHECK(std::strstr(buf, "HDA Controller 0") != nullptr);
    CHECK(std::strstr(buf, "0x1234ABCD") != nullptr);

    /* Obciazenie. */
    char maly[16];
    HdaRaportBufor rb2;
    hda_raport_init(&rb2, maly, sizeof(maly));
    hda_raport_dopisz(&rb2, "0123456789ABCDEFGHIJ");
    CHECK(rb2.obciety);
    CHECK(rb2.dlugosc < sizeof(maly));
}

static void test_raport_obciazenie_marker() {
    HdaDiagSnapshot snap{};
    snap.wersja = HDA_DIAG_WERSJA_ABI;
    snap.rozmiar = sizeof(HdaDiagSnapshot);
    snap.flagi = HDA_DIAG_FLAGA_OBECIETA;
    snap.liczba_kontrolerow = 0;
    char buf[512];
    HdaRaportBufor rb;
    hda_raport_init(&rb, buf, sizeof(buf));
    CHECK(hda_sformatuj_raport(&snap, "1.0.0", &rb));
    CHECK(std::strstr(buf, "obcieta") != nullptr);
    CHECK(std::strstr(buf, "Nie znaleziono kontrolera") != nullptr);
}

static void test_limity_abi() {
    CHECK(HDA_DIAG_WERSJA_ABI == 1);
    CHECK(HDA_DIAG_MAKS_POLACZEN >= 4);
    CHECK(HDA_DIAG_MAKS_DLUGOSC_SCIEZKI >= 3);
    CHECK(sizeof(HdaDiagNaglowek) == 8);
    /* Snapshot miesci sie w rozsadnym limicie (nie nieograniczony). */
    CHECK(sizeof(HdaDiagSnapshot) < 128 * 1024);
    CHECK(sizeof(HdaDiagWidget) < 128);
}

static void test_stale_testu() {
    CHECK(HDA_TEST_SWEEP_MIN_HZ == 200);
    CHECK(HDA_TEST_SWEEP_MAX_HZ == 4000);
    CHECK(HDA_TEST_DOMYSLNY_CZAS_MS >= 1000 && HDA_TEST_DOMYSLNY_CZAS_MS <= 2000);
    CHECK(HDA_TEST_AMPLITUDA < 32767);
    CHECK(HDA_TEST_TON_A4_HZ == 440);
    CHECK(HDA_TEST_TON_1K_HZ == 1000);
}

int main() {
    test_awcap_typy();
    test_awcap_flagi();
    test_pincfg();
    test_parametry();
    test_amp_pcm();
    test_connlist_short();
    test_connlist_long();
    test_connlist_range();
    test_connlist_malformed();
    test_connlist_truncacja();
    test_sciezka_prosta();
    test_sciezka_z_mixerem();
    test_sciezka_selector();
    test_sciezka_wiele_glosnikow();
    test_sciezka_cykl();
    test_sciezka_brak_pinow();
    test_sciezka_hdmi_i_niepodlaczony();
    test_sciezka_limit();
    test_raport();
    test_raport_obciazenie_marker();
    test_limity_abi();
    test_stale_testu();

    std::printf("HDA diag host: %d testow, %d bledow\n", testy, bledy);
    return bledy == 0 ? 0 : 1;
}
