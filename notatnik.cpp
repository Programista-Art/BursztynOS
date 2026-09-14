/*
 * Bursztyn OS - Notatnik (BursztynUI Modern)
 *
 * Nowoczesna aplikacja Ring 3 oparta na frameworku BursztynUI.
 * Korzysta z centralnego silnika motywów bez prywatnych palet barwnych.
 * Reaguje automatycznie na BWS_ZDARZENIE_MOTYW_ZMIENIONY w czasie rzeczywistym.
 */

#include "loader.h"
#include "bursztyn_ui/bursztyn_ui.h"
#include "bursztyn_gui.h"

using namespace BursztynUI;

// =============================================================================
// Nagłówek formatu .bur
// =============================================================================

extern "C" __attribute__((noreturn)) void _start();

static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 0x1000ULL;
static constexpr uint64_t BUR_TEKST_ROZMIAR     = 0x30000ULL; // 192 KiB
static constexpr uint64_t BUR_TEKST_WIRTUALNY     = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE   = 0x31000ULL;
static constexpr uint64_t BUR_DANE_ROZMIAR         = 0x20000ULL; // 128 KiB
static constexpr uint64_t BUR_DANE_WIRTUALNY       = 0x1031000ULL;

extern "C" {
__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},
    reinterpret_cast<uint64_t>(&_start),
    BUR_TEKST_PRZESUNIECIE,
    BUR_TEKST_ROZMIAR,
    BUR_TEKST_WIRTUALNY,
    BUR_DANE_PRZESUNIECIE,
    BUR_DANE_ROZMIAR,
    BUR_DANE_WIRTUALNY
};
}

// =============================================================================
// Deklaracje aplikacji Notatnik
// =============================================================================

enum class TrybMonitu : uint8_t {
    Brak = 0,
    Otworz,
    ZapiszJako
};

class OknoNotatnika;

struct NotatnikApp {
    AplikacjaGUI* app{nullptr};
    OknoNotatnika* okno{nullptr};
    MenuGlowne* menu{nullptr};
    PasekNarzedzi* pasek_narzedzi{nullptr};
    Panel* pasek_monitu{nullptr};
    Etykieta* etykieta_monitu{nullptr};
    PoleTekstowe* wejscie_monitu{nullptr};
    Przycisk* btn_zatwierdz{nullptr};
    Przycisk* btn_anuluj{nullptr};
    PoleWielowierszowe* memo{nullptr};
    PasekStanu* pasek_stanu{nullptr};

    char biezaca_sciezka[256]{};
    bool zmodyfikowany{false};
    TrybMonitu tryb_monitu{TrybMonitu::Brak};

    void nowy_dokument();
    bool otworz_plik(const char* sciezka);
    bool zapisz_plik_biezacy();
    bool zapisz_plik_jako(const char* sciezka);
    void pokaz_monit(TrybMonitu tryb);
    void ukryj_monit();
    void zatwierdz_monit();
    void aktualizuj_tytul();
    void aktualizuj_stan();
    void przelacz_zawijanie();
    void pokaz_o_programie();
};

static NotatnikApp g_notatnik;

class OknoNotatnika : public Okno {
public:
    explicit OknoNotatnika(const char* tytul, int szer = 680, int wys = 500)
        : Okno(tytul, szer, wys) {}

    void obsluz_zdarzenie(const bws_zdarzenie& ev) override {
        if (ev.typ == BWS_ZDARZENIE_OTWORZ_PLIK) {
            char nowa_sciezka[256] = {};
            if (pobierz_argument_startowy(nowa_sciezka, sizeof(nowa_sciezka)) && nowa_sciezka[0] != '\0') {
                g_notatnik.otworz_plik(nowa_sciezka);
            }
            return;
        }

        if (ev.typ == BWS_ZDARZENIE_KLAWISZ || ev.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY) {
            bool fizyczny = (ev.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY);
            bool wcisniety = !fizyczny || ev.x != 0;
            if (wcisniety) {
                uint32_t kod = ev.kod;
                bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
                uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);
                bool ctrl = (ev.przyciski & BWS_MOD_CTRL) != 0;

                if (g_notatnik.tryb_monitu != TrybMonitu::Brak) {
                    if (kod == 27U || (usb && baza == 41U) || (!usb && baza == 1U)) {
                        g_notatnik.ukryj_monit();
                        return;
                    }
                }

                if (ctrl) {
                    if (kod == 'n' || kod == 'N' || (usb && baza == 17U) || (!usb && baza == 0x31U)) {
                        g_notatnik.nowy_dokument();
                        return;
                    }
                    if (kod == 'o' || kod == 'O' || (usb && baza == 18U) || (!usb && baza == 0x18U)) {
                        g_notatnik.pokaz_monit(TrybMonitu::Otworz);
                        return;
                    }
                    if (kod == 's' || kod == 'S' || (usb && baza == 22U) || (!usb && baza == 0x1FU)) {
                        g_notatnik.zapisz_plik_biezacy();
                        return;
                    }
                }
            }
        }

        Okno::obsluz_zdarzenie(ev);
        g_notatnik.aktualizuj_stan();
    }
};

// =============================================================================
// Implementacja logiki Notatnika
// =============================================================================

static void wyodrebnij_nazwe_pliku(const char* sciezka, char* nazwa, size_t pojemnosc) {
    if (!sciezka || !nazwa || pojemnosc == 0) return;
    const char* p = sciezka;
    const char* ost = sciezka;
    while (*p) {
        if (*p == '/' || *p == '\\') ost = p + 1;
        ++p;
    }
    size_t i = 0;
    while (ost[i] && i + 1 < pojemnosc) {
        nazwa[i] = ost[i];
        ++i;
    }
    nazwa[i] = '\0';
}

void NotatnikApp::aktualizuj_tytul() {
    char buf[300];
    const char* prefix = "Notatnik - ";
    size_t pos = 0;
    while (prefix[pos] && pos + 1 < sizeof(buf)) {
        buf[pos] = prefix[pos];
        ++pos;
    }
    if (biezaca_sciezka[0] != '\0') {
        char plik[64];
        wyodrebnij_nazwe_pliku(biezaca_sciezka, plik, sizeof(plik));
        size_t j = 0;
        while (plik[j] && pos + 1 < sizeof(buf)) {
            buf[pos++] = plik[j++];
        }
    } else {
        const char* b = "Nowy dokument";
        size_t j = 0;
        while (b[j] && pos + 1 < sizeof(buf)) {
            buf[pos++] = b[j++];
        }
    }
    if (zmodyfikowany && pos + 2 < sizeof(buf)) {
        buf[pos++] = ' ';
        buf[pos++] = '*';
    }
    buf[pos] = '\0';
    if (okno) {
        okno->ustaw_tytul(buf);
    }
}

static void formatuj_liczbe(char* buf, size_t cap, int64_t val) {
    if (!buf || cap == 0) return;
    if (val == 0) {
        if (cap >= 2) { buf[0] = '0'; buf[1] = '\0'; }
        else { buf[0] = '\0'; }
        return;
    }
    bool ujemna = val < 0;
    uint64_t u = ujemna ? static_cast<uint64_t>(-(val + 1)) + 1 : static_cast<uint64_t>(val);
    char tmp[24];
    size_t n = 0;
    while (u > 0 && n < sizeof(tmp)) {
        tmp[n++] = static_cast<char>('0' + (u % 10));
        u /= 10;
    }
    size_t p = 0;
    if (ujemna && p + 1 < cap) buf[p++] = '-';
    while (n > 0 && p + 1 < cap) buf[p++] = tmp[--n];
    buf[p] = '\0';
}

void NotatnikApp::aktualizuj_stan() {
    if (!memo || !pasek_stanu) return;

    size_t kursor = memo->pozycja_kursora();
    const char* txt = memo->tekst();
    size_t linia = 1;
    size_t kolumna = 1;

    for (size_t i = 0; i < kursor && txt && txt[i]; ++i) {
        if (txt[i] == '\n') {
            ++linia;
            kolumna = 1;
        } else if ((static_cast<unsigned char>(txt[i]) & 0xC0) != 0x80) {
            ++kolumna;
        }
    }

    char buf_poz[48];
    char l_str[16];
    char c_str[16];
    formatuj_liczbe(l_str, sizeof(l_str), static_cast<int64_t>(linia));
    formatuj_liczbe(c_str, sizeof(c_str), static_cast<int64_t>(kolumna));

    size_t p = 0;
    const char* w_lbl = "Wiersz: ";
    while (*w_lbl && p + 1 < sizeof(buf_poz)) buf_poz[p++] = *w_lbl++;
    for (size_t j = 0; l_str[j] && p + 1 < sizeof(buf_poz); ++j) buf_poz[p++] = l_str[j];
    const char* k_lbl = ", Kol: ";
    while (*k_lbl && p + 1 < sizeof(buf_poz)) buf_poz[p++] = *k_lbl++;
    for (size_t j = 0; c_str[j] && p + 1 < sizeof(buf_poz); ++j) buf_poz[p++] = c_str[j];
    buf_poz[p] = '\0';
    pasek_stanu->ustaw_tekst(1, buf_poz);

    char buf_linii[32];
    char n_str[16];
    formatuj_liczbe(n_str, sizeof(n_str), static_cast<int64_t>(memo->liczba_wierszy()));
    p = 0;
    const char* lin_lbl = "Linii: ";
    while (*lin_lbl && p + 1 < sizeof(buf_linii)) buf_linii[p++] = *lin_lbl++;
    for (size_t j = 0; n_str[j] && p + 1 < sizeof(buf_linii); ++j) buf_linii[p++] = n_str[j];
    buf_linii[p] = '\0';
    pasek_stanu->ustaw_tekst(2, buf_linii);
}

void NotatnikApp::nowy_dokument() {
    if (memo) memo->ustaw_tekst("");
    biezaca_sciezka[0] = '\0';
    zmodyfikowany = false;
    ukryj_monit();
    aktualizuj_tytul();
    aktualizuj_stan();
    if (pasek_stanu) pasek_stanu->ustaw_tekst(0, "Utworzono nowy dokument");
}

bool NotatnikApp::otworz_plik(const char* sciezka) {
    if (!sciezka || sciezka[0] == '\0') return false;

    static char bufor_odczytu[65536];
    bufor_odczytu[0] = '\0';
    if (!czytaj_plik(sciezka, bufor_odczytu, sizeof(bufor_odczytu) - 1)) {
        if (pasek_stanu) pasek_stanu->ustaw_tekst(0, "Błąd: nie można otworzyć pliku!");
        return false;
    }

    size_t i = 0;
    while (sciezka[i] && i + 1 < sizeof(biezaca_sciezka)) {
        biezaca_sciezka[i] = sciezka[i];
        ++i;
    }
    biezaca_sciezka[i] = '\0';

    if (memo) {
        memo->ustaw_tekst(bufor_odczytu);
    }
    zmodyfikowany = false;
    ukryj_monit();
    aktualizuj_tytul();
    aktualizuj_stan();

    char plik[64];
    wyodrebnij_nazwe_pliku(biezaca_sciezka, plik, sizeof(plik));
    char msg[128];
    size_t mp = 0;
    const char* pref = "Otwarto: ";
    while (*pref && mp + 1 < sizeof(msg)) msg[mp++] = *pref++;
    for (size_t j = 0; plik[j] && mp + 1 < sizeof(msg); ++j) msg[mp++] = plik[j];
    msg[mp] = '\0';
    if (pasek_stanu) pasek_stanu->ustaw_tekst(0, msg);

    return true;
}

bool NotatnikApp::zapisz_plik_biezacy() {
    if (biezaca_sciezka[0] == '\0') {
        pokaz_monit(TrybMonitu::ZapiszJako);
        return true;
    }
    return zapisz_plik_jako(biezaca_sciezka);
}

bool NotatnikApp::zapisz_plik_jako(const char* sciezka) {
    if (!sciezka || sciezka[0] == '\0' || !memo) return false;

    const char* dane = memo->tekst();
    size_t len = memo->dlugosc();

    if (!zapisz_plik(sciezka, dane, static_cast<uint32_t>(len))) {
        if (pasek_stanu) pasek_stanu->ustaw_tekst(0, "Błąd zapisu pliku!");
        return false;
    }

    size_t i = 0;
    while (sciezka[i] && i + 1 < sizeof(biezaca_sciezka)) {
        biezaca_sciezka[i] = sciezka[i];
        ++i;
    }
    biezaca_sciezka[i] = '\0';

    zmodyfikowany = false;
    ukryj_monit();
    aktualizuj_tytul();

    char plik[64];
    wyodrebnij_nazwe_pliku(biezaca_sciezka, plik, sizeof(plik));
    char msg[128];
    size_t mp = 0;
    const char* pref = "Zapisano: ";
    while (*pref && mp + 1 < sizeof(msg)) msg[mp++] = *pref++;
    for (size_t j = 0; plik[j] && mp + 1 < sizeof(msg); ++j) msg[mp++] = plik[j];
    msg[mp] = '\0';
    if (pasek_stanu) pasek_stanu->ustaw_tekst(0, msg);

    return true;
}

void NotatnikApp::pokaz_monit(TrybMonitu tryb) {
    tryb_monitu = tryb;
    if (!pasek_monitu) return;

    if (tryb == TrybMonitu::Otworz) {
        if (etykieta_monitu) etykieta_monitu->ustaw_tekst("Otwórz plik:");
        if (btn_zatwierdz) btn_zatwierdz->ustaw_tekst("Otwórz");
    } else {
        if (etykieta_monitu) etykieta_monitu->ustaw_tekst("Zapisz jako:");
        if (btn_zatwierdz) btn_zatwierdz->ustaw_tekst("Zapisz");
    }

    if (wejscie_monitu) {
        wejscie_monitu->ustaw_tekst(biezaca_sciezka[0] ? biezaca_sciezka : "/dokument.txt");
        if (okno) okno->ustaw_fokus(wejscie_monitu);
    }

    pasek_monitu->ustaw_widoczna(true);
    if (okno) {
        okno->przelicz_uklad();
        okno->uniewaznij();
    }
}

void NotatnikApp::ukryj_monit() {
    tryb_monitu = TrybMonitu::Brak;
    if (pasek_monitu) {
        pasek_monitu->ustaw_widoczna(false);
    }
    if (okno) {
        if (memo) okno->ustaw_fokus(memo);
        okno->przelicz_uklad();
        okno->uniewaznij();
    }
}

void NotatnikApp::zatwierdz_monit() {
    if (!wejscie_monitu) return;
    const char* sciezka = wejscie_monitu->tekst();
    if (!sciezka || sciezka[0] == '\0') return;

    if (tryb_monitu == TrybMonitu::Otworz) {
        otworz_plik(sciezka);
    } else if (tryb_monitu == TrybMonitu::ZapiszJako) {
        zapisz_plik_jako(sciezka);
    }
}

void NotatnikApp::przelacz_zawijanie() {
    if (!memo) return;
    bool cur = memo->czy_zawija_wiersze();
    memo->ustaw_zawijanie_wierszy(!cur);
    if (pasek_stanu) {
        pasek_stanu->ustaw_tekst(0, !cur ? "Włączono zawijanie wierszy" : "Wyłączono zawijanie wierszy");
    }
}

void NotatnikApp::pokaz_o_programie() {
    if (pasek_stanu) {
        pasek_stanu->ustaw_tekst(0, "Notatnik BursztynOS v2.0 - zmodernizowany dla BursztynUI");
    }
}

// =============================================================================
// Punkt Wejścia
// =============================================================================

extern "C" {
    void* __dso_handle = nullptr;
    int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
}

extern "C" __attribute__((noreturn)) void _start() {
    AplikacjaGUI app;
    g_notatnik.app = &app;

    OknoNotatnika okno("Notatnik - Nowy dokument", 680, 500);
    g_notatnik.okno = &okno;
    app.ustaw_glowne_okno(&okno);

    Panel* korzen = new Panel(Orientacja::Pionowa);
    korzen->ustaw_wypelnienie(Marginesy(2, 2, 2, 2));
    korzen->ustaw_odstep(2);
    okno.ustaw_kontrolke_glowna(korzen);

    // 1. Menu Główne
    MenuGlowne& menu = korzen->dodaj<MenuGlowne>();
    g_notatnik.menu = &menu;

    // Menu: Plik
    Menu& m_plik = menu.menu().dodaj_podmenu("Plik", 100);
    m_plik.dodaj_element("Nowy (Ctrl+N)", 101, [](ElementMenu&, void*) { g_notatnik.nowy_dokument(); });
    m_plik.dodaj_element("Otwórz... (Ctrl+O)", 102, [](ElementMenu&, void*) { g_notatnik.pokaz_monit(TrybMonitu::Otworz); });
    m_plik.dodaj_element("Zapisz (Ctrl+S)", 103, [](ElementMenu&, void*) { g_notatnik.zapisz_plik_biezacy(); });
    m_plik.dodaj_element("Zapisz jako...", 104, [](ElementMenu&, void*) { g_notatnik.pokaz_monit(TrybMonitu::ZapiszJako); });
    m_plik.dodaj_separator();
    m_plik.dodaj_element("Zakończ (Alt+F4)", 109, [](ElementMenu&, void*) { if (g_notatnik.app) g_notatnik.app->zakoncz(); });

    // Menu: Edycja
    Menu& m_edycja = menu.menu().dodaj_podmenu("Edycja", 200);
    m_edycja.dodaj_element("Cofnij (Ctrl+Z)", 201, [](ElementMenu&, void*) { if (g_notatnik.memo) g_notatnik.memo->cofnij(); });
    m_edycja.dodaj_separator();
    m_edycja.dodaj_element("Wytnij (Ctrl+X)", 202, [](ElementMenu&, void*) { if (g_notatnik.memo) g_notatnik.memo->wytnij_do_schowka(); });
    m_edycja.dodaj_element("Kopiuj (Ctrl+C)", 203, [](ElementMenu&, void*) { if (g_notatnik.memo) g_notatnik.memo->kopiuj_do_schowka(); });
    m_edycja.dodaj_element("Wklej (Ctrl+V)", 204, [](ElementMenu&, void*) { if (g_notatnik.memo) g_notatnik.memo->wklej_ze_schowka(); });
    m_edycja.dodaj_separator();
    m_edycja.dodaj_element("Zaznacz wszystko (Ctrl+A)", 205, [](ElementMenu&, void*) { if (g_notatnik.memo) g_notatnik.memo->zaznacz_wszystko(); });
    m_edycja.dodaj_element("Wyczyść wszystko", 206, [](ElementMenu&, void*) { if (g_notatnik.memo) g_notatnik.memo->wyczysc(); });

    // Menu: Widok
    Menu& m_widok = menu.menu().dodaj_podmenu("Widok", 300);
    m_widok.dodaj_element("Zawijanie wierszy", 301, [](ElementMenu&, void*) { g_notatnik.przelacz_zawijanie(); });

    // Menu: Pomoc
    Menu& m_pomoc = menu.menu().dodaj_podmenu("Pomoc", 400);
    m_pomoc.dodaj_element("O programie Notatnik", 401, [](ElementMenu&, void*) { g_notatnik.pokaz_o_programie(); });

    // 2. Pasek Narzędzi
    PasekNarzedzi& pasek = korzen->dodaj<PasekNarzedzi>();
    g_notatnik.pasek_narzedzi = &pasek;
    pasek.dodaj_przycisk("Nowy", 0, [](ElementPaskaNarzedzi*, void*) { g_notatnik.nowy_dokument(); });
    pasek.dodaj_przycisk("Otwórz", 0, [](ElementPaskaNarzedzi*, void*) { g_notatnik.pokaz_monit(TrybMonitu::Otworz); });
    pasek.dodaj_przycisk("Zapisz", 0, [](ElementPaskaNarzedzi*, void*) { g_notatnik.zapisz_plik_biezacy(); });
    pasek.dodaj_separator();
    pasek.dodaj_przycisk("Cofnij", 0, [](ElementPaskaNarzedzi*, void*) { if (g_notatnik.memo) g_notatnik.memo->cofnij(); });
    pasek.dodaj_przycisk("Wytnij", 0, [](ElementPaskaNarzedzi*, void*) { if (g_notatnik.memo) g_notatnik.memo->wytnij_do_schowka(); });
    pasek.dodaj_przycisk("Kopiuj", 0, [](ElementPaskaNarzedzi*, void*) { if (g_notatnik.memo) g_notatnik.memo->kopiuj_do_schowka(); });
    pasek.dodaj_przycisk("Wklej", 0, [](ElementPaskaNarzedzi*, void*) { if (g_notatnik.memo) g_notatnik.memo->wklej_ze_schowka(); });

    // 3. Pasek monitu (Otwórz / Zapisz jako)
    Panel& p_monit = korzen->dodaj<Panel>(Orientacja::Pozioma);
    g_notatnik.pasek_monitu = &p_monit;
    p_monit.ustaw_preferowany_rozmiar(Rozmiar(-1, 32));
    p_monit.ustaw_wypelnienie(Marginesy(4, 4, 4, 4));
    p_monit.ustaw_odstep(6);
    p_monit.ustaw_widoczna(false);

    Etykieta& lbl_m = p_monit.dodaj<Etykieta>("Ścieżka:");
    g_notatnik.etykieta_monitu = &lbl_m;
    lbl_m.ustaw_preferowany_rozmiar(Rozmiar(90, 24));

    PoleTekstowe& txt_m = p_monit.dodaj<PoleTekstowe>("/dokument.txt");
    g_notatnik.wejscie_monitu = &txt_m;
    txt_m.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    txt_m.przy_zatwierdzeniu([](PoleTekstowe*, const char*, void*) { g_notatnik.zatwierdz_monit(); });

    Przycisk& btn_ok = p_monit.dodaj<Przycisk>("Zatwierdź");
    g_notatnik.btn_zatwierdz = &btn_ok;
    btn_ok.ustaw_preferowany_rozmiar(Rozmiar(80, 24));
    btn_ok.przy_kliknieciu([](Przycisk*, void*) { g_notatnik.zatwierdz_monit(); });

    Przycisk& btn_cancel = p_monit.dodaj<Przycisk>("Anuluj");
    g_notatnik.btn_anuluj = &btn_cancel;
    btn_cancel.ustaw_preferowany_rozmiar(Rozmiar(70, 24));
    btn_cancel.przy_kliknieciu([](Przycisk*, void*) { g_notatnik.ukryj_monit(); });

    // 4. Pole wielowierszowe (64 KiB pojemności)
    PoleWielowierszowe* memo = new PoleWielowierszowe("", 65536);
    g_notatnik.memo = memo;
    memo->ustaw_zawijanie_wierszy(true);
    memo->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    memo->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    memo->ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    memo->przy_zmianie([](PoleWielowierszowe*, const char*, void*) {
        g_notatnik.zmodyfikowany = true;
        g_notatnik.aktualizuj_tytul();
        g_notatnik.aktualizuj_stan();
    });
    korzen->dodaj_kontrolke(memo);

    // 5. Pasek Stanu
    PasekStanu& sb = korzen->dodaj<PasekStanu>();
    g_notatnik.pasek_stanu = &sb;
    sb.ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
    sb.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    sb.dodaj_sekcje("Gotowy", 160, RozmiarSekcjiStanu::Rozciagnij);
    sb.dodaj_sekcje("Wiersz: 1, Kol: 1", 140, RozmiarSekcjiStanu::Stala);
    sb.dodaj_sekcje("Linii: 1", 90, RozmiarSekcjiStanu::Stala);
    sb.dodaj_sekcje("UTF-8", 60, RozmiarSekcjiStanu::Stala);

    // Sprawdź początkowy argument pliku
    char start_arg[256] = {};
    if (pobierz_argument_startowy(start_arg, sizeof(start_arg)) && start_arg[0] != '\0') {
        g_notatnik.otworz_plik(start_arg);
    } else {
        g_notatnik.nowy_dokument();
    }

    app.uruchom(okno);
    bws_zakoncz_proces();
}
