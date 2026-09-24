#pragma once

#include "okno_dialogowe.h"
#include "pasek_narzedzi.h"
#include "pasek_sciezki.h"
#include "rozdzielacz.h"
#include "drzewo.h"
#include "tabela.h"
#include "pole_tekstowe.h"
#include "lista_rozwijana.h"
#include "przycisk.h"
#include "etykieta.h"
#include "panel.h"
#include "panel_dokowany.h"
#include "../bursztyn_gui.h"

namespace BursztynUI {

enum class TrybDialoguPliku : uint8_t {
    Otworz = 0,
    Zapisz = 1,
    WybierzKatalog = 2
};

enum class WynikDialoguPliku : uint8_t {
    Brak = 0,
    Wybrano = 1,
    Anulowano = 2
};

struct FiltrPliku {
    char nazwa[96]{};
    char rozszerzenia[96]{}; // np. "*.*", "*.txt;*.log", ".bur;.cebula"
    bool czy_wszystkie{false};
};

class DialogPliku;

// Modalny dialog wprowadzania nazwy dla nowo tworzonego katalogu
class DialogNowegoKatalogu : public OknoDialogowe {
public:
    explicit DialogNowegoKatalogu(DialogPliku* rodzic = nullptr)
        : OknoDialogowe("Nowy katalog", "", PrzyciskiDialogu::Wlasne, TypDialogu::Zwykly),
          rodzic_dialogu_(rodzic) {}

    ~DialogNowegoKatalogu() override = default;

    PoleTekstowe* pole_nazwy() const { return pole_nazwy_; }

protected:
    void oblicz_geometrie_dialogu(int& szer, int& wys) override {
        SkalaUI s = skala_dpi_;
        szer = 400 * s;
        wys = 190 * s;
    }

    void zbuduj_interfejs(int w, int h) override {
        wyczysc_kontrolki();
        wyczysc_sloty_przyciskow();
        SkalaUI s = skala_dpi_;

        uklad_ = new PanelDokowany();
        uklad_->ustaw_wypelnienie(Marginesy(0, 0, 0, 0));
        uklad_->ustaw_odstep(0);
        wyzeruj_tlo(*uklad_);
        dodaj_kontrolke(uklad_);

        // Pasek przyciskow na dole
        panel_przyciskow_ = new Wiersz();
        panel_przyciskow_->ustaw_preferowany_rozmiar(Rozmiar(-1, 46));
        panel_przyciskow_->ustaw_wypelnienie(Marginesy(16, 6, 16, 10));
        panel_przyciskow_->ustaw_odstep(10);
        panel_przyciskow_->ustaw_wyrownanie_poziome(WyrownaniePoziome::Koniec);
        panel_przyciskow_->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Srodek);
        wyzeruj_tlo(*panel_przyciskow_);
        uklad_->dodaj_dokowany(panel_przyciskow_, Dokowanie::Dol);

        Kontrolka* wyp = new Kontrolka();
        wyp->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        wyp->ustaw_mozliwosc_fokusu(false);
        panel_przyciskow_->dodaj_kontrolke(wyp);

        btn_utworz_ = new Przycisk("Utwórz");
        btn_utworz_->ustaw_preferowany_rozmiar(Rozmiar(95, 30));
        panel_przyciskow_->dodaj_kontrolke(btn_utworz_);
        zarejestruj_przycisk_akcji(btn_utworz_, WynikDialogu::OK, true, false);

        btn_anuluj_ = new Przycisk("Anuluj");
        btn_anuluj_->ustaw_preferowany_rozmiar(Rozmiar(95, 30));
        panel_przyciskow_->dodaj_kontrolke(btn_anuluj_);
        zarejestruj_przycisk_akcji(btn_anuluj_, WynikDialogu::Anuluj, false, true);

        btn_utworz_->przy_kliknieciu([](Przycisk*, void* d) {
            static_cast<DialogNowegoKatalogu*>(d)->zatwierdz_katalog();
        }, this);
        btn_anuluj_->przy_kliknieciu([](Przycisk*, void* d) {
            static_cast<DialogNowegoKatalogu*>(d)->zamknij_z_wynikiem(WynikDialogu::Anuluj);
        }, this);

        // Tresc dialogu
        Kolumna* tresc = new Kolumna();
        tresc->ustaw_wypelnienie(Marginesy(18, 14, 18, 10));
        tresc->ustaw_odstep(8);
        tresc->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        wyzeruj_tlo(*tresc);
        uklad_->dodaj_dokowany(tresc, Dokowanie::Wypelnij);

        Etykieta* ety = new Etykieta("Wprowadź nazwę nowego katalogu:");
        ety->ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST));
        tresc->dodaj_kontrolke(ety);

        pole_nazwy_ = new PoleTekstowe("Nowy folder");
        pole_nazwy_->ustaw_preferowany_rozmiar(Rozmiar(-1, 30));
        pole_nazwy_->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        pole_nazwy_->ustaw_tekst("Nowy folder");
        pole_nazwy_->przy_zatwierdzeniu([](PoleTekstowe*, const char*, void* d) {
            static_cast<DialogNowegoKatalogu*>(d)->zatwierdz_katalog();
        }, this);
        tresc->dodaj_kontrolke(pole_nazwy_);

        uklad_->rozmiesc(Prostokat(0, 32 * s, w, h > 32 * s ? h - 32 * s : 0));
        aktualizuj_obszary_ukladu();
    }

private:
    void zatwierdz_katalog();

    DialogPliku* rodzic_dialogu_{nullptr};
    PoleTekstowe* pole_nazwy_{nullptr};
    Przycisk* btn_utworz_{nullptr};
    Przycisk* btn_anuluj_{nullptr};
};

class DialogPliku : public OknoDialogowe {
public:
    static constexpr size_t MAKS_SCIEZKI = 512;
    static constexpr size_t MAKS_NAZWY = 64;
    static constexpr size_t MAKS_FILTROW = 16;
    static constexpr size_t MAKS_WPISOW = 512;
    static constexpr size_t ROZMIAR_LISTINGU = 64U * 1024U;
    static constexpr size_t MAKS_HISTORII = 16;

    using FunkcjaWynikuPliku = void (*)(DialogPliku* dialog, WynikDialoguPliku wynik,
                                        const char* sciezka, void* dane);

    struct WpisKatalogu {
        char nazwa[MAKS_NAZWY]{};
        bool katalog{false};
        uint64_t rozmiar{0};
        uint64_t czas_rtc{0};
    };

    explicit DialogPliku(TrybDialoguPliku tryb = TrybDialoguPliku::Otworz,
                         const char* tytul = nullptr)
        : OknoDialogowe(tytul ? tytul : domyslny_tytul(tryb), "", PrzyciskiDialogu::Wlasne, TypDialogu::Zwykly),
          tryb_(tryb) {
        normalizuj_sciezke("/uzytkownicy", biezacy_katalog_, sizeof(biezacy_katalog_));
        dodaj_filtr("Wszystkie pliki (*.*)", "*.*");
        if (tryb == TrybDialoguPliku::Otworz || tryb == TrybDialoguPliku::Zapisz) {
            dodaj_filtr("Pliki tekstowe (*.txt)", "*.txt");
            dodaj_filtr("Dokumenty (*.txt;*.md;*.log)", "*.txt;*.md;*.log");
            dodaj_filtr("Aplikacje Bursztyn (*.bur)", "*.bur");
            dodaj_filtr("Skrypty Cebula (*.cebula)", "*.cebula");
            dodaj_filtr("Obrazy BMP (*.bmp)", "*.bmp");
        }
    }

    DialogPliku(TrybDialoguPliku tryb, const char* tytul, const char* katalog_poczatkowy, const char* filtry = nullptr)
        : DialogPliku(tryb, tytul) {
        if (katalog_poczatkowy) {
            ustaw_katalog_poczatkowy(katalog_poczatkowy);
        }
        if (filtry) {
            wyczysc_filtry();
            dodaj_filtry_z_napisu(filtry);
        }
    }

    ~DialogPliku() override = default;

    static const char* domyslny_tytul(TrybDialoguPliku t) {
        switch (t) {
            case TrybDialoguPliku::Otworz: return "Otwórz plik";
            case TrybDialoguPliku::Zapisz: return "Zapisz plik";
            case TrybDialoguPliku::WybierzKatalog: return "Wybierz katalog";
            default: return "Pliki";
        }
    }

    void ustaw_tryb(TrybDialoguPliku tryb) {
        if (tryb_ != tryb) {
            tryb_ = tryb;
            ustaw_tekst(domyslny_tytul(tryb));
            if (btn_akcja_) {
                btn_akcja_->ustaw_tekst(tekst_przycisku_akcji());
            }
            if (filtr_combo_) {
                filtr_combo_->ustaw_widoczna(tryb_ != TrybDialoguPliku::WybierzKatalog);
            }
            if (pole_nazwy_ && tryb_ == TrybDialoguPliku::WybierzKatalog) {
                pole_nazwy_->ustaw_tekst("");
            }
            uniewaznij();
        }
    }
    TrybDialoguPliku tryb() const { return tryb_; }

    const char* tekst_przycisku_akcji() const {
        switch (tryb_) {
            case TrybDialoguPliku::Otworz: return "Otwórz";
            case TrybDialoguPliku::Zapisz: return "Zapisz";
            case TrybDialoguPliku::WybierzKatalog: return "Wybierz";
            default: return "OK";
        }
    }

    void ustaw_tytul_dialogu(const char* tytul) {
        if (tytul && tytul[0]) {
            ustaw_tekst(tytul);
        }
    }

    void ustaw_katalog_poczatkowy(const char* katalog) {
        if (!katalog || !katalog[0]) return;
        normalizuj_sciezke(katalog, biezacy_katalog_, sizeof(biezacy_katalog_));
        if (pasek_sciezki_) pasek_sciezki_->ustaw_sciezke(biezacy_katalog_);
        if (czy_otwarta()) odswiez();
    }

    void ustaw_nazwe_poczatkowa(const char* nazwa) {
        if (!nazwa) nazwa = "";
        kopiuj_str(wybrana_nazwa_, sizeof(wybrana_nazwa_), nazwa);
        if (pole_nazwy_) pole_nazwy_->ustaw_tekst(nazwa);
    }

    bool dodaj_filtr(const char* nazwa, const char* rozszerzenia) {
        if (liczba_filtrow_ >= MAKS_FILTROW || !nazwa || !rozszerzenia) return false;
        FiltrPliku& f = filtry_[liczba_filtrow_++];
        kopiuj_str(f.nazwa, sizeof(f.nazwa), nazwa);
        kopiuj_str(f.rozszerzenia, sizeof(f.rozszerzenia), rozszerzenia);
        f.czy_wszystkie = czy_filtr_wszystkie(rozszerzenia);
        if (filtr_combo_) {
            filtr_combo_->dodaj_element(nazwa);
            if (liczba_filtrow_ == 1) filtr_combo_->ustaw_wybrany_indeks(0);
        }
        return true;
    }

    void wyczysc_filtry() {
        liczba_filtrow_ = 0;
        aktywny_filtr_ = 0;
        if (filtr_combo_) filtr_combo_->wyczysc();
    }

    void dodaj_filtry_z_napisu(const char* ciag) {
        if (!ciag || !ciag[0]) return;
        const char* p = ciag;
        while (*p) {
            while (*p == ' ' || *p == ';') ++p;
            if (!*p) break;

            const char* end = p;
            while (*end) {
                if (end[0] == ';' && end[1] == ';') break;
                ++end;
            }

            char element[128]{};
            size_t elen = end - p;
            if (elen >= sizeof(element)) elen = sizeof(element) - 1;
            for (size_t i = 0; i < elen; ++i) element[i] = p[i];
            element[elen] = '\0';

            char nazwa[96]{};
            char rozszerzenia[96]{};

            char* sep_pipe = nullptr;
            char* nawias_otw = nullptr;
            char* nawias_zamk = nullptr;
            for (size_t i = 0; i < elen; ++i) {
                if (element[i] == '|') sep_pipe = &element[i];
                if (element[i] == '(') nawias_otw = &element[i];
                if (element[i] == ')') nawias_zamk = &element[i];
            }

            if (sep_pipe) {
                *sep_pipe = '\0';
                kopiuj_str(nazwa, sizeof(nazwa), element);
                kopiuj_str(rozszerzenia, sizeof(rozszerzenia), sep_pipe + 1);
            } else if (nawias_otw && nawias_zamk && nawias_zamk > nawias_otw) {
                kopiuj_str(nazwa, sizeof(nazwa), element);
                size_t ext_len = (nawias_zamk - 1) - nawias_otw;
                if (ext_len >= sizeof(rozszerzenia)) ext_len = sizeof(rozszerzenia) - 1;
                for (size_t i = 0; i < ext_len; ++i) rozszerzenia[i] = nawias_otw[1 + i];
                rozszerzenia[ext_len] = '\0';
            } else {
                kopiuj_str(nazwa, sizeof(nazwa), element);
                kopiuj_str(rozszerzenia, sizeof(rozszerzenia), "*.*");
            }

            dodaj_filtr(nazwa, rozszerzenia);

            if (*end == ';' && *(end + 1) == ';') {
                p = end + 2;
            } else {
                p = end;
            }
        }
    }

    bool ustaw_aktywny_filtr(size_t indeks) {
        if (indeks >= liczba_filtrow_) return false;
        aktywny_filtr_ = indeks;
        if (filtr_combo_) filtr_combo_->ustaw_wybrany_indeks(static_cast<int>(indeks));
        odswiez_widok_tabeli();
        return true;
    }
    size_t aktywny_filtr() const { return aktywny_filtr_; }
    size_t liczba_filtrow() const { return liczba_filtrow_; }
    const FiltrPliku* filtr(size_t indeks) const {
        return (indeks < liczba_filtrow_) ? &filtry_[indeks] : nullptr;
    }

    const char* wybrana_sciezka() const { return wybrana_sciezka_; }
    const char* wybrana_nazwa() const { return wybrana_nazwa_; }
    const char* biezacy_katalog() const { return biezacy_katalog_; }
    WynikDialoguPliku wynik_pliku() const { return wynik_pliku_; }
    bool czy_listing_niepelny() const { return listing_niepelny_; }

    void przy_wyniku_pliku(FunkcjaWynikuPliku cb, void* dane = nullptr) {
        cb_wynik_pliku_ = cb;
        dane_wyniku_pliku_ = dane;
    }

    bool przejdz_do_katalogu(const char* sciezka) {
        if (!sciezka || !sciezka[0]) return false;
        char norm[MAKS_SCIEZKI]{};
        normalizuj_sciezke(sciezka, norm, sizeof(norm));

        // Sprawdz czy katalog istnieje i jest czytelny
        char* test_buf = static_cast<char*>(gui_malloc(1024));
        if (!test_buf) return false;
        bool ok = wylistuj_katalog_uzytkownika(norm, test_buf, 1024);
        gui_free(test_buf);

        if (!ok) {
            if (zarzadca()) {
                Komunikat::blad(*zarzadca(), "Błąd katalogu",
                                "Nie można otworzyć katalogu.\nBrak uprawnień lub ścieżka nie istnieje.",
                                nullptr, nullptr, this);
            }
            return false;
        }

        if (!rowne_str(biezacy_katalog_, norm)) {
            dodaj_do_historii(biezacy_katalog_);
        }

        kopiuj_str(biezacy_katalog_, sizeof(biezacy_katalog_), norm);
        if (pasek_sciezki_) pasek_sciezki_->ustaw_sciezke(biezacy_katalog_);

        return odswiez_katalog();
    }

    bool przejdz_poziom_wyzej() {
        if (rowne_str(biezacy_katalog_, "/")) {
            return true; // Juz na najwyzszym poziomie korzenia, bezpieczny brak akcji
        }
        char nadrzedny[MAKS_SCIEZKI]{};
        pobierz_katalog_nadrzedny(biezacy_katalog_, nadrzedny, sizeof(nadrzedny));
        return przejdz_do_katalogu(nadrzedny);
    }
    bool przejdz_w_gore() { return przejdz_poziom_wyzej(); }

    bool przejdz_wstecz() {
        if (pozycja_historii_ >= 0 && pozycja_historii_ < static_cast<int>(liczba_historii_)) {
            char target[MAKS_SCIEZKI]{};
            kopiuj_str(target, sizeof(target), historia_[pozycja_historii_]);
            --pozycja_historii_;
            kopiuj_str(biezacy_katalog_, sizeof(biezacy_katalog_), target);
            if (pasek_sciezki_) pasek_sciezki_->ustaw_sciezke(biezacy_katalog_);
            return odswiez_katalog();
        }
        return false;
    }

    void odswiez() {
        (void)odswiez_katalog();
    }

    void otworz_dialog_nowego_katalogu() {
        if (!zarzadca()) return;
        DialogNowegoKatalogu* dlg = new DialogNowegoKatalogu(this);
        dlg->pokaz(*zarzadca(), this);
    }

    bool utworz_nowy_katalog(const char* nazwa = nullptr) {
        if (!nazwa || !nazwa[0]) {
            otworz_dialog_nowego_katalogu();
            return true;
        }

        // Walidacja nazwy folderu
        if (!czy_poprawna_nazwa_elementu(nazwa)) {
            if (zarzadca()) {
                Komunikat::ostrzezenie(*zarzadca(), "Nieprawidłowa nazwa",
                                       "Nazwa katalogu zawiera niedozwolone znaki lub jest nieprawidłowa.",
                                       nullptr, nullptr, this);
            }
            return false;
        }

        char pelna[MAKS_SCIEZKI]{};
        if (!polacz_sciezki(biezacy_katalog_, nazwa, pelna, sizeof(pelna))) {
            if (zarzadca()) {
                Komunikat::blad(*zarzadca(), "Błąd ścieżki", "Ścieżka katalogu jest zbyt długa.",
                                nullptr, nullptr, this);
            }
            return false;
        }

        // Sprawdzenie czy katalog juz istnieje
        BwsMetadanePliku meta{};
        if (pobierz_metadane_pliku(pelna, &meta)) {
            if (zarzadca()) {
                Komunikat::ostrzezenie(*zarzadca(), "Katalog już istnieje",
                                       "Katalog o podanej nazwie już istnieje w bieżącej lokalizacji.",
                                       nullptr, nullptr, this);
            }
            return false;
        }

        if (!utworz_katalog_uzytkownika(pelna)) {
            if (zarzadca()) {
                Komunikat::blad(*zarzadca(), "Błąd tworzenia katalogu",
                                "Nie udało się utworzyć katalogu.\nBrak uprawnień do zapisu w tym katalogu.",
                                nullptr, nullptr, this);
            }
            return false;
        }

        odswiez();
        if (pole_nazwy_) pole_nazwy_->ustaw_tekst(nazwa);
        return true;
    }

    static bool czy_poprawna_nazwa_elementu(const char* s) {
        if (!s || !s[0]) return false;
        if (!czy_poprawny_utf8(s)) return false;
        size_t l = dlugosc_str(s);
        if (l >= MAKS_NAZWY) return false;
        for (size_t i = 0; i < l; ++i) {
            char c = s[i];
            if (c == '/' || c == '\\' || c == ':' || c == '*' ||
                c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                return false;
            }
        }
        return true;
    }

    static bool czy_poprawna_sciezka_dialogu(const char* s) {
        if (!s || !s[0] || s[0] != '/') return false;
        if (!czy_poprawny_utf8(s)) return false;
        if (dlugosc_str(s) >= MAKS_SCIEZKI) return false;
        for (size_t i = 0; s[i]; ++i) {
            char c = s[i];
            if (c == '\\' || c == '"' || c == '<' || c == '>' || c == '|') return false;
        }
        return true;
    }

    static void kopiuj_str(char* cel, size_t cap, const char* zr) {
        if (!cel || cap == 0) return;
        if (!zr) { cel[0] = '\0'; return; }
        size_t i = 0;
        while (zr[i] && i + 1 < cap) {
            cel[i] = zr[i];
            ++i;
        }
        cel[i] = '\0';
    }

    static bool rowne_str(const char* a, const char* b) {
        if (!a || !b) return a == b;
        size_t i = 0;
        while (a[i] && b[i]) {
            if (a[i] != b[i]) return false;
            ++i;
        }
        return a[i] == b[i];
    }

    static size_t dlugosc_str(const char* s) {
        if (!s) return 0;
        size_t i = 0;
        while (s[i]) ++i;
        return i;
    }

    static bool czy_poprawny_utf8(const char* s) {
        if (!s) return false;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
        while (*p) {
            if (*p < 0x80) {
                if (*p < 32 && *p != '\t' && *p != '\n' && *p != '\r') return false;
                ++p;
            } else if ((*p & 0xE0) == 0xC0) {
                if ((p[1] & 0xC0) != 0x80) return false;
                p += 2;
            } else if ((*p & 0xF0) == 0xE0) {
                if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return false;
                p += 3;
            } else if ((*p & 0xF8) == 0xF0) {
                if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) return false;
                p += 4;
            } else {
                return false;
            }
        }
        return true;
    }

    static bool normalizuj_sciezke(const char* zr, char* cel, size_t cap) {
        if (!cel || cap < 2) return false;
        if (!zr || !zr[0]) {
            cel[0] = '/'; cel[1] = '\0';
            return true;
        }

        char tmp[MAKS_SCIEZKI]{};
        size_t ti = 0;
        for (size_t i = 0; zr[i] && ti + 1 < sizeof(tmp); ++i) {
            char c = zr[i];
            if (c == '\\') c = '/';
            if (c == '/' && ti > 0 && tmp[ti - 1] == '/') continue;
            tmp[ti++] = c;
        }
        tmp[ti] = '\0';

        char segs[32][64]{};
        size_t count = 0;

        size_t pos = 0;
        while (pos < ti) {
            while (pos < ti && tmp[pos] == '/') ++pos;
            if (pos >= ti) break;
            size_t end = pos;
            while (end < ti && tmp[end] != '/') ++end;
            size_t slen = end - pos;
            if (slen == 1 && tmp[pos] == '.') {
                // pomin
            } else if (slen == 2 && tmp[pos] == '.' && tmp[pos + 1] == '.') {
                if (count > 0) --count;
            } else {
                if (count < 32 && slen < sizeof(segs[0])) {
                    for (size_t k = 0; k < slen; ++k) segs[count][k] = tmp[pos + k];
                    segs[count][slen] = '\0';
                    ++count;
                }
            }
            pos = end;
        }

        if (count == 0) {
            cel[0] = '/'; cel[1] = '\0';
            return true;
        }

        size_t out_len = 0;
        for (size_t i = 0; i < count; ++i) {
            if (out_len + 1 < cap) cel[out_len++] = '/';
            size_t slen = dlugosc_str(segs[i]);
            for (size_t k = 0; k < slen && out_len + 1 < cap; ++k) {
                cel[out_len++] = segs[i][k];
            }
        }
        cel[out_len] = '\0';
        return true;
    }

    static bool polacz_sciezki(const char* dir, const char* name, char* cel, size_t cap) {
        if (!cel || cap < 2) return false;
        if (!dir || !dir[0]) dir = "/";
        if (!name || !name[0]) {
            return normalizuj_sciezke(dir, cel, cap);
        }
        char zlaczone[MAKS_SCIEZKI * 2]{};
        size_t dlen = dlugosc_str(dir);
        size_t nlen = dlugosc_str(name);
        if (dlen + 1 + nlen + 1 >= sizeof(zlaczone)) return false;

        size_t zi = 0;
        for (size_t i = 0; i < dlen; ++i) zlaczone[zi++] = dir[i];
        if (zi == 0 || zlaczone[zi - 1] != '/') zlaczone[zi++] = '/';
        for (size_t i = 0; i < nlen; ++i) zlaczone[zi++] = name[i];
        zlaczone[zi] = '\0';

        return normalizuj_sciezke(zlaczone, cel, cap);
    }

    static bool pobierz_katalog_nadrzedny(const char* sciezka, char* cel, size_t cap) {
        if (!cel || cap < 2) return false;
        if (!sciezka || !sciezka[0] || (sciezka[0] == '/' && sciezka[1] == '\0')) {
            cel[0] = '/'; cel[1] = '\0';
            return true;
        }
        char norm[MAKS_SCIEZKI]{};
        normalizuj_sciezke(sciezka, norm, sizeof(norm));
        size_t len = dlugosc_str(norm);
        if (len <= 1) {
            cel[0] = '/'; cel[1] = '\0';
            return true;
        }

        int last_slash = -1;
        for (size_t i = 0; i < len; ++i) {
            if (norm[i] == '/') last_slash = static_cast<int>(i);
        }

        if (last_slash <= 0) {
            cel[0] = '/'; cel[1] = '\0';
            return true;
        }

        norm[last_slash] = '\0';
        kopiuj_str(cel, cap, norm);
        return true;
    }

    static bool sprawdz_filtr(const char* nazwa, const char* rozszerzenia) {
        if (!nazwa || !rozszerzenia) return true;
        if (czy_filtr_wszystkie(rozszerzenia)) return true;

        size_t nlen = dlugosc_str(nazwa);
        const char* p = rozszerzenia;

        while (*p) {
            while (*p == ' ' || *p == ';' || *p == ',') ++p;
            if (!*p) break;
            const char* start = p;
            while (*p && *p != ';' && *p != ',' && *p != ' ') ++p;
            size_t ext_len = p - start;

            if (ext_len >= 2 && start[0] == '*' && start[1] == '.') {
                start += 1;
                ext_len -= 1;
            }

            if (ext_len > 0 && nlen >= ext_len) {
                bool match = true;
                for (size_t i = 0; i < ext_len; ++i) {
                    char cn = nazwa[nlen - ext_len + i];
                    char ce = start[i];
                    if (cn >= 'A' && cn <= 'Z') cn += ('a' - 'A');
                    if (ce >= 'A' && ce <= 'Z') ce += ('a' - 'A');
                    if (cn != ce) { match = false; break; }
                }
                if (match) return true;
            }
        }
        return false;
    }

    static void formatuj_rozmiar(uint64_t bajty, char* out, size_t cap) {
        if (!out || cap == 0) return;
        if (bajty < 1024ULL) {
            char num[24]; formatuj_u64(bajty, num, sizeof(num));
            kopiuj_str(out, cap, num);
            size_t l = dlugosc_str(out);
            if (l + 3 < cap) { out[l++] = ' '; out[l++] = 'B'; out[l] = '\0'; }
        } else if (bajty < 1024ULL * 1024ULL) {
            uint64_t kb = bajty / 1024ULL;
            char num[24]; formatuj_u64(kb, num, sizeof(num));
            kopiuj_str(out, cap, num);
            size_t l = dlugosc_str(out);
            if (l + 4 < cap) { out[l++] = ' '; out[l++] = 'K'; out[l] = 'B'; out[l] = '\0'; }
        } else {
            uint64_t mb = bajty / (1024ULL * 1024ULL);
            char num[24]; formatuj_u64(mb, num, sizeof(num));
            kopiuj_str(out, cap, num);
            size_t l = dlugosc_str(out);
            if (l + 4 < cap) { out[l++] = ' '; out[l++] = 'M'; out[l] = 'B'; out[l] = '\0'; }
        }
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        uint32_t kod = e.kod_klawisza;
        bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
        uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);

        // Escape: anuluj
        if (kod == 27U || (usb && baza == 41U) || (!usb && baza == 0x01U)) {
            anuluj();
            e.obsluzone = true;
            return;
        }

        // Enter
        if (kod == 13U || kod == 10U || (usb && baza == 40U) || (!usb && baza == 0x1CU)) {
            zatwierdz();
            e.obsluzone = true;
            return;
        }

        // Tab / Shift+Tab
        const bool tab = kod == 9U || (usb && baza == 43U) || (!usb && baza == 0x0FU);
        if (tab) {
            if (zarzadca()) synchronizuj_indeks_fokusu(zarzadca()->fokus_popupu());
            nawiguj_fokus(!e.czy_shift());
            e.obsluzone = true;
            return;
        }

        OknoDialogowe::obsluz_wcisniecie_klawisza(e);
    }

    void zatwierdz() {
        const char* name_txt = pole_nazwy_ ? pole_nazwy_->tekst() : "";
        while (*name_txt == ' ') ++name_txt;

        if (tryb_ == TrybDialoguPliku::WybierzKatalog) {
            char target[MAKS_SCIEZKI]{};
            if (name_txt[0]) {
                polacz_sciezki(biezacy_katalog_, name_txt, target, sizeof(target));
            } else {
                normalizuj_sciezke(biezacy_katalog_, target, sizeof(target));
            }
            BwsMetadanePliku meta{};
            if (!pobierz_metadane_pliku(target, &meta) || meta.typ != BWS_TYP_KATALOG) {
                char* buf = static_cast<char*>(gui_malloc(512));
                bool ok = buf && wylistuj_katalog_uzytkownika(target, buf, 512);
                if (buf) gui_free(buf);
                if (!ok) {
                    if (zarzadca()) {
                        Komunikat::blad(*zarzadca(), "Błąd wyboru",
                                        "Wybrana ścieżka nie jest katalogiem lub nie istnieje.",
                                        nullptr, nullptr, this);
                    }
                    return;
                }
            }
            if (!czy_poprawna_sciezka_dialogu(target)) {
                if (zarzadca()) {
                    Komunikat::blad(*zarzadca(), "Błąd wyboru",
                                    "Wybrana ścieżka jest nieprawidłowa albo zbyt długa.",
                                    nullptr, nullptr, this);
                }
                return;
            }
            kopiuj_str(wybrana_sciezka_, sizeof(wybrana_sciezka_), target);
            wynik_pliku_ = WynikDialoguPliku::Wybrano;
            if (cb_wynik_pliku_) {
                cb_wynik_pliku_(this, WynikDialoguPliku::Wybrano, wybrana_sciezka_, dane_wyniku_pliku_);
            }
            zamknij_z_wynikiem(WynikDialogu::OK);
            return;
        }

        if (!name_txt[0]) {
            if (zarzadca()) {
                Komunikat::blad(*zarzadca(), "Brak nazwy pliku",
                                "Wprowadź lub wybierz nazwę pliku z listy.",
                                nullptr, nullptr, this);
            }
            return;
        }

        char pelna_nazwa[MAKS_NAZWY]{};
        kopiuj_str(pelna_nazwa, sizeof(pelna_nazwa), name_txt);
        if (!czy_poprawna_nazwa_elementu(pelna_nazwa)) {
            if (zarzadca()) {
                Komunikat::blad(*zarzadca(), "Nieprawidłowa nazwa",
                                "Nazwa pliku zawiera niedozwolone znaki, jest zbyt długa albo nie jest poprawnym UTF-8.",
                                nullptr, nullptr, this);
            }
            return;
        }

        if (tryb_ == TrybDialoguPliku::Zapisz && !ma_rozszerzenie(pelna_nazwa)) {
            const char* ext = pobierz_domyslne_rozszerzenie_filtru();
            if (ext && ext[0]) {
                size_t l = dlugosc_str(pelna_nazwa);
                size_t el = dlugosc_str(ext);
                if (l + el + 1 < sizeof(pelna_nazwa)) {
                    for (size_t i = 0; i < el; ++i) pelna_nazwa[l + i] = ext[i];
                    pelna_nazwa[l + el] = '\0';
                }
            }
        }

        char pelna_sciezka[MAKS_SCIEZKI]{};
        if (!polacz_sciezki(biezacy_katalog_, pelna_nazwa, pelna_sciezka, sizeof(pelna_sciezka))) {
            if (zarzadca()) {
                Komunikat::blad(*zarzadca(), "Błąd ścieżki", "Ścieżka pliku jest zbyt długa.",
                                nullptr, nullptr, this);
            }
            return;
        }
        if (!czy_poprawna_sciezka_dialogu(pelna_sciezka)) {
            if (zarzadca()) {
                Komunikat::blad(*zarzadca(), "Błąd ścieżki",
                                "Ścieżka pliku jest nieprawidłowa albo zbyt długa.",
                                nullptr, nullptr, this);
            }
            return;
        }

        if (tryb_ == TrybDialoguPliku::Otworz) {
            BwsMetadanePliku meta{};
            bool exists = pobierz_metadane_pliku(pelna_sciezka, &meta);
            if (exists && meta.typ == BWS_TYP_KATALOG) {
                przejdz_do_katalogu(pelna_sciezka);
                return;
            }
            if (!exists) {
                uint32_t r = 0;
                if (!pobierz_rozmiar_pliku(pelna_sciezka, &r)) {
                    if (zarzadca()) {
                        Komunikat::blad(*zarzadca(), "Plik nie istnieje",
                                        "Wybrany plik nie istnieje lub brak uprawnień do odczytu.",
                                        nullptr, nullptr, this);
                    }
                    return;
                }
            }
            kopiuj_str(wybrana_sciezka_, sizeof(wybrana_sciezka_), pelna_sciezka);
            kopiuj_str(wybrana_nazwa_, sizeof(wybrana_nazwa_), pelna_nazwa);
            wynik_pliku_ = WynikDialoguPliku::Wybrano;
            if (cb_wynik_pliku_) {
                cb_wynik_pliku_(this, WynikDialoguPliku::Wybrano, wybrana_sciezka_, dane_wyniku_pliku_);
            }
            zamknij_z_wynikiem(WynikDialogu::OK);
            return;
        }

        if (tryb_ == TrybDialoguPliku::Zapisz) {
            BwsMetadanePliku meta{};
            bool exists = pobierz_metadane_pliku(pelna_sciezka, &meta);
            if (!exists) {
                uint32_t r = 0;
                exists = pobierz_rozmiar_pliku(pelna_sciezka, &r);
            }

            if (exists) {
                kopiuj_str(sciezka_do_potwierdzenia_, sizeof(sciezka_do_potwierdzenia_), pelna_sciezka);
                if (zarzadca()) {
                    Komunikat::pytanie(*zarzadca(), "Potwierdzenie zastąpienia",
                                       "Plik o podanej nazwie już istnieje.\nCzy chcesz zastąpić istniejący plik?",
                                       PrzyciskiDialogu::TakNie,
                                       [](OknoDialogowe*, WynikDialogu w, void* d) {
                                           DialogPliku* self = static_cast<DialogPliku*>(d);
                                           if (w == WynikDialogu::Tak) {
                                               self->zatwierdz_koncowo(self->sciezka_do_potwierdzenia_);
                                           }
                                       }, this, this);
                    return;
                }
            }

            zatwierdz_koncowo(pelna_sciezka);
        }
    }

    void zatwierdz_koncowo(const char* sciezka) {
        if (!czy_poprawna_sciezka_dialogu(sciezka)) {
            if (zarzadca()) {
                Komunikat::blad(*zarzadca(), "Błąd ścieżki",
                                "Ścieżka pliku jest nieprawidłowa albo zbyt długa.",
                                nullptr, nullptr, this);
            }
            return;
        }
        BwsMetadanePliku meta{};
        if (tryb_ == TrybDialoguPliku::Otworz) {
            if (!pobierz_metadane_pliku(sciezka, &meta) || meta.typ != BWS_TYP_PLIK) {
                if (zarzadca()) {
                    Komunikat::blad(*zarzadca(), "Plik niedostępny",
                                    "Wybrany plik zmienił się, nie istnieje albo nie można go odczytać.",
                                    nullptr, nullptr, this);
                }
                return;
            }
        } else if (tryb_ == TrybDialoguPliku::WybierzKatalog) {
            if (!pobierz_metadane_pliku(sciezka, &meta) || meta.typ != BWS_TYP_KATALOG) {
                if (zarzadca()) {
                    Komunikat::blad(*zarzadca(), "Katalog niedostępny",
                                    "Wybrany katalog zmienił się, nie istnieje albo nie można go odczytać.",
                                    nullptr, nullptr, this);
                }
                return;
            }
        } else if (tryb_ == TrybDialoguPliku::Zapisz) {
            if (pobierz_metadane_pliku(sciezka, &meta)) {
                if (meta.typ == BWS_TYP_KATALOG) {
                    if (zarzadca()) {
                        Komunikat::blad(*zarzadca(), "Błąd zapisu",
                                        "Wybrana ścieżka wskazuje katalog, a nie plik.",
                                        nullptr, nullptr, this);
                    }
                    return;
                }
            } else {
                char rodzic[MAKS_SCIEZKI]{};
                pobierz_katalog_nadrzedny(sciezka, rodzic, sizeof(rodzic));
                BwsMetadanePliku meta_rodzic{};
                if (!pobierz_metadane_pliku(rodzic, &meta_rodzic) ||
                    meta_rodzic.typ != BWS_TYP_KATALOG) {
                    if (zarzadca()) {
                        Komunikat::blad(*zarzadca(), "Błąd zapisu",
                                        "Katalog docelowy nie istnieje albo nie jest dostępny.",
                                        nullptr, nullptr, this);
                    }
                    return;
                }
            }
        }
        kopiuj_str(wybrana_sciezka_, sizeof(wybrana_sciezka_), sciezka);
        const char* p = sciezka;
        size_t l = dlugosc_str(sciezka);
        int last_slash = -1;
        for (size_t i = 0; i < l; ++i) if (sciezka[i] == '/') last_slash = static_cast<int>(i);
        if (last_slash >= 0 && last_slash + 1 < static_cast<int>(l)) {
            p = sciezka + last_slash + 1;
        }
        kopiuj_str(wybrana_nazwa_, sizeof(wybrana_nazwa_), p);

        wynik_pliku_ = WynikDialoguPliku::Wybrano;
        if (cb_wynik_pliku_) {
            cb_wynik_pliku_(this, WynikDialoguPliku::Wybrano, wybrana_sciezka_, dane_wyniku_pliku_);
        }
        zamknij_z_wynikiem(WynikDialogu::OK);
    }

    void anuluj() {
        wybrana_sciezka_[0] = '\0';
        wybrana_nazwa_[0] = '\0';
        wynik_pliku_ = WynikDialoguPliku::Anulowano;
        if (cb_wynik_pliku_) {
            cb_wynik_pliku_(this, WynikDialoguPliku::Anulowano, "", dane_wyniku_pliku_);
        }
        zamknij_z_wynikiem(WynikDialogu::Anuluj);
    }

    void zamknij_z_wynikiem(WynikDialogu w) override {
        if (w == WynikDialogu::Anuluj || w == WynikDialogu::Nie) {
            wybrana_sciezka_[0] = '\0';
            wybrana_nazwa_[0] = '\0';
            wynik_pliku_ = WynikDialoguPliku::Anulowano;
            if (cb_wynik_pliku_) {
                cb_wynik_pliku_(this, WynikDialoguPliku::Anulowano, "", dane_wyniku_pliku_);
            }
        }
        OknoDialogowe::zamknij_z_wynikiem(w);
    }

    void po_zamknieciu(PowodZamknieciaPopup powod) override {
        if (powod == PowodZamknieciaPopup::Anulowanie || powod == PowodZamknieciaPopup::Escape) {
            if (wynik_pliku_ == WynikDialoguPliku::Brak) {
                wybrana_sciezka_[0] = '\0';
                wybrana_nazwa_[0] = '\0';
                wynik_pliku_ = WynikDialoguPliku::Anulowano;
                if (cb_wynik_pliku_) {
                    cb_wynik_pliku_(this, WynikDialoguPliku::Anulowano, "", dane_wyniku_pliku_);
                }
            }
        }
        OknoDialogowe::po_zamknieciu(powod);
    }

    PasekNarzedzi* pasek_narzedzi() const { return pasek_narzedzi_; }
    PasekSciezki* pasek_sciezki() const { return pasek_sciezki_; }
    Drzewo* drzewo_miejsc() const { return drzewo_miejsc_; }
    Tabela* tabela_plikow() const { return tabela_plikow_; }
    PoleTekstowe* pole_nazwy() const { return pole_nazwy_; }
    ListaRozwijana* lista_filtrow() const { return filtr_combo_; }
    Przycisk* przycisk_akcji() const { return btn_akcja_; }
    Przycisk* przycisk_zatwierdz() const { return btn_akcja_; }
    Przycisk* przycisk_anuluj() const { return btn_anuluj_; }
    const char* sciezka_do_potwierdzenia() const { return sciezka_do_potwierdzenia_; }
    void potwierdz_nadpisanie() { zatwierdz_koncowo(sciezka_do_potwierdzenia_); }

    static DialogPliku* otworz(ZarzadcaPopupow& z, const char* katalog = "/uzytkownicy",
                               const char* filtry = nullptr,
                               FunkcjaWynikuPliku cb = nullptr, void* dane = nullptr,
                               Kontrolka* owner = nullptr, const char* tytul = "Otwórz plik") {
        DialogPliku* dlg = new DialogPliku(TrybDialoguPliku::Otworz, tytul, katalog, filtry);
        dlg->przy_wyniku_pliku(cb, dane);
        dlg->OknoDialogowe::pokaz(z, owner);
        return dlg;
    }

    static DialogPliku* otworz_plik(ZarzadcaPopupow& z, const char* tytul = "Otwórz plik",
                                    const char* katalog = "/uzytkownicy",
                                    FunkcjaWynikuPliku cb = nullptr, void* dane = nullptr,
                                    Kontrolka* owner = nullptr) {
        return otworz(z, katalog, nullptr, cb, dane, owner, tytul);
    }

    static DialogPliku* zapisz(ZarzadcaPopupow& z, const char* katalog = "/uzytkownicy",
                               const char* filtry = nullptr,
                               FunkcjaWynikuPliku cb = nullptr, void* dane = nullptr,
                               Kontrolka* owner = nullptr, const char* tytul = "Zapisz plik") {
        DialogPliku* dlg = new DialogPliku(TrybDialoguPliku::Zapisz, tytul, katalog, filtry);
        dlg->przy_wyniku_pliku(cb, dane);
        dlg->OknoDialogowe::pokaz(z, owner);
        return dlg;
    }

    static DialogPliku* zapisz_plik(ZarzadcaPopupow& z, const char* tytul = "Zapisz plik",
                                    const char* katalog = "/uzytkownicy",
                                    const char* domyslna_nazwa = "dokument.txt",
                                    FunkcjaWynikuPliku cb = nullptr, void* dane = nullptr,
                                    Kontrolka* owner = nullptr) {
        DialogPliku* dlg = new DialogPliku(TrybDialoguPliku::Zapisz, tytul, katalog, nullptr);
        dlg->ustaw_nazwe_poczatkowa(domyslna_nazwa);
        dlg->przy_wyniku_pliku(cb, dane);
        dlg->OknoDialogowe::pokaz(z, owner);
        return dlg;
    }

    static DialogPliku* wybierz_katalog(ZarzadcaPopupow& z, const char* katalog = "/uzytkownicy",
                                        FunkcjaWynikuPliku cb = nullptr, void* dane = nullptr,
                                        Kontrolka* owner = nullptr, const char* tytul = "Wybierz katalog") {
        DialogPliku* dlg = new DialogPliku(TrybDialoguPliku::WybierzKatalog, tytul, katalog, nullptr);
        dlg->przy_wyniku_pliku(cb, dane);
        dlg->OknoDialogowe::pokaz(z, owner);
        return dlg;
    }

protected:
    void oblicz_geometrie_dialogu(int& szer, int& wys) override {
        SkalaUI s = skala_dpi_;
        int label_w = szerokosc_etykiety_dolnej(s);
        int filter_w = szerokosc_combo_filtra(s);
        int btn_w = szerokosc_przycisku(tekst_przycisku_akcji(), s) +
                    szerokosc_przycisku("Anuluj", s) + 10 * s;
        int needed_w = 28 * s + label_w + 8 * s + filter_w + 16 * s + btn_w + 28 * s;
        int base_w = 740 * s;
        szer = needed_w > base_w ? needed_w : base_w;
        wys = 500 * s;
    }

    void zbuduj_interfejs(int w, int h) override {
        wyczysc_kontrolki();
        wyczysc_sloty_przyciskow();
        panel_tresci_ = nullptr;
        panel_przyciskow_ = nullptr;
        uklad_ = nullptr;
        SkalaUI s = skala_dpi_;

        uklad_ = new PanelDokowany();
        uklad_->ustaw_wypelnienie(Marginesy(0, 0, 0, 0));
        uklad_->ustaw_odstep(0);
        wyzeruj_tlo(*uklad_);
        dodaj_kontrolke(uklad_);

        // 1. Pasek narzedzi na gorze
        pasek_narzedzi_ = new PasekNarzedzi();
        pasek_narzedzi_->ustaw_preferowany_rozmiar(Rozmiar(-1, 32));
        pasek_narzedzi_->ustaw_pokazywanie_tekstu(true);
        pasek_narzedzi_->dodaj_przycisk("Wstecz", 0, [](ElementPaskaNarzedzi*, void* d) {
            static_cast<DialogPliku*>(d)->przejdz_wstecz();
        }, this);
        pasek_narzedzi_->dodaj_przycisk("Poziom wyżej", 0, [](ElementPaskaNarzedzi*, void* d) {
            static_cast<DialogPliku*>(d)->przejdz_poziom_wyzej();
        }, this);
        pasek_narzedzi_->dodaj_przycisk("Odśwież", 0, [](ElementPaskaNarzedzi*, void* d) {
            static_cast<DialogPliku*>(d)->odswiez();
        }, this);
        pasek_narzedzi_->dodaj_separator();
        pasek_narzedzi_->dodaj_przycisk("Nowy katalog", 0, [](ElementPaskaNarzedzi*, void* d) {
            static_cast<DialogPliku*>(d)->otworz_dialog_nowego_katalogu();
        }, this);
        uklad_->dodaj_dokowany(pasek_narzedzi_, Dokowanie::Gora);

        // 2. Pasek sciezki pod paskiem narzedzi
        pasek_sciezki_ = new PasekSciezki();
        pasek_sciezki_->ustaw_preferowany_rozmiar(Rozmiar(-1, 28));
        pasek_sciezki_->ustaw_sciezke(biezacy_katalog_);
        pasek_sciezki_->przy_wyborze_segmentu([](PasekSciezki*, int idx, const char*, void* d) {
            DialogPliku* dlg = static_cast<DialogPliku*>(d);
            if (!dlg || !dlg->pasek_sciezki_) return;
            char sciezka[MAKS_SCIEZKI]{};
            sciezka[0] = '/';
            size_t out_l = 1;
            for (int i = 0; i <= idx; ++i) {
                const char* seg = dlg->pasek_sciezki_->segment(i);
                size_t slen = dlugosc_str(seg);
                for (size_t k = 0; k < slen && out_l + 1 < sizeof(sciezka); ++k) {
                    sciezka[out_l++] = seg[k];
                }
                if (i < idx && out_l + 1 < sizeof(sciezka)) sciezka[out_l++] = '/';
            }
            sciezka[out_l] = '\0';
            dlg->przejdz_do_katalogu(sciezka);
        }, this);
        uklad_->dodaj_dokowany(pasek_sciezki_, Dokowanie::Gora);

        // 3. Dolny panel: wpis nazwy, filtry oraz przyciski akcji
        int wysokosc_pola = wysokosc_kontrolki_pola(s);
        int wysokosc_przycisku = wysokosc_przycisku_akcji(s);
        int wysokosc_wiersza = wysokosc_pola > wysokosc_przycisku ? wysokosc_pola : wysokosc_przycisku;
        int wysokosc_panelu = ui_odskaluj(8 * s + wysokosc_pola + 8 * s + wysokosc_wiersza + 10 * s, s.procent());

        panel_przyciskow_ = new Wiersz();
        panel_przyciskow_->ustaw_preferowany_rozmiar(Rozmiar(-1, wysokosc_panelu));
        panel_przyciskow_->ustaw_wypelnienie(Marginesy(14, 8, 14, 10));
        panel_przyciskow_->ustaw_odstep(0);
        wyzeruj_tlo(*panel_przyciskow_);
        uklad_->dodaj_dokowany(panel_przyciskow_, Dokowanie::Dol);

        Kolumna* dol_kolumna = new Kolumna();
        dol_kolumna->ustaw_wypelnienie(Marginesy(0, 0, 0, 0));
        dol_kolumna->ustaw_odstep(8);
        dol_kolumna->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        dol_kolumna->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        panel_przyciskow_->dodaj_kontrolke(dol_kolumna);

        const int etykieta_w = ui_odskaluj(szerokosc_etykiety_dolnej(s), s.procent());

        // Rzad 1: Etykieta + PoleTekstowe
        Wiersz* wiersz_wpisu = new Wiersz();
        wiersz_wpisu->ustaw_wypelnienie(Marginesy(0, 0, 0, 0));
        wiersz_wpisu->ustaw_preferowany_rozmiar(Rozmiar(-1, ui_odskaluj(wysokosc_pola, s.procent())));
        wiersz_wpisu->ustaw_odstep(8);
        wiersz_wpisu->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Srodek);
        dol_kolumna->dodaj_kontrolke(wiersz_wpisu);

        Etykieta* ety_nazwa = new Etykieta(tryb_ == TrybDialoguPliku::WybierzKatalog ? "Katalog:" : "Nazwa pliku:");
        ety_nazwa->ustaw_preferowany_rozmiar(Rozmiar(etykieta_w, ui_odskaluj(wysokosc_pola, s.procent())));
        wiersz_wpisu->dodaj_kontrolke(ety_nazwa);

        pole_nazwy_ = new PoleTekstowe(tryb_ == TrybDialoguPliku::WybierzKatalog ? "(bieżący katalog)" : "Nazwa pliku...");
        pole_nazwy_->ustaw_preferowany_rozmiar(Rozmiar(-1, ui_odskaluj(wysokosc_pola, s.procent())));
        pole_nazwy_->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        if (wybrana_nazwa_[0]) pole_nazwy_->ustaw_tekst(wybrana_nazwa_);
        pole_nazwy_->przy_zatwierdzeniu([](PoleTekstowe*, const char*, void* d) {
            static_cast<DialogPliku*>(d)->zatwierdz();
        }, this);
        wiersz_wpisu->dodaj_kontrolke(pole_nazwy_);

        // Rzad 2: Etykieta filtrow + ListaRozwijana + Wypychacz + Przyciski akcji
        Wiersz* wiersz_akcji = new Wiersz();
        wiersz_akcji->ustaw_wypelnienie(Marginesy(0, 0, 0, 0));
        wiersz_akcji->ustaw_preferowany_rozmiar(Rozmiar(-1, ui_odskaluj(wysokosc_wiersza, s.procent())));
        wiersz_akcji->ustaw_odstep(10);
        wiersz_akcji->ustaw_wyrownanie_poziome(WyrownaniePoziome::Koniec);
        wiersz_akcji->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Srodek);
        dol_kolumna->dodaj_kontrolke(wiersz_akcji);

        Etykieta* ety_filtr = new Etykieta("Typ pliku:");
        ety_filtr->ustaw_preferowany_rozmiar(Rozmiar(etykieta_w, ui_odskaluj(wysokosc_pola, s.procent())));
        wiersz_akcji->dodaj_kontrolke(ety_filtr);

        filtr_combo_ = new ListaRozwijana();
        filtr_combo_->ustaw_preferowany_rozmiar(Rozmiar(ui_odskaluj(szerokosc_combo_filtra(s), s.procent()),
                                                       ui_odskaluj(wysokosc_pola, s.procent())));
        filtr_combo_->ustaw_minimalny_rozmiar(Rozmiar(180, ui_odskaluj(wysokosc_pola, s.procent())));
        for (size_t i = 0; i < liczba_filtrow_; ++i) {
            filtr_combo_->dodaj_element(filtry_[i].nazwa);
        }
        if (liczba_filtrow_ > 0) {
            filtr_combo_->ustaw_wybrany_indeks(static_cast<int>(aktywny_filtr_));
        }
        filtr_combo_->przy_zmianie_wyboru([](ListaRozwijana*, int idx, void* d) {
            DialogPliku* dlg = static_cast<DialogPliku*>(d);
            if (dlg && idx >= 0) {
                dlg->aktywny_filtr_ = static_cast<size_t>(idx);
                dlg->odswiez_widok_tabeli();
            }
        }, this);
        if (tryb_ == TrybDialoguPliku::WybierzKatalog) {
            ety_filtr->ustaw_widoczna(false);
            filtr_combo_->ustaw_widoczna(false);
        }
        wiersz_akcji->dodaj_kontrolke(filtr_combo_);

        Kontrolka* wyp = new Kontrolka();
        wyp->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        wyp->ustaw_mozliwosc_fokusu(false);
        wiersz_akcji->dodaj_kontrolke(wyp);

        const char* txt_akcja = tekst_przycisku_akcji();
        int btn_akcja_w = szerokosc_przycisku(txt_akcja, s);

        btn_akcja_ = new Przycisk(txt_akcja);
        btn_akcja_->ustaw_preferowany_rozmiar(Rozmiar(ui_odskaluj(btn_akcja_w, s.procent()),
                                                     ui_odskaluj(wysokosc_przycisku, s.procent())));
        wiersz_akcji->dodaj_kontrolke(btn_akcja_);
        zarejestruj_przycisk_akcji(btn_akcja_, WynikDialogu::OK, true, false);

        int btn_anuluj_w = szerokosc_przycisku("Anuluj", s);

        btn_anuluj_ = new Przycisk("Anuluj");
        btn_anuluj_->ustaw_preferowany_rozmiar(Rozmiar(ui_odskaluj(btn_anuluj_w, s.procent()),
                                                       ui_odskaluj(wysokosc_przycisku, s.procent())));
        wiersz_akcji->dodaj_kontrolke(btn_anuluj_);
        zarejestruj_przycisk_akcji(btn_anuluj_, WynikDialogu::Anuluj, false, true);

        btn_akcja_->przy_kliknieciu([](Przycisk*, void* d) {
            static_cast<DialogPliku*>(d)->zatwierdz();
        }, this);
        btn_anuluj_->przy_kliknieciu([](Przycisk*, void* d) {
            static_cast<DialogPliku*>(d)->anuluj();
        }, this);

        // 4. Glowny obszar: Rozdzielacz z lewym panelem Drzewo i prawym Tabela
        rozdzielacz_ = new Rozdzielacz(Orientacja::Pozioma);
        rozdzielacz_->ustaw_pozycje_podzialu(170 * s);
        rozdzielacz_->ustaw_minimalne_rozmiary(120 * s, 300 * s);
        uklad_->dodaj_dokowany(rozdzielacz_, Dokowanie::Wypelnij);
        panel_tresci_ = rozdzielacz_;

        // Lewy panel: Szybki dostep
        drzewo_miejsc_ = new Drzewo();
        drzewo_miejsc_->ustaw_preferowany_rozmiar(Rozmiar(170, -1));
        zainicjalizuj_drzewo_miejsc();
        rozdzielacz_->ustaw_pierwszy(drzewo_miejsc_);

        // Prawy panel: Tabela plikow
        tabela_plikow_ = new Tabela();
        tabela_plikow_->ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
        tabela_plikow_->dodaj_kolumne("Nazwa", 300 * s, WyrownaniePoziome::Poczatek);
        tabela_plikow_->dodaj_kolumne("Rozmiar", 100 * s, WyrownaniePoziome::Koniec);
        tabela_plikow_->dodaj_kolumne("Typ", 140 * s, WyrownaniePoziome::Poczatek);

        tabela_plikow_->przy_zmianie_zaznaczenia([](Tabela*, int row, void* d) {
            DialogPliku* dlg = static_cast<DialogPliku*>(d);
            if (!dlg || row < 0 || row >= static_cast<int>(dlg->liczba_wyswietlanych_)) return;
            const WpisKatalogu& w = dlg->wyswietlane_[row];
            if (dlg->tryb_ != TrybDialoguPliku::WybierzKatalog) {
                if (!w.katalog && dlg->pole_nazwy_) {
                    dlg->pole_nazwy_->ustaw_tekst(w.nazwa);
                }
            } else {
                if (dlg->pole_nazwy_) {
                    dlg->pole_nazwy_->ustaw_tekst(w.nazwa);
                }
            }
        }, this);

        tabela_plikow_->przy_aktywacji([](Tabela*, int row, void* d) {
            DialogPliku* dlg = static_cast<DialogPliku*>(d);
            if (!dlg || row < 0 || row >= static_cast<int>(dlg->liczba_wyswietlanych_)) return;
            const WpisKatalogu& w = dlg->wyswietlane_[row];
            if (w.katalog) {
                char target[MAKS_SCIEZKI]{};
                dlg->polacz_sciezki(dlg->biezacy_katalog_, w.nazwa, target, sizeof(target));
                dlg->przejdz_do_katalogu(target);
            } else if (dlg->tryb_ == TrybDialoguPliku::Otworz) {
                if (dlg->pole_nazwy_) dlg->pole_nazwy_->ustaw_tekst(w.nazwa);
                dlg->zatwierdz();
            }
        }, this);

        rozdzielacz_->ustaw_drugi(tabela_plikow_);

        // Kolejnosc Tab
        kontrolki_fokusu_[0] = drzewo_miejsc_;
        kontrolki_fokusu_[1] = tabela_plikow_;
        kontrolki_fokusu_[2] = pole_nazwy_;
        kontrolki_fokusu_[3] = filtr_combo_;
        kontrolki_fokusu_[4] = btn_akcja_;
        kontrolki_fokusu_[5] = btn_anuluj_;
        liczba_kontrolek_fokusu_ = 6;
        indeks_fokusu_ = 1;

        uklad_->rozmiesc(Prostokat(0, 32 * s, w, h > 32 * s ? h - 32 * s : 0));
        aktualizuj_obszary_ukladu();

        (void)odswiez_katalog();
    }

private:
    void zainicjalizuj_drzewo_miejsc() {
        if (!drzewo_miejsc_) return;
        drzewo_miejsc_->wyczysc();
        UchwytWezlaDrzewa k_root = drzewo_miejsc_->dodaj_korzen("Główny (/)", 1);
        drzewo_miejsc_->dodaj_dziecko(k_root, "Użytkownicy", 2);
        drzewo_miejsc_->dodaj_dziecko(k_root, "Pulpit", 3);
        drzewo_miejsc_->dodaj_dziecko(k_root, "Dokumenty", 4);
        drzewo_miejsc_->dodaj_dziecko(k_root, "System", 5);

        drzewo_miejsc_->przy_aktywacji([](Drzewo*, UchwytWezlaDrzewa w, void* d) {
            DialogPliku* dlg = static_cast<DialogPliku*>(d);
            if (!dlg) return;
            switch (w.indeks) {
                case 0: dlg->przejdz_do_katalogu("/"); break;
                case 1: dlg->przejdz_do_katalogu("/uzytkownicy"); break;
                case 2: dlg->przejdz_do_katalogu("/uzytkownicy/Pulpit"); break;
                case 3: dlg->przejdz_do_katalogu("/uzytkownicy/Dokumenty"); break;
                case 4: dlg->przejdz_do_katalogu("/system"); break;
                default: break;
            }
        }, this);
    }

    bool odswiez_katalog() {
        liczba_wpisow_ = 0;
        listing_niepelny_ = false;
        char* bufor = static_cast<char*>(gui_malloc(ROZMIAR_LISTINGU));
        if (!bufor) return false;

        bool wynik = wylistuj_katalog_uzytkownika(biezacy_katalog_, bufor, ROZMIAR_LISTINGU);
        if (!wynik) {
            gui_free(bufor);
            odswiez_widok_tabeli();
            return false;
        }

        size_t dl = dlugosc_str_limit(bufor, ROZMIAR_LISTINGU);
        if (dl >= ROZMIAR_LISTINGU || (dl > 0 && bufor[dl - 1] != '\n')) {
            listing_niepelny_ = true;
            gui_free(bufor);
            odswiez_widok_tabeli();
            if (zarzadca()) {
                Komunikat::blad(*zarzadca(), "Lista katalogu niepełna",
                                "BWS zwrócił niepełną listę katalogu. Zawartość nie zostanie pokazana jako kompletna.",
                                nullptr, nullptr, this);
            }
            return false;
        }

        size_t pos = 0;
        while (bufor[pos] != '\0' && liczba_wpisow_ < MAKS_WPISOW) {
            size_t end = pos;
            while (bufor[end] != '\0' && bufor[end] != '\n') ++end;
            if (end - pos > 7) {
                bool is_dir = (bufor[pos] == '[' && bufor[pos + 1] == 'K');
                bool is_file = (bufor[pos] == '[' && bufor[pos + 1] == 'P');
                if (is_dir || is_file) {
                    size_t name_len = end - (pos + 7);
                    if (name_len > 0 && name_len < MAKS_NAZWY) {
                        WpisKatalogu& wp = wpisy_[liczba_wpisow_++];
                        for (size_t i = 0; i < name_len; ++i) {
                            wp.nazwa[i] = bufor[pos + 7 + i];
                        }
                        wp.nazwa[name_len] = '\0';
                        wp.katalog = is_dir;
                        wp.rozmiar = 0;
                        wp.czas_rtc = 0;

                        char full[MAKS_SCIEZKI]{};
                        if (polacz_sciezki(biezacy_katalog_, wp.nazwa, full, sizeof(full))) {
                            BwsMetadanePliku meta{};
                            if (pobierz_metadane_pliku(full, &meta)) {
                                wp.rozmiar = meta.rozmiar;
                                wp.czas_rtc = meta.czas_utworzenia_rtc;
                            }
                        }
                    }
                }
            }
            pos = (bufor[end] == '\n') ? end + 1 : end;
        }

        gui_free(bufor);
        odswiez_widok_tabeli();
        return true;
    }

    static size_t dlugosc_str_limit(const char* s, size_t limit) {
        if (!s) return 0;
        size_t i = 0;
        while (i < limit && s[i]) ++i;
        return i;
    }

    static int wysokosc_kontrolki_pola(SkalaUI s) {
        int h = CzcionkaUI::pobierz_wysokosc(s) + 14 * s;
        if (h < 30 * s) h = 30 * s;
        return h;
    }

    static int wysokosc_przycisku_akcji(SkalaUI s) {
        int h = CzcionkaUI::pobierz_wysokosc(s) + 16 * s;
        if (h < 30 * s) h = 30 * s;
        return h;
    }

    static int szerokosc_przycisku(const char* tekst, SkalaUI s) {
        int w = CzcionkaUI::zmierz_tekst(tekst ? tekst : "", s) + 32 * s;
        if (w < 95 * s) w = 95 * s;
        return w;
    }

    static bool czy_filtr_wszystkie(const char* rozszerzenia) {
        return !rozszerzenia || !rozszerzenia[0] ||
               rowne_str(rozszerzenia, "*.*") ||
               rowne_str(rozszerzenia, "*") ||
               rowne_str(rozszerzenia, ".");
    }

    int szerokosc_etykiety_dolnej(SkalaUI s) const {
        int w1 = CzcionkaUI::zmierz_tekst(tryb_ == TrybDialoguPliku::WybierzKatalog ? "Katalog:" : "Nazwa pliku:", s);
        int w2 = CzcionkaUI::zmierz_tekst("Typ pliku:", s);
        int w = (w1 > w2 ? w1 : w2) + 6 * s;
        if (w < 92 * s) w = 92 * s;
        return w;
    }

    int szerokosc_combo_filtra(SkalaUI s) const {
        int max_w = CzcionkaUI::zmierz_tekst("Wszystkie pliki (*.*)", s);
        for (size_t i = 0; i < liczba_filtrow_; ++i) {
            int w = CzcionkaUI::zmierz_tekst(filtry_[i].nazwa, s);
            if (w > max_w) max_w = w;
        }
        max_w += 44 * s;
        if (max_w < 260 * s) max_w = 260 * s;
        return max_w;
    }

    void odswiez_widok_tabeli() {
        if (!tabela_plikow_) return;
        tabela_plikow_->wyczysc_wiersze();
        liczba_wyswietlanych_ = 0;

        const char* current_ext = "*.*";
        bool czy_wszystkie = true;
        if (aktywny_filtr_ < liczba_filtrow_) {
            current_ext = filtry_[aktywny_filtr_].rozszerzenia;
            czy_wszystkie = filtry_[aktywny_filtr_].czy_wszystkie;
        }

        // Najpierw katalogi, potem pliki
        for (int pass = 0; pass < 2; ++pass) {
            bool dir_pass = (pass == 0);
            for (size_t i = 0; i < liczba_wpisow_; ++i) {
                const WpisKatalogu& wp = wpisy_[i];
                if (wp.katalog != dir_pass) continue;

                // Katalogi zawsze widoczne. Pliki podlegaja filtrowi.
                if (!wp.katalog && !czy_wszystkie && !sprawdz_filtr(wp.nazwa, current_ext)) {
                    continue;
                }

                if (liczba_wyswietlanych_ < MAKS_WPISOW) {
                    wyswietlane_[liczba_wyswietlanych_] = wp;
                    char col0[MAKS_NAZWY + 8]{};
                    if (wp.katalog) {
                        col0[0] = '['; col0[1] = 'K'; col0[2] = 'A'; col0[3] = 'T'; col0[4] = ']'; col0[5] = ' ';
                        size_t l = 6;
                        for (size_t k = 0; wp.nazwa[k] && l + 1 < sizeof(col0); ++k) col0[l++] = wp.nazwa[k];
                        col0[l] = '\0';
                    } else {
                        kopiuj_str(col0, sizeof(col0), wp.nazwa);
                    }

                    char col1[32]{};
                    if (wp.katalog) {
                        kopiuj_str(col1, sizeof(col1), "<KAT>");
                    } else {
                        formatuj_rozmiar(wp.rozmiar, col1, sizeof(col1));
                    }

                    char col2[32]{};
                    okresl_typ_pliku(wp.nazwa, wp.katalog, col2, sizeof(col2));

                    const char* komorki[] = { col0, col1, col2 };
                    tabela_plikow_->dodaj_wiersz(komorki, 3, liczba_wyswietlanych_);
                    ++liczba_wyswietlanych_;
                }
            }
        }
        tabela_plikow_->uniewaznij();
    }

    void okresl_typ_pliku(const char* nazwa, bool is_dir, char* out, size_t cap) const {
        if (is_dir) {
            kopiuj_str(out, cap, "Folder");
            return;
        }
        if (sprawdz_filtr(nazwa, "*.bur")) kopiuj_str(out, cap, "Program .bur");
        else if (sprawdz_filtr(nazwa, "*.txt;*.log;*.md")) kopiuj_str(out, cap, "Dokument");
        else if (sprawdz_filtr(nazwa, "*.cebula")) kopiuj_str(out, cap, "Skrypt .cebula");
        else if (sprawdz_filtr(nazwa, "*.bmp")) kopiuj_str(out, cap, "Obraz BMP");
        else if (sprawdz_filtr(nazwa, "*.bursztyn_projekt")) kopiuj_str(out, cap, "Projekt Bursztyn");
        else if (sprawdz_filtr(nazwa, "*.mesh")) kopiuj_str(out, cap, "Siatka 3D Mesh");
        else kopiuj_str(out, cap, "Plik");
    }

    static void formatuj_u64(uint64_t val, char* b, size_t cap) {
        if (!b || cap < 2) return;
        char tmp[24];
        size_t p = 0;
        do {
            tmp[p++] = static_cast<char>('0' + (val % 10ULL));
            val /= 10ULL;
        } while (val > 0 && p < sizeof(tmp));
        size_t o = 0;
        while (p > 0 && o + 1 < cap) b[o++] = tmp[--p];
        b[o] = '\0';
    }

    bool ma_rozszerzenie(const char* s) const {
        if (!s) return false;
        size_t l = dlugosc_str(s);
        for (size_t i = 0; i < l; ++i) {
            if (s[i] == '.') return true;
        }
        return false;
    }

    const char* pobierz_domyslne_rozszerzenie_filtru() const {
        if (aktywny_filtr_ >= liczba_filtrow_) return "";
        if (filtry_[aktywny_filtr_].czy_wszystkie) return "";
        const char* f = filtry_[aktywny_filtr_].rozszerzenia;
        if (czy_filtr_wszystkie(f)) return "";
        if (f[0] == '*' && f[1] == '.') return f + 1; // np. ".txt"
        if (f[0] == '.') return f;
        return "";
    }

    void dodaj_do_historii(const char* sciezka) {
        if (!sciezka || !sciezka[0]) return;
        if (pozycja_historii_ + 1 < static_cast<int>(MAKS_HISTORII)) {
            ++pozycja_historii_;
            kopiuj_str(historia_[pozycja_historii_], sizeof(historia_[0]), sciezka);
            liczba_historii_ = static_cast<size_t>(pozycja_historii_ + 1);
        }
    }

    void nawiguj_fokus(bool nastepny) {
        if (liczba_kontrolek_fokusu_ == 0) return;
        if (nastepny) {
            indeks_fokusu_ = (indeks_fokusu_ + 1) % liczba_kontrolek_fokusu_;
        } else {
            indeks_fokusu_ = (indeks_fokusu_ + liczba_kontrolek_fokusu_ - 1) % liczba_kontrolek_fokusu_;
        }
        Kontrolka* k = kontrolki_fokusu_[indeks_fokusu_];
        if (k && k->czy_widoczna()) {
            if (zarzadca()) {
                zarzadca()->ustaw_fokus_popupu(k);
            } else {
                k->przy_fokusie();
            }
            k->uniewaznij();
        }
    }

    void synchronizuj_indeks_fokusu(Kontrolka* fokus) {
        if (!fokus) return;
        for (size_t i = 0; i < liczba_kontrolek_fokusu_; ++i) {
            if (kontrolki_fokusu_[i] == fokus) {
                indeks_fokusu_ = i;
                return;
            }
        }
    }

    TrybDialoguPliku tryb_{TrybDialoguPliku::Otworz};
    WynikDialoguPliku wynik_pliku_{WynikDialoguPliku::Brak};

    char biezacy_katalog_[MAKS_SCIEZKI]{};
    char wybrana_sciezka_[MAKS_SCIEZKI]{};
    char wybrana_nazwa_[MAKS_NAZWY]{};
    char sciezka_do_potwierdzenia_[MAKS_SCIEZKI]{};

    FiltrPliku filtry_[MAKS_FILTROW]{};
    size_t liczba_filtrow_{0};
    size_t aktywny_filtr_{0};

    char historia_[MAKS_HISTORII][MAKS_SCIEZKI]{};
    size_t liczba_historii_{0};
    int pozycja_historii_{-1};

    WpisKatalogu wpisy_[MAKS_WPISOW]{};
    size_t liczba_wpisow_{0};
    WpisKatalogu wyswietlane_[MAKS_WPISOW]{};
    size_t liczba_wyswietlanych_{0};
    bool listing_niepelny_{false};

    FunkcjaWynikuPliku cb_wynik_pliku_{nullptr};
    void* dane_wyniku_pliku_{nullptr};

    PasekNarzedzi* pasek_narzedzi_{nullptr};
    PasekSciezki* pasek_sciezki_{nullptr};
    Rozdzielacz* rozdzielacz_{nullptr};
    Drzewo* drzewo_miejsc_{nullptr};
    Tabela* tabela_plikow_{nullptr};
    PoleTekstowe* pole_nazwy_{nullptr};
    ListaRozwijana* filtr_combo_{nullptr};
    Przycisk* btn_akcja_{nullptr};
    Przycisk* btn_anuluj_{nullptr};

    Kontrolka* kontrolki_fokusu_[8]{};
    size_t liczba_kontrolek_fokusu_{0};
    size_t indeks_fokusu_{0};
};

inline void DialogNowegoKatalogu::zatwierdz_katalog() {
    const char* txt = pole_nazwy_ ? pole_nazwy_->tekst() : "";
    while (*txt == ' ') ++txt;
    if (!txt[0] || !DialogPliku::czy_poprawna_nazwa_elementu(txt)) {
        if (zarzadca()) {
            Komunikat::ostrzezenie(*zarzadca(), "Nieprawidłowa nazwa",
                                   "Nazwa katalogu jest nieprawidłowa lub zawiera niedozwolone znaki.",
                                   nullptr, nullptr, this);
        }
        return;
    }
    if (rodzic_dialogu_) {
        if (rodzic_dialogu_->utworz_nowy_katalog(txt)) {
            zamknij_z_wynikiem(WynikDialogu::OK);
        }
    } else {
        zamknij_z_wynikiem(WynikDialogu::OK);
    }
}

} // namespace BursztynUI
