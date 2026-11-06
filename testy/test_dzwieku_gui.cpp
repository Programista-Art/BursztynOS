// Real app + retained controls + system font service + real PSF under sanitizers.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#define BURSZTYN_UI_CZCIONKA_SYSTEMOWA 1
#define BURSZTYN_PSF_HOST_TEST 1
#include "bursztyn_ui_host_fixture.h"
#define czytaj_fragment_pliku psf_host_czytaj_fragment_pliku
#include "../jadro/fs/psf.cpp"
#undef czytaj_fragment_pliku
#include "../jadro/gui/czcionka_systemowa.cpp"
#define private public
#define _start test_dzwieku_entry
#include "../programy/test_dzwieku/test_dzwieku.cpp"
#undef _start
#undef private

static unsigned checks = 0, scenarios = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
void* kmalloc(size_t n) { return std::malloc(n); }
void kfree(void* p) { std::free(p); }
void wypisz_log(const char*) {}
static std::vector<std::string> startup_trace;
void wypisz(const char* p) { startup_trace.emplace_back(p); }
bool pobierz_czas_rtc_bezpiecznie(czas_rtc*) { return false; }
bool blokowy_zapis_instalatora() { return false; }
static unsigned windows = 0, hda_calls = 0, snapshot_calls = 0, tone_starts = 0, tone_stops = 0;
static bool font_writable = true;
static int copy_budget = -1;
bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void* p, size_t n) {
    return font_writable && p && reinterpret_cast<uintptr_t>(p) <= UINTPTR_MAX - n;
}
bool skopiuj_do_przestrzeni_uzytkownika(void* dst, const void* src, size_t n) {
    if (!copy_budget) return false;
    if (copy_budget > 0) --copy_budget;
    std::memcpy(dst, src, n); return true;
}
bool skopiuj_z_przestrzeni_uzytkownika(void* dst, const void* src, size_t n) { std::memcpy(dst, src, n); return true; }
bool skopiuj_string_z_uzytkownika(char* dst, const char* src, size_t cap) {
    if (!src || std::strlen(src) >= cap) return false;
    std::memcpy(dst, src, std::strlen(src) + 1); return true;
}
namespace KernelFsHost {
bool instalator_tryb() { return false; }
void powiadom_zmiane_systemu_plikow(const char*, uint32_t) {}
#include "../.hda-test/repair/filesystem_actual.inc"
}
static proces_t app_process{};
int bws_utworz_warstwe(int, int, int, int, int) { ++windows; return 1; }
static HdaDiagSnapshot fixture{};
static HdaDiagSkan scan{};
static bool connected = true, ready = true, tone = false, tone_ok = true;
static uint32_t scan_error = 0;
static uint64_t file_fail_nr = 0, file_fail_code = 0;
static bool wrong_file_size = false;
static std::vector<BwsHdaTestStart> tone_requests;
static std::vector<uint64_t> tone_commands;
static uint64_t hook(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    if (nr == 51) return bws_czcionka_kopiuj(PRAWO_GUI, a1, a2, a3);
    if (nr == 89 || nr == 90) { ++hda_calls; CHECK(windows > 0); CHECK(test_refreshes > 0); }
    if (nr == 89) {
        if (!connected) return 0;
        switch (a1) {
            case BWS_HDA_DIAG_STAN: CHECK(a3==sizeof(HdaDiagSkan)&&a4==HDA_DIAG_WERSJA_ABI);std::memcpy(reinterpret_cast<void*>(a2), &scan, sizeof(scan)); return 1;
            case BWS_HDA_DIAG_SKAN_START:
                scan = {}; scan.aktywny = 1; scan.etap = HDA_DIAG_ETAP_KONTROLER; scan.generacja_skanu = 7; return 1;
            case BWS_HDA_DIAG_SKAN_KROK:
                scan.aktywny = 0; scan.gotowy = !scan_error; scan.blad = scan_error;
                scan.etap = scan_error ? HDA_DIAG_ETAP_BLAD : HDA_DIAG_ETAP_GOTOWY;
                scan.generacja_skanu = 7; scan.generacja_opublikowana = 7; return 1;
            case BWS_HDA_DIAG_SKAN_ANULUJ:
                scan.aktywny = 0; scan.przerwany = 1; return 1;
            case BWS_HDA_DIAG_SNAPSHOT: {
                ++snapshot_calls; if (!ready) return 0;
                auto* request = reinterpret_cast<BwsHdaSnapshotZadanie*>(a2);
                CHECK(request->bufor_maks == sizeof(fixture));
                std::memcpy(reinterpret_cast<void*>(request->bufor), &fixture, sizeof(fixture));
                request->zapisano = sizeof(fixture); request->generacja = 7; return 1;
            }
            case BWS_HDA_DIAG_TEST_STAN: {
                auto* t = reinterpret_cast<HdaDiagStanTestu*>(a2); *t = fixture.test; t->aktywny = tone; return 1;
            }
            case BWS_HDA_DIAG_STRUMIEN:
                std::memcpy(reinterpret_cast<void*>(a2), &fixture.strumien, sizeof(fixture.strumien)); return 1;
            default: CHECK(false); return 0;
        }
    }
    if (nr == 90) {
        tone_commands.push_back(a1);
        if (a1 == BWS_HDA_TEST_STOP) { ++tone_stops; tone = false; return 1; }
        CHECK(a1 == BWS_HDA_TEST_START); CHECK(!tone);CHECK(a3==g_kontroler&&a4==HDA_DIAG_WERSJA_ABI);
        const auto t = *reinterpret_cast<BwsHdaTestStart*>(a2);
        CHECK(t.wersja == HDA_DIAG_WERSJA_ABI); CHECK(t.rozmiar == sizeof(t));
        CHECK(t.czas_ms == HDA_TEST_DOMYSLNY_CZAS_MS); CHECK(t.kanaly >= 1 && t.kanaly <= 3);
        tone_requests.push_back(t); ++tone_starts; tone = tone_ok; return tone_ok;
    }
    if (nr == file_fail_nr) return file_fail_code;
    if (nr == BWS_UTWORZ_PLIK || nr == BWS_UTWORZ_KATALOG) {
        CHECK(a2 == BWS_STATUS_PLIKOWY);
        return nr == BWS_UTWORZ_PLIK ? KernelFsHost::bws_utworz_plik(app_process, a1, a2)
                                   : KernelFsHost::bws_utworz_katalog(app_process, a1, a2);
    }
    if (nr == BWS_ZAPISZ_PLIK) return KernelFsHost::bws_zapisz_plik(app_process, a1, a2, a3);
    if (nr == BWS_METADANE_PLIKU) {
        const auto rc = KernelFsHost::bws_pobierz_metadane(app_process, a1, a2);
        auto* out = reinterpret_cast<BwsMetadanePliku*>(a2);
        if (rc && wrong_file_size && out->typ == BWS_TYP_PLIK) ++out->rozmiar;
        return rc;
    }
    return 0;
}
static void reset() {
    connected = ready = tone_ok = true; scan = {}; scan_error = 0; tone = false;
    g_snapshot_ok = false; g_generacja = 0; g_kontroler=0; g_stan_skan = {}; g_raport_obciety = false;
    fixture = {}; fixture.wersja = HDA_DIAG_WERSJA_ABI; fixture.rozmiar = sizeof(fixture);
    fixture.liczba_kontrolerow = 1; fixture.etap = HDA_DIAG_ETAP_GOTOWY;
    auto& c = fixture.kontrolery[0]; c.bus = 0; c.slot = 27; c.vendor_id = 0x8086;
    c.device_id = 0x2668;c.gotowy=1;c.test_dostepny=1;c.transport=1;c.inicjalizacja=22;c.hda_vmaj=1; c.gcap = 0x4401; c.statests = 1; c.liczba_kodekow = 1;
    auto& codec = fixture.kodeki[0][0]; codec.odpowiada = 1; codec.vendor_id = 0x1af4; codec.device_id = 0x12;
    auto& fg = fixture.fg[0][0][0]; fg.nid = 1; fg.audio = 1; fg.typ = 1; fg.widget_start = 2; fg.widget_count = 2;
    auto& dac = fixture.widgety[0][0][0][0]; dac.nid = 2; dac.typ = HDA_WID_OUT; dac.awcap = 1;
    auto& pin = fixture.widgety[0][0][0][1]; pin.nid = 3; pin.typ = HDA_WID_PIN;
    pin.awcap = 0x400181; pin.pin_caps = 0x10; pin.liczba_polaczen = 1; pin.polaczenia[0] = 2;
    fixture.strumien.stan = HDA_STRUMIEN_BEZCZYNNY;
    fixture.strumien.wersja = HDA_DIAG_WERSJA_ABI; fixture.strumien.rozmiar = sizeof(fixture.strumien);
    fixture.test.wersja = HDA_DIAG_WERSJA_ABI; fixture.test.rozmiar = sizeof(fixture.test);
    windows = hda_calls = snapshot_calls = tone_starts = tone_stops = 0;
    test_refreshes = 0; tone_requests.clear(); tone_commands.clear(); file_fail_nr = 0; wrong_file_size = false;
    startup_trace.clear();
    g_test_bws_hook = hook; test_wait_observer = nullptr;
    app_process.poziom_zaufania = 3;
    app_process.uprawnienia = PRAWO_GUI | PRAWO_PLIKI_CZYTAJ | PRAWO_PLIKI_ZAPISZ;
    test_ustaw_aktywny_motyw(nullptr); ui_skala_procent = 100;
}
struct EndLoop {};
static void stop_loop() { throw EndLoop{}; }
static void start(OknoTestDzwieku& window) {
    ++scenarios; window.inicjalizuj(); CHECK(!hda_calls); CHECK(!tone_starts);
    AplikacjaGUI app; test_wait_observer = stop_loop;
    try { app.uruchom(window); CHECK(false); } catch (const EndLoop&) {}
    test_wait_observer = nullptr;
    CHECK(windows == 1); CHECK(test_refreshes > 0); CHECK(hda_calls > 0);
    CHECK(!tone_starts); CHECK(window.pierwsza_klatka_);
}
static void click(Przycisk& b) { ZdarzenieKlawiatury e{}; e.kod_klawisza = 13; b.obsluz_wcisniecie_klawisza(e); CHECK(e.obsluzone); }
static void draw(Okno& w) { KontekstRysowania ctx; w.rysuj(ctx, true); }
static void dump_client(Okno& w, uint32_t scale, int section) {
    char path[128]; std::snprintf(path, sizeof(path), ".hda-test/repair/client-%u-section-%d.ppm", scale, section);
    const auto rect = w.obszar_klienta(); FILE* out = std::fopen(path, "wb"); CHECK(out);
    std::fprintf(out, "P6\n%d %d\n255\n", rect.szerokosc, rect.wysokosc);
    const auto* pixels = w.bufor_regionu(); CHECK(pixels);
    for (int i = 0; i < rect.szerokosc * rect.wysokosc; ++i) {
        const uint8_t rgb[] = {uint8_t(pixels[i] >> 16), uint8_t(pixels[i] >> 8), uint8_t(pixels[i])};
        (void)std::fwrite(rgb, 1, 3, out);
    }
    CHECK(!std::ferror(out));
    CHECK(!std::fclose(out));
}
static void font_tests() {
    ++scenarios; auto out = std::make_unique<BwsCzcionkaSystemowa>(); const uint64_t ptr = reinterpret_cast<uint64_t>(out.get());
    CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 0, 0, 0) == 16);
    CHECK(!bws_czcionka_kopiuj(0, 1, ptr, sizeof(*out)));
    CHECK(!bws_czcionka_kopiuj(PRAWO_GUI, 2, ptr, sizeof(*out)));
    CHECK(!bws_czcionka_kopiuj(PRAWO_GUI, 1, 0, sizeof(*out)));
    CHECK(!bws_czcionka_kopiuj(PRAWO_GUI, 1, ptr, sizeof(*out) - 1));
    CHECK(!bws_czcionka_kopiuj(PRAWO_GUI, 1, UINT64_MAX - 3, sizeof(*out)));
    font_writable = false; CHECK(!bws_czcionka_kopiuj(PRAWO_GUI, 1, ptr, sizeof(*out))); font_writable = true;
    copy_budget = 1; CHECK(!bws_czcionka_kopiuj(PRAWO_GUI, 1, ptr, sizeof(*out))); CHECK(!out->wersja); copy_budget = -1;
    CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 1, ptr, sizeof(*out)) == 1);
    CHECK(!std::memcmp(out->bitmapy, nowa_czcionka_16x16, sizeof(out->bitmapy)));
    CHECK(!std::memcmp(out->szerokosci, nowa_czcionka_szerokosci, sizeof(out->szerokosci)));
    CHECK(CzcionkaUI::pobierz_systemowa());
    CHECK(CzcionkaUI::zmierz_tekst("iii", SkalaUI(1)) < CzcionkaUI::zmierz_tekst("WWW", SkalaUI(1)));
    for (const char* t : {"ą", "ć", "ę", "ł", "ń", "ó", "ś", "ź", "ż", "Ś"}) {
        uint32_t cp = 0; CHECK(CzcionkaUI::dekoduj_utf8(t, 0, &cp) == 2);
        CHECK(cp < BWS_CZCIONKA_GLIFY); CHECK(CzcionkaUI::postep_znaku(cp, SkalaUI(1)) > 0);
    }
}
static void layout(Kontrolka* c, const Prostokat& client, SkalaUI scale) {
    if (!c->czy_widoczna()) return;
    const auto r = c->obszar_ekranu();
    CHECK(r.x >= client.x); CHECK(r.y >= client.y);
    CHECK(r.x + r.szerokosc <= client.x + client.szerokosc);
    CHECK(r.y + r.wysokosc <= client.y + client.wysokosc);
    if (auto* b = dynamic_cast<Przycisk*>(c)) {
        CHECK(CzcionkaUI::zmierz_tekst(b->tekst(), scale) + 12 * scale <= r.szerokosc);
        CHECK(CzcionkaUI::pobierz_wysokosc(scale) <= r.wysokosc);
    }
    for (size_t i = 0; i < c->liczba_dzieci(); ++i) layout(c->dziecko(i), client, scale);
}
int main(int argc, char** argv) {
    std::vector<uint8_t> ram(16 * 1024 * 1024); inicjalizuj_psf(ram.data(), ram.size());
    reset(); font_tests();
    ++scenarios; CHECK(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> binary(std::istreambuf_iterator<char>(file), {});
    CHECK(!binary.empty());
    auto absent = [&](const void* ptr, size_t n) {
        const auto* bytes = static_cast<const uint8_t*>(ptr);
        CHECK(std::search(binary.begin(), binary.end(), bytes, bytes + n) == binary.end());
    };
    absent(nowa_czcionka_16x16, sizeof(nowa_czcionka_16x16));
    absent(nowa_czcionka_szerokosci, sizeof(nowa_czcionka_szerokosci));
    reset(); ++scenarios; test_wait_observer = stop_loop;
    try { test_dzwieku_entry(); } catch (const EndLoop&) {}
    test_wait_observer = nullptr;
    CHECK(windows == 1); CHECK(test_refreshes > 0); CHECK(hda_calls > 0); CHECK(!tone_starts);
#if BURSZTYNDIAGTESTDZWIEKU
    CHECK(startup_trace.size() == 5);
    CHECK(startup_trace.front().find("Test dźwięku entry reached") != std::string::npos);
    CHECK(startup_trace[3].find("first frame published") != std::string::npos);
    CHECK(startup_trace[4].find("HDA diagnostic ABI connected") != std::string::npos);
#endif
    for (int error : {-1, int(HDA_DIAG_BLAD_BRAK_KONTROLERA), int(HDA_DIAG_BLAD_KODEK_NIE_ODPOWIADA), -2, -3}) {
        reset(); connected = error != -1; ready = error != -2; scan_error = error > 0 ? error : 0;
        if (error == -3) fixture.kodeki[0][0].odpowiada = 0;
        OknoTestDzwieku window; start(window);
        CHECK(window.status_->typ_komunikatu() == TypKomunikatu::Blad);
        CHECK(std::strlen(window.status_->tekst()) > 0); CHECK(window.btn_odswiez_->czy_wlaczona());
        connected = ready = true; scan_error = 0;
        fixture.kodeki[0][0].odpowiada = 1;
        click(*window.btn_odswiez_); window.tick_skanu(); CHECK(g_snapshot_ok);
        CHECK(window.status_->typ_komunikatu() == TypKomunikatu::Sukces);
    }
    reset(); OknoTestDzwieku window; start(window); CHECK(g_snapshot_ok); CHECK(g_generacja == 7);
    CHECK(window.wskaznik() != nullptr);
    CHECK(!window.wskaznik()->czy_aktywny());
    CHECK(!window.wskaznik()->czy_widoczna());
    /* Test animacji spinnera, odswiezania i odpornosci na wielokrotne klikniecia */
    click(*window.btn_odswiez_);
    CHECK(window.wskaznik()->czy_aktywny());
    CHECK(window.wskaznik()->czy_widoczna());
    click(*window.btn_odswiez_);
    click(*window.btn_odswiez_);
    CHECK(window.wskaznik()->czy_aktywny());
    window.tick_skanu();
    CHECK(!window.wskaznik()->czy_aktywny());
    CHECK(!window.wskaznik()->czy_widoczna());
    for (const char* text : {"PCI BDF 00:1B.0", "0x8086", "0x2668", "GCAP", "STATESTS"})
        CHECK(std::strstr(window.tekst_kontroler_->tekst(), text));
    CHECK(std::strstr(window.tekst_kodeki_->tekst(), "AFG NID"));
    CHECK(std::strstr(window.tekst_widgety_->tekst(), "Pin Complex"));
    CHECK(std::strstr(window.tekst_widgety_->tekst(), "conn:"));
    CHECK(std::strstr(window.tekst_strumien_->tekst(), "bezczynny"));
    const unsigned snapshots = snapshot_calls;
    for (int i = 0; i < 10; ++i) { window.tick_skanu(); draw(window); }
    CHECK(snapshot_calls == snapshots); CHECK(!window.potrzebuje_ticku());
    for (uint32_t scale : {100, 125, 150, 175, 200}) {
        ++scenarios; bws_zdarzenie ev{}; ev.typ = BWS_ZDARZENIE_EKRAN; ev.kod = scale; window.obsluz_zdarzenie(ev);
        CHECK(window.x() >= 0 && window.y() >= 0); CHECK(window.x() + window.szerokosc() <= 1280);
        CHECK(window.y() + window.wysokosc() <= 720);
        for (int section = 0; section < 7; ++section) {
            window.sekcje_->ustaw_wybrany_indeks(section);
            window.pokaz_sekcje(section); draw(window);
            layout(window.kontrolka_glowna(), window.obszar_klienta(), window.skala_dpi());
            CHECK(window.pages_[section]->wysokosc() > 0);
            if ((scale == 100 || scale == 200) && (section == 0 || section == 4)) dump_client(window, scale, section);
        }
    }
    ++scenarios; BursztynMotyw alternate = g_domyslny_motyw_fallback;
    alternate.przycisk = 0x204060; alternate.przycisknajechany = 0x315171;
    alternate.akcentaktywny = 0x426282; alternate.panel = 0x152535;
    alternate.tekst = 0xeefaff; alternate.ramka = 0x556677; alternate.poletekstowe = 0x123456;
    alternate.tekstdrugorzedny = 0xabcde0;
    test_ustaw_aktywny_motyw(&alternate);
    bws_zdarzenie theme{}; theme.typ = BWS_ZDARZENIE_MOTYW_ZMIENIONY; window.obsluz_zdarzenie(theme);
    const auto& style = window.btn_zapisz_->styl();
    CHECK(style.normalny.tlo == alternate.przycisk); CHECK(style.najechany.tlo == alternate.przycisknajechany);
    CHECK(style.wcisniety.tlo == alternate.akcentaktywny); CHECK(style.normalny.tekst == alternate.tekst);
    CHECK(style.normalny.ramka == alternate.ramka);
    CHECK(style.wylaczony.tlo == alternate.przycisk); CHECK(style.wylaczony.tekst == alternate.wylaczony);
    CHECK(style.fokus.ramka == alternate.akcent);
    CHECK(window.tekst_raport_->kolor_tla() == alternate.poletekstowe);
    CHECK(window.info_->kolor_tekstu() == alternate.tekstdrugorzedny);
    test_ustaw_aktywny_motyw(nullptr); window.obsluz_zdarzenie(theme);
    CHECK(window.btn_zapisz_->kolor_tla() == g_domyslny_motyw_fallback.przycisk);
    ++scenarios; window.pokaz_sekcje(6);
    std::string long_report; for (int i = 0; i < 1000; ++i) long_report += "Ścieżki audio: Łódź, głośniki, żółć — Unicode\n";
    window.tekst_raport_->ustaw_tekst(long_report.c_str()); draw(window);
    CHECK(window.tekst_raport_->dostawca()->liczba_elementow() >= 1000);
    window.tekst_raport_->przewin_do(999); draw(window); CHECK(window.tekst_raport_->pierwszy_widoczny_indeks() > 0);
    CHECK(window.tekst_raport_->liczba_zapytan_dostawcy() < 100);
    for (size_t i = 0; i < 10; ++i) { char row[512]{}; window.tekst_raport_->dostawca()->pobierz_tekst(i, row, sizeof(row));
        CHECK(CzcionkaUI::zmierz_tekst(row, window.skala_dpi()) <= window.tekst_raport_->width_); }
    ++scenarios; click(*window.btn_zapisz_); CHECK(window.status_->typ_komunikatu() == TypKomunikatu::Sukces);
    CHECK(czy_katalog_istnieje("/dane/diagnostyka")); CHECK(rozmiar_pliku(SCIEZKA_RAPORTU) == std::strlen(g_raport));
    std::vector<char> saved(std::strlen(g_raport)); uint32_t bytes = 0;
    CHECK(psf_host_czytaj_fragment_pliku(SCIEZKA_RAPORTU, 0, saved.data(), saved.size(), &bytes));
    CHECK(bytes == saved.size()); CHECK(!std::memcmp(saved.data(), g_raport, bytes));
    ++scenarios; app_process.uprawnienia &= ~PRAWO_PLIKI_ZAPISZ; click(*window.btn_zapisz_);
    CHECK(g_plik_bws == BWS_UTWORZ_KATALOG); CHECK(g_plik_kod == uint64_t(WynikPlikowy::BrakUprawnien));
    CHECK(window.status_->typ_komunikatu() == TypKomunikatu::Blad); app_process.uprawnienia |= PRAWO_PLIKI_ZAPISZ;
    auto* protected_report = pobierz_wezel(rozwiaz_sciezke(SCIEZKA_RAPORTU, nullptr, false));
    const uint64_t old_flags = protected_report->flagi_zabezpieczen;
    ustaw_metadane_nowego_wezla(protected_report, 2); click(*window.btn_zapisz_);
    CHECK(g_plik_bws == BWS_ZAPISZ_PLIK); CHECK(!g_plik_kod);
    CHECK(window.status_->typ_komunikatu() == TypKomunikatu::Blad);
    protected_report->flagi_zabezpieczen = old_flags;
    for (uint64_t nr : {uint64_t(46), uint64_t(2), uint64_t(3), uint64_t(47)}) {
        ++scenarios; file_fail_nr = nr; file_fail_code = nr == 3 || nr == 47 ? 0 : uint64_t(WynikPlikowy::BrakUprawnien);
        click(*window.btn_zapisz_); CHECK(window.status_->typ_komunikatu() == TypKomunikatu::Blad);
        CHECK(g_plik_bws == nr); CHECK(g_plik_kod == file_fail_code); CHECK(std::strstr(window.status_->tekst(), "BWS"));
    }
    file_fail_nr = 0; wrong_file_size = true; click(*window.btn_zapisz_);
    CHECK(window.status_->typ_komunikatu() == TypKomunikatu::Blad); wrong_file_size = false;
    ++scenarios; window.pokaz_sekcje(4);
    auto* grid = window.pages_[4]->dziecko(1); CHECK(grid->liczba_dzieci() == 6);
    for (size_t i = 0; i < 6; ++i) { click(*static_cast<Przycisk*>(grid->dziecko(i))); CHECK(tone); }
    CHECK(tone_starts == 6); CHECK(tone_stops == 6);
    CHECK(tone_requests[0].czestotliwosc_hz == 440); CHECK(tone_requests[1].czestotliwosc_hz == 1000);
    CHECK(tone_requests[2].tryb == HDA_TEST_SWEEP);
    CHECK(tone_requests[3].kanaly == 1); CHECK(tone_requests[4].kanaly == 2); CHECK(tone_requests[5].kanaly == 3);
    click(*window.btn_stop_); click(*window.btn_stop_); CHECK(!tone); CHECK(!window.ton_aktywny_);
    tone_ok = false; click(*static_cast<Przycisk*>(grid->dziecko(0)));
    CHECK(window.status_->typ_komunikatu() == TypKomunikatu::Blad); CHECK(!tone);
    CHECK(!window.ton_aktywny_);
    ++scenarios; connected = false; window.odswiez_strumien();
    CHECK(window.status_->typ_komunikatu() == TypKomunikatu::Blad);
    CHECK(std::strstr(window.tekst_strumien_->tekst(), "niedostępny"));
    connected = true; window.odswiez_strumien();
    ++scenarios; CHECK(usun_twor(SCIEZKA_RAPORTU)); CHECK(usun_twor("/dane/diagnostyka"));
    CHECK(utworz_plik("/dane/diagnostyka")); click(*window.btn_zapisz_);
    CHECK(g_plik_bws == BWS_METADANE_PLIKU); CHECK(!g_plik_kod);
    CHECK(window.status_->typ_komunikatu() == TypKomunikatu::Blad);
    CHECK(usun_twor("/dane/diagnostyka")); click(*window.btn_zapisz_);
    CHECK(window.status_->typ_komunikatu() == TypKomunikatu::Sukces);
    ++scenarios; char clipped[6]{}; size_t position = 0; dopisz_str(clipped, sizeof(clipped), &position, "ąęł");
    CHECK(!std::strcmp(clipped, "ąę")); CHECK(position == 4);
    ++scenarios; g_snapshot = fixture; g_snapshot_ok = true;
    g_snapshot.liczba_kontrolerow = HDA_DIAG_MAKS_KONTROLEROW;
    for (uint32_t k = 0; k < HDA_DIAG_MAKS_KONTROLEROW; ++k)
        for (uint32_t c = 0; c < HDA_DIAG_MAKS_KODEKOW; ++c) {
            g_snapshot.kodeki[k][c].odpowiada = 1;
            for (uint32_t f = 0; f < HDA_DIAG_MAKS_FG; ++f) {
                auto& fg = g_snapshot.fg[k][c][f]; fg.nid = f + 1; fg.widget_count = HDA_DIAG_MAKS_WIDGETOW;
                for (uint32_t w = 0; w < HDA_DIAG_MAKS_WIDGETOW; ++w) {
                    auto& widget = g_snapshot.widgety[k][c][f][w]; widget.nid = w + 1;
                    widget.typ = HDA_WID_PIN; widget.awcap = 0x40018f; widget.pin_caps = UINT32_MAX;
                    widget.default_config = UINT32_MAX; widget.out_amp_caps = UINT32_MAX; widget.in_amp_caps = UINT32_MAX;
                    widget.liczba_polaczen = HDA_DIAG_MAKS_POLACZEN;
                    std::memset(widget.polaczenia, 0x7f, sizeof(widget.polaczenia));
                    std::memset(widget.polaczenia_surowe, 0xff, sizeof(widget.polaczenia_surowe));
                }
            }
        }
    // A realistic laptop report fits; the deliberately impossible maximum is explicitly clipped.
    window.odswiez_wszystko(); CHECK(g_raport_obciety); CHECK(!zapisz_raport());
    g_snapshot.liczba_kontrolerow=2;
    for(uint32_t k=0;k<HDA_DIAG_MAKS_KONTROLEROW;++k)
        for(uint32_t c=0;c<HDA_DIAG_MAKS_KODEKOW;++c){
            g_snapshot.kodeki[k][c].odpowiada=k<2&&c==0;
            for(uint32_t f=0;f<HDA_DIAG_MAKS_FG;++f)g_snapshot.fg[k][c][f].widget_count=f==0?64:0;
        }
    window.odswiez_wszystko(); CHECK(!g_raport_obciety); CHECK(!window.tekst_widgety_->czy_obciety());
    CHECK(zapisz_raport()); CHECK(rozmiar_pliku(SCIEZKA_RAPORTU) == std::strlen(g_raport));
    std::vector<char> maximal(std::strlen(g_raport)); bytes = 0;
    CHECK(psf_host_czytaj_fragment_pliku(SCIEZKA_RAPORTU, 0, maximal.data(), maximal.size(), &bytes));
    CHECK(bytes == maximal.size()); CHECK(!std::memcmp(maximal.data(), g_raport, bytes));
    char too_small[32]{}; bool truncated_report = false; zbuduj_raport(too_small, sizeof(too_small), &truncated_report);
    CHECK(truncated_report); std::printf("two-controller 128-widget report: %zu bytes, complete file roundtrip PASS\n", maximal.size());
    ++scenarios;g_snapshot=fixture;g_snapshot_ok=true;g_snapshot.liczba_kontrolerow=2;
    g_snapshot.kontrolery[1]=g_snapshot.kontrolery[0];g_snapshot.kontrolery[1].vendor_id=0x10de;
    g_snapshot.kontrolery[1].device_id=0x0fb9;g_snapshot.kontrolery[1].test_dostepny=0;
    window.odswiez_wszystko();CHECK(window.kontrolery_->czy_widoczna());
    g_kontroler=1;window.odswiez_wszystko();CHECK(std::strstr(window.tekst_kontroler_->tekst(),"0x10DE"));
    CHECK(std::strstr(window.info_->tekst(),"niedostępny"));
    CHECK(!window.przyciski_testu_[0]->czy_wlaczona());
    g_snapshot.kontrolery[1].gotowy=0;g_snapshot.kontrolery[1].etap_bledu=15;
    window.odswiez_wszystko();CHECK(std::strstr(window.info_->tekst(),"CORB"));
    g_kontroler=0;g_snapshot=fixture;window.odswiez_wszystko();
    ++scenarios; fixture.widgety[0][0][0][0].liczba_polaczen = 255; CHECK(!diag_snapshot(&g_snapshot));
    fixture.widgety[0][0][0][0].liczba_polaczen = 0; fixture.liczba_sciezek = HDA_DIAG_MAKS_SCIEZEK + 1;
    CHECK(!diag_snapshot(&g_snapshot)); fixture.liczba_sciezek = 0;
    fixture.rozmiar = 0; CHECK(!diag_snapshot(&g_snapshot));
    std::printf("Test dzwieku GUI/font/report: %u scenarios, %u checks PASS; ASan+UBSan\n", scenarios, checks);
}
