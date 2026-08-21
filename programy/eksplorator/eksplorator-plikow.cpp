/*
 * Bursztyn OS - Eksplorator Plikow
 *
 * Aplikacja Ring 3. Korzysta wylacznie z publicznego API BWS/GUI:
 *   - BWS 6  do listowania katalogow,
 *   - BWS 5/44 do odczytu manifestu paczki .cebula,
 *   - BWS 10 do uruchamiania programu przez loader i PZB.
 *
 * Lista BWS 6 jest parsowana do prywatnych, ograniczonych rekordow. Kod
 * nie wlacza psf.h i nie zna zadnej kernelowej struktury systemu plikow.
 */

#include "../../bursztyn_gui.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct NaglowekBur {
    uint8_t magia[4];
    uint64_t punkt_wejscia;
    uint64_t tekst_przesuniecie;
    uint64_t tekst_rozmiar;
    uint64_t tekst_wirtualny;
    uint64_t dane_przesuniecie;
    uint64_t dane_rozmiar;
    uint64_t dane_wirtualny;
} __attribute__((packed));

static_assert(sizeof(NaglowekBur) == 60U,
              "Naglowek .bur musi miec 60 bajtow");

extern "C" __attribute__((noreturn)) void _start();

extern "C" {

__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},
    reinterpret_cast<uint64_t>(&_start),
    UINT64_C(0x1000), UINT64_C(0x8000), UINT64_C(0x601000),
    UINT64_C(0x9000), UINT64_C(0x20000), UINT64_C(0x609000)
};

}

namespace {

constexpr size_t POJEMNOSC_SCIEZKI = 512U;
constexpr size_t POJEMNOSC_NAZWY = 56U;
constexpr size_t POJEMNOSC_STATUSU = 160U;
constexpr size_t POJEMNOSC_WIDOCZNEGO_TEKSTU = 160U;
constexpr uint32_t POJEMNOSC_LISTY_BWS = 64U * 1024U;
constexpr size_t MAKS_WPISOW = 8192U;
constexpr uint32_t MAKS_MANIFEST = 16U * 1024U;
constexpr size_t POJEMNOSC_ENTRY_MANIFESTU = 256U;
constexpr size_t LIMIT_SCIEZKI_LOADERA = 64U;

constexpr int DOMYSLNY_X = 120;
constexpr int DOMYSLNY_Y = 70;
constexpr int DOMYSLNY_W = 780;
constexpr int DOMYSLNY_H = 520;
constexpr int MIN_W = 520;
constexpr int MIN_H = 320;
constexpr int PASEK_SYSTEMOWY_H = 40;
constexpr int Z_ORDER_OKNA = 10;

constexpr int BELKA_H = 28;
constexpr int TOOLBAR_Y = 34;
constexpr int TOOLBAR_H = 28;
constexpr int LISTA_Y = 70;
constexpr int STATUS_H = 26;
constexpr int MARGINES = 8;
constexpr int WIERSZ_H = 24;
constexpr int SCROLL_W = 28;
constexpr int IKONA = 14;

constexpr uint32_t KOLOR_TLO = 0x00280F00U;
constexpr uint32_t KOLOR_PANEL = 0x00301500U;
constexpr uint32_t KOLOR_RAMKA = 0x00E58A00U;
constexpr uint32_t KOLOR_TEKST = 0x00FFFFFFU;
constexpr uint32_t KOLOR_DRUGI = 0x00D1D5DBU;
constexpr uint32_t KOLOR_ZAZNACZENIA = 0x00603800U;
constexpr uint32_t KOLOR_FOLDER = 0x00FFBF00U;
constexpr uint32_t KOLOR_BUR = 0x0038B000U;
constexpr uint32_t KOLOR_CEBULA = 0x00B040C0U;
constexpr uint32_t KOLOR_PLIK = 0x00708090U;

size_t dlugosc_limit(const char* tekst, size_t limit) {
    if (!tekst) return limit;
    for (size_t i = 0; i < limit; ++i)
        if (tekst[i] == '\0') return i;
    return limit;
}

void wyzeruj(void* ptr, size_t ile) {
    if (!ptr) return;
    uint8_t* p = static_cast<uint8_t*>(ptr);
    for (size_t i = 0; i < ile; ++i) p[i] = 0;
}

bool kopiuj_tekst(char* cel, size_t pojemnosc, const char* zrodlo) {
    if (!cel || pojemnosc == 0 || !zrodlo) return false;
    const size_t len = dlugosc_limit(zrodlo, pojemnosc);
    if (len >= pojemnosc) {
        cel[0] = '\0';
        return false;
    }
    for (size_t i = 0; i <= len; ++i) cel[i] = zrodlo[i];
    return true;
}

bool tekst_rowny(const char* a, const char* b) {
    if (!a || !b) return false;
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return false;
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
}

bool tekst_konczy_sie(const char* tekst, const char* sufiks) {
    if (!tekst || !sufiks) return false;
    const size_t lt = dlugosc_limit(tekst, POJEMNOSC_SCIEZKI);
    const size_t ls = dlugosc_limit(sufiks, POJEMNOSC_SCIEZKI);
    if (lt >= POJEMNOSC_SCIEZKI || ls >= POJEMNOSC_SCIEZKI || ls > lt)
        return false;
    for (size_t i = 0; i < ls; ++i)
        if (tekst[lt - ls + i] != sufiks[i]) return false;
    return true;
}

bool znak_kontynuacji_utf8(uint8_t c) {
    return (c & 0xC0U) == 0x80U;
}

int dlugosc_kodpunktu_utf8(uint8_t c) {
    if (c >= 0xC2U && c <= 0xDFU) return 2;
    if (c >= 0xE0U && c <= 0xEFU) return 3;
    if (c >= 0xF0U && c <= 0xF4U) return 4;
    return 1;
}

size_t granica_utf8_przed(const char* tekst, size_t limit) {
    if (!tekst) return 0;
    size_t p = 0;
    size_t ostatnia = 0;
    while (p < limit && tekst[p] != '\0') {
        int n = dlugosc_kodpunktu_utf8(static_cast<uint8_t>(tekst[p]));
        if (n > 1) {
            bool poprawny = p + static_cast<size_t>(n) <= limit;
            for (int i = 1; poprawny && i < n; ++i)
                poprawny = znak_kontynuacji_utf8(
                    static_cast<uint8_t>(tekst[p + static_cast<size_t>(i)]));
            if (!poprawny) break;
        }
        p += static_cast<size_t>(n);
        ostatnia = p;
    }
    return ostatnia;
}

void skroc_z_ellipsis(const char* tekst, char* wynik, size_t pojemnosc,
                      int maks_bajtow, bool pokaz_koniec) {
    if (!wynik || pojemnosc == 0) return;
    wynik[0] = '\0';
    if (!tekst || maks_bajtow <= 0) return;
    size_t len = dlugosc_limit(tekst, POJEMNOSC_SCIEZKI);
    if (len >= POJEMNOSC_SCIEZKI) return;
    size_t limit = static_cast<size_t>(maks_bajtow);
    if (limit >= pojemnosc) limit = pojemnosc - 1U;
    if (len <= limit) {
        (void)kopiuj_tekst(wynik, pojemnosc, tekst);
        return;
    }
    if (limit < 4U) return;

    wynik[0] = wynik[1] = wynik[2] = '.';
    if (pokaz_koniec) {
        size_t start = len - (limit - 3U);
        while (start < len &&
               znak_kontynuacji_utf8(static_cast<uint8_t>(tekst[start])))
            ++start;
        size_t out = 3;
        while (start < len && out + 1U < pojemnosc && out < limit)
            wynik[out++] = tekst[start++];
        wynik[out] = '\0';
        return;
    }

    const size_t bezpieczne = granica_utf8_przed(tekst, limit - 3U);
    for (size_t i = 0; i < bezpieczne; ++i) wynik[i] = tekst[i];
    size_t out = bezpieczne;
    wynik[out++] = '.';
    wynik[out++] = '.';
    wynik[out++] = '.';
    wynik[out] = '\0';
}

bool poprawna_nazwa(const char* nazwa) {
    if (!nazwa) return false;
    const size_t len = dlugosc_limit(nazwa, POJEMNOSC_NAZWY);
    if (len == 0 || len >= POJEMNOSC_NAZWY) return false;
    if (tekst_rowny(nazwa, ".") || tekst_rowny(nazwa, "..")) return false;
    for (size_t i = 0; i < len; ++i) {
        const uint8_t c = static_cast<uint8_t>(nazwa[i]);
        if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\') return false;
    }
    return true;
}

bool dolacz_segment(const char* baza, const char* segment,
                    char* wynik, size_t pojemnosc) {
    if (!baza || !poprawna_nazwa(segment) || !wynik || pojemnosc == 0)
        return false;
    const size_t lb = dlugosc_limit(baza, pojemnosc);
    const size_t ls = dlugosc_limit(segment, POJEMNOSC_NAZWY);
    if (lb >= pojemnosc || ls >= POJEMNOSC_NAZWY || lb == 0 || baza[0] != '/')
        return false;
    const bool root = lb == 1U && baza[0] == '/';
    const size_t wymagane = lb + (root ? 0U : 1U) + ls + 1U;
    if (wymagane > pojemnosc) return false;
    size_t out = 0;
    for (size_t i = 0; i < lb; ++i) wynik[out++] = baza[i];
    if (!root) wynik[out++] = '/';
    for (size_t i = 0; i < ls; ++i) wynik[out++] = segment[i];
    wynik[out] = '\0';
    return true;
}

bool dolacz_sciezke_wzgledna(const char* baza, const char* wzgledna,
                             char* wynik, size_t pojemnosc) {
    if (!baza || !wzgledna || !wynik || pojemnosc == 0 || wzgledna[0] == '/')
        return false;
    const size_t lb = dlugosc_limit(baza, pojemnosc);
    const size_t lr = dlugosc_limit(wzgledna, pojemnosc);
    if (lb == 0 || lb >= pojemnosc || lr == 0 || lr >= pojemnosc ||
        baza[0] != '/') return false;

    size_t start = 0;
    while (start < lr) {
        size_t koniec = start;
        while (koniec < lr && wzgledna[koniec] != '/') ++koniec;
        const size_t segment_len = koniec - start;
        if (segment_len == 0 || segment_len >= POJEMNOSC_NAZWY) return false;
        char segment[POJEMNOSC_NAZWY] = {};
        for (size_t i = 0; i < segment_len; ++i) segment[i] = wzgledna[start + i];
        if (!poprawna_nazwa(segment)) return false;
        start = koniec + (koniec < lr ? 1U : 0U);
    }

    const bool root = lb == 1U && baza[0] == '/';
    const size_t wymagane = lb + (root ? 0U : 1U) + lr + 1U;
    if (wymagane > pojemnosc) return false;
    size_t out = 0;
    for (size_t i = 0; i < lb; ++i) wynik[out++] = baza[i];
    if (!root) wynik[out++] = '/';
    for (size_t i = 0; i < lr; ++i) wynik[out++] = wzgledna[i];
    wynik[out] = '\0';
    return true;
}

bool sciezka_rodzica(const char* obecna, char* wynik, size_t pojemnosc) {
    if (!obecna || !wynik || pojemnosc < 2U || obecna[0] != '/') return false;
    const size_t len = dlugosc_limit(obecna, pojemnosc);
    if (len == 0 || len >= pojemnosc) return false;
    if (len == 1U) return kopiuj_tekst(wynik, pojemnosc, "/");
    size_t slash = len;
    while (slash > 0 && obecna[slash - 1U] != '/') --slash;
    if (slash <= 1U) return kopiuj_tekst(wynik, pojemnosc, "/");
    const size_t nowy_len = slash - 1U;
    if (nowy_len + 1U > pojemnosc) return false;
    for (size_t i = 0; i < nowy_len; ++i) wynik[i] = obecna[i];
    wynik[nowy_len] = '\0';
    return true;
}

enum class TypWpisu : uint8_t {
    RODZIC = 0,
    FOLDER,
    PROGRAM_BUR,
    PACZKA_CEBULA,
    PLIK
};

struct WpisListy {
    char nazwa[POJEMNOSC_NAZWY];
    TypWpisu typ;
    bool katalog;
};

enum class WynikAktywacji : uint8_t {
    TYLKO_STATUS = 0,
    ZMIANA_KATALOGU
};

class ModelEksploratora {
public:
    ModelEksploratora()
        : wpisy_(nullptr), bufor_listy_(nullptr), liczba_(0),
          zaznaczony_(-1), scroll_(0), gotowy_(false) {
        wyzeruj(sciezka_, sizeof(sciezka_));
        wyzeruj(status_, sizeof(status_));
        (void)kopiuj_tekst(sciezka_, sizeof(sciezka_), "/");
        ustaw_status("Uruchamianie Eksploratora...");
    }

    ~ModelEksploratora() {
        gui_free(wpisy_);
        gui_free(bufor_listy_);
    }

    bool inicjalizuj() {
        wpisy_ = static_cast<WpisListy*>(gui_malloc(
            sizeof(WpisListy) * MAKS_WPISOW));
        bufor_listy_ = static_cast<char*>(gui_malloc(POJEMNOSC_LISTY_BWS));
        if (!wpisy_ || !bufor_listy_) {
            ustaw_status("Blad: brak pamieci na liste katalogu.");
            return false;
        }
        gotowy_ = zaladuj_sciezke("/");
        return gotowy_;
    }

    const char* sciezka() const { return sciezka_; }
    const char* status() const { return status_; }
    int liczba() const { return liczba_; }
    int zaznaczony() const { return zaznaczony_; }
    int scroll() const { return scroll_; }
    const WpisListy* wpis(int indeks) const {
        return indeks >= 0 && indeks < liczba_ ? &wpisy_[indeks] : nullptr;
    }

    void ustaw_status(const char* tekst) {
        if (!kopiuj_tekst(status_, sizeof(status_), tekst))
            (void)kopiuj_tekst(status_, sizeof(status_), "Blad komunikatu.");
    }

    bool zaladuj_sciezke(const char* nowa_sciezka) {
        if (!gotowa_sciezka_absolutna(nowa_sciezka)) {
            ustaw_status("Blad: nieprawidlowa albo zbyt dluga sciezka.");
            return false;
        }
        wyzeruj(bufor_listy_, POJEMNOSC_LISTY_BWS);
        if (!wylistuj_katalog_uzytkownika(
                nowa_sciezka, bufor_listy_, POJEMNOSC_LISTY_BWS)) {
            ustaw_status("BWS 6: katalog nie istnieje albo brak prawa odczytu.");
            return false;
        }

        const size_t len = dlugosc_limit(bufor_listy_, POJEMNOSC_LISTY_BWS);
        if (len >= POJEMNOSC_LISTY_BWS ||
            (len != 0 && bufor_listy_[len - 1U] != '\n')) {
            ustaw_status("BWS 6: lista katalogu jest niepelna (limit 64 KiB).");
            return false;
        }

        size_t liczba_docelowa = tekst_rowny(nowa_sciezka, "/") ? 0U : 1U;
        if (!waliduj_liste(len, &liczba_docelowa)) return false;
        if (liczba_docelowa > MAKS_WPISOW) {
            ustaw_status("Blad: katalog ma zbyt wiele wpisow.");
            return false;
        }

        size_t indeks = 0;
        if (!tekst_rowny(nowa_sciezka, "/")) {
            wyzeruj(&wpisy_[0], sizeof(wpisy_[0]));
            (void)kopiuj_tekst(wpisy_[0].nazwa, sizeof(wpisy_[0].nazwa), "..");
            wpisy_[0].typ = TypWpisu::RODZIC;
            wpisy_[0].katalog = true;
            indeks = 1;
        }
        wypelnij_liste(len, &indeks);
        if (indeks != liczba_docelowa ||
            !kopiuj_tekst(sciezka_, sizeof(sciezka_), nowa_sciezka)) {
            ustaw_status("Blad wewnetrzny parsera listy katalogu.");
            return false;
        }
        liczba_ = static_cast<int>(indeks);
        zaznaczony_ = -1;
        scroll_ = 0;
        ustaw_status_liczbe("Wpisow: ", liczba_, ".");
        gotowy_ = true;
        return true;
    }

    bool przejdz_rodzic() {
        char rodzic[POJEMNOSC_SCIEZKI] = {};
        if (!sciezka_rodzica(sciezka_, rodzic, sizeof(rodzic))) {
            ustaw_status("Blad: nie mozna wyznaczyc katalogu rodzica.");
            return false;
        }
        if (tekst_rowny(rodzic, sciezka_)) {
            ustaw_status("To jest katalog root.");
            return false;
        }
        return zaladuj_sciezke(rodzic);
    }

    bool ustaw_zaznaczenie(int indeks, int widoczne, bool* scroll_zmieniony) {
        if (scroll_zmieniony) *scroll_zmieniony = false;
        if (indeks < 0 || indeks >= liczba_) return false;
        zaznaczony_ = indeks;
        const int stary_scroll = scroll_;
        if (zaznaczony_ < scroll_) scroll_ = zaznaczony_;
        if (widoczne > 0 && zaznaczony_ >= scroll_ + widoczne)
            scroll_ = zaznaczony_ - widoczne + 1;
        ogranicz_scroll(widoczne);
        if (scroll_zmieniony) *scroll_zmieniony = stary_scroll != scroll_;
        return true;
    }

    bool przesun_zaznaczenie(int delta, int widoczne, int* stary,
                             bool* scroll_zmieniony) {
        if (stary) *stary = zaznaczony_;
        if (liczba_ <= 0) return false;
        int nowy = zaznaczony_ < 0 ? 0 : zaznaczony_ + delta;
        if (nowy < 0) nowy = 0;
        if (nowy >= liczba_) nowy = liczba_ - 1;
        return ustaw_zaznaczenie(nowy, widoczne, scroll_zmieniony);
    }

    bool przewin(int delta, int widoczne) {
        const int stary = scroll_;
        scroll_ += delta;
        ogranicz_scroll(widoczne);
        return stary != scroll_;
    }

    void ogranicz_scroll(int widoczne) {
        if (widoczne < 1) widoczne = 1;
        int maks = liczba_ - widoczne;
        if (maks < 0) maks = 0;
        if (scroll_ < 0) scroll_ = 0;
        if (scroll_ > maks) scroll_ = maks;
    }

    WynikAktywacji aktywuj_zaznaczony() {
        if (zaznaczony_ < 0 || zaznaczony_ >= liczba_) {
            ustaw_status("Najpierw zaznacz element.");
            return WynikAktywacji::TYLKO_STATUS;
        }
        const WpisListy& e = wpisy_[zaznaczony_];
        if (e.typ == TypWpisu::RODZIC)
            return przejdz_rodzic() ? WynikAktywacji::ZMIANA_KATALOGU
                                    : WynikAktywacji::TYLKO_STATUS;

        char pelna[POJEMNOSC_SCIEZKI] = {};
        if (!dolacz_segment(sciezka_, e.nazwa, pelna, sizeof(pelna))) {
            ustaw_status("Blad: pelna sciezka przekracza 511 bajtow.");
            return WynikAktywacji::TYLKO_STATUS;
        }

        if (e.typ == TypWpisu::FOLDER)
            return zaladuj_sciezke(pelna) ? WynikAktywacji::ZMIANA_KATALOGU
                                         : WynikAktywacji::TYLKO_STATUS;
        if (e.typ == TypWpisu::PROGRAM_BUR) {
            uruchom_bur(pelna);
            return WynikAktywacji::TYLKO_STATUS;
        }
        if (e.typ == TypWpisu::PACZKA_CEBULA) {
            uruchom_paczke(pelna, e.katalog);
            return WynikAktywacji::TYLKO_STATUS;
        }
        ustaw_status("Nieobslugiwany typ pliku; element pozostaje zaznaczony.");
        return WynikAktywacji::TYLKO_STATUS;
    }

private:
    WpisListy* wpisy_;
    char* bufor_listy_;
    char sciezka_[POJEMNOSC_SCIEZKI];
    char status_[POJEMNOSC_STATUSU];
    int liczba_;
    int zaznaczony_;
    int scroll_;
    bool gotowy_;

    bool gotowa_sciezka_absolutna(const char* sciezka) const {
        if (!sciezka || sciezka[0] != '/') return false;
        const size_t len = dlugosc_limit(sciezka, POJEMNOSC_SCIEZKI);
        if (len == 0 || len >= POJEMNOSC_SCIEZKI) return false;
        if (len > 1U && sciezka[len - 1U] == '/') return false;
        size_t start = 1;
        while (start < len) {
            size_t koniec = start;
            while (koniec < len && sciezka[koniec] != '/') ++koniec;
            const size_t n = koniec - start;
            if (n == 0 || n >= POJEMNOSC_NAZWY) return false;
            char segment[POJEMNOSC_NAZWY] = {};
            for (size_t i = 0; i < n; ++i) segment[i] = sciezka[start + i];
            if (!poprawna_nazwa(segment)) return false;
            start = koniec + (koniec < len ? 1U : 0U);
        }
        return true;
    }

    bool waliduj_liste(size_t len, size_t* liczba_docelowa) {
        if (!liczba_docelowa) return false;
        size_t p = 0;
        while (p < len) {
            size_t koniec = p;
            while (koniec < len && bufor_listy_[koniec] != '\n') ++koniec;
            if (koniec >= len || koniec - p <= 7U) {
                ustaw_status("BWS 6: uszkodzony rekord listy katalogu.");
                return false;
            }
            bool katalog = false;
            if (bufor_listy_[p] == '[' && bufor_listy_[p + 1U] == 'K' &&
                bufor_listy_[p + 2U] == 'A' && bufor_listy_[p + 3U] == 'T' &&
                bufor_listy_[p + 4U] == ']' && bufor_listy_[p + 5U] == ' ' &&
                bufor_listy_[p + 6U] == ' ') katalog = true;
            const bool plik =
                bufor_listy_[p] == '[' && bufor_listy_[p + 1U] == 'P' &&
                bufor_listy_[p + 2U] == 'L' && bufor_listy_[p + 3U] == 'I' &&
                bufor_listy_[p + 4U] == 'K' && bufor_listy_[p + 5U] == ']' &&
                bufor_listy_[p + 6U] == ' ';
            if (!katalog && !plik) {
                ustaw_status("BWS 6: nieznany typ rekordu katalogu.");
                return false;
            }
            const size_t nazwa_len = koniec - (p + 7U);
            if (nazwa_len == 0 || nazwa_len >= POJEMNOSC_NAZWY) {
                ustaw_status("BWS 6: nazwa wpisu przekracza limit 55 bajtow.");
                return false;
            }
            char nazwa[POJEMNOSC_NAZWY] = {};
            for (size_t i = 0; i < nazwa_len; ++i)
                nazwa[i] = bufor_listy_[p + 7U + i];
            if (!poprawna_nazwa(nazwa)) {
                ustaw_status("BWS 6: wpis zawiera niebezpieczna nazwe.");
                return false;
            }
            ++(*liczba_docelowa);
            if (*liczba_docelowa > MAKS_WPISOW) return false;
            p = koniec + 1U;
        }
        return true;
    }

    void wypelnij_liste(size_t len, size_t* indeks) {
        size_t p = 0;
        while (p < len && *indeks < MAKS_WPISOW) {
            size_t koniec = p;
            while (koniec < len && bufor_listy_[koniec] != '\n') ++koniec;
            WpisListy& e = wpisy_[(*indeks)++];
            wyzeruj(&e, sizeof(e));
            e.katalog = bufor_listy_[p + 1U] == 'K';
            const size_t nazwa_len = koniec - (p + 7U);
            for (size_t i = 0; i < nazwa_len; ++i)
                e.nazwa[i] = bufor_listy_[p + 7U + i];
            e.nazwa[nazwa_len] = '\0';
            if (tekst_konczy_sie(e.nazwa, ".cebula"))
                e.typ = TypWpisu::PACZKA_CEBULA;
            else if (e.katalog)
                e.typ = TypWpisu::FOLDER;
            else if (tekst_konczy_sie(e.nazwa, ".bur"))
                e.typ = TypWpisu::PROGRAM_BUR;
            else
                e.typ = TypWpisu::PLIK;
            p = koniec + 1U;
        }
    }

    void ustaw_status_liczbe(const char* prefiks, int liczba,
                             const char* sufiks) {
        char wynik[POJEMNOSC_STATUSU] = {};
        size_t out = 0;
        for (size_t i = 0; prefiks && prefiks[i] != '\0' && out + 1U < sizeof(wynik); ++i)
            wynik[out++] = prefiks[i];
        char cyfry[16] = {};
        size_t n = 0;
        unsigned int v = liczba < 0 ? 0U : static_cast<unsigned int>(liczba);
        do {
            cyfry[n++] = static_cast<char>('0' + (v % 10U));
            v /= 10U;
        } while (v != 0 && n < sizeof(cyfry));
        while (n > 0 && out + 1U < sizeof(wynik)) wynik[out++] = cyfry[--n];
        for (size_t i = 0; sufiks && sufiks[i] != '\0' && out + 1U < sizeof(wynik); ++i)
            wynik[out++] = sufiks[i];
        wynik[out] = '\0';
        ustaw_status(wynik);
    }

    void uruchom_bur(const char* sciezka_programu) {
        const size_t len = dlugosc_limit(sciezka_programu, POJEMNOSC_SCIEZKI);
        if (len >= LIMIT_SCIEZKI_LOADERA) {
            ustaw_status("Loader: sciezka .bur przekracza 63 bajty.");
            return;
        }
        if (uruchom_program_uzytkownika(sciezka_programu))
            ustaw_status("Program przekazany do loadera.");
        else
            ustaw_status("BWS 10: odmowa PZB, blad loadera lub program juz dziala.");
    }

    bool parsuj_plik_startowy(const char* manifest, size_t len,
                              char* entry, size_t pojemnosc) {
        if (!manifest || !entry || pojemnosc == 0) return false;
        entry[0] = '\0';
        bool znaleziono = false;
        size_t p = 0;
        static const char KLUCZ[] = "plik_startowy";
        while (p < len) {
            size_t koniec = p;
            while (koniec < len && manifest[koniec] != '\n' && manifest[koniec] != '\r')
                ++koniec;
            size_t q = p;
            while (q < koniec && (manifest[q] == ' ' || manifest[q] == '\t')) ++q;
            bool klucz = true;
            size_t k = 0;
            while (KLUCZ[k] != '\0') {
                if (q + k >= koniec || manifest[q + k] != KLUCZ[k]) {
                    klucz = false;
                    break;
                }
                ++k;
            }
            if (klucz) {
                q += k;
                while (q < koniec && (manifest[q] == ' ' || manifest[q] == '\t')) ++q;
                if (q >= koniec || manifest[q++] != '=') return false;
                while (q < koniec && (manifest[q] == ' ' || manifest[q] == '\t')) ++q;
                if (q >= koniec || manifest[q++] != '"' || znaleziono) return false;
                size_t out = 0;
                while (q < koniec && manifest[q] != '"') {
                    const uint8_t c = static_cast<uint8_t>(manifest[q]);
                    if (c < 0x20U || c == '\\' || out + 1U >= pojemnosc) return false;
                    entry[out++] = manifest[q++];
                }
                if (q >= koniec || manifest[q++] != '"' || out == 0) return false;
                while (q < koniec && (manifest[q] == ' ' || manifest[q] == '\t')) ++q;
                if (q != koniec) return false;
                entry[out] = '\0';
                znaleziono = true;
            }
            p = koniec;
            while (p < len && (manifest[p] == '\n' || manifest[p] == '\r')) ++p;
        }
        return znaleziono;
    }

    void uruchom_paczke(const char* sciezka_paczki, bool katalog) {
        if (!katalog) {
            ustaw_status("Paczka .cebula musi byc katalogiem z manifestem.");
            return;
        }
        char manifest_path[POJEMNOSC_SCIEZKI] = {};
        if (!dolacz_segment(sciezka_paczki, "opis.aplikacji",
                            manifest_path, sizeof(manifest_path))) {
            ustaw_status("Paczka: sciezka manifestu jest zbyt dluga.");
            return;
        }
        uint32_t rozmiar = 0;
        if (!pobierz_rozmiar_pliku(manifest_path, &rozmiar)) {
            ustaw_status("Paczka: brak czytelnego opis.aplikacji.");
            return;
        }
        if (rozmiar == 0 || rozmiar > MAKS_MANIFEST) {
            ustaw_status("Paczka: manifest jest pusty albo przekracza 16 KiB.");
            return;
        }
        char* manifest = static_cast<char*>(gui_malloc(
            static_cast<unsigned long>(rozmiar) + 1UL));
        if (!manifest) {
            ustaw_status("Paczka: brak pamieci na manifest.");
            return;
        }
        wyzeruj(manifest, static_cast<size_t>(rozmiar) + 1U);
        const bool odczyt = czytaj_plik(manifest_path, manifest, rozmiar);
        if (!odczyt) {
            gui_free(manifest);
            ustaw_status("Paczka: BWS 5 nie odczytal manifestu.");
            return;
        }
        for (uint32_t i = 0; i < rozmiar; ++i) {
            if (manifest[i] == '\0') {
                gui_free(manifest);
                ustaw_status("Paczka: manifest zawiera osadzony NUL.");
                return;
            }
        }
        manifest[rozmiar] = '\0';
        char entry[POJEMNOSC_ENTRY_MANIFESTU] = {};
        const bool manifest_ok = parsuj_plik_startowy(
            manifest, rozmiar, entry, sizeof(entry));
        gui_free(manifest);
        if (!manifest_ok || !tekst_konczy_sie(entry, ".bur")) {
            ustaw_status("Paczka: brak poprawnego plik_startowy .bur.");
            return;
        }
        char program[POJEMNOSC_SCIEZKI] = {};
        if (!dolacz_sciezke_wzgledna(
                sciezka_paczki, entry, program, sizeof(program))) {
            ustaw_status("Paczka: niebezpieczny albo zbyt dlugi plik_startowy.");
            return;
        }
        uruchom_bur(program);
    }
};

struct Prostokat {
    int x;
    int y;
    int w;
    int h;
};

class OknoEksploratora {
public:
    int x = DOMYSLNY_X;
    int y = DOMYSLNY_Y;
    int w = DOMYSLNY_W;
    int h = DOMYSLNY_H;
    int restore_x = DOMYSLNY_X;
    int restore_y = DOMYSLNY_Y;
    int restore_w = DOMYSLNY_W;
    int restore_h = DOMYSLNY_H;
    int screen_w = 1024;
    int screen_h = 768;
    bool maksymalizowane = false;
    bool zminimalizowane = false;

    void ogranicz() {
        if (screen_w < MIN_W) screen_w = MIN_W;
        if (screen_h < MIN_H + PASEK_SYSTEMOWY_H)
            screen_h = MIN_H + PASEK_SYSTEMOWY_H;
        if (w < MIN_W) w = MIN_W;
        if (h < MIN_H) h = MIN_H;
        if (w > screen_w) w = screen_w;
        if (h > screen_h - PASEK_SYSTEMOWY_H) h = screen_h - PASEK_SYSTEMOWY_H;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x + w > screen_w) x = screen_w - w;
        if (y + h > screen_h - PASEK_SYSTEMOWY_H)
            y = screen_h - PASEK_SYSTEMOWY_H - h;
    }

    bool utworz_warstwe() {
        ogranicz();
        return bws_utworz_warstwe(x, y, w, h, Z_ORDER_OKNA) >= 0;
    }

    bool przelacz_maksymalizacje() {
        const int sx = x, sy = y, sw = w, sh = h;
        if (!maksymalizowane) {
            restore_x = x; restore_y = y; restore_w = w; restore_h = h;
            x = 0; y = 0; w = screen_w; h = screen_h - PASEK_SYSTEMOWY_H;
        } else {
            x = restore_x; y = restore_y; w = restore_w; h = restore_h;
        }
        ogranicz();
        if (!utworz_warstwe()) {
            x = sx; y = sy; w = sw; h = sh;
            (void)utworz_warstwe();
            return false;
        }
        maksymalizowane = !maksymalizowane;
        return true;
    }

    Prostokat lista() const {
        return {x + MARGINES, y + LISTA_Y,
                w - MARGINES * 2 - SCROLL_W - 4,
                h - LISTA_Y - STATUS_H - 4};
    }

    Prostokat status() const {
        return {x + 4, y + h - STATUS_H, w - 8, STATUS_H - 2};
    }

    int widoczne_wiersze() const {
        const Prostokat p = lista();
        const int n = p.h / WIERSZ_H;
        return n > 0 ? n : 1;
    }
};

bool punkt_w_prostokacie(int px, int py, const Prostokat& r) {
    return r.w > 0 && r.h > 0 && px >= r.x && py >= r.y &&
           px < r.x + r.w && py < r.y + r.h;
}

class WidokEksploratora {
public:
    WidokEksploratora(ModelEksploratora& model, OknoEksploratora& okno)
        : model_(model), okno_(okno) {}

    void rysuj_pelne(bool wyczysc_warstwe) {
        if (okno_.zminimalizowane) return;
        if (wyczysc_warstwe) gui_odswiez_pulpit();
        gui_rysuj_okno(okno_.x, okno_.y, okno_.w, okno_.h,
                       "Eksplorator Plikow");
        gui_rysuj_standardowa_belke(
            okno_.x, okno_.y, okno_.w, "Eksplorator Plikow",
            okno_.maksymalizowane);
        rysuj_zawartosc();
        gui_odswiez();
    }

    void rysuj_zawartosc() {
        gui_rysuj_prostokat(okno_.x + 2, okno_.y + BELKA_H,
                            okno_.w - 4, okno_.h - BELKA_H - 2, KOLOR_TLO);
        rysuj_toolbar();
        rysuj_liste();
        rysuj_status();
    }

    void rysuj_toolbar() {
        const int y = okno_.y + TOOLBAR_Y;
        RysujPrzycisk(okno_.x + MARGINES, y, 30, TOOLBAR_H,
                      KOLOR_RAMKA, KOLOR_TLO, "..");
        const int px = okno_.x + MARGINES + 36;
        const int pw = okno_.w - MARGINES * 2 - 36;
        gui_rysuj_prostokat(px, y, pw, TOOLBAR_H, KOLOR_PANEL);
        gui_rysuj_prostokat(px, y, pw, 1, KOLOR_RAMKA);
        char pokaz[POJEMNOSC_WIDOCZNEGO_TEKSTU] = {};
        int maks = (pw - 12) / 9;
        if (maks < 1) maks = 1;
        skroc_z_ellipsis(model_.sciezka(), pokaz, sizeof(pokaz), maks, true);
        gui_wypisz_tekst_kolor(px + 6, y + 6, KOLOR_TEKST, pokaz);
    }

    void rysuj_liste() {
        const Prostokat lista = okno_.lista();
        gui_rysuj_prostokat(lista.x, lista.y, lista.w, lista.h, KOLOR_TLO);
        gui_rysuj_prostokat(lista.x, lista.y, lista.w, 1, KOLOR_RAMKA);
        gui_rysuj_prostokat(lista.x, lista.y, 1, lista.h, KOLOR_RAMKA);
        gui_rysuj_prostokat(lista.x + lista.w - 1, lista.y, 1, lista.h,
                            KOLOR_RAMKA);
        const int widoczne = okno_.widoczne_wiersze();
        for (int i = 0; i < widoczne; ++i)
            rysuj_wiersz(model_.scroll() + i);
        rysuj_scroll();
    }

    void rysuj_wiersz(int indeks) {
        const Prostokat lista = okno_.lista();
        const int lokalny = indeks - model_.scroll();
        if (lokalny < 0 || lokalny >= okno_.widoczne_wiersze()) return;
        const int y = lista.y + lokalny * WIERSZ_H + 1;
        const int h = (y + WIERSZ_H <= lista.y + lista.h)
            ? WIERSZ_H : lista.y + lista.h - y;
        if (h <= 0) return;
        const bool wybrany = indeks == model_.zaznaczony();
        gui_rysuj_prostokat(lista.x + 1, y, lista.w - 2, h,
                            wybrany ? KOLOR_ZAZNACZENIA : KOLOR_TLO);
        const WpisListy* e = model_.wpis(indeks);
        if (!e) return;
        uint32_t kolor = KOLOR_PLIK;
        if (e->typ == TypWpisu::FOLDER || e->typ == TypWpisu::RODZIC)
            kolor = KOLOR_FOLDER;
        else if (e->typ == TypWpisu::PROGRAM_BUR)
            kolor = KOLOR_BUR;
        else if (e->typ == TypWpisu::PACZKA_CEBULA)
            kolor = KOLOR_CEBULA;
        gui_rysuj_prostokat(lista.x + 7, y + 5, IKONA, IKONA, kolor);
        if (e->katalog)
            gui_rysuj_prostokat(lista.x + 9, y + 3, IKONA - 5, 4, kolor);
        char nazwa[POJEMNOSC_WIDOCZNEGO_TEKSTU] = {};
        int maks = (lista.w - 38) / 9;
        if (maks < 1) maks = 1;
        skroc_z_ellipsis(e->nazwa, nazwa, sizeof(nazwa), maks, false);
        gui_wypisz_tekst_kolor(lista.x + 28, y + 5,
                               wybrany ? KOLOR_TEKST : KOLOR_DRUGI, nazwa);
    }

    void rysuj_status() {
        const Prostokat s = okno_.status();
        gui_rysuj_prostokat(s.x, s.y, s.w, s.h, KOLOR_PANEL);
        gui_rysuj_prostokat(s.x, s.y, s.w, 1, KOLOR_RAMKA);
        char pokaz[POJEMNOSC_WIDOCZNEGO_TEKSTU] = {};
        int maks = (s.w - 12) / 9;
        if (maks < 1) maks = 1;
        skroc_z_ellipsis(model_.status(), pokaz, sizeof(pokaz), maks, false);
        gui_wypisz_tekst_kolor(s.x + 6, s.y + 5, KOLOR_TEKST, pokaz);
    }

    void rysuj_scroll() {
        const Prostokat lista = okno_.lista();
        const int x = lista.x + lista.w + 4;
        RysujPrzycisk(x, lista.y, SCROLL_W, 24,
                      KOLOR_RAMKA, KOLOR_TLO, "^");
        RysujPrzycisk(x, lista.y + lista.h - 24, SCROLL_W, 24,
                      KOLOR_RAMKA, KOLOR_TLO, "v");
    }

    int indeks_wiersza(int mx, int my) const {
        const Prostokat lista = okno_.lista();
        if (!punkt_w_prostokacie(mx, my, lista)) return -1;
        const int lokalny = (my - lista.y - 1) / WIERSZ_H;
        if (lokalny < 0 || lokalny >= okno_.widoczne_wiersze()) return -1;
        const int indeks = model_.scroll() + lokalny;
        return indeks < model_.liczba() ? indeks : -1;
    }

    bool klik_scroll_gora(int mx, int my) const {
        const Prostokat lista = okno_.lista();
        return punkt_w_prostokacie(mx, my,
            {lista.x + lista.w + 4, lista.y, SCROLL_W, 24});
    }

    bool klik_scroll_dol(int mx, int my) const {
        const Prostokat lista = okno_.lista();
        return punkt_w_prostokacie(mx, my,
            {lista.x + lista.w + 4, lista.y + lista.h - 24, SCROLL_W, 24});
    }

    bool klik_rodzic(int mx, int my) const {
        return punkt_w_prostokacie(mx, my,
            {okno_.x + MARGINES, okno_.y + TOOLBAR_Y, 30, TOOLBAR_H});
    }

private:
    ModelEksploratora& model_;
    OknoEksploratora& okno_;
};

enum class StanANSI : uint8_t {
    BRAK = 0,
    ESC,
    CSI
};

bool podwojny_klik(uint64_t poprzedni, uint64_t obecny) {
    if (poprzedni == 0 || obecny < poprzedni) return false;
    const uint64_t roznica = obecny - poprzedni;
    const bool nanosekundy = obecny >= UINT64_C(1000000000);
    return nanosekundy ? roznica <= UINT64_C(500000000) : roznica <= 50U;
}

void odswiez_zaznaczenie(WidokEksploratora& widok,
                         ModelEksploratora& model,
                         int stary, bool scroll_zmieniony) {
    if (scroll_zmieniony) {
        widok.rysuj_liste();
        gui_odswiez();
        return;
    }
    if (stary == model.zaznaczony()) return;
    if (stary >= 0 && stary != model.zaznaczony()) {
        widok.rysuj_wiersz(stary);
        /* GUI przechowuje jeden oczekujacy dirty rect na proces. Osobny
           present zapobiega polaczeniu odleglych wierszy w duzy prostokat. */
        gui_odswiez();
    }
    widok.rysuj_wiersz(model.zaznaczony());
    gui_odswiez();
}

void pokaz_wynik_aktywacji(WidokEksploratora& widok,
                           WynikAktywacji wynik) {
    if (wynik == WynikAktywacji::ZMIANA_KATALOGU)
        widok.rysuj_zawartosc();
    else
        widok.rysuj_status();
    gui_odswiez();
}

} // namespace

extern "C" __attribute__((noreturn)) void _start() {
    ModelEksploratora model;
    OknoEksploratora okno;
    gui_pobierz_rozdzielczosc(&okno.screen_w, &okno.screen_h);
    okno.ogranicz();
    if (!okno.utworz_warstwe()) gui_zakoncz_aplikacje();
    gui_ustaw_przejecie_myszy(true);
    (void)model.inicjalizuj();
    WidokEksploratora widok(model, okno);
    widok.rysuj_pelne(true);

    bool koniec = false;
    bool dragging = false;
    int drag_off_x = 0;
    int drag_off_y = 0;
    int ostatni_klik_indeks = -1;
    uint64_t ostatni_klik_czas = 0;
    StanANSI ansi = StanANSI::BRAK;

    while (!koniec) {
        bws_zdarzenie zdarzenie{};
        if (!gui_czekaj_na_zdarzenie(&zdarzenie)) continue;

        if (zdarzenie.typ == BWS_ZDARZENIE_ZAMKNIJ) {
            koniec = true;
            continue;
        }
        if (zdarzenie.typ == BWS_ZDARZENIE_FOCUS && okno.zminimalizowane) {
            okno.zminimalizowane = false;
            widok.rysuj_pelne(false);
        }
        if (okno.zminimalizowane) continue;

        const int mx = zdarzenie.x;
        const int my = zdarzenie.y;
        const bool lewy = (zdarzenie.przyciski & 0x01U) != 0;

        if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_RUCH && dragging && lewy) {
            okno.x = mx - drag_off_x;
            okno.y = my - drag_off_y;
            okno.ogranicz();
            bws_przesun_warstwe(okno.x, okno.y);
            continue;
        }
        if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_UP && dragging) {
            dragging = false;
            gui_ustaw_capture_myszy(false);
            continue;
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN) {
            const gui_akcja_belki belka = gui_hit_test_belki(
                mx, my, okno.x, okno.y, okno.w);
            if (belka == GUI_BELKA_ZAMKNIJ) {
                koniec = true;
                continue;
            }
            if (belka == GUI_BELKA_MINIMALIZUJ) {
                okno.zminimalizowane = gui_minimalizuj_okno();
                dragging = false;
                gui_ustaw_capture_myszy(false);
                continue;
            }
            if (belka == GUI_BELKA_MAKSYMALIZUJ) {
                if (!okno.przelacz_maksymalizacje())
                    model.ustaw_status("Blad: nie mozna zmienic rozmiaru okna.");
                model.ogranicz_scroll(okno.widoczne_wiersze());
                widok.rysuj_pelne(true);
                continue;
            }
            if (belka == GUI_BELKA_DRAG && !okno.maksymalizowane) {
                dragging = true;
                drag_off_x = mx - okno.x;
                drag_off_y = my - okno.y;
                gui_ustaw_capture_myszy(true);
                continue;
            }

            if (widok.klik_rodzic(mx, my)) {
                ostatni_klik_indeks = -1;
                ostatni_klik_czas = 0;
                if (model.przejdz_rodzic()) widok.rysuj_zawartosc();
                else widok.rysuj_status();
                gui_odswiez();
                continue;
            }
            if (widok.klik_scroll_gora(mx, my)) {
                if (model.przewin(-1, okno.widoczne_wiersze())) {
                    widok.rysuj_liste();
                    gui_odswiez();
                }
                continue;
            }
            if (widok.klik_scroll_dol(mx, my)) {
                if (model.przewin(1, okno.widoczne_wiersze())) {
                    widok.rysuj_liste();
                    gui_odswiez();
                }
                continue;
            }

            const int indeks = widok.indeks_wiersza(mx, my);
            if (indeks >= 0) {
                const int stary = model.zaznaczony();
                bool scroll_zmieniony = false;
                (void)model.ustaw_zaznaczenie(
                    indeks, okno.widoczne_wiersze(), &scroll_zmieniony);
                odswiez_zaznaczenie(widok, model, stary, scroll_zmieniony);
                const bool podwojny = indeks == ostatni_klik_indeks &&
                    podwojny_klik(ostatni_klik_czas, zdarzenie.timestamp);
                ostatni_klik_indeks = indeks;
                ostatni_klik_czas = zdarzenie.timestamp;
                if (podwojny) {
                    pokaz_wynik_aktywacji(widok, model.aktywuj_zaznaczony());
                    ostatni_klik_indeks = -1;
                    ostatni_klik_czas = 0;
                }
            } else {
                ostatni_klik_indeks = -1;
                ostatni_klik_czas = 0;
            }
            continue;
        }

        if (zdarzenie.typ != BWS_ZDARZENIE_KLAWISZ) continue;
        const char c = static_cast<char>(zdarzenie.kod);
        if (ansi == StanANSI::ESC) {
            ansi = c == '[' ? StanANSI::CSI : StanANSI::BRAK;
            continue;
        }
        if (ansi == StanANSI::CSI) {
            ansi = StanANSI::BRAK;
            int delta = 0;
            if (c == 'A') delta = -1;
            else if (c == 'B') delta = 1;
            if (delta != 0) {
                int stary = -1;
                bool scroll_zmieniony = false;
                if (model.przesun_zaznaczenie(
                        delta, okno.widoczne_wiersze(), &stary,
                        &scroll_zmieniony))
                    odswiez_zaznaczenie(widok, model, stary, scroll_zmieniony);
            }
            continue;
        }
        if (c == '\x1B') {
            ansi = StanANSI::ESC;
            continue;
        }
        if (c == '\n' || c == '\r') {
            pokaz_wynik_aktywacji(widok, model.aktywuj_zaznaczony());
            continue;
        }
        if (c == '\b' || static_cast<uint8_t>(c) == 0x7FU) {
            if (model.przejdz_rodzic()) widok.rysuj_zawartosc();
            else widok.rysuj_status();
            gui_odswiez();
        }
    }

    gui_ustaw_capture_myszy(false);
    gui_zakoncz_aplikacje();
}
