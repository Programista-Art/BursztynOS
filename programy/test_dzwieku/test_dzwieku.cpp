/*
 * Bursztyn OS - aplikacja "Test dźwięku" (.cebula)
 *
 * Diagnostyka Intel High Definition Audio:
 *   kontroler, kodeki, widgety, ścieżki audio, testy tonów, strumień, raport.
 *
 * Ring 3 nie ma dostępu do MMIO ani surowych verbów HDA.
 * Cały dostęp do sprzętu idzie przez BWS 89/90 (kernel-owned).
 */

#define BURSZTYN_UI_CZCIONKA_SYSTEMOWA 1
#include "../../bursztyn_gui.h"
#include "../../loader.h"
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "../../bws_hda_diag.h"
#include "../../plik_status.h"
#include "../../test_dzwieku_trace.h"
#include "../../wersja_systemu.h"
#include "../../sterowniki/dzwiek/hda_diag_format.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

using namespace BursztynUI;

extern "C" void _start();

/* The linker emits the exact BUR v1 header from final section sizes. */

namespace {

void test_trace(const char* stage) {
#if BURSZTYNDIAGTESTDZWIEKU
    char msg[96] = "[TEST DZWIEKU] ";
    size_t p = 15;
    while (*stage && p + 1 < sizeof(msg)) msg[p++] = *stage++;
    msg[p] = 0;
    wypisz(msg);
#else
    (void)stage;
#endif
}

/* ------------------------------------------------------------------------- */
/* Stan aplikacji                                                            */
/* ------------------------------------------------------------------------- */

HdaDiagSnapshot g_snapshot;
uint32_t g_kontroler = 0;
bool g_snapshot_ok = false;
uint32_t g_generacja = 0;
HdaDiagSkan g_stan_skan{};
char g_raport[HDA_DIAG_MAKS_RAPORTU];
bool g_raport_obciety = false;

const char* SCIEZKA_RAPORTU = "/dane/diagnostyka/hda.txt";

/* ------------------------------------------------------------------------- */
/* Pomocnicze formatowanie                                                   */
/* ------------------------------------------------------------------------- */

void dopisz_str(char* buf, size_t cap, size_t* pos, const char* s) {
    if (!buf || !pos || !s) return;
    for (size_t i = 0; s[i];) {
        uint32_t cp = 0;
        const size_t n = CzcionkaUI::dekoduj_utf8(s, i, &cp);
        if (*pos >= cap || n >= cap - *pos) break;
        for (size_t j = 0; j < n; ++j) buf[(*pos)++] = s[i++];
    }
    if (*pos < cap) buf[*pos] = '\0';
}

void dopisz_liczbe(char* buf, size_t cap, size_t* pos, uint64_t v, uint8_t baza = 10) {
    char tmp[24];
    int i = 0;
    if (baza < 2 || baza > 16) baza = 10;
    if (v == 0) { dopisz_str(buf, cap, pos, "0"); return; }
    const char* cyfry = "0123456789ABCDEF";
    while (v > 0 && i < 23) { tmp[i++] = cyfry[v % baza]; v /= baza; }
    while (i > 0) {
        if (*pos + 1 >= cap) break;
        buf[(*pos)++] = tmp[--i];
    }
    if (*pos < cap) buf[*pos] = '\0';
}

void dopisz_hex8(char* buf, size_t cap, size_t* pos, uint8_t v) {
    char tmp[3];
    tmp[0] = "0123456789ABCDEF"[(v >> 4) & 0xF];
    tmp[1] = "0123456789ABCDEF"[v & 0xF];
    tmp[2] = '\0';
    dopisz_str(buf, cap, pos, "0x");
    dopisz_str(buf, cap, pos, tmp);
}

void dopisz_hex32(char* buf, size_t cap, size_t* pos, uint32_t v) {
    char tmp[9];
    for (int i = 0; i < 8; ++i) {
        tmp[i] = "0123456789ABCDEF"[(v >> (28 - i * 4)) & 0xF];
    }
    tmp[8] = '\0';
    dopisz_str(buf, cap, pos, "0x");
    dopisz_str(buf, cap, pos, tmp);
}

[[maybe_unused]] static const char* nazwa_etapu(uint32_t etap) {
    switch (etap) {
        case HDA_DIAG_ETAP_BEZCZYNNY: return "Bezczynny";
        case HDA_DIAG_ETAP_KONTROLER: return "Skanowanie kontrolera";
        case HDA_DIAG_ETAP_KODEK: return "Skanowanie kodeków";
        case HDA_DIAG_ETAP_FG: return "Skanowanie grup funkcyjnych";
        case HDA_DIAG_ETAP_WIDGETY: return "Skanowanie widgetów";
        case HDA_DIAG_ETAP_SCIEZKI: return "Budowanie ścieżek";
        case HDA_DIAG_ETAP_GOTOWY: return "Gotowe";
        case HDA_DIAG_ETAP_BLAD: return "Błąd skanu";
        default: return "Nieznany";
    }
}

const char* nazwa_bledu(uint32_t blad) {
    switch (blad) {
        case HDA_DIAG_OK: return "OK";
        case HDA_DIAG_BLAD_BRAK_KONTROLERA: return "Brak kontrolera HDA";
        case HDA_DIAG_BLAD_MMIO: return "Nie udało się odczytać kontrolera";
        case HDA_DIAG_BLAD_RESET: return "Błąd resetu kontrolera";
        case HDA_DIAG_BLAD_BRAK_KODEKA: return "Brak odpowiedzi kodeka";
        case HDA_DIAG_BLAD_KODEK_NIE_ODPOWIADA: return "Brak odpowiedzi kodeka";
        case HDA_DIAG_BLAD_ZLY_ZAKRES_WEZLOW: return "Nieprawidłowy zakres węzłów";
        case HDA_DIAG_BLAD_LISTA_POLACZEN: return "Nieprawidłowa lista połączeń";
        case HDA_DIAG_BLAD_BRAK_AFG: return "Brak Audio Function Group";
        case HDA_DIAG_BLAD_BRAK_WYJSC: return "Brak wykrytego wyjścia audio";
        case HDA_DIAG_BLAD_BRAK_GLOSNIKA: return "Brak wyjścia Głośniki";
        case HDA_DIAG_BLAD_STRUMIEN: return "Błąd uruchamiania strumienia";
        case HDA_DIAG_BLAD_TIMEOUT_ODTWORZENIA: return "Upłynął czas odtwarzania";
        case HDA_DIAG_BLAD_NIEOBSLUGIWANY_SPRZET: return "Nieobsługiwany sprzęt";
        case HDA_DIAG_BLAD_PRZEKROCZENIE_LIMITU: return "Migawka niepełna (limit ABI)";
        case HDA_DIAG_BLAD_ZLA_ODPOWIEDZ: return "Nieprawidłowa odpowiedź kodeka";
        case HDA_DIAG_BLAD_TIMEOUT: return "Przekroczony czas odpowiedzi";
        default: return "Inny błąd";
    }
}

const char* nazwa_stanu_strumienia(uint32_t s) {
    switch (s) {
        case HDA_STRUMIEN_BEZCZYNNY: return "bezczynny";
        case HDA_STRUMIEN_SKONFIGUROWANY: return "skonfigurowany";
        case HDA_STRUMIEN_GRA: return "gra";
        case HDA_STRUMIEN_ZATRZYMANY: return "zatrzymany";
        case HDA_STRUMIEN_BLAD: return "błąd";
        default: return "nieznany";
    }
}

/* ------------------------------------------------------------------------- */
/* BWS wrapper diagnostyki                                                   */
/* ------------------------------------------------------------------------- */

bool diag_stan(HdaDiagSkan* out) {
    return out && bws_wywolaj(BWS_HDA_DIAG, BWS_HDA_DIAG_STAN,
                              reinterpret_cast<uint64_t>(out), sizeof(HdaDiagSkan), HDA_DIAG_WERSJA_ABI) == 1;
}

bool diag_skan_start() {
    return bws_wywolaj(BWS_HDA_DIAG, BWS_HDA_DIAG_SKAN_START, 0, 0, 0) == 1;
}

bool diag_skan_krok() {
    return bws_wywolaj(BWS_HDA_DIAG, BWS_HDA_DIAG_SKAN_KROK, 0, 0, 0) == 1;
}

bool diag_skan_anuluj() {
    return bws_wywolaj(BWS_HDA_DIAG, BWS_HDA_DIAG_SKAN_ANULUJ, 0, 0, 0) == 1;
}

bool diag_snapshot(HdaDiagSnapshot* out) {
    if (!out) return false;
    BwsHdaSnapshotZadanie zad{};
    zad.wersja = HDA_DIAG_WERSJA_ABI;
    zad.rozmiar = sizeof(BwsHdaSnapshotZadanie);
    zad.bufor = reinterpret_cast<uint64_t>(out);
    zad.bufor_maks = sizeof(HdaDiagSnapshot);
    if (bws_wywolaj(BWS_HDA_DIAG, BWS_HDA_DIAG_SNAPSHOT,
                    reinterpret_cast<uint64_t>(&zad), 0, 0) != 1) {
        return false;
    }
    if (zad.zapisano != sizeof(HdaDiagSnapshot) ||
        out->wersja != HDA_DIAG_WERSJA_ABI || out->rozmiar != sizeof(*out) ||
        out->liczba_kontrolerow > HDA_DIAG_MAKS_KONTROLEROW ||
        out->liczba_sciezek > HDA_DIAG_MAKS_SCIEZEK) return false;
    for(uint32_t k=0;k<out->liczba_kontrolerow;++k){
        if(out->kontrolery[k].liczba_kodekow>HDA_DIAG_MAKS_KODEKOW)return false;
        for(uint32_t c=0;c<HDA_DIAG_MAKS_KODEKOW;++c)
            if(out->kodeki[k][c].liczba_fg>HDA_DIAG_MAKS_FG)return false;
    }
    for (uint32_t k = 0; k < HDA_DIAG_MAKS_KONTROLEROW; ++k)
        for (uint32_t c = 0; c < HDA_DIAG_MAKS_KODEKOW; ++c)
            for (uint32_t f = 0; f < HDA_DIAG_MAKS_FG; ++f)
                for (uint32_t w = 0; w < HDA_DIAG_MAKS_WIDGETOW; ++w)
                    if (out->widgety[k][c][f][w].liczba_polaczen > HDA_DIAG_MAKS_POLACZEN || out->widgety[k][c][f][w].liczba_slow_polaczen>64)
                        return false;
    for (uint32_t i = 0; i < out->liczba_sciezek; ++i)
        if (out->sciezki[i].dlugosc > HDA_DIAG_MAKS_DLUGOSC_SCIEZKI) return false;
    g_generacja = zad.generacja;
    return true;
}

bool test_start(uint32_t tryb, uint32_t hz, uint32_t czas_ms, uint8_t kanaly) {
    BwsHdaTestStart t{};
    t.wersja = HDA_DIAG_WERSJA_ABI;
    t.rozmiar = sizeof(BwsHdaTestStart);
    t.tryb = tryb;
    t.czestotliwosc_hz = hz;
    t.czas_ms = czas_ms;
    t.kanaly = kanaly;
    return bws_wywolaj(BWS_HDA_TEST, BWS_HDA_TEST_START,
                       reinterpret_cast<uint64_t>(&t), g_kontroler, HDA_DIAG_WERSJA_ABI) == 1;
}

void test_stop() {
    (void)bws_wywolaj(BWS_HDA_TEST, BWS_HDA_TEST_STOP, 0, 0, 0);
}

/* ------------------------------------------------------------------------- */
/* Raport tekstowy                                                           */
/* ------------------------------------------------------------------------- */

void zbuduj_raport(char* out, size_t cap, bool* obciety) {
    HdaRaportBufor rb;
    hda_raport_init(&rb, out, static_cast<uint32_t>(cap));
    char wersja[32];
    size_t p = 0;
    dopisz_str(wersja, sizeof(wersja), &p, "");
    dopisz_liczbe(wersja, sizeof(wersja), &p, BURSZTYN_WERSJA_MAJOR);
    dopisz_str(wersja, sizeof(wersja), &p, ".");
    dopisz_liczbe(wersja, sizeof(wersja), &p, BURSZTYN_WERSJA_MINOR);
    dopisz_str(wersja, sizeof(wersja), &p, ".");
    dopisz_liczbe(wersja, sizeof(wersja), &p, BURSZTYN_WERSJA_PATCH);

    if (g_snapshot_ok) {
        if (g_stan_skan.aktywny) {
            hda_raport_linia(&rb, "[UWAGA: Skan w toku - raport zawiera poprzednia kompletna migawke]");
            hda_raport_klucz_wartosc(&rb, "Biezacy etap skanu", hda_diag_etap_nazwa(g_stan_skan.etap));
        }
        (void)hda_sformatuj_raport(&g_snapshot, wersja, &rb);
    } else {
        hda_raport_linia(&rb, "=== Raport diagnostyczny HDA ===");
        hda_raport_linia(&rb, "Brak migawki. Użyj Odśwież raport.");
        hda_raport_klucz_wartosc(&rb, "Stan", hda_diag_etap_nazwa(g_stan_skan.etap));
        hda_raport_klucz_wartosc(&rb, "Błąd", nazwa_bledu(g_stan_skan.blad));
    }
    if (obciety) *obciety = rb.obciety;
}

uint64_t g_plik_bws = 0;
uint64_t g_plik_kod = 0;
bool zapisz_raport() {
    if (!g_snapshot_ok) { g_plik_bws=89; g_plik_kod=HDA_DIAG_BLAD_BRAK_SNAPSHOTU; return false; }
    zbuduj_raport(g_raport, sizeof(g_raport), &g_raport_obciety);
    if (g_raport_obciety) { g_plik_bws = 0; g_plik_kod = 12; return false; }
    const char* dirs[] = {"/dane", "/dane/diagnostyka"};
    for (const char* dir : dirs) {
        g_plik_bws = BWS_UTWORZ_KATALOG;
        g_plik_kod = bws_wywolaj(g_plik_bws, reinterpret_cast<uint64_t>(dir),
                                 BWS_STATUS_PLIKOWY, 0, 0);
        if (g_plik_kod != uint64_t(WynikPlikowy::Ok) &&
            g_plik_kod != uint64_t(WynikPlikowy::Istnieje)) return false;
        BwsMetadanePliku meta{};
        if (!pobierz_metadane_pliku(dir, &meta) || meta.typ != BWS_TYP_KATALOG)
            { g_plik_bws = BWS_METADANE_PLIKU; g_plik_kod = 0; return false; }
    }
    g_plik_bws = BWS_UTWORZ_PLIK;
    g_plik_kod = bws_wywolaj(g_plik_bws, reinterpret_cast<uint64_t>(SCIEZKA_RAPORTU),
                             BWS_STATUS_PLIKOWY, 0, 0);
    if (g_plik_kod != uint64_t(WynikPlikowy::Ok) &&
        g_plik_kod != uint64_t(WynikPlikowy::Istnieje)) return false;
    size_t n = 0;
    while (n < sizeof(g_raport) && g_raport[n]) ++n;
    if (n == sizeof(g_raport)) { g_plik_bws = 0; g_plik_kod = 12; return false; }
    g_plik_bws = BWS_ZAPISZ_PLIK;
    g_plik_kod = bws_wywolaj(g_plik_bws, reinterpret_cast<uint64_t>(SCIEZKA_RAPORTU),
                             reinterpret_cast<uint64_t>(g_raport), n, 0);
    if (g_plik_kod != 1) return false;
    BwsMetadanePliku meta{};
    if (!pobierz_metadane_pliku(SCIEZKA_RAPORTU, &meta) ||
        !(meta.flagi & BWS_META_ROZMIAR_DOSTEPNY) || meta.rozmiar != n)
        { g_plik_bws = BWS_METADANE_PLIKU; g_plik_kod = 0; return false; }
    return true;
}

} // namespace

/* ========================================================================= */
/* Okno aplikacji                                                            */
/* ========================================================================= */

namespace {

/* Cached UTF-8 rows, rewrapped only when text or available glyph width changes.
 * Scrolling paints visible rows through the standard themed virtual list. */
class WidokRaportu : public ListaWirtualna, private DostawcaDanychListy {
public:
    explicit WidokRaportu(size_t cap) : cap_(cap) {
        tekst_ = new char[cap_]; tekst_[0] = 0;
        rows_ = new Wiersz[MAKS_WIERSZY];
        ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
        ustaw_dostawce(this);
    }
    ~WidokRaportu() override { delete[] rows_; delete[] tekst_; }
    void ustaw_tekst(const char* t) override {
        size_t n = 0;
        while (t && t[n] && n + 1 < cap_) { tekst_[n] = t[n]; ++n; }
        tekst_[n] = 0;
        obciety_ = t && t[n];
        indeksuj();
    }
    const char* tekst() const override { return tekst_; }
    void rozmiesc(Prostokat r) override {
        ListaWirtualna::rozmiesc(r);
        const int w = r.szerokosc - 28 * skala_dpi_;
        const uint32_t scale = skala_dpi_.procent();
        if (w != width_ || scale != scale_) { width_ = w; scale_ = scale; indeksuj(); }
    }
    bool czy_obciety() const { return obciety_; }
private:
    struct Wiersz { uint32_t start, end; };
    static constexpr size_t MAKS_WIERSZY = 16384;
    void indeksuj() {
        count_ = 0;
        size_t p = 0;
        while (tekst_[p] && count_ < MAKS_WIERSZY - 1) {
            const size_t start = p;
            int width = 0;
            while (tekst_[p] && tekst_[p] != '\n') {
                uint32_t cp = 0;
                const size_t n = CzcionkaUI::dekoduj_utf8(tekst_, p, &cp);
                const int advance = CzcionkaUI::postep_znaku(cp, skala_dpi_);
                if (p != start && (width + advance > width_ || p - start + n > 480)) break;
                width += advance; p += n;
            }
            rows_[count_++] = {static_cast<uint32_t>(start), static_cast<uint32_t>(p)};
            if (tekst_[p] == '\n') ++p;
        }
        if (tekst_[p]) { obciety_ = true; rows_[count_++] = {UINT32_MAX, 0}; }
        ustaw_dostawce(this);
    }
    size_t liczba_elementow() override { return count_; }
    const char* pobierz_tekst(size_t i, char* out, size_t cap) override {
        if (!cap || i >= count_) return "";
        const Wiersz r = rows_[i];
        if (r.start == UINT32_MAX) return "Widok przekracza limit wierszy; pełna treść jest w raporcie.";
        size_t p = r.start, n = 0;
        while (p < r.end) {
            uint32_t cp = 0;
            const size_t bytes = CzcionkaUI::dekoduj_utf8(tekst_, p, &cp);
            if (n + bytes >= cap) break;
            for (size_t j = 0; j < bytes; ++j) out[n++] = tekst_[p++];
        }
        out[n] = 0;
        return out;
    }
    char* tekst_{nullptr}; Wiersz* rows_{nullptr};
    size_t cap_, count_{0}; int width_{480}; uint32_t scale_{100}; bool obciety_{false};
};

class OknoTestDzwieku;


class ZegarSkanu : public Kontrolka {
public:
    void ustaw_okno(OknoTestDzwieku* o) { okno_ = o; }
private:
    bool czy_wymaga_animacji() const override;
    void krok_animacji(uint64_t teraz_ns = 0) override;
    void rysuj_siebie(KontekstRysowania&) override {}
private:
    OknoTestDzwieku* okno_{nullptr};
};

class OknoTestDzwieku : public Okno {
public:
    OknoTestDzwieku() : Okno("Test dźwięku", 980, 620) {}
    ~OknoTestDzwieku() override {
        if (skan_w_toku_) {
            diag_skan_anuluj();
            skan_w_toku_ = false;
        }
        if (wskaznik_) {
            wskaznik_->stop();
        }
        test_stop();
        // Okno owns its default Panel; a replacement root is app-owned.
        zarzadca_popupow().zamknij(PowodZamknieciaPopup::WlascicielZamkniety);
        auto* root = kontrolka_glowna();
        ustaw_kontrolke_glowna(nullptr);
        delete root;
    }
    void zaktualizuj_motyw() override {
        Okno::zaktualizuj_motyw();
        if (info_) info_->ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI));
    }

    WskaznikZajetosci* wskaznik() const { return wskaznik_; }

    void inicjalizuj() {
        test_trace("BursztynUI initialized");
        PanelDokowany& root = dodaj<PanelDokowany>();
        root.ustaw_wypelnienie(Marginesy(8)); root.ustaw_odstep(6);
        Siatka& header = root.dodaj<Siatka>(Dokowanie::Gora);
        header.ustaw_wypelnienie(Marginesy(0)); header.ustaw_odstep(6);
        header.ustaw_preferowany_rozmiar(Rozmiar(-1, 36));
        header.dodaj_wiersz(DefinicjaWymiaru::piksele(36));
        header.dodaj_kolumne(DefinicjaWymiaru::gwiazdka(1));
        header.dodaj_kolumne(DefinicjaWymiaru::automatyczny());

        status_ = new PasekInformacyjny(TypKomunikatu::Informacja, "Łączenie z diagnostyką HDA");
        status_->ustaw_przycisk_zamkniecia(false);
        status_->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        status_->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        header.dodaj_do_siatki(status_, 0, 0);

        wskaznik_ = new WskaznikZajetosci(false);
        wskaznik_->ustaw_preferowany_rozmiar(Rozmiar(32, 32));
        wskaznik_->ustaw_wyrownanie_poziome(WyrownaniePoziome::Srodek);
        wskaznik_->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Srodek);
        wskaznik_->ustaw_widoczna(false);
        header.dodaj_do_siatki(wskaznik_, 0, 1);

        kontrolery_ = &root.dodaj<ListaRozwijana>(Dokowanie::Gora);
        kontrolery_->ustaw_preferowany_rozmiar(Rozmiar(-1, 34));
        kontrolery_->ustaw_widoczna(false);
        kontrolery_->przy_zmianie_wyboru([](ListaRozwijana*, int i, void* ctx) {
            if (i < 0 || !g_snapshot_ok || uint32_t(i) >= g_snapshot.liczba_kontrolerow) return;
            g_kontroler=uint32_t(i);
            static_cast<OknoTestDzwieku*>(ctx)->odswiez_wszystko();
        }, this);

        sekcje_ = &root.dodaj<ListaRozwijana>(Dokowanie::Gora);
        sekcje_->ustaw_preferowany_rozmiar(Rozmiar(-1, 34));
        const char* names[] = {"Kontroler", "Kodeki", "Widgety", "Ścieżki audio",
                                "Testy", "Strumień", "Raport"};
        for (const char* n : names) sekcje_->dodaj_element(n);
        sekcje_->ustaw_wybrany_indeks(0);
        sekcje_->przy_zmianie_wyboru([](ListaRozwijana*, int i, void* ctx) {
            static_cast<OknoTestDzwieku*>(ctx)->pokaz_sekcje(i);
        }, this);
        Siatka& akcje = root.dodaj<Siatka>(Dokowanie::Dol);
        akcje.ustaw_wypelnienie(Marginesy(0)); akcje.ustaw_odstep(8);
        akcje.ustaw_preferowany_rozmiar(Rozmiar(-1, 38));
        akcje.dodaj_wiersz();
        for (int i = 0; i < 3; ++i) akcje.dodaj_kolumne();
        btn_odswiez_ = new Przycisk("Odśwież");
        btn_zapisz_ = new Przycisk("Zapisz raport");
        btn_stop_ = new Przycisk("Stop");
        Przycisk* buttons[] = {btn_odswiez_, btn_zapisz_, btn_stop_};
        for (int i = 0; i < 3; ++i) {
            buttons[i]->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
            buttons[i]->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
            akcje.dodaj_do_siatki(buttons[i], 0, i);
        }
        btn_odswiez_->przy_kliknieciu([](Przycisk*, void* d) {
            static_cast<OknoTestDzwieku*>(d)->rozpocznij_skan();
        }, this);
        btn_zapisz_->przy_kliknieciu([](Przycisk*, void* d) {
            auto* o = static_cast<OknoTestDzwieku*>(d);
            if (zapisz_raport()) o->ustaw_status("Raport zapisany: /dane/diagnostyka/hda.txt", TypKomunikatu::Sukces);
            else o->blad_bws("Zapis raportu", g_plik_bws, g_plik_kod);
        }, this);
        btn_stop_->przy_kliknieciu([](Przycisk*, void* d) {
            auto* o = static_cast<OknoTestDzwieku*>(d);
            const uint64_t rc = bws_wywolaj(BWS_HDA_TEST, BWS_HDA_TEST_STOP, 0, 0, 0);
            if (rc != 1) o->blad_bws("Stop", BWS_HDA_TEST, rc);
            else { o->ton_aktywny_ = false; o->ustaw_status("Test zatrzymany"); o->odswiez_strumien(); }
        }, this);
        zegar_ = &root.dodaj<ZegarSkanu>(Dokowanie::Gora);
        zegar_->ustaw_okno(this); zegar_->ustaw_preferowany_rozmiar(Rozmiar(0, 0));
        Siatka& content = root.dodaj<Siatka>(Dokowanie::Wypelnij);
        content.ustaw_wypelnienie(Marginesy(0));
        content.dodaj_wiersz(); content.dodaj_kolumne();
        WidokRaportu** views[] = {&tekst_kontroler_, &tekst_kodeki_, &tekst_widgety_,
            &tekst_sciezki_, nullptr, &tekst_strumien_, &tekst_raport_};
        const size_t capacities[] = {8192, 16384, HDA_DIAG_MAKS_RAPORTU, 8192, 0, 8192, HDA_DIAG_MAKS_RAPORTU};
        for (int i = 0; i < 7; ++i) if (views[i]) {
            *views[i] = new WidokRaportu(capacities[i]);
            pages_[i] = *views[i];
            (*views[i])->ustaw_tekst("Dane niedostępne - trwa łączenie z diagnostyką HDA.");
            content.dodaj_do_siatki(pages_[i], 0, 0);
        }
        Panel* tests = new Panel(Orientacja::Pionowa);
        tests->ustaw_wypelnienie(Marginesy(6)); tests->ustaw_odstep(8);
        info_ = &tests->dodaj<Etykieta>("Głośniki - wybierz test. Czas: 1,5 s. Dźwięk nie uruchamia się automatycznie.");
        info_->ustaw_zawijanie(true);
        info_->ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI));
        info_->ustaw_preferowany_rozmiar(Rozmiar(-1, 60));
        Siatka& tones = tests->dodaj<Siatka>();
        tones.ustaw_wypelnienie(Marginesy(0)); tones.ustaw_odstep(8);
        tones.ustaw_preferowany_rozmiar(Rozmiar(-1, 88));
        for (int i = 0; i < 2; ++i) tones.dodaj_wiersz();
        for (int i = 0; i < 3; ++i) tones.dodaj_kolumne();
        dodaj_przycisk_testu(tones, "440 Hz", HDA_TEST_TON, HDA_TEST_TON_A4_HZ, 3);
        dodaj_przycisk_testu(tones, "1000 Hz", HDA_TEST_TON, HDA_TEST_TON_1K_HZ, 3);
        dodaj_przycisk_testu(tones, "Krótki sweep", HDA_TEST_SWEEP, HDA_TEST_SWEEP_MIN_HZ, 3);
        dodaj_przycisk_testu(tones, "Lewy kanał", HDA_TEST_LEWY, HDA_TEST_TON_A4_HZ, 1);
        dodaj_przycisk_testu(tones, "Prawy kanał", HDA_TEST_PRAWY, HDA_TEST_TON_A4_HZ, 2);
        dodaj_przycisk_testu(tones, "Oba kanały", HDA_TEST_OBA, HDA_TEST_TON_A4_HZ, 3);
        pages_[4] = tests; content.dodaj_do_siatki(tests, 0, 0);
        pokaz_sekcje(0);
        test_trace("window created");
        // No HDA call before AplikacjaGUI has submitted the first visible frame.
    }

    void pokaz_sekcje(int i) {
        if (i < 0 || i >= 7) return;
        for (int j = 0; j < 7; ++j) if (pages_[j]) pages_[j]->ustaw_widoczna(i == j);
        przelicz_uklad();
    }
    void blad_bws(const char* operacja, uint64_t nr, uint64_t rc) {
        char msg[160]{}; size_t p = 0;
        dopisz_str(msg, sizeof(msg), &p, "Błąd: "); dopisz_str(msg, sizeof(msg), &p, operacja);
        dopisz_str(msg, sizeof(msg), &p, " - BWS "); dopisz_liczbe(msg, sizeof(msg), &p, nr);
        dopisz_str(msg, sizeof(msg), &p, " = "); dopisz_liczbe(msg, sizeof(msg), &p, rc);
        ustaw_status(msg, TypKomunikatu::Blad);
    }

    void rozpocznij_skan() {
        if (!polaczony_) {
            if (!diag_stan(&g_stan_skan)) { blad_bws("Diagnostyka HDA", 89, 0); return; }
            polaczony_ = true;
        }
        if (skan_w_toku_) {
            diag_skan_anuluj();
            skan_w_toku_ = false;
        }
        if (!diag_skan_start()) {
            if (diag_stan(&g_stan_skan)) ustaw_status(nazwa_bledu(g_stan_skan.blad), TypKomunikatu::Blad);
            else blad_bws("Skan HDA", 89, 0);
            if (wskaznik_) {
                wskaznik_->stop();
                wskaznik_->ustaw_widoczna(false);
            }
            odswiez_wszystko();
            return;
        }
        if (!diag_stan(&g_stan_skan)) { blad_bws("Stan skanu", 89, 0); return; }
        zadana_generacja_ = g_stan_skan.generacja_skanu;
        skan_w_toku_ = true;
        timer_ = 1;
        if (wskaznik_) {
            wskaznik_->ustaw_widoczna(true);
            wskaznik_->start();
        }
        zaktualizuj_postep();
    }
    void tick_skanu() {
        if (!pierwsza_klatka_) {
            pierwsza_klatka_ = true;
            test_trace("first frame published");
            if (!diag_stan(&g_stan_skan)) {
                blad_bws("Łączenie z diagnostyką HDA", 89, 0);
                odswiez_wszystko(); return;
            }
            polaczony_ = true;
            test_trace("HDA diagnostic ABI connected");
            rozpocznij_skan();
        }
        if (skan_w_toku_) {
            const bool koniec = diag_skan_krok();
            if (!diag_stan(&g_stan_skan)) {
                skan_w_toku_ = false;
                timer_ = 0;
                if (wskaznik_) {
                    wskaznik_->stop();
                    wskaznik_->ustaw_widoczna(false);
                }
                blad_bws("Stan skanu", 89, 0); return;
            }
            if (g_stan_skan.generacja_skanu == zadana_generacja_) {
                zaktualizuj_postep();
                if (!g_stan_skan.aktywny || koniec) {
                    skan_w_toku_ = false;
                    timer_ = 0;
                    if (wskaznik_) {
                        wskaznik_->stop();
                        wskaznik_->ustaw_widoczna(false);
                    }
                    if (g_stan_skan.etap == HDA_DIAG_ETAP_GOTOWY) {
                        g_snapshot_ok = diag_snapshot(&g_snapshot);
                        bool kodek_odpowiada = false;
                        for (uint32_t k = 0; k < g_snapshot.liczba_kontrolerow; ++k)
                            for (uint32_t c = 0; c < HDA_DIAG_MAKS_KODEKOW; ++c)
                                kodek_odpowiada |= g_snapshot.kodeki[k][c].odpowiada != 0;

                        const char* st_tekst = hda_diag_stan_wyjscia_tekst(g_snapshot_ok ? &g_snapshot : nullptr, &g_stan_skan);
                        TypKomunikatu st_typ = TypKomunikatu::Sukces;
                        if (!g_snapshot_ok || g_snapshot.liczba_kontrolerow == 0 || !kodek_odpowiada || g_stan_skan.blad != HDA_DIAG_OK) {
                            st_typ = TypKomunikatu::Blad;
                        }
                        ustaw_status(st_tekst, st_typ);
                        odswiez_wszystko();
                    } else if (g_stan_skan.etap == HDA_DIAG_ETAP_BLAD) {
                        ustaw_status(nazwa_bledu(g_stan_skan.blad), TypKomunikatu::Blad);
                        odswiez_wszystko();
                    }
                }
            }
        }
        if (ton_aktywny_) {
            HdaDiagStanTestu t{};
            if (bws_wywolaj(89, BWS_HDA_DIAG_TEST_STAN, reinterpret_cast<uint64_t>(&t), sizeof(t), HDA_DIAG_WERSJA_ABI) != 1) {
                ton_aktywny_ = false; blad_bws("Stan testu", 89, 0);
            } else if (!t.aktywny) {
                ton_aktywny_ = false; ustaw_status("Gotowe", TypKomunikatu::Sukces); odswiez_strumien();
            }
        }
    }
    void ustaw_status(const char* t, TypKomunikatu typ = TypKomunikatu::Informacja) {
        if (status_) { status_->ustaw_typ_komunikatu(typ); status_->ustaw_tekst(t ? t : ""); }
    }
    bool potrzebuje_ticku() const { return !pierwsza_klatka_ || skan_w_toku_ || timer_ || ton_aktywny_; }


private:
    void dodaj_przycisk_testu(Siatka& rzad, const char* etykieta, uint32_t tryb,
                              uint32_t hz, uint8_t kanaly) {
        Przycisk& b = *new Przycisk(etykieta);
        b.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        b.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        rzad.dodaj_do_siatki(&b, liczba_kontekstow_ / 3, liczba_kontekstow_ % 3);
        /* Callback musi byc statyczny; uzywamy tablicy kontekstow. */
        const int idx = liczba_kontekstow_;
        if (idx >= MAKS_KONTEKSTOW) return;
        przyciski_testu_[idx]=&b;
        konteksty_[idx] = {this, tryb, hz, kanaly};
        ++liczba_kontekstow_;
        b.przy_kliknieciu(
            [](Przycisk*, void* d) {
                auto* k = static_cast<KontekstTestu*>(d);
                if (!k) return;
                char buf[160]{};
                size_t p = 0;
                dopisz_str(buf, sizeof(buf), &p, "Test: ");
                dopisz_str(buf, sizeof(buf), &p,
                           k->tryb == HDA_TEST_SWEEP
                               ? "sweep 200-4000 Hz"
                               : (k->tryb == HDA_TEST_LEWY
                                      ? "lewy kanał"
                                      : (k->tryb == HDA_TEST_PRAWY
                                             ? "prawy kanał"
                                             : (k->tryb == HDA_TEST_OBA
                                                    ? "oba kanały"
                                                    : "ton"))));
                dopisz_str(buf, sizeof(buf), &p, ", Hz=");
                dopisz_liczbe(buf, sizeof(buf), &p, k->hz);
                dopisz_str(buf, sizeof(buf), &p, ", czas=");
                dopisz_liczbe(buf, sizeof(buf), &p, HDA_TEST_DOMYSLNY_CZAS_MS);
                dopisz_str(buf, sizeof(buf), &p, " ms, kanały=");
                dopisz_liczbe(buf, sizeof(buf), &p, k->kanaly);
                dopisz_str(buf, sizeof(buf), &p, "\nŚcieżka: ");
                if (g_snapshot_ok) {
                    dopisz_str(buf, sizeof(buf), &p, "CAD ");
                    dopisz_liczbe(buf, sizeof(buf), &p, g_snapshot.test.codec_cad);
                    dopisz_str(buf, sizeof(buf), &p, ", konwerter ");
                    dopisz_hex8(buf, sizeof(buf), &p, g_snapshot.test.konwerter_nid);
                    dopisz_str(buf, sizeof(buf), &p, ", pin ");
                    dopisz_hex8(buf, sizeof(buf), &p, g_snapshot.test.pin_nid);
                    dopisz_str(buf, sizeof(buf), &p,
                               g_snapshot.test.routing_fizyczny
                                   ? " (routing odkryty)"
                                   : " (routing QEMU, brak auto-routingu fizycznego)");
                } else {
                    dopisz_str(buf, sizeof(buf), &p, "brak migawki");
                }
                test_stop();
                k->okno->ton_aktywny_ = false;
                if (test_start(k->tryb, k->hz, HDA_TEST_DOMYSLNY_CZAS_MS, k->kanaly)) {
                    k->okno->ton_aktywny_ = true;
                    k->okno->odswiez_strumien();
                    dopisz_str(buf, sizeof(buf), &p, "\nTest uruchomiony.");
                } else {
                    k->okno->blad_bws("Test tonu", 90, 0);
                    dopisz_str(buf, sizeof(buf), &p, "\nNie udało się uruchomić testu.");
                }
                k->okno->odswiez_info_testu(buf);
            },
            &konteksty_[idx]);
    }

    void odswiez_info_testu(const char* t) {
        if (info_) info_->ustaw_tekst(t ? t : "");
    }

    void zaktualizuj_postep() {
        char buf[96]{}; size_t p = 0;
        dopisz_str(buf, sizeof(buf), &p, hda_diag_etap_nazwa(g_stan_skan.etap));
        if (g_stan_skan.postep_0_1000 > 0 && g_stan_skan.postep_0_1000 <= 1000) {
            dopisz_str(buf, sizeof(buf), &p, " (");
            dopisz_liczbe(buf, sizeof(buf), &p, g_stan_skan.postep_0_1000 / 10);
            dopisz_str(buf, sizeof(buf), &p, "%)");
        }
        ustaw_status(buf, TypKomunikatu::Informacja);
    }
    void odswiez_strumien() {
        if (!g_snapshot_ok) return;
        const uint64_t stream_rc = bws_wywolaj(89, BWS_HDA_DIAG_STRUMIEN,
                          reinterpret_cast<uint64_t>(&g_snapshot.strumien), sizeof(HdaDiagStrumien), HDA_DIAG_WERSJA_ABI);
        if (stream_rc != 1) {
            g_snapshot.strumien = {};
            blad_bws("Stan strumienia", 89, stream_rc);
        }
        const uint64_t test_rc = bws_wywolaj(89, BWS_HDA_DIAG_TEST_STAN,
                          reinterpret_cast<uint64_t>(&g_snapshot.test), sizeof(HdaDiagStanTestu), HDA_DIAG_WERSJA_ABI);
        if (test_rc != 1) {
            g_snapshot.test = {};
            blad_bws("Stan testu", 89, test_rc);
        }
        odswiez_wszystko();
    }

    void odswiez_raport() {
        zbuduj_raport(g_raport, sizeof(g_raport), &g_raport_obciety);
        if (tekst_raport_) {
            tekst_raport_->ustaw_tekst(g_raport);
        }
    }

    void odswiez_wszystko() {
        const bool playable=g_snapshot_ok&&g_kontroler<g_snapshot.liczba_kontrolerow&&g_snapshot.kontrolery[g_kontroler].test_dostepny;
        for(int i=0;i<liczba_kontekstow_;++i)if(przyciski_testu_[i])przyciski_testu_[i]->ustaw_wlaczona(playable);
        if (g_snapshot_ok && kontrolery_ && liczba_kontrolerow_ui_ != g_snapshot.liczba_kontrolerow) {
            liczba_kontrolerow_ui_ = g_snapshot.liczba_kontrolerow;
            kontrolery_->wyczysc();
            for(uint32_t k=0;k<g_snapshot.liczba_kontrolerow;++k){
                char name[96]{};size_t p=0;const auto& c=g_snapshot.kontrolery[k];
                dopisz_str(name,sizeof(name),&p,c.vendor_id==0x8086?"Intel HDA ":c.vendor_id==0x10de?"NVIDIA HDA ":"HDA ");
                dopisz_liczbe(name,sizeof(name),&p,c.vendor_id,16);dopisz_str(name,sizeof(name),&p," ");
                dopisz_liczbe(name,sizeof(name),&p,c.device_id,16);dopisz_str(name,sizeof(name),&p," [");
                dopisz_liczbe(name,sizeof(name),&p,c.bus,16);dopisz_str(name,sizeof(name),&p,":");
                dopisz_liczbe(name,sizeof(name),&p,c.slot,16);dopisz_str(name,sizeof(name),&p,".");
                dopisz_liczbe(name,sizeof(name),&p,c.func);dopisz_str(name,sizeof(name),&p,"]");
                kontrolery_->dodaj_element(name);
            }
            if(g_kontroler>=g_snapshot.liczba_kontrolerow)g_kontroler=0;
            kontrolery_->ustaw_wybrany_indeks(int(g_kontroler));
            kontrolery_->ustaw_widoczna(g_snapshot.liczba_kontrolerow>1);
        }
        if (!g_snapshot_ok) {
            if (tekst_kontroler_)
                tekst_kontroler_->ustaw_tekst("Dane niedostępne - użyj Odśwież.");
            WidokRaportu* views[] = {tekst_kodeki_, tekst_widgety_, tekst_sciezki_, tekst_strumien_};
            for (auto* v : views) if (v) v->ustaw_tekst("Dane niedostępne - użyj Odśwież.");
            odswiez_raport();
            return;
        }

        /* Kontroler */
        if (tekst_kontroler_) {
            char buf[2048]{};
            size_t p = 0;
            for (uint32_t k = g_kontroler; k < g_kontroler+1 && k < g_snapshot.liczba_kontrolerow &&
                                k < HDA_DIAG_MAKS_KONTROLEROW; ++k) {
                const HdaDiagKontroler& c = g_snapshot.kontrolery[k];
                dopisz_str(buf, sizeof(buf), &p, "Kontroler HDA ");
                dopisz_liczbe(buf, sizeof(buf), &p, k);
                char bdf[] = "00:00.0";
                const char* hex = "0123456789ABCDEF";
                bdf[0] = hex[c.bus >> 4]; bdf[1] = hex[c.bus & 15];
                bdf[3] = hex[c.slot >> 4]; bdf[4] = hex[c.slot & 15];
                bdf[6] = hex[c.func & 15];
                dopisz_str(buf, sizeof(buf), &p, "\nPCI BDF ");
                dopisz_str(buf, sizeof(buf), &p, bdf);
                dopisz_str(buf, sizeof(buf), &p, "\nVendor ID 0x");
                dopisz_liczbe(buf, sizeof(buf), &p, c.vendor_id, 16);
                dopisz_str(buf, sizeof(buf), &p, " Device ID 0x");
                dopisz_liczbe(buf, sizeof(buf), &p, c.device_id, 16);
                dopisz_str(buf, sizeof(buf), &p, "\nRewizja ");
                dopisz_liczbe(buf, sizeof(buf), &p, c.revision, 16);
                dopisz_str(buf, sizeof(buf), &p, "\nGCAP ");
                dopisz_hex32(buf, sizeof(buf), &p, c.gcap);
                dopisz_str(buf, sizeof(buf), &p, " GCTL ");
                dopisz_hex32(buf, sizeof(buf), &p, c.gctl);
                dopisz_str(buf, sizeof(buf), &p, "\nSTATESTS teraz ");
                dopisz_hex32(buf, sizeof(buf), &p, c.statests);
                dopisz_str(buf, sizeof(buf), &p, "\nSTATESTS przy starcie ");
                dopisz_hex32(buf, sizeof(buf), &p, c.initial_statests);
                dopisz_str(buf, sizeof(buf), &p, "\nMaska wykryta podczas inicjalizacji ");
                dopisz_hex32(buf, sizeof(buf), &p, c.discovered_codec_mask);
                dopisz_str(buf, sizeof(buf), &p, "\nMaska potwierdzona skanem ");
                dopisz_hex32(buf, sizeof(buf), &p, c.responsive_codec_mask);
                dopisz_str(buf, sizeof(buf), &p, "\nOSS ");
                dopisz_liczbe(buf, sizeof(buf), &p, c.oss);
                dopisz_str(buf, sizeof(buf), &p, " ISS ");
                dopisz_liczbe(buf, sizeof(buf), &p, c.iss);
                dopisz_str(buf, sizeof(buf), &p, " BSS ");
                dopisz_liczbe(buf, sizeof(buf), &p, c.bss);
                dopisz_str(buf, sizeof(buf), &p, " DMA64 ");
                dopisz_liczbe(buf, sizeof(buf), &p, c.dma64);
                dopisz_str(buf, sizeof(buf), &p, "\nWersja HDA ");
                dopisz_liczbe(buf,sizeof(buf),&p,c.hda_vmaj);dopisz_str(buf,sizeof(buf),&p,".");dopisz_liczbe(buf,sizeof(buf),&p,c.hda_vmin);
                dopisz_str(buf,sizeof(buf),&p,"\nStan: ");dopisz_str(buf,sizeof(buf),&p,c.gotowy?"Kontroler zainicjalizowany":"Kontroler wykryty, inicjalizacja nieudana");
                dopisz_str(buf,sizeof(buf),&p,"\nEtap: ");dopisz_str(buf,sizeof(buf),&p,hda_nazwa_etapu_bledu(c.etap_bledu));
                dopisz_str(buf,sizeof(buf),&p,"\nTransport: ");dopisz_str(buf,sizeof(buf),&p,hda_nazwa_transportu(c.transport));
                dopisz_str(buf,sizeof(buf),&p," stan ");dopisz_liczbe(buf,sizeof(buf),&p,c.status_transportu);
                dopisz_str(buf,sizeof(buf),&p,"\nCORB caps ");dopisz_hex32(buf,sizeof(buf),&p,c.corb_caps);
                dopisz_str(buf,sizeof(buf),&p," rozmiar ");dopisz_liczbe(buf,sizeof(buf),&p,c.corb_rozmiar);
                dopisz_str(buf,sizeof(buf),&p," RUN ");dopisz_liczbe(buf,sizeof(buf),&p,c.corb_run);
                dopisz_str(buf,sizeof(buf),&p,"\nRIRB caps ");dopisz_hex32(buf,sizeof(buf),&p,c.rirb_caps);
                dopisz_str(buf,sizeof(buf),&p," rozmiar ");dopisz_liczbe(buf,sizeof(buf),&p,c.rirb_rozmiar);
                dopisz_str(buf,sizeof(buf),&p," RUN ");dopisz_liczbe(buf,sizeof(buf),&p,c.rirb_run);
                dopisz_str(buf,sizeof(buf),&p,"\nBAR ");dopisz_liczbe(buf,sizeof(buf),&p,c.bar_typ);
                dopisz_str(buf,sizeof(buf),&p," bit, MMIO ");dopisz_liczbe(buf,sizeof(buf),&p,c.mmio_zmapowane);
                dopisz_str(buf,sizeof(buf),&p,"\nMaks. faza transportu [ns] ");dopisz_liczbe(buf,sizeof(buf),&p,c.maks_krok_ns);
                dopisz_str(buf,sizeof(buf),&p,"\n");
                dopisz_str(buf, sizeof(buf), &p, "Liczba odpowiadających kodeków: ");
                dopisz_liczbe(buf, sizeof(buf), &p, c.liczba_kodekow);
                dopisz_str(buf, sizeof(buf), &p, "\n\n");
            }
            if (g_snapshot.liczba_kontrolerow == 0) {
                dopisz_str(buf, sizeof(buf), &p, "Nie znaleziono kontrolera HDA.\n");
            }
            if (p >= sizeof(buf) - 1) buf[sizeof(buf) - 1] = '\0';
            tekst_kontroler_->ustaw_tekst(buf);
        }

        /* Kodeki */
        if (tekst_kodeki_) {
            char buf[4096]{};
            size_t p = 0;
            for (uint32_t k = g_kontroler; k < g_kontroler+1 && k < g_snapshot.liczba_kontrolerow &&
                                k < HDA_DIAG_MAKS_KONTROLEROW; ++k) {
                for (uint32_t kd = 0; kd < HDA_DIAG_MAKS_KODEKOW; ++kd) {
                    const HdaDiagKodek& ko = g_snapshot.kodeki[k][kd];
                    if (!ko.odpowiada) continue;
                    dopisz_str(buf, sizeof(buf), &p, "Adres kodeka ");
                    dopisz_liczbe(buf, sizeof(buf), &p, ko.cad);
                    dopisz_str(buf, sizeof(buf), &p, "\nVendor ID 0x");
                    dopisz_liczbe(buf, sizeof(buf), &p, ko.vendor_id, 16);
                    if (ko.vendor_id == 0x1AF4) dopisz_str(buf, sizeof(buf), &p, " (QEMU / Red Hat)");
                    else if (ko.vendor_id == 0x8086) dopisz_str(buf, sizeof(buf), &p, " (Intel)");
                    else if (ko.vendor_id == 0x10EC) dopisz_str(buf, sizeof(buf), &p, " (Realtek)");
                    dopisz_str(buf, sizeof(buf), &p, " Device ID 0x");
                    dopisz_liczbe(buf, sizeof(buf), &p, ko.device_id, 16);
                    dopisz_str(buf, sizeof(buf), &p, "\nRevisionID ");
                    dopisz_hex32(buf, sizeof(buf), &p, ko.revision_id);
                    dopisz_str(buf, sizeof(buf), &p, "\nRootSubCount ");
                    dopisz_hex32(buf, sizeof(buf), &p, ko.root_sub_count);
                    dopisz_str(buf, sizeof(buf), &p, "\nFunction Groups: ");
                    dopisz_liczbe(buf, sizeof(buf), &p, ko.liczba_fg);
                    for (uint32_t f = 0; f < HDA_DIAG_MAKS_FG; ++f) {
                        const HdaDiagFG& g = g_snapshot.fg[k][kd][f];
                        if (g.nid == 0 && g.widget_count == 0) continue;
                        dopisz_str(buf, sizeof(buf), &p, "\nAFG NID ");
                        dopisz_hex8(buf, sizeof(buf), &p, g.nid);
                        dopisz_str(buf, sizeof(buf), &p, " typ ");
                        dopisz_liczbe(buf, sizeof(buf), &p, g.typ);
                        dopisz_str(buf, sizeof(buf), &p, " (Audio Function Group)\nWidget count: ");
                        dopisz_liczbe(buf, sizeof(buf), &p, g.widget_count);
                        dopisz_str(buf, sizeof(buf), &p, " widgety ");
                        dopisz_liczbe(buf, sizeof(buf), &p, g.widget_start);
                        dopisz_str(buf, sizeof(buf), &p, "+");
                        dopisz_liczbe(buf, sizeof(buf), &p, g.widget_count);
                    }
                    dopisz_str(buf, sizeof(buf), &p, "\n\n");
                }
            }
            if (p == 0) dopisz_str(buf, sizeof(buf), &p, "Brak odpowiadających kodeków.\n");
            if (p >= sizeof(buf) - 1) buf[sizeof(buf) - 1] = '\0';
            tekst_kodeki_->ustaw_tekst(buf);
        }

        /* Widgety */
        if (tekst_widgety_) {
            static char buf[HDA_DIAG_MAKS_RAPORTU];
            buf[0] = 0;
            size_t p = 0;
            for (uint32_t k = g_kontroler; k < g_kontroler+1 && k < g_snapshot.liczba_kontrolerow &&
                                k < HDA_DIAG_MAKS_KONTROLEROW; ++k) {
                for (uint32_t kd = 0; kd < HDA_DIAG_MAKS_KODEKOW; ++kd) {
                    for (uint32_t f = 0; f < HDA_DIAG_MAKS_FG; ++f) {
                        for (uint32_t w = 0; w < HDA_DIAG_MAKS_WIDGETOW; ++w) {
                            const HdaDiagWidget& wd =
                                g_snapshot.widgety[k][kd][f][w];
                            if (wd.nid == 0 && wd.awcap == 0) continue;
                            dopisz_str(buf, sizeof(buf), &p, "NID ");
                            dopisz_hex8(buf, sizeof(buf), &p, wd.nid);
                            dopisz_str(buf, sizeof(buf), &p, " ");
                            if (wd.blad) {
                                dopisz_str(buf, sizeof(buf), &p, "Dane niedostępne: ");
                                dopisz_str(buf, sizeof(buf), &p, nazwa_bledu(wd.blad));
                                dopisz_str(buf, sizeof(buf), &p, "\n");
                                continue;
                            }
                            dopisz_str(buf, sizeof(buf), &p,
                                       hda_nazwa_typu_widgetu(wd.typ));
                            dopisz_str(buf, sizeof(buf), &p, " AWCAP ");
                            dopisz_hex32(buf, sizeof(buf), &p, wd.awcap);
                            if (hda_typ_widgetu_pin(wd.typ)) {
                                dopisz_str(buf, sizeof(buf), &p, " PinCaps ");
                                dopisz_hex32(buf, sizeof(buf), &p, wd.pin_caps);
                                dopisz_str(buf, sizeof(buf), &p, " DefCfg ");
                                dopisz_hex32(buf, sizeof(buf), &p, wd.default_config);
                                dopisz_str(buf, sizeof(buf), &p, " ");
                                dopisz_str(buf, sizeof(buf), &p,
                                           hda_nazwa_domyslnego_urzadzenia(
                                               hda_pincfg_dev(wd.default_config)));
                                dopisz_str(buf, sizeof(buf), &p, " / ");
                                dopisz_str(buf, sizeof(buf), &p,
                                           hda_nazwa_lokacji(
                                               hda_pincfg_lokacja(wd.default_config)));
                                dopisz_str(buf, sizeof(buf), &p, " / ");
                                dopisz_str(buf, sizeof(buf), &p,
                                           hda_nazwa_portu(
                                               hda_pincfg_port(wd.default_config)));
                            }
                            if (wd.liczba_polaczen) {
                                dopisz_str(buf, sizeof(buf), &p, " conn:");
                                for (uint8_t ci = 0; ci < wd.liczba_polaczen; ++ci) {
                                    dopisz_str(buf, sizeof(buf), &p, " ");
                                    dopisz_hex8(buf, sizeof(buf), &p, wd.polaczenia[ci]);
                                }
                            }
                            dopisz_str(buf, sizeof(buf), &p, "\n");
                            if (p > sizeof(buf) - 128) {
                                dopisz_str(buf, sizeof(buf), &p, "...(obcięto)\n");
                                break;
                            }
                        }
                    }
                }
            }
            if (p == 0) dopisz_str(buf, sizeof(buf), &p, "Brak widgetów.\n");
            buf[sizeof(buf) - 1] = '\0';
            tekst_widgety_->ustaw_tekst(buf);
        }

        /* Ścieżki */
        if (tekst_sciezki_) {
            char buf[4096]{};
            size_t p = 0;
            if (g_snapshot.liczba_sciezek == 0) {
                dopisz_str(buf, sizeof(buf), &p,
                           "Brak odkrytych ścieżek wyjściowych.\n");
            }
            for (uint32_t s = 0; s < g_snapshot.liczba_sciezek &&
                                s < HDA_DIAG_MAKS_SCIEZEK; ++s) {
                const HdaDiagSciezka& sc = g_snapshot.sciezki[s];
                if(sc.kontroler_idx!=g_kontroler)continue;
                dopisz_str(buf, sizeof(buf), &p, "Ścieżka ");
                dopisz_liczbe(buf, sizeof(buf), &p, s);
                dopisz_str(buf, sizeof(buf), &p, ": konwerter ");
                dopisz_hex8(buf, sizeof(buf), &p, sc.konwerter_nid);
                dopisz_str(buf, sizeof(buf), &p, " -> pin ");
                dopisz_hex8(buf, sizeof(buf), &p, sc.pin_nid);
                dopisz_str(buf, sizeof(buf), &p, " (");
                dopisz_str(buf, sizeof(buf), &p, hda_nazwa_celu_pinu(sc.cel));
                dopisz_str(buf, sizeof(buf), &p, ") hop ");
                for (uint8_t i = 0; i < sc.dlugosc; ++i) {
                    dopisz_hex8(buf, sizeof(buf), &p, sc.nids[i]);
                    dopisz_str(buf, sizeof(buf), &p, " ");
                }
                dopisz_str(buf, sizeof(buf), &p, "\n");
            }
            dopisz_str(buf, sizeof(buf), &p,
                       "\nUwaga: automatyczny routing fizyczny nie jest jeszcze "
                       "dostępny. Raport pokazuje realną topologię.\n");
            buf[sizeof(buf) - 1] = '\0';
            tekst_sciezki_->ustaw_tekst(buf);
        }

        /* Strumień */
        if (tekst_strumien_) {
            char buf[2048]{};
            size_t p = 0;
            const HdaDiagStrumien& s = g_snapshot.strumien;
            dopisz_str(buf, sizeof(buf), &p, "Stan: ");
            dopisz_str(buf, sizeof(buf), &p,
                s.wersja == HDA_DIAG_WERSJA_ABI && s.rozmiar == sizeof(s)
                    ? nazwa_stanu_strumienia(s.stan) : "niedostępny");
            dopisz_str(buf, sizeof(buf), &p, "\nStream tag ");
            dopisz_liczbe(buf, sizeof(buf), &p, s.stream_tag);
            dopisz_str(buf, sizeof(buf), &p, " format ");
            dopisz_hex32(buf, sizeof(buf), &p, s.format_raw);
            dopisz_str(buf, sizeof(buf), &p, "\nRate ");
            dopisz_liczbe(buf, sizeof(buf), &p, s.sample_rate);
            dopisz_str(buf, sizeof(buf), &p, " bits ");
            dopisz_liczbe(buf, sizeof(buf), &p, s.bits);
            dopisz_str(buf, sizeof(buf), &p, " kanały ");
            dopisz_liczbe(buf, sizeof(buf), &p, s.kanaly);
            dopisz_str(buf, sizeof(buf), &p, "\nCBL ");
            dopisz_liczbe(buf, sizeof(buf), &p, s.cbl);
            dopisz_str(buf, sizeof(buf), &p, " LVI ");
            dopisz_liczbe(buf, sizeof(buf), &p, s.lvi);
            dopisz_str(buf, sizeof(buf), &p, " LPIB ");
            dopisz_liczbe(buf, sizeof(buf), &p, s.lpib);
            dopisz_str(buf, sizeof(buf), &p, "\nCTL ");
            dopisz_hex32(buf, sizeof(buf), &p, s.ctl);
            dopisz_str(buf, sizeof(buf), &p, " STS ");
            dopisz_hex32(buf, sizeof(buf), &p, s.sts);
            dopisz_str(buf, sizeof(buf), &p, "\nBCIS ");
            dopisz_liczbe(buf, sizeof(buf), &p, s.bcis);
            dopisz_str(buf, sizeof(buf), &p, " FIFOE ");
            dopisz_liczbe(buf, sizeof(buf), &p, s.fifoe);
            dopisz_str(buf, sizeof(buf), &p, " DESE ");
            dopisz_liczbe(buf, sizeof(buf), &p, s.dese);
            dopisz_str(buf, sizeof(buf), &p, " RUN ");
            dopisz_liczbe(buf, sizeof(buf), &p, s.run);
            dopisz_str(buf, sizeof(buf), &p, "\nBDL wpisy ");
            dopisz_liczbe(buf, sizeof(buf), &p, s.bdl.liczba_wpisow);
            dopisz_str(buf, sizeof(buf), &p, " (adresy zamaskowane)\n");
            dopisz_str(buf, sizeof(buf), &p, "Tone active ");
            dopisz_liczbe(buf, sizeof(buf), &p, s.ton_aktywny);
            dopisz_str(buf, sizeof(buf), &p, "\nTone deadline ns ");
            dopisz_liczbe(buf, sizeof(buf), &p, s.ton_deadline_ns);
            dopisz_str(buf, sizeof(buf), &p, "\nTone stop reason: ");
            dopisz_str(buf, sizeof(buf), &p, hda_ton_stop_nazwa(s.ton_stop_powod));
            dopisz_str(buf, sizeof(buf), &p, "\nStream RUN after stop ");
            dopisz_liczbe(buf, sizeof(buf), &p, s.run_po_stop);
            dopisz_str(buf, sizeof(buf), &p, "\n");
            buf[sizeof(buf) - 1] = '\0';
            tekst_strumien_->ustaw_tekst(buf);
        }

        if (info_ && g_snapshot_ok) {
            const char* dev_name = "Wyjście audio";
            uint8_t best=0;bool speaker=false;
            for(uint32_t j=0;j<g_snapshot.liczba_sciezek&&j<HDA_DIAG_MAKS_SCIEZEK;++j){
                const auto& path=g_snapshot.sciezki[j];if(path.kontroler_idx!=g_kontroler)continue;
                speaker|=path.cel==HDA_CEL_GLOSNIK&&path.ocena==100;
                if(path.ocena>=best){best=path.ocena;dev_name=hda_nazwa_celu_pinu(path.cel);}
            }
            char ibuf[160]{}; size_t ip = 0;
            dopisz_str(ibuf, sizeof(ibuf), &ip, dev_name);
            dopisz_str(ibuf, sizeof(ibuf), &ip, " - wybierz test. Czas: 1,5 s. Dźwięk nie uruchamia się automatycznie.");
            const auto& c=g_snapshot.kontrolery[g_kontroler];
            if(!c.gotowy){
                ibuf[0]=0;ip=0;dopisz_str(ibuf,sizeof(ibuf),&ip,"Kontroler wykryty. Błąd: ");
                dopisz_str(ibuf,sizeof(ibuf),&ip,hda_nazwa_etapu_bledu(c.etap_bledu));
            }else if(!c.liczba_kodekow){
                ibuf[0]=0;ip=0;dopisz_str(ibuf,sizeof(ibuf),&ip,"Kontroler zainicjalizowany; brak odpowiedzi kodeka.");
            }else if(!c.test_dostepny){
                ibuf[0]=0;ip=0;dopisz_str(ibuf,sizeof(ibuf),&ip,speaker?"Trasa głośnika wykryta; jeszcze nieskonfigurowana. Test niedostępny.":"Topologia odkryta; trasa głośnika niepotwierdzona. Test niedostępny.");
            }
            info_->ustaw_tekst(ibuf);
        }

        odswiez_raport();
    }

    Przycisk* btn_odswiez_ = nullptr;
    Przycisk* btn_zapisz_ = nullptr;
    Przycisk* btn_stop_ = nullptr;
    WskaznikZajetosci* wskaznik_ = nullptr;
    Etykieta* info_ = nullptr;
    ListaRozwijana* sekcje_ = nullptr;
    ListaRozwijana* kontrolery_ = nullptr;
    uint32_t liczba_kontrolerow_ui_ = UINT32_MAX;
    PasekInformacyjny* status_ = nullptr;
    Kontrolka* pages_[7]{};
    WidokRaportu* tekst_kontroler_ = nullptr;
    WidokRaportu* tekst_kodeki_ = nullptr;
    WidokRaportu* tekst_widgety_ = nullptr;
    WidokRaportu* tekst_sciezki_ = nullptr;
    WidokRaportu* tekst_strumien_ = nullptr;
    WidokRaportu* tekst_raport_ = nullptr;
    bool pierwsza_klatka_{false}, polaczony_{false}, ton_aktywny_{false};
    bool skan_w_toku_{false};
    uint32_t zadana_generacja_{0};
    uint8_t timer_ = 0;
    uint64_t ostatni_tick_ns_ = 0;

    static constexpr int MAKS_KONTEKSTOW = 8;
    struct KontekstTestu {
        OknoTestDzwieku* okno;
        uint32_t tryb, hz;
        uint8_t kanaly;
    };
    KontekstTestu konteksty_[MAKS_KONTEKSTOW]{};
    Przycisk* przyciski_testu_[MAKS_KONTEKSTOW]{};
    int liczba_kontekstow_ = 0;
    ZegarSkanu* zegar_ = nullptr;
    friend class ZegarSkanu;
};

void ZegarSkanu::krok_animacji(uint64_t teraz_ns) {
    if (!okno_) return;
    const uint64_t t = teraz_ns ? teraz_ns : gui_czas_monotoniczny_ns();
    if (t - okno_->ostatni_tick_ns_ > 20000000ULL) {
        okno_->ostatni_tick_ns_ = t;
        okno_->tick_skanu();
    }
    Kontrolka::krok_animacji(teraz_ns);
}

bool ZegarSkanu::czy_wymaga_animacji() const {
    return okno_ && okno_->potrzebuje_ticku();
}

} // namespace

extern "C" __attribute__((noreturn)) void _start() {
    AplikacjaGUI app;
    test_trace("Test dźwięku entry reached");
    {
        struct WlascicielOkna {
            OknoTestDzwieku* p;
            ~WlascicielOkna() { delete p; }
        } okno{new OknoTestDzwieku};
        okno.p->inicjalizuj();
        (void)app.uruchom(*okno.p);
    }
    test_stop();
    diag_skan_anuluj();
    bws_zakoncz_proces();
}
