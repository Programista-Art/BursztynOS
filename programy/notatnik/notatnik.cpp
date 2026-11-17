/*
 * Bursztyn OS - Notatnik (BursztynUI Modern)
 *
 * Nowoczesna aplikacja Ring 3 oparta na frameworku BursztynUI.
 * Korzysta z centralnego silnika motywów bez prywatnych palet barwnych.
 * Reaguje automatycznie na BWS_ZDARZENIE_MOTYW_ZMIENIONY w czasie rzeczywistym.
 *
 * Otwieranie i zapisywanie: natywny DialogPliku BursztynUI (bursztyn_ui/dialog_pliku.h),
 * wyswietlany jako modalna powierzchnia popup procesu Notatnika. Dialog dziala w tym
 * samym procesie i z tymi samymi uprawnieniami PZB (okna, pliki_czytaj, pliki_zapisz),
 * wiec nie otrzymuje zadnego dodatkowego dostepu do systemu plikow ani innych procesow.
 *
 * Polecenia edycji (Cofnij, Ponów, Wytnij, Kopiuj, Wklej, Zaznacz wszystko) sa w menu
 * Edycja i w menu kontekstowym BursztynUI pod prawym przyciskiem myszy w edytorze.
 */

#include "loader.h"
#include "bursztyn_ui/bursztyn_ui.h"
#include "bursztyn_ui/okno_podrzedne.h"
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

static constexpr uint32_t NOTATNIK_POJEMNOSC = 65536;       // bajty bufora edytora
static constexpr size_t NOTATNIK_MAKS_SCIEZKI = DialogPliku::MAKS_SCIEZKI;
static constexpr const char* NOTATNIK_KATALOG_DOMYSLNY = "/uzytkownicy";
static constexpr const char* NOTATNIK_FILTRY =
    "Dokumenty tekstowe (*.txt;*.md;*.log;*.ini;*.cfg;*.cebula)|*.txt;*.md;*.log;*.ini;*.cfg;*.cebula;;"
    "Pliki tekstowe (*.txt)|*.txt;;"
    "Wszystkie pliki (*.*)|*.*";

// Akcja, ktora moze zastapic albo zamknac biezacy dokument. Gdy dokument ma
// niezapisane zmiany, akcja czeka na decyzje uzytkownika (i ewentualny zapis).
enum class AkcjaDokumentu : uint8_t {
    Brak = 0,
    Nowy,
    Otworz,          // pokazuje dialog otwierania
    OtworzArgument,  // plik przekazany przez system (BWS_ZDARZENIE_OTWORZ_PLIK)
    Zamknij
};

enum class IdEdycji : uint32_t {
    Cofnij = 201, Ponow = 202, Wytnij = 203, Kopiuj = 204, Wklej = 205,
    ZaznaczWszystko = 206, Wyczysc = 207
};

class OknoNotatnika;
class EdytorNotatnika;

struct NotatnikApp {
    AplikacjaGUI* app{nullptr};
    OknoNotatnika* okno{nullptr};
    MenuGlowne* menu{nullptr};
    PoleWielowierszowe* memo{nullptr};
    PasekStanu* pasek_stanu{nullptr};

    // Polecenia edycji: menu Edycja (pasek menu) i menu kontekstowe edytora.
    ElementMenu* menu_cofnij{nullptr};
    ElementMenu* menu_ponow{nullptr};
    ElementMenu* menu_wytnij{nullptr};
    ElementMenu* menu_kopiuj{nullptr};
    MenuKontekstowe* menu_kontekstowe{nullptr};
    ElementMenu* kontekst[6]{};  // Cofnij, Ponow, Wytnij, Kopiuj, Wklej, Zaznacz wszystko

    char biezaca_sciezka[NOTATNIK_MAKS_SCIEZKI]{};
    bool zmodyfikowany{false};
    char pozostala_kopia[NOTATNIK_MAKS_SCIEZKI]{};  // kopia, ktorej nie udalo sie usunac po zapisie

    // Dialogi. Wyniki sa odkladane i przetwarzane po zakonczeniu obslugi
    // zdarzenia, gdy dialog jest juz zamkniety (bez zagniezdzania popupow).
    DialogPliku* dialog_pliku{nullptr};
    OknoDialogowe* pytanie_zmian{nullptr};
    OknoDialogowe* komunikat{nullptr};
    AkcjaDokumentu akcja_oczekujaca{AkcjaDokumentu::Brak};
    AkcjaDokumentu akcja_po_zapisie{AkcjaDokumentu::Brak};
    char sciezka_argumentu[NOTATNIK_MAKS_SCIEZKI]{};
    WynikDialogu wynik_pytania{WynikDialogu::Brak};
    bool wynik_pliku_gotowy{false};
    TrybDialoguPliku tryb_wyniku_pliku{TrybDialoguPliku::Otworz};
    WynikDialoguPliku wynik_pliku{WynikDialoguPliku::Brak};
    char sciezka_wyniku[NOTATNIK_MAKS_SCIEZKI]{};
    bool zamykanie_potwierdzone{false};

    void nowy_dokument();
    bool otworz_plik(const char* sciezka);
    bool zapisz_plik_biezacy();
    bool zapisz_plik_jako(const char* sciezka);
    void zapisz_jako();

    void zadaj(AkcjaDokumentu akcja);
    void wykonaj(AkcjaDokumentu akcja);
    void pokaz_dialog_pliku(TrybDialoguPliku tryb);
    void przetworz_oczekujace();
    bool czy_dialog_otwarty() const;
    void pokaz_blad(const char* tytul, const char* tresc, TypDialogu typ = TypDialogu::Blad);
    void ostrzez_o_kopii(const char* sciezka);
    bool zapisz_z_kopia(const char* sciezka, const char* dane, uint32_t len, uint32_t rozmiar_oryginalu);

    void aktualizuj_tytul();
    void aktualizuj_stan();
    void aktualizuj_polecenia(bool z_schowkiem);
    void przelacz_zawijanie();
    void pokaz_pomoc(OknoPodrzedne::Rodzaj rodzaj, const char* tytul, const char* tresc);
};

static NotatnikApp g_notatnik;

// Edytor dokumentu: wspoldzielony PoleWielowierszowe z menu kontekstowym
// BursztynUI. Stany polecen sa odswiezane tuz przed pokazaniem menu.
class EdytorNotatnika : public PoleWielowierszowe {
public:
    explicit EdytorNotatnika(size_t pojemnosc) : PoleWielowierszowe("", pojemnosc) {}
    void przygotuj_menu_kontekstowe(MenuKontekstowe&) override {
        g_notatnik.aktualizuj_polecenia(true);
    }
};

static bool czy_klawisz(const bws_zdarzenie& ev, uint32_t ps2, uint32_t usb_hid) {
    const bool usb = (ev.kod & BWS_KEY_SOURCE_USB) != 0;
    if (usb) return (ev.kod & 0xFFU) == usb_hid;
    return (ev.kod & BWS_KEY_SOURCE_E0) == 0 && (ev.kod & 0x7FU) == ps2;
}

class OknoNotatnika : public Okno {
public:
    explicit OknoNotatnika(const char* tytul, int szer = 680, int wys = 500)
        : Okno(tytul, szer, wys) {}

    void obsluz_zdarzenie(const bws_zdarzenie& ev) override {
        if (ev.typ == BWS_ZDARZENIE_OTWORZ_PLIK) {
            char nowa_sciezka[NOTATNIK_MAKS_SCIEZKI] = {};
            if (pobierz_argument_startowy(nowa_sciezka, sizeof(nowa_sciezka)) && nowa_sciezka[0] != '\0') {
                DialogPliku::kopiuj_str(g_notatnik.sciezka_argumentu, sizeof(g_notatnik.sciezka_argumentu), nowa_sciezka);
                g_notatnik.zadaj(AkcjaDokumentu::OtworzArgument);
            }
            g_notatnik.przetworz_oczekujace();
            return;
        }

        // Skroty dokumentu tylko z fizycznego klawisza, bez AltGr (AltGr+N/O/S
        // to ń/ó/ś) i nigdy, gdy otwarty jest dialog lub menu.
        if (ev.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY && ev.x != 0 &&
            bws_mod_ctrl_skrotu(ev.przyciski) && !bws_mod_alt_skrotu(ev.przyciski) &&
            !zarzadca_popupow().czy_otwarty()) {
            const bool shift = (ev.przyciski & BWS_MOD_SHIFT) != 0;
            if (czy_klawisz(ev, 0x31U, 17U)) { g_notatnik.zadaj(AkcjaDokumentu::Nowy); g_notatnik.przetworz_oczekujace(); return; }
            if (czy_klawisz(ev, 0x18U, 18U)) { g_notatnik.zadaj(AkcjaDokumentu::Otworz); g_notatnik.przetworz_oczekujace(); return; }
            if (czy_klawisz(ev, 0x1FU, 22U)) {
                if (shift) g_notatnik.zapisz_jako(); else g_notatnik.zapisz_plik_biezacy();
                g_notatnik.przetworz_oczekujace();
                return;
            }
        }

        Okno::obsluz_zdarzenie(ev);
        g_notatnik.przetworz_oczekujace();
        g_notatnik.aktualizuj_stan();
    }

    // Zamkniecie okna (X, Alt+F4, BWS_ZDARZENIE_ZAMKNIJ, Plik > Zakoncz) nie
    // gubi niezapisanych zmian: najpierw pytanie, zamkniecie dopiero po decyzji.
    void zamknij() override {
        if (g_notatnik.zmodyfikowany && !g_notatnik.zamykanie_potwierdzone) {
            g_notatnik.zadaj(AkcjaDokumentu::Zamknij);
            return;
        }
        Okno::zamknij();
    }
};

// =============================================================================
// Pomocnicze (bez biblioteki standardowej Ring 3)
// =============================================================================

// Kopiuje UTF-8 bez rozcinania sekwencji wielobajtowej przy obcinaniu.
static size_t dopisz_utf8(char* cel, size_t pos, size_t cap, const char* zr) {
    if (!cel || cap == 0) return pos;
    size_t i = 0;
    while (zr && zr[i] && pos + 1 < cap) {
        unsigned char c = static_cast<unsigned char>(zr[i]);
        size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        if (pos + n >= cap) break;
        for (size_t k = 0; k < n && zr[i]; ++k) cel[pos++] = zr[i++];
    }
    cel[pos] = '\0';
    return pos;
}

static const char* nazwa_pliku(const char* sciezka) {
    if (!sciezka) return "";
    const char* ost = sciezka;
    for (const char* p = sciezka; *p; ++p)
        if (*p == '/' || *p == '\\') ost = p + 1;
    return ost;
}

static void katalog_pliku(const char* sciezka, char* cel, size_t cap) {
    cel[0] = '\0';
    if (!sciezka || sciezka[0] != '/') return;
    size_t ost = 0;
    for (size_t i = 0; sciezka[i]; ++i) if (sciezka[i] == '/') ost = i;
    if (ost == 0) { if (cap > 1) { cel[0] = '/'; cel[1] = '\0'; } return; }
    if (ost + 1 > cap) return;
    for (size_t i = 0; i < ost; ++i) cel[i] = sciezka[i];
    cel[ost] = '\0';
}

static bool czy_katalog(const char* sciezka) {
    BwsMetadanePliku meta{};
    return sciezka && sciezka[0] && pobierz_metadane_pliku(sciezka, &meta) && meta.typ == BWS_TYP_KATALOG;
}

static void formatuj_liczbe(char* buf, size_t cap, uint64_t u) {
    if (!buf || cap == 0) return;
    char tmp[24];
    size_t n = 0;
    do { tmp[n++] = static_cast<char>('0' + (u % 10)); u /= 10; } while (u > 0 && n < sizeof(tmp));
    size_t p = 0;
    while (n > 0 && p + 1 < cap) buf[p++] = tmp[--n];
    buf[p] = '\0';
}

static void ustaw_stan(size_t sekcja, const char* prefiks, const char* tresc = nullptr) {
    if (!g_notatnik.pasek_stanu) return;
    char msg[320];
    size_t p = dopisz_utf8(msg, 0, sizeof(msg), prefiks);
    if (tresc) dopisz_utf8(msg, p, sizeof(msg), tresc);
    g_notatnik.pasek_stanu->ustaw_tekst(sekcja, msg);
}

// =============================================================================
// Implementacja logiki Notatnika
// =============================================================================

void NotatnikApp::aktualizuj_tytul() {
    char buf[320];
    size_t pos = dopisz_utf8(buf, 0, sizeof(buf), "Notatnik - ");
    pos = dopisz_utf8(buf, pos, sizeof(buf) - 3,
                      biezaca_sciezka[0] ? nazwa_pliku(biezaca_sciezka) : "Nowy dokument");
    if (zmodyfikowany) pos = dopisz_utf8(buf, pos, sizeof(buf), " *");
    if (okno) okno->ustaw_tytul(buf);
}

void NotatnikApp::aktualizuj_stan() {
    if (!memo || !pasek_stanu) return;

    size_t linia = 1;
    size_t kolumna = 1;
#if defined(BSF_NATIVE)
    memo->pozycja_logiczna(linia, kolumna);
#else
    const size_t kursor = memo->pozycja_kursora();
    const char* txt = memo->tekst();
    for (size_t i = 0; i < kursor && txt && txt[i]; ++i) {
        if (txt[i] == '\n') {
            ++linia;
            kolumna = 1;
        } else if (txt[i] != '\r' && (static_cast<unsigned char>(txt[i]) & 0xC0) != 0x80) {
            ++kolumna;
        }
    }
#endif

    char l_str[24], c_str[24], n_str[24];
    formatuj_liczbe(l_str, sizeof(l_str), linia);
    formatuj_liczbe(c_str, sizeof(c_str), kolumna);
    formatuj_liczbe(n_str, sizeof(n_str), memo->liczba_wierszy());

    char buf_poz[64];
    size_t p = dopisz_utf8(buf_poz, 0, sizeof(buf_poz), "Wiersz: ");
    p = dopisz_utf8(buf_poz, p, sizeof(buf_poz), l_str);
    p = dopisz_utf8(buf_poz, p, sizeof(buf_poz), ", Kol: ");
    dopisz_utf8(buf_poz, p, sizeof(buf_poz), c_str);
    pasek_stanu->ustaw_tekst(1, buf_poz);

    char buf_linii[40];
    p = dopisz_utf8(buf_linii, 0, sizeof(buf_linii), "Linii: ");
    dopisz_utf8(buf_linii, p, sizeof(buf_linii), n_str);
    pasek_stanu->ustaw_tekst(2, buf_linii);

    aktualizuj_polecenia(false);
}

static void ustaw_wlaczony(ElementMenu* e, bool v) {
    if (e && e->czy_wlaczony() != v) e->ustaw_wlaczony(v);
}

// Dostepnosc polecen edycji odpowiada rzeczywistemu stanowi edytora. Stan
// schowka (wywolanie BWS) jest sprawdzany tylko przed pokazaniem menu.
void NotatnikApp::aktualizuj_polecenia(bool z_schowkiem) {
    if (!memo) return;
    const bool ro = memo->czy_tylko_do_odczytu();
    const bool zaznaczenie = memo->czy_ma_zaznaczenie();
    const bool cofnij = memo->czy_mozna_cofnac();
    const bool ponow = memo->czy_mozna_ponowic();
    ustaw_wlaczony(menu_cofnij, cofnij);
    ustaw_wlaczony(menu_ponow, ponow);
    ustaw_wlaczony(menu_wytnij, zaznaczenie && !ro);
    ustaw_wlaczony(menu_kopiuj, zaznaczenie);
    ustaw_wlaczony(kontekst[0], cofnij);
    ustaw_wlaczony(kontekst[1], ponow);
    ustaw_wlaczony(kontekst[2], zaznaczenie && !ro);
    ustaw_wlaczony(kontekst[3], zaznaczenie);
    if (z_schowkiem) {
        uint64_t rozmiar = 0;
        const bool tekst = clipboard_size("text/plain", &rozmiar) && rozmiar > 0;
        ustaw_wlaczony(kontekst[4], tekst && !ro);
    }
    ustaw_wlaczony(kontekst[5], memo->dlugosc() > 0);
}

void NotatnikApp::nowy_dokument() {
    if (memo) {
        memo->ustaw_tekst("");
        memo->wyczysc_historie();
    }
    biezaca_sciezka[0] = '\0';
    zmodyfikowany = false;
    aktualizuj_tytul();
    aktualizuj_stan();
    ustaw_stan(0, "Utworzono nowy dokument");
}

void NotatnikApp::pokaz_blad(const char* tytul, const char* tresc, TypDialogu typ) {
    ustaw_stan(0, typ == TypDialogu::Blad ? "Błąd: " : "Uwaga: ", tresc);
    if (!okno) return;
    if (!komunikat) komunikat = new OknoDialogowe(tytul, tresc, PrzyciskiDialogu::TylkoOk, typ);
    if (!komunikat || komunikat->czy_otwarta() || okno->zarzadca_popupow().czy_otwarty()) return;
    komunikat->ustaw_tekst(tytul);
    komunikat->ustaw_wiadomosc(tresc);
    komunikat->ustaw_typ_dialogu(typ);
    komunikat->pokaz(okno->zarzadca_popupow(), memo);
}

// Wspolny bufor odczytu i weryfikacji zapisu (operacje nigdy nie trwaja naraz).
static char bufor_pliku[NOTATNIK_POJEMNOSC];

// =============================================================================
// Bezpieczny zapis (bez atomowej podmiany w PSF)
// =============================================================================
//
// PSF (jadro/fs/psf.cpp, zapisz_do_pliku) nadpisuje plik w miejscu: najpierw
// zwalnia nadmiarowe bloki i ustawia nowy rozmiar, potem kopiuje dane blok po
// bloku. Blad w trakcie (brak wolnego bloku, blad nosnika) zostawia plik
// czesciowo nadpisany. PSF nie ma dziennika, a BWS 8 (zmiana nazwy) odmawia,
// gdy nazwa docelowa istnieje, wiec podmiana "tymczasowy -> docelowy" wymagalaby
// chwili bez pliku docelowego i zmienilaby jego wezel (PZB, czas utworzenia).
//
// Dlatego Notatnik stosuje kopie bezpieczenstwa obok pliku:
//   1. oryginal jest czytany do pamieci i zapisywany do NOWEGO pliku kopii
//      ("~nazwa.kopia", "~nazwa.kopia1"...; istniejace pliki nigdy nie sa
//      nadpisywane ani usuwane), kopia jest weryfikowana ponownym odczytem;
//   2. dopiero potem dokument jest zapisywany w miejscu i weryfikowany;
//   3. sukces: kopia jest usuwana; blad: oryginal jest przywracany z pamieci
//      i weryfikowany; jesli przywrocenie zawiedzie, kopia zostaje na dysku,
//      a uzytkownik dostaje jej nazwe.
// W kazdej chwili (takze po przerwaniu) pelna tresc oryginalu jest w pliku
// docelowym albo w zweryfikowanej kopii. To bezpieczna podmiana, nie atomowa.

static constexpr uint32_t NOTATNIK_MAKS_KOPII = 16U * 1024U * 1024U;
static constexpr size_t NOTATNIK_MAKS_NAZWY_PSF = 55;  // PSF_MAX_NAZWA - 1
static constexpr const char* NOTATNIK_SUFIKS_KOPII = ".kopia";

#ifdef NOTATNIK_ALOKATOR_TESTOWY
static char* notatnik_alokuj(uint32_t n);
#else
static char* notatnik_alokuj(uint32_t n) { return new char[n]; }  // Ring 3: nullptr przy braku pamieci
#endif

// Plik ma dokladnie podana tresc (rozmiar i bajty).
static bool plik_zgodny(const char* sciezka, const char* dane, uint32_t len) {
    uint32_t rozmiar = 0;
    if (!pobierz_rozmiar_pliku(sciezka, &rozmiar) || rozmiar != len) return false;
    if (len == 0) return true;
    char* bufor = len < NOTATNIK_POJEMNOSC ? bufor_pliku : notatnik_alokuj(len);
    if (!bufor) return false;
    bool zgodny = czytaj_plik(sciezka, bufor, len);
    for (uint32_t i = 0; zgodny && i < len; ++i) zgodny = bufor[i] == dane[i];
    if (bufor != bufor_pliku) delete[] bufor;
    return zgodny;
}

static bool zapisz_i_zweryfikuj(const char* sciezka, const char* dane, uint32_t len) {
    return zapisz_plik(sciezka, dane, len) && plik_zgodny(sciezka, dane, len);
}

static bool element_istnieje(const char* sciezka) {
    uint32_t r = 0;
    BwsMetadanePliku meta{};
    return pobierz_rozmiar_pliku(sciezka, &r) || pobierz_metadane_pliku(sciezka, &meta);
}

// Sciezka kopii w katalogu pliku: "~nazwa.kopia[N]". Nazwa jest skracana na
// granicy znaku UTF-8, aby zmiescic sie w limicie nazwy PSF.
static bool sciezka_kopii(const char* sciezka, unsigned numer, char* cel, size_t cap) {
    const char* nazwa = nazwa_pliku(sciezka);
    const size_t katalog = static_cast<size_t>(nazwa - sciezka);
    if (katalog == 0 || katalog >= cap) return false;
    char sufiks[16]{};
    size_t s = dopisz_utf8(sufiks, 0, sizeof(sufiks), NOTATNIK_SUFIKS_KOPII);
    if (numer > 0) {
        char cyfry[8]{};
        size_t n = 0;
        for (unsigned v = numer; v && n < sizeof(cyfry); v /= 10) cyfry[n++] = static_cast<char>('0' + v % 10);
        while (n && s + 1 < sizeof(sufiks)) sufiks[s++] = cyfry[--n];
        sufiks[s] = '\0';
    }
    char nazwa_kopii[NOTATNIK_MAKS_NAZWY_PSF + 1]{};
    size_t p = dopisz_utf8(nazwa_kopii, 0, sizeof(nazwa_kopii), "~");
    p = dopisz_utf8(nazwa_kopii, p, sizeof(nazwa_kopii) - s, nazwa);
    p = dopisz_utf8(nazwa_kopii, p, sizeof(nazwa_kopii), sufiks);
    if (katalog + p + 1 > cap) return false;
    for (size_t i = 0; i < katalog; ++i) cel[i] = sciezka[i];
    for (size_t i = 0; i <= p; ++i) cel[katalog + i] = nazwa_kopii[i];
    return DialogPliku::czy_poprawna_nazwa_elementu(nazwa_kopii);
}

// Pozostalosc przerwanego zapisu: kopia obok otwieranego pliku. Notatnik jej
// nie usuwa (moze zawierac jedyna pelna wersje); informuje uzytkownika.
void NotatnikApp::ostrzez_o_kopii(const char* sciezka) {
    static char wiadomosc[768];
    char kopia[NOTATNIK_MAKS_SCIEZKI]{};
    for (unsigned numer = 0; numer < 10; ++numer) {
        if (!sciezka_kopii(sciezka, numer, kopia, sizeof(kopia))) return;
        uint32_t r = 0;
        if (!pobierz_rozmiar_pliku(kopia, &r)) continue;
        size_t p = dopisz_utf8(wiadomosc, 0, sizeof(wiadomosc),
            "Obok pliku znajduje się kopia bezpieczeństwa z przerwanego lub nieudanego zapisu: ");
        p = dopisz_utf8(wiadomosc, p, sizeof(wiadomosc), nazwa_pliku(kopia));
        dopisz_utf8(wiadomosc, p, sizeof(wiadomosc),
            ". Sprawdź jej zawartość; Notatnik jej nie usuwa.");
        pokaz_blad("Kopia bezpieczeństwa", wiadomosc, TypDialogu::Ostrzezenie);
        return;
    }
}

// Zapis do istniejacego pliku z kopia bezpieczenstwa oryginalu.
bool NotatnikApp::zapisz_z_kopia(const char* sciezka, const char* dane, uint32_t len,
                                 uint32_t rozmiar_oryginalu) {
    static char wiadomosc[768];
    char* oryginal = nullptr;
    char kopia[NOTATNIK_MAKS_SCIEZKI]{};
    bool ma_kopie = false;

    if (rozmiar_oryginalu > 0) {
        if (rozmiar_oryginalu > NOTATNIK_MAKS_KOPII) {
            pokaz_blad("Błąd zapisu", "Plik docelowy jest zbyt duży, aby bezpiecznie utworzyć jego kopię. Plik nie został zmieniony.");
            return false;
        }
        oryginal = notatnik_alokuj(rozmiar_oryginalu);
        if (!oryginal) {
            pokaz_blad("Błąd zapisu", "Brak pamięci na kopię bezpieczeństwa. Plik nie został zmieniony.");
            return false;
        }
        uint32_t teraz = 0;
        if (!czytaj_plik(sciezka, oryginal, rozmiar_oryginalu) ||
            !pobierz_rozmiar_pliku(sciezka, &teraz) || teraz != rozmiar_oryginalu) {
            delete[] oryginal;
            pokaz_blad("Błąd zapisu", "Nie można odczytać obecnej zawartości pliku docelowego. Plik nie został zmieniony.");
            return false;
        }
        bool identyczny = rozmiar_oryginalu == len;
        for (uint32_t i = 0; identyczny && i < len; ++i) identyczny = oryginal[i] == dane[i];
        if (identyczny) {  // nic do zapisania; plik juz zawiera dokument
            delete[] oryginal;
            return true;
        }

        // Kopia zawsze w NOWYM pliku: istniejacy plik o tej nazwie (np. z przerwanego
        // zapisu albo uzytkownika) nie jest ani nadpisywany, ani usuwany.
        for (unsigned numer = 0; numer < 10 && !ma_kopie; ++numer) {
            if (!sciezka_kopii(sciezka, numer, kopia, sizeof(kopia))) break;
            if (element_istnieje(kopia)) continue;
            if (!utworz(kopia)) break;
            if (!zapisz_i_zweryfikuj(kopia, oryginal, rozmiar_oryginalu)) {
                (void)usun_twor_uzytkownika(kopia);  // wlasny, niekompletny plik kopii
                break;
            }
            ma_kopie = true;
        }
        if (!ma_kopie) {
            delete[] oryginal;
            pokaz_blad("Błąd zapisu", "Nie można utworzyć kopii bezpieczeństwa obok pliku (brak miejsca, uprawnień lub wolnej nazwy). Plik nie został zmieniony.");
            return false;
        }
    }

    if (zapisz_i_zweryfikuj(sciezka, dane, len)) {
        delete[] oryginal;
        if (ma_kopie && !usun_twor_uzytkownika(kopia))
            DialogPliku::kopiuj_str(pozostala_kopia, sizeof(pozostala_kopia), kopia);
        return true;
    }

    // Zapis nieudany: plik moze byc czesciowo nadpisany. Przywracanie oryginalu.
    const bool przywrocono = zapisz_i_zweryfikuj(sciezka, oryginal ? oryginal : "", rozmiar_oryginalu);
    delete[] oryginal;
    if (przywrocono) {
        if (ma_kopie) (void)usun_twor_uzytkownika(kopia);
        pokaz_blad("Błąd zapisu", "Zapis nie powiódł się (brak miejsca, uprawnień lub błąd nośnika). Przywrócono poprzednią zawartość pliku. Dokument pozostaje niezapisany.");
        return false;
    }
    size_t p = dopisz_utf8(wiadomosc, 0, sizeof(wiadomosc),
        "Zapis nie powiódł się, a przywrócenie poprzedniej zawartości również. Plik docelowy może być niekompletny. ");
    if (ma_kopie) {
        p = dopisz_utf8(wiadomosc, p, sizeof(wiadomosc), "Poprzednia zawartość jest zachowana w pliku kopii: ");
        p = dopisz_utf8(wiadomosc, p, sizeof(wiadomosc), kopia);
        p = dopisz_utf8(wiadomosc, p, sizeof(wiadomosc), ". ");
    }
    dopisz_utf8(wiadomosc, p, sizeof(wiadomosc), "Dokument pozostaje niezapisany w Notatniku.");
    pokaz_blad("Błąd zapisu", wiadomosc);
    return false;
}

// Wczytuje dokument. Biezacy dokument zmienia sie dopiero po poprawnym
// odczycie i walidacji; przy bledzie pozostaje nietkniety.
bool NotatnikApp::otworz_plik(const char* sciezka) {
    if (!sciezka || sciezka[0] == '\0') return false;
    if (!DialogPliku::czy_poprawna_sciezka_dialogu(sciezka)) {
        pokaz_blad("Nie można otworzyć pliku", "Ścieżka pliku jest nieprawidłowa albo zbyt długa.");
        return false;
    }

    char* bufor_odczytu = bufor_pliku;
    uint32_t rozmiar = 0;
    if (!pobierz_rozmiar_pliku(sciezka, &rozmiar)) {
        pokaz_blad("Nie można otworzyć pliku", "Plik nie istnieje albo brak uprawnień do odczytu.");
        return false;
    }
    if (rozmiar >= NOTATNIK_POJEMNOSC) {
        pokaz_blad("Nie można otworzyć pliku", "Plik przekracza limit Notatnika (65535 bajtów).");
        return false;
    }
    bufor_odczytu[rozmiar] = 0;
    if (rozmiar > 0 && !czytaj_plik(sciezka, bufor_odczytu, NOTATNIK_POJEMNOSC - 1)) {
        pokaz_blad("Nie można otworzyć pliku", "Odczyt pliku nie powiódł się.");
        return false;
    }
    bufor_odczytu[rozmiar] = 0;
    for (uint32_t b = 0; b < rozmiar; ++b) {
        if (!bufor_odczytu[b]) {
            pokaz_blad("Nie można otworzyć pliku", "Plik zawiera bajt NUL i nie jest dokumentem tekstowym.");
            return false;
        }
    }

    DialogPliku::kopiuj_str(biezaca_sciezka, sizeof(biezaca_sciezka), sciezka);
    if (memo) {
        memo->ustaw_tekst(bufor_odczytu);
        memo->wyczysc_historie();
        memo->ustaw_zaznaczenie(0, 0);
    }
    zmodyfikowany = false;
    aktualizuj_tytul();
    aktualizuj_stan();
    ustaw_stan(0, "Otwarto: ", nazwa_pliku(biezaca_sciezka));
    ostrzez_o_kopii(biezaca_sciezka);
    return true;
}

bool NotatnikApp::zapisz_plik_biezacy() {
    if (biezaca_sciezka[0] == '\0') {
        zapisz_jako();
        return false;
    }
    return zapisz_plik_jako(biezaca_sciezka);
}

void NotatnikApp::zapisz_jako() {
    if (czy_dialog_otwarty()) return;
    pokaz_dialog_pliku(TrybDialoguPliku::Zapisz);
}

// Zapisuje dokument pod wskazana sciezka. Sukces jest zglaszany wylacznie po
// zapisie potwierdzonym ponownym odczytem; przy bledzie dokument, jego sciezka
// i znacznik zmian pozostaja bez zmian. System plikow Bursztyn OS nie ma
// atomowej podmiany pliku: istniejacy plik jest chroniony kopia bezpieczenstwa
// (zapisz_z_kopia), a niekompletny nowy plik jest usuwany.
bool NotatnikApp::zapisz_plik_jako(const char* sciezka) {
    if (!sciezka || sciezka[0] == '\0' || !memo) return false;
    if (!DialogPliku::czy_poprawna_sciezka_dialogu(sciezka) || nazwa_pliku(sciezka)[0] == '\0' ||
        !DialogPliku::czy_poprawna_nazwa_elementu(nazwa_pliku(sciezka))) {
        pokaz_blad("Błąd zapisu", "Ścieżka lub nazwa pliku jest nieprawidłowa.");
        return false;
    }

    const char* dane = memo->tekst();
    const size_t len = memo->dlugosc();
    if (len >= NOTATNIK_POJEMNOSC) {
        pokaz_blad("Błąd zapisu", "Dokument przekracza limit Notatnika.");
        return false;
    }

    BwsMetadanePliku meta{};
    if (pobierz_metadane_pliku(sciezka, &meta) && meta.typ == BWS_TYP_KATALOG) {
        pokaz_blad("Błąd zapisu", "Wskazana ścieżka jest katalogiem.");
        return false;
    }
    const uint32_t dlugosc = static_cast<uint32_t>(len);
    pozostala_kopia[0] = '\0';
    uint32_t poprzedni_rozmiar = 0;
    if (pobierz_rozmiar_pliku(sciezka, &poprzedni_rozmiar)) {
        // Istniejacy plik: kopia bezpieczenstwa, zapis w miejscu, weryfikacja.
        if (!zapisz_z_kopia(sciezka, dane, dlugosc, poprzedni_rozmiar)) return false;
    } else {
        // Nowy plik. Jadro odrzuca utworzenie przy braku katalogu, uprawnien PZB,
        // miejsca albo gdy nazwa jest juz zajeta.
        if (!utworz(sciezka)) {
            pokaz_blad("Błąd zapisu", "Nie można utworzyć pliku: katalog docelowy nie istnieje, brak uprawnień lub miejsca.");
            return false;
        }
        if (!zapisz_i_zweryfikuj(sciezka, dane, dlugosc)) {
            // Usuwany jest wylacznie plik utworzony przed chwila przez ten zapis.
            const bool usunieto = usun_twor_uzytkownika(sciezka);
            pokaz_blad("Błąd zapisu", usunieto
                ? "Zapis nowego pliku nie powiódł się (brak miejsca, uprawnień lub błąd weryfikacji). Niekompletny plik został usunięty. Dokument pozostaje niezapisany."
                : "Zapis nowego pliku nie powiódł się, a niekompletnego pliku nie udało się usunąć. Dokument pozostaje niezapisany.");
            return false;
        }
    }

    DialogPliku::kopiuj_str(biezaca_sciezka, sizeof(biezaca_sciezka), sciezka);
    zmodyfikowany = false;
    aktualizuj_tytul();
    if (pozostala_kopia[0]) ustaw_stan(0, "Zapisano; nie usunięto kopii bezpieczeństwa: ", nazwa_pliku(pozostala_kopia));
    else ustaw_stan(0, "Zapisano: ", nazwa_pliku(biezaca_sciezka));
    return true;
}

bool NotatnikApp::czy_dialog_otwarty() const {
    return okno && okno->zarzadca_popupow().czy_otwarty();
}

void NotatnikApp::pokaz_dialog_pliku(TrybDialoguPliku tryb) {
    if (!okno || czy_dialog_otwarty()) return;
    // Poprzedni dialog jest juz zamkniety (sprawdzone wyzej): bezpieczne zwolnienie.
    delete dialog_pliku;
    dialog_pliku = nullptr;

    char katalog[NOTATNIK_MAKS_SCIEZKI];
    katalog_pliku(biezaca_sciezka, katalog, sizeof(katalog));
    if (!czy_katalog(katalog)) DialogPliku::kopiuj_str(katalog, sizeof(katalog), NOTATNIK_KATALOG_DOMYSLNY);

    dialog_pliku = new DialogPliku(tryb,
        tryb == TrybDialoguPliku::Otworz ? "Otwórz dokument" : "Zapisz dokument jako",
        katalog, NOTATNIK_FILTRY);
    if (!dialog_pliku) return;
    if (tryb == TrybDialoguPliku::Zapisz)
        dialog_pliku->ustaw_nazwe_poczatkowa(biezaca_sciezka[0] ? nazwa_pliku(biezaca_sciezka) : "dokument.txt");
    dialog_pliku->przy_wyniku_pliku([](DialogPliku* d, WynikDialoguPliku w, const char* sciezka, void*) {
        g_notatnik.wynik_pliku_gotowy = true;
        g_notatnik.tryb_wyniku_pliku = d ? d->tryb() : TrybDialoguPliku::Otworz;
        g_notatnik.wynik_pliku = w;
        DialogPliku::kopiuj_str(g_notatnik.sciezka_wyniku, sizeof(g_notatnik.sciezka_wyniku),
                                w == WynikDialoguPliku::Wybrano ? sciezka : "");
    });
    wynik_pliku_gotowy = false;
    dialog_pliku->pokaz(okno->zarzadca_popupow(), memo);
}

// Akcja zastepujaca dokument: przy niezapisanych zmianach najpierw pytanie
// Tak (zapisz) / Nie (odrzuc) / Anuluj.
void NotatnikApp::zadaj(AkcjaDokumentu akcja) {
    if (akcja == AkcjaDokumentu::Brak || czy_dialog_otwarty()) return;
    if (!zmodyfikowany) { wykonaj(akcja); return; }
    if (!okno) return;
    if (!pytanie_zmian) {
        pytanie_zmian = new OknoDialogowe("Niezapisane zmiany",
            "Dokument zawiera niezapisane zmiany.\nCzy zapisać je przed kontynuowaniem?",
            PrzyciskiDialogu::TakNieAnuluj, TypDialogu::Pytanie);
        if (!pytanie_zmian) return;
        pytanie_zmian->przy_wyniku([](OknoDialogowe*, WynikDialogu w, void*) {
            g_notatnik.wynik_pytania = w == WynikDialogu::Brak ? WynikDialogu::Anuluj : w;
        });
    }
    akcja_oczekujaca = akcja;
    wynik_pytania = WynikDialogu::Brak;
    pytanie_zmian->pokaz(okno->zarzadca_popupow(), memo);
}

void NotatnikApp::wykonaj(AkcjaDokumentu akcja) {
    switch (akcja) {
    case AkcjaDokumentu::Nowy: nowy_dokument(); break;
    case AkcjaDokumentu::Otworz: pokaz_dialog_pliku(TrybDialoguPliku::Otworz); break;
    case AkcjaDokumentu::OtworzArgument: otworz_plik(sciezka_argumentu); break;
    case AkcjaDokumentu::Zamknij:
        zamykanie_potwierdzone = true;
        if (okno) okno->zamknij();      // konczy proces (gui_zakoncz_aplikacje)
        else if (app) app->zakoncz();
        break;
    default: break;
    }
}

// Wywolywane po kazdym zdarzeniu okna: dialog, ktory zglosil wynik, jest juz
// zamkniety, wiec kolejny dialog (bledu, zapisu) moze zostac pokazany.
void NotatnikApp::przetworz_oczekujace() {
    if (czy_dialog_otwarty()) return;
    if (wynik_pytania != WynikDialogu::Brak) {
        const WynikDialogu w = wynik_pytania;
        const AkcjaDokumentu a = akcja_oczekujaca;
        wynik_pytania = WynikDialogu::Brak;
        akcja_oczekujaca = AkcjaDokumentu::Brak;
        if (w == WynikDialogu::Tak) {
            if (biezaca_sciezka[0]) {
                if (zapisz_plik_jako(biezaca_sciezka)) wykonaj(a);
            } else {
                akcja_po_zapisie = a;
                pokaz_dialog_pliku(TrybDialoguPliku::Zapisz);
            }
        } else if (w == WynikDialogu::Nie) {
            wykonaj(a);
        }
        return;
    }
    if (wynik_pliku_gotowy) {
        wynik_pliku_gotowy = false;
        const AkcjaDokumentu po_zapisie = akcja_po_zapisie;
        akcja_po_zapisie = AkcjaDokumentu::Brak;
        if (wynik_pliku != WynikDialoguPliku::Wybrano || !sciezka_wyniku[0]) {
            ustaw_stan(0, "Anulowano");
            return;
        }
        if (tryb_wyniku_pliku == TrybDialoguPliku::Otworz) {
            otworz_plik(sciezka_wyniku);
        } else if (tryb_wyniku_pliku == TrybDialoguPliku::Zapisz) {
            if (zapisz_plik_jako(sciezka_wyniku)) wykonaj(po_zapisie);
        }
    }
}

void NotatnikApp::przelacz_zawijanie() {
    if (!memo) return;
    bool cur = memo->czy_zawija_wiersze();
    memo->ustaw_zawijanie_wierszy(!cur);
    ustaw_stan(0, !cur ? "Włączono zawijanie wierszy" : "Wyłączono zawijanie wierszy");
}

// Pomoc: natywne okna podrzedne (BWS 101/102). Jadro uruchamia osobny proces
// okna informacyjnego (tylko PRAWO_GUI), wiaze go z Notatnikiem i aktywuje
// istniejace okno zamiast tworzyc duplikat.
void NotatnikApp::pokaz_pomoc(OknoPodrzedne::Rodzaj rodzaj, const char* tytul, const char* tresc) {
    if (OknoPodrzedne::otworz(rodzaj, tytul, tresc) == OknoPodrzedne::Otwarcie::Odmowa)
        ustaw_stan(0, "Nie można otworzyć okna pomocy: ", tytul);
}

static const char* const TEKST_O_NOTATNIKU =
    "# Notatnik dla Bursztyn OS\n"
    "Natywny edytor zwykłego tekstu systemu Bursztyn OS, zbudowany na frameworku BursztynUI. "
    "Służy do tworzenia i edycji notatek, plików konfiguracyjnych i innych dokumentów tekstowych.\n"
    "\n"
    "# Możliwości\n"
    "Kodowanie\tUTF-8: polskie znaki, inne alfabety i tekst pisany od prawej do lewej (np. hebrajski)\n"
    "Pliki\tnatywne okna Otwórz i Zapisz jako z filtrami typów i nazwami UTF-8\n"
    "Bezpieczny zapis\tkopia bezpieczeństwa oryginału, sprawdzenie zapisu i przywrócenie przy błędzie\n"
    "Ochrona zmian\tpytanie przed utratą niezapisanego dokumentu (Nowy, Otwórz, zamknięcie)\n"
    "Edycja\tcofanie i ponawianie, schowek, menu kontekstowe pod prawym przyciskiem myszy\n"
    "Widok\tzawijanie wierszy, pasek stanu z wierszem, kolumną i liczbą linii\n"
    "\n"
    "# Ograniczenia\n"
    "Dokument\tdo 64 KiB tekstu, bez bajtów NUL\n"
    "Cofanie\tostatnie 7 zmian";

static const char* const TEKST_STEROWANIE_NOTATNIK =
    "# Plik\n"
    "Ctrl+N\tnowy dokument\n"
    "Ctrl+O\totwórz plik\n"
    "Ctrl+S\tzapisz (nowy dokument: Zapisz jako)\n"
    "Ctrl+Shift+S\tzapisz jako\n"
    "\n"
    "# Edycja\n"
    "Ctrl+Z\tcofnij\n"
    "Ctrl+Y lub Ctrl+Shift+Z\tponów\n"
    "Ctrl+X, Ctrl+C, Ctrl+V\twytnij, kopiuj, wklej\n"
    "Ctrl+A\tzaznacz wszystko\n"
    "Backspace, Delete\tusuń znak przed / za kursorem albo zaznaczenie\n"
    "Enter\tnowa linia\n"
    "Prawy przycisk myszy\tmenu kontekstowe: Cofnij, Ponów, Wytnij, Kopiuj, Wklej, Zaznacz wszystko\n"
    "\n"
    "# Polskie znaki\n"
    "AltGr + litera\tą ć ę ł ń ó ś ź ż (układ Polski programisty: AltGr + A, C, E, L, N, O, S, X, Z)\n"
    "AltGr + Shift + litera\tĄ Ć Ę Ł Ń Ó Ś Ź Ż\n"
    "\n"
    "# Poruszanie kursorem\n"
    "Strzałki\to znak lub wiersz\n"
    "Ctrl + strzałka w lewo / w prawo\to słowo\n"
    "Home, End\tpoczątek / koniec wiersza\n"
    "Ctrl+Home, Ctrl+End\tpoczątek / koniec dokumentu\n"
    "Page Up, Page Down\to stronę\n"
    "Shift + ruch kursora\tzaznaczanie; przeciąganie myszą również zaznacza\n"
    "Esc\tusuwa zaznaczenie, zamyka menu i okna dialogowe\n"
    "\n"
    "# Menu\n"
    "Lewy Alt\tprzechodzi do paska menu (Plik, Edycja, Widok, Pomoc)";

// =============================================================================
// Budowa interfejsu
// =============================================================================

static void wykonaj_edycje(IdEdycji id) {
    PoleWielowierszowe* m = g_notatnik.memo;
    if (!m) return;
    switch (id) {
    case IdEdycji::Cofnij: m->cofnij(); break;
    case IdEdycji::Ponow: m->ponow(); break;
    case IdEdycji::Wytnij: m->wytnij_do_schowka(); break;
    case IdEdycji::Kopiuj: m->kopiuj_do_schowka(); break;
    case IdEdycji::Wklej: m->wklej_ze_schowka(); break;
    case IdEdycji::ZaznaczWszystko: m->zaznacz_wszystko(); break;
    case IdEdycji::Wyczysc: m->wyczysc(); break;
    }
    if (g_notatnik.okno) g_notatnik.okno->ustaw_fokus(m);
    g_notatnik.aktualizuj_stan();
}

static void akcja_edycji(ElementMenu& e, void*) { wykonaj_edycje(static_cast<IdEdycji>(e.identyfikator())); }

static void dodaj_edycje(Menu& m, const char* tekst, IdEdycji id, const char* skrot) {
    ElementMenu& e = m.dodaj_element(tekst, static_cast<uint32_t>(id), akcja_edycji);
    if (skrot) e.ustaw_skrot(skrot);
}

// Menu przechowuje elementy w rosnacej tablicy: wskazniki sa pobierane dopiero
// po zakonczeniu budowy menu, gdy tablica nie bedzie juz realokowana.
static ElementMenu* element_edycji(Menu& m, IdEdycji id) {
    for (size_t i = 0; i < m.liczba_elementow(); ++i) {
        ElementMenu* e = m.element(i);
        if (e && !e->czy_separator() && e->identyfikator() == static_cast<uint32_t>(id)) return e;
    }
    return nullptr;
}

static void zbuduj_notatnik(OknoNotatnika& okno) {
    g_notatnik.okno = &okno;

    Panel* korzen = new Panel(Orientacja::Pionowa);
    korzen->ustaw_wypelnienie(Marginesy(2, 2, 2, 2));
    korzen->ustaw_odstep(2);
    okno.ustaw_kontrolke_glowna(korzen);

    // 1. Menu Główne
    MenuGlowne& menu = korzen->dodaj<MenuGlowne>();
    g_notatnik.menu = &menu;

    Menu& m_plik = menu.menu().dodaj_podmenu("Plik", 100);
    m_plik.dodaj_element("Nowy", 101, [](ElementMenu&, void*) { g_notatnik.zadaj(AkcjaDokumentu::Nowy); }).ustaw_skrot("Ctrl+N");
    m_plik.dodaj_element("Otwórz...", 102, [](ElementMenu&, void*) { g_notatnik.zadaj(AkcjaDokumentu::Otworz); }).ustaw_skrot("Ctrl+O");
    m_plik.dodaj_element("Zapisz", 103, [](ElementMenu&, void*) { g_notatnik.zapisz_plik_biezacy(); }).ustaw_skrot("Ctrl+S");
    m_plik.dodaj_element("Zapisz jako...", 104, [](ElementMenu&, void*) { g_notatnik.zapisz_jako(); }).ustaw_skrot("Ctrl+Shift+S");
    m_plik.dodaj_separator();
    m_plik.dodaj_element("Zakończ", 109, [](ElementMenu&, void*) { g_notatnik.zadaj(AkcjaDokumentu::Zamknij); });

    Menu& m_edycja = menu.menu().dodaj_podmenu("Edycja", 200);
    dodaj_edycje(m_edycja, "Cofnij", IdEdycji::Cofnij, "Ctrl+Z");
    dodaj_edycje(m_edycja, "Ponów", IdEdycji::Ponow, "Ctrl+Y");
    m_edycja.dodaj_separator();
    dodaj_edycje(m_edycja, "Wytnij", IdEdycji::Wytnij, "Ctrl+X");
    dodaj_edycje(m_edycja, "Kopiuj", IdEdycji::Kopiuj, "Ctrl+C");
    dodaj_edycje(m_edycja, "Wklej", IdEdycji::Wklej, "Ctrl+V");
    m_edycja.dodaj_separator();
    dodaj_edycje(m_edycja, "Zaznacz wszystko", IdEdycji::ZaznaczWszystko, "Ctrl+A");
    dodaj_edycje(m_edycja, "Wyczyść wszystko", IdEdycji::Wyczysc, nullptr);
    g_notatnik.menu_cofnij = element_edycji(m_edycja, IdEdycji::Cofnij);
    g_notatnik.menu_ponow = element_edycji(m_edycja, IdEdycji::Ponow);
    g_notatnik.menu_wytnij = element_edycji(m_edycja, IdEdycji::Wytnij);
    g_notatnik.menu_kopiuj = element_edycji(m_edycja, IdEdycji::Kopiuj);

    Menu& m_widok = menu.menu().dodaj_podmenu("Widok", 300);
    m_widok.dodaj_element("Zawijanie wierszy", 301, [](ElementMenu&, void*) { g_notatnik.przelacz_zawijanie(); });

    Menu& m_pomoc = menu.menu().dodaj_podmenu("Pomoc", 400);
    m_pomoc.dodaj_element("Sterowanie", 401, [](ElementMenu&, void*) {
        g_notatnik.pokaz_pomoc(OknoPodrzedne::Rodzaj::SterowanieNotatnik, "Sterowanie", TEKST_STEROWANIE_NOTATNIK); });
    m_pomoc.dodaj_separator();
    m_pomoc.dodaj_element("O Notatniku", 402, [](ElementMenu&, void*) {
        g_notatnik.pokaz_pomoc(OknoPodrzedne::Rodzaj::ONotatniku, "O Notatniku", TEKST_O_NOTATNIKU); });

    // 2. Edytor bezposrednio pod paskiem menu (bez osobnego paska narzedzi:
    // polecenia dokumentu sa w menu Plik i pod skrotami klawiszowymi).
    // Edytor (64 KiB pojemności) z menu kontekstowym BursztynUI.
    EdytorNotatnika* memo = new EdytorNotatnika(NOTATNIK_POJEMNOSC);
    g_notatnik.memo = memo;
    memo->ustaw_zawijanie_wierszy(true);
    memo->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    memo->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    memo->ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    memo->przy_zmianie([](PoleWielowierszowe*, const char*, void*) {
        if (!g_notatnik.zmodyfikowany) {
            g_notatnik.zmodyfikowany = true;
            g_notatnik.aktualizuj_tytul();
        }
    });
    MenuKontekstowe* kontekst = new MenuKontekstowe();
    g_notatnik.menu_kontekstowe = kontekst;
    Menu& mk = kontekst->menu();
    dodaj_edycje(mk, "Cofnij", IdEdycji::Cofnij, "Ctrl+Z");
    dodaj_edycje(mk, "Ponów", IdEdycji::Ponow, "Ctrl+Y");
    mk.dodaj_separator();
    dodaj_edycje(mk, "Wytnij", IdEdycji::Wytnij, "Ctrl+X");
    dodaj_edycje(mk, "Kopiuj", IdEdycji::Kopiuj, "Ctrl+C");
    dodaj_edycje(mk, "Wklej", IdEdycji::Wklej, "Ctrl+V");
    mk.dodaj_separator();
    dodaj_edycje(mk, "Zaznacz wszystko", IdEdycji::ZaznaczWszystko, "Ctrl+A");
    const IdEdycji kolejnosc[6] = {IdEdycji::Cofnij, IdEdycji::Ponow, IdEdycji::Wytnij,
                                   IdEdycji::Kopiuj, IdEdycji::Wklej, IdEdycji::ZaznaczWszystko};
    for (size_t i = 0; i < 6; ++i) g_notatnik.kontekst[i] = element_edycji(mk, kolejnosc[i]);
    memo->ustaw_menu_kontekstowe(kontekst);
    korzen->dodaj_kontrolke(memo);

    // 3. Pasek Stanu
    PasekStanu& sb = korzen->dodaj<PasekStanu>();
    g_notatnik.pasek_stanu = &sb;
    sb.ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
    sb.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    sb.dodaj_sekcje("Gotowy", 160, RozmiarSekcjiStanu::Rozciagnij);
    sb.dodaj_sekcje("Wiersz: 1, Kol: 1", 140, RozmiarSekcjiStanu::Stala);
    sb.dodaj_sekcje("Linii: 1", 90, RozmiarSekcjiStanu::Stala);
    sb.dodaj_sekcje("UTF-8", 60, RozmiarSekcjiStanu::Stala);

    okno.ustaw_fokus(memo);
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
    app.ustaw_glowne_okno(&okno);
    zbuduj_notatnik(okno);

    // Sprawdź początkowy argument pliku
    char start_arg[NOTATNIK_MAKS_SCIEZKI] = {};
    if (pobierz_argument_startowy(start_arg, sizeof(start_arg)) && start_arg[0] != '\0') {
        if (!g_notatnik.otworz_plik(start_arg)) g_notatnik.nowy_dokument();
    } else {
        g_notatnik.nowy_dokument();
    }

    app.uruchom(okno);
    bws_zakoncz_proces();
}
