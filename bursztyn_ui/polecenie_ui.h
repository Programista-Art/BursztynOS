#pragma once

#include <stdint.h>
#include <stddef.h>
#include "bws_zdarzenia.h"

namespace BursztynUI {

class PolecenieUI;
class ListaPolecen;

class OdbiorcaListyPolecen {
public:
    virtual ~OdbiorcaListyPolecen() = default;
    virtual void lista_polecen_zniszczona(ListaPolecen& lista) = 0;
};

// Odbiorca jest nieposiadajacym wlascicielstwa wezlem wiazania. Zarowno
// PolecenieUI, jak i odbiorca odpinaja polaczenie w swoich destruktorach.
class OdbiorcaPolecenia {
public:
    virtual ~OdbiorcaPolecenia() = default;
    virtual void odswiez_z_polecenia(PolecenieUI& polecenie) = 0;
    virtual void polecenie_zniszczone(PolecenieUI& polecenie) = 0;
};

struct SkrotPolecenia {
    uint32_t kod{0};
    uint32_t modyfikatory{0};

    constexpr SkrotPolecenia() = default;
    constexpr SkrotPolecenia(uint32_t k, uint32_t m) : kod(k), modyfikatory(m) {}
    static constexpr SkrotPolecenia usb(uint8_t uzycie, uint32_t modyfikatory) {
        return SkrotPolecenia(BWS_KEY_SOURCE_USB | uzycie, modyfikatory);
    }
    constexpr bool czy_pusty() const { return kod == 0; }
};

class PolecenieUI {
public:
    static constexpr size_t MAKS_ODBIORCOW = 64;
    using FunkcjaWykonania = void (*)(PolecenieUI& polecenie, void* kontekst);

    explicit PolecenieUI(uint32_t identyfikator = 0, const char* tekst = "")
        : identyfikator_(identyfikator) {
        kopiuj(tekst_, sizeof(tekst_), tekst);
    }
    ~PolecenieUI() {
        // Callback moze niszczyc lub przepinac inne obiekty, dlatego przed
        // zawiadomieniem usuwamy wpis z tablicy.
        while (liczba_odbiorcow_ > 0) {
            OdbiorcaPolecenia* odbiorca = odbiorcy_[--liczba_odbiorcow_];
            odbiorcy_[liczba_odbiorcow_] = nullptr;
            if (odbiorca) odbiorca->polecenie_zniszczone(*this);
        }
    }
    PolecenieUI(const PolecenieUI&) = delete;
    PolecenieUI& operator=(const PolecenieUI&) = delete;

    uint32_t identyfikator() const { return identyfikator_; }
    void ustaw_identyfikator(uint32_t v) { if (identyfikator_ != v) { identyfikator_ = v; powiadom(); } }
    const char* tekst() const { return tekst_; }
    void ustaw_tekst(const char* v) {
        char nowy[sizeof(tekst_)]; kopiuj(nowy, sizeof(nowy), v);
        if (rowne(tekst_, nowy)) return;
        kopiuj(tekst_, sizeof(tekst_), nowy); powiadom();
    }
    uint32_t ikona() const { return ikona_; }
    void ustaw_ikone(uint32_t v) { if (ikona_ != v) { ikona_ = v; powiadom(); } }
    SkrotPolecenia skrot() const { return skrot_; }
    const char* etykieta_skrotu() const { return etykieta_skrotu_; }
    void ustaw_skrot(SkrotPolecenia v, const char* etykieta = "") {
        char nowa[sizeof(etykieta_skrotu_)]; kopiuj(nowa, sizeof(nowa), etykieta);
        if (skrot_.kod == v.kod && skrot_.modyfikatory == v.modyfikatory &&
            rowne(etykieta_skrotu_, nowa)) return;
        skrot_ = v; kopiuj(etykieta_skrotu_, sizeof(etykieta_skrotu_), nowa); powiadom();
    }
    bool czy_wlaczona() const { return wlaczona_; }
    void ustaw_wlaczona(bool v) { if (wlaczona_ != v) { wlaczona_ = v; powiadom(); } }
    bool czy_widoczna() const { return widoczna_; }
    void ustaw_widoczna(bool v) { if (widoczna_ != v) { widoczna_ = v; powiadom(); } }
    bool czy_zaznaczona() const { return zaznaczona_; }
    void ustaw_zaznaczona(bool v) { if (zaznaczona_ != v) { zaznaczona_ = v; powiadom(); } }

    void przy_wykonaniu(FunkcjaWykonania cb, void* kontekst = nullptr) {
        wykonanie_ = cb; kontekst_ = kontekst;
    }
    bool wykonaj() {
        if (!wlaczona_ || !widoczna_) return false;
        FunkcjaWykonania cb = wykonanie_;
        void* kontekst = kontekst_;
        if (!cb) return false;
        cb(*this, kontekst);
        // Callback moze usunac polecenie; po jego wywolaniu nie odczytujemy
        // juz zadnego pola obiektu.
        return true;
    }
    bool pasuje_do(uint32_t kod, uint32_t modyfikatory) const {
        constexpr uint32_t MASKA = BWS_MOD_SHIFT | BWS_MOD_CTRL | BWS_MOD_ALT | BWS_MOD_SUPER;
        return !skrot_.czy_pusty() && skrot_.kod == kod &&
               (skrot_.modyfikatory & MASKA) == (modyfikatory & MASKA);
    }

    bool dolacz(OdbiorcaPolecenia* odbiorca) {
        if (!odbiorca) return false;
        for (size_t i = 0; i < liczba_odbiorcow_; ++i)
            if (odbiorcy_[i] == odbiorca) return true;
        if (liczba_odbiorcow_ >= MAKS_ODBIORCOW) return false;
        odbiorcy_[liczba_odbiorcow_++] = odbiorca;
        odbiorca->odswiez_z_polecenia(*this);
        return true;
    }
    void odlacz(OdbiorcaPolecenia* odbiorca) {
        for (size_t i = 0; i < liczba_odbiorcow_; ++i) {
            if (odbiorcy_[i] != odbiorca) continue;
            for (size_t j = i; j + 1 < liczba_odbiorcow_; ++j) odbiorcy_[j] = odbiorcy_[j + 1];
            odbiorcy_[--liczba_odbiorcow_] = nullptr;
            return;
        }
    }
    bool zastap_odbiorce(OdbiorcaPolecenia* stary, OdbiorcaPolecenia* nowy) {
        if (!stary || !nowy) return false;
        for (size_t i = 0; i < liczba_odbiorcow_; ++i) {
            if (odbiorcy_[i] == stary) { odbiorcy_[i] = nowy; return true; }
        }
        return false;
    }
    size_t liczba_odbiorcow() const { return liczba_odbiorcow_; }

private:
    static void kopiuj(char* dst, size_t cap, const char* src) {
        if (!src) src = "";
        size_t i = 0; for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i]; dst[i] = '\0';
    }
    static bool rowne(const char* a, const char* b) {
        size_t i = 0; while (a[i] && a[i] == b[i]) ++i; return a[i] == b[i];
    }
    void powiadom() {
        // Odbiorca moze sie odpiac podczas powiadomienia. Ponowne sprawdzenie
        // indeksu zapobiega pominieciu przesunietego wpisu.
        size_t i = 0;
        while (i < liczba_odbiorcow_) {
            OdbiorcaPolecenia* biezacy = odbiorcy_[i];
            if (biezacy) biezacy->odswiez_z_polecenia(*this);
            if (i < liczba_odbiorcow_ && odbiorcy_[i] == biezacy) ++i;
        }
    }

    uint32_t identyfikator_{0};
    char tekst_[96]{};
    uint32_t ikona_{0};
    SkrotPolecenia skrot_{};
    char etykieta_skrotu_[40]{};
    bool wlaczona_{true};
    bool widoczna_{true};
    bool zaznaczona_{false};
    FunkcjaWykonania wykonanie_{nullptr};
    void* kontekst_{nullptr};
    OdbiorcaPolecenia* odbiorcy_[MAKS_ODBIORCOW]{};
    size_t liczba_odbiorcow_{0};
};

class ListaPolecen : private OdbiorcaPolecenia {
public:
    static constexpr size_t MAKS_POLECEN = 128;
    static constexpr size_t MAKS_ODBIORCOW = 16;
    ~ListaPolecen() override {
        while (liczba_odbiorcow_ > 0) {
            OdbiorcaListyPolecen* o = odbiorcy_[--liczba_odbiorcow_];
            odbiorcy_[liczba_odbiorcow_] = nullptr;
            if (o) o->lista_polecen_zniszczona(*this);
        }
        wyczysc();
    }
    ListaPolecen(const ListaPolecen&) = delete;
    ListaPolecen& operator=(const ListaPolecen&) = delete;
    ListaPolecen() = default;

    bool dodaj(PolecenieUI& p) {
        for (size_t i = 0; i < liczba_; ++i) if (polecenia_[i] == &p) return true;
        if (liczba_ >= MAKS_POLECEN || !p.dolacz(this)) return false;
        polecenia_[liczba_++] = &p; return true;
    }
    bool usun(PolecenieUI& p) {
        for (size_t i = 0; i < liczba_; ++i) {
            if (polecenia_[i] != &p) continue;
            p.odlacz(this); usun_indeks(i); return true;
        }
        return false;
    }
    void wyczysc() {
        while (liczba_ > 0) {
            PolecenieUI* p = polecenia_[--liczba_]; polecenia_[liczba_] = nullptr;
            if (p) p->odlacz(this);
        }
    }
    size_t liczba_polecen() const { return liczba_; }
    PolecenieUI* polecenie(size_t i) const { return i < liczba_ ? polecenia_[i] : nullptr; }
    PolecenieUI* znajdz(uint32_t id) const {
        for (size_t i = 0; i < liczba_; ++i)
            if (polecenia_[i] && polecenia_[i]->identyfikator() == id) return polecenia_[i];
        return nullptr;
    }
    bool wykonaj_skrot(uint32_t kod, uint32_t modyfikatory) {
        for (size_t i = 0; i < liczba_; ++i) {
            PolecenieUI* p = polecenia_[i];
            if (p && p->czy_widoczna() && p->czy_wlaczona() && p->pasuje_do(kod, modyfikatory))
                return p->wykonaj();
        }
        return false;
    }
    bool dolacz_odbiorce(OdbiorcaListyPolecen* o) {
        if (!o) return false;
        for (size_t i = 0; i < liczba_odbiorcow_; ++i) if (odbiorcy_[i] == o) return true;
        if (liczba_odbiorcow_ >= MAKS_ODBIORCOW) return false;
        odbiorcy_[liczba_odbiorcow_++] = o; return true;
    }
    void odlacz_odbiorce(OdbiorcaListyPolecen* o) {
        for (size_t i = 0; i < liczba_odbiorcow_; ++i) {
            if (odbiorcy_[i] != o) continue;
            for (size_t j = i; j + 1 < liczba_odbiorcow_; ++j) odbiorcy_[j] = odbiorcy_[j + 1];
            odbiorcy_[--liczba_odbiorcow_] = nullptr; return;
        }
    }

private:
    void odswiez_z_polecenia(PolecenieUI&) override {}
    void polecenie_zniszczone(PolecenieUI& p) override {
        for (size_t i = 0; i < liczba_; ++i)
            if (polecenia_[i] == &p) { usun_indeks(i); return; }
    }
    void usun_indeks(size_t i) {
        for (size_t j = i; j + 1 < liczba_; ++j) polecenia_[j] = polecenia_[j + 1];
        polecenia_[--liczba_] = nullptr;
    }
    PolecenieUI* polecenia_[MAKS_POLECEN]{};
    size_t liczba_{0};
    OdbiorcaListyPolecen* odbiorcy_[MAKS_ODBIORCOW]{};
    size_t liczba_odbiorcow_{0};
};

} // namespace BursztynUI
